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

#ifndef PDFDOCUMENTTEXTEXPORTER_H
#define PDFDOCUMENTTEXTEXPORTER_H

#include "pdfglobal.h"
#include "pdfdocumenttextflow.h"
#include "pdfoutputformatter.h"
#include "pdfprogress.h"

#include <QByteArray>
#include <QString>
#include <QStringDecoder>

#include <vector>

namespace pdf
{

class PDFDocument;
class PDFOperationControl;

/// Writes the text flow of a document (PDFDocumentTextFlowFactory) by an output formatter:
/// the one loop of `PdfTool fetch-text` and of the tool Extract Text. The text is encoded
/// by the exporter itself (the console of PdfTool has its own encoding), so the file of the
/// tool says its charset and an assistant or the clipboard can read it back.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentTextExporter
{
public:
    PDFDocumentTextExporter() = delete;

    /// What is written besides the text of the pages (the items of the structure tree)
    struct Options
    {
        bool showPageNumbers = false;                       ///< "Page N" before each page (layout and content stream order)
        bool showStructureTitles = false;                   ///< Titles of the structure items
        bool showStructureLanguage = false;                 ///< Languages of the structure items
        bool showStructureAlternativeDescription = false;   ///< Alternative descriptions (texts of pictures)
        bool showStructureExpandedForm = false;             ///< Expanded forms of abbreviations
        bool showStructureActualText = false;               ///< Replacement texts
        bool showStructurePhoneme = false;                  ///< Pronunciation hints

        /// An empty line after each text (a plain text of paragraphs); otherwise a line
        /// break ends each page as in fetch-text
        bool separateItems = false;

        /// Characters, which XML 1.0 does not allow (control characters, lone surrogates,
        /// U+FFFE / U+FFFF), are replaced by U+FFFD: a document can hold anything, a written
        /// XML or HTML file must stay readable
        bool sanitizeMarkup = false;

        /// White space at the end of each line of a text is removed (the layout ends the lines
        /// of a block by a space)
        bool trimLineEnds = false;

        /// Title of the root element (HTML), empty = none (fetch-text)
        QString documentTitle;
    };

    /// Writes the document element, the items of the flow and the end of the document
    /// \param flow Text flow of the document
    /// \param options What is written
    /// \param formatter Formatter of the output (a new one)
    static void write(const PDFDocumentTextFlow& flow, const Options& options, PDFOutputFormatter& formatter);

    /// The item is written (its flags and the options; an item without a text is never written)
    static bool isItemShown(const PDFDocumentTextFlow::Item& item, const Options& options);

    /// Counts of the written text: the words and the characters of the shown items, the pages
    /// with a text (an item of the page with a text other than white space)
    struct TextCount
    {
        qint64 words = 0;
        qint64 characters = 0;
        std::vector<PDFInteger> pagesWithText;  ///< Indices of the pages, sorted
        bool hasText = false;                   ///< Something else than page numbers is written
    };

    /// Counts the text, which write writes
    /// \param flow Text flow
    /// \param options What is written
    /// \param pageIndex Only the items of this page (-1 = all items; an item of the structure,
    ///        which has no page, counts only for -1)
    static TextCount count(const PDFDocumentTextFlow& flow, const Options& options, PDFInteger pageIndex = -1);

    /// Encoding of an output
    enum class Encoding
    {
        Utf8,       ///< UTF-8, every language
        Latin1,     ///< ISO-8859-1, Western European legacy programs
        System      ///< The encoding of this computer (the ANSI code page of Windows, UTF-8 elsewhere)
    };

    /// Encoded output of a formatter
    struct EncodedText
    {
        QByteArray data;            ///< The bytes of the file
        QString mimeType;           ///< "text/plain", "text/html; charset=ISO-8859-1", …: UTF-8 has no charset
        qint64 lostCharacters = 0;  ///< Characters, which the encoding cannot hold ('?' in the file)
    };

    /// Encodes the output of a formatter. An XML or HTML file says its charset (a declaration of
    /// the XML encoding other than UTF-8, a meta element of HTML), so a reader decodes it right.
    /// \param text Output of the formatter
    /// \param style Style of the formatter
    /// \param encoding Encoding
    static EncodedText encode(QString text, PDFOutputFormatter::Style style, Encoding encoding);

    /// Name of the charset of the encoding System: "windows-1250" by the ANSI code page of
    /// Windows, "UTF-8" elsewhere (Qt takes the system encoding of Unix as UTF-8)
    static QString getSystemCharset();

    /// Decoder of an output of encode by the charset of its type (UTF-8 without one). The
    /// charset of the system is decoded by Qt itself, also when Qt has no ICU (Windows knows
    /// only its code page). An invalid decoder: the charset is not known.
    /// \param mimeType Type of the output
    static QStringDecoder createDecoder(const QString& mimeType);

    /// Pages of the given ones, which have a text: a character other than white space is
    /// drawn on them, also in a form, as the layout algorithm reads it (images are not
    /// decoded, a page is left at its first character). Pages run in the threads of
    /// PDFExecutionPolicy, the cancellation is checked per operator.
    /// \param document Document
    /// \param pageIndices Pages (indices from 0)
    /// \param control Cancellation (optional)
    /// \param progress Progress, a step per page (optional)
    /// \returns Indices of the pages with a text, sorted
    static std::vector<PDFInteger> findPagesWithText(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, const PDFOperationControl* control, PDFProgressSink* progress);
};

}   // namespace pdf

#endif // PDFDOCUMENTTEXTEXPORTER_H
