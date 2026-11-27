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

// Edit Content, stage E0, package P02 (tree and state): the builder of the content tree
// (GUI_overhaul/edit_tool/zadani_jadro.md 4.2, 4.3; zadani_overeni.md 6.1 - 6.3).
//
// How the tree is made. The processor interprets the content; the builder listens to every
// operation of the content of the container (never of a glyph procedure, a form or a pattern cell)
// before and after it is executed:
//
//   before  the operation is added to the list (its bytes, operands, resource names) and gets
//           its place in the tree: it opens or closes a node, is a leaf, or joins the leaf being
//           collected (a state run, a path, an opaque span);
//   after   the symbolic state follows the operation, the state after it is compared with the
//           last shared state (a new one is made only when something differs) and the node,
//           which the operation has finished, gets its leak and goes to its parent.
//
// The nodes are plain build nodes first; when the content has ended, they are numbered by
// a depth first walk and handed to PDFContentContainerEditor, which makes the immutable container.
//
// Ownership inside of a build: the processor object owns the build nodes (the open ones are on
// the stack of frames, each finished node is owned by its parent), the operation list and the
// issues; everything is local to the call of PDFContentTreeBuilder::build and to its thread.

#include "pdfcontenttreebuilder.h"

#include "pdfcatalog.h"
#include "pdfconstants.h"
#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfobject.h"
#include "pdfoperationcontrol.h"
#include "pdfpage.h"
#include "pdfpagecontentprocessor.h"
#include "pdfparser.h"
#include "pdfpattern.h"
#include "pdfprogress.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QRectF>
#include <QString>
#include <QTransform>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace pdf
{

/// What a key of a container names in a document: the decoded streams of the content and the
/// values of its dictionary, which the builder and the fingerprint need.
///
/// Threads and ownership: a local value of one call; \p page and \p stream point into the
/// document, which outlives the call.
class PDFContentTreeBuilderSource
{
public:
    /// One stream of the content
    struct Stream
    {
        QByteArray data;            ///< Decoded data; empty, when the stream cannot be decoded
        bool isDecoded = false;
    };

    /// The decoded bytes of all streams of a content together, at most (each item of /Contents
    /// counts, also when it names a stream again). A container keeps every byte of its content:
    /// in the decoded streams while it is built and in the operations afterwards (and once more,
    /// when the content is one operation above the operation limit). /Contents can name one small
    /// stream any number of times, so without a limit a file of a few kilobytes makes a content
    /// of any size. Above the limit the content is not read at all: the container is empty and
    /// read-only (shown by the renderer, never changed, never written). 512 MB is far above the
    /// content of a real page (a content of the 5 000 000 operations of
    /// PDFContentContainer::MAXIMUM_OPERATION_COUNT has some tens of megabytes).
    static constexpr qint64 MAXIMUM_CONTENT_SIZE = qint64(512) * 1024 * 1024;

    bool isValid = false;                   ///< The key names a content of the document
    QString error;                          ///< Why it does not
    PDFContentContainerKey key;
    const PDFPage* page = nullptr;          ///< Kind::Page: the page
    const PDFStream* stream = nullptr;      ///< Kind::Form, Kind::PatternCell: the stream
    bool isUncoloredPatternCell = false;    ///< Kind::PatternCell: the pattern is uncolored (/PaintType 2), its cell specifies no color
    std::vector<Stream> streams;           ///< The streams of the content in their order (a page: the items of /Contents)
    bool isReadOnly = false;                ///< A stream cannot be decoded, /Contents is neither a stream nor an array, or the content is larger than MAXIMUM_CONTENT_SIZE
    bool isOverSize = false;                ///< The content is larger than MAXIMUM_CONTENT_SIZE: no stream has data, the content is not interpreted
    PDFObject resources;                    ///< /Resources of the content as they are in its dictionary (a page: inherited from the page tree); null = none of its own (also for a reference to nothing or to null)
    QRectF boundingBox;                     ///< The media box of the page, /BBox of the form or of the pattern
    QTransform matrix;                      ///< /Matrix of the form or of the pattern
    PDFInteger structuralParentKey = -1;    ///< /StructParents, -1 = none
    PDFObject transparencyGroup;            ///< /Group of the form (dereferenced)

    /// Reads, what the key names. It never throws: a dictionary, which cannot be read (/Matrix of
    /// a form, which is not six numbers), gives a source, which is not valid, with the reason
    /// (the worker of a session has nothing, which would catch an exception).
    static PDFContentTreeBuilderSource read(const PDFDocument* document, const PDFContentContainerKey& key);

    /// The whole content: the streams joined by one line feed (ISO 32000-2, 7.8.2)
    QByteArray getContent() const;

    /// The fingerprint of the content (zadani_jadro.md 4.2, 4.6): a hash of the decoded bytes of
    /// the streams and of the references of the resource dictionaries
    QByteArray computeFingerprint(const PDFDocument* document) const;

private:
    /// The decoded data of the streams read so far by their references: an item of /Contents,
    /// which names a stream again, shares its data (implicitly shared bytes) and is not decoded
    /// again
    using DecodedStreams = std::map<PDFObjectReference, Stream>;

    /// read without its promise: the functions, which read a dictionary, may throw
    static PDFContentTreeBuilderSource readUnchecked(const PDFDocument* document, const PDFContentContainerKey& key);

    void addStream(const PDFDocument* document, const PDFObject& object, DecodedStreams& decodedStreams);

    /// The content is larger than MAXIMUM_CONTENT_SIZE: the data of all streams are released
    void setOverSize();

    qint64 m_contentSize = 0;               ///< The decoded bytes of the streams added so far

    static void hashInteger(QCryptographicHash& hash, qint64 value);
    static void hashBytes(QCryptographicHash& hash, const QByteArray& bytes);
    static void hashReference(QCryptographicHash& hash, PDFObjectReference reference);
    static void hashObject(QCryptographicHash& hash, const PDFObject& object, int depth);
    static void hashResources(QCryptographicHash& hash, const PDFDocument* document, const PDFObject& resources);

    /// Direct objects nested deeper are not hashed (a direct object cannot contain itself, but
    /// its depth is given by the document)
    static constexpr int MAXIMUM_HASHED_OBJECT_DEPTH = 32;
};

PDFContentTreeBuilderSource PDFContentTreeBuilderSource::read(const PDFDocument* document, const PDFContentContainerKey& key)
{
    // The functions, which read the dictionary of a form or of a pattern, throw for a value
    // they cannot read (readMatrixFromDictionary for a /Matrix, which is not six numbers)
    QString error;

    try
    {
        return readUnchecked(document, key);
    }
    catch (const PDFException& exception)
    {
        error = exception.getMessage();
    }
    catch (const PDFRendererException& exception)
    {
        error = exception.getError().message;
    }

    PDFContentTreeBuilderSource source;
    source.key = key;
    source.error = PDFTranslationContext::tr("The dictionary of the content cannot be read: %1").arg(error);
    return source;
}

PDFContentTreeBuilderSource PDFContentTreeBuilderSource::readUnchecked(const PDFDocument* document, const PDFContentContainerKey& key)
{
    PDFContentTreeBuilderSource source;
    source.key = key;

    if (!document || !key.isValid())
    {
        source.error = PDFTranslationContext::tr("The content does not exist in the document.");
        return source;
    }

    PDFDocumentDataLoaderDecorator loader(document);
    DecodedStreams decodedStreams;

    switch (key.kind)
    {
        case PDFContentContainerKey::Kind::Page:
        {
            const PDFCatalog* catalog = document->getCatalog();
            if (key.pageIndex < 0 || size_t(key.pageIndex) >= catalog->getPageCount() ||
                catalog->getPage(size_t(key.pageIndex))->getPageReference() != key.reference)
            {
                source.error = PDFTranslationContext::tr("The page does not exist in the document.");
                return source;
            }

            source.page = catalog->getPage(size_t(key.pageIndex));

            const PDFObject& contents = document->getObject(source.page->getContents());
            if (contents.isArray())
            {
                const PDFArray* array = contents.getArray();
                const size_t count = array->getCount();
                source.streams.reserve(count);
                for (size_t i = 0; i < count; ++i)
                {
                    source.addStream(document, array->getItem(i), decodedStreams);
                }
            }
            else if (contents.isStream())
            {
                source.addStream(document, source.page->getContents(), decodedStreams);
            }
            else if (!contents.isNull())
            {
                // Something else than a content: it must not be replaced by a written one
                source.isReadOnly = true;
            }

            source.resources = source.page->getResources();
            source.boundingBox = source.page->getMediaBox();

            if (const PDFDictionary* pageDictionary = document->getDictionaryFromObject(document->getObjectByReference(key.reference)))
            {
                source.structuralParentKey = loader.readIntegerFromDictionary(pageDictionary, "StructParents", -1);
            }
            break;
        }

        case PDFContentContainerKey::Kind::Form:
        case PDFContentContainerKey::Kind::PatternCell:
        {
            const PDFObject& object = document->getObjectByReference(key.reference);
            if (!object.isStream())
            {
                source.error = PDFTranslationContext::tr("The object of the content is not a stream.");
                return source;
            }

            const PDFStream* stream = object.getStream();
            const PDFDictionary* dictionary = stream->getDictionary();

            if (key.kind == PDFContentContainerKey::Kind::Form)
            {
                if (loader.readNameFromDictionary(dictionary, "Subtype") != "Form")
                {
                    source.error = PDFTranslationContext::tr("The object is not a form XObject.");
                    return source;
                }

                source.structuralParentKey = loader.readIntegerFromDictionary(dictionary, "StructParents", -1);
                source.transparencyGroup = document->getObject(dictionary->get("Group"));
            }
            else
            {
                if (loader.readIntegerFromDictionary(dictionary, "PatternType", 0) != 1)
                {
                    source.error = PDFTranslationContext::tr("The object is not a tiling pattern.");
                    return source;
                }

                // The paint type (ISO 32000-2, Table 74), read as the processor reads it, when it
                // creates the pattern (PDFPattern::createPattern): an integer 1 (colored) or 2
                // (uncolored). Anything else - no key, another number, a value of another type -
                // is refused there, such a pattern is never painted: it is no container.
                const PDFInteger paintType = loader.readIntegerFromDictionary(dictionary, "PaintType", 0);
                if (paintType != PDFInteger(PDFTilingPattern::PaintType::Colored) &&
                    paintType != PDFInteger(PDFTilingPattern::PaintType::Uncolored))
                {
                    source.error = PDFTranslationContext::tr("The paint type of the tiling pattern is invalid.");
                    return source;
                }

                source.isUncoloredPatternCell = paintType == PDFInteger(PDFTilingPattern::PaintType::Uncolored);
            }

            source.stream = stream;
            source.addStream(document, object, decodedStreams);
            source.resources = dictionary->get("Resources");
            if (document->getObject(source.resources).isNull())
            {
                // A reference to nothing or to null is the null object (ISO 32000-2, 7.3.10):
                // no resources of its own, as a missing entry (7.3.7) - the content uses the
                // resources of the page, as the renderer and the document writer read it
                source.resources = PDFObject();
            }
            source.boundingBox = loader.readRectangle(dictionary->get("BBox"), QRectF());
            source.matrix = loader.readMatrixFromDictionary(dictionary, "Matrix", QTransform());
            break;
        }
    }

    source.isValid = true;
    return source;
}

void PDFContentTreeBuilderSource::addStream(const PDFDocument* document, const PDFObject& object, DecodedStreams& decodedStreams)
{
    if (isOverSize)
    {
        // Nothing more is decoded; the item keeps its place among the streams
        streams.push_back(Stream());
        return;
    }

    Stream item;

    // A stream named again by /Contents is decoded once: its items share the bytes
    const auto it = object.isReference() ? decodedStreams.find(object.getReference()) : decodedStreams.end();
    if (it != decodedStreams.end())
    {
        item = it->second;
    }
    else
    {
        const PDFObject& dereferencedObject = document->getObject(object);
        if (dereferencedObject.isStream())
        {
            try
            {
                item.data = document->getDecodedStream(dereferencedObject.getStream());
                item.isDecoded = true;
            }
            catch (const PDFException&)
            {
                item.data = QByteArray();
            }
        }

        if (object.isReference())
        {
            decodedStreams[object.getReference()] = item;
        }
    }

    if (!item.isDecoded)
    {
        isReadOnly = true;
    }

    // Each item counts with its whole size: the content has its bytes at the place of each
    m_contentSize += item.data.size();
    streams.push_back(std::move(item));

    if (m_contentSize > MAXIMUM_CONTENT_SIZE)
    {
        decodedStreams.clear();
        setOverSize();
    }
}

void PDFContentTreeBuilderSource::setOverSize()
{
    isOverSize = true;
    isReadOnly = true;

    for (Stream& item : streams)
    {
        item = Stream();
    }
}

QByteArray PDFContentTreeBuilderSource::getContent() const
{
    QByteArray content;
    for (size_t i = 0; i < streams.size(); ++i)
    {
        if (i > 0)
        {
            content.append('\n');
        }
        content.append(streams[i].data);
    }
    return content;
}

QByteArray PDFContentTreeBuilderSource::computeFingerprint(const PDFDocument* document) const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView("PDF4QT content fingerprint 1"));
    hashInteger(hash, qint64(key.kind));
    hashInteger(hash, qint64(streams.size()));
    for (const Stream& item : streams)
    {
        hashInteger(hash, item.isDecoded ? 1 : 0);
        hashBytes(hash, item.data);
    }
    hashInteger(hash, isReadOnly ? 1 : 0);
    hashResources(hash, document, resources);
    return hash.result();
}

void PDFContentTreeBuilderSource::hashInteger(QCryptographicHash& hash, qint64 value)
{
    // The bytes of the number in a fixed order, whatever the machine
    char bytes[8];
    quint64 unsignedValue = quint64(value);
    for (char& byte : bytes)
    {
        byte = char(unsignedValue & 0xFF);
        unsignedValue >>= 8;
    }
    hash.addData(QByteArrayView(bytes, sizeof(bytes)));
}

void PDFContentTreeBuilderSource::hashBytes(QCryptographicHash& hash, const QByteArray& bytes)
{
    // The length first: two texts in a row never give the bytes of two other texts
    hashInteger(hash, bytes.size());
    hash.addData(QByteArrayView(bytes));
}

void PDFContentTreeBuilderSource::hashReference(QCryptographicHash& hash, PDFObjectReference reference)
{
    hash.addData(QByteArrayView("R"));
    hashInteger(hash, reference.objectNumber);
    hashInteger(hash, reference.generation);
}

void PDFContentTreeBuilderSource::hashObject(QCryptographicHash& hash, const PDFObject& object, int depth)
{
    if (depth > MAXIMUM_HASHED_OBJECT_DEPTH)
    {
        hash.addData(QByteArrayView("?"));
        return;
    }

    switch (object.getType())
    {
        case PDFObject::Type::Null:
            hash.addData(QByteArrayView("N"));
            break;

        case PDFObject::Type::Bool:
            hash.addData(QByteArrayView(object.getBool() ? "T" : "F"));
            break;

        case PDFObject::Type::Int:
            hash.addData(QByteArrayView("I"));
            hashInteger(hash, object.getInteger());
            break;

        case PDFObject::Type::Real:
            hash.addData(QByteArrayView("D"));
            hashBytes(hash, QByteArray::number(object.getReal(), 'g', 17));
            break;

        case PDFObject::Type::String:
            hash.addData(QByteArrayView("S"));
            hashBytes(hash, object.getString());
            break;

        case PDFObject::Type::Name:
            hash.addData(QByteArrayView("/"));
            hashBytes(hash, object.getString());
            break;

        case PDFObject::Type::Array:
        {
            const PDFArray* array = object.getArray();
            hash.addData(QByteArrayView("["));
            hashInteger(hash, qint64(array->getCount()));
            for (size_t i = 0; i < array->getCount(); ++i)
            {
                hashObject(hash, array->getItem(i), depth + 1);
            }
            break;
        }

        case PDFObject::Type::Dictionary:
        {
            const PDFDictionary* dictionary = object.getDictionary();
            hash.addData(QByteArrayView("<"));
            hashInteger(hash, qint64(dictionary->getCount()));
            for (size_t i = 0; i < dictionary->getCount(); ++i)
            {
                hashBytes(hash, dictionary->getKey(i).getString());
                hashObject(hash, dictionary->getValue(i), depth + 1);
            }
            break;
        }

        case PDFObject::Type::Stream:
            // A stream is always an indirect object in a file: a direct one has no place there
            hash.addData(QByteArrayView("s"));
            break;

        case PDFObject::Type::Reference:
            hashReference(hash, object.getReference());
            break;
    }
}

void PDFContentTreeBuilderSource::hashResources(QCryptographicHash& hash, const PDFDocument* document, const PDFObject& resources)
{
    if (resources.isReference())
    {
        hashReference(hash, resources.getReference());
    }

    const PDFDictionary* dictionary = document->getDictionaryFromObject(resources);
    if (!dictionary)
    {
        hash.addData(QByteArrayView("-"));
        return;
    }

    // The names of the resources and what they refer to: an identifier of a node and a name used
    // by an operation mean the same objects, as long as these are the same
    for (const PDFContentResourceCategory category : PDFContentResources::getCategories())
    {
        const QByteArray categoryKey = PDFContentResources::getCategoryKey(category);
        const PDFObject& categoryObject = dictionary->get(categoryKey);

        hashBytes(hash, categoryKey);
        if (categoryObject.isReference())
        {
            hashReference(hash, categoryObject.getReference());
        }

        const PDFDictionary* categoryDictionary = document->getDictionaryFromObject(categoryObject);
        if (!categoryDictionary)
        {
            hash.addData(QByteArrayView("-"));
            continue;
        }

        hashInteger(hash, qint64(categoryDictionary->getCount()));
        for (size_t i = 0; i < categoryDictionary->getCount(); ++i)
        {
            hashBytes(hash, categoryDictionary->getKey(i).getString());
            hashObject(hash, categoryDictionary->getValue(i), 0);
        }
    }
}

/// The names, which content without /Resources of its own drawn by a content uses (item 5 of
/// PB0S3, A1L2-4; PDFContentResources::getReservedNames). Such content - a form XObject, a tiling
/// pattern, a Type 3 font - takes its names from the content, which draws it, and a name that
/// content does not define from the page (NOTES.txt 7.8.3). A name added to the resources of the
/// content being built must not become one of them:
///  - a page: every such content drawn on the page at any depth (through forms, patterns and
///    fonts with resources of their own too: their inheriting content takes a name they do not
///    define from the page);
///  - a form or a pattern cell: such content, which it draws directly or through other such
///    content (content with resources of its own is the caller of what it draws).
/// The walk reads the dictionaries; it decodes and scans only the streams of the content without
/// resources, whose every name token is taken (the operators are not interpreted: more names than
/// needed, never fewer, as far as the walk goes).
///
/// Bounds (hostile input): one visited set of the references of the whole walk, the depth
/// MAXIMUM_DEPTH, MAXIMUM_ITEMS objects looked at, MAXIMUM_SCANNED_CONTENTS contents (a form, a
/// cell, each glyph procedure of a Type 3 font, also one, which is no stream), MAXIMUM_SCANNED_BYTES
/// decoded bytes and MAXIMUM_NAMES names; beyond them the walk stops (the names found so far are
/// reserved, the rest is not known). The cost is linear in what the walk reads (review R2-5): the
/// names of a scanned content are looked up in an index of each dictionary, made once per walk
/// (not a loop over the dictionary per content).
///
/// The walk runs at each build of a container (each build reads the document anew); it is not
/// cached between builds: its inputs (the document, the resources of the page and of the caller)
/// belong to the session, and a cache keyed by them could go stale silently.
///
/// Threads and ownership: static functions; called by the builder in its thread, reads the
/// document only.
class PDFContentTreeBuilderReservedNames
{
public:
    PDFContentTreeBuilderReservedNames() = delete;

    static constexpr int MAXIMUM_DEPTH = 32;
    static constexpr size_t MAXIMUM_ITEMS = 65536;
    static constexpr size_t MAXIMUM_SCANNED_CONTENTS = 1024;
    static constexpr qint64 MAXIMUM_SCANNED_BYTES = qint64(64) * 1024 * 1024;
    static constexpr size_t MAXIMUM_NAMES = 65536;

    /// The names of the content without resources the content built draws.
    /// \param document The document
    /// \param pageResources The resources of the page of the content (dereferenced or not)
    /// \param resources The resources the content was interpreted with (its own, the merged ones
    ///        of a content without them, those of the page)
    /// \param isPage The content is a page
    /// \param operations The operations of the content: the forms, patterns and fonts they name
    ///        are the beginning of the walk
    static std::set<QByteArray> collect(const PDFDocument* document,
                                        const PDFObject& pageResources,
                                        const PDFObject& resources,
                                        bool isPage,
                                        const PDFContentOperationList& operations);

    /// The names, which are not keys of the dictionary (nullptr: all of them). The keys are read
    /// once (linear in the dictionary), each name is looked up in them.
    static std::set<QByteArray> getNamesNotIn(const std::set<QByteArray>& names, const PDFDictionary* dictionary);

private:
    struct Item
    {
        PDFObject object;           ///< A form, a pattern, a font (a reference or a direct object)
        PDFObject callerResources;  ///< The resources of the content, which draws it
        int depth = 0;
    };

    /// Key -> the index of its first entry (PDFDictionary::get finds the first one)
    using DictionaryIndex = std::map<QByteArray, size_t>;

    /// The dictionaries of the document, which the walk looked a name up in, with their indices.
    /// The dictionaries are owned by the document (or by a resource dictionary of it), which
    /// outlives the walk: their addresses do not change during it.
    using DictionaryIndices = std::map<const PDFDictionary*, DictionaryIndex>;

    /// The dictionary of a category of a resource dictionary (dereferenced), nullptr when none
    static const PDFDictionary* getCategory(const PDFDocument* document, const PDFObject& resources, const char* key);

    /// The index of a dictionary, made at the first call for it
    static const DictionaryIndex& getIndex(DictionaryIndices& indices, const PDFDictionary* dictionary);

    /// Adds every name token of a content stream to \p names (and to \p contentNames); false,
    /// when a bound is reached
    static bool scan(const QByteArray& content, std::set<QByteArray>& names, std::set<QByteArray>& contentNames);
};

std::set<QByteArray> PDFContentTreeBuilderReservedNames::collect(const PDFDocument* document,
                                                                 const PDFObject& pageResources,
                                                                 const PDFObject& resources,
                                                                 bool isPage,
                                                                 const PDFContentOperationList& operations)
{
    static const char* const drawnCategories[] = { "XObject", "Pattern", "Font" };

    std::set<QByteArray> names;
    std::vector<Item> queue;
    std::set<PDFObjectReference> visited;
    DictionaryIndices indices;

    // The beginning: what the operations name in the three categories
    std::set<std::pair<PDFContentResourceCategory, QByteArray>> usedNames;
    for (size_t i = 0; i < operations.getCount(); ++i)
    {
        for (const PDFContentResourceName& name : operations.getOperation(i).getResourceNames())
        {
            if (name.category == PDFContentResourceCategory::XObject || name.category == PDFContentResourceCategory::Pattern || name.category == PDFContentResourceCategory::Font)
            {
                usedNames.emplace(name.category, name.name);
            }
        }
    }
    for (const auto& usedName : usedNames)
    {
        if (const PDFDictionary* dictionary = getCategory(document, resources, PDFContentResources::getCategoryKey(usedName.first).constData()))
        {
            const DictionaryIndex& index = getIndex(indices, dictionary);
            auto it = index.find(usedName.second);
            if (it != index.cend() && !dictionary->getValue(it->second).isNull() && queue.size() < MAXIMUM_ITEMS)
            {
                queue.push_back(Item{ dictionary->getValue(it->second), resources, 1 });
            }
        }
    }

    size_t scannedContents = 0;
    qint64 scannedBytes = 0;
    for (size_t i = 0; i < queue.size(); ++i)
    {
        const Item item = queue[i];
        if (item.depth > MAXIMUM_DEPTH || (item.object.isReference() && !visited.insert(item.object.getReference()).second))
        {
            continue;
        }

        // The content streams and the resources of the object: a form or a tiling pattern (one
        // stream), a Type 3 font (its glyph procedures); anything else draws nothing
        const PDFObject& object = document->getObject(item.object);
        bool isForm = false;
        const PDFDictionary* procedures = nullptr;
        PDFObject ownResources;
        if (object.isStream())
        {
            const PDFDictionary* dictionary = object.getStream()->getDictionary();
            const PDFObject& subtype = document->getObject(dictionary->get("Subtype"));
            const PDFObject& patternType = document->getObject(dictionary->get("PatternType"));
            if ((subtype.isName() && subtype.getString() == "Form") || (patternType.isInt() && patternType.getInteger() == 1))
            {
                isForm = true;
                ownResources = document->getObject(dictionary->get("Resources"));
            }
        }
        else if (object.isDictionary())
        {
            const PDFDictionary* dictionary = object.getDictionary();
            const PDFObject& subtype = document->getObject(dictionary->get("Subtype"));
            if (subtype.isName() && subtype.getString() == "Type3")
            {
                procedures = document->getDictionaryFromObject(dictionary->get("CharProcs"));
                ownResources = document->getObject(dictionary->get("Resources"));
            }
        }

        const size_t contentCount = isForm ? 1 : (procedures ? procedures->getCount() : 0);
        if (contentCount == 0)
        {
            continue;
        }

        if (!ownResources.isNull())
        {
            // A caller of its own: what it draws without resources takes its names first and a
            // name it lacks from the page - the walk of a page goes on, the walk of another
            // content ends here
            if (isPage)
            {
                for (const char* key : drawnCategories)
                {
                    if (const PDFDictionary* dictionary = getCategory(document, ownResources, key))
                    {
                        for (size_t j = 0; j < dictionary->getCount() && queue.size() < MAXIMUM_ITEMS; ++j)
                        {
                            queue.push_back(Item{ dictionary->getValue(j), ownResources, item.depth + 1 });
                        }
                    }
                }
            }
            continue;
        }

        // Without resources: every name of its streams is reserved; what it draws is looked up
        // in the page and in its caller (as the processor looks it up)
        std::set<QByteArray> contentNames;
        bool isComplete = true;
        for (size_t j = 0; j < contentCount; ++j)
        {
            // Every content counts, also a glyph procedure, which is no stream: a font with
            // many such entries drawn by many contents costs no more than the bound
            if (++scannedContents > MAXIMUM_SCANNED_CONTENTS)
            {
                isComplete = false;
                break;
            }

            const PDFObject& stream = document->getObject(isForm ? item.object : procedures->getValue(j));
            if (!stream.isStream())
            {
                continue;
            }

            QByteArray data;
            try
            {
                data = document->getDecodedStream(stream.getStream());
            }
            catch (const PDFException&)
            {
                continue;
            }

            scannedBytes += data.size();
            if (scannedBytes > MAXIMUM_SCANNED_BYTES || !scan(data, names, contentNames))
            {
                isComplete = false;
                break;
            }
        }

        // Each name looked up in the index of the dictionary: the cost is the number of the names
        // of the content (bounded by the decoded bytes), not the size of the dictionary
        for (const char* key : drawnCategories)
        {
            const PDFDictionary* pageDictionary = nullptr;
            for (const PDFObject* lookupResources : { &pageResources, &item.callerResources })
            {
                const PDFDictionary* dictionary = getCategory(document, *lookupResources, key);
                if (!dictionary || contentNames.empty() || (lookupResources != &pageResources && dictionary == pageDictionary))
                {
                    // (the caller is the page: its entries are in the queue already)
                    continue;
                }
                pageDictionary = lookupResources == &pageResources ? dictionary : pageDictionary;

                const DictionaryIndex& index = getIndex(indices, dictionary);
                for (auto it = contentNames.cbegin(); it != contentNames.cend() && queue.size() < MAXIMUM_ITEMS; ++it)
                {
                    auto itEntry = index.find(*it);
                    if (itEntry != index.cend())
                    {
                        queue.push_back(Item{ dictionary->getValue(itEntry->second), item.callerResources, item.depth + 1 });
                    }
                }
            }
        }

        if (!isComplete)
        {
            break;
        }
    }

    return names;
}

std::set<QByteArray> PDFContentTreeBuilderReservedNames::getNamesNotIn(const std::set<QByteArray>& names, const PDFDictionary* dictionary)
{
    if (!dictionary || names.empty())
    {
        return names;
    }

    std::set<QByteArray> keys;
    for (size_t i = 0; i < dictionary->getCount(); ++i)
    {
        keys.insert(dictionary->getKey(i).getString());
    }

    std::set<QByteArray> result;
    std::set_difference(names.cbegin(), names.cend(), keys.cbegin(), keys.cend(), std::inserter(result, result.end()));
    return result;
}

const PDFDictionary* PDFContentTreeBuilderReservedNames::getCategory(const PDFDocument* document, const PDFObject& resources, const char* key)
{
    const PDFDictionary* dictionary = document->getDictionaryFromObject(resources);
    return dictionary ? document->getDictionaryFromObject(dictionary->get(key)) : nullptr;
}

const PDFContentTreeBuilderReservedNames::DictionaryIndex& PDFContentTreeBuilderReservedNames::getIndex(DictionaryIndices& indices, const PDFDictionary* dictionary)
{
    auto it = indices.find(dictionary);
    if (it == indices.end())
    {
        DictionaryIndex index;
        for (size_t i = 0; i < dictionary->getCount(); ++i)
        {
            // The first entry of a key written twice (PDFDictionary::get)
            index.emplace(dictionary->getKey(i).getString(), i);
        }
        it = indices.emplace(dictionary, std::move(index)).first;
    }
    return it->second;
}

bool PDFContentTreeBuilderReservedNames::scan(const QByteArray& content, std::set<QByteArray>& names, std::set<QByteArray>& contentNames)
{
    using TokenType = PDFLexicalAnalyzer::TokenType;

    PDFLexicalAnalyzer analyzer(content.constData(), content.constData() + content.size());
    try
    {
        while (!analyzer.isAtEnd())
        {
            const PDFLexicalAnalyzer::Token token = analyzer.fetch();
            if (token.type == TokenType::EndOfFile)
            {
                break;
            }

            if (token.type == TokenType::Name)
            {
                if (names.size() >= MAXIMUM_NAMES)
                {
                    return false;
                }

                const QByteArray name = token.data.toByteArray();
                names.insert(name);
                contentNames.insert(name);
            }
            else if (token.type == TokenType::Command && token.data.toByteArray() == "ID")
            {
                // The data of an inline image are no tokens: the scan goes on behind EI, as
                // the processor reads it (white space, EI, white space or the end)
                qsizetype position = analyzer.pos();
                while (true)
                {
                    position = content.indexOf("EI", position);
                    if (position < 0)
                    {
                        return true;
                    }

                    const bool isSpaceBefore = position > 0 && PDFLexicalAnalyzer::isWhitespace(content[position - 1]);
                    const bool isSpaceAfter = position + 2 >= content.size() || PDFLexicalAnalyzer::isWhitespace(content[position + 2]);
                    if (isSpaceBefore && isSpaceAfter)
                    {
                        break;
                    }
                    position += 2;
                }
                analyzer.seek(position + 2);
            }
        }
    }
    catch (const PDFException&)
    {
        // A stream, which is no content, has no more names
    }

    return true;
}

/// Cancels the processing, when the caller cancels it, or when the builder has seen more
/// operations than a container can have (the rest of the content is not interpreted then).
///
/// Threads and ownership: owned by the processor of the build; the control of the caller must
/// outlive the build. The flag is atomic, the control can be asked by any thread.
class PDFContentTreeBuilderControl : public PDFOperationControl
{
public:
    explicit PDFContentTreeBuilderControl(const PDFOperationControl* callerControl) :
        m_callerControl(callerControl)
    {

    }

    virtual bool isOperationCanceled() const override
    {
        return m_isStopped.load() || isCanceledByCaller();
    }

    bool isCanceledByCaller() const { return m_callerControl && m_callerControl->isOperationCanceled(); }
    void stop() { m_isStopped.store(true); }

private:
    const PDFOperationControl* m_callerControl;
    std::atomic<bool> m_isStopped = false;
};

/// The processor of a build: it interprets the content and makes the tree of its nodes.
///
/// Threads and ownership: a local object of PDFContentTreeBuilder::build (one thread). It owns
/// the build nodes, the operations and the issues, until createResult moves them into the
/// container. It reads the document and the caches of the context, which outlive the call.
class PDFContentTreeBuilderProcessor : public PDFPageContentProcessor
{
public:
    explicit PDFContentTreeBuilderProcessor(const PDFContentTreeBuilderSource* source,
                                            const PDFPage* page,
                                            const PDFDocument* document,
                                            const PDFContentBuildContext& context,
                                            const PDFContentBuildOptions& options,
                                            const PDFOperationControl* control);

    /// Interprets the content
    /// \param initialState The state at the beginning of a form (nullptr = the default state)
    void run(const PDFContentResolvedStatePointer& initialState);

    /// The caller has canceled the build
    bool isCanceledByCaller() const { return m_control.isCanceledByCaller(); }

    /// Makes the container and the lists of the result. The processor is not used afterwards.
    PDFContentBuildResult createResult();

protected:
    virtual void performInterceptInstruction(Operator currentOperator, ProcessOrder processOrder, const QByteArray& operatorAsText) override;
    virtual void performOperatorFailed(Operator currentOperator, const PDFRenderError& error) override;
    virtual void performType3GlyphBegin(const PDFType3Font* font, unsigned int code, const QByteArray& content) override;
    virtual void performType3GlyphEnd() override;
    virtual void performMarkedContentPoint(const QByteArray& tag, const PDFObject& properties) override;
    virtual void performMarkedContentBegin(const QByteArray& tag, const PDFObject& properties) override;
    virtual void performRestoreGraphicState(ProcessOrder order) override;
    virtual bool isContentKindSuppressed(ContentKind kind) const override;
    virtual bool isContentSuppressedByOC(PDFObjectReference ocgOrOcmd) override;
    virtual bool isTilingPatternProcessingAllowed(PDFInteger tileCount) const override;

private:
    using Operands = PDFContentOperation::Operands;
    using Property = PDFContentStateProperty;
    using TokenType = PDFLexicalAnalyzer::TokenType;

    /// More issues are not recorded (a hostile content has one for each of its operations)
    static constexpr size_t MAXIMUM_ISSUE_COUNT = 1000;

    /// The longest dash array a state of a node has. The state of the processor is compared
    /// with the last state after every operation and copied into a new state at every change,
    /// and every node keeps its states: both costs grow with the length of the dash array,
    /// which the content chooses (the operator d and /D of gs take any number of elements). The
    /// content, in which a longer array is in effect, is an opaque node, and no state is made
    /// there (so no state holds such an array and no two of them are compared). A dash array of
    /// a real document has a few elements; 128 numbers are one kilobyte, about the size of the
    /// rest of a state.
    static constexpr size_t MAXIMUM_DASH_LENGTH = 128;

    /// The place of an operator in the structure of a content
    enum class Category
    {
        Save,           ///< q
        Restore,        ///< Q
        Transform,      ///< cm
        State,          ///< w J j M d ri i gs, the color operators, Tc Tw Tz TL Tf Tr Ts
        TextBegin,      ///< BT
        TextEnd,        ///< ET
        TextPosition,   ///< Td TD Tm T*
        TextShow,       ///< Tj TJ ' "
        PathConstruct,  ///< m l c v y h re
        PathClip,       ///< W W*
        PathPaint,      ///< S s f F f* B B* b b* n
        XObject,        ///< Do
        Shading,        ///< sh
        InlineImage,    ///< BI ... ID ... EI
        MarkedPoint,    ///< MP DP
        MarkedBegin,    ///< BMC BDC
        MarkedEnd,      ///< EMC
        Compatibility,  ///< BX EX
        Other           ///< An unknown operator, d0, d1, a stray ID or EI
    };

    /// What the call after an operation finishes
    enum class AfterAction
    {
        None,
        FinishLeaf,     ///< The leaf of the operation
        FinishPath,     ///< The path being collected (the operation is its painting operator)
        FillFrame,      ///< The data of the marked content just opened
        CloseFrame,     ///< The frame at the top
        CheckCapture    ///< The opaque span, when nothing is pending in it
    };

    /// The leaf, which collects more operations
    enum class LeafMode
    {
        None,
        StateRun,
        Path
    };

    /// A node being built. A node with children owns them; an open node is owned by the
    /// stack of frames, an open leaf by the processor.
    struct BuildNode
    {
        PDFContentNode::Data data;
        std::vector<std::unique_ptr<BuildNode>> children;
        int closedCount = 0;    ///< Group: number of its operators Q read so far
        quint64 id = 0;         ///< Assigned, when the tree is complete
    };

    /// The two states of one place of the content
    struct Snapshot
    {
        PDFContentResolvedStatePointer resolved;
        PDFContentSymbolicStatePointer symbolic;
    };

    /// What q saves beside the state of the processor
    struct SavedState
    {
        PDFContentSymbolicStatePointer symbolic;
        PDFContentResolvedStatePointer resolved;
        const PDFDictionary* softMask = nullptr;
    };

    /// The operation between its two calls of the intercept
    struct CurrentOperation
    {
        bool isRecorded = false;
        Operator contentOperator = Operator::Invalid;
        Category category = Category::Other;
        size_t index = 0;
        bool isFailed = false;
        AfterAction afterAction = AfterAction::None;
        std::unique_ptr<BuildNode> leaf;        ///< AfterAction::FinishLeaf
        bool hasMarkedProperties = false;
        PDFObject markedProperties;             ///< The property list the processor has read (a name or a dictionary)
        bool isOperandLimit = false;            ///< More operands than MAXIMUM_OPERAND_COUNT: they are not kept
    };

    /// An opaque span being collected: it ends, when every bracket opened in it (or crossed by
    /// it) is closed and no path is under construction
    struct Capture
    {
        bool isActive = false;
        std::unique_ptr<BuildNode> node;
        size_t pendingSave = 0;
        size_t pendingMarked = 0;
        size_t pendingText = 0;
        bool isPathPending = false;
        bool isUntilEnd = false;    ///< An inline image without an end: the rest of the content
    };

    /// An issue, whose node is known only when the nodes have their identifiers
    struct PendingIssue
    {
        PDFContentIssue issue;
        bool hasOperation = false;
        const BuildNode* node = nullptr;
    };

    /// The operations of a node for the search of the node of an operation
    struct SpanOwner
    {
        size_t first = 0;
        size_t count = 0;
        quint64 id = 0;
    };

    /// The result of looking at the XObject of Do
    struct XObjectInfo
    {
        PDFContentNodeKind kind = PDFContentNodeKind::Opaque;
        PDFObjectReference reference;
        QByteArray name;
        QRectF boundingBox;     ///< In the space of the operator
        bool isDefined = false;
        bool isUnreadable = false;  ///< The dictionary of the XObject cannot be read (the kind is Opaque)
    };

    static Category getCategory(Operator contentOperator);
    static bool isRunCategory(Category category, bool isInsideText);
    static bool readNumbers(const Operands& operands, size_t count, PDFReal* numbers);
    static bool isString(const Operands& operands, size_t index) { return index < operands.size() && operands[index].type == TokenType::String; }
    static QByteArray getName(const Operands& operands, size_t index);
    static bool isReservedColorSpaceName(const QByteArray& name);
    static bool isTailReadable(const QByteArray& tail);

    /// The value (dereferenced) of a key of a graphics state parameter dictionary has a type
    /// the key can have: only then gs sets the parameter of the key
    static bool isExtGStateValueUsable(Property property, const char* key, const PDFObject& value);

    /// The font of the value [font size] of the key /Font of a graphics state parameter
    /// dictionary: the reference of the font dictionary (the first item; for a name in its place
    /// the value of that name in the font resources). Null, when there is none.
    PDFObject getExtGStateFont(const PDFObject& value) const;

    /// The entries of all merged category dictionaries of createInheritedResources at most: the
    /// value of PDFPageContentProcessor::MAXIMUM_INHERITED_DICTIONARY_ENTRIES (private there),
    /// so that the builder gives up the merging, where the processor gives it up
    static constexpr size_t MAXIMUM_INHERITED_DICTIONARY_ENTRIES = 1048576;

    /// The resources of a content without /Resources of its own drawn by a content with the
    /// resources \p callerResources, exactly as PDFPageContentProcessor::initInheritedDictionaries
    /// and getInheritedDictionary make them (NOTES.txt 7.8.3): per category (in their order
    /// there: ColorSpace, Font, XObject, ExtGState, Properties, Shading, Pattern) the dictionary
    /// of the caller; the one of the page, when the caller has none; when both differ, every entry
    /// of the caller with a value other than null and the entries of the page under the names the
    /// caller does not define - unless the entries of the two would exceed what is left of
    /// MAXIMUM_INHERITED_DICTIONARY_ENTRIES (the caller alone then). /ProcSet is left out: the
    /// processor keeps the procedure sets in effect, but nothing reads them.
    /// \returns a direct /Resources dictionary; null, when the caller has no resource
    ///          dictionary (the processor takes the page then)
    static PDFObject createInheritedResources(const PDFDocument* document, const PDFObject& pageResources, const PDFObject& callerResources);

    /// The most components of a color of a state of a node (PDF_MAX_COLOR_COMPONENTS, the number
    /// of colorants the library takes elsewhere, PDF 1.7 Annex C). A DeviceN space has as many
    /// components as the content names colorants (/DeviceN with any number of names), and a
    /// state keeps its colors by value: the components are copied with every new state and
    /// compared after every operation, as the dash array was - the content, in which a longer
    /// color is in effect, is an opaque node, as for MAXIMUM_DASH_LENGTH.
    static constexpr size_t MAXIMUM_COLOR_COMPONENTS = PDF_MAX_COLOR_COMPONENTS;

    /// The state of the processor has a value, which the states of the nodes do not take: a dash
    /// array longer than MAXIMUM_DASH_LENGTH, a color of more than MAXIMUM_COLOR_COMPONENTS
    static bool isStateOverLimit(const PDFPageContentProcessorState& state);

    /// The stroking or the filling color has more than MAXIMUM_COLOR_COMPONENTS components
    static bool isColorOverLimit(const PDFPageContentProcessorState& state);

    /// The text of the issue and the reason of the opaque node of a state over its limit
    static QString getStateLimitIssueText(const PDFPageContentProcessorState& state);
    static QString getStateLimitReason(const PDFPageContentProcessorState& state);

    /// The color, which stands for a color over the limit in a state: one component, which is not
    /// a number - equal to no color (the components are compared by value), itself included, so
    /// a node placed behind sets its own color (R-2). It is the state of a place only (the end
    /// state behind an opaque node, the initial state of a form), never a value a node sets.
    static PDFColor createColorMarker();

    /// Replaces the colors over MAXIMUM_COLOR_COMPONENTS of a state by createColorMarker
    static void replaceColorsOverLimit(PDFPageContentProcessorState& state);

    /// Makes the operation being finished the beginning of an opaque span: the state of the
    /// processor is over its limit since this operation (afterOperation calls it, before the
    /// states follow the operation)
    void startCaptureForStateLimit();

    /// Begins an opaque span with the operation, which has been recorded and is not structured
    /// yet: the state of the processor is over its limit before it (beforeOperation calls it)
    void startCaptureBeforeOperation();

    /// The content has ended (the processor restores the states the content left saved, or the
    /// result is made): when a state over the limit is in effect at its end, the state there
    /// is kept with a marker in place of its dash pattern (m_endStateOverLimit). Once per content.
    void takeEndState();

    /// The state of the processor with a dash pattern, which equals no pattern: its elements and
    /// its phase are not numbers (a NaN is equal to nothing, itself included). The state behind
    /// an opaque node, in which a pattern over the limit is still in effect at the end of the
    /// content: a node placed there sees a dash pattern different from its own and sets it
    /// (R-2), and no state holds the long array. The writer refuses to write such a value (it
    /// is never a value of a node, only the state of a place).
    PDFContentResolvedStatePointer createStateWithDashMarker() const;

    /// The operation belongs to the content of the container: forms and pattern cells are never
    /// entered, so the only other content is a glyph procedure of a Type 3 font
    bool isOwnContent() const { return m_glyphDepth == 0; }
    bool isInsideText() const { return m_textFrameIndex >= 0; }
    size_t getOpenBracketCount() const;

    void beforeOperation(Operator contentOperator);
    void afterOperation();

    // Operations
    void recordOperation();
    QByteArray getBytes(const ContentOperationRange& range) const;
    QByteArray getStreamData(PDFInteger stream) const;
    std::vector<PDFContentResourceName> getResourceNames(Operator contentOperator, const Operands& operands) const;
    void addInlineImageResourceNames(std::vector<PDFContentResourceName>& names) const;
    void checkResourceNames(const std::vector<PDFContentResourceName>& names, size_t index);
    const PDFDictionary* getResourceDictionary(PDFContentResourceCategory category) const;
    XObjectInfo getXObjectInfo(const Operands& operands) const;
    void addIssue(PDFContentIssue::Kind kind, const QString& text, bool hasOperation, size_t operationIndex, const BuildNode* node = nullptr);

    // States
    void refreshResolvedState();
    const PDFContentSymbolicStatePointer& getSymbolicState();
    Snapshot getSnapshot() { return Snapshot{ m_resolved, getSymbolicState() }; }
    void setSource(Property property, PDFContentPropertySource source);
    void updateSymbolicState(const CurrentOperation& current);
    void updateSymbolicColor(Property property, const CurrentOperation& current, const Operands& operands);

    /// A color operator in the cell of an uncolored tiling pattern: returns true, when the
    /// processor ignores the color operators now (the operation sets nothing); the first one
    /// of the container is recorded as an issue
    bool reportIgnoredColorOperator(const CurrentOperation& current);
    void updateSymbolicExtGState(const CurrentOperation& current, const Operands& operands, const PDFContentStatePropertySet* changedProperties);
    PDFContentStateLeak computeLeak(const Snapshot& begin, const Snapshot& end, bool isClipChanged) const;

    // Structure
    std::unique_ptr<BuildNode> createNode(PDFContentNodeKind kind, size_t firstOperation, size_t operationCount);
    void appendChild(std::unique_ptr<BuildNode> node) { m_frames.back()->children.push_back(std::move(node)); }
    void structureOperation();
    void structureOperationInsideText();
    void structureOperationOutsideText();
    void addToStateRun();
    void addToPath();
    void closeOpenLeaf();
    void finishPath(std::unique_ptr<BuildNode> node);
    void createLeaf(PDFContentNodeKind kind);
    void createOpaqueLeaf(const QString& reason);
    void openFrame(PDFContentNodeKind kind);
    void closeBracket(PDFContentNodeKind kind);
    void closeTopFrame(const Snapshot& end);
    void splitPartialGroup();
    void fillMarkedData(PDFContentNode::Data& data, const CurrentOperation& current);
    void fillTextShow(BuildNode& node, const CurrentOperation& current);
    void updateTextPosition(const CurrentOperation& current);

    /// Td, TD, T* and the first step of ' and ": Tlm moves by (tx, ty), Tm = Tlm (9.4.2). The
    /// text position is known afterwards, when Tlm was known and the move is finite: no show
    /// changes Tlm, so a show the layout did not follow (m_isTextPositionUnknown) or a glyph
    /// without a width before it does not matter any more (the layout starts its completeness
    /// anew at Tlm). A move out of the finite numbers is not made by the layout: Tlm is unknown.
    void moveTextLine(PDFReal tx, PDFReal ty);

    /// Tm: Tm = Tlm = the matrix (9.4.2): the text position is known afterwards, unless the
    /// matrix is not finite (the layout does not set it: Tlm is unknown)
    void setTextMatrix(const QTransform& matrix);

    /// The layout of the text object starts anew at its text line matrix (Tm = Tlm, complete)
    void restartTextLayoutAtLine();

    static bool isFiniteMatrix(const QTransform& matrix);

    QRectF getTextBoundingBox(const PDFContentNode::Data& data, const QTransform& currentTransformationMatrix) const;

    // Opaque spans
    void addPendingBrackets(Capture& capture, const BuildNode& frame) const;
    void startCaptureAtOperation(const QString& reason);
    void startCaptureFromFrame(size_t frameIndex, const QString& reason);
    void startCaptureFromPath(const QString& reason);
    void captureOperation();
    void crossOutward(PDFContentNodeKind kind);
    void finishCapture();
    void dropFrames(size_t frameIndex);

    // Result
    void finishContent();
    void createOperationLimitContent();
    void computeBoundingBoxes(BuildNode* root) const;

    const PDFContentTreeBuilderSource* m_source;
    PDFContentBuildOptions m_options;
    PDFContentTreeBuilderControl m_control;
    bool m_isInheritedUnknown;      ///< A form, a pattern cell: an inherited property has an unknown value
    PDFInteger m_inheritedResourcesPageIndex;   ///< A form, a pattern cell without /Resources of its own: the page, whose resources the processor uses; -1 otherwise
    PDFObject m_pageResources;                  ///< The resources of the page of the content
    PDFObject m_callerResources;                ///< PDFContentBuildContext::callerResources of a form, a pattern cell without /Resources of its own; null otherwise
    PDFObject m_inheritedResources;             ///< createInheritedResources of m_callerResources: the resources the content is interpreted with; null = those of the page

    PDFContentOperationList m_operations;
    size_t m_operationCount = 0;
    bool m_isOverLimit = false;
    PDFContentStreamPosition m_lastEnd{ 0, 0 };     ///< The end of the last operation of the content
    int m_glyphDepth = 0;
    int m_compatibilityDepth = 0;
    bool m_isContentEntered = false;
    bool m_isIgnoredColorOperatorReported = false;  ///< The issue of the color operators of the cell of an uncolored pattern is recorded
    PDFObject m_inheritedSoftMask;                 ///< An empty dictionary: the soft mask of the initial state of a form, whose dictionary is not known here

    std::vector<std::unique_ptr<BuildNode>> m_frames;   ///< The open nodes, the root first
    std::unique_ptr<BuildNode> m_openLeaf;
    LeafMode m_leafMode = LeafMode::None;
    Capture m_capture;
    CurrentOperation m_current;

    // The path under construction
    bool m_isPathInvalid = false;
    bool m_hasCurrentPoint = false;
    QPointF m_currentPoint;
    QPointF m_subpathStart;

    // The text object
    int m_textFrameIndex = -1;
    bool m_isTextClipping = false;
    bool m_isTextPositionUnknown = false;   ///< A show of the text object was executed, but not laid out (fillTextShow: no show after it is complete, until Tlm puts the text where the standard says)
    bool m_isTextLineMatrixUnknown = false; ///< Tlm of the text object is not the one of the standard (an opaque ' or ", a move out of the finite numbers): only Tm makes the position known again
    std::optional<PDFContentTextLayout> m_textLayout;   ///< The text matrices of the last text object begun; no value, when no text is laid out

    // The states
    PDFContentResolvedStatePointer m_resolved;
    const PDFDictionary* m_resolvedSoftMask = nullptr;  ///< The soft mask of the processor state m_resolved was made from (compared only, never read)
    PDFContentSymbolicState m_symbolicValue;
    PDFContentSymbolicStatePointer m_symbolic;          ///< nullptr, when m_symbolicValue has changed since the last one
    std::vector<SavedState> m_savedStates;
    Snapshot m_initial;
    bool m_isEndStateTaken = false;
    PDFContentResolvedStatePointer m_endStateOverLimit;    ///< takeEndState: the state at the end of the content, when a dash pattern over the limit is in effect there

    std::set<PDFInteger> m_markedContentIds;
    std::vector<PendingIssue> m_issues;
    std::vector<PDFContentContainerKey> m_usedForms;
    std::set<PDFObjectReference> m_usedFormReferences;
    std::vector<PDFContentContainerKey> m_usedPatternCells;
    std::set<PDFObjectReference> m_usedPatternReferences;
};

PDFContentTreeBuilderProcessor::PDFContentTreeBuilderProcessor(const PDFContentTreeBuilderSource* source,
                                                               const PDFPage* page,
                                                               const PDFDocument* document,
                                                               const PDFContentBuildContext& context,
                                                               const PDFContentBuildOptions& options,
                                                               const PDFOperationControl* control) :
    PDFPageContentProcessor(page, document, context.fontCache, context.iccProfileCache, context.cms, nullptr, QTransform(), PDFMeshQualitySettings()),
    m_source(source),
    m_options(options),
    m_control(control),
    m_isInheritedUnknown(source->key.kind != PDFContentContainerKey::Kind::Page),
    m_inheritedResourcesPageIndex((source->key.kind != PDFContentContainerKey::Kind::Page && source->resources.isNull()) ? context.pageIndex : -1),
    m_inheritedSoftMask(PDFObject::createDictionary(PDFDictionaryBuilder()))
{
    setOperationControl(&m_control);

    m_pageResources = page->getResources();
    if (source->key.kind != PDFContentContainerKey::Kind::Page && source->resources.isNull())
    {
        m_callerResources = context.callerResources;
        m_inheritedResources = createInheritedResources(document, page->getResources(), m_callerResources);
    }

    m_frames.push_back(std::make_unique<BuildNode>());
    m_frames.back()->data.kind = PDFContentNodeKind::Root;
}

void PDFContentTreeBuilderProcessor::run(const PDFContentResolvedStatePointer& initialState)
{
    switch (m_source->key.kind)
    {
        case PDFContentContainerKey::Kind::Page:
        {
            // The content is entered from the start: a form drawn by Do is never processed.
            // A content above the size limit is not interpreted (the processor would decode
            // its streams itself): the container is empty and read-only.
            m_isContentEntered = true;
            if (m_source->isOverSize)
            {
                // Only the initial state
                initializeProcessor();
            }
            else
            {
                processContents();
            }
            break;
        }

        case PDFContentContainerKey::Kind::Form:
        case PDFContentContainerKey::Kind::PatternCell:
        {
            initializeProcessor();

            if (m_source->key.kind == PDFContentContainerKey::Kind::Form && initialState)
            {
                // The state of the place of the first use; the content of the form has the
                // space of the form (the matrix of the container says, where it is drawn)
                PDFPageContentProcessorState state = initialState->getState();
                state.setCurrentTransformationMatrix(QTransform());
                state.setTextMatrix(QTransform());
                state.setTextLineMatrix(QTransform());
                state.setSoftMask(initialState->hasSoftMask() ? m_inheritedSoftMask.getDictionary() : nullptr);
                setGraphicsState(state);
            }

            // The cell of an uncolored tiling pattern specifies no color (ISO 32000-2, 8.7.3.3):
            // under the guard the processor ignores its color operators, so the colors of
            // every state of the cell are those of its beginning. They are the initial ones of
            // the processor (DeviceGray, black) and their source is inherited: the color of the
            // cell is the color of the use, which paints with the pattern (the operands of its
            // scn / SCN in the underlying color space), and the cell does not know its uses.
            PDFUncoloredTilingPatternGuard guard(this, m_source->isUncoloredPatternCell);

            if (!m_source->streams.empty() && m_source->streams.front().isDecoded)
            {
                try
                {
                    // Without /Resources of its own: null resources make the processor take
                    // those of the page (its caller is the page here); the resources merged with
                    // those of the real caller give the names the renderer gives (item 1 of PB0S3)
                    const PDFObject& resources = m_inheritedResources.isNull() ? m_source->resources : m_inheritedResources;
                    processForm(QTransform(), m_source->boundingBox, resources, m_source->transparencyGroup, m_source->streams.front().data, m_source->structuralParentKey);
                }
                catch (const PDFException& exception)
                {
                    addIssue(PDFContentIssue::Kind::OperatorFailed, exception.getMessage(), false, 0);
                }
                catch (const PDFRendererException& exception)
                {
                    addIssue(PDFContentIssue::Kind::OperatorFailed, exception.getError().message, false, 0);
                }
            }
            break;
        }
    }

    if (!m_initial.resolved)
    {
        // A content without an operation: the state the processor is left in is the initial one
        refreshResolvedState();
        m_initial = getSnapshot();
    }
}

void PDFContentTreeBuilderProcessor::performInterceptInstruction(Operator currentOperator, ProcessOrder processOrder, const QByteArray& operatorAsText)
{
    Q_UNUSED(operatorAsText);

    if (!isOwnContent())
    {
        // A glyph procedure of a Type 3 font: its operations are not operations of the container
        return;
    }

    if (processOrder == ProcessOrder::BeforeOperation)
    {
        beforeOperation(currentOperator);
    }
    else
    {
        afterOperation();
    }
}

void PDFContentTreeBuilderProcessor::performOperatorFailed(Operator currentOperator, const PDFRenderError& error)
{
    Q_UNUSED(currentOperator);

    if (!isOwnContent() || !m_current.isRecorded)
    {
        return;
    }

    m_current.isFailed = true;
    PDFContentOperation::Flags flags = m_operations.getOperation(m_current.index).getFlags();
    flags.setFlag(PDFContentOperation::Failed);
    m_operations.setFlags(m_current.index, flags);
    addIssue(PDFContentIssue::Kind::OperatorFailed, error.message, true, m_current.index);

    if (m_leafMode == LeafMode::Path)
    {
        // The path the processor has is not the path of the operands
        m_isPathInvalid = true;
    }
}

void PDFContentTreeBuilderProcessor::performType3GlyphBegin(const PDFType3Font* font, unsigned int code, const QByteArray& content)
{
    Q_UNUSED(font);
    Q_UNUSED(code);
    Q_UNUSED(content);
    ++m_glyphDepth;
}

void PDFContentTreeBuilderProcessor::performType3GlyphEnd()
{
    --m_glyphDepth;
}

void PDFContentTreeBuilderProcessor::performMarkedContentPoint(const QByteArray& tag, const PDFObject& properties)
{
    Q_UNUSED(tag);

    if (isOwnContent() && m_current.isRecorded)
    {
        m_current.hasMarkedProperties = true;
        m_current.markedProperties = properties;
    }
}

void PDFContentTreeBuilderProcessor::performMarkedContentBegin(const QByteArray& tag, const PDFObject& properties)
{
    Q_UNUSED(tag);

    if (isOwnContent() && m_current.isRecorded)
    {
        m_current.hasMarkedProperties = true;
        m_current.markedProperties = properties;
    }
}

bool PDFContentTreeBuilderProcessor::isContentKindSuppressed(ContentKind kind) const
{
    // Nothing is painted. A form is never entered by Do; the public processForm of the content
    // of a form container asks before its first operation (pe0_skeleton_notes.md, section 5,
    // contradiction 1).
    if (kind == ContentKind::Forms)
    {
        return m_isContentEntered;
    }

    return true;
}

bool PDFContentTreeBuilderProcessor::isContentSuppressedByOC(PDFObjectReference ocgOrOcmd)
{
    // The tree holds hidden content too
    Q_UNUSED(ocgOrOcmd);
    return false;
}

bool PDFContentTreeBuilderProcessor::isTilingPatternProcessingAllowed(PDFInteger tileCount) const
{
    Q_UNUSED(tileCount);
    return false;
}

PDFContentTreeBuilderProcessor::Category PDFContentTreeBuilderProcessor::getCategory(Operator contentOperator)
{
    switch (contentOperator)
    {
        case Operator::SaveGraphicState:
            return Category::Save;

        case Operator::RestoreGraphicState:
            return Category::Restore;

        case Operator::AdjustCurrentTransformationMatrix:
            return Category::Transform;

        case Operator::SetLineWidth:
        case Operator::SetLineCap:
        case Operator::SetLineJoin:
        case Operator::SetMitterLimit:
        case Operator::SetLineDashPattern:
        case Operator::SetRenderingIntent:
        case Operator::SetFlatness:
        case Operator::SetGraphicState:
        case Operator::ColorSetStrokingColorSpace:
        case Operator::ColorSetFillingColorSpace:
        case Operator::ColorSetStrokingColor:
        case Operator::ColorSetStrokingColorN:
        case Operator::ColorSetFillingColor:
        case Operator::ColorSetFillingColorN:
        case Operator::ColorSetDeviceGrayStroking:
        case Operator::ColorSetDeviceGrayFilling:
        case Operator::ColorSetDeviceRGBStroking:
        case Operator::ColorSetDeviceRGBFilling:
        case Operator::ColorSetDeviceCMYKStroking:
        case Operator::ColorSetDeviceCMYKFilling:
        case Operator::TextSetCharacterSpacing:
        case Operator::TextSetWordSpacing:
        case Operator::TextSetHorizontalScale:
        case Operator::TextSetLeading:
        case Operator::TextSetFontAndFontSize:
        case Operator::TextSetRenderMode:
        case Operator::TextSetRise:
            return Category::State;

        case Operator::TextBegin:
            return Category::TextBegin;

        case Operator::TextEnd:
            return Category::TextEnd;

        case Operator::TextMoveByOffset:
        case Operator::TextSetLeadingAndMoveByOffset:
        case Operator::TextSetMatrix:
        case Operator::TextMoveByLeading:
            return Category::TextPosition;

        case Operator::TextShowTextString:
        case Operator::TextShowTextIndividualSpacing:
        case Operator::TextNextLineShowText:
        case Operator::TextSetSpacingAndShowText:
            return Category::TextShow;

        case Operator::MoveCurrentPoint:
        case Operator::LineTo:
        case Operator::Bezier123To:
        case Operator::Bezier23To:
        case Operator::Bezier13To:
        case Operator::EndSubpath:
        case Operator::Rectangle:
            return Category::PathConstruct;

        case Operator::ClipWinding:
        case Operator::ClipEvenOdd:
            return Category::PathClip;

        case Operator::PathStroke:
        case Operator::PathCloseStroke:
        case Operator::PathFillWinding:
        case Operator::PathFillWinding2:
        case Operator::PathFillEvenOdd:
        case Operator::PathFillStrokeWinding:
        case Operator::PathFillStrokeEvenOdd:
        case Operator::PathCloseFillStrokeWinding:
        case Operator::PathCloseFillStrokeEvenOdd:
        case Operator::PathClear:
            return Category::PathPaint;

        case Operator::PaintXObject:
            return Category::XObject;

        case Operator::ShadingPaintShape:
            return Category::Shading;

        case Operator::InlineImage:
            return Category::InlineImage;

        case Operator::MarkedContentPoint:
        case Operator::MarkedContentPointWithProperties:
            return Category::MarkedPoint;

        case Operator::MarkedContentBegin:
        case Operator::MarkedContentBeginWithProperties:
            return Category::MarkedBegin;

        case Operator::MarkedContentEnd:
            return Category::MarkedEnd;

        case Operator::CompatibilityBegin:
        case Operator::CompatibilityEnd:
            return Category::Compatibility;

        case Operator::Type3FontSetOffset:
        case Operator::Type3FontSetOffsetAndBB:
        case Operator::InlineImageBegin:
        case Operator::InlineImageData:
        case Operator::InlineImageEnd:
        case Operator::Invalid:
            return Category::Other;
    }

    return Category::Other;
}

bool PDFContentTreeBuilderProcessor::isRunCategory(Category category, bool isInsideText)
{
    switch (category)
    {
        case Category::State:
        case Category::Compatibility:
        case Category::Transform:
            // (cm inside of a text object makes the text object opaque, whatever leaf is open)
            return true;

        case Category::TextPosition:
            // The operators of the text position are operators of a text object only (9.4.2)
            return isInsideText;

        default:
            return false;
    }
}

bool PDFContentTreeBuilderProcessor::readNumbers(const Operands& operands, size_t count, PDFReal* numbers)
{
    if (operands.size() < count)
    {
        return false;
    }

    for (size_t i = 0; i < count; ++i)
    {
        const PDFLexicalAnalyzer::Token& token = operands[i];
        if (token.type != TokenType::Integer && token.type != TokenType::Real)
        {
            return false;
        }
        numbers[i] = token.data.value<PDFReal>();
    }

    return true;
}

QByteArray PDFContentTreeBuilderProcessor::getName(const Operands& operands, size_t index)
{
    if (index < operands.size() && operands[index].type == TokenType::Name)
    {
        return operands[index].data.toByteArray();
    }

    return QByteArray();
}

bool PDFContentTreeBuilderProcessor::isReservedColorSpaceName(const QByteArray& name)
{
    // The names, which the operators cs and CS take without a resource (ISO 32000-2, 8.6.8,
    // Table 73: the names DeviceGray, DeviceRGB, DeviceCMYK and Pattern always identify their
    // color spaces directly, they never refer to the resources)
    return name == "DeviceGray" || name == "DeviceRGB" || name == "DeviceCMYK" || name == "Pattern";
}

bool PDFContentTreeBuilderProcessor::isTailReadable(const QByteArray& tail)
{
    // White space and comments are nothing to split into operations (7.2.3, 7.2.4)
    bool isComment = false;
    for (const char character : tail)
    {
        if (isComment)
        {
            isComment = character != '\n' && character != '\r';
        }
        else if (character == '%')
        {
            isComment = true;
        }
        else if (!PDFLexicalAnalyzer::isWhitespace(character))
        {
            return false;
        }
    }

    return true;
}

size_t PDFContentTreeBuilderProcessor::getOpenBracketCount() const
{
    // A chain of q is one node now, but it becomes a node inside of a node for each part of
    // its Q, which does not come in a row: every q counts as a level
    size_t count = 0;
    for (size_t i = 1; i < m_frames.size(); ++i)
    {
        const PDFContentNode::Data& data = m_frames[i]->data;
        count += data.kind == PDFContentNodeKind::Group ? size_t(data.saveDepth) : 1;
    }
    return count;
}

void PDFContentTreeBuilderProcessor::beforeOperation(Operator contentOperator)
{
    m_isContentEntered = true;

    if (!m_initial.resolved)
    {
        refreshResolvedState();
        m_initial = getSnapshot();
        m_frames.front()->data.state = m_initial.resolved;
        m_frames.front()->data.symbolicState = m_initial.symbolic;
    }

    m_current = CurrentOperation();
    m_current.contentOperator = contentOperator;
    m_current.category = getCategory(contentOperator);

    if (m_isOverLimit)
    {
        return;
    }

    if (m_operationCount >= PDFContentContainer::MAXIMUM_OPERATION_COUNT)
    {
        // The container is one opaque node: nothing more is interpreted
        m_isOverLimit = true;
        m_control.stop();
        return;
    }

    recordOperation();

    // A state over its limit before the operation, while no opaque span is open: the initial
    // state of a form, which the caller gives (an operation of the content, which brings the
    // state over the limit, begins a span itself - startCaptureForStateLimit). The span begins
    // with this operation, whatever its operator is: no node may begin in such a state.
    if (!m_capture.isActive && isStateOverLimit(*getGraphicState()))
    {
        startCaptureBeforeOperation();
    }

    structureOperation();
}

void PDFContentTreeBuilderProcessor::afterOperation()
{
    if (!m_current.isRecorded)
    {
        m_current = CurrentOperation();
        return;
    }

    // A state over its limit: the content from this operation on is opaque, as long as the
    // state stays so (the states of the builder are still the ones before the operation here)
    if (!m_capture.isActive && isStateOverLimit(*getGraphicState()))
    {
        startCaptureForStateLimit();
    }

    // The symbolic state first (it needs the state before the operation, when the operator
    // has failed), then the state after the operation
    updateSymbolicState(m_current);
    if (m_current.category == Category::TextPosition)
    {
        updateTextPosition(m_current);
    }
    refreshResolvedState();

    // A text show, which the processor has executed, but which is no TextShow node (too many
    // operands: an opaque node): it moved the text matrix, the layout did not follow it - the
    // positions of the shows after it in the text object are not known (A2L2-4)
    // (the category of such an operation is Other: the operator tells)
    if (getCategory(m_current.contentOperator) == Category::TextShow && !m_current.isFailed && isInsideText() &&
        !(m_current.afterAction == AfterAction::FinishLeaf && m_current.leaf && m_current.leaf->data.kind == PDFContentNodeKind::TextShow))
    {
        m_isTextPositionUnknown = true;

        // ' and " move Tlm by T* first (9.4.3, Table 107): the layout did not see it either
        if (m_current.contentOperator == Operator::TextNextLineShowText || m_current.contentOperator == Operator::TextSetSpacingAndShowText)
        {
            m_isTextLineMatrixUnknown = true;
        }
    }

    switch (m_current.afterAction)
    {
        case AfterAction::None:
            break;

        case AfterAction::FinishLeaf:
        {
            std::unique_ptr<BuildNode> leaf = std::move(m_current.leaf);
            if (leaf->data.kind == PDFContentNodeKind::TextShow)
            {
                fillTextShow(*leaf, m_current);
            }
            else if (leaf->data.kind == PDFContentNodeKind::MarkedPoint)
            {
                fillMarkedData(leaf->data, m_current);
            }

            const Snapshot end = getSnapshot();
            if (leaf->data.kind == PDFContentNodeKind::Opaque)
            {
                leaf->data.leak.isUnknown = true;
                leaf->data.leak.endState = end.resolved;
                leaf->data.leak.endSymbolicState = end.symbolic;
            }
            else
            {
                leaf->data.leak = computeLeak(Snapshot{ leaf->data.state, leaf->data.symbolicState }, end, leaf->data.hasClipEffect);
            }
            appendChild(std::move(leaf));
            break;
        }

        case AfterAction::FinishPath:
        {
            std::unique_ptr<BuildNode> path = std::move(m_openLeaf);
            m_leafMode = LeafMode::None;
            finishPath(std::move(path));
            break;
        }

        case AfterAction::FillFrame:
            fillMarkedData(m_frames.back()->data, m_current);
            break;

        case AfterAction::CloseFrame:
            closeTopFrame(getSnapshot());
            break;

        case AfterAction::CheckCapture:
        {
            // The span does not end, while the state of the processor is over its limit: no
            // node can begin in such a state
            if (m_capture.isActive && !m_capture.isUntilEnd && !m_capture.isPathPending &&
                m_capture.pendingSave == 0 && m_capture.pendingMarked == 0 && m_capture.pendingText == 0 &&
                !isStateOverLimit(*getGraphicState()))
            {
                finishCapture();
            }
            break;
        }
    }

    m_current = CurrentOperation();
}

void PDFContentTreeBuilderProcessor::recordOperation()
{
    const Operator contentOperator = m_current.contentOperator;
    const ContentOperationRange& range = getOperationRange();

    PDFContentByteRange byteRange;
    byteRange.begin = PDFContentStreamPosition{ range.beginStream, range.beginOffset };
    byteRange.end = PDFContentStreamPosition{ range.endStream, range.endOffset };

    PDFContentOperation::Flags flags = PDFContentOperation::None;
    flags.setFlag(PDFContentOperation::Unknown, contentOperator == Operator::Invalid);

    // BX and EX themselves are not inside
    if (contentOperator == Operator::CompatibilityEnd && m_compatibilityDepth > 0)
    {
        --m_compatibilityDepth;
    }
    flags.setFlag(PDFContentOperation::InsideCompatibility, m_compatibilityDepth > 0);
    if (contentOperator == Operator::CompatibilityBegin)
    {
        ++m_compatibilityDepth;
    }

    const auto& processorOperands = getOperands();
    const size_t index = m_operations.getCount();

    Operands operands;
    if (processorOperands.size() > PDFContentContainer::MAXIMUM_OPERAND_COUNT)
    {
        // The operands are not decoded: the operation is opaque, unless it is a bracket of the
        // structure (an operator takes its operands from the start, the rest does not count)
        addIssue(PDFContentIssue::Kind::OperandLimit, PDFTranslationContext::tr("An operation has too many operands."), true, index);
        m_current.isOperandLimit = true;
        switch (m_current.category)
        {
            case Category::Save:
            case Category::Restore:
            case Category::TextBegin:
            case Category::TextEnd:
            case Category::MarkedBegin:
            case Category::MarkedEnd:
                break;

            default:
                m_current.category = Category::Other;
                break;
        }
    }
    else
    {
        operands.reserve(processorOperands.size());
        for (size_t i = 0; i < processorOperands.size(); ++i)
        {
            operands.push_back(processorOperands[i]);
        }
    }

    std::vector<PDFContentResourceName> resourceNames = getResourceNames(contentOperator, operands);
    checkResourceNames(resourceNames, index);

    // The operation is in the list, before anything of the document is read for it: the
    // processor has already moved the beginning of the next operation behind this one, so an
    // exception, which left this function before, would lose the bytes of the operation (they
    // would be in no operation and not in the tail). Nothing above reads a value of the document,
    // which can refuse to be read.
    m_operations.append(PDFContentOperation::createOriginal(contentOperator, getBytes(range), byteRange, std::move(operands), std::move(resourceNames), flags));
    ++m_operationCount;
    m_lastEnd = byteRange.end;

    m_current.isRecorded = true;
    m_current.index = index;

    if (contentOperator == Operator::PaintXObject)
    {
        // Every form used by the container is listed, also inside of an opaque span
        const XObjectInfo info = getXObjectInfo(m_operations.getOperation(index).getOperands());
        if (info.kind == PDFContentNodeKind::FormUse && info.reference.isValid() && m_usedFormReferences.insert(info.reference).second)
        {
            m_usedForms.push_back(PDFContentContainerKey::createForm(info.reference));
        }
    }
}

QByteArray PDFContentTreeBuilderProcessor::getBytes(const ContentOperationRange& range) const
{
    if (!range.isValid())
    {
        return QByteArray();
    }

    if (range.beginStream == range.endStream)
    {
        return getStreamData(range.endStream).mid(range.beginOffset, range.endOffset - range.beginOffset);
    }

    // The streams of /Contents are joined by one line feed (7.8.2)
    QByteArray bytes = getStreamData(range.beginStream).mid(range.beginOffset);
    for (PDFInteger stream = range.beginStream + 1; stream < range.endStream; ++stream)
    {
        bytes.append('\n');
        bytes.append(getStreamData(stream));
    }
    bytes.append('\n');
    bytes.append(getStreamData(range.endStream).left(range.endOffset));
    return bytes;
}

QByteArray PDFContentTreeBuilderProcessor::getStreamData(PDFInteger stream) const
{
    // The decoded streams of the source: the same bytes the processor reads (it keeps only
    // the stream it is processing), in the same order and with the same indices
    if (stream >= 0 && size_t(stream) < m_source->streams.size())
    {
        return m_source->streams[size_t(stream)].data;
    }

    return QByteArray();
}

std::vector<PDFContentResourceName> PDFContentTreeBuilderProcessor::getResourceNames(Operator contentOperator, const Operands& operands) const
{
    std::vector<PDFContentResourceName> names;

    auto addName = [&names](PDFContentResourceCategory category, const QByteArray& name)
    {
        if (!name.isEmpty())
        {
            names.push_back(PDFContentResourceName{ category, name });
        }
    };

    switch (contentOperator)
    {
        case Operator::TextSetFontAndFontSize:
            addName(PDFContentResourceCategory::Font, getName(operands, 0));
            break;

        case Operator::PaintXObject:
            addName(PDFContentResourceCategory::XObject, getName(operands, 0));
            break;

        case Operator::SetGraphicState:
            addName(PDFContentResourceCategory::ExtGState, getName(operands, 0));
            break;

        case Operator::ColorSetStrokingColorSpace:
        case Operator::ColorSetFillingColorSpace:
        {
            const QByteArray name = getName(operands, 0);
            if (!isReservedColorSpaceName(name))
            {
                addName(PDFContentResourceCategory::ColorSpace, name);
            }
            break;
        }

        case Operator::ColorSetStrokingColorN:
        case Operator::ColorSetFillingColorN:
        {
            // The name of a pattern is the last operand (8.7.3.2, 8.7.3.3)
            if (!operands.empty())
            {
                addName(PDFContentResourceCategory::Pattern, getName(operands, operands.size() - 1));
            }
            break;
        }

        case Operator::ShadingPaintShape:
            addName(PDFContentResourceCategory::Shading, getName(operands, 0));
            break;

        case Operator::MarkedContentPointWithProperties:
        case Operator::MarkedContentBeginWithProperties:
            // The property list is a name of /Properties, or an inline dictionary (14.6.2)
            addName(PDFContentResourceCategory::Properties, getName(operands, 1));
            break;

        case Operator::InlineImage:
            addInlineImageResourceNames(names);
            break;

        default:
            break;
    }

    return names;
}

void PDFContentTreeBuilderProcessor::addInlineImageResourceNames(std::vector<PDFContentResourceName>& names) const
{
    const InlineImageRange& imageRange = getInlineImageRange();
    if (!imageRange.isValid() || imageRange.dictionaryOffset < 0 || imageRange.dataOperatorOffset < imageRange.dictionaryOffset)
    {
        return;
    }

    // The color space of an inline image can be a name of /ColorSpace of the resources (8.9.7,
    // Table 91 and the text below Table 92); the abbreviations and the device spaces are not
    const QByteArray data = getStreamData(getOperationRange().endStream);
    if (imageRange.dataOperatorOffset > data.size())
    {
        return;
    }

    auto isDirectName = [](const QByteArray& name)
    {
        return isReservedColorSpaceName(name) || name == "G" || name == "RGB" || name == "CMYK" || name == "I" || name == "Indexed" ||
               name == "CalGray" || name == "CalRGB" || name == "Lab" || name == "ICCBased" || name == "Separation" || name == "DeviceN";
    };

    try
    {
        PDFLexicalAnalyzer analyzer(data.constData() + imageRange.dictionaryOffset, data.constData() + imageRange.dataOperatorOffset);

        bool isColorSpaceValue = false;
        int arrayItem = -1;     // Index of the next item of the array of the color space, -1 = no array
        while (!analyzer.isAtEnd())
        {
            const PDFLexicalAnalyzer::Token token = analyzer.fetch();
            if (token.type == TokenType::EndOfFile)
            {
                break;
            }

            if (arrayItem >= 0)
            {
                // [ /Indexed base hival lookup ]: the base can be a name of the resources
                if (token.type == TokenType::ArrayEnd)
                {
                    arrayItem = -1;
                }
                else
                {
                    if (arrayItem == 1 && token.type == TokenType::Name && !isDirectName(token.data.toByteArray()))
                    {
                        names.push_back(PDFContentResourceName{ PDFContentResourceCategory::ColorSpace, token.data.toByteArray() });
                    }
                    ++arrayItem;
                }
                continue;
            }

            if (isColorSpaceValue)
            {
                isColorSpaceValue = false;
                if (token.type == TokenType::Name)
                {
                    if (!isDirectName(token.data.toByteArray()))
                    {
                        names.push_back(PDFContentResourceName{ PDFContentResourceCategory::ColorSpace, token.data.toByteArray() });
                    }
                }
                else if (token.type == TokenType::ArrayStart)
                {
                    arrayItem = 0;
                }
                continue;
            }

            if (token.type == TokenType::Name)
            {
                const QByteArray key = token.data.toByteArray();
                isColorSpaceValue = key == "CS" || key == "ColorSpace";
            }
        }
    }
    catch (const PDFException&)
    {
        // The dictionary cannot be read: the processor has refused the image
    }
}

const PDFDictionary* PDFContentTreeBuilderProcessor::getResourceDictionary(PDFContentResourceCategory category) const
{
    switch (category)
    {
        case PDFContentResourceCategory::Font:
            return getFontDictionary();

        case PDFContentResourceCategory::XObject:
            return getXObjectDictionary();

        case PDFContentResourceCategory::ExtGState:
            return getExtendedGraphicStateDictionary();

        case PDFContentResourceCategory::ColorSpace:
            return getColorSpaceDictionary();

        case PDFContentResourceCategory::Pattern:
            return getPatternDictionary();

        case PDFContentResourceCategory::Shading:
            return getShadingDictionary();

        case PDFContentResourceCategory::Properties:
            return getPropertiesDictionary();
    }

    return nullptr;
}

void PDFContentTreeBuilderProcessor::checkResourceNames(const std::vector<PDFContentResourceName>& names, size_t index)
{
    for (const PDFContentResourceName& name : names)
    {
        const PDFDictionary* dictionary = getResourceDictionary(name.category);
        if (!dictionary || !dictionary->hasKey(name.name))
        {
            addIssue(PDFContentIssue::Kind::UndefinedResource,
                     PDFTranslationContext::tr("The resource '%1' is not defined.").arg(QString::fromLatin1(name.name)),
                     true, index);
        }
    }
}

PDFContentTreeBuilderProcessor::XObjectInfo PDFContentTreeBuilderProcessor::getXObjectInfo(const Operands& operands) const
{
    XObjectInfo info;
    info.name = getName(operands, 0);

    const PDFDictionary* dictionary = getXObjectDictionary();
    if (info.name.isEmpty() || !dictionary || !dictionary->hasKey(info.name))
    {
        return info;
    }

    info.isDefined = true;

    const PDFObject& entry = dictionary->get(info.name);
    if (entry.isReference())
    {
        info.reference = entry.getReference();
    }

    const PDFObject& object = getDocument()->getObject(entry);
    if (!object.isStream())
    {
        return info;
    }

    // The functions, which read the dictionary, throw for a value they cannot read
    // (readMatrixFromDictionary for a /Matrix, which is not six numbers). This function is
    // called while an operation is recorded and gets its place in the tree, where no exception
    // may leave: the XObject is opaque then (the processor refuses its Do for the same reason,
    // a form container of the object does not exist - PDFContentTreeBuilderSource::read).
    try
    {
        PDFDocumentDataLoaderDecorator loader(getDocument());
        const PDFDictionary* streamDictionary = object.getStream()->getDictionary();
        const QByteArray subtype = loader.readNameFromDictionary(streamDictionary, "Subtype");

        if (subtype == "Image")
        {
            // An image is painted into the unit square of the user space (8.9.5)
            info.kind = PDFContentNodeKind::Image;
            info.boundingBox = QRectF(0.0, 0.0, 1.0, 1.0);
        }
        else if (subtype == "Form")
        {
            // The form is painted in its space, which /Matrix maps to the user space, and clipped
            // by its /BBox (8.10.2)
            const QRectF boundingBox = loader.readRectangle(streamDictionary->get("BBox"), QRectF());
            const QTransform matrix = loader.readMatrixFromDictionary(streamDictionary, "Matrix", QTransform());
            info.kind = PDFContentNodeKind::FormUse;
            info.boundingBox = matrix.mapRect(boundingBox);
        }
    }
    catch (const PDFException&)
    {
        info.kind = PDFContentNodeKind::Opaque;
        info.isUnreadable = true;
    }
    catch (const PDFRendererException&)
    {
        info.kind = PDFContentNodeKind::Opaque;
        info.isUnreadable = true;
    }

    return info;
}

void PDFContentTreeBuilderProcessor::addIssue(PDFContentIssue::Kind kind, const QString& text, bool hasOperation, size_t operationIndex, const BuildNode* node)
{
    if (m_issues.size() >= MAXIMUM_ISSUE_COUNT)
    {
        return;
    }

    PendingIssue pendingIssue;
    pendingIssue.issue.kind = kind;
    pendingIssue.issue.operationIndex = hasOperation ? operationIndex : 0;
    pendingIssue.issue.text = text;
    pendingIssue.hasOperation = hasOperation;
    pendingIssue.node = node;
    m_issues.push_back(std::move(pendingIssue));
}

PDFObject PDFContentTreeBuilderProcessor::createInheritedResources(const PDFDocument* document, const PDFObject& pageResources, const PDFObject& callerResources)
{
    const PDFObject& callerObject = document->getObject(callerResources);
    if (!callerObject.isDictionary())
    {
        return PDFObject();
    }

    const PDFObject& pageObject = document->getObject(pageResources);
    const PDFDictionary* pageDictionary = pageObject.isDictionary() ? pageObject.getDictionary() : nullptr;
    const PDFDictionary* callerDictionary = callerObject.getDictionary();

    // The dictionary of a category as PDFPageContentProcessor::initDictionaries reads it: the
    // dereferenced value of the key, when it is a dictionary
    auto getCategory = [document](const PDFDictionary* resources, const char* key) -> PDFObject
    {
        if (resources && resources->hasKey(key))
        {
            const PDFObject& object = document->getObject(resources->get(key));
            if (object.isDictionary())
            {
                return object;
            }
        }
        return PDFObject();
    };

    // The order of initInheritedDictionaries: the budget of the entries is spent in this order
    static const char* const categoryKeys[] = { "ColorSpace", "Font", "XObject", "ExtGState", "Properties", "Shading", "Pattern" };

    PDFDictionaryBuilder result;
    size_t entryCount = 0;
    for (const char* key : categoryKeys)
    {
        const PDFObject pageCategory = getCategory(pageDictionary, key);
        const PDFObject callerCategory = getCategory(callerDictionary, key);
        const PDFDictionary* pageCategoryDictionary = pageCategory.isDictionary() ? pageCategory.getDictionary() : nullptr;
        const PDFDictionary* callerCategoryDictionary = callerCategory.isDictionary() ? callerCategory.getDictionary() : nullptr;

        PDFObject value;
        if (!callerCategoryDictionary)
        {
            // The caller has no dictionary of the category: every name from the page
            value = pageCategory;
        }
        else if (!pageCategoryDictionary || callerCategoryDictionary == pageCategoryDictionary)
        {
            value = callerCategory;
        }
        else
        {
            const size_t count = pageCategoryDictionary->getCount() + callerCategoryDictionary->getCount();
            if (count > MAXIMUM_INHERITED_DICTIONARY_ENTRIES - qMin(entryCount, MAXIMUM_INHERITED_DICTIONARY_ENTRIES))
            {
                // Above the budget the processor takes the caller alone
                value = callerCategory;
            }
            else
            {
                entryCount += count;

                // The entries of the caller (an entry with the null value defines nothing), then
                // those of the page under the names the caller does not define
                std::set<QByteArray> callerNames;
                PDFDictionaryBuilder merged;
                for (size_t i = 0; i < callerCategoryDictionary->getCount(); ++i)
                {
                    if (!callerCategoryDictionary->getValue(i).isNull())
                    {
                        callerNames.insert(callerCategoryDictionary->getKey(i).getString());
                        merged.addEntry(callerCategoryDictionary->getKey(i), callerCategoryDictionary->getValue(i));
                    }
                }
                for (size_t i = 0; i < pageCategoryDictionary->getCount(); ++i)
                {
                    if (!callerNames.count(pageCategoryDictionary->getKey(i).getString()))
                    {
                        merged.addEntry(pageCategoryDictionary->getKey(i), pageCategoryDictionary->getValue(i));
                    }
                }
                value = PDFObject::createDictionary(std::move(merged));
            }
        }

        if (!value.isNull())
        {
            result.addEntry(PDFInplaceOrMemoryString(key), std::move(value));
        }
    }

    return PDFObject::createDictionary(std::move(result));
}

bool PDFContentTreeBuilderProcessor::isStateOverLimit(const PDFPageContentProcessorState& state)
{
    return state.getLineDashPattern().getDashArray().size() > MAXIMUM_DASH_LENGTH || isColorOverLimit(state);
}

bool PDFContentTreeBuilderProcessor::isColorOverLimit(const PDFPageContentProcessorState& state)
{
    return state.getStrokeColorOriginal().size() > MAXIMUM_COLOR_COMPONENTS || state.getFillColorOriginal().size() > MAXIMUM_COLOR_COMPONENTS;
}

QString PDFContentTreeBuilderProcessor::getStateLimitIssueText(const PDFPageContentProcessorState& state)
{
    return isColorOverLimit(state) ? PDFTranslationContext::tr("A color has too many components.")
                                   : PDFTranslationContext::tr("The dash pattern of a line has too many elements.");
}

QString PDFContentTreeBuilderProcessor::getStateLimitReason(const PDFPageContentProcessorState& state)
{
    return isColorOverLimit(state) ? PDFTranslationContext::tr("Content with a color of too many components")
                                   : PDFTranslationContext::tr("Content with a dash pattern of too many elements");
}

PDFColor PDFContentTreeBuilderProcessor::createColorMarker()
{
    PDFColor marker;
    marker.push_back(std::numeric_limits<PDFColorComponent>::quiet_NaN());
    return marker;
}

void PDFContentTreeBuilderProcessor::replaceColorsOverLimit(PDFPageContentProcessorState& state)
{
    if (state.getStrokeColorOriginal().size() > MAXIMUM_COLOR_COMPONENTS)
    {
        state.setStrokeColor(state.getStrokeColor(), createColorMarker());
    }
    if (state.getFillColorOriginal().size() > MAXIMUM_COLOR_COMPONENTS)
    {
        state.setFillColor(state.getFillColor(), createColorMarker());
    }
}

void PDFContentTreeBuilderProcessor::startCaptureForStateLimit()
{
    // The operation, which has brought the state over its limit, is d or gs, or a color operator
    // (cs, scn, sc and their stroking twins): a state operator,
    // so it is the last operation of the open state run, or (with too many operands) the opaque
    // leaf of the operation. It is taken out of there and begins the opaque span. Q cannot bring
    // such a state back outside of an opaque span: the span, in which the state was set, does
    // not end before the state is within the limit again.
    const size_t index = m_current.index;

    if (m_current.afterAction == AfterAction::FinishLeaf && m_current.leaf)
    {
        m_current.leaf.reset();
        m_current.afterAction = AfterAction::None;
    }
    else if (m_leafMode == LeafMode::StateRun && m_openLeaf && m_openLeaf->data.prefix.count > 0 &&
             m_openLeaf->data.prefix.first + m_openLeaf->data.prefix.count == index + 1)
    {
        if (--m_openLeaf->data.prefix.count == 0)
        {
            m_openLeaf.reset();
            m_leafMode = LeafMode::None;
        }
    }
    else
    {
        // Not reached: d and gs are state operators (outside of a path - inside of one the
        // path is an opaque span already - they end a state run, inside of a text object too);
        // with too many operands they are an opaque leaf; Q restores such a state only inside of
        // the span, in which it was set, or as an end without a beginning (an opaque leaf); the
        // initial state of a form is handled by beforeOperation. Should it be reached, the span
        // begins with the next operation (beforeOperation); the states are not refreshed
        // meanwhile, so nothing is copied or compared.
        return;
    }

    addIssue(PDFContentIssue::Kind::StateLimit, getStateLimitIssueText(*getGraphicState()), true, index);

    const QString reason = getStateLimitReason(*getGraphicState());
    if (m_frames.size() > 1)
    {
        // Inside of open nodes: the span begins with the outermost of them, so that it holds
        // whole brackets (it ends with them, when the state is within the limit then - a Q
        // brings the dash pattern before its q back)
        startCaptureFromFrame(1, reason);
    }
    else
    {
        startCaptureAtOperation(reason);
    }

    captureOperation();
}

void PDFContentTreeBuilderProcessor::startCaptureBeforeOperation()
{
    // The span takes what is open (the outermost open node, or the path under construction);
    // structureOperation adds the operation to it then
    addIssue(PDFContentIssue::Kind::StateLimit, getStateLimitIssueText(*getGraphicState()), true, m_current.index);

    const QString reason = getStateLimitReason(*getGraphicState());
    if (m_frames.size() > 1)
    {
        startCaptureFromFrame(1, reason);
    }
    else if (m_leafMode == LeafMode::Path)
    {
        startCaptureFromPath(reason);
    }
    else
    {
        startCaptureAtOperation(reason);
    }
}

void PDFContentTreeBuilderProcessor::takeEndState()
{
    if (m_isEndStateTaken)
    {
        return;
    }

    m_isEndStateTaken = true;

    // The states of the builder are not refreshed while the state is over its limit
    // (refreshResolvedState): the state of the end is made here, once
    if (m_capture.isActive && isStateOverLimit(*getGraphicState()))
    {
        m_endStateOverLimit = createStateWithDashMarker();
    }
}

PDFContentResolvedStatePointer PDFContentTreeBuilderProcessor::createStateWithDashMarker() const
{
    // The copy holds the long array for a moment only
    PDFPageContentProcessorState copy = *getGraphicState();

    if (copy.getLineDashPattern().getDashArray().size() > MAXIMUM_DASH_LENGTH)
    {
        const PDFReal marker = std::numeric_limits<PDFReal>::quiet_NaN();
        PDFLineDashPattern pattern;
        pattern.setDashArray({ marker, marker });
        pattern.setDashOffset(marker);
        copy.setLineDashPattern(std::move(pattern));
    }

    // A color of too many components: the marker of one not-a-number component (item 4 of PB0S3)
    replaceColorsOverLimit(copy);

    copy.setTextMatrix(QTransform());
    copy.setTextLineMatrix(QTransform());
    copy.setStateFlags(PDFPageContentProcessorState::StateUnchanged);
    return std::make_shared<const PDFContentResolvedState>(copy);
}

void PDFContentTreeBuilderProcessor::performRestoreGraphicState(ProcessOrder order)
{
    // A restore outside of an operation of the content: the processor restores the states, which
    // the content left saved, or the state of its beginning (the end of a form) - the state of
    // the processor is the one of the end of the content for the last time
    if (order == ProcessOrder::BeforeOperation && m_isContentEntered && !m_isOverLimit && !m_current.isRecorded)
    {
        takeEndState();
    }
}

void PDFContentTreeBuilderProcessor::refreshResolvedState()
{
    const PDFPageContentProcessorState& state = *getGraphicState();

    if (m_resolved && isStateOverLimit(state))
    {
        // No state is made of a state over its limit, and nothing is compared with it: the
        // content is an opaque span there (startCaptureForStateLimit), whose nodes need no
        // state. The last state stays, until the processor is within the limit again.
        return;
    }

    if (m_resolved)
    {
        const PDFPageContentProcessorState& lastState = m_resolved->getState();
        if (lastState.getCurrentTransformationMatrix() == state.getCurrentTransformationMatrix() &&
            m_resolvedSoftMask == state.getSoftMask() &&
            PDFContentResolvedState::getDifference(lastState, state).isEmpty())
        {
            return;
        }
    }

    // The text matrices of the processor are not a part of the state of a node (the layout of
    // a text has its own ones by the standard): the shared states do not differ by them
    PDFPageContentProcessorState copy = state;
    copy.setTextMatrix(QTransform());
    copy.setTextLineMatrix(QTransform());
    if (isStateOverLimit(copy))
    {
        // Only the first state of a content can be made of such a state (the initial state of
        // a form given by the caller): no state holds a value over the limit. A color has no
        // short value of its own space: the marker, which equals no color
        if (copy.getLineDashPattern().getDashArray().size() > MAXIMUM_DASH_LENGTH)
        {
            copy.setLineDashPattern(PDFLineDashPattern());
        }
        replaceColorsOverLimit(copy);
    }
    copy.setStateFlags(PDFPageContentProcessorState::StateUnchanged);

    m_resolved = std::make_shared<const PDFContentResolvedState>(copy);
    m_resolvedSoftMask = state.getSoftMask();
}

const PDFContentSymbolicStatePointer& PDFContentTreeBuilderProcessor::getSymbolicState()
{
    if (!m_symbolic)
    {
        m_symbolic = std::make_shared<const PDFContentSymbolicState>(m_symbolicValue);
    }

    return m_symbolic;
}

void PDFContentTreeBuilderProcessor::setSource(Property property, PDFContentPropertySource source)
{
    m_symbolicValue = m_symbolicValue.withSource(property, std::move(source));
    m_symbolic.reset();
}

void PDFContentTreeBuilderProcessor::updateSymbolicState(const CurrentOperation& current)
{
    const Operator contentOperator = current.contentOperator;

    // q and Q keep the symbolic state as the processor keeps its state. A refused Q restores
    // nothing.
    if (contentOperator == Operator::SaveGraphicState)
    {
        m_savedStates.push_back(SavedState{ getSymbolicState(), m_resolved, m_resolvedSoftMask });
        return;
    }

    if (contentOperator == Operator::RestoreGraphicState)
    {
        if (!current.isFailed && !m_savedStates.empty())
        {
            SavedState savedState = std::move(m_savedStates.back());
            m_savedStates.pop_back();

            m_symbolicValue = *savedState.symbolic;
            m_symbolic = std::move(savedState.symbolic);

            // The state before q is shared again, when the processor is back in it
            const PDFPageContentProcessorState& state = *getGraphicState();
            if (savedState.resolved &&
                savedState.resolved->getCurrentTransformationMatrix() == state.getCurrentTransformationMatrix() &&
                savedState.softMask == state.getSoftMask() &&
                PDFContentResolvedState::getDifference(savedState.resolved->getState(), state).isEmpty())
            {
                m_resolved = std::move(savedState.resolved);
                m_resolvedSoftMask = savedState.softMask;
            }
        }
        return;
    }

    if (current.category != Category::State && contentOperator != Operator::TextSetLeadingAndMoveByOffset && contentOperator != Operator::TextSetSpacingAndShowText)
    {
        return;
    }

    // An operator, which has failed, is the source only of what it has changed nevertheless
    // (Tf with a font, which cannot be read, sets the size)
    PDFContentStatePropertySet changedProperties;
    const PDFContentStatePropertySet* changed = nullptr;
    if (current.isFailed)
    {
        if (m_resolved)
        {
            changedProperties = PDFContentResolvedState::getDifference(m_resolved->getState(), *getGraphicState());
            if (m_resolvedSoftMask != getGraphicState()->getSoftMask())
            {
                changedProperties.insert(Property::SoftMask);
            }
        }
        changed = &changedProperties;
    }

    auto isSet = [changed](Property property)
    {
        return !changed || changed->contains(property);
    };

    auto setOperatorSource = [this, &current, &isSet](Property property)
    {
        if (isSet(property))
        {
            PDFContentPropertySource source;
            source.origin = PDFContentPropertySource::Origin::Operator;
            source.operationIndex = current.index;
            setSource(property, std::move(source));
        }
    };

    const Operands& operands = m_operations.getOperation(current.index).getOperands();

    switch (contentOperator)
    {
        case Operator::SetLineWidth:
            setOperatorSource(Property::LineWidth);
            break;

        case Operator::SetLineCap:
            setOperatorSource(Property::LineCap);
            break;

        case Operator::SetLineJoin:
            setOperatorSource(Property::LineJoin);
            break;

        case Operator::SetMitterLimit:
            setOperatorSource(Property::MiterLimit);
            break;

        case Operator::SetLineDashPattern:
            setOperatorSource(Property::DashPattern);
            break;

        case Operator::SetRenderingIntent:
            setOperatorSource(Property::RenderingIntent);
            break;

        case Operator::SetFlatness:
            setOperatorSource(Property::Flatness);
            break;

        case Operator::SetGraphicState:
            updateSymbolicExtGState(current, operands, changed);
            break;

        case Operator::ColorSetStrokingColorSpace:
        case Operator::ColorSetStrokingColor:
        case Operator::ColorSetStrokingColorN:
        case Operator::ColorSetDeviceGrayStroking:
        case Operator::ColorSetDeviceRGBStroking:
        case Operator::ColorSetDeviceCMYKStroking:
            if (!reportIgnoredColorOperator(current) && isSet(Property::StrokeColor))
            {
                updateSymbolicColor(Property::StrokeColor, current, operands);
            }
            break;

        case Operator::ColorSetFillingColorSpace:
        case Operator::ColorSetFillingColor:
        case Operator::ColorSetFillingColorN:
        case Operator::ColorSetDeviceGrayFilling:
        case Operator::ColorSetDeviceRGBFilling:
        case Operator::ColorSetDeviceCMYKFilling:
            if (!reportIgnoredColorOperator(current) && isSet(Property::FillColor))
            {
                updateSymbolicColor(Property::FillColor, current, operands);
            }
            break;

        case Operator::TextSetCharacterSpacing:
            setOperatorSource(Property::CharacterSpacing);
            break;

        case Operator::TextSetWordSpacing:
            setOperatorSource(Property::WordSpacing);
            break;

        case Operator::TextSetHorizontalScale:
            setOperatorSource(Property::HorizontalScaling);
            break;

        case Operator::TextSetLeading:
        case Operator::TextSetLeadingAndMoveByOffset:
            setOperatorSource(Property::Leading);
            break;

        case Operator::TextSetFontAndFontSize:
        {
            if (isSet(Property::Font))
            {
                PDFContentPropertySource source;
                source.origin = PDFContentPropertySource::Origin::Operator;
                source.operationIndex = current.index;
                source.resourceName = getName(operands, 0);
                if (const PDFDictionary* dictionary = getFontDictionary())
                {
                    source.object = dictionary->get(source.resourceName);
                }
                setSource(Property::Font, std::move(source));
            }
            break;
        }

        case Operator::TextSetRenderMode:
            setOperatorSource(Property::TextRenderingMode);
            break;

        case Operator::TextSetRise:
            setOperatorSource(Property::TextRise);
            break;

        case Operator::TextSetSpacingAndShowText:
            // aw ac string ": the word spacing and the character spacing (9.4.3, Table 107)
            setOperatorSource(Property::WordSpacing);
            setOperatorSource(Property::CharacterSpacing);
            break;

        default:
            break;
    }
}

bool PDFContentTreeBuilderProcessor::reportIgnoredColorOperator(const CurrentOperation& current)
{
    if (!isColorOperatorsIgnored())
    {
        return false;
    }

    // ISO 32000-2, 8.7.3.3: the cell of an uncolored tiling pattern specifies no color, the
    // processor does nothing for a color operator there. The operation stays in the content
    // as it is (it is written back with its bytes), but it is the source of no color and it
    // uses no pattern. The container says so once, at the first such operation.
    if (!m_isIgnoredColorOperatorReported)
    {
        m_isIgnoredColorOperatorReported = true;
        addIssue(PDFContentIssue::Kind::IgnoredColorOperator,
                 PDFTranslationContext::tr("The cell of an uncolored tiling pattern has color operators, they have no effect."),
                 true, current.index);
    }

    return true;
}

void PDFContentTreeBuilderProcessor::updateSymbolicColor(Property property, const CurrentOperation& current, const Operands& operands)
{
    PDFContentPropertySource source;
    source.origin = PDFContentPropertySource::Origin::Operator;
    source.operationIndex = current.index;

    switch (current.contentOperator)
    {
        case Operator::ColorSetStrokingColorSpace:
        case Operator::ColorSetFillingColorSpace:
        {
            // The color space; the color is its initial one and no pattern is set (8.6.8)
            const QByteArray name = getName(operands, 0);
            const PDFDictionary* dictionary = getColorSpaceDictionary();
            if (isReservedColorSpaceName(name) || !dictionary || !dictionary->hasKey(name))
            {
                source.object = PDFObject::createName(name);
                if (!isReservedColorSpaceName(name))
                {
                    source.resourceName = name;
                }
            }
            else
            {
                source.resourceName = name;
                source.object = dictionary->get(name);
            }
            break;
        }

        case Operator::ColorSetStrokingColor:
        case Operator::ColorSetStrokingColorN:
        case Operator::ColorSetFillingColor:
        case Operator::ColorSetFillingColorN:
        {
            // The color in the current color space: the space stays as it was set (for an
            // inherited one nothing is known), the pattern is the last operand, when it is a name
            const PDFContentPropertySource& oldSource = m_symbolicValue.getSource(property);
            if (oldSource.origin == PDFContentPropertySource::Origin::Operator)
            {
                source.resourceName = oldSource.resourceName;
                source.object = oldSource.object;
            }

            const bool isPatternOperator = current.contentOperator == Operator::ColorSetStrokingColorN || current.contentOperator == Operator::ColorSetFillingColorN;
            if (isPatternOperator && !operands.empty())
            {
                source.patternName = getName(operands, operands.size() - 1);
                if (!source.patternName.isEmpty())
                {
                    if (const PDFDictionary* dictionary = getPatternDictionary())
                    {
                        source.patternObject = dictionary->get(source.patternName);
                    }

                    // A tiling pattern is a container of its own: the session builds it, when it is entered
                    if (source.patternObject.isReference())
                    {
                        const PDFObject& patternObject = getDocument()->getObject(source.patternObject);
                        if (patternObject.isStream())
                        {
                            PDFDocumentDataLoaderDecorator loader(getDocument());
                            const PDFObjectReference reference = source.patternObject.getReference();
                            if (loader.readIntegerFromDictionary(patternObject.getStream()->getDictionary(), "PatternType", 0) == 1 &&
                                m_usedPatternReferences.insert(reference).second)
                            {
                                m_usedPatternCells.push_back(PDFContentContainerKey::createPatternCell(reference));
                            }
                        }
                    }
                }
            }
            break;
        }

        case Operator::ColorSetDeviceGrayStroking:
        case Operator::ColorSetDeviceGrayFilling:
            source.object = PDFObject::createName("DeviceGray");
            break;

        case Operator::ColorSetDeviceRGBStroking:
        case Operator::ColorSetDeviceRGBFilling:
            source.object = PDFObject::createName("DeviceRGB");
            break;

        case Operator::ColorSetDeviceCMYKStroking:
        case Operator::ColorSetDeviceCMYKFilling:
            source.object = PDFObject::createName("DeviceCMYK");
            break;

        default:
            break;
    }

    setSource(property, std::move(source));
}

void PDFContentTreeBuilderProcessor::updateSymbolicExtGState(const CurrentOperation& current, const Operands& operands, const PDFContentStatePropertySet* changedProperties)
{
    const QByteArray name = getName(operands, 0);
    const PDFDictionary* resourceDictionary = getExtendedGraphicStateDictionary();
    if (name.isEmpty() || !resourceDictionary)
    {
        return;
    }

    const PDFDictionary* dictionary = getDocument()->getDictionaryFromObject(resourceDictionary->get(name));
    if (!dictionary)
    {
        return;
    }

    // The keys of a graphics state parameter dictionary and what they set (ISO 32000-2, 8.4.5,
    // Table 57). gs sets only the parameters, whose keys are present. BG2, UCR2 and TR2 come
    // after BG, UCR and TR: when both are present, the second one takes precedence.
    static constexpr std::pair<const char*, Property> keys[] =
    {
        { "LW", Property::LineWidth },
        { "LC", Property::LineCap },
        { "LJ", Property::LineJoin },
        { "ML", Property::MiterLimit },
        { "D", Property::DashPattern },
        { "RI", Property::RenderingIntent },
        { "OP", Property::OverprintStroking },
        { "op", Property::OverprintFilling },
        { "OPM", Property::OverprintMode },
        { "Font", Property::Font },
        { "BG", Property::BlackGeneration },
        { "BG2", Property::BlackGeneration },
        { "UCR", Property::UndercolorRemoval },
        { "UCR2", Property::UndercolorRemoval },
        { "TR", Property::Transfer },
        { "TR2", Property::Transfer },
        { "HT", Property::Halftone },
        { "FL", Property::Flatness },
        { "SM", Property::Smoothness },
        { "SA", Property::StrokeAdjustment },
        { "BM", Property::BlendMode },
        { "SMask", Property::SoftMask },
        { "CA", Property::AlphaStroking },
        { "ca", Property::AlphaFilling },
        { "AIS", Property::AlphaIsShape },
        { "TK", Property::TextKnockout },
        { "UseBlackPtComp", Property::BlackPointCompensation },
        { "HTO", Property::HalftoneOrigin }
    };

    auto setKeySource = [this, &current, &name, dictionary, changedProperties](const char* key, Property property)
    {
        if (changedProperties && !changedProperties->contains(property))
        {
            return;
        }

        PDFContentPropertySource source;
        source.origin = PDFContentPropertySource::Origin::ExtGState;
        source.operationIndex = current.index;
        source.resourceName = name;
        source.extGStateKey = key;
        source.object = dictionary->get(key);

        if (property == Property::Font)
        {
            // The value of the key is the array [font size] (Table 57), the object of the
            // property is the font: what is written for it later is "/name size Tf", and the
            // name must be a name of the font, never of the array. The size is the font size of
            // the resolved state. An array without a font the processor can take sets nothing,
            // as the processor does (the entry is handled as an absent one).
            source.object = getExtGStateFont(source.object);
            if (source.object.isNull())
            {
                return;
            }
        }

        setSource(property, std::move(source));
    };

    for (const auto& [key, property] : keys)
    {
        // An entry with a value of another type than Table 57 gives sets nothing, as an entry,
        // which is absent
        if (!dictionary->hasKey(key) || !isExtGStateValueUsable(property, key, getDocument()->getObject(dictionary->get(key))))
        {
            continue;
        }

        setKeySource(key, property);

        if (property == Property::OverprintStroking)
        {
            // "If this entry is absent, the OP entry, if any, shall also set this parameter"
            // (Table 57, the key op): OP sets both overprint parameters; the key op comes
            // after it in the table and replaces the source of the second one
            setKeySource(key, Property::OverprintFilling);
        }

        if (property == Property::SoftMask && (!changedProperties || changedProperties->contains(Property::SoftMask)))
        {
            // The space of the mask is the space of the moment it is set (11.6.5.1): the
            // matrix is kept with it (R-6)
            m_symbolicValue = m_symbolicValue.withSoftMaskMatrix(getGraphicState()->getCurrentTransformationMatrix());
            m_symbolic.reset();
        }
    }
}

PDFObject PDFContentTreeBuilderProcessor::getExtGStateFont(const PDFObject& value) const
{
    // [font size]: "font shall be an indirect reference to a font dictionary" (ISO 32000-2,
    // Table 57). The processor takes a name in its place too and looks it up in the font
    // resources, as Tf does (NOTES.txt): the font is the value of that name then.
    const PDFObject& arrayObject = getDocument()->getObject(value);
    if (!arrayObject.isArray() || arrayObject.getArray()->getCount() != 2)
    {
        return PDFObject();
    }

    const PDFObject& font = arrayObject.getArray()->getItem(0);
    if (font.isReference())
    {
        return font;
    }

    if (font.isName())
    {
        const PDFDictionary* fontDictionary = getFontDictionary();
        const QByteArray fontName = font.getString();
        if (fontDictionary && fontDictionary->hasKey(fontName))
        {
            return fontDictionary->get(fontName);
        }
    }

    return PDFObject();
}

bool PDFContentTreeBuilderProcessor::isExtGStateValueUsable(Property property, const char* key, const PDFObject& value)
{
    // The types of the values of a graphics state parameter dictionary (ISO 32000-2, 8.4.5,
    // Table 57). The line cap and the line join are integers there; a real number is taken too,
    // as the processor takes it.
    const bool isNumber = value.isInt() || value.isReal();
    const bool isFunction = value.isDictionary() || value.isStream();

    switch (property)
    {
        case Property::LineWidth:
        case Property::LineCap:
        case Property::LineJoin:
        case Property::MiterLimit:
        case Property::Flatness:
        case Property::Smoothness:
        case Property::AlphaStroking:
        case Property::AlphaFilling:
            return isNumber;

        case Property::OverprintMode:
            return value.isInt();

        case Property::OverprintStroking:
        case Property::OverprintFilling:
        case Property::StrokeAdjustment:
        case Property::AlphaIsShape:
        case Property::TextKnockout:
            return value.isBool();

        case Property::RenderingIntent:
        case Property::BlackPointCompensation:
            return value.isName();

        case Property::DashPattern:
        case Property::HalftoneOrigin:
            return value.isArray();

        case Property::Font:
            // [font size]
            return value.isArray() && value.getArray()->getCount() == 2;

        case Property::BlackGeneration:
        case Property::UndercolorRemoval:
            // BG, UCR: a function; BG2, UCR2: a function or the name Default
            return isFunction || (value.isName() && QByteArrayView(key).endsWith('2'));

        case Property::Transfer:
            // A function, an array of four functions or a name
            return isFunction || value.isArray() || value.isName();

        case Property::Halftone:
            // A halftone dictionary or stream, or the name Default
            return isFunction || value.isName();

        case Property::BlendMode:
            return value.isName() || value.isArray();

        case Property::SoftMask:
            // A soft-mask dictionary, or the name None
            return value.isDictionary() || value.isName();

        default:
            return false;
    }
}

PDFContentStateLeak PDFContentTreeBuilderProcessor::computeLeak(const Snapshot& begin, const Snapshot& end, bool isClipChanged) const
{
    PDFContentStateLeak leak;
    leak.isClipChanged = isClipChanged;

    if (begin.resolved && end.resolved && begin.resolved != end.resolved)
    {
        leak.properties = PDFContentResolvedState::getDifference(*begin.resolved, *end.resolved);
        leak.isTransformChanged = begin.resolved->getCurrentTransformationMatrix() != end.resolved->getCurrentTransformationMatrix();
    }

    if (begin.symbolic && end.symbolic && begin.symbolic != end.symbolic)
    {
        // The soft mask: a resolved state says only, whether one is set; another gs with
        // a mask is another mask (or the same one in another space). A mask is set and removed
        // by gs only, so its source tells every change of it.
        const PDFContentPropertySource& beginMask = begin.symbolic->getSource(Property::SoftMask);
        const PDFContentPropertySource& endMask = end.symbolic->getSource(Property::SoftMask);
        if (beginMask.origin != endMask.origin || beginMask.operationIndex != endMask.operationIndex)
        {
            leak.properties.insert(Property::SoftMask);
        }

        if (m_isInheritedUnknown)
        {
            // The value of an inherited property of a form is the value of one of its uses
            // only: a property set inside differs from it, whatever the two values are
            for (int i = 0; i < int(Property::Count); ++i)
            {
                const Property property = Property(i);
                if (begin.symbolic->isInherited(property) && !end.symbolic->isInherited(property))
                {
                    leak.properties.insert(property);
                }
            }
        }
    }

    if (!leak.isEmpty())
    {
        leak.endState = end.resolved;
        leak.endSymbolicState = end.symbolic;
    }

    return leak;
}

std::unique_ptr<PDFContentTreeBuilderProcessor::BuildNode> PDFContentTreeBuilderProcessor::createNode(PDFContentNodeKind kind, size_t firstOperation, size_t operationCount)
{
    std::unique_ptr<BuildNode> node = std::make_unique<BuildNode>();
    node->data.kind = kind;
    node->data.prefix = PDFContentOperationSpan{ firstOperation, operationCount };
    node->data.state = m_resolved;
    node->data.symbolicState = getSymbolicState();
    return node;
}

void PDFContentTreeBuilderProcessor::structureOperation()
{
    if (m_capture.isActive)
    {
        captureOperation();
        return;
    }

    // A chain q q ... Q Q is one node only when nothing comes between its operators: a group,
    // which got a part of its Q, is divided, before anything else is added
    if (m_current.category != Category::Restore)
    {
        splitPartialGroup();
    }

    if (m_leafMode == LeafMode::Path)
    {
        switch (m_current.category)
        {
            case Category::PathConstruct:
            case Category::PathClip:
            case Category::PathPaint:
                addToPath();
                return;

            default:
                // A foreign operator between the construction of a path and its painting
                // operator: the whole path is opaque
                addIssue(PDFContentIssue::Kind::InvalidPath, PDFTranslationContext::tr("An operator, which is not allowed there, is inside of a path."), true, m_current.index);
                startCaptureFromPath(PDFTranslationContext::tr("Path with an operator, which is not allowed inside of a path"));
                captureOperation();
                return;
        }
    }

    if (m_leafMode == LeafMode::StateRun && !isRunCategory(m_current.category, isInsideText()))
    {
        closeOpenLeaf();
    }

    if (m_current.category == Category::Other)
    {
        // An operation the tree cannot express: one opaque node, wherever it is
        if (m_current.isOperandLimit)
        {
            createOpaqueLeaf(PDFTranslationContext::tr("Operation with too many operands"));
        }
        else if (m_current.contentOperator == Operator::Invalid)
        {
            // Between BX and EX an unknown operator is no error (ISO 32000-2, 7.8.2); it is
            // opaque everywhere
            if (m_compatibilityDepth == 0)
            {
                addIssue(PDFContentIssue::Kind::UnknownOperator, PDFTranslationContext::tr("Unknown operator."), true, m_current.index);
            }
            createOpaqueLeaf(PDFTranslationContext::tr("Unknown operator"));
        }
        else
        {
            createOpaqueLeaf(PDFTranslationContext::tr("Operator, which is not allowed in this content"));
        }
        return;
    }

    if (isInsideText())
    {
        structureOperationInsideText();
    }
    else
    {
        structureOperationOutsideText();
    }
}

void PDFContentTreeBuilderProcessor::structureOperationInsideText()
{
    switch (m_current.category)
    {
        case Category::State:
        case Category::TextPosition:
        case Category::Compatibility:
            addToStateRun();
            break;

        case Category::TextShow:
            createLeaf(PDFContentNodeKind::TextShow);
            break;

        case Category::MarkedPoint:
            createLeaf(PDFContentNodeKind::MarkedPoint);
            break;

        case Category::MarkedBegin:
            openFrame(PDFContentNodeKind::MarkedContent);
            break;

        case Category::MarkedEnd:
            closeBracket(PDFContentNodeKind::MarkedContent);
            break;

        case Category::TextEnd:
            closeBracket(PDFContentNodeKind::TextObject);
            break;

        default:
        {
            // BT, q, Q, cm, Do, sh, BI and the path operators are not allowed inside of a text
            // object (8.2, Figure 9): the whole text object is opaque
            addIssue(PDFContentIssue::Kind::InvalidTextObject, PDFTranslationContext::tr("An operator, which is not allowed there, is inside of a text object."), true, m_current.index);
            startCaptureFromFrame(size_t(m_textFrameIndex), PDFTranslationContext::tr("Text object with an operator, which is not allowed inside of a text object"));
            captureOperation();
            break;
        }
    }
}

void PDFContentTreeBuilderProcessor::structureOperationOutsideText()
{
    switch (m_current.category)
    {
        case Category::Save:
        {
            BuildNode* top = m_frames.back().get();
            if (m_frames.size() > 1 && top->data.kind == PDFContentNodeKind::Group && top->children.empty() && top->closedCount == 0 &&
                top->data.prefix.first + top->data.prefix.count == m_current.index &&
                getOpenBracketCount() < size_t(PDFContentContainer::MAXIMUM_DEPTH))
            {
                // q right after q: the same node
                ++top->data.saveDepth;
                ++top->data.prefix.count;
            }
            else
            {
                openFrame(PDFContentNodeKind::Group);
            }
            break;
        }

        case Category::Restore:
            closeBracket(PDFContentNodeKind::Group);
            break;

        case Category::Transform:
        case Category::State:
        case Category::Compatibility:
            addToStateRun();
            break;

        case Category::TextBegin:
            openFrame(PDFContentNodeKind::TextObject);
            break;

        case Category::TextEnd:
            closeBracket(PDFContentNodeKind::TextObject);
            break;

        case Category::TextPosition:
        case Category::TextShow:
            addIssue(PDFContentIssue::Kind::InvalidTextObject, PDFTranslationContext::tr("A text operator is outside of a text object."), true, m_current.index);
            createOpaqueLeaf(PDFTranslationContext::tr("Text operator outside of a text object"));
            break;

        case Category::PathConstruct:
        case Category::PathClip:
        case Category::PathPaint:
        {
            m_openLeaf = createNode(PDFContentNodeKind::Path, m_current.index, 0);
            m_leafMode = LeafMode::Path;
            m_isPathInvalid = false;
            m_hasCurrentPoint = false;
            addToPath();
            break;
        }

        case Category::XObject:
        {
            const Operands& operands = m_operations.getOperation(m_current.index).getOperands();
            const XObjectInfo info = getXObjectInfo(operands);
            if (info.isUnreadable)
            {
                createOpaqueLeaf(PDFTranslationContext::tr("XObject, whose dictionary cannot be read"));
            }
            else if (info.kind == PDFContentNodeKind::Opaque)
            {
                createOpaqueLeaf(info.isDefined ? PDFTranslationContext::tr("XObject of an unknown type") : PDFTranslationContext::tr("XObject, which is not defined"));
            }
            else
            {
                createLeaf(info.kind);
                PDFContentNode::Data& data = m_current.leaf->data;
                data.xobject = info.reference;
                data.resourceName = info.name;
                data.boundingBox = m_resolved->getCurrentTransformationMatrix().mapRect(info.boundingBox);
            }
            break;
        }

        case Category::Shading:
        {
            createLeaf(PDFContentNodeKind::Shading);
            m_current.leaf->data.resourceName = getName(m_operations.getOperation(m_current.index).getOperands(), 0);
            break;
        }

        case Category::InlineImage:
        {
            if (!getInlineImageRange().isValid())
            {
                // The end of the image is not known: the bytes after BI cannot be divided into
                // operations reliably, the rest of the content is opaque
                startCaptureAtOperation(PDFTranslationContext::tr("Inline image without an end and the content after it"));
                captureOperation();
            }
            else
            {
                createLeaf(PDFContentNodeKind::Image);
                PDFContentNode::Data& data = m_current.leaf->data;
                data.isInlineImage = true;
                data.boundingBox = m_resolved->getCurrentTransformationMatrix().mapRect(QRectF(0.0, 0.0, 1.0, 1.0));
            }
            break;
        }

        case Category::MarkedPoint:
            createLeaf(PDFContentNodeKind::MarkedPoint);
            break;

        case Category::MarkedBegin:
            openFrame(PDFContentNodeKind::MarkedContent);
            break;

        case Category::MarkedEnd:
            closeBracket(PDFContentNodeKind::MarkedContent);
            break;

        case Category::Other:
            // Never reached: structureOperation has made the node
            break;
    }
}

void PDFContentTreeBuilderProcessor::addToStateRun()
{
    if (m_leafMode != LeafMode::StateRun)
    {
        m_openLeaf = createNode(PDFContentNodeKind::StateRun, m_current.index, 0);
        m_leafMode = LeafMode::StateRun;
    }

    ++m_openLeaf->data.prefix.count;
}

void PDFContentTreeBuilderProcessor::addToPath()
{
    PDFContentNode::Data& data = m_openLeaf->data;
    ++data.prefix.count;

    const Operands& operands = m_operations.getOperation(m_current.index).getOperands();
    PDFReal numbers[6] = { };

    // The geometry by the operands (8.5.2.1, Table 58). The current point is the one of the
    // standard: no operator except m and re can begin a path.
    switch (m_current.contentOperator)
    {
        case Operator::MoveCurrentPoint:
        {
            if (readNumbers(operands, 2, numbers))
            {
                m_currentPoint = QPointF(numbers[0], numbers[1]);
                m_subpathStart = m_currentPoint;
                m_hasCurrentPoint = true;
                data.path.moveTo(m_currentPoint);
            }
            else
            {
                m_isPathInvalid = true;
            }
            break;
        }

        case Operator::LineTo:
        {
            if (m_hasCurrentPoint && readNumbers(operands, 2, numbers))
            {
                m_currentPoint = QPointF(numbers[0], numbers[1]);
                data.path.lineTo(m_currentPoint);
            }
            else
            {
                m_isPathInvalid = true;
            }
            break;
        }

        case Operator::Bezier123To:
        {
            if (m_hasCurrentPoint && readNumbers(operands, 6, numbers))
            {
                m_currentPoint = QPointF(numbers[4], numbers[5]);
                data.path.curveTo(QPointF(numbers[0], numbers[1]), QPointF(numbers[2], numbers[3]), m_currentPoint);
            }
            else
            {
                m_isPathInvalid = true;
            }
            break;
        }

        case Operator::Bezier23To:
        {
            // v: the current point is the first control point
            if (m_hasCurrentPoint && readNumbers(operands, 4, numbers))
            {
                const QPointF control1 = m_currentPoint;
                m_currentPoint = QPointF(numbers[2], numbers[3]);
                data.path.curveTo(control1, QPointF(numbers[0], numbers[1]), m_currentPoint);
            }
            else
            {
                m_isPathInvalid = true;
            }
            break;
        }

        case Operator::Bezier13To:
        {
            // y: the end point is the second control point
            if (m_hasCurrentPoint && readNumbers(operands, 4, numbers))
            {
                m_currentPoint = QPointF(numbers[2], numbers[3]);
                data.path.curveTo(QPointF(numbers[0], numbers[1]), m_currentPoint, m_currentPoint);
            }
            else
            {
                m_isPathInvalid = true;
            }
            break;
        }

        case Operator::EndSubpath:
        {
            // h: "if the current subpath is already closed, h shall do nothing"; without a
            // subpath there is nothing to close
            if (m_hasCurrentPoint)
            {
                data.path.close();
                m_currentPoint = m_subpathStart;
            }
            break;
        }

        case Operator::Rectangle:
        {
            if (readNumbers(operands, 4, numbers))
            {
                data.path.addRectangle(QRectF(numbers[0], numbers[1], numbers[2], numbers[3]));
                m_currentPoint = QPointF(numbers[0], numbers[1]);
                m_subpathStart = m_currentPoint;
                m_hasCurrentPoint = true;
            }
            else
            {
                m_isPathInvalid = true;
            }
            break;
        }

        case Operator::ClipWinding:
            data.paint.clip = PDFContentPathPaint::Clip::NonZero;
            break;

        case Operator::ClipEvenOdd:
            data.paint.clip = PDFContentPathPaint::Clip::EvenOdd;
            break;

        // The painting operators (8.5.3.1, Table 59)
        case Operator::PathStroke:
            data.paint.stroke = true;
            break;

        case Operator::PathCloseStroke:
            data.paint.stroke = true;
            data.paint.closeBeforePaint = true;
            break;

        case Operator::PathFillWinding:
        case Operator::PathFillWinding2:
            data.paint.fill = true;
            break;

        case Operator::PathFillEvenOdd:
            data.paint.fill = true;
            data.paint.fillEvenOdd = true;
            break;

        case Operator::PathFillStrokeWinding:
            data.paint.fill = true;
            data.paint.stroke = true;
            break;

        case Operator::PathFillStrokeEvenOdd:
            data.paint.fill = true;
            data.paint.stroke = true;
            data.paint.fillEvenOdd = true;
            break;

        case Operator::PathCloseFillStrokeWinding:
            data.paint.fill = true;
            data.paint.stroke = true;
            data.paint.closeBeforePaint = true;
            break;

        case Operator::PathCloseFillStrokeEvenOdd:
            data.paint.fill = true;
            data.paint.stroke = true;
            data.paint.fillEvenOdd = true;
            data.paint.closeBeforePaint = true;
            break;

        case Operator::PathClear:
        default:
            break;
    }

    if (m_current.category == Category::PathPaint)
    {
        m_current.afterAction = AfterAction::FinishPath;
    }
}

void PDFContentTreeBuilderProcessor::closeOpenLeaf()
{
    if (m_leafMode == LeafMode::None)
    {
        return;
    }

    std::unique_ptr<BuildNode> leaf = std::move(m_openLeaf);
    const LeafMode mode = m_leafMode;
    m_leafMode = LeafMode::None;

    if (mode == LeafMode::Path)
    {
        // A path without a painting operator (the content has ended)
        m_isPathInvalid = true;
        finishPath(std::move(leaf));
        return;
    }

    leaf->data.leak = computeLeak(Snapshot{ leaf->data.state, leaf->data.symbolicState }, getSnapshot(), false);
    appendChild(std::move(leaf));
}

void PDFContentTreeBuilderProcessor::finishPath(std::unique_ptr<BuildNode> node)
{
    PDFContentNode::Data& data = node->data;
    const Snapshot end = getSnapshot();

    if (m_isPathInvalid)
    {
        // The operands do not give the path the processor has painted: the node is written
        // back as it is, never from its geometry
        addIssue(PDFContentIssue::Kind::InvalidPath, PDFTranslationContext::tr("A path cannot be read."), true, data.prefix.first);
        data.kind = PDFContentNodeKind::Opaque;
        data.opaqueReason = PDFTranslationContext::tr("Path, which cannot be read");
        data.path = PDFContentPath();
        data.paint = PDFContentPathPaint();
        data.leak.isUnknown = true;
        data.leak.endState = end.resolved;
        data.leak.endSymbolicState = end.symbolic;
    }
    else
    {
        data.hasClipEffect = data.paint.clip != PDFContentPathPaint::Clip::None;
        data.leak = computeLeak(Snapshot{ data.state, data.symbolicState }, end, data.hasClipEffect);
        if (data.state)
        {
            data.boundingBox = data.path.transformed(data.state->getCurrentTransformationMatrix()).getBoundingBox();
        }
    }

    m_isPathInvalid = false;
    m_hasCurrentPoint = false;
    appendChild(std::move(node));
}

void PDFContentTreeBuilderProcessor::createLeaf(PDFContentNodeKind kind)
{
    m_current.leaf = createNode(kind, m_current.index, 1);
    m_current.afterAction = AfterAction::FinishLeaf;
}

void PDFContentTreeBuilderProcessor::createOpaqueLeaf(const QString& reason)
{
    createLeaf(PDFContentNodeKind::Opaque);
    m_current.leaf->data.opaqueReason = reason;
}

void PDFContentTreeBuilderProcessor::openFrame(PDFContentNodeKind kind)
{
    if (getOpenBracketCount() >= size_t(PDFContentContainer::MAXIMUM_DEPTH))
    {
        // Deeper content is one opaque node up to its end
        addIssue(PDFContentIssue::Kind::DepthLimit, PDFTranslationContext::tr("The content is nested too deeply."), true, m_current.index);
        startCaptureAtOperation(PDFTranslationContext::tr("Content nested too deeply"));
        captureOperation();
        return;
    }

    std::unique_ptr<BuildNode> frame = createNode(kind, m_current.index, 1);

    switch (kind)
    {
        case PDFContentNodeKind::Group:
            frame->data.saveDepth = 1;
            break;

        case PDFContentNodeKind::MarkedContent:
            m_current.afterAction = AfterAction::FillFrame;
            break;

        case PDFContentNodeKind::TextObject:
        {
            m_textFrameIndex = int(m_frames.size());
            m_isTextClipping = false;
            m_isTextPositionUnknown = false;
            m_isTextLineMatrixUnknown = false;
            if (m_options.layoutText)
            {
                // The text matrices of every text object start at the identity (9.4.1)
                m_textLayout.emplace();
                m_textLayout->beginText();
            }
            break;
        }

        default:
            break;
    }

    m_frames.push_back(std::move(frame));
}

void PDFContentTreeBuilderProcessor::closeBracket(PDFContentNodeKind kind)
{
    BuildNode* top = m_frames.back().get();
    if (m_frames.size() > 1 && top->data.kind == kind)
    {
        PDFContentOperationSpan& suffix = top->data.suffix;
        if (suffix.count == 0)
        {
            suffix.first = m_current.index;
        }
        ++suffix.count;

        if (kind == PDFContentNodeKind::Group)
        {
            // A chain of q is closed by as many Q
            ++top->closedCount;
            if (top->closedCount < top->data.saveDepth)
            {
                return;
            }
        }

        m_current.afterAction = AfterAction::CloseFrame;
        return;
    }

    // The bracket of a node below the top: the brackets cross each other (BDC q EMC Q). The
    // smallest span, which covers them, is one opaque node.
    for (size_t i = m_frames.size() - 1; i >= 1; --i)
    {
        if (m_frames[i]->data.kind == kind)
        {
            addIssue(PDFContentIssue::Kind::CrossedBrackets, PDFTranslationContext::tr("The brackets of the content cross each other."), true, m_current.index);
            startCaptureFromFrame(i, PDFTranslationContext::tr("Content with crossed brackets"));
            captureOperation();
            return;
        }
    }

    // An end without a beginning
    addIssue(PDFContentIssue::Kind::UnbalancedEnd, PDFTranslationContext::tr("An end of a part of the content has no beginning."), true, m_current.index);
    createOpaqueLeaf(PDFTranslationContext::tr("End without a beginning"));
}

void PDFContentTreeBuilderProcessor::closeTopFrame(const Snapshot& end)
{
    Q_ASSERT(m_frames.size() > 1);

    std::unique_ptr<BuildNode> frame = std::move(m_frames.back());
    m_frames.pop_back();

    PDFContentNode::Data& data = frame->data;
    switch (data.kind)
    {
        case PDFContentNodeKind::Group:
            // q ... Q is balanced: nothing leaks
            break;

        case PDFContentNodeKind::MarkedContent:
        {
            // The leak of its children; a clip of a child reaches the nodes after the sequence
            bool isClipChanged = false;
            for (const std::unique_ptr<BuildNode>& child : frame->children)
            {
                isClipChanged = isClipChanged || child->data.leak.isClipChanged;
            }
            data.leak = computeLeak(Snapshot{ data.state, data.symbolicState }, end, isClipChanged);
            break;
        }

        case PDFContentNodeKind::TextObject:
        {
            // The clip of a text of the rendering modes 4 to 7 applies from ET (9.3.6)
            data.clipsByText = m_isTextClipping;
            data.hasClipEffect = m_isTextClipping;
            data.leak = computeLeak(Snapshot{ data.state, data.symbolicState }, end, m_isTextClipping);
            m_textFrameIndex = -1;
            m_isTextClipping = false;
            break;
        }

        default:
            break;
    }

    appendChild(std::move(frame));
}

void PDFContentTreeBuilderProcessor::splitPartialGroup()
{
    BuildNode* top = m_frames.back().get();
    if (m_frames.size() < 2 || top->data.kind != PDFContentNodeKind::Group || top->closedCount == 0 || top->closedCount >= top->data.saveDepth)
    {
        return;
    }

    // q q A Q B ...: the inner q with A and its Q is a group inside of the group of the outer q
    const int innerDepth = top->closedCount;
    const int outerDepth = top->data.saveDepth - innerDepth;

    std::unique_ptr<BuildNode> inner = std::make_unique<BuildNode>();
    inner->data.kind = PDFContentNodeKind::Group;
    inner->data.saveDepth = innerDepth;
    inner->data.prefix = PDFContentOperationSpan{ top->data.prefix.first + size_t(outerDepth), size_t(innerDepth) };
    inner->data.suffix = top->data.suffix;
    inner->data.state = top->data.state;
    inner->data.symbolicState = top->data.symbolicState;
    inner->children = std::move(top->children);

    top->children.clear();
    top->data.saveDepth = outerDepth;
    top->data.prefix.count = size_t(outerDepth);
    top->data.suffix = PDFContentOperationSpan();
    top->closedCount = 0;
    top->children.push_back(std::move(inner));
}

void PDFContentTreeBuilderProcessor::fillMarkedData(PDFContentNode::Data& data, const CurrentOperation& current)
{
    const Operands& operands = m_operations.getOperation(current.index).getOperands();
    data.tag = getName(operands, 0);

    // 14.8.2.2: an artifact is a marked-content sequence with the tag Artifact
    data.isArtifact = data.tag == "Artifact";

    if (!current.hasMarkedProperties)
    {
        return;
    }

    const PDFDocument* document = getDocument();

    if (current.markedProperties.isName())
    {
        // A name of /Properties of the resources (14.6.2)
        data.propertiesName = current.markedProperties.getString();
        if (const PDFDictionary* dictionary = getPropertiesDictionary())
        {
            data.properties = dictionary->get(data.propertiesName);
        }
    }
    else
    {
        data.properties = current.markedProperties;
    }

    // 8.11.3.2: optional content in a content stream is /OC with the name of a group or of
    // a membership dictionary
    if (data.tag == "OC" && data.properties.isReference())
    {
        data.optionalContent = data.properties.getReference();
    }

    const PDFDictionary* dictionary = document->getDictionaryFromObject(data.properties);
    if (!dictionary)
    {
        return;
    }

    // 14.7.5.2: the marked-content identifier /MCID
    const PDFObject& identifier = document->getObject(dictionary->get("MCID"));
    if (identifier.isInt())
    {
        const PDFInteger value = identifier.getInteger();
        if (value < 0 || value > PDFContentContainer::MAXIMUM_MARKED_CONTENT_ID)
        {
            addIssue(PDFContentIssue::Kind::InvalidMarkedContentId, PDFTranslationContext::tr("The marked-content identifier %1 is out of range.").arg(value), true, current.index);
        }
        else
        {
            data.mcid = value;
            if (!m_markedContentIds.insert(value).second)
            {
                addIssue(PDFContentIssue::Kind::InvalidMarkedContentId, PDFTranslationContext::tr("The marked-content identifier %1 is used twice.").arg(value), true, current.index);
            }
        }
    }
    else if (!identifier.isNull())
    {
        addIssue(PDFContentIssue::Kind::InvalidMarkedContentId, PDFTranslationContext::tr("The marked-content identifier is not an integer."), true, current.index);
    }

    if (dictionary->hasKey("ActualText"))
    {
        PDFDocumentDataLoaderDecorator loader(document);
        data.actualText = loader.readTextStringFromDictionary(dictionary, "ActualText", QString());
    }
}

void PDFContentTreeBuilderProcessor::updateTextPosition(const CurrentOperation& current)
{
    if (current.isFailed || !isInsideText() || !m_textLayout || m_capture.isActive)
    {
        return;
    }

    const Operands& operands = m_operations.getOperation(current.index).getOperands();
    PDFReal numbers[6] = { };

    // The text matrices by 9.4.2, Table 106
    try
    {
        switch (current.contentOperator)
        {
            case Operator::TextMoveByOffset:
            case Operator::TextSetLeadingAndMoveByOffset:
            {
                if (readNumbers(operands, 2, numbers))
                {
                    moveTextLine(numbers[0], numbers[1]);
                }
                break;
            }

            case Operator::TextSetMatrix:
            {
                if (readNumbers(operands, 6, numbers))
                {
                    setTextMatrix(QTransform(numbers[0], numbers[1], numbers[2], numbers[3], numbers[4], numbers[5]));
                }
                break;
            }

            case Operator::TextMoveByLeading:
                // T* is 0 -Tl Td
                moveTextLine(0.0, -getGraphicState()->getTextLeading());
                break;

            default:
                break;
        }
    }
    catch (const PDFException&)
    {
        // The layout of the text object goes on from where it was
    }
}

void PDFContentTreeBuilderProcessor::moveTextLine(PDFReal tx, PDFReal ty)
{
    // The layout makes the same move (PDFContentTextLayout::moveByOffset), unless it is not finite
    const bool isFinite = isFiniteMatrix(QTransform::fromTranslate(tx, ty) * m_textLayout->getTextLineMatrix());
    m_textLayout->moveByOffset(tx, ty);

    if (!isFinite)
    {
        m_isTextLineMatrixUnknown = true;
    }
    else if (!m_isTextLineMatrixUnknown)
    {
        restartTextLayoutAtLine();
    }
}

void PDFContentTreeBuilderProcessor::setTextMatrix(const QTransform& matrix)
{
    m_textLayout->setMatrix(matrix);

    if (!isFiniteMatrix(matrix))
    {
        m_isTextLineMatrixUnknown = true;
        return;
    }

    // The matrix replaces Tm and Tlm, whatever they were
    m_isTextLineMatrixUnknown = false;
    restartTextLayoutAtLine();
}

void PDFContentTreeBuilderProcessor::restartTextLayoutAtLine()
{
    // Tm = Tlm: the state of the layout after the operator is the state of a text object
    // beginning with "Tlm Tm" - the matrices are those, the completeness of the glyphs shown
    // before does not reach behind it
    const QTransform lineMatrix = m_textLayout->getTextLineMatrix();
    m_textLayout->beginText();
    m_textLayout->setMatrix(lineMatrix);
    m_isTextPositionUnknown = false;
}

bool PDFContentTreeBuilderProcessor::isFiniteMatrix(const QTransform& matrix)
{
    return std::isfinite(matrix.m11()) && std::isfinite(matrix.m12()) && std::isfinite(matrix.m13()) &&
           std::isfinite(matrix.m21()) && std::isfinite(matrix.m22()) && std::isfinite(matrix.m23()) &&
           std::isfinite(matrix.m31()) && std::isfinite(matrix.m32()) && std::isfinite(matrix.m33());
}

void PDFContentTreeBuilderProcessor::fillTextShow(BuildNode& node, const CurrentOperation& current)
{
    PDFContentNode::Data& data = node.data;
    const PDFPageContentProcessorState& state = *getGraphicState();

    // The text state of the operator: the state after it (the operator " sets the spacings
    // before it shows its string)
    PDFContentTextState& textState = data.textState;
    textState.font = state.getTextFont();
    textState.fontSize = state.getTextFontSize();
    textState.characterSpacing = state.getTextCharacterSpacing();
    textState.wordSpacing = state.getTextWordSpacing();
    textState.horizontalScaling = state.getTextHorizontalScaling();
    textState.leading = state.getTextLeading();
    textState.rise = state.getTextRise();
    textState.renderingMode = state.getTextRenderingMode();

    // The rendering mode does not apply to a Type 3 font (9.3.6): its text never clips
    const bool isType3 = textState.font && textState.font->getFontType() == FontType::Type3;
    data.hasClipEffect = isTextRenderingModeClipped(textState.renderingMode) && !isType3;
    if (data.hasClipEffect)
    {
        m_isTextClipping = true;
    }

    data.isTextComplete = false;
    if (!m_textLayout)
    {
        // The caller did not ask for the glyphs (PDFContentBuildOptions::layoutText)
        return;
    }

    const Operands& operands = m_operations.getOperation(current.index).getOperands();
    const bool canShow = !current.isFailed && textState.font;

    // The show begins at the position of the standard only, when every show before it in the
    // text object was laid out completely (A2L2-4: a glyph without a width, a show, which is no
    // TextShow node, a vertical font before it - the layout of the text object since its last
    // Tm, or since a relative move of Tlm, while Tlm was known: moveTextLine). ' and " move Tlm
    // first and ask again.
    bool isStartKnown = m_textLayout->isComplete() && !m_isTextPositionUnknown;

    if (textState.font)
    {
        // The outlines of the glyphs; a font, which cannot be realized, has none
        try
        {
            textState.realizedFont = getFontCache()->getRealizedFont(textState.font, textState.fontSize, this);
        }
        catch (const PDFException&)
        {
            textState.realizedFont.reset();
        }
        catch (const PDFRendererException&)
        {
            textState.realizedFont.reset();
        }
    }

    try
    {
        bool isShown = false;
        PDFReal numbers[2] = { };

        switch (current.contentOperator)
        {
            case Operator::TextShowTextString:
            {
                if (canShow && isString(operands, 0))
                {
                    data.textItems = m_textLayout->showString(operands[0].data.toByteArray(), textState);
                    isShown = true;
                }
                break;
            }

            case Operator::TextNextLineShowText:
            {
                // string ' is T* string Tj: the line moves, whenever the operand can be read
                if (isString(operands, 0))
                {
                    moveTextLine(0.0, -textState.leading);
                    isStartKnown = m_textLayout->isComplete() && !m_isTextPositionUnknown;
                    if (canShow)
                    {
                        data.textItems = m_textLayout->showString(operands[0].data.toByteArray(), textState);
                        isShown = true;
                    }
                }
                break;
            }

            case Operator::TextSetSpacingAndShowText:
            {
                // aw ac string " is aw Tw ac Tc string '
                if (readNumbers(operands, 2, numbers) && isString(operands, 2))
                {
                    moveTextLine(0.0, -textState.leading);
                    isStartKnown = m_textLayout->isComplete() && !m_isTextPositionUnknown;
                    if (canShow)
                    {
                        data.textItems = m_textLayout->showString(operands[2].data.toByteArray(), textState);
                        isShown = true;
                    }
                }
                break;
            }

            case Operator::TextShowTextIndividualSpacing:
            {
                if (canShow && operands.size() >= 2 && operands.front().type == TokenType::ArrayStart && operands.back().type == TokenType::ArrayEnd)
                {
                    for (size_t i = 1; i + 1 < operands.size(); ++i)
                    {
                        const PDFLexicalAnalyzer::Token& token = operands[i];
                        if (token.type == TokenType::String)
                        {
                            std::vector<PDFContentTextItem> items = m_textLayout->showString(token.data.toByteArray(), textState);
                            data.textItems.insert(data.textItems.end(), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
                        }
                        else if (token.type == TokenType::Integer || token.type == TokenType::Real)
                        {
                            data.textItems.push_back(m_textLayout->applyAdjustment(token.data.value<PDFReal>(), textState));
                        }
                    }
                    isShown = true;
                }
                break;
            }

            default:
                break;
        }

        if (isShown)
        {
            // The positions are the ones of the standard only with a width of every glyph in
            // the font dictionary and a font, whose text runs along the x axis
            bool isComplete = PDFContentTextLayout::isEditable(textState.font.data());
            for (const PDFContentTextItem& item : data.textItems)
            {
                if (item.kind == PDFContentTextItem::Kind::Glyph && !item.width.has_value())
                {
                    isComplete = false;
                    break;
                }
            }
            data.isTextComplete = isComplete && isStartKnown;
        }
    }
    catch (const PDFException&)
    {
        data.textItems.clear();
        data.isTextComplete = false;
    }
    catch (const PDFRendererException&)
    {
        data.textItems.clear();
        data.isTextComplete = false;
    }

    if (data.state)
    {
        data.boundingBox = getTextBoundingBox(data, data.state->getCurrentTransformationMatrix());
    }
}

QRectF PDFContentTreeBuilderProcessor::getTextBoundingBox(const PDFContentNode::Data& data, const QTransform& currentTransformationMatrix) const
{
    const PDFContentTextState& textState = data.textState;
    const PDFReal horizontalScaling = textState.horizontalScaling * 0.01;
    const PDFReal sign = textState.fontSize < 0.0 ? -1.0 : 1.0;

    // The outlines of a realized font are made for the absolute font size: the text rendering
    // matrix of 9.4.4 without the size
    const QTransform outlineMatrix(horizontalScaling * sign, 0.0, 0.0, sign, 0.0, textState.rise);

    QRectF boundingBox;
    for (const PDFContentTextItem& item : data.textItems)
    {
        if (item.kind != PDFContentTextItem::Kind::Glyph)
        {
            continue;
        }

        QRectF glyphBox;
        if (item.outline && !item.outline->isEmpty())
        {
            glyphBox = (outlineMatrix * item.textMatrix * currentTransformationMatrix).mapRect(item.outline->getBoundingRect());
        }
        else if (item.width.has_value() && *item.width != 0.0)
        {
            // No outline (a glyph of a Type 3 font, a missing glyph): the cell of the glyph by
            // its width, from a fifth of the size below the baseline to the size above it
            const PDFReal width = *item.width * textState.fontSize * horizontalScaling;
            const QRectF cell = QRectF(qMin(0.0, width), textState.rise - 0.2 * textState.fontSize, qAbs(width), 1.2 * textState.fontSize).normalized();
            glyphBox = (item.textMatrix * currentTransformationMatrix).mapRect(cell);
        }

        if (!glyphBox.isNull())
        {
            boundingBox = boundingBox.isNull() ? glyphBox : boundingBox.united(glyphBox);
        }
    }

    return boundingBox;
}

void PDFContentTreeBuilderProcessor::addPendingBrackets(Capture& capture, const BuildNode& frame) const
{
    switch (frame.data.kind)
    {
        case PDFContentNodeKind::Group:
            capture.pendingSave += size_t(frame.data.saveDepth - frame.closedCount);
            break;

        case PDFContentNodeKind::MarkedContent:
            ++capture.pendingMarked;
            break;

        case PDFContentNodeKind::TextObject:
            ++capture.pendingText;
            break;

        default:
            break;
    }
}

void PDFContentTreeBuilderProcessor::startCaptureAtOperation(const QString& reason)
{
    closeOpenLeaf();

    m_capture = Capture();
    m_capture.isActive = true;
    m_capture.node = createNode(PDFContentNodeKind::Opaque, m_current.index, 0);
    m_capture.node->data.opaqueReason = reason;
}

void PDFContentTreeBuilderProcessor::startCaptureFromFrame(size_t frameIndex, const QString& reason)
{
    Q_ASSERT(frameIndex >= 1 && frameIndex < m_frames.size());

    Capture capture;
    capture.isActive = true;
    capture.isPathPending = m_leafMode == LeafMode::Path;

    // The open leaf is a part of the span
    m_openLeaf.reset();
    m_leafMode = LeafMode::None;

    for (size_t i = frameIndex; i < m_frames.size(); ++i)
    {
        addPendingBrackets(capture, *m_frames[i]);
    }

    const BuildNode& first = *m_frames[frameIndex];
    capture.node = std::make_unique<BuildNode>();
    capture.node->data.kind = PDFContentNodeKind::Opaque;
    capture.node->data.prefix = PDFContentOperationSpan{ first.data.prefix.first, m_current.index - first.data.prefix.first };
    capture.node->data.state = first.data.state;
    capture.node->data.symbolicState = first.data.symbolicState;
    capture.node->data.opaqueReason = reason;

    dropFrames(frameIndex);
    m_capture = std::move(capture);
}

void PDFContentTreeBuilderProcessor::startCaptureFromPath(const QString& reason)
{
    Q_ASSERT(m_leafMode == LeafMode::Path);

    m_capture = Capture();
    m_capture.isActive = true;
    m_capture.isPathPending = true;
    m_capture.node = std::move(m_openLeaf);
    m_capture.node->data.kind = PDFContentNodeKind::Opaque;
    m_capture.node->data.opaqueReason = reason;
    m_capture.node->data.path = PDFContentPath();
    m_capture.node->data.paint = PDFContentPathPaint();

    m_leafMode = LeafMode::None;
    m_isPathInvalid = false;
    m_hasCurrentPoint = false;
}

void PDFContentTreeBuilderProcessor::dropFrames(size_t frameIndex)
{
    m_frames.resize(frameIndex);

    if (m_textFrameIndex >= int(frameIndex))
    {
        m_textFrameIndex = -1;
        m_isTextClipping = false;
    }
}

void PDFContentTreeBuilderProcessor::captureOperation()
{
    PDFContentNode::Data& data = m_capture.node->data;
    data.prefix.count = m_current.index - data.prefix.first + 1;
    m_current.afterAction = AfterAction::CheckCapture;

    if (m_capture.isUntilEnd)
    {
        // The bytes after an inline image without an end are image data, whatever operators
        // the processor has read in them: they open and close nothing of the tree
        return;
    }

    switch (m_current.category)
    {
        case Category::Save:
            ++m_capture.pendingSave;
            break;

        case Category::Restore:
        {
            if (m_capture.pendingSave > 0)
            {
                --m_capture.pendingSave;
            }
            else
            {
                crossOutward(PDFContentNodeKind::Group);
            }
            break;
        }

        case Category::MarkedBegin:
            ++m_capture.pendingMarked;
            break;

        case Category::MarkedEnd:
        {
            if (m_capture.pendingMarked > 0)
            {
                --m_capture.pendingMarked;
            }
            else
            {
                crossOutward(PDFContentNodeKind::MarkedContent);
            }
            break;
        }

        case Category::TextBegin:
            ++m_capture.pendingText;
            break;

        case Category::TextEnd:
        {
            if (m_capture.pendingText > 0)
            {
                --m_capture.pendingText;
            }
            else
            {
                crossOutward(PDFContentNodeKind::TextObject);
            }
            break;
        }

        case Category::PathConstruct:
        case Category::PathClip:
            m_capture.isPathPending = true;
            break;

        case Category::PathPaint:
            m_capture.isPathPending = false;
            break;

        case Category::InlineImage:
        {
            if (!getInlineImageRange().isValid())
            {
                m_capture.isUntilEnd = true;
            }
            break;
        }

        default:
            break;
    }
}

void PDFContentTreeBuilderProcessor::crossOutward(PDFContentNodeKind kind)
{
    // The operator closes a bracket, which was opened before the opaque span: the span grows
    // to the beginning of that node and takes everything opened since then
    for (size_t i = m_frames.size() - 1; i >= 1; --i)
    {
        if (m_frames[i]->data.kind != kind)
        {
            continue;
        }

        addIssue(PDFContentIssue::Kind::CrossedBrackets, PDFTranslationContext::tr("The brackets of the content cross each other."), true, m_current.index);

        for (size_t j = i + 1; j < m_frames.size(); ++j)
        {
            addPendingBrackets(m_capture, *m_frames[j]);
        }

        const BuildNode& first = *m_frames[i];
        if (kind == PDFContentNodeKind::Group)
        {
            // One of its q is closed by this operator
            m_capture.pendingSave += size_t(first.data.saveDepth - first.closedCount - 1);
        }

        PDFContentNode::Data& data = m_capture.node->data;
        data.prefix = PDFContentOperationSpan{ first.data.prefix.first, m_current.index - first.data.prefix.first + 1 };
        data.state = first.data.state;
        data.symbolicState = first.data.symbolicState;

        dropFrames(i);
        return;
    }

    addIssue(PDFContentIssue::Kind::UnbalancedEnd, PDFTranslationContext::tr("An end of a part of the content has no beginning."), true, m_current.index);
}

void PDFContentTreeBuilderProcessor::finishCapture()
{
    std::unique_ptr<BuildNode> node = std::move(m_capture.node);
    m_capture = Capture();

    // Nothing is known about what an opaque node does; the state after it is the one of the
    // processor (a span ends within the content only, when the state is within the limit; at
    // the end of the content it can be over the limit - takeEndState)
    node->data.leak.isUnknown = true;
    node->data.leak.endState = m_endStateOverLimit ? m_endStateOverLimit : m_resolved;
    node->data.leak.endSymbolicState = getSymbolicState();
    appendChild(std::move(node));
}

void PDFContentTreeBuilderProcessor::finishContent()
{
    // The content has ended: what is open, ends without its closing operators. The states are
    // the ones after the last operation (the processor has restored its stack since then).
    // Without such a restore the state of the processor is still the one of the end.
    takeEndState();
    if (m_capture.isActive)
    {
        // A span, in which something is still open (a bracket, a path, the data of an inline
        // image without an end): content written behind it would be a part of it
        const Capture& capture = m_capture;
        m_capture.node->data.isOpenEnd = capture.pendingSave > 0 || capture.pendingMarked > 0 || capture.pendingText > 0 ||
                                         capture.isPathPending || capture.isUntilEnd;
        finishCapture();
    }

    if (m_leafMode == LeafMode::Path && m_openLeaf)
    {
        // A path without a painting operator: the painting operator of content written behind
        // it would paint it too (closeOpenLeaf makes it an opaque node)
        m_openLeaf->data.isOpenEnd = true;
    }
    closeOpenLeaf();

    while (m_frames.size() > 1)
    {
        splitPartialGroup();

        // A text object of the rendering modes 4 to 7 without its ET: the ET, which the model
        // writes in front of content behind it, makes the clip real for that content (9.3.6)
        if (m_frames.back()->data.kind == PDFContentNodeKind::TextObject && int(m_frames.size()) - 1 == m_textFrameIndex && m_isTextClipping)
        {
            m_frames.back()->data.isOpenEnd = true;
        }
        closeTopFrame(getSnapshot());
    }
}

void PDFContentTreeBuilderProcessor::createOperationLimitContent()
{
    // More operations than a container can have: the whole content is one operation of one
    // opaque node, written back as it is
    m_capture = Capture();
    m_openLeaf.reset();
    m_leafMode = LeafMode::None;
    m_frames.resize(1);
    m_frames.front()->children.clear();
    m_issues.clear();
    m_markedContentIds.clear();

    const QByteArray content = m_source->getContent();

    PDFContentByteRange range;
    range.begin = PDFContentStreamPosition{ 0, 0 };
    range.end = PDFContentStreamPosition{ 0, 0 };
    if (!m_source->streams.empty())
    {
        range.end = PDFContentStreamPosition{ PDFInteger(m_source->streams.size()) - 1, PDFInteger(m_source->streams.back().data.size()) };
    }

    m_operations = PDFContentOperationList();
    m_operations.append(PDFContentOperation::createOriginal(Operator::Invalid, content, range, Operands(), std::vector<PDFContentResourceName>(), PDFContentOperation::Unknown));
    m_operations.finishOriginal(QByteArray());

    std::unique_ptr<BuildNode> node = std::make_unique<BuildNode>();
    node->data.kind = PDFContentNodeKind::Opaque;
    node->data.prefix = PDFContentOperationSpan{ 0, 1 };
    node->data.state = m_initial.resolved;
    node->data.symbolicState = m_initial.symbolic;
    node->data.leak.isUnknown = true;
    node->data.opaqueReason = PDFTranslationContext::tr("Content with too many operations");

    addIssue(PDFContentIssue::Kind::OperationLimit, PDFTranslationContext::tr("The content has too many operations."), false, 0, node.get());
    appendChild(std::move(node));
}

void PDFContentTreeBuilderProcessor::computeBoundingBoxes(BuildNode* root) const
{
    // The box of a node with children is the box of its children: children first, with a stack
    // of its own
    struct Item
    {
        BuildNode* node = nullptr;
        size_t nextChild = 0;
    };

    std::vector<Item> stack;
    stack.push_back(Item{ root, 0 });

    while (!stack.empty())
    {
        Item& item = stack.back();
        BuildNode* node = item.node;

        if (item.nextChild < node->children.size())
        {
            BuildNode* child = node->children[item.nextChild++].get();
            if (!child->children.empty())
            {
                stack.push_back(Item{ child, 0 });  // "item" is not used after this line
            }
            continue;
        }

        if (!PDFContentNode::isLeafKind(node->data.kind))
        {
            QRectF boundingBox;
            for (const std::unique_ptr<BuildNode>& child : node->children)
            {
                const QRectF& childBox = child->data.boundingBox;
                if (!childBox.isNull())
                {
                    boundingBox = boundingBox.isNull() ? childBox : boundingBox.united(childBox);
                }
            }
            node->data.boundingBox = boundingBox;
        }

        stack.pop_back();
    }
}

PDFContentBuildResult PDFContentTreeBuilderProcessor::createResult()
{
    PDFContentBuildResult result;
    const PDFDocument* document = getDocument();

    BuildNode* tailNode = nullptr;

    if (m_isOverLimit)
    {
        createOperationLimitContent();
    }
    else
    {
        finishContent();

        // The bytes after the last operation (a content above the size limit has no bytes here:
        // its streams are not read)
        QByteArray tail;
        const PDFInteger streamCount = m_source->isOverSize ? 0 : PDFInteger(m_source->streams.size());
        for (PDFInteger stream = qMax<PDFInteger>(m_lastEnd.stream, 0); stream < streamCount; ++stream)
        {
            const QByteArray data = getStreamData(stream);
            if (stream == m_lastEnd.stream)
            {
                tail.append(data.mid(m_lastEnd.offset));
            }
            else
            {
                tail.append('\n');
                tail.append(data);
            }
        }

        if (!isTailReadable(tail))
        {
            // The end of the content, which is not an operation (an unfinished string, operands
            // without an operator): one opaque node at the end, new content goes before it
            std::unique_ptr<BuildNode> node = createNode(PDFContentNodeKind::Opaque, m_operations.getCount(), 0);
            node->data.leak.isUnknown = true;
            node->data.opaqueReason = PDFTranslationContext::tr("End of the content, which cannot be read");
            tailNode = node.get();
            addIssue(PDFContentIssue::Kind::UnreadableTail, PDFTranslationContext::tr("The end of the content cannot be read."), false, 0, tailNode);
            appendChild(std::move(node));
        }

        m_operations.finishOriginal(std::move(tail));
    }

    if (m_source->isOverSize)
    {
        addIssue(PDFContentIssue::Kind::ContentSizeLimit, PDFTranslationContext::tr("The content is too large, it cannot be changed."), false, 0);
    }
    else if (m_source->isReadOnly)
    {
        addIssue(PDFContentIssue::Kind::UndecodableStream, PDFTranslationContext::tr("A stream of the content cannot be decoded, the content cannot be changed."), false, 0);
    }

    BuildNode* root = m_frames.front().get();
    root->data.state = m_initial.resolved;
    root->data.symbolicState = m_initial.symbolic;
    computeBoundingBoxes(root);

    // The identifiers: a depth first walk from 1 (the root), so the same content gives the
    // same numbers. The nodes go to the editor in the same order.
    PDFContentContainerEditor editor(m_source->key);
    editor.getOperations() = std::move(m_operations);

    std::vector<SpanOwner> spanOwners;
    const bool hasIssues = !m_issues.empty();

    auto addSpanOwner = [&spanOwners, hasIssues](const PDFContentOperationSpan& span, quint64 id)
    {
        if (hasIssues && span.count > 0)
        {
            spanOwners.push_back(SpanOwner{ span.first, span.count, id });
        }
    };

    struct Item
    {
        BuildNode* node = nullptr;
        size_t nextChild = 0;
    };

    quint64 nextId = 1;
    root->id = nextId++;
    addSpanOwner(root->data.prefix, root->id);
    addSpanOwner(root->data.suffix, root->id);
    editor.setNodeData(PDFContentNodeId{ root->id }, std::move(root->data));

    std::vector<Item> stack;
    stack.push_back(Item{ root, 0 });

    while (!stack.empty())
    {
        Item& item = stack.back();
        BuildNode* node = item.node;

        if (item.nextChild >= node->children.size())
        {
            stack.pop_back();
            continue;
        }

        BuildNode* child = node->children[item.nextChild++].get();
        child->id = nextId++;
        child->data.id = PDFContentNodeId{ child->id };
        addSpanOwner(child->data.prefix, child->id);
        addSpanOwner(child->data.suffix, child->id);
        const bool isInserted = editor.insertNode(std::move(child->data), PDFContentNodeId{ node->id }, -1);
        Q_ASSERT(isInserted);   // The limits of the builder are the limits of the editor
        Q_UNUSED(isInserted);

        if (!child->children.empty())
        {
            stack.push_back(Item{ child, 0 });  // "item" is not used after this line
        }
    }

    // The node of each issue: the node, whose operations contain the operation of the issue
    std::sort(spanOwners.begin(), spanOwners.end(), [](const SpanOwner& left, const SpanOwner& right) { return left.first < right.first; });

    std::vector<PDFContentIssue> issues;
    issues.reserve(m_issues.size());
    for (PendingIssue& pendingIssue : m_issues)
    {
        PDFContentIssue issue = std::move(pendingIssue.issue);
        if (pendingIssue.node)
        {
            issue.nodeId = PDFContentNodeId{ pendingIssue.node->id };
        }
        else if (pendingIssue.hasOperation)
        {
            auto it = std::upper_bound(spanOwners.cbegin(), spanOwners.cend(), issue.operationIndex, [](size_t index, const SpanOwner& owner) { return index < owner.first; });
            if (it != spanOwners.cbegin())
            {
                --it;
                if (issue.operationIndex < it->first + it->count)
                {
                    issue.nodeId = PDFContentNodeId{ it->id };
                }
            }
        }
        issues.push_back(std::move(issue));
    }

    PDFContentContainer::Data& containerData = editor.getContainerData();
    containerData.key = m_source->key;
    containerData.initialState = m_initial.resolved;
    containerData.initialSymbolicState = m_initial.symbolic;
    containerData.boundingBox = m_source->boundingBox;
    containerData.matrix = m_source->matrix;
    containerData.structuralParentKey = m_source->structuralParentKey;
    containerData.fingerprint = m_source->computeFingerprint(document);
    containerData.issues = issues;
    containerData.isReadOnly = m_source->isReadOnly;
    containerData.isColorIgnored = m_source->isUncoloredPatternCell;
    containerData.inheritedResourcesPageIndex = m_inheritedResourcesPageIndex;
    containerData.inheritedResources = m_inheritedResources;

    editor.getResources() = PDFContentResources::read(document, m_source->resources, editor.getOperations());

    // The names, which content without resources drawn by this one uses: a name added to these
    // resources must not become one of them (A1L2-4). The resources the content was interpreted
    // with: its own, the merged ones of the caller, or those of the page. Such content looks a
    // name up in the content drawing it first, a name that content lacks in the page (NOTES.txt
    // 7.8.3): only for a page is a name the page defines no risk (the page has it already, the
    // allocator never gives it). For a form or a pattern cell every name counts: a name added to
    // it would hide the name of the page from such content. A content without resources of its
    // own also reserves every name it was interpreted with: the document writer gives it a copy
    // of them with the added names, so an added name never takes the place of one of them.
    const bool isPage = m_source->key.kind == PDFContentContainerKey::Kind::Page;
    const PDFObject& interpretedResources = !m_source->resources.isNull() ? m_source->resources : (!m_inheritedResources.isNull() ? m_inheritedResources : m_pageResources);
    if (!m_isOverLimit && !m_source->isOverSize)
    {
        std::set<QByteArray> names = PDFContentTreeBuilderReservedNames::collect(document, m_pageResources, interpretedResources, isPage, editor.getOperations());
        const PDFDictionary* pageDictionary = isPage ? document->getDictionaryFromObject(m_pageResources) : nullptr;
        const PDFDictionary* inheritedDictionary = (!isPage && m_source->resources.isNull()) ? document->getDictionaryFromObject(interpretedResources) : nullptr;
        for (const PDFContentResourceCategory category : PDFContentResources::getCategories())
        {
            // The keys of the category are read once (review R2-5), not once per name
            const QByteArray categoryKey = PDFContentResources::getCategoryKey(category);
            const PDFDictionary* pageCategory = pageDictionary ? document->getDictionaryFromObject(pageDictionary->get(categoryKey)) : nullptr;
            std::set<QByteArray> categoryNames = pageDictionary ? PDFContentTreeBuilderReservedNames::getNamesNotIn(names, pageCategory) : names;
            if (const PDFDictionary* inheritedCategory = inheritedDictionary ? document->getDictionaryFromObject(inheritedDictionary->get(categoryKey)) : nullptr)
            {
                for (size_t i = 0; i < inheritedCategory->getCount() && categoryNames.size() < PDFContentTreeBuilderReservedNames::MAXIMUM_NAMES; ++i)
                {
                    categoryNames.insert(inheritedCategory->getKey(i).getString());
                }
            }
            editor.getResources().setReservedNames(category, std::move(categoryNames));
        }
    }

    result.container = editor.build();
    result.issues = std::move(issues);
    result.usedForms = std::move(m_usedForms);
    result.usedPatternCells = std::move(m_usedPatternCells);

    // The caller of what this content draws: the content itself with its own resources, else
    // its own caller (the processor merges the dictionaries of the page and of that caller
    // again for the nested content: the result is the same); the page for a page
    if (m_source->key.kind != PDFContentContainerKey::Kind::Page)
    {
        result.nestedCallerResources = m_source->resources.isNull() ? m_callerResources : m_source->resources;
    }
    return result;
}

PDFContentBuildResult PDFContentTreeBuilder::build(const PDFDocument* document,
                                                   const PDFContentContainerKey& key,
                                                   const PDFContentResolvedStatePointer& initialState,
                                                   const PDFContentBuildContext& context,
                                                   const PDFContentBuildOptions& options,
                                                   const PDFOperationControl* control,
                                                   PDFProgressSink* progress)
{
    PDFContentBuildResult result;

    auto fail = [&result](const QString& text)
    {
        PDFContentIssue issue;
        issue.kind = PDFContentIssue::Kind::InvalidContainer;
        issue.text = text;
        result.issues.push_back(std::move(issue));
        return result;
    };

    const PDFContentTreeBuilderSource source = PDFContentTreeBuilderSource::read(document, key);
    if (!source.isValid)
    {
        return fail(source.error);
    }

    if (!context.fontCache || !context.cms)
    {
        return fail(PDFTranslationContext::tr("The content cannot be read without a font cache and a color management system."));
    }

    // The processor needs a page: the page of the key, or the page, whose content uses the
    // form or the pattern first
    const PDFPage* page = source.page;
    if (!page)
    {
        const PDFCatalog* catalog = document->getCatalog();
        if (context.pageIndex < 0 || size_t(context.pageIndex) >= catalog->getPageCount())
        {
            return fail(PDFTranslationContext::tr("The content of a form or of a pattern cannot be read without its page."));
        }
        page = catalog->getPage(size_t(context.pageIndex));
    }

    if (PDFOperationControl::isOperationCanceled(control))
    {
        result.isCanceled = true;
        return result;
    }

    if (progress)
    {
        progress->start(1, ProgressStartupInfo());
    }

    PDFContentTreeBuilderProcessor processor(&source, page, document, context, options, control);
    processor.run(initialState);

    if (processor.isCanceledByCaller())
    {
        result.isCanceled = true;
    }
    else
    {
        result = processor.createResult();
    }

    if (progress)
    {
        progress->step();
        progress->finish();
    }

    return result;
}

QByteArray PDFContentTreeBuilder::computeFingerprint(const PDFDocument* document, const PDFContentContainerKey& key)
{
    const PDFContentTreeBuilderSource source = PDFContentTreeBuilderSource::read(document, key);
    if (!source.isValid)
    {
        return QByteArray();
    }

    return source.computeFingerprint(document);
}

}   // namespace pdf
