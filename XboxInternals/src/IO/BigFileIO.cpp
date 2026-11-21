#include <XboxInternals/IO/BigFileIO.h>

#include <utility>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

#ifndef _WIN32
#include <fstream>
#include <memory>
#endif

BigFileIO::BigFileIO(std::string filePath, bool create)
    : filePath(std::move(filePath))
{
#ifdef _WIN32
    hFile = INVALID_HANDLE_VALUE;
    DWORD disposition = create ? CREATE_ALWAYS : OPEN_EXISTING;
    DWORD access = GENERIC_READ | GENERIC_WRITE;
    DWORD share = FILE_SHARE_READ;

    hFile = CreateFileA(this->filePath.c_str(), access, share, nullptr, disposition, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        throw std::string("BigFileIO: Unable to open the file.");
    }
#else
    length = 0;
    std::ios::openmode mode = std::ios::binary | std::ios::in | std::ios::out;
    if (create) {
        mode |= std::ios::trunc;
    }

    fstr = std::make_unique<std::fstream>(this->filePath, mode);
    if (!fstr || !fstr->is_open()) {
        fstr.reset();
        throw std::string("BigFileIO: Unable to open the file.");
    }

    fstr->seekg(0, std::ios::end);
    length = static_cast<u64>(fstr->tellg());
    fstr->seekg(0, std::ios::beg);
    fstr->seekp(0, std::ios::beg);
#endif
}

BigFileIO::~BigFileIO()
{
    Close();
}

void BigFileIO::ReadBytes(u8 *outBuffer, u32 len)
{
#ifdef _WIN32
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, outBuffer, len, &bytesRead, nullptr) || bytesRead != len) {
        throw std::string("BigFileIO: Error reading from file.");
    }
#else
    if (!fstr || !fstr->good()) {
        throw std::string("BigFileIO: File stream is not available.");
    }

    fstr->read(reinterpret_cast<char*>(outBuffer), len);
    if (fstr->gcount() != static_cast<std::streamsize>(len)) {
        throw std::string("BigFileIO: Error reading from file.");
    }
#endif
}

void BigFileIO::WriteBytes(u8 *buffer, u32 len)
{
#ifdef _WIN32
    DWORD bytesWritten = 0;
    if (!WriteFile(hFile, buffer, len, &bytesWritten, nullptr) || bytesWritten != len) {
        throw std::string("BigFileIO: Error writing to the file.");
    }
#else
    if (!fstr || !fstr->good()) {
        throw std::string("BigFileIO: File stream is not available.");
    }

    fstr->write(reinterpret_cast<char*>(buffer), len);
    if (!fstr->good()) {
        throw std::string("BigFileIO: Error writing to the file.");
    }

    const u64 newPos = static_cast<u64>(fstr->tellp());
    if (newPos > length) {
        length = newPos;
    }
#endif
}

u64 BigFileIO::Length()
{
#ifdef _WIN32
    LARGE_INTEGER size {};
    if (!GetFileSizeEx(hFile, &size)) {
        throw std::string("BigFileIO: Error getting file size.");
    }
    return static_cast<u64>(size.QuadPart);
#else
    return length;
#endif
}

u64 BigFileIO::GetPosition()
{
#ifdef _WIN32
    LARGE_INTEGER distance {};
    LARGE_INTEGER current {};
    if (!SetFilePointerEx(hFile, distance, &current, FILE_CURRENT)) {
        throw std::string("BigFileIO: Error getting file position.");
    }
    return static_cast<u64>(current.QuadPart);
#else
    if (!fstr) {
        throw std::string("BigFileIO: File stream is not available.");
    }

    auto gpos = fstr->tellg();
    if (gpos >= 0) {
        return static_cast<u64>(gpos);
    }

    auto ppos = fstr->tellp();
    if (ppos >= 0) {
        return static_cast<u64>(ppos);
    }

    throw std::string("BigFileIO: Error getting file position.");
#endif
}

void BigFileIO::SetPosition(u64 position, std::ios_base::seekdir dir)
{
#ifdef _WIN32
    LARGE_INTEGER distance {};
    distance.QuadPart = static_cast<LONGLONG>(position);

    DWORD moveMethod = FILE_BEGIN;
    if (dir == std::ios_base::cur) {
        moveMethod = FILE_CURRENT;
    } else if (dir == std::ios_base::end) {
        moveMethod = FILE_END;
    }

    if (!SetFilePointerEx(hFile, distance, nullptr, moveMethod)) {
        throw std::string("BigFileIO: Error setting position.");
    }
#else
    if (!fstr) {
        throw std::string("BigFileIO: File stream is not available.");
    }

    fstr->seekg(static_cast<std::streamoff>(position), dir);
    fstr->seekp(static_cast<std::streamoff>(position), dir);
    if (!fstr->good()) {
        throw std::string("BigFileIO: Error setting position.");
    }
#endif
}

void BigFileIO::Close()
{
#ifdef _WIN32
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;
    }
#else
    if (fstr) {
        fstr->close();
        fstr.reset();
        length = 0;
    }
#endif
}

void BigFileIO::Flush()
{
#ifdef _WIN32
    if (!FlushFileBuffers(hFile)) {
        throw std::string("BigFileIO: Unable to flush file buffers.");
    }
#else
    if (!fstr) {
        throw std::string("BigFileIO: File stream is not available.");
    }

    fstr->flush();
    if (!fstr->good()) {
        throw std::string("BigFileIO: Unable to flush file buffers.");
    }
#endif
}
