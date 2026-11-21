#include <XboxInternals/IO/IndexableMultiFileIO.h>

#include <algorithm>

IndexableMultiFileIO::IndexableMultiFileIO()
    : addressInFile(0), fileIndex(0), currentIO(nullptr) {}

IndexableMultiFileIO::~IndexableMultiFileIO() = default;

void IndexableMultiFileIO::SetPosition(u32 newAddressInFile, int desiredFileIndex)
{
    if (currentIO == nullptr && files.empty()) {
        throw std::string("MultiFileIO: No files are loaded.");
    }

    if (desiredFileIndex == -1 || static_cast<u32>(desiredFileIndex) == fileIndex) {
        if (newAddressInFile >= CurrentFileLength()) {
            throw std::string("MultiFileIO: Cannot seek beyond the end of the file\n");
        }

        currentIO->SetPosition(newAddressInFile);
        addressInFile = newAddressInFile;
        return;
    }

    if (desiredFileIndex < 0 || static_cast<size_t>(desiredFileIndex) >= files.size()) {
        throw std::string("MultiFileIO: Specified file index is out of range\n");
    }

    if (currentIO != nullptr) {
        currentIO->Close();
        currentIO.reset();
    }

    currentIO = openFile(files.at(static_cast<size_t>(desiredFileIndex)));
    if (newAddressInFile >= CurrentFileLength()) {
        throw std::string("MultiFileIO: Cannot seek beyond the end of the file\n");
    }

    addressInFile = newAddressInFile;
    fileIndex = static_cast<u32>(desiredFileIndex);
    currentIO->SetPosition(addressInFile);
}

void IndexableMultiFileIO::GetPosition(u32 *addressOut, u32 *fileIndexOut)
{
    if (addressOut) {
        *addressOut = addressInFile;
    }
    if (fileIndexOut) {
        *fileIndexOut = fileIndex;
    }
}

u32 IndexableMultiFileIO::CurrentFileLength()
{
    if (currentIO == nullptr) {
        throw std::string("MultiFileIO: No current file is open\n");
    }

    currentIO->SetPosition(0, std::ios_base::end);
    u32 fileLen = static_cast<u32>(currentIO->GetPosition());
    currentIO->SetPosition(addressInFile);
    return fileLen;
}

void IndexableMultiFileIO::ReadBytes(u8 *outBuffer, u32 len)
{
    while (len > 0) {
        u32 bytesLeft = CurrentFileLength() - addressInFile;
        u32 amountToRead = (bytesLeft > len) ? len : bytesLeft;

        currentIO->ReadBytes(outBuffer, amountToRead);

        if (len >= bytesLeft && (fileIndex + 1) < FileCount()) {
            SetPosition(static_cast<u32>(0), static_cast<int>(fileIndex + 1));
        } else if (len < bytesLeft) {
            SetPosition(addressInFile + len);
        }

        len -= amountToRead;
        outBuffer += amountToRead;
    }
}

void IndexableMultiFileIO::WriteBytes(u8 *buffer, u32 len)
{
    while (len > 0) {
        u32 bytesLeft = CurrentFileLength() - addressInFile;
        u32 amountToWrite = (bytesLeft > len) ? len : bytesLeft;

        currentIO->Write(buffer, amountToWrite);

        if (len >= bytesLeft) {
            SetPosition(static_cast<u32>(0), static_cast<int>(fileIndex + 1));
        } else {
            SetPosition(addressInFile + len);
        }

        len -= amountToWrite;
        buffer += amountToWrite;
    }
}

void IndexableMultiFileIO::Close()
{
    if (currentIO) {
        currentIO->Close();
    }
}

void IndexableMultiFileIO::Flush()
{
    if (currentIO) {
        currentIO->Flush();
    }
}

u64 IndexableMultiFileIO::Length()
{
    if (currentIO == nullptr) {
        return 0;
    }
    return currentIO->Length();
}

u32 IndexableMultiFileIO::FileCount()
{
    return static_cast<u32>(files.size());
}

void IndexableMultiFileIO::SetPosition(u64, std::ios_base::seekdir)
{
    throw std::string("MultiFileIO: Unused function has been called.\n");
}

u64 IndexableMultiFileIO::GetPosition()
{
    throw std::string("MultiFileIO: Unused function has been called.\n");
}
