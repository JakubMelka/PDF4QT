// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#ifndef PDFOCRCOMPRESSIONPREVIEWDIALOG_H
#define PDFOCRCOMPRESSIONPREVIEWDIALOG_H

#include "pdfviewerglobal.h"
#include "pdfocrcompression.h"

#include <QImage>
#include <QDialog>
#include <QFuture>

#include <memory>

class QLabel;
class QTimer;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QToolButton;
class QDialogButtonBox;

namespace pdf
{
class PDFDocument;
class PDFOCRCancelToken;
}

namespace pdfviewer
{

/// Comparison of the original and of the compressed image: the original is left of
/// the divider, the compressed image right of it. The divider is moved by the left
/// mouse button, the view is zoomed by the wheel and panned by the right button.
/// Both images are drawn with the same zoom and position (synchronized).
class PDFOCRComparisonView : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFOCRComparisonView(QWidget* parent);

    /// Sets the images; the compressed image may be null (the image is not compressed)
    void setImages(QImage original, QImage compressed, QString message);

    /// Zoom: 0 = fit into the view, otherwise the scale (1 = one image pixel per screen pixel)
    void setZoom(double zoom);
    double getZoom() const { return m_zoom; }

    /// Position of the divider (0-1 of the width of the view)
    void setDividerPosition(double position);
    double getDividerPosition() const { return m_divider; }

    virtual QSize sizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void wheelEvent(QWheelEvent* event) override;

private:
    QRectF getImageRect() const;

    QImage m_original;
    QImage m_compressed;
    QString m_message;
    double m_zoom = 0.0;
    double m_divider = 0.5;
    QPointF m_offset;
    QPoint m_lastMousePosition;
    bool m_draggingDivider = false;
    bool m_panning = false;
};

/// Preview of the compression of the scanned images (phase 3 of OCR_PLAN.md). The images
/// of the pages to be written are compressed in the background with the current settings,
/// the original and the result are compared side by side. The threshold of the conversion
/// to black and white can be changed live and single images can be excluded. The dialog
/// is accepted by the button Confirm, the settings are then taken back by the caller.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFOCRCompressionPreviewDialog : public QDialog
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    /// \param document Document
    /// \param pages Pages, whose images are going to be compressed
    /// \param currentPage Page shown first (if it is one of the pages)
    /// \param settings Current settings
    /// \param parent Parent widget
    explicit PDFOCRCompressionPreviewDialog(const pdf::PDFDocument* document,
                                            std::vector<pdf::PDFInteger> pages,
                                            pdf::PDFInteger currentPage,
                                            pdf::PDFOCRCompressionSettings settings,
                                            QWidget* parent);
    virtual ~PDFOCRCompressionPreviewDialog() override;

    /// Returns the settings (threshold, excluded images) confirmed by the user
    const pdf::PDFOCRCompressionSettings& getSettings() const { return m_settings; }

private:
    void schedulePreview();
    void startPreview();
    void onPreviewReady(int generation, std::vector<pdf::PDFOCRImageCompressor::Preview> previews);
    void updateImage();
    void updateThresholdControls();
    void setPageIndex(int index);

    const pdf::PDFDocument* m_document;
    std::vector<pdf::PDFInteger> m_pages;
    pdf::PDFOCRCompressionSettings m_settings;

    QComboBox* m_pageComboBox = nullptr;
    QToolButton* m_previousPageButton = nullptr;
    QToolButton* m_nextPageButton = nullptr;
    QComboBox* m_imageComboBox = nullptr;
    QComboBox* m_zoomComboBox = nullptr;
    QComboBox* m_thresholdComboBox = nullptr;
    QSpinBox* m_thresholdSpinBox = nullptr;
    QCheckBox* m_excludeCheckBox = nullptr;
    QLabel* m_infoLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    PDFOCRComparisonView* m_view = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
    QTimer* m_previewTimer = nullptr;

    int m_generation = 0;
    std::shared_ptr<pdf::PDFOCRCancelToken> m_token;
    QFuture<void> m_future;
    std::vector<pdf::PDFOCRImageCompressor::Preview> m_previews;
    bool m_updatingUi = false;
};

}   // namespace pdfviewer

#endif // PDFOCRCOMPRESSIONPREVIEWDIALOG_H
