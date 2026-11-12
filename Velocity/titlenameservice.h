#ifndef TITLENAMESERVICE_H
#define TITLENAMESERVICE_H

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>

class QNetworkAccessManager;

// Optional online title lookup, mirroring GODspeed's "UseUnity" option:
// XboxUnity maps a title ID to a name and a cover image. Local package data
// always wins; this is only consulted for titles that could not be resolved
// from the device itself, and only when the user enabled it in Preferences.
class TitleNameService : public QObject
{
    Q_OBJECT

public:
    static TitleNameService &instance();

    // A cached name that is still within its expiry, or an empty string.
    QString cached(const QString &titleId) const;

    // Queue a lookup for a title ID (no-op when disabled, cached or in flight).
    void request(const QString &titleId);

signals:
    void resolved(const QString &titleId, const QString &name);
    void iconResolved(const QString &titleId, const QPixmap &icon);

private:
    struct Entry
    {
        QString name;
        qint64 timestamp = 0;
    };

    explicit TitleNameService(QObject *parent = nullptr);

    void loadCache();
    void saveCache() const;
    void finishName(const QString &titleId, const QString &name);
    void fetchIcon(const QString &titleId);

    QNetworkAccessManager *manager;
    QHash<QString, Entry> cache;
    QSet<QString> pending;
    QSet<QString> iconRequested;
    bool cacheLoaded = false;
};

#endif // TITLENAMESERVICE_H
