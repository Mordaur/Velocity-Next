#include "contentnames.h"

#include <QHash>

#include <cctype>
#include <cstdlib>

#include <XboxInternals/Stfs/StfsConstants.h>
#include <XboxInternals/Stfs/StfsDefinitions.h>

namespace
{
    bool isHex(const std::string &text)
    {
        if (text.empty())
            return false;

        for (char character : text)
            if (!std::isxdigit(static_cast<unsigned char>(character)))
                return false;

        return true;
    }

    bool isTitleIdShape(const std::string &upper)
    {
        // Matches GODspeed's game pattern [1-9A-E][0-9A-F]{7}: content-type
        // folders start with '0' and F-prefixed IDs are system data
        // (FFFE07D1 is the profile area, not a title).
        if (upper.size() != 8 || !isHex(upper))
            return false;

        const char first = upper[0];
        return first >= '1' && first <= '9' ? true : (first >= 'A' && first <= 'E');
    }

    bool isContentTypeShape(const std::string &upper)
    {
        return upper.size() == 8 && isHex(upper) && upper[0] == '0';
    }

    bool isProfileShape(const std::string &upper)
    {
        return upper.size() == 16 && upper[0] == 'E' && isHex(upper);
    }

    QString contentTypesLabel(const std::string &upper)
    {
        const DWORD value = static_cast<DWORD>(std::strtoul(upper.c_str(), nullptr, 16));

        switch (static_cast<ContentType>(value))
        {
        case ArcadeGame: return "Arcade Game";
        case AvatarAssetPack: return "Avatar Asset Pack";
        case AvatarItem: return "Avatar Item";
        case CacheFile: return "Cache File";
        case CommunityGame: return "Community Game";
        case GameDemo: return "Game Demo";
        case GameOnDemand: return "Games on Demand";
        case GamerPicture: return "Gamer Pictures";
        case GamerTitle: return "Gamer Title";
        case GameTrailer: return "Game Trailer";
        case GameVideo: return "Game Video";
        case InstalledGame: return "Installed Game";
        case Installer: return "Installer";
        case IPTVPauseBuffer: return "Pause Buffer";
        case LicenseStore: return "License Store";
        case MarketPlaceContent: return "Marketplace Content";
        case Movie: return "Movie";
        case MusicVideo: return "Music Video";
        case PodcastVideo: return "Podcast Video";
        case Profile: return "Profiles";
        case Publisher: return "Publisher Content";
        case SavedGame: return "Saved Games";
        case StorageDownload: return "Storage Download";
        case Theme: return "Themes";
        case Video: return "Video";
        case ViralVideo: return "Viral Video";
        case XboxDownload: return "Xbox Download";
        case XboxOriginalGame: return "Xbox Originals";
        case XboxSavedGame: return "Xbox Saved Games";
        case Xbox360Title: return "Installed Xbox 360 Games";
        case XNA: return "XNA / Indie Games";
        default: return {};
        }
    }

    QString systemFolderLabel(const std::string &upper, bool isDirectory)
    {
        if (upper == "0000000000000000")
            return "Games";

        if (upper == "584E07D2")
            return "XNA Indie Games";

        if (upper == "FFFE07C3")
            return "Gamer Pictures";

        if (upper == "FFFE07D1")
            return "Profiles";

        if (upper == "FFFE07DF")
            return "Avatar Editor";

        if (upper.size() == 8 && upper[0] == 'F' && isHex(upper))
            return isDirectory ? "System Data" : "System Data File";

        return {};
    }
}

bool ContentNames::IsTitleId(const std::string &name)
{
    std::string upper(name);
    for (char &character : upper)
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));

    return isTitleIdShape(upper);
}

bool ContentNames::IsTitleIdFile(const std::string &name)
{
    std::string upper(name);
    for (char &character : upper)
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));

    if (upper.size() != 12 || upper.compare(8, 4, ".GPD") != 0)
        return false;

    return isTitleIdShape(upper.substr(0, 8));
}

bool ContentNames::IsDataFile(const std::string &name)
{
    std::string upper(name);
    for (char &character : upper)
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));

    if (upper.rfind("TU", 0) == 0 && upper.size() > 2)
        return true;

    return upper.size() >= 4 && isHex(upper);
}

QString ContentNames::Recognize(const std::string &name, bool isDirectory)
{
    std::string upper(name);
    for (char &character : upper)
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));

    // .gpd files carry the title ID as their file name.
    if (!isDirectory && upper.size() > 4 &&
        upper.compare(upper.size() - 4, 4, ".GPD") == 0)
    {
        return "Game Data";
    }

    if (const QString systemLabel = systemFolderLabel(upper, isDirectory);
        !systemLabel.isEmpty())
        return systemLabel;

    if (isProfileShape(upper))
        return "Profile";

    if (isContentTypeShape(upper))
    {
        const QString label = contentTypesLabel(upper);
        return label.isEmpty() ? QString("Content") : label;
    }

    if (isTitleIdShape(upper))
        return "Unknown Game";

    return {};
}

QString ContentNames::Display(const std::string &name, bool isDirectory, bool friendly)
{
    const QString raw = QString::fromStdString(name);
    if (!friendly)
        return raw;

    const QString label = Recognize(name, isDirectory);
    if (label.isEmpty())
        return raw;

    return label + " (" + raw + ")";
}
