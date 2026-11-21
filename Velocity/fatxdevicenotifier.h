#ifndef FATXDEVICENOTIFIER_H
#define FATXDEVICENOTIFIER_H

#include <QList>
#include <QMutex>
#include <QThread>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <XboxInternals/Fatx/FatxDriveDetection.h>

#include "fatxmetatypes.h"

class HelperClient;

class FatxDeviceNotifier : public QThread
{
    Q_OBJECT

public:
    explicit FatxDeviceNotifier(QObject *parent = nullptr);
    ~FatxDeviceNotifier();

    void StopLooking();

    // Called from the GUI thread. The notifier only performs socket I/O with
    // it; QProcess lifetime stays on the GUI thread.
    void SetHelperClient(std::shared_ptr<HelperClient> client);

signals:
    void newDevicesDetected(QList<FatxDrive*> newDevices);
    void helperRequired();

private:
    std::vector<std::unique_ptr<FatxDrive>> cachedDrives;
    std::atomic<bool> stop;
    std::mutex helperMutex;
    std::shared_ptr<HelperClient> helper;

    void run() override;

    // Uses a client created on the GUI thread; the worker only opens/reads.
    std::vector<std::unique_ptr<FatxDrive>> loadDrivesThroughHelper(
            const std::shared_ptr<HelperClient> &client);
};

#endif // FATXDEVICENOTIFIER_H
