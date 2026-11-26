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

#ifndef PDFCONTENTTREEBUILDER_H
#define PDFCONTENTTREEBUILDER_H

#include "pdfcontentnode.h"

#include <vector>

namespace pdf
{
class PDFCMS;
class PDFDocument;
class PDFFontCache;
class PDFICCProfileCache;
class PDFOperationControl;
class PDFProgressSink;

// Edit Content, stage E0, package P02. Plan: zadani_jadro.md 4.3; zadani_overeni.md 6.1 - 6.3.

// Threads and ownership of the plain types of this header: PDFContentBuildOptions is a value;
// PDFContentBuildContext holds raw pointers to caches of the caller, which only have to outlive
// the call of build; PDFContentBuildResult is a value owned by the caller - its container is
// immutable and shared, so the result goes from the worker to the thread of the session as it is.

/// Options of a build
struct PDFContentBuildOptions
{
    /// Build the glyphs of the texts (PDFContentTextLayout); false for a caller, which needs only
    /// the structure (a listing of MCP without text, a count of objects)
    bool layoutText = true;
};

/// What the builder needs besides the document. The caches must outlive the call; the container
/// co-owns what it takes from them (fonts, realized fonts, color spaces), so it does not depend
/// on them afterwards.
struct PDFContentBuildContext
{
    const PDFFontCache* fontCache = nullptr;
    const PDFICCProfileCache* iccProfileCache = nullptr;    ///< May be nullptr
    const PDFCMS* cms = nullptr;

    /// Kind::Form and Kind::PatternCell: the index of the page, whose content uses the
    /// container first (the session knows it from the container, which named the form or the
    /// pattern). The processor is created for that page, and a content without /Resources of
    /// its own inherits the resources of that page (ISO 32000-2, 7.8.3; R-4: when such a
    /// container is changed, it gets its own copy of them). Ignored for Kind::Page. A Form or
    /// PatternCell key with the page index -1 gives no container and an issue.
    PDFInteger pageIndex = -1;
};

/// The result of a build
struct PDFContentBuildResult
{
    PDFContentContainerPointer container;   ///< nullptr, when the build was canceled or the key names nothing of the document
    std::vector<PDFContentIssue> issues;    ///< The issues of the container (the same list) and the reason of a missing container
    bool isCanceled = false;

    /// The form XObjects used by the container (Do), each once, in the order of their first use:
    /// the session queues their containers. The builder itself never descends into a form or
    /// into a cell of a tiling pattern.
    std::vector<PDFContentContainerKey> usedForms;

    /// The tiling patterns used as a color by the container, each once (built when entered)
    std::vector<PDFContentContainerKey> usedPatternCells;
};

/// Builds the container of one content: a page, a form XObject or a cell of a tiling pattern.
/// A descendant of PDFPageContentProcessor inside (the processor interprets the state, the fonts
/// and the colors; the builder adds the structure, the raw bytes, the resource names and the
/// text layout by the standard). One pass over the own stream of the container: forms and tiling
/// patterns are suppressed (isContentKindSuppressed), glyph procedures of Type 3 fonts are skipped
/// (performType3GlyphBegin / End), the visibility of optional content is ignored (the tree holds
/// hidden content too). Nothing of the content is refused: what the builder does not understand
/// is an Opaque node, a page with a stream it cannot decode is read-only.
///
/// Neither function throws: a key, whose dictionary cannot be read (a form or a pattern with
/// a /Matrix, which is not six numbers), gives no container and an issue (InvalidContainer), an
/// empty fingerprint; the Do of such a form is an Opaque node of the content, which uses it.
///
/// Two limits beside those of PDFContentContainer, both private to the builder:
///  - the decoded bytes of all streams of a content together (every item of /Contents counts,
///    also one, which names a stream again): above 512 MB the content is not read - the
///    container is empty and read-only, with an issue (a container keeps every byte of its
///    content, and /Contents can name one stream any number of times);
///  - the dash array of a state of a node: 128 elements. The content, in which a longer array is
///    in effect (set by d or by /D of gs), is one Opaque node with an issue, from the operation,
///    which sets it - or from the outermost open node around it -, up to the operation, after
///    which the pattern is within the limit again and every bracket opened in between is closed
///    (the states are compared after every operation and kept by the nodes: their cost must
///    not grow with a length the content chooses).
///
/// Threads and ownership: build is reentrant and runs in a worker thread (one task per container,
/// PDFExecutionPolicy::startTask); it reads the document and the caches only. The result is
/// immutable and independent of the builder.
class PDF4QTLIBCORESHARED_EXPORT PDFContentTreeBuilder
{
public:
    PDFContentTreeBuilder() = delete;

    /// Builds a container.
    /// \param document The document (must outlive the call)
    /// \param key The container
    /// \param initialState State at the beginning of the content: nullptr for a page and for a
    ///        cell of a tiling pattern (the default state), the state of the place of the first
    ///        use for a form (zadani_jadro.md 4.10)
    /// \param context Caches, and the page of a form or of a cell of a pattern (without it such a
    ///        key gives no container and an issue)
    /// \param options Options
    /// \param control Cancellation, may be nullptr
    /// \param progress Progress, may be nullptr
    static PDFContentBuildResult build(const PDFDocument* document,
                                       const PDFContentContainerKey& key,
                                       const PDFContentResolvedStatePointer& initialState,
                                       const PDFContentBuildContext& context,
                                       const PDFContentBuildOptions& options,
                                       const PDFOperationControl* control,
                                       PDFProgressSink* progress);

    /// The fingerprint a container of the key would have for the document now, without building
    /// the tree: empty, when the key names nothing of the document. Equal fingerprints mean the
    /// identifiers of the nodes of a container are valid for the document. The fingerprint of a
    /// form or of a pattern covers its own stream and its own /Resources only (never the
    /// resources it inherits from a page), so it needs no page.
    static QByteArray computeFingerprint(const PDFDocument* document, const PDFContentContainerKey& key);
};

}   // namespace pdf

#endif // PDFCONTENTTREEBUILDER_H
