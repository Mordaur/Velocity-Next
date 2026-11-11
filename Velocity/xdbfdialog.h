#ifndef XdbfDIALOG_H
#define XdbfDIALOG_H

// qt
#include <QDialog>
#include <QModelIndex>
#include <QMessageBox>
#include <QDateTime>
#include <QMenu>
#include <QFile>
#include <QFileDialog>
#include <QStatusBar>
#include "qthelpers.h"

#include <memory>

// xbox360
#include <XboxInternals/Gpd/Xdbf.h>
#include <XboxInternals/Gpd/XdbfDefinitions.h>
#include <optional>

#include <XboxInternals/Gpd/XdbfHelpers.h>
#include <XboxInternals/Gpd/GpdBase.h>
#include <XboxInternals/IO/FileIO.h>

// forms
#include "imagedialog.h"
#include "addressconverterdialog.h"

namespace Ui
{
class XdbfDialog;
}

struct Entry
{
    EntryType type = Undefined;
    DWORD index = 0;
};

class XdbfDialog : public QDialog
{
    Q_OBJECT

public:
    explicit XdbfDialog(QStatusBar *statusBar, std::unique_ptr<GpdBase> gpd, bool *modified = nullptr,
            QWidget *parent = 0);
    ~XdbfDialog();

private slots:
    void on_treeWidget_doubleClicked(const QModelIndex &index);

    void showContextMenu(QPoint p);

private:
    Ui::XdbfDialog *ui;
    std::unique_ptr<GpdBase> gpd;
    bool *modified;
    QStatusBar *statusBar;

    void addEntriesToTable(vector<XdbfEntry> entries, QString type);

    std::optional<Entry> indexToEntry(DWORD index);

    void loadEntries();
};

#endif // XdbfDIALOG_H


