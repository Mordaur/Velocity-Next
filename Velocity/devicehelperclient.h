#ifndef DEVICEHELPERCLIENT_H
#define DEVICEHELPERCLIENT_H

#include <QByteArray>
#include <QProcess>
#include <QString>

#include <cstdint>
#include <memory>
#include <mutex>

#include <XboxInternals/IO/BaseIO.h>

struct HelperDeviceHandle
{
    uint32_t handle = 0;
    uint64_t size = 0;
    bool writable = false;
};

class HelperClient
{
public:
    HelperClient();
    ~HelperClient();

    bool start(QString *error);
    bool isRunning() const;
    void stop();

    HelperDeviceHandle openDevice(const std::wstring &path, bool writable);
    QByteArray readAt(uint32_t handle, uint64_t offset, uint32_t length);
    uint32_t writeAt(uint32_t handle, uint64_t offset, const QByteArray &data);
    uint64_t length(uint32_t handle);
    void close(uint32_t handle);

private:
#if defined(_WIN32)
    using NativeStream = void *;
    static constexpr NativeStream kInvalidStream = nullptr;
#else
    using NativeStream = int;
    static constexpr NativeStream kInvalidStream = -1;
#endif

    // The device notifier and the background content-load worker share one
    // client. A request/response pair must be atomic, otherwise frames can
    // interleave and a reply can be matched to the wrong request.
    std::mutex requestMutex;

    QProcess process;
    QString endpoint;
    NativeStream stream = kInvalidStream;
#if defined(_WIN32)
    void *helperProcess = nullptr;
#endif
    uint32_t nextRequestId = 1;

    void sendFrame(uint16_t type, uint32_t requestId, const QByteArray &payload);
    QByteArray receiveFrame(uint16_t expectedType, uint32_t requestId);
    bool connectToEndpoint(QString *error);
    bool isElevationPending() const;
    void closeSocket();
};

class HelperDeviceIO : public BaseIO
{
public:
    HelperDeviceIO(std::shared_ptr<HelperClient> client, HelperDeviceHandle handle);
    ~HelperDeviceIO() override;

    void SetPosition(UINT64 position, std::ios_base::seekdir dir = std::ios_base::beg) override;
    UINT64 GetPosition() override;
    UINT64 Length() override;

    void ReadBytes(BYTE *outBuffer, DWORD len) override;
    void WriteBytes(BYTE *buffer, DWORD len) override;

    void Close() noexcept override;
    void Flush() override;

private:
    // FATX parsing issues a lot of tiny reads (dirents, FAT entries, STFS
    // headers); each one would otherwise be a separate pipe round trip. A read
    // that fits inside one aligned window is served from this cache instead.
    static constexpr UINT64 kCacheWindow = 0x10000;
    QByteArray cache;
    UINT64 cacheStart = 0;
    size_t cacheLength = 0;

    std::shared_ptr<HelperClient> client;
    HelperDeviceHandle device;
    UINT64 position;
    bool closed;
};

#endif // DEVICEHELPERCLIENT_H
