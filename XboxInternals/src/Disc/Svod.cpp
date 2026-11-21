#include <XboxInternals/Disc/Svod.h>

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include <XboxInternals/Utils.h>
#include <XboxInternals/Fatx/FatxDrive.h>
#include <filesystem>
#include <XboxInternals/IO/LocalIndexableMultiFileIO.h>
#include <XboxInternals/IO/FatxIndexableMultiFileIO.h>
#include <XboxInternals/IO/FatxIO.h>
#include <XboxInternals/IO/FileIO.h>
#include <XboxInternals/Disc/SvodLayout.h>

#include <cstdio>

namespace {
bool SvodNameEquals(const std::string &a, const std::string &b)
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

bool SvodIsEnhanced(const XContentHeader &header)
{
    return XboxInternals::SvodLayout::IsEnhanced(header.svodVolumeDescriptor.flags);
}

u32 SvodDataBlockOffset(const XContentHeader &header)
{
    return XboxInternals::SvodLayout::DataBlockOffset(
            header.svodVolumeDescriptor.dataBlockOffset, SvodIsEnhanced(header));
}
} // namespace

std::vector<std::string> SVOD::GetDataFilePaths(const std::string &rootDescriptorPath)
{
    SVOD svod(rootDescriptorPath);

    std::string dataFileDirectoryPath = rootDescriptorPath + ".data/";
    // use std::filesystem for cross-platform existence checks
    std::filesystem::path p(dataFileDirectoryPath);
    bool dataFileDirectoryExists = std::filesystem::exists(p) && std::filesystem::is_directory(p);

    std::vector<std::string> dataFiles;
    if (dataFileDirectoryExists)
    {
        dataFiles = Utils::FilesInDirectory(dataFileDirectoryPath);
        std::sort(dataFiles.begin(), dataFiles.end());
    }

    return dataFiles;
}

SVOD::SVOD(string rootPath, FatxDrive *drive, bool readFileListing)
    : baseAddress(0), offset(0), drive(drive), didReadFileListing(false)
{
    rootPath = Utils::NormalizeFilePath(rootPath, '\\', '/');

    string fileName = rootPath.substr(rootPath.find_last_of("/") + 1);
    contentDirectory = rootPath.substr(0, rootPath.find_last_of("/")) + "/" + fileName + ".data/";

    if (drive == nullptr)
    {
        rootFile = std::make_unique<FileIO>(rootPath);
    }
    else
    {
        std::string fatxPath = Utils::NormalizeFilePath(rootPath);
        FatxFileEntry *rootFileEntry = drive->GetFileEntry(fatxPath);
        rootFile = std::make_unique<FatxIO>(drive->GetFatxIO(rootFileEntry));
    }

    metaDataOwner = std::make_unique<XContentHeader>(rootFile.get());
    metaData = metaDataOwner.get();

    const bool enhanced = SvodIsEnhanced(*metaData);
    const u32 dataBlockOffset = SvodDataBlockOffset(*metaData);

    // SvodRead reads volume block 16 after the dataBlockOffset/enhanced shift,
    // except for enhanced packages whose prefix covers it: those alias it onto
    // data block 0.
    u32 headerBlock = 0;
    if (!XboxInternals::SvodLayout::GdfHeaderBlock(dataBlockOffset, enhanced, &headerBlock))
        throw string("SVOD: The GDF volume descriptor is inside the skipped prefix.\n");

    baseAddress = static_cast<u32>(XboxInternals::SvodLayout::DataBlockAddress(headerBlock));
    offset = static_cast<u32>(XboxInternals::SvodLayout::DataBlockAddress(0));

    if (metaData->fileSystem != FileSystemSVOD)
        throw string("SVOD: Invalid file system header.\n");

    // A zero data-block count would silently disable the volume bound in
    // SvodLayout::Map, so a corrupt/truncated descriptor must fail here.
    if (metaData->svodVolumeDescriptor.dataBlockCount == 0)
        throw string("SVOD: The data volume is empty.\n");

    switch (metaData->contentType)
    {
        case GameOnDemand:
        case InstalledGame:
        case XboxOriginalGame:
            break;
        default:
            throw string("SVOD: Unrecognized content type.\n");
    }

    if (drive == nullptr)
    {
        io = std::make_unique<LocalIndexableMultiFileIO>(contentDirectory);
    }
    else
    {
        std::string fatxDataDirectory = Utils::NormalizeFilePath(contentDirectory);
        io = std::make_unique<FatxIndexableMultiFileIO>(fatxDataDirectory, drive);
    }

    ValidateDataFiles();

    io->SetPosition(baseAddress, 0);
    GdfxReadHeader(io.get(), &header);

    if (!VerifyRoot())
        throw string("SVOD: Root hash does not match part 0.\n");

    if (readFileListing)
        GetFileListing();
}

SVOD::~SVOD()
{
    if (io)
    {
        io->Close();
    }

    if (rootFile)
    {
        rootFile->Close();
    }
}

void SVOD::Resign(string kvPath)
{
    if (metaData->magic != CON)
        throw string("SVOD: Can only resign console systems.\n");
    metaData->ResignHeader(kvPath);
}

void SVOD::SectorToAddress(u32 sector, u32 *addressInDataFile, u32 *dataFileIndex)
{
    if (!XboxInternals::SvodLayout::Map(sector, SvodDataBlockOffset(*metaData),
            SvodIsEnhanced(*metaData), addressInDataFile, dataFileIndex,
            metaData->svodVolumeDescriptor.dataBlockCount))
        throw string("SVOD: Sector outside the data volume.\n");
}

void SVOD::ValidateDataFiles()
{
    const u32 expected = metaData->dataFileCount;
    if (expected == 0 || expected > 0x38)
        throw string("SVOD: Invalid data file count.\n");

    const auto &paths = io->FilePaths();
    for (u32 i = 0; i < expected; ++i)
    {
        char name[16];
        std::snprintf(name, sizeof(name), "Data%04u", static_cast<unsigned>(i));

        bool found = false;
        for (const auto &path : paths)
        {
            const size_t separator = path.find_last_of("/\\");
            const std::string base = (separator == std::string::npos)
                    ? path : path.substr(separator + 1);
            if (SvodNameEquals(base, name))
            {
                found = true;
                break;
            }
        }

        if (!found)
            throw string("SVOD: Missing data part ") + name + ".\n";
    }
}

void SVOD::ReadFileListing(vector<GdfxFileEntry> *entryList, u32 sector, u64 size,
                          string path, int depth,
                          std::set<std::pair<u32, u64>> &visited)
{
    auto finish = [entryList]() {
        std::sort(entryList->begin(), entryList->end(), DirectoryFirstCompareGdfxEntries);
    };

    if (size == 0)
    {
        finish();
        return;
    }

    if (depth >= 32)
    {
        finish();
        return;
    }

    if (!visited.insert({sector, size}).second)
    {
        finish();
        return;
    }

    const bool enhanced = SvodIsEnhanced(*metaData);
    const u32 dataBlockOffset = SvodDataBlockOffset(*metaData);

    io->SetEndian(LittleEndian);
    u64 offset = 0;
    while (offset < size)
    {
        if (size - offset < 14)
            break;

        const u32 sectorIndex = sector + static_cast<u32>(offset / 0x800);
        const u32 inSector = static_cast<u32>(offset % 0x800);

        u32 addressInFile = 0, fileIndex = 0;
        if (!XboxInternals::SvodLayout::Map(sectorIndex, dataBlockOffset, enhanced,
                                            &addressInFile, &fileIndex,
                                            metaData->svodVolumeDescriptor.dataBlockCount))
            break;

        if (static_cast<u64>(addressInFile) + inSector + 14 >
            XboxInternals::SvodLayout::kFullPartBytes)
            break;

        io->SetPosition(addressInFile + inSector, static_cast<int>(fileIndex));

        GdfxFileEntry current{};
        io->GetPosition(&current.address, &current.fileIndex);

        // Match the kernel's GDF directory rule: the first dword is checked
        // before any bounds test, and an all-ones dword is the sector end
        // marker, so a multi-sector directory continues in the next sector.
        u32 marker = io->ReadDword();
        if (marker == 0xFFFFFFFF)
        {
            offset = (offset / 0x800 + 1) * 0x800;
            continue;
        }

        if (inSector > 0x7F2)
            break;

        current.unknown = marker;
        current.sector = io->ReadDword();
        current.size = io->ReadDword();
        current.attributes = io->ReadByte();
        current.nameLen = io->ReadByte();

        if (inSector + current.nameLen > 0x7F2)
            break;

        if (current.nameLen == 0)
        {
            offset += (14 + 3) & ~static_cast<u64>(3);
            continue;
        }

        current.name.resize(current.nameLen);
        io->ReadBytes(reinterpret_cast<u8 *>(&current.name[0]), current.nameLen);

        const u64 stride =
                (static_cast<u64>(current.nameLen) + 0x11) & ~static_cast<u64>(3);
        if (stride == 0)
            break;

        if ((current.attributes & GdfxDirectory) && current.size != 0)
        {
            ReadFileListing(&current.files, current.sector, current.size,
                            path + current.name + "/", depth + 1, visited);
        }

        current.filePath = path;
        entryList->push_back(std::move(current));
        offset += stride;
    }

    finish();
}

GdfxFileEntry SVOD::GetFileEntry(string path, vector<GdfxFileEntry> *listing)
{
    while (!path.empty() && path.front() == '/')
        path.erase(0, 1);

    if (path.empty())
        throw string("SVOD: Empty path.\n");

    const size_t separator = path.find('/');
    const string entryName = path.substr(0, separator);

    for (auto &entry : *listing)
    {
        if (!SvodNameEquals(entry.name, entryName))
            continue;

        if (separator == string::npos)
            return entry;

        return GetFileEntry(path.substr(separator + 1), &entry.files);
    }

    throw string("SVOD: File not found.\n");
}

SvodIO SVOD::GetSvodIO(string path)
{
    return GetSvodIO(GetFileEntry(path, &root));
}

SvodIO SVOD::GetSvodIO(GdfxFileEntry entry)
{
    return SvodIO(metaData, std::move(entry), io.get());
}

void SVOD::Rehash(void (*progress)(u32, u32, void*), void *arg)
{
    if (metaData->magic != CON)
        throw string("SVOD: Rehash is only supported for console-signed packages; the LIVE package signature cannot be recomputed.\n");

    u32 fileCount = io->FileCount();
    u8 master[0x1000] = {0};
    u8 level0[0x1000] = {0};
    u8 currentBlock[0x1000];
    u8 prevHash[0x14] = {0};

    for (u32 i = fileCount; i--;)
    {
        // CurrentFileLength() reports the *current* part. Rehash walks parts in
        // reverse, so select the part before asking for its length; otherwise a
        // multi-part GOD rehash uses the wrong size and rewrites bad hashes.
        io->SetPosition(static_cast<u32>(0), static_cast<int>(i));

        const u32 fileLength = io->CurrentFileLength();
        if (fileLength < 0x2000)
            throw string("SVOD: Data part is too small for a master block.\n");

        const u64 hashBytes = static_cast<u64>(fileLength) - 0x2000;
        const u32 hashTableCount = static_cast<u32>(
                (hashBytes + XboxInternals::SvodLayout::kGroupStrideBytes - 1) /
                XboxInternals::SvodLayout::kGroupStrideBytes);
        if (hashTableCount > XboxInternals::SvodLayout::kGroupsPerPart)
            throw string("SVOD: Hash table count exceeds the master block.\n");

        const u64 tableBytes = static_cast<u64>(hashTableCount) * 0x1000;
        if (static_cast<u64>(fileLength) < 0x1000 + tableBytes)
            throw string("SVOD: Data part is shorter than its hash tables.\n");

        u32 totalBlockCount = static_cast<u32>(
                (static_cast<u64>(fileLength) - 0x1000 - tableBytes) >> 0xC);

        for (u32 x = 0; x < hashTableCount; x++)
        {
            io->SetPosition(static_cast<u32>(0x2000 +
                    static_cast<u64>(x) * XboxInternals::SvodLayout::kGroupStrideBytes),
                    static_cast<int>(i));

            const u32 blockCount = std::min<u32>(
                    static_cast<u32>(XboxInternals::SvodLayout::kDataBlocksPerGroup),
                    totalBlockCount);
            totalBlockCount -= blockCount;

            for (u32 y = 0; y < blockCount; y++)
            {
                io->ReadBytes(currentBlock, 0x1000);
                HashBlock(currentBlock, level0 + y * 0x14);
            }

            io->SetPosition(static_cast<u32>(0x1000 +
                    static_cast<u64>(x) * XboxInternals::SvodLayout::kGroupStrideBytes),
                    static_cast<int>(i));
            io->WriteBytes(level0, 0x1000);

            HashBlock(level0, master + x * 0x14);
            memset(level0, 0, 0x1000);
        }

        // Slot 203 (offset 4060) carries the next part's master hash.
        memcpy(master + static_cast<std::size_t>(XboxInternals::SvodLayout::kGroupsPerPart) * 0x14,
               prevHash, 0x14);

        io->SetPosition(static_cast<u32>(0), static_cast<int>(i));
        io->WriteBytes(master, 0x1000);

        HashBlock(master, prevHash);
        memset(master, 0, 0x1000);

        if (progress)
            progress(fileCount - i, fileCount, arg);
    }

    memcpy(metaData->svodVolumeDescriptor.rootHash, prevHash, 0x14);
    metaData->WriteVolumeDescriptor();

    // The header hash covers 0x344 up to the aligned header end. `headerSize`
    // comes straight from the file, so bound it before allocating: a small or
    // corrupt value used to underflow to ~4 GiB or read past the file.
    const u32 alignedHeaderSize = (metaData->headerSize + 0xFFF) & 0xFFFFF000;
    if (alignedHeaderSize < 0x344 || static_cast<u64>(alignedHeaderSize) > rootFile->Length())
        throw string("SVOD: Invalid header size while rehashing.\n");

    const u32 dataLen = alignedHeaderSize - 0x344;
    std::vector<u8> buff(dataLen);

    rootFile->SetPosition(0x344);
    rootFile->ReadBytes(buff.data(), dataLen);

    const auto sha1 = Botan::HashFunction::create_or_throw("SHA-1");
    sha1->update(buff.data(), dataLen);
    sha1->final(metaData->headerHash);

    metaData->WriteMetaData();
}

bool SVOD::VerifyRoot()
{
    u8 master[0x1000];
    u8 digest[0x14];

    io->SetPosition(static_cast<u32>(0), 0);
    io->ReadBytes(master, 0x1000);
    HashBlock(master, digest);

    return memcmp(digest, metaData->svodVolumeDescriptor.rootHash, 0x14) == 0;
}

bool SVOD::Verify()
{
    // SvodCreateDevice counts dataBlockCount + 1 data blocks for enhanced
    // packages: data block 0 holds the GDF descriptor copy when the skipped
    // prefix covers volume block 16.
    const bool enhanced = SvodIsEnhanced(*metaData);
    const u32 totalDataBlocks = metaData->svodVolumeDescriptor.dataBlockCount +
            (enhanced ? 1 : 0);
    const u32 partCount = metaData->dataFileCount;
    constexpr u32 blocksPerPart =
            static_cast<u32>(XboxInternals::SvodLayout::kDataBlocksPerPart);
    constexpr u32 blocksPerGroup =
            static_cast<u32>(XboxInternals::SvodLayout::kDataBlocksPerGroup);

    u8 master[0x1000];
    u8 level0[0x1000];
    u8 data[0x1000];
    u8 digest[0x14];
    u8 expectedMaster[0x14] = {0};

    for (u32 part = 0; part < partCount; ++part)
    {
        io->SetPosition(static_cast<u32>(0), static_cast<int>(part));
        io->ReadBytes(master, 0x1000);
        HashBlock(master, digest);

        if (part == 0)
        {
            if (memcmp(digest, metaData->svodVolumeDescriptor.rootHash, 0x14) != 0)
                return false;
        }
        else if (memcmp(digest, expectedMaster, 0x14) != 0)
        {
            return false;
        }

        // Slot 203 links to the next part's master block.
        memcpy(expectedMaster,
               master + static_cast<std::size_t>(XboxInternals::SvodLayout::kGroupsPerPart) * 0x14,
               0x14);

        const u32 firstBlock = part * blocksPerPart;
        if (firstBlock >= totalDataBlocks)
            break;

        const u32 partBlocks = std::min(blocksPerPart, totalDataBlocks - firstBlock);
        const u32 groups = (partBlocks + blocksPerGroup - 1) / blocksPerGroup;

        for (u32 group = 0; group < groups; ++group)
        {
            io->SetPosition(static_cast<u32>(0x1000 +
                    static_cast<u64>(group) * XboxInternals::SvodLayout::kGroupStrideBytes),
                    static_cast<int>(part));
            io->ReadBytes(level0, 0x1000);
            HashBlock(level0, digest);

            if (memcmp(digest, master + group * 0x14, 0x14) != 0)
                return false;

            const u32 blocks = std::min(blocksPerGroup, partBlocks - group * blocksPerGroup);
            for (u32 k = 0; k < blocks; ++k)
            {
                const u32 block = group * blocksPerGroup + k;
                const u32 address = static_cast<u32>(
                        XboxInternals::SvodLayout::DataBlockAddress(block));

                io->SetPosition(address, static_cast<int>(part));
                io->ReadBytes(data, 0x1000);
                HashBlock(data, digest);

                if (memcmp(digest, level0 + k * 0x14, 0x14) != 0)
                    return false;
            }
        }
    }

    return true;
}

void SVOD::HashBlock(u8 *block, u8 *outHash)
{
    const auto sha1 = Botan::HashFunction::create_or_throw("SHA-1");
    sha1->update(block, 0x1000);
    sha1->final(outHash);
}

void SVOD::WriteFileEntry(GdfxFileEntry *entry)
{
    io->SetPosition(entry->address, static_cast<int>(entry->fileIndex));
    GdfxWriteFileEntry(io.get(), entry);
}

u32 SVOD::GetSectorCount()
{
    const u64 blocks = static_cast<u64>(metaData->svodVolumeDescriptor.dataBlockCount) +
            SvodDataBlockOffset(*metaData);
    const u64 sectors = blocks * 2;
    return sectors > UINT32_MAX ? UINT32_MAX : static_cast<u32>(sectors);
}

std::string SVOD::GetContentName()
{
    std::string headerHashBeginning = Utils::ConvertToHexString(metaData->headerHash, 16);
    std::string firstByteOfTitleID = Utils::ConvertToHexString((metaData->titleID >> 24) & 0xFF);

    return headerHashBeginning + firstByteOfTitleID;
}

void SVOD::GetFileListing()
{
    if (!didReadFileListing)
    {
        didReadFileListing = true;
        std::set<std::pair<u32, u64>> visited;
        ReadFileListing(&root, header.rootSector, header.rootSize, "/", 0, visited);
    }
}
