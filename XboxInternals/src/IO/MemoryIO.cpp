#include <XboxInternals/IO/MemoryIO.h>

MemoryIO::MemoryIO(u8 *data, size_t length) :
    BaseIO(), memory(data), length(length), pos(0)
{
    SetPosition(0);
}

MemoryIO::~MemoryIO()
{
    // Destructors must not throw; no seek is needed on teardown.
}

void MemoryIO::SetPosition(u64 pos, std::ios_base::seekdir dir)
{
    u64 newPos;
    switch (dir)
    {
        case std::ios_base::beg:
            newPos = pos;
            break;
        case std::ios_base::cur:
            newPos = this->pos + pos;
            break;
        case std::ios_base::end:
            newPos = length + pos;
            break;
        default:
            throw std::string("MemoryIO: Unsupported seek direction\n");
    }

    if (newPos > length)
        throw std::string("MemoryIO: Cannot seek beyond the end of the stream\n");
    this->pos = newPos;
}

u64 MemoryIO::GetPosition()
{
    return pos;
}

u64 MemoryIO::Length()
{
    return length;
}

void MemoryIO::ReadBytes(u8 *outBuffer, u32 len)
{
    if (pos + len > length)
        throw std::string("MemoryIO: Cannot read beyond the end of the stream\n");
    memcpy(outBuffer, memory + pos, len);
    pos += len;
}

void MemoryIO::WriteBytes(u8 *buffer, u32 len)
{
    if (pos + len > length)
        throw std::string("MemoryIO: Cannot write beyond the end of the stream\n");
    memcpy(memory + pos, buffer, len);
    pos += len;
}

void MemoryIO::Close()
{

}

void MemoryIO::Flush()
{

}



