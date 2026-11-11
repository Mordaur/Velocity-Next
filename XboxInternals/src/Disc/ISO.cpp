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

namespace XboxInternals::Iso {

#define ISO_SECTOR_SIZE     0x800
#define ISO_XGD1_ADDRESS    0x10000
#define ISO_XGD2_ADDRESS    0xFDA0000
#define ISO_XGD3_ADDRESS    0x2090000

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
    BYTE gdfx_magic_buffer[20];
    
    impl_->io->SetPosition(address);
    impl_->io->ReadBytes(gdfx_magic_buffer, 20);
    
    return memcmp(gdfx_magic_buffer, "MICROSOFT*XBOX*MEDIA", 20) == 0;
}

void IsoImage::ParseISO() {
    // Find the address of the GDFX header based on XGD version
    if (ValidGDFXHeader(ISO_XGD1_ADDRESS)) {
        gdfxHeaderAddress = ISO_XGD1_ADDRESS;
        info_.type = IsoType::GDF;
        xgdVersion = "XGD1";
    }
    else if (ValidGDFXHeader(ISO_XGD2_ADDRESS)) {
        gdfxHeaderAddress = ISO_XGD2_ADDRESS;
        info_.type = IsoType::Unknown;
        xgdVersion = "XGD2";
    }
    else if (ValidGDFXHeader(ISO_XGD3_ADDRESS)) {
        gdfxHeaderAddress = ISO_XGD3_ADDRESS;
        info_.type = IsoType::XGD3;
        xgdVersion = "XGD3";
    }
    else {
        throw std::string("ISO: Invalid Xbox 360 ISO.");
    }
    
    info_.imageOffset = gdfxHeaderAddress;
    
    // Parse the GDFX header using unified functions
    impl_->io->SetPosition(gdfxHeaderAddress);
    GdfxReadHeader(impl_->io.get(), &gdfxHeader);
    
    info_.rootDirSector = gdfxHeader.rootSector;
    info_.rootDirSize = gdfxHeader.rootSize;
}

void IsoImage::ReadFileListing(std::vector<GdfxFileEntry> *entryList, DWORD sector, int size, std::string path) {
    // Seek to the start of the directory listing
    UINT64 entryAddress = SectorToAddress(sector);
    impl_->io->SetPosition(entryAddress);
    
    GdfxFileEntry current;
    DWORD bytesLeft = size;
    
    while (bytesLeft != 0) {
        // Save position before reading
        UINT64 currentAddress = impl_->io->GetPosition();
        
        // Use unified GDFX reading function
        if (!GdfxReadFileEntry(impl_->io.get(), &current))
            break;
        
        // If it's a non-empty directory, recursively read its contents
        if (current.attributes & GdfxDirectory && current.size != 0) {
            // Preserve the current position
            UINT64 seekAddr = impl_->io->GetPosition();
            
            ReadFileListing(&current.files, current.sector, current.size, 
                           path + current.name + "/");
            
            // Reset position to current listing
            impl_->io->SetPosition(seekAddr);
        }
        
        current.filePath = path;
        
        // Get the file magic for non-directory entries
        if (!(current.attributes & GdfxDirectory)) {
            current.magic = GetFileMagic(&current);
        } else {
            current.magic = 0;
        }
        
        entryList->push_back(current);
        
        // Seek to the next entry (aligned to 4-byte boundary)
        entryAddress += (current.nameLen + 0x11) & 0xFFFFFFFC;
        impl_->io->SetPosition(entryAddress);
        
        // Check for end marker
        DWORD nextBytes = impl_->io->ReadDword();
        if (nextBytes == 0xFFFFFFFF) {
            if ((size - ISO_SECTOR_SIZE) <= 0) {
                // Sort the file entries so that directories are first
                std::sort(entryList->begin(), entryList->end(), DirectoryFirstCompareGdfxEntries);
                return;
            }
            else {
                size -= ISO_SECTOR_SIZE;
                entryAddress = SectorToAddress(++sector);
                impl_->io->SetPosition(entryAddress);
            }
        }
        
        // Calculate the amount of bytes left in the file listing table to process
        bytesLeft -= entryAddress - currentAddress;
        
        // Back up to the entry
        impl_->io->SetPosition(entryAddress);
        
        // Reset the directory
        current.files.clear();
    }
    
    std::sort(entryList->begin(), entryList->end(), DirectoryFirstCompareGdfxEntries);
}

void IsoImage::GetFileListing() {
    if (!didReadFileListing) {
        didReadFileListing = true;
        ReadFileListing(&root, gdfxHeader.rootSector, gdfxHeader.rootSize, "");
    }
}

UINT64 IsoImage::SectorToAddress(DWORD sector) {
    return (UINT64)sector * ISO_SECTOR_SIZE + gdfxHeaderAddress - ISO_XGD1_ADDRESS;
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
            if (curEntry->name == entryName) {
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
    return new IsoIO(impl_->io.get(), entry, this);
}

std::string IsoImage::GetXGDVersion() {
    return xgdVersion;
}

UINT64 IsoImage::GetTotalSectors() {
    return (impl_->io->Length() - gdfxHeaderAddress) / ISO_SECTOR_SIZE;
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
    
    fs::path fullPath = fs::path(outputDir) / fullPathStr;
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
