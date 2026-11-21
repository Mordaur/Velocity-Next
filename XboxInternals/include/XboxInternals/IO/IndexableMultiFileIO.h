#pragma once

#include <ios>
#include <memory>
#include <string>
#include <vector>

#include <XboxInternals/Export.h>
#include <XboxInternals/Types.h>
#include <XboxInternals/IO/BaseIO.h>

// Treats a directory of files as a single seekable data source with index support.
class XBOXINTERNALS_EXPORT IndexableMultiFileIO : public BaseIO {
public:
    IndexableMultiFileIO();
    ~IndexableMultiFileIO() override;

    // Seek to a specific address within the logical file. Pass -1 for fileIndex to reuse the current file.
    void SetPosition(u32 addressInFile, int fileIndex = -1);

    // Retrieve the current logical position and file index.
    void GetPosition(u32 *addressInFile, u32 *fileIndex);

    void ReadBytes(u8 *outBuffer, u32 len) override;
    void WriteBytes(u8 *buffer, u32 len) override;

    void Close() override;
    void Flush() override;

    u64 Length() override;

    u32 FileCount();
    u32 CurrentFileLength();
    const std::vector<std::string>& FilePaths() const { return files; }

    // Unused BaseIO overrides required by the interface.
    void SetPosition(u64 position, std::ios_base::seekdir dir = std::ios_base::beg) override;
    u64 GetPosition() override;

protected:
    u32 addressInFile;
    u32 fileIndex;

    std::unique_ptr<BaseIO> currentIO;
    std::vector<std::string> files;

    virtual void loadDirectories(std::string path) = 0;
    virtual std::unique_ptr<BaseIO> openFile(std::string path) = 0;
};
