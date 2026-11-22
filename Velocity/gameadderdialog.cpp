#include "gameadderdialog.h"
#include "ui_gameadderdialog.h"
#include <QSettings>
#include <XboxInternals/Gpd/GameGpd.h>

#include <algorithm>
#include <utility>

GameAdderDialog::GameAdderDialog(StfsPackage *package, QWidget *parent, bool dispose, bool *ok)
    : QDialog(parent), ui(std::make_unique<Ui::GameAdderDialog>()), package(package), dispose(dispose) {

    // Set up the dialog window
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    ui->setupUi(this);

    // Configure the font for the game name label
    QFont font = ui->lblGameName->font();
#ifdef _WIN32
    font.setPointSize(10);
#endif
    ui->lblGameName->setFont(font);

    // Initialize member variables
    allowInjection = false;
    pecPackage = nullptr;

    // Set up the headers of the tree widgets
    ui->treeWidgetAllGames->header()->resizeSection(0, 182);
    ui->treeWidgetQueue->header()->resizeSection(0, 182);

    // Check if the temporary directory is valid and writable
    QDir tempDir(QDir::tempPath());
    QFileInfo tempDirInfo(tempDir.absolutePath());
    if (!tempDir.exists() || !tempDirInfo.isWritable()) {
        QMessageBox::critical(this, "Path Error", "The temporary path is not valid or not writable.");
        close();
        return;
    }

    // Extract and initialize the dashboard GPD
    try {
        dashGpdTempPath = QDir::cleanPath(tempDir.path() + "/" + QUuid::createUuid().toString().remove('{').remove('}').remove('-'));
        package->ExtractFile("FFFE07D1.gpd", dashGpdTempPath.toStdString());
        dashGpd = std::make_unique<DashboardGpd>(dashGpdTempPath.toStdString());
    } catch (const std::string &error) {
        QMessageBox::critical(this, "File Error", "The dashboard Gpd could not be extracted/opened.\n\n" + QString::fromStdString(error));
        close();
        return;
    }
    catch (const std::exception &exception)
    {
        QtHelpers::ShowUnexpectedError(this, exception);
    }

    // Generate and clean the PEC temporary path
    pecTempPath = QDir::cleanPath(tempDir.path() + "/" + QUuid::createUuid().toString().remove('{').remove('}').remove('-'));

    // Initialize the network manager
    manager = std::make_unique<QNetworkAccessManager>(this);

    // Check for network reachability
    if (QNetworkInformation::instance()->reachability() != QNetworkInformation::Reachability::Online) {
        QMessageBox::warning(this, "Listing Error", "The listing could not be parsed. Try again later, as the servers may be down and make sure Velocity has access to the internet.");
        *ok = false;
        return;
    }

    // Connect network signals and initialize network operations
    connect(manager.get(), &QNetworkAccessManager::finished, this, &GameAdderDialog::gameReplyFinished);
    getListing();

    // Set up the context menus for the tree widgets
    ui->treeWidgetAllGames->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->treeWidgetQueue->setContextMenuPolicy(Qt::CustomContextMenu);

    // Set up the image manager for handling thumbnails
    imageManager = std::make_unique<QNetworkAccessManager>(this);
    connect(imageManager.get(), &QNetworkAccessManager::finished, this, &GameAdderDialog::thumbnailReplyFinished);

    // Set up the thumbnail injection manager for async thumbnail downloads during GPD injection
    thumbnailInjectionManager = std::make_unique<QNetworkAccessManager>(this);
    connect(thumbnailInjectionManager.get(), &QNetworkAccessManager::finished, this, &GameAdderDialog::gpdThumbnailDownloadFinished);

    // Connect custom context menu signals to their respective slots
    connect(ui->treeWidgetAllGames, &QWidget::customContextMenuRequested, this, &GameAdderDialog::showRemoveContextMenu_AllGames);
    connect(ui->treeWidgetQueue, &QWidget::customContextMenuRequested, this, &GameAdderDialog::showRemoveContextMenu_QueuedGames);

    *ok = true;
}

GameAdderDialog::~GameAdderDialog()
{
    cancelOperation();

    for (const PendingGpdInjection &pending : std::as_const(pendingInjections))
        removeStagedFiles(pending);
    pendingInjections.clear();

    // Close before removing: Windows cannot delete a file that is still open.
    if (dashGpd)
    {
        try { dashGpd->Close(); } catch (...) {}
        dashGpd.reset();
    }
    if (pecPackage)
    {
        try { pecPackage->Close(); } catch (...) {}
        pecPackage.reset();
    }
    if (!dashGpdTempPath.isEmpty())
        QFile::remove(dashGpdTempPath);
    if (!pecTempPath.isEmpty())
        QFile::remove(pecTempPath);

    if (dispose && package)
    {
        try { package->Close(); } catch (...) {}
        delete package;
        package = nullptr;
    }
}

void GameAdderDialog::gameReplyFinished(QNetworkReply *aReply) {
    // Check for network errors
    if (aReply->error() != QNetworkReply::NoError) {
        QMessageBox::critical(this, "Network Error",
                              "Failed to fetch game data. Error: " + aReply->errorString());
        aReply->deleteLater();
        return;
    }

    // Read the JSON data from the reply
    QString jsonStr = aReply->readAll();

    // Get the games played from dashGpd
    std::vector<TitleEntry> gamesPlayed = dashGpd->gamesPlayed;

    // Parse the JSON data using Qt's built-in JSON functionality
    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        ui->tabWidget->setEnabled(false);
        QMessageBox::warning(this, "Listing Error",
                             "The listing could not be parsed. Try again later, as the servers may be down and make sure Velocity has access to the internet.");
        return;
    }

    // Extract the main JSON object and the "games" array
    QJsonObject jsonObj = jsonDoc.object();
    QJsonArray gamesArray = jsonObj.value("games").toArray();

    // Iterate through each game in the "games" array
    for (const QJsonValue &gameValue : gamesArray) {
        QJsonObject gameMap = gameValue.toObject();
        QString gameName = gameMap.value("nm").toString();

        // Extract and convert other fields, ensuring we use correct conversions
        u32 titleId = gameMap.value("tid").toString().toULong(nullptr, 16);

        bool alreadyExists = false;
        for (size_t i = 0; i < gamesPlayed.size(); ++i) {
            if (gamesPlayed[i].titleID == titleId) {
                gamesPlayed.erase(gamesPlayed.begin() + i);
                alreadyExists = true;
                break;
            }
        }

        if (alreadyExists) continue;

        // Correctly convert all fields to ensure they are populated
        u32 achievementCount = static_cast<u32>(gameMap.value("achc").toInt());
        u32 totalGamerscore = static_cast<u32>(gameMap.value("ttlgs").toInt());
        u8 totalAwardCount = static_cast<u8>(gameMap.value("ttlac").toInt());
        u8 maleAwardCount = static_cast<u8>(gameMap.value("ttlmac").toInt());
        u8 femaleAwardCount = static_cast<u8>(gameMap.value("ttlfac").toInt());

        // Create a new item for the tree widget
        QTreeWidgetItem *item = new QTreeWidgetItem(ui->treeWidgetAllGames);
        item->setText(0, gameName.trimmed());
        item->setText(1, QString::number(totalGamerscore));
        item->setText(2, QString::number(totalAwardCount));

        // Populate the TitleEntry structure
        TitleEntry entry = {
            .entry = {
                .type = Undefined,
                .id = 0,
                .addressSpecifier = 0,
                .length = 0
            },
            .initialLength = 0,
            .titleID = titleId,
            .achievementCount = achievementCount,
            .achievementsUnlocked = 0,
            .totalGamerscore = totalGamerscore,
            .gamerscoreUnlocked = 0,
            .achievementsUnlockedOnline = 0,
            .avatarAwardsEarned = 0,
            .avatarAwardCount = totalAwardCount,
            .maleAvatarAwardsEarned = 0,
            .maleAvatarAwardCount = maleAwardCount,
            .femaleAvatarAwardsEarned = 0,
            .femaleAvatarAwardCount = femaleAwardCount,
            .flags = 0,
            .lastPlayed = QDateTime::currentSecsSinceEpoch(),
            .gameName = gameName.toStdWString()
        };

        item->setData(0, Qt::UserRole, QVariant::fromValue(entry));
        ui->treeWidgetAllGames->addTopLevelItem(item);
    }

    // Set the first item as selected and sort the list alphabetically
    if (ui->treeWidgetAllGames->topLevelItemCount() > 0) {
        ui->treeWidgetAllGames->setCurrentItem(ui->treeWidgetAllGames->topLevelItem(0));
        ui->treeWidgetAllGames->topLevelItem(0)->setSelected(true);
    }
    ui->treeWidgetAllGames->sortByColumn(0, Qt::AscendingOrder);
    ui->tabWidget->setEnabled(true);

    aReply->deleteLater();
}




void GameAdderDialog::thumbnailReplyFinished(QNetworkReply *aReply) {
    // Check for network errors
    if (aReply->error() != QNetworkReply::NoError) {
        ui->imgThumbnail->setText("<i>Unable to download image. Error: " + aReply->errorString() + "</i>");
        aReply->deleteLater();
        return;
    }

    int count = ui->treeWidgetAllGames->selectedItems().count();
    if (count != 1) {
        ui->imgThumbnail->setPixmap(QPixmap(":/Images/watermark.png"));
    } else {
        QByteArray thumbnail = aReply->readAll();
        if (!thumbnail.isEmpty() && !thumbnail.contains("File not found.")) {
            ui->imgThumbnail->setPixmap(QPixmap::fromImage(QImage::fromData(thumbnail)));
        } else {
            ui->imgThumbnail->setText("<i>Unable to download image.</i>");
        }
    }
    aReply->deleteLater();
}

void GameAdderDialog::showRemoveContextMenu_AllGames(QPoint point) {
    if (ui->treeWidgetAllGames->selectedItems().isEmpty()) return;

    QMenu contextMenu;
    contextMenu.addAction(QPixmap(":/Images/add.png"), "Add to Queue");
    auto selectedItem = contextMenu.exec(ui->treeWidgetAllGames->mapToGlobal(point));
    if (!selectedItem) return;

    for (auto *item : ui->treeWidgetAllGames->selectedItems()) {
        auto queueItem = std::make_unique<QTreeWidgetItem>(ui->treeWidgetQueue);
        queueItem->setText(0, item->text(0));
        queueItem->setText(1, item->text(1));
        queueItem->setText(2, item->text(2));
        queueItem->setData(0, Qt::UserRole, item->data(0, Qt::UserRole));
        ui->treeWidgetQueue->addTopLevelItem(queueItem.release());

        allowInjection = true;
        delete item;
    }

    ui->lblAchievements->setText("N/A");
    ui->lblAvatarAwards->setText("N/A");
    ui->lblGameName->setText("");
    ui->lblGamerscore->setText("N/A");
    ui->lblTitleID->setText("");
    ui->imgThumbnail->setPixmap(QPixmap(":/Images/watermark.png"));
}

void GameAdderDialog::on_treeWidgetAllGames_itemDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (!item) return;

    // Add the double-clicked item to the queue
    auto queueItem = std::make_unique<QTreeWidgetItem>(ui->treeWidgetQueue);
    queueItem->setText(0, item->text(0));
    queueItem->setText(1, item->text(1));
    queueItem->setText(2, item->text(2));
    queueItem->setData(0, Qt::UserRole, item->data(0, Qt::UserRole));
    ui->treeWidgetQueue->addTopLevelItem(queueItem.release());

    allowInjection = true;
    delete item;

    // Clear selection details
    ui->lblAchievements->setText("N/A");
    ui->lblAvatarAwards->setText("N/A");
    ui->lblGameName->setText("");
    ui->lblGamerscore->setText("N/A");
    ui->lblTitleID->setText("");
    ui->imgThumbnail->setPixmap(QPixmap(":/Images/watermark.png"));
}

void GameAdderDialog::on_btnAddToQueue_clicked() {
    if (ui->treeWidgetAllGames->selectedItems().isEmpty()) {
        QMessageBox::information(this, "No Selection", "Please select one or more games to add to the queue.");
        return;
    }

    for (auto *item : ui->treeWidgetAllGames->selectedItems()) {
        auto queueItem = std::make_unique<QTreeWidgetItem>(ui->treeWidgetQueue);
        queueItem->setText(0, item->text(0));
        queueItem->setText(1, item->text(1));
        queueItem->setText(2, item->text(2));
        queueItem->setData(0, Qt::UserRole, item->data(0, Qt::UserRole));
        ui->treeWidgetQueue->addTopLevelItem(queueItem.release());

        allowInjection = true;
        delete item;
    }

    ui->lblAchievements->setText("N/A");
    ui->lblAvatarAwards->setText("N/A");
    ui->lblGameName->setText("");
    ui->lblGamerscore->setText("N/A");
    ui->lblTitleID->setText("");
    ui->imgThumbnail->setPixmap(QPixmap(":/Images/watermark.png"));
}

void GameAdderDialog::on_btnRemoveFromQueue_clicked() {
    if (ui->treeWidgetQueue->selectedItems().isEmpty()) {
        QMessageBox::information(this, "No Selection", "Please select one or more games to remove from the queue.");
        return;
    }

    for (auto *item : ui->treeWidgetQueue->selectedItems()) {
        delete item;
    }

    if (ui->treeWidgetQueue->topLevelItemCount() == 0) {
        allowInjection = false;
    }
}

void GameAdderDialog::finishedDownloadingGpd(QString gamePath, QString awardPath, TitleEntry entry, bool error, DownloadError errorDetails) {
    auto *downloader = qobject_cast<GpdDownloader *>(sender());
    int index = downloader ? downloader->index() : -1;
    if (downloader) {
        // It has reported; it must never be cancelled now, because the files belong to us.
        downloaders.removeAll(downloader);
        downloader->deleteLater();
    }

    if (cancelled) {
        if (!gamePath.isEmpty()) QFile::remove(gamePath);
        if (!awardPath.isEmpty()) QFile::remove(awardPath);
        return;
    }

    if (error || !allowInjection) {
        // Update progress bar for failed/skipped downloads
        QMetaObject::invokeMethod(ui->progressBar, "setValue", Qt::QueuedConnection,
                                  Q_ARG(int, static_cast<int>(((double)++downloadedCount / totalDownloadCount) * 100)));

        // A failed award download still hands over the game GPD it saved.
        if (!gamePath.isEmpty()) QFile::remove(gamePath);
        if (!awardPath.isEmpty()) QFile::remove(awardPath);
        if (error) {
            FailedDownload failure;
            failure.gameName = QString::fromStdWString(entry.gameName);
            failure.error = errorDetails;
            failedDownloads.append(failure);
        }
    } else {
        // Start asynchronous thumbnail download
        // When it completes, gpdThumbnailDownloadFinished() will stage the GPD
        QString thumbnailUrl = QString("http://image.xboxlive.com/global/t.%1/icon/0/8000")
            .arg(QString::number(entry.titleID, 16));

        PendingGpdInjection pending;
        pending.gpdPath = gamePath;
        pending.awardPath = awardPath;
        pending.entry = entry;
        pending.treeIndex = index;

        QNetworkReply *thumbnailReply = thumbnailInjectionManager->get(QNetworkRequest(QUrl(thumbnailUrl)));
        pendingInjections[thumbnailReply] = pending;

        // Don't increment downloadedCount yet - will do it in gpdThumbnailDownloadFinished
        return; // Exit early, processing continues in gpdThumbnailDownloadFinished
    }

    if (downloadedCount == totalDownloadCount) {
        finalizeInjection();
    }
}

void GameAdderDialog::gpdThumbnailDownloadFinished(QNetworkReply* reply) {
    // Find the pending injection for this reply
    if (!pendingInjections.contains(reply)) {
        reply->deleteLater();
        return;
    }
    
    PendingGpdInjection pending = pendingInjections.take(reply);
    QByteArray thumbnailData = reply->readAll();
    reply->deleteLater();

    if (cancelled) {
        removeStagedFiles(pending);
        return;
    }
    
    // Try to inject thumbnail into GPD if download succeeded
    if (!thumbnailData.isEmpty() && !thumbnailData.contains("File not found.")) {
        try {
            GameGpd gameGpd(pending.gpdPath.toStdString());
            
            ImageEntry thumbnailEntry;
            thumbnailEntry.image.assign(
                reinterpret_cast<const u8*>(thumbnailData.constData()),
                reinterpret_cast<const u8*>(thumbnailData.constData()) + thumbnailData.size()
            );
            thumbnailEntry.length = thumbnailData.size();
            
            gameGpd.StartWriting();
            gameGpd.CreateImageEntry(&thumbnailEntry, 0x8000);
            gameGpd.StopWriting();
            gameGpd.Close();
        } catch (...) {
            // Continue without thumbnail if injection fails
        }
    }
    
    // Nothing touches the profile until finalize, so Cancel is a true abort.
    staged.append(pending);

    QMetaObject::invokeMethod(ui->progressBar, "setValue", Qt::QueuedConnection,
                              Q_ARG(int, static_cast<int>(((double)++downloadedCount / totalDownloadCount) * 100)));

    if (downloadedCount == totalDownloadCount)
        finalizeInjection();

    // Update progress
    QMetaObject::invokeMethod(ui->progressBar, "setValue", Qt::QueuedConnection,
                              Q_ARG(int, static_cast<int>(((double)++downloadedCount / totalDownloadCount) * 100)));
    
    // Check if all downloads are complete
    if (downloadedCount == totalDownloadCount) {
        finalizeInjection();
    }
}

void GameAdderDialog::closeEvent(QCloseEvent *event)
{
    QDialog::closeEvent(event);
}

bool GameAdderDialog::initializePecPackage() {
    try {
        u32 flags = StfsPackagePEC;
        if (!package->FileExists("PEC", false)) {
            flags |= StfsPackageCreate;
        } else if (package->GetFileEntry("PEC", false, NULL, false).blocksForFile < 3) {
            QMessageBox::warning(this, "Invalid PEC File",
                    "The PEC file has an invalid block count and will be recreated.");
            flags |= StfsPackageCreate;
        } else {
            package->ExtractFile("PEC", pecTempPath.toStdString());
        }

        pecPackage = std::make_unique<StfsPackage>(pecTempPath.toStdString(), flags);
        return true;
    }
    catch (const std::string &error) {
        QMessageBox::critical(this, "PEC Error",
                "The avatar award container (PEC) could not be opened.\n\n" +
                QString::fromStdString(error));
    }
    catch (const std::exception &error) {
        QtHelpers::ShowUnexpectedError(this, error);
    }

    pecPackage.reset();
    return false;
}

void GameAdderDialog::commitStagedGame(PendingGpdInjection pending)
{
    const std::string name =
            (QString::number(pending.entry.titleID, 16).toUpper() + ".gpd").toStdString();
    const QString awardBackup = pending.awardPath.isEmpty() ? QString() : pending.awardPath + ".old";
    bool awardInjected = false, awardReplaced = false;
    QString failure;

    try
    {
        // 1. Award into the temporary PEC first: undoing it touches nothing in the profile.
        if (!pending.awardPath.isEmpty())
        {
            if (!pecPackage)
                throw std::string("The avatar award container (PEC) is not open.\n");
            if (pecPackage->FileExists(name, false))
            {
                pecPackage->ExtractFile(name, awardBackup.toStdString());
                pecPackage->ReplaceFile(pending.awardPath.toStdString(), name);
                awardReplaced = true;
            }
            else
            {
                pecPackage->InjectFile(pending.awardPath.toStdString(), name);
                awardInjected = true;
            }
        }

        // 2. Game GPD replaced in place: the old one survives a failed download or inject.
        if (package->FileExists(name, false))
            package->ReplaceFile(pending.gpdPath.toStdString(), name);
        else
            package->InjectFile(pending.gpdPath.toStdString(), name);

        // 3. One dashboard title entry per title; the count grows only for a new title.
        auto existing = std::find_if(dashGpd->gamesPlayed.begin(), dashGpd->gamesPlayed.end(),
                [&](const TitleEntry &t) { return t.titleID == pending.entry.titleID; });
        if (existing != dashGpd->gamesPlayed.end())
        {
            TitleEntry old = *existing;
            dashGpd->DeleteTitleEntry(&old);
        }
        else
            dashGpd->gamePlayedCount.int32++;
        dashGpd->CreateTitleEntry(&pending.entry);

        if (pending.treeIndex >= 0)
            ui->treeWidgetQueue->topLevelItem(pending.treeIndex)->setData(
                    0, Qt::UserRole, QVariant::fromValue(pending.entry));
    }
    catch (const std::string &error) { failure = QString::fromStdString(error).trimmed(); }
    catch (const std::exception &error) { failure = QString::fromUtf8(error.what()); }

    if (!failure.isEmpty())
    {
        try
        {
            if (awardInjected)
                pecPackage->RemoveFile(name);
            else if (awardReplaced)
                pecPackage->ReplaceFile(awardBackup.toStdString(), name);
        }
        catch (...) {}
        injectionErrors << QString("%1: %2").arg(QString::fromStdWString(pending.entry.gameName), failure);
    }

    QFile::remove(pending.gpdPath);
    if (!pending.awardPath.isEmpty())
        QFile::remove(pending.awardPath);
    if (!awardBackup.isEmpty())
        QFile::remove(awardBackup);
}

void GameAdderDialog::finalizeInjection()
{
    if (finalizing || cancelled)
        return;
    finalizing = true;
    downloaders.clear();

    // Commit the staged games first; until here the profile was untouched.
    for (PendingGpdInjection &pending : staged)
        commitStagedGame(pending);
    const bool anyStaged = !staged.isEmpty();
    staged.clear();

    QStringList problems = injectionErrors;
    for (const auto &failure : failedDownloads)
        problems << QString("%1: download failed (%2)")
                .arg(failure.gameName, failure.error.userFriendlyDescription);

    if (!anyStaged)
    {
        // Every download failed or was skipped: the profile was not changed.
        if (!problems.isEmpty())
            QMessageBox::warning(this, "No Games Added",
                    "Nothing was changed:\n\n• " + problems.join("\n• "));
        if (dispose)
            close();
        else
            done(QDialog::Rejected);
        return;
    }

    // 1. Award container: match the profile, rehash and sign it, then store it.
    if (pecPackage)
    {
        try
        {
            memcpy(pecPackage->metaData->profileID, package->metaData->profileID, 8);
            pecPackage->metaData->enabled = true;
            pecPackage->metaData->WriteMetaData();
            pecPackage->Rehash();
            pecPackage->Resign(kvPath);
            pecPackage->Close();
            pecPackage.reset();

            if (package->FileExists("PEC", false))
                package->ReplaceFile(pecTempPath.toStdString(), "PEC");
            else
                package->InjectFile(pecTempPath.toStdString(), "PEC");
        }
        catch (const std::string &error) { problems << "Avatar awards (PEC): " + QString::fromStdString(error).trimmed(); }
        catch (const std::exception &error) { problems << "Avatar awards (PEC): " + QString::fromUtf8(error.what()); }
    }

    // 2. Dashboard GPD last among the contents: it indexes the titles.
    try
    {
        dashGpd->WriteSettingEntry(dashGpd->gamePlayedCount);
        dashGpd->Close();
        package->ReplaceFile(dashGpdTempPath.toStdString(), "FFFE07D1.gpd");
    }
    catch (const std::string &error) { problems << "Dashboard GPD: " + QString::fromStdString(error).trimmed(); }
    catch (const std::exception &error) { problems << "Dashboard GPD: " + QString::fromUtf8(error.what()); }

    // 3. Always rehash and sign the profile: the per-game steps already changed it.
    bool resigned = false;
    try
    {
        package->Rehash();
        package->Resign(kvPath);
        resigned = true;
    }
    catch (const std::string &error) { problems << "Rehash/resign: " + QString::fromStdString(error).trimmed(); }
    catch (const std::exception &error) { problems << "Rehash/resign: " + QString::fromUtf8(error.what()); }

    QFile::remove(dashGpdTempPath);
    QFile::remove(pecTempPath);

    if (dispose && package)
    {
        package->Close();
        delete package;
        package = nullptr;
    }

    if (!problems.isEmpty())
        QMessageBox::warning(this, "Games Added With Problems",
                "Some steps did not complete:\n\n• " + problems.join("\n• ") +
                (resigned ? QString() :
                 QString("\n\nThe profile was NOT resigned; a console may refuse to load it.")));
    else if (!dispose)
        QMessageBox::information(this, "Success",
                "Games have been added. The profile was rehashed and resigned.");

    if (dispose)
        close();
    else
    {
        emit operationsComplete();
        done(QDialog::Accepted);
    }
}

void GameAdderDialog::showRemoveContextMenu_QueuedGames(QPoint point) {
    if (ui->treeWidgetQueue->selectedItems().isEmpty()) return;

    QMenu contextMenu;
    contextMenu.addAction(QPixmap(":/Images/delete.png"), "Remove from Queue");
    auto selectedItem = contextMenu.exec(ui->treeWidgetQueue->mapToGlobal(point));
    if (!selectedItem) return;

    for (auto *item : ui->treeWidgetQueue->selectedItems()) {
        auto allGamesItem = std::make_unique<QTreeWidgetItem>(ui->treeWidgetAllGames);
        allGamesItem->setText(0, item->text(0));
        allGamesItem->setText(1, item->text(1));
        allGamesItem->setText(2, item->text(2));
        allGamesItem->setData(0, Qt::UserRole, item->data(0, Qt::UserRole));
        ui->treeWidgetAllGames->addTopLevelItem(allGamesItem.release());

        delete item;
    }
}

void GameAdderDialog::on_treeWidgetAllGames_currentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *) {
    if (!current) {
        ui->lblGameName->setText("<i>N/A</i>");
        ui->lblTitleID->setText("<span style=\"color:#4f4f4f;\"><i>N/A</i></span>");
        ui->imgThumbnail->setPixmap(QPixmap());
        return;
    }

    if (ui->treeWidgetAllGames->selectedItems().count() == 1) {
        TitleEntry entry = current->data(0, Qt::UserRole).value<TitleEntry>();
        imageManager->get(QNetworkRequest(QUrl("http://image.xboxlive.com/global/t." + QString::number(entry.titleID, 16) + "/icon/0/8000")));

        ui->lblGameName->setText(QString::fromStdWString(entry.gameName));
        ui->lblTitleID->setText("<span style=\"color:#4f4f4f;\">" + QString::number(entry.titleID, 16).toUpper() + "</span>");
        ui->lblGamerscore->setText(QString::number(entry.totalGamerscore));
        ui->lblAvatarAwards->setText(QString::number(entry.avatarAwardCount));
        ui->lblAchievements->setText(QString::number(entry.achievementCount));
    } else {
        ui->lblAchievements->setText("N/A");
        ui->lblAvatarAwards->setText("N/A");
        ui->lblGameName->setText("");
        ui->lblGamerscore->setText("N/A");
        ui->lblTitleID->setText("");
        ui->imgThumbnail->setPixmap(QPixmap(":/Images/watermark.png"));
    }
}

void GameAdderDialog::on_pushButton_clicked()
{
    reject();
}

void GameAdderDialog::reject()
{
    cancelOperation();
    QDialog::reject();
}

void GameAdderDialog::cancelOperation()
{
    // After finalize starts, the writes are committed; a close then just closes.
    if (cancelled || finalizing)
        return;
    cancelled = true;
    allowInjection = false;

    for (const QPointer<GpdDownloader> &downloader : std::as_const(downloaders))
        if (downloader)
            downloader->Cancel();
    downloaders.clear();

    // abort() can emit finished() synchronously; the slot sees `cancelled`
    // and drops the files, so iterate over a copy of the keys.
    const QList<QNetworkReply *> replies = pendingInjections.keys();
    for (QNetworkReply *reply : replies)
        reply->abort();

    discardStaged();
}

void GameAdderDialog::removeStagedFiles(const PendingGpdInjection &pending)
{
    if (!pending.gpdPath.isEmpty())
        QFile::remove(pending.gpdPath);
    if (!pending.awardPath.isEmpty())
        QFile::remove(pending.awardPath);
}

void GameAdderDialog::discardStaged()
{
    for (const PendingGpdInjection &pending : std::as_const(staged))
        removeStagedFiles(pending);
    staged.clear();
}

void GameAdderDialog::on_pushButton_2_clicked() {
    if (!downloaders.isEmpty() || !pendingInjections.isEmpty() || !staged.isEmpty())
        return;

    qDebug() << "GameAdderDialog::on_pushButton_2_clicked() - Add Game button clicked";
    
    int totalCount = ui->treeWidgetQueue->topLevelItemCount();
    QString msg = QString("Total games in queue: %1").arg(totalCount);
    qDebug() << msg;
    
    if (totalCount < 1) {
        QMessageBox::warning(this, "No Games", "You have selected no games to add.");
        return;
    }

    totalDownloadCount = totalCount;
    downloadedCount = 0;

    try {
        if (dashGpd->gamePlayedCount.entry.type == 0) {
            dashGpd->gamePlayedCount.type = Int32;
            dashGpd->gamePlayedCount.int32 = 0;
            dashGpd->CreateSettingEntry(&dashGpd->gamePlayedCount, GamercardTitlesPlayed);
        }
    }
    catch (const std::string &error) {
        QMessageBox::critical(this, "GPD Creation Error", "Error creating gamePlayedCount setting entry:\n" + QString::fromStdString(error));
        return;  // Exit the function if an error occurs
    }
    catch (const std::exception &exception)
    {
        QtHelpers::ShowUnexpectedError(this, exception);
    }

    // Temporary test hook: if settings.network/forceDownload is true (default true), always download
    QSettings settings;
    bool forceDownload = settings.value("network/forceDownload", false).toBool();

    QList<QPair<TitleEntry, int>> toDownload;

    for (int i = 0; i < totalCount; ++i) {
        TitleEntry entry = ui->treeWidgetQueue->topLevelItem(i)->data(0, Qt::UserRole).value<TitleEntry>();
        QString gpdName = QString::number(entry.titleID, 16).toUpper() + ".gpd";

        bool fileExists = package->FileExists(gpdName.toStdString());

        if (fileExists && !forceDownload) {
            QString gameName = QString::fromStdWString(entry.gameName);
            QMessageBox msgBox(this);
            msgBox.setIcon(QMessageBox::Question);
            msgBox.setWindowTitle("Game Already Exists");
            msgBox.setText(QString("The game '%1' already exists in this profile.").arg(gameName));
            msgBox.setInformativeText("Do you want to replace it with a fresh download?");
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            msgBox.setDefaultButton(QMessageBox::No);

            int ret = msgBox.exec();

            if (ret == QMessageBox::Cancel) {
                totalDownloadCount = 0;
                downloadedCount = 0;
                return;
            } else if (ret == QMessageBox::No) {
                ++downloadedCount;
                continue;
            }
            // Yes: download first; the GPD is replaced only after it succeeds.
        }

        toDownload.append(qMakePair(entry, i));
    }

    if (toDownload.isEmpty()) {
        if (dispose) {
            package->Close();
            delete package;
            package = nullptr;
        }
        close();
        return;
    }

    kvPath = QtHelpers::GetKVPath(package->metaData->certificate.ownerConsoleType, this);
    if (kvPath.empty()) {
        QMessageBox::warning(this, "No KeyVault",
                "No keyvault was selected, so nothing was changed.");
        totalDownloadCount = downloadedCount = 0;
        return;
    }

    const bool needsPec = std::any_of(toDownload.begin(), toDownload.end(),
            [](const QPair<TitleEntry, int> &d) { return d.first.avatarAwardCount != 0; });
    if (needsPec && !pecPackage && !initializePecPackage()) {
        totalDownloadCount = downloadedCount = 0;
        return;
    }

    injectionErrors.clear();
    finalizing = false;

    ui->pushButton_2->setEnabled(false);
    for (const auto &d : toDownload) {
        try {
            auto *downloader = new GpdDownloader(d.first, d.second,
                    d.first.avatarAwardCount != 0, this);
            connect(downloader, &GpdDownloader::FinishedDownloading,
                    this, &GameAdderDialog::finishedDownloadingGpd);
            downloaders.append(downloader);
            downloader->BeginDownload();
        } catch (const std::exception &e) {
            injectionErrors << QString("download start: %1").arg(e.what());
            ++downloadedCount;
        }
    }

    if (downloadedCount == totalDownloadCount)
        finalizeInjection();
}

void GameAdderDialog::showAllItems() {
    for (int i = 0; i < ui->treeWidgetAllGames->topLevelItemCount(); ++i) {
        ui->treeWidgetAllGames->topLevelItem(i)->setHidden(false);
    }
}

void GameAdderDialog::getListing() {
    ui->treeWidgetAllGames->clear();
    ui->tabWidget->setEnabled(false);

    QString url = "https://raw.githubusercontent.com/Mordaur/xbox360-gpd-files/main/gameadder.json";
    QNetworkRequest request((QUrl(url)));

    QNetworkReply *reply = manager->get(request);

    // Create a timeout timer
    QTimer *timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, this, [this, reply]() {
        if (reply->isRunning()) {
            reply->abort();
            QMessageBox::warning(this, "Timeout Error", "The network request timed out. Please check your connection.");
        }
    });
    timeoutTimer->start(10000); // 10 seconds timeout

    // Clean up the timer when the request is finished
    connect(reply, &QNetworkReply::finished, timeoutTimer, &QTimer::deleteLater);
}

void GameAdderDialog::on_btnShowAll_clicked() {
    ui->txtSearch->clear();
    ui->btnShowAll_2->setChecked(false);
    showAllItems();
}

void GameAdderDialog::on_txtSearch_textChanged(const QString &) {
    auto itemsMatched = ui->treeWidgetAllGames->findItems(ui->txtSearch->text(), Qt::MatchContains | Qt::MatchRecursive);
    for (int i = 0; i < ui->treeWidgetAllGames->topLevelItemCount(); ++i) {
        ui->treeWidgetAllGames->topLevelItem(i)->setHidden(true);
    }

    if (itemsMatched.isEmpty()) {
        QtHelpers::SetErrorStyle(ui->txtSearch);
        showAllItems();
        return;
    }

    QtHelpers::ClearErrorStyle(ui->txtSearch);
    for (auto *item : itemsMatched) {
        for (auto *parent = item->parent(); parent; parent = parent->parent()) {
            parent->setHidden(false);
            parent->setExpanded(true);
        }
        item->setHidden(false);
    }
}

void GameAdderDialog::on_btnShowAll_2_clicked(bool checked) {
    for (int i = 0; i < ui->treeWidgetAllGames->topLevelItemCount(); ++i) {
        TitleEntry entry = ui->treeWidgetAllGames->topLevelItem(i)->data(0, Qt::UserRole).value<TitleEntry>();
        ui->treeWidgetAllGames->topLevelItem(i)->setHidden(checked && entry.avatarAwardCount == 0);
    }
}


