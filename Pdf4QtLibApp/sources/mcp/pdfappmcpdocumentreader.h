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

#include "pdfdocumentimagelist.h"

#include <QByteArray>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QStringList>

#include <deque>
#include <memory>
#include <optional>
#include <vector>

namespace pdf
{

class PDFAppContext;
class PDFAppDocument;
class PDFAppDocumentView;
class PDFOperationControl;
class PDFDocument;
class PDFMcpJsonWriter;
class PDFOutlineItem;

/// documentId and title of a result (copied in the GUI thread, used by a worker)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpDocumentHeader
{
public:
    QString documentId;
    QString title;
    bool isPagedXFA = false;    ///< The document is the paged layout of an XFA form (step 8; not written, the note of the form fields says it)

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

/// Parameters of document.list_images (step 4 of phase 2, plan 7, R4-8)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpImageQuery
{
public:
    std::vector<PDFInteger> pageIndices;    ///< Pages of the selection (an image drawn on one of them is listed)
    int maximumResults = 200;
    qint64 offset = 0;                      ///< Index of the first image of the listed sequence (the cursor)
};

/// The complete lists of the images of the last versions of the documents (document.list_images,
/// RS4APP): the pages of a document are walked once for all the cursors of one listing, not again
/// for every page of the results. Thread-safe (the jobs of the workers read and fill it); the
/// lists are immutable values shared with the jobs. At most MAXIMUM_LISTS lists, the oldest go.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpImageListCache
{
public:
    static constexpr size_t MAXIMUM_LISTS = 4;

    /// The list of the version of the document, nullptr = none
    std::shared_ptr<const PDFDocumentImageList> find(const QString& documentId, quint64 version) const;

    /// Remembers the complete list of the version of the document (an incomplete one is ignored)
    void insert(const QString& documentId, quint64 version, std::shared_ptr<const PDFDocumentImageList> list);

private:
    class Entry
    {
    public:
        QString documentId;
        quint64 version = 0;
        std::shared_ptr<const PDFDocumentImageList> list;
    };

    mutable QMutex m_mutex;
    std::deque<Entry> m_entries;    ///< The newest last
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

    /// Most pages of one image in a result of list_images (pageCount has all of them)
    static constexpr int MAXIMUM_IMAGE_PAGES = 100;

    /// list_images (a worker on a snapshot): the images of PDFDocumentImageList with the options of an
    /// export without a tab (every layer; hiddenLayer by /D with the automatic states of /Export,
    /// OCUsage::Export - MCP has no tab), drawn on a page of the query, in the order of the list; the
    /// number is the index of the whole list + 1 (N of "Image N" and of the token #). The collection
    /// stops, when \p control is canceled (the result is then not used).
    static QByteArray getImages(const PDFAppMcpDocumentHeader& header, const PDFDocument* document, const PDFAppMcpImageQuery& query, const PDFOperationControl* control);

    /// getImages of a list collected before (collectImages, PDFAppMcpImageListCache)
    static QByteArray getImages(const PDFAppMcpDocumentHeader& header, const PDFDocumentImageList& list, const PDFAppMcpImageQuery& query);

    /// The list of the images of list_images (the options of an export without a tab)
    static std::shared_ptr<const PDFDocumentImageList> collectImages(const PDFDocument* document, const PDFOperationControl* control);

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
