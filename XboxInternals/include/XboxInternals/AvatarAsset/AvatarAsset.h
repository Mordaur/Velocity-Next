#pragma once

#include <XboxInternals/IO/FileIO.h>
#include <XboxInternals/AvatarAsset/AvatarAssetDefinitions.h>
#include <iostream>
#include <vector>

#include <XboxInternals/Export.h>

using std::string;
using std::vector;

class XBOXINTERNALS_EXPORT AvatarAsset
{
public:
    AvatarAsset(string assetPath);
    AvatarAsset(FileIO *io);
    ~AvatarAsset(void);

    vector<STRBBlock> blocks;

    // Description: retrieve the metadata that describes the asset
    AssetMetadata GetAssetMetadata();

    // Description: retrieve the custom color table
    ColorTable GetCustomColorTable();

    // Description: retrieve the animation info for the asset
    struct Animation GetAnimation();

    // Description: read the block's data in
    void ReadBlockData(STRBBlock *block);

private:
    std::unique_ptr<FileIO> ioOwned;
    FileIO *io;
    STRBHeader header;
    AssetMetadata metadata;
    struct Animation animation; // TODO: Parse animation data
    ColorTable customColors;

    // Description: read the STRB header
    void readHeader();

    // Description: read in all of the block information except for the actual data
    void readBlocks();

    // Description: parse v1 asset metadata
    void parseMetadataLegacyV1(u32 pos);

    // Description: parse either version of the metadata
    void parseMetadata(u32 pos);

    // Description: parse a color table
    void parseColorTable(u32 pos);

    // Description: read some of the basic animation information
    void readAnimationInfo(u32 pos);

    // Description: round the number to a block boundry
    u32 roundUpToBlockAlignment(u32 valueToRound);
};



