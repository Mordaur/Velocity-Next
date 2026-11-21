#include "device_validation.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#if defined(_WIN32)
// Windows only needs the textual path check here; the bus/system-disk checks
// run on the opened handle in main.cpp.
#else
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/sysmacros.h>
#elif defined(__APPLE__)
#include <sys/mount.h>
#include <sys/param.h>
#endif
#endif

namespace velocity::helper {
namespace {

bool starts_with(const std::string &value, const char *prefix)
{
    return value.rfind(prefix, 0) == 0;
}

bool contains_dotdot(const std::string &value)
{
    return value.find("..") != std::string::npos;
}

std::string read_text_file(const std::string &path)
{
    std::ifstream file(path);
    if (!file)
        return {};

    std::string value;
    std::getline(file, value);
    return value;
}

#if defined(__linux__)

namespace fs = std::filesystem;

bool directory_has_entries(const fs::path &path)
{
    std::error_code ec;
    if (!fs::is_directory(path, ec))
        return false;

    fs::directory_iterator iterator(path, ec);
    if (ec)
        return false;

    return iterator != fs::directory_iterator();
}

bool disk_has_holders(const std::string &disk)
{
    const fs::path base = "/sys/block/" + disk;
    if (directory_has_entries(base / "holders"))
        return true;

    // A partition held by dm/md/LVM means the whole disk is in use.
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(base, ec))
    {
        if (ec)
            break;
        if (entry.is_directory(ec) && directory_has_entries(entry.path() / "holders"))
            return true;
    }

    return false;
}

bool removable_or_usb(const std::string &disk)
{
    if (read_text_file("/sys/block/" + disk + "/removable") == "1")
        return true;

    std::error_code ec;
    const fs::path deviceNode = fs::canonical("/sys/block/" + disk + "/device", ec);
    return !ec && deviceNode.string().find("/usb") != std::string::npos;
}

bool read_at(int fd, uint64_t offset, unsigned char *buffer, size_t length)
{
    const ssize_t done = pread(fd, buffer, length, static_cast<off_t>(offset));
    return done == static_cast<ssize_t>(length);
}

bool fat_boot_sector(const unsigned char *sector)
{
    return IsFatBootSector(sector, 512);
}

bool has_fat_filesystem(int fd)
{
    unsigned char sector[512] = {};
    if (!read_at(fd, 0, sector, sizeof(sector)))
        return false;
    if (IsGptProtectiveMbr(sector, sizeof(sector)))
        return false; // The Xbox 360 only mounts MBR volumes.
    if (fat_boot_sector(sector))
        return true;

    // MBR: check the first FAT partition's boot sector.
    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return false;

    for (int i = 0; i < 4; ++i)
    {
        const unsigned char *entry = sector + 0x1BE + (i * 16);
        if (entry[4] == 0)
            continue;
        if (entry[4] == 0xEE)
            return false; // GPT protective MBR: the Xbox 360 only reads MBR.

        const uint64_t start = static_cast<uint64_t>(entry[8]) |
                               (static_cast<uint64_t>(entry[9]) << 8) |
                               (static_cast<uint64_t>(entry[10]) << 16) |
                               (static_cast<uint64_t>(entry[11]) << 24);
        unsigned char boot[512] = {};
        if (read_at(fd, start * 512, boot, sizeof(boot)) && fat_boot_sector(boot))
            return true;
    }

    return false;
}

std::string block_device_path(dev_t device)
{
    return "/sys/dev/block/" + std::to_string(major(device)) + ":" +
           std::to_string(minor(device));
}

// Resolve a /sys/dev/block node to the whole-disk name (sda, nvme0n1, dm-0).
std::string disk_name_for_sysfs(const fs::path &node)
{
    std::error_code ec;
    fs::path current = fs::canonical(node, ec);
    if (ec)
        return {};

    // /sys/dev/block/<maj:min> resolves either to .../block/sdX (whole disk)
    // or .../block/sdX/sdX1 (partition, which has a "partition" attribute).
    if (fs::exists(current / "partition", ec))
        current = current.parent_path();

    return current.filename().string();
}

std::string disk_name_for_device(dev_t device)
{
    return disk_name_for_sysfs(block_device_path(device));
}

void collect_disk_names(const fs::path &blockNode, std::set<std::string> &names)
{
    // Direct disk name only. Walking sysfs slaves/ is not done here: the tree
    // is device-specific and a self-referential entry can make the walk hang.
    const std::string disk = disk_name_for_sysfs(blockNode);
    if (!disk.empty())
        names.insert(disk);
}

std::set<std::string> system_disk_names()
{
    std::set<std::string> names;

    struct stat status {};
    if (stat("/", &status) == 0)
        collect_disk_names(block_device_path(status.st_dev), names);
    if (stat("/boot", &status) == 0)
        collect_disk_names(block_device_path(status.st_dev), names);

    std::ifstream swaps("/proc/swaps");
    std::string line;
    std::getline(swaps, line); // header
    while (std::getline(swaps, line))
    {
        const std::string filename = line.substr(0, line.find_first_of(" \t"));
        if (filename.empty())
            continue;
        if (stat(filename.c_str(), &status) == 0 && S_ISBLK(status.st_mode))
            collect_disk_names(block_device_path(status.st_rdev), names);
    }

    return names;
}

bool validate_linux_status(const struct stat &status, std::string &reason)
{
    if (!S_ISBLK(status.st_mode))
    {
        reason = "not a block device";
        return false;
    }

    const std::string disk = disk_name_for_device(status.st_rdev);
    if (disk.empty())
    {
        reason = "cannot map the device to a kernel disk";
        return false;
    }

    static const char *const forbidden[] = {"loop", "ram", "zram", "dm-", "md",
                                            "sr", "nbd", "fd"};
    for (const char *prefix : forbidden)
        if (starts_with(disk, prefix))
        {
            reason = "virtual/optical block devices are not supported";
            return false;
        }

    // Any non-system, unused block device is allowed: internal SATA/eSATA and
    // VM-attached Xbox drives are primary use cases, and USB gating rejected
    // them. O_EXCL in open_device is the hard "not in use" rule.
    if (disk_has_holders(disk))
    {
        reason = "device is held by another block device (dm/md/LVM)";
        return false;
    }

    if (system_disk_names().count(disk) != 0)
    {
        reason = "refusing the root, boot or swap disk";
        return false;
    }

    return true;
}

bool validate_linux(const std::string &path, std::string &reason)
{
    if (!starts_with(path, "/dev/") || contains_dotdot(path))
    {
        reason = "only /dev block devices are allowed";
        return false;
    }

    std::error_code ec;
    const fs::path resolved = fs::canonical(path, ec);
    if (ec || !starts_with(resolved.string(), "/dev/"))
    {
        reason = "device path cannot be resolved";
        return false;
    }

    struct stat status {};
    if (stat(resolved.c_str(), &status) != 0)
    {
        reason = "not a block device";
        return false;
    }

    return validate_linux_status(status, reason);
}

#elif defined(__APPLE__)

bool read_at(int fd, uint64_t offset, unsigned char *buffer, size_t length)
{
    const ssize_t done = pread(fd, buffer, length, static_cast<off_t>(offset));
    return done == static_cast<ssize_t>(length);
}

bool fat_boot_sector(const unsigned char *sector)
{
    return IsFatBootSector(sector, 512);
}

bool has_fat_filesystem(int fd)
{
    unsigned char sector[512] = {};
    if (!read_at(fd, 0, sector, sizeof(sector)))
        return false;
    if (IsGptProtectiveMbr(sector, sizeof(sector)))
        return false; // The Xbox 360 only mounts MBR volumes.
    if (fat_boot_sector(sector))
        return true;

    // MBR: check the first FAT partition's boot sector.
    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return false;

    for (int i = 0; i < 4; ++i)
    {
        const unsigned char *entry = sector + 0x1BE + (i * 16);
        if (entry[4] == 0)
            continue;
        if (entry[4] == 0xEE)
            return false; // GPT protective MBR.

        const uint64_t start = static_cast<uint64_t>(entry[8]) |
                               (static_cast<uint64_t>(entry[9]) << 8) |
                               (static_cast<uint64_t>(entry[10]) << 16) |
                               (static_cast<uint64_t>(entry[11]) << 24);
        unsigned char boot[512] = {};
        if (read_at(fd, start * 512, boot, sizeof(boot)) && fat_boot_sector(boot))
            return true;
    }

    return false;
}

std::string mac_disk_name(const std::string &path)
{
    const std::string base = std::filesystem::path(path).filename().string();
    std::string name = base;
    if (starts_with(name, "r"))
        name.erase(0, 1);
    if (!starts_with(name, "disk"))
        return {};

    const std::string digits = name.substr(4, name.find_first_not_of("0123456789", 4) - 4);
    return digits.empty() ? std::string() : "disk" + digits;
}

bool validate_macos(const std::string &path, std::string &reason)
{
    if (!starts_with(path, "/dev/") || contains_dotdot(path))
    {
        reason = "only /dev disk devices are allowed";
        return false;
    }

    char resolved[PATH_MAX] = {};
    if (realpath(path.c_str(), resolved) == nullptr)
    {
        reason = "device path cannot be resolved";
        return false;
    }

    struct stat status {};
    if (stat(resolved, &status) != 0 || (!S_ISCHR(status.st_mode) && !S_ISBLK(status.st_mode)))
    {
        reason = "not a disk device";
        return false;
    }

    const std::string disk = mac_disk_name(resolved);
    if (disk.empty())
    {
        reason = "unrecognized disk device";
        return false;
    }

    struct statfs root {};
    if (statfs("/", &root) == 0)
    {
        const std::string rootDisk = mac_disk_name(root.f_mntfromname);
        if (!rootDisk.empty() && rootDisk == disk)
        {
            reason = "refusing the boot disk";
            return false;
        }
    }

    // IOKit removable/USB validation is still a follow-up on macOS; the boot
    // disk and non-disk checks are enforced here.
    return true;
}

bool validate_macos_fd(int fd, std::string &reason)
{
    struct stat status {};
    if (fstat(fd, &status) != 0 || !S_ISCHR(status.st_mode))
    {
        reason = "not a raw disk device";
        return false;
    }

    // f_mntfromname can name a partition; compare the whole-disk raw node when
    // it is available. Mapping an arbitrary descriptor to diskN needs IOKit,
    // which is still a follow-up.
    struct statfs root {};
    if (statfs("/", &root) == 0)
    {
        const std::string rootDisk = mac_disk_name(root.f_mntfromname);
        if (!rootDisk.empty())
        {
            struct stat rootStatus {};
            if (stat(("/dev/r" + rootDisk).c_str(), &rootStatus) == 0 &&
                rootStatus.st_rdev == status.st_rdev)
            {
                reason = "refusing the boot disk";
                return false;
            }
        }
    }

    return true;
}

#elif !defined(_WIN32)

bool validate_posix_fallback(const std::string &path, std::string &reason)
{
    if (!starts_with(path, "/dev/") || contains_dotdot(path))
    {
        reason = "only /dev block devices are allowed";
        return false;
    }

    char resolved[PATH_MAX] = {};
    if (realpath(path.c_str(), resolved) == nullptr)
    {
        reason = "device path cannot be resolved";
        return false;
    }

    struct stat status {};
    if (stat(resolved, &status) != 0 || !S_ISBLK(status.st_mode))
    {
        reason = "not a block device";
        return false;
    }

    return true;
}

bool validate_posix_fallback_fd(int fd, std::string &reason)
{
    struct stat status {};
    if (fstat(fd, &status) != 0 || !S_ISBLK(status.st_mode))
    {
        reason = "not a block device";
        return false;
    }

    return true;
}

#endif

} // namespace

bool IsFatBootSector(const unsigned char *sector, std::size_t length)
{
    if (sector == nullptr || length < 512)
        return false;
    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return false;

    const auto isFat = [sector](std::size_t offset) {
        return sector[offset] == 'F' && sector[offset + 1] == 'A' &&
               sector[offset + 2] == 'T';
    };
    return isFat(0x36) || isFat(0x52);
}

bool IsGptProtectiveMbr(const unsigned char *sector, std::size_t length)
{
    if (sector == nullptr || length < 512)
        return false;
    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return false;

    for (int i = 0; i < 4; ++i)
        if (sector[0x1BE + (i * 16) + 4] == 0xEE)
            return true;

    return false;
}

bool IsFatxSuperblock(const unsigned char *sector, std::size_t length)
{
    return sector != nullptr && length >= 4 &&
           sector[0] == 'X' && sector[1] == 'T' &&
           sector[2] == 'A' && sector[3] == 'F';
}

bool ValidateDevicePath(const std::string &path, std::string &reason)
{
    reason.clear();
    if (path.empty() || path.size() >= 4096 || path.find('\0') != std::string::npos)
    {
        reason = "invalid device path";
        return false;
    }

#if defined(_WIN32)
    std::string lower(path);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

    const std::string prefix = "\\\\.\\";
    if (!starts_with(lower, prefix.c_str()))
    {
        reason = "only \\\\.\\PHYSICALDRIVE<n> devices are allowed";
        return false;
    }

    const std::string rest = lower.substr(prefix.size());
    const std::string physical = "physicaldrive";
    if (!starts_with(rest, physical.c_str()))
    {
        reason = "only raw physical drives are allowed (volume letters are not)";
        return false;
    }

    const std::string digits = rest.substr(physical.size());
    if (digits.empty() || digits.size() > 4 ||
        !std::all_of(digits.begin(), digits.end(),
                     [](char character) { return character >= '0' && character <= '9'; }))
    {
        reason = "invalid physical drive number";
        return false;
    }

    return true;
#elif defined(__linux__)
    return validate_linux(path, reason);
#elif defined(__APPLE__)
    return validate_macos(path, reason);
#else
    return validate_posix_fallback(path, reason);
#endif
}

#if !defined(_WIN32)
bool ValidateOpenedDevice(int fd, std::string &reason)
{
    reason.clear();

    struct stat status {};
    if (fstat(fd, &status) != 0)
    {
        reason = "could not stat the opened device";
        return false;
    }

#if defined(__linux__)
    return validate_linux_status(status, reason);
#elif defined(__APPLE__)
    return validate_macos_fd(fd, reason);
#else
    return validate_posix_fallback_fd(fd, reason);
#endif
}
#endif

bool ValidateDeviceContent(int fd, std::string &reason)
{
    reason.clear();

#if defined(__linux__)
    // A retail Xbox 360 hard drive has the big-endian FATX ("XTAF") magic at
    // HddOffsets::Data.
    unsigned char magic[4] = {};
    if (read_at(fd, 0x130EB0000ULL, magic, sizeof(magic)) &&
        IsFatxSuperblock(magic, sizeof(magic)))
        return true;

    struct stat status {};
    if (fstat(fd, &status) != 0)
    {
        reason = "could not stat the opened device";
        return false;
    }

    const std::string disk = disk_name_for_device(status.st_rdev);
    if (!disk.empty() && removable_or_usb(disk) && has_fat_filesystem(fd))
        return true;

    reason = disk.empty() ? "not an Xbox device" : "not an Xbox FATX/FAT32 drive";
    return false;
#elif defined(__APPLE__)
    // A retail Xbox 360 hard drive has the big-endian FATX ("XTAF") magic at
    // HddOffsets::Data. Raw macOS disk devices require sector-aligned reads, so
    // read the whole 512-byte sector and check its first four bytes.
    unsigned char magic[512] = {};
    if (read_at(fd, 0x130EB0000ULL, magic, sizeof(magic)) &&
        IsFatxSuperblock(magic, sizeof(magic)))
        return true;

    if (has_fat_filesystem(fd))
        return true;

    reason = "not an Xbox FATX/FAT32 drive";
    return false;
#else
    // No cheap content probe on other POSIX platforms; the GUI validates the
    // FATX/FAT32 layout after opening.
    (void)fd;
    return true;
#endif
}

} // namespace velocity::helper
