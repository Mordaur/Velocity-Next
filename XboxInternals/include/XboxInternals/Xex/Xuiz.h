#ifndef XUIZ_H
#define XUIZ_H

#include <XboxInternals/IO/BaseIO.h>
#include <XboxInternals/IO/FileIO.h>
#include <XboxInternals/Types.h>
#include <XboxInternals/Export.h>

#include <memory>
#include <string>
#include <vector>

#define XUIZ_MAGIC              0x5855495A    // 'XUIZ'
#define XUIZ_HEADER_SIZE        0x16
#define XUIZ_COPY_BUFFER_SIZE   0x100000

struct XuizFileEntry
{
    u32 size;
    u32 offset;
    u32 realAddress;
    std::string path;
};

class XBOXINTERNALS_EXPORT Xuiz
{
public:
    Xuiz(std::string filePath);
    Xuiz(BaseIO *io);
    ~Xuiz();

    std::vector<XuizFileEntry> GetFiles() const;

    void ExtractFile(std::string outFile, XuizFileEntry *fileEntry) const;

    void ExtractFile(std::string outFile, std::string xuizFilePath) const;

    XuizFileEntry *GetFileEntry(std::string xuizFilePath) const;

private:
    BaseIO *io;
    std::unique_ptr<BaseIO> ownedIO;
    std::vector<XuizFileEntry> files;

    void Parse();
};

#endif // XUIZ_H
