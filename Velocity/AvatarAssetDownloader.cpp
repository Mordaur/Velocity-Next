#include "AvatarAssetDownloader.h"

#include <QDebug>

AvatarAssetDownloader::AvatarAssetDownloader(QString titleID, QString guid, QObject *parent) :
    QObject(parent), titleID(titleID), guid(guid), v1TempPath(QString("")), v2TempPath(QString("")),
    v1Done(false), v2Done(false)
{
    manager = new QNetworkAccessManager(this);
    connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(onRequestFinished(QNetworkReply*)));

    /*http = new QHttp(this);
    http->setHost("download.xboxlive.com");

    connect(http, SIGNAL(done(bool)), this, SLOT(onDone(bool)));
    connect(http, SIGNAL(requestFinished(int,bool)), this, SLOT(onRequestFinished(int,bool)));*/
}

void AvatarAssetDownloader::BeginDownload()
{
    manager->get(QNetworkRequest(QUrl("http://download.xboxlive.com/content/" + titleID +
            "/avataritems/" + guid + ".bin")));
}

QString AvatarAssetDownloader::GetV1TempPath()
{
    return v1TempPath;
}

QString AvatarAssetDownloader::GetV2TempPath()
{
    return v2TempPath;
}

QString AvatarAssetDownloader::GetGUID()
{
    return guid;
}

QString AvatarAssetDownloader::GetTitleID()
{
    return titleID;
}

void AvatarAssetDownloader::onRequestFinished(QNetworkReply *aReply)
{
    if (aReply->error() != QNetworkReply::NoError)
    {
        qDebug() << "error: " << aReply->errorString();
        return;
    }

    DWORD fileSize = aReply->bytesAvailable();

    // All assets have a Ytgr header that's 0x140 bytes
    if (fileSize < 0x140)
    {
        if (!v2Done)
        {
            v1Done = true;
            v2Done = true;
            v1TempPath = "";
            manager->get(QNetworkRequest(QUrl("http://download.xboxlive.com/content/" + titleID +
                    "/avataritems/v2/" + guid + ".bin")));
        }
        else
            v2TempPath = "";
        return;
    }

    // Skip 0x140 byte Ytgr header
    BYTE temp[0x140];
    aReply->read((char*)temp, 0x140);

    QString tempPath = QDir::tempPath() + "/" + QUuid::createUuid().toString().replace("{",
            "").replace("}", "").replace("-", "");

    QFile assetFile(tempPath);
    if (!assetFile.open(QFile::Truncate | QFile::WriteOnly))
    {
        // Leave the path empty so the caller does not try to inject a file that
        // was never written.
        qWarning() << "AvatarAssetDownloader: could not write" << tempPath;
        tempPath.clear();
    }
    else
    {
        assetFile.write(aReply->readAll());
        assetFile.flush();
        assetFile.close();
    }

    if (!v1Done)
        v1TempPath = tempPath;
    else
        v2TempPath = tempPath;

    v1Done = true;

    if (!v2Done)
    {
        v2Done = true;
        manager->get(QNetworkRequest(QUrl("http://download.xboxlive.com/content/" + titleID +
                "/avataritems/v2/" + guid + ".bin")));
    }
    else
        emit FinishedDownloading();
}

void AvatarAssetDownloader::onDone(bool /* error */)
{
}


