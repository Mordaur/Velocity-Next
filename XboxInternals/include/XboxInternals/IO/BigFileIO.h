#pragma once

#include <memory>
#include <string>

#include <XboxInternals/IO/BaseIO.h>

#ifndef _WIN32
    #include <fstream>
#endif

// Cross-platform large file I/O
// Uses native Windows API on Windows, falls back to fstream on other platforms
class BigFileIO : public BaseIO
{
public:
    BigFileIO(std::string filePath, bool create = false);
    ~BigFileIO();

    void ReadBytes(u8 *outBuffer, u32 len) override;
    void WriteBytes(u8 *buffer, u32 len) override;
    u64 Length() override;
    u64 GetPosition() override;
    void SetPosition(u64 position, std::ios_base::seekdir dir = std::ios_base::beg) override;
    void Close() override;
    void Flush() override;

private:
    std::string filePath;

#ifdef _WIN32
    void *hFile; // Win32 HANDLE; <windows.h> stays out of the public header
#else
    std::unique_ptr<std::fstream> fstr;
    u64 length;
#endif
};
