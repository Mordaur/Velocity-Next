/* Most parts of this class were originally developed by Lander Griffith (https://github.com/landr0id/).
   Much of his code is used throughout this class or very slightly modified */

#ifndef MULTIFILEIO_H
#define MULTIFILEIO_H

#include <memory>
#include <vector>

#include <XboxInternals/IO/FileIO.h>

class XBOXINTERNALS_EXPORT MultiFileIO : public BaseIO
{
public:
    MultiFileIO(std::vector<std::string> filePaths);
    MultiFileIO(std::vector<BaseIO*> files);
    virtual ~MultiFileIO();
    
    // Delete copy operations (unique_ptr is not copyable)
    MultiFileIO(const MultiFileIO&) = delete;
    MultiFileIO& operator=(const MultiFileIO&) = delete;

    // seek to a position in a file
    void SetPosition(u64 position, std::ios_base::seekdir dir = std::ios_base::beg);

    // get current address in the file
    u64 GetPosition();

    // get the length of all the files
    u64 Length();

    // read len bytes from the current file at the current position into buffer
    void ReadBytes(u8 *outBuffer, u32 len);

    // Write len bytes from the current file at the current position into buffer
    void WriteBytes(u8 *buffer, u32 len);

    // flushes the stream
    void Flush();

    // closes all the files
    void Close();

private:
    u64 pos, lengthOfFiles;
    bool isClosed;
    u32 currentIOIndex;
    std::vector<std::unique_ptr<BaseIO>> files;

    void calculateLengthOfAllFiles();
};

#endif // MULTIFILEIO_H


