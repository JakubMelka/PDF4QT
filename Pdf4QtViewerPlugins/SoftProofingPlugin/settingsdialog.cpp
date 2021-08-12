//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include "pdfwidgetutils.h"

SettingsDialog::SettingsDialog(QWidget* parent, const pdf::PDFCMSSettings& settings, const pdf::PDFCMSManager* manager) :
    QDialog(parent),
    ui(new Ui::SettingsDialog),
    m_settings(settings)
{
    ui->setupUi(this);

    ui->cmsProofingIntentComboBox->addItem(tr("Auto"), int(pdf::RenderingIntent::Auto));
    ui->cmsProofingIntentComboBox->addItem(tr("Perceptual"), int(pdf::RenderingIntent::Perceptual));
    ui->cmsProofingIntentComboBox->addItem(tr("Relative colorimetric"), int(pdf::RenderingIntent::RelativeColorimetric));
    ui->cmsProofingIntentComboBox->addItem(tr("Absolute colorimetric"), int(pdf::RenderingIntent::AbsoluteColorimetric));
    ui->cmsProofingIntentComboBox->addItem(tr("Saturation"), int(pdf::RenderingIntent::Saturation));

    for (const pdf::PDFColorProfileIdentifier& identifier : manager->getCMYKProfiles())
    {
        ui->cmsProofingColorProfileComboBox->addItem(identifier.name, identifier.id);
    }

    ui->cmsProofingIntentComboBox->setCurrentIndex(ui->cmsProofingIntentComboBox->findData(int(m_settings.proofingIntent)));
    ui->cmsProofingColorProfileComboBox->setCurrentIndex(ui->cmsProofingColorProfileComboBox->findData(m_settings.softProofingProfile));
    ui->outOfGamutColorEdit->setText(m_settings.outOfGamutColor.name(QColor::HexRgb));

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(320, 160)));
    pdf::PDFWidgetUtils::style(this);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::accept()
{
    m_settings.proofingIntent = static_cast<pdf::RenderingIntent>(ui->cmsProofingIntentComboBox->currentData().toInt());
    m_settings.softProofingProfile = ui->cmsProofingColorProfileComboBox->currentData().toString();
    m_settings.outOfGamutColor.setNamedColor(ui->outOfGamutColorEdit->text());
    if (!m_settings.outOfGamutColor.isValid())
    {
        m_settings.outOfGamutColor = Qt::red;
    }

    QDialog::accept();
}
