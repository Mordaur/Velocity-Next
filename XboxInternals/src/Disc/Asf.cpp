#include <XboxInternals/Disc/Asf.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace
{
    // ASF GUIDs are stored little-endian in the first three fields.
    constexpr std::array<u8, 0x10> kHeaderObject = {
        0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
        0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C
    };

    constexpr std::array<u8, 0x10> kContentEncryption = {
        0xFB, 0xB3, 0x11, 0x22, 0x23, 0xBD, 0xD2, 0x11,
        0xB4, 0xB7, 0x00, 0xA0, 0xC9, 0x55, 0xFC, 0x6E
    };

    constexpr std::array<u8, 0x10> kExtendedContentEncryption = {
        0x14, 0xE6, 0x8A, 0x29, 0x22, 0x26, 0x17, 0x4C,
        0xB9, 0x35, 0xDA, 0xE0, 0x7E, 0xE9, 0x28, 0x9C
    };

    constexpr std::array<u8, 0x10> kAdvancedContentEncryption = {
        0x33, 0x85, 0x05, 0x43, 0x81, 0x69, 0xE6, 0x49,
        0x9B, 0x74, 0xAD, 0x12, 0xCB, 0x86, 0xD5, 0x8C
    };

    u64 ReadLe64(const u8 *data)
    {
        u64 value = 0;
        for (int i = 7; i >= 0; --i)
            value = (value << 8) | data[i];
        return value;
    }

    bool ContainsGuid(const std::vector<u8> &data, const std::array<u8, 0x10> &guid)
    {
        if (data.size() < guid.size())
            return false;

        const auto end = data.end() - static_cast<std::ptrdiff_t>(guid.size()) + 1;
        return std::search(data.begin(), end, guid.begin(), guid.end()) != end;
    }

    // Keeps the caller's position intact whether the probe succeeds or throws.
    struct PositionGuard
    {
        BaseIO *io;
        u64 position;
        explicit PositionGuard(BaseIO *stream) : io(stream), position(stream->GetPosition()) {}
        ~PositionGuard()
        {
            try { io->SetPosition(position); } catch (...) {}
        }
    };
}

bool XboxInternals::Asf::IsAsf(BaseIO *io)
{
    if (io == nullptr)
        return false;

    try
    {
        PositionGuard guard(io);

        std::array<u8, 0x10> header{};
        io->SetPosition(0);
        io->ReadBytes(header.data(), header.size());
        return header == kHeaderObject;
    }
    catch (...)
    {
        return false;
    }
}

bool XboxInternals::Asf::IsDrmProtected(BaseIO *io)
{
    if (!IsAsf(io))
        return false;

    try
    {
        PositionGuard guard(io);

        std::array<u8, 0x20> prefix{};
        io->SetPosition(0);
        io->ReadBytes(prefix.data(), prefix.size());

        const u64 headerSize = ReadLe64(prefix.data() + 0x10);
        if (headerSize < prefix.size())
            return false;

        // The header is small in practice; bound the read so a corrupt size
        // cannot make the probe allocate or read the whole file.
        const u64 available = io->Length();
        const u64 toRead = std::min<u64>({headerSize, available, 0x100000});

        std::vector<u8> header(static_cast<std::size_t>(toRead));
        io->SetPosition(0);
        io->ReadBytes(header.data(), static_cast<u32>(header.size()));

        return ContainsGuid(header, kContentEncryption) ||
                ContainsGuid(header, kExtendedContentEncryption) ||
                ContainsGuid(header, kAdvancedContentEncryption);
    }
    catch (const std::string &)
    {
        return false;
    }
    catch (const std::exception &)
    {
        return false;
    }
    catch (...)
    {
        return false;
    }
}
