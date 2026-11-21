#ifndef SVODIO_H
#define SVODIO_H

#include "../Disc/Gdfx.h"
#include <XboxInternals/IO/BaseIO.h>
#include <XboxInternals/IO/IndexableMultiFileIO.h>
#include "../Stfs/XContentHeader.h"
#include <XboxInternals/Export.h>

class XBOXINTERNALS_EXPORT SvodIO : public BaseIO
{
public:
    SvodIO(XContentHeader *metadata, GdfxFileEntry entry, IndexableMultiFileIO *io);

    ~SvodIO() override;

    void ReadBytes(u8 *outBuffer, u32 len) override;

    void WriteBytes(u8 *buffer, u32 len) override;

    void SaveFile(string savePath, void(*progress)(void*, u32, u32) = nullptr, void *arg = nullptr);

    void OverWriteFile(string inPath, void (*progress)(void*, u32, u32) = nullptr, void *arg = nullptr);

    void SetPosition(u64 address, std::ios_base::seekdir dir = std::ios_base::beg) override;

    u64 GetPosition() override;

    u64 Length() override;

    void Flush() override;

    void Close() override;

private:
    void SectorToAddress(u32 sector, u32 *addressInDataFile, u32 *dataFileIndex);

    IndexableMultiFileIO *io;
    XContentHeader *metadata;
    GdfxFileEntry fileEntry;
    u64 pos;
    u32 offset;
};

#endif // SVODIO_H


