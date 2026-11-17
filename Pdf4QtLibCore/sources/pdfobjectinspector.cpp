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

#include "pdfobjectinspector.h"
#include "pdfcatalog.h"
#include "pdfcolorspaces.h"
#include "pdfdocumenttooltablewriter.h"
#include "pdfdocumentwriter.h"
#include "pdfencoding.h"
#include "pdfexception.h"
#include "pdfimage.h"
#include "pdfoperationcontrol.h"
#include "pdfparser.h"

#include <QStringDecoder>

#include <algorithm>
#include <limits>

namespace pdf
{

/// The PDF syntax of an object for the inspector (getSyntax): a dictionary with one entry on a line, the
/// other values inline; the text stops at its limit
class PDFObjectInspectorSyntaxWriter
{
public:
    explicit PDFObjectInspectorSyntaxWriter(qint64 limit) :
        m_limit(limit)
    {

    }

    void write(const PDFObject& object, int indent)
    {
        if (isFull())
        {
            return;
        }

        switch (object.getType())
        {
            case PDFObject::Type::Null:
                append(QStringLiteral("null"));
                break;

            case PDFObject::Type::Bool:
                append(object.getBool() ? QStringLiteral("true") : QStringLiteral("false"));
                break;

            case PDFObject::Type::Int:
                append(QString::number(object.getInteger()));
                break;

            case PDFObject::Type::Real:
                append(QString::fromLatin1(PDFDocumentWriter::getRealNumberText(object.getReal())));
                break;

            case PDFObject::Type::String:
                append(getStringText(object.getString()));
                break;

            case PDFObject::Type::Name:
                append(getNameText(object.getString()));
                break;

            case PDFObject::Type::Reference:
                append(PDFObjectInspector::getReferenceText(object.getReference()));
                break;

            case PDFObject::Type::Array:
            {
                const PDFArray* array = object.getArray();
                append(QStringLiteral("["));
                for (size_t i = 0; i < array->getCount() && !isFull(); ++i)
                {
                    append(QStringLiteral(" "));
                    write(array->getItem(i), indent);
                }
                append(QStringLiteral(" ]"));
                break;
            }

            case PDFObject::Type::Dictionary:
            case PDFObject::Type::Stream:
            {
                const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : object.getDictionary();
                if (indent >= MAXIMUM_INDENT)
                {
                    // Deeper direct dictionaries are written on one line
                    append(QStringLiteral("<< ... >>"));
                    break;
                }

                const QString padding(indent * 2 + 2, QLatin1Char(' '));
                append(QStringLiteral("<<"));
                for (size_t i = 0; i < dictionary->getCount() && !isFull(); ++i)
                {
                    append(QLatin1Char('\n') + padding + getNameText(dictionary->getKey(i).getString()) + QLatin1Char(' '));
                    write(dictionary->getValue(i), indent + 1);
                }
                append(QLatin1Char('\n') + QString(indent * 2, QLatin1Char(' ')) + QStringLiteral(">>"));
                break;
            }

            default:
                Q_ASSERT(false);
                break;
        }
    }

    const QString& getText() const { return m_text; }
    bool isTruncated() const { return m_isTruncated; }

    /// A name by ISO 32000-2, 7.3.5: a byte, which is no regular character, and the number sign as #xx
    static QString getNameText(const QByteArray& name)
    {
        QString text = QStringLiteral("/");
        for (const char character : name)
        {
            if (PDFLexicalAnalyzer::isRegular(character) && character != '#' && static_cast<unsigned char>(character) < 0x7F)
            {
                text += QLatin1Char(character);
            }
            else
            {
                text += QLatin1Char('#') + QString::fromLatin1(QByteArray(1, character).toHex().toUpper());
            }
        }
        return text;
    }

    /// A string by 7.3.4: a literal string, when every byte is printable (a parenthesis and a backslash
    /// escaped), else a hexadecimal string
    static QString getStringText(const QByteArray& string)
    {
        const bool isPrintable = std::all_of(string.cbegin(), string.cend(), [](char character) { return character >= 0x20 && character < 0x7F; });
        if (!isPrintable)
        {
            return QLatin1Char('<') + QString::fromLatin1(string.toHex().toUpper()) + QLatin1Char('>');
        }

        QString text = QStringLiteral("(");
        for (const char character : string)
        {
            if (character == '(' || character == ')' || character == '\\')
            {
                text += QLatin1Char('\\');
            }
            text += QLatin1Char(character);
        }
        return text + QLatin1Char(')');
    }

private:
    /// Deepest indentation of the nested dictionaries (the parser limits the nesting of a file, a document
    /// made by a program may be deeper)
    static constexpr int MAXIMUM_INDENT = 32;

    bool isFull() const { return m_isTruncated; }

    void append(const QString& text)
    {
        if (m_isTruncated)
        {
            return;
        }

        if (qint64(m_text.size()) + qint64(text.size()) > m_limit)
        {
            m_text += text.left(qsizetype(m_limit - qint64(m_text.size())));
            m_isTruncated = true;
            return;
        }
        m_text += text;
    }

    qint64 m_limit;
    QString m_text;
    bool m_isTruncated = false;
};

PDFObjectInspector::PDFObjectInspector(PDFDocumentPointer document) :
    m_document(std::move(document)),
    m_cache(std::make_shared<Cache>())
{

}

bool PDFObjectInspector::prepare(const PDFOperationControl* control) const
{
    std::lock_guard<std::mutex> lock(m_cache->mutex);
    if (m_cache->isPrepared)
    {
        return true;
    }

    const PDFDocument* document = m_document.data();
    if (!m_cache->classifier.classify(document, control))
    {
        return false;
    }

    m_cache->objectNumbers.clear();
    m_cache->pages.clear();
    if (document)
    {
        const PDFObjectStorage::PDFObjects& objects = document->getStorage().getObjects();
        for (size_t i = 0; i < objects.size(); ++i)
        {
            if (!objects[i].object.isNull())
            {
                m_cache->objectNumbers.push_back(PDFInteger(i));
            }
        }

        // A page object listed twice in a damaged page tree is one root
        std::set<PDFObjectReference> pages;
        const PDFCatalog* catalog = document->getCatalog();
        for (size_t i = 0; i < catalog->getPageCount(); ++i)
        {
            const PDFPage* page = catalog->getPage(i);
            if (page && page->getPageReference().isValid() && pages.insert(page->getPageReference()).second)
            {
                m_cache->pages.push_back(page->getPageReference());
            }
        }
    }

    m_cache->isPrepared = true;
    return true;
}

const PDFObjectInspector::Cache* PDFObjectInspector::getCache() const
{
    prepare(nullptr);
    return m_cache.get();
}

PDFObjectClassifier::Type PDFObjectInspector::getClassOfView(PDFObjectInspectorView view)
{
    switch (view)
    {
        case PDFObjectInspectorView::ContentStreams:
            return PDFObjectClassifier::ContentStream;
        case PDFObjectInspectorView::GraphicStates:
            return PDFObjectClassifier::GraphicState;
        case PDFObjectInspectorView::ColorSpaces:
            return PDFObjectClassifier::ColorSpace;
        case PDFObjectInspectorView::Patterns:
            return PDFObjectClassifier::Pattern;
        case PDFObjectInspectorView::Shadings:
            return PDFObjectClassifier::Shading;
        case PDFObjectInspectorView::Images:
            return PDFObjectClassifier::Image;
        case PDFObjectInspectorView::Forms:
            return PDFObjectClassifier::Form;
        case PDFObjectInspectorView::Fonts:
            return PDFObjectClassifier::Font;
        case PDFObjectInspectorView::Actions:
            return PDFObjectClassifier::Action;
        case PDFObjectInspectorView::Annotations:
            return PDFObjectClassifier::Annotation;

        case PDFObjectInspectorView::Document:
        case PDFObjectInspectorView::Pages:
        case PDFObjectInspectorView::Objects:
            break;
    }

    return PDFObjectClassifier::None;
}

std::vector<PDFObjectReference> PDFObjectInspector::getRootReferences(PDFObjectInspectorView view) const
{
    switch (view)
    {
        case PDFObjectInspectorView::Document:
            return { };

        case PDFObjectInspectorView::Pages:
            return getCache()->pages;

        case PDFObjectInspectorView::Objects:
        {
            const PDFObjectStorage::PDFObjects& objects = m_document->getStorage().getObjects();
            std::vector<PDFObjectReference> references;
            for (const PDFInteger number : getCache()->objectNumbers)
            {
                references.emplace_back(number, objects[size_t(number)].generation);
            }
            return references;
        }

        default:
            return getCache()->classifier.getObjectsByType(getClassOfView(view));
    }
}

std::vector<PDFObjectInspectorNode> PDFObjectInspector::getRoots(PDFObjectInspectorView view, OpenedReferences* opened) const
{
    return getRoots(view, opened, 0, (std::numeric_limits<qint64>::max)());
}

std::vector<PDFObjectInspectorNode> PDFObjectInspector::getRoots(PDFObjectInspectorView view, OpenedReferences* opened, qint64 offset, qint64 count) const
{
    std::vector<PDFObjectInspectorNode> roots;
    if (!m_document || offset < 0 || count <= 0)
    {
        return roots;
    }

    const bool isObjectList = view == PDFObjectInspectorView::Objects;
    qint64 index = 0;
    auto isInWindow = [&index, offset, count]() { return index >= offset && index - offset < count; };

    // The trailer: the view Document is the trailer, the Object List starts by it (482:16200)
    if (view == PDFObjectInspectorView::Document || isObjectList)
    {
        if (isInWindow())
        {
            roots.push_back(createTrailerNode(!isObjectList));
        }
        ++index;
    }

    if (view == PDFObjectInspectorView::Document)
    {
        return roots;
    }

    const PDFObjectStorage& storage = m_document->getStorage();
    if (isObjectList)
    {
        // The window directly by the list of the numbers (a large document is fetched in parts)
        const std::vector<PDFInteger>& numbers = getCache()->objectNumbers;
        const qint64 first = qMax(qint64(0), offset - index);
        for (qint64 i = first; i < qint64(numbers.size()) && qint64(roots.size()) < count; ++i)
        {
            const PDFObjectReference reference(numbers[size_t(i)], storage.getObjects()[size_t(numbers[size_t(i)])].generation);
            roots.push_back(createNode(reference, storage.getObject(reference), reference, 0, false, opened));
        }
        return roots;
    }

    const std::vector<PDFObjectReference> references = getRootReferences(view);
    for (const PDFObjectReference reference : references)
    {
        if (isInWindow())
        {
            roots.push_back(createNode(reference, storage.getObject(reference), reference, 0, true, opened));
        }
        ++index;
    }

    // Each root is opened: a reference to a root elsewhere in the view is a repeated node, also when the
    // root is outside of the window
    if (opened)
    {
        opened->insert(references.cbegin(), references.cend());
    }
    return roots;
}

qint64 PDFObjectInspector::getRootCount(PDFObjectInspectorView view) const
{
    if (!m_document)
    {
        return 0;
    }

    switch (view)
    {
        case PDFObjectInspectorView::Document:
            return 1;
        case PDFObjectInspectorView::Pages:
            return qint64(getCache()->pages.size());
        case PDFObjectInspectorView::Objects:
            return 1 + qint64(getCache()->objectNumbers.size());
        default:
            return getCache()->classifier.getObjectCountByType(getClassOfView(view));
    }
}

PDFObjectInspectorNode PDFObjectInspector::createTrailerNode(bool isFollowingReferences) const
{
    PDFObjectInspectorNode node;
    node.isTrailer = true;
    node.isFollowingReferences = isFollowingReferences;

    // FD-6: the trailer of a file with a cross-reference stream is the dictionary of the stream
    const PDFObjectStorage& storage = m_document->getStorage();
    if (const PDFDictionary* trailer = storage.getDictionaryFromObject(storage.getTrailerDictionary()))
    {
        node.object = PDFObject::createDictionary(PDFDictionaryBuilder(*trailer));
        node.hasChildren = trailer->getCount() > 0;
    }
    return node;
}

PDFObjectInspectorNode PDFObjectInspector::createNode(PDFObjectReference reference, const PDFObject& object, PDFObjectReference container, int depth, bool isFollowingReferences, OpenedReferences* opened) const
{
    PDFObjectInspectorNode node;
    node.reference = reference;
    node.object = object;
    node.container = container;
    node.depth = depth;
    node.isFollowingReferences = isFollowingReferences;

    bool hasItems = false;
    switch (object.getType())
    {
        case PDFObject::Type::Array:
            hasItems = object.getArray()->getCount() > 0;
            break;
        case PDFObject::Type::Dictionary:
            hasItems = object.getDictionary()->getCount() > 0;
            break;
        case PDFObject::Type::Stream:
            hasItems = object.getStream()->getDictionary()->getCount() > 0;
            break;
        default:
            break;
    }

    // A node at the depth limit is a leaf; its reference is not opened, a shorter path may open it
    const bool isExpandable = hasItems && depth < MAXIMUM_PATH_DEPTH;
    if (reference.isValid())
    {
        auto it = m_objectStreams.find(reference.objectNumber);
        if (it != m_objectStreams.cend())
        {
            node.objectStream = it->second;
            node.objectStreamNote = tr("in object stream %1").arg(getReferenceText(it->second));
        }

        if (depth > 0 && !isFollowingReferences)
        {
            // The Object List: every object is a root of its own (a reference to nothing is no object)
            node.isRepeated = !object.isNull();
            return node;
        }

        if (isFollowingReferences && opened)
        {
            if (depth > 0 && opened->count(reference))
            {
                node.isRepeated = true;
                return node;
            }
            if (isExpandable || depth == 0)
            {
                opened->insert(reference);
            }
        }
    }

    node.hasChildren = isExpandable;
    return node;
}

qint64 PDFObjectInspector::getChildCount(const PDFObjectInspectorNode& node)
{
    if (node.isRepeated || !node.hasChildren || node.depth >= MAXIMUM_PATH_DEPTH)
    {
        return 0;
    }

    const PDFObject& object = node.object;
    if (object.isArray())
    {
        return qint64(object.getArray()->getCount());
    }
    if (object.isDictionary())
    {
        return qint64(object.getDictionary()->getCount());
    }
    if (object.isStream())
    {
        return qint64(object.getStream()->getDictionary()->getCount());
    }
    return 0;
}

std::vector<PDFObjectInspectorNode> PDFObjectInspector::getChildren(const PDFObjectInspectorNode& node, OpenedReferences* opened) const
{
    return getChildren(node, opened, 0, (std::numeric_limits<qint64>::max)());
}

std::vector<PDFObjectInspectorNode> PDFObjectInspector::getChildren(const PDFObjectInspectorNode& node, OpenedReferences* opened, qint64 offset, qint64 count) const
{
    std::vector<PDFObjectInspectorNode> children;
    const qint64 childCount = getChildCount(node);
    if (!m_document || offset < 0 || count <= 0 || offset >= childCount)
    {
        return children;
    }
    const size_t first = size_t(offset);
    const size_t last = size_t(offset + qMin(count, childCount - offset));

    const PDFObjectStorage& storage = m_document->getStorage();
    const PDFObjectReference container = node.reference.isValid() ? node.reference : node.container;
    const int depth = node.depth + 1;

    auto addChild = [&](const PDFObject& value) -> PDFObjectInspectorNode&
    {
        if (value.isReference())
        {
            const PDFObjectReference reference = value.getReference();
            children.push_back(createNode(reference, storage.getObject(reference), reference, depth, node.isFollowingReferences, opened));
        }
        else
        {
            children.push_back(createNode(PDFObjectReference(), value, container, depth, node.isFollowingReferences, opened));
        }
        return children.back();
    };

    const PDFObject& object = node.object;
    children.reserve(last - first);
    if (object.isArray())
    {
        const PDFArray* array = object.getArray();
        for (size_t i = first; i < last; ++i)
        {
            addChild(array->getItem(i)).arrayIndex = PDFInteger(i);
        }
    }
    else if (object.isDictionary() || object.isStream())
    {
        const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : object.getDictionary();
        for (size_t i = first; i < last; ++i)
        {
            addChild(dictionary->getValue(i)).key = dictionary->getKey(i).getString();
        }
    }

    return children;
}

QString PDFObjectInspector::describe(const PDFObjectInspectorNode& node) const
{
    return describe(node, nullptr);
}

QString PDFObjectInspector::describe(const PDFObjectInspectorNode& node, const PDFDocumentToolValueFormatter* formatter) const
{
    if (!formatter)
    {
        formatter = PDFDocumentToolValueFormatter::getCLocale();
    }

    const PDFObject& object = node.object;
    switch (object.getType())
    {
        case PDFObject::Type::Null:
            return QStringLiteral("null");

        case PDFObject::Type::Bool:
            return object.getBool() ? QStringLiteral("true") : QStringLiteral("false");

        case PDFObject::Type::Int:
            return formatter->formatInteger(object.getInteger());

        case PDFObject::Type::Real:
            return QString::fromLatin1(PDFDocumentWriter::getRealNumberText(object.getReal()));

        // A long string or name is cut before its text is made (MAXIMUM_DESCRIBED_BYTES; an even count of bytes
        // keeps the characters of UTF-16BE whole)
        case PDFObject::Type::String:
        {
            const QByteArray string = object.getString();
            const bool isCut = string.size() > MAXIMUM_DESCRIBED_BYTES;
            const QByteArray shown = isCut ? string.left(qsizetype(MAXIMUM_DESCRIBED_BYTES)) : string;
            return QStringLiteral("\"%1\"%2").arg(PDFEncoding::convertSmartFromByteStringToRepresentableQString(shown), isCut ? QStringLiteral("…") : QString());
        }

        case PDFObject::Type::Name:
        {
            const QByteArray name = object.getString();
            const bool isCut = name.size() > MAXIMUM_DESCRIBED_BYTES;
            return PDFObjectInspectorSyntaxWriter::getNameText(isCut ? name.left(qsizetype(MAXIMUM_DESCRIBED_BYTES)) : name) + (isCut ? QStringLiteral("…") : QString());
        }

        case PDFObject::Type::Reference:
            return getReferenceText(object.getReference());

        // The counts of the items are numbers of the formatter (the fix wave of step 7 of phase 2: PDFAppLocale in
        // the GUI, the C locale in the data of MCP), the count chooses the plural form of the translation
        case PDFObject::Type::Array:
        {
            const int itemCount = int(qMin<size_t>(object.getArray()->getCount(), size_t((std::numeric_limits<int>::max)())));
            return tr("Array [%1 items]", nullptr, itemCount).arg(formatter->formatInteger(itemCount));
        }

        case PDFObject::Type::Dictionary:
        {
            const int itemCount = int(qMin<size_t>(object.getDictionary()->getCount(), size_t((std::numeric_limits<int>::max)())));
            return tr("Dictionary [%1 items]", nullptr, itemCount).arg(formatter->formatInteger(itemCount));
        }

        case PDFObject::Type::Stream:
        {
            const PDFStream* stream = object.getStream();
            const PDFDictionary* dictionary = stream->getDictionary();
            const int itemCount = int(qMin<size_t>(dictionary->getCount(), size_t((std::numeric_limits<int>::max)())));
            const QString items = formatter->formatInteger(itemCount);
            const QString bytes = formatter->formatInteger(stream->getContent()->size());

            // An image XObject by its /Subtype only (B185: /Type is optional, Table 87); its size from the
            // dictionary, nothing is decoded
            const PDFObjectStorage* storage = m_document ? &m_document->getStorage() : nullptr;
            const PDFObject& subtype = storage ? storage->getObject(dictionary->get("Subtype")) : dictionary->get("Subtype");
            const PDFObject& width = storage ? storage->getObject(dictionary->get("Width")) : dictionary->get("Width");
            const PDFObject& height = storage ? storage->getObject(dictionary->get("Height")) : dictionary->get("Height");
            if (subtype.isName() && subtype.getString() == "Image" && width.isInt() && height.isInt())
            {
                const PDFObject& bits = storage ? storage->getObject(dictionary->get("BitsPerComponent")) : dictionary->get("BitsPerComponent");
                if (bits.isInt())
                {
                    return tr("Stream [%1 items, %2 data bytes, %3 x %4 pixels, %5 bits per component]", nullptr, itemCount)
                            .arg(items, bytes, formatter->formatInteger(width.getInteger()), formatter->formatInteger(height.getInteger()), formatter->formatInteger(bits.getInteger()));
                }
                return tr("Stream [%1 items, %2 data bytes, %3 x %4 pixels]", nullptr, itemCount)
                        .arg(items, bytes, formatter->formatInteger(width.getInteger()), formatter->formatInteger(height.getInteger()));
            }
            return tr("Stream [%1 items, %2 data bytes]", nullptr, itemCount).arg(items, bytes);
        }

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

QString PDFObjectInspector::getSyntax(const PDFObjectInspectorNode& node, bool* isTruncated) const
{
    PDFObjectInspectorSyntaxWriter writer(MAXIMUM_SHOWN_BYTES);
    writer.write(node.object, 0);
    if (isTruncated)
    {
        *isTruncated = writer.isTruncated();
    }
    return writer.getText();
}

QString PDFObjectInspector::getReferenceText(PDFObjectReference reference)
{
    return QStringLiteral("%1 %2 R").arg(reference.objectNumber).arg(reference.generation);
}

bool PDFObjectInspector::isBinary(const QByteArray& data)
{
    return std::any_of(data.cbegin(), data.cend(), [](char character)
    {
        const unsigned char byte = static_cast<unsigned char>(character);
        return (byte < 0x20 && byte != '\t' && byte != '\n' && byte != '\f' && byte != '\r') || byte == 0x7F;
    });
}

QString PDFObjectInspector::toText(const QByteArray& data)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = decoder.decode(data);
    if (!decoder.hasError())
    {
        return text;
    }
    return QString::fromLatin1(data);
}

QString PDFObjectInspector::createHexDump(const QByteArray& data)
{
    constexpr qsizetype BYTES_PER_LINE = 16;
    QString text;
    text.reserve((data.size() / BYTES_PER_LINE + 1) * 78);
    for (qsizetype offset = 0; offset < data.size(); offset += BYTES_PER_LINE)
    {
        const qsizetype count = qMin(BYTES_PER_LINE, data.size() - offset);
        QString hex;
        QString characters;
        for (qsizetype i = 0; i < BYTES_PER_LINE; ++i)
        {
            if (i < count)
            {
                const unsigned char byte = static_cast<unsigned char>(data[offset + i]);
                hex += QStringLiteral("%1 ").arg(uint(byte), 2, 16, QLatin1Char('0')).toUpper();
                characters += (byte >= 0x20 && byte < 0x7F) ? QLatin1Char(char(byte)) : QLatin1Char('.');
            }
            else
            {
                hex += QStringLiteral("   ");
            }
        }
        text += QStringLiteral("%1  %2 |%3|\n").arg(offset, 8, 16, QLatin1Char('0')).arg(hex, characters);
    }
    return text;
}

PDFObjectInspector::ObjectStreams PDFObjectInspector::findObjectStreams(const PDFDocument* document, const PDFOperationControl* control)
{
    ObjectStreams result;
    if (!document)
    {
        return result;
    }

    const PDFObjectStorage& storage = document->getStorage();
    const PDFObjectStorage::PDFObjects& objects = storage.getObjects();
    for (size_t i = 0; i < objects.size(); ++i)
    {
        if (PDFOperationControl::isOperationCanceled(control))
        {
            break;
        }

        const PDFObject& object = objects[i].object;
        if (!PDFObjectUtils::isObjectStream(object))
        {
            continue;
        }

        // The header of an object stream (ISO 32000-2, 7.5.7): /N pairs of an object number and an offset
        // in the first /First bytes
        const PDFStream* stream = object.getStream();
        const PDFObject& first = storage.getObject(stream->getDictionary()->get("First"));
        const PDFObject& count = storage.getObject(stream->getDictionary()->get("N"));
        if (!first.isInt() || !count.isInt() || first.getInteger() < 0 || count.getInteger() < 0)
        {
            continue;
        }

        // Only the header is decoded (RS7CORE), not the objects after it: a stream of objects of hundreds of
        // megabytes is never decoded whole to read its numbers
        const PDFLimitedStreamDecoder::Result decoded = PDFLimitedStreamDecoder::decodePrefix(stream, [&storage](const PDFObject& value) -> const PDFObject& { return storage.getObject(value); },
                                                                                              storage.getSecurityHandler(), first.getInteger(), INSPECTOR_MAXIMUM_DECODED_SIZE, control);
        if (decoded.status != PDFLimitedStreamDecoder::Status::Decoded)
        {
            continue;
        }

        const QList<QByteArray> tokens = decoded.data.left(qsizetype(qMin<PDFInteger>(first.getInteger(), decoded.data.size()))).simplified().split(' ');
        const PDFObjectReference objectStream(PDFInteger(i), objects[i].generation);
        for (qsizetype token = 0; token + 1 < tokens.size() && token / 2 < count.getInteger(); token += 2)
        {
            bool isNumber = false;
            const PDFInteger number = tokens[token].toLongLong(&isNumber);
            if (isNumber && number >= 0 && number < PDFInteger(objects.size()))
            {
                result[number] = objectStream;
            }
        }
    }

    return result;
}

void PDFObjectInspector::setObjectStreams(ObjectStreams objectStreams)
{
    m_objectStreams = std::move(objectStreams);
}

PDFObjectInspector::DecodedStream PDFObjectInspector::decodeStream(PDFObjectReference reference, qint64 maximumDecodedSize, const PDFOperationControl* control) const
{
    DecodedStream result;
    const PDFObject& object = m_document ? m_document->getStorage().getObject(reference) : PDFObject();
    if (!object.isStream())
    {
        result.status = PDFLimitedStreamDecoder::Status::Error;
        result.errorMessage = tr("The object %1 is not a stream.").arg(getReferenceText(reference));
        return result;
    }

    const PDFObjectStorage& storage = m_document->getStorage();
    PDFLimitedStreamDecoder::Result decoded = PDFLimitedStreamDecoder::decode(object.getStream(), [&storage](const PDFObject& value) -> const PDFObject& { return storage.getObject(value); },
                                                                              storage.getSecurityHandler(), maximumDecodedSize, control);
    result.status = decoded.status;
    result.filterName = decoded.filterName;
    result.errorMessage = decoded.errorMessage;
    result.decodedSize = decoded.data.size();
    result.isTruncated = qint64(decoded.data.size()) > MAXIMUM_SHOWN_BYTES;
    result.data = result.isTruncated ? decoded.data.left(qsizetype(MAXIMUM_SHOWN_BYTES)) : std::move(decoded.data);
    result.isBinary = isBinary(result.data);

    if (decoded.status == PDFLimitedStreamDecoder::Status::Encoded)
    {
        result.errorMessage = tr("The data are encoded by the %1 filter, which is not a decoding filter of streams.").arg(QString::fromLatin1(decoded.filterName));
    }
    return result;
}

QImage PDFObjectInspector::getImage(PDFObjectReference reference, const PDFOperationControl* control, QString* errorMessage) const
{
    QString message;
    QImage image;
    const PDFObject& object = m_document ? m_document->getStorage().getObject(reference) : PDFObject();
    const PDFDocument* document = m_document.data();

    // An image XObject by its /Subtype (B185: /Type is optional)
    const PDFStream* stream = object.isStream() ? object.getStream() : nullptr;
    const PDFDictionary* dictionary = stream ? stream->getDictionary() : nullptr;
    const PDFObject& subtype = dictionary ? document->getObject(dictionary->get("Subtype")) : PDFObject();
    if (!subtype.isName() || subtype.getString() != "Image")
    {
        message = tr("The object %1 is not an image.").arg(getReferenceText(reference));
    }
    else
    {
        // The size of the picture and the decoded data are bounded before anything is decoded
        // (SECURITY_TODO.md 1, 2: the decoders of the images have no limits)
        const PDFObject& width = document->getObject(dictionary->get("Width"));
        const PDFObject& height = document->getObject(dictionary->get("Height"));
        const PDFLimitedStreamDecoder::Result decoded = PDFLimitedStreamDecoder::decode(stream, [document](const PDFObject& value) -> const PDFObject& { return document->getObject(value); },
                                                                                        document->getStorage().getSecurityHandler(), INSPECTOR_MAXIMUM_DECODED_SIZE, control);
        if (!width.isInt() || !height.isInt() || width.getInteger() <= 0 || height.getInteger() <= 0)
        {
            message = tr("The image has no valid size.");
        }
        else if (width.getInteger() > MAXIMUM_IMAGE_PIXELS / height.getInteger())
        {
            message = tr("The image of %1 x %2 pixels is too large to be shown.").arg(width.getInteger()).arg(height.getInteger());
        }
        else if (decoded.status == PDFLimitedStreamDecoder::Status::Canceled)
        {
            message = tr("The operation was canceled.");
        }
        else if (decoded.status != PDFLimitedStreamDecoder::Status::Decoded && decoded.status != PDFLimitedStreamDecoder::Status::Encoded)
        {
            message = decoded.errorMessage;
        }
        else
        {
            try
            {
                PDFRenderErrorReporterDummy reporter;
                PDFColorSpacePointer colorSpace;
                const PDFObject& colorSpaceObject = document->getObject(dictionary->get("ColorSpace"));
                if (colorSpaceObject.isName() || colorSpaceObject.isArray())
                {
                    colorSpace = PDFAbstractColorSpace::createColorSpace(nullptr, document, nullptr, colorSpaceObject);
                }

                const PDFImage pdfImage = PDFImage::createImage(document, stream, std::move(colorSpace), false, RenderingIntent::Perceptual, &reporter);
                const PDFCMSPointer cms = m_cms ? m_cms : PDFCMSPointer(new PDFCMSGeneric());
                image = pdfImage.getImage(cms.data(), &reporter, control);
                if (image.isNull())
                {
                    message = tr("The image cannot be decoded.");
                }
            }
            catch (const PDFException& exception)
            {
                message = tr("The image cannot be decoded: %1").arg(exception.getMessage());
            }
            catch (const PDFRendererException& exception)
            {
                message = tr("The image cannot be decoded: %1").arg(exception.getError().message);
            }
        }
    }

    if (!message.isEmpty())
    {
        image = QImage();
    }
    if (errorMessage)
    {
        *errorMessage = message;
    }
    return image;
}

QString PDFObjectInspector::getViewId(PDFObjectInspectorView view)
{
    switch (view)
    {
        case PDFObjectInspectorView::Document:
            return QStringLiteral("document");
        case PDFObjectInspectorView::Pages:
            return QStringLiteral("pages");
        case PDFObjectInspectorView::ContentStreams:
            return QStringLiteral("content-streams");
        case PDFObjectInspectorView::GraphicStates:
            return QStringLiteral("graphic-states");
        case PDFObjectInspectorView::ColorSpaces:
            return QStringLiteral("color-spaces");
        case PDFObjectInspectorView::Patterns:
            return QStringLiteral("patterns");
        case PDFObjectInspectorView::Shadings:
            return QStringLiteral("shadings");
        case PDFObjectInspectorView::Images:
            return QStringLiteral("images");
        case PDFObjectInspectorView::Forms:
            return QStringLiteral("forms");
        case PDFObjectInspectorView::Fonts:
            return QStringLiteral("fonts");
        case PDFObjectInspectorView::Actions:
            return QStringLiteral("actions");
        case PDFObjectInspectorView::Annotations:
            return QStringLiteral("annotations");
        case PDFObjectInspectorView::Objects:
            return QStringLiteral("objects");
    }
    return QString();
}

std::optional<PDFObjectInspectorView> PDFObjectInspector::findView(const QString& id)
{
    for (const PDFObjectInspectorView view : getViews())
    {
        if (getViewId(view) == id)
        {
            return view;
        }
    }
    return std::nullopt;
}

std::vector<PDFObjectInspectorView> PDFObjectInspector::getViews()
{
    return { PDFObjectInspectorView::Document, PDFObjectInspectorView::Pages, PDFObjectInspectorView::ContentStreams, PDFObjectInspectorView::GraphicStates,
             PDFObjectInspectorView::ColorSpaces, PDFObjectInspectorView::Patterns, PDFObjectInspectorView::Shadings, PDFObjectInspectorView::Images,
             PDFObjectInspectorView::Forms, PDFObjectInspectorView::Fonts, PDFObjectInspectorView::Actions, PDFObjectInspectorView::Annotations,
             PDFObjectInspectorView::Objects };
}

}   // namespace pdf
