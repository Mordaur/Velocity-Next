#include <XboxInternals/Fatx/FatxDriveDetection.h>
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <dirent.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <sys/stat.h>

    #include <cstdlib>
    #include <cstring>
    #include <functional>
    #include <set>

    #ifdef __APPLE__
        #include <sys/disk.h>
        #include <sys/ioctl.h>
        #include <sys/mount.h>
    #endif

    #ifdef __linux
        #include <linux/hdreg.h>
        #include <mntent.h>
        #include <pwd.h>
    #endif
#endif

std::vector<std::unique_ptr<FatxDrive>> FatxDriveDetection::GetAllFatxDrives(
        std::vector<DeviceDiagnostic> *diagnostics)
{
    auto logicalDrivePaths = getLogicalDrives();
    std::vector<std::unique_ptr<FatxDrive>> drives;

    auto physicalDiskPaths = GetPhysicalDiskPaths();

    for (const auto &path : physicalDiskPaths)
    {
        DeviceErrorCode errorCode = DeviceErrorCode::None;
        try
        {
            auto device = std::make_unique<DeviceIO>(path, &errorCode);

            if (device->Length() > HddOffsets::Data)
            {
                device->SetPosition(HddOffsets::Data);
                if (device->ReadDword() == FATX_MAGIC)
                {
                    drives.push_back(std::make_unique<FatxDrive>(std::move(device), FatxHarddrive));
                }
            }
        }
        catch (const std::string &error)
        {
            if (diagnostics)
            {
                diagnostics->push_back(DeviceDiagnostic{
                        path,
                        errorCode == DeviceErrorCode::None ? DeviceErrorCode::IoError : errorCode,
                        error});
            }
        }
        catch (...)
        {
            if (diagnostics)
            {
                diagnostics->push_back(DeviceDiagnostic{
                        path,
                        errorCode == DeviceErrorCode::None ? DeviceErrorCode::IoError : errorCode,
                        "unknown device error"});
            }
        }
    }

    std::vector<std::string> dataFiles;
    for (const auto &logicalDrivePath : logicalDrivePaths)
    {
        // clear data files from the previous drive
        dataFiles.clear();

        try
        {
            std::string directory;
            #ifdef _WIN32
            // Convert wide string to narrow string using WideCharToMultiByte
            int size = WideCharToMultiByte(CP_UTF8, 0, logicalDrivePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (size > 0)
            {
                directory.resize(size - 1); // -1 to exclude null terminator
                WideCharToMultiByte(CP_UTF8, 0, logicalDrivePath.c_str(), -1, &directory[0], size, nullptr, nullptr);
            }
            #else
            // POSIX mount paths are already native bytes; no wide round-trip.
            directory = logicalDrivePath;
            #endif

            #ifdef _WIN32
                WIN32_FIND_DATAW fi;

                HANDLE h = FindFirstFileW((logicalDrivePath + L"\\Data*").c_str(), &fi);
                if (h != INVALID_HANDLE_VALUE)
                {
                    do
                    {
                        // Convert the wide name with the same UTF-8 mapping used
                        // for the directory; never into a fixed-size buffer.
                        const int nameSize = WideCharToMultiByte(CP_UTF8, 0,
                                fi.cFileName, -1, nullptr, 0, nullptr, nullptr);
                        if (nameSize <= 0)
                            continue;

                        std::string fileName(static_cast<size_t>(nameSize), '\0');
                        WideCharToMultiByte(CP_UTF8, 0, fi.cFileName, -1,
                                &fileName[0], nameSize, nullptr, nullptr);
                        fileName.resize(static_cast<size_t>(nameSize - 1));

                        dataFiles.push_back(directory + "\\" + fileName);
                    }
                    while (FindNextFileW(h, &fi));

                    FindClose(h);

                    if (dataFiles.size() >= 3)
                    {
                        // make sure the data files are loaded in the right order
                        std::sort(dataFiles.begin(), dataFiles.end());
                        drives.push_back(std::make_unique<FatxDrive>(
                                std::make_unique<JoinedMultiFileIO>(dataFiles), FatxFlashDrive));
                    }
                }
            #else
                DIR *dir = NULL;
                dirent *ent = NULL;
                dir = opendir(directory.c_str());
                if (dir != NULL)
                {
                    // search for valid data files
                    while ((ent = readdir(dir)) != NULL)
                    {
                        // the disks start with 'data'
                        if (std::string(ent->d_name).substr(0, 4) == "Data")
                            dataFiles.push_back(directory + std::string(ent->d_name));
                    }

                    if (dataFiles.size() >= 3)
                    {
                        // make sure the data files are loaded in the right order
                        std::sort(dataFiles.begin(), dataFiles.end());
                        drives.push_back(std::make_unique<FatxDrive>(
                                std::make_unique<MultiFileIO>(dataFiles), FatxFlashDrive));
                    }
                }
            #endif
        }
        catch (...)
        {
        }
    }

    return drives;
}

std::vector<std::wstring> FatxDriveDetection::GetPhysicalDiskPaths()
{
    std::vector<std::wstring> physicalDiskPaths;

#ifdef _WIN32
    for (int i = 0; i < 16; i++)
    {
        std::wstringstream ss;
        ss << L"\\\\.\\PHYSICALDRIVE" << i;

        // Probe existence without requesting access: a standard user may open a
        // disk with desired access 0, and this avoids handing the helper a list
        // of drives that do not exist.
        HANDLE probe = CreateFileW(ss.str().c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
        if (probe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(probe);
            physicalDiskPaths.push_back(ss.str());
            continue;
        }

        const DWORD lastError = GetLastError();
        if (lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_PATH_NOT_FOUND)
            continue; // no such physical drive

        // Denied or otherwise unknown: keep it so the helper can report why.
        physicalDiskPaths.push_back(ss.str());
    }
#else
    DIR *dir = opendir("/dev/");
    if (dir != NULL)
    {
        while (dirent *ent = readdir(dir))
        {
            const std::string name = ent->d_name;
#ifdef __APPLE__
            const bool wholeDisk = name.rfind("disk", 0) == 0 && name.find('s', 4) == std::string::npos;
            if (wholeDisk)
            {
                physicalDiskPaths.push_back(L"/dev/r" + std::wstring(name.begin(), name.end()));
            }
#elif __linux__
            const bool wholeDisk = name.rfind("sd", 0) == 0 &&
                    name.find_first_of("0123456789") == std::string::npos;
            if (wholeDisk)
            {
                const std::string path = "/dev/" + name;
                struct stat st {};
                // Only real block devices; skip /dev/sd* nodes that are not present.
                if (stat(path.c_str(), &st) == 0 && S_ISBLK(st.st_mode))
                    physicalDiskPaths.push_back(std::wstring(path.begin(), path.end()));
            }
#endif
        }
        closedir(dir);
    }
#endif

    return physicalDiskPaths;
}

#ifdef _WIN32
std::vector<std::wstring> FatxDriveDetection::getLogicalDrives()
{
    std::vector<std::wstring> driveStrings;

    const DWORD drives = GetLogicalDrives();
    std::wstringstream ss;
    char currentChar = 'A';

    for (int i = 0; i < 32; i++)
    {
        if (drives & (1u << i))
        {
            ss << currentChar << ":\\Xbox360";
            driveStrings.push_back(ss.str());
            ss.str(std::wstring());
        }
        currentChar++;
    }

    return driveStrings;
}
#else
std::vector<std::string> FatxDriveDetection::getLogicalDrives()
{
    std::vector<std::string> driveStrings;
    std::set<std::string> seen;

    const auto addCandidate = [&](const std::string &base) {
        const std::string xboxPath = base + "/Xbox360";
        struct stat status {};
        if (stat(xboxPath.c_str(), &status) != 0 || !S_ISDIR(status.st_mode))
            return;

        const std::string drive = xboxPath + "/";
        if (seen.insert(drive).second)
            driveStrings.push_back(drive);
    };

#ifdef __APPLE__
    // getmntinfo returns a shared buffer, and drive detection can run on both
    // the GUI and the device-watcher thread. getfsstat fills caller storage.
    const int mountCount = getfsstat(nullptr, 0, MNT_NOWAIT);
    if (mountCount > 0)
    {
        std::vector<struct statfs> mounts(static_cast<std::size_t>(mountCount) + 8);
        const int filled = getfsstat(mounts.data(),
                static_cast<int>(mounts.size() * sizeof(struct statfs)), MNT_NOWAIT);
        for (int i = 0; i < filled; ++i)
        {
            const char *type = mounts[static_cast<std::size_t>(i)].f_fstypename;
            if (strcmp(type, "msdos") == 0 || strcmp(type, "exfat") == 0)
                addCandidate(mounts[static_cast<std::size_t>(i)].f_mntonname);
        }
    }

    // Fallback for systems where the mount table is unavailable.
    if (driveStrings.empty())
    {
        DIR *dir = opendir("/Volumes/");
        if (dir != nullptr)
        {
            while (dirent *ent = readdir(dir))
            {
                if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                    continue;
                addCandidate(std::string("/Volumes/") + ent->d_name);
            }
            closedir(dir);
        }
    }
#elif __linux
    // Enumerate the actual mount table so udisks2 mounts under
    // /run/media/<user>, /mnt and non-standard locations are found.
    bool mountTableRead = false;
    FILE *mountTable = setmntent("/proc/self/mounts", "r");
    if (mountTable != nullptr)
    {
        mountTableRead = true;
        // getmntent uses a static result; drive detection also runs on the
        // device-watcher thread, so use the reentrant variant.
        struct mntent entry {};
        char entryBuffer[4096];
        while (getmntent_r(mountTable, &entry, entryBuffer, sizeof(entryBuffer)) != nullptr)
        {
            const char *type = entry.mnt_type;
            if (strcmp(type, "vfat") == 0 || strcmp(type, "exfat") == 0 ||
                strcmp(type, "msdos") == 0)
                addCandidate(entry.mnt_dir);
        }
        endmntent(mountTable);
    }

    // Fallback only when /proc is unavailable; an empty mount table means
    // there is genuinely no FAT/Xbox volume mounted.
    if (!mountTableRead)
    {
        std::function<void(const std::string &, int)> scan =
            [&](const std::string &root, int depth) {
            DIR *dir = opendir(root.c_str());
            if (dir == nullptr)
                return;

            addCandidate(root);
            if (depth > 0)
            {
                while (dirent *ent = readdir(dir))
                {
                    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                        continue;

                    const std::string child = root + "/" + ent->d_name;
                    struct stat status {};
                    if (stat(child.c_str(), &status) == 0 && S_ISDIR(status.st_mode))
                        scan(child, depth - 1);
                }
            }

            closedir(dir);
        };

        scan("/media", 2);
        if (const char *user = std::getenv("USER"))
            scan(std::string("/run/media/") + user, 1);
        else
            scan("/run/media", 2);
        scan("/mnt", 1);
    }
#endif

    return driveStrings;
}
#endif
