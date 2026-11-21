#include "gamerpicturepackdialog.h"
#include "ui_gamerpicturepackdialog.h"

GamerPicturePackDialog::GamerPicturePackDialog(QStatusBar *statusBar, QWidget *parent) :
    QDialog(parent), ui(new Ui::GamerPicturePackDialog), statusBar(statusBar), isSearching(false),
    totalRequests(0), completedRequests(0), foundPictures(0), totalQueuedSearches(0), completedSearches(0),
    requestsSinceLastFind(0), searchTerminatedEarly(false)
{
    ui->setupUi(this);

    titleIDFinder = new TitleIdFinder("", this);
    connect(titleIDFinder, SIGNAL(SearchFinished(QList<TitleData>)), this,
            SLOT(onTitleIDSearchReturn(QList<TitleData>)));

    searchedIDs = new QList<QString>;
    addedIDs = new QList<QString>;
    searchedTitleIDs = new QList<u32>;

    ui->listSearch->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listSearch, SIGNAL(customContextMenuRequested(QPoint)), this,
            SLOT(showContextMenuSearch(QPoint)));

    ui->listPack->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listPack, SIGNAL(customContextMenuRequested(QPoint)), this,
            SLOT(showContextMenuPack(QPoint)));

    gpManager = new QNetworkAccessManager(this);

    gpManager->setAutoDeleteReplies(true);
    connect(gpManager, SIGNAL(finished(QNetworkReply*)), this,
            SLOT(gamerPictureDownloaded(QNetworkReply*)));

    connect(ui->txtSearch, SIGNAL(returnPressed()), this, SLOT(on_pushButton_clicked()));
}

GamerPicturePackDialog::~GamerPicturePackDialog()
{
    delete searchedIDs;
    delete addedIDs;
    delete searchedTitleIDs;

    statusBar->showMessage("");

    delete ui;
}

void GamerPicturePackDialog::on_pushButton_clicked()
{
    // make sure there's something entered
    if (ui->txtSearch->text().trimmed().isEmpty())
        return;

    QString searchText = ui->txtSearch->text().trimmed();

    // Check if it's a Title ID (8 hex digits)
    if (isValidTitleID(searchText))
    {
        // Search by Title ID
        bool ok;
        u32 titleID = searchText.toULong(&ok, 16);
        
        if (searchedTitleIDs->contains(titleID))
            return;

        currentSearchName = searchText.toUpper();
        searchedTitleIDs->push_back(titleID);
        findGamerPictures(searchText.toUpper());
    }
    else
    {
        // Search by Game Name
        statusBar->showMessage("Searching for '" + searchText + "'...", 3000);
        titleIDFinder->SetGameName(searchText);
        titleIDFinder->StartSearch();
    }
}

void GamerPicturePackDialog::onTitleIDSearchReturn(QList<TitleData> titlesFound)
{
    if (titlesFound.size() == 0)
        return;

    statusBar->showMessage("Search returned " + QString::number(titlesFound.size()) + " result(s)",
            3000);

    currentTitles = new QList<TitleData>(titlesFound);

    // clear the current names
    ui->listGameNames->clear();

    // display all the titles found in the widget
    for (int i = 0; i < titlesFound.size(); i++)
    {
        QString newStr = ((QString*)&titlesFound.at(i).titleName)->replace("&#174;", "®").replace("&#39;",
                "'").replace("&amp;","&").replace("&gt;",">").replace("&lt;","<").replace("â", "").replace("¢", "");
        QString titleID = QString("%1").arg(titlesFound.at(i).titleID, 8, 16, QChar('0')).toUpper();
        ui->listGameNames->addItem(newStr + " [" + titleID + "]");
    }
}

void GamerPicturePackDialog::on_listGameNames_itemClicked(QListWidgetItem *item)
{
    u32 titleID = currentTitles->at(ui->listGameNames->currentIndex().row()).titleID;
    
    // make sure that we haven't already searched for the title
    if (searchedTitleIDs->contains(titleID))
        return;
    
    searchedTitleIDs->push_back(titleID);

    // Add to search queue
    SearchRequest req;
    req.titleID = QString::number(titleID, 16).toUpper();
    req.displayName = item->text();
    searchQueue.append(req);
    
    // Start search if not already searching
    if (!isSearching)
    {
        totalQueuedSearches = searchQueue.size();
        completedSearches = 0;
        processNextSearch();
    }
    else
    {
        totalQueuedSearches++;
        statusBar->showMessage(QString("Added to queue (%1 pending)").arg(searchQueue.size()), 2000);
    }
}

void GamerPicturePackDialog::processNextSearch()
{
    if (searchQueue.isEmpty())
    {
        isSearching = false;
        ui->btnStopSearch->setEnabled(false);
        statusBar->showMessage("All searches complete", 3000);
        return;
    }
    
    SearchRequest req = searchQueue.takeFirst();
    completedSearches++;
    currentSearchTitleID = req.titleID;
    currentSearchName = req.displayName;
    
    isSearching = true;
    findGamerPictures(req.titleID);
}

void GamerPicturePackDialog::findGamerPictures(QString titleID)
{
    // Reset counters for new search
    totalRequests = 4096;  // 0x400 * 4 offsets
    completedRequests = 0;
    foundPictures = 0;
    requestsSinceLastFind = 0;
    searchTerminatedEarly = false;
    
    statusBar->showMessage(QString("Starting search for %1 (%2/%3)...")
        .arg(currentSearchName.split(" [")[0])
        .arg(completedSearches)
        .arg(totalQueuedSearches));
    
    ui->btnStopSearch->setEnabled(true);

    // offsets to search from
    const int searchOffsets[] = {0, 0x400, 0x1000, 0x8000};

    // reset the manager
    disconnect(gpManager, SIGNAL(finished(QNetworkReply*)), this,
               SLOT(gamerPictureDownloaded(QNetworkReply*)));
    delete gpManager;
    gpManager = new QNetworkAccessManager(this);

    gpManager->setAutoDeleteReplies(true);
    connect(gpManager, SIGNAL(finished(QNetworkReply*)), this,
            SLOT(gamerPictureDownloaded(QNetworkReply*)));

    // make a bunch of requests
    for (u32 i = 0; i < 0x400; i++)
    {
        // iterate through all the offsets
        for (int x = 0; x < 4; x++)
            gpManager->get(QNetworkRequest(QUrl("http://image.xboxlive.com/global/t." + titleID.toUpper() +
                    "/tile/0/2" + QString("%1").arg(searchOffsets[x] + i, 4, 16, QChar('0')).toUpper())));
    }
}

void GamerPicturePackDialog::gamerPictureDownloaded(QNetworkReply *reply)
{
    // If already terminated early, ignore remaining replies
    if (searchTerminatedEarly)
    {
        reply->deleteLater();
        return;
    }
    
    completedRequests++;
    
    if (reply->bytesAvailable() != 0)
    {
        // parse the image
        QImage image;
        image.loadFromData(reply->readAll(), "PNG");

        // make sure the image is valid
        if (!image.isNull())
        {
            foundPictures++;
            requestsSinceLastFind = 0;  // Reset counter when we find a picture
            
            QString pictureID = reply->url().toString().mid(reply->url().toString().indexOf("t.") + 2,
                    8) + "." + reply->url().toString().mid(reply->url().toString().length() - 4);

            // add the gamerpicture to the list widget
            QListWidgetItem *item = new QListWidgetItem(ui->listSearch);
            item->setIcon(QIcon(QPixmap::fromImage(image)));
            item->setToolTip("<b>" + currentSearchName + "</b><br />" + pictureID);
            ui->listSearch->addItem(item);

            searchedIDs->push_back(pictureID);

            reply->deleteLater();
            QCoreApplication::processEvents();
        }
    }
    
    // Only count requests after we've found at least one picture
    if (foundPictures > 0)
        requestsSinceLastFind++;
    
    // Early termination: stop if no results in last 500 requests (only after finding at least one)
    if (foundPictures > 0 && requestsSinceLastFind >= 500 && !searchTerminatedEarly)
    {
        searchTerminatedEarly = true;
        
        statusBar->showMessage(QString("Search stopped early for %1: found %2 gamerpicture%3 (no new results after 500 requests)")
            .arg(currentSearchName.split(" [")[0])
            .arg(foundPictures)
            .arg(foundPictures == 1 ? "" : "s"), 5000);
        
        // Move to next search immediately
        QTimer::singleShot(0, this, &GamerPicturePackDialog::processNextSearch);
        return;
    }
    
    // Check if search is complete (either all requests done OR terminated early and requests finished)
    if (completedRequests >= totalRequests || (searchTerminatedEarly && completedRequests >= totalRequests))
    {
        // Search complete - only show completion message if we haven't already shown early termination message
        if (!searchTerminatedEarly)
        {
            statusBar->showMessage(QString("Completed %1: found %2 gamerpicture%3")
                .arg(currentSearchName.split(" [")[0])
                .arg(foundPictures)
                .arg(foundPictures == 1 ? "" : "s"), 3000);
        }
        
        // Process next in queue after returning from this slot
        QTimer::singleShot(0, this, &GamerPicturePackDialog::processNextSearch);
        return;
    }
    
    // Update progress every 100 requests (skip if terminated early - already showed message)
    if (!searchTerminatedEarly && completedRequests % 100 == 0)
    {
        int percentage = (completedRequests * 100) / totalRequests;
        
        // Show progress
        statusBar->showMessage(QString("Searching %1 (%2/%3)... %4% (%5 found)")
            .arg(currentSearchName.split(" [")[0])
            .arg(completedSearches)
            .arg(totalQueuedSearches)
            .arg(percentage)
            .arg(foundPictures));
    }
}

void GamerPicturePackDialog::on_listSearch_itemDoubleClicked(QListWidgetItem *item)
{
    switchImage(item, ui->listSearch, ui->listPack, searchedIDs, addedIDs);
}

void GamerPicturePackDialog::on_listPack_itemDoubleClicked(QListWidgetItem *item)
{
    switchImage(item, ui->listPack, ui->listSearch, addedIDs, searchedIDs);
}

void GamerPicturePackDialog::switchImage(QListWidgetItem *currentItem, QListWidget *current,
        QListWidget *toAddTo, QList<QString> *currentStrs, QList<QString> *toAddToStrs)
{
    if (toAddToStrs->contains(currentStrs->at(current->row(currentItem))) && current == ui->listSearch)
    {
        statusBar->showMessage("Image already exists in pack", 3000);
        return;
    }
    else if (toAddToStrs->contains(currentStrs->at(current->row(currentItem))))
    {
        // delete only
        currentStrs->removeAt(current->row(currentItem));
        delete currentItem;
        return;
    }

    // create a new list item
    QListWidgetItem *newItem = new QListWidgetItem(*currentItem);
    toAddTo->addItem(newItem);

    // move the ID to the pack list
    toAddToStrs->push_back(currentStrs->at(current->row(currentItem)));
    currentStrs->removeAt(current->row(currentItem));

    // delete the old item
    delete currentItem;

    ui->btnCreatePack->setEnabled(ui->listPack->count() != 0 && ui->txtPackName->text() != "");
}

void GamerPicturePackDialog::showContextMenuSearch(QPoint p)
{
    showContextMenu(p, ui->listSearch, ui->listPack, searchedIDs, addedIDs);
}

void GamerPicturePackDialog::showContextMenuPack(QPoint p)
{
    showContextMenu(p, ui->listPack, ui->listSearch, addedIDs, searchedIDs);
}

void GamerPicturePackDialog::showContextMenu(QPoint p, QListWidget *current, QListWidget *toAddTo,
        QList<QString> *currentStrs, QList<QString> *toAddToStrs)
{
    QPoint globalPos = current->mapToGlobal(p);
    QMenu contextMenu;

    bool forward = (current == ui->listSearch);

    contextMenu.addAction(QPixmap(":/Images/undo.png"), "Clear All");
    contextMenu.addAction((forward) ? "Add All to Pack" : "Remove All from Pack");

    if (current->selectedItems().count() > 0)
    {
        if (forward)
            contextMenu.addAction(QPixmap(":/Images/add.png"), "Add to Pack");
        else
            contextMenu.addAction(QPixmap(":/Images/delete.png"), "Remove from Pack");
        contextMenu.addAction(QPixmap(":/Images/save.png"), "Save Image");
    }

    QAction *selectedItem = contextMenu.exec(globalPos);
    if (selectedItem == nullptr)
        return;

    if (selectedItem->text() == ((forward) ? "Add to Pack" : "Remove from Pack"))
        switchImage(current->currentItem(), current, toAddTo, currentStrs, toAddToStrs);
    else if (selectedItem->text() == "Save Image")
    {
        QString savePath = QFileDialog::getSaveFileName(this, "Choose a place to save the gamerpicture",
                QtHelpers::DefaultLocation() + getImageName(currentStrs->at(current->currentIndex().row()),
                        true) + ".png", "*.png");

        if (savePath == "")
            return;

        current->currentItem()->icon().pixmap(64, 64).save(savePath);

        statusBar->showMessage("Successfully saved gamerpicture", 3000);
    }
    else if (selectedItem->text() == ((forward) ? "Add All to Pack" : "Remove All from Pack"))
    {
        u32 itemCount = current->count();
        for (u32 i = 0; i < itemCount; i++)
            switchImage(current->item(0), current, toAddTo, currentStrs, toAddToStrs);

        ui->btnCreatePack->setEnabled(ui->listPack->count() != 0 && ui->txtPackName->text() != "");
    }
    else if (selectedItem->text() == "Clear All")
    {
        current->clear();
        currentStrs->clear();
        
        // If clearing search results, also clear the searched title IDs list
        // This allows users to re-search for the same titles
        if (current == ui->listSearch)
            searchedTitleIDs->clear();
        
        ui->btnCreatePack->setEnabled(ui->listPack->count() != 0 && ui->txtPackName->text() != "");
    }
}

QString GamerPicturePackDialog::getImageName(QString id, bool big)
{
    return ((big) ? "64_" : "32_") + id.mid(0, 8) + "0002" + id.mid(8) + "0001" + id.mid(8);
}

void GamerPicturePackDialog::on_txtPackName_textChanged(const QString &arg1)
{
    ui->btnCreatePack->setEnabled(arg1 != "" && ui->listPack->count() != 0);
}

void GamerPicturePackDialog::on_btnCreatePack_clicked()
{
    QString savePath = QFileDialog::getSaveFileName(this, "Choose a place to save your picture pack",
            QtHelpers::DefaultLocation() + "/" + ui->txtPackName->text());
    if (savePath == "")
        return;

    try
    {
        StfsPackage picturePack(savePath.toStdString(), StfsPackageCreate);

        picturePack.metaData->contentType = GamerPicture;
        picturePack.metaData->displayName = ui->txtPackName->text().toStdWString();
        picturePack.metaData->titleID = 0xFFFE07D1;
        picturePack.metaData->transferFlags = 0x40;

        // set thumbnail image
        QByteArray ba1;
        QBuffer buffer1(&ba1);
        buffer1.open(QIODevice::WriteOnly);
        QPixmap(":/Images/20001.png").save(&buffer1, "PNG");
    const auto *thumbStart = reinterpret_cast<const u8*>(ba1.constData());
    picturePack.metaData->thumbnailImage.assign(thumbStart, thumbStart + ba1.size());
    picturePack.metaData->thumbnailImageSize = static_cast<u32>(picturePack.metaData->thumbnailImage.size());

        // set title thumbnail image
        QByteArray ba2;
        QBuffer buffer2(&ba2);
        buffer2.open(QIODevice::WriteOnly);
        QPixmap(":/Images/defaultTitleImage.png").save(&buffer2, "PNG");
    const auto *titleThumbStart = reinterpret_cast<const u8*>(ba2.constData());
    picturePack.metaData->titleThumbnailImage.assign(titleThumbStart, titleThumbStart + ba2.size());
    picturePack.metaData->titleThumbnailImageSize = static_cast<u32>(picturePack.metaData->titleThumbnailImage.size());

        statusBar->showMessage("Creating picture pack, %0 complete");
        ui->tabWidget->setEnabled(false);
        ui->btnCreatePack->setEnabled(false);
        ui->txtPackName->setEnabled(false);
        ui->txtSearch->setEnabled(false);
        ui->btnStopSearch->setEnabled(false);
        ui->pushButton->setEnabled(false);
        ui->listGameNames->setEnabled(false);

        // add all the gamerpictures to the pacakge
        for (int i = 0; i < ui->listPack->count(); i++)
        {
            // inject the 64x64 image
            QByteArray largeImage;
            QBuffer buffLrg(&largeImage);
            buffLrg.open(QIODevice::WriteOnly);
            ui->listPack->item(i)->icon().pixmap(64, 64).save(&buffLrg, "PNG");
            picturePack.InjectData((u8*)largeImage.data(), largeImage.length(), getImageName(addedIDs->at(i),
                    true).toStdString() + ".png");

            QByteArray smallImage;
            QBuffer buffSm(&smallImage);
            buffSm.open(QIODevice::WriteOnly);
            ui->listPack->item(i)->icon().pixmap(32, 32).save(&buffSm, "PNG");
            picturePack.InjectData((u8*)smallImage.data(), smallImage.length(), getImageName(addedIDs->at(i),
                    false).toStdString() + ".png");

            statusBar->showMessage("Creating picture pack, " + QString::number(((float)i /
                    (float)ui->listPack->count()) * 100, 'f', 0) + "% complete");
            QApplication::processEvents();
        }

        // fix the package
        picturePack.RebuildHashes();
        QtHelpers::ResignWithKV(Retail, this,
                [&picturePack](const std::string &kv) { picturePack.Resign(kv); });

        statusBar->showMessage("Successfully created your picture pack", 3000);
        ui->tabWidget->setEnabled(true);
        ui->btnCreatePack->setEnabled(true);
        ui->txtPackName->setEnabled(true);
        ui->txtSearch->setEnabled(true);
        ui->btnStopSearch->setEnabled(true);
        ui->listGameNames->setEnabled(true);
        ui->pushButton->setEnabled(true);
        QMessageBox::information(this, "Success", "Successfully created your picture pack.");
    }
    catch (string error)
    {
        QMessageBox::critical(this, "Error",
                "An error occurred while creating your picture pack.\n\n" + QString::fromStdString(error));
    }
}

void GamerPicturePackDialog::on_txtSearch_textChanged(const QString &arg1)
{
    QString trimmed = arg1.trimmed();
    
    if (trimmed.isEmpty())
    {
        QtHelpers::ClearErrorStyle(ui->txtSearch);
        ui->pushButton->setEnabled(false);
        return;
    }
    
    // Always enable search button - let the search logic decide what to do
    QtHelpers::ClearErrorStyle(ui->txtSearch);
    ui->pushButton->setEnabled(true);
}

bool GamerPicturePackDialog::isValidTitleID(const QString &text)
{
    if (text.length() != 8)
        return false;
    
    return QtHelpers::VerifyHexString(text);
}

void GamerPicturePackDialog::on_btnStopSearch_clicked()
{
    // Cancel current search
    disconnect(gpManager, SIGNAL(finished(QNetworkReply*)), this,
               SLOT(gamerPictureDownloaded(QNetworkReply*)));
    delete gpManager;
    gpManager = new QNetworkAccessManager(this);

    gpManager->setAutoDeleteReplies(true);
    connect(gpManager, SIGNAL(finished(QNetworkReply*)), this,
            SLOT(gamerPictureDownloaded(QNetworkReply*)));
    QApplication::processEvents();

    // Clear entire queue
    int queuedCount = searchQueue.size();
    searchQueue.clear();
    isSearching = false;
    
    if (queuedCount > 0)
        statusBar->showMessage(QString("Search stopped, %1 queued search%2 cancelled").arg(queuedCount).arg(queuedCount == 1 ? "" : "es"), 3000);
    else
        statusBar->showMessage("Search stopped", 3000);
    
    ui->btnStopSearch->setEnabled(false);
}


