#include "packageviewer.h"
#include "ui_packageviewer.h"
#include "xexdialog.h"
#include "multiprogressdialog.h"

#include <XboxInternals/Disc/Asf.h>
#include <XboxInternals/IO/StfsIO.h>

#include "videoplayback.h"

#include <QStorageInfo>
#include <QTemporaryDir>

#include <memory>
#include <stdexcept>
#include <string>

static QString StartAddressText(StfsPackage *package, const StfsFileEntry &entry)
{
    // A zero-block entry has no start block (0xFFFFFF, or 0x7FFFFF from older Velocity).
    if (entry.blocksForFile == 0)
        return QStringLiteral("N/A");

    try
    {
        return "0x" + QString::number(package->BlockToAddress(entry.startingBlockNum), 16).toUpper();
    }
    catch (const std::string &)
    {
        return QStringLiteral("Invalid");
    }
}

PackageViewer::PackageViewer(QStatusBar *statusBar, StfsPackage *package,
        QList<QAction *> gpdActions, QList<QAction *> gameActions, QWidget *parent, bool disposePackage) :
    QDialog(parent),ui(new Ui::PackageViewer), package(package), disposePackage(disposePackage),
    parent (parent), statusBar(statusBar), openInMenu(nullptr), gpdActions(gpdActions),
    gameActions(gameActions)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    ui->setupUi(this);
    QtHelpers::GenAdjustWidgetAppearanceToOS(this);

    ui->treeWidget->header()->setDefaultSectionSize(75);
    ui->treeWidget->header()->resizeSection(0, 200);
    ui->treeWidget->header()->resizeSection(2, 100);

    ui->imgTile->setPixmap(QPixmap());

    // load all of the data
    if (package->IsPEC())
    {
        ui->lblMagic->setText("Magic: <i>N/A</i>");
        ui->lblDisplayName->setText("Display Name: <i>N/A</i>");
        ui->lblTitleName->setText("Title ID: <i>N/A</i>");
        ui->lblType->setText("Type: PEC");

        ui->txtDeviceID->setEnabled(false);
        ui->txtProfileID->setEnabled(false);
    }
    else
    {
        ui->lblMagic->setText("Magic: 0x" + QString::number(package->metaData->magic,
                16).toUpper() + " (" + QString::fromStdString(MagicToString(package->metaData->magic)) + ")");
        ui->lblDisplayName->setText("Display Name: " + QString::fromStdWString(
                    package->metaData->displayName));

        if (package->metaData->magic == LIVE || package->metaData->magic == PIRS)
            ui->btnFix->setText("Rehash");

        try
        {
            ui->lblType->setText(QString::fromStdString("Package Type: " + ContentTypeToString(
                        package->metaData->contentType)));
        }
        catch (...)
        {
            ui->lblType->setText("Package Type: <i>Unknown</i>");
        }
        ui->lblTitleName->setText("Title ID: " + QString::number(package->metaData->titleID, 16).toUpper());

        QString builder = "";
        for (int i = 0; i < 8; i++)
            builder += QString("%1").arg((u8)package->metaData->profileID[i], 2, 16, QChar('0')).toUpper();

        ui->txtProfileID->setText(builder.toUpper());

        builder = "";
        for (int i = 0; i < 20; i++)
            builder += QString("%1").arg((u8)package->metaData->deviceID[i], 2, 16, QChar('0')).toUpper();

        ui->txtDeviceID->setText(builder.toUpper());

    // set the thumbnail
    if (!package->metaData->thumbnailImage.empty())
    {
        QByteArray imageBuff(reinterpret_cast<const char*>(package->metaData->thumbnailImage.data()),
            static_cast<int>(package->metaData->thumbnailImage.size()));
        ui->imgTile->setPixmap(QPixmap::fromImage(QImage::fromData(imageBuff)));
    }
    else
    {
        ui->imgTile->setPixmap(QPixmap());
    }

        if (package->metaData->magic != CON)
        {
            ui->txtProfileID->setEnabled(false);
            ui->txtDeviceID->setEnabled(false);
        }

        openInMenu = new QMenu(this);
        connect(openInMenu, SIGNAL(aboutToShow()), this, SLOT(aboutToShow()));

        if (package->metaData->contentType == Profile)
        {
            profileEditor = new QAction("Profile Editor", this);
            gameAdder = new QAction("Game Adder", this);

            connect(openInMenu, SIGNAL(triggered(QAction*)), this, SLOT(onOpenInSelected(QAction*)));

            openInMenu->addAction(profileEditor);
            openInMenu->addAction(gameAdder);
        }
    }

    listing = package->GetFileListing();
    PopulateTreeWidget(&listing);

    if (package->metaData->contentType == SavedGame)
    {
        for (int x = 0; x < gameActions.size(); x++)
        {
            u32 titleId = gameActions.at(x)->property("titleid").toUInt();
            if (titleId == package->metaData->titleID)
            {
                gameActions.at(x)->setProperty("package", QVariant::fromValue(package));
                openInMenu->addAction(gameActions.at(x));
            }
        }
    }


    if (openInMenu != nullptr && openInMenu->actions().size() > 0)
    {
        ui->btnOpenIn->setEnabled(true);
        ui->btnOpenIn->setMenu(openInMenu);
    }

    // setup the context menus
    ui->treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->treeWidget, SIGNAL(customContextMenuRequested(QPoint)), this,
            SLOT(showRemoveContextMenu(QPoint)));

    ui->imgTile->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->imgTile, SIGNAL(customContextMenuRequested(QPoint)), this,
            SLOT(showSaveImageContextMenu(QPoint)));
}

PackageViewer::~PackageViewer()
{
    // Stop any extraction that still holds `package` before it is released.
    cancelProgressDialogs(this);

    // Child XexDialogs own an StfsIO whose destructor flushes through `package`.
    const auto xexDialogs = findChildren<XexDialog*>();
    for (XexDialog *dialog : xexDialogs)
        delete dialog;

    for (int i = 0; i < gpdActions.size(); i++)
        if (gpdActions.at(i)->property("package").isValid())
            gpdActions.at(i)->setProperty("package", QVariant());

    for (int i = 0; i < gameActions.size(); i++)
        if (gameActions.at(i)->property("package").isValid())
            gameActions.at(i)->setProperty("package", QVariant());

    if (disposePackage)
    {
        package->Close();
        delete package;
    }
    delete ui;
}

void PackageViewer::PopulateTreeWidget(StfsFileListing *entry, QTreeWidgetItem *parent)
{
    bool isRootEntry = entry->folder.entryIndex == 0xFFFF;

    for (unsigned int i = 0; i < entry->folderEntries.size(); i++)
    {
        QTreeWidgetItem *folder;
        if (entry->folderEntries.at(i).folder.pathIndicator != 0xFFFF)
            folder = new QTreeWidgetItem(parent);
        else
            folder = new QTreeWidgetItem(ui->treeWidget);

        folder->setText(0, QString::fromStdString(entry->folderEntries.at(i).folder.name));
        folder->setIcon(0, QIcon(":/Images/FolderFileIcon.png"));
        folder->setText(1, "0 bytes");
        folder->setText(2, "N/A");
        folder->setText(3, "N/A");

        PopulateTreeWidget(&entry->folderEntries.at(i), folder);
    }

    // add all files
    for (u32 i = 0; i < entry->fileEntries.size(); i++)
    {
        QTreeWidgetItem *fileEntry;
        if (!isRootEntry)
            fileEntry = new QTreeWidgetItem(parent);
        else
            fileEntry = new QTreeWidgetItem(ui->treeWidget);

        QString path;
        GetPackagePath(fileEntry, &path);
        SetIcon(entry->fileEntries.at(i).name, &entry->fileEntries.at(i), fileEntry);

        QString name = QString::fromStdString(entry->fileEntries.at(i).name);
        fileEntry->setText(0, name);
        fileEntry->setText(1, QString::fromStdString(ByteSizeToString(entry->fileEntries.at(i).fileSize)));
        fileEntry->setText(2, StartAddressText(package, entry->fileEntries.at(i)));
        fileEntry->setText(3, "0x" + QString::number(entry->fileEntries.at(i).startingBlockNum,
                16).toUpper());

        if (!package->IsPEC() && package->metaData->contentType == Profile)
        {
            for (int x = 0; x < gpdActions.size(); x++)
            {
                if (QString::number(gpdActions.at(x)->property("titleid").toUInt(),
                        16).toUpper() + ".Gpd" == name.toUpper())
                {
                    gpdActions.at(x)->setProperty("package", QVariant::fromValue(package));
                    openInMenu->addAction(gpdActions.at(x));
                }
            }
        }
    }
}

void PackageViewer::GetPackagePath(QTreeWidgetItem *item, QString *out, bool folderOnly)
{
    bool hasParent = item->parent() != nullptr;

    if (!hasParent && folderOnly)
        return;

    QString slash = "";
    if (hasParent)
        slash = "\\";

    out->push_front(slash + item->text(0));

    if (hasParent)
        GetPackagePath(item->parent(), out);
}

void PackageViewer::SetIcon(string name, StfsFileEntry *entry, QTreeWidgetItem *item)
{
    try
    {
        QIcon toSet;
        QtHelpers::GetFileIcon(package->GetFileMagic(*entry), QString::fromStdString(name), toSet, *item);
        item->setIcon(0, toSet);
    }
    catch (string error)
    {
        QMessageBox::critical(this, "", QString::fromStdString(error));
    }
}

void PackageViewer::on_btnFix_clicked()
{
    bool success = true, resigned = false;

    // verify the profile and device id
    if ((!QtHelpers::VerifyHexStringBuffer(ui->txtProfileID->text()) ||
            ui->txtProfileID->text().length() != 16) && !package->IsPEC())
    {
        QMessageBox::warning(this, "Invalid Value", "The profile ID must be 16 hexadecimal digits.");
        return;
    }
    if ((!QtHelpers::VerifyHexStringBuffer(ui->txtDeviceID->text()) ||
            ui->txtDeviceID->text().length() != 40) && !package->IsPEC())
    {
        QMessageBox::warning(this, "Invalid Value", "The device ID must be 40 hexadecimal digits.");
        return;
    }

    // save the ids if it's not PEC
    if (!package->IsPEC())
    {
        QtHelpers::ParseHexStringBuffer(ui->txtProfileID->text(), package->metaData->profileID, 8);
        QtHelpers::ParseHexStringBuffer(ui->txtDeviceID->text(), package->metaData->deviceID, 20);
    }

    try
    {
        package->Rehash();
    }
    catch (string error)
    {
        success = false;
        QMessageBox::critical(this, "Error",
                "Failed to rehash the package.\n\n" + QString::fromStdString(error));
    }

    if (success && (package->metaData->magic == CON || package->IsPEC()))
    {
        // ResignWithKV warns when the picker is cancelled or the chosen file is
        // not a keyvault, and reports backend failures itself.
        resigned = QtHelpers::ResignWithKV(package->metaData->certificate.ownerConsoleType, this,
                [&](const std::string &kv) { package->Resign(kv); });
        success = resigned;
    }

    if (success)
    {
        QString resignedStr = (resigned ? " and resigned" : "");
        statusBar->showMessage("Rehashed" + resignedStr + " successfully", 3000);
        QMessageBox::information(this, "Success",
                "The package has successfully been rehashed" + resignedStr + ".");
    }
    else if (package->metaData->magic == CON || package->IsPEC())
    {
        // The rehash itself was applied, but the package was not resigned; do
        // not claim a full success.
        statusBar->showMessage("Rehashed, not resigned", 3000);
    }
}

void PackageViewer::on_btnViewAll_clicked()
{
    Metadata meta(statusBar, package->metaData, package->IsPEC(), this);
    meta.exec();

    if (package->metaData->magic == LIVE || package->metaData->magic == PIRS)
        ui->btnFix->setText("Rehash");
    else
        ui->btnFix->setText("Rehash/Resign");
}

void PackageViewer::showSaveImageContextMenu(QPoint point)
{
    if (ui->imgTile->pixmap().isNull())
        return;

    QPoint globalPos = ui->imgTile->mapToGlobal(point);
    QMenu contextMenu;

    contextMenu.addAction(QPixmap(":/Images/save.png"), "Save Image");
    QAction *selectedItem = contextMenu.exec(globalPos);

    if (selectedItem == nullptr)
        return;
    else if (selectedItem->text() == "Save Image")
    {
        QString imageSavePath = QFileDialog::getSaveFileName(this,
                "Choose a location to save the thumbnail", QtHelpers::DefaultLocation() + "\\thumbnail.png",
                "*.png");
        if (imageSavePath == "")
            return;

        ui->imgTile->pixmap().save(imageSavePath, "PNG");

        statusBar->showMessage("Thumbnail image saved successfully", 3000);
    }
}

void PackageViewer::on_txtSearch_textChanged(const QString &arg1)
{
    QtHelpers::SearchTreeWidget(ui->treeWidget, ui->txtSearch, arg1);
}

void PackageViewer::onOpenInSelected(QAction *action)
{
    if (action)
    {
        if (action == gameAdder)
        {
            bool ok;
            GameAdderDialog *dialog = new GameAdderDialog(package, this, false, &ok);
            if (ok)
            {
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                
                // Handle the custom operationsComplete signal emitted after Rehash/Resign
                connect(dialog, &GameAdderDialog::operationsComplete, this, [this, dialog]() {
                    // CRITICAL: Defer tree refresh with QTimer to avoid event loop issues
                    QTimer::singleShot(0, this, [this, dialog]() {
                        try {
                            listing = package->GetFileListing(true);  // Force update after Rehash/Resign
                            PopulateTreeWidget(&listing);
                        } catch (const std::exception &e) {
                            QMessageBox::warning(this, "Refresh Error", QString("Failed to refresh package view: %1").arg(e.what()));
                        } catch (const std::string &error) {
                            QMessageBox::warning(this, "Refresh Error", QString("Failed to refresh package view: %1").arg(QString::fromStdString(error)));
                        } catch (...) {
                            QMessageBox::warning(this, "Refresh Error", "Failed to refresh package view.");
                        }
                    });
                });
                
                // Handle normal dialog closure (finished signal for Cancel/X button)
                connect(dialog, &QDialog::finished, this, [this, dialog](int result) {
                    Q_UNUSED(result);
                    try {
                        listing = package->GetFileListing(true);  // Force update after Rehash/Resign
                        PopulateTreeWidget(&listing);
                    } catch (const std::exception &e) {
                        QMessageBox::warning(this, "Refresh Error", QString("Failed to refresh package view: %1").arg(e.what()));
                    } catch (const std::string &error) {
                        QMessageBox::warning(this, "Refresh Error", QString("Failed to refresh package view: %1").arg(QString::fromStdString(error)));
                    } catch (...) {
                        QMessageBox::warning(this, "Refresh Error", "Failed to refresh package view.");
                    }
                });
                dialog->show();
            } else {
                // If dialog construction failed, delete allocated pointer
                delete dialog;
            }
        }
        else if (action == profileEditor)
        {
            ProfileEditor editor(statusBar, package, false, this);

            if(editor.isOk())
                editor.exec();
        }
    }
}

void PackageViewer::aboutToShow()
{
    for (int i = 0; i < gpdActions.size(); i++)
        gpdActions.at(i)->setProperty("fromPackageViewer", QVariant(true));
    for (int i = 0; i < gameActions.size(); i++)
        gameActions.at(i)->setProperty("fromPackageViewer", QVariant(true));
}

void PackageViewer::GetSubFilesStfs(StfsFileListing *parent, QList<void *> &entries,
        QString currentPath)
{
    for (size_t i = 0; i < parent->fileEntries.size(); i++)
    {
        StfsExtractEntry *exEntry = new StfsExtractEntry;
        exEntry->entry = parent->fileEntries.at(i);
        exEntry->path = currentPath;
        entries.push_back(exEntry);
    }

    for (size_t i = 0; i < parent->folderEntries.size(); i++)
        GetSubFilesStfs(&parent->folderEntries.at(i), entries,
                currentPath + QString::fromStdString(parent->folderEntries.at(i).folder.name) + "/");
}

void PackageViewer::showRemoveContextMenu(QPoint point)
{
    int amount = ui->treeWidget->selectedItems().length();

    QPoint globalPos = ui->treeWidget->mapToGlobal(point);
    QMenu contextMenu;

    contextMenu.addAction(QPixmap(":/Images/extract.png"), "Extract All");
    contextMenu.addSeparator();

    bool isFolder = false;
    if (amount == 0)
    {
        contextMenu.addAction(QPixmap(":/Images/add.png"), "Inject Here");
    }
    else
    {
        isFolder = ui->treeWidget->selectedItems()[0]->text(3) == "N/A";

        if (!isFolder)
        {
            if (amount == 1 &&
                    ui->treeWidget->selectedItems()[0]->data(1, Qt::UserRole).toString() == "Video")
                contextMenu.addAction("Play");

            contextMenu.addAction(QPixmap(":/Images/extract.png"), "Extract Selected");
            contextMenu.addAction(QPixmap(":/Images/delete.png"), "Remove Selected");
        }

        if (amount == 1)
        {
            contextMenu.addSeparator();
            contextMenu.addAction(QPixmap(":/Images/rename.png"), "Rename Selected");

            if (!isFolder)
                contextMenu.addAction(QPixmap(":/Images/replace.png"), "Replace File");
            else
            {
                contextMenu.addSeparator();
                contextMenu.addAction(QPixmap(":/Images/add.png"), "Inject Here");
            }
        }

        contextMenu.addSeparator();
        contextMenu.addAction(QPixmap(":/Images/properties.png"), "View Properties");
    }

    QAction *selectedItem = contextMenu.exec(globalPos);
    if(selectedItem == nullptr)
        return;

    QList <QTreeWidgetItem*> items = ui->treeWidget->selectedItems();
    int totalCount = items.count(), successCount = 0;

    if (selectedItem->text() == "Play")
    {
        QString packagePath;
        GetPackagePath(items.at(0), &packagePath);
        PlayVideo(packagePath);
    }
    else if (selectedItem->text() == "Extract Selected")
    {
        bool multiple = totalCount > 1;

        QString path;
        if (multiple)
            path = QFileDialog::getExistingDirectory(this, "Save Location", QtHelpers::DefaultLocation()) + "/";
        else
            path = QFileDialog::getSaveFileName(this, "Save Location",
                    QtHelpers::DefaultLocation() + "/" + ui->treeWidget->selectedItems()[0]->text(0));

        if (path.isEmpty())
            return;


        QList<void*> outFiles;
        for (int i = 0; i < totalCount; i++)
        {
            QString packagePath;
            GetPackagePath(items.at(i), &packagePath);

            StfsExtractEntry *entry = new StfsExtractEntry;
            entry->path = "";
            entry->entry = package->GetFileEntry(packagePath.toStdString());
            outFiles.append(const_cast<void*>((void*)entry));
        }

        try
        {
            MultiProgressDialog *dialog = new MultiProgressDialog(OpExtract, FileSystemSTFS, package,
                    path.replace("\\", "/"), outFiles, this);
            dialog->setModal(true);
            dialog->show();
            dialog->start();

            successCount++;
        }
        catch (string error)
        {
            QMessageBox::critical(this, "Error", "Failed to extract file.\n\n" + QString::fromStdString(error));
            return;
        }

        statusBar->showMessage("Selected files extracted successfully", 3000);

    }
    else if (selectedItem->text() == "Extract All")
    {
        QString path = QFileDialog::getExistingDirectory(this, "Save Location",
                QtHelpers::DefaultLocation()) + "/";

        QList<void*> entries;
        GetSubFilesStfs(&listing, entries);

        try
        {
            MultiProgressDialog *dialog = new MultiProgressDialog(OpExtract, FileSystemSTFS, package,
                    path.replace("\\", "/"), entries, this);
            dialog->setModal(true);
            dialog->show();
            dialog->start();
        }
        catch (string error)
        {
            QMessageBox::critical(this, "Error", "Failed to extract file.\n\n" + QString::fromStdString(error));
            return;
        }

        statusBar->showMessage("All files extracted successfully", 3000);
    }
    else if (selectedItem->text() == "Remove Selected")
    {
        for (int i = 0; i < totalCount; i++)
        {
            QString packagePath;
            GetPackagePath(items.at(i), &packagePath);
            try
            {
                package->RemoveFile(packagePath.toStdString());
                delete items.at(i);
                successCount++;
            }
            catch (string error)
            {
                QMessageBox::critical(this, "Error", "Failed to delete file.\n\n" + QString::fromStdString(error));
            }
        }

        if (successCount == totalCount)
            statusBar->showMessage("Selected files removed successfully", 3000);
        else
            QMessageBox::warning(this, "Warning",
                    "Some files could not be deleted.\n\nSuccessful Deletions: " + QString::number(
                        successCount) + " / " + QString::number(totalCount));
    }
    else if (selectedItem->text() == "Rename Selected")
    {
        QString packagePath;
        GetPackagePath(items.at(0), &packagePath);
        try
        {
            string newName = items.at(0)->text(0).toStdString();

            RenameDialog dialog(this, &newName);
            dialog.exec();

            if (newName == items.at(0)->text(0).toStdString())
                return;

            package->RenameFile(newName, packagePath.toStdString());
            items.at(0)->setText(0, QString::fromStdString(newName));
            statusBar->showMessage("File renamed successfully", 3000);
        }
        catch (string error)
        {
            QMessageBox::critical(this, "Error", "Failed to rename file.\n\n" + QString::fromStdString(error));
        }
    }
    else if (selectedItem->text() == "Replace File")
    {
        QString packagePath;
        GetPackagePath(items.at(0), &packagePath);

        QString path = QFileDialog::getOpenFileName(this, "New File",
                QtHelpers::DefaultLocation() + "/" + ui->treeWidget->selectedItems()[0]->text(0));
        if (path.isEmpty())
            return;

        try
        {
            SingleProgressDialog dialog(FileSystemSTFS, package, OpReplace,
                    packagePath, path, nullptr, this);
            dialog.setModal(true);
            dialog.show();
            dialog.start();
        }
        catch(string error)
        {
            QMessageBox::critical(this, "Error", "Failed to replace file.\n\n" + QString::fromStdString(error));
        }

        statusBar->showMessage("File replaced successfully", 3000);
    }
    else if (selectedItem->text() == "Inject Here")
    {
        QString packagePath;

        QString path = QFileDialog::getOpenFileName(this, "New File", QtHelpers::DefaultLocation());
        if (path.isEmpty())
            return;

        if (totalCount == 1)
        {
            GetPackagePath(items.at(0), &packagePath, true);
            packagePath += "\\";

            if (isFolder && items.at(0)->parent() == nullptr)
                packagePath.append(items.at(0)->text(0) + "\\");
        }

        QFileInfo pathInfo(path);
        packagePath.append(pathInfo.fileName());

        try
        {
            StfsFileEntry injectedEntry;
            SingleProgressDialog dialog(FileSystemSTFS, package, OpInject,
                    packagePath, path, &injectedEntry, this);
            dialog.setModal(true);
            dialog.show();
            dialog.start();

            listing = package->GetFileListing();

            QTreeWidgetItem *fileEntry;
            if (injectedEntry.pathIndicator != 0xFFFF)
                fileEntry = new QTreeWidgetItem(items.at(0));
            else
                fileEntry = new QTreeWidgetItem(ui->treeWidget);

            QString filePath;
            GetPackagePath(fileEntry, &filePath);
            SetIcon(injectedEntry.name, &injectedEntry, fileEntry);

            fileEntry->setText(0, QString::fromStdString(injectedEntry.name));
            fileEntry->setText(1, QString::fromStdString(ByteSizeToString(injectedEntry.fileSize)));
            fileEntry->setText(2, StartAddressText(package, injectedEntry));
            fileEntry->setText(3, "0x" + QString::number(injectedEntry.startingBlockNum, 16).toUpper());

            statusBar->showMessage("File injected successfully", 3000);
        }
        catch(string error)
        {
            QMessageBox::critical(this, "Error", "Failed to replace file.\n\n" + QString::fromStdString(error));
        }
    }
    else if (selectedItem->text() == "View Properties")
    {
        try
        {
            QString packagePath;
            GetPackagePath(items.at(0), &packagePath);

            StfsFileEntry entry = package->GetFileEntry(packagePath.toStdString(), true);
            bool folder = entry.flags & 2;

            bool changed = false;
            PropertiesDialog dialog(&entry, packagePath, &changed, items.at(0)->icon(0),
                    items.at(0)->childCount() != 0, this);
            dialog.exec();

            if (changed)
            {
                package->GetFileEntry(packagePath.toStdString(), true, &entry, false);
                listing = package->GetFileListing();

                items.at(0)->setText(0, QString::fromStdString(entry.name));

                bool newStatus = (entry.flags & 2);
                if (folder != newStatus)
                {
                    if (newStatus)
                    {
                        items.at(0)->setIcon(0, QIcon(":/Images/FolderFileIcon.png"));
                        items.at(0)->setText(1, "0");
                        items.at(0)->setText(2, "N/A");
                        items.at(0)->setText(3, "N/A");
                    }
                    else
                    {
                        QString filePath;
                        GetPackagePath(items.at(0), &filePath);
                        SetIcon(entry.name, &entry, items.at(0));
                        items.at(0)->setText(0, QString::fromStdString(entry.name));
                        items.at(0)->setText(1, "0x" + QString::number(entry.fileSize, 16).toUpper());
                        items.at(0)->setText(2, StartAddressText(package, entry));
                        items.at(0)->setText(3, "0x" + QString::number(entry.startingBlockNum, 16).toUpper());
                    }
                }
            }
        }
        catch(string error)
        {
            QMessageBox::critical(this, "Error", "Failed to open file.\n\n" + QString::fromStdString(error));
        }
    }
}

bool PackageViewer::PlayVideo(const QString &pathInPackage)
{
    if (!VideoPlayback::IsAvailable())
    {
        VideoPlayback::Open(this, pathInPackage, pathInPackage, nullptr);
        return false;
    }

    const StfsFileEntry entry = package->GetFileEntry(pathInPackage.toStdString());
    if (entry.nameLen == 0)
    {
        QMessageBox::warning(this, "Video", "The file could not be found in the package.");
        return false;
    }

    if (entry.fileSize > 0x80000000ull)
    {
        QMessageBox::warning(this, "Video",
                "The video is larger than 2 GiB and cannot be copied to the temporary directory.");
        return false;
    }

    const QStorageInfo storage(QDir::tempPath());
    if (storage.isValid() && entry.fileSize > 0 &&
            static_cast<quint64>(storage.bytesAvailable()) < entry.fileSize + (64ull << 20))
    {
        QMessageBox::warning(this, "Video",
                "There is not enough free space in the temporary directory for this video.");
        return false;
    }

    try
    {
        // Probe the package stream before extracting: DRM-protected files can
        // only fail inside the media backend with an opaque format error.
        std::unique_ptr<StfsIO> io(package->GetStfsIO(pathInPackage.toStdString()));
        if (io && XboxInternals::Asf::IsDrmProtected(io.get()))
        {
            QMessageBox::warning(this, "Video",
                    "This video is DRM-protected (Windows Media DRM / PlayReady) and "
                    "cannot be played.");
            return false;
        }
    }
    catch (const std::string &error)
    {
        QMessageBox::warning(this, "Video", QString::fromStdString(error));
        return false;
    }
    catch (const std::exception &error)
    {
        QMessageBox::warning(this, "Video", QString(error.what()));
        return false;
    }

    auto tempDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/velocity-video-XXXXXX");
    if (!tempDir->isValid())
    {
        QMessageBox::warning(this, "Video",
                "Could not create a temporary directory for the video.");
        return false;
    }

    const QString fileName = QFileInfo(pathInPackage).fileName();
    const QString destination = tempDir->filePath(fileName);
    package->ExtractFile(pathInPackage.toStdString(), destination.toStdString());

    return VideoPlayback::Open(this, destination, fileName, std::move(tempDir));
}

void PackageViewer::on_treeWidget_itemDoubleClicked(QTreeWidgetItem *item, int /*column*/)
{
    // make sure that the item clicked isn't a folder
    if (item->text(2) == "N/A")
        return;

    // make sure the file double clicked on is a gpd
    int index = item->text(0).lastIndexOf(".");
    QString extension;
    if (index != -1)
        extension = item->text(0).mid(index).toLower();

    if (item->data(1, Qt::UserRole).toString() == "Video")
    {
        QString packagePath;
        GetPackagePath(item, &packagePath);
        PlayVideo(packagePath);
    }
    else if (item->data(1, Qt::UserRole).toString() == "Xdbf")
    {
        try
        {
            // get the path of the file in the package
            QString packagePath;
            GetPackagePath(item, &packagePath);

            // verify the magic
            if (package->GetFileMagic(packagePath.toStdString()) != 0x58444246)
            {
                statusBar->showMessage("Invalid Gpd", 3000);
                return;
            }

            // get a temporary file name
            QString tempName = (QDir::tempPath() + "/" + QUuid::createUuid().toString().replace("{",
                    "").replace("}", "").replace("-", ""));
            string tempNameStd = tempName.toStdString();

            // extract the file to a temporary location
            package->ExtractFile(packagePath.toStdString(), tempNameStd);

            // parse the gpd
            auto gpd = std::make_unique<GpdBase>(tempNameStd);
            statusBar->showMessage("Gpd parsed successfully", 3000);

            bool changed = false;
            {
                XdbfDialog dialog(statusBar, std::move(gpd), &changed, this);
                dialog.exec();
            }

            if (changed)
                package->ReplaceFile(tempNameStd, packagePath.toStdString());

            // delete the temp file
            QFile::remove(tempName);
        }
        catch(string error)
        {
            QMessageBox::critical(this, "Error", "Failed to open the Gpd.\n\n" + QString::fromStdString(error));
        }
    }
    else if (item->data(1, Qt::UserRole).toString() == "STRB")
    {
        // get the path of the file in the package
        QString packagePath;
        GetPackagePath(item, &packagePath);

        // verify the magic
        if (package->GetFileMagic(packagePath.toStdString()) != 0x53545242)
            return;

        // get a temporary file name
        string tempName = (QDir::tempPath() + "/" + QUuid::createUuid().toString().replace("{",
                "").replace("}", "").replace("-", "")).toStdString();

        // extract the file to a temporary location
        package->ExtractFile(packagePath.toStdString(), tempName);

        // show the avatar asset dialog
        AvatarAsset *asset = new AvatarAsset(tempName);
        statusBar->showMessage("STRB file parsed successfully", 3000);

        StrbDialog dialog(asset, this);
        dialog.exec();

        // delete the temp file
        remove(tempName.c_str());
    }
    else if (item->data(1, Qt::UserRole).toString() == "XEX")
    {
        try
        {
            QString packagePath;
            GetPackagePath(item, &packagePath);

            std::unique_ptr<StfsIO> io(package->GetStfsIO(packagePath.toStdString()));
            if (!io)
                throw std::runtime_error("Failed to create streaming IO for XEX file.");

            auto *xex = new Xex(io.get());
            XexDialog *dialog = new XexDialog(xex, this, io.release());
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();

            if (statusBar)
                statusBar->showMessage("XEX opened successfully", 3000);
        }
        catch (const std::exception &e)
        {
            QMessageBox::critical(this, "Error", QString::fromLatin1("Failed to open XEX file.\n\n%1").arg(e.what()));
        }
        catch (const std::string &error)
        {
            QMessageBox::critical(this, "Error", "Failed to open XEX file.\n\n" + QString::fromStdString(error));
        }
        catch (...)
        {
            QMessageBox::critical(this, "Error", "Failed to open XEX file.");
        }
    }
    else if (item->data(1, Qt::UserRole).toString() == "Image")
    {
        // get a temporary file name
        string tempName = (QDir::tempPath() + "/" + QUuid::createUuid().toString().replace("{",
                "").replace("}", "").replace("-", "")).toStdString();

        // extract the file to a temporary location
        QString imagePath;
        GetPackagePath(item, &imagePath);

        package->ExtractFile(imagePath.toStdString(), tempName);

        // display the image
        QImage image(QString::fromStdString(tempName));
        if (image.isNull())
            return;

        ImageDialog *dialog = new ImageDialog(image, item->text(0), this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();

        // delete the temp file
        QFile::remove(QString::fromStdString(tempName));
    }
    else if (item->data(1, Qt::UserRole).toString() == "XML")
    {
        // get a temporary file name
        string tempName = (QDir::tempPath() + "/" + QUuid::createUuid().toString().replace("{",
                "").replace("}", "").replace("-", "")).toStdString();

        // extract the file to a temporary location
        QString xmlPath;
        GetPackagePath(item, &xmlPath);

        package->ExtractFile(xmlPath.toStdString(), tempName);

        // Read XML file with proper encoding detection (Xbox 360 uses Windows encodings)
        QFile file(QString::fromStdString(tempName));
        if (file.open(QIODevice::ReadOnly))
        {
            QByteArray data = file.readAll();
            file.close();
            
            QString xmlContent;
            
            // Check for BOM (Byte Order Mark) to detect encoding
            if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && 
                (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
            {
                // UTF-8 with BOM
                auto toUtf8 = QStringDecoder(QStringDecoder::Utf8);
                xmlContent = toUtf8(data.mid(3)); // Skip BOM
            }
            else if (data.size() >= 2 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xFE)
            {
                // UTF-16 LE with BOM
                auto toUtf16 = QStringDecoder(QStringDecoder::Utf16LE);
                xmlContent = toUtf16(data.mid(2)); // Skip BOM
            }
            else if (data.size() >= 2 && (unsigned char)data[0] == 0xFE && (unsigned char)data[1] == 0xFF)
            {
                // UTF-16 BE with BOM
                auto toUtf16 = QStringDecoder(QStringDecoder::Utf16BE);
                xmlContent = toUtf16(data.mid(2)); // Skip BOM
            }
            else
            {
                // No BOM, try UTF-8 first (most common for Xbox 360 config files)
                auto toUtf8 = QStringDecoder(QStringDecoder::Utf8);
                xmlContent = toUtf8(data);
                
                // If UTF-8 failed, try Windows-1252/Latin1
                if (xmlContent.contains(QChar::ReplacementCharacter))
                {
                    auto toLatin1 = QStringDecoder(QStringDecoder::Latin1);
                    xmlContent = toLatin1(data);
                }
            }

            // Display in XML viewer dialog
            XmlDialog *dialog = new XmlDialog(xmlContent, item->text(0), this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        }

        // delete the temp file
        QFile::remove(QString::fromStdString(tempName));
    }
    else if (item->data(1, Qt::UserRole).toString() == "Text")
    {
        // get a temporary file name
        string tempName = (QDir::tempPath() + "/" + QUuid::createUuid().toString().replace("{",
                "").replace("}", "").replace("-", "")).toStdString();

        // extract the file to a temporary location
        QString textPath;
        GetPackagePath(item, &textPath);

        package->ExtractFile(textPath.toStdString(), tempName);

        // Read text file with proper encoding detection (Xbox 360 uses Windows encodings)
        QFile file(QString::fromStdString(tempName));
        if (file.open(QIODevice::ReadOnly))
        {
            QByteArray data = file.readAll();
            file.close();
            
            QString textContent;
            
            // Check for BOM (Byte Order Mark) to detect encoding
            if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && 
                (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
            {
                // UTF-8 with BOM
                auto toUtf8 = QStringDecoder(QStringDecoder::Utf8);
                textContent = toUtf8(data.mid(3)); // Skip BOM
            }
            else if (data.size() >= 2 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xFE)
            {
                // UTF-16 LE with BOM
                auto toUtf16 = QStringDecoder(QStringDecoder::Utf16LE);
                textContent = toUtf16(data.mid(2)); // Skip BOM
            }
            else if (data.size() >= 2 && (unsigned char)data[0] == 0xFE && (unsigned char)data[1] == 0xFF)
            {
                // UTF-16 BE with BOM
                auto toUtf16 = QStringDecoder(QStringDecoder::Utf16BE);
                textContent = toUtf16(data.mid(2)); // Skip BOM
            }
            else
            {
                // No BOM, try UTF-8 first (most common)
                auto toUtf8 = QStringDecoder(QStringDecoder::Utf8);
                textContent = toUtf8(data);
                
                // If UTF-8 failed, try Windows-1252/Latin1
                if (textContent.contains(QChar::ReplacementCharacter))
                {
                    auto toLatin1 = QStringDecoder(QStringDecoder::Latin1);
                    textContent = toLatin1(data);
                }
            }

            // Display in text viewer dialog
            TextDialog *dialog = new TextDialog(textContent, item->text(0), this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        }

        // delete the temp file
        QFile::remove(QString::fromStdString(tempName));
    }
    else if (item->data(1, Qt::UserRole).toString() == "ZIP")
    {
        // get a temporary file name
        string tempName = (QDir::tempPath() + "/" + QUuid::createUuid().toString().replace("{",
                "").replace("}", "").replace("-", "")).toStdString();

        // extract the ZIP file to a temporary location
        QString zipPath;
        GetPackagePath(item, &zipPath);

        package->ExtractFile(zipPath.toStdString(), tempName);

        // Open ZIP viewer dialog
        ZipViewer *dialog = new ZipViewer(QString::fromStdString(tempName), item->text(0), this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        
        // Note: temp file will be cleaned up when dialog closes
    }
    else if (item->data(1, Qt::UserRole).toString() == "PEC")
    {
        try
        {
            // get a temporary file name
            string tempName = (QDir::tempPath() + "/" + QUuid::createUuid().toString().replace("{",
                    "").replace("}", "").replace("-", "")).toStdString();

            // extract the file to a temporary location
            QString packagePath;
            GetPackagePath(item, &packagePath);

            package->ExtractFile(packagePath.toStdString(), tempName);

            StfsPackage pec (tempName, StfsPackagePEC);
            PackageViewer dialog(statusBar, &pec, gpdActions, gameActions, this, false);
            dialog.exec();

            pec.Close();

            // replace the PEC with the modified one
            package->ReplaceFile(tempName, packagePath.toStdString());

            // delete the temp file
            remove(tempName.c_str());
        }
        catch(string error)
        {
            QMessageBox::critical(this, "Error",
                    "Failed to open PEC file.\n\n" + QString::fromStdString(error));
        }
    }
    else if (item->data(1, Qt::UserRole).toString() == "STFS")
    {
        QString packagePath;
        GetPackagePath(item, &packagePath);

        try
        {
            // get a temporary file name
            string tempName = (QDir::tempPath() + "/" + QUuid::createUuid().toString().replace("{",
                    "").replace("}", "").replace("-", "")).toStdString();

            // extract the file to a temporary location
            package->ExtractFile(packagePath.toStdString(), tempName);

            StfsPackage pack(tempName);
            PackageViewer dialog(statusBar, &pack, gpdActions, gameActions, this, false);
            dialog.exec();

            pack.Close();

            // replace
            package->ReplaceFile(tempName, packagePath.toStdString());

            // delete the temporary file
            QFile::remove(QString::fromStdString(tempName));
        }
        catch(string error)
        {
            QMessageBox::critical(this, "Error",
                    "Failed to open Stfs Package.\n\n" + QString::fromStdString(error));
        }
    }
}

void PackageViewer::on_btnStfsTools_clicked()
{
    StfsToolsDialog dialog(package, this);
    dialog.exec();
}

void PackageViewer::on_btnShowAll_clicked()
{
    ui->txtSearch->setText("");
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
        QtHelpers::ShowAllItems(ui->treeWidget->topLevelItem(i));
}


