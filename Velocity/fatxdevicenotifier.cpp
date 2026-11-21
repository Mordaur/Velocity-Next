#include "fatxdevicenotifier.h"

#include <algorithm>

#include "devicehelperclient.h"

FatxDeviceNotifier::FatxDeviceNotifier(QObject *parent) :
    QThread(parent), stop(false)
{
    qRegisterMetaType<QList<FatxDrive *>>("QList<FatxDrive *>");
}

FatxDeviceNotifier::~FatxDeviceNotifier()
{
    StopLooking();
    wait();

    std::lock_guard<std::mutex> lock(helperMutex);
    helper.reset();
}

void FatxDeviceNotifier::StopLooking()
{
    stop = true;
}

void FatxDeviceNotifier::SetHelperClient(std::shared_ptr<HelperClient> client)
{
    std::lock_guard<std::mutex> lock(helperMutex);
    helper = std::move(client);
}

void FatxDeviceNotifier::run()
{
    bool helperAttempted = false;
    bool helperRequested = false;

    while (!stop)
    {
        QList<FatxDrive *> newDrives;
        std::vector<std::unique_ptr<FatxDrive>> foundDrives;
        std::vector<DeviceDiagnostic> diagnostics;

        try
        {
            foundDrives = FatxDriveDetection::GetAllFatxDrives(&diagnostics);
        }
        catch (...)
        {
            // Device probing can fail while a drive is being attached; retry next cycle.
        }

        // Local access needs root/disk-group membership. Ask the helper once, so
        // the content viewer works for unprivileged users like DeviceViewer does.
        bool permissionDenied = false;
        for (const auto &diagnostic : diagnostics)
            if (diagnostic.code == DeviceErrorCode::PermissionDenied)
                permissionDenied = true;

        if (foundDrives.empty() && permissionDenied && !helperAttempted)
        {
            std::shared_ptr<HelperClient> client;
            {
                std::lock_guard<std::mutex> lock(helperMutex);
                client = helper;
            }

            if (client)
            {
                helperAttempted = true;
                foundDrives = loadDrivesThroughHelper(client);
            }
            else if (!helperRequested)
            {
                // The GUI thread creates and starts the helper, then calls
                // SetHelperClient; this worker never owns a QProcess.
                helperRequested = true;
                emit helperRequired();
            }
        }

        {
            for (auto &drive : foundDrives)
            {
                bool known = std::any_of(cachedDrives.begin(), cachedDrives.end(),
                        [&drive](const std::unique_ptr<FatxDrive> &cached) {
                            return *cached == *drive;
                        });

                if (!known)
                {
                    newDrives.push_back(drive.get());
                    cachedDrives.push_back(std::move(drive));
                }
            }
        }

        if (!newDrives.isEmpty())
            emit newDevicesDetected(newDrives);

        msleep(2000);
    }
}

std::vector<std::unique_ptr<FatxDrive>> FatxDeviceNotifier::loadDrivesThroughHelper(
        const std::shared_ptr<HelperClient> &client)
{
    std::vector<std::unique_ptr<FatxDrive>> drives;

    for (const auto &path : FatxDriveDetection::GetPhysicalDiskPaths())
    {
        try
        {
            auto device = std::make_unique<HelperDeviceIO>(client, client->openDevice(path, true));
            auto drive = std::make_unique<FatxDrive>(std::move(device), FatxHarddrive);

            if (!drive->GetPartitions().empty())
                drives.push_back(std::move(drive));
        }
        catch (...)
        {
            // Not a FATX drive, or the helper refused this candidate.
        }
    }

    // The drives hold the client alive through HelperDeviceIO. The GUI owns
    // the process lifetime, so do not stop it from this worker.
    return drives;
}
