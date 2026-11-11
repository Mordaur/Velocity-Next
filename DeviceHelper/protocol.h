#ifndef VELOCITY_DEVICE_HELPER_PROTOCOL_H
#define VELOCITY_DEVICE_HELPER_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace velocity::helper
{
    constexpr uint32_t kMagic = 0x31484456; // "VDH1" on the wire as little-endian
    constexpr uint16_t kVersion = 1;
    constexpr size_t kHeaderSize = 16;
    constexpr uint32_t kMaxPayload = 1024 * 1024;
    constexpr uint32_t kMaxHandles = 16;

    enum class MessageType : uint16_t
    {
        Hello = 1,
        Open = 2,
        Read = 3,
        Write = 4,
        Length = 5,
        Close = 6,
        Error = 7,
        Ping = 8
    };

    enum class OpenMode : uint8_t
    {
        ReadOnly = 0,
        ReadWrite = 1
    };

    enum class ErrorCode : uint32_t
    {
        None = 0,
        InvalidRequest = 1,
        AccessDenied = 2,
        NotFound = 3,
        IoError = 4,
        OutOfHandles = 5,
        RangeError = 6,
        Unsupported = 7
    };

    struct Header
    {
        uint32_t magic = 0;
        uint16_t version = 0;
        MessageType type = MessageType::Error;
        uint32_t requestId = 0;
        uint32_t payloadLength = 0;
    };

    struct DeviceInfo
    {
        uint64_t size = 0;
        bool writable = false;
    };

    void append_u16(std::vector<uint8_t> &out, uint16_t value);
    void append_u32(std::vector<uint8_t> &out, uint32_t value);
    void append_u64(std::vector<uint8_t> &out, uint64_t value);
    void append_bytes(std::vector<uint8_t> &out, const void *data, size_t length);

    bool read_u16(const uint8_t *data, size_t length, size_t &offset, uint16_t &value);
    bool read_u32(const uint8_t *data, size_t length, size_t &offset, uint32_t &value);
    bool read_u64(const uint8_t *data, size_t length, size_t &offset, uint64_t &value);

    std::vector<uint8_t> encode_header(const Header &header);
    bool decode_header(const uint8_t *data, size_t length, Header &header);
    std::vector<uint8_t> encode_error(ErrorCode code, const std::string &message);

#if defined(_WIN32)
    // Win32 HANDLE (named pipe or device handle).
    using StreamHandle = void *;
#else
    // POSIX file descriptor.
    using StreamHandle = int;
#endif

    // Blocking helpers used for IPC framing. They return false on a closed or
    // broken stream and only succeed after the full buffer has been produced.
    bool read_full(StreamHandle stream, void *data, size_t length);
    bool write_full(StreamHandle stream, const void *data, size_t length);
} // namespace velocity::helper

#endif // VELOCITY_DEVICE_HELPER_PROTOCOL_H
