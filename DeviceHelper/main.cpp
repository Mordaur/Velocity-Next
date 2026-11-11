#include "protocol.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/ioctl.h>
#elif defined(__APPLE__)
#include <sys/disk.h>
#include <sys/ioctl.h>
#endif
#endif

namespace vh = velocity::helper;

namespace
{
    constexpr uint32_t kSectorFallback = 512;
    constexpr size_t kMaxIoChunk = 1024 * 1024;
    constexpr int kConnectTimeoutSeconds = 120;

    struct Device
    {
        uint64_t size = 0;
        bool writable = false;
#if defined(_WIN32)
        HANDLE handle = INVALID_HANDLE_VALUE;
        uint32_t sectorSize = kSectorFallback;
#else
        int fd = -1;
#endif
    };

    std::map<uint32_t, Device> g_devices;
    uint32_t g_nextHandle = 1;

    void send_frame(vh::StreamHandle stream, vh::MessageType type, uint32_t requestId,
                    const std::vector<uint8_t> &payload)
    {
        vh::Header header;
        header.magic = vh::kMagic;
        header.version = vh::kVersion;
        header.type = type;
        header.requestId = requestId;
        header.payloadLength = static_cast<uint32_t>(payload.size());

        const auto encoded = vh::encode_header(header);
        vh::write_full(stream, encoded.data(), encoded.size());
        if (!payload.empty())
            vh::write_full(stream, payload.data(), payload.size());
    }

    void send_error(vh::StreamHandle stream, uint32_t requestId, vh::ErrorCode code,
                    const std::string &message)
    {
        send_frame(stream, vh::MessageType::Error, requestId, vh::encode_error(code, message));
    }

#if defined(_WIN32)
    std::wstring utf8_to_wide(const std::string &text)
    {
        if (text.empty())
            return {};

        const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                               static_cast<int>(text.size()), nullptr, 0);
        if (length <= 0)
            return {};

        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                            wide.data(), length);
        return wide;
    }

    std::string windows_error_text(DWORD error)
    {
        char *buffer = nullptr;
        const DWORD length = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, error, 0, reinterpret_cast<char *>(&buffer), 0, nullptr);

        std::string text = length > 0 ? std::string(buffer, length)
                                      : "windows error " + std::to_string(error);
        if (buffer)
            LocalFree(buffer);

        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
            text.pop_back();
        return text;
    }

    vh::ErrorCode map_windows_error(DWORD error)
    {
        switch (error)
        {
        case ERROR_ACCESS_DENIED:
        case ERROR_PRIVILEGE_NOT_HELD:
            return vh::ErrorCode::AccessDenied;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_DRIVE:
            return vh::ErrorCode::NotFound;
        case ERROR_WRITE_PROTECT:
            return vh::ErrorCode::AccessDenied;
        default:
            return vh::ErrorCode::IoError;
        }
    }

    // Only raw disks (\\.\PHYSICALDRIVE<n>) and volumes (\\.\X:) are accepted.
    bool validate_device_path(const std::string &path)
    {
        std::string lower(path);
        for (char &character : lower)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

        const std::string prefix = "\\\\.\\";
        if (lower.rfind(prefix, 0) != 0)
            return false;

        const std::string rest = lower.substr(prefix.size());
        const std::string physical = "physicaldrive";
        if (rest.rfind(physical, 0) == 0)
        {
            const std::string digits = rest.substr(physical.size());
            if (digits.empty() || digits.size() > 4)
                return false;
            return std::all_of(digits.begin(), digits.end(),
                               [](char character) { return character >= '0' && character <= '9'; });
        }

        return rest.size() == 2 && rest[0] >= 'a' && rest[0] <= 'z' && rest[1] == ':';
    }

    bool read_aligned(HANDLE handle, uint64_t start, size_t length, std::vector<uint8_t> &buffer)
    {
        buffer.assign(length, 0);

        LARGE_INTEGER position {};
        position.QuadPart = static_cast<LONGLONG>(start);
        if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN))
            return false;

        size_t done = 0;
        while (done < length)
        {
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(length - done, kMaxIoChunk));
            DWORD count = 0;
            if (!ReadFile(handle, buffer.data() + done, chunk, &count, nullptr) || count == 0)
                return false;
            done += count;
        }
        return true;
    }

    bool open_device(const std::string &path, bool wantWritable, Device &device,
                     vh::ErrorCode &code, std::string &message)
    {
        const std::wstring widePath = utf8_to_wide(path);
        if (widePath.empty())
        {
            code = vh::ErrorCode::InvalidRequest;
            message = "invalid device path";
            return false;
        }

        HANDLE handle = CreateFileW(widePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                    0, nullptr);
        bool writable = wantWritable;

        if (handle == INVALID_HANDLE_VALUE && wantWritable)
        {
            handle = CreateFileW(widePath.c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                 0, nullptr);
            writable = false;
        }

        if (handle == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            code = map_windows_error(error);
            message = "open failed: " + windows_error_text(error);
            return false;
        }

        GET_LENGTH_INFORMATION lengthInfo {};
        DWORD returned = 0;
        if (!DeviceIoControl(handle, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &lengthInfo,
                             sizeof(lengthInfo), &returned, nullptr))
        {
            const DWORD error = GetLastError();
            CloseHandle(handle);
            code = map_windows_error(error);
            message = "could not determine device length: " + windows_error_text(error);
            return false;
        }

        DISK_GEOMETRY geometry {};
        if (DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY, nullptr, 0, &geometry,
                            sizeof(geometry), &returned, nullptr) &&
            geometry.BytesPerSector != 0)
        {
            device.sectorSize = geometry.BytesPerSector;
        }
        else
        {
            device.sectorSize = kSectorFallback;
        }

        device.handle = handle;
        device.size = static_cast<uint64_t>(lengthInfo.Length.QuadPart);
        device.writable = writable;
        return true;
    }

    void close_device(Device &device)
    {
        if (device.handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(device.handle);
            device.handle = INVALID_HANDLE_VALUE;
        }
    }

    bool read_device(Device &device, uint64_t position, uint8_t *out, size_t length,
                     size_t &done, vh::ErrorCode &code, std::string &message)
    {
        const uint32_t sector = device.sectorSize != 0 ? device.sectorSize : kSectorFallback;
        const uint64_t start = position - (position % sector);
        const size_t lead = static_cast<size_t>(position - start);
        const size_t window = ((lead + length + sector - 1) / sector) * sector;

        std::vector<uint8_t> buffer;
        if (!read_aligned(device.handle, start, window, buffer))
        {
            code = vh::ErrorCode::IoError;
            message = "read failed: " + windows_error_text(GetLastError());
            return false;
        }

        std::memcpy(out, buffer.data() + lead, length);
        done = length;
        return true;
    }

    bool write_device(Device &device, uint64_t position, const uint8_t *data, size_t length,
                      size_t &done, vh::ErrorCode &code, std::string &message)
    {
        const uint32_t sector = device.sectorSize != 0 ? device.sectorSize : kSectorFallback;

        // Raw disk writes must be sector aligned; do a read-modify-write when the
        // caller asks for a partial sector.
        if ((position % sector) == 0 && (length % sector) == 0)
        {
            LARGE_INTEGER startPosition {};
            startPosition.QuadPart = static_cast<LONGLONG>(position);
            if (!SetFilePointerEx(device.handle, startPosition, nullptr, FILE_BEGIN))
            {
                code = vh::ErrorCode::IoError;
                message = "seek failed: " + windows_error_text(GetLastError());
                return false;
            }

            size_t written = 0;
            while (written < length)
            {
                const DWORD chunk = static_cast<DWORD>(
                        std::min<size_t>(length - written, kMaxIoChunk));
                DWORD count = 0;
                if (!WriteFile(device.handle, data + written, chunk, &count, nullptr) || count == 0)
                {
                    code = vh::ErrorCode::IoError;
                    message = "write failed: " + windows_error_text(GetLastError());
                    return false;
                }
                written += count;
            }

            done = written;
            return true;
        }

        const uint64_t start = position - (position % sector);
        const size_t lead = static_cast<size_t>(position - start);
        const size_t window = ((lead + length + sector - 1) / sector) * sector;

        std::vector<uint8_t> buffer;
        if (!read_aligned(device.handle, start, window, buffer))
        {
            code = vh::ErrorCode::IoError;
            message = "read-modify-write failed: " + windows_error_text(GetLastError());
            return false;
        }

        std::memcpy(buffer.data() + lead, data, length);

        LARGE_INTEGER startPosition {};
        startPosition.QuadPart = static_cast<LONGLONG>(start);
        if (!SetFilePointerEx(device.handle, startPosition, nullptr, FILE_BEGIN))
        {
            code = vh::ErrorCode::IoError;
            message = "seek failed: " + windows_error_text(GetLastError());
            return false;
        }

        size_t written = 0;
        while (written < window)
        {
            const DWORD chunk = static_cast<DWORD>(
                    std::min<size_t>(window - written, kMaxIoChunk));
            DWORD count = 0;
            if (!WriteFile(device.handle, buffer.data() + written, chunk, &count, nullptr) ||
                count == 0)
            {
                code = vh::ErrorCode::IoError;
                message = "write failed: " + windows_error_text(GetLastError());
                return false;
            }
            written += count;
        }

        done = length;
        return true;
    }
#else
    bool read_device_length(int fd, uint64_t &size)
    {
#if defined(__linux__)
        uint64_t bytes = 0;
        if (ioctl(fd, BLKGETSIZE64, &bytes) == 0)
        {
            size = bytes;
            return true;
        }
#elif defined(__APPLE__)
        uint64_t blocks = 0;
        uint32_t blockSize = 0;
        if (ioctl(fd, DKIOCGETBLOCKCOUNT, &blocks) == 0 &&
            ioctl(fd, DKIOCGETBLOCKSIZE, &blockSize) == 0)
        {
            size = blocks * blockSize;
            return true;
        }
#endif
        const off_t end = lseek(fd, 0, SEEK_END);
        if (end < 0)
            return false;
        size = static_cast<uint64_t>(end);
        return true;
    }

    bool validate_device_path(const std::string &path)
    {
        if (path.rfind("/dev/", 0) != 0)
            return false;
        if (path.find("..") != std::string::npos)
            return false;
        return path.size() > 5 && path.size() < 4096;
    }

    bool open_device(const std::string &path, bool wantWritable, Device &device,
                     vh::ErrorCode &code, std::string &message)
    {
        bool writable = wantWritable;
        int fd = open(path.c_str(), (writable ? O_RDWR : O_RDONLY) | O_CLOEXEC);
        if (fd < 0 && writable && (errno == EACCES || errno == EROFS))
        {
            fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
            writable = false;
        }
        if (fd < 0)
        {
            code = errno == EACCES ? vh::ErrorCode::AccessDenied : vh::ErrorCode::IoError;
            message = std::string("open failed: ") + std::strerror(errno);
            return false;
        }

        struct stat status {};
        if (fstat(fd, &status) != 0 || !S_ISBLK(status.st_mode))
        {
            close(fd);
            code = vh::ErrorCode::InvalidRequest;
            message = "not a block device";
            return false;
        }

        uint64_t size = 0;
        if (!read_device_length(fd, size))
        {
            close(fd);
            code = vh::ErrorCode::IoError;
            message = "could not determine device length";
            return false;
        }

        device.fd = fd;
        device.size = size;
        device.writable = writable;
        return true;
    }

    void close_device(Device &device)
    {
        if (device.fd >= 0)
        {
            close(device.fd);
            device.fd = -1;
        }
    }

    bool read_device(Device &device, uint64_t position, uint8_t *out, size_t length,
                     size_t &done, vh::ErrorCode &code, std::string &message)
    {
        size_t offset = 0;
        while (offset < length)
        {
            const ssize_t count = pread(device.fd, out + offset, length - offset,
                                        static_cast<off_t>(position + offset));
            if (count < 0)
            {
                if (errno == EINTR)
                    continue;
                code = vh::ErrorCode::IoError;
                message = std::string("read failed: ") + std::strerror(errno);
                return false;
            }
            if (count == 0)
                break;
            offset += static_cast<size_t>(count);
        }

        done = offset;
        return true;
    }

    bool write_device(Device &device, uint64_t position, const uint8_t *data, size_t length,
                      size_t &done, vh::ErrorCode &code, std::string &message)
    {
        size_t offset = 0;
        while (offset < length)
        {
            const ssize_t count = pwrite(device.fd, data + offset, length - offset,
                                         static_cast<off_t>(position + offset));
            if (count < 0)
            {
                if (errno == EINTR)
                    continue;
                code = vh::ErrorCode::IoError;
                message = std::string("write failed: ") + std::strerror(errno);
                return false;
            }
            offset += static_cast<size_t>(count);
        }

        done = offset;
        return true;
    }
#endif

    void handle_open(vh::StreamHandle stream, uint32_t requestId,
                     const std::vector<uint8_t> &payload)
    {
        if (payload.size() < 5)
        {
            send_error(stream, requestId, vh::ErrorCode::InvalidRequest, "invalid open request");
            return;
        }

        const uint8_t mode = payload[0];
        size_t offset = 1;
        uint32_t pathLength = 0;

        if (!vh::read_u32(payload.data(), payload.size(), offset, pathLength) ||
            offset + pathLength > payload.size() ||
            pathLength == 0 ||
            pathLength > 4096)
        {
            send_error(stream, requestId, vh::ErrorCode::InvalidRequest, "invalid device path");
            return;
        }

        const std::string path(reinterpret_cast<const char *>(payload.data() + offset), pathLength);
        if (!validate_device_path(path))
        {
            send_error(stream, requestId, vh::ErrorCode::InvalidRequest, "path is not a device node");
            return;
        }

        if (g_devices.size() >= vh::kMaxHandles)
        {
            send_error(stream, requestId, vh::ErrorCode::OutOfHandles, "too many open handles");
            return;
        }

        const bool writable = mode == static_cast<uint8_t>(vh::OpenMode::ReadWrite);

        Device device;
        vh::ErrorCode code = vh::ErrorCode::None;
        std::string message;
        if (!open_device(path, writable, device, code, message))
        {
            send_error(stream, requestId, code, message);
            return;
        }

        const uint32_t handle = g_nextHandle++;
        g_devices[handle] = device;

        std::vector<uint8_t> response;
        vh::append_u32(response, handle);
        vh::append_u64(response, device.size);
        response.push_back(device.writable ? 1 : 0);
        send_frame(stream, vh::MessageType::Open, requestId, response);
    }

    void handle_read(vh::StreamHandle stream, uint32_t requestId,
                     const std::vector<uint8_t> &payload)
    {
        size_t offset = 0;
        uint32_t handle = 0;
        uint64_t position = 0;
        uint32_t length = 0;

        if (!vh::read_u32(payload.data(), payload.size(), offset, handle) ||
            !vh::read_u64(payload.data(), payload.size(), offset, position) ||
            !vh::read_u32(payload.data(), payload.size(), offset, length) ||
            length > vh::kMaxPayload)
        {
            send_error(stream, requestId, vh::ErrorCode::InvalidRequest, "invalid read request");
            return;
        }

        auto it = g_devices.find(handle);
        if (it == g_devices.end())
        {
            send_error(stream, requestId, vh::ErrorCode::NotFound, "unknown handle");
            return;
        }

        Device &device = it->second;
        if (position > device.size || length > device.size - position)
        {
            send_error(stream, requestId, vh::ErrorCode::RangeError, "read outside device");
            return;
        }

        std::vector<uint8_t> data(length);
        size_t done = 0;
        vh::ErrorCode code = vh::ErrorCode::None;
        std::string message;
        if (!read_device(device, position, data.data(), length, done, code, message))
        {
            send_error(stream, requestId, code, message);
            return;
        }

        data.resize(done);
        send_frame(stream, vh::MessageType::Read, requestId, data);
    }

    void handle_write(vh::StreamHandle stream, uint32_t requestId,
                      const std::vector<uint8_t> &payload)
    {
        size_t offset = 0;
        uint32_t handle = 0;
        uint64_t position = 0;
        uint32_t length = 0;

        if (!vh::read_u32(payload.data(), payload.size(), offset, handle) ||
            !vh::read_u64(payload.data(), payload.size(), offset, position) ||
            !vh::read_u32(payload.data(), payload.size(), offset, length) ||
            offset + length > payload.size() ||
            length > vh::kMaxPayload)
        {
            send_error(stream, requestId, vh::ErrorCode::InvalidRequest, "invalid write request");
            return;
        }

        auto it = g_devices.find(handle);
        if (it == g_devices.end())
        {
            send_error(stream, requestId, vh::ErrorCode::NotFound, "unknown handle");
            return;
        }

        Device &device = it->second;
        if (!device.writable)
        {
            send_error(stream, requestId, vh::ErrorCode::AccessDenied, "handle is read-only");
            return;
        }
        if (position > device.size || length > device.size - position)
        {
            send_error(stream, requestId, vh::ErrorCode::RangeError, "write outside device");
            return;
        }

        size_t done = 0;
        vh::ErrorCode code = vh::ErrorCode::None;
        std::string message;
        if (!write_device(device, position, payload.data() + offset, length, done, code, message))
        {
            send_error(stream, requestId, code, message);
            return;
        }

        std::vector<uint8_t> response;
        vh::append_u32(response, static_cast<uint32_t>(done));
        send_frame(stream, vh::MessageType::Write, requestId, response);
    }

    void handle_connection(vh::StreamHandle stream)
    {
        while (true)
        {
            uint8_t headerBytes[vh::kHeaderSize];
            if (!vh::read_full(stream, headerBytes, sizeof(headerBytes)))
                return;

            vh::Header header;
            if (!vh::decode_header(headerBytes, sizeof(headerBytes), header))
                return;

            std::vector<uint8_t> payload(header.payloadLength);
            if (!payload.empty() && !vh::read_full(stream, payload.data(), payload.size()))
                return;

            switch (header.type)
            {
            case vh::MessageType::Hello:
            {
                std::vector<uint8_t> response;
                vh::append_u32(response, vh::kVersion);
                vh::append_u32(response, 3); // read, write, range validation
                send_frame(stream, vh::MessageType::Hello, header.requestId, response);
                break;
            }
            case vh::MessageType::Open:
                handle_open(stream, header.requestId, payload);
                break;
            case vh::MessageType::Read:
                handle_read(stream, header.requestId, payload);
                break;
            case vh::MessageType::Write:
                handle_write(stream, header.requestId, payload);
                break;
            case vh::MessageType::Length:
            {
                size_t offset = 0;
                uint32_t handle = 0;
                if (!vh::read_u32(payload.data(), payload.size(), offset, handle))
                {
                    send_error(stream, header.requestId, vh::ErrorCode::InvalidRequest, "invalid length request");
                    break;
                }
                auto it = g_devices.find(handle);
                if (it == g_devices.end())
                {
                    send_error(stream, header.requestId, vh::ErrorCode::NotFound, "unknown handle");
                    break;
                }
                std::vector<uint8_t> response;
                vh::append_u64(response, it->second.size);
                send_frame(stream, vh::MessageType::Length, header.requestId, response);
                break;
            }
            case vh::MessageType::Close:
            {
                size_t offset = 0;
                uint32_t handle = 0;
                if (!vh::read_u32(payload.data(), payload.size(), offset, handle))
                {
                    send_error(stream, header.requestId, vh::ErrorCode::InvalidRequest, "invalid close request");
                    break;
                }
                auto it = g_devices.find(handle);
                if (it == g_devices.end())
                {
                    send_error(stream, header.requestId, vh::ErrorCode::NotFound, "unknown handle");
                    break;
                }
                close_device(it->second);
                g_devices.erase(it);
                send_frame(stream, vh::MessageType::Close, header.requestId, {});
                break;
            }
            case vh::MessageType::Ping:
                send_frame(stream, vh::MessageType::Ping, header.requestId, {});
                break;
            default:
                send_error(stream, header.requestId, vh::ErrorCode::Unsupported, "unsupported message");
                break;
            }
        }
    }

    void close_all_devices()
    {
        for (auto &entry : g_devices)
            close_device(entry.second);
        g_devices.clear();
    }

#if defined(_WIN32)
    bool enable_security_privilege()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(),
                              TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
            return false;

        LUID luid {};
        if (!LookupPrivilegeValueW(nullptr, L"SeSecurityPrivilege", &luid))
        {
            CloseHandle(token);
            return false;
        }

        TOKEN_PRIVILEGES privileges {};
        privileges.PrivilegeCount = 1;
        privileges.Privileges[0].Luid = luid;
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        const bool ok = AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr) != 0 &&
                        GetLastError() == ERROR_SUCCESS;
        CloseHandle(token);
        return ok;
    }

    // Allow the unprivileged (medium integrity) GUI to talk to a pipe created by
    // this elevated process: grant the owning user full access and label the
    // pipe medium integrity instead of inheriting the creator's high label.
    bool build_pipe_security(PSECURITY_DESCRIPTOR &descriptor, SECURITY_ATTRIBUTES &attributes)
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;

        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<uint8_t> buffer(size);
        const bool tokenOk =
                GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != 0;
        CloseHandle(token);
        if (!tokenOk || size == 0)
            return false;

        auto *user = reinterpret_cast<TOKEN_USER *>(buffer.data());
        LPWSTR sidText = nullptr;
        if (!ConvertSidToStringSidW(user->User.Sid, &sidText))
            return false;

        const std::wstring dacl = std::wstring(L"D:(A;;GA;;;") + sidText + L")";
        LocalFree(sidText);

        // Creating a mandatory label needs SeSecurityPrivilege. Build the whole
        // descriptor (DACL + label) in one SDDL string so it stays self-relative,
        // which is the form CreateNamedPipe expects.
        enable_security_privilege();

        const std::wstring labelled = dacl + L"S:(ML;;NW;;;ME)";
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                    labelled.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
        {
            attributes.nLength = sizeof(attributes);
            attributes.lpSecurityDescriptor = descriptor;
            attributes.bInheritHandle = FALSE;
            return true;
        }

        // Falling back to a DACL-only descriptor keeps the helper usable when the
        // label cannot be set, but the GUI must then run elevated to connect.
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                    dacl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
            return false;

        attributes.nLength = sizeof(attributes);
        attributes.lpSecurityDescriptor = descriptor;
        attributes.bInheritHandle = FALSE;
        return true;
    }

    bool valid_endpoint_name(const std::string &name)
    {
        if (name.empty() || name.size() > 200)
            return false;
        return std::all_of(name.begin(), name.end(), [](char character) {
            return (character >= 'a' && character <= 'z') ||
                   (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') ||
                   character == '-' || character == '_';
        });
    }

    int run_server(const std::string &endpoint)
    {
        if (!valid_endpoint_name(endpoint))
        {
            std::fprintf(stderr, "invalid endpoint name\n");
            return 2;
        }

        PSECURITY_DESCRIPTOR descriptor = nullptr;
        SECURITY_ATTRIBUTES attributes {};
        if (!build_pipe_security(descriptor, attributes))
        {
            const DWORD error = GetLastError();
            std::fprintf(stderr, "could not build pipe security: %s\n",
                         windows_error_text(error).c_str());
            if (descriptor)
                LocalFree(descriptor);
            return 1;
        }

        const std::wstring pipeName = L"\\\\.\\pipe\\" + utf8_to_wide(endpoint);
        HANDLE pipe = CreateNamedPipeW(
                pipeName.c_str(),
                PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                1, 64 * 1024, 64 * 1024, 0, &attributes);

        if (pipe == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            std::fprintf(stderr, "CreateNamedPipe failed: %s\n",
                         windows_error_text(error).c_str());
            LocalFree(descriptor);
            return 1;
        }

        // Exit instead of lingering elevated when the GUI never connects.
        std::atomic<bool> connected {false};
        std::thread([&connected]() {
            for (int second = 0; second < kConnectTimeoutSeconds; ++second)
            {
                if (connected.load())
                    return;
                Sleep(1000);
            }
            if (!connected.load())
                ExitProcess(0);
        }).detach();

        if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED)
        {
            const DWORD error = GetLastError();
            std::fprintf(stderr, "ConnectNamedPipe failed: %s\n",
                         windows_error_text(error).c_str());
            CloseHandle(pipe);
            LocalFree(descriptor);
            return 1;
        }

        connected.store(true);
        handle_connection(static_cast<vh::StreamHandle>(pipe));
        close_all_devices();

        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        LocalFree(descriptor);
        return 0;
    }
#else
    int run_server(const std::string &endpoint, uid_t allowedUid)
    {
        signal(SIGPIPE, SIG_IGN);

        const int server = socket(AF_UNIX, SOCK_STREAM, 0);
        if (server < 0)
        {
            std::fprintf(stderr, "socket: %s\n", std::strerror(errno));
            return 1;
        }

        sockaddr_un addr {};
        addr.sun_family = AF_UNIX;
        if (endpoint.size() >= sizeof(addr.sun_path))
        {
            std::fprintf(stderr, "endpoint path too long\n");
            close(server);
            return 1;
        }
        std::strncpy(addr.sun_path, endpoint.c_str(), sizeof(addr.sun_path) - 1);
        unlink(endpoint.c_str());

        if (bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
            chown(endpoint.c_str(), allowedUid, static_cast<gid_t>(-1)) != 0 ||
            chmod(endpoint.c_str(), 0600) != 0 ||
            listen(server, 1) != 0)
        {
            std::fprintf(stderr, "bind/listen: %s\n", std::strerror(errno));
            close(server);
            unlink(endpoint.c_str());
            return 1;
        }

        // Do not linger privileged if the GUI never connects.
        alarm(static_cast<unsigned int>(kConnectTimeoutSeconds));

        const int client = accept(server, nullptr, nullptr);
        alarm(0);

        if (client < 0)
        {
            std::fprintf(stderr, "accept: %s\n", std::strerror(errno));
            close(server);
            unlink(endpoint.c_str());
            return 1;
        }

        bool peerAllowed = false;
#if defined(__linux__)
        ucred credentials {};
        socklen_t credentialsLength = sizeof(credentials);
        peerAllowed = getsockopt(client, SOL_SOCKET, SO_PEERCRED, &credentials,
                                 &credentialsLength) == 0 &&
                      credentials.uid == allowedUid;
#elif defined(__APPLE__)
        uid_t peerUid = 0;
        gid_t peerGid = 0;
        peerAllowed = getpeereid(client, &peerUid, &peerGid) == 0 && peerUid == allowedUid;
#else
        (void)allowedUid;
        peerAllowed = true;
#endif

        if (!peerAllowed)
        {
            std::fprintf(stderr, "rejected client uid\n");
            close(client);
            close(server);
            unlink(endpoint.c_str());
            return 1;
        }

        handle_connection(client);
        close_all_devices();

        close(client);
        close(server);
        unlink(endpoint.c_str());
        return 0;
    }
#endif
} // namespace

int main(int argc, char **argv)
{
    std::string endpoint;

#if defined(_WIN32)
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--endpoint" && i + 1 < argc)
            endpoint = argv[++i];
    }

    if (endpoint.empty())
    {
        std::fprintf(stderr, "usage: %s --endpoint <pipe-name>\n", argv[0]);
        return 2;
    }

    return run_server(endpoint);
#else
    uid_t allowedUid = getuid();

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--endpoint" && i + 1 < argc)
            endpoint = argv[++i];
        else if (arg == "--uid" && i + 1 < argc)
            allowedUid = static_cast<uid_t>(std::strtoul(argv[++i], nullptr, 10));
    }

    if (endpoint.empty())
    {
        std::fprintf(stderr, "usage: %s --endpoint <socket> [--uid <uid>]\n", argv[0]);
        return 2;
    }

    return run_server(endpoint, allowedUid);
#endif
}
