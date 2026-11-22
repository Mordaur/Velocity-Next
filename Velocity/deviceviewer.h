#ifndef DEVICEVIEWER_H
#define DEVICEVIEWER_H

// qt
#include <QDialog>
#include <QTreeWidget>
#include <QFileDialog>
#include <QDesktopServices>
#include <QMenu>
#include <QMessageBox>
#include <QDateTime>
#include <QUrl>
#include <QStatusBar>
#include <QInputDialog>
#include <QProgressBar>
#include <QPixmap>
#include <QHash>
#include <QAction>
#include "qthelpers.h"

// forms
#include "multiprogressdialog.h"
#include "clustertooldialog.h"
#include "securitysectordialog.h"
#include "fatxfiledialog.h"
#include "partitiondialog.h"
#include "singleprogressdialog.h"
#include "packageviewer.h"
#include "xexdialog.h"
#include "svoddialog.h"
#include "flashdriveconfigdatadialog.h"
#include "devicehelperclient.h"

// xbox
#include <XboxInternals/Fatx/FatxDriveDetection.h>
#include <XboxInternals/Fatx/FatxDrive.h>
#include <XboxInternals/Stfs/StfsDefinitions.h>

// widgets
#include "nightcharts.h"
#include "dragdroptreewidget.h"
#include "fatxmetatypes.h"

#include <memory>

namespace Ui
{
class DeviceViewer;
}

void updateUI(void *arg, bool finished);
void updateUIDelete(void *arg);

class DeviceViewer : public QDialog
{
    Q_OBJECT

public:
    explicit DeviceViewer(QStatusBar *statusBar, QList<QAction *> gpdActions,
            QList<QAction *> gameActions, QWidget *parent = 0);

    void LoadDrives();
    ~DeviceViewer();

private slots:
    void showContextMenu(QPoint point);
    void on_treeWidget_doubleClicked(const QModelIndex &index);
    void on_btnBack_clicked();
    void on_treeWidget_2_itemExpanded(QTreeWidgetItem *item);
    void on_treeWidget_2_itemClicked(QTreeWidgetItem *item, int column);
    void on_btnSecurityBlob_clicked();
    void on_btnPartitions_clicked();
    void onDragDropped(QDropEvent *event);
    void onDragEntered(QDragEnterEvent *event);
    void onDragLeft(QDragLeaveEvent *event);
    void on_btnBackup_clicked();
    void on_btnRestore_clicked();
    void on_txtSearch_textChanged(const QString &arg1);
    void on_btnShowAll_clicked();
    void on_txtPath_returnPressed();
    void on_txtDriveName_editingFinished();

private:
    Ui::DeviceViewer *ui;
    std::vector<std::unique_ptr<FatxDrive>> loadedDrives;
    FatxDrive *currentDrive;
    FatxFileEntry *parentEntry;
    QList<FatxFileEntry*> directoryChain;
    QList<QAction *> gpdActions, gameActions;
    QStatusBar *statusBar;
    QProgressBar *progressBar;
    QTreeWidgetItem *currentDriveItem;
    QString previousName;
    bool drivesLoaded;
    bool friendlyNames;
    // Guards against re-entering a folder load from the event loop that
    // GetChildFileEntries pumps: a nested load would clear the tree the outer
    // loop is still iterating and interleave reads on the same drive.
    bool loadingFolder;
    // True between a Ctrl+press on the file-list header and its release, so a
    // filtered click never leaves the header half-way through a section move.
    bool headerCtrlClick;
    // Set while a device file is open in a modal viewer. Title-name callbacks
    // can fire during exec() and would read the same FATX drive as the viewer.
    bool openingDeviceFile;
    bool refreshTreePending;
    QList<QPair<QString, QPixmap>> pendingTitleIcons;
    // GPD title cache, kept here so an edited GPD can drop its stale entry.
    QHash<QString, QString> gpdTitleCache;
    std::shared_ptr<HelperClient> helperClient;

    // Returns false when the load was skipped (already loading) or failed, so
    // callers do not sync the navigation tree to a folder the list never showed.
    bool LoadFolderAll(FatxFileEntry *folder);
    // Load a folder and reveal it in the left navigation tree. Used by the
    // right-pane double click, the back button and the path box; the left tree
    // itself already selects the item the user clicked.
    void NavigateToFolder(FatxFileEntry *folder);
    void SelectNavigationItem(FatxFileEntry *entry);
    QTreeWidgetItem *FindNavigationDriveItem(FatxDrive *drive);
    QTreeWidgetItem *FindNavigationChild(QTreeWidgetItem *parent, FatxFileEntry *entry,
            const std::string &name);
    void ToggleFriendlyNames();
    // Real title name from the first package in a title-ID folder (cached);
    // empty when it cannot be determined.
    QString ResolveTitleName(FatxFileEntry *entry);
    QString ResolveGpdTitle(FatxFileEntry *entry);
    QString ResolveDataFileName(FatxFileEntry *entry);
    void ApplyLocalIcon(QTreeWidgetItem *item, FatxFileEntry *entry);
    void RefreshTreeNames();
    void ApplyTitleIcon(const QString &titleId, const QPixmap &icon);
    QString DisplayName(FatxFileEntry *entry);
    bool eventFilter(QObject *watched, QEvent *event) override;
    void LoadFolderTree(QTreeWidgetItem *item);
    void LoadPartitions();
    void GetSubFilesFATX(FatxFileEntry *parent, QList<void*> &entries);
    void GetSubFilesLocal(QString parent, QList<void *> &files);
    FatxFileEntry* GetFatxFileEntry(QTreeWidgetItem *item);
    // Opens a row with the viewer for its detected type. Returns false for
    // directories and unknown types.
    bool OpenFileEntry(QTreeWidgetItem *item);
    // Opens a row with a specific viewer. Returns true when a viewer was
    // attempted (even if it failed), so callers do not fall through.
    bool OpenEntryAs(QTreeWidgetItem *item, const QString &type);
    static bool IsOpenableType(const QString &type);
    void DrawMemoryGraph();
    void InjectFiles(QList<void *> files, QString rootPath);
    void DrawHeader(QString driveName);
    void SetWidgetsEnabled(bool enabled);

    friend void updateUI(void *arg, bool finished);
    friend void updateUIDelete(void *arg);
};

#endif // DEVICEVIEWER_H


