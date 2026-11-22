#include <XboxInternals/IO/IndexableMultiFileIO.h>

#include <algorithm>

IndexableMultiFileIO::IndexableMultiFileIO()
    : addressInFile(0), fileIndex(0), currentIO(nullptr) {}

IndexableMultiFileIO::~IndexableMultiFileIO() = default;

void IndexableMultiFileIO::SetPosition(u32 newAddressInFile, int desiredFileIndex)
{
    if (files.empty())
        throw std::string("MultiFileIO: No files are loaded.\n");

    if (desiredFileIndex == -1)
        desiredFileIndex = static_cast<int>(fileIndex);

    if (desiredFileIndex < 0 || static_cast<size_t>(desiredFileIndex) >= files.size())
        throw std::string("MultiFileIO: Specified file index is out of range\n");

    if (currentIO == nullptr || static_cast<u32>(desiredFileIndex) != fileIndex)
    {
        // Open first so a failure leaves the current part intact.
        std::unique_ptr<BaseIO> next = openFile(files.at(static_cast<size_t>(desiredFileIndex)));
        if (currentIO)
            currentIO->Close();
        currentIO = std::move(next);
        fileIndex = static_cast<u32>(desiredFileIndex);
        addressInFile = 0;
    }

    // The end of a part is a valid position; past it is not.
    if (newAddressInFile > CurrentFileLength())
        throw std::string("MultiFileIO: Cannot seek beyond the end of the file\n");

    currentIO->SetPosition(newAddressInFile);
    addressInFile = newAddressInFile;
}

void IndexableMultiFileIO::ensureAvailable(u32 len)
{
    u64 available = CurrentFileLength() - addressInFile;
    for (u32 i = fileIndex + 1; available < len && i < FileCount(); ++i)
    {
        std::unique_ptr<BaseIO> part = openFile(files.at(i));
        available += part->Length();
        part->Close();
    }

    if (available < len)
        throw std::string("MultiFileIO: Cannot access beyond the end of the last file\n");
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
    ensureAvailable(len);
    while (len > 0)
    {
        const u32 bytesLeft = CurrentFileLength() - addressInFile;
        if (bytesLeft == 0)
        {
            // ensureAvailable() guarantees a following part.
            SetPosition(static_cast<u32>(0), static_cast<int>(fileIndex + 1));
            continue;
        }

        const u32 chunk = std::min(len, bytesLeft);
        currentIO->ReadBytes(outBuffer, chunk);
        addressInFile += chunk;
        outBuffer += chunk;
        len -= chunk;
    }
}

void IndexableMultiFileIO::WriteBytes(u8 *buffer, u32 len)
{
    ensureAvailable(len);
    while (len > 0)
    {
        const u32 bytesLeft = CurrentFileLength() - addressInFile;
        if (bytesLeft == 0)
        {
            SetPosition(static_cast<u32>(0), static_cast<int>(fileIndex + 1));
            continue;
        }

        const u32 chunk = std::min(len, bytesLeft);
        currentIO->WriteBytes(buffer, chunk);
        addressInFile += chunk;
        buffer += chunk;
        len -= chunk;
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
