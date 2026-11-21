#ifndef XEXBASEIO_H
#define XEXBASEIO_H

#include <XboxInternals/IO/BaseIO.h>

#include <ios>

class Xex;

#define XEX_NULL_BUFFER_SIZE 	0x10000

// Abstract class containing the operations common to all XexIOs
class XexBaseIO : public BaseIO
{
public:
    XexBaseIO(BaseIO *io, Xex *xex);

    void SetPosition(u64 position, std::ios_base::seekdir dir);

    u64 GetPosition();

    void Flush();

    void Close();

protected:
    BaseIO *io;
    Xex *xex;
    u64 position;
};

#endif // XEXBASEIO_H
