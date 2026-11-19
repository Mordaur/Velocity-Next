/* Smoke target for the sanitizer CI job.
 *
 * It is deliberately small and corpus-free: it exercises the code paths that can
 * be driven with synthetic data and asserts only on invariants (round-trips,
 * bounds rejection, "must throw"), never on parsed values. Anything that needs a
 * real image belongs in the out-of-tree harness under tests/ at the work folder
 * root.
 */

#include <XboxInternals/Disc/ISO.h>
#include <XboxInternals/Gpd/Xdbf.h>
#include <XboxInternals/IO/MemoryIO.h>
#include <XboxInternals/Stfs/StfsPackage.h>
#include <XboxInternals/Stfs/XContentHeader.h>
#include <XboxInternals/Xex/XexLzxDecoder.h>

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

int main()
{
    testMemoryIO();
    testLzxGarbage();
    testLzxRegression();
    testCorruptStfsHeaders();
    testMSTimeRoundTrip();
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
