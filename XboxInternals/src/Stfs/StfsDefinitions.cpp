#include <XboxInternals/Stfs/StfsDefinitions.h>

#include <XboxInternals/Export.h>

void ReadStfsVolumeDescriptorEx(StfsVolumeDescriptor *descriptor, BaseIO *io, u32 address)
{
    // seek to the volume descriptor
    io->SetPosition(address);

    // read the descriptor
    descriptor->size = io->ReadByte();
    descriptor->reserved = io->ReadByte();
    descriptor->blockSeparation = io->ReadByte();

    io->SetEndian(LittleEndian);

    descriptor->fileTableBlockCount = io->ReadWord();
    descriptor->fileTableBlockNum = io->ReadInt24();
    io->ReadBytes(descriptor->topHashTableHash, 0x14);

    io->SetEndian(BigEndian);

    descriptor->allocatedBlockCount = io->ReadDword();
    descriptor->unallocatedBlockCount = io->ReadDword();
}

void ReadSvodVolumeDescriptorEx(SvodVolumeDescriptor *descriptor, BaseIO *io)
{
    // seek to the volume descriptor
    io->SetPosition(0x379);

    descriptor->size = io->ReadByte();
    descriptor->blockCacheElementCount = io->ReadByte();
    descriptor->workerThreadProcessor = io->ReadByte();
    descriptor->workerThreadPriority = io->ReadByte();

    io->ReadBytes(descriptor->rootHash, 0x14);

    descriptor->flags = io->ReadByte();
    descriptor->dataBlockCount = io->ReadInt24(BigEndian);
    // SvodCreateDevice reads the offset little-endian (low byte first),
    // unlike the big-endian dataBlockCount above.
    descriptor->dataBlockOffset = io->ReadInt24(LittleEndian);

    io->ReadBytes(descriptor->reserved, 0x05);
}

XBOXINTERNALS_EXPORT string LicenseTypeToString(LicenseType type)
{
    switch (type)
    {
        case Unused:
            return string("Unused");
        case Unrestricted:
            return string("Unrestricted");
        case ConsoleProfileLicense:
            return string("ConsoleProfileLicense");
        case WindowsProfileLicense:
            return string("WindowsProfileLicense");
        case ConsoleLicense:
            return string("ConsoleLicense");
        case MediaFlags:
            return string("MediaFlags");
        case KeyVaultPrivileges:
            return string("KeyVaultPrivileges");
        case HyperVisorFlags:
            return string("HyperVisorFlags");
        case UserPrivileges:
            return string("UserPrivileges");
        default:
            throw string("STFS: Invalid 'License Type' value.\n");
    }
}

XBOXINTERNALS_EXPORT string ByteSizeToString(u64 bytes)
{
    u32 B = 1; //byte
    u32 KB = 1024 * B; //kilobyte
    u32 MB = 1024 * KB; //megabyte
    u32 GB = 1024 * MB; //gigabyte

    // divide stuff
    std::stringstream result;
    result.precision(1);
    if (bytes > GB)
        result << std::fixed << ((float)bytes / GB) << " GB";
    else if (bytes > MB)
        result << std::fixed << ((float)bytes / MB) << " MB";
    else if (bytes > KB)
        result << std::fixed << ((u32)(((bytes / (float)KB) + 0.005f) * 100) / 100.0f) << " KB";
    else
        result << bytes << " bytes";

    return result.str();
}

XBOXINTERNALS_EXPORT u32 MSTimeToDWORD(MSTime time)
{
    u32 toReturn = 0;
    toReturn |= ((time.year - 1980) & 0x7F) << 25;
    toReturn |= (time.month & 0xF) << 21;
    toReturn |= (time.monthDay & 0x1F) << 16;
    toReturn |= (time.hours & 0x1F) << 11;
    toReturn |= (time.minutes & 0x3F) << 5;
    // FAT-style timestamps store seconds in two-second units.
    toReturn |= (time.seconds >> 1) & 0x1F;

    return toReturn;
}

XBOXINTERNALS_EXPORT MSTime DWORDToMSTime(u32 winTime)
{
    MSTime time;

    time.year = ((winTime & 0xFE000000) >> 25) + 1980;
    time.month = (winTime & 0x1E00000) >> 21;
    time.monthDay = (winTime & 0x1F0000) >> 16;
    time.hours = (winTime & 0xF800) >> 11;
    time.minutes = (winTime & 0x7E0) >> 5;
    time.seconds = (winTime & 0x1F) * 2;

    return time;
}

XBOXINTERNALS_EXPORT MSTime TimetToMSTime(time_t time)
{
    struct tm *timeInfo = localtime(&time);

    MSTime toReturn;
    toReturn.year = timeInfo->tm_year + 1900;
    toReturn.month = timeInfo->tm_mon + 1;
    toReturn.monthDay = timeInfo->tm_mday;
    toReturn.hours = timeInfo->tm_hour;
    toReturn.minutes = timeInfo->tm_min;
    toReturn.seconds = timeInfo->tm_sec;

    return toReturn;
}

void WriteStfsVolumeDescriptorEx(StfsVolumeDescriptor *descriptor, BaseIO *io, u32 address)
{
    // volume descriptor position
    io->SetPosition(address);

    // Write size, padding and block separation
    u32 start = 0x240000;
    start |= descriptor->blockSeparation;
    io->WriteInt24(start);

    // Write the rest of the descriptor
    io->SwapEndian();
    io->Write(descriptor->fileTableBlockCount);
    io->SwapEndian();

    io->WriteInt24(descriptor->fileTableBlockNum, LittleEndian);
    io->Write(descriptor->topHashTableHash, 0x14);
    io->Write(descriptor->allocatedBlockCount);
    io->Write(descriptor->unallocatedBlockCount);
}

void WriteSvodVolumeDescriptorEx(SvodVolumeDescriptor *descriptor, BaseIO *io)
{
    // volume descriptor position
    io->SetPosition(0x379);
    io->SetEndian(LittleEndian);

    io->Write(descriptor->size);
    io->Write(descriptor->blockCacheElementCount);
    io->Write(descriptor->workerThreadProcessor);
    io->Write(descriptor->workerThreadPriority);

    io->Write(descriptor->rootHash, 0x14);

    io->Write(descriptor->flags);
    io->WriteInt24(descriptor->dataBlockCount, BigEndian);
    io->WriteInt24(descriptor->dataBlockOffset, LittleEndian);

    io->Write(descriptor->reserved, 5);

    io->SetEndian(BigEndian);
}

void ReadCertificateEx(Certificate *cert, BaseIO *io, u32 address)
{
    // seek to the address of the certificate
    io->SetPosition(address);

    cert->publicKeyCertificateSize = io->ReadWord();
    io->ReadBytes(cert->ownerConsoleID, 5);

    char tempPartNum[0x15] = {0};
    io->ReadBytes((u8*)tempPartNum, 0x11);
    // Keep all 17 bytes: part number (0xB) + reserved (4) + privileges (2).
    cert->ownerConsolePartNumber.assign(tempPartNum, 0x11);

    u32 temp = io->ReadDword();
    cert->ownerConsoleType = (ConsoleType)(temp & 3);
    cert->consoleTypeFlags = (ConsoleTypeFlags)(temp & 0xFFFFFFFC);
    if (cert->ownerConsoleType != DevKit && cert->ownerConsoleType != Retail)
        throw string("STFS: Invalid console type.\n");

    char tempGenDate[9] = {0};
    io->ReadBytes((u8*)tempGenDate, 8);
    cert->dateGeneration = string(tempGenDate);

    cert->publicExponent = io->ReadDword();
    io->ReadBytes(cert->publicModulus, 0x80);
    io->ReadBytes(cert->certificateSignature, 0x100);
    io->ReadBytes(cert->signature, 0x80);
}

void WriteCertificateEx(Certificate *cert, BaseIO *io, u32 address)
{
    // seek to the position of the certificate
    io->SetPosition(address);

    // Write the certificate
    io->Write((u16)cert->publicKeyCertificateSize);
    io->Write(cert->ownerConsoleID, 5);
    io->Write(cert->ownerConsolePartNumber, 0x11, false);
    u32 temp = static_cast<u32>(cert->consoleTypeFlags) | static_cast<u32>(cert->ownerConsoleType);
    io->Write(temp);
    io->Write(cert->dateGeneration, 0x8, false);
    io->Write(cert->publicExponent);
    io->Write(cert->publicModulus, 0x80);
    io->Write(cert->certificateSignature, 0x100);
    io->Write(cert->signature, 0x80);
}

XBOXINTERNALS_EXPORT string MagicToString(Magic magic)
{
    switch (magic)
    {
        case CON:
            return string("CON ");
        case LIVE:
            return string("LIVE");
        case PIRS:
            return string("PIRS");
        default:
            return string("N/A");
    }
}

XBOXINTERNALS_EXPORT string ConsoleTypeToString(ConsoleType type)
{
    switch (type)
    {
        case DevKit:
            return string("DevKit");
        case Retail:
            return string("Retail");
        default:
            throw string("STFS: Invalid console type.\n");
    }
}

XBOXINTERNALS_EXPORT string ContentTypeToString(ContentType type)
{
    switch (type)
    {
        case ArcadeGame:
            return string("Arcade Game");
        case AvatarAssetPack:
            return string("Avatar Asset Pack");
        case AvatarItem:
            return string("AvatarItem");
        case CacheFile:
            return string("Cache File");
        case CommunityGame:
            return string("Community Game");
        case GameDemo:
            return string("Game Demo");
        case GameOnDemand:
            return string("Game On Demand");
        case GamerPicture:
            return string("Gamer Picture");
        case GamerTitle:
            return string("Gamer Title");
        case GameTrailer:
            return string("Game Trailer");
        case GameVideo:
            return string("Game Video");
        case InstalledGame:
            return string("Installed Game");
        case Installer:
            return string("Installer");
        case IPTVPauseBuffer:
            return string("IPTV Pause Buffer");
        case LicenseStore:
            return string("License Store");
        case MarketPlaceContent:
            return string("Market Place Content");
        case Movie:
            return string("Movie");
        case MusicVideo:
            return string("Music Video");
        case PodcastVideo:
            return string("Podcast Video");
        case Profile:
            return string("Profile");
        case Publisher:
            return string("Publisher");
        case SavedGame:
            return string("Saved Game");
        case StorageDownload:
            return string("Storage Download");
        case Theme:
            return string("Theme");
        case Video:
            return string("Video");
        case ViralVideo:
            return string("Viral Video");
        case XboxDownload:
            return string("Xbox Download");
        case XboxOriginalGame:
            return string("Xbox Original Game");
        case XboxSavedGame:
            return string("Xbox Saved Game");
        case Xbox360Title:
            return string("Xbox360 Title");
        case XNA:
            return string("XNA");
        case ContentTypeUnknown:
        default:
            return string("Unknown");
    }
}


