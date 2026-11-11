#include <XboxInternals/IO/DeviceIO.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#if __APPLE__
#include <sys/disk.h>
#elif __linux__
#include <linux/fs.h>
#endif
#include <unistd.h>
#endif

#ifdef __linux__
#define SECTOR_COUNT BLKGETSIZE
#define SECTOR_SIZE BLKSSZGET
#elif __APPLE__
#define SECTOR_COUNT DKIOCGETBLOCKCOUNT
#define SECTOR_SIZE DKIOCGETBLOCKSIZE
#endif

namespace
{
    DeviceErrorCode mapPosixError(int error)
    {
        switch (error)
        {
        case EACCES:
        case EPERM:
            return DeviceErrorCode::PermissionDenied;
        case ENOENT:
            return DeviceErrorCode::NotFound;
        case EBUSY:
            return DeviceErrorCode::Busy;
        default:
            return DeviceErrorCode::IoError;
        }
    }

#ifdef _WIN32
    DeviceErrorCode mapWindowsError(DWORD error)
    {
        switch (error)
        {
        case ERROR_ACCESS_DENIED:
            return DeviceErrorCode::PermissionDenied;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return DeviceErrorCode::NotFound;
        case ERROR_BUSY:
        case ERROR_SHARING_VIOLATION:
            return DeviceErrorCode::Busy;
        default:
            return DeviceErrorCode::IoError;
        }
    }
#endif
}

class DeviceIO::Impl
{
public:
#ifdef _WIN32
    Impl() : deviceHandle(INVALID_HANDLE_VALUE), offset{} {}
    HANDLE deviceHandle;
    OVERLAPPED offset;
#else
    Impl() : device(-1), offset(0) {}
    int device;
    INT64 offset;
#endif

    DWORD sectorSize = FAT_SECTOR_SIZE;
    BYTE *bounce = nullptr;
    size_t bounceCapacity = 0;
};

DeviceIO::DeviceIO(std::string devicePath, DeviceErrorCode *error) :
    impl(std::make_unique<Impl>()), pos(0), readOnly(false)
{
    if (error)
        *error = DeviceErrorCode::None;

    // convert it to a wstring
    std::wstring wsDevicePath;
    wsDevicePath.assign(devicePath.begin(), devicePath.end());

    // load the device
    loadDevice(wsDevicePath, error);
}

DeviceIO::DeviceIO(std::wstring devicePath, DeviceErrorCode *error) :
    impl(std::make_unique<Impl>()), pos(0), readOnly(false)
{
    if (error)
        *error = DeviceErrorCode::None;

    // load the device
    loadDevice(devicePath, error);
}

DeviceIO::~DeviceIO()
{
    Close();
}

namespace
{
    constexpr size_t kBounceTarget = 0x40000; // 256 KiB per physical transfer

    BYTE *AllocateAligned(size_t size, size_t alignment)
    {
#ifdef _WIN32
        return static_cast<BYTE *>(_aligned_malloc(size, alignment));
#else
        void *aligned = nullptr;
        if (posix_memalign(&aligned, alignment, size) != 0)
            return nullptr;
        return static_cast<BYTE *>(aligned);
#endif
    }

    void FreeAligned(BYTE *buffer)
    {
#ifdef _WIN32
        _aligned_free(buffer);
#else
        free(buffer);
#endif
    }
}

UINT64 DeviceIO::DownToSector(UINT64 address) const
{
    return address - (address % impl->sectorSize);
}

size_t DeviceIO::RoundUpToSector(size_t length) const
{
    const size_t sector = impl->sectorSize;
    return ((length + sector - 1) / sector) * sector;
}

void DeviceIO::ReadRaw(BYTE *buffer, DWORD length)
{
#ifdef _WIN32
    if (!ReadFile(impl->deviceHandle, buffer, length, nullptr, &impl->offset))
        throw std::string("DeviceIO: Error reading from device, may be disconnected.\n");
#else
    size_t done = 0;
    while (done < length)
    {
        const ssize_t count = read(impl->device, buffer + done, length - done);
        if (count <= 0)
            throw std::string("DeviceIO: Error reading from device.\n");
        done += static_cast<size_t>(count);
    }
#endif
}

void DeviceIO::WriteRaw(const BYTE *buffer, DWORD length)
{
#ifdef _WIN32
    if (!WriteFile(impl->deviceHandle, buffer, length, nullptr, &impl->offset))
        throw std::string("DeviceIO: Error writing to the device, may be disconnected.\n");
#else
    size_t done = 0;
    while (done < length)
    {
        const ssize_t count = write(impl->device, buffer + done, length - done);
        if (count <= 0)
            throw std::string("DeviceIO: Error writing to device.\n");
        done += static_cast<size_t>(count);
    }
#endif
}

void DeviceIO::ReadBytes(BYTE *outBuffer, DWORD len)
{
    if (len == 0)
        return;

    const UINT64 requestStart = pos;
    const UINT64 requestEnd = pos + len;
    UINT64 cursor = DownToSector(requestStart);
    UINT64 remaining = RoundUpToSector(static_cast<size_t>(requestStart - cursor) + len);

    while (remaining > 0)
    {
        const size_t chunk = static_cast<size_t>(std::min<UINT64>(remaining, impl->bounceCapacity));
        SetPosition(cursor);
        ReadRaw(impl->bounce, static_cast<DWORD>(chunk));

        const UINT64 chunkEnd = cursor + chunk;
        const UINT64 copyStart = std::max<UINT64>(cursor, requestStart);
        const UINT64 copyEnd = std::min<UINT64>(chunkEnd, requestEnd);
        if (copyEnd > copyStart)
        {
            std::memcpy(outBuffer + (copyStart - requestStart),
                        impl->bounce + (copyStart - cursor),
                        static_cast<size_t>(copyEnd - copyStart));
        }

        cursor = chunkEnd;
        remaining -= chunk;
    }

    SetPosition(requestEnd);
}

void DeviceIO::WriteBytes(BYTE *buffer, DWORD len)
{
    if (readOnly)
        throw std::string("DeviceIO: Device was opened read-only; elevated access is required for writes.\n");

    if (len == 0)
        return;

    const UINT64 requestStart = pos;
    const UINT64 requestEnd = pos + len;

    if (requestEnd > Length())
        throw std::string("DeviceIO: Cannot Write beyond the end of the stream.\n");

    UINT64 cursor = DownToSector(requestStart);
    UINT64 remaining = RoundUpToSector(static_cast<size_t>(requestStart - cursor) + len);

    while (remaining > 0)
    {
        const size_t chunk = static_cast<size_t>(std::min<UINT64>(remaining, impl->bounceCapacity));
        const UINT64 chunkEnd = cursor + chunk;
        const UINT64 copyStart = std::max<UINT64>(cursor, requestStart);
        const UINT64 copyEnd = std::min<UINT64>(chunkEnd, requestEnd);

        // Only skip the read when the caller's data covers the whole transfer
        // and it happens to be sector aligned.
        const bool coversChunk = chunkEnd <= requestEnd && cursor >= requestStart;

        SetPosition(cursor);
        if (!coversChunk)
            ReadRaw(impl->bounce, static_cast<DWORD>(chunk));

        if (copyEnd > copyStart)
        {
            std::memcpy(impl->bounce + (copyStart - cursor),
                        buffer + (copyStart - requestStart),
                        static_cast<size_t>(copyEnd - copyStart));
        }

        SetPosition(cursor);
        WriteRaw(impl->bounce, static_cast<DWORD>(chunk));

        cursor = chunkEnd;
        remaining -= chunk;
    }

    SetPosition(requestEnd);
}


UINT64 DeviceIO::Length()
{
    UINT64 length;
#ifdef _WIN32
    DISK_GEOMETRY geometry;
    DWORD bytesReturned;

    memset(&geometry, 0, sizeof(DISK_GEOMETRY));
    DeviceIoControl(
        impl->deviceHandle,			// Device to get the geometry from
        IOCTL_DISK_GET_DRIVE_GEOMETRY,	// Action we're taking (getting the geometry)
        NULL,							// Not used since this requires no input data
        0,								// No data
        &geometry,						// Output struct
        sizeof(DISK_GEOMETRY),			// Output buffer size
        &bytesReturned,					// Number of bytes returned
        NULL);							// Not used

    UINT64 cylinders = (UINT64)((UINT64)geometry.Cylinders.HighPart << 32) |
            geometry.Cylinders.LowPart; // Convert the BIG_INTEGER to UINT64
    length = cylinders * (UINT64)geometry.TracksPerCylinder	* (UINT64)geometry.SectorsPerTrack *
             (UINT64)geometry.BytesPerSector;
#else
    UINT64 numberOfSectors = 0;
    int device = impl->device;

    ioctl(device, SECTOR_COUNT, &numberOfSectors);

    UINT64 sectorSize = 0;
    ioctl(device, SECTOR_SIZE, &sectorSize);

    // calculate the length in bytes
    length = (UINT64)numberOfSectors * (UINT64)sectorSize;
#endif

    return length;
}

void DeviceIO::SetPosition(UINT64 address, std::ios_base::seekdir dir)
{
    if (dir != std::ios_base::beg)
        throw std::string("DeviceIO: Unsupported seek direction\n");

    pos = address;
    address = DownToSector(address); // Round the position down to the nearest sector offset

#ifdef _WIN32
    impl->offset.Offset = (DWORD)address;
    impl->offset.OffsetHigh = (DWORD)(address >> 32);
#else
    impl->offset = address;
#endif

#ifdef __linux__
    lseek64(impl->device, address, SEEK_SET);
#else
#ifdef _WIN32
    LONG addressHigh = impl->offset.OffsetHigh;
    SetFilePointer(impl->deviceHandle, impl->offset.Offset, &addressHigh, FILE_BEGIN);
#else
    lseek(impl->device, address, SEEK_SET);
#endif
#endif
}

UINT64 DeviceIO::realPosition()
{
#ifdef _WIN32
    return (UINT64)(((UINT64)impl->offset.OffsetHigh << 32) | impl->offset.Offset);
#else
    return impl->offset;
#endif
}

UINT64 DeviceIO::GetPosition()
{
    return pos;
}

void DeviceIO::Close()
{
    if (impl && impl->bounce != nullptr)
    {
        FreeAligned(impl->bounce);
        impl->bounce = nullptr;
        impl->bounceCapacity = 0;
    }

#if defined _WIN32
    if (impl && impl->deviceHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(impl->deviceHandle);
        impl->deviceHandle = INVALID_HANDLE_VALUE;
    }
#else
    if (impl && impl->device != -1) {
        close(impl->device);
        impl->device = -1;
    }
#endif
}

void DeviceIO::loadDevice(std::wstring devicePath, DeviceErrorCode *error)
{
    readOnly = false;

#ifdef _WIN32
    pos = 0;
    memset(&impl->offset, 0, sizeof(OVERLAPPED));

    // Try read/write first; fall back to read-only for browsing without UAC.
    impl->deviceHandle = CreateFile(
                devicePath.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_ATTRIBUTE_DEVICE,
                NULL);

    if (impl->deviceHandle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED)
    {
        impl->deviceHandle = CreateFile(
                    devicePath.c_str(),
                    GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_NO_BUFFERING | FILE_ATTRIBUTE_DEVICE,
                    NULL);

        if (impl->deviceHandle != INVALID_HANDLE_VALUE)
            readOnly = true;
    }

    if (impl->deviceHandle == INVALID_HANDLE_VALUE)
    {
        const DWORD lastError = GetLastError();
        if (error)
            *error = mapWindowsError(lastError);
        throw std::string("DeviceIO: Could not open device handle (error " +
                          std::to_string(lastError) + ").\n");
    }
#else

    // need to convert this into a regular string, since open takes a char*
    std::string tempPath(devicePath.begin(), devicePath.end());

    int firstErrno = 0;
    impl->device = open(tempPath.c_str(), O_RDWR);
    if (impl->device == -1)
        firstErrno = errno;

    if (impl->device == -1 && (firstErrno == EACCES || firstErrno == EROFS))
    {
        impl->device = open(tempPath.c_str(), O_RDONLY);
        if (impl->device != -1)
            readOnly = true;
    }

    if (impl->device == -1)
    {
        if (error)
            *error = mapPosixError(firstErrno);
        throw std::string("DeviceIO: Error opening device.\n" + std::string(strerror(firstErrno)));
    }
#endif

    // Logical sector size: 4Kn media needs 4096-byte aligned transfers, and both
    // FILE_FLAG_NO_BUFFERING and raw block I/O reject anything else.
#ifdef _WIN32
    DISK_GEOMETRY geometry {};
    DWORD geometryBytes = 0;
    if (DeviceIoControl(impl->deviceHandle, IOCTL_DISK_GET_DRIVE_GEOMETRY, nullptr, 0,
                        &geometry, sizeof(geometry), &geometryBytes, nullptr) &&
        geometry.BytesPerSector != 0)
    {
        impl->sectorSize = geometry.BytesPerSector;
    }
#else
    int logicalSectorSize = 0;
    if (ioctl(impl->device, SECTOR_SIZE, &logicalSectorSize) == 0 && logicalSectorSize > 0)
        impl->sectorSize = static_cast<DWORD>(logicalSectorSize);
#endif

    const size_t capacity = RoundUpToSector(kBounceTarget);
    impl->bounce = AllocateAligned(capacity, impl->sectorSize);
    if (impl->bounce == nullptr)
        throw std::string("DeviceIO: Could not allocate the aligned I/O buffer.\n");
    impl->bounceCapacity = capacity;
}

void DeviceIO::Flush()
{
#ifdef _WIN32
    if (!readOnly)
        FlushFileBuffers(impl->deviceHandle);
#endif
}


