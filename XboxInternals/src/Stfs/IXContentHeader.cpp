#include <XboxInternals/Stfs/IXContentHeader.h>

IXContentHeader::IXContentHeader()
    : metaData(nullptr), metadata(metaData)
{
}

IXContentHeader::~IXContentHeader() = default;

#include <XboxInternals/Stfs/IXContentHeader.h>

#include <XboxInternals/IO/FileIO.h>
#include <XboxInternals/Utils.h>

FileSystem IXContentHeader::GetFileSystem(const std::string &filePath)
{
    FileIO fileIO(filePath);
    fileIO.SetPosition(0x3AC);
    FileSystem fileSystem = static_cast<FileSystem>(fileIO.ReadByte());
    fileIO.Close();

    return fileSystem;
}

std::string IXContentHeader::GetFatxFilePath()
{
    if (metaData == nullptr)
        return {};

    std::string profileID = Utils::ConvertToHexString(metaData->profileID, 8);
    std::string titleID = Utils::ConvertToHexString(static_cast<u64>(metaData->titleID));
    std::string contentType = Utils::ConvertToHexString(static_cast<u64>(metaData->contentType));

    while (contentType.size() < 8)
        contentType = "0" + contentType;

    return "Drive:\\Content\\Content\\" + profileID + "\\" + titleID + "\\" + contentType + "\\";
}
