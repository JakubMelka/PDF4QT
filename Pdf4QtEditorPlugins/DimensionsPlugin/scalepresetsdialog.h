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

#ifndef SCALEPRESETSDIALOG_H
#define SCALEPRESETSDIALOG_H

#include "dimensionunits.h"

#include <QDialog>

#include <vector>

namespace Ui
{
class ScalePresetsDialog;
}

/// Dialog for the management of the named scale presets. The presets are shared
/// by all documents and are stored in the application settings.
class ScalePresetsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScalePresetsDialog(QWidget* parent, std::vector<DimensionScale> presets);
    virtual ~ScalePresetsDialog() override;

    /// Returns the edited presets
    const std::vector<DimensionScale>& getPresets() const { return m_presets; }

private:
    void updateTable();
    void updateActions();

    /// Returns the index of the selected preset, or -1, if no preset is selected
    int getSelectedIndex() const;

    void onAddTriggered();
    void onEditTriggered();
    void onRemoveTriggered();
    void onRestoreDefaultsTriggered();

    Ui::ScalePresetsDialog* ui;

    std::vector<DimensionScale> m_presets;
};

#endif // SCALEPRESETSDIALOG_H
