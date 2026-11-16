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

#ifndef PDFREDACT_H
#define PDFREDACT_H

#include "pdfdocument.h"
#include "pdfrenderer.h"

#include <QCoreApplication>

#include <optional>
#include <vector>

namespace pdf
{

class PDFOperationControl;
class PDFProgressSink;

/// Creates a redacted document from the document (plan of step 6 of phase 2, 5.4 with B138 -
/// B154, R6-5 ... R6-7, R6-17, R6-18, R6-30; owner answers Q6.8, Q6.11): every page is rebuilt - it
/// is compiled, the redacted areas and the content outside of its crop box are removed from it
/// (PDFPrecompiledPage::redact: paths, strokes, clips, images by their pixels, meshes), it is drawn
/// into a new content stream (text becomes curves) and the fill of the areas is its own last content
/// stream (R6-5: the check renders the page without it). The new page is the visible part of the old
/// one (the media box is the crop box, Q6.11) with its rotation, /UserUnit (B148) and the other
/// boxes within it. Nothing else of the document is carried over (annotations, forms, the structure,
/// the hidden data); only /Info of direct values of the standard keys (R6-6) and the outline, when
/// the options say so. The document is optimized (no unused object, one revision when written).
///
/// The pages are processed in parallel (PDFExecutionPolicy, the cancellation by PDFOperationControl,
/// a step of the progress per page, B144); the document is assembled in the calling thread. The
/// caches and the color management given to the constructor are only read.
class PDF4QTLIBCORESHARED_EXPORT PDFRedact
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFRedact)

public:
    /// \param document The document (must live during perform)
    /// \param fontCache The font cache of the document
    /// \param iccProfileCache The ICC profile cache of the document
    /// \param cms The color management
    /// \param optionalContentActivity The states of the layers (a layer OFF is not carried over)
    /// \param meshQualitySettings The quality of the meshes of the shadings
    /// \param redactFillColor The fill of the areas of the old interface perform(Options); invalid = no fill
    explicit PDFRedact(const PDFDocument* document,
                       const PDFFontCache* fontCache,
                       const PDFICCProfileCache* iccProfileCache,
                       const PDFCMS* cms,
                       const PDFOptionalContentActivity* optionalContentActivity,
                       const PDFMeshQualitySettings* meshQualitySettings,
                       QColor redactFillColor);

    enum Option
    {
        None            = 0x0000,
        CopyTitle       = 0x0001,
        CopyMetadata    = 0x0002,
        CopyOutline     = 0x0004
    };
    Q_DECLARE_FLAGS(Options, Option)

    /// Limits of the marks (plan 7.2)
    static constexpr int MAXIMUM_MARK_COUNT = 10000;            ///< Redact annotations of a document
    static constexpr int MAXIMUM_QUADRILATERAL_COUNT = 1000;    ///< Quadrilaterals of one annotation

    /// A redacted area of a page of the document, in the default user space of the page (a mark,
    /// an area of MCP, a match of a search, a whole page)
    struct Area
    {
        PDFInteger pageIndex = -1;  ///< The page (index from 0)
        QPainterPath path;          ///< The area; every subpath is a part of it (they are united, R6-7)
    };

    /// What perform does
    struct Settings
    {
        std::vector<Area> areas;            ///< The redacted areas (a page index outside of the document is ignored)
        std::vector<PDFInteger> wholePages; ///< Pages redacted whole: a blank page of the same size (R6-30)
        std::optional<QColor> fillColor;    ///< The fill of the areas; std::nullopt = No fill (Table 195: the area stays transparent)
        Options options = None;             ///< What is kept of the document
    };

    /// A page, whose content could not be drawn whole (B143) or which has no valid size (R6-17)
    struct PageProblem
    {
        PDFInteger pageIndex = -1;  ///< The page (index from 0)
        QString message;            ///< Translated text, never a redacted text
    };

    /// The result of perform
    struct Result
    {
        PDFDocument document;                       ///< The redacted document
        std::vector<Area> areas;                    ///< The redacted region of each page with any (the union of its areas within the new media box, a region of the non-zero rule, which may have holes; a whole page is its box): what the fill covers and the check verifies
        std::vector<PageProblem> pageProblems;      ///< The problems of the pages (B143, R6-17)
        bool isStopped = false;                     ///< The run was canceled: nothing is to be used
    };

    /// Redacts the document (see the class)
    /// \param settings The areas, the fill and the options
    /// \param control Cancellation (may be null)
    /// \param progress Progress, a step per page (may be null)
    Result perform(const Settings& settings, const PDFOperationControl* control, PDFProgressSink* progress) const;

    /// The redacted document of the Redact annotations of the document with the fill of the
    /// constructor (the old interface of the Editor and of PdfTool)
    pdf::PDFDocument perform(Options options);

    /// The areas of the Redact annotations (the marks, ISO 32000-2, 12.5.6.23) of the document: the
    /// quadrilaterals of /QuadPoints (their convex hulls, B146), else /Rect
    /// \param document The document
    /// \param errorMessage Translated reason, when a limit is exceeded (MAXIMUM_MARK_COUNT, MAXIMUM_QUADRILATERAL_COUNT)
    /// \param markCount Number of the marks (optional)
    /// \returns the areas, empty with an error message for a limit
    static std::vector<Area> getMarkAreas(const PDFDocument* document, QString* errorMessage, int* markCount = nullptr);

    /// The box of the new page: the crop box within the media box (Q6.11); an invalid rectangle
    /// for a page without a valid media box (R6-17)
    static QRectF getVisibleBox(const PDFPage* page);

private:
    const PDFDocument* m_document;
    const PDFFontCache* m_fontCache;
    const PDFICCProfileCache* m_iccProfileCache;
    const PDFCMS* m_cms;
    const PDFOptionalContentActivity* m_optionalContentActivity;
    const PDFMeshQualitySettings* m_meshQualitySettings;
    QColor m_redactFillColor;
};

}   // namespace pdf

#endif // PDFREDACT_H
