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

#ifndef PDFSCANPREPARATIONDIALOG_H
#define PDFSCANPREPARATIONDIALOG_H

#include "pdfviewerglobal.h"
#include "pdfscanpreparation.h"
#include "pdfmeshqualitysettings.h"

#include <QImage>
#include <QDialog>
#include <QFuture>
#include <QTimer>

#include <map>
#include <set>
#include <memory>
#include <optional>

class QLabel;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QListWidget;
class QToolButton;
class QPushButton;
class QTableWidget;
class QProgressBar;
class QStackedWidget;
class QDoubleSpinBox;
class QDialogButtonBox;

namespace pdf
{
class PDFCMS;
class PDFDrawWidgetProxy;
class PDFOCRCancelToken;
class PDFOptionalContentActivity;
}

namespace pdfviewer
{

/// View of a page in the preparation of the scan. It shows the rendered visible page
/// with the overlays of the plan: the halves of the split with the gutter, the crop
/// rectangles (the area outside of them is darkened), the live rotation of the deskew
/// (the rendered image is rotated in memory, the page is not rendered again) and an
/// auxiliary grid. The manual crop is edited by its eight handles, the split line is
/// moved by the mouse. The wheel zooms, the right button pans.
class PDFScanPreparationPageView : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFScanPreparationPageView(QWidget* parent);

    /// Overlay of the plan of the page (visible points of the page)
    struct Overlay
    {
        std::vector<QRectF> regions;    ///< Regions (the halves of the split, or the whole page)
        std::vector<QRectF> crops;      ///< Crop of each region (empty = the region)
        std::vector<double> angles;     ///< Deskew of each region (degrees)
        std::optional<double> splitPosition;
        bool splitVertical = true;
        double gutterWidth = 0.0;
        bool cropEditable = false;      ///< The crop of the (single) region can be edited
        bool showGrid = false;
        double gridSpacing = 20.0;
    };

    /// Sets the rendered image of the visible page and the size of the page in points
    void setPage(QImage image, QSizeF visibleSize);

    /// Clears the page and shows the message
    void clear(const QString& message);

    void setOverlay(const Overlay& overlay);

    /// Result mode: the rendered output pages of the source page are shown side by side
    void setResult(std::vector<QImage> images, const QString& message);
    void setShowResult(bool showResult);
    bool isShowingResult() const { return m_showResult; }

    void setZoom(double zoom);
    double getZoom() const { return m_zoom; }

signals:
    void cropEdited(QRectF crop);
    void splitPositionEdited(double position);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void wheelEvent(QWheelEvent* event) override;

private:
    enum class Drag
    {
        None,
        Handle,
        Move,
        Split,
        Pan
    };

    /// Transformation from the visible points of the page to the widget
    QTransform getPageToWidget() const;

    /// Returns the handles of the crop rectangle (widget coordinates)
    std::vector<QPointF> getHandles(const QRectF& rect) const;

    void paintResult(QPainter& painter);

    QImage m_image;
    QSizeF m_visibleSize;
    QString m_message;
    Overlay m_overlay;
    std::vector<QImage> m_resultImages;
    QString m_resultMessage;
    bool m_showResult = false;
    double m_zoom = 1.0;
    QPointF m_offset;

    Drag m_drag = Drag::None;
    int m_dragHandle = -1;
    QPointF m_dragStart;
    QRectF m_dragStartRect;
    QPoint m_lastMousePosition;
};

/// Preparation of the scanned pages before the recognition (phase 5 of OCR_PLAN.md):
/// crop, split of the spreads and permanent deskew of the checked pages. The detection
/// only proposes the values; they are shown in the view with their confidence and can
/// be changed. The result is a new document, which replaces the document of the editor
/// as one step of the undo history. Nothing is re-encoded.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFScanPreparationDialog : public QDialog
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    struct Context
    {
        const pdf::PDFDocument* document = nullptr;
        pdf::PDFDrawWidgetProxy* proxy = nullptr;
        const pdf::PDFCMS* cms = nullptr;
        bool hasSignatures = false;
        pdf::PDFInteger currentPage = 0;
    };

    explicit PDFScanPreparationDialog(const Context& context, QWidget* parent);
    virtual ~PDFScanPreparationDialog() override;

    enum class CropMode
    {
        None,
        Automatic,
        Manual,
        SameSize
    };

    enum class SplitMode
    {
        None,
        SideBySide,
        OneAboveAnother
    };

    /// Settings of a page (visible points)
    struct PageSettings
    {
        bool deskew = false;
        double deskewAngle = 0.0;
        bool deskewEdited = false;

        SplitMode splitMode = SplitMode::None;
        double splitPosition = -1.0;    ///< Negative = the middle of the page
        double gutterWidth = 0.0;
        bool rightToLeft = false;
        bool splitEdited = false;

        CropMode cropMode = CropMode::None;
        QRectF manualCrop;
        double cropMargin = 14.17;      ///< 5 mm

        bool operator==(const PageSettings&) const = default;
    };

    /// Returns the plan of the current settings (unchecked pages are not changed)
    pdf::PDFScanPreparation::Plan createPlan() const;

    /// Returns the prepared document (valid after the dialog was accepted)
    bool hasResultDocument() const { return !m_resultDocument.isNull(); }
    pdf::PDFDocumentPointer takeResultDocument() { return std::move(m_resultDocument); }

    /// Returns the settings of the page (for the tests)
    const PageSettings& getPageSettings(pdf::PDFInteger pageIndex) const;

    /// Returns true, if the page has its analysis
    bool hasAnalysis(pdf::PDFInteger pageIndex) const { return m_analysis.count(pageIndex) > 0; }

    virtual void done(int result) override;

private:
    // Initialization
    void createUi();
    void loadSettings();
    void saveSettings() const;

    // Pages
    void updatePageItem(pdf::PDFInteger pageIndex);
    void updatePageFilter();
    std::vector<pdf::PDFInteger> getCheckedPages() const;
    void setCurrentPage(pdf::PDFInteger pageIndex);

    // Background work
    void startAnalysis();
    void stopAnalysis();
    void onAnalysisReady(int generation, pdf::PDFInteger pageIndex, pdf::PDFScanPreparation::PageAnalysis analysis);
    void onAnalysisFinished(int generation);
    void requestRender(pdf::PDFInteger pageIndex);
    void onRenderReady(pdf::PDFInteger pageIndex, QImage image);
    void scheduleResult();
    void startResult();

    /// Proposes the detected values for a page, which was not edited
    void applyDetection(pdf::PDFInteger pageIndex);

    // Settings of the current page
    void updateControls();
    void onControlsChanged();
    void updateView();
    void updateSummary();
    void updateTable();
    void updateTableRow(int row, pdf::PDFInteger pageIndex);
    void onTableItemChanged(int row, int column);
    void applyToChecked(bool angle, bool split, bool crop);
    void resetPage();
    void rotateCurrentPage(double delta);
    void onApply();

    /// Returns the regions of the page (the halves of the split, or the whole page)
    std::vector<QRectF> getRegions(pdf::PDFInteger pageIndex, const PageSettings& settings) const;

    /// Returns the output pages of the page by its settings
    std::vector<pdf::PDFScanPreparation::OutputPage> getOutputPages(pdf::PDFInteger pageIndex) const;

    /// Returns the effective deskew angle of the page (0, if the deskew is off or below the minimum)
    double getEffectiveAngle(const PageSettings& settings) const;

    /// Size of the content used by the crop mode "Same size" (median of the content of the
    /// checked pages, separately for the odd and even pages, if requested)
    QSizeF getSameSize(pdf::PDFInteger pageIndex) const;

    QSizeF getVisibleSize(pdf::PDFInteger pageIndex) const;
    PageSettings& getEditableSettings(pdf::PDFInteger pageIndex);

    Context m_context;
    pdf::PDFInteger m_pageCount = 0;
    pdf::PDFInteger m_currentPage = -1;
    pdf::PDFOptionalContentActivity* m_optionalContentActivity = nullptr;
    pdf::PDFMeshQualitySettings m_meshQualitySettings;

    std::map<pdf::PDFInteger, PageSettings> m_settings;
    std::map<pdf::PDFInteger, pdf::PDFScanPreparation::PageAnalysis> m_analysis;
    std::map<pdf::PDFInteger, QImage> m_images;
    std::vector<pdf::PDFInteger> m_imageOrder;
    std::set<pdf::PDFInteger> m_pendingRenders;
    mutable std::map<pdf::PDFInteger, QSizeF> m_sameSizeCache;
    mutable bool m_sameSizeCacheValid = false;

    // Widgets
    QListWidget* m_pageListWidget = nullptr;
    QComboBox* m_pageFilterComboBox = nullptr;
    QDoubleSpinBox* m_filterSkewSpinBox = nullptr;
    PDFScanPreparationPageView* m_view = nullptr;
    QTableWidget* m_tableWidget = nullptr;
    QStackedWidget* m_centerStack = nullptr;
    QPushButton* m_analyzeButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QProgressBar* m_analysisProgressBar = nullptr;
    QCheckBox* m_deskewCheckBox = nullptr;
    QDoubleSpinBox* m_deskewAngleSpinBox = nullptr;
    QLabel* m_deskewDetectedLabel = nullptr;
    QPushButton* m_deskewResetButton = nullptr;
    QDoubleSpinBox* m_deskewMinimumSpinBox = nullptr;
    QComboBox* m_splitModeComboBox = nullptr;
    QDoubleSpinBox* m_splitPositionSpinBox = nullptr;
    QDoubleSpinBox* m_gutterWidthSpinBox = nullptr;
    QComboBox* m_readingOrderComboBox = nullptr;
    QLabel* m_splitDetectedLabel = nullptr;
    QComboBox* m_cropModeComboBox = nullptr;
    QDoubleSpinBox* m_cropMarginSpinBox = nullptr;
    QCheckBox* m_cropOddEvenCheckBox = nullptr;
    QLabel* m_cropSizeLabel = nullptr;
    QToolButton* m_applyToCheckedButton = nullptr;
    QPushButton* m_resetPageButton = nullptr;
    QCheckBox* m_gridCheckBox = nullptr;
    QDoubleSpinBox* m_gridSpacingSpinBox = nullptr;
    QToolButton* m_resultViewButton = nullptr;
    QToolButton* m_tableViewButton = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_warningLabel = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;

    bool m_updatingUi = false;
    int m_analysisGeneration = 0;
    int m_resultGeneration = 0;
    std::shared_ptr<pdf::PDFOCRCancelToken> m_analysisToken;
    std::shared_ptr<pdf::PDFOCRCancelToken> m_resultToken;
    std::vector<QFuture<void>> m_futures;
    QTimer m_resultTimer;
    pdf::PDFDocumentPointer m_resultDocument;
};

}   // namespace pdfviewer

#endif // PDFSCANPREPARATIONDIALOG_H
