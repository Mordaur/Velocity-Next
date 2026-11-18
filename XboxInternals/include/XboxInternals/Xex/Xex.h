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

    DWORD GetModuleFlags() const;

    ESRBRating GetEsrbRating() const;

    std::string GetEsrbRatingText() const;

    PEGIRating GetPegiRating() const;

    std::string GetPegiRatingText() const;

    OFLCAURating GetOflcAURating() const;

    std::string GetOflcAURatingText() const;

    OFLCNZRating GetOflcNZRating() const;

    std::string GetOflcNZRatingText() const;

    std::string GetOriginalPEImageName() const;

    DWORD GetImageBaseAddress() const;

    DWORD GetEntryPoint() const;

    DWORD GetOriginalBaseAddress() const;

    DWORD GetDefaultStackSize() const;

    DWORD GetDefaultFileSystemCacheSize() const;

    DWORD GetDefaultHeapSize() const;

    DWORD GetTitleWorkspaceSize() const;

    DWORD GetAllowedMediaTypes() const;

    void ExtractBaseImageFile(std::string outPath);

    void ExtractResource(std::string resourceName, std::string outPath);

    void ExtractData(std::string path);

    void ExtractDecryptedData(std::string path) const;

    void ExtractDecompressedData(std::string path);

    void ExtractData2(std::string path);

    bool HasRegion(XexRegion region);

    void SetRegion(XexRegion region);

    DWORD GetImageFlags() const;

    DWORD GetImageSize() const;

    DWORD GetLoadAddress() const;

    BYTE* GetMediaID();

    BYTE* GetKey();

    BYTE* GetHeaderHash();

    BYTE* GetLANKey();

    DWORD GetTitleID() const;

    bool IsEncrypted() const;

    XexEncryptionState GetEncryptionState() const;

    std::string GetEncryptionStateStr() const;

    XexCompressionState GetCompressionState() const;

    std::string GetCompressionStateStr() const;

private:
    BaseIO *io;
    std::unique_ptr<BaseIO> ownedIO;
    XexHeader header;
    DWORD pageSize;
    DWORD firstResourceFileAddr;

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

    DWORD imageBaseAddress;
    DWORD entryPoint;
    DWORD originalBaseAddress;
    DWORD defaultStackSize;
    DWORD defaultFileSystemCacheSize;
    DWORD defaultHeapSize;
    DWORD titleWorkspaceSize;

    XexSecurityInfo securityInfo;
    bool encrypted;
    XexEncryptionState encryptionState;
    XexCompressionState compressionState;
    std::vector<XexCompressionBlock> compressionBlocks;
    BYTE lanKey[XEX_LAN_KEY_SIZE];
    BYTE decryptedKey[XEX_AES_BLOCK_SIZE];

    // Type 2 (XCompress/LZX) base-file descriptor (optional header 0x3FF):
    // { u32 size; u16 encryption; u16 compression; u32 blockSize;
    //   u32 sizeOfBlock0; u8 sha1OfBlock0[20] }
    DWORD compressedBlockSize;
    DWORD compressedBlock0Size;
    BYTE compressedBlock0Sha1[0x14];

    // Upper bound for a decompressed image; anything above is rejected instead
    // of trying to materialise it.
    static constexpr DWORD MaxDecompressedImageSize = 0x40000000; // 1 GiB

    XexExecutionInfo executionInfo;


    void AesCbcDecrypt(Botan::BlockCipher *aes, BYTE *initializationVector, const BYTE *bufferEnc, BYTE *bufferDec) const;

    void Parse();

    void ParseOptionalHeaderEntry(int index);

    void ParseSystemImportLibraryTable(DWORD address);

    void ParseRatingInformation(DWORD address);

    void ParseStaticLibraryTable(DWORD address);

    void ParseOriginalPEImageName(DWORD address);

    void ParseResourceFileTable(DWORD address);

    void ParseBaseFileDescriptor(DWORD address);

    void ParseLANKey(DWORD address);

    void ParseExecutionInfo(DWORD address);

    void ExtractFromRawData(std::string outPath, DWORD address, DWORD size);

    bool TryKey(const BYTE *key);

    // Type 2 (XCompress/LZX) support.
    void ExtractCompressedData(std::string path);
    // Derives the XEX image key (AES-128-ECB of securityInfo.key with the given
    // master key).
    void ComputeImageKey(const BYTE *masterKey, BYTE *imageKey) const;
    // Picks the master key by hashing the decrypted first compressed block and
    // comparing it with the SHA-1 in the base-file descriptor. Returns the key
    // to use for the image key derivation and updates encryptionState.
    const BYTE *SelectCompressedImageKey();
    static void ComputeSha1(const BYTE *data, size_t length, BYTE *outHash);


    // Get an IO pointing to the beginning of the decrypted and fully decompressed data in an XEX
    BaseIO* GetRawDataIO();

    friend class XexBaseIO;
    friend class XexAesIO;
    friend class XexZeroBasedCompressionIO;
};

#endif // XEX_H
