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
    XContentDeviceItem(std::string pathOnDevice, std::string rawName, std::shared_ptr<IXContentHeader> content, UINT64 fileSize = 0, std::vector<std::string> contentFilePaths=std::vector<std::string>());

    std::shared_ptr<IXContentHeader> content;

    virtual std::string GetPathOnDevice();
    virtual std::wstring GetName();
    virtual BYTE* GetThumbnail();
    virtual DWORD GetThumbnailSize();
    virtual std::string GetRawName();
    virtual BYTE* GetProfileID();
    virtual UINT64 GetFileSize();

    std::vector<std::string> GetContentFilePaths();

private:
    std::string pathOnDevice;
    std::vector<std::string> contentFilePaths;
    std::string rawName;
    UINT64 fileSize = 0;
};

#endif // XCONTENTDEVICEITEM_H
