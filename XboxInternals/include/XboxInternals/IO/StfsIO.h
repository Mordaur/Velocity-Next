#pragma once

#include <XboxInternals/IO/BaseIO.h>
#include <XboxInternals/Stfs/StfsPackage.h>

class XBOXINTERNALS_EXPORT StfsIO : public BaseIO
{
public:
    StfsIO(BaseIO *io, StfsPackage *package, StfsFileEntry entry);
    ~StfsIO() override;

    StfsFileEntry GetStfsFileEntry();

    void SetPosition(u64 position, std::ios_base::seekdir dir = std::ios_base::beg) override;
    u64 GetPosition() override;
    u64 Length() override;

    void ReadBytes(u8 *outBuffer, u32 len) override;
    void WriteBytes(u8 *buffer, u32 len) override;

    void Flush() override;
    void Close() override;

    void Resize(u64 size);

private:
    BaseIO *io;
    StfsPackage *package;
    StfsFileEntry entry;

    u64 entryPosition;
    bool didChangeSize;
    u32 blocksAllocated;
};
