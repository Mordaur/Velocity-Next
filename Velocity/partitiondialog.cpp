#include "partitiondialog.h"
#include "ui_partitiondialog.h"

PartitionDialog::PartitionDialog(std::vector<PartitionInfo> infos, QWidget *parent) :
    QDialog(parent), ui(new Ui::PartitionDialog), partitions(std::move(infos))
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    ui->setupUi(this);

    for (const PartitionInfo &info : partitions)
        ui->comboBox->addItem(QString::fromStdString(info.name));

    if (!partitions.empty())
        on_comboBox_currentIndexChanged(0);
}

PartitionDialog::~PartitionDialog()
{
    delete ui;
}

void PartitionDialog::on_comboBox_currentIndexChanged(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= partitions.size())
        return;

    const PartitionInfo &part = partitions.at(static_cast<size_t>(index));
    ui->lblPartID->setText("0x" + QString::number(part.partitionId, 16).toUpper());
    ui->lblPartSize->setText(QString::fromStdString(ByteSizeToString(part.size)));
    ui->lblPartClusterSize->setText("0x" + QString::number(part.clusterSize, 16).toUpper());
    ui->lblPartFirstClusterAddr->setText("0x" + QString::number(part.clusterStartingAddress,
            16).toUpper());
    ui->lblPartSectorsPerCluster->setText(QString::number(part.sectorsPerCluster));
    ui->lblPartRootDirCluster->setText("0x" + QString::number(part.rootDirectoryCluster,
            16).toUpper());

    // calculate the percentage
    const UINT64 totalBytes = static_cast<UINT64>(part.clusterCount) * part.clusterSize;
    float freeMemPercentage = totalBytes ? static_cast<float>((part.freeMemory * 100.0) /
            totalBytes) : 0.0f;

    // draw the chart
    QPixmap chart(391, 221);
    chart.fill(ui->imgPiechart->palette().window().color());
    QPainter painter(&chart);
    Nightcharts pieChart;
    pieChart.setType(Nightcharts::Dpie);
    pieChart.setCords(10, 10, 200, 150);
    pieChart.setFont(QFont("Arial", 48));
    pieChart.addPiece("Used Space", QColor(0, 0, 254), 100.0 - freeMemPercentage);
    pieChart.addPiece("Free Space", QColor(255, 0, 254), freeMemPercentage);
    pieChart.setLegendCords(235, 50);
    pieChart.setLegendType(Nightcharts::Vertical);
    pieChart.draw(&painter);
    pieChart.drawLegend(&painter);

    chart.copy();

    ui->imgPiechart->setPixmap(chart);
}

void PartitionDialog::on_btnClusterTool_clicked()
{
    const int index = ui->comboBox->currentIndex();
    if (index < 0 || static_cast<size_t>(index) >= partitions.size())
        return;

    ClusterToolDialog *dialog = new ClusterToolDialog(partitions.at(static_cast<size_t>(index)),
                                                      this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}


