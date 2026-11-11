#ifndef XCONTENTDEVICE_H
#define XCONTENTDEVICE_H

#include <iostream>
#include <memory>
#include <vector>
#include <ctype.h>
#include <iomanip>

#ifndef _WIN32
    #include <sys/stat.h>
#endif

#include <XboxInternals/Stfs/StfsPackage.h>
#include <XboxInternals/Fatx/FatxDrive.h>
#include <XboxInternals/IO/FatxIO.h>
#include <XboxInternals/Disc/Svod.h>
#include <XboxInternals/Fatx/XContentDeviceProfile.h>
#include <XboxInternals/Fatx/XContentDeviceSharedItem.h>
#include <XboxInternals/Utils.h>
#include <XboxInternals/Export.h>

class XBOXINTERNALSSHARED_EXPORT XContentDevice
{
public:
    XContentDevice(FatxDrive *drive);
    ~XContentDevice();

    std::unique_ptr<std::vector<XContentDeviceProfile>> profiles;

    std::unique_ptr<std::vector<XContentDeviceSharedItem>> games;
    std::unique_ptr<std::vector<XContentDeviceSharedItem>> dlc;
    std::unique_ptr<std::vector<XContentDeviceSharedItem>> demos;
    std::unique_ptr<std::vector<XContentDeviceSharedItem>> videos;
    std::unique_ptr<std::vector<XContentDeviceSharedItem>> themes;
    std::unique_ptr<std::vector<XContentDeviceSharedItem>> gamerPictures;
    std::unique_ptr<std::vector<XContentDeviceSharedItem>> avatarItems;
    std::unique_ptr<std::vector<XContentDeviceSharedItem>> updates;
    std::unique_ptr<std::vector<XContentDeviceSharedItem>> systemItems;

    bool LoadDevice(void(*progress)(void*, bool) = NULL, void *arg = NULL);
    FatxDriveType GetDeviceType();
    UINT64 GetFreeMemory(void(*progress)(void*, bool) = NULL, void *arg = NULL, bool finish = true);
    UINT64 GetTotalMemory();
    std::wstring GetName();
    void SetName(std::wstring name);
    FatxDrive *GetFatxDrive();

    void CopyFileToLocalDisk(std::string outPath, std::string inPath, void(*progress)(void*, DWORD, DWORD) = NULL, void *arg = NULL);
    void CopyFileToDevice(std::string outPath, void(*progress)(void*, DWORD, DWORD) = NULL, void *arg = NULL);
    void CopyFileToRawDevice(std::string outPath, std::string name, std::string inPath, void(*progress)(void*, DWORD, DWORD) = NULL, void *arg = NULL);
    void DeleteFile(IXContentHeader *package, std::string pathOnDevice);

private:
    FatxDrive *drive;
    Partition *content;

    bool ValidOfflineXuid(std::string xuid);
    bool ValidTitleID(std::string id);
    void GetAllContentItems(FatxFileEntry &titleFolder, vector<XContentDeviceItem> &itemsFound, void(*progress)(void*, bool) = NULL, void *arg = NULL);
    void CleanupSharedFiles(std::vector<XContentDeviceSharedItem> *category);
    std::string ToUpper(std::string str);
};

#endif // XCONTENTDEVICE_H
