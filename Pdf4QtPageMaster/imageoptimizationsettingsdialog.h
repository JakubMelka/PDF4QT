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

#ifndef PDFPAGEMASTER_IMAGEOPTIMIZATIONSETTINGSDIALOG_H
#define PDFPAGEMASTER_IMAGEOPTIMIZATIONSETTINGSDIALOG_H

#include "pdfimageoptimizer.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QSpinBox;

namespace pdfpagemaster
{

class ImageOptimizationSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImageOptimizationSettingsDialog(pdf::PDFImageOptimizer::Settings settings, QWidget* parent);

    pdf::PDFImageOptimizer::Settings getSettings() const { return m_settings; }

private:
    void loadSettingsToUi();
    void applyUiToSettings();
    void setupProfileTab(QWidget* tab,
                         QComboBox*& algorithmComboBox,
                         QSpinBox*& dpiSpinBox,
                         QComboBox*& resampleComboBox,
                         QSpinBox*& jpegQualitySpinBox,
                         bool addJpegQuality);

    pdf::PDFImageOptimizer::Settings m_settings;

    QComboBox* m_modeComboBox = nullptr;
    QComboBox* m_colorModeComboBox = nullptr;
    QComboBox* m_goalComboBox = nullptr;
    QCheckBox* m_keepOriginalCheckBox = nullptr;
    QCheckBox* m_preserveTransparencyCheckBox = nullptr;

    QComboBox* m_colorAlgorithmComboBox = nullptr;
    QSpinBox* m_colorDpiSpinBox = nullptr;
    QComboBox* m_colorResampleComboBox = nullptr;
    QSpinBox* m_colorJpegQualitySpinBox = nullptr;

    QComboBox* m_grayAlgorithmComboBox = nullptr;
    QSpinBox* m_grayDpiSpinBox = nullptr;
    QComboBox* m_grayResampleComboBox = nullptr;
    QSpinBox* m_grayJpegQualitySpinBox = nullptr;

    QComboBox* m_bitonalAlgorithmComboBox = nullptr;
    QSpinBox* m_bitonalDpiSpinBox = nullptr;
    QComboBox* m_bitonalResampleComboBox = nullptr;
};

}   // namespace pdfpagemaster

#endif // PDFPAGEMASTER_IMAGEOPTIMIZATIONSETTINGSDIALOG_H
