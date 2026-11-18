#include "multiprogressdialog.h"
#include "ui_multiprogressdialog.h"
#include <QTimer>
#include <QFileInfo>
#include <QMutexLocker>
#include <memory>

// ExtractionWorker implementation
ExtractionWorker::ExtractionWorker(MultiProgressDialog *dialog)
    : QThread(dialog), dialog_(dialog)
{
}

void ExtractionWorker::run()
{
    // This runs on the worker thread, not the main GUI thread
    try
    {
        for (int i = 0; i < dialog_->internalFiles.size(); i++)
        {
            if (isInterruptionRequested())
            {
                emit extractionComplete(false, "Extraction cancelled by user");
                return;
            }

            dialog_->fileIndex = i;
            dialog_->prevProgress = 0;

            switch (dialog_->system)
            {
                case FileSystemSTFS:
                {
                    StfsExtractEntry *entry = reinterpret_cast<StfsExtractEntry*>(dialog_->internalFiles.at(i));
                    std::unique_ptr<StfsExtractEntry> entryOwner(entry);

                    emit windowTitleUpdate("Extracting " + QString::fromStdString(entry->entry.name));

                    try
                    {
                        // Make all the directories needed
                        QString dirPath = QDir::toNativeSeparators(dialog_->outDir + entry->path).replace("\\", "/");
                        std::string dirPathStd = dirPath.toStdString();

                        if (dialog_->internalFiles.size() != 1)
                        {
                            QDir saveDir(dirPath);
                            if (!saveDir.exists())
                                saveDir.mkpath(dirPath);
                            dirPathStd += entry->entry.name;
                        }
                        else if (QFileInfo(dirPath).isDir())
                        {
                            // The file dialog can return an existing directory
                            // for a single-file extraction; write the file
                            // inside it instead of failing with "is a directory".
                            dirPathStd += "/" + entry->entry.name;
                        }

                        StfsPackage *package = reinterpret_cast<StfsPackage*>(dialog_->device);
                        package->ExtractFile(&entry->entry, dirPathStd, updateProgress, dialog_);
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
                        io.SaveFile(dialog_->outDir.toStdString() + entry->name, updateProgress, dialog_);
                        io.Close();
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
                        QString filePath;

                        if (i < dialog_->relativePaths.size())
                        {
                            // The caller already chose the local layout.
                            QString relative = dialog_->relativePaths.at(i);
                            relative.replace('\\', '/');
                            filePath = dialog_->outDir + relative;

                            const QString dirPath = filePath.left(filePath.lastIndexOf('/'));
                            QDir().mkpath(dirPath);
                        }
                        else
                        {
                            QString temp = QString::fromStdString(entry->path);
                            // FATX paths use '\\', which is not a separator outside Windows
                            QString dirPath = QDir::toNativeSeparators(dialog_->outDir +
                                    temp.replace(dialog_->rootPath, "").replace('\\', '/'));
                            QDir saveDir(dirPath);

                            if (!saveDir.exists())
                                saveDir.mkpath(dirPath);

                            filePath = dirPath + QString::fromStdString(entry->name);
                        }

                        io.SaveFile(filePath.toStdString(), updateProgress, dialog_);
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
    if (worker_)
    {
        worker_->requestInterruption();
        worker_->wait(5000);  // Wait up to 5 seconds
        delete worker_;
    }
    delete ui;
}

void MultiProgressDialog::start()
{
    // calculate the total overall progress
    switch(system)
    {
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
        
        worker_->start();
    }
    else
    {
        // For other operations (inject, etc.), use original synchronous approach
        operateOnNextFile();
    }
}

void MultiProgressDialog::onProgressUpdate(int fileIndex, DWORD curProgress, DWORD total)
{
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
    // File completed, progress update already handled
}

void MultiProgressDialog::onExtractionComplete(bool success, QString errorMessage)
{
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
        // Use QTimer to close the dialog after returning from the callback
        // This prevents crashes when closing during progress updates
        QTimer::singleShot(0, this, &MultiProgressDialog::close);
        return;
    }
    prevProgress = 0;

    switch(system)
    {
        case FileSystemSTFS:
        {
            // get the file entry
            StfsExtractEntry *entry = reinterpret_cast<StfsExtractEntry*>(internalFiles.at(fileIndex++));
            std::unique_ptr<StfsExtractEntry> entryOwner(entry);

            setWindowTitle("Extracting " + QString::fromStdString(entry->entry.name));

            try
            {
                // make all the directories needed
                QString dirPath = QDir::toNativeSeparators(outDir + entry->path).replace("\\", "/");
                std::string dirPathStd = dirPath.toStdString();

                if (internalFiles.size() != 1)
                {
                    QDir saveDir(dirPath);

                    if (!saveDir.exists())
                        saveDir.mkpath(dirPath);

                    dirPathStd += entry->entry.name;
                }
                else if (QFileInfo(dirPath).isDir())
                {
                    dirPathStd += "/" + entry->entry.name;
                }

                StfsPackage *package = reinterpret_cast<StfsPackage*>(device);
                package->ExtractFile(&entry->entry, dirPathStd, updateProgress, this);
            }
            catch (string error)
            {
                QMessageBox::critical(this, "",
                        "An error occurred while extracting files.\n\n" + QString::fromStdString(error));
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
            SvodIO io = svod->GetSvodIO(*entry);

            try
            {
                // extract the file
                io.SaveFile(outDir.toStdString() + entry->name, updateProgress, this);
                io.Close();
            }
            catch (string error)
            {
                QMessageBox::critical(this, "",
                        "An error occurred while extracting files.\n\n" + QString::fromStdString(error));
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
                        // make all the directories needed
                        QString temp = QString::fromStdString(entry->path);
                        // FATX paths use '\\', which is not a separator outside Windows
                        QString dirPath = QDir::toNativeSeparators(outDir +
                                temp.replace(rootPath, "").replace('\\', '/'));
                        QDir saveDir(dirPath);

                        if (!saveDir.exists())
                            saveDir.mkpath(dirPath);

                        // extract the file
                        io.SaveFile(dirPath.toStdString() + entry->name, updateProgress, this);
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
            }

            // cleanup
            close();
            return;
        }
    }
}

void updateProgress(void *form, DWORD curProgress, DWORD total)
{
    // get the dialog back
    MultiProgressDialog *dialog = reinterpret_cast<MultiProgressDialog*>(form);

    // If worker thread exists, emit signals for thread-safe UI updates
    if (dialog->worker_ && QThread::currentThread() == dialog->worker_)
    {
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


