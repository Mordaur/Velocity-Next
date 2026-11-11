#include "gpduploader.h"
#include <QUrlQuery>
#include <memory>

GpdUploader::GpdUploader(QStringList gamePaths, QStringList avatarPaths, QStringList titleIDs,
        bool deleteGpds, QObject *parent = 0) : QObject(parent), gamePaths(gamePaths),
    avatarPaths(avatarPaths), titleIDs(titleIDs), deleteGpds(deleteGpds)
{
    settings = new QSettings("Exetelek", "Velocity");

    // only if they selected to share data with Velocity
    if (settings->value("AnonData").toBool())
    {
        // initalize variables
        success = 0;
        failures = 0;
        currentIndex = 0;

        networkAccessManager = new QNetworkAccessManager(this);
        connect(networkAccessManager, SIGNAL(finished(QNetworkReply*)), this, SLOT(reply(QNetworkReply*)));

        // begin the first upload
        if (gamePaths.count() > 0)
            uploadGpd(gamePaths.at(currentIndex), avatarPaths.at(currentIndex), titleIDs.at(currentIndex));
    }
    else
    {
        // remove temporary files
        foreach (const QString &gamePath, gamePaths)
        {
            if (!gamePath.isEmpty() && QFile::exists(gamePath) && !QFile::remove(gamePath))
                qWarning() << "GpdUploader: Failed to remove temporary game GPD" << gamePath;
        }

        foreach (const QString &avatarPath, avatarPaths)
        {
            if (!avatarPath.isEmpty() && QFile::exists(avatarPath) && !QFile::remove(avatarPath))
                qWarning() << "GpdUploader: Failed to remove temporary award GPD" << avatarPath;
        }
    }
}

void GpdUploader::uploadGpd(QString gamePath, QString awardPath, QString titleID)
{
    std::shared_ptr<GameGpd> gpd;
    try
    {
        // open the Gpd
        try
        {
            gpd = std::make_shared<GameGpd>(gamePath.toStdString());
        }
        catch (string error)
        {
            qDebug() << titleID << " Error: " << QString::fromStdString(error);
            return;
        }

        gpd->StartWriting();

        // calculate total gamerscore
        int totalGamerscore = 0;
        for (DWORD x = 0; x < gpd->achievements.size(); x++)
            totalGamerscore += gpd->achievements.at(x).gamerscore;

        // check for award Gpd
        DWORD awards = 0, mAwards = 0, fAwards = 0;
        if (QFile::exists(awardPath))
        {
            // open the award gpd, get the amount of awards
        AvatarAwardGpd agpd(awardPath.toStdString());
            awards = agpd.avatarAwards.size();

            for (DWORD x = 0; x < agpd.avatarAwards.size(); x++)
            {
                AvatarAwardData award = agpd.avatarAwards.at(x);
                AssetGender g = AvatarAwardGpd::GetAssetGender(&award);

                // lock all awards
                award.flags &= 0xF;
                agpd.WriteAvatarAward(&award);

                // caluclate male/female award counts
                if (g == Male)
                    mAwards++;
                else if (g == Female)
                    fAwards++;
                else
                {
                    mAwards++;
                    fAwards++;
                }
            }

            // close award file
            agpd.Close();
        }


        // remove all setting entries
        int iterateCount = gpd->settings.size();
        for (int x = 0; x < iterateCount; x++)
            gpd->DeleteSettingEntry(gpd->settings.at(0));

        // remove all images
        iterateCount = gpd->images.size();
        char hitTitleImage = 0;
        for (int x = 0; x < iterateCount; x++)
        {
            if (gpd->images.at(hitTitleImage).entry.id != TitleInformation)
                gpd->DeleteImageEntry(gpd->images.at(hitTitleImage));
            else
                hitTitleImage++;
        }

        // lock all achievements
        for (DWORD x = 0; x < gpd->achievements.size(); x++)
        {
            AchievementEntry chiev = gpd->achievements.at(x);
            chiev.flags &= 0xF;
            gpd->WriteAchievementEntry(&chiev);
        }


        qDebug() << "game name: " << QString::fromStdWString(gpd->gameName.ws);

        // clean the gpd
        gpd->StopWriting();
        gpd->CleanGpd();
        gpd->Close();

        // get the arguments before deleting the gpd
    QString titleName = QString::fromStdWString(gpd->gameName.ws);
    DWORD achievementCount = gpd->achievements.size();
    gpd.reset();

        // send the Gpd(s) to the server
        sendRequest(gamePath, (QFile::exists(awardPath)) ? awardPath : "", titleName, titleID,
                    achievementCount, totalGamerscore, awards, mAwards, fAwards);
    }
    catch (string s)
    {
        qDebug() << QString::fromStdString(s) + "\r\n" + gamePath;
    }
    catch(...)
    {
        qDebug() << "unknown error (uploading gpd)";
    }

    gpd.reset();
}

void GpdUploader::reply(QNetworkReply *reply)
{
    // print out the reply/error message
    if (reply->error() || reply->bytesAvailable() < 1)
    {
        failures++;
    }
    else
    {
        success++;
    }

    // begin the next request, if there is one
    if (gamePaths.count() > ++currentIndex)
        uploadGpd(gamePaths.at(currentIndex), avatarPaths.at(currentIndex), titleIDs.at(currentIndex));
}

void GpdUploader::sendRequest(QString filePath, QString awardFilePath, QString gameName,
        QString titleID, DWORD achievementCount, DWORD gamerscoreTotal, DWORD awards, DWORD mAwards,
        DWORD fAwards)
{
    try
    {
        // setup the GET parameters with URL encoding
        QUrlQuery query;
        query.addQueryItem("nm", gameName);
        query.addQueryItem("tid", titleID.toUpper());
        query.addQueryItem("achc", QString::number(achievementCount));
        query.addQueryItem("ttlgs", QString::number(gamerscoreTotal));
        query.addQueryItem("ttlac", QString::number(awards));
        query.addQueryItem("ttlmac", QString::number(mAwards));
        query.addQueryItem("ttlfac", QString::number(fAwards));
        QString getData = "?" + query.toString(QUrl::FullyEncoded);

        // open the Gpd file
        QFile gpdFile(filePath);
        if (!gpdFile.open(QIODevice::ReadWrite))
            return;

        // open the award file (if there is one)
        QFile awardFile(awardFilePath);
        if (awardFilePath != "" && !awardFile.open(QIODevice::ReadWrite))
        {
            qDebug() << titleID << " Error: could not open award file" << awardFilePath;
            return;
        }

        // initialize boundary
        QString boundary = "-----------------------------7d935033608e2";

        // byte array to be sent in POST
        QByteArray dataToSend;

        // add data to the variable to be sent
        dataToSend.append(("\r\n--" + boundary + "\r\n").toUtf8());
        dataToSend.append(("Content-Disposition: form-data; name=\"game\"; filename=\"" + titleID.toUpper() +
                ".gpd\"\r\n").toUtf8());
        dataToSend.append("Content-Type: application/octet-stream\r\n\r\n");
        dataToSend.append(gpdFile.readAll());

        // remove/close the gpd
        gpdFile.close();
        if (deleteGpds)
            if (!QFile::remove(filePath))
                qDebug() << "failed to remove gpd file";

        // setup next boundary
        dataToSend.append(("\r\n--" + boundary + "\r\n").toUtf8());

        // add the award gpd to the data to be sent, if there is an award gpd
        if (awardFilePath != "")
        {
            dataToSend.append(("Content-Disposition: form-data; name=\"award\"; filename=\"" + titleID.toUpper()
                    + ".gpd\"\r\n").toUtf8());
            dataToSend.append("Content-Type: application/octet-stream\r\n\r\n");
            dataToSend.append(awardFile.readAll());

            // remove/close the award gpd
            if (deleteGpds)
            {
                if (!awardFile.remove())
                    qDebug() << "failed to remove award file";
            }
            else
                awardFile.close();
        }

        // end the data to be sent
        dataToSend.append(("\r\n--" + boundary + "--\r\n").toUtf8());

        // set the network request
        QString fullUrl = "https://steho.be/velocity/gameadder/add.php" + getData;
        QUrl url(fullUrl);
        QNetworkRequest request{url};
        request.setRawHeader("Content-Type",
                "multipart/form-data; boundary=-----------------------------7d935033608e2");
        request.setHeader(QNetworkRequest::ContentLengthHeader, dataToSend.size());

        // perform POST request
        networkAccessManager->post(request, dataToSend);
    }
    catch (...)
    {
        qDebug() << "unknown error (uploading gpd) at sendRequest";
    }
}


