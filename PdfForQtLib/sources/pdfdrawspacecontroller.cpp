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

namespace pdf
{

PDFDrawSpaceController::PDFDrawSpaceController(QObject* parent) :
    QObject(parent),
    m_document(nullptr),
    m_pageLayoutMode(PageLayout::SinglePage),
    m_verticalSpacingMM(5.0),
    m_horizontalSpacingMM(1.0)
{

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

}   // namespace pdf
