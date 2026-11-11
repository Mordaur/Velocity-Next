#ifndef CLUSTERTOOLDIALOG_H
#define CLUSTERTOOLDIALOG_H

// qt
#include <QDialog>

#include <XboxInternals/Fatx/FatxConstants.h>

namespace Ui
{
class ClusterToolDialog;
}

class ClusterToolDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ClusterToolDialog(PartitionInfo partition, QWidget *parent = 0);
    ~ClusterToolDialog();

private slots:
    void on_spinBox_valueChanged(int cluster);

private:
    Ui::ClusterToolDialog *ui;
    PartitionInfo part;
};

#endif // CLUSTERTOOLDIALOG_H


