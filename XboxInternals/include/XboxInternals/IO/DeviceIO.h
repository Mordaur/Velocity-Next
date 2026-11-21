/* Most parts of this class were originally developed by Lander Griffith (https://github.com/landr0id/).
   Much of his code is used throughout this class or very slightly modified */

#ifndef DEVICEIO_H
#define DEVICEIO_H

#define FAT_SECTOR_SIZE 0x200

#include <memory>
#include <vector>

#include <XboxInternals/IO/BaseIO.h>
#include <XboxInternals/IO/DeviceError.h>
#include "../Fatx/FatxHelpers.h"
#include <XboxInternals/Export.h>

class XBOXINTERNALS_EXPORT DeviceIO : public BaseIO
{
public:
    // Platform-agnostic constructors
    DeviceIO(std::string devicePath, DeviceErrorCode *error = nullptr);
    DeviceIO(std::wstring devicePath, DeviceErrorCode *error = nullptr);
    virtual ~DeviceIO();

    void ReadBytes(u8 *outBuffer, u32 len);

    void WriteBytes(u8 *buffer, u32 len);

    void SetPosition(u64 address, std::ios_base::seekdir dir = std::ios_base::beg);

    u64 GetPosition();

    u64 Length();

    void Close();

    void Flush();

    bool IsReadOnly() const { return readOnly; }

private:
    void loadDevice(std::wstring devicePath, DeviceErrorCode *error);

    u64 realPosition();

    // The device's logical sector size, which is not always 512 (4Kn media).
    // Raw block I/O and FILE_FLAG_NO_BUFFERING require aligned transfers, so
    // reads and writes are bounced through a sector-aligned buffer.
    u64 DownToSector(u64 address) const;
    size_t RoundUpToSector(size_t length) const;
    void ReadRaw(u8 *buffer, u32 length);
    void WriteRaw(const u8 *buffer, u32 length);

    class Impl;
    std::unique_ptr<Impl> impl;

    std::string yolo;

    u64 pos;
    bool readOnly;
};

#endif // DEVICEIO_H


