#include "isodialog.h"
#include "imagedialog.h"
#include "textdialog.h"
#include "xmldialog.h"
#include "packageviewer.h"
#include "isosectordialog.h"
#include "xexdialog.h"
#include "qthelpers.h"
#include "TextEncoding/EncodingDetector.h"
#include "XboxInternals/IO/IsoIO.h"
#include "XboxInternals/Stfs/StfsPackage.h"
#include "XboxInternals/Stfs/StfsConstants.h"
#include <QPushButton>
#include <QTreeWidget>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMessageBox>
#include <QMenu>
#include <QHeaderView>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QStatusBar>
#include <QPixmap>
#include <QBuffer>
#include <QStringDecoder>
#include <QStringConverter>
#include <QProgressDialog>
#include <QTimer>
#include <QRegularExpression>
#include <QApplication>
#include <QtConcurrent/QtConcurrent>
#include <fstream>
#include <memory>
#include "XboxInternals/Iso/IsoImage.h"
#include "XboxInternals/Disc/Gdfx.h"

// Helper function to format file sizes
static QString formatSize(quint64 size) {
    const quint64 KB = 1024;
    const quint64 MB = KB * 1024;
    const quint64 GB = MB * 1024;
    
    if (size >= GB) {
        return QString::number(size / static_cast<double>(GB), 'f', 2) + " GB";
    } else if (size >= MB) {
        return QString::number(size / static_cast<double>(MB), 'f', 2) + " MB";
    } else if (size >= KB) {
        return QString::number(size / static_cast<double>(KB), 'f', 2) + " KB";
    } else {
        return QString::number(size) + " bytes";
    }
}

IsoDialog::IsoDialog(QStatusBar* statusBar, QWidget* parent)
    : QDialog(parent), statusBar_(statusBar)
{
    setWindowTitle(tr("Xbox 360 ISO Browser"));
    setAcceptDrops(true);
    resize(900, 600);

    // Top button row
    extractSelectedButton_ = new QPushButton(tr("Extract Selected"), this);
    extractAllButton_ = new QPushButton(tr("Extract All Files"), this);
    expandAllButton_ = new QPushButton(tr("Expand All"), this);
    collapseAllButton_ = new QPushButton(tr("Collapse All"), this);
    sectorToolButton_ = new QPushButton(tr("Sector Tool"), this);
    
    extractSelectedButton_->setEnabled(false);
    extractAllButton_->setEnabled(false);
    expandAllButton_->setEnabled(false);
    collapseAllButton_->setEnabled(false);
    sectorToolButton_->setEnabled(false);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(extractSelectedButton_);
    buttonLayout->addWidget(extractAllButton_);
    buttonLayout->addStretch();
    buttonLayout->addWidget(sectorToolButton_);
    buttonLayout->addWidget(expandAllButton_);
    buttonLayout->addWidget(collapseAllButton_);

    // Search box
    auto* searchLayout = new QHBoxLayout();
    auto* searchLabel = new QLabel(tr("Search:"), this);
    searchBox_ = new QLineEdit(this);
    searchBox_->setPlaceholderText(tr("Filter files by name..."));
    searchBox_->setClearButtonEnabled(true);
    searchLayout->addWidget(searchLabel);
    searchLayout->addWidget(searchBox_);

    // Tree widget for ISO contents (collapsible hierarchy)
    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({tr("Name"), tr("Size"), tr("Sector"), tr("Address")});
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->header()->setStretchLastSection(true);  // Last column stretches to fill space
    tree_->header()->setSectionResizeMode(QHeaderView::Interactive);  // All columns manually resizable
    tree_->setColumnWidth(0, 300);  // Name column default width
    tree_->setColumnWidth(1, 100);  // Size column
    tree_->setColumnWidth(2, 100);  // Sector column
    tree_->setIndentation(20); // Proper indentation for folders

    // Setup context menu like Gualdimar does
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addLayout(searchLayout);
    mainLayout->addWidget(tree_);

    connect(extractSelectedButton_, &QPushButton::clicked, this, &IsoDialog::onExtractSelected);
    connect(extractAllButton_, &QPushButton::clicked, this, &IsoDialog::onExtractAll);
    connect(expandAllButton_, &QPushButton::clicked, this, &IsoDialog::onExpandAll);
    connect(collapseAllButton_, &QPushButton::clicked, this, &IsoDialog::onCollapseAll);
    connect(sectorToolButton_, &QPushButton::clicked, this, &IsoDialog::onSectorToolClicked);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, &IsoDialog::onItemDoubleClicked);
    connect(searchBox_, &QLineEdit::textChanged, this, &IsoDialog::onSearchTextChanged);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &IsoDialog::showContextMenu);
}

IsoDialog::~IsoDialog() = default;

void IsoDialog::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void IsoDialog::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    for (const QUrl& u : urls) {
        const QString path = u.toLocalFile();
        if (!path.isEmpty() && path.endsWith(".iso", Qt::CaseInsensitive)) {
            loadIsoContents(path);
            break;  // Only load first ISO
        }
    }
}

void IsoDialog::loadIsoContents(const QString& path) {
    tree_->clear();
    folderItems_.clear();
    currentIsoPath_ = path;

    if (!iso_) iso_.reset(new XboxInternals::Iso::IsoImage());
    
    if (!iso_->open(path.toStdString())) {
        QMessageBox::warning(this, tr("Error"), 
            tr("Failed to open ISO file:\n%1\n\nThis could be due to:\n"
               "- File is not a valid Xbox 360 ISO\n"
               "- Magic string not found at expected offsets\n"
               "- File access permissions\n"
               "- Corrupted file").arg(path));
        return;
    }

    populateTree();

    extractSelectedButton_->setEnabled(true);
    extractAllButton_->setEnabled(true);
    expandAllButton_->setEnabled(true);
    collapseAllButton_->setEnabled(true);
    sectorToolButton_->setEnabled(true);

    const auto& info = iso_->info();
    const QString fileName = QFileInfo(path).fileName();
    const QString xgdVersion = QString::fromStdString(iso_->GetXGDVersion());
    const QString sizeStr = formatSize(info.imageSize);
    const QString titleName = QString::fromStdWString(iso_->GetTitleName());

    if (titleName.isEmpty())
    {
        setWindowTitle(tr("Xbox 360 ISO Browser - %1 (%2) - %3")
                       .arg(fileName, xgdVersion, sizeStr));
    }
    else
    {
        setWindowTitle(tr("Xbox 360 ISO Browser - %1 [%2] (%3) - %4")
                       .arg(fileName, titleName, xgdVersion, sizeStr));
    }
    
    if (statusBar_) {
        statusBar_->showMessage(tr("ISO parsed successfully"), 5000);
    }
}

QTreeWidgetItem* IsoDialog::findOrCreateFolder(const QString& path) {
    if (path.isEmpty()) return nullptr;
    
    // Check cache first
    if (folderItems_.contains(path)) {
        return folderItems_[path];
    }
    
    // Split path and create hierarchy
    QStringList parts = path.split('/', Qt::SkipEmptyParts);
    QTreeWidgetItem* parent = nullptr;
    QString currentPath;
    
    for (const QString& part : parts) {
        currentPath += (currentPath.isEmpty() ? "" : "/") + part;
        
        if (folderItems_.contains(currentPath)) {
            parent = folderItems_[currentPath];
        } else {
            QTreeWidgetItem* item;
            if (parent) {
                item = new QTreeWidgetItem(parent);
            } else {
                item = new QTreeWidgetItem(tree_);
            }
            
            item->setText(0, part);
            item->setText(1, tr("Folder"));
            item->setText(2, "");
            item->setData(0, Qt::UserRole, currentPath);
            item->setData(0, Qt::UserRole + 1, true); // Mark as folder
            
            folderItems_[currentPath] = item;
            parent = item;
        }
    }
    
    return parent;
}



void IsoDialog::populateTree() {
    // Get the GDFX root entries directly (already sorted with directories first)
    iso_->GetFileListing();
    
    // Build tree - icons created directly in buildTreeItem like PackageViewer does
    for (const auto& entry : iso_->root) {
        buildTreeItem(tree_, nullptr, entry);
    }
}

void IsoDialog::buildTreeItem(QTreeWidget* tree, QTreeWidgetItem* parent, 
                              const GdfxFileEntry& entry) {
    QTreeWidgetItem* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    
    item->setText(0, QString::fromStdString(entry.name));
    
    // Store metadata using custom data roles like Gualdimar does
    QString fullPath = QString::fromStdString(entry.filePath + entry.name);
    item->setData(0, static_cast<int>(Qt::UserRole) + IsoTreeWidgetItemDataPathInISO, fullPath);
    item->setData(0, static_cast<int>(Qt::UserRole) + IsoTreeWidgetItemDataMagic, (quint32)entry.magic);
    
    bool isDirectory = entry.attributes & GdfxDirectory;
    item->setData(0, static_cast<int>(Qt::UserRole) + IsoTreeWidgetItemIsDirectory, isDirectory);
    
    if (isDirectory) {
        item->setIcon(0, QIcon(":/Images/FolderFileIcon.png"));
        item->setText(1, "");
        item->setText(2, QString("0x%1").arg(entry.sector, 0, 16).toUpper());
        quint64 address = iso_->SectorToAddress(entry.sector);
        item->setText(3, QString("0x%1").arg(address, 0, 16).toUpper());
        
        // Recursively add children
        for (const auto& child : entry.files) {
            buildTreeItem(tree, item, child);
        }
    } else {
        // File icon using QtHelpers pattern
        QIcon fileIcon;
        
        QtHelpers::GetFileIcon(entry.magic, QString::fromStdString(entry.name), fileIcon, *item);
        item->setIcon(0, fileIcon);
        item->setText(1, formatSize(entry.size));
        item->setText(2, QString("0x%1").arg(entry.sector, 0, 16).toUpper());
        quint64 address = iso_->SectorToAddress(entry.sector);
        item->setText(3, QString("0x%1").arg(address, 0, 16).toUpper());
    }
}

void IsoDialog::onExtractSelected() {
    auto selected = tree_->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("Info"), tr("No files or folders selected"));
        return;
    }

    const QString outDir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory"));
    if (outDir.isEmpty()) return;

    int extracted = 0;
    auto entries = iso_->listEntries();
    
    for (auto* item : selected) {
        QString itemPath = item->data(0, static_cast<int>(Qt::UserRole) + IsoTreeWidgetItemDataPathInISO).toString();
        bool isFolder = item->data(0, static_cast<int>(Qt::UserRole) + IsoTreeWidgetItemIsDirectory).toBool();
        
        if (isFolder) {
            // Extract all files within this folder, preserving structure from parent
            QString folderPrefix = itemPath + "/";
            
            // Get the parent path to strip (everything before the selected folder)
            QString folderName = itemPath;
            int lastSlash = itemPath.lastIndexOf('/');
            QString parentPath = (lastSlash >= 0) ? itemPath.left(lastSlash + 1) : "";
            
            for (const auto& entry : entries) {
                QString entryPath = QString::fromStdString(entry.path);
                if (entry.type == XboxInternals::Iso::IsoEntryType::File &&
                    entryPath.startsWith(folderPrefix)) {
                    
                    // Calculate relative path by removing parent folders
                    QString relativePath = entryPath.mid(parentPath.length());
                    
                    // Create a modified entry with relative path
                    XboxInternals::Iso::IsoEntry modEntry = entry;
                    modEntry.path = relativePath.toStdString();
                    
                    if (iso_->extractFile(modEntry, outDir.toStdString())) {
                        extracted++;
                    }
                }
            }
        } else {
            // Extract single file with just its filename
            for (const auto& entry : entries) {
                if (QString::fromStdString(entry.path) == itemPath && 
                    entry.type == XboxInternals::Iso::IsoEntryType::File) {
                    
                    // Create a modified entry with just the filename
                    XboxInternals::Iso::IsoEntry modEntry = entry;
                    modEntry.path = entry.name;  // Use just the filename
                    
                    if (iso_->extractFile(modEntry, outDir.toStdString())) {
                        extracted++;
                    }
                    break;
                }
            }
        }
    }

    QMessageBox::information(this, tr("Success"), tr("Extracted %1 file(s)").arg(extracted));
}

void IsoDialog::onExtractAll() {
    const QString outDir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory for Batch Export"));
    if (outDir.isEmpty()) return;

    // Derive a default folder name using any Title ID embedded in the ISO filename
    const QString isoBaseName = QFileInfo(currentIsoPath_).baseName();
    QString folderName;
    QRegularExpression titleIdRegex(QStringLiteral("([0-9A-Fa-f]{8})"));
    const QRegularExpressionMatch titleIdMatch = titleIdRegex.match(isoBaseName);
    if (titleIdMatch.hasMatch()) {
        folderName = titleIdMatch.captured(1).toUpper();
    } else {
        folderName = isoBaseName;
    }

    QString fullOutDir = outDir + "/" + folderName;
    
    // Create progress dialog
    extractionProgress_ = new QProgressDialog(tr("Extracting ISO..."), QString(), 0, 100, this);
    extractionProgress_->setWindowModality(Qt::WindowModal);
    extractionProgress_->setMinimumDuration(0);
    extractionProgress_->setValue(0);
    extractionProgress_->setAutoClose(false);  // Don't auto-close when reaching 100%
    extractionProgress_->setAutoReset(false);  // Don't auto-reset
    
    // Setup watcher for extraction (only once)
    if (!extractionWatcher_) {
        extractionWatcher_ = new QFutureWatcher<ExtractionResult>(this);
        connect(extractionWatcher_, &QFutureWatcher<ExtractionResult>::finished,
                this, &IsoDialog::onExtractionFinished);
        // Connect progress signal (only once)
        connect(this, &IsoDialog::extractionProgressUpdate, this, &IsoDialog::onProgressUpdate);
    }
    
    // Run extraction in background
    auto future = QtConcurrent::run([this, fullOutDir]() -> ExtractionResult {
        // Progress callback that emits signal
        auto progressCallback = [](void* arg, uint32_t current, uint32_t total) {
            IsoDialog* dialog = static_cast<IsoDialog*>(arg);
            emit dialog->extractionProgressUpdate(static_cast<int>(current), static_cast<int>(total));
        };
        
        bool success = iso_->extractAll(fullOutDir.toStdString(), progressCallback, this);
        
        // Count actual extracted files by scanning the output directory
        int fileCount = 0;
        QDirIterator it(fullOutDir, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            if (it.fileInfo().isFile()) {
                fileCount++;
            }
        }
        
        return ExtractionResult{success, fileCount, "", fullOutDir};
    });
    
    extractionWatcher_->setFuture(future);
}

void IsoDialog::onProgressUpdate(int current, int total) {
    if (extractionProgress_) {
        extractionProgress_->setMaximum(total);
        extractionProgress_->setValue(current);
    }
}

void IsoDialog::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    
    // Skip if it's a folder
    bool isFolder = item->data(0, static_cast<int>(Qt::UserRole) + IsoTreeWidgetItemIsDirectory).toBool();
    if (isFolder) {
        return;
    }
    
    // Get magic number and path from item data
    DWORD magic = item->data(0, static_cast<int>(Qt::UserRole) + IsoTreeWidgetItemDataMagic).toUInt();
    QString pathInISO = item->data(0, static_cast<int>(Qt::UserRole) + IsoTreeWidgetItemDataPathInISO).toString();
    
    // Handle based on magic number like Gualdimar does
    switch (magic) {
        case CON:
        case LIVE:
        case PIRS: {
            // Open STFS package in PackageViewer
            try {
                IsoIO* io = iso_->GetIO(pathInISO.toStdString());
                if (!io) {
                    QMessageBox::warning(this, tr("Error"), tr("Failed to open file in ISO"));
                    return;
                }
                
                auto package = std::unique_ptr<StfsPackage>(new StfsPackage(io));
                
                // Create empty action lists for PackageViewer
                QList<QAction*> gpdActions;
                QList<QAction*> gameActions;
                
                PackageViewer* viewer = new PackageViewer(statusBar_, package.release(), gpdActions, gameActions, this);
                viewer->setAttribute(Qt::WA_DeleteOnClose);
                viewer->show();
            } catch (const std::exception& e) {
                QMessageBox::warning(this, tr("Error"), 
                                   tr("Failed to open package: %1").arg(e.what()));
            }
            return;
        }
        
        case 0x58455832: // XEX2
        case 0x58455831: // XEX1
        {
            try {
                std::unique_ptr<IsoIO> io(iso_->GetIO(pathInISO.toStdString()));
                if (!io) {
                    QMessageBox::warning(this, tr("Error"), tr("Failed to open XEX stream"));
                    return;
                }

                Xex *xex = new Xex(io.get());
                XexDialog *dialog = new XexDialog(xex, this, io.release());
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->show();

                if (statusBar_) {
                    statusBar_->showMessage(tr("XEX opened successfully"), 5000);
                }
            } catch (const std::exception &e) {
                QMessageBox::warning(this, tr("Error"), tr("Failed to open XEX: %1").arg(e.what()));
            } catch (const std::string &e) {
                QMessageBox::warning(this, tr("Error"), tr("Failed to open XEX: %1").arg(QString::fromStdString(e)));
            }
            return;
        }
        
        default:
            // Fall back to the legacy viewer based on file extension
            auto entries = iso_->listEntries();
            for (const auto& entry : entries) {
                if (QString::fromStdString(entry.path) == pathInISO && 
                    entry.type == XboxInternals::Iso::IsoEntryType::File) {
                    openFileInViewer(entry);
                    break;
                }
            }
            break;
    }
}

void IsoDialog::openFileInViewer(const XboxInternals::Iso::IsoEntry& entry) {
    QString fileName = QString::fromStdString(entry.name).toLower();
    
    // Extract to temp location
    QString tempDir = QDir::temp().filePath("velocity_iso_preview");
    QDir().mkpath(tempDir);
    
    if (!iso_->extractFile(entry, tempDir.toStdString())) {
        QMessageBox::warning(this, tr("Error"), 
            tr("Failed to extract file for viewing:\n%1\n\nPath: %2")
            .arg(QString::fromStdString(entry.name))
            .arg(QString::fromStdString(entry.path)));
        return;
    }

    // Use full path since extractFile now preserves directory structure
    QString tempFile = tempDir + "/" + QString::fromStdString(entry.path);
    
    // Verify file exists
    if (!QFile::exists(tempFile)) {
        QMessageBox::warning(this, tr("Error"), 
            tr("Extracted file not found:\n%1").arg(tempFile));
        return;
    }
    
    // Handle image files with ImageDialog popup
    if (fileName.endsWith(".jpg") || fileName.endsWith(".jpeg") || 
        fileName.endsWith(".png") || fileName.endsWith(".bmp") || 
        fileName.endsWith(".gif") || fileName.endsWith(".tga") ||
        fileName.endsWith(".dds")) {
        
        // Try to load as standard image first
        QImage image;
        
        // For JPG files, explicitly try JPEG format
        if (fileName.endsWith(".jpg") || fileName.endsWith(".jpeg")) {
            image.load(tempFile, "JPEG");
        } else if (fileName.endsWith(".png")) {
            image.load(tempFile, "PNG");
        } else if (fileName.endsWith(".bmp")) {
            image.load(tempFile, "BMP");
        } else {
            // Let Qt auto-detect
            image.load(tempFile);
        }
        
        if (!image.isNull()) {
            ImageDialog *dlg = new ImageDialog(image, QString::fromStdString(entry.name), this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        } else {
            // Read first few bytes to check if it's really a JPEG or Xbox texture format
            QFile checkFile(tempFile);
            if (checkFile.open(QIODevice::ReadOnly)) {
                QByteArray header = checkFile.read(4);
                checkFile.close();
                
                // Check for known image formats
                bool isStandardFormat = false;
                if (header.size() >= 3 && (unsigned char)header[0] == 0xFF && 
                    (unsigned char)header[1] == 0xD8 && (unsigned char)header[2] == 0xFF) {
                    isStandardFormat = true; // JPEG
                } else if (header.size() >= 4 && header[0] == (char)0x89 && 
                           header[1] == 'P' && header[2] == 'N' && header[3] == 'G') {
                    isStandardFormat = true; // PNG
                } else if (header.size() >= 4 && header[0] == 'D' && header[1] == 'D' && 
                           header[2] == 'S' && header[3] == ' ') {
                    isStandardFormat = true; // DDS
                }
                
                if (!isStandardFormat) {
                    // Xbox 360 proprietary texture format - offer to extract
                    QMessageBox msgBox(this);
                    msgBox.setWindowTitle(tr("Xbox 360 Texture Format"));
                    msgBox.setText(tr("This file appears to be an Xbox 360 proprietary texture format, not a standard image.\n\n"
                                     "File: %1\nSize: %2 bytes\n\n"
                                     "Would you like to extract it to view with external tools?")
                                   .arg(QString::fromStdString(entry.name))
                                   .arg(entry.size));
                    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                    msgBox.setDefaultButton(QMessageBox::Yes);
                    
                    if (msgBox.exec() == QMessageBox::Yes) {
                        QString saveFile = QFileDialog::getSaveFileName(this, 
                            tr("Save Xbox Texture"), 
                            QString::fromStdString(entry.name),
                            tr("All Files (*)"));
                        
                        if (!saveFile.isEmpty()) {
                            if (QFile::copy(tempFile, saveFile)) {
                                QMessageBox::information(this, tr("Success"), 
                                    tr("File extracted to:\n%1").arg(saveFile));
                            } else {
                                QMessageBox::warning(this, tr("Error"), 
                                    tr("Failed to save file"));
                            }
                        }
                    }
                } else {
                    // Standard format but Qt couldn't load it
                    QMessageBox::warning(this, tr("Error"), 
                        tr("Failed to load image:\n%1\n\nFile appears to be a standard format but Qt failed to decode it.\n"
                           "File size: %2 bytes")
                        .arg(QString::fromStdString(entry.name))
                        .arg(QFileInfo(tempFile).size()));
                }
            } else {
                QMessageBox::warning(this, tr("Error"), 
                    tr("Failed to load image:\n%1\n\nCould not read file.")
                    .arg(QString::fromStdString(entry.name)));
            }
        }
        QFile::remove(tempFile);
        return;
    }
    
    // Handle XML files with XmlDialog
    if (fileName.endsWith(".xml") || fileName.endsWith(".xsd") || 
        fileName.endsWith(".xsl") || fileName.endsWith(".xslt")) {
        
        QFile file(tempFile);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            file.close();
            
            QString xmlContent;
            // Check for BOM and decode accordingly (same logic as packageviewer)
            if (data.startsWith("\xFF\xFE")) {
                auto toUtf16LE = QStringDecoder(QStringDecoder::Utf16LE);
                xmlContent = toUtf16LE(data.mid(2));
            } else if (data.startsWith("\xFE\xFF")) {
                auto toUtf16BE = QStringDecoder(QStringDecoder::Utf16BE);
                xmlContent = toUtf16BE(data.mid(2));
            } else if (data.startsWith("\xEF\xBB\xBF")) {
                auto toUtf8 = QStringDecoder(QStringDecoder::Utf8);
                xmlContent = toUtf8(data.mid(3));
            } else {
                auto toUtf8 = QStringDecoder(QStringDecoder::Utf8);
                xmlContent = toUtf8(data);
                
                if (xmlContent.contains(QChar::ReplacementCharacter)) {
                    auto toLatin1 = QStringDecoder(QStringDecoder::Latin1);
                    xmlContent = toLatin1(data);
                }
            }
            
            XmlDialog *dlg = new XmlDialog(xmlContent, QString::fromStdString(entry.name), this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        } else {
            QMessageBox::warning(this, tr("Error"), tr("Failed to read XML file"));
        }
        QFile::remove(tempFile);
        return;
    }
    
    // Handle text files with TextDialog
    if (fileName.endsWith(".txt") || fileName.endsWith(".cfg") || 
        fileName.endsWith(".ini") || fileName.endsWith(".json") || 
        fileName.endsWith(".log") || fileName.endsWith(".lua") ||
        fileName.endsWith(".h") || fileName.endsWith(".cpp") ||
        fileName.endsWith(".c") || fileName.endsWith(".hlsl") ||
        fileName.endsWith(".fx") || fileName.endsWith(".shader")) {
        
        QFile file(tempFile);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            file.close();
            
            // Pass raw data to TextDialog - it will handle encoding detection and UI
            TextDialog *dlg = new TextDialog(data, QString::fromStdString(entry.name), this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        } else {
            QMessageBox::warning(this, tr("Error"), tr("Failed to read text file"));
        }
        QFile::remove(tempFile);
        return;
    }
    
    // For other files, show info including magic number for debugging
    // Get the stored magic number from the tree item
    DWORD magic = 0;
    QTreeWidgetItemIterator it(tree_);
    while (*it) {
        QString itemPath = (*it)->data(0, static_cast<int>(Qt::UserRole) + IsoTreeWidgetItemDataPathInISO).toString();
        if (itemPath == QString::fromStdString(entry.path)) {
            magic = (*it)->data(0, static_cast<int>(Qt::UserRole) + IsoTreeWidgetItemDataMagic).toUInt();
            break;
        }
        ++it;
    }
    
    QMessageBox::information(this, tr("File Info"), 
        tr("File: %1\nSize: %2 bytes\nMagic: 0x%3\n\n(Binary file - no preview available)")
        .arg(QString::fromStdString(entry.name))
        .arg(entry.size)
        .arg(magic, 8, 16, QChar('0')).toUpper());
    QFile::remove(tempFile);
}

void IsoDialog::onSearchTextChanged(const QString& text) {
    // Simple search: hide items that don't match
    QString searchLower = text.toLower();
    
    QTreeWidgetItemIterator it(tree_);
    while (*it) {
        QTreeWidgetItem* item = *it;
        QString itemName = item->text(0).toLower();
        
        if (searchLower.isEmpty() || itemName.contains(searchLower)) {
            item->setHidden(false);
        } else {
            // Only hide files, not folders (folders might contain matching files)
            bool isFolder = item->data(0, Qt::UserRole + 1).toBool();
            if (!isFolder) {
                item->setHidden(true);
            }
        }
        ++it;
    }
}

void IsoDialog::showContextMenu(const QPoint& point) {
    // Get the item at the click position
    QTreeWidgetItem* item = tree_->itemAt(point);
    if (!item) return;
    
    // Create context menu
    QMenu contextMenu(this);
    
    // Only show Extract for single selection and non-directories
    if (tree_->selectedItems().size() == 1) {
        bool isDirectory = item->data(0, static_cast<int>(Qt::UserRole) + IsoTreeWidgetItemIsDirectory).toBool();
        if (!isDirectory) {
            QAction* extractAction = contextMenu.addAction(QIcon(":/Images/extract.png"), tr("Extract"));
            
            // Show menu and handle result
            QPoint globalPos = tree_->mapToGlobal(point);
            QAction* selectedAction = contextMenu.exec(globalPos);
            
            if (selectedAction == extractAction) {
                // Extract single file
                QString pathInISO = item->data(0, static_cast<int>(Qt::UserRole) + IsoTreeWidgetItemDataPathInISO).toString();
                
                QString defaultName = QFileInfo(pathInISO).fileName();
                QString savePath = QFileDialog::getSaveFileName(this, tr("Extract File"), defaultName);
                if (savePath.isEmpty()) return;
                
                try {
                    // Get IO for reading the file
                    IsoIO* io = iso_->GetIO(pathInISO.toStdString());
                    if (!io) {
                        QMessageBox::warning(this, tr("Error"), tr("Failed to open file in ISO"));
                        return;
                    }
                    
                    // Read the entire file
                    UINT64 fileSize = io->Length();
                    std::vector<BYTE> buffer(fileSize);
                    io->SetPosition(0);
                    io->ReadBytes(buffer.data(), fileSize);
                    
                    // Write to output file
                    QFile outFile(savePath);
                    if (!outFile.open(QIODevice::WriteOnly)) {
                        delete io;
                        QMessageBox::warning(this, tr("Error"), tr("Failed to create output file"));
                        return;
                    }
                    
                    outFile.write(reinterpret_cast<const char*>(buffer.data()), fileSize);
                    outFile.close();
                    delete io;
                    
                    if (statusBar_) {
                        statusBar_->showMessage(tr("File extracted successfully"), 5000);
                    }
                } catch (const std::exception& e) {
                    QMessageBox::warning(this, tr("Error"), 
                                       tr("Failed to extract file: %1").arg(e.what()));
                }
            }
        }
    }
}

void IsoDialog::onExpandAll() {
    tree_->expandAll();
}

void IsoDialog::onCollapseAll() {
    tree_->collapseAll();
}

void IsoDialog::onExtractionFinished() {
    // Close and cleanup progress dialog first
    if (extractionProgress_) {
        extractionProgress_->reset();  // Reset before deleting to avoid issues
        extractionProgress_->deleteLater();  // Use deleteLater instead of immediate delete
        extractionProgress_ = nullptr;
    }
    
    // Get result before showing message box
    if (extractionWatcher_) {
        auto result = extractionWatcher_->result();
        
        // Use QTimer::singleShot to show message box after event loop processes deletion
        QTimer::singleShot(100, this, [this, result]() {
            if (result.success) {
                QString message = tr("Successfully extracted %n file(s) to:\n%1", "", result.filesExtracted)
                                  .arg(result.outputPath);
                QMessageBox::information(this, tr("Extraction Complete"), message);
                
                if (statusBar_) {
                    QString statusMsg = result.filesExtracted == 1 
                        ? tr("File extracted successfully")
                        : tr("Files extracted successfully");
                    statusBar_->showMessage(statusMsg, 5000);
                }
            } else {
                QString message = tr("Extraction failed");
                if (!result.errorMessage.isEmpty()) {
                    message += tr(":\n%1").arg(result.errorMessage);
                }
                QMessageBox::warning(this, tr("Error"), message);
            }
        });
    }
}

void IsoDialog::onSectorToolClicked() {
    if (!iso_) return;
    
    IsoSectorDialog dialog(iso_.get(), this);
    dialog.exec();
}

