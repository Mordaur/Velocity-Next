#include "xexdialog.h"
#include "ui_xexdialog.h"

#include <XboxInternals/Stfs/StfsDefinitions.h>
#include <XboxInternals/Utils.h>

XexDialog::XexDialog(Xex *xex, QWidget *parent, BaseIO *ownedIo) :
    QDialog(parent), ui(new Ui::XexDialog), xex(xex), ownedIo(ownedIo)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    ui->setupUi(this);

    try {
        setWindowTitle("XEX Dialog [" + QString::fromStdString(xex->GetOriginalPEImageName()) + "]");
    } catch (...) {
        setWindowTitle("XEX Dialog [Unknown]");
    }

    try {
        // load the system import libraries
        std::vector<std::string> systemImportLibraries = xex->GetSystemImportLibraries();
        for (size_t i = 0; i < systemImportLibraries.size(); i++)
        {
            QString systemImportLibraryName = QString::fromStdString(systemImportLibraries.at(i));
            ui->lstSystemImportLibraries->addItem(systemImportLibraryName);
        }
    } catch (...) {
        // Ignore if system import libraries fail
    }

    try {
        // load the module flags
        u32 moduleFlags = xex->GetModuleFlags();
        ui->chkTitleModule->setChecked(!!(moduleFlags & ModuleTitle));
        ui->chkDeltaPatch->setChecked(!!(moduleFlags & ModulePatchDelta));
        ui->chkDLL->setChecked(!!(moduleFlags & ModuleDLL));
        ui->chkExportsToTitle->setChecked(!!(moduleFlags & ModuleExportsToTitle));
        ui->chkFullPatch->setChecked(!!(moduleFlags & ModulePatchFull));
        ui->chkSystemDebugger->setChecked(!!(moduleFlags & ModuleSystemDebugger));
        ui->chkUserMode->setChecked(!!(moduleFlags & ModuleUserMode));
    } catch (...) {
        // Ignore if module flags fail
    }

    // Wrap all remaining initialization in try-catch to prevent crashes
    try {
    QString esrbRatingIconPath = ":/Images/esrb_rating_" + QString::fromStdString(xex->GetEsrbRatingText()) + ".png";
    ui->imgEsrbRating->setPixmap(QPixmap(esrbRatingIconPath));

    QString pegiRatingIconPath;
    if (xex->GetPegiRating() == PEGI_Unrated)
        pegiRatingIconPath = ":/Images/HiddenAchievement.png";
    else
        pegiRatingIconPath = ":/Images/pegi_" + QString::fromStdString(xex->GetPegiRatingText()) + ".jpg";
    ui->imgPegiRating->setPixmap(QPixmap(pegiRatingIconPath));

    QString oflcAuRating;
    if (xex->GetOflcAURating() == OFLCAU_UNRATED)
        pegiRatingIconPath = ":/Images/HiddenAchievement.png";
    else
        oflcAuRating = ":/Images/rating_oflc_au_" + QString::fromStdString(xex->GetOflcAURatingText()) + ".png";
    ui->imgOflcAU->setPixmap(QPixmap(oflcAuRating));

    QString oflcNzRating;
    if (xex->GetOflcNZRating() == OFLCNZ_UNRATED)
        oflcNzRating = ":/Images/HiddenAchievement.png";
    else
        oflcNzRating = ":/Images/rating_oflc_nz_" + QString::fromStdString(xex->GetOflcNZRatingText()) + ".png";
    ui->imgOflcNZ->setPixmap(QPixmap(oflcNzRating));


    // load the static libraries
    std::vector<XexStaticLibraryInfo> staticLibs = xex->GetStaticLibraries();
    for (size_t i = 0; i < staticLibs.size(); i++)
    {
        XexStaticLibraryInfo staticLib = staticLibs.at(i);

        QTreeWidgetItem *staticLibItem = new QTreeWidgetItem(ui->treStaticLibraries);
        staticLibItem->setText(0, QString::fromStdString(staticLib.name));
        staticLibItem->setText(1, QString::fromStdString(Utils::VersionToString(staticLib.version)));
    }

    // load resources
    std::vector<XexResourceFileEntry> resources = xex->GetResourceFileInfo();
    for (size_t i = 0; i < resources.size(); i++)
    {
        XexResourceFileEntry resourceEntry = resources.at(i);

        QTreeWidgetItem *resourceItem = new QTreeWidgetItem(ui->treResources);
        resourceItem->setText(0, QString::fromStdString(resourceEntry.name));
        resourceItem->setText(1, QString::fromStdString(ByteSizeToString(resourceEntry.size)));
    }

    // load executable information
    AddExecutableProperty("Image Base Address", xex->GetImageBaseAddress());
    AddExecutableProperty("EntryPoint", xex->GetEntryPoint());
    AddExecutableProperty("Original Base Address", xex->GetOriginalBaseAddress());
    AddExecutableProperty("Default Stack Size", xex->GetDefaultStackSize());
    AddExecutableProperty("Default File System Cache Size", xex->GetDefaultFileSystemCacheSize());
    AddExecutableProperty("Default Heap Size", xex->GetDefaultHeapSize());
    AddExecutableProperty("Title Workspace Size", xex->GetTitleWorkspaceSize());

    // load game regions
    ui->chkNorthAmerica->setChecked(xex->HasRegion(XexRegionNorthAmerica));
    ui->chkJapan->setChecked(xex->HasRegion(XexRegionJapan));
    ui->chkChina->setChecked(xex->HasRegion(XexRegionChina));
    ui->chkRestOfAsia->setChecked(xex->HasRegion(XexRegionRestOfAsia));
    ui->chkAustraliaNewZealand->setChecked(xex->HasRegion(XexRegionAustraliaNewZealand));
    ui->chkRestOfEurope->setChecked(xex->HasRegion(XexRegionRestOfEurope));
    ui->chkRestOfWorld->setChecked(xex->HasRegion(XexRegionRestOfWorld));

    // load the security info
    ui->lblImageSize->setText("0x" + QString::fromStdString(Utils::ConvertToHexString(xex->GetImageSize())));
    ui->lblLoadAddress->setText("0x" + QString::fromStdString(Utils::ConvertToHexString(xex->GetImageSize())));
    ui->lblMediaID->setText(QString::fromStdString(Utils::ConvertToHexString(xex->GetMediaID(), 0x10)));
    ui->lblKey->setText(QString::fromStdString(Utils::ConvertToHexString(xex->GetKey(), XEX_AES_BLOCK_SIZE)));
    ui->lblLANKey->setText(QString::fromStdString(Utils::ConvertToHexString(xex->GetLANKey(), 0x10)));

    // load the allowed media types
    u32 allowedMediaTypes = xex->GetAllowedMediaTypes();
    ui->chkHardDisk->setChecked(!!(allowedMediaTypes & HardDisk));
    ui->chkDVDX2->setChecked(!!(allowedMediaTypes & DVDX2));
    ui->chkDVDCD->setChecked(!!(allowedMediaTypes & DVDCD));
    ui->chkDVD5->setChecked(!!(allowedMediaTypes & DVD5));
    ui->chkDVD9->setChecked(!!(allowedMediaTypes & DVD9));
    ui->chkSystemFlash->setChecked(!!(allowedMediaTypes & SystemFlash));
    ui->chkMemoryUnit->setChecked(!!(allowedMediaTypes & MemoryUnit));
    ui->chkMassStorageDevice->setChecked(!!(allowedMediaTypes & MassStorageDevice));
    ui->chkSMBFileSystem->setChecked(!!(allowedMediaTypes & SMBFileSystem));
    ui->chkDirectFromRAM->setChecked(!!(allowedMediaTypes & DirectFromRAM));
    ui->chkSecureOpticalDevice->setChecked(!!(allowedMediaTypes & SecureVirtualOpticalDevice));
    ui->chkWirelessNStorageDevice->setChecked(!!(allowedMediaTypes & WirelessNStorageDevice));
    ui->chkSystemExtendedPartition->setChecked(!!(allowedMediaTypes & SystemExtendedPartition));
    ui->chkSystemAuxillaryPartition->setChecked(!!(allowedMediaTypes & SystemAuxillaryPartition));
    ui->chkInsecurePackage->setChecked(!!(allowedMediaTypes & InsecurePackage));
    ui->chkSaveGamePackage->setChecked(!!(allowedMediaTypes & SaveGamePackage));
    ui->chkLocallySignedPackage->setChecked(!!(allowedMediaTypes & LocallySignedPackage));
    ui->chkLiveSignedPackage->setChecked(!!(allowedMediaTypes & LiveSignedPackage));
    ui->chkXboxPlatformPackage->setChecked(!!(allowedMediaTypes & XboxPlatformPackage));

    // load the image flags
    u32 imageFlags = xex->GetImageFlags();
    ui->chkXgd2MediaOnly->setChecked(!!(imageFlags & XGD2MediaOnly));
    ui->chkCardeaKey->setChecked(!!(imageFlags & CardeaKey));
    ui->chkXeikaKey->setChecked(!!(imageFlags & XeikaKey));
    ui->chkTitleUserMode->setChecked(!!(imageFlags & TitleUserMode));
    ui->chkSystemUserMode->setChecked(!!(imageFlags & SystemUserMode));
    ui->chkOrange0->setChecked(!!(imageFlags & Orange0));
    ui->chkOrange1->setChecked(!!(imageFlags & Orange1));
    ui->chkOrange2->setChecked(!!(imageFlags & Orange2));
    ui->chkIptvSignupApplication->setChecked(!!(imageFlags & IPTVSignupApplication));
    ui->chkIptvTitleApplication->setChecked(!!(imageFlags & IPTVTitleApplication));
    ui->chkKeyvaultPrivilegesRequired->setChecked(!!(imageFlags & KeyvaultPrivilegesRequired));
    ui->chkOnlineActivationRequired->setChecked(!!(imageFlags & OnlineActivationRequired));
    ui->chkPageSize4KB->setChecked(!!(imageFlags & PageSize4KB));
    ui->chkNoGameRegion->setChecked(!!(imageFlags & NoGameRegion));
    ui->chkRevocationCheckOptional->setChecked(!!(imageFlags & RevocationCheckOptional));
    ui->chkRevocationCheckRequired->setChecked(!!(imageFlags & RevocationCheckRequired));

    // load the box art
    boxArtRetriever = new BoxArtRetriever(this);
    connect(boxArtRetriever, SIGNAL(largeBoxArtRetrieved(QPixmap)), this, SLOT(onLargeBoxArtRetrieved(QPixmap)));

    u32 titleID = xex->GetTitleID();
    boxArtRetriever->FetchBoxArt(titleID, BoxArtLarge);

    // load the state
    ui->lblEncrypted->setText(QString::fromStdString(xex->GetEncryptionStateStr()));
    ui->lblCompressed->setText(QString::fromStdString(xex->GetCompressionStateStr()));

    // set up the context menu for the resources
    ui->treResources->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->treResources, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
    
    } catch (const std::exception& e) {
        qDebug() << "Exception in XexDialog initialization:" << e.what();
        // Continue anyway - dialog can still display partial data
    } catch (...) {
        qDebug() << "Unknown exception in XexDialog initialization";
        // Continue anyway
    }
}

XexDialog::~XexDialog()
{
    delete xex;
    delete ownedIo;
    delete ui;
}

void XexDialog::AddExecutableProperty(QString name, u32 value)
{
    if (value != 0)
        AddExecutableProperty(name, "0x" + QString::fromStdString(Utils::ConvertToHexString(value)));
}

void XexDialog::AddExecutableProperty(QString name, QString value)
{
    QTreeWidgetItem *property = new QTreeWidgetItem(ui->treExecutableInfo);
    property->setText(0, name);
    property->setText(1, value);
}

void XexDialog::on_pushButton_clicked()
{
    QString suggestedBaseFilePath = QtHelpers::DefaultLocation() + "/" + QString::fromStdString(xex->GetOriginalPEImageName());
    QString outBaseFilePath = QFileDialog::getSaveFileName(this, "Save the base file", suggestedBaseFilePath);

    if (outBaseFilePath.isEmpty())
        return;

    try
    {
        xex->ExtractBaseImageFile(outBaseFilePath.toStdString());
    }
    catch (std::string error)
    {
        QMessageBox::critical(this, "Error Extracting Base File", "An error occurred while extracting the base file.\n\n" +
                              QString::fromStdString(error));
    }
}

void XexDialog::onLargeBoxArtRetrieved(QPixmap boxArt)
{
    if (!boxArt.isNull())
        ui->imgBoxArt->setPixmap(boxArt);
}

void XexDialog::showContextMenu(QPoint point)
{
    QPoint globalPos = ui->treResources->mapToGlobal(point);
    QMenu contextMenu;

    if (ui->treResources->selectedItems().size() == 1)
        contextMenu.addAction(QPixmap(":/Images/extract.png"), "Extract");

    QAction *selectedItem = contextMenu.exec(globalPos);
    if (selectedItem == NULL)
        return;

    if (selectedItem->text() == "Extract")
    {
        QString fileName = ui->treResources->selectedItems().at(0)->text(0);
        QString outPath = QFileDialog::getSaveFileName(this, "Choose a place to extract the file to", QtHelpers::DefaultLocation() + "/" + fileName);
        if (!outPath.isEmpty())
        {
            try
            {
                xex->ExtractResource(fileName.toStdString(), outPath.toStdString());
            }
            catch (std::string error)
            {
                QMessageBox::critical(this, "Extracting Error", "An error occurred while extracted a file.\n\n" +
                                      QString::fromStdString(error));
            }
        }
    }
}
