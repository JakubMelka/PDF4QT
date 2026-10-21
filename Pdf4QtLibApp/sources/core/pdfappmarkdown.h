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

#ifndef PDFAPPMARKDOWN_H
#define PDFAPPMARKDOWN_H

#include "pdfappglobal.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace pdf
{

/// Kind of the target of a link of a Markdown text. Only these targets are links:
/// everything else (javascript:, file:, http:, mailto:, relative paths) is shown
/// as a plain text.
enum class PDFAppMarkdownLinkKind
{
    None,       ///< Not a link (refused target)
    Route,      ///< Route of the application ("/settings/general", "/help/portable-mode#data")
    External,   ///< External address, https only
    Anchor      ///< Heading of the same text ("#keyboard")
};

/// Run of an inline text with one format
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMarkdownSpan
{
public:
    QString text;
    bool bold = false;
    bool italic = false;
    bool code = false;

    /// Target of a link (empty = no link) and its kind
    QString link;
    PDFAppMarkdownLinkKind linkKind = PDFAppMarkdownLinkKind::None;

    bool isLink() const { return linkKind != PDFAppMarkdownLinkKind::None; }
    bool hasSameFormat(const PDFAppMarkdownSpan& other) const;
};

using PDFAppMarkdownSpans = std::vector<PDFAppMarkdownSpan>;

/// Kind of a callout (block quote starting with [!NOTE], [!TIP], ...)
enum class PDFAppMarkdownCallout
{
    None,       ///< A plain block quote
    Note,       ///< [!NOTE] blue, icon info
    Tip,        ///< [!TIP] blue, icon zap
    Important,  ///< [!IMPORTANT] violet, icon alert-circle
    Warning,    ///< [!WARNING] amber, icon alert-circle
    Caution     ///< [!CAUTION] red, icon alert-triangle
};

/// Block of a Markdown document
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMarkdownBlock
{
public:
    enum class Type
    {
        Paragraph,      ///< spans
        Heading,        ///< level 1 - 6, spans, anchor
        List,           ///< children = items (Type::ListItem), ordered, startNumber
        ListItem,       ///< children = blocks of the item
        CodeBlock,      ///< text (language is ignored)
        Quote,          ///< children, callout, spans = title of a callout
        Table,          ///< rows of cells (the first row is the header), alignments
        Image,          ///< source, text = alternative text
        Rule            ///< thematic break
    };

    Type type = Type::Paragraph;

    /// Inline content (paragraph, heading, title of a callout)
    PDFAppMarkdownSpans spans;

    /// Level of a heading (1 - 6)
    int level = 0;

    /// Anchor of a heading (explicit {#anchor} or created from the text)
    QString anchor;

    /// Text of a code block, alternative text of an image
    QString text;

    /// Source of an image (a path of the resources)
    QString source;

    /// List: ordered, the number of the first item
    bool ordered = false;
    int startNumber = 1;

    /// Kind of a quote
    PDFAppMarkdownCallout callout = PDFAppMarkdownCallout::None;

    /// Nested blocks (list items, blocks of an item, of a quote)
    std::vector<PDFAppMarkdownBlock> children;

    /// Table: rows of cells, the first row is the header; alignments of the
    /// columns (Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight)
    std::vector<std::vector<PDFAppMarkdownSpans>> rows;
    std::vector<int> alignments;
};

/// Heading of a document (table of contents, anchors)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMarkdownHeading
{
public:
    int level = 0;
    QString text;
    QString anchor;
};

/// Parsed Markdown document
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMarkdownDocument
{
public:
    std::vector<PDFAppMarkdownBlock> blocks;

    /// The text was longer than PDFAppMarkdownParser::MAXIMUM_TEXT_LENGTH and was cut
    bool truncated = false;

    /// Headings of the whole document in their order
    std::vector<PDFAppMarkdownHeading> getHeadings() const;

    /// Returns true, if a heading has the anchor
    bool hasAnchor(const QString& anchor) const;

    /// Targets of all links (also the refused ones are not here - they are texts)
    QStringList getLinks() const;

    /// Plain text of the document (search, accessibility); blocks are separated by
    /// new lines
    QString toPlainText() const;

    bool isEmpty() const { return blocks.empty(); }
};

/// Parser of the subset of Markdown used by the user manual and the release notes
/// (CommonMark-like): ATX headings with an optional explicit anchor "{#id}",
/// paragraphs, emphasis (* _ ** __ ***), inline code, escapes, links [text](target)
/// (only routes "/...", "https://..." and anchors "#..."), fenced code blocks,
/// nested lists (- * + and 1. 1)), block quotes and callouts (> [!NOTE] Title),
/// pipe tables, block images ![alt](path) of the resources and thematic breaks.
///
/// Raw HTML is never rendered: it stays a literal text. The parser is a pure
/// function, its input is untrusted: its time is linear in the length of the text,
/// containers deeper than MAXIMUM_DEPTH are plain text, a text longer than
/// MAXIMUM_TEXT_LENGTH is cut, control characters are removed.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMarkdownParser
{
public:
    PDFAppMarkdownParser() = delete;

    /// Longest text, which is parsed (characters)
    static constexpr qsizetype MAXIMUM_TEXT_LENGTH = 2 * 1024 * 1024;

    /// Deepest nesting of lists and quotes
    static constexpr int MAXIMUM_DEPTH = 8;

    /// Most columns of a table (more cells are joined into the last one)
    static constexpr int MAXIMUM_TABLE_COLUMNS = 16;

    /// Longest target of a link
    static constexpr qsizetype MAXIMUM_LINK_LENGTH = 2048;

    /// Parses the text
    static PDFAppMarkdownDocument parse(const QString& text);

    /// Parses an inline text (no blocks)
    static PDFAppMarkdownSpans parseInline(const QString& text);

    /// Anchor of a heading text: lowercase letters and digits, words joined by '-'
    /// ("What you need" -> "what-you-need"), at most 64 characters
    static QString createAnchor(const QString& text);

    /// Kind of the target of a link (see PDFAppMarkdownLinkKind)
    static PDFAppMarkdownLinkKind classifyLink(const QString& target);

    /// Plain text of the spans
    static QString toPlainText(const PDFAppMarkdownSpans& spans);
};

}   // namespace pdf

#endif // PDFAPPMARKDOWN_H
