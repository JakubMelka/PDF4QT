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

#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "dimensiontool.h"

#include <QDialog>

class QComboBox;

namespace Ui
{
class SettingsDialog;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent,
                            DimensionUnit& lengthUnit,
                            DimensionUnit& areaUnit,
                            DimensionUnit& angleUnit);
    virtual ~SettingsDialog() override;

    virtual void accept() override;

private:
    Ui::SettingsDialog* ui;

    void initComboBox(const DimensionUnits& units, const DimensionUnit& currentUnit, QComboBox* comboBox);

    DimensionUnits m_lengthUnits;
    DimensionUnits m_areaUnits;
    DimensionUnits m_angleUnits;
    DimensionUnit& m_lengthUnit;
    DimensionUnit& m_areaUnit;
    DimensionUnit& m_angleUnit;
};

#endif // SETTINGSDIALOG_H
