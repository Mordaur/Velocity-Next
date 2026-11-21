#pragma once

#include <set>
#include <string>
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

    void SectorToAddress(u32 sector, u32 *addressInDataFile, u32 *dataFileIndex);
    void Rehash(void (*progress)(u32, u32, void*) = nullptr, void *arg = nullptr);
    void WriteFileEntry(GdfxFileEntry *entry);
    u32 GetSectorCount();

    // Verify part 0's master block against the descriptor root hash.
    bool VerifyRoot();
    // Verify every data block, level-0 table and the master chain. Reads the
    // whole volume; intended as an integrity check, not a per-read guard.
    bool Verify();

    // Reads the GDFX file listing into root; does nothing if it was already read.
    void GetFileListing();

    std::vector<GdfxFileEntry> root;

private:
    void ReadFileListing(std::vector<GdfxFileEntry> *entryList, u32 sector, u64 size,
                         std::string path, int depth,
                         std::set<std::pair<u32, u64>> &visited);
    GdfxFileEntry GetFileEntry(std::string path, std::vector<GdfxFileEntry> *listing);
    void ValidateDataFiles();
    void HashBlock(u8 *block, u8 *outHash);
    std::string GetContentName();

    std::string contentDirectory;
    std::unique_ptr<IndexableMultiFileIO> io;
    std::unique_ptr<BaseIO> rootFile;
    std::unique_ptr<XContentHeader> metaDataOwner;
    GdfxHeader header;
    u32 baseAddress;
    u32 offset;
    FatxDrive *drive;
    bool didReadFileListing;
};
