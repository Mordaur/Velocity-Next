/* Most parts of this class were originally developed by Lander Griffith (https://github.com/landr0id/).
   Much of his code is used throughout this class or very slightly modified */

#ifndef FATXCONSTANTS_H
#define FATXCONSTANTS_H

#include <XboxInternals/Types.h>

#include <XboxInternals/Stfs/StfsDefinitions.h>
#include <XboxInternals/Stfs/XContentHeader.h>

#include <deque>
#include <memory>
#include <vector>
#include <iostream>

#define FAT32 4
#define FAT16 2

#define FATX_MAGIC 0x58544146

#define FATX_ENTRY_SIZE 0x40
#define FATX_ENTRY_MAX_NAME_LENGTH 0x2A
#define FATX_ENTRY_DELETED 0xE5

#define FAT_SECTOR_SIZE 0x200

#define FATX_HEADER_SIZE 0x1000
#define FATX_SECTOR_SIZE 0x200

#define FAT_CLUSTER_AVAILABLE (u32)0x00000000
#define FAT_CLUSTER_RESERVED (u32)0xfffffff0
#define FAT_CLUSTER_LAST (u32)0xffffffff

#define FAT_CLUSTER16_AVAILABLE (u16)0x0000
#define FAT_CLUSTER16_RESERVED (u16)0xfff0
#define FAT_CLUSTER16_LAST (u16)0xffff

class FatxDrive;
struct Partition;

enum FatxDriveType
{
    FatxFlashDrive,
    FatxHarddrive
};

struct SecurityInfo
{
    std::string serialNumber;
    std::string firmwareRevision;
    std::string modelNumber;
    u8 msLogoHash[0x14];
    u32 userAddressableSectors;
    u8 rsaSignature[0x100];
    bool validSignature;

    u32 msLogoSize;
    std::vector<u8> msLogo;
};

struct FlashDriveConfigurationData
{
    // type1
    Certificate certificate;

    // type2
    u8 deviceSignature[0x100];

    u8 deviceID[0x14];
    u32 securityLength; // must be 0x228 or 0x100
    u64 deviceLength;
    u16 readSpeed;
    u16 WriteSpeed;
};

// One contiguous run of a file on the device. fileOffset is relative to the
// start of the file, deviceOffset is absolute in the drive.
struct FileExtent
{
    u64 fileOffset = 0;
    u64 deviceOffset = 0;
    u64 length = 0;
};

struct FatxFileEntry;

// Stable-address list of child entries. A plain std::deque<FatxFileEntry>
// member of FatxFileEntry is rejected by libc++ (incomplete value type), so
// the deque lives behind a pointer. The pointer is never invalidated by
// push_back and the container is deep-copied when the entry is copied.
class FatxFileEntryList
{
public:
    FatxFileEntryList();
    FatxFileEntryList(const FatxFileEntryList &other);
    FatxFileEntryList &operator=(const FatxFileEntryList &other);
    FatxFileEntryList(FatxFileEntryList &&other) noexcept;
    FatxFileEntryList &operator=(FatxFileEntryList &&other) noexcept;
    ~FatxFileEntryList();

    size_t size() const;
    bool empty() const;
    void clear();

    void push_back(const FatxFileEntry &entry);
    void push_back(FatxFileEntry &&entry);

    FatxFileEntry &operator[](size_t index);
    const FatxFileEntry &operator[](size_t index) const;
    FatxFileEntry &at(size_t index);
    const FatxFileEntry &at(size_t index) const;

private:
    std::unique_ptr<std::deque<FatxFileEntry>> entries;
    void ensure();
};

struct FatxFileEntry
{
    Partition *partition = nullptr;

    u8 nameLen = 0;
    u8 fileAttributes = 0;

    std::string name;
    u32 startingCluster = 0;
    u32 fileSize = 0;

    // times
    u32 creationDate = 0;
    u32 lastWriteDate = 0;
    u32 lastAccessDate = 0;

    bool readDirectories = false;
    i64 address = 0;
    u32 magic = 0;
    FileSystem fileSystem = FileSystemSTFS;
    // std::deque, not std::vector: callers (and the GUI tree) hold raw
    // FatxFileEntry* for children. push_back on a deque never invalidates
    // references to existing elements, while a vector reallocation freed every
    // sibling pointer handed out earlier.
    FatxFileEntryList cachedFiles;
    std::vector<u32> clusterChain;
    std::string path;
};

inline FatxFileEntryList::FatxFileEntryList() = default;
inline FatxFileEntryList::~FatxFileEntryList() = default;
inline FatxFileEntryList::FatxFileEntryList(FatxFileEntryList &&other) noexcept = default;
inline FatxFileEntryList &FatxFileEntryList::operator=(FatxFileEntryList &&other) noexcept = default;

inline FatxFileEntryList::FatxFileEntryList(const FatxFileEntryList &other)
{
    if (other.entries)
        entries = std::make_unique<std::deque<FatxFileEntry>>(*other.entries);
}

inline FatxFileEntryList &FatxFileEntryList::operator=(const FatxFileEntryList &other)
{
    if (this == &other)
        return *this;

    if (other.entries)
        entries = std::make_unique<std::deque<FatxFileEntry>>(*other.entries);
    else
        entries.reset();

    return *this;
}

inline size_t FatxFileEntryList::size() const
{
    return entries ? entries->size() : 0;
}

inline bool FatxFileEntryList::empty() const
{
    return !entries || entries->empty();
}

inline void FatxFileEntryList::clear()
{
    if (entries)
        entries->clear();
}

inline void FatxFileEntryList::ensure()
{
    if (!entries)
        entries = std::make_unique<std::deque<FatxFileEntry>>();
}

inline void FatxFileEntryList::push_back(const FatxFileEntry &entry)
{
    ensure();
    entries->push_back(entry);
}

inline void FatxFileEntryList::push_back(FatxFileEntry &&entry)
{
    ensure();
    entries->push_back(std::move(entry));
}

inline FatxFileEntry &FatxFileEntryList::operator[](size_t index)
{
    ensure();
    return (*entries)[index];
}

inline const FatxFileEntry &FatxFileEntryList::operator[](size_t index) const
{
    return (*entries)[index];
}

inline FatxFileEntry &FatxFileEntryList::at(size_t index)
{
    ensure();
    return entries->at(index);
}

inline const FatxFileEntry &FatxFileEntryList::at(size_t index) const
{
    return entries->at(index);
}

struct Partition
{
    std::string name;
    i64 address;
    u64 size;
    FatxDrive *drive;

    // header
    u32 magic;
    u32 partitionId;
    u32 sectorsPerCluster;
    u32 rootDirectoryCluster;

    // partition entries
    FatxFileEntry root;

    // calculated information
    u32 lastFreeClusterFound;
    u8 clusterEntrySize;
    u32 clusterCount;
    u32 clusterSize;
    u64 clusterStartingAddress;
    u64 chainmapSize;
    u64 freeMemory;
    std::vector<u32> freeClusters;
};

// Copyable snapshot of a partition's metadata. Consumers such as dialogs and
// tools own this by value, so no raw Partition pointers leave the IO layer and
// nothing can dangle when a drive is reloaded or a temporary vector dies.
struct PartitionInfo
{
    std::string name;
    u32 partitionId = 0;
    u32 sectorsPerCluster = 0;
    u32 rootDirectoryCluster = 0;
    u32 clusterCount = 0;
    u32 clusterSize = 0;
    i64 address = 0;
    u64 size = 0;
    u64 clusterStartingAddress = 0;
    u64 freeMemory = 0;

    // Byte offset of a cluster inside the partition; FATX clusters start at 1.
    u64 ClusterToOffset(u32 cluster) const
    {
        return clusterStartingAddress + static_cast<u64>(clusterSize) * (cluster - 1);
    }
};

enum FatxDirentAttributes
{
    FatxReadOnly = 0x01,
    FatxHidden = 0x02,
    FatxSystem = 0x04,
    FatxDirectory = 0x10,
    FatxArchive = 0x20,
    FatxDevice = 0x40,
    FatxNormal = 0x80
};

struct HddSizes
{
    static const i64 SystemCache = 0x80000000;
    static const i64 GameCache = 0xA0E30000;
    static const i64 SystemPartition = 0x10000000;
    static const i64 SystemAuxiliary = 0xCE30000;
    static const i64 SystemExtended = 0x8000000;
};

struct HddOffsets
{
    static const i64 Data = 0x130EB0000;
    static const i64 Josh = 0x800;
    static const i64 SecuritySector = 0x2000;
    static const i64 SystemCache = 0x80000;
    static const i64 GameCache = 0x80080000;
    static const i64 SystemAuxiliary = 0x10C080000;
    static const i64 SystemExtended = 0x118EB0000;
    static const i64 SystemPartition = 0x120EB0000;
};

struct UsbOffsets
{
    static const i64 SystemAuxiliary = 0x8115200;
    static const i64 SystemExtended = 0x12000400;
    static const i64 SystemCache = 0x8000400;
    static const i64 Data = 0x20000000;
};

struct UsbSizes
{
    static const i64 CacheNoSystem = 0x4000000;
    static const i64 SystemCache = 0x47FF000;
    static const i64 SystemAuxiliary = 0x8000000;
    static const i64 SystemExtended = 0xDFFFC00;
};

#endif // FATXCONSTANTS_H
