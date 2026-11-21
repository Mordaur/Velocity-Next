#ifndef FATXIO_H
#define FATXIO_H

#include <XboxInternals/IO/BaseIO.h>
#include <XboxInternals/IO/FileIO.h>
#include <XboxInternals/IO/MemoryIO.h>
#include "../Fatx/FatxConstants.h"
#include "../Cryptography/XeCrypt.h"

#include <iostream>
#include <vector>
#include <algorithm>

struct Range
{
    u64 start;
    u64 len;
};

class XBOXINTERNALS_EXPORT FatxIO : public BaseIO
{
public:
    FatxFileEntry *entry;

    FatxIO(BaseIO *device, FatxFileEntry *entry);
    virtual ~FatxIO();

    // get the current file entry
    FatxFileEntry *GetFatxFileEntry();

    // set the position
    void SetPosition(u64 position, std::ios_base::seekdir dir = std::ios_base::beg);

    // get the current position
    u64 GetPosition();

    // returns the length
    u64 Length();

    // get the drive's current position
    u64 GetDrivePosition();

    // read bytes at the current position
    void ReadBytes(u8 *outBuffer, u32 len);

    // Write bytes at the current position
    void WriteBytes(u8 *buffer, u32 len);

    // save the file to disk
    void SaveFile(std::string savePath, void(*progress)(void*, u32, u32) = NULL, void *arg = NULL);

    // does nothing, required implementation
    void Flush();

    // does nothing, required implementation
    void Close();

    // expands the cluster chain if necessary, returns the amount of new clusters allocated
    int AllocateMemory(u32 byteAmount);

    // Write the entry to disk
    void WriteEntryToDisk(std::vector<u32> *clusterChain = NULL);

    // replace the file with one from a local disk
    void ReplaceFile(std::string sourcePath, void(*progress)(void*, u32, u32) = NULL,
            void *arg = NULL);

    // convert a cluster to an offset
    static u64 ClusterToOffset(Partition *part, u32 cluster);

    // sets all the clusters equal to value
    static void SetAllClusters(BaseIO *device, Partition *part, std::vector<u32> &clusters,
            u32 value);

    // get the ranges of consecutive numbers in list where it's sorted
    static void GetConsecutive(std::vector<u32> &list, std::vector<Range> &outRanges,
            bool includeNonConsec = false);

private:
    // find count amount of free custers
    std::vector<u32> getFreeClusters(Partition *part, u32 count);

    // Writes the cluster chain (and links them correctly) starting from startingCluster
    void WriteClusterChain(Partition *part, u32 startingCluster, std::vector<u32> clusterChain);

    BaseIO *device;
    u64 pos;
    u32 maxReadConsecutive;
};

bool compareRanges(Range a, Range b);

#endif // FATXIO_H


