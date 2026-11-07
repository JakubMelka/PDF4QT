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

#ifndef PDFAPPPAGERENDERCONTEXT_H
#define PDFAPPPAGERENDERCONTEXT_H

#include "pdfappglobal.h"

#include "pdfannotation.h"
#include "pdfcms.h"
#include "pdfcolorspaces.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"
#include "pdfrenderer.h"

#include <QTransform>

#include <map>

class QPainter;

namespace pdf
{

class PDFOperationControl;

/// The caches and the managers of a rendering of pages of one document in a worker (the
/// pages of an assistant, the result of a tool): PDFRenderer with its own font, ICC profile
/// and optional content data, nothing of a view. Used by one thread at a time; a next
/// thread may go on with it, when the hand-off between them synchronizes. The document
/// must outlive the context and it is only read.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPageRenderContext
{
public:
    /// The features of a rendering, which has no view
    static PDFRenderer::Features getDefaultFeatures();

    /// \param document Document
    /// \param cmsSettings Color management of the rendering (a value, taken in the thread
    ///        of the user interface; never a pointer to an object of a view)
    /// \param features Features of the rendering
    explicit PDFAppPageRenderContext(const PDFDocument* document, const PDFCMSSettings& cmsSettings = PDFCMSSettings(), PDFRenderer::Features features = getDefaultFeatures());

    /// The states of the layers (the optional content groups), as a view has them (values
    /// taken in the thread of the user interface); a group not given keeps its default state
    void setOptionalContentStates(const std::map<PDFObjectReference, OCState>& states);

    /// Draws the page: its content and its annotations
    /// \param painter Painter
    /// \param pageIndex Index of an existing page
    /// \param pageToImage Matrix of the page space to the space of the painter
    /// \param showAnnotations The annotations are drawn
    /// \param control Cancellation (optional), a canceled page is not drawn
    void draw(QPainter* painter, PDFInteger pageIndex, const QTransform& pageToImage, bool showAnnotations, const PDFOperationControl* control);

private:
    const PDFDocument* m_document;
    PDFRenderer::Features m_features;
    PDFFontCache m_fontCache;
    PDFICCProfileCache m_iccProfileCache;
    PDFOptionalContentActivity m_optionalContentActivity;
    PDFCMSManager m_cmsManager;
    PDFAnnotationManager m_annotationManager;
};

}   // namespace pdf

#endif // PDFAPPPAGERENDERCONTEXT_H
