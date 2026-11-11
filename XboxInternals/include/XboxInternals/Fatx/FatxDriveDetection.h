#ifndef FATXDRIVEDETECTION_H
#define FATXDRIVEDETECTION_H

#include <iostream>
#include <vector>
#include <memory>
#include <sstream>
#include <XboxInternals/Fatx/FatxDrive.h>
#include <XboxInternals/IO/DeviceError.h>
#include <XboxInternals/Export.h>

class XBOXINTERNALSSHARED_EXPORT FatxDriveDetection
{
public:
    static std::vector<std::unique_ptr<FatxDrive>> GetAllFatxDrives(
            std::vector<DeviceDiagnostic> *diagnostics = nullptr);

private:
    static std::vector<std::wstring> getPhysicalDiskPaths();
    static std::vector<std::wstring> getLogicalDrives();
};

#endif // FATXDRIVEDETECTION_H
