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

#include "pdfappcompareview.h"

#include "pdfapppagelayout.h"
#include "pdfapptoolpanelparts.h"
#include "pdfappworkspace.h"

#include "pdfcms.h"
#include "pdfcolorconvertor.h"
#include "pdfcomponentbadges.h"
#include "pdfdrawwidget.h"
#include "pdffluentwidgets.h"
#include "pdfoptionalcontent.h"
#include "pdfpainterutils.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfwidgetutils.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace pdf
{

/// Helpers of the canvas of Compare
class PDFAppCompareViewHelper
{
public:
    PDFAppCompareViewHelper() = delete;

    /// The zoom of the canvas, at which a row of the layout (the pair of Side by Side, the page of one document, the
    /// overlaid pages) is seen whole (0 = none) (492:18424, 492:19314, 493:18777: both pages of a pair, each caption over its page): the
    /// width of all rows, the height of the tallest page. The fit of the proxy measures one page; the rows are wider
    /// or taller by their ratio.
    static PDFReal getFitZoom(const PDFDrawWidgetProxy* proxy, const PDFDrawSpaceController::LayoutItems& items)
    {
        if (!proxy || items.empty())
        {
            return 0.0;
        }

        const QRectF page = items.front().pageRectMM;
        QRectF rows = page;
        PDFReal height = page.height();
        for (const PDFDrawSpaceController::LayoutItem& item : items)
        {
            rows = rows.united(item.pageRectMM);
            height = qMax(height, item.pageRectMM.height());
        }
        if (page.isEmpty() || rows.isEmpty())
        {
            return 0.0;
        }

        const PDFReal widthZoom = proxy->getZoomHintForPage(PDFDrawWidgetProxy::ZoomHint::FitWidth, items.front().pageIndex) * page.width() / rows.width();
        const PDFReal heightZoom = proxy->getZoomHintForPage(PDFDrawWidgetProxy::ZoomHint::FitHeight, items.front().pageIndex) * page.height() / height;
        const PDFReal zoom = qMin(widthZoom, heightZoom);
        return zoom > 0.0 ? qBound(PDFDrawWidgetProxy::getMinZoom(), zoom, PDFDrawWidgetProxy::getMaxZoom()) : 0.0;
    }

    /// The rotated media box of the page of the document in millimeters, an empty size for no such page
    static QSizeF getPageSize(const PDFDocument* document, PDFInteger pageIndex)
    {
        const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
        if (!catalog || pageIndex < 0 || pageIndex >= PDFInteger(catalog->getPageCount()))
        {
            return QSizeF();
        }
        const PDFPage* page = catalog->getPage(size_t(pageIndex));
        return page ? page->getRotatedMediaBoxMM().size() : QSizeF();
    }

    /// The rectangles of the side of a difference into the index (only valid page numbers)
    static void addRectangles(std::map<PDFInteger, std::vector<PDFAppCompareDrawInterface::Rectangle>>& rectangles,
                              size_t index,
                              std::pair<PDFDiffResult::RectInfosIt, PDFDiffResult::RectInfosIt> range)
    {
        for (auto it = range.first; it != range.second; ++it)
        {
            if (it->first >= 0 && it->second.isValid())
            {
                rectangles[it->first].push_back({ index, it->second });
            }
        }
    }
};

// ------------------------------------------------------------- PDFAppCompareLayout

void PDFAppCompareLayout::addPage(PDFInteger pageIndex, PDFInteger groupIndex, const QRectF& rect, PDFInteger originalPageIndex, PDFInteger revisedPageIndex)
{
    m_layout.emplace_back(0, pageIndex, groupIndex, rect);
    if (originalPageIndex >= 0)
    {
        m_originalOfPage[pageIndex] = originalPageIndex;
        m_pageOfOriginal[originalPageIndex] = pageIndex;
    }
    if (revisedPageIndex >= 0)
    {
        m_revisedOfPage[pageIndex] = revisedPageIndex;
        m_pageOfRevised[revisedPageIndex] = pageIndex;
    }
}

void PDFAppCompareLayout::update(PDFDiffReportView view,
                                 bool isOnlyChangedPages,
                                 const PDFDiffResult& result,
                                 const PDFDocument* document,
                                 const std::vector<PDFDiffCombinedPage>& pages,
                                 const PDFAppCompareOverlaySettings& overlay)
{
    using Helper = PDFAppCompareViewHelper;

    m_layout.clear();
    m_originalOfPage.clear();
    m_revisedOfPage.clear();
    m_pageOfOriginal.clear();
    m_pageOfRevised.clear();
    if (!document)
    {
        return;
    }

    // Where each page of an input lies in the shown document (each page shows one page of one input)
    std::map<PDFInteger, PDFInteger> shownOriginal;
    std::map<PDFInteger, PDFInteger> shownRevised;
    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    for (size_t i = 0; i < pages.size() && PDFInteger(i) < pageCount; ++i)
    {
        if (pages[i].originalPage >= 0)
        {
            shownOriginal.emplace(pages[i].originalPage, PDFInteger(i));
        }
        else if (pages[i].revisedPage >= 0)
        {
            shownRevised.emplace(pages[i].revisedPage, PDFInteger(i));
        }
    }

    // Only Pages with Differences: the pairs with a changed page (an added / removed page is its own pair)
    const std::vector<PDFInteger> changedOriginal = result.getChangedLeftPageIndices();
    const std::vector<PDFInteger> changedRevised = result.getChangedRightPageIndices();

    double yPosition = 0.0;
    for (const PDFDiffResult::PageSequenceItem& item : result.getPageSequence())
    {
        if (isOnlyChangedPages)
        {
            const bool isOriginalChanged = item.leftPage >= 0 && std::binary_search(changedOriginal.cbegin(), changedOriginal.cend(), item.leftPage);
            const bool isRevisedChanged = item.rightPage >= 0 && std::binary_search(changedRevised.cbegin(), changedRevised.cend(), item.rightPage);
            if (!isOriginalChanged && !isRevisedChanged)
            {
                continue;
            }
        }

        const auto originalIt = item.leftPage >= 0 ? shownOriginal.find(item.leftPage) : shownOriginal.end();
        const auto revisedIt = item.rightPage >= 0 ? shownRevised.find(item.rightPage) : shownRevised.end();
        const PDFInteger originalPage = originalIt != shownOriginal.end() ? originalIt->second : -1;
        const PDFInteger revisedPage = revisedIt != shownRevised.end() ? revisedIt->second : -1;
        const QSizeF originalSize = Helper::getPageSize(document, originalPage);
        const QSizeF revisedSize = Helper::getPageSize(document, revisedPage);

        switch (view)
        {
            case PDFDiffReportView::Original:
            case PDFDiffReportView::Revised:
            {
                const bool isOriginal = view == PDFDiffReportView::Original;
                const PDFInteger page = isOriginal ? originalPage : revisedPage;
                const QSizeF size = isOriginal ? originalSize : revisedSize;
                if (page >= 0 && !size.isEmpty())
                {
                    addPage(page, -1, QRectF(-size.width() * 0.5, yPosition, size.width(), size.height()), isOriginal ? item.leftPage : -1, isOriginal ? -1 : item.rightPage);
                    yPosition += size.height() + PAGE_GAP;
                }
                break;
            }

            case PDFDiffReportView::SideBySide:
            {
                double advance = 0.0;
                if (originalPage >= 0 && !originalSize.isEmpty())
                {
                    addPage(originalPage, -1, QRectF(-originalSize.width() - PAGE_GAP, yPosition, originalSize.width(), originalSize.height()), item.leftPage, -1);
                    advance = originalSize.height() + PAGE_GAP;
                }
                if (revisedPage >= 0 && !revisedSize.isEmpty())
                {
                    addPage(revisedPage, -1, QRectF(PAGE_GAP, yPosition, revisedSize.width(), revisedSize.height()), -1, item.rightPage);
                    advance = qMax(advance, revisedSize.height() + PAGE_GAP);
                }
                yPosition += advance;
                break;
            }

            case PDFDiffReportView::Overlay:
            {
                if (originalPage >= 0 && revisedPage >= 0 && !originalSize.isEmpty() && !revisedSize.isEmpty())
                {
                    QSizeF original = originalSize;
                    QSizeF revised = revisedSize;
                    if (overlay.scaleMode == PDFAppCompareOverlaySettings::ScaleMode::Fit)
                    {
                        // Each page fits into the common paper size without distorting the drawing
                        const QSizeF target = original.expandedTo(revised);
                        original *= qMin(target.width() / original.width(), target.height() / original.height());
                        revised *= qMin(target.width() / revised.width(), target.height() / revised.height());
                    }
                    else if (overlay.scaleMode == PDFAppCompareOverlaySettings::ScaleMode::Manual)
                    {
                        original *= overlay.originalScale;
                        revised *= overlay.revisedScale;
                    }

                    // Original sizes: aligned at the top; Fit and Manual: both centered
                    QRectF originalRect(QPointF(-original.width() * 0.5, 0.0), original);
                    QRectF revisedRect(QPointF(-revised.width() * 0.5, 0.0), revised);
                    if (overlay.scaleMode != PDFAppCompareOverlaySettings::ScaleMode::Original)
                    {
                        originalRect.moveCenter(QPointF());
                        revisedRect.moveCenter(QPointF());
                    }
                    revisedRect.translate(overlay.revisedOffsetMM);

                    const QRectF bounds = originalRect.united(revisedRect);
                    const QPointF translation(0.0, yPosition - bounds.top());
                    addPage(originalPage, ORIGINAL_GROUP, originalRect.translated(translation), item.leftPage, -1);
                    addPage(revisedPage, REVISED_GROUP, revisedRect.translated(translation), -1, item.rightPage);
                    yPosition += bounds.height() + PAGE_GAP;
                    break;
                }

                // A page without its pair stays alone, in its own size, centered
                double advance = 0.0;
                if (originalPage >= 0 && !originalSize.isEmpty())
                {
                    addPage(originalPage, -1, QRectF(-originalSize.width() * 0.5, yPosition, originalSize.width(), originalSize.height()), item.leftPage, -1);
                    advance = originalSize.height() + PAGE_GAP;
                }
                if (revisedPage >= 0 && !revisedSize.isEmpty())
                {
                    addPage(revisedPage, -1, QRectF(-revisedSize.width() * 0.5, yPosition + advance, revisedSize.width(), revisedSize.height()), -1, item.rightPage);
                    advance += revisedSize.height() + PAGE_GAP;
                }
                yPosition += advance;
                break;
            }
        }
    }
}

PDFInteger PDFAppCompareLayout::getOriginalPageIndex(PDFInteger pageIndex) const
{
    const auto it = m_originalOfPage.find(pageIndex);
    return it != m_originalOfPage.cend() ? it->second : -1;
}

PDFInteger PDFAppCompareLayout::getRevisedPageIndex(PDFInteger pageIndex) const
{
    const auto it = m_revisedOfPage.find(pageIndex);
    return it != m_revisedOfPage.cend() ? it->second : -1;
}

PDFInteger PDFAppCompareLayout::getPageIndexFromOriginal(PDFInteger originalPageIndex) const
{
    const auto it = m_pageOfOriginal.find(originalPageIndex);
    return it != m_pageOfOriginal.cend() ? it->second : -1;
}

PDFInteger PDFAppCompareLayout::getPageIndexFromRevised(PDFInteger revisedPageIndex) const
{
    const auto it = m_pageOfRevised.find(revisedPageIndex);
    return it != m_pageOfRevised.cend() ? it->second : -1;
}

// ------------------------------------------------------------- PDFAppCompareDrawInterface

void PDFAppCompareDrawInterface::setResult(const PDFDiffResult& result)
{
    using Helper = PDFAppCompareViewHelper;

    m_result = result;
    m_originalRectangles.clear();
    m_revisedRectangles.clear();
    m_originalPageDifferences.clear();
    m_revisedPageDifferences.clear();
    m_selectedIndex.reset();

    // One pass over the differences: each page later draws only its own rectangles
    const size_t count = m_result.getDifferencesCount();
    for (size_t i = 0; i < count; ++i)
    {
        Helper::addRectangles(m_originalRectangles, i, m_result.getLeftRectangles(i));
        Helper::addRectangles(m_revisedRectangles, i, m_result.getRightRectangles(i));
        if (m_result.isPageMoveAddRemoveDifference(i))
        {
            if (m_result.getLeftPage(i) >= 0)
            {
                m_originalPageDifferences[m_result.getLeftPage(i)] = i;
            }
            if (m_result.getRightPage(i) >= 0)
            {
                m_revisedPageDifferences[m_result.getRightPage(i)] = i;
            }
        }
    }
}

QColor PDFAppCompareDrawInterface::getColor(const PDFDiffResult& result, size_t index, const PDFDiffReportColors& colors)
{
    // The order of the old Diff: a replacement is also an addition and a removal of its texts
    if (index >= result.getDifferencesCount())
    {
        return QColor();
    }
    if (result.isReplaceDifference(index))
    {
        return colors.replaced;
    }
    if (result.isRemoveDifference(index))
    {
        return colors.removed;
    }
    if (result.isAddDifference(index))
    {
        return colors.added;
    }
    if (result.isPageMoveDifference(index))
    {
        return colors.moved;
    }
    return QColor();
}

QString PDFAppCompareDrawInterface::getBubbleText(const PDFDiffResult& result, size_t index)
{
    if (index >= result.getDifferencesCount())
    {
        return QString();
    }

    switch (result.getType(index))
    {
        case PDFDiffResult::Type::PageAdded:
            return QStringLiteral("+");

        case PDFDiffResult::Type::PageRemoved:
            return QString(QChar(0x2212));

        case PDFDiffResult::Type::PageMoved:
            // 492:105001 "5 → 6": the pages from 1
            return QStringLiteral("%1 %2 %3").arg(result.getLeftPage(index) + 1).arg(QChar(0x2192)).arg(result.getRightPage(index) + 1);

        default:
            break;
    }
    return QString();
}

std::vector<PDFAppCompareDrawInterface::Rectangle> PDFAppCompareDrawInterface::getRectangles(PDFInteger pageIndex, bool isOriginal) const
{
    const std::map<PDFInteger, std::vector<Rectangle>>& rectangles = isOriginal ? m_originalRectangles : m_revisedRectangles;
    const auto it = rectangles.find(pageIndex);
    return it != rectangles.cend() ? it->second : std::vector<Rectangle>();
}

void PDFAppCompareDrawInterface::drawPage(QPainter* painter,
                                          PDFInteger pageIndex,
                                          const PDFPrecompiledPage* compiledPage,
                                          PDFTextLayoutGetter& layoutGetter,
                                          const QTransform& pagePointToDevicePointMatrix,
                                          const PDFColorConvertor& convertor,
                                          QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    if (!m_layout || !m_isHighlightsShown)
    {
        return;
    }

    const PDFInteger originalPage = m_layout->getOriginalPageIndex(pageIndex);
    const PDFInteger revisedPage = m_layout->getRevisedPageIndex(pageIndex);
    if (originalPage >= 0)
    {
        drawRectangles(painter, originalPage, true, pagePointToDevicePointMatrix, convertor);
    }
    if (revisedPage >= 0)
    {
        drawRectangles(painter, revisedPage, false, pagePointToDevicePointMatrix, convertor);
    }

    // The bubble of a moved, added or removed page at its top left corner (the old Diff)
    std::optional<size_t> pageDifference;
    if (originalPage >= 0 && m_originalPageDifferences.count(originalPage))
    {
        pageDifference = m_originalPageDifferences.at(originalPage);
    }
    if (revisedPage >= 0 && m_revisedPageDifferences.count(revisedPage))
    {
        pageDifference = m_revisedPageDifferences.at(revisedPage);
    }
    if (pageDifference)
    {
        const QString text = getBubbleText(m_result, *pageDifference);
        if (!text.isEmpty())
        {
            PDFPainterStateGuard guard(painter);
            const QColor color = convertor.convert(getColor(m_result, *pageDifference, m_colors), false, true);
            const QPointF target = pagePointToDevicePointMatrix.map(QPointF(5, 5));
            PDFPainterHelper::drawBubble(painter, target.toPoint(), color, text, Qt::AlignRight | Qt::AlignTop);
        }
    }
}

void PDFAppCompareDrawInterface::drawRectangles(QPainter* painter, PDFInteger inputPageIndex, bool isOriginal, const QTransform& matrix, const PDFColorConvertor& convertor) const
{
    const std::map<PDFInteger, std::vector<Rectangle>>& rectangles = isOriginal ? m_originalRectangles : m_revisedRectangles;
    const auto it = rectangles.find(inputPageIndex);
    if (it == rectangles.cend())
    {
        return;
    }

    PDFPainterStateGuard guard(painter);
    const QColor selectionColor = PDFStylePainter::color(PDFThemeColors::BgAccent);
    const qreal selectionWidth = PDFWidgetUtils::scaleDPI_x(painter->device(), 2.0);
    for (const Rectangle& rectangle : it->second)
    {
        const QColor color = convertor.convert(getColor(m_result, rectangle.index, m_colors), false, true);
        const QRectF deviceRect = matrix.mapRect(rectangle.rect);
        QColor fill = color;
        fill.setAlphaF(0.5);
        painter->fillRect(deviceRect, fill);
        if (m_isMarkersShown)
        {
            drawMarker(painter, deviceRect, color, isOriginal);
        }

        // K7.25: the selection is the accent outline, never the color of a kind
        if (m_selectedIndex && *m_selectedIndex == rectangle.index)
        {
            PDFPainterStateGuard selectionGuard(painter);
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(selectionColor, selectionWidth));
            painter->drawRect(deviceRect);
        }
    }
}

void PDFAppCompareDrawInterface::drawMarker(QPainter* painter, const QRectF& deviceRect, const QColor& color, bool isOriginal) const
{
    // 492:19314: a short line and an arrow tip at the edge of the view, at the height of the difference -
    // the left edge for the original page, the right edge for the revised page
    PDFPainterStateGuard guard(painter);
    const QPaintDevice* device = painter->device();
    const qreal width = device ? qreal(device->width()) : 0.0;
    const qreal lineLength = PDFWidgetUtils::scaleDPI_x(device, 16.0);
    const qreal lineWidth = PDFWidgetUtils::scaleDPI_x(device, 2.0);
    const qreal tip = PDFWidgetUtils::scaleDPI_x(device, 5.0);
    const qreal y = deviceRect.center().y();

    const qreal edge = isOriginal ? 0.0 : width;
    const qreal direction = isOriginal ? 1.0 : -1.0;
    painter->setPen(QPen(color, lineWidth));
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(QPointF(edge + direction * tip, y), QPointF(edge + direction * (tip + lineLength), y));

    // The tip points at the edge of the view (the page lies inwards)
    const std::array<QPointF, 3> points = { QPointF(edge, y), QPointF(edge + direction * tip * 2.0, y - tip), QPointF(edge + direction * tip * 2.0, y + tip) };
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawConvexPolygon(points.data(), int(points.size()));
}

// ------------------------------------------------------------- PDFAppCompareView

PDFAppCompareView::PDFAppCompareView(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("compareView"));
    m_drawInterface.setLayout(&m_layout);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);

    // The pages: the preview bar, the captions, the canvas and the info callout (492:18424, 492:19314)
    m_pagesPage = new QWidget(m_stack);
    QVBoxLayout* pagesLayout = new QVBoxLayout(m_pagesPage);
    pagesLayout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    pagesLayout->setSpacing(GAP);

    QWidget* bar = new QWidget(m_pagesPage);
    bar->setFixedHeight(BAR_HEIGHT);
    QHBoxLayout* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(8);
    m_title = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, bar);
    m_title->setObjectName(QStringLiteral("compareViewTitle"));
    barLayout->addWidget(m_title, 1);
    m_detail = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextTertiary, bar);
    m_detail->setObjectName(QStringLiteral("compareViewDetail"));
    barLayout->addWidget(m_detail);
    pagesLayout->addWidget(bar);

    // viewport 492:20194: "Original · Report_v1.pdf" over the left pages, "Revised · Report_v2.pdf" over the right ones
    m_captionRow = new QWidget(m_pagesPage);
    QHBoxLayout* captionLayout = new QHBoxLayout(m_captionRow);
    captionLayout->setContentsMargins(PADDING, 0, PADDING, 0);
    captionLayout->setSpacing(16);
    m_originalCaption = PDFAppPageWidgets::createLabel(QString(), QFont::Medium, 11, PDFThemeColors::TextSecondary, m_captionRow);
    m_originalCaption->setObjectName(QStringLiteral("compareViewOriginalCaption"));
    captionLayout->addWidget(m_originalCaption, 1);
    m_revisedCaption = PDFAppPageWidgets::createLabel(QString(), QFont::Medium, 11, PDFThemeColors::TextSecondary, m_captionRow);
    m_revisedCaption->setObjectName(QStringLiteral("compareViewRevisedCaption"));
    captionLayout->addWidget(m_revisedCaption, 1);
    pagesLayout->addWidget(m_captionRow);

    // The canvas: its own color management (the defaults; the documents compared are snapshots), the pages rendered
    // by the compiler of the widget in its threads
    m_cmsManager = new PDFCMSManager(this);
    m_pdfWidget = new PDFWidget(m_cmsManager, RendererEngine::QPainter, m_pagesPage);
    m_pdfWidget->setObjectName(QStringLiteral("compareCanvas"));
    m_pdfWidget->getDrawWidgetProxy()->registerDrawInterface(&m_drawInterface);
    m_pdfWidget->installEventFilter(this);
    pagesLayout->addWidget(m_pdfWidget, 1);

    m_info = new PDFAppToolCallout(PDFAccentHue::Blue, QStringLiteral("info"), QString(), QString(), m_pagesPage);
    m_info->setObjectName(QStringLiteral("compareViewInfo"));
    m_info->hide();
    pagesLayout->addWidget(m_info);
    m_stack->addWidget(m_pagesPage);

    // The message state: No differences found (492:105498) or the red callout of a failure (FC-2)
    m_messagePage = new QWidget(m_stack);
    QVBoxLayout* messageLayout = new QVBoxLayout(m_messagePage);
    messageLayout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    messageLayout->setSpacing(8);
    messageLayout->addStretch(1);
    m_messageBadge = new PDFIconBadge(QStringLiteral("check"), 56, 24, m_messagePage);
    m_messageBadge->setObjectName(QStringLiteral("compareViewMessageBadge"));
    m_messageBadge->setColors(PDFThemeColors::AccentGreenBg, PDFThemeColors::AccentGreenIcon);
    messageLayout->addWidget(m_messageBadge, 0, Qt::AlignHCenter);
    m_messageTitle = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 16, PDFThemeColors::TextPrimary, m_messagePage);
    m_messageTitle->setObjectName(QStringLiteral("compareViewMessageTitle"));
    m_messageTitle->setAlignment(Qt::AlignCenter);
    messageLayout->addWidget(m_messageTitle);
    m_messageText = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, m_messagePage);
    m_messageText->setObjectName(QStringLiteral("compareViewMessageText"));
    m_messageText->setAlignment(Qt::AlignCenter);
    m_messageText->setMaximumWidth(420);
    messageLayout->addWidget(m_messageText, 0, Qt::AlignHCenter);
    m_error = new PDFAppToolCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), QString(), QString(), m_messagePage);
    m_error->setObjectName(QStringLiteral("compareViewError"));
    m_error->setMaximumWidth(560);
    messageLayout->addWidget(m_error, 0, Qt::AlignHCenter);
    // empty-state 492:105810: centered in the canvas (y 359 of 842 for its 124)
    messageLayout->addStretch(1);
    m_stack->addWidget(m_messagePage);

    m_stack->setCurrentWidget(m_pagesPage);
    updateCaptions();
}

PDFAppCompareView::~PDFAppCompareView()
{
    // The widget draws by the interface of this view: it goes first (it is a child, but the members die before the
    // children)
    if (m_pdfWidget)
    {
        m_pdfWidget->getDrawWidgetProxy()->unregisterDrawInterface(&m_drawInterface);
        delete m_pdfWidget.data();
    }
}

void PDFAppCompareView::setDocument(const PDFDiffCombinedDocument& document)
{
    // The new document is shown first, then the old one and its activity go (the widget never holds a freed
    // document)
    const PDFDocumentPointer oldDocument = m_document;
    QPointer<PDFOptionalContentActivity> oldActivity = m_optionalContentActivity;
    m_document = document.isSuccess() ? document.document : PDFDocumentPointer();
    m_pages = document.isSuccess() ? document.pages : std::vector<PDFDiffCombinedPage>();

    if (m_document)
    {
        m_optionalContentActivity = new PDFOptionalContentActivity(m_document.data(), OCUsage::View, this);
        m_cmsManager->setDocument(m_document.data());
        m_pdfWidget->setDocument(PDFModifiedDocument(m_document.data(), m_optionalContentActivity.data()), {});
    }
    else
    {
        m_optionalContentActivity = nullptr;
        m_pdfWidget->setDocument(PDFModifiedDocument(), {});
        m_cmsManager->setDocument(nullptr);
    }

    if (oldActivity)
    {
        oldActivity->deleteLater();
    }
    updateLayout();

    // 492:18424, 492:19314: the pages of a pair are seen whole, each caption over its page (the default zoom 100 %
    // showed only the Original of an A4 pair, the caption of the Revised over it); a new document of the view (other
    // inputs, a new result) fits again
    if (m_document && m_document != oldDocument)
    {
        requestFit();
    }
}

void PDFAppCompareView::requestFit()
{
    // After the layout of the new document reaches the canvas and its viewport
    m_isFitPending = true;
    QMetaObject::invokeMethod(this, &PDFAppCompareView::fitWhenShown, Qt::QueuedConnection);
}

void PDFAppCompareView::fitWhenShown()
{
    // The fit measures the canvas: a hidden canvas (the choice of the documents, a page of the stack behind) has no
    // size yet
    if (!m_isFitPending || !m_pdfWidget || !m_document || !m_pdfWidget->isVisible() || m_pdfWidget->width() <= 0 || m_pdfWidget->height() <= 0)
    {
        return;
    }

    m_isFitPending = false;
    QPointer<PDFDrawWidgetProxy> proxy = m_pdfWidget->getDrawWidgetProxy();
    const PDFReal zoom = PDFAppCompareViewHelper::getFitZoom(proxy.data(), m_layout.getLayout());
    if (zoom <= 0.0)
    {
        return;
    }

    // The signals of the proxy run code of the application (the zoom in the status bar, which may close the tab):
    // the state first, then the proxy alone, checked between its calls
    const PDFInteger firstPage = m_layout.getLayout().front().pageIndex;
    m_fittedZoom = zoom;
    proxy->zoom(zoom);
    if (proxy)
    {
        proxy->goToPage(firstPage);
    }
}

bool PDFAppCompareView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_pdfWidget.data() && (event->type() == QEvent::Show || event->type() == QEvent::Resize))
    {
        // A pending fit, or the fitted zoom follows the size of the canvas (a zoom of the user stays)
        if (!m_isFitPending && m_document && m_fittedZoom > 0.0 && qFuzzyCompare(m_pdfWidget->getDrawWidgetProxy()->getZoom(), m_fittedZoom))
        {
            m_isFitPending = true;
        }
        if (m_isFitPending)
        {
            // After the canvas lays out its viewport (the event comes to the filter first)
            QMetaObject::invokeMethod(this, &PDFAppCompareView::fitWhenShown, Qt::QueuedConnection);
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppCompareView::setResult(const PDFDiffResult& result)
{
    m_result = result;
    m_drawInterface.setResult(m_result);
    updateLayout();
}

void PDFAppCompareView::setView(PDFDiffReportView view)
{
    if (m_view != view)
    {
        m_view = view;
        updateCaptions();
        updateLayout();

        // Another view has rows of another width (a pair, one page, the overlay)
        if (m_document)
        {
            requestFit();
        }
    }
}

void PDFAppCompareView::setOverlaySettings(const PDFAppCompareOverlaySettings& settings)
{
    const bool isLayoutChanged = settings.scaleMode != m_overlay.scaleMode || settings.originalScale != m_overlay.originalScale ||
                                 settings.revisedScale != m_overlay.revisedScale || settings.revisedOffsetMM != m_overlay.revisedOffsetMM;
    m_overlay = settings;
    if (isLayoutChanged)
    {
        updateLayout();
    }
    else
    {
        updateTransparency();
    }
}

void PDFAppCompareView::setShowOptions(bool isHighlightsShown, bool isMarkersShown, bool isOnlyChangedPages)
{
    m_drawInterface.setHighlightsShown(isHighlightsShown);
    m_drawInterface.setMarkersShown(isMarkersShown);
    if (m_isOnlyChangedPages != isOnlyChangedPages)
    {
        m_isOnlyChangedPages = isOnlyChangedPages;
        updateLayout();
    }
    else if (m_pdfWidget)
    {
        m_pdfWidget->update();
    }
}

void PDFAppCompareView::setColors(const PDFDiffReportColors& colors)
{
    m_drawInterface.setColors(colors);
    if (m_pdfWidget)
    {
        m_pdfWidget->update();
    }
}

void PDFAppCompareView::setSelectedDifference(std::optional<size_t> index, bool isFollowed)
{
    m_drawInterface.setSelectedDifference(index);
    if (!m_pdfWidget)
    {
        return;
    }

    if (isFollowed && index && *index < m_result.getDifferencesCount())
    {
        // The page of the original first, else of the revised one (the old Diff)
        PDFInteger page = -1;
        if (m_result.getLeftPage(*index) >= 0)
        {
            page = m_layout.getPageIndexFromOriginal(m_result.getLeftPage(*index));
        }
        if (page < 0 && m_result.getRightPage(*index) >= 0)
        {
            page = m_layout.getPageIndexFromRevised(m_result.getRightPage(*index));
        }
        if (page >= 0)
        {
            m_pdfWidget->getDrawWidgetProxy()->goToPage(page);
        }
    }
    m_pdfWidget->update();
}

void PDFAppCompareView::setBarTexts(const QString& title, const QString& detail)
{
    m_title->setText(title);
    m_detail->setText(detail);
}

void PDFAppCompareView::setCaptions(const QString& original, const QString& revised)
{
    m_originalCaptionText = original;
    m_revisedCaptionText = revised;
    updateCaptions();
}

void PDFAppCompareView::setInfo(const QString& title, const QString& text)
{
    m_info->setTexts(title, text);
    m_info->setVisible(!title.isEmpty());
}

void PDFAppCompareView::showPages()
{
    m_stack->setCurrentWidget(m_pagesPage);
}

void PDFAppCompareView::showMessage(const QString& title, const QString& text)
{
    m_messageTitle->setText(title);
    m_messageText->setText(text);
    m_messageBadge->show();
    m_messageTitle->show();
    m_messageText->show();
    m_error->hide();
    m_stack->setCurrentWidget(m_messagePage);
}

void PDFAppCompareView::showError(const QString& title, const QString& text)
{
    m_error->setTexts(title, text);
    m_messageBadge->hide();
    m_messageTitle->hide();
    m_messageText->hide();
    m_error->show();
    m_stack->setCurrentWidget(m_messagePage);
}

bool PDFAppCompareView::isPagesShown() const
{
    return m_stack->currentWidget() == m_pagesPage;
}

PDFInteger PDFAppCompareView::getCurrentPageIndex() const
{
    if (!m_pdfWidget || !m_document)
    {
        return -1;
    }
    const std::vector<PDFInteger> pages = m_pdfWidget->getDrawWidget()->getCurrentPages();
    return pages.empty() ? -1 : pages.front();
}

double PDFAppCompareView::getZoom() const
{
    return m_pdfWidget ? m_pdfWidget->getDrawWidgetProxy()->getZoom() : 1.0;
}

void PDFAppCompareView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas: bg/track
    QPainter painter(this);
    PDFStylePainter::fillRect(&painter, QRectF(rect()), PDFThemeColors::BgTrack);
}

void PDFAppCompareView::updateLayout()
{
    if (!m_pdfWidget)
    {
        return;
    }

    m_layout.update(m_view, m_isOnlyChangedPages, m_result, m_document.data(), m_pages, m_overlay);
    PDFDrawWidgetProxy* proxy = m_pdfWidget->getDrawWidgetProxy();
    proxy->setCustomPageLayout(m_layout.getLayout());
    proxy->setPageLayout(PageLayout::Custom);
    updateTransparency();
}

void PDFAppCompareView::updateTransparency()
{
    if (!m_pdfWidget)
    {
        return;
    }

    // The old Diff: the original page with its paper, the revised page over it without its paper
    PDFDrawWidgetProxy* proxy = m_pdfWidget->getDrawWidgetProxy();
    const PDFReal revisedShare = qBound(0, m_overlay.blend, 100) * 0.01;
    proxy->setGroupTransparency(PDFAppCompareLayout::ORIGINAL_GROUP, true, 1.0 - revisedShare);
    proxy->setGroupTransparency(PDFAppCompareLayout::REVISED_GROUP, false, revisedShare);
    m_pdfWidget->update();
}

void PDFAppCompareView::updateCaptions()
{
    // Side by Side: both captions; one document: its caption; Overlay: none (the bar names the page)
    const bool isOriginalShown = m_view == PDFDiffReportView::SideBySide || m_view == PDFDiffReportView::Original;
    const bool isRevisedShown = m_view == PDFDiffReportView::SideBySide || m_view == PDFDiffReportView::Revised;
    m_originalCaption->setText(m_originalCaptionText);
    m_revisedCaption->setText(m_revisedCaptionText);
    m_originalCaption->setVisible(isOriginalShown && !m_originalCaptionText.isEmpty());
    m_revisedCaption->setVisible(isRevisedShown && !m_revisedCaptionText.isEmpty());
    m_captionRow->setVisible(m_originalCaption->isVisibleTo(m_captionRow) || m_revisedCaption->isVisibleTo(m_captionRow));
}

}   // namespace pdf

#include "moc_pdfappcompareview.cpp"
