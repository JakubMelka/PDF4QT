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

#ifndef PDFAPPCOMPAREVIEW_H
#define PDFAPPCOMPAREVIEW_H

#include "pdfappglobal.h"
#include "pdfapptoolpanelparts.h"
#include "pdfappworkspace.h"

#include "pdfcomponentbadges.h"
#include "pdfdiff.h"
#include "pdfdiffreport.h"
#include "pdfdocumentdrawinterface.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdffluentwidgets.h"
#include "pdfoptionalcontent.h"
#include "pdfcms.h"

#include <QColor>
#include <QPointer>
#include <QWidget>

#include <map>
#include <optional>
#include <vector>

class QStackedWidget;

namespace pdf
{

class PDFAppParagraph;
class PDFAppToolCallout;
class PDFCMSManager;
class PDFIconBadge;
class PDFLabel;
class PDFOptionalContentActivity;
class PDFWidget;

/// The overlay of Compare (the section Overlay, 493:18777, 493:19325; the old Diff, plan 4.10): the blend of the
/// two pages of a pair, the page sizes (K7.4: a combo of three modes), the scales of Manual and the shift of the
/// revised page in millimeters. Only the view changes - the documents, the result and the reports never do.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCompareOverlaySettings
{
public:
    enum class ScaleMode
    {
        Original,   ///< Original sizes: the pages keep their sizes, aligned at the top
        Fit,        ///< Fit smaller page to larger: each page is enlarged proportionally to the common size, both centered
        Manual      ///< Manual scale: each document gets its own percentage, both centered
    };

    /// Default share of the revised page in the blend (493:18777 "50 % / 50 %")
    static constexpr int DEFAULT_BLEND = 50;

    /// Limits of the scale of Manual (in per cent) and of the shift (mm)
    static constexpr double MINIMUM_SCALE = 10.0;
    static constexpr double MAXIMUM_SCALE = 1000.0;
    static constexpr double MAXIMUM_OFFSET = 1000.0;

    ScaleMode scaleMode = ScaleMode::Original;
    double originalScale = 1.0;     ///< Scale of the original page (Manual), 1 = 100 %
    double revisedScale = 1.0;      ///< Scale of the revised page (Manual)
    QPointF revisedOffsetMM;        ///< Shift of the revised page against the original one (mm, x to the right, y down)
    int blend = DEFAULT_BLEND;      ///< Share of the revised page in per cent, 0 = only the original, 100 = only the revised

    bool operator==(const PDFAppCompareOverlaySettings&) const = default;
};

/// The page layout of the views of Compare (a rewrite of ComparedDocumentMapper of Pdf4QtDiff/utils.cpp, plan 4.9 -
/// not a copy): the custom layout of PDFDrawWidgetProxy for the document of a view (PDFDiffReportBuilder::
/// createCombinedDocument) and the mapping between its pages and the pages of the two inputs. The pairs of the page
/// sequence of the result are laid out top down, 5 mm apart:
///  - SideBySide: the original page left of the axis, the revised page right of it (10 mm apart);
///  - Original / Revised: the pages of one document centered on the axis;
///  - Overlay: a pair is two pages of the groups 1 (original) and 2 (revised) over each other - the sizes by
///    PDFAppCompareOverlaySettings (Original at the top, Fit / Manual centered), the revised one shifted by the
///    offset; a page without its pair stays alone (no group, no scale).
/// Only Pages with Differences keeps the pairs with a changed page of the shown result. The sizes are the rotated
/// media boxes of the pages of the shown document (never changed). A page of the sequence, which the shown document
/// does not hold, is skipped (hostile or stale input never indexes out of range). A value; no Qt object.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCompareLayout
{
public:
    using LayoutItems = PDFDrawSpaceController::LayoutItems;

    /// Gap between two pairs and between the two pages of Side by Side (mm, the old Diff)
    static constexpr double PAGE_GAP = 5.0;

    /// Page groups of the overlay (the transparency of PDFDrawWidgetProxy::setGroupTransparency)
    static constexpr PDFInteger ORIGINAL_GROUP = 1;
    static constexpr PDFInteger REVISED_GROUP = 2;

    /// Lays the pages out
    /// \param view The view
    /// \param isOnlyChangedPages Only the pairs, whose original or revised page has a shown difference
    /// \param result The shown result (its page sequence and its changed pages)
    /// \param document The shown document (nullptr = no layout)
    /// \param pages Per page of \p document its page of the original or of the revised input (PDFDiffCombinedDocument::pages)
    /// \param overlay The overlay (Overlay only)
    void update(PDFDiffReportView view,
                bool isOnlyChangedPages,
                const PDFDiffResult& result,
                const PDFDocument* document,
                const std::vector<PDFDiffCombinedPage>& pages,
                const PDFAppCompareOverlaySettings& overlay);

    const LayoutItems& getLayout() const { return m_layout; }

    /// The page of the original (revised) input shown by the page of the shown document, -1 = none
    PDFInteger getOriginalPageIndex(PDFInteger pageIndex) const;
    PDFInteger getRevisedPageIndex(PDFInteger pageIndex) const;

    /// The page of the shown document, which shows the page of the original (revised) input and is laid out, -1 = none
    PDFInteger getPageIndexFromOriginal(PDFInteger originalPageIndex) const;
    PDFInteger getPageIndexFromRevised(PDFInteger revisedPageIndex) const;

private:
    void addPage(PDFInteger pageIndex, PDFInteger groupIndex, const QRectF& rect, PDFInteger originalPageIndex, PDFInteger revisedPageIndex);

    LayoutItems m_layout;
    std::map<PDFInteger, PDFInteger> m_originalOfPage;      ///< Page of the shown document → page of the original
    std::map<PDFInteger, PDFInteger> m_revisedOfPage;       ///< Page of the shown document → page of the revised
    std::map<PDFInteger, PDFInteger> m_pageOfOriginal;      ///< Page of the original → page of the shown document
    std::map<PDFInteger, PDFInteger> m_pageOfRevised;       ///< Page of the revised → page of the shown document
};

/// What the canvas of Compare draws over the pages (the old DifferencesDrawInterface rewritten, plan 4.9; 492:19314):
/// a rectangle of each shown difference in the color of its kind with 50 % opacity (Difference Highlights), a
/// marker at the edge of the view at its height (Markers at the Edge: an arrow at the left edge for an original
/// page, at the right edge for a revised page), the bubble of a moved, added or removed page ("5 → 6", "+", "−")
/// and the outline of the selected difference in the accent color (K7.25: never the color of Moved). The
/// rectangles are indexed per page when the result is set (any number of differences; a page draws only its own).
/// It runs in the GUI thread (the paint of the canvas).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCompareDrawInterface : public IDocumentDrawInterface
{
public:
    explicit PDFAppCompareDrawInterface() = default;
    virtual ~PDFAppCompareDrawInterface() override = default;

    /// The shown result (the interface keeps a copy of its index) and the layout of the view (owned by the view)
    void setResult(const PDFDiffResult& result);
    void setLayout(const PDFAppCompareLayout* layout) { m_layout = layout; }

    void setColors(const PDFDiffReportColors& colors) { m_colors = colors; }
    void setHighlightsShown(bool isShown) { m_isHighlightsShown = isShown; }
    void setMarkersShown(bool isShown) { m_isMarkersShown = isShown; }

    /// The selected difference (an index of the shown result), std::nullopt = none
    void setSelectedDifference(std::optional<size_t> index) { m_selectedIndex = index; }

    /// The color of the kind of the difference of the shown result (replaced, removed, added, moved)
    static QColor getColor(const PDFDiffResult& result, size_t index, const PDFDiffReportColors& colors);

    /// The text of the bubble of a page difference ("5 → 6", "+", "−"), empty for another difference
    static QString getBubbleText(const PDFDiffResult& result, size_t index);

    /// Rectangles of the page of the original (\p isOriginal) or of the revised input: the shown differences and
    /// their rectangles (tests: what drawPage draws)
    struct Rectangle
    {
        size_t index = 0;   ///< Index of the difference in the shown result
        QRectF rect;        ///< In the coordinates of the page
    };
    std::vector<Rectangle> getRectangles(PDFInteger pageIndex, bool isOriginal) const;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

private:
    void drawRectangles(QPainter* painter, PDFInteger inputPageIndex, bool isOriginal, const QTransform& matrix, const PDFColorConvertor& convertor) const;
    void drawMarker(QPainter* painter, const QRectF& deviceRect, const QColor& color, bool isOriginal) const;

    const PDFAppCompareLayout* m_layout = nullptr;
    PDFDiffResult m_result;
    std::map<PDFInteger, std::vector<Rectangle>> m_originalRectangles;
    std::map<PDFInteger, std::vector<Rectangle>> m_revisedRectangles;
    std::map<PDFInteger, size_t> m_originalPageDifferences;   ///< Original page → its moved / removed page difference
    std::map<PDFInteger, size_t> m_revisedPageDifferences;    ///< Revised page → its moved / added page difference
    PDFDiffReportColors m_colors;
    std::optional<size_t> m_selectedIndex;
    bool m_isHighlightsShown = true;
    bool m_isMarkersShown = true;
};

/// The canvas of Compare (plan of step 7 of phase 2, 4.9; 492:18424, 492:19314, 492:22537, 492:22973, 493:18777,
/// 493:19325, 492:105498, 492:106266): bg/track, padding 20; the preview bar (Semi Bold 13 "Page 3 of 24 · Page 3 of
/// 25", at its trailing edge Regular 12 text/tertiary "Difference 3 of 14"), the captions of the documents ("Original ·
/// Report_v1.pdf"), an own PDFWidget + PDFDrawWidgetProxy over the document of the view (PDFDiffReportBuilder::
/// createCombinedDocument; the custom layout PDFAppCompareLayout, the drawing PDFAppCompareDrawInterface; the
/// overlay by the transparency of the page groups) and the info callout at the bottom ("Press Compare to find the
/// differences"); instead of the pages the message state (No differences found: the check badge, Semi Bold 16, the
/// text) or a red callout (a comparison, which failed - FC-2). A rewrite of the overlay and the views of the old Diff
/// - every view, filter, color and the overlay are kept (plan 4.10). The pages are rendered by the compiler of the
/// PDFWidget in its threads, never in the GUI thread.
///
/// Ownership: the view owns the PDFWidget, the color management of its widget, the activity of the optional
/// content and the draw interface; it holds the shown document by its shared pointer (immutable, nobody else
/// changes it). No signal of its own; the PDFWidget is reachable for the GUI (getPdfWidget).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCompareView : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int PADDING = 20;
    static constexpr int BAR_HEIGHT = 36;
    static constexpr int GAP = 12;

    explicit PDFAppCompareView(QWidget* parent = nullptr);
    virtual ~PDFAppCompareView() override;

    /// The shown document and its pages (a failure shows nothing; the GUI says why)
    void setDocument(const PDFDiffCombinedDocument& document);
    const PDFDocument* getDocument() const { return m_document.data(); }

    /// The shown result (the differences filtered by the menu Filter; its page sequence lays the pages out)
    void setResult(const PDFDiffResult& result);

    void setView(PDFDiffReportView view);
    PDFDiffReportView getView() const { return m_view; }

    void setOverlaySettings(const PDFAppCompareOverlaySettings& settings);
    const PDFAppCompareOverlaySettings& getOverlaySettings() const { return m_overlay; }

    /// The items of the menu Show
    void setShowOptions(bool isHighlightsShown, bool isMarkersShown, bool isOnlyChangedPages);
    void setColors(const PDFDiffReportColors& colors);

    /// The selected difference of the shown result (std::nullopt = none); \p isFollowed scrolls the view to its
    /// page (Follow the Selected Difference)
    void setSelectedDifference(std::optional<size_t> index, bool isFollowed);

    /// The texts of the preview bar and of the captions (empty = hidden)
    void setBarTexts(const QString& title, const QString& detail);
    void setCaptions(const QString& original, const QString& revised);

    /// The info callout at the bottom (492:18424), empty title = hidden
    void setInfo(const QString& title, const QString& text);

    /// The pages (with the bar), or the message state of no differences (492:105498), or the red callout of a
    /// failure (FC-2)
    void showPages();
    void showMessage(const QString& title, const QString& text);
    void showError(const QString& title, const QString& text);
    bool isPagesShown() const;

    /// The page of the shown document in the view (the current page of the proxy), -1 = none
    PDFInteger getCurrentPageIndex() const;

    /// The zoom of the view (1 = 100 %)
    double getZoom() const;

    const PDFAppCompareLayout& getLayout() const { return m_layout; }
    const PDFAppCompareDrawInterface& getDrawInterface() const { return m_drawInterface; }
    PDFWidget* getPdfWidget() const { return m_pdfWidget.data(); }
    PDFLabel* getTitleLabel() const { return m_title.data(); }
    PDFLabel* getDetailLabel() const { return m_detail.data(); }
    PDFLabel* getOriginalCaption() const { return m_originalCaption.data(); }
    PDFLabel* getRevisedCaption() const { return m_revisedCaption.data(); }
    PDFAppToolCallout* getInfoCallout() const { return m_info.data(); }
    PDFLabel* getMessageTitle() const { return m_messageTitle.data(); }
    PDFAppParagraph* getMessageText() const { return m_messageText.data(); }
    PDFAppToolCallout* getErrorCallout() const { return m_error.data(); }

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateLayout();
    void updateTransparency();
    void updateCaptions();

    /// The zoom fits the rows of the layout (a pair of pages, one page, the overlay) into the canvas: at once, when the
    /// canvas is shown, else at its first show; afterwards with its size, until the user zooms
    void requestFit();
    void fitWhenShown();

    bool m_isFitPending = false;
    double m_fittedZoom = 0.0;

    PDFDiffReportView m_view = PDFDiffReportView::SideBySide;
    PDFAppCompareOverlaySettings m_overlay;
    PDFAppCompareLayout m_layout;
    PDFAppCompareDrawInterface m_drawInterface;
    PDFDiffResult m_result;
    PDFDocumentPointer m_document;
    std::vector<PDFDiffCombinedPage> m_pages;
    bool m_isOnlyChangedPages = false;
    QString m_originalCaptionText;
    QString m_revisedCaptionText;

    QPointer<PDFCMSManager> m_cmsManager;
    QPointer<PDFOptionalContentActivity> m_optionalContentActivity;
    QPointer<QStackedWidget> m_stack;
    QPointer<QWidget> m_pagesPage;
    QPointer<QWidget> m_messagePage;
    QPointer<PDFLabel> m_title;
    QPointer<PDFLabel> m_detail;
    QPointer<QWidget> m_captionRow;
    QPointer<PDFLabel> m_originalCaption;
    QPointer<PDFLabel> m_revisedCaption;
    QPointer<PDFWidget> m_pdfWidget;
    QPointer<PDFAppToolCallout> m_info;
    QPointer<PDFIconBadge> m_messageBadge;
    QPointer<PDFLabel> m_messageTitle;
    QPointer<PDFAppParagraph> m_messageText;
    QPointer<PDFAppToolCallout> m_error;
};

}   // namespace pdf

#endif // PDFAPPCOMPAREVIEW_H
