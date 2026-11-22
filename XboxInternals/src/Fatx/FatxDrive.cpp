/* Most parts of this class were originally developed by Lander Griffith (https://github.com/landr0id/).
   Much of his code is used throughout this class or very slightly modified */

#include <XboxInternals/Fatx/FatxDrive.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #undef DeleteFile
    #undef ReplaceFile
#else
    #include <fcntl.h>
    #include <sys/types.h>
    #include <sys/ioctl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif


FatxDrive::FatxDrive(std::string drivePath, FatxDriveType type)  : type(type)
{
    // convert it to a wstring
    std::wstring wsDrivePath;
    wsDrivePath.assign(drivePath.begin(), drivePath.end());

    // load the device
    loadFatxDrive(wsDrivePath);
}

FatxDrive::FatxDrive(std::wstring drivePath, FatxDriveType type) : type(type)
{
    loadFatxDrive(drivePath);
}

FatxDrive::FatxDrive(std::unique_ptr<BaseIO> io, FatxDriveType type) : io(std::move(io)), type(type)
{
    loadFatxDrive();
}

FatxDrive::FatxDrive(BaseIO *io, FatxDriveType type) : FatxDrive(std::unique_ptr<BaseIO>(io), type)
{
}

std::vector<Partition*> FatxDrive::GetPartitions()
{
    std::vector<Partition*> rawPartitions;
    for (const auto& p : partitions) {
        rawPartitions.push_back(p.get());
    }
    return rawPartitions;
}

std::vector<PartitionInfo> FatxDrive::GetPartitionInfo() const
{
    std::vector<PartitionInfo> infos;
    infos.reserve(partitions.size());

    for (const auto& partition : partitions)
    {
        PartitionInfo info;
        info.name = partition->name;
        info.partitionId = partition->partitionId;
        info.sectorsPerCluster = partition->sectorsPerCluster;
        info.rootDirectoryCluster = partition->rootDirectoryCluster;
        info.clusterCount = partition->clusterCount;
        info.clusterSize = partition->clusterSize;
        info.address = partition->address;
        info.size = partition->size;
        info.clusterStartingAddress = partition->clusterStartingAddress;
        info.freeMemory = partition->freeMemory;
        infos.push_back(info);
    }

    return infos;
}

FatxDriveType FatxDrive::GetFatxDriveType()
{
    return type;
}

FatxIO FatxDrive::GetFatxIO(FatxFileEntry *entry)
{
    // only if it hasn't been read yet
    if (entry->clusterChain.size() == 0)
        ReadClusterChain(entry);

    return FatxIO(io.get(), entry);
}

void FatxDrive::processBootSector(Partition *part)
{
    // seek to the partition
    io->SetPosition(part->address);

    // read the header
    part->magic = io->ReadDword();
    if (part->magic != FATX_MAGIC)
    {
        part->address = 0;
        return;
    }

    part->partitionId = io->ReadDword();
    part->sectorsPerCluster = io->ReadDword();
    part->rootDirectoryCluster = io->ReadDword();

    switch(part->sectorsPerCluster)
    {
        case 0x2:
        case 0x4:
        case 0x8:
        case 0x10:
        case 0x20:
        case 0x40:
        case 0x80:
            break;
        default:
            throw std::string("FATX: Found invalid sectors per cluster.\n");
    }

    part->clusterSize = part->sectorsPerCluster * FATX_SECTOR_SIZE;
    u64 totalClusters = (part->size / part->clusterSize) + 1;

    if ((this->type == FatxFlashDrive && part->address == UsbOffsets::Data) || totalClusters >= FAT_CLUSTER16_RESERVED)
        part->clusterEntrySize = 4;
    else
        part->clusterEntrySize = 2;


    // https://docs.google.com/presentation/d/1LJLFRMAbCm9RBPg0l241GjwWdjGWROlihVgrC7pzLwg/edit?usp=sharing
    // go to slide 11 for a good visual to make sense of this
    u64 chainmapSize = Utils::RoundToNearestHex1000((part->size / part->clusterSize + 1) * part->clusterEntrySize);
    u64 totalDataSize = (part->size - FATX_HEADER_SIZE - chainmapSize);

    part->clusterCount = totalDataSize / part->clusterSize;
    part->chainmapSize = chainmapSize;
    part->clusterStartingAddress = part->address + FATX_HEADER_SIZE + chainmapSize;
    part->lastFreeClusterFound = 1;
    part->freeMemory = 0;

    // setup the root
    part->root.startingCluster = part->rootDirectoryCluster;
    part->root.readDirectories = false;
    part->root.name = part->name;
    part->root.partition = part;
    part->root.fileAttributes = FatxDirectory;
    part->root.address = -1;
    part->root.path = "Drive:\\";
    part->drive = this;
}

void FatxDrive::ExtractSecurityBlob(string path)
{
    i64 originalPos = io->GetPosition();

    // create a file to save to
    FileIO outFile(path, true);

    // seek to the beginning of the security blob
    io->SetPosition(0x2000);

    // allocate memory for the data
    const u64 blobSize = static_cast<u64>(securityBlob.msLogoSize) + 0x204;
    if (blobSize > 0xFFFFFFFFULL)
        throw std::string("FATX: Security blob size overflow.\n");

    const u32 blobSize32 = static_cast<u32>(blobSize);
    std::vector<u8> buffer(static_cast<size_t>(blobSize));

    // read in the data
    io->ReadBytes(buffer.data(), blobSize32);
    outFile.WriteBytes(buffer.data(), blobSize32);
    outFile.Flush();

    outFile.Close();
    io->SetPosition(originalPos);
}

void FatxDrive::ReplaceSecurityBlob(std::string path)
{
    i64 originalPos = io->GetPosition();

    // open the security blob
    FileIO inFile(path);

    const u64 blobSize = static_cast<u64>(securityBlob.msLogoSize) + 0x204;
    if (blobSize > 0xFFFFFFFFULL)
        throw std::string("FATX: Security blob size overflow.\n");

    // verify the file size
    if (inFile.Length() != blobSize)
        throw std::string("FATX: Invalid security blob, size mismatch.\n");

    // seek to the beginning of the security blob
    io->SetPosition(0x2000);

    // allocate memory for the data
    const u32 blobSize32 = static_cast<u32>(blobSize);
    std::vector<u8> buffer(static_cast<size_t>(blobSize));
    inFile.SetPosition(0);
    inFile.ReadBytes(buffer.data(), blobSize32);

    io->WriteBytes(buffer.data(), blobSize32);
    io->Flush();

    inFile.Close();
    io->SetPosition(originalPos);
}

void FatxDrive::CreateFileX(FatxFileEntry *parent, std::string name)
{
    FatxFileEntry newEntry;
    newEntry.name = name;
    newEntry.fileSize = 0;

    createFileEntry(parent, &newEntry);
}

FatxFileEntry* FatxDrive::createFileEntry(FatxFileEntry *parent, FatxFileEntry *newEntry, bool errorIfAlreadyExists)
{
    if (!(parent->fileAttributes & FatxDirectory))
        throw std::string("FATX: Parent file entry is not a directory.\n");

    newEntry->path = parent->path + parent->name + "\\";

    // set the times
    u32 currentTime = MSTimeToDWORD(TimetToMSTime(time(NULL)));
    newEntry->creationDate = currentTime;
    newEntry->lastAccessDate = currentTime;
    newEntry->lastWriteDate = currentTime;

    // set the address to null
    newEntry->address = 0;
    newEntry->startingCluster = 0;

    // get the child entries
    GetChildFileEntries(parent);

    for (size_t i = 0; i < parent->cachedFiles.size(); i++)
        if (NamesEqual(parent->cachedFiles.at(i).name, newEntry->name))
        {
            // if it's deleted, it's okay!
            if (parent->cachedFiles.at(i).nameLen != FATX_ENTRY_DELETED)
            {
                if (errorIfAlreadyExists)
                {
                    std::stringstream errorText;
                    errorText << "FATX: Entry \"" << newEntry->path << newEntry->name << "\" already exists.\n";
                    throw errorText.str();
                }
                else
                    return NULL;
            }

            newEntry->address = parent->cachedFiles.at(i).address;
        }

    // set the name length
    newEntry->nameLen = newEntry->name.length();

    if (newEntry->address == 0)
    {
        u64 freeEntryAddress = parent->cachedFiles.size() * FATX_ENTRY_SIZE;
        FatxIO parentIO = GetFatxIO(parent);

        parent->fileSize = parent->cachedFiles.size() * FATX_ENTRY_SIZE;
        if (parent->clusterChain.size() < 1 || parent->fileSize >= parent->partition->clusterSize)
        {
            parentIO.AllocateMemory(FATX_ENTRY_SIZE);
            parentIO.SetPosition(parent->fileSize);
            parentIO.Write((u8)0xFF);
        }

        parentIO.SetPosition(freeEntryAddress);
        newEntry->address = parentIO.GetDrivePosition();
    }

    newEntry->partition = parent->partition;

    u32 fileSize = newEntry->fileSize;
    newEntry->fileSize = 0;

    FatxIO childIO(io.get(), newEntry);
    // A zero-byte file owns no clusters (free60: null starting cluster).
    // AllocateMemory(0) would allocate and link one while leaving the size 0.
    if (fileSize != 0 || (newEntry->fileAttributes & FatxDirectory))
        childIO.AllocateMemory(fileSize);
    childIO.WriteEntryToDisk();

    parent->cachedFiles.push_back(*newEntry);
    return &parent->cachedFiles.at(parent->cachedFiles.size() - 1);
}

FatxFileEntry* FatxDrive::CreateFolder(FatxFileEntry *parent, std::string folderName)
{
    if (this->FileExists(parent, folderName))
        throw std::string("FATX: The folder already exists.");

    FatxFileEntry newEntry;
    newEntry.fileSize = FATX_ENTRY_SIZE;
    newEntry.name = folderName;
    newEntry.fileAttributes = FatxDirectory;

    return this->createFileEntry(parent, &newEntry);
}

FatxFileEntry* FatxDrive::CreatePath(std::string folderPath)
{
    FatxFileEntry *lastEntry = GetFileEntry(folderPath);
    if (lastEntry != NULL)
        return lastEntry;

    std::vector<std::string> elems;
    std::stringstream ss(folderPath);
    std::string item;
    while (std::getline(ss, item, '\\'))
        elems.push_back(item);

    if (elems.size() < 3)
        throw std::string("FATX: Invalid folder path given.");

    std::string currentPath = elems.at(0) + "\\" + elems.at(1);

    for (size_t i = 2; i < elems.size(); i++)
    {
        if (GetFileEntry(currentPath + "\\" + elems.at(i)) == NULL)
            lastEntry = CreateFolder(GetFileEntry(currentPath), elems.at(i));
        currentPath += "\\" + elems.at(i);
    }

    return lastEntry;
}

void FatxDrive::RemoveFile(FatxFileEntry *entry, void(*progress)(void*), void *arg)
{
    // check if the file is already deleted
    if (entry->nameLen == FATX_ENTRY_DELETED)
        return;

    // read the data
    GetChildFileEntries(entry);
    ReadClusterChain(entry);

    for (size_t i = 0; i < entry->cachedFiles.size(); i++)
        RemoveFile(&entry->cachedFiles.at(i), progress, arg);

    // set all the clusters to available; drop the duplicate start cluster and
    // sort so GetConsecutive sees a canonical list.
    if (entry->startingCluster != 0)
        entry->clusterChain.push_back(entry->startingCluster);

    std::sort(entry->clusterChain.begin(), entry->clusterChain.end());
    entry->clusterChain.erase(
            std::unique(entry->clusterChain.begin(), entry->clusterChain.end()),
            entry->clusterChain.end());

    FatxIO::SetAllClusters(io.get(), entry->partition, entry->clusterChain, FAT_CLUSTER_AVAILABLE);

    // generate cluster ranges for fast insertion into the cluster chain
    std::vector<Range> clusterRanges;
    FatxIO::GetConsecutive(entry->clusterChain, clusterRanges, true);

    // inject every range back into the cluster chain
    for (u32 i = 0; i < clusterRanges.size(); i++)
    {
        clusterRanges.at(i).start = entry->clusterChain.at(clusterRanges.at(i).start);
        injectRange(entry->partition->freeClusters, clusterRanges.at(i));
    }

    // update the entry
    entry->clusterChain.clear();
    entry->nameLen = FATX_ENTRY_DELETED;

    // update the entry file name lenght to deleted
    io->SetPosition(entry->address);
    io->Write((u8)FATX_ENTRY_DELETED);
    io->Flush();

    if (progress)
        progress(arg);
}

void FatxDrive::InjectFile(FatxFileEntry *parent, std::string name, std::string filePath, void (*progress)(void *, u32, u32), void *arg)
{
    u64 fileLength = 0;

#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (!GetFileAttributesExA(filePath.c_str(), GetFileExInfoStandard, &fileInfo))
        throw std::string("FATX: Error opening the file.\n");
    fileLength = ((u64)fileInfo.nFileSizeHigh << 32) | fileInfo.nFileSizeLow;
#else
    struct stat fileInfo;
    if (stat(filePath.c_str(), &fileInfo) != 0)
        throw std::string("FATX: Error opening the file.\n");
    fileLength = fileInfo.st_size;
#endif

    if (fileLength >= 4294967296)
        throw std::string("FATX: File too large. All files in this file system must be less than 4GB.\n");

    FatxFileEntry entry;
    entry.name = name;

    // seek to the end of the file
    FileIO inFile(filePath);
    inFile.SetPosition(0, ios_base::end);

    // get the file size
    u64 fileSize = inFile.GetPosition();
    entry.fileSize = fileSize;

    // set other stuff
    entry.fileAttributes = 0;
    inFile.SetPosition(0);
    // A file shorter than the magic has none; GetFileEntryMagic() treats 0 the
    // same way and re-reads it once the file has data.
    entry.magic = (fileSize >= 4) ? inFile.ReadDword() : 0;
    inFile.Close();

    // create the entry
    createFileEntry(parent, &entry);

    FatxIO fatxIO = GetFatxIO(&entry);
    fatxIO.ReplaceFile(filePath, progress, arg);
}

void FatxDrive::GetFileEntryMagic(FatxFileEntry *entry)
{
    if (entry->fileSize < 4 || entry->magic != 0)
        return;

    io->SetPosition(FatxIO::ClusterToOffset(entry->partition, entry->startingCluster));
    entry->magic = io->ReadDword();

    // get the file system if possible
    if (entry->fileSize >= 0x3AD)
    {
        io->SetPosition(FatxIO::ClusterToOffset(entry->partition, entry->startingCluster) + 0x3AC);
        entry->fileSystem = io->ReadEnum<FileSystem>(1);
    }
}

void FatxDrive::GetChildFileEntries(FatxFileEntry *entry, void(*progress)(void*, bool), void *arg)
{
    // if all entries have been read, skip this
    if (entry->readDirectories || !(entry->fileAttributes & FatxDirectory))
        return;

    // if the cluster chain has already been read, skip this
    if (entry->clusterChain.size() == 0)
        ReadClusterChain(entry);

    // find out how many entries are in a single cluster
    u32 entriesInCluster = entry->partition->clusterSize / FATX_ENTRY_SIZE;

    bool doneForGood = false;

    // read all entries
    for (size_t i = 0; i < entry->clusterChain.size(); i++)
    {
        u64 posCur = FatxIO::ClusterToOffset(entry->partition, entry->clusterChain.at(i));

        // go to the cluster offset
        io->SetPosition(posCur);

        for (u32 x = 0; x < entriesInCluster; x++)
        {
            // read the name length
            FatxFileEntry newEntry;
            newEntry.nameLen = io->ReadByte();

            // check if there are no more entries
            if (newEntry.nameLen == 0xFF || newEntry.nameLen == 0)
            {
                doneForGood = true;
                break;
            }

            // calcualte the address
            newEntry.address = posCur + (x * 0x40);

            // read the attributes
            newEntry.fileAttributes = io->ReadByte();

            // read the name (0xFF is the null terminator)
            bool subtract = true;
            if (newEntry.nameLen == FATX_ENTRY_DELETED)
                newEntry.name = io->ReadString(-1, 0xFF, true, FATX_ENTRY_MAX_NAME_LENGTH);
            else
            {
                newEntry.name = io->ReadString(newEntry.nameLen);
                subtract = false;
            }

            // if the name is invalid, then the entry must be corrupt so we'll skip to the next entry
            if (!ValidFileName(newEntry.name))
            {
                io->SetPosition((io->GetPosition() + 0x3F) & 0xFFFFFFFFFFFFFFC0);
                continue;
            }

            // seek past the name
            io->SetPosition(io->GetPosition() + (FATX_ENTRY_MAX_NAME_LENGTH - newEntry.name.length()) - subtract);

            // read the rest of the entry information
            newEntry.startingCluster = io->ReadDword();
            if (newEntry.startingCluster == entry->startingCluster)
                throw std::string("FATX: FAT has circular link.\n");

            newEntry.fileSize = io->ReadDword();
            newEntry.creationDate = io->ReadDword();
            newEntry.lastWriteDate = io->ReadDword();
            newEntry.lastAccessDate = io->ReadDword();
            newEntry.partition = entry->partition;
            newEntry.readDirectories = false;
            newEntry.path = entry->path + entry->name + "\\";
            newEntry.magic = 0;

            // add it to the file cache
            entry->cachedFiles.push_back(newEntry);

            // update progress if needed
            if (progress)
                progress(arg, false);
        }

        if (doneForGood)
            break;
    }

    // update progress if needed
    if (progress)
        progress(arg, true);

    entry->fileSize = (entry->cachedFiles.size() * FATX_ENTRY_SIZE);
    entry->readDirectories = true;
}

bool FatxDrive::ComputeFileExtents(Partition *partition, u32 fileSize,
        const std::vector<u32> &clusterChain, std::vector<FileExtent> &extents)
{
    extents.clear();
    if (partition == nullptr)
        return false;
    if (fileSize == 0)
        return true;
    if (partition->clusterSize == 0)
        return false;

    u64 remaining = fileSize;
    size_t index = 0;

    while (index < clusterChain.size() && remaining > 0)
    {
        const u32 first = clusterChain[index];
        if (first == 0 || first > partition->clusterCount)
            return false;

        // Count the physically contiguous clusters in this run.
        size_t runClusters = 1;
        while (index + runClusters < clusterChain.size())
        {
            const u32 expected = first + static_cast<u32>(runClusters);
            if (clusterChain[index + runClusters] != expected)
                break;
            ++runClusters;
        }

        const u64 runBytes = static_cast<u64>(runClusters) * partition->clusterSize;
        const u64 length = std::min<u64>(runBytes, remaining);

        extents.push_back(FileExtent{
                fileSize - remaining,
                partition->clusterStartingAddress +
                        static_cast<u64>(partition->clusterSize) * (first - 1),
                length });

        remaining -= length;
        index += runClusters;
    }

    return remaining == 0;
}

bool FatxDrive::GetFileExtents(FatxFileEntry *entry, std::vector<FileExtent> &extents)
{
    extents.clear();
    if (entry == nullptr || entry->partition == nullptr || this != entry->partition->drive)
        return false;

    if (entry->fileSize == 0)
        return true;

    if (entry->clusterChain.empty())
        ReadClusterChain(entry);

    return ComputeFileExtents(entry->partition, entry->fileSize, entry->clusterChain, extents);
}

void FatxDrive::ReadClusterChain(FatxFileEntry *entry)
{
    // clear the current chain
    entry->clusterChain.clear();

    // calculate values
    bool clusterSizeIs2 = (entry->partition->clusterEntrySize == FAT16);
    u32 lastCluster =  (clusterSizeIs2) ? FAT_CLUSTER16_LAST : FAT_CLUSTER_LAST;
    u32 availableCluster = (clusterSizeIs2) ? FAT_CLUSTER16_AVAILABLE : FAT_CLUSTER_AVAILABLE;
    i64 clusterMapAddress = entry->partition->address + 0x1000;

    // Clusters are numbered from 1 up to the partition's cluster count. A chain
    // can never be longer than that, and it must never revisit a cluster: a
    // corrupt or cross-linked chain would otherwise loop forever while growing
    // clusterChain until the process runs out of memory.
    const u32 maxCluster = entry->partition->clusterCount;
    const size_t maxChainLength = static_cast<size_t>(maxCluster) + 1;

    std::unordered_set<u32> visited;

    // start with the starting cluster
    u32 previousCluster = entry->startingCluster;

    while (previousCluster != lastCluster && previousCluster != availableCluster)
    {
        if (previousCluster == 0 || previousCluster > maxCluster)
            throw std::string("FATX: Cluster chain of \"") + entry->name +
                    "\" references cluster " + std::to_string(previousCluster) +
                    " outside partition \"" + entry->partition->name + "\" (1.." +
                    std::to_string(maxCluster) + ").\n";

        if (entry->clusterChain.size() >= maxChainLength)
            throw std::string("FATX: Cluster chain of \"") + entry->name +
                    "\" is longer than partition \"" + entry->partition->name + "\".\n";

        if (!visited.insert(previousCluster).second)
            throw std::string("FATX: Cluster chain of \"") + entry->name +
                    "\" contains a loop at cluster " + std::to_string(previousCluster) + ".\n";

        // add it to the cluster chain
        entry->clusterChain.push_back(previousCluster);

        // seek to the next cluster
        io->SetPosition(clusterMapAddress + (previousCluster * entry->partition->clusterEntrySize));

        // read the cluster
        if (clusterSizeIs2)
            previousCluster = io->ReadWord();
        else
            previousCluster = io->ReadDword();
    }
}

void FatxDrive::Close()
{
    if (io)
        io->Close();
}

void FatxDrive::CreateBackup(std::string outPath, void (*progress)(void *, u32, u32), void *arg)
{
    // create a file on the local disk to store the backup
    FileIO outBackup(outPath, true);

    u64 driveLen = io->Length();
    io->SetPosition(0);

    std::vector<u8> buffer(0x100000);

    // start writing the drive contents to the local disk
    u32 i = 0;
    u64 totalProgress = driveLen / 0x100000;
    while (driveLen >= 0x100000)
    {
        io->ReadBytes(buffer.data(), 0x100000);
        outBackup.WriteBytes(buffer.data(), 0x100000);

        if (progress)
            progress(arg, i, totalProgress);

        driveLen -= 0x100000;
        i++;
    }

    if (driveLen > 0)
    {
        // Read remaining data
        const u32 tailSize = static_cast<u32>(driveLen);
        io->ReadBytes(buffer.data(), tailSize);
        outBackup.WriteBytes(buffer.data(), tailSize);
    }

    if (progress)
        progress(arg, totalProgress, totalProgress);

    outBackup.Close();

}

void FatxDrive::RestoreFromBackup(std::string backupPath, void (*progress)(void *, u32, u32), void *arg)
{
    std::vector<u8> buffer(0x100000);
    const u64 deviceLength = io->Length();

#ifdef _WIN32
    std::wstring wBackupPath;
    wBackupPath.assign(backupPath.begin(), backupPath.end());
    HANDLE hFile = CreateFileW(wBackupPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        throw std::string("FATX: Could not open drive backup.");

    LARGE_INTEGER backupSize {};
    if (!GetFileSizeEx(hFile, &backupSize))
    {
        CloseHandle(hFile);
        throw std::string("FATX: Could not determine the backup size.");
    }

    const u64 bytesLeftTotal = static_cast<u64>(backupSize.QuadPart);
    if (deviceLength != 0 && bytesLeftTotal != deviceLength)
    {
        CloseHandle(hFile);
        throw std::string("FATX: Backup size does not match the device size.");
    }

    io->SetPosition(0);
    const u64 totalProgress = bytesLeftTotal / 0x100000;
    u64 bytesLeft = bytesLeftTotal;
    u32 i = 0;

    try
    {
        while (bytesLeft > 0)
        {
            const DWORD toRead = static_cast<DWORD>(
                    std::min<u64>(bytesLeft, buffer.size()));
            DWORD totalRead = 0;
            while (totalRead < toRead)
            {
                DWORD got = 0;
                if (!ReadFile(hFile, buffer.data() + totalRead, toRead - totalRead, &got, NULL) ||
                    got == 0)
                    throw std::string("FATX: Error reading from the backup file.");
                totalRead += got;
            }

            io->WriteBytes(buffer.data(), toRead);
            bytesLeft -= toRead;

            if (progress)
                progress(arg, i, totalProgress);
            ++i;
        }
    }
    catch (...)
    {
        CloseHandle(hFile);
        throw;
    }

    CloseHandle(hFile);
#else
    struct stat sb {};
    if (stat(backupPath.c_str(), &sb) != 0)
        throw std::string("FATX: Could not determine the backup size.");

    const u64 bytesLeftTotal = static_cast<u64>(sb.st_size);
    if (deviceLength != 0 && bytesLeftTotal != deviceLength)
        throw std::string("FATX: Backup size does not match the device size.");

    const int backupFile = open(backupPath.c_str(), O_RDONLY);
    if (backupFile == -1)
        throw std::string("FATX: Could not open drive backup.");

    io->SetPosition(0);
    const u64 totalProgress = bytesLeftTotal / 0x100000;
    u64 bytesLeft = bytesLeftTotal;
    u32 i = 0;

    try
    {
        while (bytesLeft > 0)
        {
            const size_t toRead = static_cast<size_t>(
                    std::min<u64>(bytesLeft, buffer.size()));
            size_t totalRead = 0;
            while (totalRead < toRead)
            {
                const ssize_t got = read(backupFile, buffer.data() + totalRead,
                                         toRead - totalRead);
                if (got < 0)
                {
                    if (errno == EINTR)
                        continue;
                    throw std::string("FATX: Error reading from the backup file.");
                }
                if (got == 0)
                    throw std::string("FATX: Backup file ended early.");
                totalRead += static_cast<size_t>(got);
            }

            io->WriteBytes(buffer.data(), static_cast<u32>(toRead));
            bytesLeft -= toRead;

            if (progress)
                progress(arg, i, totalProgress);
            ++i;
        }
    }
    catch (...)
    {
        close(backupFile);
        throw;
    }

    close(backupFile);
#endif

    if (progress)
        progress(arg, totalProgress, totalProgress);

    // reload the entire drive
    ReloadDrive();
}

u8 FatxDrive::cntlzw(u32 x)
{
    if (x == 0)
        return 0;

    u32 t = 0x80000000;
    u8 largestCount = 0, currentCount = 0;
    u8 n = 0;

    while (n < 32)
    {
        bool bitSet = (x & t) >> (31 - n);
        if (bitSet)
        {
            if (currentCount > largestCount)
                largestCount = currentCount;
            currentCount = 0;
        }
        else
            currentCount++;

        t >>= 1;
        n++;
    }

    if (currentCount > largestCount)
        largestCount = currentCount;

    return largestCount;
}

bool FatxDrive::validFileChar(char c)
{
    const unsigned char value = static_cast<unsigned char>(c);

    // FatxIsValidFatFileName rejects only control bytes and the punctuation
    // below. Everything else, including 0x7F and 0x80..0xFF, is a legal byte;
    // the kernel does not interpret FATX names as UTF-8 or UTF-16.
    if (value < 0x20)
        return false;

    switch (value)
    {
        case '"':
        case '*':
        case '+':
        case ',':
        case '/':
        case ':':
        case ';':
        case '<':
        case '=':
        case '>':
        case '?':
        case '\\':
        case '|':
            return false;
        default:
            return true;
    }
}

FatxDrive::~FatxDrive()
{
    if (io)
        io->Close();
}

namespace
{
std::string TrimDriveSerial(const std::string &serial)
{
    const auto isPadding = [](unsigned char character) {
        return character == 0 || character == 0xFF || character == ' ';
    };

    std::size_t begin = 0;
    std::size_t end = serial.size();
    while (begin < end && isPadding(static_cast<unsigned char>(serial[begin])))
        ++begin;
    while (end > begin && isPadding(static_cast<unsigned char>(serial[end - 1])))
        --end;

    return serial.substr(begin, end - begin);
}
} // namespace

bool FatxDrive::operator==(const FatxDrive &other) const
{
    if (type != other.type)
        return false;

    if (type == FatxHarddrive)
    {
        // Model alone collapses two identical drives (for example two retail
        // 320 GB units in USB enclosures). Serial is unique per drive; fall back
        // to model + addressable size when a dump has no serial. A blank serial
        // is often space- or 0xFF-padded, so trim it before deciding.
        const std::string serial = TrimDriveSerial(securityBlob.serialNumber);
        const std::string otherSerial = TrimDriveSerial(other.securityBlob.serialNumber);
        if (!serial.empty() || !otherSerial.empty())
            return serial == otherSerial;
        return securityBlob.modelNumber == other.securityBlob.modelNumber &&
               securityBlob.userAddressableSectors == other.securityBlob.userAddressableSectors;
    }
    else
        return memcmp(configurationData.deviceID, other.configurationData.deviceID, 0x14) == 0;
}

void FatxDrive::loadProfiles()
{
    // the general path for profiles is Drive:\Content\Content\OFFLINE_XUID\FFFE07D1\00010000\OFFLINE_XUID

    FatxFileEntry *contentRoot = NULL;

    // get the content partition's root
    for (u32 i = 0; i < partitions.size(); i++)
    {
        if (partitions.at(i)->name == "Content")
            contentRoot = &partitions.at(i)->root;
    }

    // if the content partition doesn't exist, then return
    if (contentRoot == NULL)
        return;

    // get the content folder in the content partition
    GetChildFileEntries(contentRoot);
}

void FatxDrive::injectRange(vector<u32> &clusters, Range &range)
{
    // do a binary search to find the position to inject the clusters
    int start = 0, end = clusters.size() - 1, pos = 0;
    while (start < end)
    {
        pos = (start + end) / 2;
        if (clusters.at(pos) < range.start)
            start = pos + 1;
        else if (clusters.at(pos) > range.start)
            end = pos - 1;
        else if (clusters.at(pos) == range.start)
            throw std::string("FATX: Error freeing cluster, cluster already free.\n");
    }

    // generate the range of clusters to inject
    vector<u32> sillyRange;
    for (u32 i = 0; i < range.len; i++)
        sillyRange.push_back(range.start + i);

    // insert that range, yo
    clusters.insert(clusters.begin() + start, sillyRange.begin(), sillyRange.end());
}

void FatxDrive::loadFatxDrive(std::wstring drivePath)
{
    if (type == FatxHarddrive)
    {
        // open the device io
        io = std::make_unique<DeviceIO>(drivePath);
    }
    else if (type == FatxFlashDrive)
    {
        std::vector<std::string> dataFiles;
        std::stringstream ss;
        for (int i = 0; i < 3; i++)
        {
            ss << "F:/Xbox360/Data000" << i;
            dataFiles.push_back(ss.str());
            ss.str(std::string());
        }

        io = std::make_unique<JoinedMultiFileIO>(dataFiles);
    }

    loadFatxDrive();
}

void FatxDrive::loadFatxDrive()
{
    // parse the security blob
    if (type == FatxHarddrive)
    {
        io->SetPosition(HddOffsets::SecuritySector);
        securityBlob.serialNumber = io->ReadString(0x14);
        securityBlob.firmwareRevision = io->ReadString(8);
        securityBlob.modelNumber = io->ReadString(0x28);
        io->ReadBytes(securityBlob.msLogoHash, 0x14);
        io->SetEndian(LittleEndian);
        securityBlob.userAddressableSectors = io->ReadDword();
        io->SetEndian(BigEndian);
        io->ReadBytes(securityBlob.rsaSignature, 0x100);
        securityBlob.validSignature = false;

        // TODO: verify the signature

        // seek to the next sector
        io->SetPosition(HddOffsets::SecuritySector + FAT_SECTOR_SIZE);
        securityBlob.msLogoSize = io->ReadDword();
        securityBlob.msLogo.resize(securityBlob.msLogoSize);
        if (securityBlob.msLogoSize > 0)
            io->ReadBytes(securityBlob.msLogo.data(), securityBlob.msLogoSize);

        // if it's a dev drive then we have to read the partition table
        io->SetPosition(0);

        // parse the version
        lastFormatRecoveryVersion.major = io->ReadWord();
        lastFormatRecoveryVersion.minor = io->ReadWord();
        lastFormatRecoveryVersion.build = io->ReadWord();
        lastFormatRecoveryVersion.revision = io->ReadWord();
    }
    else if (type == FatxFlashDrive)
    {
        io->SetPosition(0x228);

        // read the data after the security information
        io->ReadBytes(configurationData.deviceID, 0x14);
        configurationData.securityLength = io->ReadDword();
        configurationData.deviceLength = io->ReadUInt64();
        configurationData.readSpeed = io->ReadWord();
        configurationData.WriteSpeed = io->ReadWord();

        // check for type 1/2
        io->SetPosition(0);
        if (configurationData.securityLength == 0x228)
            ReadCertificateEx(&configurationData.certificate, io.get(), 0);
        else if (configurationData.securityLength == 0x100)
            io->ReadBytes(configurationData.deviceSignature, 0x100);
        else
            throw std::string("FATX: Device contains invalid configuration data.\n");

        // set this so the next check fails
        lastFormatRecoveryVersion.major = 0;
    }

    // Eaton determined this was a version struct and figured out the minimum version
    if (lastFormatRecoveryVersion.major == 2 && lastFormatRecoveryVersion.build >= 1525 && lastFormatRecoveryVersion.revision >= 1)
    {
        auto content = std::make_unique<Partition>();
        content->address = (u64)io->ReadDword() * FAT_SECTOR_SIZE;
        content->size = (u64)io->ReadDword() * FAT_SECTOR_SIZE;
        content->name = "Content";

        auto dashboard = std::make_unique<Partition>();
        dashboard->address = (u64)io->ReadDword() * FAT_SECTOR_SIZE;
        dashboard->size = (u64)io->ReadDword() * FAT_SECTOR_SIZE;
        dashboard->name = "Xbox 360 Dashboard";

        partitions.push_back(std::move(content));
        partitions.push_back(std::move(dashboard));
    }
    else
    {
        // system extended partition initialization
        auto systemExtended = std::make_unique<Partition>();
        systemExtended->address = (type == FatxHarddrive) ? +HddOffsets::SystemExtended : +UsbOffsets::SystemExtended;
        systemExtended->size = (type == FatxHarddrive) ? +HddSizes::SystemExtended : +UsbSizes::SystemExtended;
        systemExtended->name = "System Extended";

        // system auxiliary partition initialization
        auto systemAuxiliary = std::make_unique<Partition>();
        systemAuxiliary->address = (type == FatxHarddrive) ? +HddOffsets::SystemAuxiliary : +UsbOffsets::SystemAuxiliary;
        systemAuxiliary->size = (type == FatxHarddrive) ? +HddSizes::SystemAuxiliary : +UsbSizes::SystemAuxiliary;
        systemAuxiliary->name = "System Auxiliary";

        if (type == FatxHarddrive)
        {
            // system partition initialization
            auto systemPartition = std::make_unique<Partition>();
            systemPartition->address = HddOffsets::SystemPartition;
            systemPartition->size = HddSizes::SystemPartition;
            systemPartition->name = "System Partition";
            this->partitions.push_back(std::move(systemPartition));
        }

        // system cache partition initialization
        auto systemCache = std::make_unique<Partition>();
        systemCache->address = (type == FatxHarddrive) ? +HddOffsets::SystemCache : +UsbOffsets::SystemCache;
        systemCache->size = (type == FatxHarddrive) ? +HddSizes::SystemCache : +UsbSizes::SystemCache;
        systemCache->name = "System Cache";

        // content partition initialization
        auto content = std::make_unique<Partition>();
        content->address = (type == FatxHarddrive) ? +HddOffsets::Data : +UsbOffsets::Data;
        content->size = io->Length() - content->address;
        content->name = "Content";

        // add the partitions to the vector
        this->partitions.push_back(std::move(systemExtended));
        this->partitions.push_back(std::move(systemAuxiliary));
        this->partitions.push_back(std::move(systemCache));
        this->partitions.push_back(std::move(content));
    }

    // process all bootsectors
    for (size_t i = 0; i < this->partitions.size(); )
    {
        processBootSector(this->partitions.at(i).get());

        // if there's invalid magic
        if (this->partitions.at(i)->address == 0)
        {
            partitions.erase(partitions.begin() + i);
        }
        else
            i++;
    }
}

u64 FatxDrive::GetFreeMemory(Partition *part, void(*progress)(void*, bool), void *arg, bool finish)
{
    if (part->freeMemory != 0)
    {
        // The field doubles as the "already scanned" flag; keep it live so a
        // session that injected or removed files reports the right free space.
        part->freeMemory = (u64)part->freeClusters.size() * (u64)part->clusterSize;
        return part->freeMemory;
    }

    // allocate memory for a buffer to minimize the amount of reads
    std::vector<u8> buffer(0x50000);

    // seek to the chainmap
    io->SetPosition(part->address + 0x1000);

    // check if it's FAT16
    bool clusterSizeIs2 = (part->clusterEntrySize == FAT16);

    u64 bytesLeft = (u64)part->clusterCount * (u64)part->clusterEntrySize;
    u32 readSize;
    u32 x = 0;
    while (bytesLeft > 0)
    {
        // calculate the read size
        readSize = (bytesLeft > 0x50000) ? 0x50000 : bytesLeft;
        bytesLeft -= readSize;

        // read in the segment
        io->ReadBytes(buffer.data(), readSize);
        MemoryIO memory(buffer.data(), 0x50000);
        memory.SetPosition(0);

        // update progress if needed
        if (progress)
            progress(arg, false);

        // iterate through all of the clusters
        if (clusterSizeIs2)
        {
            for (u32 i = 0; i < (readSize / 2); i++)
            {
                if (memory.ReadWord() == FAT_CLUSTER16_AVAILABLE)
                    part->freeClusters.push_back(x + i);
            }
            x += 0x28000;
        }
        else
        {
            for (u32 i = 0; i < (readSize / 4); i++)
            {
                u32 test = memory.ReadDword();
                if (test == FAT_CLUSTER_AVAILABLE)
                    part->freeClusters.push_back(x + i);
            }
            x += 0x14000;
        }
    }

    // calculate the amount of free memory
    part->freeMemory = (u64)part->freeClusters.size() * (u64)part->clusterSize;

    if (progress)
        progress(arg, finish);

    return part->freeMemory;
}

u64 FatxDrive::GetTotalSize()
{
    u64 toReturn = 0;
    for (size_t i = 0; i < partitions.size(); i++)
        toReturn += partitions.at(i)->size;

    return toReturn;
}

u64 FatxDrive::GetDeviceSize()
{
    return io->Length();
}

void FatxDrive::ReloadDrive()
{
    partitions.clear();
    loadFatxDrive();
}

bool FatxDrive::NamesEqual(const std::string &a, const std::string &b)
{
    // Kernel FATX lookup compares length first, then folds a-z to A-Z
    // (xboxkrnlct 16547, 0x800D38B0 -> 0x800AB4A8 -> 0x800AB308).
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) !=
                std::toupper(static_cast<unsigned char>(b[i])))
            return false;

    return true;
}

bool FatxDrive::FileExists(std::string filePath)
{
    return !!GetFileEntry(filePath);
}

bool FatxDrive::FileExists(FatxFileEntry *folder, std::string fileName, bool checkDeleted)
{
    GetChildFileEntries(folder);

    for (size_t i = 0; i < folder->cachedFiles.size(); i++)
    {
        // there's a better way to do this, but...
        if (NamesEqual(folder->cachedFiles.at(i).name, fileName))
        {
             if (folder->cachedFiles.at(i).nameLen == FATX_ENTRY_DELETED)
             {
                 if (checkDeleted)
                    return true;
             }
             else
                 return true;
        }
    }

    return false;
}

FatxFileEntry* FatxDrive::GetFileEntry(std::string filePath)
{
    // make sure the path starts with "Drive:\\"
    if (filePath.size() < 7 || filePath.substr(0, 7) != "Drive:\\")
        throw string("FATX: Invalid path name.");

    // strip off the prefix
    filePath = filePath.substr(7);

    // get the partition
    std::string partitionName = filePath.substr(0, filePath.find('\\'));

    if (filePath.find('\\') == std::string::npos)
        filePath = "";
    else
        filePath = filePath.substr(filePath.find('\\') + 1);

    Partition *part = NULL;
    for (u32 i = 0; i < partitions.size(); i++)
    {
        if (partitions.at(i)->name == partitionName)
        {
            part = partitions.at(i).get();
            break;
        }
    }

    // if there is no corresponding partition, then the path is invalid
    if (part == NULL)
        return NULL;

    FatxFileEntry *parent = &part->root;
    while ((int)filePath.find('\\') >= 0 || filePath.size() > 0)
    {
        // load all the entries children (not recursively)
        GetChildFileEntries(parent);

        // get the next file name
        std::string fileName = filePath.substr(0, filePath.find('\\'));

        if (filePath.find('\\') == std::string::npos)
            filePath = "";
        else
            filePath = filePath.substr(filePath.find('\\') + 1);

        // check and see if the sub-directory exists
        FatxFileEntry *foundEntry = NULL;
        for (u32 i = 0; i < parent->cachedFiles.size(); i++)
        {
            if (NamesEqual(parent->cachedFiles.at(i).name, fileName) && parent->cachedFiles.at(i).nameLen != FATX_ENTRY_DELETED)
            {
                foundEntry = &parent->cachedFiles.at(i);
                break;
            }
        }

        if (foundEntry == NULL)
            return NULL;

        parent = foundEntry;
    }

    return parent;
}

void FatxDrive::SetDriveName(std::wstring name)
{
    FatxFileEntry *nameEntry = GetFileEntry("Drive:\\Content\\name.txt");

    // if the file doesn't exist, then we need to create it
    if (nameEntry == NULL)
    {
        CreateFileX(GetFileEntry("Drive:\\Content"), "name.txt");
        nameEntry = GetFileEntry("Drive:\\Content\\name.txt");
    }

    FatxIO nameIO = GetFatxIO(nameEntry);

    // update the entry size
    int count = (name.length() * 2) + 2;

    if (count > 0x36)
        throw std::string("FATX: Drive name cannot be larger than 26 characters.\n");

    // A fresh name.txt owns no clusters; allocate before the size is set,
    // because SetPosition() maps through the cluster chain.
    if (nameEntry->clusterChain.empty())
        nameIO.AllocateMemory(static_cast<u32>(count));

    nameIO.entry->fileSize = count;
    nameIO.WriteEntryToDisk();

    // set the postition
    nameIO.SetPosition(0);

    // the xbox requires that these 2 bytes are at the beginning of the file for whatever reason
    nameIO.Write((u16)0xFEFF);
    nameIO.Write(name);
}

bool FatxDrive::ValidFileName(std::string fileName)
{
    if (fileName.empty() || fileName.size() > FATX_ENTRY_MAX_NAME_LENGTH)
        return false;

    // FatxIsValidFatFileName also rejects the dot entries.
    if (fileName == "." || fileName == "..")
        return false;

    for (u32 i = 0; i < fileName.size(); i++)
        if (!validFileChar(fileName.at(i)))
            return false;

    return true;
}
