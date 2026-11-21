#ifndef GAMERPICTUREPACKDIALOG_H
#define GAMERPICTUREPACKDIALOG_H

// qt
#include <QDialog>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QMessageBox>
#include <QListWidgetItem>
#include <QList>
#include <QStatusBar>
#include <QMenu>
#include <QFileDialog>
#include <QBuffer>
#include <QDebug>
#include <QTimer>
#include "qthelpers.h"

// xbox360
#include <XboxInternals/Stfs/StfsPackage.h>

// other
#include "titleidfinder.h"

namespace Ui
{
class GamerPicturePackDialog;
}

typedef QList<struct Title> QListTile;

class GamerPicturePackDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GamerPicturePackDialog(QStatusBar *statusBar, QWidget *parent = 0);
    ~GamerPicturePackDialog();

private slots:
    void on_pushButton_clicked();
    void onTitleIDSearchReturn(QList<TitleData>);
    void on_listGameNames_itemClicked(QListWidgetItem *item);
    void gamerPictureDownloaded(QNetworkReply *reply);
    void on_listSearch_itemDoubleClicked(QListWidgetItem *item);
    void on_listPack_itemDoubleClicked(QListWidgetItem *item);
    void showContextMenuSearch(QPoint p);
    void showContextMenuPack(QPoint p);
    void on_txtPackName_textChanged(const QString &arg1);
    void on_btnCreatePack_clicked();
    void on_txtSearch_textChanged(const QString &arg1);

    void on_btnStopSearch_clicked();

private:
    struct SearchRequest {
        QString titleID;
        QString displayName;
    };

    Ui::GamerPicturePackDialog *ui;
    TitleIdFinder *titleIDFinder;
    QString currentSearchName;

    QList<TitleData> *currentTitles;
    QList<QString> *searchedIDs;
    QList<QString> *addedIDs;
    QList<u32> *searchedTitleIDs;
    QStatusBar *statusBar;
    QNetworkAccessManager *gpManager;
    
    // Search queue tracking
    QList<SearchRequest> searchQueue;
    bool isSearching;
    int totalRequests;
    int completedRequests;
    int foundPictures;
    QString currentSearchTitleID;
    int totalQueuedSearches;
    int completedSearches;
    int requestsSinceLastFind;
    bool searchTerminatedEarly;

    void findGamerPictures(QString titleID);
    void switchImage(QListWidgetItem *currentItem, QListWidget *current, QListWidget *toAddTo,
            QList<QString> *currentStrs, QList<QString> *toAddToStrs);
    QString getImageName(QString id, bool big);
    void showContextMenu(QPoint p, QListWidget *current, QListWidget *toAddTo,
            QList<QString> *currentStrs, QList<QString> *toAddToStrs);
    bool isValidTitleID(const QString &text);
    void processNextSearch();
};

#endif // GAMERPICTUREPACKDIALOG_H


