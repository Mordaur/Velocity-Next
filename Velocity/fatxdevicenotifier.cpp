#include "fatxdevicenotifier.h"

#include <algorithm>

FatxDeviceNotifier::FatxDeviceNotifier(QObject *parent) :
    QThread(parent), stop(false)
{
    qRegisterMetaType<QList<FatxDrive *>>("QList<FatxDrive *>");
}

FatxDeviceNotifier::~FatxDeviceNotifier()
{
    StopLooking();
    wait();
}

void FatxDeviceNotifier::StopLooking()
{
    stop = true;
}

void FatxDeviceNotifier::run()
{
    while (!stop)
    {
        QList<FatxDrive *> newDrives;

        try
        {
            auto foundDrives = FatxDriveDetection::GetAllFatxDrives();

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
        catch (...)
        {
            // Device probing can fail while a drive is being attached; retry next cycle.
        }

        if (!newDrives.isEmpty())
            emit newDevicesDetected(newDrives);

        msleep(2000);
    }
}
