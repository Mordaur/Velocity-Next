#include "devicecontentviewer.h"
#include "ui_devicecontentviewer.h"

#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QMenu>
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
    progressBar->setMinimumWidth(statusBar->width());
    progressBar->setTextVisible(false);
    progressBar->setVisible(false);
    statusBar->addWidget(progressBar);

    ui->tabWidget->setEnabled(false);

    ui->treeWidget->setColumnWidth(0, 500);
    ui->treeWidget->setColumnWidth(1, 50);

    OPEN = true;
}

DeviceContentViewer::~DeviceContentViewer()
{
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

            QApplication::processEvents();
            SvodDialog viewer(svod, statusBar, this);
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

    // Everything selected must have a device path to be extractable.
    bool canExtract = !selected.isEmpty();
    for (QTreeWidgetItem *cur : selected)
        if (cur->data(1, Qt::UserRole).toString().isEmpty())
            canExtract = false;

    QMenu contextMenu(this);
    QAction *extract = nullptr;
    QAction *remove = nullptr;

    if (canExtract)
    {
        extract = contextMenu.addAction(QPixmap(":/Images/extract.png"), "Copy Selected to Local Disk");
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
    else if (chosen == extract)
    {
        const QString saveDir = QFileDialog::getExistingDirectory(this,
                "Choose a place to extract the files to...", QtHelpers::DefaultLocation());
        if (saveDir.isEmpty())
            return;

        QList<void *> filesToExtract;
        QStringList paths;

        for (QTreeWidgetItem *curItem : selected)
        {
            const QString path = curItem->data(1, Qt::UserRole).toString();
            filesToExtract.push_back(new std::string(path.toStdString()));
            paths.push_back(path);

            // SVOD content is split across several data files.
            auto *content = curItem->data(0, Qt::UserRole).value<IXContentHeader *>();
            if (content != nullptr && content->metaData->fileSystem == FileSystemSVOD)
            {
                const QStringList contentFilePaths =
                        curItem->data(4, Qt::UserRole).toStringList();
                for (const QString &contentFilePath : contentFilePaths)
                {
                    filesToExtract.push_back(new std::string(contentFilePath.toStdString()));
                    paths.push_back(contentFilePath);
                }
            }
        }

        // MultiProgressDialog strips one root from every path; use the deepest
        // directory shared by the selection so the layout is preserved.
        QString rootPath;
        if (!paths.isEmpty())
        {
            rootPath = paths.first();
            rootPath = rootPath.left(rootPath.lastIndexOf('\\') + 1);
            for (const QString &path : paths)
            {
                while (!rootPath.isEmpty() && !path.startsWith(rootPath))
                    rootPath = rootPath.left(rootPath.lastIndexOf('\\', rootPath.size() - 2) + 1);
            }
        }

        auto *dialog = new MultiProgressDialog(OpExtract, FileSystemFATX,
                currentDevice, saveDir, filesToExtract, this, rootPath);
        dialog->setModal(true);
        dialog->show();
        dialog->start();

        for (void *file : filesToExtract)
            delete static_cast<std::string *>(file);
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

                    const std::string kvPath = QtHelpers::GetKVPath(
                            content->metaData->certificate.ownerConsoleType, this);
                    content->metaData->ResignHeader(kvPath);
                    content->metaData->WriteMetaData();
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
    bool added = false;

    for (auto *drive : newDrives)
    {
        try
        {
            auto *device = new XContentDevice(drive);
            if (device->LoadDevice())
            {
                devices.append(device);
                added = true;
            }
            else
            {
                delete device;
            }
        }
        catch (...)
        {
        }
    }

    if (added)
        LoadDevicesp();
}
