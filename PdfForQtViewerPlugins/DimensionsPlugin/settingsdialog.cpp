//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include "pdfwidgetutils.h"

SettingsDialog::SettingsDialog(QWidget* parent, DimensionUnit& lengthUnit, DimensionUnit& areaUnit, DimensionUnit& angleUnit) :
    QDialog(parent),
    ui(new Ui::SettingsDialog),
    m_lengthUnit(lengthUnit),
    m_areaUnit(areaUnit),
    m_angleUnit(angleUnit)
{
    ui->setupUi(this);

    m_lengthUnits = DimensionUnit::getLengthUnits();
    m_areaUnits = DimensionUnit::getAreaUnits();
    m_angleUnits = DimensionUnit::getAngleUnits();

    initComboBox(m_lengthUnits, m_lengthUnit, ui->lengthsComboBox);
    initComboBox(m_areaUnits, m_areaUnit, ui->areasComboBox);
    initComboBox(m_angleUnits, m_angleUnit, ui->anglesComboBox);

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(320, 160)));
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

    QDialog::accept();
}
