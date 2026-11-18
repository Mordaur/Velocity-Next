#ifndef BASEIO_H
#define BASEIO_H

#include <iostream>
#include <XboxInternals/TypeDefinitions.h>

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
    virtual void SetPosition(UINT64 position, std::ios_base::seekdir dir = std::ios_base::beg) = 0;

    // get current address in the file
    virtual UINT64 GetPosition() = 0;

    // get length of the file
    virtual UINT64 Length() = 0;

    // get the byte order in which to read the bytes
    EndianType GetEndian();

    // read len bytes from the current file at the current position into buffer
    virtual void ReadBytes(BYTE *outBuffer, DWORD len) = 0;

    // Write len bytes from the current file at the current position into buffer
    virtual void WriteBytes(BYTE *buffer, DWORD len) = 0;

    // all the read functions
    BYTE ReadByte();
    INT16 ReadInt16();
    WORD ReadWord();
    INT24 ReadInt24(EndianType et = Default);
    INT32 ReadInt32();
    DWORD ReadDword();
    INT64 ReadInt64();
    UINT64 ReadUInt64();
    UINT64 ReadMultiByte(size_t size);

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
    void Write(BYTE b);
    void Write(WORD w);
    void Write(INT24 i24, EndianType et = Default);
    void Write(DWORD dw);
    void Write(UINT64 u64);
    void Write(string s, int forceLen = -1, bool nullTerminating = true, BYTE nullTerminator = 0);
    void Write(wstring ws, bool nullTerminating = true);
    void Write(BYTE *buffer, DWORD len);

    // flush the io's buffer
    virtual void Flush() = 0;

    // close the io
    virtual void Close() = 0;

protected:
    EndianType byteOrder;

private:
    void reverseByteArray(BYTE *array, DWORD len);
};

#endif //BASEIO_H


