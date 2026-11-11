# Centralized list of Botan modules required by Velocity/XboxInternals.
# Trim to the minimum; update if code evolves.
# Botan CMake integration adapted from: https://github.com/Tectu/botan-cmake
set(VELOCITY_BOTAN_MODULES
    sha1
    hmac
    rc4
    rsa
    pubkey
    bigint
    auto_rng
    system_rng
    sha2_32
    asn1
    base64
    hex
    pem
    emsa_pkcs1 # EMSA-PKCS1-v1_5 padding (aka EMSA3) for RSA signing (used in XContentHeader::ResignHeader)
    aes         # AES-128 block cipher for XEX decryption
    modes       # Cipher modes (includes CBC for XEX)
    block       # Block cipher base class
)
