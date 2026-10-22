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

#ifndef PDFAPPPRINTDIALOG_H
#define PDFAPPPRINTDIALOG_H

#include "pdfappglobal.h"
#include "pdfappprint.h"

#include "pdfmessagebox.h"

#include <QFutureWatcher>
#include <QImage>
#include <QPointer>
#include <QWidget>

#include <memory>
#include <vector>

class QCheckBox;
class QPushButton;
class QScrollArea;

namespace pdf
{

class PDFAppDocument;
class PDFComboBox;
class PDFIconBadge;
class PDFIconButton;
class PDFLabel;
class PDFPageSelection;
class PDFSegmentedControl;
class PDFSpinBox;

/// Preview of the print dialog (1029:24642 preview-stage): the paper of the
/// selected paper size and orientation (bg/reading paper with the shadow) on the
/// stage bg/track, radius 12, padding 16, with the picture of the page rendered by
/// a worker. Not interactive.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPrintPreview : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Size of the stage of the design (300 x 378)
    static constexpr int STAGE_WIDTH = 300;
    static constexpr int STAGE_HEIGHT = 378;
    static constexpr int STAGE_PADDING = 16;
    static constexpr int STAGE_RADIUS = 12;

    explicit PDFAppPrintPreview(QWidget* parent);
    virtual ~PDFAppPrintPreview() override;

    /// Paper (points, oriented) shown on the stage
    void setPaperSize(const QSizeF& paperSize);
    QSizeF getPaperSize() const { return m_paperSize; }

    /// Rectangle of the paper inside the widget
    QRectF getPaperRect() const;

    /// Picture of the paper (nullptr image = an empty paper while it is rendered)
    void setImage(const QImage& image);
    const QImage& getImage() const { return m_image; }

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QSizeF m_paperSize = QSizeF(595, 842);
    QImage m_image;
};

/// Print dialog of the design on all platforms (O14: pdf4qt-document-print
/// 1029:24203, no printer 1029:24881): the message box card 880 wide with the badge
/// of the printer, the preview of the selected pages (300 px), the settings
/// (printer with its state and Printer properties… of the driver on Windows, pages
/// by PDFPageSelection with Selected preselected for a selection of thumbnails (O9),
/// copies and collate, orientation, paper size, scaling, color), the amber note
/// without a printer (Refresh) and the summary with Cancel / Print. Two-sided
/// printing is not offered in phase 1. A narrow window hides the preview and
/// scrolls the settings.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPrintDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    /// Width of the card of the design
    static constexpr int CARD_WIDTH = 880;

    /// Width of the labels of the rows of the settings
    static constexpr int LABEL_WIDTH = 88;

    /// \param initialPages Pages (from 0) of the mode Selected (the selection of the
    ///        thumbnails); more than one page or another page than the current one
    ///        preselects the mode Selected, else the mode All
    explicit PDFAppPrintDialog(PDFAppDocument* document, PDFInteger currentPage, const std::vector<PDFInteger>& initialPages,
                               const PDFCMSSettings& cmsSettings, QWidget* parent);
    virtual ~PDFAppPrintDialog() override;

    /// Opens the dialog with the range of the pages (Print the Rest…)
    void setRange(const QString& range);
    void setCopies(int copies);

    /// Printers offered by the dialog
    const std::vector<PDFAppPrinterInfo>& getPrinters() const { return m_printers; }

    /// Index of the selected printer (-1 = none)
    int getPrinterIndex() const;
    void setPrinterIndex(int index);

    /// Reads the printers again (Refresh)
    void refreshPrinters();

    /// The printer of the job (created for the selected printer, the settings of
    /// the driver applied); nullptr without a printer
    std::shared_ptr<QPrinter> getPrinter() const { return m_printer; }

    /// Settings of the job from the controls
    PDFAppPrintSettings getSettings() const;

    /// The document allows only the low resolution
    bool isLowResolution() const { return m_isLowResolution; }

    /// Texts of the dialog
    QString getSummaryText() const;
    QString getPreviewText() const;
    QString getPaperText() const;
    QString getPrinterStatusText() const;

    /// Page of the preview (index into the result pages)
    int getPreviewIndex() const { return m_previewIndex; }
    void setPreviewIndex(int index);

    /// A preview is being rendered
    bool isPreviewRendering() const { return !m_previewWatcher.isNull(); }

    /// Parts of the dialog (tests, accessibility)
    PDFComboBox* getPrinterCombo() const;
    QPushButton* getPropertiesButton() const;
    PDFPageSelection* getPageSelection() const;
    PDFSpinBox* getCopiesSpinBox() const;
    QCheckBox* getCollateCheckBox() const;
    PDFSegmentedControl* getOrientationControl() const;
    PDFComboBox* getPaperCombo() const;
    PDFSegmentedControl* getScalingControl() const;
    PDFSpinBox* getScaleSpinBox() const;
    PDFSegmentedControl* getColorControl() const;
    QWidget* getNoPrinterNote() const;
    QPushButton* getRefreshButton() const;
    QPushButton* getPrintButton() const;
    QPushButton* getCancelButton() const;
    PDFAppPrintPreview* getPreview() const;
    PDFIconButton* getPreviousButton() const;
    PDFIconButton* getNextButton() const;

Q_SIGNALS:
    /// A preview has been rendered (tests)
    void previewRendered();

public:
    virtual void setVisible(bool visible) override;

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void createHead();
    QWidget* createPreviewColumn();
    QWidget* createSettingsColumn();
    QWidget* createRow(const QString& label, QWidget* control, bool isTopAligned);
    void createNoPrinterNote();
    void createActions();

    void fillPaperSizes();
    void createPrinterForSelection();
    void updateControls();
    void updateSummary();
    void updatePreviewNavigation();
    /// Page of the preview (from 1, 0 = none), its paper (points, oriented) and
    /// the printable area of the printer on it (points)
    class PreviewGeometry
    {
    public:
        int page = 0;
        QRectF paperRect;
        QRectF printableRect;
    };

    PreviewGeometry getPreviewGeometry(const PDFAppPrintSettings& settings) const;
    void schedulePreview();
    void startPreview();
    void adaptToHost();

    void onPrinterChanged(int index);
    void onPropertiesClicked();
    void onRefreshClicked();
    void onPrintClicked();
    void onSettingsChanged();
    void onScalingChanged(int index);
    void onPreviousClicked();
    void onNextClicked();
    void onPreviewFinished();

    /// Pages of the result (from 0)
    std::vector<PDFInteger> getResultPages() const;

    QPointer<PDFAppDocument> m_document;
    PDFDocumentPointer m_pdfDocument;
    PDFCMSSettings m_cmsSettings;
    PDFInteger m_currentPage = 0;
    bool m_isLowResolution = false;
    bool m_isRealText = false;
    std::vector<PDFAppPrinterInfo> m_printers;
    std::shared_ptr<QPrinter> m_printer;
    int m_previewIndex = 0;
    int m_previewRequest = 0;
    bool m_isPreviewPending = false;

    QPointer<PDFLabel> m_titleLabel;
    QPointer<PDFLabel> m_subtitleLabel;
    QPointer<QScrollArea> m_scrollArea;
    QPointer<QWidget> m_previewColumn;
    QPointer<PDFAppPrintPreview> m_preview;
    QPointer<PDFIconButton> m_previousButton;
    QPointer<PDFIconButton> m_nextButton;
    QPointer<PDFLabel> m_previewLabel;
    QPointer<PDFLabel> m_paperLabel;
    QPointer<PDFComboBox> m_printerCombo;
    QPointer<QPushButton> m_propertiesButton;
    QPointer<QWidget> m_printerStatus;
    QPointer<PDFIconBadge> m_printerStatusIcon;
    QPointer<PDFLabel> m_printerStatusLabel;
    QPointer<PDFPageSelection> m_pageSelection;
    QPointer<PDFSpinBox> m_copiesSpinBox;
    QPointer<QCheckBox> m_collateCheckBox;
    QPointer<PDFSegmentedControl> m_orientationControl;
    QPointer<PDFComboBox> m_paperCombo;
    QPointer<PDFSegmentedControl> m_scalingControl;
    QPointer<PDFSpinBox> m_scaleSpinBox;
    QPointer<PDFSegmentedControl> m_colorControl;
    QPointer<QWidget> m_noPrinterNote;
    QPointer<QPushButton> m_refreshButton;
    QPointer<PDFLabel> m_summaryLabel;
    QPointer<QPushButton> m_cancelButton;
    QPointer<QPushButton> m_printButton;
    QPointer<QFutureWatcher<QImage>> m_previewWatcher;
    QPointer<QWidget> m_host;
};

}   // namespace pdf

#endif // PDFAPPPRINTDIALOG_H
