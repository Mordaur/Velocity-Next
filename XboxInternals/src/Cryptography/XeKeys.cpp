#include <XboxInternals/Cryptography/XeKeys.h>

bool XeKeys::VerifyRSASignature(XeKeysRsaKeys key, u8 *pbMessage, u32 cbMessage,
        u8 *signature)
{
    u8 *modulus1, *modulus2;
    u32 exponent1, exponent2;

    // get the correct key
    switch (key)
    {
        case PIRSKey:
            modulus1 = const_cast<u8*>(PirsModulus1);
            modulus2 = NULL;
            exponent1 = 3;
            exponent2 = 0;
            break;
        case LIVEKey:
            modulus1 = const_cast<u8*>(LiveModulus1);
            modulus2 = const_cast<u8*>(LiveDeviceModulus);
            exponent1 = 0x10001;
            exponent2 = 3;
            break;
        case DeviceKey:
            modulus1 = const_cast<u8*>(DeviceModulus1);
            modulus2 = const_cast<u8*>(LiveDeviceModulus);
            exponent1 = exponent2 = 3;
            break;
        case UnknownKey:
            modulus1 = const_cast<u8*>(UnknownModulus1);
            modulus2 = const_cast<u8*>(UnknownModulus2);
            exponent1 = exponent2 = 3;
            break;
        default:
            throw std::string("XeKeys: Invalid key.\n");
    }

    // format the signature
    for (int i = 0; i < 0x20; i++)
        FileIO::ReverseGenericArray(&signature[i * 8], 1, 8);
    XeCrypt::BnQw_SwapDwQwLeBe(signature, 0x100);

    // check with the first key
    bool key1 = XeCrypt::Pkcs1Verify(pbMessage, cbMessage, signature, 0x100, exponent1, modulus1);

    if (key1 || key == PIRSKey)
        return key1;

    // check with the second key
    return XeCrypt::Pkcs1Verify(pbMessage, cbMessage, signature, 0x100, exponent2, modulus2);
}


