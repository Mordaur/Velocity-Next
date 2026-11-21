#ifndef MEMORYSTREAM_H
#define MEMORYSTREAM_H

#include <XboxInternals/Types.h>
#include <string.h>
#include <XboxInternals/IO/BaseIO.h>

class XBOXINTERNALS_EXPORT MemoryIO : public BaseIO
{
public:
    MemoryIO(u8 *data, size_t length);
    virtual ~MemoryIO();

    void SetPosition(u64 pos, std::ios_base::seekdir dir = std::ios_base::beg);
    u64 GetPosition();
    u64 Length();

    void ReadBytes(u8 *outBuffer, u32 len);
    void WriteBytes(u8 *buffer, u32 len);

    void Close();
    void Flush();

private:
    u8 *memory;
    size_t length;

    u64 pos;
};

#endif // MEMORYSTREAM_H


