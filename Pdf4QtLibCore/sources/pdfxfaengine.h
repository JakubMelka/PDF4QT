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

#ifndef PDFXFAENGINE_H
#define PDFXFAENGINE_H

#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfexception.h"

#include <memory>

namespace pdf
{

class PDFForm;
class PDFXFAEngineImpl;

class PDFXFAEngine
{
public:
    PDFXFAEngine();
    ~PDFXFAEngine();

    void setDocument(const PDFModifiedDocument& document, PDFForm* form);

    /// Returns list of page sizes (after paging is complete)
    std::vector<QSizeF> getPageSizes() const;

    /// Largest number of pages of the layout (also of a hostile template)
    static constexpr size_t MAXIMUM_PAGE_COUNT = 128;

    /// Lays out the XFA form \p form of \p document and returns the sizes of the
    /// pages of its layout (the same as getPageSizes after setDocument). Empty,
    /// when the form is not an XFA form or its template cannot be read. Only the
    /// template is read: no font of the document is registered in the font
    /// database, so the function may be called from a worker thread.
    /// \param document Document
    /// \param form Form of the document
    static std::vector<QSizeF> getLayoutPageSizes(const PDFDocument* document, const PDFForm* form);

    /// Draws XFA form
    /// \param pagePointToDevicePointMatrix Page point to device point matrix
    /// \param page Page
    /// \param errors Error list (for reporting rendering errors)
    /// \param painter Painter
    void draw(const QTransform& pagePointToDevicePointMatrix,
              const PDFPage* page,
              QList<PDFRenderError>& errors,
              QPainter* painter);

private:
    std::unique_ptr<PDFXFAEngineImpl> m_impl;
};

/* START GENERATED CODE */

namespace xfa
{
} // namespace xfa


/* END GENERATED CODE */

}   // namespace pdf

#endif // PDFXFAENGINE_H
