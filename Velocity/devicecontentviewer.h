#ifndef DEVICECONTENTVIEWER_H
#define DEVICECONTENTVIEWER_H

#include <QDialog>
#include <QLabel>
#include <QList>
#include <QProgressBar>
#include <QStatusBar>
#include <QTreeWidgetItem>

#include <XboxInternals/Fatx/XContentDevice.h>
#include <XboxInternals/Stfs/IXContentHeader.h>

#include "fatxdevicenotifier.h"

namespace Ui {
class DeviceContentViewer;
}

Q_DECLARE_METATYPE(IXContentHeader*)

enum class ContentItemType : quint32
{
    Device = 0,
    Profile,
    Title,
    Content,
    SharedItemCategory
};

class DeviceContentViewer : public QDialog
{
    Q_OBJECT

public:
    static bool OPEN;

    explicit DeviceContentViewer(QStatusBar *statusBar, QWidget *parent = nullptr);
    ~DeviceContentViewer();

private slots:
    void on_treeWidget_itemDoubleClicked(QTreeWidgetItem *item, int column);
    void on_treeWidget_currentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void showContextMenu(const QPoint &pos);
    void on_btnViewPackage_clicked();
    void onNewDevicesDetected(QList<FatxDrive*> newDrives);

private:
    Ui::DeviceContentViewer *ui;
    QStatusBar *statusBar;
    QList<XContentDevice*> devices;
    IXContentHeader *currentPackage;
    QProgressBar *progressBar;
    FatxDeviceNotifier *deviceNotifier;
    XContentDevice *currentDevice;

    void LoadDevicesp();
    void LoadSharedItemCategory(const QString &category,
                                std::vector<XContentDeviceSharedItem> *items,
                                QTreeWidgetItem *parent, const QString &iconPath);
    void ClearSidePanel();
    void SetLabelText(QLabel *label, const QString &text);
    void OpenContent(bool updateButton);
    void UpdateDevicePanel();
    void UpdateUI();
};

#endif // DEVICECONTENTVIEWER_H
