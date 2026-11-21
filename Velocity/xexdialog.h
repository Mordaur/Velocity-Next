#ifndef XEXDIALOG_H
#define XEXDIALOG_H

#include <QDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include "qthelpers.h"
#include "boxartretriever.h"

#include <XboxInternals/Xex/Xex.h>
#include <XboxInternals/IO/BaseIO.h>

namespace Ui {
class XexDialog;
}

class XexDialog : public QDialog
{
    Q_OBJECT

public:
    explicit XexDialog(Xex *xex, QWidget *parent = nullptr, BaseIO *ownedIo = nullptr);
    ~XexDialog();

private slots:
    void on_pushButton_clicked();

    void onLargeBoxArtRetrieved(QPixmap boxArt);

    void showContextMenu(QPoint point);

private:
    Ui::XexDialog *ui;
    Xex *xex;
    BoxArtRetriever *boxArtRetriever;
    BaseIO *ownedIo;

    void AddExecutableProperty(QString name, u32 value);

    void AddExecutableProperty(QString name, QString value);
};

#endif // XEXDIALOG_H
