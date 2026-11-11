#include "protocol.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace velocity::helper
{
    namespace
    {
        void append_u16_impl(std::vector<uint8_t> &out, uint16_t value)
        {
            out.push_back(static_cast<uint8_t>(value & 0xFF));
            out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        }

        void append_u32_impl(std::vector<uint8_t> &out, uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
        }

        void append_u64_impl(std::vector<uint8_t> &out, uint64_t value)
        {
            for (int i = 0; i < 8; ++i)
                out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
        }

        bool read_u16_impl(const uint8_t *data, size_t length, size_t &offset, uint16_t &value)
        {
            if (offset + 2 > length)
                return false;
            value = static_cast<uint16_t>(data[offset]) |
                    static_cast<uint16_t>(data[offset + 1] << 8);
            offset += 2;
            return true;
        }

        bool read_u32_impl(const uint8_t *data, size_t length, size_t &offset, uint32_t &value)
        {
            if (offset + 4 > length)
                return false;
            value = 0;
            for (int i = 0; i < 4; ++i)
                value |= static_cast<uint32_t>(data[offset + i]) << (8 * i);
            offset += 4;
            return true;
        }

        bool read_u64_impl(const uint8_t *data, size_t length, size_t &offset, uint64_t &value)
        {
            if (offset + 8 > length)
                return false;
            value = 0;
            for (int i = 0; i < 8; ++i)
                value |= static_cast<uint64_t>(data[offset + i]) << (8 * i);
            offset += 8;
            return true;
        }
    } // namespace

    void append_u16(std::vector<uint8_t> &out, uint16_t value)
    {
        append_u16_impl(out, value);
    }

    void append_u32(std::vector<uint8_t> &out, uint32_t value)
    {
        append_u32_impl(out, value);
    }

    void append_u64(std::vector<uint8_t> &out, uint64_t value)
    {
        append_u64_impl(out, value);
    }

    void append_bytes(std::vector<uint8_t> &out, const void *data, size_t length)
    {
        const auto *bytes = static_cast<const uint8_t *>(data);
        out.insert(out.end(), bytes, bytes + length);
    }

    bool read_u16(const uint8_t *data, size_t length, size_t &offset, uint16_t &value)
    {
        return read_u16_impl(data, length, offset, value);
    }

    bool read_u32(const uint8_t *data, size_t length, size_t &offset, uint32_t &value)
    {
        return read_u32_impl(data, length, offset, value);
    }

    bool read_u64(const uint8_t *data, size_t length, size_t &offset, uint64_t &value)
    {
        return read_u64_impl(data, length, offset, value);
    }

    std::vector<uint8_t> encode_header(const Header &header)
    {
        std::vector<uint8_t> out;
        out.reserve(kHeaderSize);
        append_u32_impl(out, header.magic);
        append_u16_impl(out, header.version);
        append_u16_impl(out, static_cast<uint16_t>(header.type));
        append_u32_impl(out, header.requestId);
        append_u32_impl(out, header.payloadLength);
        return out;
    }

    bool decode_header(const uint8_t *data, size_t length, Header &header)
    {
        if (length < kHeaderSize)
            return false;

        size_t offset = 0;
        uint16_t type = 0;
        if (!read_u32_impl(data, length, offset, header.magic) ||
            !read_u16_impl(data, length, offset, header.version) ||
            !read_u16_impl(data, length, offset, type) ||
            !read_u32_impl(data, length, offset, header.requestId) ||
            !read_u32_impl(data, length, offset, header.payloadLength))
            return false;

        if (header.magic != kMagic || header.version != kVersion)
            return false;
        if (header.payloadLength > kMaxPayload)
            return false;

        header.type = static_cast<MessageType>(type);
        return true;
    }

    std::vector<uint8_t> encode_error(ErrorCode code, const std::string &message)
    {
        std::vector<uint8_t> out;
        append_u32(out, static_cast<uint32_t>(code));
        append_u32(out, static_cast<uint32_t>(message.size()));
        append_bytes(out, message.data(), message.size());
        return out;
    }

#if defined(_WIN32)
    bool read_full(StreamHandle stream, void *data, size_t length)
    {
        auto *bytes = static_cast<uint8_t *>(data);
        const HANDLE handle = static_cast<HANDLE>(stream);
        size_t offset = 0;
        while (offset < length)
        {
            const DWORD request = static_cast<DWORD>(
                    std::min<size_t>(length - offset, kMaxPayload));
            DWORD count = 0;
            if (!ReadFile(handle, bytes + offset, request, &count, nullptr))
            {
                if (GetLastError() == ERROR_OPERATION_ABORTED)
                    return false;
                return false;
            }
            if (count == 0)
                return false;
            offset += count;
        }
        return true;
    }

    bool write_full(StreamHandle stream, const void *data, size_t length)
    {
        const auto *bytes = static_cast<const uint8_t *>(data);
        const HANDLE handle = static_cast<HANDLE>(stream);
        size_t offset = 0;
        while (offset < length)
        {
            const DWORD request = static_cast<DWORD>(
                    std::min<size_t>(length - offset, kMaxPayload));
            DWORD count = 0;
            if (!WriteFile(handle, bytes + offset, request, &count, nullptr))
                return false;
            if (count == 0)
                return false;
            offset += count;
        }
        return true;
    }
#else
    bool read_full(StreamHandle fd, void *data, size_t length)
    {
        auto *bytes = static_cast<uint8_t *>(data);
        size_t offset = 0;
        while (offset < length)
        {
            const ssize_t count = ::read(fd, bytes + offset, length - offset);
            if (count == 0)
                return false;
            if (count < 0)
            {
                if (errno == EINTR)
                    continue;
                return false;
            }
            offset += static_cast<size_t>(count);
        }
        return true;
    }

    bool write_full(StreamHandle fd, const void *data, size_t length)
    {
        const auto *bytes = static_cast<const uint8_t *>(data);
        size_t offset = 0;
        while (offset < length)
        {
            const ssize_t count = ::write(fd, bytes + offset, length - offset);
            if (count < 0)
            {
                if (errno == EINTR)
                    continue;
                return false;
            }
            offset += static_cast<size_t>(count);
        }
        return true;
    }
#endif
} // namespace velocity::helper
