#ifndef StfsConstants
#define StfsConstants

#include <XboxInternals/TypeDefinitions.h>

enum StfsPackageFlags
{
    StfsPackagePEC = 1,
    StfsPackageCreate = 2,
    StfsPackageFemale = 4,    // only used when creating a packge
    StfsPackageDeleteIO = 8,
    StfsPackageDontReadFileListing = 16
};

enum Sex
{
    StfsFemale = 0,
    StfsMale
};

enum Level
{
    Zero,
    One,
    Two
};

enum ConsoleType
{
    DevKit = 1,
    Retail = 2
};

enum ConsoleTypeFlags
{
    TestKit = 0x40000000,
    RecoveryGenerated = 0x80000000
};

enum Magic
{
    Unknown = 0,
    CON = 0x434F4E20,
    LIVE = 0x4C495645,
    PIRS = 0x50495253
};

enum FileEntryFlags
{
    ConsecutiveBlocks = 1,
    Folder = 2
};

enum InstallerType
{
    InstallerTypeUnknown = -1,
    None = 0,
    SystemUpdate = 0x53555044,
    TitleUpdate = 0x54555044,
    SystemUpdateProgressCache = 0x50245355,
    TitleUpdateProgressCache = 0x50245455,
    TitleContentProgressCache = 0x50245443
};

enum ContentType
{
    ContentTypeUnknown = 0,
    ArcadeGame = 0xD0000,
    AvatarAssetPack = 0x8000,
    AvatarItem = 0x9000,
    CacheFile = 0x40000,
    CommunityGame = 0x2000000,
    GameDemo = 0x80000,
    GameOnDemand = 0x7000,
    GamerPicture = 0x20000,
    GamerTitle = 0xA0000,
    GameTrailer = 0xC0000,
    GameVideo = 0x400000,
    InstalledGame = 0x4000,
    Installer = 0xB0000,
    IPTVPauseBuffer = 0x2000,
    LicenseStore = 0xF0000,
    MarketPlaceContent = 2,
    Movie = 0x100000,
    MusicVideo = 0x300000,
    PodcastVideo = 0x500000,
    Profile = 0x10000,
    Publisher = 3,
    SavedGame = 1,
    StorageDownload = 0x50000,
    Theme = 0x30000,
    Video = 0x200000,
    ViralVideo = 0x600000,
    XboxDownload = 0x70000,
    XboxOriginalGame = 0x5000,
    XboxSavedGame = 0x60000,
    Xbox360Title = 0x1000,
    XNA = 0xE0000,
    IndieGame = XNA
};

template <>
struct EnumTraits<ContentType>
{
    static ContentType FromRaw(UINT64 raw)
    {
        return raw <= CommunityGame ? static_cast<ContentType>(raw) : ContentTypeUnknown;
    }
};

template <>
struct EnumTraits<InstallerType>
{
    static InstallerType FromRaw(UINT64 raw)
    {
        switch (raw)
        {
            case None:
            case SystemUpdate:
            case TitleUpdate:
            case SystemUpdateProgressCache:
            case TitleUpdateProgressCache:
            case TitleContentProgressCache:
                return static_cast<InstallerType>(raw);
            default:
                return InstallerTypeUnknown;
        }
    }
};

enum BlockStatusLevelZero
{
    Unallocated = 0,
    PreviouslyAllocated = 0x40,
    Allocated = 0x80,
    NewlyAllocated = 0xC0
};

// this is from eaton
enum SVODFeatures
{
    EnhancedGDFLayout = 0x40,
    houldBeZeroForDownLevelClients = 0x80
};

struct Version
{
    WORD major, minor, build, revision;
};

#endif


