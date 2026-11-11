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

    #ifdef __APPLE__
        #include <sys/disk.h>
        #include <sys/ioctl.h>
    #endif

    #ifdef __linux
        #include <linux/hdreg.h>
        #include <pwd.h>
    #endif
#endif

std::vector<std::unique_ptr<FatxDrive>> FatxDriveDetection::GetAllFatxDrives(
        std::vector<DeviceDiagnostic> *diagnostics)
{
    std::vector<std::wstring> logicalDrivePaths = getLogicalDrives();
    std::vector<std::unique_ptr<FatxDrive>> drives;

    auto physicalDiskPaths = getPhysicalDiskPaths();

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
            // Convert wide string to narrow string using wcstombs
            size_t len = wcstombs(nullptr, logicalDrivePath.c_str(), 0);
            if (len != static_cast<size_t>(-1))
            {
                directory.resize(len);
                wcstombs(&directory[0], logicalDrivePath.c_str(), len + 1);
            }
            #endif

            #ifdef _WIN32
                WIN32_FIND_DATA fi;

                HANDLE h = FindFirstFile((logicalDrivePath + L"\\Data*").c_str(), &fi);
                if (h != INVALID_HANDLE_VALUE)
                {
                    do
                    {
                        char path[9];
                        wcstombs(path, fi.cFileName, wcslen(fi.cFileName) + 1);
                        dataFiles.push_back(directory + "\\" + std::string(path));
                    }
                    while (FindNextFile(h, &fi));

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

std::vector<std::wstring> FatxDriveDetection::getPhysicalDiskPaths()
{
    std::vector<std::wstring> physicalDiskPaths;

#ifdef _WIN32
    for (int i = 0; i < 16; i++)
    {
        std::wstringstream ss;
        ss << L"\\\\.\\PHYSICALDRIVE" << i;
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
#elif __linux
            const bool wholeDisk = name.rfind("sd", 0) == 0 &&
                    name.find_first_of("0123456789") == std::string::npos;
            if (wholeDisk)
            {
                physicalDiskPaths.push_back(L"/dev/" + std::wstring(name.begin(), name.end()));
            }
#endif
        }
        closedir(dir);
    }
#endif

    return physicalDiskPaths;
}

std::vector<std::wstring> FatxDriveDetection::getLogicalDrives()
{
    std::vector<std::wstring> driveStrings;

#ifdef _WIN32
    DWORD drives = GetLogicalDrives();
    std::wstringstream ss;
    char currentChar = 'A';

    for (int i = 0; i < 32; i++)
    {
        if (drives & 1)
        {
            ss << currentChar << ":\\Xbox360";
            driveStrings.push_back(ss.str());
            ss.str(std::wstring());
        }

        drives >>= 1;
        currentChar++;
    }
#elif __APPLE__
    DIR *dir = NULL;
    dirent *ent = NULL;
    dir = opendir("/Volumes/");
    if (dir != NULL)
    {
        std::stringstream path;

        // search for valid flash drives
        while ((ent = readdir(dir)) != NULL)
        {
            try
            {
                // initialize the path
                path << "/Volumes/";
                path << ent->d_name;
                path << "/Xbox360/";

                // get the xbox360 folder path
                std::string xboxDirPath = path.str();

                // add it to our list of drives
                std::wstring widePathStr;
                widePathStr.assign(xboxDirPath.begin(), xboxDirPath.end());
                driveStrings.push_back(widePathStr);
            }
            catch(...)
            {
                // something bad happened
                // skip this device, and try the next one
            }

            // clear the stringstream
            path.str(std::string());
        }
        if (dir)
            closedir(dir);
        if (ent)
            delete ent;
    }
#elif __linux
    DIR *dir = NULL, *dir2 = NULL;
    dirent *ent = NULL, *ent2 = NULL;
    dir = opendir("/media/");
    if (dir != NULL)
    {
        std::stringstream path;

        // search for valid flash drives
        while ((ent = readdir(dir)) != NULL)
        {
            if (ent->d_type != DT_DIR)
                continue;

            path.str(std::string());
            path << "/media/" << ent->d_name;
            dir2 = opendir(path.str().c_str());
            if (dir2 == NULL)
                continue;

            // search for valid flash drives
            while ((ent2 = readdir(dir2)) != NULL)
            {

                if (strcmp(ent2->d_name, ".") == 0 || strcmp(ent2->d_name, "..") == 0)
                    continue;

                try
                {
                    // initialize the path
                    path << "/" << ent2->d_name << "/Xbox360/";

                    // get the xbox360 folder path
                    std::string xboxDirPath = path.str();

                    // add it to our list of drives
                    std::wstring widePathStr;
                    widePathStr.assign(xboxDirPath.begin(), xboxDirPath.end());
                    driveStrings.push_back(widePathStr);
                }
                catch(...)
                {
                    // something bad happened
                    // skip this device, and try the next one
                }

                // clear the stringstream
                path.str(std::string());
                path << "/media/" << ent->d_name;
            }
        }
        if (dir)
            closedir(dir);
        if (ent)
            delete ent;
        if (ent2)
            delete ent2;
    }
#endif
    return driveStrings;
}
