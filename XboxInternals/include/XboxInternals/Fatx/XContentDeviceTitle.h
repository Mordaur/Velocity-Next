#ifndef XCONTENTDEVICETITLE_H
#define XCONTENTDEVICETITLE_H

#include <iostream>
#include <vector>
#include <XboxInternals/Stfs/StfsPackage.h>
#include <XboxInternals/Fatx/XContentDeviceItem.h>
#include <XboxInternals/Export.h>

class XBOXINTERNALSSHARED_EXPORT XContentDeviceTitle : public XContentDeviceItem
{
public:
    XContentDeviceTitle();
    XContentDeviceTitle(std::string pathOnDevice, std::string rawName);

    std::wstring GetName();
    u8* GetThumbnail();
    u32 GetThumbnailSize();
    u32 GetTitleID();
    u8* GetProfileID();

    std::vector<XContentDeviceItem> titleSaves;

private:
    std::string pathOnDevice;
};

#endif // XCONTENTDEVICETITLE_H
