#include <XboxInternals/IO/IsoIO.h>
#include <XboxInternals/Disc/ISO.h>

IsoIO::IsoIO(BaseIO *isoIO, GdfxFileEntry *entry, XboxInternals::Iso::IsoImage *iso) :
    isoIO(isoIO), entry(entry), iso(iso), virtualPosition(0)
{
    SetPosition(0);
}

IsoIO::IsoIO(BaseIO *isoIO, std::string isoFilePath, XboxInternals::Iso::IsoImage *iso) :
    isoIO(isoIO), iso(iso), virtualPosition(0)
{
    entry = iso->GetFileEntry(isoFilePath);
    SetPosition(0);
}

void IsoIO::ReadBytes(u8 *outBuffer, u32 len)
{
    SetPosition(GetPosition());

    // check for end of file
    if (GetPosition() + len > Length())
        throw std::string("IsoIO: Cannot read beyond end of file.");

    isoIO->ReadBytes(outBuffer, len);
    virtualPosition += len;
}

void IsoIO::WriteBytes(u8 *buffer, u32 len)
{
    SetPosition(GetPosition());

    // check for end of file
    if (GetPosition() + len > Length())
        throw std::string("IsoIO: Cannot write beyond end of file.");

    isoIO->WriteBytes(buffer, len);
    virtualPosition += len;
}

u64 IsoIO::GetPosition()
{
    return virtualPosition;
}

void IsoIO::SetPosition(u64 position, std::ios_base::seekdir dir)
{
    u64 newVirtualPosition = position;
    switch (dir)
    {
        case std::ios_base::beg:
            break;
        case std::ios_base::cur:
            newVirtualPosition += GetPosition();
            break;
        case std::ios_base::end:
            newVirtualPosition += Length();
            break;
        default:
            throw std::string("IsoIO: Unsupported seek direction");
    }

    if (newVirtualPosition > Length())
        throw std::string("IsoIO: Cannot seek beyond end of file.");

    virtualPosition = newVirtualPosition;

    // calculate the real position
    u64 realPosition = iso->SectorToAddress(entry->sector) + newVirtualPosition;
    isoIO->SetPosition(realPosition);
}

u64 IsoIO::Length()
{
    return entry->size;
}

void IsoIO::Flush()
{
    isoIO->Flush();
}

void IsoIO::Close()
{
    // Don't close the underlying ISO file
}
