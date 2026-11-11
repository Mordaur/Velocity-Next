#include "protocol.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#ifdef __linux__
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
#ifndef _WIN32
    struct Device
    {
        int fd = -1;
        uint64_t size = 0;
        bool writable = false;
    };

    std::map<uint32_t, Device> g_devices;
    uint32_t g_nextHandle = 1;

    void send_frame(int fd, vh::MessageType type, uint32_t requestId,
                    const std::vector<uint8_t> &payload)
    {
        vh::Header header;
        header.magic = vh::kMagic;
        header.version = vh::kVersion;
        header.type = type;
        header.requestId = requestId;
        header.payloadLength = static_cast<uint32_t>(payload.size());

        const auto encoded = vh::encode_header(header);
        vh::write_full(fd, encoded.data(), encoded.size());
        if (!payload.empty())
            vh::write_full(fd, payload.data(), payload.size());
    }

    void send_error(int fd, uint32_t requestId, vh::ErrorCode code, const std::string &message)
    {
        send_frame(fd, vh::MessageType::Error, requestId, vh::encode_error(code, message));
    }

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

    void handle_open(int client, uint32_t requestId, const std::vector<uint8_t> &payload)
    {
        if (payload.size() < 5)
        {
            send_error(client, requestId, vh::ErrorCode::InvalidRequest, "invalid open request");
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
            send_error(client, requestId, vh::ErrorCode::InvalidRequest, "invalid device path");
            return;
        }

        const std::string path(reinterpret_cast<const char *>(payload.data() + offset), pathLength);
        if (!validate_device_path(path))
        {
            send_error(client, requestId, vh::ErrorCode::InvalidRequest, "path is not a device node");
            return;
        }

        if (g_devices.size() >= vh::kMaxHandles)
        {
            send_error(client, requestId, vh::ErrorCode::OutOfHandles, "too many open handles");
            return;
        }

        bool writable = mode == static_cast<uint8_t>(vh::OpenMode::ReadWrite);
        int fd = open(path.c_str(), (writable ? O_RDWR : O_RDONLY) | O_CLOEXEC);
        if (fd < 0 && writable && (errno == EACCES || errno == EROFS))
        {
            fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
            writable = false;
        }
        if (fd < 0)
        {
            send_error(client, requestId,
                       errno == EACCES ? vh::ErrorCode::AccessDenied : vh::ErrorCode::IoError,
                       std::string("open failed: ") + std::strerror(errno));
            return;
        }

        struct stat st {};
        if (fstat(fd, &st) != 0 || !S_ISBLK(st.st_mode))
        {
            close(fd);
            send_error(client, requestId, vh::ErrorCode::InvalidRequest, "not a block device");
            return;
        }

        uint64_t size = 0;
        if (!read_device_length(fd, size))
        {
            close(fd);
            send_error(client, requestId, vh::ErrorCode::IoError, "could not determine device length");
            return;
        }

        const uint32_t handle = g_nextHandle++;
        g_devices[handle] = Device{fd, size, writable};

        std::vector<uint8_t> response;
        vh::append_u32(response, handle);
        vh::append_u64(response, size);
        response.push_back(writable ? 1 : 0);
        send_frame(client, vh::MessageType::Open, requestId, response);
    }

    void handle_read(int client, uint32_t requestId, const std::vector<uint8_t> &payload)
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
            send_error(client, requestId, vh::ErrorCode::InvalidRequest, "invalid read request");
            return;
        }

        auto it = g_devices.find(handle);
        if (it == g_devices.end())
        {
            send_error(client, requestId, vh::ErrorCode::NotFound, "unknown handle");
            return;
        }

        const Device &device = it->second;
        if (position > device.size || length > device.size - position)
        {
            send_error(client, requestId, vh::ErrorCode::RangeError, "read outside device");
            return;
        }

        std::vector<uint8_t> data(length);
        size_t done = 0;
        while (done < length)
        {
            const ssize_t count = pread(device.fd, data.data() + done, length - done,
                                        static_cast<off_t>(position + done));
            if (count < 0)
            {
                if (errno == EINTR)
                    continue;
                send_error(client, requestId, vh::ErrorCode::IoError,
                           std::string("read failed: ") + std::strerror(errno));
                return;
            }
            if (count == 0)
                break;
            done += static_cast<size_t>(count);
        }

        data.resize(done);
        send_frame(client, vh::MessageType::Read, requestId, data);
    }

    void handle_write(int client, uint32_t requestId, const std::vector<uint8_t> &payload)
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
            send_error(client, requestId, vh::ErrorCode::InvalidRequest, "invalid write request");
            return;
        }

        auto it = g_devices.find(handle);
        if (it == g_devices.end())
        {
            send_error(client, requestId, vh::ErrorCode::NotFound, "unknown handle");
            return;
        }

        Device &device = it->second;
        if (!device.writable)
        {
            send_error(client, requestId, vh::ErrorCode::AccessDenied, "handle is read-only");
            return;
        }
        if (position > device.size || length > device.size - position)
        {
            send_error(client, requestId, vh::ErrorCode::RangeError, "write outside device");
            return;
        }

        size_t done = 0;
        while (done < length)
        {
            const ssize_t count = pwrite(device.fd, payload.data() + offset + done,
                                         length - done,
                                         static_cast<off_t>(position + done));
            if (count < 0)
            {
                if (errno == EINTR)
                    continue;
                send_error(client, requestId, vh::ErrorCode::IoError,
                           std::string("write failed: ") + std::strerror(errno));
                return;
            }
            done += static_cast<size_t>(count);
        }

        std::vector<uint8_t> response;
        vh::append_u32(response, static_cast<uint32_t>(done));
        send_frame(client, vh::MessageType::Write, requestId, response);
    }

    void handle_connection(int client)
    {
        while (true)
        {
            uint8_t headerBytes[vh::kHeaderSize];
            if (!vh::read_full(client, headerBytes, sizeof(headerBytes)))
                return;

            vh::Header header;
            if (!vh::decode_header(headerBytes, sizeof(headerBytes), header))
                return;

            std::vector<uint8_t> payload(header.payloadLength);
            if (!payload.empty() && !vh::read_full(client, payload.data(), payload.size()))
                return;

            switch (header.type)
            {
            case vh::MessageType::Hello:
            {
                std::vector<uint8_t> response;
                vh::append_u32(response, vh::kVersion);
                vh::append_u32(response, 3); // read, write, range validation
                send_frame(client, vh::MessageType::Hello, header.requestId, response);
                break;
            }
            case vh::MessageType::Open:
                handle_open(client, header.requestId, payload);
                break;
            case vh::MessageType::Read:
                handle_read(client, header.requestId, payload);
                break;
            case vh::MessageType::Write:
                handle_write(client, header.requestId, payload);
                break;
            case vh::MessageType::Length:
            {
                size_t offset = 0;
                uint32_t handle = 0;
                if (!vh::read_u32(payload.data(), payload.size(), offset, handle))
                {
                    send_error(client, header.requestId, vh::ErrorCode::InvalidRequest, "invalid length request");
                    break;
                }
                auto it = g_devices.find(handle);
                if (it == g_devices.end())
                {
                    send_error(client, header.requestId, vh::ErrorCode::NotFound, "unknown handle");
                    break;
                }
                std::vector<uint8_t> response;
                vh::append_u64(response, it->second.size);
                send_frame(client, vh::MessageType::Length, header.requestId, response);
                break;
            }
            case vh::MessageType::Close:
            {
                size_t offset = 0;
                uint32_t handle = 0;
                if (!vh::read_u32(payload.data(), payload.size(), offset, handle))
                {
                    send_error(client, header.requestId, vh::ErrorCode::InvalidRequest, "invalid close request");
                    break;
                }
                auto it = g_devices.find(handle);
                if (it == g_devices.end())
                {
                    send_error(client, header.requestId, vh::ErrorCode::NotFound, "unknown handle");
                    break;
                }
                close(it->second.fd);
                g_devices.erase(it);
                send_frame(client, vh::MessageType::Close, header.requestId, {});
                break;
            }
            case vh::MessageType::Ping:
                send_frame(client, vh::MessageType::Ping, header.requestId, {});
                break;
            default:
                send_error(client, header.requestId, vh::ErrorCode::Unsupported, "unsupported message");
                break;
            }
        }
    }

    int runPosixServer(const std::string &endpoint, uid_t allowedUid)
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

        const int client = accept(server, nullptr, nullptr);
        if (client < 0)
        {
            std::fprintf(stderr, "accept: %s\n", std::strerror(errno));
            close(server);
            unlink(endpoint.c_str());
            return 1;
        }

        ucred credentials {};
        socklen_t credentialsLength = sizeof(credentials);
        if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &credentials, &credentialsLength) != 0 ||
            credentials.uid != allowedUid)
        {
            std::fprintf(stderr, "rejected client uid\n");
            close(client);
            close(server);
            unlink(endpoint.c_str());
            return 1;
        }

        handle_connection(client);

        for (auto &entry : g_devices)
            close(entry.second.fd);
        g_devices.clear();

        close(client);
        close(server);
        unlink(endpoint.c_str());
        return 0;
    }
#endif // !_WIN32
} // namespace

int main(int argc, char **argv)
{
#ifdef _WIN32
    (void)argc;
    (void)argv;
    std::fprintf(stderr, "velocity-device-helper: Windows transport not implemented yet\n");
    return 1;
#else
    std::string endpoint;
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

    return runPosixServer(endpoint, allowedUid);
#endif
}
