#include <XboxInternals/IO/BaseIO.h>
#include <vector>
#include <bit>

using namespace std;

BaseIO::BaseIO() :
    byteOrder(BigEndian)
{
    // should be implemented by derived class
}

BaseIO::~BaseIO()
{

}

void BaseIO::SetEndian(EndianType byteOrder)
{
    this->byteOrder = byteOrder;
}

EndianType BaseIO::GetEndian()
{
    return this->byteOrder;
}

void BaseIO::reverseByteArray(u8 *array, u32 len)
{
    u8 temp;
    for (u32 i = 0; i < len / 2; i++)
    {
        temp = array[i];
        array[i] = array[len - i - 1];
        array[len - i - 1] = temp;
    }
}

u8 BaseIO::ReadByte()
{
    u8 toReturn;
    ReadBytes(&toReturn, 1);
    return toReturn;
}

i16 BaseIO::ReadInt16()
{
    return (i16)ReadWord();
}

u16 BaseIO::ReadWord()
{
    u16 toReturn;
    ReadBytes(reinterpret_cast<u8*>(&toReturn), 2);

    if (byteOrder == BigEndian)
        reverseByteArray(reinterpret_cast<u8*>(&toReturn), 2);

    return toReturn;
}

u32 BaseIO::ReadInt24(EndianType et)
{
    EndianType orig = byteOrder;

    if(et != Default)
        byteOrder = et;

    u32 returnVal = ReadDword();

    if(byteOrder == BigEndian)
        returnVal = (returnVal & 0xFFFFFF00) >> 8;
    else
        returnVal = returnVal & 0x00FFFFFF;

    SetPosition(GetPosition() - 1);
    byteOrder = orig;

    return returnVal;
}

i32 BaseIO::ReadInt32()
{
    return (i32)ReadDword();
}

u32 BaseIO::ReadDword()
{
    u32 toReturn;
    ReadBytes(reinterpret_cast<u8*>(&toReturn), 4);

    if (byteOrder == BigEndian)
        reverseByteArray(reinterpret_cast<u8*>(&toReturn), 4);

    return toReturn;
}

i64 BaseIO::ReadInt64()
{
    return (i64)ReadUInt64();
}

u64 BaseIO::ReadUInt64()
{
    u64 toReturn;
    ReadBytes(reinterpret_cast<u8*>(&toReturn), 8);

    if (byteOrder == BigEndian)
        reverseByteArray(reinterpret_cast<u8*>(&toReturn), 8);

    return toReturn;
}

u64 BaseIO::ReadMultiByte(size_t size)
{
    switch (size)
    {
        case 1:
            return ReadByte();
        case 2:
            return ReadWord();
        case 4:
            return ReadDword();
        case 8:
            return ReadUInt64();
        default:
            throw string("BaseIO: Invalid multi-byte size.\n");
    }
}

float BaseIO::ReadFloat()
{
    // ReadDword/UInt64 already apply the file byte order; reinterpret the bits,
    // do not convert the integer value.
    return std::bit_cast<float>(ReadDword());
}

double BaseIO::ReadDouble()
{
    return std::bit_cast<double>(ReadUInt64());
}

string BaseIO::ReadString(int len, char nullTerminator, bool forceInclude0, int maxLength)
{
    string toReturn;

    // assume it's null terminating
    if (len == -1)
    {
        toReturn = "";
        int i = 1;
        char nextChar;
        while ((nextChar = ReadByte()) != nullTerminator && (forceInclude0 && nextChar != 0) &&
                (i++ <= maxLength))
            toReturn += nextChar;
    }
    else
    {
        toReturn.resize(static_cast<size_t>(len));
        ReadBytes(reinterpret_cast<u8*>(toReturn.data()), static_cast<u32>(len));

        const size_t terminator = toReturn.find('\0');
        if (terminator != std::string::npos)
            toReturn.resize(terminator);
    }

    return toReturn;
}

wstring BaseIO::ReadWString(int len)
{
    wstring toReturn;

    // assume it's null terminating
    if (len == -1)
    {
        toReturn = L"";
        wchar_t nextChar;
        while ((nextChar = ReadWord()) != 0)
            toReturn += nextChar;
    }
    else
    {
        for (int i = 0; i < len; i++)
        {
            wchar_t c = static_cast<wchar_t>(ReadWord());
            if (c != 0)
                toReturn += c;
            else
                break;
        }
    }

    return toReturn;
}

void BaseIO::Write(u8 b)
{
    WriteBytes(&b, 1);
}

void BaseIO::Write(u16 w)
{
    if (byteOrder == BigEndian)
        reverseByteArray(reinterpret_cast<u8*>(&w), 2);
    WriteBytes(reinterpret_cast<u8*>(&w), 2);
}

void BaseIO::WriteInt24(u32 value, EndianType et)
{
    EndianType orig = byteOrder;
    if(et != Default)
        byteOrder = et;

    if(byteOrder == BigEndian)
    {
        value <<= 8;
        reverseByteArray(reinterpret_cast<u8*>(&value), 4);
    }
    WriteBytes(reinterpret_cast<u8*>(&value), 3);

    byteOrder = orig;
}

void BaseIO::Write(u32 dw)
{
    if (byteOrder == BigEndian)
        reverseByteArray(reinterpret_cast<u8*>(&dw), 4);
    WriteBytes(reinterpret_cast<u8*>(&dw), 4);
}

void BaseIO::Write(u64 qw)
{
    if (byteOrder == BigEndian)
        reverseByteArray(reinterpret_cast<u8*>(&qw), 8);
    WriteBytes(reinterpret_cast<u8*>(&qw), 8);
}

void BaseIO::Write(string s, int forceLen, bool nullTerminating, u8 nullTerminator)
{
    WriteBytes((u8*)s.c_str(), s.length() + nullTerminating);

    if (forceLen > 0)
    {
        forceLen -= s.size();

        for (int i = 0; i < forceLen; i++)
            Write(nullTerminator);
    }
}

void BaseIO::Write(wstring ws, bool nullTerminating)
{
    for (wchar_t c : ws)
    {
        u32 codepoint = static_cast<u32>(c);

        if (sizeof(wchar_t) == 2 || codepoint <= 0xFFFF)
        {
            Write(static_cast<u16>(codepoint));
        }
        else
        {
            // Encode supplementary characters as a UTF-16 surrogate pair.
            codepoint -= 0x10000;
            Write(static_cast<u16>(0xD800 + (codepoint >> 10)));
            Write(static_cast<u16>(0xDC00 + (codepoint & 0x3FF)));
        }
    }

    if (nullTerminating)
        Write(static_cast<u16>(0));
}

void BaseIO::Write(u8 *buffer, u32 len)
{
    WriteBytes(buffer, len);
}

void BaseIO::SwapEndian()
{
    if (byteOrder == LittleEndian)
        byteOrder = BigEndian;
    else
        byteOrder = LittleEndian;
}


