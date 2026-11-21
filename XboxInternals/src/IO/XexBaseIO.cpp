#include <XboxInternals/IO/XexBaseIO.h>
#include <XboxInternals/Xex/Xex.h>


XexBaseIO::XexBaseIO(BaseIO *io, Xex *xex) :
    io(io), xex(xex), position(0)
{

}

void XexBaseIO::SetPosition(u64 position, std::ios_base::seekdir dir)
{
    if (position != 0 || dir != std::ios_base::beg)
        throw std::string("XexBaseIO: Can only seek to the beginning.");

    xex->io->SetPosition(xex->header.dataAddress);
    this->position = 0;
}

u64 XexBaseIO::GetPosition()
{
    return position;
}

void XexBaseIO::Flush()
{
    xex->io->Flush();
}

void XexBaseIO::Close()
{
    // don't want to close the XEX's IO so don't do anything
}
