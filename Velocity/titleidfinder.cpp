#include "titleidfinder.h"

TitleIdFinder::TitleIdFinder(QString gameName, QObject* parent)
    : QObject(parent), gameName(gameName), isExactIdSearch(false) {
    // Initialize the network manager
    networkManager = new QNetworkAccessManager(this);

    networkManager->setAutoDeleteReplies(true);
    connect(networkManager, &QNetworkAccessManager::finished, this, &TitleIdFinder::replyFinished);
}

void TitleIdFinder::StartSearch() {
    // Detect if input is a hex Title ID (8 hex characters) or a game name
    QString url;
    QRegularExpression hexPattern("^[0-9A-Fa-f]{8}$");
    
    if (hexPattern.match(gameName.trimmed()).hasMatch()) {
        // Search by Title ID (exact match) - uses different endpoint
        isExactIdSearch = true;
        url = QString("https://dbox.tools/api/title_ids/%1").arg(gameName.trimmed().toUpper());
    } else {
        // Search by name (partial match)
        isExactIdSearch = false;
        url = QString("https://dbox.tools/api/title_ids/?name=%1&system=XBOX360&limit=100&offset=0").arg(gameName);
    }
    
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(10000);  // 10 second timeout
    networkManager->get(request);
}

void TitleIdFinder::replyFinished(QNetworkReply* reply) {
    QList<TitleData> matches;  // Initialize outside to ensure it's always emitted
    
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray response = reply->readAll();
        QJsonDocument jsonResponse = QJsonDocument::fromJson(response);
        
        if (jsonResponse.isNull()) {
            qWarning() << "Title ID Finder: Invalid JSON response from dbox.tools API";
            emit SearchFinished(matches);  // Empty list
            reply->deleteLater();
            return;
        }
        
        if (isExactIdSearch) {
            // Exact Title ID lookup returns a single object
            if (!jsonResponse.isObject()) {
                qWarning() << "Title ID Finder: Expected JSON object for exact ID lookup";
                emit SearchFinished(matches);
                reply->deleteLater();
                return;
            }
            
            QJsonObject item = jsonResponse.object();
            // Check if the response has valid data (not an error response)
            if (item.contains("name") && item.contains("title_id")) {
                TitleData data;
                data.titleName = item["name"].toString();
                data.titleID = item["title_id"].toString().toUInt(nullptr, 16);
                data.bingId = item["bing_id"].toString();
                matches.append(data);
            }
        } else {
            // Name search returns an object with "items" array
            if (!jsonResponse.isObject()) {
                qWarning() << "Title ID Finder: Expected JSON object for name search";
                emit SearchFinished(matches);
                reply->deleteLater();
                return;
            }
            
            QJsonObject jsonObject = jsonResponse.object();
            QJsonArray itemsArray = jsonObject["items"].toArray();

            for (const QJsonValue& value : itemsArray) {
                QJsonObject item = value.toObject();
                TitleData data;
                data.titleName = item["name"].toString();
                data.titleID = item["title_id"].toString().toUInt(nullptr, 16); // Parse as hex
                data.bingId = item["bing_id"].toString();

                matches.append(data);
            }
        }

        emit SearchFinished(matches);
    } else {
        // Handle network errors with specific messages
        QString errorMsg = "Title ID Finder error: ";
        if (reply->error() == QNetworkReply::TimeoutError) {
            errorMsg += "Connection timeout (dbox.tools not responding)";
        } else if (reply->error() == QNetworkReply::HostNotFoundError) {
            errorMsg += "Host not found (dbox.tools unreachable)";
        } else {
            errorMsg += reply->errorString();
        }
        qWarning() << errorMsg;
        emit SearchFinished(matches);  // Empty list on error
    }
    reply->deleteLater();
}

void TitleIdFinder::SetGameName(QString newGameName) {
    gameName = newGameName;
}


