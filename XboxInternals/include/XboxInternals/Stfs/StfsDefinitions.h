#ifndef STFSDEFINITIONS_H
#define STFSDEFINITIONS_H

#include <XboxInternals/Types.h>
#include <XboxInternals/Stfs/StfsConstants.h>
#include <XboxInternals/IO/FileIO.h>
#include <iostream>
#include <sstream>
#include <time.h>

using std::string;

// STFS block numbers are 24-bit on disk and held in a u32.
// 0xFFFFFF ends a hash-entry chain and marks an empty file's start block.
inline constexpr u32 kBlockChainTerminator = 0xFFFFFF;
inline constexpr u32 kBlockNumberMask = 0xFFFFFF;
enum LicenseType
{
    Unused = 0x0000,
    LicenseTypeUnknown = 0x0001,
    Unrestricted = 0xFFFF,
    ConsoleProfileLicense = 0x0009,
    WindowsProfileLicense = 0x0003,
    ConsoleLicense = 0xF000,
    MediaFlags = 0xE000,
    KeyVaultPrivileges = 0xD000,
    HyperVisorFlags = 0xC000,
    UserPrivileges = 0xB000
};

struct LicenseEntry
{
    LicenseType type;
    u64 data;
    u32 bits;
    u32 flags;
};

struct StfsVolumeDescriptor
{
    u8 size;
    u8 reserved;
    u8 blockSeparation;
    u16 fileTableBlockCount;
    u32 fileTableBlockNum;
    u8 topHashTableHash[0x14];
    u32 allocatedBlockCount;
    u32 unallocatedBlockCount;
};

struct SvodVolumeDescriptor
{
    u8 size;
    u8 blockCacheElementCount;
    u8 workerThreadProcessor;
    u8 workerThreadPriority;
    u8 rootHash[0x14];
    u8 flags;
    u32 dataBlockCount;
    u32 dataBlockOffset;
    u8 reserved[5];
};

struct Certificate
{
    u16 publicKeyCertificateSize;
    u8 ownerConsoleID[5];
    string ownerConsolePartNumber;
    ConsoleType ownerConsoleType;
    ConsoleTypeFlags consoleTypeFlags;
    string dateGeneration;
    u32 publicExponent;
    u8 publicModulus[0x80];
    u8 certificateSignature[0x100];
    u8 signature[0x80];
};

struct MSTime
{
    u16 year;
    u8 month;
    u8 monthDay;

    u8 hours;
    u8 minutes;
    u8 seconds;
};

void ReadStfsVolumeDescriptorEx(StfsVolumeDescriptor *descriptor, BaseIO *io, u32 address);

XBOXINTERNALS_EXPORT void ReadSvodVolumeDescriptorEx(SvodVolumeDescriptor *descriptor, BaseIO *io);

void WriteStfsVolumeDescriptorEx(StfsVolumeDescriptor *descriptor, BaseIO *io, u32 address);

XBOXINTERNALS_EXPORT void WriteSvodVolumeDescriptorEx(SvodVolumeDescriptor *descriptor, BaseIO *io);

void ReadCertificateEx(Certificate *cert, BaseIO *io, u32 address);

void WriteCertificateEx(Certificate *cert, BaseIO *io, u32 address);

XBOXINTERNALS_EXPORT string MagicToString(Magic magic);

XBOXINTERNALS_EXPORT string ContentTypeToString(ContentType type);

XBOXINTERNALS_EXPORT string ConsoleTypeToString(ConsoleType type);

template <>
struct EnumTraits<LicenseType>
{
    static LicenseType FromRaw(u64 raw)
    {
        switch (raw)
        {
            case Unused:
            case Unrestricted:
            case ConsoleProfileLicense:
            case WindowsProfileLicense:
            case ConsoleLicense:
            case MediaFlags:
            case KeyVaultPrivileges:
            case HyperVisorFlags:
            case UserPrivileges:
                return static_cast<LicenseType>(raw);
            default:
                return LicenseTypeUnknown;
        }
    }
};

XBOXINTERNALS_EXPORT string ByteSizeToString(u64 bytes);

XBOXINTERNALS_EXPORT string LicenseTypeToString(LicenseType type);

XBOXINTERNALS_EXPORT MSTime DWORDToMSTime(u32 winTime);

XBOXINTERNALS_EXPORT u32 MSTimeToDWORD(MSTime time);

XBOXINTERNALS_EXPORT MSTime TimetToMSTime(time_t time);

#endif // STFSDEFINITIONS_H


