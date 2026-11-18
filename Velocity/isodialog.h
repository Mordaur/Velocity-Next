#pragma once
#include <QDialog>
#include <QMap>
#include <QFutureWatcher>
#include <atomic>
#include <memory>
#include "XboxInternals/Iso/IsoImage.h"  // Full header needed for GdfxFileEntry in method signature

QT_BEGIN_NAMESPACE
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QDragEnterEvent;
class QDropEvent;
class QProgressDialog;
class QStatusBar;
QT_END_NAMESPACE

namespace XboxInternals::Iso { 
    class IsoImage; 
    struct IsoEntry;
}

struct GdfxFileEntry;  // Forward declaration (defined in Gdfx.h, global namespace)

// Custom data roles for storing item metadata like Gualdimar does
enum IsoTreeWidgetItemData {
    IsoTreeWidgetItemDataPathInISO = 0,
    IsoTreeWidgetItemDataMagic = 1,
    IsoTreeWidgetItemIsDirectory = 2
};

class IsoDialog : public QDialog {
    Q_OBJECT
public:
    explicit IsoDialog(QStatusBar* statusBar, QWidget* parent = nullptr);
    ~IsoDialog() override;

    // Load an ISO or GOD file
    void loadIsoContents(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

signals:
    void extractionProgressUpdate(int current, int total);

private slots:
    void onExtractSelected();
    void onExtractAll();
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onSearchTextChanged(const QString& text);
    void onExpandAll();
    void onCollapseAll();
    void onProgressUpdate(int current, int total);
    void showContextMenu(const QPoint& point);
    void onSectorToolClicked();

private:
    struct ExtractionResult {
        bool success;
        int filesExtracted;
        QString errorMessage;
        QString outputPath;
    };

    void populateTree();
    void buildTreeItem(QTreeWidget* tree, QTreeWidgetItem* parent, 
                      const GdfxFileEntry& entry);
    void openFileInViewer(const XboxInternals::Iso::IsoEntry& entry);
    QTreeWidgetItem* findOrCreateFolder(const QString& path);
    void onExtractionFinished();
    // Enables/disables the widgets that must not touch `iso_` while the
    // background extraction owns it.
    void setExtractionBusy(bool busy);
    
    QPushButton* extractSelectedButton_ = nullptr;
    QPushButton* extractAllButton_ = nullptr;
    QPushButton* expandAllButton_ = nullptr;
    QPushButton* collapseAllButton_ = nullptr;
    QPushButton* sectorToolButton_ = nullptr;
    QLineEdit* searchBox_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QProgressDialog* extractionProgress_ = nullptr;
    QFutureWatcher<ExtractionResult>* extractionWatcher_ = nullptr;
    QStatusBar* statusBar_ = nullptr;
    
    std::unique_ptr<XboxInternals::Iso::IsoImage> iso_;
    QString currentIsoPath_;
    QMap<QString, QTreeWidgetItem*> folderItems_;
    // Set by the destructor and checked once per extracted file; the QtConcurrent
    // future cannot be cancelled through QFuture::cancel().
    std::atomic<bool> cancelExtraction_{false};
};

