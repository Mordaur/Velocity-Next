/* Most parts of this class were originally developed by Lander Griffith (https://github.com/landr0id/).
   Much of his code is used throughout this class or very slightly modified */

#ifndef DEVICEIO_H
#define DEVICEIO_H

#define FAT_SECTOR_SIZE 0x200

#include <memory>

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

    void ReadBytes(BYTE *outBuffer, DWORD len);

    void WriteBytes(BYTE *buffer, DWORD len);

    void SetPosition(UINT64 address, std::ios_base::seekdir dir = std::ios_base::beg);

    UINT64 GetPosition();

    UINT64 Length();

    void Close();

    void Flush();

    bool IsReadOnly() const { return readOnly; }

private:
    void loadDevice(std::wstring devicePath, DeviceErrorCode *error);

    UINT64 realPosition();

    class Impl;
    std::unique_ptr<Impl> impl;

    std::string yolo;

    UINT64 pos;
    BYTE lastReadData[FAT_SECTOR_SIZE];
    UINT64 lastReadOffset;
    bool readOnly;
};

#endif // DEVICEIO_H


