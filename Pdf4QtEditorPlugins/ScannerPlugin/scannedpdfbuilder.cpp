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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "scannedpdfbuilder.h"

#include "pdfdocumentbuilder.h"

#include <QPainter>

namespace pdfplugin
{

void ScannedPdfBuilder::appendPages(pdf::PDFDocumentBuilder* builder, const std::vector<ScannedPage>& pages)
{
    constexpr pdf::PDFReal pointsPerInch = 72.0;

    pdf::PDFPageContentStreamBuilder contentBuilder(builder);
    for (const ScannedPage& page : pages)
    {
        if (page.image.isNull())
        {
            continue;
        }

        const int dpiX = page.dpiX > 0 ? page.dpiX : 300;
        const int dpiY = page.dpiY > 0 ? page.dpiY : dpiX;
        const pdf::PDFReal width = page.image.width() * pointsPerInch / dpiX;
        const pdf::PDFReal height = page.image.height() * pointsPerInch / dpiY;
        const QRectF mediaBox(0.0, 0.0, width, height);

        QPainter* painter = contentBuilder.beginNewPage(mediaBox);
        if (!painter)
        {
            continue;
        }

        painter->drawImage(mediaBox, page.image);
        contentBuilder.end(painter);
    }
}

}   // namespace pdfplugin
