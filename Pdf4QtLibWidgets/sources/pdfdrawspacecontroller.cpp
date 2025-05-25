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

#include "pdfdrawspacecontroller.h"
#include "pdfrenderer.h"
#include "pdfpainter.h"
#include "pdfcompiler.h"
#include "pdfconstants.h"
#include "pdfcms.h"
#include "pdfannotation.h"
#include "pdfdrawwidget.h"
#include "pdfwidgetannotation.h"
#include "pdfpainterutils.h"
#include "pdfwidgetutils.h"

#include <QTimer>
#include <QPainter>
#include <QFontMetrics>
#include <QScreen>
#include <QGuiApplication>

#include "pdfdbgheap.h"

namespace pdf
{

PDFDrawSpaceController::PDFDrawSpaceController(QObject* parent) :
    QObject(parent),
    m_document(nullptr),
    m_optionalContentActivity(nullptr),
    m_pageLayoutMode(PageLayout::OneColumn),
    m_verticalSpacingMM(5.0),
    m_horizontalSpacingMM(1.0),
    m_pageRotation(PageRotation::None),
    m_fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT)
{

}

PDFDrawSpaceController::~PDFDrawSpaceController()
{

}

void PDFDrawSpaceController::setDocument(const PDFModifiedDocument& document)
{
    if (document != m_document)
    {
        m_document = document;
        m_fontCache.setDocument(document);
        m_optionalContentActivity = document.getOptionalContentActivity();

        // If document is not being reset, then recalculation is not needed,
        // pages should remain the same.
        if (document.hasReset())
        {
            recalculate();
        }
    }
}

void PDFDrawSpaceController::setPageLayout(PageLayout pageLayout)
{
    if (m_pageLayoutMode != pageLayout)
    {
        m_pageLayoutMode = pageLayout;
        recalculate();
    }
}

QRectF PDFDrawSpaceController::getBlockBoundingRectangle(size_t blockIndex) const
{
    if (blockIndex < m_blockItems.size())
    {
        return m_blockItems[blockIndex].blockRectMM;
    }

    return QRectF();
}

PDFDrawSpaceController::LayoutItems PDFDrawSpaceController::getLayoutItems(size_t blockIndex) const
{
    LayoutItems result;

    auto comparator = [](const LayoutItem& l, const LayoutItem& r)
    {
        return l.blockIndex < r.blockIndex;
    };
    Q_ASSERT(std::is_sorted(m_layoutItems.cbegin(), m_layoutItems.cend(), comparator));

    LayoutItem templateItem;
    templateItem.blockIndex = blockIndex;

    auto range = std::equal_range(m_layoutItems.cbegin(), m_layoutItems.cend(), templateItem, comparator);
    result.reserve(std::distance(range.first, range.second));
    std::copy(range.first, range.second, std::back_inserter(result));

    return result;
}

PDFDrawSpaceController::LayoutItem PDFDrawSpaceController::getLayoutItemForPage(PDFInteger pageIndex) const
{
    LayoutItem result;

    if (pageIndex >= 0 && pageIndex < static_cast<PDFInteger>(m_layoutItems.size()) && m_layoutItems[pageIndex].pageIndex == pageIndex)
    {
        result = m_layoutItems[pageIndex];
    }

    if (!result.isValid())
    {
        auto it = std::find_if(m_layoutItems.cbegin(), m_layoutItems.cend(), [pageIndex](const LayoutItem& item) { return item.pageIndex == pageIndex; });
        if (it != m_layoutItems.cend())
        {
            result = *it;
        }
    }

    return result;
}

QSizeF PDFDrawSpaceController::getReferenceBoundingBox() const
{
    QRectF rect;

    for (const LayoutItem& item : m_layoutItems)
    {
        QRectF pageRect = item.pageRectMM;
        pageRect.translate(0, -pageRect.top());
        rect = rect.united(pageRect);
    }

    if (rect.isValid())
    {
        rect.adjust(0, 0, m_horizontalSpacingMM, m_verticalSpacingMM);
    }

    return rect.size();
}

void PDFDrawSpaceController::setPageRotation(PageRotation pageRotation)
{
    if (m_pageRotation != pageRotation)
    {
        m_pageRotation = pageRotation;
        recalculate();
    }
}

void PDFDrawSpaceController::setCustomLayout(LayoutItems customLayoutItems)
{
    if (m_customLayoutItems != customLayoutItems)
    {
        m_customLayoutItems = std::move(customLayoutItems);

        if (m_pageLayoutMode == PageLayout::Custom)
        {
            // Recalculate only, if custom layout is active
            recalculate();
        }
    }
}

void PDFDrawSpaceController::recalculate()
{
    if (!m_document)
    {
        clear(true);
        return;
    }

    const PDFCatalog* catalog = m_document->getCatalog();
    size_t pageCount = catalog->getPageCount();

    // Clear the old draw space
    clear(false);

    static constexpr size_t INVALID_PAGE_INDEX = std::numeric_limits<size_t>::max();

    // Places the pages on the left/right sides. Pages can be nullptr, but not both of them.
    // Updates bounding rectangle.
    auto placePagesLeftRight = [this, catalog](PDFInteger blockIndex, size_t leftIndex, size_t rightIndex, PDFReal& yPos, QRectF& boundingRect)
    {
        PDFReal yPosAdvance = 0.0;

        if (leftIndex != INVALID_PAGE_INDEX)
        {
            QSizeF pageSize = PDFPage::getRotatedBox(catalog->getPage(leftIndex)->getRotatedMediaBoxMM(), m_pageRotation).size();
            PDFReal xPos = -pageSize.width() - m_horizontalSpacingMM * 0.5;
            QRectF rect(xPos, yPos, pageSize.width(), pageSize.height());
            m_layoutItems.emplace_back(blockIndex, leftIndex, -1, rect);
            yPosAdvance = qMax(yPosAdvance, pageSize.height());
            boundingRect = boundingRect.united(rect);
        }

        if (rightIndex != INVALID_PAGE_INDEX)
        {
            QSizeF pageSize = PDFPage::getRotatedBox(catalog->getPage(rightIndex)->getRotatedMediaBoxMM(), m_pageRotation).size();
            PDFReal xPos = m_horizontalSpacingMM * 0.5;
            QRectF rect(xPos, yPos, pageSize.width(), pageSize.height());
            m_layoutItems.emplace_back(blockIndex, rightIndex, -1, rect);
            yPosAdvance = qMax(yPosAdvance, pageSize.height());
            boundingRect = boundingRect.united(rect);
        }

        if (yPosAdvance > 0.0)
        {
            yPos += yPosAdvance + m_verticalSpacingMM;
        }
    };

    // Generates block with pages using page indices. If generateBlocks is true, then
    // for each pair of pages, single block is generated, otherwise block containing all
    // pages is generated.
    auto placePagesLeftRightByIndices = [this, &placePagesLeftRight](const std::vector<size_t>& indices, bool generateBlocks)
    {
        Q_ASSERT(indices.size() % 2 == 0);

        PDFReal yPos = 0.0;
        PDFInteger blockIndex = 0;
        QRectF boundingRectangle;

        size_t count = indices.size() / 2;
        for (size_t i = 0; i < count; ++i)
        {
            const size_t leftPageIndex = indices[2 * i];
            const size_t rightPageIndex = indices[2 * i + 1];
            placePagesLeftRight(blockIndex, leftPageIndex, rightPageIndex, yPos, boundingRectangle);

            if (generateBlocks)
            {
                m_blockItems.emplace_back(boundingRectangle);

                // Clear the old data
                yPos = 0.0;
                ++blockIndex;
                boundingRectangle = QRectF();
            }
        }

        if (!generateBlocks)
        {
            // Generate single block for all layed out pages
            m_blockItems.emplace_back(boundingRectangle);
        }
    };

    switch (m_pageLayoutMode)
    {
        case PageLayout::SinglePage:
        {
            // Each block contains single page
            m_layoutItems.reserve(pageCount);
            m_blockItems.reserve(pageCount);

            // Pages can have different size, so we center them around the center.
            // Block size will equal to the page size.

            for (size_t i = 0; i < pageCount; ++i)
            {
                QSizeF pageSize = PDFPage::getRotatedBox(catalog->getPage(i)->getRotatedMediaBoxMM(), m_pageRotation).size();
                QRectF rect(-pageSize.width() * 0.5, -pageSize.height() * 0.5, pageSize.width(), pageSize.height());
                m_layoutItems.emplace_back(i, i, -1, rect);
                m_blockItems.emplace_back(rect);
            }

            break;
        }

        case PageLayout::OneColumn:
        {
            // Single block, one column
            m_layoutItems.reserve(pageCount);
            m_blockItems.reserve(1);

            PDFReal yPos = 0.0;
            QRectF boundingRectangle;

            for (size_t i = 0; i < pageCount; ++i)
            {
                // Top of current page is at yPos.
                QSizeF pageSize = PDFPage::getRotatedBox(catalog->getPage(i)->getRotatedMediaBoxMM(), m_pageRotation).size();
                QRectF rect(-pageSize.width() * 0.5, yPos, pageSize.width(), pageSize.height());
                m_layoutItems.emplace_back(0, i, -1, rect);
                yPos += pageSize.height() + m_verticalSpacingMM;
                boundingRectangle = boundingRectangle.united(rect);
            }

            // Insert the single block with union of bounding rectangles
            m_blockItems.emplace_back(boundingRectangle);

            break;
        }

        case PageLayout::TwoColumnLeft:
        {
            // Pages with number 1, 3, 5, ... are on the left, 2, 4, 6 are on the right.
            // Page indices are numbered from 0, so pages 0, 2, 4 will be on the left,
            // 1, 3, 5 will be on the right.
            // For purposes or paging, "left" pages will be on the left side of y axis (negative x axis),
            // the "right" pages will be on the right side of y axis (positive x axis).

            m_layoutItems.reserve(pageCount);
            m_blockItems.reserve(1);

            std::vector<size_t> pageIndices(pageCount, INVALID_PAGE_INDEX);
            std::iota(pageIndices.begin(), pageIndices.end(), static_cast<size_t>(0));

            if (pageIndices.size() % 2 == 1)
            {
                pageIndices.push_back(INVALID_PAGE_INDEX);
            }

            placePagesLeftRightByIndices(pageIndices, false);
            break;
        }

        case PageLayout::TwoColumnRight:
        {
            // Similar to previous case, but page sequence start on the right.

            m_layoutItems.reserve(pageCount);
            m_blockItems.reserve(1);

            std::vector<size_t> pageIndices(pageCount + 1, INVALID_PAGE_INDEX);
            std::iota(std::next(pageIndices.begin()), pageIndices.end(), static_cast<size_t>(0));

            if (pageIndices.size() % 2 == 1)
            {
                pageIndices.push_back(INVALID_PAGE_INDEX);
            }

            placePagesLeftRightByIndices(pageIndices, false);
            break;
        }

        case PageLayout::TwoPagesLeft:
        {
            m_layoutItems.reserve(pageCount);
            m_blockItems.reserve((pageCount / 2) + (pageCount % 2));

            std::vector<size_t> pageIndices(pageCount, INVALID_PAGE_INDEX);
            std::iota(pageIndices.begin(), pageIndices.end(), static_cast<size_t>(0));

            if (pageIndices.size() % 2 == 1)
            {
                pageIndices.push_back(INVALID_PAGE_INDEX);
            }

            placePagesLeftRightByIndices(pageIndices, true);
            break;
        }

        case PageLayout::TwoPagesRight:
        {
            m_layoutItems.reserve(pageCount);
            m_blockItems.reserve((pageCount / 2) + (pageCount % 2));

            std::vector<size_t> pageIndices(pageCount + 1, INVALID_PAGE_INDEX);
            std::iota(std::next(pageIndices.begin()), pageIndices.end(), static_cast<size_t>(0));

            if (pageIndices.size() % 2 == 1)
            {
                pageIndices.push_back(INVALID_PAGE_INDEX);
            }

            placePagesLeftRightByIndices(pageIndices, true);
            break;
        }

        case PageLayout::Custom:
        {
            m_layoutItems = m_customLayoutItems;

            // We do not support page rotation for custom layout
            Q_ASSERT(m_pageRotation == PageRotation::None);

            // Assure, that layout items are sorted by block and page group
            auto comparator = [](const LayoutItem& l, const LayoutItem& r)
            {
                return std::tie(l.blockIndex, l.groupIndex) < std::tie(r.blockIndex, r.groupIndex);
            };
            std::stable_sort(m_layoutItems.begin(), m_layoutItems.end(), comparator);

            // Now, compute blocks
            if (!m_layoutItems.empty())
            {
                m_blockItems.reserve(m_layoutItems.back().blockIndex + 1);

                QRectF currentBoundingRect;
                PDFInteger blockIndex = -1;

                for (const LayoutItem& layoutItem : m_layoutItems)
                {
                    if (blockIndex != layoutItem.blockIndex)
                    {
                        blockIndex = layoutItem.blockIndex;

                        if (currentBoundingRect.isValid())
                        {
                            m_blockItems.push_back(LayoutBlock(currentBoundingRect));
                            currentBoundingRect = QRectF();
                        }
                    }

                    currentBoundingRect = currentBoundingRect.united(layoutItem.pageRectMM);
                }

                if (currentBoundingRect.isValid())
                {
                    m_blockItems.push_back(LayoutBlock(currentBoundingRect));
                    currentBoundingRect = QRectF();
                }
            }

            break;
        }

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    Q_EMIT drawSpaceChanged();
}

void PDFDrawSpaceController::clear(bool emitSignal)
{
    m_layoutItems.clear();
    m_blockItems.clear();

    if (emitSignal)
    {
        Q_EMIT drawSpaceChanged();
    }
}

PDFDrawWidgetProxy::PDFDrawWidgetProxy(QObject* parent) :
    QObject(parent),
    m_updateDisabled(false),
    m_currentBlock(INVALID_BLOCK_INDEX),
    m_pixelPerMM(PDF_DEFAULT_DPMM),
    m_zoom(1.0),
    m_pixelToDeviceSpaceUnit(0.0),
    m_deviceSpaceUnitToPixel(0.0),
    m_verticalOffset(0),
    m_horizontalOffset(0),
    m_controller(nullptr),
    m_widget(nullptr),
    m_verticalScrollbar(nullptr),
    m_horizontalScrollbar(nullptr),
    m_features(PDFRenderer::getDefaultFeatures()),
    m_compiler(new PDFAsynchronousPageCompiler(this)),
    m_textLayoutCompiler(new PDFAsynchronousTextLayoutCompiler(this)),
    m_rasterizer(new PDFRasterizer(this)),
    m_progress(nullptr),
    m_cacheClearTimer(new QTimer(this)),
    m_rendererEngine(RendererEngine::Blend2D_MultiThread)
{
    m_controller = new PDFDrawSpaceController(this);
    connect(m_controller, &PDFDrawSpaceController::drawSpaceChanged, this, &PDFDrawWidgetProxy::update);
    connect(m_controller, &PDFDrawSpaceController::repaintNeeded, this, &PDFDrawWidgetProxy::repaintNeeded);
    connect(m_controller, &PDFDrawSpaceController::pageImageChanged, this, &PDFDrawWidgetProxy::pageImageChanged);
    connect(m_compiler, &PDFAsynchronousPageCompiler::renderingError, this, &PDFDrawWidgetProxy::renderingError);
    connect(m_compiler, &PDFAsynchronousPageCompiler::pageImageChanged, this, &PDFDrawWidgetProxy::pageImageChanged);
    connect(m_textLayoutCompiler, &PDFAsynchronousTextLayoutCompiler::textLayoutChanged, this, &PDFDrawWidgetProxy::onTextLayoutChanged);
    connect(m_cacheClearTimer, &QTimer::timeout, this, &PDFDrawWidgetProxy::performPageCacheClear);
}

PDFDrawWidgetProxy::~PDFDrawWidgetProxy()
{

}

void PDFDrawWidgetProxy::setDocument(const PDFModifiedDocument& document, std::vector<PDFSignatureVerificationResult> signatureVerificationResult)
{
    if (getDocument() != document)
    {
        m_cacheClearTimer->stop();
        m_compiler->stop(document.hasReset() || document.hasPageContentsChanged());
        m_textLayoutCompiler->stop(document.hasReset() || document.hasPageContentsChanged());
        m_controller->setDocument(document);

        if (PDFOptionalContentActivity* optionalContentActivity = document.getOptionalContentActivity())
        {
            connect(optionalContentActivity, &PDFOptionalContentActivity::optionalContentGroupStateChanged, this, &PDFDrawWidgetProxy::onOptionalContentGroupStateChanged, Qt::UniqueConnection);
        }

        m_compiler->start();
        m_textLayoutCompiler->start();

        if (document)
        {
            m_cacheClearTimer->start(CACHE_CLEAR_TIMEOUT);
        }
    }

    m_signatureVerificationResult = std::move(signatureVerificationResult);
}

void PDFDrawWidgetProxy::init(PDFWidget* widget)
{
    m_widget = widget;
    m_horizontalScrollbar = widget->getHorizontalScrollbar();
    m_verticalScrollbar = widget->getVerticalScrollbar();

    connect(m_horizontalScrollbar, &QScrollBar::valueChanged, this, &PDFDrawWidgetProxy::onHorizontalScrollbarValueChanged);
    connect(m_verticalScrollbar, &QScrollBar::valueChanged, this, &PDFDrawWidgetProxy::onVerticalScrollbarValueChanged);
    connect(this, &PDFDrawWidgetProxy::drawSpaceChanged, this, &PDFDrawWidgetProxy::repaintNeeded);
    connect(getCMSManager(), &PDFCMSManager::colorManagementSystemChanged, this, &PDFDrawWidgetProxy::onColorManagementSystemChanged);

    // We must update the draw space - widget has been set
    update();
}

void PDFDrawWidgetProxy::update()
{
    if (m_updateDisabled)
    {
        return;
    }
    PDFBoolGuard guard(m_updateDisabled);

    Q_ASSERT(m_widget);
    Q_ASSERT(m_horizontalScrollbar);
    Q_ASSERT(m_verticalScrollbar);

    QWidget* widget = m_widget->getDrawWidget()->getWidget();
    QScreen* primaryScreen = QGuiApplication::primaryScreen();

    // First, we must calculate pixel per mm ratio to obtain DPMM (device pixel per mm),
    // we also assume, that zoom is correctly set.
    QSizeF physicalSize = primaryScreen->physicalSize();
    QSizeF pixelSize = primaryScreen->size();

    m_pixelPerMM = pixelSize.width() / physicalSize.width();

    // Are we using logical pixels instead of physical ones?
    if (m_features.testFlag(PDFRenderer::LogicalSizeZooming))
    {
        qreal ldpi = primaryScreen->logicalDotsPerInch();
        qreal pdpi = primaryScreen->physicalDotsPerInch();
        qreal ratioLogicalToPhysical = ldpi / pdpi;
        m_pixelPerMM *= ratioLogicalToPhysical;
    }

    Q_ASSERT(m_zoom > 0.0);
    Q_ASSERT(m_pixelPerMM > 0.0);

    m_deviceSpaceUnitToPixel = m_pixelPerMM * m_zoom;
    m_pixelToDeviceSpaceUnit = 1.0 / m_deviceSpaceUnitToPixel;

    m_layout.clear();

    // Switch to the first block, if we haven't selected any, otherwise fix active
    // block item (select first block available).
    if (m_controller->getBlockCount() > 0)
    {
        if (m_currentBlock == INVALID_BLOCK_INDEX)
        {
            m_currentBlock = 0;
        }
        else
        {
            m_currentBlock = qBound<PDFInteger>(0, m_currentBlock, m_controller->getBlockCount() - 1);
        }
    }
    else
    {
        m_currentBlock = INVALID_BLOCK_INDEX;
    }

    // Then, create pixel size layout of the pages using the draw space controller
    QRectF rectangle = m_controller->getBlockBoundingRectangle(m_currentBlock);
    if (rectangle.isValid())
    {
        // We must have a valid block
        PDFDrawSpaceController::LayoutItems items = m_controller->getLayoutItems(m_currentBlock);

        m_layout.items.reserve(items.size());
        for (const PDFDrawSpaceController::LayoutItem& item : items)
        {
            m_layout.items.emplace_back(item.pageIndex, item.groupIndex, fromDeviceSpace(item.pageRectMM).toRect());
        }

        m_layout.blockRect = fromDeviceSpace(rectangle).toRect();
    }

    QSize blockSize = m_layout.blockRect.size();
    QSize widgetSize = widget->size();

    // Horizontal scrollbar
    const int horizontalDifference = blockSize.width() - widgetSize.width();
    if (horizontalDifference > 0)
    {
        m_horizontalScrollbar->setVisible(true);
        m_horizontalScrollbar->setMinimum(0);
        m_horizontalScrollbar->setMaximum(horizontalDifference);

        m_horizontalOffsetRange = Range<PDFInteger>(-horizontalDifference, 0);
        m_horizontalOffset = m_horizontalOffsetRange.bound(m_horizontalOffset);
        m_horizontalScrollbar->setValue(-m_horizontalOffset);
    }
    else
    {
        // We do not need the horizontal scrollbar, because block can be draw onto widget entirely.
        // We set the offset to the half of available empty space.
        m_horizontalScrollbar->setVisible(false);
        m_horizontalOffset = -horizontalDifference / 2;
        m_horizontalOffsetRange = Range<PDFInteger>(m_horizontalOffset);
    }

    // Vertical scrollbar - has two meanings, in block mode, it switches between blocks,
    // in continuous mode, it controls the vertical offset.
    if (isBlockMode())
    {
        size_t blockCount = m_controller->getBlockCount();
        if (blockCount > 0)
        {
            Q_ASSERT(m_currentBlock != INVALID_BLOCK_INDEX);

            m_verticalScrollbar->setVisible(blockCount > 1);
            m_verticalScrollbar->setMinimum(0);
            m_verticalScrollbar->setMaximum(static_cast<int>(blockCount - 1));
            m_verticalScrollbar->setValue(static_cast<int>(m_currentBlock));
            m_verticalScrollbar->setSingleStep(1);
            m_verticalScrollbar->setPageStep(1);
        }
        else
        {
            Q_ASSERT(m_currentBlock == INVALID_BLOCK_INDEX);
            m_verticalScrollbar->setVisible(false);
        }

        // We must fix case, when we can display everything on the widget (we have
        // enough space). Then we will center the page on the widget.
        const int verticalDifference = blockSize.height() - widgetSize.height();
        if (verticalDifference < 0)
        {
            m_verticalOffset = -verticalDifference / 2;
            m_verticalOffsetRange = Range<PDFInteger>(m_verticalOffset);
        }
        else
        {
            m_verticalOffsetRange = Range<PDFInteger>(-verticalDifference, 0);
            m_verticalOffset = m_verticalOffsetRange.bound(m_verticalOffset);
        }
    }
    else
    {
        const int verticalDifference = blockSize.height() - widgetSize.height();
        if (verticalDifference > 0)
        {
            m_verticalScrollbar->setVisible(true);
            m_verticalScrollbar->setMinimum(0);
            m_verticalScrollbar->setMaximum(verticalDifference);

            // We must also calculate single step/page step. Because pages can have different size,
            // we use first page to compute page step.
            if (!m_layout.items.empty())
            {
                const LayoutItem& item = m_layout.items.front();

                const int pageStep = qMax(item.pageRect.height(), 1);
                const int singleStep = qMax(pageStep / 10, 1);

                m_verticalScrollbar->setPageStep(pageStep);
                m_verticalScrollbar->setSingleStep(singleStep);
            }

            m_verticalOffsetRange = Range<PDFInteger>(-verticalDifference, 0);
            m_verticalOffset = m_verticalOffsetRange.bound(m_verticalOffset);
            m_verticalScrollbar->setValue(-m_verticalOffset);
        }
        else
        {
            m_verticalScrollbar->setVisible(false);
            m_verticalOffset = -verticalDifference / 2;
            m_verticalOffsetRange = Range<PDFInteger>(m_verticalOffset);
        }
    }

    Q_EMIT drawSpaceChanged();
}

QTransform PDFDrawWidgetProxy::createPagePointToDevicePointMatrix(const PDFPage* page, const QRectF& rectangle) const
{
    QTransform matrix;

    // We want to create transformation from unrotated rectangle
    // to rotated page rectangle.

    QRectF unrotatedRectangle = rectangle;
    switch (m_controller->getPageRotation())
    {
        case PageRotation::None:
            break;

        case PageRotation::Rotate180:
        {
            matrix.translate(rectangle.left(), rectangle.top() + rectangle.bottom());
            matrix.scale(1.0, -1.0);
            matrix.translate(rectangle.width(), 0);
            matrix.scale(-1.0, 1.0);
            matrix.translate(-rectangle.left(), 0);
            Q_ASSERT(qFuzzyCompare(matrix.map(rectangle.topLeft()).y(), rectangle.bottom()));
            Q_ASSERT(qFuzzyCompare(matrix.map(rectangle.bottomLeft()).y(), rectangle.top()));
            Q_ASSERT(qFuzzyCompare(matrix.map(rectangle.topLeft()).x(), rectangle.right()));
            Q_ASSERT(qFuzzyCompare(matrix.map(rectangle.bottomRight()).x(), rectangle.left()));
            break;
        }

        case PageRotation::Rotate90:
        {
            unrotatedRectangle = rectangle.transposed();
            matrix.translate(rectangle.left(), rectangle.top());
            matrix.rotate(90);
            matrix.translate(-rectangle.left(), -rectangle.top());
            matrix.translate(0, -unrotatedRectangle.height());
            break;
        }

        case PageRotation::Rotate270:
        {
            unrotatedRectangle = rectangle.transposed();
            matrix.translate(rectangle.left(), rectangle.top());
            matrix.rotate(90);
            matrix.translate(-rectangle.left(), -rectangle.top());
            matrix.translate(0, -unrotatedRectangle.height());
            matrix.translate(0.0, unrotatedRectangle.top() + unrotatedRectangle.bottom());
            matrix.scale(1.0, -1.0);
            matrix.translate(unrotatedRectangle.left() + unrotatedRectangle.right(), 0.0);
            matrix.scale(-1.0, 1.0);
            break;
        }

        default:
            break;
    }

    return PDFRenderer::createPagePointToDevicePointMatrix(page, unrotatedRectangle) * matrix;
}

void PDFDrawWidgetProxy::draw(QPainter* painter, QRect rect)
{
    drawPages(painter, rect, m_features);

    for (IDocumentDrawInterface* drawInterface : m_drawInterfaces)
    {
        painter->save();
        drawInterface->drawPostRendering(painter, rect);
        painter->restore();
    }
}

QColor PDFDrawWidgetProxy::getPaperColor()
{
    QColor paperColor = getCMSManager()->getCurrentCMS()->getPaperColor();
    PDFColorConvertor colorConvertor = getCMSManager()->getColorConvertor();
    PDFRenderer::applyFeaturesToColorConvertor(getFeatures(), colorConvertor);

    paperColor = colorConvertor.convert(paperColor, true, false);
    return paperColor;
}

void PDFDrawWidgetProxy::drawPages(QPainter* painter, QRect rect, PDFRenderer::Features features)
{
    const bool darkScheme = PDFWidgetUtils::isDarkTheme();
    QColor backgroundColor = darkScheme ? QColor(34, 34, 34) : QColor(Qt::lightGray);

    painter->fillRect(rect, backgroundColor);
    QTransform baseMatrix = painter->worldTransform();

    // Use current paper color (it can be a bit different from white)
    QColor paperColor = getPaperColor();

    // Color management system
    PDFCMSPointer cms = getCMSManager()->getCurrentCMS();
    PDFColorConvertor convertor = cms->getColorConvertor();
    PDFRenderer::applyFeaturesToColorConvertor(features, convertor);

    // Iterate trough pages and display them on the painter device
    for (const LayoutItem& item : m_layout.items)
    {
        // The offsets m_horizontalOffset and m_verticalOffset are offsets to the
        // topleft point of the block. But block maybe doesn't start at (0, 0),
        // so we must also use translation from the block beginning.
        QRect placedRect = item.pageRect.translated(m_horizontalOffset - m_layout.blockRect.left(), m_verticalOffset - m_layout.blockRect.top());
        if (placedRect.intersects(rect))
        {
            GroupInfo groupInfo = getGroupInfo(item.groupIndex);

            // Clear the page space by paper color
            if (groupInfo.drawPaper)
            {
                painter->fillRect(placedRect, paperColor);
            }

            const PDFPrecompiledPage* compiledPage = m_compiler->getCompiledPage(item.pageIndex, true);
            if (compiledPage && compiledPage->isValid())
            {
                QElapsedTimer timer;
                timer.start();

                const PDFPage* page = m_controller->getDocument()->getCatalog()->getPage(item.pageIndex);
                QTransform matrix = QTransform(createPagePointToDevicePointMatrix(page, placedRect)) * baseMatrix;
                PDFTextLayoutGetter layoutGetter = m_textLayoutCompiler->getTextLayoutLazy(item.pageIndex);

                bool isPageContentDrawSuppressed = false;
                for (IDocumentDrawInterface* drawInterface : m_drawInterfaces)
                {
                    isPageContentDrawSuppressed = isPageContentDrawSuppressed || drawInterface->isPageContentDrawSuppressed();
                }

                if (!isPageContentDrawSuppressed)
                {
                    compiledPage->draw(painter, page->getCropBox(), matrix, features, groupInfo.transparency);
                }

                // Draw text blocks/text lines, if it is enabled
                if (features.testFlag(PDFRenderer::DebugTextBlocks))
                {
                    m_textLayoutCompiler->makeTextLayout();
                    const PDFTextLayout& layout = layoutGetter;
                    const PDFTextBlocks& textBlocks = layout.getTextBlocks();

                    painter->save();
                    painter->setFont(m_widget->font());
                    painter->setPen(Qt::red);
                    painter->setBrush(QColor(255, 0, 0, 128));

                    QFontMetricsF fontMetrics(painter->font(), painter->device());
                    int blockIndex = 1;
                    for (const PDFTextBlock& block : textBlocks)
                    {
                        QString blockNumber = QString::number(blockIndex++);

                        painter->drawPath(matrix.map(block.getBoundingBox()));
                        painter->drawText(matrix.map(block.getTopLeft()) - QPointF(fontMetrics.horizontalAdvance(blockNumber), 0), blockNumber, Qt::TextSingleLine, 0);
                    }

                    painter->restore();
                }
                if (features.testFlag(PDFRenderer::DebugTextLines))
                {
                    m_textLayoutCompiler->makeTextLayout();
                    const PDFTextLayout& layout = layoutGetter;
                    const PDFTextBlocks& textBlocks = layout.getTextBlocks();

                    painter->save();
                    painter->setFont(m_widget->font());
                    painter->setPen(Qt::green);
                    painter->setBrush(QColor(0, 255, 0, 128));

                    QFontMetricsF fontMetrics(painter->font(), painter->device());
                    int lineIndex = 1;
                    for (const PDFTextBlock& block : textBlocks)
                    {
                        for (const PDFTextLine& line : block.getLines())
                        {
                            QString lineNumber = QString::number(lineIndex++);

                            painter->drawPath(matrix.map(line.getBoundingBox()));
                            painter->drawText(matrix.map(line.getTopLeft()) - QPointF(fontMetrics.horizontalAdvance(lineNumber), 0), lineNumber, Qt::TextSingleLine, 0);
                        }
                    }

                    painter->restore();
                }

                QList<PDFRenderError> drawInterfaceErrors;
                if (!features.testFlag(PDFRenderer::DenyExtraGraphics))
                {
                    for (IDocumentDrawInterface* drawInterface : m_drawInterfaces)
                    {
                        painter->save();
                        drawInterface->drawPage(painter, item.pageIndex, compiledPage, layoutGetter, matrix, convertor, drawInterfaceErrors);
                        painter->restore();
                    }
                }

                const qint64 drawTimeNS = timer.nsecsElapsed();

                // Draw rendering times
                if (features.testFlag(PDFRenderer::DisplayTimes))
                {
                    QFont font = m_widget->font();
                    font.setPointSize(12);

                    auto formatDrawTime = [](qint64 nanoseconds)
                    {
                        PDFReal miliseconds = nanoseconds / 1000000.0;
                        return QString::number(miliseconds, 'f', 3);
                    };

                    QFontMetrics fontMetrics(font);
                    const int lineSpacing = fontMetrics.lineSpacing();

                    painter->save();
                    painter->setPen(Qt::red);
                    painter->setFont(font);
                    painter->translate(placedRect.topLeft());
                    painter->translate(placedRect.width() / 20.0, placedRect.height() / 20.0); // Offset

                    painter->setBackground(QBrush(Qt::white));
                    painter->setBackgroundMode(Qt::OpaqueMode);
                    painter->drawText(0, 0, PDFTranslationContext::tr("Compile time:    %1 [ms]").arg(formatDrawTime(compiledPage->getCompilingTimeNS())));
                    painter->translate(0, lineSpacing);
                    painter->drawText(0, 0, PDFTranslationContext::tr("Draw time:       %1 [ms]").arg(formatDrawTime(drawTimeNS)));

                    painter->restore();
                }

                const QList<PDFRenderError>& pageErrors = compiledPage->getErrors();
                if (!pageErrors.empty() || !drawInterfaceErrors.empty())
                {
                    QList<PDFRenderError> errors = pageErrors;
                    if (!drawInterfaceErrors.isEmpty())
                    {
                        errors.append(drawInterfaceErrors);
                    }
                    Q_EMIT renderingError(item.pageIndex, qMove(errors));
                }
            }
        }
    }
}

QImage PDFDrawWidgetProxy::drawThumbnailImage(PDFInteger pageIndex, int pixelSize) const
{
    QImage image;
    if (!m_controller->getDocument())
    {
        // No thumbnail - return empty image
        return image;
    }

    if (const PDFPage* page = m_controller->getDocument()->getCatalog()->getPage(pageIndex))
    {
        QRectF pageRect = page->getRotatedMediaBox();
        QSizeF pageSize = pageRect.size();
        pageSize.scale(pixelSize, pixelSize, Qt::KeepAspectRatio);
        QSize imageSize = pageSize.toSize();

        if (imageSize.isValid())
        {
            const PDFPrecompiledPage* compiledPage = m_compiler->getCompiledPage(pageIndex, true);
            if (compiledPage && compiledPage->isValid())
            {
                // Rasterize the image.
                PDFCMSPointer cms = getCMSManager()->getCurrentCMS();
                image = m_rasterizer->render(pageIndex, page, compiledPage, imageSize, m_features, m_widget->getAnnotationManager(), cms.data(), PageRotation::None);
            }

            if (image.isNull())
            {
                image = QImage(imageSize, QImage::Format_RGBA8888_Premultiplied);
                image.fill(Qt::white);
            }
        }
    }

    return image;
}

std::vector<PDFInteger> PDFDrawWidgetProxy::getPagesIntersectingRect(QRect rect) const
{
    std::vector<PDFInteger> pages;

    // We assume, that no more, than 32 pages will be displayed in the rectangle
    pages.reserve(32);

    // Iterate trough pages, place them and test, if they intersects with rectangle
    for (const LayoutItem& item : m_layout.items)
    {
        // The offsets m_horizontalOffset and m_verticalOffset are offsets to the
        // topleft point of the block. But block maybe doesn't start at (0, 0),
        // so we must also use translation from the block beginning.
        QRect placedRect = item.pageRect.translated(m_horizontalOffset - m_layout.blockRect.left(), m_verticalOffset - m_layout.blockRect.top());
        if (placedRect.intersects(rect))
        {
            pages.push_back(item.pageIndex);
        }
    }
    std::sort(pages.begin(), pages.end());

    return pages;
}

std::vector<PDFInteger> PDFDrawWidgetProxy::getActivePages() const
{
    std::vector<PDFInteger> activePages = getPagesIntersectingRect(m_widget->rect());

    // Consider page prefetching - at least two pages after last current
    // pages are treated as active.
    if (!activePages.empty())
    {
        if (const PDFDocument* document = getDocument())
        {
            const PDFInteger pageIndex = activePages.back();
            const PDFInteger pageCount = document->getCatalog()->getPageCount();
            const PDFInteger pageEnd = qMin(pageCount, pageIndex + 3);
            for (PDFInteger i = pageIndex + 1; i < pageEnd; ++i)
            {
                activePages.push_back(i);
            }
        }
    }

    return activePages;
}

PDFInteger PDFDrawWidgetProxy::getPageUnderPoint(QPoint point, QPointF* pagePoint) const
{
    // Iterate trough pages, place them and test, if they intersects with rectangle
    for (const LayoutItem& item : m_layout.items)
    {
        // The offsets m_horizontalOffset and m_verticalOffset are offsets to the
        // topleft point of the block. But block maybe doesn't start at (0, 0),
        // so we must also use translation from the block beginning.
        QRect placedRect = item.pageRect.translated(m_horizontalOffset - m_layout.blockRect.left(), m_verticalOffset - m_layout.blockRect.top());
        placedRect.adjust(0, 0, 1, 1);
        if (placedRect.contains(point))
        {
            if (pagePoint)
            {
                const PDFPage* page = m_controller->getDocument()->getCatalog()->getPage(item.pageIndex);
                QTransform matrix = createPagePointToDevicePointMatrix(page, placedRect).inverted();
                *pagePoint = matrix.map(point);
            }

            return item.pageIndex;
        }
    }

    return -1;
}

PDFWidgetSnapshot PDFDrawWidgetProxy::getSnapshot() const
{
    PDFWidgetSnapshot snapshot;

    // Get the viewport
    QRect viewport = getWidget()->rect();

    // Iterate trough pages, place them and test, if they intersects with rectangle
    for (const LayoutItem& item : m_layout.items)
    {
        // The offsets m_horizontalOffset and m_verticalOffset are offsets to the
        // topleft point of the block. But block maybe doesn't start at (0, 0),
        // so we must also use translation from the block beginning.
        QRect placedRect = item.pageRect.translated(m_horizontalOffset - m_layout.blockRect.left(), m_verticalOffset - m_layout.blockRect.top());
        if (placedRect.intersects(viewport))
        {
            const PDFPage* page = m_controller->getDocument()->getCatalog()->getPage(item.pageIndex);

            PDFWidgetSnapshot::SnapshotItem snapshotItem;
            snapshotItem.rect = placedRect;
            snapshotItem.pageIndex = item.pageIndex;
            snapshotItem.compiledPage = m_compiler->getCompiledPage(item.pageIndex, false);
            snapshotItem.pageToDeviceMatrix = createPagePointToDevicePointMatrix(page, placedRect);
            snapshot.items.emplace_back(qMove(snapshotItem));
        }
    }

    return snapshot;
}

void PDFDrawWidgetProxy::setGroupTransparency(PDFInteger groupIndex, bool drawPaper, PDFReal transparency)
{
    GroupInfo groupInfo;
    groupInfo.drawPaper = drawPaper;
    groupInfo.transparency = transparency;

    if (groupInfo == GroupInfo())
    {
        m_groupInfos.erase(groupIndex);
    }
    else
    {
        m_groupInfos[groupIndex] = std::move(groupInfo);
    }
}

QRect PDFDrawWidgetProxy::getPagesIntersectingRectBoundingBox(QRect rect) const
{
    QRect resultRect;

    // Iterate trough pages, place them and test, if they intersects with rectangle
    for (const LayoutItem& item : m_layout.items)
    {
        // The offsets m_horizontalOffset and m_verticalOffset are offsets to the
        // topleft point of the block. But block maybe doesn't start at (0, 0),
        // so we must also use translation from the block beginning.
        QRect placedRect = item.pageRect.translated(m_horizontalOffset - m_layout.blockRect.left(), m_verticalOffset - m_layout.blockRect.top());
        if (placedRect.intersects(rect))
        {
            resultRect = resultRect.united(placedRect);
        }
    }

    return resultRect;
}

void PDFDrawWidgetProxy::performOperation(Operation operation)
{
    switch (operation)
    {
        case NavigateDocumentStart:
        {
            if (m_verticalScrollbar->isVisible())
            {
                m_verticalScrollbar->setValue(0);
            }
            break;
        }

        case NavigateDocumentEnd:
        {
            if (m_verticalScrollbar->isVisible())
            {
                m_verticalScrollbar->setValue(m_verticalScrollbar->maximum());
            }
            break;
        }

        case NavigateNextPage:
        {
            if (m_verticalScrollbar->isVisible())
            {
                m_verticalScrollbar->setValue(m_verticalScrollbar->value() + m_verticalScrollbar->pageStep());
            }
            break;
        }

        case NavigatePreviousPage:
        {
            if (m_verticalScrollbar->isVisible())
            {
                m_verticalScrollbar->setValue(m_verticalScrollbar->value() - m_verticalScrollbar->pageStep());
            }
            break;
        }

        case NavigateNextStep:
        {
            if (m_verticalScrollbar->isVisible())
            {
                m_verticalScrollbar->setValue(m_verticalScrollbar->value() + m_verticalScrollbar->singleStep());
            }
            break;
        }

        case NavigatePreviousStep:
        {
            if (m_verticalScrollbar->isVisible())
            {
                m_verticalScrollbar->setValue(m_verticalScrollbar->value() - m_verticalScrollbar->singleStep());
            }
            break;
        }

        case ZoomIn:
        {
            zoom(m_zoom * ZOOM_STEP);
            break;
        }

        case ZoomOut:
        {
            zoom(m_zoom / ZOOM_STEP);
            break;
        }

        case ZoomFit:
        {
            zoom(getZoomHint(ZoomHint::Fit));
            break;
        }

        case ZoomFitWidth:
        {
            zoom(getZoomHint(ZoomHint::FitWidth));
            break;
        }

        case ZoomFitHeight:
        {
            zoom(getZoomHint(ZoomHint::FitHeight));
            break;
        }

        case RotateRight:
        {
            m_controller->setPageRotation(getPageRotationRotatedRight(m_controller->getPageRotation()));
            break;
        }

        case RotateLeft:
        {
            m_controller->setPageRotation(getPageRotationRotatedLeft(m_controller->getPageRotation()));
            break;
        }

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }
}

QPoint PDFDrawWidgetProxy::scrollByPixels(QPoint offset)
{
    PDFInteger oldHorizontalOffset = m_horizontalOffset;
    PDFInteger oldVerticalOffset = m_verticalOffset;

    setHorizontalOffset(m_horizontalOffset + offset.x());
    setVerticalOffset(m_verticalOffset + offset.y());

    return QPoint(m_horizontalOffset - oldHorizontalOffset, m_verticalOffset - oldVerticalOffset);
}

void PDFDrawWidgetProxy::zoom(PDFReal zoom, std::optional<QPointF> widgetPosition)
{
    const PDFReal clampedZoom = qBound(MIN_ZOOM, zoom, MAX_ZOOM);
    if (m_zoom != clampedZoom)
    {
        if (widgetPosition.has_value())
        {
            QPointF point = widgetPosition.value();

            const PDFReal posX = (m_horizontalOffset - point.x()) * m_pixelToDeviceSpaceUnit;
            const PDFReal posY = (m_verticalOffset - point.y()) * m_pixelToDeviceSpaceUnit;

            m_zoom = clampedZoom;

            update();

            const PDFReal hp = posX / m_pixelToDeviceSpaceUnit + point.x();
            const PDFReal vp = posY / m_pixelToDeviceSpaceUnit + point.y();

            setHorizontalOffset(hp);
            setVerticalOffset(vp);
        }
        else
        {
            const PDFReal oldHorizontalOffsetMM = m_horizontalOffset * m_pixelToDeviceSpaceUnit;
            const PDFReal oldVerticalOffsetMM = m_verticalOffset * m_pixelToDeviceSpaceUnit;

            m_zoom = clampedZoom;

            update();

            // Try to restore offsets, so we are in the same place
            setHorizontalOffset(oldHorizontalOffsetMM * m_deviceSpaceUnitToPixel);
            setVerticalOffset(oldVerticalOffsetMM * m_deviceSpaceUnitToPixel);
        }
    }
}

PDFReal PDFDrawWidgetProxy::getZoomHint(ZoomHint hint) const
{
    QSizeF referenceSize = m_controller->getReferenceBoundingBox();
    if (referenceSize.isValid())
    {
        const PDFReal ratio = 0.95;
        const PDFReal widthMM = m_widget->widthMM() * ratio;
        const PDFReal heightMM = m_widget->heightMM() * ratio;

        const PDFReal widthHint = widthMM / referenceSize.width();
        const PDFReal heightHint = heightMM / referenceSize.height();

        switch (hint)
        {
            case ZoomHint::Fit:
                return qMin(widthHint, heightHint);

            case ZoomHint::FitWidth:
                return widthHint;

            case ZoomHint::FitHeight:
                return heightHint;

            default:
                break;
        }
    }

    // Return default 100% zoom
    return 1.0;
}

void PDFDrawWidgetProxy::goToPage(PDFInteger pageIndex)
{
    PDFDrawSpaceController::LayoutItem layoutItem = m_controller->getLayoutItemForPage(pageIndex);

    if (layoutItem.isValid())
    {
        // We have found our page, navigate onto it
        if (isBlockMode())
        {
            setBlockIndex(layoutItem.blockIndex);
        }
        else
        {
            QRect rect = fromDeviceSpace(layoutItem.pageRectMM).toRect();
            setVerticalOffset(-rect.top() - m_layout.blockRect.top());
        }
    }
}

void PDFDrawWidgetProxy::goToPageAndEnsureVisible(PDFInteger pageIndex, QRectF ensureVisibleRect)
{
    PDFDrawSpaceController::LayoutItem layoutItem = m_controller->getLayoutItemForPage(pageIndex);

    if (layoutItem.isValid())
    {
        // We have found our page, navigate onto it
        if (isBlockMode())
        {
            setBlockIndex(layoutItem.blockIndex);
        }

        QRectF pageRect = fromDeviceSpace(layoutItem.pageRectMM);
        QRectF placedRect = pageRect.translated(m_horizontalOffset - m_layout.blockRect.left(), m_verticalOffset - m_layout.blockRect.top());

        const PDFPage* page = m_controller->getDocument()->getCatalog()->getPage(layoutItem.pageIndex);
        QTransform matrix = QTransform(createPagePointToDevicePointMatrix(page, placedRect));

        QRect wishedRect = matrix.mapRect(ensureVisibleRect).toRect();
        QRect displayedRect = getWidget()->getDrawWidget()->getWidget()->rect();
        QPoint topRectPoint = wishedRect.topLeft();

        if (!displayedRect.contains(topRectPoint))
        {
            QPoint center = displayedRect.center();
            QPoint p1(center.x(), topRectPoint.y());

            if (!displayedRect.contains(p1))
            {
                scrollByPixels(-QPoint(0, p1.y() - displayedRect.top()));
            }

            QPoint p2(topRectPoint.x(), center.y());
            if (!displayedRect.contains(p2))
            {
                scrollByPixels(-QPoint(p2.x() - displayedRect.x(), 0));
            }
        }
    }
}

void PDFDrawWidgetProxy::setPageLayout(PageLayout pageLayout)
{
    if (getPageLayout() != pageLayout)
    {
        std::optional<PDFInteger> pageIndex;

        if (m_widget)
        {
            std::vector<PDFInteger> currentPages = m_widget->getDrawWidget()->getCurrentPages();
            if (!currentPages.empty())
            {
                pageIndex = currentPages.front();
            }
        }

        m_controller->setPageLayout(pageLayout);

        if (pageIndex.has_value())
        {
            goToPage(*pageIndex);
        }

        Q_EMIT pageLayoutChanged();
    }
}

void PDFDrawWidgetProxy::setCustomPageLayout(PDFDrawSpaceController::LayoutItems layoutItems)
{
    if (m_controller->getCustomLayout() != layoutItems)
    {
        m_controller->setCustomLayout(std::move(layoutItems));
        Q_EMIT pageLayoutChanged();
    }
}

QRectF PDFDrawWidgetProxy::fromDeviceSpace(const QRectF& rect) const
{
    Q_ASSERT(rect.isValid());

    return QRectF(rect.left() * m_deviceSpaceUnitToPixel,
                  rect.top() * m_deviceSpaceUnitToPixel,
                  rect.width() * m_deviceSpaceUnitToPixel,
                  rect.height() * m_deviceSpaceUnitToPixel);
}

void PDFDrawWidgetProxy::performPageCacheClear()
{
    std::vector<PDFInteger> activePage = getActivePages();
    m_compiler->smartClearCache(CACHE_PAGE_EXPIRATION_TIMEOUT, activePage);
}

void PDFDrawWidgetProxy::onTextLayoutChanged()
{
    Q_EMIT repaintNeeded();
    Q_EMIT textLayoutChanged();
}

bool PDFDrawWidgetProxy::isBlockMode() const
{
    switch (m_controller->getPageLayout())
    {
        case PageLayout::OneColumn:
        case PageLayout::TwoColumnLeft:
        case PageLayout::TwoColumnRight:
            return false;

        case PageLayout::SinglePage:
        case PageLayout::TwoPagesLeft:
        case PageLayout::TwoPagesRight:
            return true;

        case PageLayout::Custom:
            return m_controller->getBlockCount() > 1;
    }

    Q_ASSERT(false);
    return false;
}

void PDFDrawWidgetProxy::updateRenderer(RendererEngine rendererEngine)
{
    m_rendererEngine = rendererEngine;
    m_rasterizer->reset(m_rendererEngine);
}

void PDFDrawWidgetProxy::prefetchPages(PDFInteger pageIndex)
{
    // Determine number of pages, which should be prefetched. In case of two or more pages,
    // we need to prefetch more pages (for example, two for two columns/two pages display mode).
    int prefetchCount = 0;
    switch (m_controller->getPageLayout())
    {
        case PageLayout::OneColumn:
        case PageLayout::SinglePage:
            prefetchCount = 1;
            break;

        case PageLayout::TwoPagesLeft:
        case PageLayout::TwoPagesRight:
        case PageLayout::TwoColumnLeft:
        case PageLayout::TwoColumnRight:
            prefetchCount = 2;
            break;

        case PageLayout::Custom:
            prefetchCount = 0;
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    if (const PDFDocument* document = getDocument())
    {
        const PDFInteger pageCount = document->getCatalog()->getPageCount();
        const PDFInteger pageEnd = qMin(pageCount, pageIndex + prefetchCount + 1);
        for (PDFInteger i = pageIndex + 1; i < pageEnd; ++i)
        {
            m_compiler->getCompiledPage(i, true);
        }
    }
}

void PDFDrawWidgetProxy::onHorizontalScrollbarValueChanged(int value)
{
    if (!m_updateDisabled && !m_horizontalScrollbar->isHidden())
    {
        setHorizontalOffset(-value);
    }
}

void PDFDrawWidgetProxy::onVerticalScrollbarValueChanged(int value)
{
    if (!m_updateDisabled && !m_verticalScrollbar->isHidden())
    {
        if (isBlockMode())
        {
            setBlockIndex(value);
        }
        else
        {
            setVerticalOffset(-value);
        }
    }
}

void PDFDrawWidgetProxy::setHorizontalOffset(int value)
{
    const PDFInteger horizontalOffset = m_horizontalOffsetRange.bound(value);

    if (m_horizontalOffset != horizontalOffset)
    {
        m_horizontalOffset = horizontalOffset;
        updateHorizontalScrollbarFromOffset();
        Q_EMIT drawSpaceChanged();
    }
}

void PDFDrawWidgetProxy::setVerticalOffset(int value)
{
    const PDFInteger verticalOffset = m_verticalOffsetRange.bound(value);

    if (m_verticalOffset != verticalOffset)
    {
        m_verticalOffset = verticalOffset;
        updateVerticalScrollbarFromOffset();
        Q_EMIT drawSpaceChanged();
    }
}

void PDFDrawWidgetProxy::setBlockIndex(int index)
{
    if (m_currentBlock != index)
    {
        m_currentBlock = static_cast<size_t>(index);
        update();
    }
}

void PDFDrawWidgetProxy::updateHorizontalScrollbarFromOffset()
{
    if (!m_horizontalScrollbar->isHidden())
    {
        PDFBoolGuard guard(m_updateDisabled);
        m_horizontalScrollbar->setValue(-m_horizontalOffset);
    }
}

void PDFDrawWidgetProxy::updateVerticalScrollbarFromOffset()
{
    if (!m_verticalScrollbar->isHidden() && !isBlockMode())
    {
        PDFBoolGuard guard(m_updateDisabled);
        m_verticalScrollbar->setValue(-m_verticalOffset);
    }
}

PDFDrawWidgetProxy::GroupInfo PDFDrawWidgetProxy::getGroupInfo(int groupIndex) const
{
    auto it = m_groupInfos.find(groupIndex);
    if (it != m_groupInfos.cend())
    {
        return it->second;
    }

    return GroupInfo();
}

PDFWidgetAnnotationManager* PDFDrawWidgetProxy::getAnnotationManager() const
{
    return m_widget->getAnnotationManager();
}

PDFRenderer::Features PDFDrawWidgetProxy::getFeatures() const
{
    return m_features;
}

const PDFCMSManager* PDFDrawWidgetProxy::getCMSManager() const
{
    return m_widget->getCMSManager();
}

void PDFDrawWidgetProxy::setFeatures(PDFRenderer::Features features)
{
    if (m_features != features)
    {
        m_compiler->stop(true);
        m_textLayoutCompiler->stop(true);
        m_features = features;
        m_compiler->start();
        m_textLayoutCompiler->start();
        Q_EMIT pageImageChanged(true, { });
    }
}

void PDFDrawWidgetProxy::setPreferredMeshResolutionRatio(PDFReal ratio)
{
    if (m_meshQualitySettings.preferredMeshResolutionRatio != ratio)
    {
        m_compiler->stop(true);
        m_meshQualitySettings.preferredMeshResolutionRatio = ratio;
        m_compiler->start();
        Q_EMIT pageImageChanged(true, { });
    }
}

void PDFDrawWidgetProxy::setMinimalMeshResolutionRatio(PDFReal ratio)
{
    if (m_meshQualitySettings.minimalMeshResolutionRatio != ratio)
    {
        m_compiler->stop(true);
        m_meshQualitySettings.minimalMeshResolutionRatio = ratio;
        m_compiler->start();
        Q_EMIT pageImageChanged(true, { });
    }
}

void PDFDrawWidgetProxy::setColorTolerance(PDFReal colorTolerance)
{
    if (m_meshQualitySettings.tolerance != colorTolerance)
    {
        m_compiler->stop(true);
        m_meshQualitySettings.tolerance = colorTolerance;
        m_compiler->start();
        Q_EMIT pageImageChanged(true, { });
    }
}

void PDFDrawWidgetProxy::onColorManagementSystemChanged()
{
    m_compiler->reset();
    Q_EMIT pageImageChanged(true, { });
}

void PDFDrawWidgetProxy::onOptionalContentGroupStateChanged()
{
    m_compiler->reset();
    m_textLayoutCompiler->reset();
    Q_EMIT pageImageChanged(true, { });
}

void IDocumentDrawInterface::drawPage(QPainter* painter,
                                      PDFInteger pageIndex,
                                      const PDFPrecompiledPage* compiledPage,
                                      PDFTextLayoutGetter& layoutGetter,
                                      const QTransform& pagePointToDevicePointMatrix,
                                      const pdf::PDFColorConvertor& convertor,
                                      QList<PDFRenderError>& errors) const
{
    Q_UNUSED(painter);
    Q_UNUSED(pageIndex);
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(pagePointToDevicePointMatrix);
    Q_UNUSED(convertor);
    Q_UNUSED(errors);
}

void IDocumentDrawInterface::drawPostRendering(QPainter* painter, QRect rect) const
{
    Q_UNUSED(painter);
    Q_UNUSED(rect);
}

bool IDocumentDrawInterface::isPageContentDrawSuppressed() const
{
    return false;
}

}   // namespace pdf
