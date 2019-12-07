//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.


#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfrenderer.h"

#include <QPainter>

namespace pdf
{

PDFDrawSpaceController::PDFDrawSpaceController(QObject* parent) :
    QObject(parent),
    m_document(nullptr),
    m_optionalContentActivity(nullptr),
    m_pageLayoutMode(PageLayout::OneColumn),
    m_verticalSpacingMM(5.0),
    m_horizontalSpacingMM(1.0),
    m_fontCache(FONT_CACHE_LIMIT, REALIZED_FONT_CACHE_LIMIT)
{

}

PDFDrawSpaceController::~PDFDrawSpaceController()
{

}

void PDFDrawSpaceController::setDocument(const PDFDocument* document, const PDFOptionalContentActivity* optionalContentActivity)
{
    if (document != m_document)
    {
        m_document = document;
        m_fontCache.setDocument(document);
        m_optionalContentActivity = optionalContentActivity;
        connect(m_optionalContentActivity, &PDFOptionalContentActivity::optionalContentGroupStateChanged, this, &PDFDrawSpaceController::onOptionalContentGroupStateChanged);
        recalculate();
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

void PDFDrawSpaceController::onOptionalContentGroupStateChanged()
{
    emit pageImageChanged(true, { });
    emit repaintNeeded();
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

    // First, preserve page rotations. We assume the count of pages is the same as the document.
    // Document should not be changed while viewing. If a new document is setted, then the draw
    // space is cleared first.
    std::vector<PageRotation> pageRotation(pageCount, PageRotation::None);
    for (size_t i = 0; i < pageCount; ++i)
    {
        pageRotation[i] = catalog->getPage(i)->getPageRotation();
    }
    for (const LayoutItem& layoutItem : m_layoutItems)
    {
        pageRotation[layoutItem.pageIndex] = layoutItem.pageRotation;
    }

    // Clear the old draw space
    clear(false);

    static constexpr size_t INVALID_PAGE_INDEX = std::numeric_limits<size_t>::max();

    // Places the pages on the left/right sides. Pages can be nullptr, but not both of them.
    // Updates bounding rectangle.
    auto placePagesLeftRight = [this, catalog, &pageRotation](PDFInteger blockIndex, size_t leftIndex, size_t rightIndex, PDFReal& yPos, QRectF& boundingRect)
    {
        PDFReal yPosAdvance = 0.0;

        if (leftIndex != INVALID_PAGE_INDEX)
        {
            QSizeF pageSize = PDFPage::getRotatedBox(catalog->getPage(leftIndex)->getMediaBoxMM(), pageRotation[leftIndex]).size();
            PDFReal xPos = -pageSize.width() - m_horizontalSpacingMM * 0.5;
            QRectF rect(xPos, yPos, pageSize.width(), pageSize.height());
            m_layoutItems.emplace_back(blockIndex, leftIndex, pageRotation[leftIndex], rect);
            yPosAdvance = qMax(yPosAdvance, pageSize.height());
            boundingRect = boundingRect.united(rect);
        }

        if (rightIndex != INVALID_PAGE_INDEX)
        {
            QSizeF pageSize = PDFPage::getRotatedBox(catalog->getPage(rightIndex)->getMediaBoxMM(), pageRotation[rightIndex]).size();
            PDFReal xPos = m_horizontalSpacingMM * 0.5;
            QRectF rect(xPos, yPos, pageSize.width(), pageSize.height());
            m_layoutItems.emplace_back(blockIndex, rightIndex, pageRotation[rightIndex], rect);
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
                QSizeF pageSize = PDFPage::getRotatedBox(catalog->getPage(i)->getMediaBoxMM(), pageRotation[i]).size();
                QRectF rect(-pageSize.width() * 0.5, -pageSize.height() * 0.5, pageSize.width(), pageSize.height());
                m_layoutItems.emplace_back(i, i, pageRotation[i], rect);
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
                QSizeF pageSize = PDFPage::getRotatedBox(catalog->getPage(i)->getMediaBoxMM(), pageRotation[i]).size();
                QRectF rect(-pageSize.width() * 0.5, yPos, pageSize.width(), pageSize.height());
                m_layoutItems.emplace_back(0, i, pageRotation[i], rect);
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

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    emit drawSpaceChanged();
}

void PDFDrawSpaceController::clear(bool emitSignal)
{
    m_layoutItems.clear();
    m_blockItems.clear();

    if (emitSignal)
    {
        emit drawSpaceChanged();
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
    m_horizontalScrollbar(nullptr),
    m_verticalScrollbar(nullptr),
    m_features(PDFRenderer::getDefaultFeatures())
{
    m_controller = new PDFDrawSpaceController(this);
    connect(m_controller, &PDFDrawSpaceController::drawSpaceChanged, this, &PDFDrawWidgetProxy::update);
    connect(m_controller, &PDFDrawSpaceController::repaintNeeded, this, &PDFDrawWidgetProxy::repaintNeeded);
    connect(m_controller, &PDFDrawSpaceController::pageImageChanged, this, &PDFDrawWidgetProxy::pageImageChanged);
}

PDFDrawWidgetProxy::~PDFDrawWidgetProxy()
{

}

void PDFDrawWidgetProxy::setDocument(const PDFDocument* document, const PDFOptionalContentActivity* optionalContentActivity)
{
    m_controller->setDocument(document, optionalContentActivity);
}

void PDFDrawWidgetProxy::init(PDFWidget* widget)
{
    m_widget = widget;
    m_horizontalScrollbar = widget->getHorizontalScrollbar();
    m_verticalScrollbar = widget->getVerticalScrollbar();

    connect(m_horizontalScrollbar, &QScrollBar::valueChanged, this, &PDFDrawWidgetProxy::onHorizontalScrollbarValueChanged);
    connect(m_verticalScrollbar, &QScrollBar::valueChanged, this, &PDFDrawWidgetProxy::onVerticalScrollbarValueChanged);
    connect(this, &PDFDrawWidgetProxy::drawSpaceChanged, this, &PDFDrawWidgetProxy::repaintNeeded);

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

    // First, we must calculate pixel per mm ratio to obtain DPMM (device pixel per mm),
    // we also assume, that zoom is correctly set.
    m_pixelPerMM = static_cast<PDFReal>(widget->width()) / static_cast<PDFReal>(widget->widthMM());

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
            m_layout.items.emplace_back(item.pageIndex, item.pageRotation, fromDeviceSpace(item.pageRectMM).toRect());
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

    emit drawSpaceChanged();
}

void PDFDrawWidgetProxy::draw(QPainter* painter, QRect rect)
{
    painter->fillRect(rect, Qt::lightGray);

    // Iterate trough pages and display them on the painter device
    for (const LayoutItem& item : m_layout.items)
    {
        // The offsets m_horizontalOffset and m_verticalOffset are offsets to the
        // topleft point of the block. But block maybe doesn't start at (0, 0),
        // so we must also use translation from the block beginning.
        QRect placedRect = item.pageRect.translated(m_horizontalOffset - m_layout.blockRect.left(), m_verticalOffset - m_layout.blockRect.top());
        if (placedRect.intersects(rect))
        {
            // Clear the page space by white color
            painter->fillRect(placedRect, Qt::white);

            PDFRenderer renderer(m_controller->getDocument(), m_controller->getFontCache(), m_controller->getOptionalContentActivity(), m_features, m_meshQualitySettings);
            QList<PDFRenderError> errors = renderer.render(painter, placedRect, item.pageIndex);

            if (!errors.empty())
            {
                emit renderingError(item.pageIndex, errors);
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
            image = QImage(imageSize, QImage::Format_RGBA8888_Premultiplied);
            image.fill(Qt::white);

            QPainter painter(&image);
            PDFRenderer renderer(m_controller->getDocument(), m_controller->getFontCache(), m_controller->getOptionalContentActivity(), m_features, m_meshQualitySettings);
            renderer.render(&painter, QRect(QPoint(0, 0), imageSize), pageIndex);
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

    return pages;
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

void PDFDrawWidgetProxy::zoom(PDFReal zoom)
{
    const PDFReal clampedZoom = qBound(MIN_ZOOM, zoom, MAX_ZOOM);
    if (m_zoom != clampedZoom)
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

void PDFDrawWidgetProxy::setPageLayout(PageLayout pageLayout)
{
    if (getPageLayout() != pageLayout)
    {
        m_controller->setPageLayout(pageLayout);
        emit pageLayoutChanged();
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
    }

    Q_ASSERT(false);
    return false;
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
        emit drawSpaceChanged();
    }
}

void PDFDrawWidgetProxy::setVerticalOffset(int value)
{
    const PDFInteger verticalOffset = m_verticalOffsetRange.bound(value);

    if (m_verticalOffset != verticalOffset)
    {
        m_verticalOffset = verticalOffset;
        updateVerticalScrollbarFromOffset();
        emit drawSpaceChanged();
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

PDFRenderer::Features PDFDrawWidgetProxy::getFeatures() const
{
    return m_features;
}

void PDFDrawWidgetProxy::setFeatures(PDFRenderer::Features features)
{
    if (m_features != features)
    {
        m_features = features;
        emit repaintNeeded();
    }
}

void PDFDrawWidgetProxy::setPreferredMeshResolutionRatio(PDFReal ratio)
{
    if (m_meshQualitySettings.preferredMeshResolutionRatio != ratio)
    {
        m_meshQualitySettings.preferredMeshResolutionRatio = ratio;
        emit repaintNeeded();
    }
}

void PDFDrawWidgetProxy::setMinimalMeshResolutionRatio(PDFReal ratio)
{
    if (m_meshQualitySettings.minimalMeshResolutionRatio != ratio)
    {
        m_meshQualitySettings.minimalMeshResolutionRatio = ratio;
        emit repaintNeeded();
    }
}

void PDFDrawWidgetProxy::setColorTolerance(PDFReal colorTolerance)
{
    if (m_meshQualitySettings.tolerance != colorTolerance)
    {
        m_meshQualitySettings.tolerance = colorTolerance;
        emit repaintNeeded();
    }
}

}   // namespace pdf
