#ifndef XUIZDIALOG_H
#define XUIZDIALOG_H

#include <QDialog>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>

#include "qthelpers.h"
#include <XboxInternals/Xex/Xuiz.h>

namespace Ui {
class XuizDialog;
}

class XuizDialog : public QDialog
{
    Q_OBJECT

public:
    explicit XuizDialog(QStatusBar *statusBar, Xuiz *xuiz, QWidget *parent = nullptr);
    ~XuizDialog();

private slots:
    void showContextMenu(QPoint point);

private:
    Ui::XuizDialog *ui;
    QStatusBar *statusBar;
    Xuiz *xuiz;
};

#endif // XUIZDIALOG_H
