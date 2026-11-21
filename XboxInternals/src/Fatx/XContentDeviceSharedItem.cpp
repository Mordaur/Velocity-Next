#include <XboxInternals/Fatx/XContentDeviceSharedItem.h>

XContentDeviceSharedItem::XContentDeviceSharedItem() :
    XContentDeviceItem()
{

}

XContentDeviceSharedItem::XContentDeviceSharedItem(FatxFileEntry *fileEntry, std::shared_ptr<IXContentHeader> package, std::vector<std::string> contentFilePaths) :
    XContentDeviceItem(fileEntry, std::move(package), contentFilePaths)
{

}

XContentDeviceSharedItem::XContentDeviceSharedItem(std::string pathOnDevice, std::string rawName, std::shared_ptr<IXContentHeader> content, u64 fileSize, std::vector<std::string> contentFilePaths) :
    XContentDeviceItem(pathOnDevice, rawName, std::move(content), fileSize, contentFilePaths)
{

}

