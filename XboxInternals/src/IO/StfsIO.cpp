#include <XboxInternals/IO/StfsIO.h>

#include <cstdint>

#include <XboxInternals/Stfs/StfsPackage.h>

StfsIO::StfsIO(BaseIO *io, StfsPackage *package, StfsFileEntry entry) :
    io(io), package(package), entry(entry), didChangeSize(false)
{
    package->GenerateBlockChain(&this->entry);
    blocksAllocated = this->entry.blocksForFile;
    SetPosition(0);
}

StfsIO::~StfsIO()
{
    Flush();
}

StfsFileEntry StfsIO::GetStfsFileEntry()
{
    return entry;
}

void StfsIO::SetPosition(u64 position, std::ios_base::seekdir dir)
{
    if (dir == std::ios_base::cur)
        position += this->entryPosition;
    else if (dir == std::ios_base::end)
        position = (Length() + position);

    bool resizeNecessary = (position > Length());
    if (resizeNecessary)
        Resize(position);

    this->entryPosition = position;

    // An empty file has no blocks; position 0 is its end, with nothing to seek to.
    if (entry.blockChain.empty())
        return;

    u32 blockNum;
    u64 offset = position % 0x1000;
    if (position == Length())
    {
        blockNum = entry.blockChain.back();
        // A block-aligned end points one byte past the last block. WriteBytes
        // allocates the next block and seeks again before writing.
        if (offset == 0)
            offset = 0x1000;
    }
    else
    {
        blockNum = entry.blockChain.at(position / 0x1000);
    }

    u64 packagePosition = this->package->BlockToAddress(blockNum) + offset;
    io->SetPosition(packagePosition);
}

u64 StfsIO::GetPosition()
{
    return this->entryPosition;
}

u64 StfsIO::Length()
{
    return this->entry.fileSize;
}

void StfsIO::ReadBytes(u8 *outBuffer, u32 len)
{
    u32 maxRead = 0x1000 - (this->entryPosition % 0x1000);
    u64 endingPosition = this->entryPosition + len;

    while (len >= maxRead)
    {
        io->ReadBytes(outBuffer, maxRead);
        len -= maxRead;
        outBuffer += maxRead;
        SetPosition(maxRead, std::ios_base::cur);
    }

    if (len != 0)
        io->ReadBytes(outBuffer, len);

    SetPosition(endingPosition);
}

void StfsIO::WriteBytes(u8 *buffer, u32 len)
{
    const u64 startPosition = GetPosition();
    const u64 endingPosition = startPosition + len;
    if (endingPosition > UINT32_MAX)
        throw string("STFS: File size exceeds the 32-bit on-disk limit.\n");

    if (endingPosition > Length())
    {
        didChangeSize = true;

        const u64 originalBlockCount = (Length() + 0xFFF) / 0x1000;
        const u64 newBlockCount = (endingPosition + 0xFFF) / 0x1000;
        if (newBlockCount > originalBlockCount)
        {
            if (this->entry.blockChain.empty())
                throw string("STFS: Cannot grow a file that has no blocks.\n");

            u32 amountOfBlocksToAllocate = static_cast<u32>(newBlockCount - originalBlockCount);
            // AllocateBlock() increments allocatedBlockCount itself.
            this->package->metaData->stfsVolumeDescriptor.unallocatedBlockCount -= amountOfBlocksToAllocate;

            u32 previousBlock = this->entry.blockChain.back();
            while (amountOfBlocksToAllocate-- > 0)
            {
                u32 currentBlock = this->package->AllocateBlock();

                this->package->SetNextBlock(previousBlock, currentBlock);

                this->entry.blockChain.push_back(currentBlock);
                previousBlock = currentBlock;
                blocksAllocated++;
            }

            this->package->SetNextBlock(previousBlock, kBlockChainTerminator);
            this->package->metaData->WriteVolumeDescriptor();
        }

        entry.fileSize = static_cast<u32>(endingPosition);

        // Growth may have appended blocks. Re-seek from the saved position so
        // the first write lands at the new logical offset, not the old block.
        SetPosition(startPosition);
    }

    const u16 maxWrite = static_cast<u16>(0x1000 - (this->entryPosition % 0x1000));
    while (len >= maxWrite)
    {
        io->WriteBytes(buffer, maxWrite);
        len -= maxWrite;
        buffer += maxWrite;
        SetPosition(maxWrite, std::ios_base::cur);
    }

    if (len != 0)
        io->WriteBytes(buffer, len);

    SetPosition(endingPosition);
}

void StfsIO::Flush()
{
    if (didChangeSize)
    {
        this->io->SetPosition(this->entry.fileEntryAddress + 0x29);
        this->io->WriteInt24(blocksAllocated, LittleEndian);
        this->io->WriteInt24(blocksAllocated, LittleEndian);

        this->io->SetPosition(this->entry.fileEntryAddress + 0x34);
        this->io->Write((u32)entry.fileSize);
    }

    this->io->Flush();
}

void StfsIO::Close()
{
    Flush();
}

void StfsIO::Resize(u64 size)
{
    if (size == this->entry.fileSize)
        return;

    if (size > UINT32_MAX)
        throw string("STFS: File size exceeds the 32-bit on-disk limit.\n");

    // Growing from, or shrinking to, zero blocks would also need the entry's
    // starting block rewritten, which Flush() does not do.
    if (this->entry.blockChain.empty() || (size + 0xFFF) / 0x1000 == 0)
        throw string("STFS: Cannot resize a file to or from zero blocks.\n");

    u32 originalSize = this->entry.fileSize;
    this->entry.fileSize = (u32)size;
    didChangeSize = true;

    u32 newBlockCount = (size + 0xFFF) / 0x1000;
    u32 originalBlockCount = (originalSize + 0xFFF) / 0x1000;
    this->entry.blocksForFile = newBlockCount;
    // Flush() writes blocksAllocated into both the blocks-for-file and
    // blocks-allocated fields, so keep it in step with the chain.
    blocksAllocated = newBlockCount;

    if (originalBlockCount > newBlockCount)
    {
        u32 amountOfBlocksToDeallocate = originalBlockCount - newBlockCount;
        for (u32 i = 0; i < amountOfBlocksToDeallocate; i++)
        {
            u32 currentBlock = this->entry.blockChain.back();
            this->entry.blockChain.pop_back();

            // The status byte is +0x14; +0x15 is the 24-bit next-block field.
            io->SetPosition(this->package->GetHashAddressOfBlock(currentBlock) + 0x14);
            io->Write(static_cast<u8>(PreviouslyAllocated));
        }

        this->package->SetNextBlock(this->entry.blockChain.back(), kBlockChainTerminator);
        this->package->metaData->stfsVolumeDescriptor.allocatedBlockCount -= amountOfBlocksToDeallocate;
        this->package->metaData->stfsVolumeDescriptor.unallocatedBlockCount += amountOfBlocksToDeallocate;
        this->package->metaData->WriteVolumeDescriptor();
    }
    else if (originalBlockCount < newBlockCount)
    {
        u32 amountOfBlocksToAllocate = newBlockCount - originalBlockCount;
        // AllocateBlock() increments allocatedBlockCount itself.
        this->package->metaData->stfsVolumeDescriptor.unallocatedBlockCount -= amountOfBlocksToAllocate;
        u32 previousBlock = this->entry.blockChain.back();
        for (u32 i = 0; i < amountOfBlocksToAllocate; i++)
        {
            u32 currentBlock = this->package->AllocateBlock();
            this->package->SetNextBlock(previousBlock, currentBlock);
            this->entry.blockChain.push_back(currentBlock);
            previousBlock = currentBlock;
        }

        this->package->SetNextBlock(previousBlock, kBlockChainTerminator);
    }

    Flush();
}
