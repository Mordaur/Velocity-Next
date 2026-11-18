#include <XboxInternals/Stfs/XContentHeader.h>
#include <XboxInternals/IO/MemoryIO.h>
#include <iostream>
#include <sstream>

using namespace std;

XContentHeader::XContentHeader(BaseIO *io, DWORD flags) : installerType((InstallerType)0),
    flags(flags)
{
    // set the io
    this->io = io;

    if ((flags & MetadataSkipRead) == 0)
        readMetadata();
}

namespace {

// RSA-2048 public moduli for the LIVE/PIRS package signature, big-endian.
// Public keys; extracted from the retail/devkit key descriptors (the PIRS-retail
// entry is proven by the corpus PIRS packages).
static const BYTE kPirsRetailModulus[0x100] =
{
    0x8B, 0xE8, 0xD9, 0x20, 0xF2, 0x05, 0xE0, 0x70, 0x57, 0xA8, 0x68, 0xB4,
    0x61, 0xF4, 0x1B, 0xC6, 0x40, 0xD2, 0x4E, 0xA6, 0xC6, 0x11, 0x04, 0x1B,
    0xBD, 0x47, 0x7C, 0x90, 0xDD, 0x88, 0x12, 0x17, 0x84, 0xF7, 0xB0, 0xF6,
    0x4B, 0x8E, 0xF0, 0x89, 0xC7, 0xE5, 0xCE, 0xAA, 0xEC, 0xDE, 0x48, 0xA0,
    0xA4, 0x37, 0x30, 0x11, 0x51, 0x83, 0xBD, 0xF7, 0xA2, 0x10, 0x27, 0x96,
    0x2F, 0x9F, 0xA9, 0x3D, 0x32, 0x25, 0x79, 0x36, 0x0E, 0xF4, 0xED, 0x19,
    0x4A, 0x02, 0xAE, 0xD4, 0xD8, 0x37, 0xCF, 0xA6, 0xEB, 0x87, 0xE6, 0xD1,
    0xFB, 0xB3, 0x66, 0xFC, 0x09, 0x06, 0x2A, 0x61, 0x20, 0x2C, 0xF8, 0x72,
    0xFE, 0x48, 0xFF, 0x28, 0x3D, 0x10, 0xBB, 0x9E, 0xB3, 0x3D, 0x03, 0x25,
    0x60, 0xC8, 0x7A, 0x59, 0x4D, 0x84, 0xB9, 0x18, 0x97, 0x99, 0xAC, 0x00,
    0x22, 0x4E, 0x13, 0x24, 0xD3, 0xC1, 0x57, 0xDF, 0xBC, 0xA4, 0xAA, 0x1C,
    0x47, 0x4B, 0xF4, 0x97, 0x9C, 0x27, 0x51, 0xCF, 0x4B, 0x34, 0x7A, 0x35,
    0xBE, 0x66, 0x49, 0x2F, 0xAE, 0x20, 0xA4, 0x76, 0x45, 0x60, 0x46, 0xBC,
    0x36, 0xE1, 0x17, 0x71, 0xC5, 0xA7, 0x9B, 0x08, 0x11, 0x2E, 0xA0, 0x64,
    0xD0, 0xF8, 0x91, 0x02, 0xA6, 0xE7, 0x7D, 0x77, 0x11, 0x4A, 0x80, 0x24,
    0x72, 0x05, 0x7A, 0x63, 0xE6, 0x7C, 0x63, 0x0E, 0xAF, 0x4C, 0xF7, 0xE7,
    0xFC, 0x02, 0xAB, 0x77, 0xBA, 0xC8, 0xE6, 0x74, 0xB2, 0xB0, 0xAE, 0x97,
    0x69, 0xEB, 0xFE, 0x71, 0xA6, 0x58, 0xEB, 0xB6, 0xA3, 0xC4, 0xA3, 0xEE,
    0xBA, 0x03, 0x85, 0xB5, 0x1A, 0x3E, 0x25, 0xF9, 0x8E, 0xBA, 0x8E, 0xBC,
    0x1A, 0x90, 0xAC, 0x20, 0xDE, 0x0D, 0x18, 0x40, 0x72, 0xD6, 0xE3, 0x46,
    0x9D, 0xFC, 0x5C, 0x72, 0x41, 0x94, 0x58, 0x47, 0xE6, 0x3B, 0x32, 0xB2,
    0x8D, 0x9E, 0x9E, 0xE7,
};

static const BYTE kPirsDevkitModulus[0x100] =
{
    0x6B, 0xA0, 0xBF, 0xC8, 0x77, 0x35, 0x1C, 0xC9, 0xE5, 0xAC, 0x99, 0x73,
    0x6C, 0x4E, 0x2F, 0x64, 0x1E, 0xAE, 0xDB, 0xE4, 0x2E, 0xAB, 0xE7, 0x84,
    0x99, 0xAD, 0xA2, 0x4A, 0x62, 0x70, 0x06, 0x3E, 0x27, 0x93, 0xBE, 0xE6,
    0x61, 0x70, 0x76, 0xE1, 0x3A, 0x76, 0x30, 0x73, 0xFD, 0x97, 0x5D, 0x6D,
    0x3D, 0x1B, 0x8F, 0xBE, 0xC0, 0x5C, 0x70, 0xB8, 0x6B, 0x4C, 0x8C, 0x98,
    0x65, 0x85, 0x5D, 0x4C, 0x6C, 0xAA, 0x43, 0xA7, 0xC5, 0xD0, 0xBE, 0xCC,
    0x4D, 0x90, 0xBD, 0xE8, 0xF8, 0x0F, 0x91, 0x56, 0xDB, 0x71, 0x6E, 0x3B,
    0xF1, 0xA3, 0xD9, 0xB8, 0xC2, 0x39, 0x80, 0x8E, 0x1A, 0xF5, 0xE0, 0xB0,
    0x49, 0xB9, 0x6E, 0xC5, 0x42, 0x81, 0x3A, 0x4E, 0x1B, 0xC8, 0x51, 0x84,
    0xC5, 0x8D, 0xF4, 0x44, 0x97, 0xF2, 0xE3, 0xD0, 0x59, 0x45, 0xBC, 0x7D,
    0x66, 0x48, 0xF9, 0x1C, 0xEA, 0x39, 0xA0, 0xEF, 0x90, 0xDB, 0xAB, 0x22,
    0xD0, 0x8B, 0x4E, 0x66, 0x7C, 0x89, 0xA7, 0x91, 0x3D, 0xD8, 0xBC, 0x1E,
    0xED, 0x3E, 0xF3, 0xAA, 0x63, 0xDA, 0x07, 0x72, 0xB3, 0x9C, 0x28, 0xF2,
    0x7B, 0x66, 0x87, 0xD5, 0xD2, 0x2A, 0x10, 0x44, 0x65, 0x22, 0x54, 0x40,
    0x76, 0xA4, 0x6F, 0x9E, 0xF9, 0x4C, 0x48, 0xB0, 0xFC, 0x6E, 0xA5, 0xAD,
    0x4D, 0xD0, 0x18, 0x9F, 0x05, 0xC8, 0xEC, 0xB3, 0xCF, 0xA4, 0xC2, 0x9E,
    0xBB, 0xCC, 0x13, 0x25, 0xED, 0x08, 0x8C, 0xED, 0x93, 0x68, 0x4B, 0xC7,
    0x8A, 0x1A, 0x60, 0x16, 0x29, 0xAC, 0x7B, 0xCC, 0x1A, 0x27, 0x95, 0x7F,
    0x02, 0x60, 0x99, 0x0A, 0x9B, 0x41, 0xB7, 0xD4, 0xB8, 0xC0, 0xF5, 0x78,
    0xC2, 0xA7, 0xE9, 0xA6, 0x07, 0x81, 0x64, 0x71, 0x16, 0x88, 0x9D, 0xBB,
    0x02, 0x00, 0xFA, 0x1C, 0xE5, 0x33, 0x5B, 0x2C, 0xBF, 0x7F, 0xC1, 0xB1,
    0x13, 0x2F, 0x49, 0xD7,
};

static const BYTE kLiveRetailModulus[0x100] =
{
    0xC9, 0x7B, 0x51, 0x8E, 0xC7, 0x35, 0xDC, 0xEF, 0xBD, 0x3D, 0x41, 0xC8,
    0xD0, 0x00, 0xF5, 0x7D, 0x82, 0x55, 0xBE, 0x12, 0x12, 0xD9, 0x87, 0xB3,
    0x29, 0x87, 0x5A, 0xE6, 0x7B, 0x2E, 0x7D, 0x86, 0x59, 0x0D, 0x46, 0x73,
    0xFB, 0x7B, 0x78, 0x88, 0x83, 0x85, 0xDE, 0x73, 0x6E, 0x7A, 0x0E, 0xAC,
    0xC7, 0xCF, 0x5B, 0x81, 0xBE, 0xB2, 0xB3, 0xAF, 0xB3, 0x1E, 0x20, 0x05,
    0xC5, 0xC0, 0x9C, 0x5D, 0x11, 0xC7, 0xEE, 0x35, 0x52, 0x72, 0xA4, 0xC2,
    0x59, 0xAC, 0x96, 0x18, 0x16, 0x0E, 0xE9, 0x6B, 0x7E, 0xAF, 0xA7, 0xA7,
    0x27, 0x25, 0x41, 0x70, 0x6C, 0xFB, 0xF6, 0x89, 0xC4, 0xB3, 0xEB, 0xD0,
    0xA7, 0x75, 0x21, 0x41, 0x08, 0xBB, 0x9B, 0x74, 0x55, 0x35, 0x39, 0x3F,
    0x8C, 0xDC, 0xC7, 0x57, 0x1E, 0x5B, 0x15, 0x5B, 0x49, 0xAA, 0x6A, 0xDC,
    0x9D, 0x98, 0xB9, 0xBF, 0xF3, 0x72, 0xFC, 0xC0, 0xEC, 0x08, 0xDD, 0x68,
    0x5F, 0xEA, 0x69, 0xAF, 0x75, 0x77, 0x1C, 0xB0, 0xCC, 0xFA, 0x04, 0xA3,
    0x5D, 0xCA, 0xCB, 0xF9, 0x98, 0x4B, 0x68, 0x15, 0x9A, 0x7A, 0xBE, 0xB9,
    0xB7, 0x91, 0x78, 0x0A, 0xF5, 0xB0, 0xAC, 0xAB, 0xC1, 0x0B, 0x09, 0x1D,
    0xF9, 0xF7, 0x10, 0x85, 0x7E, 0xEC, 0x5C, 0x35, 0x2E, 0x22, 0x17, 0xFA,
    0x75, 0x71, 0xA0, 0x74, 0xE0, 0x7E, 0x17, 0x23, 0x61, 0xFB, 0xFD, 0x36,
    0x3D, 0xFA, 0xC9, 0xA6, 0xCF, 0x2A, 0xA8, 0x4E, 0x4C, 0xAF, 0x2E, 0x18,
    0x01, 0x9F, 0x38, 0xB1, 0xCD, 0xA1, 0x00, 0xF8, 0x9C, 0xC4, 0xAD, 0x2D,
    0xCB, 0x2A, 0xF2, 0xA7, 0x02, 0x83, 0xE8, 0xAD, 0x9E, 0x01, 0x1D, 0xDA,
    0x5D, 0x16, 0x7E, 0x54, 0x04, 0x4E, 0x58, 0x6D, 0xFC, 0xFF, 0x16, 0x1C,
    0x20, 0xB6, 0xAB, 0x41, 0x90, 0x78, 0x9D, 0xA1, 0xF2, 0xE5, 0x3E, 0x3F,
    0x03, 0x75, 0xC2, 0xB3,
};

static const BYTE kLiveDevkitModulus[0x100] =
{
    0xB5, 0xFF, 0x62, 0xEB, 0xEC, 0x7B, 0xC3, 0x3D, 0xDD, 0xA0, 0xF6, 0xE6,
    0x06, 0x8E, 0x1C, 0x84, 0x1F, 0x3F, 0x35, 0xD5, 0x30, 0xA1, 0x4A, 0xC0,
    0x77, 0x58, 0xD6, 0x84, 0x4E, 0x58, 0x88, 0xC6, 0x96, 0x02, 0xF7, 0x82,
    0xCC, 0x49, 0xBC, 0xF7, 0x78, 0xE8, 0x98, 0xA8, 0xEF, 0xA8, 0x63, 0x6E,
    0x6E, 0x3C, 0x7A, 0xFF, 0x18, 0xB3, 0xAA, 0xFE, 0x8E, 0xFC, 0x76, 0x2A,
    0x97, 0xC5, 0xE8, 0x00, 0x63, 0xD2, 0x61, 0x5B, 0x31, 0xBA, 0xDD, 0xB4,
    0x5C, 0xA7, 0xEE, 0x6F, 0x80, 0xDA, 0x78, 0x33, 0x7D, 0xD6, 0x9D, 0x90,
    0xBD, 0xDC, 0xD1, 0x56, 0xDD, 0xA1, 0x3F, 0x87, 0x21, 0x76, 0xB0, 0xB1,
    0xA7, 0x4D, 0xFA, 0x7B, 0x91, 0x11, 0xF5, 0xA9, 0x82, 0xEF, 0x91, 0x74,
    0x68, 0xB0, 0x55, 0x5D, 0x7D, 0x05, 0x19, 0x4F, 0xBE, 0x39, 0x99, 0x77,
    0x77, 0x82, 0xE2, 0x5C, 0x30, 0x90, 0x24, 0xCE, 0xA8, 0xF7, 0x04, 0xC7,
    0x02, 0xF9, 0xF3, 0x95, 0xB8, 0x1A, 0x5E, 0xEE, 0x23, 0x92, 0x7D, 0xB4,
    0x4E, 0x44, 0xA9, 0x56, 0xA3, 0xD2, 0xFD, 0x40, 0x07, 0xCA, 0x71, 0xA3,
    0x97, 0x3E, 0xD3, 0x8D, 0x25, 0x9E, 0x98, 0x42, 0x43, 0x3C, 0xB2, 0x63,
    0xFB, 0x8A, 0x31, 0x54, 0x32, 0xCC, 0x52, 0x88, 0x6D, 0xC5, 0x86, 0xAE,
    0x48, 0x4C, 0xF9, 0x19, 0xCA, 0xAE, 0x81, 0x04, 0x19, 0x29, 0xF0, 0x04,
    0x62, 0xDD, 0x92, 0x59, 0x42, 0x6D, 0xE9, 0x3E, 0xC4, 0x26, 0xC8, 0x39,
    0x61, 0x3C, 0xEC, 0xE5, 0x78, 0x07, 0x38, 0xB2, 0xF9, 0xEB, 0x64, 0x18,
    0xC1, 0x0A, 0x46, 0x16, 0x66, 0x54, 0x43, 0xAC, 0x2F, 0x5B, 0x2F, 0x7F,
    0x63, 0x89, 0xA9, 0x94, 0x65, 0x30, 0x62, 0xB2, 0xD1, 0xB4, 0x2D, 0x7E,
    0xB3, 0xBA, 0xD1, 0xDD, 0x2F, 0x59, 0xA8, 0x3F, 0x92, 0x12, 0xCD, 0x87,
    0x27, 0xF1, 0x29, 0xA5,
};

} // namespace

bool XContentHeader::VerifyPackageSignature()
{
    if (magic != PIRS && magic != LIVE)
        return false;

    BYTE message[0x118];
    io->SetPosition(0x22C);
    io->ReadBytes(message, sizeof(message));

    // The on-disk signature is fully byte-reversed.
    BYTE signature[0x100];
    for (size_t i = 0; i < sizeof(signature); ++i)
        signature[i] = packageSignature[sizeof(signature) - 1 - i];

    struct Candidate
    {
        UINT64 exponent;
        const BYTE *modulus;
    };

    const Candidate pirs[] = {
        { 3, kPirsRetailModulus },
        { 3, kPirsDevkitModulus },
    };
    const Candidate live[] = {
        { 65537, kLiveRetailModulus },
        { 3, kLiveDevkitModulus },
    };

    const Candidate *candidates = (magic == PIRS) ? pirs : live;
    const size_t count = (magic == PIRS) ? (sizeof(pirs) / sizeof(pirs[0]))
                                        : (sizeof(live) / sizeof(live[0]));

    for (size_t i = 0; i < count; ++i)
    {
        try
        {
            if (XeCrypt::Pkcs1Verify(message, sizeof(message), signature, sizeof(signature),
                    candidates[i].exponent, const_cast<BYTE *>(candidates[i].modulus)))
                return true;
        }
        catch (...)
        {
            // Try the next candidate key.
        }
    }

    return false;
}

void XContentHeader::readMetadata()
{
    // store errors thrown
    stringstream except;

    io->SetPosition(0);

    if ((flags & MetadataIsPEC) == 0)
    {
        // Read as an integer first: casting an arbitrary DWORD to Magic creates
        // an out-of-range enum value, which is UB before it is even compared.
        const DWORD magicValue = io->ReadDword();
        if (magicValue != CON && magicValue != LIVE && magicValue != PIRS)
        {
            except << "XContentHeader: Content signature type 0x" << hex << magicValue << " is invalid.\n";
            throw except.str();
        }
        magic = static_cast<Magic>(magicValue);

        if (magic == CON)
            ReadCertificateEx(&certificate, io, 4);
        else if (magic == LIVE || magic == PIRS)
            io->ReadBytes(packageSignature, 0x100);

        io->SetPosition(0x22C);

        // read licensing data
        for(int i = 0; i < 0x10; i++)
        {
            UINT64 tempYo = io->ReadUInt64();
            licenseData[i].type = EnumTraits<LicenseType>::FromRaw(tempYo >> 48);
            licenseData[i].data = (tempYo & 0xFFFFFFFFFFFF);
            licenseData[i].bits = io->ReadDword();
            licenseData[i].flags = io->ReadDword();

            switch (licenseData[i].type)
            {
                case Unused:
                case Unrestricted:
                case ConsoleProfileLicense:
                case WindowsProfileLicense:
                case ConsoleLicense:
                case MediaFlags:
                case KeyVaultPrivileges:
                case HyperVisorFlags:
                case UserPrivileges:
                    break;
                default:
                    except << "XContentHeader: Invalid license type at index " << i << ".\n";
                    throw except.str();
            }
        }

        io->ReadBytes(headerHash, 0x14);

        headerSize = io->ReadDword();

        // to do: make sure it's a valid type
        contentType = io->ReadEnum<ContentType>(4);

        metaDataVersion = io->ReadDword();
        contentSize = io->ReadUInt64();
        mediaID = io->ReadDword();
        version = io->ReadDword();
        baseVersion = io->ReadDword();
        titleID = io->ReadDword();
        platform = io->ReadByte();
        executableType = io->ReadByte();
        discNumber = io->ReadByte();
        discInSet = io->ReadByte();
        savegameID = io->ReadDword();
        io->ReadBytes(consoleID, 5);
        io->ReadBytes(profileID, 8);

        // read the file system type
        io->SetPosition(0x3A9);
        fileSystem = io->ReadEnum<FileSystem>(4);
        if (fileSystem != FileSystemSTFS && fileSystem != FileSystemSVOD)
            throw string("XContentHeader: Invalid file system. Only STFS and SVOD are supported.\n");

        // read volume descriptor
        if (fileSystem == FileSystemSTFS)
            ReadStfsVolumeDescriptorEx(&stfsVolumeDescriptor, io, 0x379);
        else if (fileSystem == FileSystemSVOD)
            ReadSvodVolumeDescriptorEx(&svodVolumeDescriptor, io);

        // These follow the 0x24-byte volume descriptor (0x379..0x39D). Seek
        // explicitly so a descriptor change cannot silently shift them.
        io->SetPosition(0x39D);
        dataFileCount = io->ReadDword();
        dataFileCombinedSize = io->ReadUInt64();

        // read the avatar metadata if needed
        if (contentType == AvatarItem)
        {
            io->SetPosition(0x3D9);
            io->SwapEndian();

            subCategory = io->ReadEnum<AssetSubcategory>(4);
            colorizable = io->ReadDword();

            io->SwapEndian();

            io->ReadBytes(guid, 0x10);
            skeletonVersion = io->ReadEnum<SkeletonVersion>(1);

            // Retail PIRS avatar items exist with skeleton version 0
            // (legacy/unknown). This range is corpus-derived: neither the
            // kernel, XAM nor the public XDK headers define a validator for the
            // content-header skeleton byte.
            if (skeletonVersion == SkeletonVersionUnknown)
                throw string("XContentHeader: Invalid skeleton version.");
        }
        else if (contentType == Video) // there may be other content types with this metadata
        {
            io->SetPosition(0x3D9);

            io->ReadBytes(seriesID, 0x10);
            io->ReadBytes(seasonID, 0x10);

            seasonNumber = io->ReadWord();
            episodeNumber = io->ReadWord();
        }

        // skip padding
        io->SetPosition(0x3FD);

        io->ReadBytes(deviceID, 0x14);
        displayName = io->ReadWString();
        io->SetPosition(0xD11);
        displayDescription = io->ReadWString();
        io->SetPosition(0x1611);
        publisherName = io->ReadWString(0x80);
        io->SetPosition(0x1691);
        titleName = io->ReadWString(0x80);
        io->SetPosition(0x1711);
        transferFlags = io->ReadByte();

        // read image sizes
        thumbnailImageSize = io->ReadDword();
        titleThumbnailImageSize = io->ReadDword();

        thumbnailImage.resize(thumbnailImageSize);
        titleThumbnailImage.resize(titleThumbnailImageSize);

        // read images
        if (thumbnailImageSize > 0)
            io->ReadBytes(thumbnailImage.data(), thumbnailImageSize);
        io->SetPosition(0x571A);

        if (titleThumbnailImageSize > 0)
            io->ReadBytes(titleThumbnailImage.data(), titleThumbnailImageSize);
        io->SetPosition(0x971A);

        if (((headerSize + 0xFFF) & 0xFFFFF000) - 0x971A < 0x15F4)
            return;

        installerType = io->ReadEnum<InstallerType>(4);
        if (installerType == InstallerTypeUnknown)
            throw string("XContentHeader: Invalid Installer Type value.");

        switch (installerType)
        {
            case SystemUpdate:
            case TitleUpdate:
            {
                DWORD tempbv = io->ReadDword();
                installerBaseVersion.major = tempbv >> 28;
                installerBaseVersion.minor = (tempbv >> 24) & 0xF;
                installerBaseVersion.build = (tempbv >> 8) & 0xFFFF;
                installerBaseVersion.revision = tempbv & 0xFF;

                DWORD tempv = io->ReadDword();
                installerVersion.major = tempv >> 28;
                installerVersion.minor = (tempv >> 24) & 0xF;
                installerVersion.build = (tempv >> 8) & 0xFFFF;
                installerVersion.revision = tempv & 0xFF;

                break;
            }
            case SystemUpdateProgressCache:
            case TitleUpdateProgressCache:
            case TitleContentProgressCache:
            {
                resumeState = io->ReadEnum<OnlineContentResumeState>(4);
                currentFileIndex = io->ReadDword();
                currentFileOffset = io->ReadUInt64();
                bytesProcessed = io->ReadUInt64();

                WINFILETIME time;
                time.dwHighDateTime = io->ReadDword();
                time.dwLowDateTime = io->ReadDword();
                lastModified = XdbfHelpers::FILETIMEtoTimeT(time);

                io->ReadBytes(cabResumeData, 0x15D0);
                break;
            }
            case None:
                break;
            default:
                throw string("XContentHeader: Invalid Installer Type value.");
        }

#ifdef DEBUG
        if(metaDataVersion != 2)
            throw string("XContentHeader: Metadata version is not 2.\n");
#endif
    }
    else
    {
        ReadCertificateEx(&certificate, io, 0);
        io->ReadBytes(headerHash, 0x14);

        ReadStfsVolumeDescriptorEx(&stfsVolumeDescriptor, io, 0x244);

        // *skip missing int*
        io->SetPosition(0x26C);
        io->ReadBytes(profileID, 8);
        enabled = (bool)io->ReadByte();
        io->ReadBytes(consoleID, 5);

        // anything between 1 and 0x1000 works, inclusive
        headerSize = 0x1000;
    }
}

void XContentHeader::WriteCertificate()
{
    if (magic != CON && (flags & MetadataIsPEC) == 0)
        throw string("XContentHeader: Error writing certificate. Package is strong signed and therefore doesn't have a certificate.\n");

    WriteCertificateEx(&certificate, io, (flags & MetadataIsPEC) ? 0 : 4);
}

void XContentHeader::FixHeaderHash()
{
    DWORD headerStart = ((flags & MetadataIsPEC) ? 0x23C : 0x344);

    // calculate header size / first hash table address
    DWORD calculated = ((headerSize + 0xFFF) & 0xFFFFF000);
    io->SetPosition(0, ios_base::end);
    calculated = (io->GetPosition() < calculated) ? (DWORD)io->GetPosition() : calculated;
    DWORD realHeaderSize = calculated - headerStart;

    std::vector<BYTE> data(realHeaderSize);

    // seek to the position
    io->SetPosition(headerStart);
    io->ReadBytes(data.data(), realHeaderSize);

    // hash the data
    const auto sha1 = Botan::HashFunction::create_or_throw("SHA-1");
    sha1->update(data.data(), realHeaderSize);
    sha1->final(headerHash);

    // Write the new hash to the file
    io->SetPosition(((flags & MetadataIsPEC) ? 0x228 : 0x32C));
    io->Write(headerHash, 0x14);
    io->Flush();
}

void XContentHeader::WriteMetaData()
{
    // seek to the beginning of the file
    io->SetPosition(0);

    if ((flags & MetadataIsPEC) == 0)
    {

        io->Write((DWORD)magic);

        if (magic == CON)
            WriteCertificate();
        else if (magic == PIRS || magic == LIVE)
            io->Write(packageSignature, 0x100);
        else
        {
            stringstream except;
            except << "XContentHeader: Content signature type 0x" << hex << (DWORD)magic << " is invalid.\n";
            throw except.str();
        }

        // Write the licensing data
        io->SetPosition(0x22C);
        for (DWORD i = 0; i < 0x10; i++)
        {
            io->Write(((UINT64)licenseData[i].type << 48) | (UINT64)licenseData[i].data);
            io->Write(licenseData[i].bits);
            io->Write(licenseData[i].flags);
        }

        io->Write(headerHash, 0x14);
        io->Write(headerSize);
        io->Write((DWORD)contentType);
        io->Write((DWORD)metaDataVersion);
        io->Write((UINT64)contentSize);
        io->Write(mediaID);
        io->Write(version);
        io->Write(baseVersion);
        io->Write(titleID);
        io->Write((BYTE)platform);
        io->Write((BYTE)executableType);
        io->Write((BYTE)discNumber);
        io->Write((BYTE)discInSet);
        io->Write((DWORD)savegameID);
        io->Write(consoleID, 5);
        io->Write(profileID, 8);

        WriteVolumeDescriptor();

        // Fixed position: the volume descriptor always ends at 0x39D.
        io->SetPosition(0x39D);
        io->Write(dataFileCount);
        io->Write(dataFileCombinedSize);

        // Write the avatar asset metadata if needed
        if (contentType == AvatarItem)
        {
            io->SetPosition(0x3D9);
            io->SwapEndian();

            io->Write((DWORD)subCategory);
            io->Write((DWORD)colorizable);

            io->SwapEndian();

            io->Write(guid, 0x10);
            io->Write((BYTE)skeletonVersion);
        }
        else if (contentType == Video)
        {
            io->SetPosition(0x3D9);

            io->Write(seriesID, 0x10);
            io->ReadBytes(seasonID, 0x10);

            io->Write(seasonNumber);
            io->Write(episodeNumber);
        }

        // skip padding
        io->SetPosition(0x3FD);

        io->Write(deviceID, 0x14);
        io->Write(displayName);

        io->SetPosition(0xD11);
        io->Write(displayDescription);

        io->SetPosition(0x1611);
        io->Write(publisherName);

        io->SetPosition(0x1691);
        io->Write(titleName);

        io->SetPosition(0x1711);
        io->Write((BYTE)transferFlags);

        io->Write(thumbnailImageSize);
        io->Write(titleThumbnailImageSize);

        if (thumbnailImageSize > 0)
            io->Write(thumbnailImage.data(), thumbnailImageSize);
        io->SetPosition(0x571A);
        if (titleThumbnailImageSize > 0)
            io->Write(titleThumbnailImage.data(), titleThumbnailImageSize);

        if (((headerSize + 0xFFF) & 0xFFFFF000) - 0x971A < 0x15F4)
            return;

        io->Write((DWORD)installerType);
        switch (installerType)
        {
            case SystemUpdate:
            case TitleUpdate:
            {
                DWORD tempbv = 0;
                tempbv |= (installerBaseVersion.major & 0xF) << 28;
                tempbv |= (installerBaseVersion.minor & 0xF) << 24;
                tempbv |= (installerBaseVersion.build & 0xFFFF) << 8;
                tempbv |= installerBaseVersion.revision & 0xFF;
                io->Write(tempbv);

                DWORD tempv = 0;
                tempv |= (installerVersion.major & 0xF) << 28;
                tempv |= (installerVersion.minor & 0xF) << 24;
                tempv |= (installerVersion.build & 0xFFFF) << 8;
                tempv |= installerVersion.revision & 0xFF;
                io->Write(tempv);

                break;
            }

            case SystemUpdateProgressCache:
            case TitleUpdateProgressCache:
            case TitleContentProgressCache:
            {
                io->Write((DWORD)resumeState);
                io->Write(currentFileIndex);
                io->Write(currentFileOffset);
                io->Write(bytesProcessed);

                WINFILETIME time = XdbfHelpers::TimeTtoFILETIME(lastModified);
                io->Write(time.dwHighDateTime);
                io->Write(time.dwLowDateTime);

                io->Write(cabResumeData, 0x15D0);
                break;
            }

            case None:
                break;
        }
    }
    else
    {
        memcpy(consoleID, certificate.ownerConsoleID, 5);

        WriteCertificateEx(&certificate, io, 0);
        io->Write(headerHash, 0x14);

        WriteStfsVolumeDescriptorEx(&stfsVolumeDescriptor, io, 0x244);

        // *skip missing int*
        io->SetPosition(0x26C);
        io->Write(profileID, 8);
        io->Write((BYTE)enabled);
        io->Write(consoleID, 5);
    }
    io->Flush();
}

void XContentHeader::WriteVolumeDescriptor()
{
    if (fileSystem == FileSystemSTFS)
        WriteStfsVolumeDescriptorEx(&stfsVolumeDescriptor, io, (flags & MetadataIsPEC) ? 0x244 : 0x379);
    else if (fileSystem == FileSystemSVOD)
        WriteSvodVolumeDescriptorEx(&svodVolumeDescriptor, io);
}

void XContentHeader::ResignHeader(string kvPath)
{
    FileIO kvIo(kvPath);
    ResignHeader(kvIo);
}

void XContentHeader::ResignHeader(BYTE* kvData, size_t length)
{
    MemoryIO kvIo(kvData, length);
    ResignHeader(kvIo);
}

void XContentHeader::ResignHeader(BaseIO& kvIo)
{
    kvIo.SetPosition(0, ios_base::end);

    DWORD adder = 0;
    if (kvIo.GetPosition() == 0x4000)
        adder = 0x10;

    DWORD headerStart, size, hashLoc, toSignLoc, consoleIDLoc;

    // set the headerStart
    if (flags & MetadataIsPEC)
    {
        headerStart = 0x23C;
        hashLoc = 0x228;
        size = 0xDC4;
        toSignLoc = 0x23C;
        consoleIDLoc = 0x275;
    }
    else
    {
        headerStart = 0x344;
        hashLoc = 0x32C;
        size = 0x118;
        toSignLoc = 0x22C;
        consoleIDLoc = 0x36C;
    }

    // calculate header size / first hash table address
    DWORD calculated = ((headerSize + 0xFFF) & 0xFFFFF000);
    io->SetPosition(0, ios_base::end);
    calculated = (io->GetPosition() < calculated) ? (DWORD)io->GetPosition() : calculated;
    DWORD realHeaderSize = calculated - headerStart;

    // read the certificate header verbatim (part number + reserved + privileges)
    kvIo.SetPosition(0x9B8 + adder);
    certificate.publicKeyCertificateSize = kvIo.ReadWord();
    kvIo.ReadBytes(certificate.ownerConsoleID, 5);

    char tempPartNum[0x15] = {0};
    kvIo.ReadBytes((BYTE*)tempPartNum, 0x11);
    certificate.ownerConsolePartNumber.assign(tempPartNum, 0x11);

    // console type is a big-endian dword at certificate offset 0x18
    kvIo.SetPosition(0x9B8 + adder + 0x18);
    DWORD consoleType = kvIo.ReadDword();
    certificate.ownerConsoleType = static_cast<ConsoleType>(consoleType & 3);
    certificate.consoleTypeFlags = static_cast<ConsoleTypeFlags>(consoleType & 0xFFFFFFFC);

    kvIo.SetPosition(0x9B8 + adder + 0x1C);
    char tempGenDate[9] = {0};
    kvIo.ReadBytes((BYTE*)tempGenDate, 8);
    certificate.dateGeneration = string(tempGenDate);

    certificate.publicExponent = kvIo.ReadDword();
    kvIo.ReadBytes(certificate.publicModulus, 0x80);
    kvIo.ReadBytes(certificate.certificateSignature, 0x100);

    // read the keys for signing
    BYTE nData[0x80];
    BYTE pData[0x40];
    BYTE qData[0x40];

    kvIo.SetPosition(0x298 + adder);
    kvIo.ReadBytes(nData, 0x80);
    kvIo.ReadBytes(pData, 0x40);
    kvIo.ReadBytes(qData, 0x40);

    // 8 byte swap all necessary keys
    XeCrypt::BnQw_SwapDwQwLeBe(nData, 0x80);
    XeCrypt::BnQw_SwapDwQwLeBe(pData, 0x40);
    XeCrypt::BnQw_SwapDwQwLeBe(qData, 0x40);

    try
    {
        // get the keys ready for signing
        Botan::BigInt n(nData, 0x80);
        Botan::BigInt p(pData, 0x40);
        Botan::BigInt q(qData, 0x40);

        Botan::AutoSeeded_RNG rng;
        Botan::RSA_PrivateKey pkey(p, q, 0x10001, 0, n);

        // Write the console id
        io->SetPosition(consoleIDLoc);
        io->Write(certificate.ownerConsoleID, 5);

        // read the data to hash
    std::vector<BYTE> buffer(realHeaderSize);
    io->SetPosition(headerStart);
    io->ReadBytes(buffer.data(), realHeaderSize);

    // hash the header
    const auto sha1 = Botan::HashFunction::create_or_throw("SHA-1");
    sha1->update(buffer.data(), realHeaderSize);
    sha1->final(headerHash);

        io->SetPosition(hashLoc);
        io->Write(headerHash, 0x14);

        io->SetPosition(toSignLoc);

    std::vector<BYTE> dataToSign(size);
    io->ReadBytes(dataToSign.data(), size);

        Botan::PK_Signer signer(pkey, rng, "EMSA3(SHA-1)");

    auto signature = signer.sign_message(reinterpret_cast<const unsigned char*>(dataToSign.data()), size,
                rng);

    // 8 byte swap the new signature
    XeCrypt::BnQw_SwapDwQwLeBe(signature.data(), 0x80);

        // reverse the new signature every 8 bytes
        for (int i = 0; i < 0x10; i++)
            FileIO::ReverseGenericArray(&signature[i * 8], 1, 8);

        // Write the certficate
        memcpy(certificate.signature, signature.data(), 0x80);
        WriteCertificate();
    }
    catch (const std::exception& e)
    {
        // Convert Botan exceptions to XboxInternals string exceptions
        throw string("XContentHeader: Cryptographic error during resign - ") + e.what() + "\n";
    }
}

XContentHeader::~XContentHeader() = default;


