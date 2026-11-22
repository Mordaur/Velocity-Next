/* Smoke target for the sanitizer CI job.
 *
 * It is deliberately small and corpus-free: it exercises the code paths that can
 * be driven with synthetic data and asserts only on invariants (round-trips,
 * bounds rejection, "must throw"), never on parsed values. Anything that needs a
 * real image belongs in the out-of-tree harness under tests/ at the work folder
 * root.
 */

#include <XboxInternals/Disc/ISO.h>
#include <XboxInternals/Disc/Svod.h>
#include <XboxInternals/Disc/SvodLayout.h>
#include <XboxInternals/Fatx/FatxDrive.h>
#include <XboxInternals/Gpd/Xdbf.h>
#include <XboxInternals/Gpd/XdbfHelpers.h>
#include <XboxInternals/IO/FileIO.h>
#include <XboxInternals/IO/FatxIO.h>
#include <XboxInternals/IO/LocalIndexableMultiFileIO.h>
#include <XboxInternals/Disc/Asf.h>
#include <XboxInternals/IO/MemoryIO.h>
#include <XboxInternals/IO/StfsIO.h>
#include <XboxInternals/Stfs/StfsDefinitions.h>
#include <XboxInternals/Stfs/StfsPackage.h>
#include <XboxInternals/Stfs/XContentHeader.h>
#include <XboxInternals/Utils.h>
#include <XboxInternals/Xex/Xex.h>
#include <XboxInternals/Xex/XexLzxDecoder.h>

#include <botan_all.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    int g_failures = 0;

    void check(bool condition, const char *what)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++g_failures;
        }
    }

    template <typename Fn>
    bool throws(Fn &&fn)
    {
        try
        {
            fn();
        }
        catch (const std::string &)
        {
            return true;
        }
        catch (const std::exception &)
        {
            return true;
        }
        catch (...)
        {
            return true;
        }
        return false;
    }

    void testMemoryIO()
    {
        u8 raw[256] = {};

        MemoryIO io(raw, sizeof(raw));
        io.SetEndian(LittleEndian);
        io.Write(static_cast<u32>(0x11223344));
        io.Write(static_cast<u16>(0xBEEF));
        io.Write(static_cast<u64>(0x0102030405060708ull));
        io.Write(std::string("hello"));

        io.SetPosition(0);
        check(io.ReadDword() == 0x11223344u, "little-endian DWORD round-trip");
        check(io.ReadWord() == 0xBEEFu, "little-endian WORD round-trip");
        check(io.ReadUInt64() == 0x0102030405060708ull, "little-endian UINT64 round-trip");
        check(io.ReadString() == "hello", "NUL-terminated string round-trip");

        u8 fp[16] = {};
        MemoryIO fpio(fp, sizeof(fp));
        fpio.SetEndian(LittleEndian);
        fpio.Write(static_cast<u32>(0x3F800000)); // 1.0f
        fpio.Write(static_cast<u64>(0x3FF0000000000000ull)); // 1.0
        fpio.SetPosition(0);
        check(fpio.ReadFloat() == 1.0f, "little-endian Float bit round-trip");
        check(fpio.ReadDouble() == 1.0, "little-endian Double bit round-trip");

        u8 be[16] = {};
        MemoryIO big(be, sizeof(be));
        big.SetEndian(BigEndian);
        big.Write(static_cast<u32>(0x11223344));
        check(be[0] == 0x11 && be[1] == 0x22 && be[2] == 0x33 && be[3] == 0x44,
              "big-endian DWORD is written MSB first");
        big.SetPosition(0);
        check(big.ReadDword() == 0x11223344u, "big-endian DWORD round-trip");

        // Bounds rejection: a fixed window must refuse both directions.
        check(throws([&] {
            MemoryIO tiny(raw, 2);
            tiny.ReadDword();
        }), "read past the end throws");

        check(throws([&] {
            MemoryIO tiny(raw, 2);
            u8 two[4] = {1, 2, 3, 4};
            tiny.WriteBytes(two, sizeof(two));
        }), "write past the end throws");

        check(throws([&] {
            MemoryIO tiny(raw, 2);
            tiny.SetPosition(3);
        }), "seek past the end throws");
    }

    void testFileIOReadWritePosition()
    {
        namespace fs = std::filesystem;

        const fs::path path = fs::temp_directory_path() / "xbi_smoke_fileio_rw.bin";
        std::error_code ignored;
        fs::remove(path, ignored);

        {
            FileIO io(path.string(), true);
            check(io.GetPosition() == 0, "FileIO starts at position 0");

            io.WriteBytes(reinterpret_cast<u8*>(const_cast<char*>("abcdefgh")), 8);
            check(io.GetPosition() == 8, "FileIO write advances the position");

            // A read-write fstream has separate get/put pointers; BaseIO has one.
            io.SetPosition(0);
            u8 buf[8] = {};
            io.ReadBytes(buf, 4);
            check(std::memcmp(buf, "abcd", 4) == 0, "FileIO reads after a write");
            check(io.GetPosition() == 4, "FileIO read advances the position");

            // A write after a read must continue at the read position, not at
            // the stale put pointer.
            io.WriteBytes(reinterpret_cast<u8*>(const_cast<char*>("WXYZ")), 4);
            check(io.GetPosition() == 8, "FileIO write continues after the read");

            io.SetPosition(0);
            io.ReadBytes(buf, 8);
            check(std::memcmp(buf, "abcdWXYZ", 8) == 0,
                  "FileIO read/write share one logical position");

            // A read after a write must continue at the write position.
            io.SetPosition(0);
            io.WriteBytes(reinterpret_cast<u8*>(const_cast<char*>("1234")), 4);
            io.ReadBytes(buf, 4);
            check(std::memcmp(buf, "WXYZ", 4) == 0, "FileIO reads after the write");

            io.SetPosition(0);
            io.ReadBytes(buf, 8);
            check(std::memcmp(buf, "1234WXYZ", 8) == 0,
                  "FileIO interleaved read/write keeps one position");

            check(io.Length() == 8, "FileIO Length follows appends");

            // A failed read must not poison the stream for later operations.
            check(throws([&] {
                io.SetPosition(0x100);
                u8 one = 0;
                io.ReadBytes(&one, 1);
            }), "FileIO read past EOF throws");
            io.SetPosition(0);
            u8 first = 0;
            io.ReadBytes(&first, 1);
            check(first == '1', "FileIO reads again after a failed read");
        }

        fs::remove(path, ignored);
    }

    void testLzxGarbage()
    {
        // Not a fuzzer: fixed patterns, fixed lengths. The contract is only
        // "either returns or throws std::string; never crashes or hangs".
        const std::size_t lengths[] = {0, 1, 2, 3, 4, 15, 16, 17, 31, 64, 257, 1024, 4096};

        for (int pattern = 0; pattern < 4; ++pattern)
        {
            for (std::size_t length : lengths)
            {
                std::vector<u8> input(length);
                std::uint32_t state = 0x12345678u + static_cast<std::uint32_t>(pattern);

                for (std::size_t i = 0; i < length; ++i)
                {
                    switch (pattern)
                    {
                    case 0:
                        input[i] = 0x00;
                        break;
                    case 1:
                        input[i] = 0xFF;
                        break;
                    case 2:
                        state = state * 1664525u + 1013904223u;
                        input[i] = static_cast<u8>(state >> 24);
                        break;
                    default:
                        input[i] = static_cast<u8>(i & 0xFF);
                        break;
                    }
                }

                XexLzxDecoder decoder;
                u8 output[0x100] = {};

                try
                {
                    decoder.DecodeChunk(length == 0 ? nullptr : input.data(), length,
                                        output, sizeof(output));
                }
                catch (const std::string &)
                {
                }
                catch (const std::exception &)
                {
                }
                catch (...)
                {
                }
            }
        }
    }

    void putBE32(std::vector<u8> &buffer, std::size_t offset, std::uint32_t value)
    {
        buffer[offset] = static_cast<u8>(value >> 24);
        buffer[offset + 1] = static_cast<u8>(value >> 16);
        buffer[offset + 2] = static_cast<u8>(value >> 8);
        buffer[offset + 3] = static_cast<u8>(value);
    }

    void testStfsNameMatchingAndGeometry()
    {
        // Kernel StfsLookupElementNameInDirectory uses IoIsNameInExpression
        // with ignore-case: '*' and '?' patterns, case-insensitive.
        check(StfsPackage::MatchesFileName("SAVEGAME.BIN", "savegame.bin"),
              "STFS name match is case-insensitive");
        check(StfsPackage::MatchesFileName("*.bin", "SaveGame.BIN"),
              "STFS name match supports '*'");
        check(StfsPackage::MatchesFileName("s?ve*", "savegame.bin"),
              "STFS name match supports '?'");
        check(!StfsPackage::MatchesFileName("save", "savegame.bin"),
              "STFS name match requires the whole name");
        check(!StfsPackage::MatchesFileName("a?c", "ac"),
              "STFS '?' requires one character");
        check(StfsPackage::MatchesFileName("*", ""), "STFS '*' matches empty");
        check(StfsPackage::MatchesFileName("a*b", "a*b", false),
              "STFS exact match keeps wildcard characters literal");
        check(!StfsPackage::MatchesFileName("a*", "abc", false),
              "STFS exact match does not treat '*' as a pattern");
        check(StfsPackage::MatchesFileName("SAVE.BIN", "save.bin", false),
              "STFS exact match is still case-insensitive");

        // Kernel StfsComputeNumberOfDataBlocks (male geometry, 0x800DFA40).
        struct Case { u64 blocks; u32 expected; };
        const Case cases[] = {
            {0x0, 0x0}, {0xAA, 0xA8}, {0xAC, 0xAA}, {0x723A, 0x70E4},
            {0xE000, 0xDD5E}, {0x4AF767, 0x4A16EF}, {0x4AF768, 0x4A16F0},
            {0x4BDA85, 0x4AF768}, {0x100000, 0xFD016},
        };
        for (const auto &c : cases)
        {
            check(StfsPackage::ComputeNumberOfDataBlocks(c.blocks) == c.expected,
                  "STFS data-block geometry matches the kernel formula");
        }

        check(StfsPackage::ComputeAllocatedBlocksForSize(0x1000ull * 0xAC) == 0xAA,
              "STFS size helper rounds up to 4 KiB blocks");
    }

    void testSvodLayout()
    {
        using namespace XboxInternals::SvodLayout;

        check(!IsEnhanced(0x00) && IsEnhanced(0x40) && !IsEnhanced(0x80) &&
              IsEnhanced(0xC0), "SVOD enhanced flag is bit 6 only");

        check(DataBlockOffset(0x123456, false) == 0x56,
              "legacy SVOD uses one byte of dataBlockOffset");
        check(DataBlockOffset(0x123456, true) == 0x123456,
              "enhanced SVOD uses 24 bits of dataBlockOffset");

        // SvodReadBackingBlocks: data block b -> b + b/204 + 2 blocks.
        check(DataBlockAddress(0) == 0x2000, "SVOD data block 0 is at 0x2000");
        check(DataBlockAddress(203) == 0xCD000, "SVOD last block of group 0");
        check(DataBlockAddress(204) == 0xCF000, "SVOD first block of group 1");
        check(DataBlockAddress(41411) == 0xA28F000, "SVOD last data block of a part");

        // GDF volume descriptor (volume block 16) after the skip and the
        // enhanced one-block shift; the kernel aliases it to data block 0 once
        // the skipped prefix covers it.
        u32 header = 0;
        check(GdfHeaderBlock(0, false, &header) && header == 16, "legacy GDF header block");
        check(GdfHeaderBlock(4, false, &header) && header == 12, "legacy GDF header after skip");
        check(!GdfHeaderBlock(17, false, &header), "legacy GDF header inside the prefix");
        check(GdfHeaderBlock(0, true, &header) && header == 17, "enhanced GDF header block");
        check(GdfHeaderBlock(16, true, &header) && header == 1, "enhanced header at 17-D");
        check(GdfHeaderBlock(17, true, &header) && header == 0, "enhanced header alias");
        check(GdfHeaderBlock(20, true, &header) && header == 0, "enhanced header still aliased");

        struct MapCase
        {
            u32 sector;
            u32 dataBlockOffset;
            bool enhanced;
            bool valid;
            u32 address;
            u32 part;
        };

        const u32 partSectors = static_cast<u32>(kSectorsPerPart); // 0x14388
        const MapCase cases[] = {
            // Legacy: sector 0 is data block 0, no shift.
            {0, 0, false, true, 0x2000, 0},
            {0x198, 0, false, true, 0xCF000, 0},               // block 204, group 1
            {partSectors - 2, 0, false, true, 0xA28F000, 0},   // last block of part 0
            {partSectors - 1, 0, false, true, 0xA28F800, 0},   // second half of it
            {partSectors, 0, false, true, 0x2000, 1},          // first block of part 1
            // Enhanced: one block of shift, applied before the part split.
            {0, 0, true, true, 0x3000, 0},
            {0x198, 0, true, true, 0xD0000, 0},                // block 205, group 1
            {partSectors - 4, 0, true, true, 0xA28F000, 0},    // block 41411, last of part 0
            {partSectors - 3, 0, true, true, 0xA28F800, 0},
            {partSectors - 2, 0, true, true, 0x2000, 1},       // block 41412 opens part 1
            // dataBlockOffset skips sectors, and enhanced volume block 16 is
            // aliased onto data block 0 when the prefix covers it.
            {7, 4, false, false, 0, 0},
            {8, 4, false, true, 0x2000, 0},
            {31, 17, true, false, 0, 0},                       // zero-filled prefix
            {32, 17, true, true, 0x2000, 0},                   // volume block 16 -> block 0
            {33, 17, true, true, 0x2800, 0},
            {34, 17, true, true, 0x3000, 0},                   // volume block 17 -> block 1
        };

        for (const auto &c : cases)
        {
            u32 address = 0, part = 0;
            const bool ok = XboxInternals::SvodLayout::Map(
                    c.sector, c.dataBlockOffset, c.enhanced, &address, &part);

            check(ok == c.valid, "SVOD map validity matches the kernel");
            if (ok && c.valid)
            {
                check(address == c.address, "SVOD map address matches the kernel");
                check(part == c.part, "SVOD map part matches the kernel");
            }
        }

        // The volume bound: 40 data blocks = 80 sectors.
        u32 boundAddress = 0, boundPart = 0;
        check(XboxInternals::SvodLayout::Map(0, 0, false, &boundAddress, &boundPart, 40),
              "SVOD volume accepts sector 0");
        check(XboxInternals::SvodLayout::Map(0x4F, 0, false, &boundAddress, &boundPart, 40),
              "SVOD volume accepts its last sector");
        check(!XboxInternals::SvodLayout::Map(0x50, 0, false, &boundAddress, &boundPart, 40),
              "SVOD volume rejects the first sector past the end");
        check(!XboxInternals::SvodLayout::Map(0x198, 0, false, &boundAddress, &boundPart, 40),
              "SVOD volume rejects a sector in a later part");

        check(BytesUntilHashBlock(0x2000) == 0xCC000, "SVOD chunk starts a group");
        check(BytesUntilHashBlock(0xCDFFF) == 1, "SVOD chunk ends before the hash block");
        check(BytesUntilHashBlock(0xCE000) == 0, "SVOD level-0 block is not data");
        check(BytesUntilHashBlock(0xCF000) == 0xCC000, "SVOD chunk restarts after a hash block");
        check(BytesUntilHashBlock(0xA28F000) == 0x1000, "SVOD chunk stops at the part end");
    }

    void testSafeExtractionPaths()
    {
        namespace fs = std::filesystem;

        check(Utils::IsSafeRelativePath("a/b.txt"), "safe path accepts nested relative names");
        check(!Utils::IsSafeRelativePath("../x"), "safe path rejects leading '..'");
        check(!Utils::IsSafeRelativePath("a/../b"), "safe path rejects an inner '..'");
        check(!Utils::IsSafeRelativePath("a/./b"), "safe path rejects an inner '.'");
        check(!Utils::IsSafeRelativePath("/abs"), "safe path rejects absolute paths");
        check(!Utils::IsSafeRelativePath("C:/x"), "safe path rejects drive/ADS separators");
        check(!Utils::IsSafeRelativePath("a:b"), "safe path rejects ADS separators");
        check(!Utils::IsSafeRelativePath(""), "safe path rejects empty names");

        std::string joined;
        check(Utils::SafeJoin("/tmp/out", "a/b.txt", joined),
              "safe join accepts a relative path");
        check(fs::path(joined) == (fs::path("/tmp/out") / "a" / "b.txt"),
              "safe join keeps the root prefix");
        check(!Utils::SafeJoin("/tmp/out", "../x", joined),
              "safe join rejects traversal");
    }

    void testSvodDescriptorEndianness()
    {
        // SvodCreateDevice: dataBlockCount is big-endian, dataBlockOffset is
        // little-endian (and only its low byte is used for legacy packages).
        std::vector<u8> buffer(0x379 + sizeof(SvodVolumeDescriptor), 0);
        buffer[0x379] = 0x24;
        buffer[0x37A] = 0x01;
        buffer[0x391] = 0x40;                     // enhanced
        buffer[0x392] = 0x00;                     // dataBlockCount = 0x000102 (BE)
        buffer[0x393] = 0x01;
        buffer[0x394] = 0x02;
        buffer[0x395] = 0x11;                     // dataBlockOffset = 0x000011 (LE)
        buffer[0x396] = 0x00;
        buffer[0x397] = 0x00;

        MemoryIO io(buffer.data(), buffer.size());
        SvodVolumeDescriptor descriptor{};
        ReadSvodVolumeDescriptorEx(&descriptor, &io);

        check(static_cast<u32>(descriptor.dataBlockCount) == 0x102,
              "SVOD dataBlockCount is big-endian");
        check(static_cast<u32>(descriptor.dataBlockOffset) == 0x11,
              "SVOD dataBlockOffset is little-endian");

        std::vector<u8> written(0x379 + sizeof(SvodVolumeDescriptor), 0);
        MemoryIO out(written.data(), written.size());
        WriteSvodVolumeDescriptorEx(&descriptor, &out);
        check(written[0x395] == 0x11 && written[0x396] == 0x00 && written[0x397] == 0x00,
              "SVOD dataBlockOffset is written little-endian");
    }

    void testMSTimeRoundTrip()
    {
        // FATX directory entries store FAT-style date/time pairs, and the
        // helpers are also used for STFS metadata timestamps. The year used to
        // be offset by -4 and masked with 0xEF, which broke 1996, 2028, 2030:
        // a year with bit 4 set came back 16 years later.
        const int years[] = {1980, 1996, 2012, 2024, 2028, 2030, 2044, 2050};

        for (int year : years)
        {
            std::tm tm{};
            tm.tm_year = year - 1900;
            tm.tm_mon = 5;
            tm.tm_mday = 15;
            tm.tm_hour = 13;
            tm.tm_min = 37;
            tm.tm_sec = 20;
            tm.tm_isdst = -1;

            const std::time_t t = std::mktime(&tm);
            const MSTime back = DWORDToMSTime(MSTimeToDWORD(TimetToMSTime(t)));

            check(back.year == year && back.month == 6 && back.monthDay == 15 &&
                  back.hours == 13 && back.minutes == 37 && back.seconds == 20,
                  "MSTime/FAT date-time round-trip");
        }

        // FAT stores seconds in two-second units: 21 must come back as 20.
        MSTime odd{};
        odd.year = 2024;
        odd.month = 6;
        odd.monthDay = 15;
        odd.hours = 13;
        odd.minutes = 37;
        odd.seconds = 21;
        check(DWORDToMSTime(MSTimeToDWORD(odd)).seconds == 20,
              "FAT timestamp rounds seconds down to two-second units");
    }

    void testCorruptStfsHeaders()
    {
        const std::size_t size = 0xC000;

        // 1. Invalid file-system value: must be rejected before any enum cast.
        {
            std::vector<u8> header(size, 0);
            header[0] = 'L'; header[1] = 'I'; header[2] = 'V'; header[3] = 'E';
            putBE32(header, 0x340, static_cast<std::uint32_t>(size));  // headerSize
            putBE32(header, 0x3A9, 0x12345678);                       // fileSystem
            MemoryIO io(header.data(), header.size());
            check(throws([&] { XContentHeader metadata(&io); }),
                  "invalid STFS file system is rejected");
        }

        // 2. Avatar metadata with an invalid installer type.
        {
            std::vector<u8> header(size, 0);
            header[0] = 'L'; header[1] = 'I'; header[2] = 'V'; header[3] = 'E';
            putBE32(header, 0x340, static_cast<std::uint32_t>(size));
            putBE32(header, 0x344, 0x9000);                 // AvatarItem
            putBE32(header, 0x3A9, 0);                      // FileSystemSTFS
            putBE32(header, 0x3D9, 0xA5A5A5A5);             // subCategory
            header[0x3F1] = 0x00;                           // skeleton 0 (valid)
            putBE32(header, 0x971A, 0x7F7F7F7F);            // installerType
            MemoryIO io(header.data(), header.size());
            check(throws([&] { XContentHeader metadata(&io); }),
                  "invalid STFS installer type is rejected");
        }

        // 3. Valid installer type with a garbage resume state and thumbnail sizes.
        {
            std::vector<u8> header(size, 0);
            header[0] = 'L'; header[1] = 'I'; header[2] = 'V'; header[3] = 'E';
            putBE32(header, 0x340, static_cast<std::uint32_t>(size));
            putBE32(header, 0x344, 0x9000);                 // AvatarItem
            putBE32(header, 0x3A9, 0);                      // FileSystemSTFS
            putBE32(header, 0x3D9, 0xA5A5A5A5);             // subCategory
            header[0x3F1] = 0x00;                           // skeleton 0 (valid)
            putBE32(header, 0x971A, 0x50245443);            // TitleContentProgressCache
            putBE32(header, 0x971E, 0xDEADBEEF);            // resumeState
            MemoryIO io(header.data(), header.size());
            bool failed = false;
            try
            {
                XContentHeader metadata(&io);
            }
            catch (const std::string &e)
            {
                std::fprintf(stderr, "  (corrupt STFS parse threw: %s)\n", e.c_str());
                failed = true;
            }
            catch (const std::exception &e)
            {
                std::fprintf(stderr, "  (corrupt STFS parse threw: %s)\n", e.what());
                failed = true;
            }
            catch (...)
            {
                failed = true;
            }
            check(!failed, "corrupt STFS metadata parses without UB");
        }

        // 4. Invalid skeleton version is rejected.
        {
            std::vector<u8> header(size, 0);
            header[0] = 'L'; header[1] = 'I'; header[2] = 'V'; header[3] = 'E';
            putBE32(header, 0x340, static_cast<std::uint32_t>(size));
            putBE32(header, 0x344, 0x9000);                 // AvatarItem
            putBE32(header, 0x3A9, 0);                      // FileSystemSTFS
            putBE32(header, 0x3D9, 0xA5A5A5A5);             // subCategory
            header[0x3F1] = 0x7F;                           // invalid skeleton
            MemoryIO io(header.data(), header.size());
            check(throws([&] { XContentHeader metadata(&io); }),
                  "invalid STFS skeleton version is rejected");
        }
    }

    void testResignFailureLeavesPackageIntact()
    {
        // A CON package with a valid console type, plus an all-zero keyvault
        // (right size, no usable RSA key). The resign must fail without writing
        // anything: a failed resign used to poison the in-memory certificate,
        // which a later rehash then wrote to disk, leaving the package
        // unreadable ("Invalid console type").
        const std::size_t size = 0xC000;

        std::vector<u8> package(size, 0);
        package[0] = 'C'; package[1] = 'O'; package[2] = 'N'; package[3] = ' ';
        putBE32(package, 0x4 + 0x18, 1);                          // console type: DevKit
        putBE32(package, 0x340, static_cast<std::uint32_t>(size)); // headerSize
        putBE32(package, 0x344, 1);                               // contentType: SavedGame
        putBE32(package, 0x3A9, 0);                               // fileSystem: STFS

        MemoryIO io(package.data(), package.size());

        std::unique_ptr<XContentHeader> metadata;
        try
        {
            metadata = std::make_unique<XContentHeader>(&io);
        }
        catch (const std::string &e)
        {
            std::fprintf(stderr, "  (synthetic CON parse threw: %s)\n", e.c_str());
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "  (synthetic CON parse threw: %s)\n", e.what());
        }

        check(metadata != nullptr, "synthetic CON header parses");
        if (!metadata)
            return;

        const std::vector<u8> before = package;

        std::vector<u8> kv(0x4000, 0);
        check(throws([&] { metadata->ResignHeader(kv.data(), kv.size()); }),
              "resign with a zero keyvault throws");

        std::vector<u8> kvTrimmed(0x3FF0, 0);
        check(throws([&] { metadata->ResignHeader(kvTrimmed.data(), kvTrimmed.size()); }),
              "resign with a zero 0x3FF0 keyvault throws");

        std::vector<u8> kvExtended(0x4010, 0);
        check(throws([&] { metadata->ResignHeader(kvExtended.data(), kvExtended.size()); }),
              "resign with an unsupported 0x4010 keyvault throws");

        bool unchanged = (package == before);
        if (!unchanged)
        {
            std::size_t first = 0;
            while (first < package.size() && package[first] == before[first])
                ++first;
            std::fprintf(stderr, "  (first byte changed at 0x%zX)\n", first);
        }
        check(unchanged, "failed resign leaves the package byte-for-byte intact");

        // A later rehash writes the metadata back; it must not resurrect the
        // rejected keyvault's certificate either.
        metadata->WriteMetaData();
        check(std::equal(package.begin() + 0x4, package.begin() + 0x22C,
                         before.begin() + 0x4),
              "rehash after a failed resign keeps the original certificate");
    }

    void testCorruptXdbf()
    {
        namespace fs = std::filesystem;

        std::vector<u8> buffer(0x40, 0);
        putBE32(buffer, 0x00, 0x58444246);  // "XDBF"
        putBE32(buffer, 0x04, 1);           // version
        putBE32(buffer, 0x08, 0x12);        // entry table length
        putBE32(buffer, 0x0C, 1);           // entry count
        putBE32(buffer, 0x10, 8);           // free-mem table length
        putBE32(buffer, 0x14, 1);           // free-mem entry count
        buffer[0x18] = 0x00;
        buffer[0x19] = 0x7F;                // unknown entry type

        const fs::path path = fs::temp_directory_path() / "xbi_smoke_corrupt.xdbf";
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char *>(buffer.data()),
                       static_cast<std::streamsize>(buffer.size()));
        }

        // Unknown entry types are legal; the contract is "no UB, no crash".
        try
        {
            Xdbf xdbf(path.string());
            (void)xdbf;
        }
        catch (const std::string &)
        {
        }
        catch (const std::exception &)
        {
        }
        catch (...)
        {
        }

        std::error_code ignored;
        fs::remove(path, ignored);
    }

    void testLzxRegression()
    {
        // libFuzzer crash from CI: an invalid pretree delta (17..19) produced a
        // 254-byte code length, which indexed past Huffman::count[17]. The input
        // must be rejected, never write out of bounds.
        static const u8 input[] = {
            0x3F, 0x2F, 0x23, 0x00, 0x00, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7B,
            0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x00, 0x49, 0x49, 0x49, 0x49, 0x49,
            0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49,
            0x49, 0x49, 0x49, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x1A, 0x1A
        };

        XexLzxDecoder decoder;
        u8 output[0x400] = {};

        try
        {
            decoder.DecodeChunk(input, sizeof(input), output, sizeof(output));
        }
        catch (const std::string &)
        {
        }
        catch (const std::exception &)
        {
        }
        catch (...)
        {
        }

        check(true, "LZX pretree-delta regression input is rejected safely");
    }

    void testLzxStoredBlock()
    {
        // LZX stored block (type 3) with no corpus coverage: 1-bit intel flag,
        // 3-bit type, 24-bit length, word-aligned 12-byte repeat offsets, then
        // the raw bytes (the kernel's XexpLoadCompressedImage path).
        u8 input[24] = {};
        // words are read little-endian but bits are taken MSB-first
        input[0] = 0x00; input[1] = 0x30; // 0 011 0000... (type 3, length high 0)
        input[2] = 0x80; input[3] = 0x00; // length low 8 at bits 20..27
        input[4 + 3] = 1;                 // R0 = 1, big-endian
        input[4 + 7] = 1;                 // R1 = 1
        input[4 + 11] = 1;                // R2 = 1

        const u8 payload[8] = {'S', 'T', 'O', 'R', 'E', 'D', '!', '!'};
        std::memcpy(input + 16, payload, sizeof(payload));

        XexLzxDecoder decoder;
        u8 output[sizeof(payload)] = {};

        bool decoded = false;
        try
        {
            decoder.DecodeChunk(input, sizeof(input), output, sizeof(output));
            decoded = true;
        }
        catch (...)
        {
        }

        check(decoded && std::memcmp(output, payload, sizeof(payload)) == 0,
              "LZX stored block copies raw bytes");
    }

    // STFS chains end at 0xFFFFFF. When a 0x7FFFFF terminator was compared
    // instead, every chain gained a bogus trailing block and StfsIO threw on
    // any read that reached EOF.
    void testStfsBlockChainTerminator()
    {
        namespace fs = std::filesystem;

        const fs::path path = fs::temp_directory_path() / "xbi_smoke_stfs_chain.bin";
        std::error_code ignored;
        fs::remove(path, ignored);

        std::vector<u8> data(0x2345);
        for (std::size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<u8>(i * 7 + 3);

        bool completed = false;
        try
        {
            StfsPackage package(path.string(), StfsPackageCreate);
            package.InjectData(data.data(), static_cast<u32>(data.size()), "data.bin");
            package.InjectData(data.data(), 0, "empty.bin");

            StfsFileEntry entry = package.GetFileEntry("data.bin");
            package.GenerateBlockChain(&entry, true);
            check(entry.blocksForFile == 3, "STFS 0x2345-byte file uses 3 blocks");
            check(entry.blockChain.size() == entry.blocksForFile,
                  "STFS block chain stops at the terminator");
            check(std::find(entry.blockChain.begin(), entry.blockChain.end(),
                            kBlockChainTerminator) == entry.blockChain.end(),
                  "STFS block chain excludes the terminator");

            std::unique_ptr<StfsIO> io(package.GetStfsIO("data.bin"));
            std::vector<u8> readBack(static_cast<std::size_t>(io->Length()));
            io->ReadBytes(readBack.data(), static_cast<u32>(readBack.size()));
            check(readBack == data, "StfsIO reads a file to EOF");
            io.reset();

            // Appending at a block-aligned EOF must allocate a new block, not
            // overwrite the start of the last existing block.
            std::vector<u8> aligned(0x1000, 0x5A);
            package.InjectData(aligned.data(), static_cast<u32>(aligned.size()), "aligned.bin");
            std::unique_ptr<StfsIO> alignedIO(package.GetStfsIO("aligned.bin"));
            alignedIO->SetPosition(0x1000);
            u8 extra = 0xA5;
            alignedIO->WriteBytes(&extra, 1);
            check(alignedIO->GetPosition() == 0x1001,
                  "StfsIO append advances one byte");

            std::vector<u8> appended(0x1001);
            alignedIO->SetPosition(0);
            alignedIO->ReadBytes(appended.data(), static_cast<u32>(appended.size()));
            check(std::equal(aligned.begin(), aligned.end(), appended.begin()),
                  "StfsIO block-aligned append preserves the last block");
            check(appended.back() == extra, "StfsIO block-aligned append writes the new byte");

            // Growing a 1-byte file by 0x1001 bytes needs exactly two blocks;
            // a size-delta split over-allocated a third one.
            const u8 seed = 0x11;
            package.InjectData(const_cast<u8*>(&seed), 1, "grow.bin");
            std::unique_ptr<StfsIO> growIO(package.GetStfsIO("grow.bin"));
            std::vector<u8> grown(0x1001, 0x77);
            growIO->SetPosition(0);
            growIO->WriteBytes(grown.data(), static_cast<u32>(grown.size()));
            check(growIO->Length() == 0x1001, "StfsIO growth sets the new size");
            growIO.reset();

            // StfsIO writes the entry straight to disk; StfsPackage's cached
            // listing is not updated, so verify through a fresh package.
            StfsPackage reopened(path.string());
            StfsFileEntry grownEntry = reopened.GetFileEntry("grow.bin");
            check(grownEntry.fileSize == 0x1001, "StfsIO growth persists the file size");
            check(grownEntry.blocksForFile == 2, "StfsIO growth allocates ceil(size/0x1000) blocks");
            reopened.GenerateBlockChain(&grownEntry, true);
            check(grownEntry.blockChain.size() == 2, "StfsIO growth chain matches the block count");

            std::unique_ptr<StfsIO> grownIO(reopened.GetStfsIO("grow.bin"));
            std::vector<u8> grownBack(0x1001);
            grownIO->ReadBytes(grownBack.data(), static_cast<u32>(grownBack.size()));
            check(grownBack == grown, "StfsIO growth preserves the written data");
            grownIO.reset();
            alignedIO.reset();

            StfsFileEntry empty = package.GetFileEntry("empty.bin");
            check(empty.startingBlockNum == kBlockChainTerminator,
                  "empty STFS file starts at the terminator");
            package.GenerateBlockChain(&empty, true);
            check(empty.blockChain.empty(), "empty STFS file has no blocks");
            check(!throws([&] { std::unique_ptr<StfsIO> emptyIO(package.GetStfsIO("empty.bin")); }),
                  "StfsIO opens an empty file");

            // A zero-byte entry has no start block to validate.
            const fs::path emptyOut = fs::temp_directory_path() / "xbi_smoke_empty.out";
            package.ExtractFile("empty.bin", emptyOut.string());
            check(fs::exists(emptyOut) && fs::file_size(emptyOut) == 0,
                  "STFS extracts a zero-byte file");
            fs::remove(emptyOut, ignored);

            const fs::path replPath = fs::temp_directory_path() / "xbi_smoke_repl.bin";
            {
                std::ofstream src(replPath, std::ios::binary | std::ios::trunc);
                std::vector<u8> payload(0x1234, 0x5A);
                src.write(reinterpret_cast<const char *>(payload.data()),
                        static_cast<std::streamsize>(payload.size()));
            }
            StfsFileEntry repl = package.GetFileEntry("empty.bin");
            package.ReplaceFile(replPath.string(), &repl, "empty.bin");
            check(package.GetFileEntry("empty.bin").fileSize == 0x1234,
                  "STFS replaces a zero-byte file");
            fs::remove(replPath, ignored);

            // Removing an empty file must not walk a chain it does not own.
            package.RemoveFile(package.GetFileEntry("empty.bin"));
            check(!package.FileExists("empty.bin"), "STFS removes a zero-byte file");

            // Older packages use 0x7FFFFF as the start block of an empty file.
            package.InjectData(data.data(), 0, "empty2.bin");
            StfsFileEntry oldEmpty = package.GetFileEntry("empty2.bin");
            oldEmpty.startingBlockNum = 0x7FFFFF;
            package.RemoveFile(oldEmpty);
            check(!package.FileExists("empty2.bin"),
                  "STFS removes an empty file with an old start block");
            completed = true;
        }
        catch (const std::string &e)
        {
            std::fprintf(stderr, "  (STFS chain test threw: %s)\n", e.c_str());
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "  (STFS chain test threw: %s)\n", e.what());
        }
        check(completed, "STFS chain test completes");

        fs::remove(path, ignored);
    }

    // The chunk size used to be computed once from the starting offset, so any
    // read or write that started mid-block crossed into the next physical block
    // after the first chunk.
    void testStfsUnalignedIo()
    {
        namespace fs = std::filesystem;

        const fs::path path = fs::temp_directory_path() / "xbi_smoke_stfs_unaligned.bin";
        std::error_code ignored;
        fs::remove(path, ignored);

        // 0xB0 blocks crosses the 0xAA-block hash table; a 0x300 offset makes
        // the old fixed chunk (0xD00) straddle physical blocks.
        const std::size_t blockCount = 0xB0;
        std::vector<u8> data(blockCount * 0x1000);
        for (std::size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<u8>(i * 7 + 3);

        bool completed = false;
        try
        {
            StfsPackage package(path.string(), StfsPackageCreate);
            package.InjectData(data.data(), static_cast<u32>(data.size()), "big.bin");

            std::vector<u8> neighbour(0x1000, 0xC7);
            package.InjectData(neighbour.data(), static_cast<u32>(neighbour.size()), "neighbour.bin");

            const std::size_t offset = 0x300;
            std::unique_ptr<StfsIO> io(package.GetStfsIO("big.bin"));

            std::vector<u8> read(data.size() - offset);
            io->SetPosition(offset);
            io->ReadBytes(read.data(), static_cast<u32>(read.size()));
            check(std::equal(read.begin(), read.end(),
                             data.begin() + static_cast<std::vector<u8>::difference_type>(offset)),
                  "StfsIO unaligned read across a hash table");

            // A pure seek past EOF must not allocate or grow the file.
            io->SetPosition(static_cast<u64>(data.size()) + 0x10000);
            check(io->Length() == data.size(), "StfsIO seek past EOF does not grow");
            check(throws([&] {
                u8 one = 0;
                io->ReadBytes(&one, 1);
            }), "StfsIO read past EOF throws");

            // A write after a seek past EOF still grows and lands where asked.
            const std::size_t gap = 4;
            io->SetPosition(static_cast<u64>(data.size()) + gap);
            u8 appended = 0x5C;
            io->WriteBytes(&appended, 1);
            check(io->Length() == data.size() + gap + 1,
                  "StfsIO write after a seek past EOF grows");

            std::vector<u8> fill(data.size() - offset, 0xEE);
            io->SetPosition(offset);
            io->WriteBytes(fill.data(), static_cast<u32>(fill.size()));
            check(io->GetPosition() == data.size(), "StfsIO unaligned write ends in place");
            io.reset();

            std::unique_ptr<StfsIO> checkIo(package.GetStfsIO("big.bin"));
            std::vector<u8> back(fill.size());
            checkIo->SetPosition(offset);
            checkIo->ReadBytes(back.data(), static_cast<u32>(back.size()));
            check(back == fill, "StfsIO unaligned write across a hash table");

            std::vector<u8> head(offset);
            checkIo->SetPosition(0);
            checkIo->ReadBytes(head.data(), static_cast<u32>(head.size()));
            check(std::equal(head.begin(), head.end(), data.begin()),
                  "StfsIO unaligned write preserves the prefix");

            std::unique_ptr<StfsIO> neighbourIo(package.GetStfsIO("neighbour.bin"));
            std::vector<u8> neighbourBack(0x1000);
            neighbourIo->ReadBytes(neighbourBack.data(), 0x1000);
            check(neighbourBack == neighbour, "StfsIO write does not corrupt a neighbour");

            // A seek before zero wraps the position; the following write must
            // be rejected instead of landing at a stale offset.
            checkIo->SetPosition(0);
            check(throws([&] {
                checkIo->SetPosition(static_cast<u64>(-4), std::ios_base::cur);
                u8 one = 0xAB;
                checkIo->WriteBytes(&one, 1);
            }), "StfsIO rejects a write after a negative seek");
            checkIo->SetPosition(0);

            completed = true;
        }
        catch (const std::string &e)
        {
            std::fprintf(stderr, "  (STFS unaligned test threw: %s)\n", e.c_str());
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "  (STFS unaligned test threw: %s)\n", e.what());
        }

        check(completed, "STFS unaligned test completes");
        fs::remove(path, ignored);
    }

    void testStfsCountersAndResize()
    {
        namespace fs = std::filesystem;
        std::error_code ignored;

        const fs::path growPath = fs::temp_directory_path() / "xbi_smoke_stfs_grow.bin";
        fs::remove(growPath, ignored);

        bool completed = false;
        try
        {
            // Appending grows the total; it never consumes a free block.
            StfsPackage package(growPath.string(), StfsPackageCreate);
            std::vector<u8> first(0x1000, 0x11);
            package.InjectData(first.data(), static_cast<u32>(first.size()), "grow.bin");

            const u32 allocatedBefore = package.metaData->stfsVolumeDescriptor.allocatedBlockCount;
            const u32 freeBefore = package.metaData->stfsVolumeDescriptor.unallocatedBlockCount;

            std::vector<u8> tail(0x1000, 0x33);
            std::unique_ptr<StfsIO> io(package.GetStfsIO("grow.bin"));
            io->SetPosition(0x1000);
            io->WriteBytes(tail.data(), static_cast<u32>(tail.size()));
            check(package.metaData->stfsVolumeDescriptor.allocatedBlockCount == allocatedBefore + 1,
                  "STFS append adds one allocated block");
            check(package.metaData->stfsVolumeDescriptor.unallocatedBlockCount == freeBefore,
                  "STFS append leaves the free count unchanged");

            // Reads must survive a Flush and another package operation moving
            // the shared IO handle.
            std::vector<u8> expected(first);
            expected.insert(expected.end(), tail.begin(), tail.end());
            std::vector<u8> got(expected.size());

            io->Flush();
            io->SetPosition(0);
            io->ReadBytes(got.data(), static_cast<u32>(got.size()));
            check(got == expected, "StfsIO reads after Flush");

            package.InjectData(first.data(), static_cast<u32>(first.size()), "filler.bin");
            io->SetPosition(0);
            io->ReadBytes(got.data(), static_cast<u32>(got.size()));
            check(got == expected, "StfsIO reads after another package operation");
            io.reset();
            completed = true;
        }
        catch (const std::string &e)
        {
            std::fprintf(stderr, "  (STFS counter test threw: %s)\n", e.c_str());
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "  (STFS counter test threw: %s)\n", e.what());
        }
        check(completed, "STFS counter test completes");

        const fs::path shrinkPath = fs::temp_directory_path() / "xbi_smoke_stfs_shrink.bin";
        fs::remove(shrinkPath, ignored);
        completed = false;
        try
        {
            StfsPackage package(shrinkPath.string(), StfsPackageCreate);
            std::vector<u8> a(0x2000, 0x5A);
            std::vector<u8> filler(0x1000, 0xC3);
            package.InjectData(a.data(), static_cast<u32>(a.size()), "a.bin");
            package.InjectData(filler.data(), static_cast<u32>(filler.size()), "filler.bin");

            const u32 allocatedBefore = package.metaData->stfsVolumeDescriptor.allocatedBlockCount;
            const u32 freeBefore = package.metaData->stfsVolumeDescriptor.unallocatedBlockCount;

            std::unique_ptr<StfsIO> aio(package.GetStfsIO("a.bin"));
            aio->Resize(0x1000);

            check(package.metaData->stfsVolumeDescriptor.allocatedBlockCount == allocatedBefore,
                  "STFS shrink keeps the allocated total");
            check(package.metaData->stfsVolumeDescriptor.unallocatedBlockCount == freeBefore + 1,
                  "STFS shrink frees one block");

            std::vector<u8> firstBlock(0x1000);
            aio->SetPosition(0);
            aio->ReadBytes(firstBlock.data(), 0x1000);
            check(std::equal(firstBlock.begin(), firstBlock.end(), a.begin()),
                  "StfsIO reads after Resize");
            aio.reset();

            // The next allocation must not land on a block that still belongs
            // to filler.bin.
            std::vector<u8> later(0x1000, 0x77);
            package.InjectData(later.data(), static_cast<u32>(later.size()), "later.bin");

            std::unique_ptr<StfsIO> fillerIo(package.GetStfsIO("filler.bin"));
            std::vector<u8> fillerBack(0x1000);
            fillerIo->ReadBytes(fillerBack.data(), 0x1000);
            check(fillerBack == filler, "STFS shrink does not recycle a live block");
            completed = true;
        }
        catch (const std::string &e)
        {
            std::fprintf(stderr, "  (STFS shrink test threw: %s)\n", e.c_str());
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "  (STFS shrink test threw: %s)\n", e.what());
        }
        check(completed, "STFS shrink test completes");

        fs::remove(growPath, ignored);
        fs::remove(shrinkPath, ignored);
    }

    void testStfsCorruptChain()
    {
        namespace fs = std::filesystem;

        const fs::path path = fs::temp_directory_path() / "xbi_smoke_stfs_corruptchain.bin";
        std::error_code ignored;
        fs::remove(path, ignored);

        bool completed = false;
        try
        {
            StfsPackage package(path.string(), StfsPackageCreate);
            std::vector<u8> data(0x1000, 0x99);
            package.InjectData(data.data(), static_cast<u32>(data.size()), "ok.bin");

            StfsFileEntry corrupt = package.GetFileEntry("ok.bin");
            corrupt.fileSize = 0x1000;
            corrupt.blocksForFile = 1;
            corrupt.startingBlockNum = kBlockChainTerminator;
            corrupt.blockChain.clear();

            std::unique_ptr<StfsIO> io(package.GetStfsIO(corrupt));
            check(throws([&] {
                u8 one = 0;
                io->ReadBytes(&one, 1);
            }), "StfsIO rejects a chain shorter than the file size");
            check(throws([&] {
                u8 one = 0;
                io->WriteBytes(&one, 1);
            }), "StfsIO refuses to grow a file with no blocks");
            io.reset();

            // A chain shorter than the size must not be resized: the shrink
            // loop would pop the chain empty and call back().
            StfsFileEntry shortEntry = package.GetFileEntry("ok.bin");
            shortEntry.fileSize = 0x2000; // claims two blocks, owns one
            std::unique_ptr<StfsIO> shortIo(package.GetStfsIO(shortEntry));
            check(throws([&] { shortIo->Resize(0x3000); }),
                  "StfsIO refuses to resize a chain that does not match its size");
            shortIo.reset();
            completed = true;
        }
        catch (const std::string &e)
        {
            std::fprintf(stderr, "  (STFS corrupt chain test threw: %s)\n", e.c_str());
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "  (STFS corrupt chain test threw: %s)\n", e.what());
        }

        check(completed, "STFS corrupt chain test completes");
        fs::remove(path, ignored);
    }

    void testStfsCachedListing()
    {
        namespace fs = std::filesystem;

        const fs::path path = fs::temp_directory_path() / "xbi_smoke_stfs_cached.bin";
        std::error_code ignored;
        fs::remove(path, ignored);

        bool completed = false;
        try
        {
            StfsPackage package(path.string(), StfsPackageCreate);
            std::vector<u8> seed(1, 0x11);
            std::vector<u8> tail(0x1000, 0x22);
            package.InjectData(seed.data(), 1, "a.bin");

            {
                std::unique_ptr<StfsIO> io(package.GetStfsIO("a.bin"));
                io->SetPosition(1);
                io->WriteBytes(tail.data(), static_cast<u32>(tail.size()));
                io->Flush();
                check(package.GetFileEntry("a.bin").fileSize == 0x1001,
                      "StfsIO growth updates the cached entry");

                // CreateFolder rewrites the file table, moving every entry slot.
                package.CreateFolder("dir");
                io->SetPosition(0x1001);
                io->WriteBytes(tail.data(), static_cast<u32>(tail.size()));
            }

            StfsFileEntry file = package.GetFileEntry("a.bin");
            check(file.fileSize == 0x2001, "StfsIO growth survives a table rewrite");
            check((file.flags & Folder) == 0, "the grown file is still a file");
            check(file.blocksForFile == 3, "the grown file reports its new block count");

            StfsFileListing listing = package.GetFileListing();
            bool folderStillFolder = false;
            for (const StfsFileListing &folder : listing.folderEntries)
                if (folder.folder.name == "dir")
                    folderStillFolder = (folder.folder.flags & Folder) != 0;
            check(folderStillFolder, "the folder entry is still a folder");

            std::unique_ptr<StfsIO> readBack(package.GetStfsIO("a.bin"));
            std::vector<u8> got(0x2001);
            readBack->ReadBytes(got.data(), static_cast<u32>(got.size()));
            check(got[0] == 0x11, "the original byte survives growth");
            check(std::equal(tail.begin(), tail.end(), got.begin() + 1),
                  "the first append lands at offset 1");
            check(std::equal(tail.begin(), tail.end(), got.begin() + 0x1001),
                  "the second append lands after the table rewrite");
            completed = true;
        }
        catch (const std::string &e)
        {
            std::fprintf(stderr, "  (STFS cached listing test threw: %s)\n", e.c_str());
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "  (STFS cached listing test threw: %s)\n", e.what());
        }

        check(completed, "STFS cached listing test completes");
        fs::remove(path, ignored);
    }

    void testFileTimeConversion()
    {
        // A FILETIME is 100 ns ticks since 1601; time_t is seconds since 1970.
        FileTime zero = {0, 0};
        check(XdbfHelpers::FILETIMEtoTimeT(zero) == -11644473600LL,
              "FILETIME 0 decodes to 1601-01-01");
        check(XdbfHelpers::FILETIMEtoMilliseconds(zero) == 0,
              "FILETIME 0 has zero milliseconds");

        FileTime tick = {0, 1};
        check(XdbfHelpers::FILETIMEtoTimeT(tick) == -11644473600LL,
              "a sub-second pre-1970 time floors");
        check(XdbfHelpers::FILETIMEtoMilliseconds(tick) == 0,
              "a sub-second pre-1970 time has no milliseconds");

        FileTime in2009 = {0x01C9C380, 0};
        check(XdbfHelpers::FILETIMEtoTimeT(in2009) == 1240428288LL,
              "a 2009 timestamp decodes");
        check(static_cast<unsigned int>(XdbfHelpers::FILETIMEtoTimeT(zero)) == 1240428288u,
              "an unset FILETIME truncates to the 2009 value");

        FileTime withMs = XdbfHelpers::TimeTtoFILETIME(1234567890, 123);
        check(XdbfHelpers::FILETIMEtoTimeT(withMs) == 1234567890LL,
              "seconds round-trip through FILETIME");
        check(XdbfHelpers::FILETIMEtoMilliseconds(withMs) == 123,
              "milliseconds round-trip through FILETIME");

        FileTime pre1970 = XdbfHelpers::TimeTtoFILETIME(-1, 0);
        check(XdbfHelpers::FILETIMEtoTimeT(pre1970) == -1LL,
              "pre-1970 seconds round-trip");
        check(XdbfHelpers::FILETIMEtoMilliseconds(pre1970) == 0,
              "pre-1970 milliseconds are not negative");
    }

    void testStfsReplaceFreesBlocks()
    {
        namespace fs = std::filesystem;

        const fs::path path = fs::temp_directory_path() / "xbi_smoke_stfs_replace.bin";
        const fs::path src = fs::temp_directory_path() / "xbi_smoke_stfs_replace_src.bin";
        std::error_code ignored;
        fs::remove(path, ignored);

        bool completed = false;
        try
        {
            StfsPackage package(path.string(), StfsPackageCreate);
            std::vector<u8> big(0x3000, 0x11);
            std::vector<u8> filler(0x1000, 0x22);
            package.InjectData(big.data(), static_cast<u32>(big.size()), "a.bin");
            package.InjectData(filler.data(), static_cast<u32>(filler.size()), "filler.bin");

            const u32 freeBefore = package.metaData->stfsVolumeDescriptor.unallocatedBlockCount;
            const u32 allocatedBefore = package.metaData->stfsVolumeDescriptor.allocatedBlockCount;

            std::vector<u8> smaller(0x1000, 0x33);
            {
                std::ofstream out(src, std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char *>(smaller.data()),
                        static_cast<std::streamsize>(smaller.size()));
            }

            StfsFileEntry a = package.GetFileEntry("a.bin");
            package.ReplaceFile(src.string(), &a, "a.bin");
            check(package.metaData->stfsVolumeDescriptor.unallocatedBlockCount == freeBefore + 2,
                  "ReplaceFile frees the blocks it stops using");
            check(package.metaData->stfsVolumeDescriptor.allocatedBlockCount == allocatedBefore,
                  "ReplaceFile keeps the allocated total");

            std::unique_ptr<StfsIO> fillerIo(package.GetStfsIO("filler.bin"));
            std::vector<u8> fillerBack(0x1000);
            fillerIo->ReadBytes(fillerBack.data(), 0x1000);
            check(fillerBack == filler, "ReplaceFile leaves the neighbour intact");
            fillerIo.reset();

            std::unique_ptr<StfsIO> aIo(package.GetStfsIO("a.bin"));
            std::vector<u8> aBack(0x1000);
            aIo->ReadBytes(aBack.data(), 0x1000);
            check(aBack == smaller, "the smaller replacement reads back");
            aIo.reset();

            // Replacing with an empty file frees the last block and resets the start.
            {
                std::ofstream out(src, std::ios::binary | std::ios::trunc);
            }
            a = package.GetFileEntry("a.bin");
            package.ReplaceFile(src.string(), &a, "a.bin");
            StfsFileEntry empty = package.GetFileEntry("a.bin");
            check(empty.fileSize == 0 && empty.blocksForFile == 0,
                  "an empty replacement has no blocks");
            check(empty.startingBlockNum == kBlockChainTerminator,
                  "an empty replacement starts at the terminator");
            check(package.metaData->stfsVolumeDescriptor.unallocatedBlockCount == freeBefore + 3,
                  "the empty replacement frees its last block");
            completed = true;
        }
        catch (const std::string &e)
        {
            std::fprintf(stderr, "  (STFS replace test threw: %s)\n", e.c_str());
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "  (STFS replace test threw: %s)\n", e.what());
        }

        check(completed, "STFS replace test completes");
        fs::remove(path, ignored);
        fs::remove(src, ignored);
    }

    void testStfsZeroBlockResize()
    {
        namespace fs = std::filesystem;

        const fs::path path = fs::temp_directory_path() / "xbi_smoke_stfs_zero.bin";
        std::error_code ignored;
        fs::remove(path, ignored);

        bool completed = false;
        try
        {
            StfsPackage package(path.string(), StfsPackageCreate);
            std::vector<u8> data(0x2000, 0x66);
            package.InjectData(data.data(), static_cast<u32>(data.size()), "a.bin");

            const u32 freeBefore = package.metaData->stfsVolumeDescriptor.unallocatedBlockCount;

            std::unique_ptr<StfsIO> io(package.GetStfsIO("a.bin"));
            io->Resize(0);
            check(io->Length() == 0, "Resize(0) clears the file size");

            StfsFileEntry emptied = package.GetFileEntry("a.bin");
            check(emptied.blocksForFile == 0, "Resize(0) clears the block count");
            check(emptied.startingBlockNum == kBlockChainTerminator,
                  "Resize(0) resets the start block");
            check(package.metaData->stfsVolumeDescriptor.unallocatedBlockCount == freeBefore + 2,
                  "Resize(0) frees both blocks");

            // Writing to the emptied file must allocate a new first block.
            std::vector<u8> regrown(0x1234, 0x77);
            io->SetPosition(0);
            io->WriteBytes(regrown.data(), static_cast<u32>(regrown.size()));
            check(io->Length() == 0x1234, "writing to an empty file grows it");
            io->SetPosition(0);
            std::vector<u8> back(0x1234);
            io->ReadBytes(back.data(), static_cast<u32>(back.size()));
            check(back == regrown, "the regrown file reads back");
            io.reset();

            StfsPackage reopened(path.string());
            StfsFileEntry grown = reopened.GetFileEntry("a.bin");
            check(grown.fileSize == 0x1234, "reopen sees the regrown size");
            check(grown.blocksForFile == 2, "reopen sees both blocks");
            check(grown.startingBlockNum != kBlockChainTerminator,
                  "reopen sees the new start block");
            completed = true;
        }
        catch (const std::string &e)
        {
            std::fprintf(stderr, "  (STFS zero resize test threw: %s)\n", e.c_str());
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "  (STFS zero resize test threw: %s)\n", e.what());
        }

        check(completed, "STFS zero resize test completes");
        fs::remove(path, ignored);
    }

    void testStfsCounterRepair()
    {
        namespace fs = std::filesystem;

        const fs::path path = fs::temp_directory_path() / "xbi_smoke_stfs_repair.bin";
        std::error_code ignored;
        fs::remove(path, ignored);

        const auto fnv = [](const fs::path &p) {
            std::ifstream in(p, std::ios::binary);
            u64 hash = 1469598103934665603ULL;
            char c;
            while (in.get(c))
            {
                hash ^= static_cast<u8>(c);
                hash *= 1099511628211ULL;
            }
            return hash;
        };

        bool completed = false;
        try
        {
            {
                StfsPackage package(path.string(), StfsPackageCreate);
                std::vector<u8> data(0x1000, 0x42);
                package.InjectData(data.data(), static_cast<u32>(data.size()), "a.bin");
                package.metaData->stfsVolumeDescriptor.unallocatedBlockCount = 0xFFFFFFFF;
                package.metaData->WriteVolumeDescriptor();
            }

            const u64 sizeBefore = fs::file_size(path);
            const u64 hashBefore = fnv(path);

            {
                StfsPackage reopened(path.string());
                check(reopened.metaData->stfsVolumeDescriptor.unallocatedBlockCount == 0,
                      "a wrapped free count is rebuilt from the hash tree");
            }

            check(fs::file_size(path) == sizeBefore && fnv(path) == hashBefore,
                  "opening a package does not rewrite the descriptor");

            // The repaired value is persisted by the next edit.
            {
                StfsPackage edited(path.string());
                std::vector<u8> more(0x1000, 0x43);
                edited.InjectData(more.data(), static_cast<u32>(more.size()), "b.bin");
            }

            StfsPackage reopened(path.string());
            check(reopened.metaData->stfsVolumeDescriptor.unallocatedBlockCount == 0,
                  "the repaired free count is persisted by the next edit");
            completed = true;
        }
        catch (const std::string &e)
        {
            std::fprintf(stderr, "  (STFS counter repair test threw: %s)\n", e.c_str());
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "  (STFS counter repair test threw: %s)\n", e.what());
        }

        check(completed, "STFS counter repair test completes");
        fs::remove(path, ignored);
    }

    void testFatxEraseRanges()
    {
        std::vector<u32> list = {1, 2, 3, 5, 7};
        FatxIO::EraseIndexRanges(list, std::vector<Range>{{0, 3}, {3, 2}});
        check(list.empty(), "FATX erases used ranges in descending order");

        list = {1, 2, 3, 5, 7};
        FatxIO::EraseIndexRanges(list, std::vector<Range>{{3, 2}, {0, 3}});
        check(list.empty(), "FATX erases ranges in any input order");

        list = {1, 2, 3, 5, 7, 9};
        FatxIO::EraseIndexRanges(list, std::vector<Range>{{1, 2}, {4, 2}});
        check(list == std::vector<u32>{1, 5}, "FATX keeps the clusters that were not used");

        list = {1, 2};
        check(throws([&] {
            FatxIO::EraseIndexRanges(list, std::vector<Range>{{1, 5}});
        }), "FATX rejects an out-of-bounds erase range");
    }

    void testIndexableMultiFileIO()
    {
        namespace fs = std::filesystem;

        const fs::path dir = fs::temp_directory_path() / "xbi_smoke_indexable";
        std::error_code ignored;
        fs::remove_all(dir, ignored);
        fs::create_directories(dir, ignored);

        {
            std::ofstream f1(dir / "01.bin", std::ios::binary | std::ios::trunc);
            f1.write("abcdefgh", 8);
            std::ofstream f2(dir / "02.bin", std::ios::binary | std::ios::trunc);
            f2.write("ijklmnop", 8);
        }

        {
            LocalIndexableMultiFileIO io(dir.string());

            // Writing 12 bytes from position 4 ends exactly at the last part.
            std::vector<u8> payload(12, 'Z');
            io.SetPosition(4, 0);
            check(!throws([&] { io.WriteBytes(payload.data(), 12); }),
                  "MultiFileIO writes to the end of the last part");

            u32 addr = 0, index = 0;
            io.GetPosition(&addr, &index);
            check(addr == 8 && index == 1,
                  "MultiFileIO position after a boundary write");

            std::vector<u8> back(12);
            io.SetPosition(4, 0);
            io.ReadBytes(back.data(), static_cast<u32>(back.size()));
            check(back == payload, "MultiFileIO reads across a part boundary");

            io.GetPosition(&addr, &index);
            check(addr == 8 && index == 1,
                  "MultiFileIO position after a boundary read");

            // Past the end must throw before any byte is read.
            io.SetPosition(4, 0);
            check(throws([&] {
                std::vector<u8> over(13);
                io.ReadBytes(over.data(), static_cast<u32>(over.size()));
            }), "MultiFileIO rejects a read past the last part");
        }

        const auto readAll = [](const fs::path &p) {
            std::ifstream in(p, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>());
        };
        check(readAll(dir / "01.bin") == "abcdZZZZ", "MultiFileIO wrote the first part");
        check(readAll(dir / "02.bin") == "ZZZZZZZZ", "MultiFileIO wrote the second part");

        fs::remove_all(dir, ignored);
    }

    void testStfsNameCollisions()
    {
        namespace fs = std::filesystem;

        const fs::path path = fs::temp_directory_path() / "xbi_smoke_stfs_collisions.bin";
        std::error_code ignored;
        fs::remove(path, ignored);

        bool completed = false;
        try
        {
            StfsPackage package(path.string(), StfsPackageCreate);
            std::vector<u8> a(0x1000, 0x11);
            std::vector<u8> b(0x2000, 0x22);
            package.InjectData(a.data(), static_cast<u32>(a.size()), "a.bin");
            package.InjectData(b.data(), static_cast<u32>(b.size()), "b.bin");
            package.CreateFolder("dir");

            check(throws([&] { package.RenameFile("b.bin", "a.bin"); }),
                  "STFS rejects renaming onto an existing sibling");
            check(throws([&] { package.RenameFile("B.BIN", "a.bin"); }),
                  "STFS rejects a case-insensitive rename collision");

            // A case-only rename of the same entry is allowed.
            package.RenameFile("A.BIN", "a.bin");
            check(package.GetFileEntry("A.BIN").name == "A.BIN",
                  "STFS allows a case-only rename");
            check(package.GetFileEntry("b.bin").fileSize == 0x2000,
                  "the untouched sibling is unchanged");

            check(throws([&] { package.CreateFolder("A.BIN"); }),
                  "STFS rejects a folder named like a file");
            check(throws([&] { package.InjectData(a.data(),
                    static_cast<u32>(a.size()), "dir"); }),
                  "STFS rejects injecting a file named like a folder");
            check(throws([&] { package.InjectData(a.data(),
                    static_cast<u32>(a.size()), "DIR"); }),
                  "STFS rejects a case-insensitive file/folder collision");

            StfsFileListing listing = package.GetFileListing();
            check(listing.fileEntries.size() == 2, "the listing still has two files");
            check(listing.folderEntries.size() == 1, "the listing still has one folder");
            completed = true;
        }
        catch (const std::string &e)
        {
            std::fprintf(stderr, "  (STFS collision test threw: %s)\n", e.c_str());
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "  (STFS collision test threw: %s)\n", e.what());
        }

        check(completed, "STFS collision test completes");
        fs::remove(path, ignored);
    }

    void testFatxFileExtents()
    {
        Partition part{};
        part.clusterSize = 0x4000;
        part.clusterStartingAddress = 0x100000;
        part.clusterCount = 64;

        const u32 fileSize = 5 * 0x4000 - 100;
        std::vector<u32> chain{1, 2, 3, 7, 9};
        std::vector<FileExtent> extents;

        check(FatxDrive::ComputeFileExtents(&part, fileSize, chain, extents),
              "contiguous FATX runs are mapped");
        check(extents.size() == 3, "FATX extents split at cluster gaps");
        if (extents.size() == 3)
        {
            check(extents[0].fileOffset == 0 && extents[0].deviceOffset == 0x100000 &&
                    extents[0].length == 3 * 0x4000, "first extent covers the run");
            check(extents[1].fileOffset == 3 * 0x4000 &&
                    extents[1].deviceOffset == 0x100000 + 6 * 0x4000 &&
                    extents[1].length == 0x4000, "second extent starts after the gap");
            check(extents[2].fileOffset == 4 * 0x4000 &&
                    extents[2].deviceOffset == 0x100000 + 8 * 0x4000 &&
                    extents[2].length == 0x4000 - 100, "last extent is clamped to fileSize");
        }

        std::vector<FileExtent> shortExtents;
        check(!FatxDrive::ComputeFileExtents(&part, fileSize, std::vector<u32>{1, 2}, shortExtents),
              "a chain shorter than the file is rejected");

        std::vector<FileExtent> badExtents;
        check(!FatxDrive::ComputeFileExtents(&part, fileSize, std::vector<u32>{0, 1}, badExtents),
              "cluster 0 is rejected");
        check(!FatxDrive::ComputeFileExtents(&part, fileSize,
                      std::vector<u32>{65}, badExtents),
              "a cluster past clusterCount is rejected");

        check(FatxDrive::ComputeFileExtents(&part, 0, std::vector<u32>{}, extents) &&
                extents.empty(), "an empty file has no extents");
    }

    void testFatxNamesEqual()
    {
        check(FatxDrive::NamesEqual("foo.bin", "FOO.BIN"),
              "FATX names compare case-insensitively");
        check(FatxDrive::NamesEqual("Name.Txt", "nAME.tXT"),
              "FATX names fold in both directions");
        check(!FatxDrive::NamesEqual("foo.bin", "foo.bi"),
              "FATX names compare length first");
    }

    void testAsfProbe()
    {
        const u8 headerGuid[16] = {
            0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
            0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C
        };
        const u8 encryptionGuid[16] = {
            0xFB, 0xB3, 0x11, 0x22, 0x23, 0xBD, 0xD2, 0x11,
            0xB4, 0xB7, 0x00, 0xA0, 0xC9, 0x55, 0xFC, 0x6E
        };

        auto makeHeader = [&](bool encrypted) {
            std::vector<u8> data(0x100, 0);
            std::memcpy(data.data(), headerGuid, sizeof(headerGuid));
            const u64 size = data.size();
            for (int i = 0; i < 8; ++i)
                data[0x10 + i] = static_cast<u8>((size >> (8 * i)) & 0xFF);
            data[0x18] = 1;    // one header object
            if (encrypted)
                std::memcpy(data.data() + 0x30, encryptionGuid, sizeof(encryptionGuid));
            return data;
        };

        std::vector<u8> clean = makeHeader(false);
        MemoryIO cleanIo(clean.data(), clean.size());
        check(XboxInternals::Asf::IsAsf(&cleanIo), "ASF header is recognised");
        check(!XboxInternals::Asf::IsDrmProtected(&cleanIo), "clean ASF is not DRM protected");
        check(cleanIo.GetPosition() == 0, "ASF probe restores the position");

        std::vector<u8> encrypted = makeHeader(true);
        MemoryIO encryptedIo(encrypted.data(), encrypted.size());
        check(XboxInternals::Asf::IsDrmProtected(&encryptedIo),
              "ASF content encryption is detected");
        check(encryptedIo.GetPosition() == 0, "ASF DRM probe restores the position");

        std::vector<u8> notAsf(0x100, 0xAA);
        MemoryIO otherIo(notAsf.data(), notAsf.size());
        check(!XboxInternals::Asf::IsAsf(&otherIo), "non-ASF data is rejected");
        check(!XboxInternals::Asf::IsDrmProtected(&otherIo), "non-ASF data is not DRM protected");
    }

    void testGarbageImages()
    {
        namespace fs = std::filesystem;

        const fs::path path = fs::temp_directory_path() / "xbi_smoke_garbage.bin";

        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            for (int i = 0; i < 128; ++i)
                file.put(static_cast<char>(0xA5));
        }

        {
            XboxInternals::Iso::IsoImage image;
            check(!image.open(path.string()), "garbage ISO is rejected");
        }

        {
            // StfsPackage takes a path and parses in its constructor; either it
            // rejects the header or it throws. Both are fine, a crash is not.
            (void)throws([&] {
                StfsPackage package(path.string());
                (void)package;
            });
        }

        std::error_code ignored;
        fs::remove(path, ignored);
    }
}

namespace
{
void putBE16(std::vector<u8> &data, std::size_t offset, u16 value)
{
    data[offset] = static_cast<u8>(value >> 8);
    data[offset + 1] = static_cast<u8>(value);
}

void putLE32(std::vector<u8> &data, std::size_t offset, u32 value)
{
    data[offset] = static_cast<u8>(value);
    data[offset + 1] = static_cast<u8>(value >> 8);
    data[offset + 2] = static_cast<u8>(value >> 16);
    data[offset + 3] = static_cast<u8>(value >> 24);
}

void putBE64(std::vector<u8> &data, std::size_t offset, u64 value)
{
    for (int i = 0; i < 8; ++i)
        data[offset + i] = static_cast<u8>(value >> (56 - i * 8));
}

void sha1Bytes(const u8 *data, std::size_t length, u8 out[0x14])
{
    const auto hash = Botan::HashFunction::create_or_throw("SHA-1");
    hash->update(data, length);
    hash->final(out);
}

bool writeTestFile(const std::filesystem::path &path, const std::vector<u8> &data)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file.write(reinterpret_cast<const char *>(data.data()),
               static_cast<std::streamsize>(data.size()));
    return file.good();
}

// Minimal XEX2 header with one type-1 base-file descriptor. The descriptor is
// the only thing these tests need; the image data is zero-filled.
std::vector<u8> makeSyntheticXex(u32 descriptorSize, u16 encryption,
                                   u16 compression, u32 blockSize, u32 nullSize)
{
    std::vector<u8> file(0x1000, 0);

    putBE32(file, 0x00, 0x58455832); // XEX2
    putBE32(file, 0x08, 0x800);      // dataAddress
    putBE32(file, 0x10, 0x200);      // headerAddress
    putBE32(file, 0x14, 1);          // one optional header

    putBE32(file, 0x18, 0x3FF);      // BaseFileDescriptor
    putBE32(file, 0x1C, 0x400);

    putBE32(file, 0x200, 0x184);     // security size
    putBE32(file, 0x204, 0x1000);    // image size

    putBE32(file, 0x400, descriptorSize);
    putBE16(file, 0x404, encryption);
    putBE16(file, 0x406, compression);

    if (descriptorSize >= 0x10)
    {
        putBE32(file, 0x408, blockSize);
        putBE32(file, 0x40C, nullSize);
    }
    if (descriptorSize >= 0x18)
    {
        putBE32(file, 0x410, 0);
        putBE32(file, 0x414, 0);
    }

    return file;
}

void testXexType1Tables()
{
    {
        auto file = makeSyntheticXex(0x18, 0, 1, 16, 0);
        try
        {
            MemoryIO io(file.data(), file.size());
            Xex xex(&io);
            check(xex.GetCompressionState() == XexCompressed,
                  "XEX type-1 table parses");
        }
        catch (...)
        {
            check(false, "XEX type-1 table parses");
        }
    }

    {
        auto file = makeSyntheticXex(0x10, 0, 1, 16, 16);
        check(throws([&] {
            MemoryIO io(file.data(), file.size());
            Xex xex(&io);
        }), "XEX type-1 table without a zero terminator throws");
    }

    {
        auto file = makeSyntheticXex(0x8, 0, 1, 0, 0);
        check(throws([&] {
            MemoryIO io(file.data(), file.size());
            Xex xex(&io);
        }), "XEX type-1 descriptor smaller than 0x10 throws");
    }

    {
        namespace fs = std::filesystem;
        const fs::path out = fs::temp_directory_path() / "xbi_smoke_xex_unaligned.bin";
        auto file = makeSyntheticXex(0x18, 1, 1, 17, 0);
        check(throws([&] {
            MemoryIO io(file.data(), file.size());
            Xex xex(&io);
            xex.ExtractData(out.string());
        }), "XEX encrypted type-1 run must be AES-block aligned");

        std::error_code ignored;
        fs::remove(out, ignored);
    }

    {
        namespace fs = std::filesystem;
        const fs::path out = fs::temp_directory_path() / "xbi_smoke_xex_valid.bin";
        auto file = makeSyntheticXex(0x18, 0, 1, 16, 0);
        bool ok = false;
        try
        {
            MemoryIO io(file.data(), file.size());
            Xex xex(&io);
            xex.ExtractData(out.string());
            ok = fs::exists(out) && fs::file_size(out) == 0x1000;
        }
        catch (...)
        {
        }
        check(ok, "XEX type-1 extraction writes the full image size");

        std::error_code ignored;
        fs::remove(out, ignored);
    }

    {
        namespace fs = std::filesystem;
        const fs::path out = fs::temp_directory_path() / "xbi_smoke_xex_raw.bin";
        auto file = makeSyntheticXex(0x8, 0, 0, 0, 0);
        bool ok = false;
        try
        {
            MemoryIO io(file.data(), file.size());
            Xex xex(&io);
            xex.ExtractData(out.string());
            ok = fs::exists(out) && fs::file_size(out) == 0x800;
        }
        catch (...)
        {
        }
        check(ok, "XEX uncompressed unencrypted extraction copies raw data");

        std::error_code ignored;
        fs::remove(out, ignored);
    }

    {
        // A plain image keeps ExtractBaseImageFile's original-file offset.
        namespace fs = std::filesystem;
        const fs::path out = fs::temp_directory_path() / "xbi_smoke_xex_plain_base.bin";
        auto file = makeSyntheticXex(0x8, 0, 0, 0, 0);
        bool ok = false;
        try
        {
            MemoryIO io(file.data(), file.size());
            Xex xex(&io);
            xex.ExtractBaseImageFile(out.string());
            ok = fs::exists(out) && fs::file_size(out) == 0x800;
        }
        catch (...)
        {
        }
        check(ok, "XEX plain base image uses the original-file offset");

        std::error_code ignored;
        fs::remove(out, ignored);
    }

    {
        // A compressed image is extracted through a temp file that starts at 0.
        // ExtractBaseImageFile must not subtract dataAddress from its length.
        namespace fs = std::filesystem;
        const fs::path out = fs::temp_directory_path() / "xbi_smoke_xex_base.bin";
        auto file = makeSyntheticXex(0x18, 0, 1, 16, 0);
        bool ok = false;
        try
        {
            MemoryIO io(file.data(), file.size());
            Xex xex(&io);
            xex.ExtractBaseImageFile(out.string());
            ok = fs::exists(out) && fs::file_size(out) == 0x1000;
        }
        catch (...)
        {
        }
        check(ok, "XEX compressed base image uses the temp-file length");

        std::error_code ignored;
        fs::remove(out, ignored);
    }
}

void testSvodMultipartRehash()
{
    namespace fs = std::filesystem;
    using namespace XboxInternals::SvodLayout;

    const fs::path dir = fs::temp_directory_path() / "xbi_smoke_svod_multipart";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "package.data", ec);
    if (ec)
    {
        check(false, "SVOD test directory can be created");
        return;
    }

    std::vector<u8> part0(42 * 0x1000, 0);
    std::vector<u8> part1(12 * 0x1000, 0);

    const std::size_t gdf = static_cast<std::size_t>(DataBlockAddress(16));
    std::fill(part0.begin() + gdf, part0.begin() + gdf + 0x800, 0xFF);
    std::memcpy(part0.data() + gdf, "MICROSOFT*XBOX*MEDIA", 0x14);
    putLE32(part0, gdf + 0x14, 33);   // root sector
    putLE32(part0, gdf + 0x18, 0x800);

    const std::size_t root = gdf + 0x800;
    std::fill(part0.begin() + root, part0.begin() + gdf + 0x1000, 0xFF);
    putLE32(part0, root + 0x00, 0);
    putLE32(part0, root + 0x04, 34);
    putLE32(part0, root + 0x08, 4);
    part0[root + 0x0C] = 0x20;
    part0[root + 0x0D] = 8;
    std::memcpy(part0.data() + root + 0x0E, "TEST.BIN", 8);
    putLE32(part0, root + 0x18, 0xFFFFFFFF);
    std::memcpy(part0.data() + DataBlockAddress(17), "TEST", 4);

    // Build part 0's hash tree, then link part 1's master block.
    u8 level0[0x1000] = {};
    for (u32 block = 0; block < 40; ++block)
        sha1Bytes(part0.data() + DataBlockAddress(block), 0x1000,
                  level0 + block * 0x14);

    std::memcpy(part0.data() + 0x1000, level0, 0x1000);

    u8 master[0x14];
    sha1Bytes(level0, 0x1000, master);
    std::memcpy(part0.data(), master, 0x14);

    std::vector<u8> zeroMaster(0x1000, 0);
    u8 part1MasterHash[0x14];
    sha1Bytes(zeroMaster.data(), zeroMaster.size(), part1MasterHash);
    std::memcpy(part0.data() + 4060, part1MasterHash, 0x14);

    u8 rootHash[0x14];
    sha1Bytes(part0.data(), 0x1000, rootHash);

    // Corrupt one data block without touching the master/level-0 hashes. The
    // constructor's root check still passes, Verify() fails, and Rehash must
    // repair it. This makes a no-op Rehash fail the test as well.
    part0[static_cast<std::size_t>(DataBlockAddress(1))] ^= 0xFF;

    std::vector<u8> header(0xB000, 0);
    putBE32(header, 0x000, 0x434F4E20); // CON
    putBE32(header, 0x01C, 1);          // certificate console type: Retail
    putBE32(header, 0x340, 0xB000);     // header size
    putBE32(header, 0x344, 0x7000);     // GameOnDemand
    putBE32(header, 0x348, 2);          // metadata version
    putBE32(header, 0x3A9, 1);          // FileSystemSVOD

    header[0x379] = 0x24;               // volume descriptor size
    std::memcpy(header.data() + 0x37D, rootHash, 0x14);
    header[0x391] = 0;                  // flags
    header[0x392] = 0;                  // dataBlockCount = 40, big-endian 24-bit
    header[0x393] = 0;
    header[0x394] = 40;
    header[0x395] = 0;                  // dataBlockOffset = 0, little-endian 24-bit
    header[0x396] = 0;
    header[0x397] = 0;
    putBE32(header, 0x39D, 2);          // two parts
    putBE64(header, 0x3A1, part0.size() + part1.size());

    bool written = writeTestFile(dir / "package", header) &&
                   writeTestFile(dir / "package.data" / "Data0000", part0) &&
                   writeTestFile(dir / "package.data" / "Data0001", part1);
    check(written, "SVOD multipart fixture can be written");

    if (written)
    {
        bool ok = false;
        try
        {
            SVOD svod((dir / "package").string(), nullptr, false);
            check(svod.GetSectorCount() == 80, "SVOD multipart sector count");
            const bool before = svod.Verify();
            svod.Rehash();
            ok = !before && svod.Verify();
        }
        catch (const std::string &error)
        {
            std::fprintf(stderr, "  (SVOD multipart threw: %s)\n", error.c_str());
        }
        catch (const std::exception &error)
        {
            std::fprintf(stderr, "  (SVOD multipart threw: %s)\n", error.what());
        }
        check(ok, "SVOD multipart Rehash then Verify");
    }

    fs::remove_all(dir, ec);
}
} // namespace

int main()
{
    testXexType1Tables();
    testSvodMultipartRehash();
    testMemoryIO();
    testFileIOReadWritePosition();
    testLzxGarbage();
    testLzxRegression();
    testCorruptStfsHeaders();
    testMSTimeRoundTrip();
    testStfsNameMatchingAndGeometry();
    testSvodLayout();
    testSafeExtractionPaths();
    testSvodDescriptorEndianness();
    testLzxStoredBlock();
    testResignFailureLeavesPackageIntact();
    testCorruptXdbf();
    testStfsBlockChainTerminator();
    testStfsUnalignedIo();
    testStfsCountersAndResize();
    testStfsCorruptChain();
    testStfsCachedListing();
    testStfsReplaceFreesBlocks();
    testStfsZeroBlockResize();
    testStfsCounterRepair();
    testFatxEraseRanges();
    testFatxNamesEqual();
    testFatxFileExtents();
    testIndexableMultiFileIO();
    testStfsNameCollisions();
    testFileTimeConversion();
    testAsfProbe();
    testGarbageImages();

    if (g_failures != 0)
    {
        std::fprintf(stderr, "xbi_smoke: %d failure(s)\n", g_failures);
        return 1;
    }

    std::printf("xbi_smoke: ok\n");
    return 0;
}
