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

#ifndef PDFOPTIMIZEIMAGESDIALOG_H
#define PDFOPTIMIZEIMAGESDIALOG_H

#include "pdfimageoptimizer.h"

#include <QDialog>
#include <QFuture>
#include <QFutureWatcher>
#include <QPushButton>

#include <optional>

namespace Ui
{
class PDFOptimizeImagesDialog;
}

namespace pdfviewer
{

class PDFOptimizeImagesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFOptimizeImagesDialog(const pdf::PDFDocument* document,
                                     pdf::PDFProgress* progress,
                                     QWidget* parent);
    virtual ~PDFOptimizeImagesDialog() override;

    pdf::PDFDocument takeOptimizedDocument() { return qMove(m_optimizedDocument); }

private:
    struct ImageEntry
    {
        pdf::PDFImageOptimizer::ImageInfo info;
        bool enabled = true;
        bool overrideEnabled = false;
        pdf::PDFImageOptimizer::Settings overrideSettings = pdf::PDFImageOptimizer::Settings::createDefault();
    };

    void loadImages();
    void updateUi();
    void updatePreview();
    void updateSelectedImageUi();

    void loadSettingsToUi(const pdf::PDFImageOptimizer::Settings& settings);
    void applyUiToSettings(pdf::PDFImageOptimizer::Settings& settings);
    pdf::PDFImageOptimizer::Settings& activeSettings();

    ImageEntry* getSelectedEntry();
    const ImageEntry* getSelectedEntry() const;

private slots:
    void onOptimizeButtonClicked();
    void onOptimizationFinished();
    void onSettingsChanged();
    void onSelectionChanged();
    void onOverrideToggled(bool checked);
    void onImageEnabledToggled(bool checked);
    void onBitonalThresholdAutoToggled(bool checked);

private:
    Ui::PDFOptimizeImagesDialog* ui;
    const pdf::PDFDocument* m_document;
    pdf::PDFProgress* m_progress;
    bool m_optimizationInProgress;
    bool m_optimized;
    bool m_updatingUi;
    QPushButton* m_optimizeButton;
    QFuture<void> m_future;
    std::optional<QFutureWatcher<void>> m_futureWatcher;

    std::vector<ImageEntry> m_images;
    pdf::PDFImageOptimizer::Settings m_settings;
    pdf::PDFDocument m_optimizedDocument;
};

}   // namespace pdfviewer

#endif // PDFOPTIMIZEIMAGESDIALOG_H
