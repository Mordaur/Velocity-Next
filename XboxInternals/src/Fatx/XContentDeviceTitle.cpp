#include <XboxInternals/Fatx/XContentDeviceTitle.h>

XContentDeviceTitle::XContentDeviceTitle() :
    XContentDeviceItem()
{

}

XContentDeviceTitle::XContentDeviceTitle(std::string pathOnDevice, std::string rawName) :
    XContentDeviceItem(pathOnDevice, rawName, nullptr), pathOnDevice(std::move(pathOnDevice))
{

}

std::wstring XContentDeviceTitle::GetName()
{
    if (titleSaves.size() == 0)
        return L"";
    return titleSaves.at(0).content->metaData->titleName;
}

u8 *XContentDeviceTitle::GetThumbnail()
{
    if (titleSaves.size() == 0)
        return nullptr;
    auto *metaData = titleSaves.at(0).content->metaData;
    if (!metaData->titleThumbnailImage.empty())
        return metaData->titleThumbnailImage.data();
    if (!metaData->thumbnailImage.empty())
        return metaData->thumbnailImage.data();
    return nullptr;
}

u32 XContentDeviceTitle::GetThumbnailSize()
{
    if (titleSaves.size() == 0)
        return 0;
    return titleSaves.at(0).content->metaData->titleThumbnailImageSize;
}

u32 XContentDeviceTitle::GetTitleID()
{
    if (titleSaves.size() == 0)
        return 0;
    return titleSaves.at(0).content->metaData->titleID;
}

u8 *XContentDeviceTitle::GetProfileID()
{
    if (titleSaves.size() == 0)
        return nullptr;
    return titleSaves.at(0).content->metaData->profileID;
}
