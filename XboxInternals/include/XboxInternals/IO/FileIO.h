#pragma once

#include <errno.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <string.h>
#include <XboxInternals/Types.h>
#include <XboxInternals/IO/BaseIO.h>

using std::string;
using std::wstring;
using std::fstream;
using std::streampos;
using std::ios_base;

class XBOXINTERNALS_EXPORT FileIO : public BaseIO
{
public:
    FileIO(string path, bool truncate = false);
    void SetPosition(u64 pos, ios_base::seekdir dir = ios_base::beg);
    u64 GetPosition();
    u64 Length();

    void ReadBytes(u8 *outBuffer, u32 len);
    void WriteBytes(u8 *buffer, u32 len);

    void Close();
    void Flush();

    string GetFilePath();

    static void ReverseGenericArray(void *arr, int elemSize, int len);
    virtual ~FileIO();
private:
    EndianType endian;
    u64 length;
    bool readOnly = false;
    // A read-write fstream keeps separate get and put positions. These track
    // which one is stale so the next operation can resync before it runs.
    bool readNeedsSync = false;
    bool writeNeedsSync = false;
    void ReadBytesWithChecks(void *buffer, i32 size);
    std::unique_ptr<fstream> fstr;
    const string filePath;
};



