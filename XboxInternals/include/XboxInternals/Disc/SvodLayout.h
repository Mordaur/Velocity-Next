#pragma once

// SVOD/GOD block layout, verified against the devkit kernel
// (SvodCreateDevice, SvodRead, SvodReadBackingBlocks).
//
// A part is 41,616 physical 4 KiB blocks:
//   block 0                       master hash block (204 x 20-byte SHA-1)
//   block 1 + 205*g               level-0 hash block for data group g
//   block 2..205, 207..410, ...   204 data blocks per group
// Data blocks per part: 203 groups x 204 = 41,412 (0xA1C4).
// Physical blocks per part: 1 + 203 * 205 = 41,616 (0xA290).
//
// Data block b of a part starts at (b + b/204 + 2) * 0x1000. The hash blocks
// never move; "enhanced" only changes which data block a GDF volume sector
// selects:
//
//   SvodCreateDevice  bits 0..5 of the descriptor flags must be zero and bit 6
//                     (0x40) selects the enhanced layout. Enhanced packages
//                     store dataBlockOffset as a big-endian 24-bit value;
//                     otherwise only its low byte is used.
//   SvodRead          skips dataBlockOffset blocks, adds one more block for
//                     enhanced packages, and aliases volume block 16 (the GDF
//                     volume descriptor) onto data block 0 when that block
//                     falls inside the skipped prefix. Reads below the data
//                     start are otherwise zero-filled.

#include <cstdint>

#include <XboxInternals/Stfs/StfsConstants.h>
#include <XboxInternals/TypeDefinitions.h>

namespace XboxInternals::SvodLayout {

constexpr uint64_t kDataBlocksPerGroup = 204;
constexpr uint64_t kGroupsPerPart = 203;
constexpr uint64_t kDataBlocksPerPart = kDataBlocksPerGroup * kGroupsPerPart; // 41,412
constexpr uint64_t kPhysicalBlocksPerPart = 1 + kGroupsPerPart * (1 + kDataBlocksPerGroup);
constexpr uint64_t kSectorsPerPart = kDataBlocksPerPart * 2;                  // 0x14388
constexpr uint64_t kFullPartBytes = kPhysicalBlocksPerPart * 0x1000;          // 0xA290000
constexpr uint64_t kGroupStrideBytes = 0x1000 + kDataBlocksPerGroup * 0x1000; // 0xCD000

// Bit 6 of the SVOD volume descriptor flags (SvodCreateDevice). The kernel
// rejects any of bits 0..5, so 0x80 alone does not mean enhanced (see
// SVODFeatures::houldBeZeroForDownLevelClients).
constexpr uint8_t kEnhancedFlag = EnhancedGDFLayout;

// GDF volume descriptor block (volume block 16, byte 0x10000).
constexpr DWORD kGdfHeaderVolumeBlock = 16;

inline bool IsEnhanced(uint8_t flags)
{
    return (flags & kEnhancedFlag) != 0;
}

// SvodCreateDevice reads the offset as one byte unless the package is
// enhanced, where it is a big-endian 24-bit value.
inline DWORD DataBlockOffset(DWORD stored, bool enhanced)
{
    return enhanced ? stored : (stored & 0xFF);
}

// SvodReadBackingBlocks, mode 0: part-relative byte offset of a data block.
// The enhanced shift selects a different data block; it never moves this one.
inline uint64_t DataBlockAddress(DWORD dataBlock)
{
    return (static_cast<uint64_t>(dataBlock) + dataBlock / kDataBlocksPerGroup + 2) * 0x1000;
}

// Data block that holds the GDF volume descriptor. Returns false when a legacy
// package skips far enough that the descriptor is zero-filled.
inline bool GdfHeaderBlock(DWORD dataBlockOffset, bool enhanced, DWORD *dataBlock)
{
    if (enhanced)
    {
        // One extra block of shift, then the block-16 alias; both give 0 once
        // the skipped prefix covers volume block 16.
        *dataBlock = dataBlockOffset >= 17 ? 0 : 17 - dataBlockOffset;
        return true;
    }

    if (dataBlockOffset > kGdfHeaderVolumeBlock)
        return false;

    *dataBlock = kGdfHeaderVolumeBlock - dataBlockOffset;
    return true;
}

// Map a logical GDF sector to (offset in part, part index). Returns false for a
// sector the kernel would zero-fill (below dataBlockOffset, or outside the
// part).
inline bool Map(DWORD sector, DWORD dataBlockOffset, bool enhanced,
                DWORD *addressInDataFile, DWORD *dataFileIndex)
{
    const uint64_t offsetSectors = static_cast<uint64_t>(dataBlockOffset) * 2;
    const uint64_t logical = static_cast<uint64_t>(sector);

    uint64_t dataBlock = 0;
    uint64_t inBlock = 0;

    if (logical < offsetSectors)
    {
        // The skipped prefix reads as zeroes, except that volume block 16 is
        // aliased onto data block 0 (SvodRead). That only happens when the
        // prefix is longer than 16 blocks.
        if (!enhanced || logical / 2 != kGdfHeaderVolumeBlock)
            return false;

        dataBlock = 0;
        inBlock = logical % 2;
    }
    else
    {
        const uint64_t relative = logical - offsetSectors;

        // SvodRead adds the enhanced block before the index is split across
        // parts, so the shift is global, not per part.
        dataBlock = relative / 2 + (enhanced ? 1 : 0);
        inBlock = relative % 2;
    }

    const uint64_t part = dataBlock / kDataBlocksPerPart;
    const uint64_t within = dataBlock % kDataBlocksPerPart;

    const uint64_t address = DataBlockAddress(static_cast<DWORD>(within)) + inBlock * 0x800;
    if (address >= kFullPartBytes)
        return false;

    if (addressInDataFile)
        *addressInDataFile = static_cast<DWORD>(address);
    if (dataFileIndex)
        *dataFileIndex = static_cast<DWORD>(part);

    return true;
}

// Number of bytes that can be read contiguously from a data-block address
// before the next level-0 hash block. Returns 0 for an address that is not
// inside a data block.
inline uint64_t BytesUntilHashBlock(uint64_t address)
{
    const uint64_t block = address / 0x1000;
    if (block < 2)
        return 0;

    const uint64_t group = (block - 2) / (kDataBlocksPerGroup + 1);
    const uint64_t blockInGroup = (block - 2) % (kDataBlocksPerGroup + 1);
    if (blockInGroup >= kDataBlocksPerGroup)
        return 0;

    const uint64_t groupStart = (group * (kDataBlocksPerGroup + 1) + 2) * 0x1000;
    return kDataBlocksPerGroup * 0x1000 - (address - groupStart);
}

} // namespace XboxInternals::SvodLayout
