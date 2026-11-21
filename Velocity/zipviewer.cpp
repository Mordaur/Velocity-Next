#include "zipviewer.h"
#include "ui_zipviewer.h"
#include <QtCore/private/qzipreader_p.h>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <XboxInternals/Utils.h>

ZipViewer::ZipViewer(const QString &zipPath, const QString &fileName, QWidget *parent)
    : QDialog(parent), ui(new Ui::ZipViewer), zipPath(zipPath)
{
    ui->setupUi(this);
    setWindowTitle("ZIP Viewer - " + fileName);
    
    // Set up table
    ui->tableWidget->setColumnCount(3);
    ui->tableWidget->setHorizontalHeaderLabels({"File Path", "Size", "Compressed"});
    ui->tableWidget->horizontalHeader()->setStretchLastSection(false);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget->setSortingEnabled(true);
    
    // Connect buttons
    connect(ui->btnExtractSelected, &QPushButton::clicked, this, &ZipViewer::onExtractSelected);
    connect(ui->btnExtractAll, &QPushButton::clicked, this, &ZipViewer::onExtractAll);
    connect(ui->btnClose, &QPushButton::clicked, this, &QDialog::close);
    
    loadZipContents();
}

ZipViewer::~ZipViewer()
{
    delete ui;
}

void ZipViewer::loadZipContents()
{
    QZipReader zip(zipPath);
    
    if (!zip.exists())
    {
        QMessageBox::warning(this, "Error", "Unable to open ZIP file.");
        return;
    }
    
    // Disable sorting while populating
    ui->tableWidget->setSortingEnabled(false);
    
    QList<QZipReader::FileInfo> files = zip.fileInfoList();
    ui->tableWidget->setRowCount(files.size());
    
    int row = 0;
    for (const auto &file : files)
    {
        // Skip directories
        if (file.isDir)
        {
            ui->tableWidget->setRowCount(ui->tableWidget->rowCount() - 1);
            continue;
        }
        
        // File path
        QTableWidgetItem *pathItem = new QTableWidgetItem(file.filePath);
        ui->tableWidget->setItem(row, 0, pathItem);
        
        // Uncompressed size
        QString sizeStr = QString::number(file.size) + " bytes";
        if (file.size > 1024 * 1024)
            sizeStr = QString::number(file.size / 1024.0 / 1024.0, 'f', 2) + " MB";
        else if (file.size > 1024)
            sizeStr = QString::number(file.size / 1024.0, 'f', 2) + " KB";
        
        NumericSizeItem *sizeItem = new NumericSizeItem(sizeStr, file.size);
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sizeItem->setData(Qt::UserRole, static_cast<qulonglong>(file.size)); // Store numeric value for sorting
        ui->tableWidget->setItem(row, 1, sizeItem);
        
        // Compressed indicator
        QTableWidgetItem *compItem = new QTableWidgetItem(file.isFile ? "Yes" : "-");
        compItem->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget->setItem(row, 2, compItem);
        
        row++;
    }
    
    ui->labelInfo->setText(QString("Total files: %1").arg(row));
    
    // Re-enable sorting after populating
    ui->tableWidget->setSortingEnabled(true);
    
    zip.close();
}

void ZipViewer::onExtractSelected()
{
    QList<QTableWidgetItem*> selected = ui->tableWidget->selectedItems();
    if (selected.isEmpty())
    {
        QMessageBox::information(this, "No Selection", "Please select files to extract.");
        return;
    }
    
    QString outputDir = QFileDialog::getExistingDirectory(this, "Select Output Directory");
    if (outputDir.isEmpty())
        return;
    
    QZipReader zip(zipPath);
    int extractedCount = 0;
    
    // Get unique rows
    QSet<int> rows;
    for (auto *item : selected)
        rows.insert(item->row());
    
    for (int row : rows)
    {
        QString filePath = ui->tableWidget->item(row, 0)->text();
        QByteArray data = zip.fileData(filePath);
        
        const QString name = QFileInfo(filePath).fileName();
        if (name.isEmpty() || name == "." || name == "..")
            continue;

        QString outputPath = outputDir + "/" + name;
        QFile outFile(outputPath);
        if (outFile.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
            outFile.write(data) == data.size())
        {
            outFile.close();
            extractedCount++;
        }
    }
    
    zip.close();
    QMessageBox::information(this, "Extraction Complete", 
                            QString("Extracted %1 file(s) to:\n%2").arg(extractedCount).arg(outputDir));
}

void ZipViewer::onExtractAll()
{
    QString outputDir = QFileDialog::getExistingDirectory(this, "Select Output Directory");
    if (outputDir.isEmpty())
        return;

    // QZipReader::extractAll only strips leading "./" and "/" and also creates
    // symlinks, so a crafted entry can escape the target directory. Validate
    // every entry and write it ourselves.
    QZipReader zip(zipPath);
    constexpr qint64 kMaxTotalSize = 16LL * 1024 * 1024 * 1024; // 16 GiB
    qint64 totalSize = 0;
    bool failed = false;

    const QList<QZipReader::FileInfo> files = zip.fileInfoList();
    for (const QZipReader::FileInfo &info : files)
    {
        if (info.isSymLink)
            continue;

        std::string target;
        if (!Utils::SafeJoin(outputDir.toStdString(), info.filePath.toStdString(), target))
        {
            QMessageBox::warning(this, "Error",
                    "Refusing to extract an unsafe path:\n" + info.filePath);
            failed = true;
            break;
        }

        const QString targetPath = QString::fromStdString(target);
        if (info.isDir)
        {
            if (!QDir().mkpath(targetPath))
            {
                QMessageBox::warning(this, "Error", "Could not create:\n" + targetPath);
                failed = true;
                break;
            }
            continue;
        }

        if (info.size > 0)
            totalSize += info.size;
        if (totalSize > kMaxTotalSize)
        {
            QMessageBox::warning(this, "Error",
                    "Archive would expand to more than 16 GiB; extraction aborted.");
            failed = true;
            break;
        }

        const QString parent = QFileInfo(targetPath).path();
        if (!parent.isEmpty() && !QDir().mkpath(parent))
        {
            QMessageBox::warning(this, "Error", "Could not create:\n" + parent);
            failed = true;
            break;
        }

        const QByteArray data = zip.fileData(info.filePath);
        QFile outFile(targetPath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            outFile.write(data) != data.size())
        {
            QMessageBox::warning(this, "Error", "Could not write:\n" + targetPath);
            failed = true;
            break;
        }
    }

    zip.close();
    if (failed)
        return;

    QMessageBox::information(this, "Extraction Complete",
                            QString("All files extracted to:\n%1").arg(outputDir));
}
