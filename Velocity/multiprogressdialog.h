#ifndef MULTIPROGRESSDIALOG_H
#define MULTIPROGRESSDIALOG_H

// qt
#include <QDialog>
#include <QMessageBox>
#include <QDir>
#include <QThread>
#include <QMutex>
#include "qthelpers.h"

// xbox
#include <XboxInternals/Stfs/XContentHeader.h>
#include <XboxInternals/Disc/Svod.h>
#include <XboxInternals/IO/SvodIO.h>
#include <XboxInternals/Stfs/StfsPackage.h>
#include <XboxInternals/Fatx/FatxConstants.h>
#include <XboxInternals/Fatx/FatxDrive.h>

enum Operation
{
    OpExtract,
    OpReplace,
    OpInject,
    OpBackup,
    OpRestore
};

struct StfsExtractEntry
{
    StfsFileEntry *entry;
    QString path;
};

namespace Ui
{
class MultiProgressDialog;
}

void updateProgress(void *form, DWORD curProgress, DWORD total);

// Forward declaration
class MultiProgressDialog;

// Worker thread for extraction operations
class ExtractionWorker : public QThread
{
    Q_OBJECT

public:
    ExtractionWorker(MultiProgressDialog *dialog);
    void run() override;

signals:
    void progressUpdate(int fileIndex, DWORD curProgress, DWORD total);
    void fileComplete(int fileIndex);
    void extractionComplete(bool success, QString errorMessage);
    void windowTitleUpdate(QString title);
    void groupBoxTitleUpdate(QString title);

private:
    MultiProgressDialog *dialog_;
};

class MultiProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MultiProgressDialog(Operation op, FileSystem fileSystem, void *device, QString outDir,
            QList<void*> internalFiles, QWidget *parent = 0, QString rootPath = "",
            FatxFileEntry *parentEntry = nullptr, QStringList relativePaths = QStringList());
    ~MultiProgressDialog();

    void start();

private slots:
    void onProgressUpdate(int fileIndex, DWORD curProgress, DWORD total);
    void onFileComplete(int fileIndex);
    void onExtractionComplete(bool success, QString errorMessage);
    void onWindowTitleUpdate(QString title);
    void onGroupBoxTitleUpdate(QString title);

private:
    Ui::MultiProgressDialog *ui;
    FileSystem system;
    void *device;
    QString outDir;
    QList<void*> internalFiles;
    int fileIndex;
    DWORD overallProgress;
    DWORD overallProgressTotal;
    DWORD prevProgress;
    QString rootPath;
    // Parallel to internalFiles when the caller decides the local layout itself;
    // empty means "derive it from rootPath" (the historical behaviour).
    QStringList relativePaths;
    Operation op;
    FatxFileEntry *parentEntry;
    QMutex progressMutex_;  // Protect progress updates from multiple threads
    ExtractionWorker *worker_;

    void operateOnNextFile();

    friend void updateProgress(void *form, DWORD curProgress, DWORD total);
    friend class ExtractionWorker;
};

#endif // MULTIPROGRESSDIALOG_H


