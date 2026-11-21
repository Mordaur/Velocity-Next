#ifndef SINGLEPROGRESSDIALOG_H
#define SINGLEPROGRESSDIALOG_H

// qt
#include <QDialog>
#include "qthelpers.h"
#include "multiprogressdialog.h"

// xbox360
#include <XboxInternals/Stfs/StfsPackage.h>

class XContentDevice;

void UpdateProgress(void *arg, u32 cur, u32 total);

namespace Ui
{
class SingleProgressDialog;
}

class SingleProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SingleProgressDialog(FileSystem system, void *device, Operation op, QString internalPath,
            QString externalPath, void *outEntry = nullptr, QWidget *parent = 0);

    // Copy a validated content package to its console path. `device` must be an
    // XContentDevice; the destination is derived from the package header, not
    // from the caller. Kept out of the FileSystem dispatch so an XContentDevice
    // can never be reinterpret_cast as a FatxDrive.
    explicit SingleProgressDialog(XContentDevice *device, const QString &sourcePath,
            QWidget *parent = 0);

    void start();
    ~SingleProgressDialog();

private:
    Ui::SingleProgressDialog *ui;
    Operation op;
    FileSystem system;
    void *device;
    void *outEntry;
    QString internalPath, externalPath;
    bool copyToDevice_ = false;

    friend void UpdateProgress(void *arg, u32 cur, u32 total);
};

#endif // SINGLEPROGRESSDIALOG_H


