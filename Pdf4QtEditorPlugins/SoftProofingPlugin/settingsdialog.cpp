// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

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

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(320, 240)));
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
    m_settings.outOfGamutColor = QColor::fromString(ui->outOfGamutColorEdit->text());
    if (!m_settings.outOfGamutColor.isValid())
    {
        m_settings.outOfGamutColor = Qt::red;
    }

    QDialog::accept();
}
