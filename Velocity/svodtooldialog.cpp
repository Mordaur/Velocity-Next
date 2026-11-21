#include "svodtooldialog.h"
#include "ui_svodtooldialog.h"

#include <QSignalBlocker>
#include <QDebug>

#include <XboxInternals/Disc/SvodLayout.h>

#include <algorithm>
#include <climits>
#include <exception>
#include <string>

SvodToolDialog::SvodToolDialog(SVOD *svod, QWidget *parent) :
    QDialog(parent), ui(new Ui::SvodToolDialog), svod(svod)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    ui->setupUi(this);
    QtHelpers::GenAdjustWidgetAppearanceToOS(this);

    const auto &descriptor = svod->metadata->svodVolumeDescriptor;
    const bool enhanced = XboxInternals::SvodLayout::IsEnhanced(descriptor.flags);
    const u64 dataOffset = XboxInternals::SvodLayout::DataBlockOffset(
            descriptor.dataBlockOffset, enhanced);
    const u64 sectorCount = svod->GetSectorCount();

    // SectorToAddress() rejects sector == count, so the spin box must stop one
    // short. A u32 sector count can also exceed QSpinBox's int range.
    const u64 minSector = dataOffset * 2;
    const u64 maxSector = sectorCount == 0 ? 0 : sectorCount - 1;
    const int maxValue = maxSector > static_cast<u64>(INT_MAX)
            ? INT_MAX : static_cast<int>(maxSector);
    const int minValue = minSector > static_cast<u64>(maxValue)
            ? maxValue : static_cast<int>(minSector);

    {
        QSignalBlocker blocker(ui->spinBox);
        ui->spinBox->setRange(minValue, maxValue);
        ui->spinBox->setValue(minValue);
    }

    on_spinBox_valueChanged(minValue);
}

SvodToolDialog::~SvodToolDialog()
{
    delete ui;
}

void SvodToolDialog::on_spinBox_valueChanged(int arg1)
{
    try
    {
        u32 addr = 0, index = 0;
        svod->SectorToAddress(static_cast<u32>(arg1), &addr, &index);
        ui->lblAddress->setText("0x" + QString::number(addr, 16).toUpper());
        ui->lblFileIndex->setText(QString::number(index));
    }
    catch (const std::string &error)
    {
        // Qt slots must not let exceptions escape; terminate() would crash.
        ui->lblAddress->setText("Out of range");
        ui->lblFileIndex->setText("-");
        qWarning("SVOD sector lookup failed: %s", error.c_str());
    }
    catch (const std::exception &error)
    {
        ui->lblAddress->setText("Out of range");
        ui->lblFileIndex->setText("-");
        qWarning("SVOD sector lookup failed: %s", error.what());
    }
}


