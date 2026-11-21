#include <XboxInternals/Xex/Xuiz.h>

#include <memory>

Xuiz::Xuiz(std::string filePath)
{
    ownedIO = std::make_unique<FileIO>(filePath);
    io = ownedIO.get();
    Parse();
}

Xuiz::Xuiz(BaseIO *io) :
    io(io)
{
    Parse();
}

Xuiz::~Xuiz()
{
    ownedIO.reset();
}

std::vector<XuizFileEntry> Xuiz::GetFiles() const
{
    return files;
}

void Xuiz::ExtractFile(std::string outFile, XuizFileEntry *fileEntry) const
{
    std::vector<u8> copyBuffer(XUIZ_COPY_BUFFER_SIZE);

    u32 copyIterations = fileEntry->size / XUIZ_COPY_BUFFER_SIZE;
    if (fileEntry->size % XUIZ_COPY_BUFFER_SIZE != 0)
        copyIterations++;

    FileIO outIO(outFile, true);
    for (u32 i = 0; i < copyIterations; i++)
    {
        // if it's the last iteration then be careful not read beyond the end of the file
        u32 numBytesToCopy = XUIZ_COPY_BUFFER_SIZE;
        if (i + 1 == copyIterations && fileEntry->size % XUIZ_COPY_BUFFER_SIZE != 0)
            numBytesToCopy = fileEntry->size % XUIZ_COPY_BUFFER_SIZE;

        io->SetPosition(fileEntry->realAddress);
        io->ReadBytes(copyBuffer.data(), numBytesToCopy);
        outIO.WriteBytes(copyBuffer.data(), numBytesToCopy);
    }

    outIO.Close();
}

void Xuiz::ExtractFile(std::string outFile, std::string xuizFilePath) const
{
    XuizFileEntry *entry = GetFileEntry(xuizFilePath);
    if (entry == nullptr)
        throw std::string("Xuiz: Unable to find the file to extract.");

    ExtractFile(outFile, entry);
}

XuizFileEntry* Xuiz::GetFileEntry(std::string xuizFilePath) const
{
    for (size_t i = 0; i < files.size(); i++)
    {
        const XuizFileEntry& file = files.at(i);
        if (file.path == xuizFilePath)
            return const_cast<XuizFileEntry*>(&files.at(i));
    }

    return nullptr;
}

void Xuiz::Parse()
{
    io->SetPosition(0);
    if (io->ReadDword() != XUIZ_MAGIC)
        throw std::string("XUIZ: Invalid magic.");

    // all unknown
    io->ReadDword();
    io->ReadDword();
    io->ReadDword();

    u32 fileTableSize = io->ReadDword();
    u16 fileTableEntryCount = io->ReadWord();

    // read all the file entries
    for (u16 i = 0; i < fileTableEntryCount; i++)
    {
        XuizFileEntry entry;
        entry.size = io->ReadDword();
        entry.offset = io->ReadDword();
        entry.realAddress = XUIZ_HEADER_SIZE + fileTableSize + entry.offset;

        u8 pathLength = io->ReadByte();
        entry.path = io->ReadString(pathLength);

        files.push_back(entry);
    }
}
