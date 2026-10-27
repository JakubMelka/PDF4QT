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


#ifndef PDFAPPMCPDOCUMENTREADER_H
#define PDFAPPMCPDOCUMENTREADER_H

#include "pdfappglobal.h"

#include "pdfglobal.h"
#include "pdfannotation.h"

#include <QByteArray>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace pdf
{

class PDFAppContext;
class PDFAppDocument;
class PDFAppDocumentView;
class PDFDocument;
class PDFMcpJsonWriter;
class PDFOutlineItem;

/// documentId and title of a result (copied in the GUI thread, used by a worker)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpDocumentHeader
{
public:
    QString documentId;
    QString title;

    static PDFAppMcpDocumentHeader create(const PDFAppDocument* document);

    void write(PDFMcpJsonWriter& writer) const;
};

/// Parameters of document.get_annotations
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpAnnotationQuery
{
public:
    std::vector<PDFInteger> pageIndices;    ///< Pages in their order
    QSet<QString> types;                    ///< Contract type names; empty = all
    bool includeReplies = true;
    int maximumResults = 200;
    qint64 offset = 0;                      ///< Index of the first annotation of the filtered sequence (the cursor)
};

/// Readers of the actions of the tool "document" (plan of step 7, 3.2) and of
/// the document resources (4.2): each returns the object of its action (the
/// property <action> of structuredContent) in the order of the contract
/// (PDFAppMcpToolSet). The texts of the document are untrusted: sanitized and
/// bounded, the results carry "origin":"document". The readers of a
/// PDFDocument only (annotations, form fields, outline, pages) touch nothing
/// else and run also in a worker on a snapshot; the readers of a PDFAppDocument
/// run in the GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpDocumentReader
{
public:
    PDFAppMcpDocumentReader() = delete;

    /// Most annotations considered on one page (a hostile /Annots array)
    static constexpr size_t MAXIMUM_PAGE_ANNOTATIONS = 10000;

    /// Most quadrilaterals of one annotation in a result
    static constexpr int MAXIMUM_ANNOTATION_QUADS = 2000;

    /// Most form fields read (a hostile form)
    static constexpr int MAXIMUM_FORM_FIELDS = 10000;

    /// Most options of a choice field in a result
    static constexpr int MAXIMUM_FIELD_OPTIONS = 1000;

    /// Most layers in a result
    static constexpr int MAXIMUM_LAYERS = 1000;

    /// The query of document.get_annotations reads more than this number of pages in a worker
    static constexpr int ANNOTATION_WORKER_PAGE_LIMIT = 50;

    /// get_info (GUI thread)
    static QByteArray getInfo(const PDFAppDocument* document);

    /// get_pages of the page indices; \p nextPage (from 1) when more pages follow
    static QByteArray getPages(const PDFAppMcpDocumentHeader& header, const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, std::optional<PDFInteger> nextPage);

    /// get_outline
    static QByteArray getOutline(const PDFAppMcpDocumentHeader& header, const PDFDocument* document, int maximumDepth, int maximumItems);

    /// Outline item of an id ("2.1.4", 1-based path); nullptr = none
    static const PDFOutlineItem* findOutlineItem(const PDFDocument* document, const QString& itemId);

    /// Page index of the destination of an outline item (-1 = none)
    static PDFInteger getOutlineItemPage(const PDFDocument* document, const PDFOutlineItem* item);

    /// get_annotations; \p errorMessage for an invalid cursor
    static QByteArray getAnnotations(const PDFAppMcpDocumentHeader& header, const PDFDocument* document, const PDFAppMcpAnnotationQuery& query);

    /// Contract name of the type of an annotation ("highlight", "note", "widget", "other")
    static QString getAnnotationTypeName(const PDFAnnotation* annotation);

    /// Cursor of the offset of get_annotations / the offset of a cursor (std::nullopt = invalid)
    static QString createCursor(qint64 offset);
    static std::optional<qint64> parseCursor(const QString& cursor);

    /// get_form_fields; \p names = only these fully qualified names (empty = all)
    static QByteArray getFormFields(const PDFAppMcpDocumentHeader& header, const PDFDocument* document, const QStringList& names, bool includeValues);

    /// get_attachments (GUI thread)
    static QByteArray getAttachments(const PDFAppDocument* document);

    /// get_signatures from the results of the verification of the tab (GUI thread)
    static QByteArray getSignatures(const PDFAppDocument* document, bool isVerifying);

    /// get_selection: the text selection of the view (GUI thread)
    static QByteArray getSelection(const PDFAppDocument* document, const PDFAppDocumentView* view);

    /// get_layers (GUI thread: the visibility of the tab)
    static QByteArray getLayers(const PDFAppDocument* document);

    /// get_bookmarks (GUI thread: the database of the context)
    static QByteArray getBookmarks(PDFAppContext* context, const PDFAppDocument* document);

    /// "#rrggbb" of the color components of a PDF (gray, RGB, CMYK); empty = no color
    static QString formatColor(const std::vector<PDFReal>& components);

    /// Writes the outline item id of a path of child indices (from 0)
    static QString createOutlineItemId(const std::vector<size_t>& path);
};

}   // namespace pdf

#endif // PDFAPPMCPDOCUMENTREADER_H
