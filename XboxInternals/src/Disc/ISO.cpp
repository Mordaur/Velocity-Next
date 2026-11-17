#include "XboxInternals/Disc/ISO.h"
#include "XboxInternals/IO/FileIO.h"
#include "XboxInternals/IO/IsoIO.h"
#include "XboxInternals/Stfs/StfsPackage.h"
#include <fstream>
#include <vector>
#include <system_error>
#include <filesystem>
#include <cstring>
#include <algorithm>
#include <functional>
#include <iostream>
#include <chrono>

namespace fs = std::filesystem;

#define ISO_SECTOR_SIZE     0x800
#define ISO_XGD1_ADDRESS    0x10000
#define ISO_XGD2_ADDRESS    0xFDA0000
#define ISO_XGD3_ADDRESS    0x2090000

namespace XboxInternals::Iso {

namespace {

constexpr char kGdfMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr size_t kGdfMagicLen = 20;
constexpr UINT64 kDirRecordHeader = 14;
constexpr UINT64 kDirRecordLimit = ISO_SECTOR_SIZE - kDirRecordHeader; // 0x7F2

bool IsoNameEquals(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); ++i)
    {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca >= 'A' && ca <= 'Z')
            ca = static_cast<unsigned char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = static_cast<unsigned char>(cb - 'A' + 'a');
        if (ca != cb)
            return false;
    }

    return true;
}

// Reject absolute paths, drive/ADS separators and any "." / ".." component.
bool SafeRelativePath(const std::string &relative, fs::path &out)
{
    std::string normalized = relative;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized.empty())
        return false;

    fs::path candidate(normalized);
    if (candidate.is_absolute())
        return false;

    for (const auto &part : candidate)
    {
        const std::string name = part.string();
        if (name.empty() || name == "." || name == "..")
            return false;
        if (name.find(':') != std::string::npos ||
            name.find('\0') != std::string::npos)
            return false;
    }

    out = candidate;
    return true;
}

} // namespace

struct IsoImage::Impl {
    std::string path;
    std::unique_ptr<BaseIO> io;
};

IsoImage::IsoImage() : impl_(new Impl()), didReadFileListing(false), gdfxHeader{},
                       gdfxHeaderAddress(0), titleName(L""), xgdVersion("") {}

IsoImage::~IsoImage() {
    close();
}

bool IsoImage::ValidGDFXHeader(UINT64 address) {
    const UINT64 length = impl_->io->Length();
    if (address > length || length - address < ISO_SECTOR_SIZE)
        return false;

    std::vector<BYTE> sector(ISO_SECTOR_SIZE);
    try {
        impl_->io->SetPosition(address);
        impl_->io->ReadBytes(sector.data(), ISO_SECTOR_SIZE);
    }
    catch (...) {
        return false;
    }

    // The kernel requires the signature at both ends of sector 32.
    return memcmp(sector.data(), kGdfMagic, kGdfMagicLen) == 0 &&
           memcmp(sector.data() + 0x7EC, kGdfMagic, kGdfMagicLen) == 0;
}

void IsoImage::ParseISO() {
    struct Candidate { UINT64 address; IsoType type; const char *version; };

    // Probe the most specific layout first; an XGD3 video area can contain the
    // XGD2 offset's bytes, which used to make the first-match order misclassify.
    static const Candidate candidates[] = {
        { ISO_XGD3_ADDRESS, IsoType::XGD3, "XGD3" },
        { ISO_XGD2_ADDRESS, IsoType::XGD2, "XGD2" },
        { ISO_XGD1_ADDRESS, IsoType::GDF,  "XGD1" },
    };

    gdfxHeaderAddress = 0;
    for (const Candidate &candidate : candidates) {
        if (ValidGDFXHeader(candidate.address)) {
            gdfxHeaderAddress = candidate.address;
            info_.type = candidate.type;
            xgdVersion = candidate.version;
            break;
        }
    }

    if (gdfxHeaderAddress == 0)
        throw std::string("ISO: Invalid Xbox 360 ISO.");

    info_.imageOffset = gdfxHeaderAddress;

    // Parse the GDFX header using unified functions
    impl_->io->SetPosition(gdfxHeaderAddress);
    GdfxReadHeader(impl_->io.get(), &gdfxHeader);

    if (!ValidateExtent(gdfxHeader.rootSector, gdfxHeader.rootSize))
        throw std::string("ISO: Invalid root directory extent.");

    info_.rootDirSector = gdfxHeader.rootSector;
    info_.rootDirSize = gdfxHeader.rootSize;
}

void IsoImage::ReadFileListing(std::vector<GdfxFileEntry> *entryList, DWORD sector,
                               UINT64 size, std::string path, int depth,
                               std::unordered_set<uint64_t> &visited) {
    auto finish = [entryList]() {
        std::sort(entryList->begin(), entryList->end(), DirectoryFirstCompareGdfxEntries);
    };

    if (size == 0) {
        finish();
        return;
    }

    // We materialise the whole tree; the kernel enumerates one directory per
    // call. Bound the depth and reject a directory that is already being read so
    // a cyclic/cross-linked entry cannot exhaust the stack.
    if (depth >= 32) {
        finish();
        return;
    }

    const uint64_t key = (static_cast<uint64_t>(sector) << 32) ^ size;
    if (!visited.insert(key).second) {
        finish();
        return;
    }

    UINT64 offset = 0;
    const UINT64 length = impl_->io->Length();

    while (offset < size) {
        if (size - offset < kDirRecordHeader)
            break;

        const DWORD sectorIndex = sector + static_cast<DWORD>(offset / ISO_SECTOR_SIZE);
        const DWORD inSector = static_cast<DWORD>(offset % ISO_SECTOR_SIZE);

        if (inSector > kDirRecordLimit)
            break;

        const UINT64 address = SectorToAddress(sectorIndex) + inSector;
        if (address > length || length - address < kDirRecordHeader)
            break;

        impl_->io->SetPosition(address);

        const DWORD marker = impl_->io->ReadDword();
        if (marker == 0xFFFFFFFF) {
            // End marker: skip the remainder of this sector and continue with
            // the next one (the kernel does not treat it as end-of-directory).
            offset = (offset / ISO_SECTOR_SIZE + 1) * ISO_SECTOR_SIZE;
            continue;
        }

        GdfxFileEntry current{};
        current.unknown = marker;
        current.sector = impl_->io->ReadDword();
        current.size = impl_->io->ReadDword();
        current.attributes = impl_->io->ReadByte();
        current.nameLen = impl_->io->ReadByte();

        // The 14-byte header and the name must stay inside one 2048-byte sector.
        if (inSector + current.nameLen > kDirRecordLimit ||
            address > length ||
            length - address < kDirRecordHeader + current.nameLen)
            break;

        if (current.nameLen != 0) {
            current.name.resize(current.nameLen);
            impl_->io->ReadBytes(reinterpret_cast<BYTE *>(&current.name[0]), current.nameLen);
        }

        const UINT64 stride =
                (static_cast<UINT64>(current.nameLen) + 0x11) & ~static_cast<UINT64>(3);
        if (stride == 0)
            break;

        if ((current.attributes & GdfxDirectory) && current.size != 0 &&
            ValidateExtent(current.sector, current.size)) {
            ReadFileListing(&current.files, current.sector, current.size,
                            path + current.name + "/", depth + 1, visited);
        }

        current.filePath = path;
        current.magic = 0;
        if (!(current.attributes & GdfxDirectory) && current.size >= 4 &&
            ValidateExtent(current.sector, current.size))
            current.magic = GetFileMagic(&current);

        entryList->push_back(std::move(current));
        offset += stride;
    }

    finish();
}

void IsoImage::GetFileListing() {
    if (!didReadFileListing) {
        didReadFileListing = true;
        std::unordered_set<uint64_t> visited;
        ReadFileListing(&root, gdfxHeader.rootSector, gdfxHeader.rootSize, "", 0, visited);
    }
}

UINT64 IsoImage::SectorToAddress(DWORD sector) {
    const UINT64 base = gdfxHeaderAddress >= ISO_XGD1_ADDRESS
            ? gdfxHeaderAddress - ISO_XGD1_ADDRESS : 0;
    return base + static_cast<UINT64>(sector) * ISO_SECTOR_SIZE;
}

bool IsoImage::ValidateExtent(DWORD sector, UINT64 size) const {
    if (!impl_->io)
        return false;

    const UINT64 base = gdfxHeaderAddress >= ISO_XGD1_ADDRESS
            ? gdfxHeaderAddress - ISO_XGD1_ADDRESS : 0;
    const UINT64 start = base + static_cast<UINT64>(sector) * ISO_SECTOR_SIZE;

    if (start < base)
        return false;
    if (size == 0)
        return true;

    const UINT64 length = impl_->io->Length();
    return start < length && size <= length - start;
}

GdfxFileEntry* IsoImage::GetFileEntry(std::string filePath) {
    std::vector<GdfxFileEntry> *curDirectory = &root;
    
    // Normalize path
    std::replace(filePath.begin(), filePath.end(), '/', '\\');
    
    // Remove trailing backslash if present
    if (!filePath.empty() && filePath.back() == '\\')
        filePath.pop_back();
    
    size_t separatorIndex = filePath.find('\\');
    
    // Iterate over all directories in the path
    GdfxFileEntry *foundFileEntry = nullptr;
    while (true) {
        std::string entryName = filePath.substr(0, separatorIndex);
        
        // Find the entry in the current directory
        for (size_t i = 0; i < curDirectory->size(); i++) {
            GdfxFileEntry *curEntry = &(curDirectory->at(i));
            if (IsoNameEquals(curEntry->name, entryName)) {
                foundFileEntry = curEntry;
                
                // Check if we're at the end
                if (separatorIndex == std::string::npos)
                    return foundFileEntry;
                    
                // Move to next directory level
                curDirectory = &curEntry->files;
                filePath = filePath.substr(separatorIndex + 1);
                separatorIndex = filePath.find('\\');
                break;
            }
        }
        
        if (foundFileEntry == nullptr)
            throw std::string("ISO: File not found.");
    }
}

IsoIO* IsoImage::GetIO(std::string filePath) {
    return new IsoIO(impl_->io.get(), filePath, this);
}

IsoIO* IsoImage::GetIO(GdfxFileEntry *entry) {
    if (!entry || !ValidateExtent(entry->sector, entry->size))
        throw std::string("ISO: File extent is outside the image.");

    return new IsoIO(impl_->io.get(), entry, this);
}

std::string IsoImage::GetXGDVersion() {
    return xgdVersion;
}

UINT64 IsoImage::GetTotalSectors() {
    if (!impl_->io)
        return 0;

    const UINT64 base = gdfxHeaderAddress >= ISO_XGD1_ADDRESS
            ? gdfxHeaderAddress - ISO_XGD1_ADDRESS : 0;
    const UINT64 length = impl_->io->Length();
    if (length <= base)
        return 0;

    return (length - base) / ISO_SECTOR_SIZE;
}

DWORD IsoImage::GetFileMagic(GdfxFileEntry *entry) {
    if (entry->size < 4)
        return 0;
    
    UINT64 savedPosition = impl_->io->GetPosition();
    EndianType savedEndian = impl_->io->GetEndian();
    
    UINT64 fileAddress = SectorToAddress(entry->sector);
    impl_->io->SetPosition(fileAddress);
    
    impl_->io->SetEndian(BigEndian);
    DWORD magic = impl_->io->ReadDword();
    
    impl_->io->SetEndian(savedEndian);
    impl_->io->SetPosition(savedPosition);
    
    entry->magic = magic;
    return magic;
}

std::wstring IsoImage::GetTitleName() {
    if (!titleName.empty())
        return titleName;

    for (auto &entry : root) {
        switch (entry.magic) {
        case 0x434F4E20: // CON
        case 0x4C495645: // LIVE
        case 0x50495253: // PIRS
            try {
                std::unique_ptr<IsoIO> io(GetIO(&entry));
                StfsPackage package(io.get());

                if (!package.metaData->titleName.empty()) {
                    titleName = package.metaData->titleName;
                    return titleName;
                }
            }
            catch (const std::string &) {
                // Not every STFS-like file in the root is a readable package.
            }
            catch (const std::exception &) {
            }
            break;
        default:
            break;
        }
    }

    return titleName;
}

DWORD IsoImage::GetTotalCopyIterations(const std::vector<GdfxFileEntry> *entryList) {
    DWORD totalCopyIterations = 0;
    
    for (size_t i = 0; i < entryList->size(); i++) {
        const GdfxFileEntry &entry = entryList->at(i);
        
        if (entry.attributes & GdfxDirectory) {
            totalCopyIterations += GetTotalCopyIterations(&entry.files);
        }
        else {
            DWORD sectionsPerFile = entry.size / (ISO_SECTOR_SIZE * 1000);
            if (entry.size % (ISO_SECTOR_SIZE * 1000) != 0)
                sectionsPerFile++;
            
            totalCopyIterations += sectionsPerFile;
        }
    }
    
    return totalCopyIterations;
}

bool IsoImage::open(const std::string& path) {
    close();
    impl_->path = path;
    
    try {
        impl_->io = std::make_unique<FileIO>(path, false);
        
        info_.imageSize = impl_->io->Length();
        info_.sectorSize = ISO_SECTOR_SIZE;
        
        ParseISO();
        
        return true;
    }
    catch (const std::string& ex) {
        close();
        return false;
    }
    catch (const std::exception& ex) {
        close();
        return false;
    }
    catch (...) {
        close();
        return false;
    }
}

void IsoImage::close() {
    impl_->io.reset();
    impl_->path.clear();
    root.clear();
    didReadFileListing = false;
    gdfxHeaderAddress = 0;
    info_ = IsoInfo();
    titleName = L"";
    xgdVersion = "";
}

std::vector<IsoEntry> IsoImage::listEntries() const {
    const_cast<IsoImage*>(this)->GetFileListing();
    
    std::vector<IsoEntry> legacyEntries;
    
    std::function<void(const std::vector<GdfxFileEntry>&, const std::string&)> convertEntries;
    convertEntries = [&](const std::vector<GdfxFileEntry>& gdfxList, const std::string& parentPath) {
        for (const auto& gdfx : gdfxList) {
            IsoEntry legacy{};
            legacy.name = gdfx.name;
            legacy.path = parentPath.empty() ? gdfx.name : parentPath + "/" + gdfx.name;
            legacy.type = (gdfx.attributes & GdfxDirectory) ? IsoEntryType::Directory : IsoEntryType::File;
            legacy.size = gdfx.size;
            legacy.startSector = gdfx.sector;
            legacy.offset = const_cast<IsoImage*>(this)->SectorToAddress(gdfx.sector);
            
            legacyEntries.push_back(legacy);
            
            if (!gdfx.files.empty()) {
                convertEntries(gdfx.files, legacy.path);
            }
        }
    };
    
    convertEntries(const_cast<IsoImage*>(this)->root, "");
    return legacyEntries;
}

bool IsoImage::extractFile(const GdfxFileEntry& entry, const std::string& outputDir,
                           void(*progress)(void*, uint32_t, uint32_t), void *arg) {
    if (!impl_->io) return false;
    if (entry.attributes & GdfxDirectory) return false;
    
    std::string fullPathStr = entry.filePath;
    if (!fullPathStr.empty() && !entry.name.empty()) {
        fullPathStr += "\\";
    }
    fullPathStr += entry.name;
    
    std::replace(fullPathStr.begin(), fullPathStr.end(), '\\', '/');

    fs::path relativePath;
    if (!SafeRelativePath(fullPathStr, relativePath))
        return false;

    fs::path fullPath = fs::path(outputDir) / relativePath;
    fs::path dir = fullPath.parent_path();
    
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return false;

    std::ofstream of(fullPath, std::ios::binary);
    if (!of) return false;

    if (entry.size == 0) {
        of.close();
        if (progress) {
            progress(arg, 1, 1);
        }
        return true;
    }

    IsoIO *io = GetIO(const_cast<GdfxFileEntry*>(&entry));
    
    uint64_t remaining = entry.size;
    uint64_t totalRead = 0;
    constexpr size_t bufferSize = 65536;
    std::vector<BYTE> buffer(bufferSize);

    try {
        while (remaining > 0) {
            size_t toRead = static_cast<size_t>(std::min<uint64_t>(remaining, bufferSize));
            io->ReadBytes(buffer.data(), toRead);
            
            of.write(reinterpret_cast<char*>(buffer.data()), toRead);
            remaining -= toRead;
            totalRead += toRead;
            
            if (progress && entry.size > 0) {
                progress(arg, static_cast<uint32_t>(totalRead), 
                             static_cast<uint32_t>(entry.size));
            }
        }
    }
    catch (...) {
        delete io;
        of.close();
        std::error_code removeError;
        fs::remove(fullPath, removeError);
        return false;
    }
    
    delete io;
    return remaining == 0;
}

bool IsoImage::extractFile(const IsoEntry& entry, const std::string& outputDir,
                           void(*progress)(void*, uint32_t, uint32_t), void *arg) {
    GdfxFileEntry gdfxEntry;
    gdfxEntry.name = entry.name;
    gdfxEntry.filePath = entry.path;
    size_t lastSlash = gdfxEntry.filePath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        gdfxEntry.filePath = gdfxEntry.filePath.substr(0, lastSlash);
    } else {
        gdfxEntry.filePath = "";
    }
    gdfxEntry.sector = entry.startSector;
    gdfxEntry.size = entry.size;
    gdfxEntry.attributes = (entry.type == IsoEntryType::Directory) ? GdfxDirectory : GdfxNormal;
    
    return extractFile(gdfxEntry, outputDir, progress, arg);
}

bool IsoImage::extractAll(const std::string& outputDir,
                          void(*progress)(void*, uint32_t, uint32_t), void *arg) {
    if (!impl_->io) return false;
    
    GetFileListing();
    
    uint32_t totalFiles = 0;
    std::function<void(const std::vector<GdfxFileEntry>&)> countFiles;
    countFiles = [&](const std::vector<GdfxFileEntry>& entries) {
        for (const auto& entry : entries) {
            if (!(entry.attributes & GdfxDirectory)) {
                totalFiles++;
            }
            if (!entry.files.empty()) {
                countFiles(entry.files);
            }
        }
    };
    countFiles(root);
    
    uint32_t completedFiles = 0;
    
    std::function<bool(const std::vector<GdfxFileEntry>&)> extractEntries;
    extractEntries = [&](const std::vector<GdfxFileEntry>& entries) -> bool {
        for (const auto& entry : entries) {
            if (!(entry.attributes & GdfxDirectory)) {
                if (extractFile(entry, outputDir, nullptr, nullptr)) {
                    completedFiles++;
                    if (progress) {
                        progress(arg, completedFiles, totalFiles);
                    }
                }
            }
            
            if (!entry.files.empty()) {
                if (!extractEntries(entry.files)) {
                    return false;
                }
            }
        }
        return true;
    };
    
    return extractEntries(root);
}

void IsoImage::ExtractAllHelper(std::string outDirectory, std::vector<GdfxFileEntry> *entryList,
                                void(*progress)(void*, uint32_t, uint32_t), void *arg,
                                DWORD *curProgress, DWORD totalProgress) {
    for (size_t i = 0; i < entryList->size(); i++) {
        GdfxFileEntry &entry = entryList->at(i);
        
        if (entry.attributes & GdfxDirectory) {
            std::string dirPath = outDirectory + "/" + entry.filePath + entry.name;
            fs::create_directories(dirPath);
            ExtractAllHelper(outDirectory, &entry.files, progress, arg, curProgress, totalProgress);
        }
        else {
            std::string filePath = outDirectory + "/" + entry.filePath;
            ExtractFileHelper(filePath, &entry, progress, arg, curProgress, totalProgress);
        }
    }
}

void IsoImage::ExtractFileHelper(std::string outDirectory, const GdfxFileEntry *toExtract,
                                 void(*progress)(void*, uint32_t, uint32_t), void *arg,
                                 DWORD *curProgress, DWORD totalProgress) {
    constexpr DWORD ISO_COPY_BUFFER_SIZE = ISO_SECTOR_SIZE * 1000;
    std::vector<BYTE> copyBuffer(ISO_COPY_BUFFER_SIZE);
    
    fs::create_directories(outDirectory);
    
    std::string outFilePath = outDirectory + "/" + toExtract->name;
    FileIO extractedFile(outFilePath, true);
    
    DWORD totalReads = toExtract->size / ISO_COPY_BUFFER_SIZE;
    if (toExtract->size % ISO_COPY_BUFFER_SIZE != 0)
        totalReads++;
    
    UINT64 readAddress = SectorToAddress(toExtract->sector);
    impl_->io->SetPosition(readAddress);
    
    for (DWORD x = 0; x < totalReads; x++) {
        DWORD numBytesToCopy = ISO_COPY_BUFFER_SIZE;
        if (x == totalReads - 1 && toExtract->size % ISO_COPY_BUFFER_SIZE != 0)
            numBytesToCopy = toExtract->size % ISO_COPY_BUFFER_SIZE;
        
        impl_->io->ReadBytes(copyBuffer.data(), numBytesToCopy);
        extractedFile.WriteBytes(copyBuffer.data(), numBytesToCopy);
        
        if (curProgress)
            (*curProgress)++;
        
        if (progress)
            progress(arg, curProgress ? *curProgress : 0, totalProgress);
    }
    
    extractedFile.Close();
}

bool IsoImage::createFromDirectory(const std::string& inputDir, const std::string& outputPath) {
    (void)inputDir;
    (void)outputPath;
    return false;
}

bool IsoImage::createFromIso(const std::string& inputIso, const std::string& outputPath, bool scrub) {
    (void)inputIso;
    (void)outputPath;
    (void)scrub;
    return false;
}

} // namespace XboxInternals::Iso
