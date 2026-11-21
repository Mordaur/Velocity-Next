#ifndef UTILS_H
#define UTILS_H

#include <XboxInternals/TypeDefinitions.h>
#include <XboxInternals/Export.h>
#include <XboxInternals/Stfs/StfsConstants.h>

#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <vector>

namespace Utils
{
    std::string XBOXINTERNALS_EXPORT NormalizeFilePath(std::string path, char cur_separater='/', char replacement_separator='\\');

    std::string XBOXINTERNALS_EXPORT ConvertToHexString(BYTE *buffer, DWORD len, std::ios_base&(letterCase)(std::ios_base&) = std::uppercase);
    std::string XBOXINTERNALS_EXPORT ConvertToHexString(UINT64 value, std::ios_base&(letterCase)(std::ios_base&) = std::uppercase);

    std::string XBOXINTERNALS_EXPORT VersionToString(const Version& ver);

    std::vector<std::string> FilesInDirectory(std::string directoryPath);

    UINT64 RoundToNearestHex1000(UINT64 num);

    bool CreateLocalDirectory(std::string filePath);

    std::string GetTemporaryFileName();

    bool DeleteLocalFile(std::string path);

    // True when `relative` is a non-empty relative path with no "."/".."
    // component, drive/ADS separator or NUL. Backslashes are separators.
    bool XBOXINTERNALS_EXPORT IsSafeRelativePath(const std::string &relative);

    // Join `root` with a validated relative path. False when the path is unsafe.
    bool XBOXINTERNALS_EXPORT SafeJoin(const std::string &root, const std::string &relative,
            std::string &out);
}

#endif // UTILS_H
