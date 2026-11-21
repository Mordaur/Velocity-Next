#include <XboxInternals/IO/SvodIO.h>
#include <XboxInternals/IO/FileIO.h>
#include <XboxInternals/Disc/SvodLayout.h>

namespace
{
    bool IsEnhanced(const XContentHeader *metadata)
    {
        return XboxInternals::SvodLayout::IsEnhanced(metadata->svodVolumeDescriptor.flags);
    }

    u32 DataBlockOffset(const XContentHeader *metadata)
    {
        return XboxInternals::SvodLayout::DataBlockOffset(
                metadata->svodVolumeDescriptor.dataBlockOffset, IsEnhanced(metadata));
    }

    u64 ChunkUntilHashBlock(u32 addressInFile)
    {
        const u64 available = XboxInternals::SvodLayout::BytesUntilHashBlock(addressInFile);
        if (available == 0)
            throw std::string("SvodIO: Invalid data address.\n");

        return available;
    }
}

SvodIO::SvodIO(XContentHeader *metadata, GdfxFileEntry entry, IndexableMultiFileIO *io) :
    BaseIO(), io(io), metadata(metadata), fileEntry(entry), pos(0)
{
    offset = static_cast<u32>(XboxInternals::SvodLayout::DataBlockAddress(0));

    // seek to the file's beginning
    u32 addr, index;
    SectorToAddress(entry.sector, &addr, &index);
    io->SetPosition(addr, static_cast<int>(index));
}

SvodIO::~SvodIO()
{

}

void SvodIO::SectorToAddress(u32 sector, u32 *addressInDataFile, u32 *dataFileIndex)
{
    if (!XboxInternals::SvodLayout::Map(sector, DataBlockOffset(metadata),
            IsEnhanced(metadata), addressInDataFile, dataFileIndex,
            metadata->svodVolumeDescriptor.dataBlockCount))
        throw std::string("SvodIO: Sector outside the data volume.\n");
}

void SvodIO::SetPosition(u64 address, ios_base::seekdir dir)
{
    if (dir != std::ios_base::beg)
        throw std::string("SvodIO: Unsupported seek direction\n");

    if (address > Length())
        throw std::string("SvodIO: Cannot seek beyond end of file.\n");

    const u64 fileSector = static_cast<u64>(fileEntry.sector) + address / 0x800;
    if (fileSector > 0xFFFFFFFFULL)
        throw std::string("SvodIO: Sector index out of range.\n");

    u32 addr = 0, index = 0;
    SectorToAddress(static_cast<u32>(fileSector), &addr, &index);

    io->SetPosition(addr + static_cast<u32>(address % 0x800), static_cast<int>(index));
    pos = address;
}

u64 SvodIO::GetPosition()
{
    return pos;
}

u64 SvodIO::Length()
{
    return fileEntry.size;
}

void SvodIO::ReadBytes(u8 *outBuffer, u32 len)
{
    if (pos + len > Length())
        throw std::string("SvodIO: Cannot read beyond end of file.\n");

    while (len > 0)
    {
        // Re-map for every chunk so a seek never relies on the previous part.
        SetPosition(pos);

        u32 addressInFile = 0, index = 0;
        io->GetPosition(&addressInFile, &index);

        u64 chunk = ChunkUntilHashBlock(addressInFile);
        if (chunk > len)
            chunk = len;

        io->ReadBytes(outBuffer, static_cast<u32>(chunk));

        outBuffer += chunk;
        pos += chunk;
        len -= static_cast<u32>(chunk);
    }
}

void SvodIO::WriteBytes(u8 *buffer, u32 len)
{
    if (pos + len > Length())
        throw std::string("SvodIO: Cannot write beyond end of file.\n");

    while (len > 0)
    {
        SetPosition(pos);

        u32 addressInFile = 0, index = 0;
        io->GetPosition(&addressInFile, &index);

        u64 chunk = ChunkUntilHashBlock(addressInFile);
        if (chunk > len)
            chunk = len;

        io->WriteBytes(buffer, static_cast<u32>(chunk));

        buffer += chunk;
        pos += chunk;
        len -= static_cast<u32>(chunk);
    }
}

void SvodIO::SaveFile(string savePath, void(*progress)(void*, u32, u32), void *arg)
{
    FileIO outFile(savePath, true);
    std::vector<u8> buffer(0x10000);
    u32 fileLen = fileEntry.size;
    u32 total = (fileLen + 0xFFFF) / 0x10000;
    u32 cur = 0;

    while (fileLen >= 0x10000)
    {
        ReadBytes(buffer.data(), 0x10000);
        outFile.Write(buffer.data(), 0x10000);
        fileLen -= 0x10000;

        if (progress)
            progress(arg, cur++, total);
    }

    if (fileLen != 0)
    {
        ReadBytes(buffer.data(), fileLen);
        outFile.Write(buffer.data(), fileLen);
    }

    if (progress)
        progress(arg, total, total);

    outFile.Close();
}

void SvodIO::OverWriteFile(string inPath, void (*progress)(void *, u32, u32), void *arg)
{
    FileIO inFile(inPath);

    // make sure that the files are the same size
    inFile.SetPosition(0, ios_base::end);
    if (inFile.GetPosition() != static_cast<u64>(fileEntry.size))
        throw string("SVOD: Cannot overWrite file of different length.\n");

    std::vector<u8> buffer(0x10000);
    u32 fileLen = fileEntry.size;
    u32 total = (fileLen + 0xFFFF) / 0x10000;
    u32 cur = 0;

    inFile.SetPosition(0);
    SetPosition(0);

    while (fileLen >= 0x10000)
    {
        inFile.ReadBytes(buffer.data(), 0x10000);
        WriteBytes(buffer.data(), 0x10000);
        fileLen -= 0x10000;

        if (progress)
            progress(arg, cur++, total);
    }

    if (fileLen != 0)
    {
        inFile.ReadBytes(buffer.data(), fileLen);
        WriteBytes(buffer.data(), fileLen);
    }

    if (progress)
        progress(arg, total, total);

    inFile.Close();
}

void SvodIO::Close()
{
    // all SvodIOs use the same internal io, so we can't close it
}

void SvodIO::Flush()
{
    io->Flush();
}


