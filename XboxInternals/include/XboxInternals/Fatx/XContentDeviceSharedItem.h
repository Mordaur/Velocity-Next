#ifndef XCONTENTDEVICESHAREDITEM_H
#define XCONTENTDEVICESHAREDITEM_H

#include <memory>
#include <XboxInternals/Stfs/IXContentHeader.h>
#include <XboxInternals/Fatx/XContentDeviceItem.h>
#include <XboxInternals/Export.h>

class XBOXINTERNALSSHARED_EXPORT XContentDeviceSharedItem : public XContentDeviceItem
{
public:
    XContentDeviceSharedItem();
    XContentDeviceSharedItem(FatxFileEntry *fileEntry, std::shared_ptr<IXContentHeader> content, std::vector<std::string> contentFilePaths=std::vector<std::string>());
    XContentDeviceSharedItem(std::string pathOnDevice, std::string rawName, std::shared_ptr<IXContentHeader> content, u64 fileSize, std::vector<std::string> contentFilePaths=std::vector<std::string>());
};

#endif // XCONTENTDEVICESHAREDITEM_H
