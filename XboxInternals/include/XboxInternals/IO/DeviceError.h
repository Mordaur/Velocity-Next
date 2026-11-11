#ifndef XBOXINTERNALS_DEVICEERROR_H
#define XBOXINTERNALS_DEVICEERROR_H

#include <string>

enum class DeviceErrorCode
{
    None = 0,
    PermissionDenied,
    NotFound,
    Busy,
    IoError,
    InvalidDevice
};

struct DeviceDiagnostic
{
    std::wstring path;
    DeviceErrorCode code = DeviceErrorCode::None;
    std::string message;
};

#endif // XBOXINTERNALS_DEVICEERROR_H
