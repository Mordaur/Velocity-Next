#include <XboxInternals/Fatx/XContentDeviceProfile.h>
#include <XboxInternals/Utils.h>
#include <XboxInternals/Stfs/StfsPackage.h>

XContentDeviceProfile::XContentDeviceProfile() :
    XContentDeviceItem()
{

}

XContentDeviceProfile::XContentDeviceProfile(FatxFileEntry *fileEntry, std::shared_ptr<IXContentHeader> profile) :
    XContentDeviceItem(fileEntry, std::move(profile))
{
}

XContentDeviceProfile::XContentDeviceProfile(std::string pathOnDevice, std::string rawName, std::shared_ptr<IXContentHeader> profile, u32 fileSize) :
    XContentDeviceItem(pathOnDevice, rawName, std::move(profile), fileSize)
{
}

u8 *XContentDeviceProfile::GetProfileID()
{
    if (content)
    {
        return content->metaData->profileID;
    }
    else
    {
        if (titles.size() != 0 && titles.at(0).titleSaves.size() != 0)
            return titles.at(0).titleSaves.at(0).content->metaData->profileID;
        else
            return nullptr;
    }
}

std::wstring XContentDeviceProfile::GetName()
{
    // check to see if the gamertag is cached
    if (gamertag.size() == 0)
    {
        ConsoleType consoleType;
        if (content)
            consoleType = content->metaData->certificate.consoleTypeFlags ? DevKit : Retail;
        else
            return L"Unknown Profile";

        try
        {
            auto *stfsPackage = dynamic_cast<StfsPackage*>(content.get());
            if (stfsPackage == nullptr)
                return L"Unknown Profile";

            std::string accountTemp = Utils::GetTemporaryFileName();
            try
            {
                stfsPackage->ExtractFile("Account", accountTemp);
                Account account(accountTemp, true, consoleType);
                gamertag = account.GetGamertag();
            }
            catch (...)
            {
                Utils::DeleteLocalFile(accountTemp);
                throw;
            }

            Utils::DeleteLocalFile(accountTemp);
        }
        catch (...)
        {
            return L"Unknown Profile";
        }
    }

    return gamertag;
}
