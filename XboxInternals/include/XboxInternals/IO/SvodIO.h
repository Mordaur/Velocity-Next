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

    void ReadBytes(BYTE *outBuffer, DWORD len) override;

    void WriteBytes(BYTE *buffer, DWORD len) override;

    void SaveFile(string savePath, void(*progress)(void*, DWORD, DWORD) = nullptr, void *arg = nullptr);

    void OverWriteFile(string inPath, void (*progress)(void*, DWORD, DWORD) = nullptr, void *arg = nullptr);

    void SetPosition(UINT64 address, std::ios_base::seekdir dir = std::ios_base::beg) override;

    UINT64 GetPosition() override;

    UINT64 Length() override;

    void Flush() override;

    void Close() override;

private:
    void SectorToAddress(DWORD sector, DWORD *addressInDataFile, DWORD *dataFileIndex);

    IndexableMultiFileIO *io;
    XContentHeader *metadata;
    GdfxFileEntry fileEntry;
    UINT64 pos;
    DWORD offset;
};

#endif // SVODIO_H


