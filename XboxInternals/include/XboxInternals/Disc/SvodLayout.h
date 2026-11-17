#pragma once

// SVOD/GOD block layout, verified against the devkit kernel
// (SvodReadBackingBlocks, SvodMapBlock, SvodMapNewBlock).
//
// A part is 41,616 physical 4 KiB blocks:
//   block 0                       master hash block (204 x 20-byte SHA-1)
//   block 1 + 205*g               level-0 hash block for data group g
//   block 2..205, 207..410, ...   204 data blocks per group
// Data blocks per part: 203 groups x 204 = 41,412 (0xA1C4).
// Physical blocks per part: 1 + 203 * 205 = 41,616 (0xA290).

#include <cstdint>

#include <XboxInternals/TypeDefinitions.h>

namespace XboxInternals::SvodLayout {

constexpr uint64_t kDataBlocksPerGroup = 204;
constexpr uint64_t kGroupsPerPart = 203;
constexpr uint64_t kDataBlocksPerPart = kDataBlocksPerGroup * kGroupsPerPart; // 41,412
constexpr uint64_t kPhysicalBlocksPerPart = 1 + kGroupsPerPart * (1 + kDataBlocksPerGroup);
constexpr uint64_t kSectorsPerPart = kDataBlocksPerPart * 2;                  // 0x14388
constexpr uint64_t kFullPartBytes = kPhysicalBlocksPerPart * 0x1000;          // 0xA290000
constexpr uint64_t kGroupStrideBytes = 0x1000 + kDataBlocksPerGroup * 0x1000; // 0xCD000

// The GDF volume starts at data block 0 for legacy packages and data block 1
// for enhanced ones (the kernel aliases virtual block 16 onto data block 0).
inline uint64_t DataStartBytes(bool enhanced)
{
    return enhanced ? 0x3000 : 0x2000;
}

// Map a logical GDF sector to (offset in part, part index). Returns false for a
// sector below dataBlockOffset, or one whose address leaves the part.
inline bool Map(DWORD sector, DWORD dataBlockOffset, bool enhanced,
                DWORD *addressInDataFile, DWORD *dataFileIndex)
{
    const uint64_t offsetSectors = static_cast<uint64_t>(dataBlockOffset) * 2;
    const uint64_t logical = static_cast<uint64_t>(sector);

    if (logical < offsetSectors)
        return false;

    const uint64_t relative = logical - offsetSectors;
    const uint64_t part = relative / kSectorsPerPart;
    const uint64_t within = relative % kSectorsPerPart;
    const uint64_t dataBlock = within / 2;
    const uint64_t group = dataBlock / kDataBlocksPerGroup;

    const uint64_t address = within * 0x800 +
            (enhanced ? 0x2000 : 0x1000) + (group + 1) * 0x1000;

    if (address >= kFullPartBytes)
        return false;

    if (addressInDataFile)
        *addressInDataFile = static_cast<DWORD>(address);
    if (dataFileIndex)
        *dataFileIndex = static_cast<DWORD>(part);

    return true;
}

} // namespace XboxInternals::SvodLayout
