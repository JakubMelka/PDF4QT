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
