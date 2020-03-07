//    Copyright (C) 2020 Jakub Melka
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

#ifndef PDFDOCUMENTDRAWINTERFACE_H
#define PDFDOCUMENTDRAWINTERFACE_H

#include "pdfglobal.h"

class QPainter;

namespace pdf
{
class PDFPrecompiledPage;
class PDFTextLayoutGetter;

class PDFFORQTLIBSHARED_EXPORT IDocumentDrawInterface
{
public:
    explicit inline IDocumentDrawInterface() = default;
    virtual ~IDocumentDrawInterface() = default;

    /// Performs drawing of additional graphics onto the painter using precompiled page,
    /// optionally text layout and page point to device point matrix.
    /// \param painter Painter
    /// \param pageIndex Page index
    /// \param compiledPage Compiled page
    /// \param layoutGetter Layout getter
    /// \param pagePointToDevicePointMatrix Matrix mapping page space to device point space
    virtual void drawPage(QPainter* painter,
                          pdf::PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix) const;

    /// Performs drawing of additional graphics after all pages are drawn onto the painter.
    /// \param painter Painter
    /// \param rect Draw rectangle (usually viewport rectangle of the pdf widget)
    virtual void drawPostRendering(QPainter* painter, QRect rect) const;
};

}   // namespace pdf

#endif // PDFDOCUMENTDRAWINTERFACE_H
