#include <XboxInternals/Gpd/GpdBase.h>
#include <utility>
#include <bit>


GpdBase::GpdBase(shared_ptr<FileIO> io) : io(std::move(io))
{
    xdbf = std::make_unique<Xdbf>(this->io);
    init();
}

GpdBase::GpdBase(string path) : GpdBase(std::make_shared<FileIO>(path))
{
}

void GpdBase::init()
{
    // read all the images
    for (u32 i = 0; i < xdbf->images.size(); i++)
    {
        // set up the information
        ImageEntry image;
        image.entry = xdbf->images.at(i);
        image.initialLength = image.entry.length;
        image.length = image.entry.length;
        image.image.resize(image.length);

        // read in the image
        if (image.length > 0)
        {
            io->SetPosition(xdbf->GetRealAddress(image.entry.addressSpecifier));
            io->ReadBytes(image.image.data(), image.length);
        }

        image.length = static_cast<u32>(image.image.size());

        // add the image to the vector
        images.push_back(image);
    }

    // read all the settings
    for (u32 i = 0; i < xdbf->settings.entries.size(); i++)
        settings.push_back(readSettingEntry(xdbf->settings.entries.at(i)));

    // read all the strings
    for (u32 i = 0; i < xdbf->strings.size(); i++)
    {
        // read in the string entry
        StringEntry entry;
        entry.entry = xdbf->strings.at(i);
        entry.ws = readStringEntry(entry.entry);
        entry.initialLength = entry.ws.length();

        // add the entry to the vector
        strings.push_back(entry);
    }
}

wstring GpdBase::readStringEntry(XdbfEntry entry)
{
    // ensure that the entry is a string entry
    if (entry.type != String)
        throw string("Xdbf: Error reading string entry. Specified entry isn't a string.\n");

    // seek to the entry's position
    io->SetPosition(xdbf->GetRealAddress(entry.addressSpecifier));

    // read the string
    return io->ReadWString(entry.length / 2);
}

SettingEntry GpdBase::readSettingEntry(XdbfEntry entry)
{
    // ensure the entry is a setting entry
    if (entry.type != Setting)
        throw string("Xdbf: Error reading setting entry. The entry specified isn't a setting.\n");

    SettingEntry toReturn;
    toReturn.entry = entry;

    // seek to the position of the setting entry, skip past the id
    u32 entryAddr = xdbf->GetRealAddress(entry.addressSpecifier);
    io->SetPosition(entryAddr + 8);

    // read the setting entry type
    toReturn.type = io->ReadEnum<SettingEntryType>(1);
    if (toReturn.type == SettingEntryTypeUnknown)
        throw string("Xdbf: Error reading setting entry. Invalid setting entry type.\n");

    // skip past the nonsense
    io->SetPosition(entryAddr + 0x10);

    switch (toReturn.type)
    {
            default: break;
        case Context:
            io->SetPosition(entryAddr);

            toReturn.binaryData.resize(entry.length);
            io->ReadBytes(toReturn.binaryData.data(), entry.length);
            break;
        case Int32:
            toReturn.int32 = io->ReadInt32();
            break;
        case Int64:
            toReturn.int64 = io->ReadInt64();
            break;
        case Double:
        {
            toReturn.doubleData = io->ReadDouble();
            break;
        }
        case UnicodeString:
        {
            u32 strLen = io->ReadDword();
            io->SetPosition(entryAddr + 0x18);
            toReturn.str = io->ReadWString(strLen);
            break;
        }
        case Float:
        {
            toReturn.floatData = io->ReadFloat();
            break;
        }
        case Binary:
            toReturn.binaryData.resize(io->ReadDword());
            io->SetPosition(entryAddr + 0x18);
            if (!toReturn.binaryData.empty())
                io->ReadBytes(toReturn.binaryData.data(), static_cast<u32>(toReturn.binaryData.size()));
            break;
        case TimeStamp:
        {
            FileTime time = { io->ReadDword(), io->ReadDword() };
            toReturn.timeStamp = XdbfHelpers::FILETIMEtoTimeT(time);
            break;
        }
    }

    return toReturn;
}

void GpdBase::DeleteSettingEntry(SettingEntry setting)
{
    // remove the entry from the list
    u32 i;
    for (i = 0; i < settings.size(); i++)
    {
        if (settings.at(i).entry.id == setting.entry.id)
        {
            settings.erase(settings.begin() + i);
            break;
        }
    }
    if (i == settings.size())
        throw string("Gpd: Error deleting setting entry. Setting doesn't exist.\n");

    // delete the entry from the file
    xdbf->DeleteEntry(setting.entry);
}

void GpdBase::DeleteImageEntry(ImageEntry image)
{
    // remove the entry from the list
    u32 i;
    for (i = 0 ; i < images.size(); i++)
    {
        if (images.at(i).entry.id == image.entry.id)
        {
            images.erase(images.begin() + i);
            break;
        }
    }
    if (i == images.size())
        throw string("Gpd: Error deleting image entry. Image doesn't exist.\n");

    // delete the entry from the file
    xdbf->DeleteEntry(image.entry);
}

void GpdBase::CreateSettingEntry(SettingEntry *setting, u64 entryID)
{
    u32 entryLen = 0;
    switch (setting->type)
    {
        case Context:
            entryLen = static_cast<u32>(setting->binaryData.size());
            break;
        case Int32:
        case Float:
        case Int64:
        case Double:
        case TimeStamp:
            entryLen = 0x18;
            break;
        case UnicodeString:
            entryLen = 0x18 + ((setting->str.size() + 1) * 2);
            break;
        case Binary:
            entryLen = 0x18 + static_cast<u32>(setting->binaryData.size());
            break;
        default:
            throw string("Gpd: Error creating setting entry. Invalid setting entry type.\n");
    }

    // create the xdbf entry
    setting->entry = xdbf->CreateEntry(Setting, entryID, entryLen);

    // add the new setting entry to the list
    settings.push_back(*setting);

    // Write the setting; the write may reallocate the entry
    WriteSettingEntry(settings.back());
    *setting = settings.back();
}

void GpdBase::CreateImageEntry(ImageEntry *image, u64 entryID)
{
    image->length = static_cast<u32>(image->image.size());
    // create Xdbf entry
    image->initialLength = image->length;
    image->entry = xdbf->CreateEntry(Image, entryID, image->length);

    // add the new entry to the list
    images.push_back(*image);

    // Write the image
    WriteImageEntry(*image);
}

void GpdBase::WriteSettingEntry(SettingEntry &setting)
{
    // get the address of the entry in the file
    u32 entryAddr = xdbf->GetRealAddress(setting.entry.addressSpecifier);

    // Write the setting entry header
    io->SetPosition(entryAddr);
    io->Write((u32)setting.entry.id);
    io->SetPosition(entryAddr + 8);
    io->Write((u8)setting.type);

    io->SetPosition(entryAddr + 0x10);
    io->Flush();

    // Write setting
    switch (setting.type)
    {
            default: break;
        case Context:
        {
            const u32 binaryLength = static_cast<u32>(setting.binaryData.size());
            if (setting.entry.length != binaryLength)
            {
                // adjust the memory if the length changed
                xdbf->DeallocateMemory(xdbf->GetRealAddress(setting.entry.addressSpecifier), setting.entry.length);
                setting.entry.length = binaryLength;
                entryAddr = xdbf->AllocateMemory(setting.entry.length);
                setting.entry.addressSpecifier = xdbf->GetSpecifier(entryAddr);
            }
            io->SetPosition(entryAddr);
            if (!setting.binaryData.empty())
                io->Write(setting.binaryData.data(), binaryLength);
            break;
        }
        case Int32:
            io->Write(static_cast<u32>(setting.int32));
            break;
        case Float:
            io->Write(std::bit_cast<u32>(setting.floatData));
            break;
        case Int64:
            io->Write(static_cast<u64>(setting.int64));
            break;
        case Double:
            io->Write(std::bit_cast<u64>(setting.doubleData));
            break;
        case TimeStamp:
        {
            FileTime time = XdbfHelpers::TimeTtoFILETIME(setting.timeStamp);
            io->Write(time.highDateTime);
            io->Write(time.lowDateTime);
            break;
        }
        case UnicodeString:
        {
            u32 calculatedLength = 0x18 + ((setting.str.size() + 1) * 2);
            if (setting.entry.length != calculatedLength)
            {
                // adjust the memory if the length changed
                xdbf->DeallocateMemory(xdbf->GetRealAddress(setting.entry.addressSpecifier), setting.entry.length);
                setting.entry.length = calculatedLength;
                entryAddr = xdbf->AllocateMemory(setting.entry.length);
                setting.entry.addressSpecifier = xdbf->GetSpecifier(entryAddr);

                io->SetPosition(entryAddr);
                io->Write((u32)setting.entry.id);
                io->Write((u32)0);
                io->Write((u8)setting.type);
                io->SetPosition(entryAddr + 0x10);
            }
            io->Write(static_cast<u32>((setting.str.size() + 1) * 2));
            io->Write((u32)0);
            io->Write(setting.str);
            break;
        }
        case Binary:
        {
            const u32 dataLength = static_cast<u32>(setting.binaryData.size());
            u32 calculatedLength = 0x18 + dataLength;
            if (setting.entry.length != calculatedLength)
            {
                // adjust the memory if the length changed
                xdbf->DeallocateMemory(xdbf->GetRealAddress(setting.entry.addressSpecifier), setting.entry.length);
                setting.entry.length = calculatedLength;
                entryAddr = xdbf->AllocateMemory(setting.entry.length);
                setting.entry.addressSpecifier = xdbf->GetSpecifier(entryAddr);

                io->SetPosition(entryAddr);
                io->Write((u32)setting.entry.id);
                io->SetPosition(entryAddr + 8);
                io->Write((u8)setting.type);
                io->SetPosition(entryAddr + 0x10);
            }
            io->Write(dataLength);
            io->Write((u32)0);
            if (!setting.binaryData.empty())
                io->Write(setting.binaryData.data(), dataLength);
            break;
        }
    }

    if (setting.entry.id != GamercardTitleAchievementsEarned &&
            setting.entry.id != GamercardTitleCredEarned && setting.entry.id != GamercardCred &&
            setting.entry.id != GamercardAchievementsEarned)
        xdbf->UpdateEntry(&setting.entry);

    // Callers may pass a copy; keep the stored entry in step when the write
    // reallocated it, so a later delete does not free the old region twice.
    for (SettingEntry &stored : settings)
        if (&stored != &setting && stored.entry.id == setting.entry.id)
            stored = setting;

    io->Flush();
}

void GpdBase::WriteImageEntry(ImageEntry image)
{
    const u32 dataLength = static_cast<u32>(image.image.size());

    // allocate memory if needed
    if (image.length != image.initialLength || image.length != dataLength)
    {
        xdbf->DeallocateMemory(xdbf->GetRealAddress(image.entry.addressSpecifier), image.entry.length);
        image.entry.length = dataLength;
        image.entry.addressSpecifier = xdbf->GetSpecifier(xdbf->AllocateMemory(image.entry.length));
        image.length = dataLength;
    }

    // Write the image
    io->SetPosition(xdbf->GetRealAddress(image.entry.addressSpecifier));
    if (!image.image.empty())
        io->Write(image.image.data(), dataLength);
}

void GpdBase::Close()
{
    if (io)
        io->Close();
}

void GpdBase::Clean()
{
    xdbf->Clean();
    io = xdbf->io;
}

SettingEntry GpdBase::GetSetting(u64 id)
{
    for (u32 i = 0; i < settings.size(); i++)
        if (settings.at(i).entry.id == id)
            return settings.at(i);
    return SettingEntry();
}


