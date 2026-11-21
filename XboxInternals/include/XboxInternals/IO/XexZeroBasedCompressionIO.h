#ifndef XEXZEROBASEDCOMPRESSIONIO_H
#define XEXZEROBASEDCOMPRESSIONIO_H

#include <XboxInternals/IO/BaseIO.h>
#include <XboxInternals/IO/XexBaseIO.h>

class Xex;

#define XEX_NULL_BUFFER_SIZE 	0x10000

class XexZeroBasedCompressionIO : public XexBaseIO
{
public:
    XexZeroBasedCompressionIO(BaseIO *io, Xex *xex);

    void ReadBytes(u8 *outBuffer, u32 readLength);

    void WriteBytes(u8 *buffer, u32 len);

    u64 Length();
};

#endif // XEXZEROBASEDCOMPRESSIONIO_H
