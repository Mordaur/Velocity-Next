#include <XboxInternals/IO/FatxIO.h>
#include <XboxInternals/Fatx/FatxHelpers.h>

#include <algorithm>
#include <cstddef>
#include <vector>

FatxIO::FatxIO(BaseIO *device, FatxFileEntry *entry) : entry(entry), device(device)
{
    // if it's a new file, then don't do any seeking yet
    if (entry->startingCluster != 0)
        SetPosition(0);
}

FatxIO::~FatxIO()
{
}

void FatxIO::SetPosition(u64 position, std::ios_base::seekdir dir)
{
    if (dir == std::ios_base::cur)
        position += pos;
    else if (dir == std::ios_base::end)
        position = (Length() + position);

    // if they seek beyond the end of the stream, then we need to allocate memory
    if (position > entry->fileSize && !(entry->fileAttributes & FatxDirectory))
    {
        // calculate how far beyond the stream the caller is seeking
        AllocateMemory(position - entry->fileSize);
    }

    pos = position;

    if (pos == entry->fileSize && !(entry->fileAttributes & FatxDirectory))
        return;

    // A zero cluster size indicates a corrupt partition header.
    if (entry->partition->clusterSize == 0)
        throw std::string("FATX: Invalid cluster size.\n");

    // calculate the cluster index to look at
    u32 clusterIndex =  position / entry->partition->clusterSize;

    if (clusterIndex >= entry->clusterChain.size())
        throw std::string("FATX: Cluster chain not sufficient enough for file size.\n");

    u32 cluster = entry->clusterChain.at(clusterIndex);

    // calculate the read position
    u32 startInCluster = (position % entry->partition->clusterSize);
    i64 driveOff = (entry->partition->clusterStartingAddress + (entry->partition->clusterSize *
            (i64)(cluster - 1))) + startInCluster;

    // this stores how many bytes we have until we reach another cluster
    maxReadConsecutive = entry->partition->clusterSize - startInCluster;

    // set the position
    device->SetPosition(driveOff);
}

void FatxIO::Flush()
{
    device->Flush();
}

void FatxIO::Close()
{
    // nothing to close since this doesn't actually have a file open
}

int FatxIO::AllocateMemory(u32 byteAmount)
{
    // preserve the position
    u64 pos = device->GetPosition();

    bool wasZero = (byteAmount == 0);
    if (wasZero)
        byteAmount++;

    // calculate how many clusters to allocate
    u32 clusterCount = (byteAmount + ((entry->partition->clusterSize - (entry->fileSize %
            entry->partition->clusterSize)) - 1)) / entry->partition->clusterSize;
    bool fileIsNull = (entry->fileSize == 0 || entry->startingCluster == 0);

    if (!(entry->fileAttributes & FatxDirectory) && !wasZero)
        entry->fileSize += byteAmount;

    // get the free clusters
    std::vector<u32> freeClusters = getFreeClusters(entry->partition, clusterCount);
    if (freeClusters.size() != clusterCount)
        throw std::string("FATX: Cannot find requested amount of free clusters.\n");

    // if it's a folder, then we need to 0xFF out all of the clusters allocated so that it doesn't pick up fake entries
    if (entry->fileAttributes & FatxDirectory)
    {
        std::vector<u8> ffBuff(entry->partition->clusterSize, 0xFF);

        for (size_t i = 0; i < freeClusters.size(); i++)
        {
            device->SetPosition(ClusterToOffset(entry->partition, freeClusters.at(i)));
            device->WriteBytes(ffBuff.data(), entry->partition->clusterSize);
        }
    }

    // add the free clusters to the cluster chain
    for (size_t i = 0; i < freeClusters.size(); i++)
        entry->clusterChain.push_back(freeClusters.at(i));

    if (fileIsNull)
        entry->startingCluster = entry->clusterChain.at(0);

    // Write the cluster chain (only if it's changed)
    if (clusterCount != 0)
        WriteClusterChain(entry->partition, entry->startingCluster, entry->clusterChain);

    if (entry->address != -1)
        WriteEntryToDisk();

    // preserve the position
    device->SetPosition(pos);

    return clusterCount;
}

u64 FatxIO::GetPosition()
{
    return pos;
}

u64 FatxIO::Length()
{
    return entry->fileSize;
}

u64 FatxIO::GetDrivePosition()
{
    return device->GetPosition();
}

FatxFileEntry* FatxIO::GetFatxFileEntry()
{
    return entry;
}

void FatxIO::ReadBytes(u8 *outBuffer, u32 len)
{
    // get the length
    u32 origLen = len;

    u32 bytesToRead = (len <= maxReadConsecutive) ? len : maxReadConsecutive;
    device->ReadBytes(outBuffer, bytesToRead);
    len -= bytesToRead;
    SetPosition(pos + bytesToRead);

    while (len >= entry->partition->clusterSize)
    {
        // calculate how many bytes to read
        device->ReadBytes(outBuffer + (origLen - len), maxReadConsecutive);

        // update the position
        SetPosition(pos + maxReadConsecutive);

        // update the length
        len -= maxReadConsecutive;
    }

    if (len > 0)
    {
        device->ReadBytes(outBuffer + (origLen - len), len);
        SetPosition(pos + len);
    }
}

void FatxIO::WriteBytes(u8 *buffer, u32 len)
{
    // get the length
    u32 origLen = len;

    u32 bytesToWrite = (len <= maxReadConsecutive) ? len : maxReadConsecutive;
    device->WriteBytes(buffer, bytesToWrite);
    len -= bytesToWrite;
    SetPosition(pos + bytesToWrite);

    while (len >= entry->partition->clusterSize)
    {
        // calculate how many bytes to read
        device->WriteBytes(buffer + (origLen - len), maxReadConsecutive);

        // update the position
        SetPosition(pos + maxReadConsecutive);

        // update the length
        len -= maxReadConsecutive;
    }

    if (len > 0)
        device->WriteBytes(buffer + (origLen - len), len);
}

void FatxIO::EraseIndexRanges(std::vector<u32> &list, std::vector<Range> ranges)
{
    // Erase from the highest index down: an erase shifts every later index, so
    // the original indices are only valid in descending order.
    std::sort(ranges.begin(), ranges.end(),
            [](const Range &a, const Range &b) { return a.start > b.start; });

    for (const Range &r : ranges)
    {
        if (r.start + r.len > list.size())
            throw std::string("FATX: Cluster range out of bounds.\n");
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(r.start),
                list.begin() + static_cast<std::ptrdiff_t>(r.start + r.len));
    }
}

std::vector<u32> FatxIO::getFreeClusters(Partition *part, u32 count)
{
    std::vector<u32> freeClusters(count);
    std::vector<Range> consecutiveClusters;
    std::vector<Range> usedRanges;

    // check to see if we have enough free clusters left
    if (count > part->freeClusters.size())
    {
        std::stringstream ss;
        ss << "FATX: Out of memory. There are only ";
        ss << ByteSizeToString(part->freeClusters.size() * part->clusterSize).c_str();
        ss << " of free memory remaining on this partition.\n";

        throw ss.str();
    }

    // get the consecutive clusters
    GetConsecutive(part->freeClusters, consecutiveClusters);

    // sort the consecutive clusters so that the longest ranges are at the beginning
    std::sort(consecutiveClusters.begin(), consecutiveClusters.end(), compareRanges);

    // first get clusters that are consecutive
    u32 i = 0, currentIndex = 0;
    while (count > 0 && i < consecutiveClusters.size())
    {
        // get the amount of clusters to copy from the consecutive cluster range
        u32 clustersToCopy = (count > consecutiveClusters.at(i).len) ? consecutiveClusters.at(
                    i).len : count;
        std::vector<u32>::iterator rangeStart = part->freeClusters.begin() + consecutiveClusters.at(
                    i).start;

        // get that calculated amount of consecutive clusters
        std::copy(rangeStart, rangeStart + clustersToCopy, freeClusters.begin() + currentIndex);

        Range r = { consecutiveClusters.at(i).start, clustersToCopy };
        usedRanges.push_back(r);

        count -= clustersToCopy;
        currentIndex += clustersToCopy;
        i++;
    }

    // remove all of the used clusters
    EraseIndexRanges(part->freeClusters, usedRanges);

    // if we still need more clusters, then we'll just copy over non-consecutive ones that are left
    std::copy(part->freeClusters.begin(), part->freeClusters.begin() + count,
              freeClusters.begin() + currentIndex);
    part->freeClusters.erase(part->freeClusters.begin(), part->freeClusters.begin() + count);

    return freeClusters;
}

void FatxIO::SetAllClusters(BaseIO *device, Partition *part, std::vector<u32> &clusters,
        u32 value)
{
    // sort the clusters numerically, order doesn't matter any more since we're just setting them all to the same value
    XeCrypt::InsertionSort(clusters.begin(), clusters.end());

    // we'll work with the clusters in 0x10000 chunks to minimize the amount of reads
    u8 buffer[0x10000];

    for (u32 i = 0; i < clusters.size(); i++)
    {
        // seek to the lowest cluster in the chainmap, but round to get a fast read
        u64 clusterEntryAddr = (part->address + 0x1000 + clusters.at(i) * part->clusterEntrySize);
        device->SetPosition(DOWN_TO_NEAREST_SECTOR(clusterEntryAddr));

        // read in a chunk of the chainmap
        device->ReadBytes(buffer, 0x10000);

        // open a MemoryIO on the memory for easier access
        MemoryIO bufferIO(buffer, 0x10000);

        // calculate the offset of the cluster in the buffer
        u64 clusterEntryOffset = clusterEntryAddr - DOWN_TO_NEAREST_SECTOR(clusterEntryAddr);

        // loop while still in the bounds of the buffer
        while (clusterEntryOffset < 0x10000)
        {
            // seek to the address of the cluster in the buffer
            bufferIO.SetPosition(clusterEntryOffset);

            // set the cluster chain map entry to the value requested
            if (part->clusterEntrySize == 2)
                bufferIO.Write((u16)value);
            else
                bufferIO.Write((u32)value);

            if (i + 1 == clusters.size())
            {
                ++i;
                break;
            }

            // calculate the value of the next cluster entry
            clusterEntryOffset = (part->address + 0x1000 + clusters.at(++i) * part->clusterEntrySize) -
                    DOWN_TO_NEAREST_SECTOR(clusterEntryAddr);
        }
        --i;

        // once we're done then Write the block of memory back
        device->SetPosition(DOWN_TO_NEAREST_SECTOR(clusterEntryAddr));
        device->WriteBytes(buffer, 0x10000);
    }

    // flush the device just to be safe
    device->Flush();
}

void FatxIO::WriteEntryToDisk(std::vector<u32> *clusterChain)
{
    bool isDeleted = (entry->nameLen == FATX_ENTRY_DELETED);
    u8 nameLen;
    if (isDeleted)
        nameLen = FATX_ENTRY_DELETED;
    else
    {
        nameLen = entry->name.length();
        if (nameLen > FATX_ENTRY_MAX_NAME_LENGTH)
            throw std::string("FATX: Entry name must be less than 42 characters.\n");
    }
    bool wantsToWriteClusterChain = (clusterChain != NULL);

    if (wantsToWriteClusterChain && entry->startingCluster != clusterChain->at(0))
        throw std::string("FATX: Entry starting cluster does not match with cluster chain.\n");

    device->SetPosition(entry->address);
    device->Write(nameLen);
    device->Write(entry->fileAttributes);
    device->Write(entry->name, FATX_ENTRY_MAX_NAME_LENGTH, false, 0xFF);
    device->Write(entry->startingCluster);

    if (entry->fileAttributes & FatxDirectory)
        device->Write((u32)0);
    else
        device->Write(entry->fileSize);

    device->Write(entry->creationDate);
    device->Write(entry->lastWriteDate);
    device->Write(entry->lastAccessDate);

    if (wantsToWriteClusterChain)
    {
        SetAllClusters(device, entry->partition, entry->clusterChain, FAT_CLUSTER_AVAILABLE);
        WriteClusterChain(entry->partition, entry->startingCluster, *clusterChain);
    }
}

void FatxIO::ReplaceFile(std::string sourcePath, void (*progress)(void *, u32, u32), void *arg)
{
    // open the file to replace the current one with
    FileIO inFile(sourcePath);

    // get the size of the file
    inFile.SetPosition(0, std::ios_base::end);
    u64 fileSize = inFile.GetPosition();

    if (fileSize == 0)
    {
        inFile.Close();
        if (progress)
            progress(arg, 1, 1);
        return;
    }

    // reset the position
    inFile.SetPosition(0);

    // calculate the amount of clusters for the file
    u32 clusterCount = (fileSize / entry->partition->clusterSize) + ((fileSize %
            entry->partition->clusterSize == 0) ? 0 : 1);

    // if the file is smaller, then we can free up some clusters at the end
    if (clusterCount < entry->clusterChain.size())
    {
        std::vector<u32> clustersToFree(entry->clusterChain.size() - clusterCount);

        // copy over the clusters that can be freed
        std::copy(entry->clusterChain.begin() + clusterCount, entry->clusterChain.end(),
                  clustersToFree.begin());

        // set all of those clusters to free
        SetAllClusters(device, entry->partition, clustersToFree, FAT_CLUSTER_AVAILABLE);

        // erase the now freed ones from the chain
        entry->clusterChain.erase(entry->clusterChain.begin() + clusterCount, entry->clusterChain.end());
    }
    // if the file is bigger then we need to allocate clustes
    else if (clusterCount > entry->clusterChain.size())
    {
        AllocateMemory((clusterCount * entry->partition->clusterSize) - entry->fileSize);
    }

    entry->fileSize = fileSize;
    WriteEntryToDisk();

    ///////////////////
    // START WRITING //
    ///////////////////

    // calculate stuff
    u32 bufferSize = (entry->fileSize / 0x10);
    if (bufferSize < 0x10000)
        bufferSize = 0x10000;
    else if (bufferSize > 0x100000)
        bufferSize = 0x100000;

    std::vector<Range> WriteRanges;
    std::vector<u8> buffer(bufferSize);
    u64 pos;

    // generate the read ranges
    for (u32 i = 0; i < entry->clusterChain.size() - 1; i++)
    {
        // calculate cluster's address
        pos = FatxIO::ClusterToOffset(entry->partition, entry->clusterChain.at(i));

        Range range = { pos, 0 };
        do
        {
            range.len += entry->partition->clusterSize;

            i++;
        }
        while ((entry->clusterChain.at(i - 1) + 1) == entry->clusterChain.at(i) &&
                i < (entry->clusterChain.size() - 2) && (range.len + entry->partition->clusterSize) <= bufferSize);
        i--;

        WriteRanges.push_back(range);
    }

    u32 finalClusterSize = entry->fileSize % entry->partition->clusterSize;
    u64 finalClusterOffset = FatxIO::ClusterToOffset(entry->partition,
            entry->clusterChain.at(entry->clusterChain.size() - 1));
    Range lastRange = { finalClusterOffset , (finalClusterSize == 0) ? entry->partition->clusterSize : finalClusterSize };
    WriteRanges.push_back(lastRange);

    u32 modulus = WriteRanges.size() / 100;
    if (modulus == 0)
        modulus = 1;
    else if (modulus > 3)
        modulus = 3;

    // read all the data in
    for (u32 i = 0; i < WriteRanges.size(); i++)
    {
        // seek to the beginning of the range
        device->SetPosition(WriteRanges.at(i).start);

        // get the range from the device
    inFile.ReadBytes(buffer.data(), WriteRanges.at(i).len);
    device->WriteBytes(buffer.data(), WriteRanges.at(i).len);

        // update progress if needed
        if (progress && i % modulus == 0)
            progress(arg, i, WriteRanges.size());
    }

    // clean up
    inFile.Close();

    // make sure it hits the end
    if (progress)
        progress(arg, WriteRanges.size(), WriteRanges.size());
}

void FatxIO::WriteClusterChain(Partition *part, u32 startingCluster,
        std::vector<u32> clusterChain)
{
    if (startingCluster == 0 || clusterChain.size() == 0)
        return;
    else if (startingCluster > part->clusterCount)
        throw std::string("FATX: Cluster is greater than cluster count.\n");

    bool clusterSizeIs16 = part->clusterEntrySize == FAT16;

    clusterChain.push_back(FAT_CLUSTER_LAST);

    // we'll work with the clusters in 0x10000 chunks to minimize the amount of reads
    u8 buffer[0x10000];

    for (u32 i = 1; i < clusterChain.size(); i++)
    {
        // seek to the lowest cluster in the chainmap, but round to get a fast read
        u64 clusterEntryAddr = (part->address + 0x1000 + clusterChain.at(i - 1) *
                part->clusterEntrySize);
        device->SetPosition(DOWN_TO_NEAREST_SECTOR(clusterEntryAddr));

        // read in a chunk of the chainmap
        device->ReadBytes(buffer, 0x10000);

        // open a MemoryIO on the memory for easier access
        MemoryIO bufferIO(buffer, 0x10000);

        // calculate the offset of the cluster in the buffer
        u64 clusterEntryOffset = clusterEntryAddr - DOWN_TO_NEAREST_SECTOR(clusterEntryAddr);

        // loop while still in the bounds of the buffer
        while (clusterEntryOffset < 0x10000)
        {
            // seek to the address of the cluster in the buffer
            bufferIO.SetPosition(clusterEntryOffset);

            // set the cluster chain map entry to the value requested
            if (clusterSizeIs16)
                bufferIO.Write((u16)clusterChain.at(i));
            else
                bufferIO.Write((u32)clusterChain.at(i));

            if (i + 1 == clusterChain.size())
            {
                ++i;
                break;
            }

            // calculate the value of the next cluster entry
            clusterEntryOffset = (part->address + 0x1000 + clusterChain.at(i++) * part->clusterEntrySize) -
                    DOWN_TO_NEAREST_SECTOR(clusterEntryAddr);
        }
        --i;

        // once we're done then Write the block of memory back
        device->SetPosition(DOWN_TO_NEAREST_SECTOR(clusterEntryAddr));
        device->WriteBytes(buffer, 0x10000);
    }

    // flush the device just to be safe
    device->Flush();

    /*for (int i = 0; i < clusterChain.size(); i++)
    {
        device->SetPosition(part->address + 0x1000 + (previousCluster * part->clusterEntrySize));

        if (clusterSizeIs16)
            device->Write((u16)clusterChain.at(i));
        else
            device->Write((u32)clusterChain.at(i));

        previousCluster = clusterChain.at(i);
    }*/
}

void FatxIO::GetConsecutive(std::vector<u32> &list, std::vector<Range> &outRanges,
        bool includeNonConsec)
{
    for (size_t i = 0; i < list.size(); i++)
    {
        u64 start = i++;
        u64 streak = 1;

        while (i < list.size() && list[i] - list[start] == i - start)
        {
            streak++;
            i++;
        }
        i--;

        if (includeNonConsec || streak > 1)
        {
            Range range = { start, streak };
            outRanges.push_back(range);
        }
    }
}

void FatxIO::SaveFile(std::string savePath, void(*progress)(void*, u32, u32), void *arg)
{
    // A zero-length file has no cluster chain. Handle it before the chain is
    // indexed below, which would otherwise read clusterChain.at(size() - 1) out
    // of bounds.
    if (entry->fileSize == 0)
    {
        FileIO outFile(savePath, true);
        outFile.Close();

        if (progress)
            progress(arg, 1, 1);
        return;
    }

    // get the current position
    u64 pos;
    u64 originalPos = device->GetPosition();

    // seek to the beggining of the file
    SetPosition(0);

    // calculate stuff
    u32 bufferSize = (entry->fileSize / 0x10);
    if (bufferSize < 0x10000)
        bufferSize = 0x10000;
    else if (bufferSize > 0x100000)
        bufferSize = 0x100000;

    std::vector<Range> readRanges;
    std::vector<u8> buffer(bufferSize);

    // generate the read ranges
    for (u32 i = 0; i < entry->clusterChain.size() - 1; i++)
    {
        // calculate cluster's address
        pos = ClusterToOffset(entry->partition, entry->clusterChain.at(i));

        Range range = { pos, 0 };
        do
        {
            range.len += entry->partition->clusterSize;

            i++;
        }
        while ((entry->clusterChain.at(i - 1) + 1) == entry->clusterChain.at(i) &&
                i < (entry->clusterChain.size() - 2) && (range.len + entry->partition->clusterSize) <= bufferSize);
        i--;

        readRanges.push_back(range);
    }

    u32 finalClusterSize = entry->fileSize % entry->partition->clusterSize;
    u64 finalClusterOffset = ClusterToOffset(entry->partition,
            entry->clusterChain.at(entry->clusterChain.size() - 1));
    Range lastRange = { finalClusterOffset , (finalClusterSize == 0) ? entry->partition->clusterSize : finalClusterSize};
    readRanges.push_back(lastRange);

    u32 modulus = readRanges.size() / 100;
    if (modulus == 0)
        modulus = 1;
    else if (modulus > 3)
        modulus = 3;

    // open the new file
    FileIO outFile(savePath, true);

    // read all the data in
    for (u32 i = 0; i < readRanges.size(); i++)
    {
        // seek to the beginning of the range
        device->SetPosition(readRanges.at(i).start);

        // get the range from the device
    device->ReadBytes(buffer.data(), readRanges.at(i).len);
    outFile.WriteBytes(buffer.data(), readRanges.at(i).len);

        // update progress if needed
        if (progress && i % modulus == 0)
            progress(arg, i + 1, readRanges.size());
    }

    // make sure it hits the end
    if (progress)
        progress(arg, readRanges.size(), readRanges.size());

    outFile.Flush();
    outFile.Close();

    device->SetPosition(originalPos);
}

u64 FatxIO::ClusterToOffset(Partition *part, u32 cluster)
{
    return part->clusterStartingAddress + (part->clusterSize * (i64)(cluster - 1));
}

bool compareRanges(Range a, Range b)
{
    return a.len > b.len;
}


