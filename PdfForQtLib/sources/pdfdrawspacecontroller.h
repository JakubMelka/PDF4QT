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

#ifndef PDFDRAWSPACECONTROLLER_H
#define PDFDRAWSPACECONTROLLER_H

#include "pdfdocument.h"

#include <QRectF>
#include <QObject>
#include <QMarginsF>

namespace pdf
{

/// This class controls draw space - page layout. Pages are divided into blocks
/// each block can contain one or multiple pages. Units are in milimeters.
/// Pages are layouted in zoom-independent mode.
class PDFDrawSpaceController : public QObject
{
    Q_OBJECT

public:
    explicit PDFDrawSpaceController(QObject* parent);

signals:
    void drawSpaceChanged();

private:
    /// Recalculates the draw space. Preserves setted page rotation.
    void recalculate();

    /// Clears the draw space. Emits signal if desired.
    void clear(bool emitSignal);

    /// Represents layouted page. This structure contains index of the block, index of the
    /// page and page rectangle, in which the page is contained.
    struct LayoutItem
    {
        constexpr inline explicit LayoutItem() : blockIndex(-1), pageIndex(-1), pageRotation(PageRotation::None) { }
        constexpr inline explicit LayoutItem(PDFInteger blockIndex, PDFInteger pageIndex, PageRotation rotation, const QRectF& pageRectMM) :
            blockIndex(blockIndex), pageIndex(pageIndex), pageRotation(rotation), pageRectMM(pageRectMM) { }

        PDFInteger blockIndex;
        PDFInteger pageIndex;
        PageRotation pageRotation;
        QRectF pageRectMM;
    };

    using LayoutItems = std::vector<LayoutItem>;

    /// Represents data for the single block. Contains block size in milimeters.
    struct LayoutBlock
    {
        constexpr inline explicit LayoutBlock() = default;
        constexpr inline explicit LayoutBlock(const QRectF& blockRectMM) : blockRectMM(blockRectMM) { }

        QRectF blockRectMM;
    };

    using BlockItems = std::vector<LayoutBlock>;

    const PDFDocument* m_document;

    PageLayout m_pageLayoutMode;
    LayoutItems m_layoutItems;
    BlockItems m_blockItems;
    PDFReal m_verticalSpacingMM;
    PDFReal m_horizontalSpacingMM;
};

}   // namespace pdf

#endif // PDFDRAWSPACECONTROLLER_H
