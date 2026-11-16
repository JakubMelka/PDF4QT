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

#ifndef PDFREDACTIONVERIFIER_H
#define PDFREDACTIONVERIFIER_H

#include "pdfglobal.h"
#include "pdfdocument.h"

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QPainterPath>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace pdf
{

class PDFOperationControl;
class PDFProgressSink;

/// The check of a redacted copy after it was written (plan of step 6 of phase 2, 6.8 as changed by
/// R6-5, R6-6, R6-14, R6-16; owner answer Q6.9: a finding means the copy is not created; package P3
/// implements it). It reads the written bytes back and checks what the promise "permanently removed,
/// nothing hidden survives" (437:13887) needs; the pages are checked in parallel (PDFExecutionPolicy,
/// progress by PDFProgressSink, shared state under a mutex).
///
///  1. Structure: the bytes read back are one revision; the content streams have no text operator
///     (Tj, TJ, ', "); the catalog has no /AcroForm, /Names, /OpenAction, /AA, /StructTreeRoot,
///     /OCProperties, /Metadata, /PieceInfo; the pages no /Annots, /Thumb, /PieceInfo, /AA; only the
///     allowed kinds of objects (the pages, their resources, the outline and /Info of direct values of
///     the standard keys - R6-6, R6-14); PDFDocumentHiddenDataScan finds nothing beyond the kept kinds
///     of Options (R6-14).
///  2. Pixels (R6-5): the fill of every area is the own last content stream of its page; the page is
///     rendered without that stream (programmatically, 150 DPI, at most 4 MPx a page) and the inside of
///     each area shrunk by 1 px must be paper or exactly the fill color (No fill: paper only, Table 195).
///     Besides, every image (XObject, inline, mask) reaching into an area is decoded and its pixels in
///     the area are the fill or transparent; and the whole page with its fill shows exactly the fill
///     color inside of each area (the fill is where the areas are).
///  3. Words (R6-14): the words of at least 4 characters of the areas of the source (every character
///     of PDFTextLayout, whose box meets an area - invisible and covered text too) are looked for in the
///     strings of the output (PDFDocEncoding and UTF-16BE): in the content = an error; in the kept
///     title, metadata or outline = a warning (owner answer Q6.10).
///  4. Soft masks (R6-16, owner answer Q6.19 a): a page, whose content hid something by an /SMask of
///     an ExtGState, which the rebuilt page cannot keep, gives a warning with its page numbers.
///
/// Reentrant: only its arguments; the texts of the findings never contain the redacted text (only
/// page numbers and counts, S6.2).
class PDF4QTLIBCORESHARED_EXPORT PDFRedactionVerifier
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFRedactionVerifier)

public:
    /// Largest rendered area of a page of the check of the pixels (plan 7.2)
    static constexpr qint64 MAXIMUM_PIXEL_COUNT = 4 * 1024 * 1024;

    /// Resolution of the check of the pixels
    static constexpr double CHECK_DPI = 150.0;

    /// Tolerance of a channel of a filled pixel (6.8: only with lossless images, B154)
    static constexpr int COLOR_TOLERANCE = 2;

    /// The redacted area of one page of the source (a mark, an area of MCP, a result of the search, a
    /// whole page), in the default user space of the page
    struct Area
    {
        PDFInteger pageIndex = -1;  ///< The page (index from 0)
        QPainterPath path;          ///< The area: a region of the non-zero rule (PDFRedact::Result::areas, the union of the quadrilaterals - R6-7); the areas of a page are united
    };

    /// What the redaction has promised
    struct Options
    {
        std::optional<QColor> fillColor;    ///< The fill of the areas, std::nullopt = No fill (the area stays paper)
        bool isTitleKept = false;           ///< The title is kept in /Info (a warning, not an error, for a redacted word in it)
        bool isMetadataKept = false;        ///< The metadata are kept (/Info of direct values only, R6-6)
        bool isOutlineKept = false;         ///< The outline is kept
    };

    /// One finding of the check
    struct Finding
    {
        PDFInteger pageIndex = -1;  ///< The page of the output (index from 0), -1 = the document
        QString message;            ///< Translated text ("Content under 2 areas on page 12 could not be removed."), never the redacted text
    };

    /// The result of verify
    struct Result
    {
        std::vector<Finding> errors;    ///< Something redacted survives: the copy is not created (Q6.9)
        std::vector<Finding> warnings;  ///< What the user has chosen to keep holds a redacted word (Q6.10), a soft mask (Q6.19)
        bool isStopped = false;         ///< The run was canceled (nothing was decided)

        bool isVerified() const { return errors.empty() && !isStopped; }
    };

    PDFRedactionVerifier() = delete;

    /// Checks the written redacted copy (see the class)
    /// \param source The redacted document (its text gives the words of step 3)
    /// \param areas The redacted areas of the pages of the source
    /// \param outputData The written bytes of the copy (PDFDocumentToolOutputDocument::writtenData)
    /// \param options What was promised
    /// \param control Cancellation (may be null)
    /// \param progress Progress of the pages (may be null)
    static Result verify(const PDFDocument* source, const std::vector<Area>& areas, const QByteArray& outputData, const Options& options,
                         const PDFOperationControl* control, PDFProgressSink* progress);
};

}   // namespace pdf

#endif // PDFREDACTIONVERIFIER_H
