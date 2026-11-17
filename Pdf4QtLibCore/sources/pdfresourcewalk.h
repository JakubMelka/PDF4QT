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

#ifndef PDFRESOURCEWALK_H
#define PDFRESOURCEWALK_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdfobjectutils.h"

#include <vector>

namespace pdf
{
class PDFDocument;
class PDFOperationControl;

/// What owns a resource dictionary (ISO 32000-2, 7.8.3, Table 34; 8.10 forms, 8.7.3 tiling patterns,
/// 9.6.4 Type 3 fonts, 11.6.5.2 soft masks, 12.5.5 appearance streams)
enum class PDFResourceOwnerKind
{
    Page,               ///< A page (its /Resources, inherited from the page tree, 7.7.3.4)
    Form,               ///< A form XObject of /XObject (/Subtype /Form)
    AppearanceStream,   ///< An appearance stream of an annotation (/AP /N, /R, /D, a stream or the streams of a dictionary of states)
    Pattern,            ///< A tiling pattern of /Pattern (/PatternType 1)
    Type3Font,          ///< A Type 3 font of /Font (/Subtype /Type3)
    SoftMaskGroup       ///< The transparency group /G of a soft mask of /ExtGState (/SMask)
};

/// One owner of resources reached by the walk (a node of the graph of the resources)
class PDF4QTLIBCORESHARED_EXPORT PDFResourceWalkNode
{
public:
    PDFResourceOwnerKind kind = PDFResourceOwnerKind::Page;
    PDFObjectReference reference;       ///< The owner object (the page, the stream of a form, the pattern, the font); invalid for a direct object
    PDFObject object;                   ///< The owner object, dereferenced (a page has its dictionary of the page tree)
    PDFObject resources;                ///< Its resource dictionary, dereferenced (a null object = none: a form or a Type 3 font without /Resources)
    PDFInteger pageIndex = -1;          ///< The page (index from 0), through which the node was reached first
    PDFObjectReference annotation;      ///< The annotation of an appearance stream (else invalid)
    int depth = 0;                      ///< The nesting: a page 0, a form of the page 1, a form of that form 2, ...
    std::vector<size_t> children;       ///< Indices of the nodes reached from its resources (and its annotations): every edge, also to a node entered before through another path - a node is entered once
};

/// The graph of the resources of the walked pages (PDFResourceWalk::walk)
class PDF4QTLIBCORESHARED_EXPORT PDFResourceWalkResult
{
public:
    std::vector<PDFResourceWalkNode> nodes;                 ///< Every owner once, in the order of the walk (breadth first from the pages)
    std::vector<std::pair<PDFInteger, size_t>> pages;       ///< The walked pages: the index of the page and its node, in the order of the pages given
    bool isLimitReached = false;                            ///< A node was not entered by the depth or the budget (PDFObjectWalk::isLimitReached): the graph is not complete
    bool isCanceled = false;                                ///< The walk was canceled: the graph is not complete

    /// The indices of the walked pages, from which the node is reachable (a form of two pages has both;
    /// sorted). A walk backwards over the edges with one visited set: O(nodes + edges) per call.
    std::vector<PDFInteger> getPageIndices(size_t nodeIndex) const;
};

/// The walk over the resources of pages (plan of step 7 of phase 2, 6.1, R7-17, B167, B197): the resources
/// of the pages, of their forms (nested), of the appearance streams of their annotations, of the tiling
/// patterns, of the Type 3 fonts and of the groups of the soft masks. Ink Coverage and Output Preview take
/// the spot colors from it (B197), Document Report the fonts (K7.24) - one walk instead of each reading only
/// the resources of the page.
///
/// The rules of every walk over the object graph (CLAUDE.md, PDFObjectWalk): one visited set for the whole
/// walk (an owner reached by two paths or by a cycle - a form, which draws itself through another form - is
/// entered once; the other path records only its edge), a depth limit of the nesting and a budget of the
/// nodes; breadth first, so the first visit of a node is by its shortest path and nothing depends on the
/// recursion of the caller (iterative: a chain of nested forms of a hostile file never overflows the stack).
/// A missing or a wrong object (no dictionary, a reference to nothing, a name where a stream is) is
/// skipped, never an error. Images, shadings and the other resources have no resources of their own: the
/// caller reads them from PDFResourceWalkNode::resources.
///
/// Threads: reentrant, the document is read only (a shared immutable document of a run).
class PDF4QTLIBCORESHARED_EXPORT PDFResourceWalk
{
public:
    PDFResourceWalk() = delete;

    /// Options of a walk
    class Options
    {
    public:
        bool includeAnnotations = true;                                     ///< The appearance streams of the annotations of the pages are walked (Q7.8: the tool filters by /F Print itself)
        int maximumDepth = PDFObjectWalk::DEFAULT_MAXIMUM_DEPTH;            ///< Deepest nesting of the owners (a page is 0)
        qint64 maximumNodes = PDFObjectWalk::DEFAULT_MAXIMUM_NODES;         ///< Most entered nodes
    };

    /// Walks the resources of the pages
    /// \param document The document (nullptr = an empty result)
    /// \param pageIndices The pages (indices from 0; an invalid index is skipped, a repeated one is walked once)
    /// \param options The options
    /// \param control Cancellation, checked at every node (optional)
    static PDFResourceWalkResult walk(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, const Options& options, const PDFOperationControl* control);
};

}   // namespace pdf

#endif // PDFRESOURCEWALK_H
