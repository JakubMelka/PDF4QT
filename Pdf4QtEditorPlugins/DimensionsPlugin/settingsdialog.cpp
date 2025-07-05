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
#include <QFontDialog>
#include <QColorDialog>

SettingsDialog::SettingsDialog(QWidget* parent, pdfplugin::DimensionsPluginSettings& originalSettings) :
    QDialog(parent),
    ui(new Ui::SettingsDialog),
    m_originalSettings(originalSettings)
{
    ui->setupUi(this);

    m_updatedSettings = m_originalSettings;

    m_lengthUnits = DimensionUnit::getLengthUnits();
    m_areaUnits = DimensionUnit::getAreaUnits();
    m_angleUnits = DimensionUnit::getAngleUnits();

    initComboBox(m_lengthUnits, m_updatedSettings.lengthUnit, ui->lengthsComboBox);
    initComboBox(m_areaUnits, m_updatedSettings.areaUnit, ui->areasComboBox);
    initComboBox(m_angleUnits, m_updatedSettings.angleUnit, ui->anglesComboBox);
    ui->scaleEdit->setValue(m_updatedSettings.scale);

    connect(ui->fontComboBox, &QFontComboBox::currentFontChanged, this, &SettingsDialog::setFont);
    connect(ui->textColorButton, &QPushButton::clicked, this, [this]() {
        QColor color = QColorDialog::getColor(m_updatedSettings.textColor, this, tr("Select Text Color"), QColorDialog::ShowAlphaChannel);
        if (color.isValid())
        {
            setTextColor(color);
        }
    });
    connect(ui->backgroundColorButton, &QPushButton::clicked, this, [this]() {
        QColor color = QColorDialog::getColor(m_updatedSettings.backgroundColor, this, tr("Select Background Color"), QColorDialog::ShowAlphaChannel);
        if (color.isValid())
        {
            setBackgroundColor(color);
        }
    });
    connect(ui->selectFontButton, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        QFont font = QFontDialog::getFont(&ok, m_updatedSettings.font, this, tr("Select Font"));
        if (ok)
        {
            setFont(font);
            ui->fontComboBox->setCurrentFont(font);
        }
    });

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(640, 480)));
    pdf::PDFWidgetUtils::style(this);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::initComboBox(const DimensionUnits& units, const DimensionUnit& currentUnit, QComboBox* comboBox)
{
    for (const DimensionUnit& unit : units)
    {
        comboBox->addItem(unit.symbol, unit.symbol);
    }

    comboBox->setCurrentIndex(comboBox->findText(currentUnit.symbol));
}

void SettingsDialog::accept()
{
    m_updatedSettings.lengthUnit = m_lengthUnits[ui->lengthsComboBox->currentIndex()];
    m_updatedSettings.areaUnit = m_areaUnits[ui->areasComboBox->currentIndex()];
    m_updatedSettings.angleUnit = m_angleUnits[ui->anglesComboBox->currentIndex()];
    m_updatedSettings.scale = ui->scaleEdit->value();

    m_originalSettings = m_updatedSettings;
    QDialog::accept();
}

void SettingsDialog::setFont(const QFont& font)
{
    m_updatedSettings.font = font;
}

QFont SettingsDialog::getFont() const
{
    return m_updatedSettings.font;
}

void SettingsDialog::setTextColor(const QColor& color)
{
    m_updatedSettings.textColor = color;
}

QColor SettingsDialog::getTextColor() const
{
    return m_updatedSettings.textColor;
}

void SettingsDialog::setBackgroundColor(const QColor& color)
{
    m_updatedSettings.backgroundColor = color;
}

QColor SettingsDialog::getBackgroundColor() const
{
    return m_updatedSettings.backgroundColor;
}
