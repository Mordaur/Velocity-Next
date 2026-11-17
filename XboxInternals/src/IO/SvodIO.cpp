#include <XboxInternals/IO/SvodIO.h>
#include <XboxInternals/IO/FileIO.h>
#include <XboxInternals/Disc/SvodLayout.h>

SvodIO::SvodIO(XContentHeader *metadata, GdfxFileEntry entry, IndexableMultiFileIO *io) :
    BaseIO(), io(io), metadata(metadata), fileEntry(entry), pos(0)
{
    offset = (metadata->svodVolumeDescriptor.flags != 0) ? 0x2000 : 0x1000;

    // seek to the file's beginning
    DWORD addr, index;
    SectorToAddress(entry.sector, &addr, &index);
    io->SetPosition(addr, static_cast<int>(index));
}

SvodIO::~SvodIO()
{

}

void SvodIO::SectorToAddress(DWORD sector, DWORD *addressInDataFile, DWORD *dataFileIndex)
{
    if (!XboxInternals::SvodLayout::Map(sector,
            metadata->svodVolumeDescriptor.dataBlockOffset,
            metadata->svodVolumeDescriptor.flags != 0,
            addressInDataFile, dataFileIndex))
        throw std::string("SvodIO: Sector outside the data volume.\n");
}

void SvodIO::SetPosition(UINT64 address, ios_base::seekdir dir)
{
    if (dir != std::ios_base::beg)
        throw std::string("SvodIO: Unsupported seek direction\n");

    if (address > Length())
        throw std::string("SvodIO: Cannot seek beyond end of file.\n");

    const UINT64 fileSector = static_cast<UINT64>(fileEntry.sector) + address / 0x800;
    if (fileSector > 0xFFFFFFFFULL)
        throw std::string("SvodIO: Sector index out of range.\n");

    DWORD addr = 0, index = 0;
    SectorToAddress(static_cast<DWORD>(fileSector), &addr, &index);

    io->SetPosition(addr + static_cast<DWORD>(address % 0x800), static_cast<int>(index));
    pos = address;
}

UINT64 SvodIO::GetPosition()
{
    return pos;
}

UINT64 SvodIO::Length()
{
    return fileEntry.size;
}

void SvodIO::ReadBytes(BYTE *outBuffer, DWORD len)
{
    if (pos + len > Length())
        throw std::string("SvodIO: Cannot read beyond end of file.\n");

    while (len > 0)
    {
        // Re-map for every chunk so a seek never relies on the previous part.
        SetPosition(pos);

        DWORD addressInFile = 0, index = 0;
        io->GetPosition(&addressInFile, &index);

        const UINT64 dataStart = XboxInternals::SvodLayout::DataStartBytes(
                metadata->svodVolumeDescriptor.flags != 0);
        if (addressInFile < dataStart)
            throw std::string("SvodIO: Invalid data address.\n");

        const UINT64 offsetInGroup = (addressInFile - dataStart) % 0xCD000;
        UINT64 chunk = 0xCC000 - offsetInGroup;
        if (chunk == 0)
            chunk = 0xCC000;
        if (chunk > len)
            chunk = len;

        io->ReadBytes(outBuffer, static_cast<DWORD>(chunk));

        outBuffer += chunk;
        pos += chunk;
        len -= static_cast<DWORD>(chunk);
    }
}

void SvodIO::WriteBytes(BYTE *buffer, DWORD len)
{
    if (pos + len > Length())
        throw std::string("SvodIO: Cannot write beyond end of file.\n");

    while (len > 0)
    {
        SetPosition(pos);

        DWORD addressInFile = 0, index = 0;
        io->GetPosition(&addressInFile, &index);

        const UINT64 dataStart = XboxInternals::SvodLayout::DataStartBytes(
                metadata->svodVolumeDescriptor.flags != 0);
        if (addressInFile < dataStart)
            throw std::string("SvodIO: Invalid data address.\n");

        const UINT64 offsetInGroup = (addressInFile - dataStart) % 0xCD000;
        UINT64 chunk = 0xCC000 - offsetInGroup;
        if (chunk == 0)
            chunk = 0xCC000;
        if (chunk > len)
            chunk = len;

        io->WriteBytes(buffer, static_cast<DWORD>(chunk));

        buffer += chunk;
        pos += chunk;
        len -= static_cast<DWORD>(chunk);
    }
}

void SvodIO::SaveFile(string savePath, void(*progress)(void*, DWORD, DWORD), void *arg)
{
    FileIO outFile(savePath, true);
    std::vector<BYTE> buffer(0x10000);
    DWORD fileLen = fileEntry.size;
    DWORD total = (fileLen + 0xFFFF) / 0x10000;
    DWORD cur = 0;

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

void SvodIO::OverWriteFile(string inPath, void (*progress)(void *, DWORD, DWORD), void *arg)
{
    FileIO inFile(inPath);

    // make sure that the files are the same size
    inFile.SetPosition(0, ios_base::end);
    if ((DWORD)inFile.GetPosition() != fileEntry.size)
        throw string("SVOD: Cannot overWrite file of different length.\n");

    std::vector<BYTE> buffer(0x10000);
    DWORD fileLen = fileEntry.size;
    DWORD total = (fileLen + 0xFFFF) / 0x10000;
    DWORD cur = 0;

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


