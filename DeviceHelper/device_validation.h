#pragma once

#include <cstddef>
#include <string>

namespace velocity::helper {

// Pure boot-sector classifiers shared by the platform content gates and the
// tests. `sector` must point to at least 512 bytes.
bool IsFatBootSector(const unsigned char *sector, std::size_t length);
bool IsGptProtectiveMbr(const unsigned char *sector, std::size_t length);

// Validates a device path *before* it is opened. On POSIX this resolves the
// path and checks the kernel device (removable/USB, not the root/boot/swap
// disk). On Windows it enforces the raw physical-drive form; the storage bus
// and system-disk checks run after the handle is open.
bool ValidateDevicePath(const std::string &path, std::string &reason);

#if !defined(_WIN32)
// Re-validates the device that was actually opened. Validation of the path is
// inherently racy (a symlink or mount can change between check and open), so
// the helper checks the opened descriptor again before any I/O.
bool ValidateOpenedDevice(int fd, std::string &reason);

// Requires the opened device to carry an Xbox layout: FATX magic at
// HddOffsets::Data, or (for removable/USB media) a FAT boot sector or a FAT
// partition. This is a content gate, not a path check.
bool ValidateDeviceContent(int fd, std::string &reason);
#endif

} // namespace velocity::helper
