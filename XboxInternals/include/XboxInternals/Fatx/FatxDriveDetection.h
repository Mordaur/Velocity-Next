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

    // Candidate whole-disk paths, without opening them.
    static std::vector<std::wstring> GetPhysicalDiskPaths();

private:
#ifdef _WIN32
    static std::vector<std::wstring> getLogicalDrives();
#else
    // POSIX mount paths are native bytes; converting them through a wchar_t
    // intermediate breaks non-ASCII labels.
    static std::vector<std::string> getLogicalDrives();
#endif
};

#endif // FATXDRIVEDETECTION_H
