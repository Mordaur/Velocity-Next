#include <XboxInternals/Cryptography/XeCrypt.h>

void XeCrypt::BnQw_SwapDwQwLeBe(u8 *data, u32 length)
{
    if (length % 8 != 0)
        throw std::string("STFS: length is not divisible by 8.\n");

    for (u32 i = 0; i < length / 2; i += 8)
    {
        u8 temp[8];
        memcpy(temp, &data[i], 8);

        u8 temp2[8];
        memcpy(temp2, &data[length - i - 8], 8);

        memcpy(&data[i], temp2, 8);
        memcpy(&data[length - i - 8], temp, 8);
    }
}

bool XeCrypt::Pkcs1Verify(u8 *pbMessage, u32 cbMessage, u8 *pbSignature, u32 cbSignature,
        u64 publicExponent, u8 *modulus)
{
    Botan::RSA_PublicKey pubKey(Botan::BigInt(modulus, cbSignature), publicExponent);
    Botan::PK_Verifier verifier(pubKey, "EMSA3(SHA-1)");
    return verifier.verify_message(pbMessage, cbMessage, pbSignature, cbSignature);
}


