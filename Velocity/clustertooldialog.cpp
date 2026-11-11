#include "clustertooldialog.h"
#include "ui_clustertooldialog.h"

ClusterToolDialog::ClusterToolDialog(PartitionInfo partition, QWidget *parent) :
    QDialog(parent), ui(new Ui::ClusterToolDialog), part(std::move(partition))
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    ui->setupUi(this);

    ui->spinBox->setMaximum(part.clusterCount > 0 ? static_cast<int>(part.clusterCount - 1) : 0);
    on_spinBox_valueChanged(1);
}

ClusterToolDialog::~ClusterToolDialog()
{
    delete ui;
}

void ClusterToolDialog::on_spinBox_valueChanged(int cluster)
{
    ui->lblAddress->setText("0x" + QString::number(part.ClusterToOffset(
            static_cast<DWORD>(cluster)), 16).toUpper());
}


