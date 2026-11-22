#pragma once

#include <iostream>
#include <map>
#include <math.h>
#include <memory>
#include <sstream>
#include <stdlib.h>
#include <time.h>
#include <vector>
#include <XboxInternals/IO/FileIO.h>
#include <XboxInternals/Stfs/IXContentHeader.h>

#include <botan_all.h>

#include <XboxInternals/Export.h>

using std::string;
using std::stringstream;
using std::hex;
using std::vector;

class StfsIO;

struct StfsFileEntry
{
    u32 entryIndex = 0;
    string name;
    u8 nameLen = 0;
    u8 flags = 0;
    u32 blocksForFile = 0;
    u32 startingBlockNum = 0;
    u16 pathIndicator = 0;
    u32 fileSize = 0;
    u32 createdTimeStamp = 0;
    u32 accessTimeStamp = 0;
    u32 fileEntryAddress = 0;
    vector<u32> blockChain;
};

struct StfsFileListing
{
    vector<StfsFileEntry> fileEntries;
    vector<StfsFileListing> folderEntries;
    StfsFileEntry folder;
};

#pragma pack(push, 1)
struct HashEntry
{
    u8 blockHash[0x14];
    u8 status;
    u32 nextBlock;
};
#pragma pack(pop)

struct HashTable
{
    Level level;
    u32 trueBlockNumber;
    u32 entryCount;
    HashEntry entries[0xAA];
    u32 addressInFile;
};

class XBOXINTERNALS_EXPORT StfsPackage : public IXContentHeader
{
public:
        using IXContentHeader::metaData;

    // Description: initialize a stfs package from an already opened io
    StfsPackage(BaseIO *io, u32 flags = 0);

    // Description: initialize a stfs package
    StfsPackage(string packgePath, u32 flags = 0);

    // Description: get the file listing of the package, forceUpdate reads from the package regardless
    StfsFileListing GetFileListing(bool forceUpdate = false);

    // Description: extract a file to designated file path
    void ExtractFile(string pathInPackage, string outPath, void(*extractProgress)(void*, u32,
            u32) = NULL, void *arg = NULL);

    // Description: extract a file (by FileEntry) to a designated file path
    void ExtractFile(StfsFileEntry *entry, string outPath, void(*extractProgress)(void*, u32,
            u32) = NULL, void *arg = NULL);

    // Description: get the file entry of a file's path, sets nameLen to '0' if not found
    StfsFileEntry GetFileEntry(string pathInPackage, bool checkFolders = false,
            StfsFileEntry *newEntry = NULL, bool useWildcards = true);

    // Description: creates an StfsIO based off of the given path
    StfsIO* GetStfsIO(string pathInPackage);

    // Description: creates an StfsIO based off of the given entry
    StfsIO* GetStfsIO(StfsFileEntry entry);

    // Description: get the first 4 bytes of a file
    u32 GetFileMagic(string pathInPackage);

    // Description: get the first 4 bytes of a file
    u32 GetFileMagic(StfsFileEntry entry);

    // Description: check if the file exists
    bool FileExists(string pathInPackage, bool useWildcards = true);

    // Description: case-insensitive name match. With useWildcards (the default,
    // mirroring IoIsNameInExpression for lookups) '*' and '?' are patterns;
    // mutations pass false so a literal '*'/'?' in a name cannot match another
    // entry.
    static bool MatchesFileName(const std::string &pattern, const std::string &name,
            bool useWildcards = true);

    // Description: number of 4 KiB data blocks that fit in a block budget once
    // STFS hash blocks are reserved (kernel StfsComputeNumberOfDataBlocks,
    // male volumes; the kernel skips the inverse for female volumes).
    static u32 ComputeNumberOfDataBlocks(u64 blocks);

    // Description: ceil(sizeBytes / 0x1000) run through ComputeNumberOfDataBlocks.
    static u32 ComputeAllocatedBlocksForSize(u64 sizeBytes);

    // Description: fix all the hashes used in the file
    void Rehash();

    // Description: same as Rehash(), but also for LIVE/PIRS packages this
    // application created itself (e.g. avatar assets for a JTAG/RGH console).
    // Their 0x100-byte package signature cannot be recomputed, so this
    // invalidates it by design; never use it on retail content.
    void RebuildHashes();

    // Description: reload the top hash table from disk after Rehash/Resign operations
    void ReloadHashTable();

    // Description: resign the file
    void Resign(string kvPath);

    // Description: resign the file
    void Resign(u8* kvData, size_t length);

    // Description: remove a file entry from the file listing
    void RemoveFile(StfsFileEntry entry);

    // Description: remove a file entry from the file listing
    void RemoveFile(string pathInPackage);

    // Description: inject a file into the package
    StfsFileEntry InjectFile(string path, string pathInPackage, void(*injectProgress)(void*, u32,
            u32) = NULL, void *arg = NULL);

    // Description: inject raw data into the package
    StfsFileEntry InjectData(u8 *data, u32 length, string pathInPackage,
            void(*injectProgress)(void*, u32, u32) = NULL, void *arg = NULL);

    // Description: replace an existing file into the package
    void ReplaceFile(string path, string pathInPackage, void(*replaceProgress)(void*, u32,
            u32) = NULL, void *arg = NULL);

    // Description: replace an existing file into the package
    void ReplaceFile(string path, StfsFileEntry *entry, string pathInPackage,
            void(*replaceProgress)(void*, u32, u32) = NULL, void *arg = NULL);

    // Description: rename an existing file in the package
    void RenameFile(string newName, string pathInPackage);

    // Description: convert a block into an address in the file
    u32 BlockToAddress(u32 blockNum);

    // Description: get the address of a hash for a data block
    u32 GetHashAddressOfBlock(u32 blockNum);

    // Description: returns whether the 'isPEC' parameter is set
    bool IsPEC();

        // Description: close the io and all other resources used
        void Close();

        // Description: creates a folder in the specified directory
        void CreateFolder(string pathInPackage);

        // Description: builds a block chain for the given entry
        void GenerateBlockChain(StfsFileEntry *entry, bool forceRefresh = false);

        // Description: clears all cached block chains, forcing regeneration on next access
        void ClearCachedBlockChains();

    ~StfsPackage(void);
private:
    StfsFileListing fileListing;
    StfsFileListing writtenToFile;

        BaseIO *io;
        std::unique_ptr<BaseIO> ownedIO;
        std::unique_ptr<XContentHeader> metaDataOwner;
    stringstream except;

    Sex packageSex;
    u32 blockStep[2];
    u32 firstHashTableAddress;
    u8 hashOffset;
    Level topLevel;
    HashTable topTable;
    HashTable cached;
    u32 tablesPerLevel[3];

    u32 flags;

    // Description: read the file listing from the file
    void ReadFileListing();

    // Description: set the out buffer to the sha1 of the block
    void HashBlock(u8 *block, u8 *outBuffer);

    // Description: swap the table used so there is a backup of the data modified
    void SwapTable(u32 index, Level lvl);

    // Description: extract a block's data
    void ExtractBlock(u32 blockNum, u8 *data, u32 length = 0x1000);

    // Description: convert a block number into a true block number, where the first block is the first hash table
    u32 ComputeBackingDataBlockNumber(u32 blockNum);

    // Description: get a block's hash entry
    HashEntry GetBlockHashEntry(u32 blockNum);

    // Description: get the true block number for the hash table that hashes the block at the level passed in
    u32 ComputeLevelNBackingHashBlockNumber(u32 blockNum, Level level);

    // Description: get the true block number for the hash table that hashes the block at level 0
    u32 ComputeLevel0BackingHashBlockNumber(u32 blockNum);

    // Description: get the true block number for the hash table that hashes the block at level 1 (female)
    u32 ComputeLevel1BackingHashBlockNumber(u32 blockNum);

    // Description: get the true block number for the hash table that hashes the block at level 2
    u32 ComputeLevel2BackingHashBlockNumber(u32 blockNum);

    // Descrption: get the address of a hash table in the package
    u32 GetHashTableAddress(u32 index, Level lvl);

    // Description: get the base address of a hash table in the package
    u32 GetBaseHashTableAddress(u32 index, Level lvl);

    // Description: get the amount of hash entries in a hash table
    u32 GetHashTableEntryCount(u32 index, Level lvl);

    // Description: get the hash table at the current index on the specified level
    HashTable GetLevelNHashTable(u32 index, Level lvl);

    // Description: build the table in memory for preperation to Write
    void BuildTableInMemory(HashTable *table, u8 *outBuffer);

    // Description: add the file entry to the file listing
    void AddToListing(StfsFileListing *fullListing, StfsFileListing *out);

    // Description: get the file entry from a file's path
    void GetFileEntry(vector<string> locationOfFile, StfsFileListing *start, StfsFileEntry *out,
            StfsFileEntry *newEntry = NULL, bool updateEntry = false, bool checkFolders = false,
            bool useWildcards = true);

    // Description: get the raw file listing
    void GenerateRawFileListing(StfsFileListing *in, vector<StfsFileEntry> *outFiles,
            vector<StfsFileEntry> *outFolders);

    // Description: split a string into multiple substrings
    vector<string> SplitString(string str, string delimeter);

    // Description: get the hash address in the parent hash table to the corresponding table
    u32 GetTableHashAddress(u32 index, Level lvl);

    // Description: set the status of the block in the corresponding hash table
    void SetBlockStatus(u32 blockNum, BlockStatusLevelZero status);

    // Description: set the next block of the current block
    void SetNextBlock(u32 blockNum, u32 nextBlockNum);

    // Description: discard the current file listing and reWrite it
    void WriteFileListing(bool usePassed = false, vector<StfsFileEntry> *outFis = NULL,
            vector<StfsFileEntry> *outFos = NULL);

    // Description: Write a file entry at the io's current position
    void WriteFileEntry(StfsFileEntry *entry);

    // Description: allocate a data block in the package, and return a block number
    u32 AllocateBlock();

    // Description: allocate 'blockCount' consecutive data blocks
    u32 AllocateBlocks(u32 blockCount);

    // Description: calculate the level of the topmost hash table
    Level CalcualateTopLevel();

    // Description: get the file listing that matches the directory path
    void FindDirectoryListing(vector<string> locationOfDirectory, StfsFileListing *start,
            StfsFileListing **out, bool useWildcards = true);

    // Description: update the entry at the given path
    void UpdateEntry(string pathInPackage, StfsFileEntry entry);

    // Description: find the cached entry that owns the given file's first block, or null
    StfsFileEntry* FindCachedFileEntry(StfsFileListing *listing, const StfsFileEntry &entry);

    // Description: get the number of bytes to skip over the hash table
    u32 GetHashTableSkipSize(u32 tableAddress);

    // Description: get the number of blocks until the next hash table
    u32 GetBlocksUntilNextHashTable(u32 currentBlock);

    // Description: parse the file
    void Parse();

    // Description: initializes the object
    void Init();

    // Description: close the io/cleanup resources
    void Cleanup();

        friend class StfsIO;
};


