#include <XboxInternals/Disc/Gdfx.h>
#include <cstring>

void GdfxReadHeader(BaseIO *io, GdfxHeader *header)
{
    // make sure that the byte order is little endian
    io->SetEndian(LittleEndian);

    // verify the magic
    io->ReadBytes((BYTE*)header->magic, 0x14);
    if (memcmp(header->magic, "MICROSOFT*XBOX*MEDIA", 0x14) != 0)
        throw string("GDFX: Invalid header magic\n");

    // read the rest of the header
    header->rootSector = io->ReadDword();
    header->rootSize = io->ReadDword();
    header->creationTime.dwLowDateTime = io->ReadDword();
    header->creationTime.dwHighDateTime = io->ReadDword();
}

bool GdfxReadFileEntry(BaseIO *io, GdfxFileEntry *entry)
{
    // everything's little endian, so let's be safe
    io->SetEndian(LittleEndian);

    // check to see if we're at the end
    DWORD nextBytes = io->ReadDword();
    if (nextBytes == 0xFFFFFFFF)
        return false;

    // parse the entry
    entry->unknown = nextBytes;
    entry->sector = io->ReadDword();
    entry->size = io->ReadDword();
    entry->attributes = io->ReadByte();
    entry->nameLen = io->ReadByte();

    // GDF names are opaque bytes; ReadString would truncate at an embedded NUL.
    entry->name.resize(entry->nameLen);
    if (entry->nameLen != 0)
        io->ReadBytes(reinterpret_cast<BYTE *>(&entry->name[0]), entry->nameLen);

    return true;
}

void GdfxWriteFileEntry(BaseIO *io, GdfxFileEntry *entry)
{
    // everything's little endian, so let's be safe
    io->SetEndian(LittleEndian);

    // Write the entry
    io->Write(entry->unknown);
    io->Write(entry->sector);
    io->Write(entry->size);
    io->Write(entry->attributes);
    io->Write(entry->nameLen);
    io->Write(entry->name);
}

int DirectoryFirstCompareGdfxEntries(const GdfxFileEntry &a, const GdfxFileEntry &b)
{
    // Compare so directories come first
    int a_directory = a.attributes & GdfxDirectory;
    int b_directory = b.attributes & GdfxDirectory;
    
    return a_directory > b_directory;
}


