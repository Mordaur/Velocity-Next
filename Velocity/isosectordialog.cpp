#include "isosectordialog.h"
#include "ui_isosectordialog.h"
#include <QString>

IsoSectorDialog::IsoSectorDialog(XboxInternals::Iso::IsoImage *iso, QWidget *parent)
    : QDialog(parent), ui(new Ui::IsoSectorDialog), iso_(iso)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    ui->setupUi(this);
    
    // Set maximum sector value
    ui->spnSector->setMinimum(0);
    ui->spnSector->setMaximum(iso_->GetTotalSectors());
    
    // Connect signal
    connect(ui->spnSector, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &IsoSectorDialog::onSectorValueChanged);
    
    // Update initial address
    onSectorValueChanged(0);
}

IsoSectorDialog::~IsoSectorDialog()
{
    delete ui;
}

void IsoSectorDialog::onSectorValueChanged(int value)
{
    u64 sectorAddress = iso_->SectorToAddress(value);
    QString addressText = QString("0x%1").arg(sectorAddress, 0, 16).toUpper();
    ui->lblAddress->setText(addressText);
}
