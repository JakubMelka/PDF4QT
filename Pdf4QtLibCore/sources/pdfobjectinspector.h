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

#ifndef PDFOBJECTINSPECTOR_H
#define PDFOBJECTINSPECTOR_H

#include "pdfglobal.h"
#include "pdfcms.h"
#include "pdfdocument.h"
#include "pdfobject.h"
#include "pdfobjectutils.h"
#include "pdfstreamfilters.h"

#include <QCoreApplication>
#include <QImage>
#include <QString>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

namespace pdf
{
class PDFOperationControl;
class PDFDocumentToolValueFormatter;

/// The views of the Object Inspector (482:15054, the 13 modes of the old ObjectInspectorPlugin): the
/// document from the trailer, the objects of one class of PDFObjectClassifier, the list of all objects
enum class PDFObjectInspectorView
{
    Document,       ///< The trailer and the catalog ("document")
    Pages,          ///< The pages ("pages")
    ContentStreams, ///< Content streams ("content-streams")
    GraphicStates,  ///< Graphic states ("graphic-states")
    ColorSpaces,    ///< Color spaces ("color-spaces")
    Patterns,       ///< Patterns ("patterns")
    Shadings,       ///< Shadings ("shadings")
    Images,         ///< Images ("images")
    Forms,          ///< Form XObjects ("forms")
    Fonts,          ///< Fonts ("fonts")
    Actions,        ///< Actions ("actions")
    Annotations,    ///< Annotations ("annotations")
    Objects         ///< Every indirect object of the storage ("objects", Object List)
};

/// One node of the tree of the inspector: an indirect object, an entry of a dictionary or an item of an
/// array. A value; the children are asked lazily (PDFObjectInspector::getChildren).
class PDF4QTLIBCORESHARED_EXPORT PDFObjectInspectorNode
{
public:
    PDFObjectReference reference;   ///< The indirect object of the node (invalid for a direct value)
    PDFObject object;               ///< The value, dereferenced
    QByteArray key;                 ///< The key of a dictionary entry (empty otherwise)
    PDFInteger arrayIndex = -1;     ///< The index of an array item (-1 otherwise)
    PDFObjectReference container;   ///< The indirect object, which contains a direct value (the reference of the path)
    int depth = 0;                  ///< Depth of the path (a root has 0; at most PDFObjectInspector::MAXIMUM_PATH_DEPTH)
    bool hasChildren = false;       ///< The node has children to fetch (a dictionary, an array, a stream; never a repeated reference)
    bool isRepeated = false;        ///< A reference opened at another place of the view already: shown, not expanded ("A reference is opened only once…", 482:14847); in the Object List every reference (each object is a root there, 482:16200)
    QString objectStreamNote;       ///< "in object stream 45 0 R" for an object of an object stream (FD-6), empty otherwise
    PDFObjectReference objectStream;    ///< The object stream of an indirect object of an object stream (FD-6; PDFObjectInspector::setObjectStreams), invalid otherwise
    bool isTrailer = false;         ///< The trailer dictionary (the root of the view Document, the first root of the Object List; FD-6: the dictionary of a cross-reference stream)
    bool isFollowingReferences = true;  ///< False in the Object List: "References are not followed in the object list" (482:16200) - a reference is a repeated node there
};

/// The inspector of the objects of a document (plan of step 7 of phase 2, 4.3, 9.3; B158, B165, B185, B207;
/// the logic of the old plugin moved to the core, values without Qt Widgets):
///  - getRoots(view): the roots of a view - the trailer of the document, the objects of a class of
///    PDFObjectClassifier, every object; each root at most once;
///  - getChildren(node): the entries of a dictionary, the items of an array, the dictionary of a stream,
///    on request only (a lazy tree: the GUI model fetches them by canFetchMore / fetchMore); a reference
///    shown at another place of the view is a repeated node, never expanded again - one visited set per
///    view (OpenedReferences of the caller), a depth of a path at most MAXIMUM_PATH_DEPTH;
///  - describe(object): the text of the right panel ("Stream [9 items, 184,330 data bytes, 1600 x 900
///    pixels …]") and its description;
///  - decodeStream(reference, limits, control): the decoded data by PDFLimitedStreamDecoder (at most
///    INSPECTOR_MAXIMUM_DECODED_SIZE, the first MAXIMUM_SHOWN_BYTES shown as a text or as a hex dump of
///    binary data, Q7.12), the name of a failed or an unknown filter (FD-4); never on the GUI thread
///    (B165): the GUI runs it in one QtConcurrent::run job with a copy of the reference and the shared
///    document (plan 4.3);
///  - getImage(reference, control): the picture of an image XObject by PDFImage, also without /Type
///    (B185), in the same job.
///
/// Threads: the views and the visited sets belong to the thread of their owner (the GUI); decodeStream and
/// getImage are reentrant over the shared immutable document. The class holds the document by its shared
/// pointer (a snapshot of the tab), never a pointer to an object of the GUI.
///
/// The views of the classes need the classification of the whole document (PDFObjectClassifier, a walk over
/// the object graph without decoding) and the Object List the numbers of the objects: prepare() computes
/// both once (any thread; the GUI calls it in its job, so getRoots never walks the graph in the GUI thread),
/// otherwise the first getRoots / getRootCount of such a view computes them. A copy of the inspector shares
/// them. Package P1a.
class PDF4QTLIBCORESHARED_EXPORT PDFObjectInspector
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFObjectInspector)

public:
    /// Deepest path of the tree (B158, B207: the old tree had no limit)
    static constexpr int MAXIMUM_PATH_DEPTH = 256;

    /// Most decoded bytes of a stream (R7-3, Q7.18: PDFLimitedStreamDecoder)
    static constexpr qint64 INSPECTOR_MAXIMUM_DECODED_SIZE = PDFLimitedStreamDecoder::INSPECTOR_MAXIMUM_DECODED_SIZE;

    /// Most bytes shown of a decoded stream ("First 1 MB shown", FD-5)
    static constexpr qint64 MAXIMUM_SHOWN_BYTES = qint64(1024) * 1024;

    /// Most bytes of a string or a name in its describe text (the rest is cut before the text is made and
    /// marked by "…"; a hostile string of a file can have hundreds of megabytes)
    static constexpr qint64 MAXIMUM_DESCRIBED_BYTES = 4096;

    /// Most pixels of a picture of getImage (256 Mi pixels: 1 GiB of the decoded picture, SECURITY_TODO.md 2)
    static constexpr qint64 MAXIMUM_IMAGE_PIXELS = qint64(256) * 1024 * 1024;

    /// \param document The document (a snapshot shared with the tab; may be null = an empty inspector)
    explicit PDFObjectInspector(PDFDocumentPointer document);

    /// The document of the inspector
    const PDFDocumentPointer& getDocument() const { return m_document; }

    /// Classifies the document and lists its objects, once (see the class; any thread, also concurrently
    /// with another call of it). False, when it was canceled: the next call computes it again.
    /// \param control Cancellation (optional)
    bool prepare(const PDFOperationControl* control) const;

    /// The references opened in one view - its one visited set, owned by the caller (the model of the view
    /// of the GUI, one run of MCP): a reference in it is a repeated node elsewhere
    using OpenedReferences = std::set<PDFObjectReference>;

    /// The roots of the view (each indirect object at most once); their references go to \p opened
    std::vector<PDFObjectInspectorNode> getRoots(PDFObjectInspectorView view, OpenedReferences* opened) const;

    /// The roots \p offset to \p offset + \p count - 1 of the view (the Object List of a large document is
    /// fetched in parts; MCP offset / limit); their references go to \p opened (except the Object List,
    /// whose references are never followed)
    std::vector<PDFObjectInspectorNode> getRoots(PDFObjectInspectorView view, OpenedReferences* opened, qint64 offset, qint64 count) const;

    /// Number of the roots of the view (the counts of the menu of the views, 482:15054)
    qint64 getRootCount(PDFObjectInspectorView view) const;

    /// The children of a node (empty for a leaf, a repeated reference or a node at the depth limit): a child,
    /// whose reference is in \p opened, is a repeated node; the others are added to \p opened
    std::vector<PDFObjectInspectorNode> getChildren(const PDFObjectInspectorNode& node, OpenedReferences* opened) const;

    /// The children \p offset to \p offset + \p count - 1 of a node (an array of millions of items of a
    /// hostile file is fetched in parts, never all its nodes at once)
    std::vector<PDFObjectInspectorNode> getChildren(const PDFObjectInspectorNode& node, OpenedReferences* opened, qint64 offset, qint64 count) const;

    /// The number of the children of a node (0 for a leaf, a repeated reference or a node at the depth limit)
    static qint64 getChildCount(const PDFObjectInspectorNode& node);

    /// Text of the description of an object for the right panel (Information, Description), numbers in the
    /// C locale
    QString describe(const PDFObjectInspectorNode& node) const;

    /// Text of the description with the numbers by \p formatter ("Stream [2 items, 4,806 data bytes]";
    /// nullptr = the C locale; the GUI passes PDFAppToolValueFormatter). An image XObject (also without
    /// /Type, B185) adds its size and bits from its dictionary; nothing is decoded.
    QString describe(const PDFObjectInspectorNode& node, const PDFDocumentToolValueFormatter* formatter) const;

    /// The PDF syntax of the object of a node ("PDF syntax of the object", 482:14847): a dictionary with one
    /// entry on a line, a stream as its dictionary (its data are decodeStream); at most MAXIMUM_SHOWN_BYTES
    /// characters (\p isTruncated). A string with other than printable characters is written in hex.
    /// \param node The node
    /// \param isTruncated The text was cut (optional)
    QString getSyntax(const PDFObjectInspectorNode& node, bool* isTruncated) const;

    /// The text of a reference ("12 0 R")
    static QString getReferenceText(PDFObjectReference reference);

    /// The data of a stream are binary (shown as a hex dump, Q7.12): a control character other than a tab,
    /// a line feed, a form feed or a carriage return
    static bool isBinary(const QByteArray& data);

    /// The text of data, which are not binary: UTF-8 when they are valid UTF-8, else Latin-1
    static QString toText(const QByteArray& data);

    /// A hex dump of binary data (Q7.12): lines of 16 bytes "00000010  25 50 44 46 ...  |%PDF...|"
    static QString createHexDump(const QByteArray& data);

    /// The object stream of each object of an object stream (FD-6): the object number and the object
    /// stream, read from the headers of the object streams of the storage (decoded by
    /// PDFLimitedStreamDecoder - any thread, never the GUI one). When more object streams list an object
    /// (an incremental update), the last one in the storage wins.
    using ObjectStreams = std::map<PDFInteger, PDFObjectReference>;
    static ObjectStreams findObjectStreams(const PDFDocument* document, const PDFOperationControl* control);

    /// The object streams of the nodes (objectStreamNote, objectStream); none by default
    void setObjectStreams(ObjectStreams objectStreams);

    /// The color management of the pictures of getImage (the CMS of the tab); null = PDFCMSGeneric
    void setCMS(PDFCMSPointer cms) { m_cms = std::move(cms); }
    const PDFCMSPointer& getCMS() const { return m_cms; }

    /// What the decoding of a stream gives
    class DecodedStream
    {
    public:
        PDFLimitedStreamDecoder::Status status = PDFLimitedStreamDecoder::Status::Error;
        QByteArray data;            ///< At most MAXIMUM_SHOWN_BYTES of the decoded data (or of the data encoded by filterName)
        qint64 decodedSize = 0;     ///< Size of the whole decoded data (not only the shown part)
        bool isBinary = false;      ///< The data are shown as a hex dump (Q7.12)
        bool isTruncated = false;   ///< Only the first MAXIMUM_SHOWN_BYTES are in \p data
        QByteArray filterName;      ///< The filter, which failed or stopped the decoding
        QString errorMessage;       ///< Translated message (482:16403 with the name of the filter)
    };

    /// Decodes the stream of the indirect object (any thread, see the class)
    /// \param reference The stream object
    /// \param maximumDecodedSize The limit of the decoded size (INSPECTOR_MAXIMUM_DECODED_SIZE; MCP its maxBytes bound)
    /// \param control Cancellation (optional)
    DecodedStream decodeStream(PDFObjectReference reference, qint64 maximumDecodedSize, const PDFOperationControl* control) const;

    /// The picture of an image XObject (any thread); a null image and the reason in \p errorMessage
    /// \param reference The image object
    /// \param control Cancellation (optional)
    /// \param errorMessage Translated reason (optional)
    QImage getImage(PDFObjectReference reference, const PDFOperationControl* control, QString* errorMessage) const;

    /// The identifier of a view ("content-streams"), the view of an identifier (std::nullopt = unknown)
    static QString getViewId(PDFObjectInspectorView view);
    static std::optional<PDFObjectInspectorView> findView(const QString& id);

    /// Every view in the order of the menu (482:15054)
    static std::vector<PDFObjectInspectorView> getViews();

    /// The class of the classifier of a view (None for Document, Pages and the Object List)
    static PDFObjectClassifier::Type getClassOfView(PDFObjectInspectorView view);

private:
    /// What prepare() computes, shared by the copies of the inspector
    class Cache
    {
    public:
        std::mutex mutex;
        bool isPrepared = false;
        PDFObjectClassifier classifier;
        std::vector<PDFInteger> objectNumbers;  ///< The objects of the Object List (not null), in the order of their numbers
        std::vector<PDFObjectReference> pages;  ///< The pages, each once
    };

    /// The cache, prepared (see prepare)
    const Cache* getCache() const;

    /// A node of an indirect object or of a direct value contained in \p container
    PDFObjectInspectorNode createNode(PDFObjectReference reference, const PDFObject& object, PDFObjectReference container, int depth, bool isFollowingReferences, OpenedReferences* opened) const;

    /// The node of the trailer
    PDFObjectInspectorNode createTrailerNode(bool isFollowingReferences) const;

    /// The references of the roots of a view (not the trailer)
    std::vector<PDFObjectReference> getRootReferences(PDFObjectInspectorView view) const;

    PDFDocumentPointer m_document;
    std::shared_ptr<Cache> m_cache;
    ObjectStreams m_objectStreams;
    PDFCMSPointer m_cms;
};

}   // namespace pdf

#endif // PDFOBJECTINSPECTOR_H
