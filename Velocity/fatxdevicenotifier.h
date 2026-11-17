#ifndef FATXDEVICENOTIFIER_H
#define FATXDEVICENOTIFIER_H

#include <QList>
#include <QMutex>
#include <QThread>

#include <atomic>
#include <memory>
#include <vector>

#include <XboxInternals/Fatx/FatxDriveDetection.h>

#include "fatxmetatypes.h"

class FatxDeviceNotifier : public QThread
{
    Q_OBJECT

public:
    explicit FatxDeviceNotifier(QObject *parent = nullptr);
    ~FatxDeviceNotifier();

    void StopLooking();

signals:
    void newDevicesDetected(QList<FatxDrive*> newDevices);

private:
    std::vector<std::unique_ptr<FatxDrive>> cachedDrives;
    std::atomic<bool> stop;

    void run() override;

    // Loads FATX drives through the privileged helper; used once when local
    // raw-device access is denied, mirroring DeviceViewer.
    std::vector<std::unique_ptr<FatxDrive>> loadDrivesThroughHelper();
};

#endif // FATXDEVICENOTIFIER_H
