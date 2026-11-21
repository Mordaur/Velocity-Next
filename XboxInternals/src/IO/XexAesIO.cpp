#include <XboxInternals/IO/XexAesIO.h>
#include <XboxInternals/Xex/Xex.h>

#include <cstring>


XexAesIO::XexAesIO(BaseIO *io, Xex *xex, const u8 *key) :
    XexBaseIO(io, xex), curAesBlockAddress(0)
{
    // the initialization vector starts out as all zeros
    memset(curIV, 0, XEX_AES_BLOCK_SIZE);

    // create the aes object
    aes = Botan::BlockCipher::create("AES-128");
    aes->set_key(key, XEX_AES_BLOCK_SIZE);

    // decrypt the first block in the file
    u8 buffer[XEX_AES_BLOCK_SIZE];
    xex->io->SetPosition(xex->header.dataAddress);
    xex->io->ReadBytes(buffer, XEX_AES_BLOCK_SIZE);
    AesCbcDecrypt(buffer);
}

XexAesIO::~XexAesIO()
{
}

void XexAesIO::ReadBytes(u8 *outBuffer, u32 readLength)
{
    if (position + readLength > Length())
        throw std::string("XexAesIO: Cannot read beyond end of file.\n");

    u32 bytesRead = 0;
    u32 bytesLeft = readLength;

    // check to see if we already have decrypted the data the caller requested
    if (position >= curAesBlockAddress &&
        position <= curAesBlockAddress + XEX_AES_BLOCK_SIZE)
    {
        // calculate the number of bytes to read, clamped to the request and the
        // cached block
        u32 bytesToRead = (curAesBlockAddress + XEX_AES_BLOCK_SIZE) - position;
        if (bytesToRead > bytesLeft)
            bytesToRead = bytesLeft;

        // calculate the offset into the decrypted block to start 'reading' from
        u32 offset = position - curAesBlockAddress;

        // read them into the buffer
        memcpy(outBuffer, curDecryptedBlock + offset, bytesToRead);
        bytesRead += bytesToRead;
        position += bytesToRead;
        bytesLeft -= bytesToRead;
    }

    // as long as there are bytes left to read, read them in
    u8 encryptedBuffer[XEX_AES_BLOCK_SIZE];
    while (bytesLeft != 0)
    {
        // seek to the beginning of the next AES block to read
        u64 nextAesBlockAddress = xex->header.dataAddress + curAesBlockAddress + XEX_AES_BLOCK_SIZE;
        xex->io->SetPosition(nextAesBlockAddress);

        xex->io->ReadBytes(encryptedBuffer, XEX_AES_BLOCK_SIZE);
        AesCbcDecrypt(encryptedBuffer);
        curAesBlockAddress += XEX_AES_BLOCK_SIZE;

        // calculate the amount of bytes to copy over
        u32 bytesToCopy = XEX_AES_BLOCK_SIZE;
        if (bytesLeft < XEX_AES_BLOCK_SIZE)
            bytesToCopy = bytesLeft;

        memcpy(outBuffer + bytesRead, curDecryptedBlock, bytesToCopy);

        bytesRead += bytesToCopy;
        position += bytesToCopy;
        bytesLeft -= bytesToCopy;
    }

    // check if we need to decrypt the next block, make sure we're not at the end of the file
    if (position != Length() && (position & ~0xF) == position)
    {
        xex->io->ReadBytes(encryptedBuffer, XEX_AES_BLOCK_SIZE);
        AesCbcDecrypt(encryptedBuffer);
        curAesBlockAddress += XEX_AES_BLOCK_SIZE;
    }
}

void XexAesIO::WriteBytes(u8 *buffer, u32 len)
{
    (void)buffer;
    (void)len;
}

u64 XexAesIO::Length()
{
    return xex->io->Length() - xex->header.dataAddress;
}

void XexAesIO::AesCbcDecrypt(const u8 *bufferEnc)
{
    aes->decrypt(bufferEnc, curDecryptedBlock);

    for (u32 x = 0; x < XEX_AES_BLOCK_SIZE; x++)
    {
        // xor the data with the old IV
        curDecryptedBlock[x] ^= curIV[x];

        // update the IV
        curIV[x] = bufferEnc[x];
    }
}
