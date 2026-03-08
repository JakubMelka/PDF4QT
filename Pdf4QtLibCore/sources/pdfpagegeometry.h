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

#ifndef PDFPAGEGEOMETRY_H
#define PDFPAGEGEOMETRY_H

#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfutils.h"

#include <QMarginsF>
#include <QSizeF>
#include <QString>
#include <QPointF>

namespace pdf
{

struct PDF4QTLIBCORESHARED_EXPORT PDFPageGeometrySettings
{
    enum class PageSubset
    {
        AllPages,
        OddPages,
        EvenPages,
        PortraitPages,
        LandscapePages
    };

    enum class ReferenceBox
    {
        MediaBox,
        CropBox,
        BleedBox,
        TrimBox,
        ArtBox
    };

    enum class Anchor
    {
        TopLeft,
        TopCenter,
        TopRight,
        MiddleLeft,
        MiddleCenter,
        MiddleRight,
        BottomLeft,
        BottomCenter,
        BottomRight
    };

    QString pageRange = "-";
    PageSubset pageSubset = PageSubset::AllPages;
    ReferenceBox referenceBox = ReferenceBox::CropBox;

    bool applyMediaBox = true;
    bool applyCropBox = true;
    bool applyBleedBox = false;
    bool applyTrimBox = false;
    bool applyArtBox = false;

    bool useTargetPageSize = false;
    QSizeF targetPageSizeMM = QSizeF(210.0, 297.0);
    QMarginsF marginsMM = QMarginsF(0.0, 0.0, 0.0, 0.0); // left, top, right, bottom

    Anchor anchor = Anchor::MiddleCenter;
    QPointF offsetMM = QPointF(0.0, 0.0);

    bool scaleContent = false;
    bool preserveAspectRatio = true;
    bool scaleAnnotationsAndFormFields = true;

    bool hasAnyTargetBoxSelected() const
    {
        return applyMediaBox || applyCropBox || applyBleedBox || applyTrimBox || applyArtBox;
    }
};

class PDF4QTLIBCORESHARED_EXPORT PDFPageGeometry
{
public:
    static PDFOperationResult apply(PDFDocument* document,
                                    const PDFPageGeometrySettings& settings,
                                    PDFModifiedDocument::ModificationFlags* modificationFlags = nullptr);
};

}   // namespace pdf

#endif // PDFPAGEGEOMETRY_H
