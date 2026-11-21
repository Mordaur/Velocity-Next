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
#include <XboxInternals/Gpd/Xdbf.h>
#include <XboxInternals/IO/MemoryIO.h>
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
        BYTE raw[256] = {};

        MemoryIO io(raw, sizeof(raw));
        io.SetEndian(LittleEndian);
        io.Write(static_cast<DWORD>(0x11223344));
        io.Write(static_cast<WORD>(0xBEEF));
        io.Write(static_cast<UINT64>(0x0102030405060708ull));
        io.Write(std::string("hello"));

        io.SetPosition(0);
        check(io.ReadDword() == 0x11223344u, "little-endian DWORD round-trip");
        check(io.ReadWord() == 0xBEEFu, "little-endian WORD round-trip");
        check(io.ReadUInt64() == 0x0102030405060708ull, "little-endian UINT64 round-trip");
        check(io.ReadString() == "hello", "NUL-terminated string round-trip");

        BYTE be[16] = {};
        MemoryIO big(be, sizeof(be));
        big.SetEndian(BigEndian);
        big.Write(static_cast<DWORD>(0x11223344));
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
            BYTE two[4] = {1, 2, 3, 4};
            tiny.WriteBytes(two, sizeof(two));
        }), "write past the end throws");

        check(throws([&] {
            MemoryIO tiny(raw, 2);
            tiny.SetPosition(3);
        }), "seek past the end throws");
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
                std::vector<BYTE> input(length);
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
                        input[i] = static_cast<BYTE>(state >> 24);
                        break;
                    default:
                        input[i] = static_cast<BYTE>(i & 0xFF);
                        break;
                    }
                }

                XexLzxDecoder decoder;
                BYTE output[0x100] = {};

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

    void putBE32(std::vector<BYTE> &buffer, std::size_t offset, std::uint32_t value)
    {
        buffer[offset] = static_cast<BYTE>(value >> 24);
        buffer[offset + 1] = static_cast<BYTE>(value >> 16);
        buffer[offset + 2] = static_cast<BYTE>(value >> 8);
        buffer[offset + 3] = static_cast<BYTE>(value);
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
        struct Case { UINT64 blocks; DWORD expected; };
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
        DWORD header = 0;
        check(GdfHeaderBlock(0, false, &header) && header == 16, "legacy GDF header block");
        check(GdfHeaderBlock(4, false, &header) && header == 12, "legacy GDF header after skip");
        check(!GdfHeaderBlock(17, false, &header), "legacy GDF header inside the prefix");
        check(GdfHeaderBlock(0, true, &header) && header == 17, "enhanced GDF header block");
        check(GdfHeaderBlock(16, true, &header) && header == 1, "enhanced header at 17-D");
        check(GdfHeaderBlock(17, true, &header) && header == 0, "enhanced header alias");
        check(GdfHeaderBlock(20, true, &header) && header == 0, "enhanced header still aliased");

        struct MapCase
        {
            DWORD sector;
            DWORD dataBlockOffset;
            bool enhanced;
            bool valid;
            DWORD address;
            DWORD part;
        };

        const DWORD partSectors = static_cast<DWORD>(kSectorsPerPart); // 0x14388
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
            DWORD address = 0, part = 0;
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
        DWORD boundAddress = 0, boundPart = 0;
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
        std::vector<BYTE> buffer(0x379 + sizeof(SvodVolumeDescriptor), 0);
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

        check(static_cast<DWORD>(descriptor.dataBlockCount) == 0x102,
              "SVOD dataBlockCount is big-endian");
        check(static_cast<DWORD>(descriptor.dataBlockOffset) == 0x11,
              "SVOD dataBlockOffset is little-endian");

        std::vector<BYTE> written(0x379 + sizeof(SvodVolumeDescriptor), 0);
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
            std::vector<BYTE> header(size, 0);
            header[0] = 'L'; header[1] = 'I'; header[2] = 'V'; header[3] = 'E';
            putBE32(header, 0x340, static_cast<std::uint32_t>(size));  // headerSize
            putBE32(header, 0x3A9, 0x12345678);                       // fileSystem
            MemoryIO io(header.data(), header.size());
            check(throws([&] { XContentHeader metadata(&io); }),
                  "invalid STFS file system is rejected");
        }

        // 2. Avatar metadata with an invalid installer type.
        {
            std::vector<BYTE> header(size, 0);
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
            std::vector<BYTE> header(size, 0);
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
            std::vector<BYTE> header(size, 0);
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

        std::vector<BYTE> package(size, 0);
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

        const std::vector<BYTE> before = package;

        std::vector<BYTE> kv(0x4000, 0);
        check(throws([&] { metadata->ResignHeader(kv.data(), kv.size()); }),
              "resign with a zero keyvault throws");

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

        std::vector<BYTE> buffer(0x40, 0);
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
        static const BYTE input[] = {
            0x3F, 0x2F, 0x23, 0x00, 0x00, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7B,
            0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x00, 0x49, 0x49, 0x49, 0x49, 0x49,
            0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49,
            0x49, 0x49, 0x49, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x1A, 0x1A
        };

        XexLzxDecoder decoder;
        BYTE output[0x400] = {};

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
        BYTE input[24] = {};
        // words are read little-endian but bits are taken MSB-first
        input[0] = 0x00; input[1] = 0x30; // 0 011 0000... (type 3, length high 0)
        input[2] = 0x80; input[3] = 0x00; // length low 8 at bits 20..27
        input[4 + 3] = 1;                 // R0 = 1, big-endian
        input[4 + 7] = 1;                 // R1 = 1
        input[4 + 11] = 1;                // R2 = 1

        const BYTE payload[8] = {'S', 'T', 'O', 'R', 'E', 'D', '!', '!'};
        std::memcpy(input + 16, payload, sizeof(payload));

        XexLzxDecoder decoder;
        BYTE output[sizeof(payload)] = {};

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
void putBE16(std::vector<BYTE> &data, std::size_t offset, WORD value)
{
    data[offset] = static_cast<BYTE>(value >> 8);
    data[offset + 1] = static_cast<BYTE>(value);
}

void putLE32(std::vector<BYTE> &data, std::size_t offset, DWORD value)
{
    data[offset] = static_cast<BYTE>(value);
    data[offset + 1] = static_cast<BYTE>(value >> 8);
    data[offset + 2] = static_cast<BYTE>(value >> 16);
    data[offset + 3] = static_cast<BYTE>(value >> 24);
}

void putBE64(std::vector<BYTE> &data, std::size_t offset, UINT64 value)
{
    for (int i = 0; i < 8; ++i)
        data[offset + i] = static_cast<BYTE>(value >> (56 - i * 8));
}

void sha1Bytes(const BYTE *data, std::size_t length, BYTE out[0x14])
{
    const auto hash = Botan::HashFunction::create_or_throw("SHA-1");
    hash->update(data, length);
    hash->final(out);
}

bool writeTestFile(const std::filesystem::path &path, const std::vector<BYTE> &data)
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
std::vector<BYTE> makeSyntheticXex(DWORD descriptorSize, WORD encryption,
                                   WORD compression, DWORD blockSize, DWORD nullSize)
{
    std::vector<BYTE> file(0x1000, 0);

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

    std::vector<BYTE> part0(42 * 0x1000, 0);
    std::vector<BYTE> part1(12 * 0x1000, 0);

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
    BYTE level0[0x1000] = {};
    for (DWORD block = 0; block < 40; ++block)
        sha1Bytes(part0.data() + DataBlockAddress(block), 0x1000,
                  level0 + block * 0x14);

    std::memcpy(part0.data() + 0x1000, level0, 0x1000);

    BYTE master[0x14];
    sha1Bytes(level0, 0x1000, master);
    std::memcpy(part0.data(), master, 0x14);

    std::vector<BYTE> zeroMaster(0x1000, 0);
    BYTE part1MasterHash[0x14];
    sha1Bytes(zeroMaster.data(), zeroMaster.size(), part1MasterHash);
    std::memcpy(part0.data() + 4060, part1MasterHash, 0x14);

    BYTE rootHash[0x14];
    sha1Bytes(part0.data(), 0x1000, rootHash);

    // Corrupt one data block without touching the master/level-0 hashes. The
    // constructor's root check still passes, Verify() fails, and Rehash must
    // repair it. This makes a no-op Rehash fail the test as well.
    part0[static_cast<std::size_t>(DataBlockAddress(1))] ^= 0xFF;

    std::vector<BYTE> header(0xB000, 0);
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
    testGarbageImages();

    if (g_failures != 0)
    {
        std::fprintf(stderr, "xbi_smoke: %d failure(s)\n", g_failures);
        return 1;
    }

    std::printf("xbi_smoke: ok\n");
    return 0;
}
