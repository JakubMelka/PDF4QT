//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

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
