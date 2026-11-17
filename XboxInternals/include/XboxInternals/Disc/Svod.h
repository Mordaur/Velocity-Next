#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include <XboxInternals/Export.h>
#include <XboxInternals/Disc/Gdfx.h>
#include <XboxInternals/IO/SvodIO.h>
#include <XboxInternals/Stfs/IXContentHeader.h>

class BaseIO;
class FatxDrive;
class IndexableMultiFileIO;

class XBOXINTERNALS_EXPORT SVOD : public IXContentHeader {
public:
    static std::vector<std::string> GetDataFilePaths(const std::string &rootDescriptorPath);

    SVOD(std::string rootFile, FatxDrive *drive = nullptr, bool readFileListing = true);
    ~SVOD();

    void Resign(std::string kvPath);

    SvodIO GetSvodIO(GdfxFileEntry entry);
    SvodIO GetSvodIO(std::string path);

    void SectorToAddress(DWORD sector, DWORD *addressInDataFile, DWORD *dataFileIndex);
    void Rehash(void (*progress)(DWORD, DWORD, void*) = nullptr, void *arg = nullptr);
    void WriteFileEntry(GdfxFileEntry *entry);
    DWORD GetSectorCount();

    // Reads the GDFX file listing into root; does nothing if it was already read.
    void GetFileListing();

    std::vector<GdfxFileEntry> root;

private:
    void ReadFileListing(std::vector<GdfxFileEntry> *entryList, DWORD sector, UINT64 size,
                         std::string path, int depth, std::unordered_set<uint64_t> &visited);
    GdfxFileEntry GetFileEntry(std::string path, std::vector<GdfxFileEntry> *listing);
    void ValidateDataFiles();
    void HashBlock(BYTE *block, BYTE *outHash);
    std::string GetContentName();

    std::string contentDirectory;
    std::unique_ptr<IndexableMultiFileIO> io;
    std::unique_ptr<BaseIO> rootFile;
    std::unique_ptr<XContentHeader> metaDataOwner;
    GdfxHeader header;
    DWORD baseAddress;
    DWORD offset;
    FatxDrive *drive;
    bool didReadFileListing;
};
