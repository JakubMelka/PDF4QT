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

#ifndef PDFAPPMCPARGUMENTS_H
#define PDFAPPMCPARGUMENTS_H

#include "pdfappglobal.h"
#include "pdfglobal.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <vector>

namespace pdf
{

class PDFAppContext;
class PDFDocument;

/// Strict parsing of the arguments of the tools (plan 2.3): coordinates,
/// colors, page ranges, labels, paths. The texts of the errors are written for
/// a model ("pages: page 12 is beyond the last page 10"). The JSON values
/// themselves are read by PDFMcpJsonReader; these are the domain rules.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpArguments
{
public:
    PDFAppMcpArguments() = delete;

    /// Color of a palette name (yellow … white - the canonical light-theme values
    /// of the annotation colors) or of "#rrggbb"
    static std::optional<QColor> parseColor(const QString& text, QString* errorMessage);

    /// Palette name of a color, "#rrggbb" otherwise (outputs)
    static QString formatColor(const QColor& color);

    /// Page indices (from 0, in the order of the document) of a range text of the
    /// PDFClosedIntervalSet syntax ("1-3,7", "5-"; empty = all pages), at most
    /// \p maximumPages pages (0 = no limit). Errors: syntax, a page beyond the
    /// last page, too many pages.
    static std::optional<std::vector<PDFInteger>> parsePages(const QString& text, PDFInteger pageCount, int maximumPages, QString* errorMessage);

    /// Page indices (from 0) of a page label; several pages may have the label
    static std::vector<PDFInteger> findPagesByLabel(const PDFDocument* document, const QString& label);

    /// Label of a page (index from 0); the page number when the document has no labels
    static QString getPageLabel(const PDFDocument* document, PDFInteger pageIndex);

    /// Text written into a document: controls and bidirectional overrides
    /// removed (tab / newline kept), at most \p maximumLength characters
    static QString sanitizeDocumentText(const QString& text, int maximumLength);

    /// Rules of a path given by an assistant (plan 7): absolute, clean, no
    /// device / UNC / network path, no NTFS stream, no reserved device name, a
    /// parent folder that exists, not a folder; \p requirePdfExtension for a
    /// written file. Symbolic links (and junctions) are resolved: the resolved
    /// path is checked by the same rules (a link anywhere in the path to a
    /// refused place is refused), a link whose target does not exist is
    /// refused. Files in the folder of the database of \p context (when it has
    /// one) and in the folder of the logs are refused. Returns the canonical
    /// absolute path (links resolved) or an empty string and the reason.
    static QString checkPath(const QString& path, bool requirePdfExtension, const PDFAppContext* context, QString* errorMessage);

    /// checkPath without the folder of the database (no context)
    static QString checkPath(const QString& path, bool requirePdfExtension, QString* errorMessage);

    /// The path lies in the folder (or is the folder), compared on the canonical
    /// paths (case insensitive on Windows); false for an empty / missing folder
    static bool isInFolder(const QString& canonicalPath, const QString& folder);

    /// Annotation id "obj:gen" -> object number and generation
    static bool parseAnnotationId(const QString& id, qint64* objectNumber, qint64* generation);

    /// Annotation id of a reference
    static QString formatAnnotationId(qint64 objectNumber, qint64 generation);
};

}   // namespace pdf

#endif // PDFAPPMCPARGUMENTS_H
