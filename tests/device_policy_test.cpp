#include <device_validation.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
    int failures = 0;

    void expect(bool allowed, const char *path, const char *what)
    {
        std::string reason;
        const bool result = velocity::helper::ValidateDevicePath(path, reason);
        if (result != allowed)
        {
            std::fprintf(stderr, "FAIL: %s: expected %s, got %s (%s)\n", what,
                         allowed ? "allowed" : "rejected",
                         result ? "allowed" : "rejected", reason.c_str());
            ++failures;
        }
    }

#if defined(__linux__)
    void expectRejectedFd(const char *path, const char *what)
    {
        const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
        {
            std::fprintf(stderr, "FAIL: %s: could not open %s\n", what, path);
            ++failures;
            return;
        }

        std::string reason;
        if (velocity::helper::ValidateOpenedDevice(fd, reason))
        {
            std::fprintf(stderr, "FAIL: %s: opened device was allowed\n", what);
            ++failures;
        }

        ::close(fd);
    }

    void expectInvalidFdRejected(const char *what)
    {
        std::string reason;
        if (velocity::helper::ValidateOpenedDevice(-1, reason))
        {
            std::fprintf(stderr, "FAIL: %s: invalid descriptor was allowed\n", what);
            ++failures;
        }
    }
#endif

    void testBootSectorClassifiers()
    {
        std::array<unsigned char, 512> gpt {};
        gpt[510] = 0x55;
        gpt[511] = 0xAA;
        gpt[0x1BE + 4] = 0xEE; // protective MBR

        if (!velocity::helper::IsGptProtectiveMbr(gpt.data(), gpt.size()))
        {
            std::fprintf(stderr, "FAIL: GPT protective MBR was not recognised\n");
            ++failures;
        }
        if (velocity::helper::IsFatBootSector(gpt.data(), gpt.size()))
        {
            std::fprintf(stderr, "FAIL: GPT protective MBR was treated as FAT\n");
            ++failures;
        }

        std::array<unsigned char, 512> fat {};
        fat[510] = 0x55;
        fat[511] = 0xAA;
        std::memcpy(fat.data() + 0x52, "FAT32", 5);

        if (!velocity::helper::IsFatBootSector(fat.data(), fat.size()))
        {
            std::fprintf(stderr, "FAIL: FAT32 boot sector was not recognised\n");
            ++failures;
        }
        if (velocity::helper::IsGptProtectiveMbr(fat.data(), fat.size()))
        {
            std::fprintf(stderr, "FAIL: FAT32 boot sector was treated as GPT\n");
            ++failures;
        }
    }
}

int main()
{
#if defined(_WIN32)
    expect(false, "C:\\", "drive root");
    expect(false, "\\\\.\\C:", "volume letter");
    expect(false, "\\\\server\\share", "UNC path");
    expect(false, "\\\\.\\PhysicalDrive", "missing drive number");
    expect(true, "\\\\.\\PHYSICALDRIVE0", "physical drive syntax");
#else
    expect(false, "/etc/passwd", "regular file");
    expect(false, "/dev/../etc/passwd", "traversal");
#if defined(__linux__)
    // /dev/loop0 does not exist in every container; do not pass the loop case
    // merely because resolution failed.
    if (::access("/dev/loop0", F_OK) == 0)
        expect(false, "/dev/loop0", "loop device");
    else if (::access("/dev/loop-control", F_OK) == 0)
        expect(false, "/dev/loop-control", "loop control device");
#endif
    expect(false, "/dev/null", "character device");
    expect(false, "", "empty path");
#if defined(__linux__)
    // The opened descriptor is what the helper actually reads and writes.
    expectRejectedFd("/dev/null", "opened character device");
    expectInvalidFdRejected("invalid descriptor");
#endif
#endif

    testBootSectorClassifiers();

    if (failures != 0)
    {
        std::fprintf(stderr, "device_policy_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("device_policy_test: ok\n");
    return 0;
}
