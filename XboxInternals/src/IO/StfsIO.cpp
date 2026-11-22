#include <XboxInternals/IO/StfsIO.h>

#include <algorithm>
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

    this->entryPosition = position;

    // Seeking past the end does not grow the file: WriteBytes grows it and
    // re-seeks, while ReadBytes rejects the read. An empty file has no blocks.
    if (position > Length() || entry.blockChain.empty())
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
    if (this->entryPosition > Length() || len > Length() - this->entryPosition)
        throw string("STFS: Cannot read past the end of the file.\n");
    if (len != 0 && (static_cast<u64>(this->entry.blockChain.size()) << 12) <
            this->entryPosition + len)
        throw string("STFS: The file's block chain is shorter than its size.\n");

    u32 remaining = len;
    while (remaining != 0)
    {
        // The shared package IO may have moved (Flush, Resize, other package
        // calls), and a block's logical successor is not its physical
        // neighbour: map every chunk from the logical position.
        SetPosition(this->entryPosition);
        const u32 chunk = std::min<u32>(remaining,
                static_cast<u32>(0x1000 - (this->entryPosition % 0x1000)));
        io->ReadBytes(outBuffer, chunk);
        outBuffer += chunk;
        remaining -= chunk;
        this->entryPosition += chunk;
    }
}

void StfsIO::WriteBytes(u8 *buffer, u32 len)
{
    if (len == 0)
        return;

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
            // AllocateBlock() appends new blocks; it never consumes a block that
            // was already free inside the package, so the free count is unchanged.
            u32 previousBlock = this->entry.blockChain.back();
            while (amountOfBlocksToAllocate-- > 0)
            {
                u32 currentBlock = this->package->AllocateBlock();

                if (currentBlock != previousBlock + 1)
                    this->entry.flags &= static_cast<u8>(~ConsecutiveBlocks);

                this->package->SetNextBlock(previousBlock, currentBlock);

                this->entry.blockChain.push_back(currentBlock);
                previousBlock = currentBlock;
                blocksAllocated++;
            }

            this->package->SetNextBlock(previousBlock, kBlockChainTerminator);
            this->package->metaData->WriteVolumeDescriptor();
        }

        entry.fileSize = static_cast<u32>(endingPosition);
    }

    if ((static_cast<u64>(this->entry.blockChain.size()) << 12) < endingPosition)
        throw string("STFS: The file's block chain is shorter than its size.\n");

    u32 remaining = len;
    while (remaining != 0)
    {
        // The shared package IO may have moved (Flush, Resize, other package
        // calls), and a block's logical successor is not its physical
        // neighbour: map every chunk from the logical position.
        SetPosition(this->entryPosition);
        const u32 chunk = std::min<u32>(remaining,
                static_cast<u32>(0x1000 - (this->entryPosition % 0x1000)));
        io->WriteBytes(buffer, chunk);
        buffer += chunk;
        remaining -= chunk;
        this->entryPosition += chunk;
    }
}

void StfsIO::Flush()
{
    if (didChangeSize)
    {
        this->entry.blocksForFile = blocksAllocated;

        // Not found means the file was removed while open; its old slot may now
        // hold another entry, so write nothing.
        if (StfsFileEntry *cached = package->FindCachedFileEntry(&package->fileListing, this->entry))
        {
            this->entry.fileEntryAddress = cached->fileEntryAddress;
            this->entry.nameLen = cached->nameLen;

            this->io->SetPosition(this->entry.fileEntryAddress + 0x28);
            this->io->Write(static_cast<u8>((this->entry.nameLen & 0x3F) | (this->entry.flags << 6)));
            this->io->WriteInt24(blocksAllocated, LittleEndian);
            this->io->WriteInt24(blocksAllocated, LittleEndian);

            this->io->SetPosition(this->entry.fileEntryAddress + 0x34);
            this->io->Write(static_cast<u32>(this->entry.fileSize));

            cached->fileSize = this->entry.fileSize;
            cached->blocksForFile = this->entry.blocksForFile;
            cached->flags = this->entry.flags;
            cached->blockChain = this->entry.blockChain;
        }

        didChangeSize = false;
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

            // Freed blocks stay inside the package and stay addressable, so the
            // allocated count is unchanged; only the free count grows.
            this->package->SetBlockStatus(currentBlock, PreviouslyAllocated);
        }

        this->package->SetNextBlock(this->entry.blockChain.back(), kBlockChainTerminator);
        this->package->metaData->stfsVolumeDescriptor.unallocatedBlockCount += amountOfBlocksToDeallocate;
        this->package->metaData->WriteVolumeDescriptor();
    }
    else if (originalBlockCount < newBlockCount)
    {
        u32 amountOfBlocksToAllocate = newBlockCount - originalBlockCount;
        u32 previousBlock = this->entry.blockChain.back();
        for (u32 i = 0; i < amountOfBlocksToAllocate; i++)
        {
            u32 currentBlock = this->package->AllocateBlock();

            if (currentBlock != previousBlock + 1)
                this->entry.flags &= static_cast<u8>(~ConsecutiveBlocks);

            this->package->SetNextBlock(previousBlock, currentBlock);
            this->entry.blockChain.push_back(currentBlock);
            previousBlock = currentBlock;
        }

        this->package->SetNextBlock(previousBlock, kBlockChainTerminator);
    }

    Flush();
}
