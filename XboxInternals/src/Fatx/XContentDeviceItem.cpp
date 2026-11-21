#include <XboxInternals/Fatx/XContentDeviceItem.h>

XContentDeviceItem::XContentDeviceItem() :
    content()
{

}

XContentDeviceItem::XContentDeviceItem(FatxFileEntry *fileEntry, std::shared_ptr<IXContentHeader> content, std::vector<std::string> contentFilePaths) :
    content(std::move(content)), pathOnDevice(fileEntry->path + fileEntry->name), contentFilePaths(std::move(contentFilePaths)), rawName(fileEntry->name), fileSize(fileEntry->fileSize)
{

}

XContentDeviceItem::XContentDeviceItem(std::string pathOnDevice, std::string rawName, std::shared_ptr<IXContentHeader> content, UINT64 fileSize, std::vector<std::string> contentFilePaths) :
    content(std::move(content)), pathOnDevice(std::move(pathOnDevice)), contentFilePaths(std::move(contentFilePaths)), rawName(std::move(rawName)), fileSize(fileSize)
{

}

std::string XContentDeviceItem::GetPathOnDevice()
{
    return pathOnDevice;
}

std::wstring XContentDeviceItem::GetName()
{
    if (!content)
        return L"";

    return content->metaData->displayName;
}

BYTE *XContentDeviceItem::GetThumbnail()
{
    if (!content)
        return nullptr;
    return content->metaData->thumbnailImage.empty() ? nullptr : content->metaData->thumbnailImage.data();
}

DWORD XContentDeviceItem::GetThumbnailSize()
{
    if (!content)
        return 0;
    return content->metaData->thumbnailImageSize;
}

std::string XContentDeviceItem::GetRawName()
{
    return rawName;
}

BYTE *XContentDeviceItem::GetProfileID()
{
    if (!content)
        return nullptr;
    return content->metaData->profileID;
}

UINT64 XContentDeviceItem::GetFileSize()
{
    UINT64 toReturn = fileSize;
    if (content)
        toReturn += content->metaData->dataFileCombinedSize;
    return toReturn;
}

std::vector<std::string> XContentDeviceItem::GetContentFilePaths()
{
    return contentFilePaths;
}
