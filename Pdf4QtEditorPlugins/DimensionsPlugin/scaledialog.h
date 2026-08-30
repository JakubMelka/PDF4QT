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

#ifndef SCALEDIALOG_H
#define SCALEDIALOG_H

#include "dimensionunits.h"

#include <QDialog>

class QComboBox;

namespace Ui
{
class ScaleDialog;
}

/// Dialog, which defines the scale of a drawing. It is used both for the manual
/// definition of a scale preset and for the calibration, where the distance
/// measured on the paper is picked in the document by the user.
class ScaleDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode
    {
        Edit,           ///< Both distances are entered by the user
        Calibration     ///< Distance on the paper was picked in the document
    };

    /// Creates the dialog for editing of the scale
    /// \param parent Parent widget
    /// \param scale Edited scale
    /// \param mode Mode of the dialog
    explicit ScaleDialog(QWidget* parent, const DimensionScale& scale, Mode mode);

    /// Creates the dialog for the calibration of the scale. Distance on the paper
    /// is filled from the picked line and the unit, in which it is displayed,
    /// is taken from the current scale.
    /// \param parent Parent widget
    /// \param measuredLength Length of the picked line in points, the UserUnit
    ///        of the page is already applied to it
    /// \param currentScale Currently used scale
    explicit ScaleDialog(QWidget* parent, pdf::PDFReal measuredLength, const DimensionScale& currentScale);

    virtual ~ScaleDialog() override;

    /// Returns the scale defined by the dialog
    DimensionScale getScale() const;

private:
    void initialize(const DimensionScale& scale, Mode mode);
    void initializeUnitComboBox(QComboBox* comboBox, const QByteArray& currentUnitId);
    void updateResultLabel();

    /// Called, when the user selects a different unit of the distance on the paper.
    /// In the calibration mode the picked length is recalculated, so the same
    /// physical distance is displayed in the new unit.
    void onPaperUnitChanged();

    Ui::ScaleDialog* ui;

    Mode m_mode;

    /// Length of the picked line in points, valid in the calibration mode only
    pdf::PDFReal m_measuredLength;

    DimensionUnits m_lengthUnits;
};

#endif // SCALEDIALOG_H
