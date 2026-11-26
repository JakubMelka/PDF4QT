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

// Edit Content, stage E0, package P05 (document writer, tool, JSON): the JSON of the tool
// (GUI_overhaul/edit_tool/zadani_jadro.md 4.6, zadani_overeni.md 6.2, 7.1).
//
// Everything here reads untrusted text (a script of an assistant) or writes data of a document,
// which anybody could have written. So every loop has its limit: the length of a script, the
// number of its items and revisions, the depth of an item, the length of an identifier and of
// its path, the number of the listed objects and of the characters of their texts, the number of
// the containers a revision covers. Nothing here throws and nothing recurses deeper than
// PDFContentJson::MAXIMUM_LIST_DEPTH.

#include "pdfcontentjson.h"

#include "pdfblendfunction.h"
#include "pdfcatalog.h"
#include "pdfcolorspaces.h"
#include "pdfcontenteditsession.h"
#include "pdfcontenttreebuilder.h"
#include "pdfdocument.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfpage.h"
#include "pdfpagecontentprocessor.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringView>

#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace pdf
{

/// Helpers of the JSON of the tool: the syntax of the identifiers and of the names, the numbers
/// and the errors of a script.
///
/// Threads and ownership: static functions only, no state.
class PDFContentJsonHelper
{
public:
    PDFContentJsonHelper() = delete;

    /// Longest temporary name of a script (PDFContentJson::KEY_AS) and longest type of an operation
    static constexpr int MAXIMUM_NAME_LENGTH = 64;

    /// Longest text of an identifier: "p", ten digits of the page, a colon and the path
    static constexpr int MAXIMUM_IDENTIFIER_LENGTH = 1024;

    /// Most nodes of the path of an identifier: a node of the page and a node in each nested form
    static constexpr int MAXIMUM_NODE_PATH_LENGTH = PDFContentEditSession::MAXIMUM_CONTAINER_NESTING + 1;

    /// Most revisions of a script. A script names the revision of every page, whose nodes it
    /// names (also a page it only copies from), so the number of the operations bounds them.
    static constexpr int MAXIMUM_REVISION_COUNT = PDFContentJson::MAXIMUM_OPERATION_COUNT;

    /// Longest revision of a page in bytes (a SHA-256 fingerprint has 32)
    static constexpr int MAXIMUM_REVISION_LENGTH = 64;

    /// Deepest nesting of the values of one operation (its own object is the level 1)
    static constexpr int MAXIMUM_ITEM_DEPTH = 32;

    /// Most errors of a script, which are reported
    static constexpr size_t MAXIMUM_ERROR_COUNT = 100;

    /// The largest real number of a PDF file (ISO 32000-2, Annex C.2: about 3.403e38, the largest
    /// value of a single precision number): a coordinate above it cannot be written
    static constexpr double MAXIMUM_NUMBER = double(std::numeric_limits<float>::max());

    /// Most tiling patterns, whose streams the revision of a page hashes (each is decoded): the
    /// patterns above it are not covered. No identifier names a node of a pattern in stage E0,
    /// so the revision still covers everything an identifier of the page can name.
    static constexpr int MAXIMUM_REVISION_PATTERN_COUNT = 1000;

    /// A number of a revision: its bytes in a fixed order, whatever the machine
    static void hashInteger(QCryptographicHash& hash, qint64 value)
    {
        char bytes[8];
        quint64 unsignedValue = quint64(value);
        for (char& byte : bytes)
        {
            byte = char(unsignedValue & 0xFF);
            unsignedValue >>= 8;
        }
        hash.addData(QByteArrayView(bytes, sizeof(bytes)));
    }

    /// A container of a revision: its key and its fingerprint (the length first, so two
    /// fingerprints in a row never give the bytes of two other ones)
    static void hashContainer(QCryptographicHash& hash, const PDFContentContainerKey& key, const QByteArray& fingerprint)
    {
        hashInteger(hash, qint64(key.kind));
        hashInteger(hash, key.reference.objectNumber);
        hashInteger(hash, key.reference.generation);
        hashInteger(hash, fingerprint.size());
        hash.addData(QByteArrayView(fingerprint));
    }

    /// The four numbers of a rectangle can be written
    static bool isFiniteRect(const QRectF& rect)
    {
        return std::isfinite(rect.left()) && std::isfinite(rect.top()) && std::isfinite(rect.width()) && std::isfinite(rect.height());
    }

    /// The value is a number, which a PDF file can hold
    static bool readNumber(const QJsonValue& value, double* number)
    {
        if (!value.isDouble())
        {
            return false;
        }

        const double result = value.toDouble();
        if (!std::isfinite(result) || std::fabs(result) > MAXIMUM_NUMBER)
        {
            return false;
        }

        *number = result;
        return true;
    }

    /// A decimal number written by ASCII digits only, without a sign and without leading zeros
    /// (one text for one number), at most \p maximum
    static std::optional<quint64> readDecimal(QStringView text, quint64 maximum)
    {
        if (text.isEmpty() || text.size() > 20 || (text.size() > 1 && text.front() == QLatin1Char('0')))
        {
            return std::nullopt;
        }

        quint64 value = 0;
        for (const QChar character : text)
        {
            const char16_t unicode = character.unicode();
            if (unicode < u'0' || unicode > u'9')
            {
                return std::nullopt;
            }

            const quint64 digit = quint64(unicode - u'0');
            if (digit > maximum || value > (maximum - digit) / 10)
            {
                return std::nullopt;
            }
            value = value * 10 + digit;
        }

        return value;
    }

    /// The text has the form of an identifier of a node: "p", at least one ASCII digit and a colon
    /// ("p3:..."). Such a text is never a temporary name of a script (decision 17 of the notes of
    /// the skeleton), so readNodeId never has to choose.
    static bool hasIdentifierForm(QStringView text)
    {
        if (text.size() < 3 || text.front() != QLatin1Char('p'))
        {
            return false;
        }

        qsizetype index = 1;
        while (index < text.size() && text[index].unicode() >= u'0' && text[index].unicode() <= u'9')
        {
            ++index;
        }

        return index > 1 && index < text.size() && text[index] == QLatin1Char(':');
    }

    /// A temporary name of a script can be used: 1 ... MAXIMUM_NAME_LENGTH letters, digits, marks,
    /// punctuation and symbols (no space, no control or format character). The form of an
    /// identifier is checked apart (hasIdentifierForm).
    static bool isNameText(QStringView text)
    {
        if (text.isEmpty() || text.size() > MAXIMUM_NAME_LENGTH)
        {
            return false;
        }

        for (const QChar character : text)
        {
            if (!character.isLetterOrNumber() && !character.isMark() && !character.isPunct() && !character.isSymbol())
            {
                return false;
            }
        }

        return true;
    }

    /// The parts of an identifier of a node
    struct NodeId
    {
        PDFInteger pageNumber = 0;              ///< From 1
        std::vector<PDFContentNodeId> path;     ///< Not empty
    };

    /// Reads the text of an identifier ("p3:57/12"); no value for a text of another form
    static std::optional<NodeId> readNodeIdText(QStringView text)
    {
        if (text.size() > MAXIMUM_IDENTIFIER_LENGTH || !hasIdentifierForm(text))
        {
            return std::nullopt;
        }

        const qsizetype colon = text.indexOf(QLatin1Char(':'));
        const std::optional<quint64> pageNumber = readDecimal(text.mid(1, colon - 1), quint64(PDFDocumentToolPageSelection::MAXIMUM_PAGE_NUMBER));
        if (!pageNumber || *pageNumber == 0)
        {
            return std::nullopt;
        }

        NodeId result;
        result.pageNumber = PDFInteger(*pageNumber);

        qsizetype begin = colon + 1;
        while (true)
        {
            if (int(result.path.size()) >= MAXIMUM_NODE_PATH_LENGTH)
            {
                return std::nullopt;
            }

            const qsizetype separator = text.indexOf(QLatin1Char('/'), begin);
            const qsizetype end = separator < 0 ? text.size() : separator;
            const std::optional<quint64> node = readDecimal(text.mid(begin, end - begin), std::numeric_limits<quint64>::max());
            if (!node || *node == 0)
            {
                return std::nullopt;
            }
            result.path.push_back(PDFContentNodeId{ *node });

            if (separator < 0)
            {
                break;
            }
            begin = separator + 1;
        }

        return result;
    }

    /// Adds an error of a script with its path; the errors above MAXIMUM_ERROR_COUNT are dropped
    static void addError(std::vector<QString>& errors, const QString& path, const QString& text)
    {
        if (errors.size() < MAXIMUM_ERROR_COUNT)
        {
            errors.push_back(QStringLiteral("%1: %2").arg(path, text));
        }
    }

    /// The values of an operation are nested at most MAXIMUM_ITEM_DEPTH deep. The walk has its own
    /// stack (the parser of Qt accepts 1024 levels).
    static bool isItemDepthValid(const QJsonObject& item)
    {
        std::vector<std::pair<QJsonValue, int>> stack;
        stack.emplace_back(QJsonValue(item), 1);

        while (!stack.empty())
        {
            const std::pair<QJsonValue, int> current = std::move(stack.back());
            stack.pop_back();

            if (!current.first.isObject() && !current.first.isArray())
            {
                continue;
            }

            if (current.second > MAXIMUM_ITEM_DEPTH)
            {
                return false;
            }

            if (current.first.isObject())
            {
                const QJsonObject object = current.first.toObject();
                for (auto it = object.constBegin(); it != object.constEnd(); ++it)
                {
                    if (it.value().isObject() || it.value().isArray())
                    {
                        stack.emplace_back(it.value(), current.second + 1);
                    }
                }
            }
            else
            {
                const QJsonArray array = current.first.toArray();
                for (const QJsonValueConstRef value : array)
                {
                    if (value.isObject() || value.isArray())
                    {
                        stack.emplace_back(QJsonValue(value), current.second + 1);
                    }
                }
            }
        }

        return true;
    }

    /// The name of the family of a color space, which is not a device space set by g / rg / k
    static QString getColorSpaceName(const PDFContentColorValue& color)
    {
        const PDFObject& object = color.colorSpaceObject;
        if (object.isName())
        {
            return QString::fromLatin1(object.getString());
        }

        if (object.isArray() && object.getArray()->getCount() > 0 && object.getArray()->getItem(0).isName())
        {
            return QString::fromLatin1(object.getArray()->getItem(0).getString());
        }

        return color.isPattern() ? QStringLiteral("Pattern") : QStringLiteral("Unknown");
    }
};

/// The listing of the objects of one page (PDFContentJson::writePage): the nodes of the page in
/// the order of the content (a node before its children) down to a depth, a window of them.
///
/// The children of a form use are the nodes of the container of the form. A form can be used
/// many times and can use other forms, so the number of the listed nodes can be far larger
/// than the number of the nodes of all containers (a thousand uses of a form of a thousand uses
/// ...): the nodes before the window are therefore never walked one by one. The number of the
/// nodes below a node is computed (for the root of a form once for each depth) and a whole
/// subtree before the window is skipped at once. The numbers saturate.
///
/// Threads and ownership: a local object of one call; it holds the session by a pointer, which
/// the caller keeps valid, and reads immutable containers only.
class PDFContentJsonLister
{
public:
    explicit PDFContentJsonLister(const PDFContentEditSession* session, PDFInteger pageIndex, PDFInteger offset, int maximumObjects, qint64 maximumTextLength) :
        m_session(session),
        m_pageIndex(pageIndex),
        m_skip(quint64(offset)),
        m_emit(maximumObjects),
        m_remainingText(maximumTextLength)
    {

    }

    /// Number of the listed nodes below a node down to the depth (the node itself is not counted)
    quint64 countChildren(const PDFContentContainer& container, const PDFContentNode& node, int remainingDepth)
    {
        if (remainingDepth <= 0)
        {
            return 0;
        }

        if (node.getKind() == PDFContentNodeKind::FormUse)
        {
            const PDFContentContainerPointer form = getFormContainer(node);
            if (!form || !form->getRoot())
            {
                return 0;
            }

            const std::pair<PDFContentContainerKey, int> key(form->getKey(), remainingDepth);
            auto it = m_formCounts.find(key);
            if (it != m_formCounts.cend())
            {
                return it->second;
            }

            const quint64 count = countNodes(*form, form->getRoot()->getChildren(), remainingDepth);
            m_formCounts[key] = count;
            return count;
        }

        return countNodes(container, node.getChildren(), remainingDepth);
    }

    /// The objects of the children of a node inside of the window
    /// \param container The container of the node
    /// \param node The node (the root of the page, or a listed node)
    /// \param forms The nodes, which draw the forms leading to the container of the node (the
    ///        beginning of the identifiers of its nodes; empty for a node of the page)
    /// \param remainingDepth Levels, which are still listed below the node
    /// \param matrix The matrix from the space of the container of the node to the space of the
    ///        page (the identity for a node of the page); no value, when it is not known
    QJsonArray listChildren(const PDFContentContainer& container, const PDFContentNode& node, const std::vector<PDFContentNodeId>& forms, int remainingDepth, const std::optional<QTransform>& matrix)
    {
        QJsonArray objects;
        if (remainingDepth <= 0)
        {
            return objects;
        }

        // The children of a form use are the nodes of the form: their identifiers continue with
        // the use ("p3:57/12") and their space is the space of the form; the children of any
        // other node are nodes of its own container
        PDFContentContainerPointer form;
        const PDFContentContainer* childContainer = &container;
        const std::vector<PDFContentNodePointer>* children = &node.getChildren();
        std::vector<PDFContentNodeId> childForms = forms;
        std::optional<QTransform> childMatrix = matrix;
        if (node.getKind() == PDFContentNodeKind::FormUse)
        {
            form = getFormContainer(node);
            if (!form || !form->getRoot())
            {
                return objects;
            }
            childContainer = form.get();
            children = &form->getRoot()->getChildren();
            childForms.push_back(node.getId());
            childMatrix = getFormMatrix(*form, node, matrix);
        }

        std::vector<PDFContentNodeId> childPath = childForms;
        childPath.push_back(PDFContentNodeId());

        for (const PDFContentNodePointer& child : *children)
        {
            if (m_emit <= 0)
            {
                break;
            }

            if (!child)
            {
                continue;
            }

            const quint64 below = countChildren(*childContainer, *child, remainingDepth - 1);
            const quint64 total = add(below, 1);
            if (m_skip >= total)
            {
                // The whole subtree is before the window
                m_skip -= total;
                continue;
            }

            childPath.back() = child->getId();

            QJsonObject object;
            if (m_skip > 0)
            {
                // The node itself is before the window, some node below it is inside: the node is
                // listed only as the place of its children
                --m_skip;
                object.insert(QStringLiteral("id"), PDFContentJson::writeNodeId(m_pageIndex, childPath));
                object.insert(QStringLiteral("kind"), PDFContentJson::writeNodeKind(child->getKind()));
                object.insert(QStringLiteral("partial"), true);
            }
            else
            {
                object = describe(*childContainer, *child, childPath, childMatrix);

                // The texts of the document, which the object carries, against what is left of
                // the limit of the call: the window ends before an object, which does not fit.
                // The first object is always listed, so every call gets on.
                const qint64 textLength = getTextLength(object);
                if (m_emitted > 0 && textLength > m_remainingText)
                {
                    m_emit = 0;
                    break;
                }

                m_remainingText -= qMin(textLength, m_remainingText);
                --m_emit;
                ++m_emitted;
            }

            if (below > 0 && m_emit > 0)
            {
                const QJsonArray childObjects = listChildren(*childContainer, *child, childForms, remainingDepth - 1, childMatrix);
                if (!childObjects.isEmpty())
                {
                    object.insert(QStringLiteral("children"), childObjects);
                }
            }

            objects.append(object);
        }

        return objects;
    }

    /// Number of the nodes, which were listed whole
    quint64 getEmittedCount() const { return m_emitted; }

    /// Number of the characters of the texts of the document in one object of a listing (the
    /// values of "text" and of "actualText"; its children are not counted)
    static qint64 getTextLength(const QJsonObject& object)
    {
        return qint64(object.value(QStringLiteral("text")).toObject().value(QStringLiteral("value")).toString().size()) +
               qint64(object.value(QStringLiteral("tag")).toObject().value(QStringLiteral("actualText")).toString().size());
    }

private:
    /// The largest number of nodes, which is counted (the numbers saturate here)
    static constexpr quint64 MAXIMUM_COUNT = quint64(1) << 62;

    static quint64 add(quint64 left, quint64 right)
    {
        return (left >= MAXIMUM_COUNT || right >= MAXIMUM_COUNT || left + right >= MAXIMUM_COUNT) ? MAXIMUM_COUNT : left + right;
    }

    quint64 countNodes(const PDFContentContainer& container, const std::vector<PDFContentNodePointer>& nodes, int remainingDepth)
    {
        quint64 count = 0;
        for (const PDFContentNodePointer& node : nodes)
        {
            if (node)
            {
                count = add(count, add(countChildren(container, *node, remainingDepth - 1), 1));
            }
        }
        return count;
    }

    /// The loaded container of the form of a form use; nullptr, when it is not loaded
    PDFContentContainerPointer getFormContainer(const PDFContentNode& node) const
    {
        const PDFObjectReference reference = node.getData().xobject;
        return reference.isValid() ? m_session->getContainer(PDFContentContainerKey::createForm(reference)) : PDFContentContainerPointer();
    }

    /// The matrix from the space of a form to the space of the page for one use of the form:
    /// /Matrix of the form, then the current transformation matrix of the use (ISO 32000-2,
    /// 8.10.1: the form matrix is concatenated with the current transformation matrix), then the
    /// matrix of the container of the use. No value, when the use has no state or its container
    /// has no known matrix.
    static std::optional<QTransform> getFormMatrix(const PDFContentContainer& form, const PDFContentNode& use, const std::optional<QTransform>& matrix)
    {
        if (!matrix || !use.getState())
        {
            return std::nullopt;
        }

        return form.getMatrix() * use.getState()->getState().getCurrentTransformationMatrix() * *matrix;
    }

    /// The text of the show operators of a node (of a TextShow: its own; of a TextObject: of
    /// all its shows in their order), at most PDFContentJson::MAXIMUM_TEXT_LENGTH characters
    static QString getText(const PDFContentNode& node, bool* isTruncated)
    {
        QString text;
        std::vector<const PDFContentNode*> stack;
        stack.push_back(&node);

        while (!stack.empty() && !*isTruncated)
        {
            const PDFContentNode* current = stack.back();
            stack.pop_back();

            if (current->getKind() == PDFContentNodeKind::TextShow)
            {
                for (const PDFContentTextItem& item : current->getData().textItems)
                {
                    if (item.kind != PDFContentTextItem::Kind::Glyph)
                    {
                        continue;
                    }

                    if (text.size() + item.unicode.size() > PDFContentJson::MAXIMUM_TEXT_LENGTH)
                    {
                        *isTruncated = true;
                        break;
                    }
                    text += item.unicode;
                }
                continue;
            }

            // The children in their order: the last one is pushed first
            const std::vector<PDFContentNodePointer>& children = current->getChildren();
            for (auto it = children.crbegin(); it != children.crend(); ++it)
            {
                if (*it)
                {
                    stack.push_back(it->get());
                }
            }
        }

        return text;
    }

    /// The fill or the stroke color of a state as the content has set it
    static QJsonValue getColor(const PDFContentNode& node, bool isFill)
    {
        const PDFPageContentProcessorState& state = node.getState()->getState();
        const PDFAbstractColorSpace* colorSpace = isFill ? state.getFillColorSpace() : state.getStrokeColorSpace();
        if (!colorSpace)
        {
            return QJsonValue();
        }

        PDFContentColorValue value;
        const PDFColor& components = isFill ? state.getFillColorOriginal() : state.getStrokeColorOriginal();
        for (size_t i = 0; i < components.size(); ++i)
        {
            value.components.push_back(PDFReal(components[i]));
        }

        switch (colorSpace->getColorSpace())
        {
            case PDFAbstractColorSpace::ColorSpace::DeviceGray:
                value.deviceSpace = "DeviceGray";
                break;

            case PDFAbstractColorSpace::ColorSpace::DeviceRGB:
                value.deviceSpace = "DeviceRGB";
                break;

            case PDFAbstractColorSpace::ColorSpace::DeviceCMYK:
                value.deviceSpace = "DeviceCMYK";
                break;

            default:
            {
                // Another space: its object is in the symbolic state (the space of cs / CS)
                if (node.getSymbolicState())
                {
                    const PDFContentPropertySource& source = node.getSymbolicState()->getSource(isFill ? PDFContentStateProperty::FillColor : PDFContentStateProperty::StrokeColor);
                    value.colorSpaceObject = source.object;
                    value.patternObject = source.patternObject;
                }
                if (value.colorSpaceObject.isNull() && colorSpace->getColorSpace() == PDFAbstractColorSpace::ColorSpace::Pattern)
                {
                    value.colorSpaceObject = PDFObject::createName("Pattern");
                }
                break;
            }
        }

        return PDFContentJson::writeColor(value);
    }

    /// The appearance of an object: what the operations set_appearance and set_transparency change
    static QJsonObject getAppearance(const PDFContentNode& node)
    {
        QJsonObject appearance;
        if (!node.getState())
        {
            return appearance;
        }

        const PDFPageContentProcessorState& state = node.getState()->getState();
        const PDFContentNodeKind kind = node.getKind();
        const bool isText = kind == PDFContentNodeKind::TextObject || kind == PDFContentNodeKind::TextShow;
        const bool isPath = kind == PDFContentNodeKind::Path;

        if (isText || (isPath && node.getData().paint.fill))
        {
            const QJsonValue color = getColor(node, true);
            if (!color.isNull())
            {
                appearance.insert(QStringLiteral("fill"), color);
            }
        }

        if (isPath && node.getData().paint.stroke)
        {
            const QJsonValue color = getColor(node, false);
            if (!color.isNull())
            {
                appearance.insert(QStringLiteral("stroke"), color);
            }
            appearance.insert(QStringLiteral("lineWidth"), state.getLineWidth());
        }

        if (state.getAlphaFilling() != 1.0)
        {
            appearance.insert(QStringLiteral("alphaFill"), state.getAlphaFilling());
        }
        if (state.getAlphaStroking() != 1.0)
        {
            appearance.insert(QStringLiteral("alphaStroke"), state.getAlphaStroking());
        }
        if (state.getBlendMode() != BlendMode::Normal)
        {
            appearance.insert(QStringLiteral("blendMode"), PDFBlendModeInfo::getBlendModeName(state.getBlendMode()));
        }

        return appearance;
    }

    /// One object of the listing, without its children
    /// \param matrix The matrix from the space of the container to the space of the page; no
    ///        value, when it is not known (the box of the node is listed in the space of its
    ///        form then, and the object says so)
    QJsonObject describe(const PDFContentContainer& container, const PDFContentNode& node, const std::vector<PDFContentNodeId>& path, const std::optional<QTransform>& matrix) const
    {
        const PDFContentNode::Data& data = node.getData();
        const PDFContentNodeKind kind = node.getKind();

        QJsonObject object;
        object.insert(QStringLiteral("id"), PDFContentJson::writeNodeId(m_pageIndex, path));
        object.insert(QStringLiteral("kind"), PDFContentJson::writeNodeKind(kind));

        if (data.boundingBox.isValid())
        {
            // zadani_overeni.md 7.1: the coordinates of a listing are of the page. The box of a
            // node of a form is mapped by the matrix of the use, below which it is listed.
            const QRectF rect = matrix ? matrix->mapRect(data.boundingBox) : data.boundingBox;
            if (PDFContentJsonHelper::isFiniteRect(rect))
            {
                object.insert(QStringLiteral("rect"), PDFContentJson::writeRect(rect));
                if (!matrix)
                {
                    object.insert(QStringLiteral("space"), QStringLiteral("form"));
                }
            }
        }

        if (kind == PDFContentNodeKind::FormUse)
        {
            // The space of the form in the space of the page: the matrices of the operations
            // over the nodes of the form are in the space of the form
            const PDFContentContainerPointer form = getFormContainer(node);
            const std::optional<QTransform> formMatrix = form ? getFormMatrix(*form, node, matrix) : std::nullopt;
            if (formMatrix && std::isfinite(formMatrix->m11()) && std::isfinite(formMatrix->m12()) && std::isfinite(formMatrix->m21()) &&
                std::isfinite(formMatrix->m22()) && std::isfinite(formMatrix->dx()) && std::isfinite(formMatrix->dy()))
            {
                object.insert(QStringLiteral("matrix"), PDFContentJson::writeMatrix(*formMatrix));
            }
        }

        if (kind == PDFContentNodeKind::TextObject || kind == PDFContentNodeKind::TextShow)
        {
            bool isTruncated = false;
            const QString text = getText(node, &isTruncated);
            if (!text.isEmpty())
            {
                // The text comes from the document: it is data, never an instruction
                QJsonObject textObject;
                textObject.insert(QStringLiteral("value"), text);
                textObject.insert(QStringLiteral("origin"), QStringLiteral("document"));
                if (isTruncated)
                {
                    textObject.insert(QStringLiteral("truncated"), true);
                }
                object.insert(QStringLiteral("text"), textObject);
            }
        }

        switch (kind)
        {
            case PDFContentNodeKind::TextObject:
            case PDFContentNodeKind::TextShow:
            case PDFContentNodeKind::Path:
            case PDFContentNodeKind::Image:
            case PDFContentNodeKind::FormUse:
            case PDFContentNodeKind::Shading:
            {
                const QJsonObject appearance = getAppearance(node);
                if (!appearance.isEmpty())
                {
                    object.insert(QStringLiteral("appearance"), appearance);
                }
                break;
            }

            case PDFContentNodeKind::MarkedContent:
            case PDFContentNodeKind::MarkedPoint:
            {
                // The tag is a name of the document, as the text is
                QJsonObject tag;
                tag.insert(QStringLiteral("name"), QString::fromLatin1(data.tag));
                tag.insert(QStringLiteral("origin"), QStringLiteral("document"));
                if (data.mcid)
                {
                    tag.insert(QStringLiteral("mcid"), qint64(*data.mcid));
                }
                if (data.isArtifact)
                {
                    tag.insert(QStringLiteral("artifact"), true);
                }
                if (data.actualText)
                {
                    tag.insert(QStringLiteral("actualText"), data.actualText->left(PDFContentJson::MAXIMUM_TEXT_LENGTH));
                }
                object.insert(QStringLiteral("tag"), tag);
                break;
            }

            default:
                break;
        }

        if (container.isReadOnly() || kind == PDFContentNodeKind::Opaque)
        {
            object.insert(QStringLiteral("readOnly"), true);
        }

        // The number of the children, also when the depth or the window leaves them out
        qint64 childCount = qint64(node.getChildren().size());
        if (kind == PDFContentNodeKind::FormUse)
        {
            const PDFContentContainerPointer form = getFormContainer(node);
            childCount = (form && form->getRoot()) ? qint64(form->getRoot()->getChildren().size()) : 0;
        }
        if (childCount > 0)
        {
            object.insert(QStringLiteral("childCount"), childCount);
        }

        return object;
    }

    const PDFContentEditSession* m_session;
    PDFInteger m_pageIndex;
    quint64 m_skip;         ///< Nodes still to skip before the window
    int m_emit;             ///< Nodes, which can still be listed
    qint64 m_remainingText; ///< Characters of the texts of the document, which can still be listed
    quint64 m_emitted = 0;
    std::map<std::pair<PDFContentContainerKey, int>, quint64> m_formCounts;     ///< Number of the listed nodes of a form down to a depth
};

std::optional<PDFContentScript> PDFContentJson::readScript(const QString& text, std::vector<QString>* errors)
{
    using Helper = PDFContentJsonHelper;

    std::vector<QString> localErrors;
    std::vector<QString>& errorList = errors ? *errors : localErrors;
    const size_t errorCountBefore = errorList.size();
    const QString root = QStringLiteral("operations");

    if (text.size() > PDFDocumentToolParameterDefinition::MAXIMUM_DATA_TEXT_LENGTH)
    {
        errorList.push_back(QStringLiteral("%1: %2").arg(root, tr("The script is longer than %1 characters.").arg(PDFDocumentToolParameterDefinition::MAXIMUM_DATA_TEXT_LENGTH)));
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        errorList.push_back(QStringLiteral("%1: %2").arg(root, tr("The script is not valid JSON (%1 at the offset %2).").arg(parseError.errorString()).arg(parseError.offset)));
        return std::nullopt;
    }

    if (!document.isObject())
    {
        errorList.push_back(QStringLiteral("%1: %2").arg(root, tr("The script must be a JSON object with the keys “revisions” and “items”.")));
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    const QString revisionsKey = QString::fromLatin1(KEY_REVISIONS);
    const QString itemsKey = QString::fromLatin1(KEY_ITEMS);

    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
    {
        if (it.key() != revisionsKey && it.key() != itemsKey)
        {
            Helper::addError(errorList, root, tr("Unknown key “%1”.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(it.key())));
        }
    }

    PDFContentScript script;

    // ---- revisions: { "<page number>": "<fingerprint in hexadecimal digits>" } ----
    const QString revisionsPath = QStringLiteral("%1.%2").arg(root, revisionsKey);
    const QJsonValue revisionsValue = object.value(revisionsKey);
    if (revisionsValue.isObject())
    {
        const QJsonObject revisions = revisionsValue.toObject();
        if (revisions.size() > Helper::MAXIMUM_REVISION_COUNT)
        {
            Helper::addError(errorList, revisionsPath, tr("There are more than %1 revisions.").arg(Helper::MAXIMUM_REVISION_COUNT));
        }
        else
        {
            for (auto it = revisions.constBegin(); it != revisions.constEnd(); ++it)
            {
                const QString path = QStringLiteral("%1.%2").arg(revisionsPath, PDFDocumentToolParameterDefinition::sanitizeForMessage(it.key()));
                const std::optional<quint64> pageNumber = Helper::readDecimal(it.key(), quint64(PDFDocumentToolPageSelection::MAXIMUM_PAGE_NUMBER));
                if (!pageNumber || *pageNumber == 0)
                {
                    Helper::addError(errorList, path, tr("The key of a revision must be a page number from 1."));
                    continue;
                }

                // An even number of hexadecimal digits: QByteArray::fromHex skips other characters
                const QString revisionText = it.value().toString();
                bool isHexadecimal = it.value().isString() && !revisionText.isEmpty() && revisionText.size() % 2 == 0 &&
                                     revisionText.size() <= 2 * Helper::MAXIMUM_REVISION_LENGTH;
                for (qsizetype i = 0; isHexadecimal && i < revisionText.size(); ++i)
                {
                    const char16_t unicode = revisionText[i].unicode();
                    isHexadecimal = (unicode >= u'0' && unicode <= u'9') || (unicode >= u'a' && unicode <= u'f') || (unicode >= u'A' && unicode <= u'F');
                }

                if (!isHexadecimal)
                {
                    Helper::addError(errorList, path, tr("The revision must be the text of the revision, which the mode list gave for the page."));
                    continue;
                }

                script.revisions[PDFInteger(*pageNumber)] = QByteArray::fromHex(revisionText.toLatin1());
            }
        }
    }
    else if (!revisionsValue.isUndefined())
    {
        Helper::addError(errorList, revisionsPath, tr("The revisions must be an object of page numbers and their revisions."));
    }

    // ---- items: [ { "type": ..., ... } ] ----
    const QString itemsPath = QStringLiteral("%1.%2").arg(root, itemsKey);
    const QJsonValue itemsValue = object.value(itemsKey);
    if (!itemsValue.isArray())
    {
        Helper::addError(errorList, itemsPath, tr("The items must be an array of operations."));
    }
    else
    {
        const QJsonArray items = itemsValue.toArray();
        if (items.isEmpty())
        {
            Helper::addError(errorList, itemsPath, tr("There is no operation."));
        }
        else if (items.size() > MAXIMUM_OPERATION_COUNT)
        {
            Helper::addError(errorList, itemsPath, tr("There are more than %1 operations.").arg(MAXIMUM_OPERATION_COUNT));
        }
        else
        {
            const QString typeKey = QString::fromLatin1(KEY_TYPE);
            const QString asKey = QString::fromLatin1(KEY_AS);

            script.items.reserve(size_t(items.size()));
            for (qsizetype i = 0; i < items.size(); ++i)
            {
                const QString path = QStringLiteral("%1[%2]").arg(itemsPath).arg(i);
                const QJsonValue itemValue = items.at(i);
                if (!itemValue.isObject())
                {
                    Helper::addError(errorList, path, tr("An operation must be an object."));
                    continue;
                }

                const QJsonObject item = itemValue.toObject();
                if (!Helper::isItemDepthValid(item))
                {
                    Helper::addError(errorList, path, tr("The values of the operation are nested deeper than %1 levels.").arg(Helper::MAXIMUM_ITEM_DEPTH));
                    continue;
                }

                const QJsonValue type = item.value(typeKey);
                if (!type.isString() || type.toString().isEmpty() || type.toString().size() > Helper::MAXIMUM_NAME_LENGTH)
                {
                    Helper::addError(errorList, QStringLiteral("%1.%2").arg(path, typeKey), tr("The type of the operation is missing."));
                }

                const QJsonValue name = item.value(asKey);
                if (!name.isUndefined())
                {
                    const QString namePath = QStringLiteral("%1.%2").arg(path, asKey);
                    if (!name.isString() || !Helper::isNameText(name.toString()))
                    {
                        Helper::addError(errorList, namePath, tr("The name must be a text of 1 to %1 characters without spaces.").arg(Helper::MAXIMUM_NAME_LENGTH));
                    }
                    else if (Helper::hasIdentifierForm(name.toString()))
                    {
                        // Decision 17: a name never looks like an identifier, so an identifier
                        // of a later operation is never taken for a name
                        Helper::addError(errorList, namePath, tr("The name “%1” has the form of an identifier of an object; choose another name.").arg(name.toString()));
                    }
                }

                script.items.push_back(item);
            }
        }
    }

    if (errorList.size() != errorCountBefore)
    {
        return std::nullopt;
    }

    return script;
}

QString PDFContentJson::writeScript(const PDFContentScript& script)
{
    QJsonObject revisions;
    for (const auto& revision : script.revisions)
    {
        revisions.insert(QString::number(revision.first), QString::fromLatin1(revision.second.toHex()));
    }

    QJsonArray items;
    for (const QJsonObject& item : script.items)
    {
        items.append(item);
    }

    QJsonObject object;
    object.insert(QString::fromLatin1(KEY_REVISIONS), revisions);
    object.insert(QString::fromLatin1(KEY_ITEMS), items);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString PDFContentJson::writeNodeId(PDFInteger pageIndex, const std::vector<PDFContentNodeId>& path)
{
    if (pageIndex < 0 || path.empty())
    {
        return QString();
    }

    QString text = QStringLiteral("p%1:").arg(pageIndex + 1);
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (!path[i].isValid())
        {
            return QString();
        }

        if (i > 0)
        {
            text += QLatin1Char('/');
        }
        text += QString::number(path[i].value);
    }

    return text;
}

PDFContentNodeReference PDFContentJson::readNodeId(const QString& text, const PDFContentEditSession* session, QString* errorText)
{
    using Helper = PDFContentJsonHelper;

    QString dummy;
    QString& error = errorText ? *errorText : dummy;
    error.clear();

    if (!session || !session->getDocument())
    {
        error = tr("There is no document.");
        return PDFContentNodeReference();
    }

    if (text.isEmpty())
    {
        error = tr("The identifier of the object is missing.");
        return PDFContentNodeReference();
    }

    if (!Helper::hasIdentifierForm(text))
    {
        // A temporary name of an earlier operation of the script
        const PDFContentNodeReference named = Helper::isNameText(text) ? session->getScriptName(text) : PDFContentNodeReference();
        if (!named.isValid())
        {
            error = tr("“%1” is neither an identifier of an object (such as p3:57) nor a name given by an earlier operation.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(text));
        }
        return named;
    }

    const std::optional<Helper::NodeId> nodeId = Helper::readNodeIdText(text);
    if (!nodeId)
    {
        error = tr("“%1” is not an identifier of an object (such as p3:57).").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(text));
        return PDFContentNodeReference();
    }

    const PDFInteger pageCount = PDFInteger(session->getDocument()->getCatalog()->getPageCount());
    if (nodeId->pageNumber > pageCount)
    {
        error = tr("The page %1 does not exist.").arg(nodeId->pageNumber);
        return PDFContentNodeReference();
    }

    PDFContentContainerKey key = session->getPageKey(nodeId->pageNumber - 1);
    PDFContentContainerPointer container = key.isValid() ? session->getContainer(key) : PDFContentContainerPointer();
    if (!container)
    {
        error = tr("The content of the page %1 is not loaded: the script has to give the revision of the page.").arg(nodeId->pageNumber);
        return PDFContentNodeReference();
    }

    for (size_t i = 0; i < nodeId->path.size(); ++i)
    {
        const PDFContentNodePointer node = container->getNode(nodeId->path[i]);
        if (!node)
        {
            error = tr("The object %1 does not exist on the page %2.").arg(text).arg(nodeId->pageNumber);
            return PDFContentNodeReference();
        }

        if (i + 1 == nodeId->path.size())
        {
            return PDFContentNodeReference{ key, node->getId() };
        }

        // The next item of the path is a node of the form, which this node draws
        if (node->getKind() != PDFContentNodeKind::FormUse || !node->getData().xobject.isValid())
        {
            error = tr("The object %1 of the page %2 is not a form, it has no objects inside.").arg(node->getId().value).arg(nodeId->pageNumber);
            return PDFContentNodeReference();
        }

        key = PDFContentContainerKey::createForm(node->getData().xobject);
        container = session->getContainer(key);
        if (!container)
        {
            error = tr("The content of the form of the object %1 of the page %2 is not loaded.").arg(node->getId().value).arg(nodeId->pageNumber);
            return PDFContentNodeReference();
        }
    }

    return PDFContentNodeReference();
}

QJsonArray PDFContentJson::writeMatrix(const QTransform& matrix)
{
    return QJsonArray({ matrix.m11(), matrix.m12(), matrix.m21(), matrix.m22(), matrix.dx(), matrix.dy() });
}

std::optional<QTransform> PDFContentJson::readMatrix(const QJsonValue& value)
{
    if (!value.isArray())
    {
        return std::nullopt;
    }

    const QJsonArray array = value.toArray();
    if (array.size() != 6)
    {
        return std::nullopt;
    }

    double numbers[6] = { };
    for (qsizetype i = 0; i < 6; ++i)
    {
        if (!PDFContentJsonHelper::readNumber(array.at(i), &numbers[i]))
        {
            return std::nullopt;
        }
    }

    return QTransform(numbers[0], numbers[1], numbers[2], numbers[3], numbers[4], numbers[5]);
}

QJsonObject PDFContentJson::writeRect(const QRectF& rect)
{
    // The y axis of the user space points up: the smaller y is the bottom
    const QRectF normalized = rect.normalized();

    QJsonObject object;
    object.insert(QStringLiteral("left"), normalized.left());
    object.insert(QStringLiteral("bottom"), normalized.top());
    object.insert(QStringLiteral("right"), normalized.right());
    object.insert(QStringLiteral("top"), normalized.bottom());
    return object;
}

std::optional<QRectF> PDFContentJson::readRect(const QJsonValue& value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    if (object.size() != 4)
    {
        return std::nullopt;
    }

    double left = 0.0;
    double bottom = 0.0;
    double right = 0.0;
    double top = 0.0;
    if (!PDFContentJsonHelper::readNumber(object.value(QStringLiteral("left")), &left) ||
        !PDFContentJsonHelper::readNumber(object.value(QStringLiteral("bottom")), &bottom) ||
        !PDFContentJsonHelper::readNumber(object.value(QStringLiteral("right")), &right) ||
        !PDFContentJsonHelper::readNumber(object.value(QStringLiteral("top")), &top))
    {
        return std::nullopt;
    }

    // The sides are named: a rectangle, whose left side is right of its right one, is a mistake
    if (left > right || bottom > top)
    {
        return std::nullopt;
    }

    return QRectF(left, bottom, right - left, top - bottom);
}

QJsonValue PDFContentJson::writeColor(const PDFContentColorValue& color)
{
    QJsonArray values;
    for (const PDFReal component : color.components)
    {
        values.append(component);
    }

    QJsonObject object;
    object.insert(QStringLiteral("space"), color.deviceSpace.isEmpty() ? PDFContentJsonHelper::getColorSpaceName(color) : QString::fromLatin1(color.deviceSpace));
    object.insert(QStringLiteral("values"), values);
    if (color.isPattern())
    {
        object.insert(QStringLiteral("pattern"), true);
    }
    return object;
}

std::optional<PDFContentColorValue> PDFContentJson::readColor(const QJsonValue& value, QString* errorText)
{
    QString dummy;
    QString& error = errorText ? *errorText : dummy;
    error.clear();

    PDFContentColorValue color;

    if (value.isString())
    {
        // "#rrggbb": DeviceRGB, each component a byte
        const QString text = value.toString();
        bool isValid = text.size() == 7 && text.front() == QLatin1Char('#');
        int bytes[3] = { };
        for (int i = 0; isValid && i < 3; ++i)
        {
            bytes[i] = text.mid(1 + 2 * i, 2).toInt(&isValid, 16);
            for (int j = 0; isValid && j < 2; ++j)
            {
                // toInt accepts a sign and white space
                const char16_t unicode = text[1 + 2 * i + j].unicode();
                isValid = (unicode >= u'0' && unicode <= u'9') || (unicode >= u'a' && unicode <= u'f') || (unicode >= u'A' && unicode <= u'F');
            }
        }

        if (!isValid)
        {
            error = tr("“%1” is not a color; write #rrggbb with six hexadecimal digits.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(text));
            return std::nullopt;
        }

        color.deviceSpace = "DeviceRGB";
        color.components = { bytes[0] / 255.0, bytes[1] / 255.0, bytes[2] / 255.0 };
        return color;
    }

    if (!value.isObject())
    {
        error = tr("A color is the text #rrggbb or an object with the keys “space” and “values”.");
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    const QJsonValue space = object.value(QStringLiteral("space"));
    const QJsonValue values = object.value(QStringLiteral("values"));
    if (object.size() != 2 || !space.isString() || !values.isArray())
    {
        error = tr("A color is the text #rrggbb or an object with the keys “space” and “values”.");
        return std::nullopt;
    }

    // ISO 32000-2, 8.6.4.2 - 8.6.4.4: one, three and four components
    qsizetype componentCount = 0;
    const QString spaceName = space.toString();
    if (spaceName == QLatin1String("DeviceGray"))
    {
        componentCount = 1;
    }
    else if (spaceName == QLatin1String("DeviceRGB"))
    {
        componentCount = 3;
    }
    else if (spaceName == QLatin1String("DeviceCMYK"))
    {
        componentCount = 4;
    }
    else
    {
        error = tr("The color space “%1” cannot be used; the spaces are DeviceGray, DeviceRGB and DeviceCMYK.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(spaceName));
        return std::nullopt;
    }

    const QJsonArray components = values.toArray();
    if (components.size() != componentCount)
    {
        error = tr("A color of the space %1 has %Ln value(s).", "", int(componentCount)).arg(spaceName);
        return std::nullopt;
    }

    for (const QJsonValueConstRef component : components)
    {
        // The operands of g, rg and k are numbers between 0 and 1
        double number = 0.0;
        if (!PDFContentJsonHelper::readNumber(component, &number) || number < 0.0 || number > 1.0)
        {
            error = tr("The values of a color are numbers from 0 to 1.");
            return std::nullopt;
        }
        color.components.push_back(number);
    }

    color.deviceSpace = spaceName.toLatin1();
    return color;
}

QString PDFContentJson::writeNodeKind(PDFContentNodeKind kind)
{
    switch (kind)
    {
        case PDFContentNodeKind::Root:
            return QStringLiteral("root");
        case PDFContentNodeKind::Group:
            return QStringLiteral("group");
        case PDFContentNodeKind::MarkedContent:
            return QStringLiteral("marked_content");
        case PDFContentNodeKind::MarkedPoint:
            return QStringLiteral("marked_point");
        case PDFContentNodeKind::TextObject:
            return QStringLiteral("text");
        case PDFContentNodeKind::TextShow:
            return QStringLiteral("text_run");
        case PDFContentNodeKind::Path:
            return QStringLiteral("path");
        case PDFContentNodeKind::Image:
            return QStringLiteral("image");
        case PDFContentNodeKind::FormUse:
            return QStringLiteral("form");
        case PDFContentNodeKind::Shading:
            return QStringLiteral("shading");
        case PDFContentNodeKind::StateRun:
            return QStringLiteral("state");
        case PDFContentNodeKind::Opaque:
            return QStringLiteral("opaque");
    }

    return QStringLiteral("opaque");
}

QByteArray PDFContentJson::getPageRevision(const PDFContentEditSession* session, PDFInteger pageIndex)
{
    using Helper = PDFContentJsonHelper;

    const PDFDocument* document = session ? session->getDocument() : nullptr;
    if (!document)
    {
        return QByteArray();
    }

    const PDFContentContainerKey pageKey = session->getPageKey(pageIndex);
    const PDFContentContainerPointer page = pageKey.isValid() ? session->getContainer(pageKey) : PDFContentContainerPointer();
    if (!page)
    {
        return QByteArray();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView("PDF4QT page revision 1"));
    Helper::hashContainer(hash, PDFContentContainerKey(), page->getFingerprint());

    // The containers the identifiers of the page reach: the page, the loaded forms its form uses
    // draw, the forms those draw. One visited set for the whole walk (a form drawn twice, a form
    // drawing itself and a pattern named by two containers are entered once); the queue holds a
    // container once, in the order of the content; the nesting is limited as the session
    // limits it.
    std::set<PDFContentContainerKey> visited;
    std::vector<std::pair<PDFContentContainerPointer, int>> queue;
    queue.emplace_back(page, 0);
    visited.insert(pageKey);
    int patternCount = 0;

    std::vector<const PDFContentNode*> stack;
    for (size_t i = 0; i < queue.size(); ++i)
    {
        // Copies: the queue grows
        const PDFContentContainerPointer container = queue[i].first;
        const int nesting = queue[i].second;

        // The tiling patterns the objects of the container can be painted with (an entry of
        // /Pattern, which is no tiling pattern - a shading pattern - has no fingerprint)
        for (const PDFContentResources::Entry& entry : container->getResources().getEntries(PDFContentResourceCategory::Pattern))
        {
            if (!entry.isOriginal || !entry.object.isReference() || patternCount >= Helper::MAXIMUM_REVISION_PATTERN_COUNT)
            {
                continue;
            }

            const PDFContentContainerKey patternKey = PDFContentContainerKey::createPatternCell(entry.object.getReference());
            if (visited.insert(patternKey).second)
            {
                ++patternCount;
                const QByteArray fingerprint = PDFContentTreeBuilder::computeFingerprint(document, patternKey);
                if (!fingerprint.isEmpty())
                {
                    Helper::hashContainer(hash, patternKey, fingerprint);
                }
            }
        }

        if (!container->getRoot() || nesting >= PDFContentEditSession::MAXIMUM_CONTAINER_NESTING)
        {
            continue;
        }

        // The tree is walked with a stack of its own, in the order of the content
        stack.clear();
        stack.push_back(container->getRoot().get());
        while (!stack.empty())
        {
            const PDFContentNode* node = stack.back();
            stack.pop_back();

            if (node->getKind() == PDFContentNodeKind::FormUse && node->getData().xobject.isValid())
            {
                const PDFContentContainerKey formKey = PDFContentContainerKey::createForm(node->getData().xobject);
                if (visited.insert(formKey).second)
                {
                    // A form, which is not loaded, has no node an identifier could name
                    const PDFContentContainerPointer form = session->getContainer(formKey);
                    if (form)
                    {
                        Helper::hashContainer(hash, formKey, form->getFingerprint());
                        queue.emplace_back(form, nesting + 1);
                    }
                }
            }

            const std::vector<PDFContentNodePointer>& children = node->getChildren();
            for (auto it = children.crbegin(); it != children.crend(); ++it)
            {
                if (*it)
                {
                    stack.push_back(it->get());
                }
            }
        }
    }

    return hash.result();
}

QJsonObject PDFContentJson::writePage(const PDFContentEditSession* session, PDFInteger pageIndex, const ListOptions& options)
{
    QJsonObject result;

    const PDFDocument* document = session ? session->getDocument() : nullptr;
    if (!document || pageIndex < 0 || size_t(pageIndex) >= document->getCatalog()->getPageCount())
    {
        return result;
    }

    const PDFContentContainerKey key = session->getPageKey(pageIndex);
    const PDFContentContainerPointer container = key.isValid() ? session->getContainer(key) : PDFContentContainerPointer();
    if (!container || !container->getRoot())
    {
        return result;
    }

    const int depth = qBound(1, options.depth, MAXIMUM_LIST_DEPTH);
    const int maximumObjects = qBound(1, options.maximumObjects, MAXIMUM_LIST_OBJECT_COUNT);
    const PDFInteger offset = qMax(PDFInteger(0), options.offset);

    const PDFPage* page = document->getCatalog()->getPage(size_t(pageIndex));
    const QRectF mediaBox = page->getMediaBox();

    int rotation = 0;
    switch (page->getPageRotation())
    {
        case PageRotation::None:
            rotation = 0;
            break;
        case PageRotation::Rotate90:
            rotation = 90;
            break;
        case PageRotation::Rotate180:
            rotation = 180;
            break;
        case PageRotation::Rotate270:
            rotation = 270;
            break;
    }

    result.insert(QStringLiteral("page"), qint64(pageIndex + 1));
    result.insert(QStringLiteral("revision"), QString::fromLatin1(getPageRevision(session, pageIndex).toHex()));
    result.insert(QStringLiteral("width"), mediaBox.width());
    result.insert(QStringLiteral("height"), mediaBox.height());
    result.insert(QStringLiteral("mediaBox"), writeRect(mediaBox));
    result.insert(QStringLiteral("rotation"), rotation);
    result.insert(QStringLiteral("userUnit"), page->getUserUnit());
    if (container->isReadOnly())
    {
        result.insert(QStringLiteral("readOnly"), true);
    }

    PDFContentJsonLister lister(session, pageIndex, offset, maximumObjects, qBound(qint64(0), options.maximumTextLength, MAXIMUM_LIST_TEXT_LENGTH));
    const quint64 total = lister.countChildren(*container, *container->getRoot(), depth);
    result.insert(QStringLiteral("objects"), lister.listChildren(*container, *container->getRoot(), std::vector<PDFContentNodeId>(), depth, QTransform()));

    // The window ended before the last node: the next call continues behind it
    const quint64 end = quint64(offset) + lister.getEmittedCount();
    if (lister.getEmittedCount() > 0 && end < total)
    {
        result.insert(QStringLiteral("nextOffset"), qint64(end));
    }

    return result;
}

}   // namespace pdf
