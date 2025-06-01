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

SettingsDialog::SettingsDialog(QWidget* parent,
                               DimensionUnit& lengthUnit,
                               DimensionUnit& areaUnit,
                               DimensionUnit& angleUnit,
                               double& scale) :
    QDialog(parent),
    ui(new Ui::SettingsDialog),
    m_lengthUnit(lengthUnit),
    m_areaUnit(areaUnit),
    m_angleUnit(angleUnit),
    m_scale(scale)
{
    ui->setupUi(this);

    m_lengthUnits = DimensionUnit::getLengthUnits();
    m_areaUnits = DimensionUnit::getAreaUnits();
    m_angleUnits = DimensionUnit::getAngleUnits();

    initComboBox(m_lengthUnits, m_lengthUnit, ui->lengthsComboBox);
    initComboBox(m_areaUnits, m_areaUnit, ui->areasComboBox);
    initComboBox(m_angleUnits, m_angleUnit, ui->anglesComboBox);
    ui->scaleEdit->setValue(m_scale);

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(320, 240)));
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
    m_lengthUnit = m_lengthUnits[ui->lengthsComboBox->currentIndex()];
    m_areaUnit = m_areaUnits[ui->areasComboBox->currentIndex()];
    m_angleUnit = m_angleUnits[ui->anglesComboBox->currentIndex()];
    m_scale = ui->scaleEdit->value();

    QDialog::accept();
}
