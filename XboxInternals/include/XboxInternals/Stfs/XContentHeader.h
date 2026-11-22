#pragma once

#include <XboxInternals/Types.h>
#include <XboxInternals/EnumTraits.h>

#include <XboxInternals/Stfs/StfsConstants.h>
#include <XboxInternals/Stfs/StfsDefinitions.h>
#include <XboxInternals/IO/FileIO.h>
#include "../AvatarAsset/AvatarAssetDefinitions.h"
#include "../Gpd/XdbfHelpers.h"
#include "../Cryptography/XeCrypt.h"

#include <iostream>
#include <vector>

#include <botan_all.h>

#include <XboxInternals/Export.h>

using std::string;

enum XContentFlags
{
    MetadataIsPEC = 1,
    MetadataSkipRead = 2,
    MetadataDontFreeThumbnails = 4
};

enum OnlineContentResumeState
{
    ResumeStateUnknown = 0,
    FileHeadersNotReady = 0x46494C48,
    NewFolder = 0x666F6C64,
    NewFolderResum_Attempt1 = 0x666F6C31,
    NewFolderResumeAttempt2 = 0x666F6C32,
    NewFolderResumeAttemptUnknown = 0x666F6C3F,
    NewFolderResumeAttemptSpecific = 0x666F6C40
};

enum FileSystem
{
    FileSystemSTFS = 0,
    FileSystemSVOD,
    FileSystemFATX,
    FileSystemUnknown
};

template <>
struct EnumTraits<FileSystem>
{
    static FileSystem FromRaw(u64 raw)
    {
        return raw <= FileSystemFATX ? static_cast<FileSystem>(raw) : FileSystemUnknown;
    }
};

template <>
struct EnumTraits<OnlineContentResumeState>
{
    static OnlineContentResumeState FromRaw(u64 raw)
    {
        switch (raw)
        {
            case FileHeadersNotReady:
            case NewFolder:
            case NewFolderResum_Attempt1:
            case NewFolderResumeAttempt2:
            case NewFolderResumeAttemptUnknown:
            case NewFolderResumeAttemptSpecific:
                return static_cast<OnlineContentResumeState>(raw);
            default:
                return ResumeStateUnknown;
        }
    }
};

class XBOXINTERNALS_EXPORT XContentHeader
{
public:
    // Description: read in all of the metadata for the package
    XContentHeader(BaseIO *io, u32 flags = 0);

    // Byte shift applied to the legacy keyvault offsets for a decrypted
    // keyvault of the given size (0x4000 -> 0x10, 0x3FF0 -> 0). Throws for
    // unsupported sizes.
    static u32 KeyvaultOffsetShift(u64 size);

    // fix the signature in the header
    void ResignHeader(string kvPath);

    // fix the signature in the header
    void ResignHeader(u8* kvData, size_t length);

    // fix the signature in the header
    void ResignHeader(BaseIO& kvIo);

    // fix the sha1 hash of the header data
    void FixHeaderHash();

    // Description: Write the console certificate
    void WriteCertificate();

    // Description: Write the volume descriptor
    void WriteVolumeDescriptor();

    // Description: Write all of the metadata
    void WriteMetaData();

    // Verify the 0x100-byte LIVE/PIRS package signature at 0x004 (RSA-2048
    // PKCS#1 v1.5/SHA-1 over [0x22C,0x344)). Returns false for CON packages or
    // when no candidate public key validates.
    bool VerifyPackageSignature();
    ~XContentHeader();

    Magic magic = Unknown;

    // only console signed
    Certificate certificate;

    // only strong signed
    u8 packageSignature[0x100];

    LicenseEntry licenseData[0x10];
    u8 headerHash[0x14];
    u32 headerSize;
    ContentType contentType;
    u32 metaDataVersion;
    u64 contentSize;
    u32 mediaID;
    u32 version;
    u32 baseVersion;
    u32 titleID;
    u8 platform;
    u8 executableType;
    u8 discNumber;
    u8 discInSet;
    u32 savegameID;
    u8 consoleID[5];
    u8 profileID[8];

    StfsVolumeDescriptor stfsVolumeDescriptor;
    SvodVolumeDescriptor svodVolumeDescriptor;
    FileSystem fileSystem;

    // only in PEC, and im not sure exactly what this byte is but it needs to always be set to 1
    bool enabled;

    // metadata v1
    u32 dataFileCount;
    u64 dataFileCombinedSize;
    u8 deviceID[0x14];
    wstring displayName;
    wstring displayDescription;
    wstring publisherName;
    wstring titleName;
    u8 transferFlags;
    u32 thumbnailImageSize;
    u32 titleThumbnailImageSize;

    // credit to Eaton for all the extra metadata stuff
    InstallerType installerType;

    // Avatar Asset
    AssetSubcategory subCategory;
    u32 colorizable;
    u8 guid[0x10];
    SkeletonVersion skeletonVersion;

    // media
    u8 seriesID[0x10];
    u8 seasonID[0x10];
    u16 seasonNumber;
    u16 episodeNumber;

    // installer progress cache data
    OnlineContentResumeState resumeState;
    u32 currentFileIndex;
    u64 currentFileOffset;
    u64 bytesProcessed;
    time_t lastModified;
    u8 cabResumeData[5584];

    // installer update data
    Version installerBaseVersion;
    Version installerVersion;

    std::vector<u8> thumbnailImage;
    std::vector<u8> titleThumbnailImage;

private:
    BaseIO *io;
    u32 flags;

    void readMetadata();
};



