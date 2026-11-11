#ifndef DEVICEHELPERCLIENT_H
#define DEVICEHELPERCLIENT_H

#include <QByteArray>
#include <QProcess>
#include <QString>

#include <cstdint>
#include <memory>

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
    QProcess process;
    QString endpoint;
    int socketFd = -1;
    uint32_t nextRequestId = 1;

    void sendFrame(uint16_t type, uint32_t requestId, const QByteArray &payload);
    QByteArray receiveFrame(uint16_t expectedType, uint32_t requestId);
    bool connectToEndpoint(QString *error);
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

    void Close() override;
    void Flush() override;

private:
    std::shared_ptr<HelperClient> client;
    HelperDeviceHandle device;
    UINT64 position;
    bool closed;
};

#endif // DEVICEHELPERCLIENT_H
