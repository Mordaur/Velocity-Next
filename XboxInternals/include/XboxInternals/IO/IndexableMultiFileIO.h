#pragma once

#include <ios>
#include <memory>
#include <string>
#include <vector>

#include <XboxInternals/Export.h>
#include <XboxInternals/TypeDefinitions.h>
#include <XboxInternals/IO/BaseIO.h>

// Treats a directory of files as a single seekable data source with index support.
class XBOXINTERNALS_EXPORT IndexableMultiFileIO : public BaseIO {
public:
    IndexableMultiFileIO();
    ~IndexableMultiFileIO() override;

    // Seek to a specific address within the logical file. Pass -1 for fileIndex to reuse the current file.
    void SetPosition(DWORD addressInFile, int fileIndex = -1);

    // Retrieve the current logical position and file index.
    void GetPosition(DWORD *addressInFile, DWORD *fileIndex);

    void ReadBytes(BYTE *outBuffer, DWORD len) override;
    void WriteBytes(BYTE *buffer, DWORD len) override;

    void Close() override;
    void Flush() override;

    UINT64 Length() override;

    DWORD FileCount();
    DWORD CurrentFileLength();
    const std::vector<std::string>& FilePaths() const { return files; }

    // Unused BaseIO overrides required by the interface.
    void SetPosition(UINT64 position, std::ios_base::seekdir dir = std::ios_base::beg) override;
    UINT64 GetPosition() override;

protected:
    DWORD addressInFile;
    DWORD fileIndex;

    std::unique_ptr<BaseIO> currentIO;
    std::vector<std::string> files;

    virtual void loadDirectories(std::string path) = 0;
    virtual std::unique_ptr<BaseIO> openFile(std::string path) = 0;
};
