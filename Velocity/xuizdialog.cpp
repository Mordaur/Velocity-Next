#include "xuizdialog.h"
#include "ui_xuizdialog.h"

#include <XboxInternals/Stfs/StfsDefinitions.h>

XuizDialog::XuizDialog(QStatusBar *statusBar, Xuiz *xuiz, QWidget *parent) :
    QDialog(parent), ui(new Ui::XuizDialog), statusBar(statusBar), xuiz(xuiz)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    ui->setupUi(this);

    ui->treeWidget->header()->setStretchLastSection(false);
    ui->treeWidget->header()->resizeSection(0, 350);

    ui->treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->treeWidget, SIGNAL(customContextMenuRequested(QPoint)), this,
            SLOT(showContextMenu(QPoint)));

    for (const auto &file : xuiz->GetFiles())
    {
        QTreeWidgetItem *item = new QTreeWidgetItem(ui->treeWidget);
        item->setText(0, QString::fromStdString(file.path));
        item->setText(1, QString::fromStdString(ByteSizeToString(file.size)));
    }
}

XuizDialog::~XuizDialog()
{
    delete xuiz;
    delete ui;
}

void XuizDialog::showContextMenu(QPoint point)
{
    QPoint globalPos = ui->treeWidget->mapToGlobal(point);
    QMenu contextMenu;

    if (ui->treeWidget->selectedItems().size() == 1)
        contextMenu.addAction(QPixmap(":/Images/extract.png"), "Extract");

    QAction *selectedItem = contextMenu.exec(globalPos);
    if (!selectedItem)
        return;

    if (selectedItem->text() == "Extract")
    {
        QString xuizFilePath = ui->treeWidget->selectedItems().at(0)->text(0);
        QString fileName = xuizFilePath;

        int lastSlashIndex = fileName.lastIndexOf('\\');
        if (lastSlashIndex != -1)
            fileName = fileName.mid(lastSlashIndex + 1);
        else
        {
            lastSlashIndex = fileName.lastIndexOf('/');
            if (lastSlashIndex != -1)
                fileName = fileName.mid(lastSlashIndex + 1);
        }

        QString outPath = QFileDialog::getSaveFileName(this,
                "Choose a place to extract the file to",
                QtHelpers::DefaultLocation() + "/" + fileName);

        if (outPath.isEmpty())
            return;

        try
        {
            xuiz->ExtractFile(outPath.toStdString(), xuizFilePath.toStdString());
            statusBar->showMessage("Successfully extracted file", 5000);
        }
        catch (const std::string &error)
        {
            QMessageBox::critical(this, "Extracting Error",
                    "An error occurred while extracting a file.\n\n" +
                    QString::fromStdString(error));
        }
        catch (const std::exception &error)
        {
            QMessageBox::critical(this, "Extracting Error",
                    "An error occurred while extracting a file.\n\n" +
                    QString::fromStdString(error.what()));
        }
    }
}
