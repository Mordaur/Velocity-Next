#ifndef XEX_H
#define XEX_H

/* Most of the algorithms and file structure information I got from the Xbox360 emulator project found here:
 * https://github.com/benvanik/xenia
 **/

#include <XboxInternals/IO/BaseIO.h>
#include <XboxInternals/IO/FileIO.h>
#include <XboxInternals/IO/MemoryIO.h>
#include <XboxInternals/Xex/XexDefinitions.h>
#include <XboxInternals/Export.h>

#include <memory>
#include <string>

#include <botan_all.h>

class XexBaseIO;
class XexAesIO;
class XexZeroBasedCompressionIO;

class XBOXINTERNALS_EXPORT Xex
{
public:
    Xex(BaseIO *io);
    Xex(std::string fileName);
    ~Xex();

    std::vector<std::string> GetSystemImportLibraries() const;

    std::vector<XexStaticLibraryInfo> GetStaticLibraries() const;

    std::vector<XexResourceFileEntry> GetResourceFileInfo() const;

    u32 GetModuleFlags() const;

    ESRBRating GetEsrbRating() const;

    std::string GetEsrbRatingText() const;

    PEGIRating GetPegiRating() const;

    std::string GetPegiRatingText() const;

    OFLCAURating GetOflcAURating() const;

    std::string GetOflcAURatingText() const;

    OFLCNZRating GetOflcNZRating() const;

    std::string GetOflcNZRatingText() const;

    std::string GetOriginalPEImageName() const;

    u32 GetImageBaseAddress() const;

    u32 GetEntryPoint() const;

    u32 GetOriginalBaseAddress() const;

    u32 GetDefaultStackSize() const;

    u32 GetDefaultFileSystemCacheSize() const;

    u32 GetDefaultHeapSize() const;

    u32 GetTitleWorkspaceSize() const;

    u32 GetAllowedMediaTypes() const;

    void ExtractBaseImageFile(std::string outPath);

    void ExtractResource(std::string resourceName, std::string outPath);

    void ExtractData(std::string path);

    void ExtractDecryptedData(std::string path) const;

    void ExtractDecompressedData(std::string path);

    void ExtractData2(std::string path);

    bool HasRegion(XexRegion region);

    void SetRegion(XexRegion region);

    u32 GetImageFlags() const;

    u32 GetImageSize() const;

    u32 GetLoadAddress() const;

    u8* GetMediaID();

    u8* GetKey();

    u8* GetHeaderHash();

    // XexpVerifyXexHeaders: SHA-1 over [securityOffset + 0x17C, headerSize)
    // followed by [0, securityOffset + 8), compared with the header hash that
    // follows the RSA-signed block. Covers the header, not the image.
    bool IsHeaderHashValid() const;

    u8* GetLANKey();

    u32 GetTitleID() const;

    bool IsEncrypted() const;

    XexEncryptionState GetEncryptionState() const;

    std::string GetEncryptionStateStr() const;

    XexCompressionState GetCompressionState() const;

    std::string GetCompressionStateStr() const;

private:
    BaseIO *io;
    std::unique_ptr<BaseIO> ownedIO;
    XexHeader header;
    u32 pageSize;
    u32 firstResourceFileAddr;

    std::unique_ptr<FileIO> rawDataIO;
    std::string rawDataPath;

    std::vector<XexOptionalHeaderEntry> optionalHeaderData;
    std::vector<XexStaticLibraryInfo> staticLibraries;
    std::vector<XexResourceFileEntry> resourceFiles;
    std::vector<XexSectionEntry> sections;
    std::vector<std::string> systemImportLibraries;
    std::string originalPEImageName;

    // ratings
    ESRBRating esrbRating;
    PEGIRating pegiRating;
    PEGIFIRating pegifiRating;
    PEGIPTRating pegiptRating;
    PEGIBBFCRating pegibbfcRating;
    OFLCAURating oflcAURating;
    OFLCNZRating oflcNZRating;
    // there are still more

    u32 imageBaseAddress;
    u32 entryPoint;
    u32 originalBaseAddress;
    u32 defaultStackSize;
    u32 defaultFileSystemCacheSize;
    u32 defaultHeapSize;
    u32 titleWorkspaceSize;

    XexSecurityInfo securityInfo;
    bool encrypted;
    bool headerHashValid;
    XexEncryptionState encryptionState;
    XexCompressionState compressionState;
    std::vector<XexCompressionBlock> compressionBlocks;
    u8 lanKey[XEX_LAN_KEY_SIZE];
    u8 decryptedKey[XEX_AES_BLOCK_SIZE];

    // Type 2 (XCompress/LZX) base-file descriptor (optional header 0x3FF):
    // { u32 size; u16 encryption; u16 compression; u32 blockSize;
    //   u32 sizeOfBlock0; u8 sha1OfBlock0[20] }
    u32 compressedBlockSize;
    u32 compressedBlock0Size;
    u8 compressedBlock0Sha1[0x14];

    // Upper bound for a decompressed image; anything above is rejected instead
    // of trying to materialise it.
    static constexpr u32 MaxDecompressedImageSize = 0x40000000; // 1 GiB

    XexExecutionInfo executionInfo;


    void AesCbcDecrypt(Botan::BlockCipher *aes, u8 *initializationVector, const u8 *bufferEnc, u8 *bufferDec) const;

    void Parse();

    void ParseOptionalHeaderEntry(int index);

    void ParseSystemImportLibraryTable(u32 address);

    void ParseRatingInformation(u32 address);

    void ParseStaticLibraryTable(u32 address);

    void ParseOriginalPEImageName(u32 address);

    void ParseResourceFileTable(u32 address);

    void ParseBaseFileDescriptor(u32 address);

    void ParseLANKey(u32 address);

    void ParseExecutionInfo(u32 address);

    void ExtractFromRawData(std::string outPath, u32 address, u32 size);

    bool TryKey(const u8 *key);

    // Type 2 (XCompress/LZX) support.
    void ExtractCompressedData(std::string path);
    // Derives the XEX image key (AES-128-ECB of securityInfo.key with the given
    // master key).
    void ComputeImageKey(const u8 *masterKey, u8 *imageKey) const;
    // Picks the master key by hashing the decrypted first compressed block and
    // comparing it with the SHA-1 in the base-file descriptor. Returns the key
    // to use for the image key derivation and updates encryptionState.
    const u8 *SelectCompressedImageKey();
    bool VerifyHeaderHash();
    static void ComputeSha1(const u8 *data, size_t length, u8 *outHash);


    // Get an IO pointing to the beginning of the decrypted and fully decompressed data in an XEX
    BaseIO* GetRawDataIO();

    friend class XexBaseIO;
    friend class XexAesIO;
    friend class XexZeroBasedCompressionIO;
};

#endif // XEX_H
