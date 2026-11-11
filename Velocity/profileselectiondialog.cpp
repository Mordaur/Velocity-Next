#include "profileselectiondialog.h"
#include "ui_profileselectiondialog.h"

#include <QIcon>
#include <QImage>
#include <QPixmap>

ProfileSelectionDialog::ProfileSelectionDialog(std::vector<XContentDeviceProfile> *profiles,
                                               const QString &fileName, QWidget *parent) :
    QDialog(parent), ui(new Ui::ProfileSelectionDialog), profiles(profiles),
    hasSelectedProfile(false)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    ui->setupUi(this);

    setWindowTitle("Owner of " + fileName);

    for (auto &profile : *profiles)
    {
        QTreeWidgetItem *profileItem = new QTreeWidgetItem(ui->treeWidget);
        const QString gamertag = QString::fromStdWString(profile.GetName());
        profileItem->setText(0, gamertag);

        if (gamertag.isEmpty() || gamertag == "Unknown Profile")
            profileItem->setHidden(true);

        if (profile.GetThumbnail() != nullptr && profile.GetThumbnailSize() != 0)
        {
            QByteArray imageBuff(reinterpret_cast<const char *>(profile.GetThumbnail()),
                                 profile.GetThumbnailSize());
            QPixmap pixmap = QPixmap::fromImage(QImage::fromData(imageBuff));
            if (!pixmap.isNull())
                profileItem->setIcon(0, QIcon(pixmap));
        }
    }

    QString instructions = ui->lblDirections->text();
    instructions.replace("FILE_NAME", fileName);
    ui->lblDirections->setText(instructions);
}

ProfileSelectionDialog::~ProfileSelectionDialog()
{
    delete ui;
}

XContentDeviceProfile ProfileSelectionDialog::GetSelectedProfile()
{
    return selectedProfile;
}

bool ProfileSelectionDialog::HasSelectedProfile()
{
    return hasSelectedProfile;
}

void ProfileSelectionDialog::on_treeWidget_currentItemChanged(QTreeWidgetItem *current,
                                                              QTreeWidgetItem * /*previous*/)
{
    if (current == nullptr)
    {
        hasSelectedProfile = false;
        return;
    }

    selectedProfile = profiles->at(ui->treeWidget->currentIndex().row());
    hasSelectedProfile = true;
}

void ProfileSelectionDialog::on_pushButton_clicked()
{
    close();
}

void ProfileSelectionDialog::on_pushButton_2_clicked()
{
    hasSelectedProfile = false;
    close();
}

void ProfileSelectionDialog::on_treeWidget_itemDoubleClicked(QTreeWidgetItem * /*item*/, int /*column*/)
{
    selectedProfile = profiles->at(ui->treeWidget->currentIndex().row());
    hasSelectedProfile = true;
    close();
}
