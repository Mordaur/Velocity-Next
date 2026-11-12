#include "titlenameservice.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace
{
    constexpr qint64 kExpirySeconds = 30 * 24 * 60 * 60; // 30 days, as GODspeed's default
    constexpr const char *kTitleListUrl =
            "http://xboxunity.net/Resources/Lib/TitleList.php?search=%1&category=0";
    constexpr const char *kIconUrl = "http://xboxunity.net/Resources/Lib/Icon.php?tid=%1";

    bool useUnityEnabled()
    {
        return QSettings("Exetelek", "Velocity").value("UseUnity", false).toBool();
    }

    QString cacheFilePath()
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (dir.isEmpty())
            return {};
        QDir().mkpath(dir);
        return dir + "/titlecache.json";
    }
}

TitleNameService &TitleNameService::instance()
{
    static TitleNameService service;
    return service;
}

TitleNameService::TitleNameService(QObject *parent) : QObject(parent)
{
    manager = new QNetworkAccessManager(this);
}

void TitleNameService::loadCache()
{
    if (cacheLoaded)
        return;
    cacheLoaded = true;

    const QString path = cacheFilePath();
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();

    const QJsonObject root = document.object();
    for (auto it = root.begin(); it != root.end(); ++it)
    {
        const QJsonObject entry = it.value().toObject();
        Entry cached;
        cached.name = entry.value("name").toString();
        cached.timestamp = static_cast<qint64>(entry.value("time").toDouble());
        if (!cached.name.isEmpty())
            cache.insert(it.key(), cached);
    }
}

void TitleNameService::saveCache() const
{
    const QString path = cacheFilePath();
    if (path.isEmpty())
        return;

    QJsonObject root;
    for (auto it = cache.begin(); it != cache.end(); ++it)
    {
        QJsonObject entry;
        entry.insert("name", it.value().name);
        entry.insert("time", static_cast<double>(it.value().timestamp));
        root.insert(it.key(), entry);
    }

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.close();
    }
}

QString TitleNameService::cached(const QString &titleId) const
{
    const auto it = cache.constFind(titleId.toUpper());
    if (it == cache.constEnd())
        return {};

    if (QDateTime::currentSecsSinceEpoch() - it.value().timestamp > kExpirySeconds)
        return {};

    return it.value().name;
}

void TitleNameService::request(const QString &titleId)
{
    loadCache();

    const QString key = titleId.toUpper();
    if (!useUnityEnabled() || pending.contains(key))
        return;
    if (!cached(key).isEmpty())
    {
        emit resolved(key, cached(key));
        fetchIcon(key); // covers are kept in memory only, so refetch once per run
        return;
    }

    pending.insert(key);

    QNetworkRequest request(QUrl(QString::fromLatin1(kTitleListUrl).arg(key)));
    request.setHeader(QNetworkRequest::UserAgentHeader, "VelocityNext");

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
        reply->deleteLater();
        pending.remove(key);

        if (reply->error() != QNetworkReply::NoError)
            return;

        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
        const QJsonArray items = document.object().value("Items").toArray();
        if (items.isEmpty())
            return;

        const QString name = items.first().toObject().value("Name").toString().trimmed();
        if (name.isEmpty())
            return;

        finishName(key, name);
        fetchIcon(key);
    });
}

void TitleNameService::finishName(const QString &titleId, const QString &name)
{
    Entry entry;
    entry.name = name;
    entry.timestamp = QDateTime::currentSecsSinceEpoch();
    cache.insert(titleId, entry);
    saveCache();

    emit resolved(titleId, name);
}

void TitleNameService::fetchIcon(const QString &titleId)
{
    if (iconRequested.contains(titleId))
        return;
    iconRequested.insert(titleId);

    QNetworkRequest request(QUrl(QString::fromLatin1(kIconUrl).arg(titleId)));
    request.setHeader(QNetworkRequest::UserAgentHeader, "VelocityNext");

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, titleId]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
            return;

        const QByteArray bytes = reply->readAll();
        QPixmap icon;
        if (icon.loadFromData(bytes, "PNG") && !icon.isNull())
            emit iconResolved(titleId, icon);
    });
}
