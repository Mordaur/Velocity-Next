#include <XboxInternals/Disc/Svod.h>

#include <algorithm>
#include <cstring>
#include <utility>

#include <XboxInternals/Utils.h>
#include <XboxInternals/Fatx/FatxDrive.h>
#include <filesystem>
#include <XboxInternals/IO/LocalIndexableMultiFileIO.h>
#include <XboxInternals/IO/FatxIndexableMultiFileIO.h>
#include <XboxInternals/IO/FatxIO.h>
#include <XboxInternals/IO/FileIO.h>
#include <XboxInternals/Disc/SvodLayout.h>

#include <cstdio>
#include <unordered_set>

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

    const bool enhanced = metaData->svodVolumeDescriptor.flags != 0;
    baseAddress = enhanced ? 0x2000 : 0x12000;
    offset = enhanced ? 0x2000 : 0x1000;

    if (metaData->fileSystem != FileSystemSVOD)
        throw string("SVOD: Invalid file system header.\n");

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

void SVOD::SectorToAddress(DWORD sector, DWORD *addressInDataFile, DWORD *dataFileIndex)
{
    if (!XboxInternals::SvodLayout::Map(sector,
            metaData->svodVolumeDescriptor.dataBlockOffset,
            metaData->svodVolumeDescriptor.flags != 0,
            addressInDataFile, dataFileIndex))
        throw string("SVOD: Sector outside the data volume.\n");
}

void SVOD::ValidateDataFiles()
{
    const DWORD expected = metaData->dataFileCount;
    if (expected == 0 || expected > 0x38)
        throw string("SVOD: Invalid data file count.\n");

    const auto &paths = io->FilePaths();
    for (DWORD i = 0; i < expected; ++i)
    {
        char name[16];
        std::snprintf(name, sizeof(name), "Data%04u", i);

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

void SVOD::ReadFileListing(vector<GdfxFileEntry> *entryList, DWORD sector, UINT64 size,
                          string path, int depth, std::unordered_set<uint64_t> &visited)
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

    const uint64_t key = (static_cast<uint64_t>(sector) << 32) ^ size;
    if (!visited.insert(key).second)
    {
        finish();
        return;
    }

    const bool enhanced = metaData->svodVolumeDescriptor.flags != 0;
    const DWORD dataBlockOffset = metaData->svodVolumeDescriptor.dataBlockOffset;

    io->SetEndian(LittleEndian);
    UINT64 offset = 0;
    while (offset < size)
    {
        if (size - offset < 14)
            break;

        const DWORD sectorIndex = sector + static_cast<DWORD>(offset / 0x800);
        const DWORD inSector = static_cast<DWORD>(offset % 0x800);

        DWORD addressInFile = 0, fileIndex = 0;
        if (!XboxInternals::SvodLayout::Map(sectorIndex, dataBlockOffset, enhanced,
                                            &addressInFile, &fileIndex))
            break;

        if (static_cast<UINT64>(addressInFile) + inSector + 14 >
            XboxInternals::SvodLayout::kFullPartBytes)
            break;

        io->SetPosition(addressInFile + inSector, static_cast<int>(fileIndex));

        GdfxFileEntry current{};
        io->GetPosition(&current.address, &current.fileIndex);

        // Match the kernel's GDF directory rule: the first dword is checked
        // before any bounds test, and an all-ones dword is the sector end
        // marker, so a multi-sector directory continues in the next sector.
        DWORD marker = io->ReadDword();
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
            offset += (14 + 3) & ~static_cast<UINT64>(3);
            continue;
        }

        current.name.resize(current.nameLen);
        io->ReadBytes(reinterpret_cast<BYTE *>(&current.name[0]), current.nameLen);

        const UINT64 stride =
                (static_cast<UINT64>(current.nameLen) + 0x11) & ~static_cast<UINT64>(3);
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

void SVOD::Rehash(void (*progress)(DWORD, DWORD, void*), void *arg)
{
    if (metaData->magic != CON)
        throw string("SVOD: Rehash is only supported for console-signed packages; the LIVE package signature cannot be recomputed.\n");

    DWORD fileCount = io->FileCount();
    BYTE master[0x1000] = {0};
    BYTE level0[0x1000] = {0};
    BYTE currentBlock[0x1000];
    BYTE prevHash[0x14] = {0};

    for (DWORD i = fileCount; i--;)
    {
        io->SetPosition(static_cast<DWORD>(0x2000), static_cast<int>(i));
        DWORD hashTableCount = ((io->CurrentFileLength() - 0x2000) + 0xCCFFF) / 0xCD000;
        if (hashTableCount > 203)
            throw string("SVOD: Hash table count exceeds the master block.\n");
        DWORD totalBlockCount = (io->CurrentFileLength() - 0x1000 - (hashTableCount * 0x1000)) >> 0xC;

        for (DWORD x = 0; x < hashTableCount; x++)
        {
            io->SetPosition(static_cast<DWORD>(0x2000 + x * 0xCD000), static_cast<int>(i));

            DWORD blockCount = (totalBlockCount >= 0xCC) ? 0xCC : totalBlockCount % 0xCC;
            totalBlockCount -= 0xCC;

            for (DWORD y = 0; y < blockCount; y++)
            {
                io->ReadBytes(currentBlock, 0x1000);
                HashBlock(currentBlock, level0 + y * 0x14);
            }

            io->SetPosition(static_cast<DWORD>(0x1000 + x * 0xCD000), static_cast<int>(i));
            io->WriteBytes(level0, 0x1000);

            HashBlock(level0, master + x * 0x14);
            memset(level0, 0, 0x1000);
        }

        // Slot 203 (offset 4060) carries the next part's master hash.
        memcpy(master + 4060, prevHash, 0x14);

        io->SetPosition(static_cast<DWORD>(0), static_cast<int>(i));
        io->WriteBytes(master, 0x1000);

        HashBlock(master, prevHash);
        memset(master, 0, 0x1000);

        if (progress)
            progress(fileCount - i, fileCount, arg);
    }

    memcpy(metaData->svodVolumeDescriptor.rootHash, prevHash, 0x14);
    metaData->WriteVolumeDescriptor();

    DWORD dataLen = ((metaData->headerSize + 0xFFF) & 0xFFFFF000) - 0x344;
    BYTE *buff = new BYTE[dataLen];

    rootFile->SetPosition(0x344);
    rootFile->ReadBytes(buff, dataLen);

    const auto sha1 = Botan::HashFunction::create_or_throw("SHA-1");
    sha1->update(buff, dataLen);
    sha1->final(metaData->headerHash);

    delete [] buff;

    metaData->WriteMetaData();
}

bool SVOD::VerifyRoot()
{
    BYTE master[0x1000];
    BYTE digest[0x14];

    io->SetPosition(static_cast<DWORD>(0), 0);
    io->ReadBytes(master, 0x1000);
    HashBlock(master, digest);

    return memcmp(digest, metaData->svodVolumeDescriptor.rootHash, 0x14) == 0;
}

bool SVOD::Verify()
{
    const DWORD totalDataBlocks = metaData->svodVolumeDescriptor.dataBlockCount;
    const DWORD partCount = metaData->dataFileCount;
    constexpr DWORD blocksPerPart = 41412;
    constexpr DWORD blocksPerGroup = 204;

    BYTE master[0x1000];
    BYTE level0[0x1000];
    BYTE data[0x1000];
    BYTE digest[0x14];
    BYTE expectedMaster[0x14] = {0};

    for (DWORD part = 0; part < partCount; ++part)
    {
        io->SetPosition(static_cast<DWORD>(0), static_cast<int>(part));
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
        memcpy(expectedMaster, master + 4060, 0x14);

        const DWORD firstBlock = part * blocksPerPart;
        if (firstBlock >= totalDataBlocks)
            break;

        const DWORD partBlocks = std::min(blocksPerPart, totalDataBlocks - firstBlock);
        const DWORD groups = (partBlocks + blocksPerGroup - 1) / blocksPerGroup;

        for (DWORD group = 0; group < groups; ++group)
        {
            io->SetPosition((1 + group * 205) * 0x1000, static_cast<int>(part));
            io->ReadBytes(level0, 0x1000);
            HashBlock(level0, digest);

            if (memcmp(digest, master + group * 0x14, 0x14) != 0)
                return false;

            const DWORD blocks = std::min(blocksPerGroup, partBlocks - group * blocksPerGroup);
            for (DWORD k = 0; k < blocks; ++k)
            {
                const DWORD block = group * blocksPerGroup + k;
                const DWORD address = (block + block / blocksPerGroup + 2) * 0x1000;

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

void SVOD::HashBlock(BYTE *block, BYTE *outHash)
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

DWORD SVOD::GetSectorCount()
{
    return metaData->svodVolumeDescriptor.dataBlockCount * 2;
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
        std::unordered_set<uint64_t> visited;
        ReadFileListing(&root, header.rootSector, header.rootSize, "/", 0, visited);
    }
}
