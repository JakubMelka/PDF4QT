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

#ifndef PDFDOCUMENTLINKOPERATIONS_H
#define PDFDOCUMENTLINKOPERATIONS_H

#include "pdfdocument.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QRectF>
#include <QString>

#include <set>
#include <vector>

namespace pdf
{

class PDFAnnotation;
class PDFOperationControl;

/// The external links of a document (Remove External Links, plan of the first batch of phase
/// 2, chapters 2.7 and 7): a link annotation, whose action leaves the document - an URI action
/// (PDFAnnotation::isExternalLinkAnnotation). The same rule is used by the old applications
/// (PdfTool remove-external-links, Edit > Remove External Links of the Editor), they call
/// removeExternalLinkAnnotations. Every function only reads its document and builds a new one;
/// the document is untrusted: the texts are sanitized and bounded, the lists are bounded.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentLinkOperations
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentLinkOperations)

public:
    PDFDocumentLinkOperations() = delete;

    /// Most external links listed (a hostile document of millions of links)
    static constexpr int MAXIMUM_LINKS = 100000;

    /// Most characters of a shown text of a link (its title, its address)
    static constexpr int MAXIMUM_TEXT_LENGTH = 512;

    /// Kind of an external link: the filters of Show (438:15032, K17 - an address of another
    /// scheme is only in All links)
    enum class LinkKind
    {
        Web,    ///< http and https
        EMail,  ///< mailto
        Other   ///< Any other address (ftp, file, ...)
    };

    /// One external link of a page
    struct ExternalLink
    {
        PDFInteger pageIndex = -1;                  ///< Index of the page from 0
        int annotationIndex = -1;                   ///< Index of the item of /Annots of the page
        PDFObjectReference annotationReference;     ///< Reference of the annotation; invalid = a direct dictionary in /Annots
        QString id;                                 ///< Id of the link (createId): "obj:gen", "p<page>:<index>" of a direct annotation
        QString title;                              ///< Shown title: the contents of the link, else the address without its scheme (sanitized)
        QString address;                            ///< Shown address (sanitized, bounded)
        LinkKind kind = LinkKind::Other;
        QRectF rectangle;                           ///< /Rect of the annotation in the coordinates of the page
    };

    /// External links of a document
    struct LinkList
    {
        std::vector<ExternalLink> links;    ///< In the order of the pages and of /Annots
        PDFInteger internalLinkCount = 0;   ///< Links, which stay inside of the document (they are never touched)
        bool isTruncated = false;           ///< More than MAXIMUM_LINKS external links: the rest is not listed
    };

    /// What a removal has made
    struct RemovalResult
    {
        PDFDocumentPointer document;                        ///< The new document, null = nothing was removed
        PDFModifiedDocument::ModificationFlags flags;       ///< Flags of the change
        int removed = 0;                                    ///< Removed links
    };

    /// Lists the external links of the document
    /// \param document The document
    /// \param control Cancellation (optional): a canceled listing is incomplete
    /// \param maximumLinks Most links listed, the rest is only counted (isTruncated); the removal
    ///        lists all of them, so that no link of a large document stays
    static LinkList listExternalLinks(const PDFDocument* document, const PDFOperationControl* control = nullptr, size_t maximumLinks = MAXIMUM_LINKS);

    /// Kind of an address by its scheme (case insensitive)
    static LinkKind getLinkKind(const QByteArray& address);

    /// Id of an annotation of a page: "obj:gen" of an indirect annotation (the id of
    /// document.get_annotations of MCP), "p<page>:<index>" of a direct one (indices from 0)
    static QString createId(PDFObjectReference reference, PDFInteger pageIndex, int annotationIndex);

    /// Parses a list of ids separated by commas ("12:0, 15:0, p3:1"); an empty text is no id.
    /// \returns false, when an item is no id (\p invalidItem is the item, sanitized)
    static bool parseIds(const QString& text, std::set<QString>* ids, QString* invalidItem);

    /// Removes the given links (a list of listExternalLinks of the same document): the items
    /// of /Annots of their pages, the indirect annotations become null objects, so that
    /// their addresses are not in a file written whole. The page tree, the numbers of the
    /// objects and the other annotations stay (an incremental update of a signed file).
    /// \param document The document
    /// \param links Links to remove
    static RemovalResult removeLinks(const PDFDocument* document, const std::vector<ExternalLink>& links);

    /// The command of the old applications (PdfTool remove-external-links, the Editor): every
    /// external link of a page with an array of references /Annots is removed by
    /// PDFDocumentBuilder::removeAnnotation after the page tree is flattened (the behavior of
    /// these applications before the tools of phase 2, unchanged)
    static RemovalResult removeExternalLinkAnnotations(const PDFDocument* document);

    /// The links as data of a result (the preview of the tool) and back
    static QJsonArray toJson(const std::vector<ExternalLink>& links);
    static std::vector<ExternalLink> fromJson(const QJsonArray& array);

    /// Name of a kind in JSON ("web", "email", "other")
    static QString getKindName(LinkKind kind);

private:
    /// Address of the URI action of an external link annotation
    static QByteArray getAddress(const PDFAnnotation* annotation);

    /// Shown title of a link without own contents ("acme-corp.example/pricing")
    static QString createTitle(const QString& address, LinkKind kind);
};

}   // namespace pdf

#endif // PDFDOCUMENTLINKOPERATIONS_H
