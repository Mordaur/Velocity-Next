#include <XboxInternals/AvatarAsset/AvatarAsset.h>


AvatarAsset::AvatarAsset(string assetPath)
{
    metadata.gender = (AssetGender)0;
    customColors.count = 0;
    animation.frameCount = 0;

    ioOwned = std::make_unique<FileIO>(assetPath);
    io = ioOwned.get();
    readHeader();
    readBlocks();
}

AvatarAsset::AvatarAsset(FileIO *io) : io(io)
{
    metadata.gender = (AssetGender)0;
    customColors.count = 0;

    readHeader();
    readBlocks();
}

void AvatarAsset::readHeader()
{
    // seek to the beginning of the file
    io->SetPosition(0);

    // read in the magic to verify the file
    header.magic = io->ReadDword();
    if (header.magic != 0x53545242)
        throw string("STRB: Invalid magic.\n");

    // read in the rest of the header
    header.blockAlignmentStored = (bool)io->ReadByte();
    header.littleEndian = (bool)io->ReadByte();
    io->SetEndian((EndianType)header.littleEndian);

    io->ReadBytes(header.guid, 16);

    // read the rest of the header
    header.blockIDSize = io->ReadByte();
    header.blockSpanSize = io->ReadByte();
    header.unused = io->ReadWord();
    header.blockAlignment = (header.blockAlignmentStored) ? io->ReadByte() : 1;

    // calculate the stuff that needs to be calculated
    header.blockStartAddress = roundUpToBlockAlignment((header.blockAlignmentStored) ? 30 : 0x1A);
    header.blockHeaderSize = roundUpToBlockAlignment((header.blockIDSize + header.blockSpanSize) +
            header.blockSpanSize);
}

void AvatarAsset::readBlocks()
{
    u32 prevAddress = header.blockStartAddress, prevDataLen = 0;
    io->SetPosition(0, ios_base::end);
    u32 fileSize = io->GetPosition();

    for (;;)
    {
        // seek to the position of the next block
        u32 curBlockAddress = prevAddress + roundUpToBlockAlignment(prevDataLen);
        io->SetPosition(curBlockAddress);

        // if the io is at the end of the file, then break out
        if ((curBlockAddress + header.blockHeaderSize) > fileSize)
            break;

        // create a new block
        STRBBlock block;

        // read the block header
        block.id = io->ReadEnum<STRRBBlockId>(static_cast<int>(header.blockIDSize));
        block.dataLength = prevDataLen = (int)io->ReadMultiByte(header.blockSpanSize);
        block.fieldSize = (int)io->ReadMultiByte(header.blockSpanSize);

        if (block.id == STRBAssetMetadata)
            parseMetadataLegacyV1(curBlockAddress + header.blockHeaderSize);
        else if (block.id == STRBAssetMetadataVersioned)
            parseMetadata(curBlockAddress + header.blockHeaderSize);
        else if (block.id == STRBCustomColorTable)
            parseColorTable(curBlockAddress + header.blockHeaderSize);
        else if (block.id == STRBAnimation)
            readAnimationInfo(curBlockAddress + header.blockHeaderSize);

        // check the values read in
        if (block.dataLength < 0 || block.fieldSize == 0 || (block.dataLength % block.fieldSize) != 0)
            throw string("STRB: Invalid block header value(s).\n");

        prevAddress = curBlockAddress + header.blockHeaderSize;
        block.dataAddress = prevAddress;

        blocks.push_back(block);
    }
}

void AvatarAsset::parseMetadataLegacyV1(u32 pos)
{
    // seek to the position of the metadata
    io->SetPosition(pos);

    // preserve the endian
    EndianType byteOrder = io->GetEndian();
    io->SetEndian(LittleEndian);

    // read in the metadata
    metadata.metadataVersion = 1;
    metadata.gender = io->ReadEnum<AssetGender>(1);
    metadata.type = io->ReadEnum<BinaryAssetType>(4);
    metadata.assetTypeDetails = io->ReadDword();
    metadata.category = io->ReadEnum<AssetSubcategory>(4);
    metadata.skeletonVersion = Nxe;

    // set the endian back
    io->SetEndian(byteOrder);
}

void AvatarAsset::parseMetadata(u32 pos)
{
    // seek to the position of the metadata
    io->SetPosition(pos);

    switch (io->ReadByte())
    {
        case 1:
            parseMetadataLegacyV1(pos + 1);
            return;
        case 2:
            // preserve the endian
            EndianType byteOrder = io->GetEndian();
            io->SetEndian(LittleEndian);

            // read in the metadata
            metadata.gender = io->ReadEnum<AssetGender>(1);
            metadata.type = io->ReadEnum<BinaryAssetType>(4);
            metadata.assetTypeDetails = io->ReadDword();
            metadata.category = io->ReadEnum<AssetSubcategory>(4);
            metadata.skeletonVersion = io->ReadEnum<SkeletonVersion>(1);

            // set the endian back
            io->SetEndian(byteOrder);
            return;
    }
}

void AvatarAsset::parseColorTable(u32 pos)
{
    // seek to the color table position
    io->SetPosition(pos);

    // preserve the endian
    EndianType byteOrder = io->GetEndian();
    io->SetEndian(LittleEndian);

    // read the entry count
    customColors.count = io->ReadDword();

    // allocate enough memory for all the entries
    customColors.entries.resize(customColors.count);

    // read the entries
    for (u32 i = 0; i < customColors.count; i++)
    {
        customColors.entries[i].color1.color.blue = io->ReadByte();
        customColors.entries[i].color1.color.green = io->ReadByte();
        customColors.entries[i].color1.color.red = io->ReadByte();
        customColors.entries[i].color1.color.alpha = io->ReadByte();
        customColors.entries[i].color1.unknown = io->ReadDword();

        customColors.entries[i].color2.color.blue = io->ReadByte();
        customColors.entries[i].color2.color.green = io->ReadByte();
        customColors.entries[i].color2.color.red = io->ReadByte();
        customColors.entries[i].color2.color.alpha = io->ReadByte();
        customColors.entries[i].color2.unknown = io->ReadDword();

        customColors.entries[i].color3.color.blue = io->ReadByte();
        customColors.entries[i].color3.color.green = io->ReadByte();
        customColors.entries[i].color3.color.red = io->ReadByte();
        customColors.entries[i].color3.color.alpha = io->ReadByte();
        customColors.entries[i].color3.unknown = io->ReadDword();
    }

    // set the endian back
    io->SetEndian(byteOrder);
}

void AvatarAsset::ReadBlockData(STRBBlock *block)
{
    // allocated enough memory for the data
    block->data.resize(block->dataLength);

    // seek to the position of the data
    io->SetPosition(block->dataAddress);

    // read in the block data
    io->ReadBytes(block->data.data(), block->dataLength);
}

void AvatarAsset::readAnimationInfo(u32 pos)
{
    // seek to the position of the animation information
    io->SetPosition(pos);

    // preserve the endian
    EndianType byteOrder = io->GetEndian();
    io->SetEndian(LittleEndian);

    // read the frame count
    animation.frameCount = io->ReadDword();

    // read the fps
    animation.framesPerSecond =  io->ReadFloat();

    // round dat, yo
    animation.framesPerSecond = (float)(((int)((animation.framesPerSecond + .05f) * 100)) / 100);

    // calculate the duration of the animation
    animation.duration = (float)animation.frameCount / animation.framesPerSecond;

    io->SetEndian(byteOrder);
}

u32 AvatarAsset::roundUpToBlockAlignment(u32 valueToRound)
{
    if (((header.blockAlignment & (header.blockAlignment - 1)) != 0) || (header.blockAlignment == 0))
        throw string("STRB: Error when rounding.\n");
    int blockAlignment = header.blockAlignment;
    return (((valueToRound + blockAlignment) - 1) & ~(blockAlignment - 1));
}

ColorTable AvatarAsset::GetCustomColorTable()
{
    if (customColors.entries.empty())
        throw string("Asset: No color table found for asset.\n");

    return customColors;
}

struct Animation AvatarAsset::GetAnimation()
{
    if (animation.frameCount == 0)
        throw string("Asset: No animation found for asset.\n");

    return animation;
}

AssetMetadata AvatarAsset::GetAssetMetadata()
{
    // if the metadata doesn't exist then we need to throw an error
    if (metadata.gender == 0)
        throw string("Asset: No metadata available for asset.\n");

    return metadata;
}

AvatarAsset::~AvatarAsset(void)
{
    // cleanup the io
    if (ioOwned)
    {
        ioOwned->Close();
    }
}


