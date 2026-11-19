#include "devicecontentviewer.h"
#include "ui_devicecontentviewer.h"

#include <QApplication>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QLabel>
#include <QProgressBar>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QMenu>
#include <QSet>

#include <QtConcurrent/QtConcurrentRun>
#include <QMessageBox>
#include <QPixmap>

#include <XboxInternals/Gpd/GpdBase.h>
#include <XboxInternals/Stfs/StfsPackage.h>

#include "multiprogressdialog.h"
#include "packageviewer.h"
#include "profileselectiondialog.h"
#include "qthelpers.h"
#include "svoddialog.h"

#include <cstring>

#include <XboxInternals/Stfs/StfsDefinitions.h>

bool DeviceContentViewer::OPEN = false;

namespace
{
// Tree column 0, role UserRole + 6: the on-device folder a node stands for.
// Device, profile, title and category nodes carry one so they can be exported
// as a folder; leaves keep their file path in column 1, role UserRole.
constexpr int kFolderRole = Qt::UserRole + 6;

const QString kContentRoot = QStringLiteral("Drive:\\Content\\Content\\");
const QString kSharedRoot = kContentRoot + QStringLiteral("0000000000000000\\");

QString ParentFolder(const QString &filePath)
{
    const int slash = filePath.lastIndexOf('\\');
    return slash < 0 ? QString() : filePath.left(slash + 1);
}

// Deepest folder that is a prefix of every folder in the list. Always ends with
// a separator, so a relative path is the remaining mid() of the full path.
QString CommonFolder(const QStringList &folders)
{
    if (folders.isEmpty())
        return QString();

    QString root = folders.first();
    for (const QString &folder : folders)
        while (!root.isEmpty() && !folder.startsWith(root))
            root = root.left(root.lastIndexOf('\\', root.size() - 2) + 1);

    return root;
}

// Adds a node's content files - its own package, any SVOD data files and,
// recursively, its children - to the extraction list.
void CollectContentFiles(QTreeWidgetItem *item, QStringList &paths, QSet<QString> &seen)
{
    QStringList files;
    files.push_back(item->data(1, Qt::UserRole).toString());
    files += item->data(4, Qt::UserRole).toStringList();

    for (const QString &file : files)
    {
        if (file.isEmpty() || seen.contains(file))
            continue;

        seen.insert(file);
        paths.push_back(file);
    }

    for (int i = 0; i < item->childCount(); ++i)
        CollectContentFiles(item->child(i), paths, seen);
}

// The folder a selection is exported relative to. A title keeps its title
// folder in the path (so saves read <TitleID>/<content type>/<file>); other
// nodes export the contents of the folder they stand for.
QString ExportRoot(QTreeWidgetItem *item)
{
    const auto kind = static_cast<ContentItemType>(item->data(5, Qt::UserRole).toUInt());

    if (kind == ContentItemType::Title && item->parent() != nullptr)
    {
        const QString profileFolder = item->parent()->data(0, kFolderRole).toString();
        if (!profileFolder.isEmpty())
            return profileFolder;
    }

    const QString folder = item->data(0, kFolderRole).toString();
    return folder.isEmpty() ? ParentFolder(item->data(1, Qt::UserRole).toString()) : folder;
}

// A file name that is unique among the names used so far by a flat export.
// The source paths are FATX paths, so both separators have to be handled even
// on platforms where '\\' is an ordinary file name character.
QString UniqueName(const QString &path, QSet<QString> &used)
{
    const int separator = path.lastIndexOf('\\') > path.lastIndexOf('/')
            ? path.lastIndexOf('\\')
            : path.lastIndexOf('/');
    const QString fileName = separator < 0 ? path : path.mid(separator + 1);

    const int dot = fileName.lastIndexOf('.');
    const QString base = dot > 0 ? fileName.left(dot) : fileName;
    const QString suffix = dot > 0 ? fileName.mid(dot + 1) : QString();

    QString name = fileName;
    for (int i = 2; used.contains(name.toLower()); ++i)
        name = suffix.isEmpty()
                ? QStringLiteral("%1 (%2)").arg(base).arg(i)
                : QStringLiteral("%1 (%2).%3").arg(base).arg(i).arg(suffix);

    used.insert(name.toLower());
    return name;
}
} // namespace

DeviceContentViewer::DeviceContentViewer(QStatusBar *statusBar, QWidget *parent) :
    QDialog(parent), ui(new Ui::DeviceContentViewer), statusBar(statusBar),
    currentPackage(nullptr), deviceNotifier(nullptr), currentDevice(nullptr)
{
    ui->setupUi(this);

    ui->treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->treeWidget, SIGNAL(customContextMenuRequested(QPoint)), this,
            SLOT(showContextMenu(QPoint)));

    deviceNotifier = new FatxDeviceNotifier(this);
    connect(deviceNotifier, SIGNAL(newDevicesDetected(QList<FatxDrive*>)), this,
            SLOT(onNewDevicesDetected(QList<FatxDrive*>)));
    deviceNotifier->start();

    progressBar = new QProgressBar(this);
    progressBar->setMaximumHeight(statusBar->height() - 5);
    progressBar->setFixedWidth(220);
    progressBar->setTextVisible(false);
    progressBar->setVisible(false);
    statusBar->addWidget(progressBar);

    // Covers the viewer while a drive is scanned. It is a child widget rather
    // than a dialog, so it dims the content and swallows clicks in place.
    scanOverlay = new QLabel("Scanning for content...", this);
    scanOverlay->setAlignment(Qt::AlignCenter);
    scanOverlay->setStyleSheet("background-color: rgba(0, 0, 0, 120);"
                               "color: white; font-size: 16pt; font-weight: bold;");
    scanOverlay->setGeometry(rect());
    scanOverlay->hide();

    ui->tabWidget->setEnabled(false);

    ui->treeWidget->setColumnWidth(0, 500);
    ui->treeWidget->setColumnWidth(1, 50);

    OPEN = true;
}

DeviceContentViewer::~DeviceContentViewer()
{
    // Stop copies/extractions still using the device handles before the
    // drive list is released below.
    cancelProgressDialogs(this);

    // The worker reads the drives owned by the notifier; let it finish before
    // those are destroyed. Its results will never be collected, so free them.
    if (loadWatcher != nullptr)
    {
        loadWatcher->waitForFinished();
        const QList<XContentDevice *> pending = loadWatcher->result();
        for (auto *device : pending)
            delete device;
    }

    qDeleteAll(queuedContent);
    queuedContent.clear();

    for (auto *device : devices)
        delete device;
    devices.clear();

    if (deviceNotifier)
    {
        deviceNotifier->StopLooking();
        deviceNotifier->wait();
    }

    delete ui;
    delete progressBar;

    OPEN = false;
}

void DeviceContentViewer::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);

    if (scanOverlay != nullptr)
        scanOverlay->setGeometry(rect());
}

void DeviceContentViewer::LoadSharedItemCategory(const QString &category,
        std::vector<XContentDeviceSharedItem> *items, QTreeWidgetItem *parent,
        const QString &iconPath)
{
    if (!items || items->empty())
        return;

    QTreeWidgetItem *categoryItem = new QTreeWidgetItem(parent);
    categoryItem->setText(0, category);
    categoryItem->setIcon(0, QIcon(QPixmap(iconPath).scaled(50, 50)));
    categoryItem->setData(5, Qt::UserRole, static_cast<quint32>(ContentItemType::SharedItemCategory));
    categoryItem->setData(0, kFolderRole, kSharedRoot);

    for (auto &content : *items)
    {
        QTreeWidgetItem *item = new QTreeWidgetItem(categoryItem);
        item->setText(0, QString::fromStdWString(content.GetName()));
        item->setText(1, QString::fromStdString(ByteSizeToString(content.GetFileSize())));

        item->setData(0, Qt::UserRole, QVariant::fromValue(content.content.get()));
        item->setData(1, Qt::UserRole, QVariant(QString::fromStdString(content.GetPathOnDevice())));
        item->setData(2, Qt::UserRole, QVariant(QString::fromStdString(content.GetRawName())));
        item->setData(3, Qt::UserRole, QVariant(static_cast<quint64>(content.GetFileSize())));
        item->setData(4, Qt::UserRole,
                      QVariant(QtHelpers::StdStringArrayToQStringList(content.GetContentFilePaths())));
        item->setData(5, Qt::UserRole, static_cast<quint32>(ContentItemType::Content));

        if (content.GetThumbnail() != nullptr && content.GetThumbnailSize() != 0)
        {
            QByteArray imageBuff(reinterpret_cast<const char *>(content.GetThumbnail()),
                                 content.GetThumbnailSize());
            QPixmap thumb = QPixmap::fromImage(QImage::fromData(imageBuff));
            if (!thumb.isNull())
                item->setIcon(0, QIcon(thumb.scaled(50, 50)));
        }
    }
}

void DeviceContentViewer::LoadDevicesp()
{
    ui->treeWidget->clear();

    for (auto *device : devices)
    {
        QTreeWidgetItem *deviceItem = new QTreeWidgetItem(ui->treeWidget);
        deviceItem->setText(0, QString::fromStdWString(device->GetName()));
        deviceItem->setText(1, QString::fromStdString(ByteSizeToString(device->GetTotalMemory())));
        deviceItem->setData(5, Qt::UserRole, static_cast<quint32>(ContentItemType::Device));
        deviceItem->setData(0, kFolderRole, kContentRoot);

        if (device->GetDeviceType() == FatxHarddrive)
            deviceItem->setIcon(0, QIcon(QPixmap(":/Images/harddrive.png").scaled(75, 50)));
        else
            deviceItem->setIcon(0, QIcon(QPixmap(":/Images/usb drive.png").scaled(75, 50)));

        if (device->profiles)
        {
            for (auto &profile : *device->profiles)
            {
                QTreeWidgetItem *profileItem = new QTreeWidgetItem(deviceItem);
                QString profileName = QString::fromStdWString(profile.GetName());
                if (profileName.isEmpty())
                    profileName = "Unknown Profile";

                profileItem->setText(0, profileName);
                if (profile.GetFileSize() != 0)
                    profileItem->setText(1, QString::fromStdString(ByteSizeToString(profile.GetFileSize())));

                profileItem->setData(0, Qt::UserRole, QVariant::fromValue(profile.content.get()));
                profileItem->setData(1, Qt::UserRole, QVariant(QString::fromStdString(profile.GetPathOnDevice())));
                profileItem->setData(2, Qt::UserRole, QVariant(QString::fromStdString(profile.GetRawName())));
                profileItem->setData(3, Qt::UserRole, QVariant(static_cast<quint64>(profile.GetFileSize())));
                profileItem->setData(5, Qt::UserRole, static_cast<quint32>(ContentItemType::Profile));
                profileItem->setData(0, kFolderRole,
                        kContentRoot + QString::fromStdString(profile.GetRawName()) + "\\");

                if (profile.GetThumbnail() != nullptr && profile.GetThumbnailSize() != 0)
                {
                    QByteArray imageBuff(reinterpret_cast<const char *>(profile.GetThumbnail()),
                                         profile.GetThumbnailSize());
                    QPixmap thumb = QPixmap::fromImage(QImage::fromData(imageBuff));
                    if (!thumb.isNull())
                        profileItem->setIcon(0, QIcon(thumb));
                }
                else
                {
                    profileItem->setIcon(0, QIcon(QPixmap(":/Images/HiddenAchievement.png").scaled(50, 50)));
                }

                for (auto &title : profile.titles)
                {
                    QTreeWidgetItem *titleItem = new QTreeWidgetItem(profileItem);
                    titleItem->setText(0, QString::fromStdWString(title.GetName()));
                    titleItem->setData(5, Qt::UserRole, static_cast<quint32>(ContentItemType::Title));

                    // The backend builds this path with a doubled separator.
                    QString titleFolder = QString::fromStdString(title.GetPathOnDevice());
                    titleFolder.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
                    if (!titleFolder.endsWith('\\'))
                        titleFolder += '\\';
                    titleItem->setData(0, kFolderRole, titleFolder);

                    if (title.GetThumbnail() != nullptr && title.GetThumbnailSize() != 0)
                    {
                        QByteArray imageBuff(reinterpret_cast<const char *>(title.GetThumbnail()),
                                             title.GetThumbnailSize());
                        QPixmap thumb = QPixmap::fromImage(QImage::fromData(imageBuff));
                        if (!thumb.isNull())
                            titleItem->setIcon(0, QIcon(thumb));
                    }

                    for (auto &save : title.titleSaves)
                    {
                        QTreeWidgetItem *saveItem = new QTreeWidgetItem(titleItem);
                        saveItem->setText(0, QString::fromStdWString(save.GetName()));
                        saveItem->setText(1, QString::fromStdString(ByteSizeToString(save.GetFileSize())));

                        saveItem->setData(0, Qt::UserRole, QVariant::fromValue(save.content.get()));
                        saveItem->setData(1, Qt::UserRole, QVariant(QString::fromStdString(save.GetPathOnDevice())));
                        saveItem->setData(2, Qt::UserRole, QVariant(QString::fromStdString(save.GetRawName())));
                        saveItem->setData(3, Qt::UserRole, QVariant(static_cast<quint64>(save.GetFileSize())));
                        saveItem->setData(5, Qt::UserRole, static_cast<quint32>(ContentItemType::Content));

                        if (save.GetThumbnail() != nullptr && save.GetThumbnailSize() != 0)
                        {
                            QByteArray imageBuff(reinterpret_cast<const char *>(save.GetThumbnail()),
                                                 save.GetThumbnailSize());
                            QPixmap thumb = QPixmap::fromImage(QImage::fromData(imageBuff));
                            if (!thumb.isNull())
                                saveItem->setIcon(0, QIcon(thumb));
                        }
                    }
                }
            }
        }

        QTreeWidgetItem *sharedItemFolder = new QTreeWidgetItem(deviceItem);
        sharedItemFolder->setText(0, "Shared Items");
        sharedItemFolder->setIcon(0, QIcon(QPixmap(":/Images/FolderFileIcon.png").scaled(50, 50)));
        sharedItemFolder->setData(0, kFolderRole, kSharedRoot);

        LoadSharedItemCategory("Games", device->games.get(), sharedItemFolder, ":/Images/xboxcontroller.png");
        LoadSharedItemCategory("DLC", device->dlc.get(), sharedItemFolder, ":/Images/xboxglobe.png");
        LoadSharedItemCategory("Demos", device->demos.get(), sharedItemFolder, ":/Images/xboxcircles.png");
        LoadSharedItemCategory("Videos", device->videos.get(), sharedItemFolder, ":/Images/film.png");
        LoadSharedItemCategory("Themes", device->themes.get(), sharedItemFolder, ":/Images/thememedium.png");
        LoadSharedItemCategory("Gamer Pictures", device->gamerPictures.get(), sharedItemFolder, ":/Images/gamerpicture.png");
        LoadSharedItemCategory("Avatar Items", device->avatarItems.get(), sharedItemFolder, ":/Images/profile.png");
        LoadSharedItemCategory("Updates", device->updates.get(), sharedItemFolder, ":/Images/updates.png");
        LoadSharedItemCategory("System Items", device->systemItems.get(), sharedItemFolder, ":/Images/preferences.png");
    }

    if (!devices.isEmpty())
    {
        currentDevice = devices.first();
        UpdateDevicePanel();
        ui->treeWidget->setEnabled(true);
        ui->tabWidget->setEnabled(true);
    }
}

void DeviceContentViewer::ClearSidePanel()
{
    ui->imgTumbnail->setPixmap(QPixmap(":/Images/watermark.png"));
    ui->imgTitleThumbnail->setPixmap(QPixmap(":/Images/watermark.png"));

    ui->lblRawName->setText("...");
    ui->lblTitleID->setText("...");
    ui->lblTitleName->setText("...");
    ui->lblPackageType->setText("...");
    ui->lblFileSize->setText("...");

    ui->btnOpenIn->setEnabled(false);
    ui->btnViewPackage->setEnabled(false);

    currentPackage = nullptr;
}

void DeviceContentViewer::SetLabelText(QLabel *label, const QString &text)
{
    label->setText(text.isEmpty() ? "..." : text);
}

void DeviceContentViewer::UpdateUI()
{
    ClearSidePanel();

    QTreeWidgetItem *item = ui->treeWidget->currentItem();
    if (!item)
        return;

    IXContentHeader *content = item->data(0, Qt::UserRole).value<IXContentHeader*>();
    if (!content)
    {
        currentPackage = nullptr;
        return;
    }

    currentPackage = content;

    SetLabelText(ui->lblRawName, item->data(2, Qt::UserRole).toString());
    ui->lblFileSize->setText(QString::fromStdString(
            ByteSizeToString(item->data(3, Qt::UserRole).toULongLong())));

    if (content->metaData)
    {
        SetLabelText(ui->lblTitleName, QString::fromStdWString(content->metaData->titleName));
        ui->lblTitleID->setText(QString("0x%1").arg(content->metaData->titleID, 8, 16, QChar('0')).toUpper());
        ui->lblPackageType->setText(content->metaData->fileSystem == FileSystemSTFS ? "STFS" : "SVOD");

        if (!content->metaData->thumbnailImage.empty())
        {
            QByteArray imageBuff(reinterpret_cast<const char *>(content->metaData->thumbnailImage.data()),
                                 static_cast<int>(content->metaData->thumbnailImage.size()));
            QPixmap thumb = QPixmap::fromImage(QImage::fromData(imageBuff));
            if (!thumb.isNull())
                ui->imgTumbnail->setPixmap(thumb.scaled(ui->imgTumbnail->size(), Qt::KeepAspectRatio));
        }
    }

    ui->btnViewPackage->setEnabled(true);
    ui->btnOpenIn->setEnabled(false);
}

void DeviceContentViewer::OpenContent(bool updateButton)
{
    IXContentHeader *content = ui->treeWidget->currentItem()
            ? ui->treeWidget->currentItem()->data(0, Qt::UserRole).value<IXContentHeader*>()
            : nullptr;
    if (!content || !content->metaData)
        return;

    try
    {
        if (content->metaData->fileSystem == FileSystemSTFS)
        {
            auto *package = dynamic_cast<StfsPackage *>(content);
            if (!package)
                return;

            if (content->metaData->contentType != Profile)
                package->GetFileListing(true);

            if (updateButton)
                ui->btnViewPackage->setText("Viewing package...");

            QApplication::processEvents();
            PackageViewer viewer(statusBar, package, QList<QAction*>(), QList<QAction*>(), this, false);
            viewer.exec();
        }
        else if (content->metaData->fileSystem == FileSystemSVOD)
        {
            auto *svod = dynamic_cast<SVOD *>(content);
            if (!svod)
                return;

            if (updateButton)
                ui->btnViewPackage->setText("Viewing package...");

            // the device loads SVODs without their file listing; read it on demand
            svod->GetFileListing();

            QApplication::processEvents();
            // the SVOD is owned by the device listing, so the dialog must not delete it
            SvodDialog viewer(svod, statusBar, this, false);
            viewer.exec();
        }
    }
    catch (const std::string &error)
    {
        QMessageBox::critical(this, "Error", QString::fromStdString(error));
    }
    catch (const std::exception &error)
    {
        QMessageBox::critical(this, "Error", QString::fromStdString(error.what()));
    }

    ui->btnViewPackage->setText("View Package");
}

void DeviceContentViewer::UpdateDevicePanel()
{
    if (!currentDevice)
        return;

    ui->lblDeviceName->setText(QString::fromStdWString(currentDevice->GetName()));
    if (currentDevice->GetDeviceType() == FatxHarddrive)
        ui->imgDeviceType->setPixmap(QPixmap(":/Images/harddrive.png"));
    else
        ui->imgDeviceType->setPixmap(QPixmap(":/Images/usb drive.png"));

    const UINT64 total = currentDevice->GetTotalMemory();
    const UINT64 free = currentDevice->GetFreeMemory();
    ui->lblFeeMemory->setText(QString::fromStdString(ByteSizeToString(free)));
    ui->lblUsedMem->setText(QString::fromStdString(ByteSizeToString(total - free)));
}

void DeviceContentViewer::on_treeWidget_itemDoubleClicked(QTreeWidgetItem * /*item*/, int /*column*/)
{
    OpenContent(false);
}

void DeviceContentViewer::on_treeWidget_currentItemChanged(QTreeWidgetItem * /*current*/,
                                                           QTreeWidgetItem * /*previous*/)
{
    UpdateUI();
}

void DeviceContentViewer::on_btnViewPackage_clicked()
{
    OpenContent(true);
}

void DeviceContentViewer::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = ui->treeWidget->itemAt(pos);
    if (!item)
        return;

    const QList<QTreeWidgetItem *> selected = ui->treeWidget->selectedItems();

    // Expand the selection to content files. Device, profile, title and
    // category nodes contribute everything below them, so selecting a profile
    // exports its package and all of its saves.
    QStringList selectedPaths;
    QStringList roots;
    QSet<QString> seenPaths;
    for (QTreeWidgetItem *cur : selected)
    {
        const QString root = ExportRoot(cur);
        if (!root.isEmpty())
            roots.push_back(root);

        CollectContentFiles(cur, selectedPaths, seenPaths);
    }

    QString root = CommonFolder(roots);
    if (root.isEmpty() && !selectedPaths.isEmpty())
        root = ParentFolder(selectedPaths.first());

    // Two layouts: mirror the folders, or drop every file into the chosen
    // folder. The flat variant only differs when the selection spans folders.
    QStringList structurePaths;
    QStringList flatPaths;
    QSet<QString> flatNames;
    bool flatDiffers = false;

    for (const QString &path : selectedPaths)
    {
        const QString relative = path.mid(root.size()).replace('\\', '/');
        structurePaths.push_back(relative);
        flatDiffers = flatDiffers || relative.contains('/');
        flatPaths.push_back(UniqueName(path, flatNames));
    }

    // Deleting still works on single content files only; a folder selection is
    // not recursively removed.
    bool canDelete = !selected.isEmpty();
    for (QTreeWidgetItem *cur : selected)
        if (cur->data(1, Qt::UserRole).toString().isEmpty())
            canDelete = false;

    QMenu contextMenu(this);
    QAction *extract = nullptr;
    QAction *extractFlat = nullptr;
    QAction *remove = nullptr;

    if (!selectedPaths.isEmpty())
    {
        extract = contextMenu.addAction(QPixmap(":/Images/extract.png"), "Copy Selected to Local Disk");

        // A flat export of a single file is the same as the default, so only
        // offer the choice when the layouts actually differ.
        if (flatDiffers)
            extractFlat = contextMenu.addAction(QPixmap(":/Images/extract.png"),
                    "Copy Selected to Local Disk (Files Only)");

        if (canDelete)
            remove = contextMenu.addAction(QPixmap(":/Images/delete.png"), "Delete Selected Items");
    }

    QAction *copyHere = contextMenu.addAction(QPixmap(":/Images/add.png"), "Copy File(s) Here");
    contextMenu.addSeparator();
    QAction *viewPackage = contextMenu.addAction("View Package");
    viewPackage->setEnabled(item->data(0, Qt::UserRole).value<IXContentHeader *>() != nullptr);

    QAction *chosen = contextMenu.exec(ui->treeWidget->mapToGlobal(pos));
    if (chosen == nullptr)
        return;

    if (chosen == viewPackage)
    {
        OpenContent(false);
    }
    else if (chosen == extract || chosen == extractFlat)
    {
        const QString saveDir = QFileDialog::getExistingDirectory(this,
                "Choose a place to extract the files to...", QtHelpers::DefaultLocation());
        if (saveDir.isEmpty())
            return;

        const QStringList &relativePaths = (chosen == extractFlat) ? flatPaths : structurePaths;

        // The FATX extractor works on FatxFileEntry copies and a FatxDrive, and it
        // runs on a worker thread, so the entries must outlive this function.
        FatxDrive *drive = currentDevice->GetFatxDrive();
        QList<void *> filesToExtract;
        try
        {
            for (const QString &path : selectedPaths)
            {
                FatxFileEntry *entry = drive->GetFileEntry(path.toStdString());
                if (entry == nullptr)
                    throw std::string("Could not find " + path.toStdString() + " on the device.");
                filesToExtract.push_back(new FatxFileEntry(*entry));
            }
        }
        catch (const std::string &error)
        {
            for (void *file : filesToExtract)
                delete static_cast<FatxFileEntry *>(file);
            QMessageBox::critical(this, "Error", QString::fromStdString(error));
            return;
        }

        auto *dialog = new MultiProgressDialog(OpExtract, FileSystemFATX,
                drive, saveDir + "/", filesToExtract, this, root, nullptr, relativePaths);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        // the dialog's destructor waits for its worker, so the entries are no longer in use
        connect(dialog, &QObject::destroyed, this, [filesToExtract]() {
            for (void *file : filesToExtract)
                delete static_cast<FatxFileEntry *>(file);
        });
        dialog->setModal(true);
        dialog->show();
        dialog->start();
    }
    else if (chosen == remove)
    {
        const QList<QTreeWidgetItem *> toRemove = selected;

        try
        {
            for (QTreeWidgetItem *curItem : toRemove)
            {
                auto *package = curItem->data(0, Qt::UserRole).value<IXContentHeader *>();
                const std::string path = curItem->data(1, Qt::UserRole).toString().toStdString();

                currentDevice->DeleteFile(package, path);
                CleanupEmptyItems(curItem);
            }
        }
        catch (const std::string &error)
        {
            QMessageBox::critical(this, "Error",
                    "An error occurred while deleting files.\n\n" +
                            QString::fromStdString(error));
        }
    }
    else if (chosen == copyHere)
    {
        const QStringList files = QFileDialog::getOpenFileNames(this, "",
                QtHelpers::DefaultLocation());
        if (!files.isEmpty())
            CopyFilesToDevice(currentDevice, files);
    }
}

void DeviceContentViewer::CleanupEmptyItems(QTreeWidgetItem *leafItem)
{
    QTreeWidgetItem *parent = leafItem->parent();
    delete leafItem;

    // Categories and profiles disappear once they have no content left.
    while (parent != nullptr && parent->parent() != nullptr && parent->childCount() == 0)
    {
        QTreeWidgetItem *next = parent->parent();
        delete parent;
        parent = next;
    }
}

void DeviceContentViewer::CopyFilesToDevice(XContentDevice *device, const QStringList &files)
{
    if (device == nullptr)
        return;

    QList<IXContentHeader *> contentFiles;

    try
    {
        for (const QString &filePath : files)
        {
            const FileSystem fileSystem = IXContentHeader::GetFileSystem(filePath.toStdString());
            IXContentHeader *content = fileSystem == FileSystemSTFS
                    ? static_cast<IXContentHeader *>(new StfsPackage(filePath.toStdString()))
                    : static_cast<IXContentHeader *>(new SVOD(filePath.toStdString()));
            contentFiles.push_back(content);

            const UINT64 required = static_cast<UINT64>(QFileInfo(filePath).size()) +
                    content->metaData->dataFileCombinedSize;
            if (device->GetFreeMemory() < required)
            {
                QMessageBox::critical(this, "Out of Space",
                        "Copying halted. There is not enough free space on the device for " +
                                QString::fromStdWString(content->metaData->displayName) + ".");
                qDeleteAll(contentFiles);
                return;
            }
        }
    }
    catch (const std::string &)
    {
        QMessageBox::critical(this, "Invalid XContent",
                "Copying halted. One or more of the files are not valid Xbox 360 content.");
        qDeleteAll(contentFiles);
        return;
    }

    // Saved games carry the profile they belong to; remap it when that profile
    // is not on this device.
    if (device->profiles)
    {
        for (IXContentHeader *content : contentFiles)
        {
            if (content->metaData->contentType != SavedGame)
                continue;

            UINT64 contentProfileID = 0;
            std::memcpy(&contentProfileID, content->metaData->profileID, sizeof(contentProfileID));

            bool found = false;
            for (auto &profile : *device->profiles)
            {
                if (profile.GetFileSize() == 0)
                    continue;

                UINT64 profileID = 0;
                std::memcpy(&profileID, profile.GetProfileID(), sizeof(profileID));
                if (profileID == contentProfileID)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                ProfileSelectionDialog dialog(device->profiles.get(),
                        QString::fromStdWString(content->metaData->displayName), this);
                dialog.exec();

                if (dialog.HasSelectedProfile())
                {
                    XContentDeviceProfile profile = dialog.GetSelectedProfile();
                    std::memcpy(content->metaData->profileID, profile.GetProfileID(), 8);
                    content->metaData->FixHeaderHash();

                    QtHelpers::ResignWithKV(content->metaData->certificate.ownerConsoleType, this,
                            [content](const std::string &kv)
                            {
                                content->metaData->ResignHeader(kv);
                                content->metaData->WriteMetaData();
                            });
                }
            }
        }
    }

    qDeleteAll(contentFiles);

    QList<void *> filesToInject;
    for (const QString &file : files)
        filesToInject.push_back(new std::string(file.toStdString()));

    auto *dialog = new MultiProgressDialog(OpInject, FileSystemFATX, device, "",
            filesToInject, this);
    dialog->setModal(true);
    dialog->show();
    dialog->start();

    for (void *file : filesToInject)
        delete static_cast<std::string *>(file);

    ui->treeWidget->clear();
    LoadDevicesp();
}

void DeviceContentViewer::onNewDevicesDetected(QList<FatxDrive*> newDrives)
{
    if (newDrives.isEmpty())
        return;

    QList<XContentDevice *> candidates;
    for (auto *drive : newDrives)
    {
        try
        {
            candidates.append(new XContentDevice(drive));
        }
        catch (...)
        {
        }
    }

    startContentLoad(candidates);
}

void DeviceContentViewer::startContentLoad(const QList<XContentDevice *> &candidates)
{
    if (candidates.isEmpty())
        return;

    // One load at a time; a drive detected while loading is queued behind it.
    if (loadWatcher != nullptr)
    {
        queuedContent.append(candidates);
        return;
    }

    loadWatcher = new QFutureWatcher<QList<XContentDevice *>>(this);
    setScanning(true);

    connect(loadWatcher, &QFutureWatcherBase::finished, this, [this]() {
        const QList<XContentDevice *> loaded = loadWatcher->result();

        loadWatcher->deleteLater();
        loadWatcher = nullptr;

        for (auto *device : loaded)
            devices.append(device);

        if (!loaded.isEmpty())
            LoadDevicesp();

        if (!queuedContent.isEmpty())
        {
            const QList<XContentDevice *> next = queuedContent;
            queuedContent.clear();
            startContentLoad(next);
        }
        else
        {
            setScanning(false);
        }
    });

    loadWatcher->setFuture(QtConcurrent::run([candidates]() {
        QList<XContentDevice *> loaded;
        for (auto *device : candidates)
        {
            bool ok = false;
            try
            {
                ok = device->LoadDevice();
            }
            catch (...)
            {
                ok = false;
            }

            if (ok)
                loaded.append(device);
            else
                delete device;
        }
        return loaded;
    }));
}

void DeviceContentViewer::setScanning(bool scanning)
{
    // Track the state ourselves: while the dialog is hidden or minimised
    // scanOverlay->isVisible() is false even after show(), so comparing
    // against it could skip the matching hide() and leave the bar running.
    if (scanning == this->scanning)
        return;

    this->scanning = scanning;

    if (scanning)
    {
        ui->treeWidget->setEnabled(false);

        scanOverlay->setGeometry(rect());
        scanOverlay->raise();
        scanOverlay->show();

        progressBar->setRange(0, 0); // busy animation
        progressBar->setVisible(true);
    }
    else
    {
        scanOverlay->hide();
        scanOverlay->lower();

        progressBar->setVisible(false);
        progressBar->reset();
    }
}
