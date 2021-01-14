//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftransparencyrenderer.h"
#include "pdfdocument.h"

namespace pdf
{

PDFFloatBitmap::PDFFloatBitmap() :
    m_width(0),
    m_height(0),
    m_pixelSize(0)
{

}

PDFFloatBitmap::PDFFloatBitmap(size_t width, size_t height, PDFPixelFormat format) :
    m_format(format),
    m_width(width),
    m_height(height),
    m_pixelSize(format.getChannelCount())
{
    Q_ASSERT(format.isValid());

    m_data.resize(format.calculateBitmapDataLength(width, height), static_cast<PDFColorComponent>(0.0));
}

PDFColorBuffer PDFFloatBitmap::getPixel(size_t x, size_t y)
{
    const size_t index = getPixelIndex(x, y);
    return PDFColorBuffer(m_data.data() + index, m_pixelSize);
}

const PDFColorComponent* PDFFloatBitmap::begin() const
{
    return m_data.data();
}

const PDFColorComponent* PDFFloatBitmap::end() const
{
    return m_data.data() + m_data.size();
}

PDFColorComponent* PDFFloatBitmap::begin()
{
    return m_data.data();
}

PDFColorComponent* PDFFloatBitmap::end()
{
    return m_data.data() + m_data.size();
}

size_t PDFFloatBitmap::getPixelIndex(size_t x, size_t y) const
{
    return (y * m_width + x) * m_pixelSize;
}

PDFTransparencyRenderer::PDFTransparencyRenderer(const PDFPage* page,
                                                 const PDFDocument* document,
                                                 const PDFFontCache* fontCache,
                                                 const PDFCMS* cms,
                                                 const PDFOptionalContentActivity* optionalContentActivity,
                                                 QMatrix pagePointToDevicePointMatrix) :
    BaseClass(page, document, fontCache, cms, optionalContentActivity, pagePointToDevicePointMatrix, PDFMeshQualitySettings()),
    m_active(false)
{

}

void PDFTransparencyRenderer::setDeviceColorSpace(PDFColorSpacePointer colorSpace)
{
    if (!colorSpace || colorSpace->isBlendColorSpace())
    {
        // Set device color space only, when it is a blend color space
        m_deviceColorSpace = colorSpace;
    }
}

void PDFTransparencyRenderer::beginPaint()
{
    Q_ASSERT(!m_active);
    m_active = true;

    // Create page transparency group
    PDFObject pageTransparencyGroupObject = getPage()->getTransparencyGroup(&getDocument()->getStorage());
    PDFTransparencyGroup transparencyGroup = parseTransparencyGroup(pageTransparencyGroupObject);
    transparencyGroup.isolated = true;

    m_pageTransparencyGroupGuard.reset(new PDFTransparencyGroupGuard(this, qMove(transparencyGroup)));
}

const PDFFloatBitmap& PDFTransparencyRenderer::endPaint()
{
    Q_ASSERT(m_active);
    m_pageTransparencyGroupGuard.reset();
    m_active = false;
}

void PDFTransparencyRenderer::performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule)
{
}

void PDFTransparencyRenderer::performClipping(const QPainterPath& path, Qt::FillRule fillRule)
{
}

void PDFTransparencyRenderer::performUpdateGraphicsState(const PDFPageContentProcessorState& state)
{
}

void PDFTransparencyRenderer::performSaveGraphicState(ProcessOrder order)
{
}

void PDFTransparencyRenderer::performRestoreGraphicState(ProcessOrder order)
{
}

void PDFTransparencyRenderer::performBeginTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup)
{
}

void PDFTransparencyRenderer::performEndTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup)
{
}

}   // namespace pdf
