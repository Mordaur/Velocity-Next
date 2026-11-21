#include "multiprogressdialog.h"
#include "ui_multiprogressdialog.h"
#include <QCloseEvent>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <XboxInternals/Utils.h>
#include <exception>
#include <memory>
#include <utility>

namespace
{
    // Thrown from the progress callback so a long copy stops at the next
    // progress tick instead of running to completion after a cancel request.
    struct ExtractionCancelled
    {
    };

    // Run a cleanup on every exit path, including the early `return`s below.
    template <typename Fn>
    struct ScopeExit
    {
        Fn fn;
        ~ScopeExit() { fn(); }
    };

    template <typename Fn>
    ScopeExit<Fn> makeScopeExit(Fn fn)
    {
        return ScopeExit<Fn>{std::move(fn)};
    }

    std::string SafeExtractTarget(const QString &outDir, const QString &relative, const char *what)
    {
        std::string target;
        if (!Utils::SafeJoin(outDir.toStdString(), relative.toStdString(), target))
            throw std::string(what) + ": Unsafe extraction path.\n";

        const QString parent = QFileInfo(QString::fromStdString(target)).path();
        if (!parent.isEmpty())
            QDir().mkpath(parent);
        return target;
    }

    QString FatxRelativePath(const QString &rootPath, const std::string &path,
            const std::string &name)
    {
        QString relative = QString::fromStdString(path);
        if (!rootPath.isEmpty() && relative.startsWith(rootPath))
            relative = relative.mid(rootPath.size());
        relative.replace('\\', '/');
        while (relative.startsWith('/'))
            relative.remove(0, 1);
        if (!relative.isEmpty() && !relative.endsWith('/'))
            relative += '/';
        relative += QString::fromStdString(name);
        return relative;
    }
}

// ExtractionWorker implementation
ExtractionWorker::ExtractionWorker(MultiProgressDialog *dialog)
    : QThread(dialog), dialog_(dialog)
{
}

void ExtractionWorker::run()
{
    // This runs on the worker thread, not the main GUI thread
    const bool ownsEntries = dialog_->system == FileSystemSTFS;
    int deleteFrom = 0;
    auto cleanupRemaining = makeScopeExit([&]() {
        if (!ownsEntries)
            return;
        for (int j = deleteFrom; j < dialog_->internalFiles.size(); ++j)
            delete reinterpret_cast<StfsExtractEntry *>(dialog_->internalFiles.at(j));
    });
    (void)cleanupRemaining;

    try
    {
        for (int i = 0; i < dialog_->internalFiles.size(); i++)
        {
            // Entry i is not owned yet. The STFS branch moves it into a
            // unique_ptr and then advances deleteFrom; entries at or after
            // deleteFrom must be released if an error or cancel returns early.
            deleteFrom = ownsEntries ? i : 0;

            if (isInterruptionRequested())
            {
                emit extractionComplete(false, "Extraction cancelled by user");
                return;
            }

            dialog_->fileIndex = i;
            dialog_->prevProgress = 0;

            switch (dialog_->system)
            {
            default: break;
                case FileSystemSTFS:
                {
                    StfsExtractEntry *entry = reinterpret_cast<StfsExtractEntry*>(dialog_->internalFiles.at(i));
                    std::unique_ptr<StfsExtractEntry> entryOwner(entry);
                    deleteFrom = i + 1;

                    emit windowTitleUpdate("Extracting " + QString::fromStdString(entry->entry.name));

                    try
                    {
                        std::string target;
                        if (dialog_->internalFiles.size() != 1)
                        {
                            target = SafeExtractTarget(dialog_->outDir,
                                    entry->path + QString::fromStdString(entry->entry.name), "STFS");
                        }
                        else if (QFileInfo(dialog_->outDir).isDir())
                        {
                            // The file dialog can return an existing directory
                            // for a single-file extraction; write the file
                            // inside it instead of failing with "is a directory".
                            target = SafeExtractTarget(dialog_->outDir,
                                    QString::fromStdString(entry->entry.name), "STFS");
                        }
                        else
                        {
                            // The user chose the full file path in the dialog.
                            target = dialog_->outDir.toStdString();
                        }

                        StfsPackage *package = reinterpret_cast<StfsPackage*>(dialog_->device);
                        package->ExtractFile(&entry->entry, target, updateProgress, dialog_);
                    }
                    catch (const ExtractionCancelled&)
                    {
                        emit extractionComplete(false, QStringLiteral("Extraction cancelled by user"));
                        return;
                    }
                    catch (const std::exception& e)
                    {
                        emit extractionComplete(false, 
                            QString("Error extracting '%1': %2")
                                .arg(QString::fromStdString(entry->entry.name))
                                .arg(e.what()));
                        return;
                    }
                    catch (string error)
                    {
                        emit extractionComplete(false, 
                            QString("Error extracting '%1': %2")
                                .arg(QString::fromStdString(entry->entry.name))
                                .arg(QString::fromStdString(error)));
                        return;
                    }
                    catch (...)
                    {
                        emit extractionComplete(false, 
                            QString("Unknown error extracting '%1'")
                                .arg(QString::fromStdString(entry->entry.name)));
                        return;
                    }
                    break;
                }
                case FileSystemSVOD:
                {
                    GdfxFileEntry *entry = reinterpret_cast<GdfxFileEntry*>(dialog_->internalFiles.at(i));
                    
                    emit windowTitleUpdate("Extracting " + QString::fromStdString(entry->name));

                    SVOD *svod = reinterpret_cast<SVOD*>(dialog_->device);
                    SvodIO io = svod->GetSvodIO(*entry);

                    try
                    {
                        const std::string target = SafeExtractTarget(dialog_->outDir,
                            QString::fromStdString(entry->name), "SVOD");
                        io.SaveFile(target, updateProgress, dialog_);
                        io.Close();
                    }
                    catch (const ExtractionCancelled&)
                    {
                        emit extractionComplete(false, QStringLiteral("Extraction cancelled by user"));
                        return;
                    }
                    catch (string error)
                    {
                        emit extractionComplete(false, 
                            QString("Error extracting '%1': %2")
                                .arg(QString::fromStdString(entry->name))
                                .arg(QString::fromStdString(error)));
                        return;
                    }
                    break;
                }
                case FileSystemFATX:
                {
                    emit groupBoxTitleUpdate("Overall Progress - " + QString::number(i + 1) + " of " + 
                                           QString::number(dialog_->internalFiles.size()));

                    FatxFileEntry *entry = reinterpret_cast<FatxFileEntry*>(dialog_->internalFiles.at(i));

                    if (entry->fileAttributes & FatxDirectory)
                        continue;

                    emit windowTitleUpdate("Copying " + QString::fromStdString(entry->name));

                    FatxDrive *drive = reinterpret_cast<FatxDrive*>(dialog_->device);
                    FatxIO io = drive->GetFatxIO(entry);

                    try
                    {
                        std::string target;
                        if (i < dialog_->relativePaths.size())
                        {
                            // The caller already chose the local layout.
                            QString relative = dialog_->relativePaths.at(i);
                            relative.replace('\\', '/');
                            target = SafeExtractTarget(dialog_->outDir, relative, "FATX");
                        }
                        else
                        {
                            target = SafeExtractTarget(dialog_->outDir,
                                    FatxRelativePath(dialog_->rootPath, entry->path, entry->name),
                                    "FATX");
                        }

                        io.SaveFile(target, updateProgress, dialog_);
                    }
                    catch (const ExtractionCancelled&)
                    {
                        emit extractionComplete(false, QStringLiteral("Extraction cancelled by user"));
                        return;
                    }
                    catch (string error)
                    {
                        emit extractionComplete(false, 
                            QString("Error copying '%1': %2")
                                .arg(QString::fromStdString(entry->name))
                                .arg(QString::fromStdString(error)));
                        return;
                    }

                    dialog_->prevProgress = 0;
                    break;
                }
            }
        }

        emit extractionComplete(true, "");
    }
    catch (const ExtractionCancelled&)
    {
        emit extractionComplete(false, QStringLiteral("Extraction cancelled by user"));
    }
    catch (const std::exception& e)
    {
        emit extractionComplete(false, QString("Extraction failed: %1").arg(e.what()));
    }
    catch (...)
    {
        emit extractionComplete(false, "Unknown extraction error occurred");
    }
}

MultiProgressDialog::MultiProgressDialog(Operation op, FileSystem fileSystem, void *device,
        QString outDir, QList<void *> internalFiles, QWidget *parent, QString rootPath,
        FatxFileEntry *parentEntry, QStringList relativePaths) :
    QDialog(parent),ui(new Ui::MultiProgressDialog), system(fileSystem), device(device), outDir(outDir),
    internalFiles(internalFiles),
    fileIndex(0), overallProgress(0), overallProgressTotal(0), prevProgress(0), rootPath(rootPath),
    relativePaths(relativePaths), op(op), parentEntry(parentEntry), worker_(nullptr)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    ui->setupUi(this);
    QtHelpers::GenAdjustWidgetAppearanceToOS(this);

    switch (op)
    {
        case OpExtract:
            ui->lblIcon->setPixmap(QPixmap(":/Images/extract.png"));
            break;
        case OpReplace:
            break;
        case OpInject:
            ui->lblIcon->setPixmap(QPixmap(":/Images/inject.png"));
            break;
        case OpBackup:
            break;
        case OpRestore:
            break;
    }
}

MultiProgressDialog::~MultiProgressDialog()
{
    // Never delete a QThread that is still running. cancelAndWait() asks the
    // worker to stop at its next progress tick; without that request a single
    // large file could keep it busy past any fixed timeout.
    cancelAndWait();
    delete worker_;
    worker_ = nullptr;
    delete ui;
}

void MultiProgressDialog::cancelAndWait()
{
    if (worker_ == nullptr || !worker_->isRunning())
        return;

    worker_->requestInterruption();
    worker_->wait();
}

void MultiProgressDialog::closeEvent(QCloseEvent *event)
{
    if (!workerFinished_ && worker_ != nullptr && worker_->isRunning())
    {
        // The caller's device must outlive the worker; finish first, then let
        // the finished() handler close and delete this dialog.
        worker_->requestInterruption();
        event->ignore();
        return;
    }

    QDialog::closeEvent(event);
}

void cancelProgressDialogs(QWidget *owner)
{
    const QList<MultiProgressDialog *> dialogs = owner->findChildren<MultiProgressDialog *>();
    for (MultiProgressDialog *dialog : dialogs)
        dialog->cancelAndWait();
}

void MultiProgressDialog::start()
{
    // calculate the total overall progress
    switch(system)
    {
            default: break;
        case FileSystemSTFS:
            for (int i = 0; i < internalFiles.size(); i++)
            {
                StfsExtractEntry *entry = reinterpret_cast<StfsExtractEntry*>(internalFiles.at(i));
                if (entry->entry.blocksForFile == 0)
                    overallProgressTotal++;
                else
                    overallProgressTotal += entry->entry.blocksForFile;
            }
            break;
        case FileSystemSVOD:
            for (int i = 0; i < internalFiles.size(); i++)
            {
                GdfxFileEntry *entry = reinterpret_cast<GdfxFileEntry*>(internalFiles.at(i));
                overallProgressTotal += (entry->size + 0xFFFF) / 0x10000;
            }
            break;
        case FileSystemFATX:
            overallProgressTotal = 0;
            ui->progressBar_2->setTextVisible(false);
            break;
    }

    ui->progressBar_2->setMaximum(overallProgressTotal);
    
    // For extraction operations, use worker thread
    if (op == OpExtract)
    {
        worker_ = new ExtractionWorker(this);
        
        // Connect signals from worker to UI update slots
        connect(worker_, &ExtractionWorker::progressUpdate, this, &MultiProgressDialog::onProgressUpdate);
        connect(worker_, &ExtractionWorker::fileComplete, this, &MultiProgressDialog::onFileComplete);
        connect(worker_, &ExtractionWorker::extractionComplete, this, &MultiProgressDialog::onExtractionComplete);
        connect(worker_, &ExtractionWorker::windowTitleUpdate, this, &MultiProgressDialog::onWindowTitleUpdate);
        connect(worker_, &ExtractionWorker::groupBoxTitleUpdate, this, &MultiProgressDialog::onGroupBoxTitleUpdate);

        // extractionComplete() is emitted from inside run(), so the thread may
        // still be alive when it arrives. finished() is the only safe point to
        // close and free the dialog, and it also covers cancel-on-close.
        connect(worker_, &QThread::finished, this, [this]() {
            workerFinished_ = true;
            close();
            deleteLater();
        });

        worker_->start();
    }
    else
    {
        // For other operations (inject, etc.), use original synchronous approach
        operateOnNextFile();
    }
}

void MultiProgressDialog::onProgressUpdate(int fileIndex, u32 curProgress, u32 total)
{
    (void)fileIndex;
    QMutexLocker locker(&progressMutex_);
    
    ui->progressBar->setValue(curProgress);
    ui->progressBar->setMaximum(total);

    if (system != FileSystemFATX)
    {
        overallProgress += (curProgress - prevProgress);
        ui->progressBar_2->setValue(overallProgress);
        prevProgress = curProgress;
    }
}

void MultiProgressDialog::onFileComplete(int fileIndex)
{
    (void)fileIndex;
    // File completed, progress update already handled
}

void MultiProgressDialog::onExtractionComplete(bool success, QString errorMessage)
{
    if (!success && errorMessage == QStringLiteral("Extraction cancelled by user"))
        return;  // finished() closes the dialog; a cancel is not an error

    if (success)
    {
        close();
    }
    else
    {
        QMessageBox::critical(this, "Extraction Error", 
            "An error occurred while extracting files.\n\n" + errorMessage);
        close();
    }
}

void MultiProgressDialog::onWindowTitleUpdate(QString title)
{
    setWindowTitle(title);
}

void MultiProgressDialog::onGroupBoxTitleUpdate(QString title)
{
    ui->groupBox_2->setTitle(title);
}

void MultiProgressDialog::operateOnNextFile()
{
    // make sure there's another file to extract
    if (fileIndex == internalFiles.size())
    {
        // Use QTimer to close and free the dialog after returning from the
        // callback; deleting it inline would invalidate the caller's frame.
        QTimer::singleShot(0, this, [this]() {
            close();
            deleteLater();
        });
        return;
    }
    prevProgress = 0;

    switch(system)
    {
            default: break;
        case FileSystemSTFS:
        {
            // get the file entry
            StfsExtractEntry *entry = reinterpret_cast<StfsExtractEntry*>(internalFiles.at(fileIndex++));
            std::unique_ptr<StfsExtractEntry> entryOwner(entry);

            setWindowTitle("Extracting " + QString::fromStdString(entry->entry.name));

            try
            {
                std::string target;
                if (internalFiles.size() != 1)
                {
                    target = SafeExtractTarget(outDir,
                            entry->path + QString::fromStdString(entry->entry.name), "STFS");
                }
                else if (QFileInfo(outDir).isDir())
                {
                    target = SafeExtractTarget(outDir,
                            QString::fromStdString(entry->entry.name), "STFS");
                }
                else
                {
                    // The user chose the full file path in the dialog.
                    target = outDir.toStdString();
                }

                StfsPackage *package = reinterpret_cast<StfsPackage*>(device);
                package->ExtractFile(&entry->entry, target, updateProgress, this);
            }
            catch (string error)
            {
                QMessageBox::critical(this, "",
                        "An error occurred while extracting files.\n\n" + QString::fromStdString(error));
            }
            catch (const std::exception &error)
            {
                QMessageBox::critical(this, "",
                        "An error occurred while extracting files.\n\n" + QString::fromUtf8(error.what()));
            }
            catch (...)
            {
                QMessageBox::critical(this, "",
                        "An unexpected error occurred while extracting files.");
            }

            break;
        }
        case FileSystemSVOD:
        {
            if (op != OpExtract)
                throw string("MultiProgressDialog: Invalid operation for file system.\n");

            // get the file entry
            GdfxFileEntry *entry = reinterpret_cast<GdfxFileEntry*>(internalFiles.at(fileIndex++));

            setWindowTitle("Extracting " + QString::fromStdString(entry->name));

            // get the file from the device
            SVOD *svod = reinterpret_cast<SVOD*>(device);

            try
            {
                SvodIO io = svod->GetSvodIO(*entry);

                // extract the file
                const std::string target = SafeExtractTarget(outDir,
                        QString::fromStdString(entry->name), "SVOD");
                io.SaveFile(target, updateProgress, this);
                io.Close();
            }
            catch (string error)
            {
                QMessageBox::critical(this, "",
                        "An error occurred while extracting files.\n\n" + QString::fromStdString(error));
            }
            catch (const std::exception &error)
            {
                QMessageBox::critical(this, "",
                        "An error occurred while extracting files.\n\n" + QString::fromUtf8(error.what()));
            }
            catch (...)
            {
                QMessageBox::critical(this, "",
                        "An unexpected error occurred while extracting files.");
            }

            break;
        }
        case FileSystemFATX:
        {
            if (op == OpExtract)
            {
                for (int i = 0; i < internalFiles.size(); i++)
                {
                    // update groupbox text
                    ui->groupBox_2->setTitle("Overall Progress - " + QString::number(i + 1) + " of " + QString::number(
                                internalFiles.size()));

                    // get the file entry
                    FatxFileEntry *entry = reinterpret_cast<FatxFileEntry*>(internalFiles.at(i));

                    if (entry->fileAttributes & FatxDirectory)
                        continue;

                    setWindowTitle("Copying " + QString::fromStdString(entry->name));

                    // get the file from the device
                    FatxDrive *drive = reinterpret_cast<FatxDrive*>(device);
                    FatxIO io = drive->GetFatxIO(entry);

                    try
                    {
                        // extract the file
                        const std::string target = SafeExtractTarget(outDir,
                                FatxRelativePath(rootPath, entry->path, entry->name), "FATX");
                        io.SaveFile(target, updateProgress, this);
                    }
                    catch (string error)
                    {
                        QMessageBox::critical(this, "",
                                "An error occurred while extracting files.\n\n" + QString::fromStdString(error));
                    }

                    // reset the progress
                    prevProgress = 0;
                }
            }
            else if (op == OpInject)
            {
                try
                {
                    for (int i = 0; i < internalFiles.size(); i++)
                    {
                        // update groupbox text
                        ui->groupBox_2->setTitle("Overall Progress - " + QString::number(i + 1) + " of " + QString::number(
                                    internalFiles.size()));

                        // get the file from the device
                        FatxDrive *drive = reinterpret_cast<FatxDrive*>(device);

                        QString *fileName = reinterpret_cast<QString*>(internalFiles.at(i));
                        QFileInfo fileInfo(*fileName);
                        QString cleanName = *fileName;

                        setWindowTitle("Copying " + fileInfo.fileName());

                        // set up the parent entry
                        FatxFileEntry *pEntry = parentEntry;
                        if (rootPath != "")
                        {
                            // fix the path separators
                            rootPath = rootPath.replace("/", "\\");
                            *fileName = fileName->replace("/", "\\");

                            // get the FATX file path
                            QString fatxPath = QString::fromStdString(parentEntry->path + parentEntry->name) +
                                    fileName->replace(rootPath, "").mid(0, fileName->replace(rootPath, "").lastIndexOf("\\"));
                            pEntry = drive->CreatePath(fatxPath.toStdString());
                        }

                        // check if the file already exists
                        if (drive->FileExists(pEntry, fileInfo.fileName().toStdString()))
                        {
                            int button = QMessageBox::question(this, "File Already Exists", "The file " + fileInfo.fileName() +
                                    " already exists in this directory. Would you like to replace the current one?",
                                    QMessageBox::Yes, QMessageBox::No);

                            if (button == QMessageBox::Yes)
                            {
                                FatxIO file = drive->GetFatxIO(drive->GetFileEntry(pEntry->path + pEntry->name + "\\" +
                                        fileInfo.fileName().toStdString()));
                                file.ReplaceFile(cleanName.toStdString(), updateProgress, this);
                            }
                        }
                        else
                        {
                            drive->InjectFile(pEntry, fileInfo.fileName().toStdString(), cleanName.toStdString(),
                                    updateProgress, this);
                        }


                        // reset the progress
                        prevProgress = 0;
                    }
                }
                catch (string error)
                {
                    QMessageBox::critical(this, "",
                            "An error occurred while copying files to the drive.\n\n" + QString::fromStdString(error));
                }
                catch (const std::exception &error)
                {
                    QMessageBox::critical(this, "",
                            "An error occurred while copying files to the drive.\n\n" +
                            QString::fromUtf8(error.what()));
                }
                catch (...)
                {
                    QMessageBox::critical(this, "",
                            "An unexpected error occurred while copying files to the drive.");
                }
            }

            // cleanup
            QTimer::singleShot(0, this, [this]() {
                close();
                deleteLater();
            });
            return;
        }
    }
}

void updateProgress(void *form, u32 curProgress, u32 total)
{
    // get the dialog back
    MultiProgressDialog *dialog = reinterpret_cast<MultiProgressDialog*>(form);

    // If worker thread exists, emit signals for thread-safe UI updates
    if (dialog->worker_ && QThread::currentThread() == dialog->worker_)
    {
        // A cancel request must not wait for the current file to finish; unwind
        // the extraction and let ExtractionWorker::run() report the cancel.
        if (dialog->worker_->isInterruptionRequested())
            throw ExtractionCancelled();

        // We're on the worker thread - use signals
        emit dialog->worker_->progressUpdate(dialog->fileIndex, curProgress, total);
        
        if (curProgress == total && dialog->system != FileSystemFATX)
        {
            emit dialog->worker_->fileComplete(dialog->fileIndex);
        }
    }
    else
    {
        // We're on the main thread (old synchronous path for inject operations)
        dialog->ui->progressBar->setValue(curProgress);
        dialog->ui->progressBar->setMaximum(total);

        if (dialog->system != FileSystemFATX)
        {
            dialog->overallProgress += (curProgress - dialog->prevProgress);
            dialog->ui->progressBar_2->setValue(dialog->overallProgress);
            dialog->prevProgress = curProgress;

            if (curProgress == total)
                dialog->operateOnNextFile();
        }

        // Process events for non-threaded operations
        if (curProgress == total)
        {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }
}


