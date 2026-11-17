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

#include "pdfdocumenttool_objectinspector.h"
#include "pdfdocumenttool_statistics.h"
#include "pdfencoding.h"
#include "pdfobjectinspector.h"
#include "pdfoperationcontrol.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>

#include <limits>
#include <set>

namespace pdf
{

// Skeleton of step 7 of phase 2: the parameters and the permissions are final, package P1a implements the run

QString PDFDocumentToolObjectInspector::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolObjectInspector::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_OBJECT), QT_TR_NOOP("Object"),
                                                   QT_TR_NOOP("The object: a reference \"12 0 R\", \"trailer\" or \"catalog\"; empty = the list of the view"), QStringLiteral("trailer"), 64));

    // The views in the order of the menu (482:15054); the texts of the GUI are its own
    std::vector<PDFDocumentToolParameterOption> views;
    views.push_back({ PDFObjectInspector::getViewId(PDFObjectInspectorView::Document), QT_TR_NOOP("Document") });
    views.push_back({ PDFObjectInspector::getViewId(PDFObjectInspectorView::Pages), QT_TR_NOOP("Pages") });
    views.push_back({ PDFObjectInspector::getViewId(PDFObjectInspectorView::ContentStreams), QT_TR_NOOP("Content streams") });
    views.push_back({ PDFObjectInspector::getViewId(PDFObjectInspectorView::GraphicStates), QT_TR_NOOP("Graphic states") });
    views.push_back({ PDFObjectInspector::getViewId(PDFObjectInspectorView::ColorSpaces), QT_TR_NOOP("Color spaces") });
    views.push_back({ PDFObjectInspector::getViewId(PDFObjectInspectorView::Patterns), QT_TR_NOOP("Patterns") });
    views.push_back({ PDFObjectInspector::getViewId(PDFObjectInspectorView::Shadings), QT_TR_NOOP("Shadings") });
    views.push_back({ PDFObjectInspector::getViewId(PDFObjectInspectorView::Images), QT_TR_NOOP("Images") });
    views.push_back({ PDFObjectInspector::getViewId(PDFObjectInspectorView::Forms), QT_TR_NOOP("Forms") });
    views.push_back({ PDFObjectInspector::getViewId(PDFObjectInspectorView::Fonts), QT_TR_NOOP("Fonts") });
    views.push_back({ PDFObjectInspector::getViewId(PDFObjectInspectorView::Actions), QT_TR_NOOP("Actions") });
    views.push_back({ PDFObjectInspector::getViewId(PDFObjectInspectorView::Annotations), QT_TR_NOOP("Annotations") });
    views.push_back({ PDFObjectInspector::getViewId(PDFObjectInspectorView::Objects), QT_TR_NOOP("Object List") });
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_VIEW), QT_TR_NOOP("View"), QT_TR_NOOP("Which objects are listed"), std::move(views),
                                                 PDFObjectInspector::getViewId(PDFObjectInspectorView::Document)));

    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_DEPTH), QT_TR_NOOP("Depth"), QT_TR_NOOP("Levels of the children of the object"), 1, 1, MAXIMUM_DEPTH));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_DECODE_STREAM), QT_TR_NOOP("Decode stream"), QT_TR_NOOP("The decoded content of a stream is returned"), false));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_MAX_BYTES), QT_TR_NOOP("Most bytes"), QT_TR_NOOP("Most bytes of the content of a stream"), DEFAULT_MAX_BYTES, 1, MAXIMUM_BYTES));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_OFFSET), QT_TR_NOOP("Offset"), QT_TR_NOOP("The first item of the list"), 0, 0, (std::numeric_limits<int>::max)()));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_LIMIT), QT_TR_NOOP("Limit"), QT_TR_NOOP("Most items of the list"), DEFAULT_LIMIT, 1, MAXIMUM_LIMIT));
    return definitions;
}

std::vector<PDFSecurityHandler::Permission> PDFDocumentToolObjectInspector::getRequiredPermissions(const QVariantMap& parameters) const
{
    // Q7.2, R7-22 (6): the content of a stream is an extraction (bit 5), the objects themselves are read
    std::vector<PDFSecurityHandler::Permission> permissions = PDFDocumentTool::getRequiredPermissions(parameters);
    if (parameters.value(QLatin1String(PARAMETER_DECODE_STREAM)).toBool())
    {
        permissions.push_back(PDFSecurityHandler::Permission::CopyContent);
    }
    return permissions;
}

/// The JSON of the objects of Object Inspector for MCP
class PDFDocumentToolObjectInspectorHelper
{
public:
    /// Most values of one object in the JSON (the rest is cut, "truncated")
    static constexpr qint64 MAXIMUM_VALUES = 10000;

    /// Deepest nesting of the direct values written (the parser limits a file, a program may nest deeper)
    static constexpr int MAXIMUM_NESTING = 64;

    /// Most bytes of one string, name or key written (RS7CORE: a hostile string of hundreds of megabytes is cut
    /// before it is converted, not after the whole JSON is made) and of all of them together (the data of MCP
    /// have at most 4 MiB)
    static constexpr qint64 MAXIMUM_STRING_BYTES = 64 * 1024;
    static constexpr qint64 MAXIMUM_TEXT_BYTES = 4 * 1024 * 1024;

    explicit PDFDocumentToolObjectInspectorHelper(const PDFObjectStorage* storage) :
        m_storage(storage)
    {

    }

    static QString name(const char* text) { return QString::fromLatin1(text); }

    /// The JSON of a value; a reference is followed \p referenceLevels times more (one visited set for the
    /// whole object: each indirect object once)
    QJsonValue toJson(const PDFObject& object, int referenceLevels, int nesting)
    {
        if (m_valueCount >= MAXIMUM_VALUES || nesting > MAXIMUM_NESTING)
        {
            m_isTruncated = true;
            return QJsonValue();
        }
        ++m_valueCount;

        QJsonObject json;
        json.insert(QStringLiteral("type"), PDFDocumentToolStatistics::getTypeId(object.getType()));
        switch (object.getType())
        {
            case PDFObject::Type::Null:
                break;

            case PDFObject::Type::Bool:
                json.insert(QStringLiteral("value"), object.getBool());
                break;

            case PDFObject::Type::Int:
                json.insert(QStringLiteral("value"), qint64(object.getInteger()));
                break;

            case PDFObject::Type::Real:
                json.insert(QStringLiteral("value"), object.getReal());
                break;

            case PDFObject::Type::String:
                json.insert(QStringLiteral("value"), PDFEncoding::convertSmartFromByteStringToRepresentableQString(takeText(object.getString(), &json)));
                break;

            case PDFObject::Type::Name:
                json.insert(QStringLiteral("value"), QString::fromLatin1(takeText(object.getString(), &json).toPercentEncoding()));
                break;

            case PDFObject::Type::Reference:
            {
                const PDFObjectReference reference = object.getReference();
                json.insert(QStringLiteral("reference"), PDFObjectInspector::getReferenceText(reference));
                if (referenceLevels > 0 && m_visited.insert(reference).second)
                {
                    const QJsonValue value = toJson(m_storage->getObject(reference), referenceLevels - 1, nesting + 1);
                    if (!value.isNull())
                    {
                        json.insert(QStringLiteral("object"), value);
                    }
                }
                break;
            }

            case PDFObject::Type::Array:
            {
                QJsonArray items;
                const PDFArray* array = object.getArray();
                for (size_t i = 0; i < array->getCount() && !m_isTruncated; ++i)
                {
                    const QJsonValue value = toJson(array->getItem(i), referenceLevels, nesting + 1);
                    if (!m_isTruncated)
                    {
                        items.append(value);
                    }
                }
                json.insert(QStringLiteral("count"), qint64(array->getCount()));
                json.insert(QStringLiteral("items"), items);
                break;
            }

            case PDFObject::Type::Dictionary:
            case PDFObject::Type::Stream:
            {
                // An array of the entries keeps their order of the file
                QJsonArray entries;
                const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : object.getDictionary();
                for (size_t i = 0; i < dictionary->getCount() && !m_isTruncated; ++i)
                {
                    const QJsonValue value = toJson(dictionary->getValue(i), referenceLevels, nesting + 1);
                    if (!m_isTruncated)
                    {
                        QJsonObject entry;
                        const QByteArray key = takeText(dictionary->getKey(i).getString(), &entry);
                        entry.insert(QStringLiteral("key"), QString::fromLatin1(key.toPercentEncoding()));
                        entry.insert(QStringLiteral("value"), value);
                        entries.append(entry);
                    }
                }
                json.insert(QStringLiteral("count"), qint64(dictionary->getCount()));
                json.insert(QStringLiteral("entries"), entries);
                if (object.isStream())
                {
                    json.insert(QStringLiteral("streamLength"), qint64(object.getStream()->getContent()->size()));
                }
                break;
            }

            default:
                break;
        }

        return json;
    }

    void markVisited(PDFObjectReference reference) { m_visited.insert(reference); }
    bool isTruncated() const { return m_isTruncated || m_isTextTruncated; }

    /// The bytes of a string, a name or a key, which are written: at most MAXIMUM_STRING_BYTES of it and the
    /// rest of MAXIMUM_TEXT_BYTES of all of them (an even count, so UTF-16BE stays whole). A cut text has the
    /// entries "length" (its bytes) and "textTruncated" in \p json; when all the bytes are used, the JSON ends.
    QByteArray takeText(const QByteArray& text, QJsonObject* json)
    {
        const qint64 size = text.size();
        qint64 taken = qMin(size, qMin(MAXIMUM_STRING_BYTES, MAXIMUM_TEXT_BYTES - m_textBytes));
        if (taken >= size)
        {
            m_textBytes += size;
            return text;
        }

        taken = qMax(qint64(0), taken - (taken % 2));
        m_textBytes += taken;
        m_isTextTruncated = true;
        m_isTruncated = m_isTruncated || m_textBytes >= MAXIMUM_TEXT_BYTES;
        json->insert(QStringLiteral("length"), size);
        json->insert(QStringLiteral("textTruncated"), true);
        return text.left(qsizetype(taken));
    }

    /// The identifier of a status of the decoding ("decoded", "encoded", "limit-exceeded", "error", "canceled")
    static QString getStatusId(PDFLimitedStreamDecoder::Status status)
    {
        switch (status)
        {
            case PDFLimitedStreamDecoder::Status::Decoded:
                return QStringLiteral("decoded");
            case PDFLimitedStreamDecoder::Status::Encoded:
                return QStringLiteral("encoded");
            case PDFLimitedStreamDecoder::Status::LimitExceeded:
                return QStringLiteral("limit-exceeded");
            case PDFLimitedStreamDecoder::Status::Error:
                return QStringLiteral("error");
            case PDFLimitedStreamDecoder::Status::Canceled:
                return QStringLiteral("canceled");
        }
        return QString();
    }

private:
    const PDFObjectStorage* m_storage;
    std::set<PDFObjectReference> m_visited;
    qint64 m_valueCount = 0;
    qint64 m_textBytes = 0;             ///< Bytes of the strings, names and keys written
    bool m_isTruncated = false;         ///< The JSON ended early (values, nesting, bytes of the texts)
    bool m_isTextTruncated = false;     ///< A text was cut
};

PDFDocumentToolResult PDFDocumentToolObjectInspector::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolObjectInspectorHelper;
    Q_UNUSED(progress);

    const PDFDocument* document = context.getDocument();
    if (!document)
    {
        return PDFDocumentToolResult::createFailed(tr("There is no document."));
    }

    PDFObjectInspector inspector(context.documents.front().document);
    const PDFObjectStorage& storage = document->getStorage();
    const QString objectText = parameters.value(Helper::name(PARAMETER_OBJECT)).toString().trimmed();
    const PDFObjectInspectorView view = PDFObjectInspector::findView(parameters.value(Helper::name(PARAMETER_VIEW)).toString()).value_or(PDFObjectInspectorView::Document);

    PDFDocumentToolResult result;
    if (objectText.isEmpty())
    {
        // The list of the roots of the view, a window of it (plan 7.1: offset / limit at most MAXIMUM_LIMIT)
        if (!inspector.prepare(control))
        {
            return PDFDocumentToolResult::createStopped();
        }
        inspector.setObjectStreams(PDFObjectInspector::findObjectStreams(document, control));
        if (PDFOperationControl::isOperationCanceled(control))
        {
            return PDFDocumentToolResult::createStopped();
        }

        const qint64 offset = qMax(qint64(0), parameters.value(Helper::name(PARAMETER_OFFSET)).toLongLong());
        const qint64 limit = qBound(qint64(1), parameters.value(Helper::name(PARAMETER_LIMIT)).toLongLong(), qint64(MAXIMUM_LIMIT));
        const qint64 total = inspector.getRootCount(view);
        PDFObjectInspector::OpenedReferences opened;
        QJsonArray roots;
        for (const PDFObjectInspectorNode& node : inspector.getRoots(view, &opened, offset, limit))
        {
            QJsonObject root;
            root.insert(QStringLiteral("object"), node.isTrailer ? QStringLiteral("trailer") : PDFObjectInspector::getReferenceText(node.reference));
            root.insert(QStringLiteral("type"), PDFDocumentToolStatistics::getTypeId(node.object.getType()));
            root.insert(QStringLiteral("description"), inspector.describe(node));
            if (node.objectStream.isValid())
            {
                root.insert(QStringLiteral("objectStream"), PDFObjectInspector::getReferenceText(node.objectStream));
            }
            roots.append(root);
        }

        result.data.insert(Helper::name(DATA_ROOTS), roots);
        result.data.insert(Helper::name(DATA_TOTAL), total);
        result.data.insert(Helper::name(DATA_TRUNCATED), offset + qint64(roots.size()) < total);
        result.data.insert(QStringLiteral("view"), PDFObjectInspector::getViewId(view));
        // The facts are machine-readable (MCP): the C locale
        result.facts.push_back({ QStringLiteral("objects"), tr("Objects"), QLocale::c().toString(total) });
        return result;
    }

    // One object: "trailer", "catalog" or a reference "12 0 R"
    PDFObjectInspectorNode node;
    if (objectText == QLatin1String("trailer"))
    {
        node = inspector.getRoots(PDFObjectInspectorView::Document, nullptr).front();
    }
    else
    {
        PDFObjectReference reference;
        if (objectText == QLatin1String("catalog"))
        {
            const PDFDictionary* trailer = storage.getDictionaryFromObject(storage.getTrailerDictionary());
            const PDFObject& root = trailer ? trailer->get("Root") : PDFObject();
            if (root.isReference())
            {
                reference = root.getReference();
            }
        }
        else
        {
            static const QRegularExpression expression(QStringLiteral("^(\\d{1,10})\\s+(\\d{1,5})\\s+R$"));
            const QRegularExpressionMatch match = expression.match(objectText);
            if (!match.hasMatch())
            {
                return PDFDocumentToolResult::createFailed(tr("The object \"%1\" is neither a reference like \"12 0 R\", nor \"trailer\" or \"catalog\".").arg(objectText));
            }
            reference = PDFObjectReference(match.captured(1).toLongLong(), match.captured(2).toLongLong());
        }

        if (!reference.isValid() || storage.getObject(reference).isNull())
        {
            return PDFDocumentToolResult::createFailed(tr("The object %1 does not exist in the document.").arg(reference.isValid() ? PDFObjectInspector::getReferenceText(reference) : objectText));
        }

        node.reference = reference;
        node.container = reference;
        node.object = storage.getObject(reference);
    }

    Helper helper(&storage);
    if (node.reference.isValid())
    {
        helper.markVisited(node.reference);
    }

    const int depth = int(qBound(qint64(1), parameters.value(Helper::name(PARAMETER_DEPTH)).toLongLong(), qint64(MAXIMUM_DEPTH)));
    QJsonObject object = helper.toJson(node.object, depth - 1, 0).toObject();
    object.insert(QStringLiteral("object"), node.isTrailer ? QStringLiteral("trailer") : PDFObjectInspector::getReferenceText(node.reference));
    object.insert(QStringLiteral("description"), inspector.describe(node));
    bool isTruncated = helper.isTruncated();

    // The content of a stream (bit 5, getRequiredPermissions): decoded with the limit of Q7.18, at most
    // maxBytes of it; binary data as hexadecimal digits
    if (parameters.value(Helper::name(PARAMETER_DECODE_STREAM)).toBool() && node.object.isStream())
    {
        const PDFObjectInspector::DecodedStream decoded = inspector.decodeStream(node.reference, PDFObjectInspector::INSPECTOR_MAXIMUM_DECODED_SIZE, control);
        if (decoded.status == PDFLimitedStreamDecoder::Status::Canceled)
        {
            return PDFDocumentToolResult::createStopped();
        }

        const qint64 maxBytes = qBound(qint64(1), parameters.value(Helper::name(PARAMETER_MAX_BYTES)).toLongLong(), qint64(MAXIMUM_BYTES));
        const QByteArray content = decoded.data.left(qsizetype(maxBytes));
        object.insert(QStringLiteral("decodeStatus"), Helper::getStatusId(decoded.status));
        if (!decoded.filterName.isEmpty())
        {
            object.insert(QStringLiteral("filter"), QString::fromLatin1(decoded.filterName));
        }
        if (!decoded.errorMessage.isEmpty())
        {
            object.insert(QStringLiteral("error"), decoded.errorMessage);
        }
        if (decoded.status == PDFLimitedStreamDecoder::Status::Decoded || decoded.status == PDFLimitedStreamDecoder::Status::Encoded)
        {
            const bool isBinary = PDFObjectInspector::isBinary(content);
            object.insert(QStringLiteral("decodedLength"), decoded.decodedSize);
            object.insert(QStringLiteral("isBinary"), isBinary);
            object.insert(QStringLiteral("content"), isBinary ? QString::fromLatin1(content.toHex()) : PDFObjectInspector::toText(content));
            object.insert(QStringLiteral("contentTruncated"), decoded.decodedSize > qint64(content.size()));
            isTruncated = isTruncated || decoded.decodedSize > qint64(content.size());
        }
    }

    result.data.insert(Helper::name(DATA_OBJECT), object);
    result.data.insert(Helper::name(DATA_TRUNCATED), isTruncated);
    return result;
}

}   // namespace pdf
