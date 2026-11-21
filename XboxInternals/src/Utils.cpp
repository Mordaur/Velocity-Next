#include <XboxInternals/Utils.h>

#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;


std::string Utils::NormalizeFilePath(std::string path, char cur_separater, char replacement_separator)
{
    for (DWORD i = 0; i < path.length(); i++)
        if (path.at(i) == cur_separater)
            path.at(i) = replacement_separator;
    return path;
}


std::string Utils::ConvertToHexString(BYTE *buffer, DWORD len, std::ios_base&(letterCase)(std::ios_base&))
{
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (DWORD i = 0; i < len; i++)
        ss << letterCase << std::hex << std::setw(2) << (int)buffer[i];
    return ss.str();
}


std::string Utils::ConvertToHexString(UINT64 value, std::ios_base&(letterCase)(std::ios_base&))
{
    std::ostringstream ss;
    ss << letterCase << std::hex << value;
    return ss.str();
}


std::string Utils::VersionToString(const Version& ver)
{
    std::ostringstream ss;
    ss << ver.major << "." << ver.minor << "." << ver.build << "." << ver.revision;
    return ss.str();
}


std::vector<std::string> Utils::FilesInDirectory(std::string directoryPath)
{
    std::vector<std::string> toReturn;

    try {
        if (fs::exists(directoryPath) && fs::is_directory(directoryPath)) {
            for (const auto& entry : fs::directory_iterator(directoryPath)) {
                if (entry.is_regular_file()) {
                    toReturn.push_back(entry.path().string());
                }
            }
        } else {
            throw std::string("Utils: Error opening directory\n");
        }
    } catch (const fs::filesystem_error&) {
        throw std::string("Utils: Error opening directory\n");
    }

    return toReturn;
}


UINT64 Utils::RoundToNearestHex1000(UINT64 num)
{
    return (num + 0xFFF) & ~0xFFF;
}


bool Utils::CreateLocalDirectory(std::string filePath)
{
    try {
        return fs::create_directories(filePath);
    } catch (const fs::filesystem_error&) {
        return false;
    }
}


std::string Utils::GetTemporaryFileName()
{
    auto tempPath = fs::temp_directory_path() / ("velocity_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".tmp");
    return tempPath.string();
}


bool Utils::DeleteLocalFile(std::string path)
{
    try {
        return fs::remove(path);
    } catch (const fs::filesystem_error&) {
        return false;
    }
}
