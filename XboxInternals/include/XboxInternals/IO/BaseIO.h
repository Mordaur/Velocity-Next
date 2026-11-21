#ifndef BASEIO_H
#define BASEIO_H

#include <iostream>
#include <XboxInternals/Types.h>
#include <XboxInternals/EnumTraits.h>

#include <XboxInternals/Export.h>

using std::string;
using std::wstring;

enum EndianType
{
    BigEndian,
    LittleEndian,
    Default
};

class XBOXINTERNALS_EXPORT BaseIO
{
public:
    BaseIO();
    virtual ~BaseIO();

    // set the byte order in which to read the bytes
    void SetEndian(EndianType byteOrder);

    // swap the endianess
    void SwapEndian();

    // seek to a position in a file
    virtual void SetPosition(u64 position, std::ios_base::seekdir dir = std::ios_base::beg) = 0;

    // get current address in the file
    virtual u64 GetPosition() = 0;

    // get length of the file
    virtual u64 Length() = 0;

    // get the byte order in which to read the bytes
    EndianType GetEndian();

    // read len bytes from the current file at the current position into buffer
    virtual void ReadBytes(u8 *outBuffer, u32 len) = 0;

    // Write len bytes from the current file at the current position into buffer
    virtual void WriteBytes(u8 *buffer, u32 len) = 0;

    // all the read functions
    u8 ReadByte();
    i16 ReadInt16();
    u16 ReadWord();
    // 24-bit fields (STFS block numbers, SVOD offsets); always 0..0xFFFFFF
    u32 ReadInt24(EndianType et = Default);
    i32 ReadInt32();
    u32 ReadDword();
    i64 ReadInt64();
    u64 ReadUInt64();
    u64 ReadMultiByte(size_t size);

    // Read `width` bytes (1, 2, 4 or 8) and convert them through the enum's
    // EnumTraits specialization. Every enum read from a file goes through this,
    // so an out-of-range value is mapped to the enum's Unknown sentinel (or
    // rejected by the caller) instead of being cast blindly.
    template <typename E>
    E ReadEnum(int width)
    {
        return EnumTraits<E>::FromRaw(ReadMultiByte(static_cast<size_t>(width)));
    }

    float ReadFloat();
    double ReadDouble();
    string ReadString(int len = -1, char nullTerminator = 0, bool forceInclude0 = true,
            int maxLength = 0x7FFFFFFF);
    wstring ReadWString(int len = -1);

    // Write functions
    void Write(u8 b);
    void Write(u16 w);
    void Write(u32 dw);
    void Write(u64 qw);
    // Writes the low 24 bits of value (3 bytes)
    void WriteInt24(u32 value, EndianType et = Default);
    void Write(string s, int forceLen = -1, bool nullTerminating = true, u8 nullTerminator = 0);
    void Write(wstring ws, bool nullTerminating = true);
    void Write(u8 *buffer, u32 len);

    // flush the io's buffer
    virtual void Flush() = 0;

    // close the io
    virtual void Close() = 0;

protected:
    EndianType byteOrder;

private:
    void reverseByteArray(u8 *array, u32 len);
};

#endif //BASEIO_H


