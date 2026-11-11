#include "devicehelperclient.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "protocol.h"

#ifndef _WIN32
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace vh = velocity::helper;

namespace
{
#ifndef _WIN32
    bool writeAll(int fd, const void *data, size_t length)
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

    bool readAll(int fd, void *data, size_t length)
    {
        auto *bytes = static_cast<uint8_t *>(data);
        size_t offset = 0;
        while (offset < length)
        {
            const ssize_t count = ::read(fd, bytes + offset, length - offset);
            if (count <= 0)
            {
                if (count < 0 && errno == EINTR)
                    continue;
                return false;
            }
            offset += static_cast<size_t>(count);
        }
        return true;
    }
#endif
}

HelperClient::HelperClient() = default;

HelperClient::~HelperClient()
{
    stop();
}

bool HelperClient::isRunning() const
{
    return socketFd >= 0;
}

bool HelperClient::start(QString *error)
{
#ifdef _WIN32
    if (error)
        *error = "The Windows device helper transport is not implemented yet.";
    return false;
#else
    const QString helperPath =
            QCoreApplication::applicationDirPath() + "/velocity-device-helper";
    if (!QFileInfo::exists(helperPath))
    {
        if (error)
            *error = "velocity-device-helper was not found next to the executable.";
        return false;
    }

    const QString pkexec = QStandardPaths::findExecutable("pkexec");
    if (pkexec.isEmpty())
    {
        if (error)
            *error = "pkexec was not found; install polkit or run the app with sudo.";
        return false;
    }

    endpoint = QString("/tmp/velocity-device-helper-%1-%2.sock")
            .arg(static_cast<qulonglong>(getuid()))
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    process.setProgram(pkexec);
    process.setArguments({helperPath, "--endpoint", endpoint,
                          "--uid", QString::number(getuid())});
    process.start();

    if (!process.waitForStarted(5000))
    {
        if (error)
            *error = "Could not start pkexec: " + process.errorString();
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 30000)
    {
        if (connectToEndpoint(error))
            return true;

        if (process.state() == QProcess::NotRunning)
            break;
        QThread::msleep(50);
    }

    if (error)
    {
        const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        *error = stderrText.isEmpty() ? "The privileged helper did not start." : stderrText;
    }
    stop();
    return false;
#endif
}

void HelperClient::stop()
{
    closeSocket();

#ifdef _WIN32
    return;
#else
    if (process.state() != QProcess::NotRunning)
    {
        process.terminate();
        if (!process.waitForFinished(2000))
            process.kill();
    }
    if (!endpoint.isEmpty())
        QFile::remove(endpoint);
#endif
}

bool HelperClient::connectToEndpoint(QString *error)
{
#ifdef _WIN32
    (void)error;
    return false;
#else
    closeSocket();

    socketFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socketFd < 0)
        return false;

    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    const QByteArray endpointBytes = endpoint.toLocal8Bit();
    if (endpointBytes.size() >= static_cast<int>(sizeof(address.sun_path)))
    {
        closeSocket();
        return false;
    }
    std::strncpy(address.sun_path, endpointBytes.constData(), sizeof(address.sun_path) - 1);

    if (connect(socketFd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
    {
        closeSocket();
        return false;
    }

    try
    {
        const uint32_t requestId = nextRequestId++;
        sendFrame(static_cast<uint16_t>(vh::MessageType::Hello), requestId, {});
        receiveFrame(static_cast<uint16_t>(vh::MessageType::Hello), requestId);
        return true;
    }
    catch (const std::string &exception)
    {
        if (error)
            *error = QString::fromStdString(exception);
        closeSocket();
        return false;
    }
#endif
}

void HelperClient::closeSocket()
{
#ifndef _WIN32
    if (socketFd >= 0)
    {
        close(socketFd);
        socketFd = -1;
    }
#endif
}

void HelperClient::sendFrame(uint16_t type, uint32_t requestId, const QByteArray &payload)
{
#ifndef _WIN32
    if (socketFd < 0)
        throw std::string("helper: not connected");

    vh::Header header;
    header.magic = vh::kMagic;
    header.version = vh::kVersion;
    header.type = static_cast<vh::MessageType>(type);
    header.requestId = requestId;
    header.payloadLength = static_cast<uint32_t>(payload.size());

    const auto encodedHeader = vh::encode_header(header);
    if (!writeAll(socketFd, encodedHeader.data(), encodedHeader.size()) ||
        (!payload.isEmpty() && !writeAll(socketFd, payload.constData(), payload.size())))
        throw std::string("helper: write failed");
#else
    (void)type;
    (void)requestId;
    (void)payload;
#endif
}

QByteArray HelperClient::receiveFrame(uint16_t expectedType, uint32_t requestId)
{
#ifndef _WIN32
    uint8_t headerBytes[vh::kHeaderSize];
    if (!readAll(socketFd, headerBytes, sizeof(headerBytes)))
        throw std::string("helper: connection closed");

    vh::Header header;
    if (!vh::decode_header(headerBytes, sizeof(headerBytes), header))
        throw std::string("helper: invalid frame");

    std::vector<uint8_t> payload(header.payloadLength);
    if (!payload.empty() && !readAll(socketFd, payload.data(), payload.size()))
        throw std::string("helper: truncated frame");

    if (header.requestId != requestId)
        throw std::string("helper: response request id mismatch");

    if (header.type == vh::MessageType::Error)
    {
        size_t offset = 0;
        uint32_t code = 0;
        uint32_t messageLength = 0;
        if (!vh::read_u32(payload.data(), payload.size(), offset, code) ||
            !vh::read_u32(payload.data(), payload.size(), offset, messageLength) ||
            offset + messageLength > payload.size())
            throw std::string("helper: malformed error response");

        const std::string message(reinterpret_cast<const char *>(payload.data() + offset),
                                  messageLength);
        throw std::string("helper: " + message + " (code " + std::to_string(code) + ")");
    }

    if (static_cast<uint16_t>(header.type) != expectedType)
        throw std::string("helper: unexpected response type");

    return QByteArray(reinterpret_cast<const char *>(payload.data()),
                      static_cast<int>(payload.size()));
#else
    (void)expectedType;
    (void)requestId;
    return {};
#endif
}

HelperDeviceHandle HelperClient::openDevice(const std::wstring &path, bool writable)
{
    const QByteArray pathBytes = QString::fromStdWString(path).toUtf8();
    QByteArray payload;
    payload.append(static_cast<char>(writable ? vh::OpenMode::ReadWrite : vh::OpenMode::ReadOnly));

    std::vector<uint8_t> lengthBytes;
    vh::append_u32(lengthBytes, static_cast<uint32_t>(pathBytes.size()));
    payload.append(reinterpret_cast<const char *>(lengthBytes.data()),
                   static_cast<int>(lengthBytes.size()));
    payload.append(pathBytes);

    const uint32_t requestId = nextRequestId++;
    sendFrame(static_cast<uint16_t>(vh::MessageType::Open), requestId, payload);
    const QByteArray response = receiveFrame(static_cast<uint16_t>(vh::MessageType::Open), requestId);

    if (response.size() < 13)
        throw std::string("helper: malformed open response");

    const auto *data = reinterpret_cast<const uint8_t *>(response.constData());
    size_t offset = 0;
    uint32_t handle = 0;
    uint64_t size = 0;
    if (!vh::read_u32(data, response.size(), offset, handle) ||
        !vh::read_u64(data, response.size(), offset, size))
        throw std::string("helper: malformed open response");

    return HelperDeviceHandle{handle, size, data[offset] != 0};
}

QByteArray HelperClient::readAt(uint32_t handle, uint64_t offset, uint32_t length)
{
    std::vector<uint8_t> payload;
    vh::append_u32(payload, handle);
    vh::append_u64(payload, offset);
    vh::append_u32(payload, length);

    const uint32_t requestId = nextRequestId++;
    sendFrame(static_cast<uint16_t>(vh::MessageType::Read), requestId,
              QByteArray(reinterpret_cast<const char *>(payload.data()),
                         static_cast<int>(payload.size())));
    return receiveFrame(static_cast<uint16_t>(vh::MessageType::Read), requestId);
}

uint32_t HelperClient::writeAt(uint32_t handle, uint64_t offset, const QByteArray &data)
{
    std::vector<uint8_t> payload;
    vh::append_u32(payload, handle);
    vh::append_u64(payload, offset);
    vh::append_u32(payload, static_cast<uint32_t>(data.size()));
    payload.insert(payload.end(), data.constData(), data.constData() + data.size());

    const uint32_t requestId = nextRequestId++;
    sendFrame(static_cast<uint16_t>(vh::MessageType::Write), requestId,
              QByteArray(reinterpret_cast<const char *>(payload.data()),
                         static_cast<int>(payload.size())));
    const QByteArray response = receiveFrame(static_cast<uint16_t>(vh::MessageType::Write), requestId);
    if (response.size() < 4)
        throw std::string("helper: malformed write response");

    const auto *bytes = reinterpret_cast<const uint8_t *>(response.constData());
    size_t responseOffset = 0;
    uint32_t written = 0;
    vh::read_u32(bytes, response.size(), responseOffset, written);
    return written;
}

uint64_t HelperClient::length(uint32_t handle)
{
    std::vector<uint8_t> payload;
    vh::append_u32(payload, handle);

    const uint32_t requestId = nextRequestId++;
    sendFrame(static_cast<uint16_t>(vh::MessageType::Length), requestId,
              QByteArray(reinterpret_cast<const char *>(payload.data()),
                         static_cast<int>(payload.size())));
    const QByteArray response = receiveFrame(static_cast<uint16_t>(vh::MessageType::Length), requestId);
    if (response.size() < 8)
        throw std::string("helper: malformed length response");

    const auto *bytes = reinterpret_cast<const uint8_t *>(response.constData());
    size_t offset = 0;
    uint64_t value = 0;
    vh::read_u64(bytes, response.size(), offset, value);
    return value;
}

void HelperClient::close(uint32_t handle)
{
    std::vector<uint8_t> payload;
    vh::append_u32(payload, handle);

    const uint32_t requestId = nextRequestId++;
    sendFrame(static_cast<uint16_t>(vh::MessageType::Close), requestId,
              QByteArray(reinterpret_cast<const char *>(payload.data()),
                         static_cast<int>(payload.size())));
    receiveFrame(static_cast<uint16_t>(vh::MessageType::Close), requestId);
}

HelperDeviceIO::HelperDeviceIO(std::shared_ptr<HelperClient> client, HelperDeviceHandle handle) :
    client(std::move(client)), device(handle), position(0), closed(false)
{
}

HelperDeviceIO::~HelperDeviceIO()
{
    try
    {
        Close();
    }
    catch (...)
    {
    }
}

void HelperDeviceIO::SetPosition(UINT64 newPosition, std::ios_base::seekdir dir)
{
    if (dir == std::ios_base::cur)
        position += newPosition;
    else if (dir == std::ios_base::end)
        position = device.size + newPosition;
    else
        position = newPosition;

    if (position > device.size)
        throw std::string("HelperDeviceIO: seek beyond device");
}

UINT64 HelperDeviceIO::GetPosition()
{
    return position;
}

UINT64 HelperDeviceIO::Length()
{
    return device.size;
}

void HelperDeviceIO::ReadBytes(BYTE *outBuffer, DWORD len)
{
    UINT64 remaining = len;
    UINT64 offset = position;
    while (remaining > 0)
    {
        const uint32_t chunk = static_cast<uint32_t>(
                std::min<UINT64>(remaining, static_cast<UINT64>(vh::kMaxPayload)));
        const QByteArray data = client->readAt(device.handle, offset, chunk);
        if (static_cast<uint32_t>(data.size()) != chunk)
            throw std::string("HelperDeviceIO: short read");

        std::memcpy(outBuffer, data.constData(), chunk);
        outBuffer += chunk;
        offset += chunk;
        remaining -= chunk;
    }
    position = offset;
}

void HelperDeviceIO::WriteBytes(BYTE *buffer, DWORD len)
{
    if (!device.writable)
        throw std::string("HelperDeviceIO: device handle is read-only");

    UINT64 remaining = len;
    UINT64 offset = position;
    while (remaining > 0)
    {
        const uint32_t chunk = static_cast<uint32_t>(
                std::min<UINT64>(remaining, static_cast<UINT64>(vh::kMaxPayload)));
        const QByteArray data(reinterpret_cast<const char *>(buffer), static_cast<int>(chunk));
        const uint32_t written = client->writeAt(device.handle, offset, data);
        if (written != chunk)
            throw std::string("HelperDeviceIO: short write");

        buffer += chunk;
        offset += chunk;
        remaining -= chunk;
    }
    position = offset;
}

void HelperDeviceIO::Close()
{
    if (closed)
        return;
    closed = true;
    client->close(device.handle);
}

void HelperDeviceIO::Flush()
{
}
