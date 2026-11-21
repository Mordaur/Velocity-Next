#ifndef XCONTENTDEVICEITEM_H
#define XCONTENTDEVICEITEM_H

#include <iostream>
#include <memory>
#include <XboxInternals/Stfs/IXContentHeader.h>
#include <XboxInternals/Fatx/FatxDrive.h>
#include <XboxInternals/Export.h>

class XBOXINTERNALSSHARED_EXPORT XContentDeviceItem
{
public:
    XContentDeviceItem();
    XContentDeviceItem(FatxFileEntry *fileEntry, std::shared_ptr<IXContentHeader> content, std::vector<std::string> contentFilePaths=std::vector<std::string>());
    XContentDeviceItem(std::string pathOnDevice, std::string rawName, std::shared_ptr<IXContentHeader> content, u64 fileSize = 0, std::vector<std::string> contentFilePaths=std::vector<std::string>());

    std::shared_ptr<IXContentHeader> content;

    virtual std::string GetPathOnDevice();
    virtual std::wstring GetName();
    virtual u8* GetThumbnail();
    virtual u32 GetThumbnailSize();
    virtual std::string GetRawName();
    virtual u8* GetProfileID();
    virtual u64 GetFileSize();

    std::vector<std::string> GetContentFilePaths();

private:
    std::string pathOnDevice;
    std::vector<std::string> contentFilePaths;
    std::string rawName;
    u64 fileSize = 0;
};

#endif // XCONTENTDEVICEITEM_H
