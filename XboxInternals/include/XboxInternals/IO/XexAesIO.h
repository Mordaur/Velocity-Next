#ifndef XEXAESIO_H
#define XEXAESIO_H

#include <XboxInternals/IO/BaseIO.h>
#include <XboxInternals/IO/XexBaseIO.h>
#include <XboxInternals/Xex/XexDefinitions.h>

#include <botan_all.h>

#include <memory>

class Xex;

#define XEX_NULL_BUFFER_SIZE 	0x10000

class XexAesIO : public XexBaseIO
{
public:
    XexAesIO(BaseIO *io, Xex *xex, const u8 *key);
    ~XexAesIO();

    void ReadBytes(u8 *outBuffer, u32 readLength);

    void WriteBytes(u8 *buffer, u32 len);

    u64 Length();

private:
    u8 curIV[XEX_AES_BLOCK_SIZE];
    u8 curDecryptedBlock[XEX_AES_BLOCK_SIZE];
    u64 curAesBlockAddress;
    std::unique_ptr<Botan::BlockCipher> aes;

    void AesCbcDecrypt(const u8 *bufferEnc);
};

#endif // XEXAESIO_H
