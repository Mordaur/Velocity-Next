#include <XboxInternals/Xex/Xex.h>
#include <XboxInternals/Xex/XexLzxDecoder.h>
#include <XboxInternals/IO/XexZeroBasedCompressionIO.h>
#include <XboxInternals/IO/XexAesIO.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    // Ratings are single bytes in the file and the enums have gaps. Reading an
    // arbitrary byte into the enum and then comparing it is UB, so validate
    // against the known values and fall back to the "unrated" sentinel.
    template <typename Enum>
    Enum RatingOrUnrated(BYTE value, std::initializer_list<Enum> valid, Enum unrated)
    {
        for (Enum candidate : valid)
        {
            if (static_cast<BYTE>(candidate) == value)
                return candidate;
        }
        return unrated;
    }
}

Xex::Xex(BaseIO *io) :
    io(io), firstResourceFileAddr(0xFFFFFFFF),
    esrbRating(ESRB_RP), pegiRating(PEGI_Unrated), pegifiRating(PEGIFI_Unrated),
    pegiptRating(PEGIPT_Unrated), pegibbfcRating(PEGIBBF_Unrated),
    oflcAURating(OFLCAU_UNRATED), oflcNZRating(OFLCNZ_UNRATED),
    imageBaseAddress(0), entryPoint(0), originalBaseAddress(0), defaultStackSize(0),
    defaultFileSystemCacheSize(0), defaultHeapSize(0), titleWorkspaceSize(0),
    encrypted(false), headerHashValid(false), encryptionState(Decrypted), compressionState(XexDecompressed),
    compressedBlockSize(0), compressedBlock0Size(0), compressedBlock0Sha1()
{
    Parse();
}

Xex::Xex(std::string fileName) :
    io(nullptr), ownedIO(std::make_unique<FileIO>(fileName)), firstResourceFileAddr(0xFFFFFFFF),
    esrbRating(ESRB_RP), pegiRating(PEGI_Unrated), pegifiRating(PEGIFI_Unrated),
    pegiptRating(PEGIPT_Unrated), pegibbfcRating(PEGIBBF_Unrated),
    oflcAURating(OFLCAU_UNRATED), oflcNZRating(OFLCNZ_UNRATED),
    imageBaseAddress(0), entryPoint(0), originalBaseAddress(0), defaultStackSize(0),
    defaultFileSystemCacheSize(0), defaultHeapSize(0), titleWorkspaceSize(0),
    encrypted(false), headerHashValid(false), encryptionState(Decrypted), compressionState(XexDecompressed),
    compressedBlockSize(0), compressedBlock0Size(0), compressedBlock0Sha1()
{
    io = ownedIO.get();

    Parse();
}

Xex::~Xex()
{
    rawDataIO.reset();

    if (!rawDataPath.empty())
        std::remove(rawDataPath.c_str());
}

std::vector<std::string> Xex::GetSystemImportLibraries() const
{
    return systemImportLibraries;
}

std::vector<XexStaticLibraryInfo> Xex::GetStaticLibraries() const
{
    return staticLibraries;
}

std::vector<XexResourceFileEntry> Xex::GetResourceFileInfo() const
{
    return resourceFiles;
}

DWORD Xex::GetModuleFlags() const
{
    return header.moduleFlags;
}

ESRBRating Xex::GetEsrbRating() const
{
    return esrbRating;
}

std::string Xex::GetEsrbRatingText() const
{
    switch (esrbRating)
    {
        case ESRB_EC:
            return "EC";
        case ESRB_E:
            return "E";
        case ESRB_10:
            return "E10";
        case ESRB_T:
            return "T";
        case ESRB_M:
            return "M";
        default:
            return "RP";
    }
}

PEGIRating Xex::GetPegiRating() const
{
    return pegiRating;
}

std::string Xex::GetPegiRatingText() const
{
    switch (pegiRating)
    {
        case PEGI_3:
            return "3";
        case PEGI_7:
            return "7";
        case PEGI_12:
            return "12";
        case PEGI_16:
            return "16";
        case PEGI_18:
            return "18";
        default:
            return "Unrated";
    }
}

OFLCAURating Xex::GetOflcAURating() const
{
    return oflcAURating;
}

std::string Xex::GetOflcAURatingText() const
{
    switch (oflcAURating)
    {
        case OFLCAU_G:
            return "G";
        case OFLCAU_PG:
            return "PG";
        case OFLCAU_M:
            return "M";
        case OFLCAU_MA15_PLUS:
            return "MA15";
        default:
            return "Unrated";
    }
}

OFLCNZRating Xex::GetOflcNZRating() const
{
    return oflcNZRating;
}

std::string Xex::GetOflcNZRatingText() const
{
    switch (oflcNZRating)
    {
        case OFLCNZ_G:
            return "G";
        case OFLCNZ_PG:
            return "PG";
        case OFLCNZ_M:
            return "M";
        case OFLCNZ_MA15_PLUS:
            return "MA15";
        case OFLCNZ_R16:
            return "R16";
        default:
            return "Unrated";
    }
}

std::string Xex::GetOriginalPEImageName() const
{
    return originalPEImageName;
}

DWORD Xex::GetImageBaseAddress() const
{
    return imageBaseAddress;
}

DWORD Xex::GetEntryPoint() const
{
    return entryPoint;
}

DWORD Xex::GetOriginalBaseAddress() const
{
    return originalBaseAddress;
}

DWORD Xex::GetDefaultStackSize() const
{
    return defaultStackSize;
}

DWORD Xex::GetDefaultFileSystemCacheSize() const
{
    return defaultFileSystemCacheSize;
}

DWORD Xex::GetDefaultHeapSize() const
{
    return defaultHeapSize;
}

DWORD Xex::GetTitleWorkspaceSize() const
{
    return titleWorkspaceSize;
}

DWORD Xex::GetAllowedMediaTypes() const
{
    return securityInfo.allowedMediaTypes;
}

void Xex::ExtractBaseImageFile(std::string outPath)
{
    BaseIO *rawDataIO = GetRawDataIO();
    const UINT64 dataStart = header.dataAddress;
    const UINT64 rawLength = rawDataIO->Length();
    const UINT64 available = rawLength > dataStart ? rawLength - dataStart : 0;

    // The logical image size is authoritative; never read past the raw data.
    UINT64 baseImageSize = securityInfo.imageSize;
    if (baseImageSize == 0 || baseImageSize > available)
        baseImageSize = available;

    // if there are resources then the PE file runs right up until the first resource
    if (firstResourceFileAddr != 0xFFFFFFFF)
    {
        const UINT64 imageBase = GetImageBaseAddress();
        if (firstResourceFileAddr < imageBase)
            throw std::string("XEX: Resource address is below the image base.");
        const UINT64 firstResourceFileDataOffset = firstResourceFileAddr - imageBase;
        if (firstResourceFileDataOffset > baseImageSize)
            throw std::string("XEX: Resource address is outside the image.");
        baseImageSize = firstResourceFileDataOffset;
    }

    if (baseImageSize > UINT32_MAX)
        throw std::string("XEX: Base image is too large to extract.");

    ExtractFromRawData(outPath, 0, static_cast<DWORD>(baseImageSize));
}

void Xex::ExtractResource(std::string resourceName, std::string outPath)
{
    // get the resource info
    XexResourceFileEntry entry;
    bool found = false;
    for (size_t i = 0; i < resourceFiles.size(); i++)
    {
        XexResourceFileEntry curEntry = resourceFiles.at(i);
        if (curEntry.name == resourceName)
        {
            found = true;
            entry = curEntry;
        }
    }

    if (!found)
        throw std::string("XEX: Couldn't find resource to extract.");

    const UINT64 imageBase = GetImageBaseAddress();
    if (entry.address < imageBase)
        throw std::string("XEX: Resource address is below the image base.");
    const UINT64 resourceOffset = entry.address - imageBase;

    const UINT64 dataStart = header.dataAddress;
    const UINT64 rawLength = GetRawDataIO()->Length();
    const UINT64 available = rawLength > dataStart ? rawLength - dataStart : 0;
    if (resourceOffset > available || entry.size > available - resourceOffset ||
        resourceOffset > UINT32_MAX || entry.size > UINT32_MAX)
        throw std::string("XEX: Resource is outside the image.");

    ExtractFromRawData(outPath, static_cast<DWORD>(resourceOffset), entry.size);
}

void Xex::ExtractData(std::string path)
{
    if (compressionState == XexDecompressed && IsEncrypted())
        ExtractDecryptedData(path);
    else if (compressionState == XexCompressed)
        ExtractDecompressedData(path);
    else if (compressionState == XexSupercompressed)
        ExtractCompressedData(path);
    else if (compressionState == XexDeltaCompressed)
        throw std::string("XEX: Delta compression (type 3) is not supported yet.\n");
    else if (compressionState == XexDecompressed)
    {
        // Plain, unencrypted image: copy the raw bytes after the header. The
        // old ExtractData2 handled this case; keep it so the alias does not
        // silently return without writing a file.
        const UINT64 dataStart = header.dataAddress;
        const UINT64 fileLength = io->Length();
        if (dataStart > fileLength)
            throw std::string("XEX: Data starts past the end of the file.\n");

        const UINT64 dataSize = fileLength - dataStart;
        if (dataSize > UINT32_MAX)
            throw std::string("XEX: Extracted data is too large.\n");

        ExtractFromRawData(path, 0, static_cast<DWORD>(dataSize));
    }
}

void Xex::ExtractDecryptedData(std::string path) const
{
    BYTE bufferEnc[XEX_AES_BLOCK_SIZE];
    BYTE bufferDec[XEX_AES_BLOCK_SIZE];

    // set up the cipher
    auto aes = Botan::BlockCipher::create("AES-128");
    aes->set_key(decryptedKey, XEX_AES_BLOCK_SIZE);

    // seek to the beginning of the data
    io->SetPosition(header.dataAddress);

    // calculate how many blocks there are
    DWORD blockCount = (io->Length() - header.dataAddress) / XEX_AES_BLOCK_SIZE;

    // open an out file
    FileIO outFile(path, true);

    // decrypt the data (AES-128 cbc)
    BYTE initializationVector[XEX_AES_BLOCK_SIZE] = {0};
    for (DWORD i = 0; i < blockCount; i++)
    {
        io->ReadBytes(bufferEnc, XEX_AES_BLOCK_SIZE);
        AesCbcDecrypt(aes.get(), initializationVector, bufferEnc, bufferDec);

        outFile.Write(bufferDec, XEX_AES_BLOCK_SIZE);
    }

    outFile.Close();
}

void Xex::ExtractDecompressedData(std::string path)
{
    std::vector<BYTE> copyBuffer(XEX_COPY_BUFFER_SIZE);
    FileIO outFile(path, true);

    // if it's encrypted we'll have to decrypt it
    auto aes = Botan::BlockCipher::create("AES-128");
    BYTE iv[XEX_AES_BLOCK_SIZE] = {0};
    aes->set_key(decryptedKey, XEX_AES_BLOCK_SIZE);

    // seek to the beginning of the data
    io->SetPosition(header.dataAddress);

    UINT64 produced = 0;

    // decompress the data
    for (DWORD i = 0; i < compressionBlocks.size(); i++)
    {
        XexCompressionBlock block = compressionBlocks.at(i);

        // Check the whole run before writing anything. A corrupt table could
        // otherwise make us write up to 4 GiB per entry before the final size
        // check, and a DWORD sum could wrap past imageSize. The kernel clamps
        // an overrunning run instead; rejecting the table is deliberate.
        const UINT64 blockTotal = static_cast<UINT64>(block.size) + block.nullSize;
        if (blockTotal > static_cast<UINT64>(securityInfo.imageSize) - produced)
            throw std::string("XEX: The compression block table exceeds the image size.\n");

        // XeCryptAesCbc decrypts only len>>4 blocks and leaves a partial tail
        // as ciphertext; the loader carries the IV across chunks and runs but
        // never carries partial blocks. We reject a non-block-aligned run
        // instead of writing ciphertext into the extracted image. Retail runs
        // are page-aligned, so this only affects hand-built files.
        if (IsEncrypted() && block.size % XEX_AES_BLOCK_SIZE != 0)
            throw std::string("XEX: Encrypted type-1 run is not AES-block aligned.\n");

        // determine how many copy iterations it will take
        DWORD copyIterations = block.size / XEX_COPY_BUFFER_SIZE;
        if (block.size % XEX_COPY_BUFFER_SIZE != 0)
            copyIterations++;

        // copy over the data
        for (DWORD x = 0; x < copyIterations; x++)
        {
            // determine the amount of bytes to copy
            DWORD bytesToCopy = XEX_COPY_BUFFER_SIZE;
            if (x + 1 == copyIterations && block.size % XEX_COPY_BUFFER_SIZE != 0)
                bytesToCopy = block.size % XEX_COPY_BUFFER_SIZE;

            // read in the data
            io->ReadBytes(copyBuffer.data(), bytesToCopy);

            // decrypt it if necessary; the IV stays chained across buffers
            if (IsEncrypted())
            {
                DWORD aesBlocksInBuffer = bytesToCopy / XEX_AES_BLOCK_SIZE;
                for (DWORD y = 0; y < aesBlocksInBuffer; y++)
                {
                    BYTE *currentAesBlock = copyBuffer.data() + y * XEX_AES_BLOCK_SIZE;
                    AesCbcDecrypt(aes.get(), iv, currentAesBlock, currentAesBlock);
                }
            }

            outFile.WriteBytes(copyBuffer.data(), bytesToCopy);
        }

        // calculate the copy iterations for the null data
        std::fill(copyBuffer.begin(), copyBuffer.end(), 0);
        copyIterations = block.nullSize / XEX_COPY_BUFFER_SIZE;
        if (block.nullSize % XEX_COPY_BUFFER_SIZE != 0)
            copyIterations++;

        // write all the null data
        for (DWORD x = 0; x < copyIterations; x++)
        {
            // determine the amount of bytes to copy
            DWORD bytesToCopy = XEX_COPY_BUFFER_SIZE;
            if (x + 1 == copyIterations && block.nullSize % XEX_COPY_BUFFER_SIZE != 0)
                bytesToCopy = block.nullSize % XEX_COPY_BUFFER_SIZE;

            outFile.WriteBytes(copyBuffer.data(), bytesToCopy);
        }

        produced += block.size;
        produced += block.nullSize;
    }

    // The block table only covers the initialised part of the image; the
    // kernel zeroes the rest of the destination (XexpLoadRawImage). Title
    // images can stop short of imageSize (Default.xex: 32 KiB).
    if (produced > securityInfo.imageSize)
        throw std::string("XEX: The compression block table exceeds the image size.\n");

    if (produced < securityInfo.imageSize)
    {
        std::fill(copyBuffer.begin(), copyBuffer.end(), 0);
        UINT64 remaining = securityInfo.imageSize - produced;
        while (remaining > 0)
        {
            const DWORD chunk = static_cast<DWORD>(
                    std::min<UINT64>(remaining, copyBuffer.size()));
            outFile.WriteBytes(copyBuffer.data(), chunk);
            remaining -= chunk;
        }
    }

    outFile.Close();
}

void Xex::ExtractData2(std::string path)
{
    // Legacy alias. Keep one extraction path so the size/bounds checks cannot
    // diverge between ExtractData and ExtractData2.
    ExtractData(std::move(path));
}

bool Xex::HasRegion(XexRegion region)
{
    return !!(securityInfo.regions & region);
}

void Xex::SetRegion(XexRegion region)
{
    securityInfo.regions |= region;
}

DWORD Xex::GetImageFlags() const
{
    return securityInfo.imageFlags;
}

DWORD Xex::GetImageSize() const
{
    return securityInfo.imageSize;
}

BYTE *Xex::GetMediaID()
{
    return securityInfo.mediaID;
}

BYTE *Xex::GetKey()
{
    return securityInfo.key;
}

BYTE *Xex::GetHeaderHash()
{
    return securityInfo.headerHash;
}

bool Xex::IsHeaderHashValid() const
{
    return headerHashValid;
}

BYTE *Xex::GetLANKey()
{
    return lanKey;
}

DWORD Xex::GetTitleID() const
{
    return executionInfo.titleID;
}

XexEncryptionState Xex::GetEncryptionState() const
{
    return encryptionState;
}

std::string Xex::GetEncryptionStateStr() const
{
    switch (encryptionState)
    {
        case RetailEncrypted:
            return "Retail Encrypted";
        case DevKitEncrypted:
            return "DevKit Encrypted";
        case Decrypted:
            return "Decrypted";
        default:
            throw std::string("XEX: Unknown encryption state");
    }
}

bool Xex::IsEncrypted() const
{
    return encrypted;
}

XexCompressionState Xex::GetCompressionState() const
{
    return compressionState;
}

std::string Xex::GetCompressionStateStr() const
{
    switch (compressionState)
    {
        case XexCompressed:
            return "Compressed";
        case XexSupercompressed:
            return "Super compressed";
        case XexDeltaCompressed:
            return "Delta compressed";
        case XexDecompressed:
            return "Decompressed";
        default:
            throw std::string("XEX: Invalid compression state.");
    }
}

void Xex::AesCbcDecrypt(Botan::BlockCipher *aes, BYTE *initializationVector, const BYTE *bufferEnc, BYTE *bufferDec) const
{
    BYTE encCopy[XEX_AES_BLOCK_SIZE];
    memcpy(encCopy, bufferEnc, XEX_AES_BLOCK_SIZE);

    // Copy to output buffer first, then decrypt in-place
    memcpy(bufferDec, bufferEnc, XEX_AES_BLOCK_SIZE);
    aes->decrypt(bufferDec);

    for (DWORD x = 0; x < XEX_AES_BLOCK_SIZE; x++)
    {
        // xor the data with the old IV
        bufferDec[x] ^= initializationVector[x];

        // update the IV
        initializationVector[x] = encCopy[x];
    }
}

void Xex::Parse()
{
    io->SetPosition(0);
    io->SetEndian(BigEndian);

    // read the header
    header.magic = io->ReadDword();
    if (header.magic != XEX_HEADER_MAGIC)
        throw std::string("XEX: Invalid header magic.");
    header.moduleFlags = io->ReadDword();
    header.dataAddress = io->ReadDword();
    header.reserved = io->ReadDword();
    header.headerAddress = io->ReadDword();
    header.optionalHeaderEntryCount = io->ReadDword();

    // read in all the optional header entries
    for (DWORD i = 0; i < header.optionalHeaderEntryCount; i++)
        ParseOptionalHeaderEntry(i);

    // read the security info
    const UINT64 fileLength = io->Length();
    if (header.headerAddress >= header.dataAddress ||
        static_cast<UINT64>(header.headerAddress) + 0x184 > fileLength)
        throw std::string("XEX: Invalid security header offset.");

    io->SetPosition(header.headerAddress);
    securityInfo.size = io->ReadDword();
    securityInfo.imageSize = io->ReadDword();
    io->ReadBytes(securityInfo.pirsRsaSignature, 0x100);
    securityInfo.imageInfoSize = io->ReadDword();
    securityInfo.imageFlags = io->ReadDword();
    securityInfo.loadAddress = io->ReadDword();
    io->ReadBytes(securityInfo.sectionHash, 0x14);
    securityInfo.importTableSize = io->ReadDword();
    io->ReadBytes(securityInfo.importTableHash, 0x14);
    io->ReadBytes(securityInfo.mediaID, 0x10);
    io->ReadBytes(securityInfo.key, XEX_AES_BLOCK_SIZE);
    securityInfo.exportTableSize = io->ReadDword();
    io->ReadBytes(securityInfo.headerHash, 0x14);
    securityInfo.regions = io->ReadDword();
    securityInfo.allowedMediaTypes = io->ReadDword();

    // XexpVerifyXexHeaders. The header hash does not depend on the image key,
    // so it also verifies images whose data uses an unsupported key.
    headerHashValid = VerifyHeaderHash();

    // Always try to determine which encryption key is used
    // Note: Some XEX files have encrypted=0 in BaseFileDescriptor but are still encrypted!
    const BYTE *key = XEX_RETAIL_KEY;

    // preserve the position in the file, the key probes change it
    UINT64 position = io->GetPosition();

    if (compressionState == XexSupercompressed)
    {
        // A compressed image does not start with a DOS header, so the MZ probe
        // below cannot work: identify the key with the signed digest of the
        // first compressed block instead (see SelectCompressedImageKey).
        key = SelectCompressedImageKey();
    }
    else if (TryKey(XEX_RETAIL_KEY))
    {
        encryptionState = RetailEncrypted;
        key = XEX_RETAIL_KEY;
    }
    else if (TryKey(XEX_DEVKIT_KEY))
    {
        encryptionState = DevKitEncrypted;
        key = XEX_DEVKIT_KEY;
    }
    else
    {
        // Neither key worked - might be unencrypted or use a different encryption
        encryptionState = Decrypted;
        // Keep key as RETAIL_KEY for the session key decryption below
    }

    io->SetPosition(position);

    // decrypt the key in the file (always done, even if data isn't encrypted)
    auto aes = Botan::BlockCipher::create("AES-128");
    aes->set_key(key, XEX_AES_BLOCK_SIZE);
    memcpy(decryptedKey, securityInfo.key, XEX_AES_BLOCK_SIZE);
    aes->decrypt(decryptedKey);

    // determine the page size
    if (securityInfo.imageFlags & PageSize4KB)
        pageSize = 0x1000;
    else
        pageSize = 0x10000;

    // read the sections
    DWORD sectionCount = io->ReadDword();
    for (DWORD i = 0; i < sectionCount; i++)
    {
        XexSectionEntry entry;

        // the bottom 4 bits are the type, the rest are the size of the entry in pages
        DWORD entryInfo = io->ReadDword();

        const DWORD sectionType = entryInfo & 0x0F;
        entry.type = (sectionType >= XexSectionCode && sectionType <= XexSectionReadOnlyData)
                         ? static_cast<XexSectionType>(sectionType)
                         : XexSectionUnknown;
        entry.totalSize = (entryInfo >> 4) * pageSize;

        io->ReadBytes(entry.hash, 0x14);
        sections.push_back(entry);
    }
}

void Xex::ParseOptionalHeaderEntry(int index)
{
    // seek to the beginning of the entry
    io->SetPosition(XEX_HEADER_SIZE + index * XEX_OPTIONAL_HEADER_ENTRY_SIZE);

    // Switch on the raw id: casting an arbitrary file value to the enum would
    // create an out-of-range enumerator (UB) for unknown optional headers.
    const DWORD headerId = io->ReadDword();
    const DWORD headerData = io->ReadDword();

    switch (headerId)
    {
        case SystemImportLibraries:
            ParseSystemImportLibraryTable(headerData);
            break;
        case RatingInformation:
            ParseRatingInformation(headerData);
            break;
        case StaticLibraries:
            ParseStaticLibraryTable(headerData);
            break;
        case OriginalPEImageName:
            ParseOriginalPEImageName(headerData);
            break;
        case ResourceInfo:
            ParseResourceFileTable(headerData);
            break;
        case ChecksumInformation:
        case TLSData:
        case PageHeapData:
        case CallcapInformation:
        case FastcapInformation:
        case Xbox360Logo:
            // These optional headers are not currently processed
            break;
        case BaseFileDescriptor:
            ParseBaseFileDescriptor(headerData);
            break;
        case ExecutionInfo:
            ParseExecutionInfo(headerData);
            break;
        case LANKey:
            ParseLANKey(headerData);
            break;
        case ImageBaseAddress:
            imageBaseAddress = headerData;
            break;
        case EntryPoint:
            entryPoint = headerData;
            break;
        case OriginalBaseAddress:
            originalBaseAddress = headerData;
            break;
        case DefaultStackSize:
            defaultStackSize = headerData;
            break;
        case DefaultFileSystemCacheSize:
            defaultFileSystemCacheSize = headerData;
            break;
        case DefaultHeapSize:
            defaultHeapSize = headerData;
            break;
        case TitleWorkspaceSize:
            titleWorkspaceSize = headerData;
            break;
    }
}

void Xex::ParseSystemImportLibraryTable(DWORD address)
{
    io->SetPosition(address);

    // unknown
    io->ReadDword();

    /*DWORD totalLength = */ io->ReadDword();
    DWORD importLibraryCount = io->ReadDword();

    // read the import library names
    for (DWORD i = 0; i < importLibraryCount; i++)
        systemImportLibraries.push_back(io->ReadString());
}

void Xex::ParseRatingInformation(DWORD address)
{
    io->SetPosition(address);

    esrbRating = RatingOrUnrated<ESRBRating>(io->ReadByte(),
            { ESRB_EC, ESRB_E, ESRB_10, ESRB_T, ESRB_M, ESRB_RP }, ESRB_RP);
    pegiRating = RatingOrUnrated<PEGIRating>(io->ReadByte(),
            { PEGI_3, PEGI_7, PEGI_12, PEGI_16, PEGI_18, PEGI_Unrated }, PEGI_Unrated);
    pegifiRating = RatingOrUnrated<PEGIFIRating>(io->ReadByte(),
            { PEGIFI_3, PEGIFI_7, PEGIFI_11, PEGIFI_15, PEGIFI_18, PEGIFI_Unrated }, PEGIFI_Unrated);
    pegiptRating = RatingOrUnrated<PEGIPTRating>(io->ReadByte(),
            { PEGIPT_4, PEGIPT_6, PEGIPT_12, PEGIPT_16, PEGIPT_18, PEGIPT_Unrated }, PEGIPT_Unrated);
    pegibbfcRating = RatingOrUnrated<PEGIBBFCRating>(io->ReadByte(),
            { PEGIBBFC_3, PEGIBBFC_4, PEGIBBFC_7, PEGIBBFC_8, PEGIBBFC_12, PEGIBBFC_15,
              PEGIBBFC_16, PEGIBBFC_18, PEGIBBF_Unrated }, PEGIBBF_Unrated);

    /*Cero*/	io->ReadByte();
    /*USK*/		io->ReadByte();

    oflcAURating = RatingOrUnrated<OFLCAURating>(io->ReadByte(),
            { OFLCAU_G, OFLCAU_PG, OFLCAU_M, OFLCAU_MA15_PLUS, OFLCAU_UNRATED }, OFLCAU_UNRATED);
    oflcNZRating = RatingOrUnrated<OFLCNZRating>(io->ReadByte(),
            { OFLCNZ_G, OFLCNZ_PG, OFLCNZ_M, OFLCNZ_MA15_PLUS, OFLCNZ_R16, OFLCNZ_UNRATED },
            OFLCNZ_UNRATED);

    // there are more
}

void Xex::ParseStaticLibraryTable(DWORD address)
{
    io->SetPosition(address);

    DWORD staticLibraryStructSize = io->ReadDword();
    if (staticLibraryStructSize < 4)
        return;
    DWORD libraryCount = (staticLibraryStructSize - 4) / XEX_STATIC_LIBRARY_ENTRY_SIZE;

    for (DWORD i = 0; i < libraryCount; i++)
    {
        XexStaticLibraryInfo staticLibInfo;
        staticLibInfo.name = io->ReadString(8);

        staticLibInfo.version.major = io->ReadWord();
        staticLibInfo.version.minor = io->ReadWord();
        staticLibInfo.version.build = io->ReadWord();
        staticLibInfo.version.revision = io->ReadWord();

        staticLibraries.push_back(staticLibInfo);
    }
}

void Xex::ParseOriginalPEImageName(DWORD address)
{
    // seek past the length at the beginning
    io->SetPosition(address + 4);

    originalPEImageName = io->ReadString();
}

void Xex::ParseResourceFileTable(DWORD address)
{
    io->SetPosition(address);
    DWORD tableSize = io->ReadDword();
    if (tableSize < 4)
        return;
    DWORD entryCount = (tableSize - 4) / XEX_RESOURCE_FILE_ENTRY_SIZE;

    for (DWORD i = 0; i < entryCount; i++)
    {
        // read the info from the file table
        XexResourceFileEntry entry;
        entry.name = io->ReadString(8);
        entry.address = io->ReadDword();
        entry.size = io->ReadDword();

        if (entry.address < firstResourceFileAddr)
            firstResourceFileAddr = entry.address;

        resourceFiles.push_back(entry);
    }
}

void Xex::ParseBaseFileDescriptor(DWORD address)
{
    io->SetPosition(address);

    DWORD baseFileDescriptorSize = io->ReadDword();
    if (baseFileDescriptorSize < 8)
        throw std::string("XEX: Invalid base file descriptor size.\n");

    encrypted = !!(io->ReadWord());
    const WORD compression = io->ReadWord();
    if (compression > XexDeltaCompressed)
        throw std::string("XEX: Unknown compression state.\n");
    compressionState = static_cast<XexCompressionState>(compression);

    // Type 1 (raw/zero-based) uses 8-byte blocks. Type 2 uses a digest-chained
    // 24-byte layout whose per-block sizes/digests live in the blocks
    // themselves; only the first block is described here. Type 3 is not
    // supported and is left unparsed.
    if (compressionState == XexCompressed)
    {
        if (baseFileDescriptorSize < 0x10 ||
            (baseFileDescriptorSize - 8) % XEX_COMPRESSION_BLOCK_SIZE != 0)
            throw std::string("XEX: Invalid compression block table.\n");

        // XexpLoadRawImage requires the descriptor to end in a zero dword and
        // stops at the first block whose nullSize is zero; trailing entries are
        // not consumed.
        const UINT64 descriptorEnd = static_cast<UINT64>(address) + baseFileDescriptorSize;
        if (descriptorEnd > io->Length())
            throw std::string("XEX: Compression block table is outside the file.\n");

        io->SetPosition(static_cast<DWORD>(descriptorEnd - 4));
        if (io->ReadDword() != 0)
            throw std::string("XEX: Compression block table has no terminator.\n");

        io->SetPosition(address + 8);
        const DWORD blockCount = (baseFileDescriptorSize - 8) / XEX_COMPRESSION_BLOCK_SIZE;
        for (DWORD i = 0; i < blockCount; i++)
        {
            XexCompressionBlock block;
            block.size = io->ReadDword();
            block.nullSize = io->ReadDword();

            compressionBlocks.push_back(block);
            if (block.nullSize == 0)
                break;
        }
    }
    else if (compressionState == XexSupercompressed)
    {
        if (baseFileDescriptorSize < 0x24)
            throw std::string("XEX: Invalid LZX base file descriptor size.\n");

        compressedBlockSize = io->ReadDword();
        compressedBlock0Size = io->ReadDword();
        io->ReadBytes(compressedBlock0Sha1, sizeof(compressedBlock0Sha1));

        // the kernel requires a power of two here (XexpLoadCompressedImage)
        if (compressedBlockSize == 0 || (compressedBlockSize & (compressedBlockSize - 1)) != 0)
            throw std::string("XEX: Invalid LZX block size.\n");

        if (compressedBlock0Size < 0x18)
            throw std::string("XEX: Invalid LZX first block size.\n");
    }
}

void Xex::ParseLANKey(DWORD address)
{
    io->SetPosition(address);
    io->ReadBytes(lanKey, XEX_LAN_KEY_SIZE);
}

void Xex::ParseExecutionInfo(DWORD address)
{
    io->SetPosition(address);

    executionInfo.mediaID = io->ReadDword();
    executionInfo.version = io->ReadDword();
    executionInfo.baseVersion = io->ReadDword();
    executionInfo.titleID = io->ReadDword();
    executionInfo.executionTable = io->ReadByte();
    executionInfo.platform = io->ReadByte();
    executionInfo.discNumber = io->ReadByte();
    executionInfo.discCount = io->ReadByte();
    executionInfo.savegameID = io->ReadDword();
}

void Xex::ExtractFromRawData(std::string outPath, DWORD address, DWORD size)
{
    BaseIO *rawDataIO = GetRawDataIO();
    DWORD offset = rawDataIO->GetPosition();

    rawDataIO->SetPosition(offset + address);

    // extract the data
    std::vector<BYTE> copyBuffer(XEX_COPY_BUFFER_SIZE);

    // determine how many copy iterations it will take
    DWORD copyIterations = size / XEX_COPY_BUFFER_SIZE;
    if (size % XEX_COPY_BUFFER_SIZE != 0)
        copyIterations++;

    FileIO outFile(outPath, true);
    for (DWORD i = 0; i < copyIterations; i++)
    {
        // determine the amount of bytes to copy
        DWORD bytesToCopy = XEX_COPY_BUFFER_SIZE;
        if (i + 1 == copyIterations && size % XEX_COPY_BUFFER_SIZE != 0)
            bytesToCopy = size % XEX_COPY_BUFFER_SIZE;

    rawDataIO->ReadBytes(copyBuffer.data(), bytesToCopy);
    outFile.WriteBytes(copyBuffer.data(), bytesToCopy);
    }

    outFile.Close();
}

bool Xex::TryKey(const BYTE *key)
{
    auto aes = Botan::BlockCipher::create("AES-128");
    BYTE decryptedKeyBuffer[XEX_AES_BLOCK_SIZE];

    // First decrypt the key from the XEX header
    aes->set_key(key, XEX_AES_BLOCK_SIZE);
    memcpy(decryptedKeyBuffer, securityInfo.key, XEX_AES_BLOCK_SIZE);
    aes->decrypt(decryptedKeyBuffer);

    // Now set up AES with the decrypted key
    aes->set_key(decryptedKeyBuffer, XEX_AES_BLOCK_SIZE);

    BYTE iv[XEX_AES_BLOCK_SIZE] = {0};
    BYTE encryptedBuffer[XEX_AES_BLOCK_SIZE] = {0};
    BYTE decryptedBuffer[XEX_AES_BLOCK_SIZE] = {0};

    // read in the first block
    io->SetPosition(header.dataAddress);
    io->ReadBytes(encryptedBuffer, XEX_AES_BLOCK_SIZE);

    // Use CBC decrypt helper
    AesCbcDecrypt(aes.get(), iv, encryptedBuffer, decryptedBuffer);

    // check to see if the windows PE magic is there
    MemoryIO peIO(decryptedBuffer, XEX_AES_BLOCK_SIZE);
    peIO.SetEndian(BigEndian);

    WORD magic = peIO.ReadWord();
    
    // Log diagnostic information if magic number validation fails
    if (magic != 0x4D5A) {
        char msg[256];
        snprintf(msg, sizeof(msg), "TryKey: magic=0x%04X (expected 0x4D5A). First 4 decrypted bytes: %02X %02X %02X %02X. Encrypted bytes: %02X %02X %02X %02X",
                 magic, decryptedBuffer[0], decryptedBuffer[1], decryptedBuffer[2], decryptedBuffer[3],
                 encryptedBuffer[0], encryptedBuffer[1], encryptedBuffer[2], encryptedBuffer[3]);
    }
    
    return (magic == 0x4D5A); 	// 'MZ'
}

void Xex::ComputeSha1(const BYTE *data, size_t length, BYTE *outHash)
{
    const auto sha1 = Botan::HashFunction::create_or_throw("SHA-1");
    sha1->update(data, length);
    sha1->final(outHash);
}

bool Xex::VerifyHeaderHash()
{
    const UINT64 fileLength = io->Length();
    const UINT64 securityOffset = header.headerAddress;
    const UINT64 headerSize = header.dataAddress;

    // XexpVerifyXexHeaders hashes the part of the header after the RSA-signed
    // block and then the part before it; the signed block itself (which holds
    // the hash field) is skipped.
    if (securityOffset > headerSize || headerSize > fileLength)
        return false;
    if (securityInfo.size < 0x184 || securityInfo.size > headerSize - securityOffset)
        return false;
    if (securityInfo.imageInfoSize > headerSize - securityOffset - 0x184)
        return false;
    if (securityOffset + 0x17C > headerSize)
        return false;

    const size_t headLength = static_cast<size_t>(securityOffset + 8);
    const size_t tailLength = static_cast<size_t>(headerSize - securityOffset - 0x17C);

    const UINT64 resume = io->GetPosition();

    std::vector<BYTE> buffer(headLength + tailLength);
    io->SetPosition(securityOffset + 0x17C);
    io->ReadBytes(buffer.data(), static_cast<DWORD>(tailLength));
    io->SetPosition(0);
    io->ReadBytes(buffer.data() + tailLength, static_cast<DWORD>(headLength));
    io->SetPosition(resume);

    BYTE digest[0x14];
    ComputeSha1(buffer.data(), buffer.size(), digest);
    return memcmp(digest, securityInfo.headerHash, sizeof(digest)) == 0;
}

void Xex::ComputeImageKey(const BYTE *masterKey, BYTE *imageKey) const
{
    auto aes = Botan::BlockCipher::create("AES-128");
    aes->set_key(masterKey, XEX_AES_BLOCK_SIZE);

    // image key = AES-128-ECB of the per-file key in the security page
    memcpy(imageKey, securityInfo.key, XEX_AES_BLOCK_SIZE);
    aes->decrypt(imageKey);
}

const BYTE *Xex::SelectCompressedImageKey()
{
    const UINT64 fileLength = io->Length();

    if (header.dataAddress >= fileLength)
        throw std::string("XEX: Compressed data starts past the end of the file.\n");

    if (compressedBlock0Size > fileLength - header.dataAddress)
        throw std::string("XEX: The first compressed block is larger than the file.\n");

    // read the first block as it is stored in the file
    std::vector<BYTE> block0(compressedBlock0Size);
    io->SetPosition(header.dataAddress);
    io->ReadBytes(block0.data(), compressedBlock0Size);

    // The payload of a type 2 image does not start with "MZ", so the SHA-1 of
    // block 0 from the signed base-file descriptor is the only reliable key
    // probe. Try the plaintext first (the descriptor's encryption flag is not
    // always trustworthy) and then the two master keys.
    const BYTE *masterKeys[3] = { nullptr, XEX_RETAIL_KEY, XEX_DEVKIT_KEY };
    const XexEncryptionState states[3] = { Decrypted, RetailEncrypted, DevKitEncrypted };

    std::vector<BYTE> decrypted(compressedBlock0Size);
    BYTE digest[0x14];

    for (size_t i = 0; i < 3; i++)
    {
        const BYTE *plain = block0.data();

        if (masterKeys[i] != nullptr)
        {
            if ((compressedBlock0Size % XEX_AES_BLOCK_SIZE) != 0)
                throw std::string("XEX: Encrypted first compressed block is not AES aligned.\n");

            BYTE imageKey[XEX_AES_BLOCK_SIZE];
            ComputeImageKey(masterKeys[i], imageKey);

            auto aes = Botan::BlockCipher::create("AES-128");
            aes->set_key(imageKey, XEX_AES_BLOCK_SIZE);

            BYTE iv[XEX_AES_BLOCK_SIZE] = {0};
            memcpy(decrypted.data(), block0.data(), compressedBlock0Size);

            for (DWORD offset = 0; offset < compressedBlock0Size; offset += XEX_AES_BLOCK_SIZE)
                AesCbcDecrypt(aes.get(), iv, decrypted.data() + offset, decrypted.data() + offset);

            plain = decrypted.data();
        }

        ComputeSha1(plain, compressedBlock0Size, digest);

        if (memcmp(digest, compressedBlock0Sha1, sizeof(compressedBlock0Sha1)) == 0)
        {
            encryptionState = states[i];
            return (masterKeys[i] != nullptr) ? masterKeys[i] : XEX_RETAIL_KEY;
        }
    }

    throw std::string("XEX: Could not identify the compression key (block 0 digest mismatch).\n");
}

void Xex::ExtractCompressedData(std::string path)
{
    // A type 2 image decompresses to exactly securityInfo.imageSize bytes.
    if (securityInfo.imageSize == 0 || securityInfo.imageSize > MaxDecompressedImageSize)
        throw std::string("XEX: Unsupported decompressed image size.\n");

    const UINT64 fileLength = io->Length();
    if (header.dataAddress >= fileLength)
        throw std::string("XEX: Compressed data starts past the end of the file.\n");

    const bool decrypt = (encryptionState == RetailEncrypted || encryptionState == DevKitEncrypted);

    std::unique_ptr<Botan::BlockCipher> aes;
    if (decrypt)
    {
        aes = Botan::BlockCipher::create("AES-128");
        aes->set_key(decryptedKey, XEX_AES_BLOCK_SIZE);
    }

    XexLzxDecoder decoder;
    // The base-file descriptor's blockSize is the LZX window size: dashboard
    // binaries (xam.xex, dash.xex) declare 1 MiB, which needs 42 position
    // slots instead of the 30 used by the 32 KiB XEXs.
    decoder.SetWindowSize(compressedBlockSize);

    std::vector<BYTE> block;
    std::vector<BYTE> chunkOutput(XexLzxDecoder::FrameSize);

    BYTE expectedHash[0x14];
    memcpy(expectedHash, compressedBlock0Sha1, sizeof(expectedHash));

    BYTE iv[XEX_AES_BLOCK_SIZE] = {0}; // CBC state, continuous across blocks
    UINT64 cursor = header.dataAddress;
    DWORD currentBlockSize = compressedBlock0Size;
    DWORD produced = 0;
    DWORD blockIndex = 0;

    FileIO outFile(path, true);

    while (produced < securityInfo.imageSize)
    {
        if (currentBlockSize < 0x18)
            throw std::string("XEX: Compressed block is too small.\n");

        if (currentBlockSize > fileLength - cursor)
            throw std::string("XEX: Compressed block runs past the end of the file.\n");

        if (decrypt && (currentBlockSize % XEX_AES_BLOCK_SIZE) != 0)
            throw std::string("XEX: Encrypted compressed block is not AES aligned.\n");

        block.resize(currentBlockSize);
        io->SetPosition(cursor);
        io->ReadBytes(block.data(), currentBlockSize);

        // The kernel decrypts whole blocks with a CBC state that carries over
        // from one block to the next (zero IV at the start).
        if (decrypt)
        {
            for (DWORD offset = 0; offset < currentBlockSize; offset += XEX_AES_BLOCK_SIZE)
                AesCbcDecrypt(aes.get(), iv, block.data() + offset, block.data() + offset);
        }

        // digest chain: the descriptor signs block 0, every block signs the next
        BYTE hash[0x14];
        ComputeSha1(block.data(), currentBlockSize, hash);

        if (memcmp(hash, expectedHash, sizeof(expectedHash)) != 0)
            throw std::string("XEX: Compressed block ") + std::to_string(blockIndex) +
                " failed its digest check (corrupt data or wrong key).\n";

        // chunk list: { u16 compressedLength; u8 data[compressedLength]; } until u16 0
        size_t offset = 0x18;

        while (offset + 2 <= currentBlockSize && produced < securityInfo.imageSize)
        {
            const WORD chunkLength = static_cast<WORD>((block[offset] << 8) | block[offset + 1]);
            offset += 2;

            if (chunkLength == 0)
                break;

            if (chunkLength > currentBlockSize - offset)
                throw std::string("XEX: Compressed chunk runs past the end of its block.\n");

            // each chunk decodes to one frame, the last one to the remainder
            const DWORD expectedOutput = std::min<DWORD>(XexLzxDecoder::FrameSize,
                                                         securityInfo.imageSize - produced);

            decoder.DecodeChunk(block.data() + offset, chunkLength, chunkOutput.data(), expectedOutput);
            outFile.WriteBytes(chunkOutput.data(), expectedOutput);

            produced += expectedOutput;
            offset += chunkLength;
        }

        // the current block describes the next one
        memcpy(expectedHash, block.data() + 4, sizeof(expectedHash));
        const DWORD nextBlockSize = (static_cast<DWORD>(block[0]) << 24) |
                                    (static_cast<DWORD>(block[1]) << 16) |
                                    (static_cast<DWORD>(block[2]) << 8) |
                                    static_cast<DWORD>(block[3]);

        cursor += currentBlockSize;
        currentBlockSize = nextBlockSize;
        blockIndex++;

        if (currentBlockSize == 0 && produced < securityInfo.imageSize)
            throw std::string("XEX: The compressed block chain ended before the image was complete.\n");
    }

    outFile.Close();
}

BaseIO *Xex::GetRawDataIO()
{
    // check if we need to decompress/decrypt anything
    if (compressionState == XexDecompressed && !IsEncrypted())
    {
        io->SetPosition(header.dataAddress);
        return io;
    }
    else
    {
        // check to see if a rawDataIO already exists
        if (rawDataIO)
        {
            rawDataIO->SetPosition(0);
            return rawDataIO.get();
        }

        // create a temporary file with the cleaned data
        auto tempPath = fs::temp_directory_path() / ("xex_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".tmp");
        rawDataPath = tempPath.string();
        ExtractData(rawDataPath);

        rawDataIO = std::make_unique<FileIO>(rawDataPath);
        return rawDataIO.get();
    }
}
