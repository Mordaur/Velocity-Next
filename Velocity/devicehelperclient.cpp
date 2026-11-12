#include "devicehelperclient.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "protocol.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace vh = velocity::helper;

namespace
{
    QString helperBinaryName()
    {
#if defined(_WIN32)
        return QStringLiteral("velocity-device-helper.exe");
#else
        return QStringLiteral("velocity-device-helper");
#endif
    }

#if defined(__APPLE__)
    // Wrap a value for /bin/sh single-quoted quoting.
    QString shellQuote(const QString &value)
    {
        QString quoted = value;
        quoted.replace("'", "'\\''");
        return "'" + quoted + "'";
    }

    // Escape a string for embedding inside an AppleScript double-quoted literal.
    QString appleScriptString(const QString &value)
    {
        QString escaped = value;
        escaped.replace("\\", "\\\\");
        escaped.replace("\"", "\\\"");
        return escaped;
    }
#endif

#if defined(_WIN32)
    QString windowsErrorText(unsigned long error)
    {
        wchar_t *buffer = nullptr;
        const unsigned long length = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, error, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);

        QString text = length > 0 ? QString::fromWCharArray(buffer, static_cast<int>(length))
                                  : QString("Windows error %1").arg(error);
        if (buffer)
            LocalFree(buffer);

        return text.trimmed();
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
    return stream != kInvalidStream;
}

bool HelperClient::start(QString *error)
{
    const QString helperPath =
            QCoreApplication::applicationDirPath() + "/" + helperBinaryName();
    if (!QFileInfo::exists(helperPath))
    {
        if (error)
            *error = helperBinaryName() + " was not found next to the executable.";
        return false;
    }

#if defined(_WIN32)
    endpoint = QString("velocity-device-helper-%1-%2")
                       .arg(QCoreApplication::applicationPid())
                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-'));

    const std::wstring helperWide =
            QDir::toNativeSeparators(helperPath).toStdWString();
    std::wstring parametersWide = QString("--endpoint " + endpoint).toStdWString();

    SHELLEXECUTEINFOW info {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = helperWide.c_str();
    info.lpParameters = parametersWide.c_str();
    info.nShow = SW_HIDE;

    if (!ShellExecuteExW(&info) || info.hProcess == nullptr)
    {
        const unsigned long code = GetLastError();
        if (error)
        {
            *error = code == ERROR_CANCELLED
                    ? QStringLiteral("Administrator access was not granted.")
                    : QStringLiteral("Could not start the privileged helper: %1")
                              .arg(windowsErrorText(code));
        }
        return false;
    }

    helperProcess = info.hProcess;
#else
    endpoint = QString("/tmp/velocity-device-helper-%1-%2.sock")
            .arg(static_cast<qulonglong>(getuid()))
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    if (geteuid() == 0)
    {
        process.setProgram(helperPath);
        process.setArguments({"--endpoint", endpoint, "--uid", QString::number(getuid())});
    }
    else
    {
        constexpr bool kEnablePasswordlessSudo = true;

        const QString sudo = QStandardPaths::findExecutable("sudo");
        bool passwordlessSudo = false;

        if (kEnablePasswordlessSudo && !sudo.isEmpty())
        {
            QProcess probe;
            probe.start(sudo, {"-n", "true"});
            if (probe.waitForFinished(2000) &&
                probe.exitStatus() == QProcess::NormalExit &&
                probe.exitCode() == 0)
                passwordlessSudo = true;
        }

        if (passwordlessSudo)
        {
            process.setProgram(sudo);
            process.setArguments({"-n", helperPath, "--endpoint", endpoint,
                                  "--uid", QString::number(getuid())});
        }
#if defined(__APPLE__)
        else
        {
            // macOS has neither pkexec nor UAC. Ask the system to run the helper
            // with administrator privileges; the command is backgrounded because
            // the helper keeps running until the GUI disconnects.
            const QString osascript = QStandardPaths::findExecutable("osascript");
            if (osascript.isEmpty())
            {
                if (error)
                    *error = "osascript was not found; run Velocity with sudo.";
                return false;
            }

            const QString command = QString("%1 --endpoint %2 --uid %3")
                                            .arg(shellQuote(helperPath),
                                                 shellQuote(endpoint),
                                                 QString::number(getuid()));
            const QString script =
                    QString("do shell script \"%1 >/dev/null 2>&1 &\" "
                            "with administrator privileges")
                            .arg(appleScriptString(command));

            process.setProgram(osascript);
            process.setArguments({"-e", script});
        }
#else
        else
        {
            const QString pkexec = QStandardPaths::findExecutable("pkexec");
            if (pkexec.isEmpty())
            {
                if (error)
                    *error = "Neither passwordless sudo nor pkexec is available.";
                return false;
            }

            process.setProgram(pkexec);
            process.setArguments({helperPath, "--endpoint", endpoint,
                                  "--uid", QString::number(getuid())});
        }
#endif
    }
    process.start();

    if (!process.waitForStarted(5000))
    {
        if (error)
            *error = "Could not start the privileged helper: " + process.errorString();
        return false;
    }
#endif

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 120000)
    {
        if (connectToEndpoint(error))
            return true;

        if (!isElevationPending())
            break;

        // Keep the GUI painting while the OS elevation prompt is open.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    if (error)
    {
#if defined(_WIN32)
        *error = "The privileged helper did not accept a connection.";
#else
        const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();

        // pkexec returns 126 when the authentication dialog is dismissed and
        // 127 when authorization is not granted; both mean the user said no.
        const bool notGranted = process.exitStatus() == QProcess::NormalExit &&
                                (process.exitCode() == 126 || process.exitCode() == 127);

        if (notGranted || stderrText.contains("User canceled", Qt::CaseInsensitive) ||
            stderrText.contains("User cancelled", Qt::CaseInsensitive))
        {
            *error = "Administrator access was not granted.";
        }
        else
        {
            *error = stderrText.isEmpty() ? "The privileged helper did not start." : stderrText;
        }
#endif
    }
    stop();
    return false;
}

bool HelperClient::isElevationPending() const
{
#if defined(_WIN32)
    if (!helperProcess)
        return false;

    // Keep retrying only while the helper process is still alive; a helper that
    // exits without connecting must not stall the GUI for two minutes.
    return WaitForSingleObject(static_cast<HANDLE>(helperProcess), 0) == WAIT_TIMEOUT;
#else
    return process.state() != QProcess::NotRunning;
#endif
}

void HelperClient::stop()
{
    closeSocket();

#if defined(_WIN32)
    if (helperProcess)
    {
        // The helper normally exits as soon as the pipe closes; this only
        // reaps or kills a helper that never connected.
        TerminateProcess(static_cast<HANDLE>(helperProcess), 0);
        CloseHandle(static_cast<HANDLE>(helperProcess));
        helperProcess = nullptr;
    }
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
#if defined(_WIN32)
    closeSocket();

    const std::wstring pipePath = L"\\\\.\\pipe\\" + endpoint.toStdWString();
    HANDLE pipe = CreateFileW(pipePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);

    if (pipe == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() == ERROR_PIPE_BUSY)
            WaitNamedPipeW(pipePath.c_str(), 250);
        return false;
    }

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
    stream = pipe;
#else
    closeSocket();

    const int socketFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socketFd < 0)
        return false;

    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    const QByteArray endpointBytes = endpoint.toLocal8Bit();
    if (endpointBytes.size() >= static_cast<int>(sizeof(address.sun_path)))
    {
        ::close(socketFd);
        return false;
    }
    std::strncpy(address.sun_path, endpointBytes.constData(), sizeof(address.sun_path) - 1);

    if (connect(socketFd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
    {
        ::close(socketFd);
        return false;
    }

    stream = socketFd;
#endif

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
    catch (const std::exception &exception)
    {
        if (error)
            *error = QString::fromUtf8(exception.what());
        closeSocket();
        return false;
    }
}

void HelperClient::closeSocket()
{
#if defined(_WIN32)
    if (stream != kInvalidStream)
    {
        CloseHandle(static_cast<HANDLE>(stream));
        stream = kInvalidStream;
    }
#else
    if (stream != kInvalidStream)
    {
        ::close(stream);
        stream = kInvalidStream;
    }
#endif
}

void HelperClient::sendFrame(uint16_t type, uint32_t requestId, const QByteArray &payload)
{
    if (stream == kInvalidStream)
        throw std::string("helper: not connected");

    vh::Header header;
    header.magic = vh::kMagic;
    header.version = vh::kVersion;
    header.type = static_cast<vh::MessageType>(type);
    header.requestId = requestId;
    header.payloadLength = static_cast<uint32_t>(payload.size());

    const auto encodedHeader = vh::encode_header(header);
    if (!vh::write_full(stream, encodedHeader.data(), encodedHeader.size()) ||
        (!payload.isEmpty() && !vh::write_full(stream, payload.constData(), payload.size())))
        throw std::string("helper: write failed");
}

QByteArray HelperClient::receiveFrame(uint16_t expectedType, uint32_t requestId)
{
    uint8_t headerBytes[vh::kHeaderSize];
    if (!vh::read_full(stream, headerBytes, sizeof(headerBytes)))
        throw std::string("helper: connection closed");

    vh::Header header;
    if (!vh::decode_header(headerBytes, sizeof(headerBytes), header))
        throw std::string("helper: invalid frame");

    std::vector<uint8_t> payload(header.payloadLength);
    if (!payload.empty() && !vh::read_full(stream, payload.data(), payload.size()))
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

void HelperDeviceIO::Close() noexcept
{
    if (closed)
        return;
    closed = true;

    try
    {
        client->close(device.handle);
    }
    catch (...)
    {
        // Closing is best-effort; a dropped helper must not terminate the GUI.
    }
}

void HelperDeviceIO::Flush()
{
}
