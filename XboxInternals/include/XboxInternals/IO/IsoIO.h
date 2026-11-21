#pragma once

#include <string>
#include <XboxInternals/IO/BaseIO.h>
#include <XboxInternals/Disc/Gdfx.h>
#include <XboxInternals/Export.h>

// Forward declaration
namespace XboxInternals::Iso {
    class IsoImage;
}

class XBOXINTERNALS_EXPORT IsoIO : public BaseIO
{
public:
    IsoIO(BaseIO *isoIO, GdfxFileEntry *entry, XboxInternals::Iso::IsoImage *iso);
    IsoIO(BaseIO *isoIO, std::string isoFilePath, XboxInternals::Iso::IsoImage *iso);

    void ReadBytes(u8 *outBuffer, u32 len) override;
    void WriteBytes(u8 *buffer, u32 len) override;

    u64 GetPosition() override;
    void SetPosition(u64 position, std::ios_base::seekdir dir = std::ios_base::beg) override;

    u64 Length() override;

    void Flush() override;
    void Close() override;

private:
    BaseIO *isoIO;
    GdfxFileEntry *entry;
    XboxInternals::Iso::IsoImage *iso;
    u64 virtualPosition;
};
