#include "svoddialog.h"
#include "ui_svoddialog.h"

#include <QCryptographicHash>

#include <XboxInternals/Stfs/XContentHeader.h>

static QByteArray FileDigest(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(&file);
    return hash.result();
}

SvodDialog::SvodDialog(SVOD *svod, QStatusBar *statusBar, QWidget *parent, bool disposeSvod) :
    QDialog(parent), ui(new Ui::SvodDialog), svod(svod), disposeSvod(disposeSvod),
    statusBar(statusBar)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    ui->setupUi(this);
    QtHelpers::GenAdjustWidgetAppearanceToOS(this);

    loadListing(nullptr, &svod->root);

    ui->treeWidget->header()->setDefaultSectionSize(75);
    ui->treeWidget->header()->resizeSection(0, 250);
    ui->treeWidget->header()->resizeSection(3, 20);

    ui->btnResign->setEnabled(svod->metadata->magic == CON);

    // setup the context menu
    ui->treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->treeWidget, SIGNAL(customContextMenuRequested(QPoint)), this,
            SLOT(showFileContextMenu(QPoint)));

    ui->lblDisplayName->setText(QString::fromStdWString(svod->metadata->displayName));
    ui->lblTitleID->setText(QString::number(svod->metadata->titleID, 16).toUpper());
    ui->lblSectorOffset->setText("0x" + QString::number(
                svod->metadata->svodVolumeDescriptor.dataBlockOffset * 2, 16).toUpper());
    ui->lblType->setText(QString::fromStdString(ContentTypeToString(svod->metadata->contentType)));

    if (!svod->metadata->thumbnailImage.empty())
    {
        QByteArray imageBuff(reinterpret_cast<const char*>(svod->metadata->thumbnailImage.data()),
                static_cast<int>(svod->metadata->thumbnailImage.size()));
        ui->imgThumbnail->setPixmap(QPixmap::fromImage(QImage::fromData(imageBuff)));
    }
    else
    {
        ui->imgThumbnail->setPixmap(QPixmap());
    }

    statusBar->showMessage("SVOD system parsed successfully", 3000);
}

SvodDialog::~SvodDialog()
{
    // Join a running extraction before `svod` is destroyed.
    cancelProgressDialogs(this);

    if (disposeSvod)
        delete svod;
    delete ui;
}

void SvodDialog::loadListing(QTreeWidgetItem *parent, vector<GdfxFileEntry> *files)
{
    u32 addr, index;

    for (u32 i = 0; i < files->size(); i++)
    {
        QTreeWidgetItem *item;
        if (parent == nullptr)
            item = new QTreeWidgetItem(ui->treeWidget);
        else
            item = new QTreeWidgetItem(parent);

        svod->SectorToAddress(files->at(i).sector, &addr, &index);


        QIcon icon;
        SvodIO io = svod->GetSvodIO(files->at(i));
        QtHelpers::GetFileIcon(io.ReadDword(), QString::fromStdString(files->at(i).name), icon, *item);

        item->setIcon(0, icon);
        item->setText(0, QString::fromStdString(files->at(i).name));
        item->setText(1, QString::fromStdString(ByteSizeToString(files->at(i).size)));
        item->setText(2, "0x" + QString::number(addr, 16).toUpper());
        item->setText(3, QString::number(index));
        item->setData(0, Qt::UserRole, QVariant::fromValue(&files->at(i)));

        if (files->at(i).attributes & GdfxDirectory)
        {
            item->setIcon(0, QIcon(":/Images/FolderFileIcon.png"));
            loadListing(item, &files->at(i).files);
        }
    }
}

void SvodDialog::showFileContextMenu(QPoint pos)
{
    if (ui->treeWidget->currentIndex().row() == -1)
        return;

    GdfxFileEntry *entry = ui->treeWidget->currentItem()->data(0, Qt::UserRole).value<GdfxFileEntry*>();

    if (entry->attributes & GdfxDirectory)
        return;

    QPoint globalPos = ui->treeWidget->mapToGlobal(pos);
    QMenu contextMenu;
    contextMenu.addAction(QIcon(":/Images/extract.png"), "Extract");
    if (ui->treeWidget->selectedItems().count() == 1)
    {
        QAction *replace = contextMenu.addAction(QIcon(":/Images/replace.png"), "Replace");
        replace->setEnabled(svod->metadata->magic == CON);
        contextMenu.addAction(QIcon(":/Images/properties.png"), "View Properties");
    }

    QAction *selectedItem = contextMenu.exec(globalPos);
    if(selectedItem == nullptr)
        return;

    if (selectedItem->text() == "View Properties")
    {
        SvodFileInfoDialog dialog(svod, entry, ui->treeWidget->currentItem()->data(1,
                Qt::UserRole).toString(), this);
        dialog.exec();
    }
    else if (selectedItem->text() == "Extract")
    {
        // get a directory to save the files to
        QString savePath = QFileDialog::getExistingDirectory(this, "Choose a place to save the file...",
                QtHelpers::DefaultLocation() + "/" + QString::fromStdString(entry->name));
        if (savePath == "")
            return;

        // get a list of all the files to extract
        QList<void*> list;
        foreach (QTreeWidgetItem *item, ui->treeWidget->selectedItems())
        {
            GdfxFileEntry *entry = item->data(0, Qt::UserRole).value<GdfxFileEntry*>();
            list.push_back(entry);
        }

        MultiProgressDialog *dialog = new MultiProgressDialog(OpExtract, FileSystemSVOD, svod,
                savePath + "/", list, this);
        dialog->setModal(true);
        dialog->show();
        dialog->start();
    }
    else if (selectedItem->text() == "Replace")
    {
        // open a file
        QString filePath = QFileDialog::getOpenFileName(this, "Choose a modifed version to repalce...",
                QtHelpers::DefaultLocation());
        if (filePath == "")
            return;

        SingleProgressDialog *dialog = new SingleProgressDialog(FileSystemSVOD, svod, OpReplace,
                QString::fromStdString(entry->filePath + entry->name), filePath, nullptr, this);
        dialog->setModal(true);
        dialog->show();
        dialog->start();
    }
}

void SvodDialog::on_btnViewAll_clicked()
{
    Metadata dialog(statusBar, svod->metadata, false, this);
    dialog.exec();
}

void SvodDialog::on_pushButton_clicked()
{
    SvodToolDialog dialog(svod, this);
    dialog.exec();
}

void SvodDialog::on_txtSearch_textChanged(const QString &arg1)
{
    QtHelpers::SearchTreeWidget(ui->treeWidget, ui->txtSearch, arg1);
}

void SvodDialog::on_btnShowAll_clicked()
{
    ui->txtSearch->setText("");
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
        QtHelpers::ShowAllItems(ui->treeWidget->topLevelItem(i));
}

void SvodDialog::on_pushButton_3_clicked()
{
    try
    {
        svod->Rehash(UpdateProgress, this);
        QMessageBox::information(this, "Success", "Successfully rehashed the system.");
    }
    catch (string error)
    {
        QMessageBox::critical(this, "Error",
                "An error occurred while rehashing the system.\n\n" + QString::fromStdString(error));
    }
}

void UpdateProgress(u32 cur, u32 total, void *arg)
{
    SvodDialog *dialog = reinterpret_cast<SvodDialog*>(arg);

    if (cur < total)
        dialog->statusBar->showMessage("Rehashing files " + QString::number(cur) + "/" + QString::number(
                    total));
    else
        dialog->statusBar->showMessage("Successfully rehashed the system", 3000);

    QApplication::processEvents();
}

void SvodDialog::on_treeWidget_itemDoubleClicked(QTreeWidgetItem *item, [[maybe_unused]] int column)
{
    GdfxFileEntry *entry = ui->treeWidget->currentItem()->data(0, Qt::UserRole).value<GdfxFileEntry*>();
    QString tempName = (QDir::tempPath() + "/" + QUuid::createUuid().toString().replace("{",
            "").replace("}", "").replace("-", ""));

    // the other file types that are supported have no buisness being in an SVOD system, so we'll just leave those out
    if (item->data(1, Qt::UserRole).toString() == "Image")
    {
        svod->GetSvodIO(*entry).SaveFile(tempName.toStdString());

        ImageDialog *dialog = new ImageDialog(QImage(tempName), item->text(0), this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();

        QFile::remove(tempName);
    }
    else if (item->data(1, Qt::UserRole).toString() == "STFS")
    {
        SvodIO io = svod->GetSvodIO(*entry);
        io.SaveFile(tempName.toStdString());
        const QByteArray before = FileDigest(tempName);

        try
        {
            {
                StfsPackage package(tempName.toStdString());

                PackageViewer viewer(statusBar, &package, QList<QAction*>(), QList<QAction*>(), this, false);
                viewer.exec();

                package.Close();
            }

            if (FileDigest(tempName) != before)
            {
                if (svod->metadata->magic != CON)
                {
                    QMessageBox::warning(this, "Changes not saved",
                            "This package is LIVE/PIRS signed. Velocity cannot recompute that "
                            "signature, so the changes were not written back.");
                }
                else
                {
                    io.OverWriteFile(tempName.toStdString());
                    svod->Rehash(UpdateProgress, this);
                    statusBar->showMessage("Saved and rehashed. Resign the package before using it "
                            "on a retail console.", 5000);
                }
            }
        }
        catch (string error)
        {
            QMessageBox::critical(this, "Error",
                    "An error occurred while editing the STFS package.\n\n" + QString::fromStdString(error));
        }

        QFile::remove(tempName);
    }
}

void SvodDialog::on_btnResign_clicked()
{
    QString kvPath = QFileDialog::getOpenFileName(this, "Choose a kv for resigning",
            QtHelpers::DefaultLocation());
    if (kvPath == "")
        return;

    // open kv
    u8 consoleID[5] = {};
    try
    {
        FileIO kvIO(kvPath.toStdString());
        kvIO.SetPosition(0, ios_base::end);

        const u64 kvSize = kvIO.GetPosition();
        u32 adder;
        try
        {
            adder = XContentHeader::KeyvaultOffsetShift(kvSize);
        }
        catch (const string &)
        {
            throw string("The selected file is not a keyvault "
                         "(expected 0x4000 or 0x3FF0 bytes).\n");
        }

        // read in the console id of the certificate in the kv
        kvIO.SetPosition(0x9BA + adder);
        kvIO.ReadBytes(consoleID, 5);
        kvIO.Close();
    }
    catch (const string &error)
    {
        QMessageBox::critical(this, "Invalid KeyVault", QString::fromStdString(error));
        return;
    }
    catch (const std::exception &error)
    {
        QMessageBox::critical(this, "Invalid KeyVault", QString(error.what()));
        return;
    }

    // make sure the console ids match
    if (memcmp(consoleID, svod->metadata->certificate.ownerConsoleID, 5) != 0)
    {
        QMessageBox::StandardButton btn = (QMessageBox::StandardButton)QMessageBox::question(this,
                "Continue?",
                "The KeyVault provided is not from the console where this SVOD system was signed. This system will show up as corrupt on the original console. Unless you know what you're doing, choose no.\n\nAre you sure that you want to contiure?",
                QMessageBox::No, QMessageBox::Yes);
        if (btn != QMessageBox::Yes)
            return;
    }

    try
    {
        svod->Resign(kvPath.toStdString());
        QMessageBox::information(this, "Success", "Successfuly resigned the system.");
    }
    catch (string error)
    {
        QMessageBox::critical(this, "Error",
                "An error occurred while resigning the system.\n\n" + QString::fromStdString(error));
    }
}


