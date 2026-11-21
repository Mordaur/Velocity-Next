#ifndef Ytgr_H
#define Ytgr_H

#include <iostream>

#include <XboxInternals/IO/FileIO.h>
#include "../Cryptography/XeKeys.h"
#include "../Gpd/XdbfHelpers.h"
#include "../Stfs/StfsConstants.h"
#include <botan_all.h>

class XBOXINTERNALS_EXPORT Ytgr
{
public:
    Ytgr(std::string filePath);
    Ytgr(FileIO *io);
    ~Ytgr();

    Ytgr(const Ytgr &) = delete;
    Ytgr &operator=(const Ytgr &) = delete;

    u32 magic;
    Version XSignerMinimumVersion;
    u32 structSize;
    time_t dateAddedToServer;
    u32 contentLength;
    u32 reserved;
    u8 contentHash[0x14];
    u8 rsaSignature[0x100];
    bool valid;

private:
    FileIO *io = nullptr;
    bool ioPassedIn;

    // parse Ytgr header
    void Parse();
};

#endif // Ytgr_H


