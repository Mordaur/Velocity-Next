#pragma once

#include <XboxInternals/Types.h>
#include <XboxInternals/Gpd/XdbfDefinitions.h>
#include <iostream>
#include <time.h>

#include <XboxInternals/Export.h>

using std::string;

class XBOXINTERNALS_EXPORT XdbfHelpers
{
public:

    static string EntryTypeToString(EntryType type);

    static string SettingEntryTypeToString(SettingEntryType type);

    static bool CompareEntries(XdbfEntry a, XdbfEntry b);

    static time_t FILETIMEtoTimeT(FileTime time);

    static unsigned int FILETIMEtoMilliseconds(FileTime time);

    static FileTime TimeTtoFILETIME(time_t time);

    static FileTime TimeTtoFILETIME(time_t time, unsigned int milliseconds);

    static string AchievementTypeToString(AchievementFlags type);

    static string RegionIDtoString(XboxLiveCountry id);

    static string AssetSubcategoryToString(AssetSubcategory category);

    static string AssetGenderToString(AssetGender gender);

    static string IDtoString(u64 id);

    static string GetAchievementState(AchievementEntry *entry);
};


