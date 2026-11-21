#ifndef GDFX_H
#define GDFX_H

#include <XboxInternals/Types.h>
#include <XboxInternals/IO/BaseIO.h>
#include <XboxInternals/Gpd/XdbfDefinitions.h>
#include <iostream>
#include <vector>

using std::string;
using std::vector;

struct GdfxHeader
{
    u8 magic[0x14];   // MICROSOFT*XBOX*MEDIA
    u32 rootSector;
    u32 rootSize;
    FileTime creationTime;
};

struct GdfxFileEntry
{
    // in the file
    u32 unknown;
    u32 sector;
    u32 size;
    u8 attributes;
    u8 nameLen;
    string name;

    // extra stuff
    string filePath;
    vector<GdfxFileEntry> files;
    u32 address;
    u32 fileIndex;
    u32 magic;  // file type identifier (first 4 bytes)
};

enum GdfxDirentAttributesutes
{
    GdfxReadOnly = 0x01,
    GdfxHidden = 0x02,
    GdfxSystem = 0x04,
    GdfxDirectory = 0x10,
    GdfxArchive = 0x20,
    GdfxDevice = 0x40,
    GdfxNormal = 0x80
};

// Read the GDFX header, seek io to position of header beforehand
void GdfxReadHeader(BaseIO *io, GdfxHeader *header);

// Read the next file entry in the listing, returns false on listing end
bool GdfxReadFileEntry(BaseIO *io, GdfxFileEntry *entry);

// Write a file entry back to the listing
void GdfxWriteFileEntry(BaseIO *io, GdfxFileEntry *entry);

// Order entries so directories come first
int DirectoryFirstCompareGdfxEntries(const GdfxFileEntry &a, const GdfxFileEntry &b);

#endif // GDFX_H


