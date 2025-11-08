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
#include "pdfdocument.h"

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

/// Dialog that previews and executes image optimization for a document.
/// Provides global settings and per-image overrides, then runs the optimizer
/// asynchronously to keep the UI responsive.
class PDFOptimizeImagesDialog : public QDialog
{
    Q_OBJECT

public:
    /// Creates the dialog for a document.
    /// \param document Source document (not owned).
    /// \param progress Progress reporter (may be null).
    /// \param parent Parent widget.
    explicit PDFOptimizeImagesDialog(const pdf::PDFDocument* document,
                                     pdf::PDFProgress* progress,
                                     QWidget* parent);
    virtual ~PDFOptimizeImagesDialog() override;

    /// Returns the optimized document (moves the stored instance).
    /// Call after optimization is finished.
    pdf::PDFDocument takeOptimizedDocument() { return qMove(m_optimizedDocument); }

private:
    /// UI state and settings for a single image row.
    struct ImageEntry
    {
        pdf::PDFImageOptimizer::ImageInfo info; ///< Image metadata and pixels.
        bool enabled = true; ///< Whether this image participates in optimization.
        bool overrideEnabled = false; ///< Whether per-image settings override global settings.
        pdf::PDFImageOptimizer::Settings overrideSettings = pdf::PDFImageOptimizer::Settings::createDefault(); ///< Custom settings.
    };

    /// Populates the internal list of images and initializes UI state.
    void loadImages();
    /// Refreshes control states, summaries, and enablement.
    void updateUi();
    /// Refreshes the preview panel for the current selection.
    void updatePreview();
    /// Refreshes widgets related to the currently selected image.
    void updateSelectedImageUi();

    /// Loads settings values into UI widgets.
    void loadSettingsToUi(const pdf::PDFImageOptimizer::Settings& settings);
    /// Reads UI widget values into the supplied settings instance.
    void applyUiToSettings(pdf::PDFImageOptimizer::Settings& settings);
    /// Returns active settings for the current selection (global or override).
    pdf::PDFImageOptimizer::Settings& activeSettings();

    /// Returns the selected image entry or null if none is selected.
    ImageEntry* getSelectedEntry();
    /// Returns the selected image entry or null if none is selected.
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
