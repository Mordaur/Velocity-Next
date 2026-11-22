#pragma once

#include "XboxInternals/Iso/IsoDefinitions.h"
#include "XboxInternals/Disc/Gdfx.h"
#include "XboxInternals/IO/BaseIO.h"
#include "XboxInternals/Export.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_set>

class IsoIO;

namespace XboxInternals::Iso {

class XBOXINTERNALS_EXPORT IsoImage {
public:
    IsoImage();
    ~IsoImage();

    bool open(const std::string& path);
    // Open an image through an existing IO. The image takes ownership; the
    // caller must keep whatever backs the IO alive for the image's lifetime.
    bool open(std::unique_ptr<BaseIO> io);
    void close();

    const IsoInfo& info() const noexcept { return info_; }

    std::vector<GdfxFileEntry> root;

    void GetFileListing();
    GdfxFileEntry* GetFileEntry(std::string filePath);

    IsoIO* GetIO(std::string filePath);
    IsoIO* GetIO(GdfxFileEntry *entry);

    u64 SectorToAddress(u32 sector);

    std::string GetXGDVersion();

    u64 GetTotalSectors();

    std::wstring GetTitleName();

    bool extractFile(const GdfxFileEntry& entry, const std::string& outputDir,
                     void(*progress)(void*, uint32_t, uint32_t) = nullptr,
                     void *arg = nullptr);

    bool extractFile(const IsoEntry& entry, const std::string& outputDir,
                     void(*progress)(void*, uint32_t, uint32_t) = nullptr,
                     void *arg = nullptr);

    bool extractAll(const std::string& outputDir,
                    void(*progress)(void*, uint32_t, uint32_t) = nullptr,
                    void *arg = nullptr);

    std::vector<IsoEntry> listEntries() const;

    static bool createFromDirectory(const std::string& inputDir, const std::string& outputIso);
    static bool createFromIso(const std::string& inputIso, const std::string& outputIso, bool scrub = false);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    IsoInfo info_;
    bool didReadFileListing;
    GdfxHeader gdfxHeader;
    u64 gdfxHeaderAddress;
    u64 volumeBaseSector = 0;
    std::wstring titleName;
    std::string xgdVersion;

    void ParseISO();
    bool ValidGDFXHeader(u64 address);
    void ReadFileListing(std::vector<GdfxFileEntry> *entryList, u32 sector, u64 size,
                         std::string path, int depth, std::unordered_set<uint64_t> &visited);
    // A directory/file extent must lie inside the image (start sector + size).
    bool ValidateExtent(u32 sector, u64 size) const;
    u32 GetFileMagic(GdfxFileEntry *entry);
    u32 GetTotalCopyIterations(const std::vector<GdfxFileEntry> *entryList);
    void ExtractAllHelper(std::string outDirectory, std::vector<GdfxFileEntry> *entryList,
                         void(*progress)(void*, uint32_t, uint32_t), void *arg,
                         u32 *curProgress, u32 totalProgress);
    void ExtractFileHelper(std::string outDirectory, const GdfxFileEntry *toExtract,
                          void(*progress)(void*, uint32_t, uint32_t), void *arg,
                          u32 *curProgress, u32 totalProgress);
};

} // namespace XboxInternals::Iso

namespace XboxInternals::Disc {
using ISO = XboxInternals::Iso::IsoImage;
}
