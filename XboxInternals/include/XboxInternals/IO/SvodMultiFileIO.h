#ifndef MULTIFILEIO_H
#define MULTIFILEIO_H

#include <iostream>
#include <memory>
#include <vector>

#include <XboxInternals/IO/BaseIO.h>
#include <XboxInternals/IO/FileIO.h>
#include <XboxInternals/Export.h>

using std::string;
using std::wstring;
using std::vector;

class XBOXINTERNALS_EXPORT SvodMultiFileIO : public BaseIO
{
public:
    SvodMultiFileIO(string fileDirectory);
    virtual ~SvodMultiFileIO();

    // seek to a certain address in the file, index of -1 for current file
    void SetPosition(u32 addressInFile, u32 fileIndex = -1);

    // get the current position of the io
    void GetPosition(u32 *addressInFile, u32 *fileIndex);

    // read len bytes from the current file at the current position into buffer
    void ReadBytes(u8 *outBuffer, u32 len);

    // Write len bytes to the file at the current position
    void WriteBytes(u8 *buffer, u32 len);

    void Close();

    void Flush();

    u64 Length();

    // get the number of files in the directory
    u32 FileCount();

    // get the number of bytes in the current file
    u32 CurrentFileLength();

    // unused
    void SetPosition(u64 position, std::ios_base::seekdir dir = std::ios_base::beg);
    u64 GetPosition();

private:
    u32 addressInFile;
    u32 fileIndex;

    std::unique_ptr<FileIO> currentIO;
    vector<string> files;

    // get all the file names in the directory
    void loadDirectories(string path);
};

#endif // MULTIFILEIO_H


