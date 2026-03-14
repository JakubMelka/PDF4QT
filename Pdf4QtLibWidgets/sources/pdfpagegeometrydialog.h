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

#ifndef PDFPAGEGEOMETRYDIALOG_H
#define PDFPAGEGEOMETRYDIALOG_H

#include "pdfwidgetsglobal.h"
#include "pdfpagegeometry.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;

namespace pdf
{

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageGeometryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFPageGeometryDialog(QWidget* parent = nullptr);

    void setPageCount(PDFInteger pageCount);
    void setSettings(const PDFPageGeometrySettings& settings);
    PDFPageGeometrySettings getSettings() const;

private slots:
    void onPageSizePresetChanged();
    void onUseTargetPageSizeChanged();
    void onScaleContentChanged();
    void onAcceptClicked();

private:
    void setPageSizeInMM(const QSizeF& size);
    QSizeF getPageSizeInMM() const;
    void updateUi();

    QLabel* m_pageCountLabel;
    QLineEdit* m_pageRangeEdit;
    QComboBox* m_pageSubsetCombo;

    QComboBox* m_referenceBoxCombo;
    QCheckBox* m_applyMediaBoxCheckBox;
    QCheckBox* m_applyCropBoxCheckBox;
    QCheckBox* m_applyBleedBoxCheckBox;
    QCheckBox* m_applyTrimBoxCheckBox;
    QCheckBox* m_applyArtBoxCheckBox;

    QCheckBox* m_useTargetPageSizeCheckBox;
    QComboBox* m_pageSizePresetCombo;
    QDoubleSpinBox* m_widthSpinBox;
    QDoubleSpinBox* m_heightSpinBox;
    QDoubleSpinBox* m_marginLeftSpinBox;
    QDoubleSpinBox* m_marginTopSpinBox;
    QDoubleSpinBox* m_marginRightSpinBox;
    QDoubleSpinBox* m_marginBottomSpinBox;

    QComboBox* m_anchorCombo;
    QDoubleSpinBox* m_offsetXSpinBox;
    QDoubleSpinBox* m_offsetYSpinBox;

    QCheckBox* m_scaleContentCheckBox;
    QCheckBox* m_preserveAspectRatioCheckBox;
    QCheckBox* m_scaleAnnotationsAndFormFieldsCheckBox;
};

}   // namespace pdf

#endif // PDFPAGEGEOMETRYDIALOG_H
