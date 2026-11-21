#include <XboxInternals/IO/MultiFileIO.h>

#include <memory>

MultiFileIO::MultiFileIO(std::vector<std::string> filePaths) : pos(0), currentIOIndex(0)
{
    files.reserve(filePaths.size());
    for (const auto &path : filePaths)
    {
        files.push_back(std::make_unique<FileIO>(path));
    }

    calculateLengthOfAllFiles();
}

MultiFileIO::MultiFileIO(std::vector<BaseIO*> ownedFiles) : pos(0), currentIOIndex(0)
{
    files.reserve(ownedFiles.size());
    for (auto *io : ownedFiles)
    {
        files.emplace_back(io);
    }

    calculateLengthOfAllFiles();
}

MultiFileIO::~MultiFileIO()
{
    Close();
}

void MultiFileIO::SetPosition(u64 position, std::ios_base::seekdir dir)
{
    if (dir == std::ios_base::end)
        throw std::string("MultiFileIO: Unsupported seek dir.");
    else if (dir == std::ios_base::cur)
        position += pos;

    pos = position;

    // calculate which stream we should use
    for (size_t i = 0; i < files.size(); i++)
    {
        if (position >= files.at(i)->Length())
            position -= files.at(i)->Length();
        else
        {
            currentIOIndex = i;
            files.at(i)->SetPosition(position);
            break;
        }
    }
}

u64 MultiFileIO::GetPosition()
{
    return pos;
}

u64 MultiFileIO::Length()
{
    return lengthOfFiles;
}

void MultiFileIO::WriteBytes(u8 *buffer, u32 len)
{
    if (len > lengthOfFiles - pos)
        throw std::string("MultiFileIO: Requested Write length is too large.\n");

    u32 offset = 0;

    // check to see if we can Write straight from the current stream
    if (files.at(currentIOIndex)->GetPosition() + len > files.at(currentIOIndex)->Length())
    {
        // if we can't, we must calculate how much we should read each time
        while (len > 0)
        {
        u32 WriteCount = (files.at(currentIOIndex)->Length() > files.at(currentIOIndex)->GetPosition() +
            len) ? len : (files.at(currentIOIndex)->Length() - files.at(currentIOIndex)->GetPosition());
        files.at(currentIOIndex)->WriteBytes(buffer + offset, WriteCount);
            offset += WriteCount;
            len -= WriteCount;
            SetPosition(pos + WriteCount);
        }
    }
    else
    {
        files.at(currentIOIndex)->WriteBytes(buffer, len);
        pos += len;
    }
}

void MultiFileIO::Flush()
{
    files.at(currentIOIndex)->Flush();
}

void MultiFileIO::ReadBytes(u8 *outBuffer, u32 len)
{
    u32 offset = 0;

    // check to see if we can read straight from the current stream
    if (files.at(currentIOIndex)->GetPosition() + len > files.at(currentIOIndex)->Length())
    {
        // if we can't, we must calculate how much we should read each time
        while (len > 0)
        {
        u32 readCount = (files.at(currentIOIndex)->Length() > files.at(currentIOIndex)->GetPosition() +
            len) ? len : (files.at(currentIOIndex)->Length() - files.at(currentIOIndex)->GetPosition());
        files.at(currentIOIndex)->ReadBytes(outBuffer + offset, readCount);
            offset += readCount;
            len -= readCount;
            SetPosition(pos + readCount);
        }
    }
    else
    {
        files.at(currentIOIndex)->ReadBytes(outBuffer, len);
        pos += len;
    }
}

void MultiFileIO::Close()
{
    if (isClosed)
        return;

    files.clear();

    isClosed = true;
}

void MultiFileIO::calculateLengthOfAllFiles()
{
    isClosed = false;
    lengthOfFiles = 0;

    for (size_t i = 0; i < files.size(); i++)
        lengthOfFiles += files.at(i)->Length();
}


