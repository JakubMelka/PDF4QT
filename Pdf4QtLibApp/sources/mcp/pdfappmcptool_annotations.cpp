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

#include "pdfappmcptool_annotations.h"
#include "pdfappmcparguments.h"
#include "pdfappmcpchange.h"
#include "pdfappmcphost.h"
#include "pdfappmcptoolset.h"
#include "pdfappmcpactivity.h"
#include "pdfappannotationoperations.h"
#include "pdfappdocument.h"

#include "pdfannotationproperties.h"
#include "pdfdocumenttextindex.h"
#include "pdftextlayout.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QUuid>

#include <cmath>
#include <map>
#include <set>

namespace pdf
{

/// Parsing and building of the actions of the tool annotations
class PDFAppMcpAnnotationsToolHelper
{
public:
    PDFAppMcpAnnotationsToolHelper() = delete;

    /// Longest quote of a change row
    static constexpr int MAXIMUM_QUOTE_LENGTH = 80;

    /// Most quotes of one change row
    static constexpr int MAXIMUM_QUOTES = 3;

    static QString tr(const char* text, int count = -1)
    {
        return QCoreApplication::translate("pdf::PDFAppMcpAnnotationsTool", text, nullptr, count);
    }

    static bool isMarkupType(const QString& type)
    {
        return type == QLatin1String("highlight") || type == QLatin1String("underline") || type == QLatin1String("strikeout") || type == QLatin1String("squiggly");
    }

    static AnnotationType getMarkupType(const QString& type)
    {
        if (type == QLatin1String("underline"))
        {
            return AnnotationType::Underline;
        }
        if (type == QLatin1String("strikeout"))
        {
            return AnnotationType::StrikeOut;
        }
        if (type == QLatin1String("squiggly"))
        {
            return AnnotationType::Squiggly;
        }
        return AnnotationType::Highlight;
    }

    static QString getIconName(const QString& type)
    {
        if (isMarkupType(type))
        {
            return type == QLatin1String("highlight") ? QStringLiteral("highlighter") : QStringLiteral("type");
        }
        if (type == QLatin1String("note"))
        {
            return QStringLiteral("message-square");
        }
        if (type == QLatin1String("text_box"))
        {
            return QStringLiteral("text-box");
        }
        if (type == QLatin1String("stamp"))
        {
            return QStringLiteral("stamp");
        }
        if (type == QLatin1String("link"))
        {
            return QStringLiteral("link");
        }
        if (type == QLatin1String("ink"))
        {
            return QStringLiteral("pen-tool");
        }
        if (type == QLatin1String("ellipse"))
        {
            return QStringLiteral("circle");
        }
        return QStringLiteral("square");
    }

    static std::optional<TextAnnotationIcon> getNoteIcon(const QString& name)
    {
        static const std::pair<const char*, TextAnnotationIcon> icons[] = {
            { "comment", TextAnnotationIcon::Comment }, { "key", TextAnnotationIcon::Key }, { "note", TextAnnotationIcon::Note },
            { "help", TextAnnotationIcon::Help }, { "new_paragraph", TextAnnotationIcon::NewParagraph }, { "paragraph", TextAnnotationIcon::Paragraph },
            { "insert", TextAnnotationIcon::Insert } };
        for (const auto& icon : icons)
        {
            if (name == QLatin1String(icon.first))
            {
                return icon.second;
            }
        }
        return std::nullopt;
    }

    static std::optional<Stamp> getStamp(const QString& name)
    {
        static const std::pair<const char*, Stamp> stamps[] = {
            { "approved", Stamp::Approved }, { "experimental", Stamp::Experimental }, { "not_approved", Stamp::NotApproved },
            { "as_is", Stamp::AsIs }, { "expired", Stamp::Expired }, { "not_for_public_release", Stamp::NotForPublicRelease },
            { "confidential", Stamp::Confidential }, { "final", Stamp::Final }, { "sold", Stamp::Sold }, { "departmental", Stamp::Departmental },
            { "for_comment", Stamp::ForComment }, { "top_secret", Stamp::TopSecret }, { "draft", Stamp::Draft },
            { "for_public_release", Stamp::ForPublicRelease } };
        for (const auto& stamp : stamps)
        {
            if (name == QLatin1String(stamp.first))
            {
                return stamp.second;
            }
        }
        return std::nullopt;
    }

    static bool isFinite(double value)
    {
        return std::isfinite(value) && std::abs(value) <= 1.0e6;
    }

    static std::optional<QRectF> readRect(const QJsonValue& value)
    {
        const QJsonObject object = value.toObject();
        const double left = object.value(QStringLiteral("left")).toDouble(qQNaN());
        const double bottom = object.value(QStringLiteral("bottom")).toDouble(qQNaN());
        const double right = object.value(QStringLiteral("right")).toDouble(qQNaN());
        const double top = object.value(QStringLiteral("top")).toDouble(qQNaN());
        if (!isFinite(left) || !isFinite(bottom) || !isFinite(right) || !isFinite(top))
        {
            return std::nullopt;
        }
        const QRectF rect = QRectF(QPointF(left, bottom), QPointF(right, top)).normalized();
        if (!(rect.width() > 0.0) || !(rect.height() > 0.0))
        {
            return std::nullopt;
        }
        return rect;
    }

    static std::optional<QPointF> readPoint(const QJsonValue& value)
    {
        const QJsonObject object = value.toObject();
        const double x = object.value(QStringLiteral("x")).toDouble(qQNaN());
        const double y = object.value(QStringLiteral("y")).toDouble(qQNaN());
        if (!isFinite(x) || !isFinite(y))
        {
            return std::nullopt;
        }
        return QPointF(x, y);
    }

    static std::optional<QPolygonF> readPoints(const QJsonArray& array)
    {
        QPolygonF points;
        for (const QJsonValue& value : array)
        {
            const std::optional<QPointF> point = readPoint(value);
            if (!point)
            {
                return std::nullopt;
            }
            points << *point;
        }
        return points;
    }

    /// Quads: arrays of 8 numbers in QuadPoints order (top-left, top-right,
    /// bottom-left, bottom-right) - the order of the markup operations
    static std::optional<QPolygonF> readQuads(const QJsonArray& array)
    {
        QPolygonF points;
        for (const QJsonValue& value : array)
        {
            const QJsonArray quad = value.toArray();
            if (quad.size() != 8)
            {
                return std::nullopt;
            }
            for (int i = 0; i < 4; ++i)
            {
                const double x = quad.at(2 * i).toDouble(qQNaN());
                const double y = quad.at(2 * i + 1).toDouble(qQNaN());
                if (!isFinite(x) || !isFinite(y))
                {
                    return std::nullopt;
                }
                points << QPointF(x, y);
            }
        }
        return points;
    }

    /// Text of the page under the quads (the quote of a markup row); empty without the index
    static QString getQuadsText(const PDFDocumentTextIndex* index, PDFInteger pageIndex, const QPolygonF& quads)
    {
        if (!index || pageIndex < 0 || pageIndex >= index->getPageCount())
        {
            return QString();
        }

        std::vector<QPolygonF> polygons;
        for (qsizetype i = 0; i + 3 < quads.size(); i += 4)
        {
            QPolygonF polygon;
            polygon << quads[i] << quads[i + 1] << quads[i + 3] << quads[i + 2];
            polygons.push_back(polygon);
        }

        QString text;
        const PDFTextLayout layout = index->getTextLayout(pageIndex);
        for (const PDFTextBlock& block : layout.getTextBlocks())
        {
            for (const PDFTextLine& line : block.getLines())
            {
                bool isInLine = false;
                for (const TextCharacter& character : line.getCharacters())
                {
                    bool isInside = false;
                    for (const QPolygonF& polygon : polygons)
                    {
                        isInside = isInside || polygon.containsPoint(character.position + QPointF(0.5, 0.5), Qt::OddEvenFill);
                    }
                    if (isInside)
                    {
                        if (!isInLine && !text.isEmpty())
                        {
                            text += QLatin1Char(' ');
                        }
                        isInLine = true;
                        text += character.character;
                    }
                    if (text.size() > MAXIMUM_QUOTE_LENGTH * 2)
                    {
                        return text;
                    }
                }
            }
        }
        return text.simplified();
    }

    static QString quote(const QString& text)
    {
        const QString simplified = PDFAppMcpArguments::sanitizeDocumentText(text, MAXIMUM_QUOTE_LENGTH * 2).simplified();
        if (simplified.isEmpty())
        {
            return QString();
        }
        const QString bounded = simplified.size() > MAXIMUM_QUOTE_LENGTH ? simplified.left(MAXIMUM_QUOTE_LENGTH - 1) + QChar(0x2026) : simplified;
        return QStringLiteral("“%1”").arg(bounded);
    }

    /// "Page 308", "Pages 3–5"
    static QString formatPages(const std::set<int>& pages)
    {
        if (pages.empty())
        {
            return QString();
        }
        if (pages.size() == 1)
        {
            return tr("Page %1").arg(*pages.begin());
        }
        return tr("Pages %1").arg(PDFAppMcpActivity::formatPages(pages));
    }

    /// References of all annotations of the pages of the document and their pages
    static std::map<PDFObjectReference, PDFInteger> collectAnnotations(const PDFDocument* document)
    {
        std::map<PDFObjectReference, PDFInteger> annotations;
        const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
        if (!catalog)
        {
            return annotations;
        }
        for (size_t i = 0; i < catalog->getPageCount(); ++i)
        {
            if (const PDFPage* page = catalog->getPage(i))
            {
                for (const PDFObjectReference& reference : page->getAnnotations())
                {
                    annotations.emplace(reference, PDFInteger(i));
                }
            }
        }
        return annotations;
    }

    static QString getContents(const PDFDocument* document, PDFObjectReference reference)
    {
        const PDFDictionary* dictionary = document ? document->getDictionaryFromObject(document->getObjectByReference(reference)) : nullptr;
        if (!dictionary)
        {
            return QString();
        }
        PDFDocumentDataLoaderDecorator loader(document);
        return loader.readTextStringFromDictionary(dictionary, "Contents", QString());
    }

    /// Reference of an id, the error of an invalid id
    static std::optional<PDFObjectReference> parseId(const QString& id, const QString& path, QString* error)
    {
        qint64 objectNumber = 0;
        qint64 generation = 0;
        if (!PDFAppMcpArguments::parseAnnotationId(id, &objectNumber, &generation))
        {
            *error = QStringLiteral("%1: \"%2\" is not an annotation id; pass the id \"obj:gen\" from document.get_annotations").arg(path, PDFAppMcpArguments::sanitizeDocumentText(id, 32));
            return std::nullopt;
        }
        return PDFObjectReference(objectNumber, generation);
    }
};

QString PDFAppMcpAnnotationsTool::getTypeCountText(const QString& type, int count)
{
    using Helper = PDFAppMcpAnnotationsToolHelper;

    if (type == QLatin1String("highlight"))
    {
        return (count == 1 ? Helper::tr("%n highlight", count) : Helper::tr("%n highlights", count));
    }
    if (type == QLatin1String("underline"))
    {
        return (count == 1 ? Helper::tr("%n underline", count) : Helper::tr("%n underlines", count));
    }
    if (type == QLatin1String("strikeout"))
    {
        return (count == 1 ? Helper::tr("%n strikeout", count) : Helper::tr("%n strikeouts", count));
    }
    if (type == QLatin1String("squiggly"))
    {
        return (count == 1 ? Helper::tr("%n squiggly underline", count) : Helper::tr("%n squiggly underlines", count));
    }
    if (type == QLatin1String("note"))
    {
        return (count == 1 ? Helper::tr("%n note", count) : Helper::tr("%n notes", count));
    }
    if (type == QLatin1String("text_box"))
    {
        return count == 1 ? Helper::tr("%n text box", count) : Helper::tr("%n text boxes", count);
    }
    if (type == QLatin1String("rectangle"))
    {
        return (count == 1 ? Helper::tr("%n rectangle", count) : Helper::tr("%n rectangles", count));
    }
    if (type == QLatin1String("ellipse"))
    {
        return (count == 1 ? Helper::tr("%n ellipse", count) : Helper::tr("%n ellipses", count));
    }
    if (type == QLatin1String("line"))
    {
        return (count == 1 ? Helper::tr("%n line", count) : Helper::tr("%n lines", count));
    }
    if (type == QLatin1String("arrow"))
    {
        return (count == 1 ? Helper::tr("%n arrow", count) : Helper::tr("%n arrows", count));
    }
    if (type == QLatin1String("polygon"))
    {
        return (count == 1 ? Helper::tr("%n polygon", count) : Helper::tr("%n polygons", count));
    }
    if (type == QLatin1String("polyline"))
    {
        return (count == 1 ? Helper::tr("%n polyline", count) : Helper::tr("%n polylines", count));
    }
    if (type == QLatin1String("ink"))
    {
        return (count == 1 ? Helper::tr("%n drawing", count) : Helper::tr("%n drawings", count));
    }
    if (type == QLatin1String("stamp"))
    {
        return (count == 1 ? Helper::tr("%n stamp", count) : Helper::tr("%n stamps", count));
    }
    if (type == QLatin1String("link"))
    {
        return (count == 1 ? Helper::tr("%n link", count) : Helper::tr("%n links", count));
    }
    return (count == 1 ? Helper::tr("%n annotation", count) : Helper::tr("%n annotations", count));
}

QString PDFAppMcpAnnotationsTool::createAddSummary(const QStringList& types)
{
    // The types in their first order with their counts
    QStringList order;
    std::map<QString, int> counts;
    for (const QString& type : types)
    {
        if (counts[type]++ == 0)
        {
            order << type;
        }
    }

    QStringList parts;
    for (const QString& type : order)
    {
        parts << getTypeCountText(type, counts[type]);
    }
    return parts.join(QStringLiteral(", "));
}

QString PDFAppMcpAnnotationsTool::createAddDescription(const QString& clientName, const QStringList& types)
{
    QStringList order;
    std::map<QString, int> counts;
    for (const QString& type : types)
    {
        if (counts[type]++ == 0)
        {
            order << type;
        }
    }

    QStringList parts;
    for (const QString& type : order)
    {
        parts << getTypeCountText(type, counts[type]);
    }
    return PDFAppMcpAnnotationsToolHelper::tr("%1 added %2").arg(clientName, PDFAppMcpChangeJob::joinParts(parts));
}

/// annotations.add: the items are checked in the GUI thread, the worker creates them
class PDFAppMcpAnnotationsAddJob : public PDFAppMcpChangeJob
{
public:
    PDFAppMcpAnnotationsAddJob(PDFAppMcpChangeContext context, std::vector<PDFAppAnnotationItem> items, QStringList types) :
        PDFAppMcpChangeJob(std::move(context)),
        m_items(std::move(items)),
        m_types(std::move(types))
    {

    }

protected:
    virtual PDFAppMcpChange build() override
    {
        using Helper = PDFAppMcpAnnotationsToolHelper;

        PDFAppMcpChange change;
        const PDFAppMcpChangeContext& context = getContext();

        std::vector<PDFAppAnnotationItem> items = m_items;
        for (PDFAppAnnotationItem& item : items)
        {
            item.data.author = context.authorName;
            item.name = QStringLiteral("pdf4qt-mcp-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        }

        std::vector<PDFObjectReference> created;
        int invalidIndex = -1;
        change.modified = PDFAppAnnotationOperations::createAnnotations(getSnapshot().document, items, getCall().get(), &created, &invalidIndex);
        if (!change.modified)
        {
            change.errorText = invalidIndex >= 0 ? QStringLiteral("items[%1]: the %2 could not be created - check its page and geometry (PDF points in the page space).").arg(invalidIndex).arg(m_types.value(invalidIndex))
                                                 : QStringLiteral("The annotations could not be created.");
            return change;
        }

        // Rows of the confirmation per type: the pages and the quotes
        QStringList order;
        std::map<QString, std::vector<size_t>> indicesOfType;
        for (size_t i = 0; i < items.size(); ++i)
        {
            const QString& type = m_types[int(i)];
            if (indicesOfType[type].empty())
            {
                order << type;
            }
            indicesOfType[type].push_back(i);
        }

        for (const QString& type : order)
        {
            const std::vector<size_t>& indices = indicesOfType[type];
            std::set<int> pages;
            QStringList quotes;
            for (size_t index : indices)
            {
                const PDFAppAnnotationItem& item = items[index];
                pages.insert(int(item.pageIndex) + 1);
                if (quotes.size() < Helper::MAXIMUM_QUOTES)
                {
                    QString text = item.data.contents;
                    if (text.isEmpty() && item.kind == PDFAppAnnotationItem::Kind::TextMarkup)
                    {
                        text = Helper::getQuadsText(context.textIndex.get(), item.pageIndex, item.quadrilaterals);
                    }
                    const QString quoted = Helper::quote(text);
                    if (!quoted.isEmpty())
                    {
                        quotes << quoted;
                    }
                }
            }

            PDFAppMcpChangeRow row;
            row.iconName = Helper::getIconName(type);
            row.text = Helper::tr("Add %1").arg(PDFAppMcpAnnotationsTool::getTypeCountText(type, int(indices.size())));
            row.page = Helper::formatPages(pages);
            row.detail = quotes.join(QStringLiteral(", "));
            change.rows.push_back(std::move(row));
        }

        // Ghosts of the preview and the output
        QJsonArray createdArray;
        for (size_t i = 0; i < items.size(); ++i)
        {
            const PDFAppAnnotationItem& item = items[i];
            PDFAppMcpGhost ghost;
            ghost.pageIndex = item.pageIndex;
            ghost.color = item.data.color;
            switch (item.kind)
            {
                case PDFAppAnnotationItem::Kind::TextMarkup:
                    ghost.shape = PDFAppMcpGhost::Shape::Quads;
                    ghost.points = item.quadrilaterals;
                    if (!ghost.color.isValid())
                    {
                        ghost.color = item.markupType == AnnotationType::Highlight ? QColor(Qt::yellow) : QColor(Qt::red);
                    }
                    break;
                case PDFAppAnnotationItem::Kind::StickyNote:
                    ghost.shape = PDFAppMcpGhost::Shape::Note;
                    ghost.points << item.point;
                    break;
                case PDFAppAnnotationItem::Kind::Circle:
                    ghost.shape = PDFAppMcpGhost::Shape::Ellipse;
                    ghost.points << item.rectangle.topLeft() << item.rectangle.bottomRight();
                    break;
                case PDFAppAnnotationItem::Kind::Line:
                case PDFAppAnnotationItem::Kind::Arrow:
                case PDFAppAnnotationItem::Kind::Polyline:
                    ghost.shape = PDFAppMcpGhost::Shape::Polyline;
                    ghost.points = item.points;
                    break;
                case PDFAppAnnotationItem::Kind::Polygon:
                    ghost.shape = PDFAppMcpGhost::Shape::Polygon;
                    ghost.points = item.points;
                    break;
                case PDFAppAnnotationItem::Kind::Ink:
                    ghost.shape = PDFAppMcpGhost::Shape::Ink;
                    ghost.strokes = item.strokes;
                    break;
                default:
                    ghost.shape = PDFAppMcpGhost::Shape::Rectangle;
                    ghost.points << item.rectangle.topLeft() << item.rectangle.bottomRight();
                    break;
            }
            if (int(change.ghosts.size()) < PDFAppMcpGhost::MAXIMUM_GHOSTS)
            {
                change.ghosts.push_back(std::move(ghost));
            }

            QJsonObject object;
            object.insert(QStringLiteral("id"), PDFAppMcpArguments::formatAnnotationId(created[i].objectNumber, created[i].generation));
            object.insert(QStringLiteral("type"), m_types[int(i)]);
            object.insert(QStringLiteral("page"), QJsonValue(qint64(item.pageIndex + 1)));
            createdArray.append(object);
        }

        change.annotations = created;
        change.panelId = QStringLiteral("notes");
        change.changeCount = int(items.size());
        change.summary = PDFAppMcpAnnotationsTool::createAddSummary(m_types);
        change.undoDescription = PDFAppMcpAnnotationsTool::createAddDescription(context.authorName, m_types);
        change.activityDetail.insert(QStringLiteral("count"), int(items.size()));
        change.activityDetail.insert(QStringLiteral("kind"), order.size() == 1 ? order.front() : QStringLiteral("annotation"));
        change.output.emplace_back(QStringLiteral("created"), createdArray);
        return change;
    }

    virtual std::shared_ptr<PDFAppMcpChangeJob> createRerun(PDFAppMcpChangeContext context) const override
    {
        return std::make_shared<PDFAppMcpAnnotationsAddJob>(std::move(context), m_items, m_types);
    }

private:
    std::vector<PDFAppAnnotationItem> m_items;
    QStringList m_types;
};

/// One change of annotations.update
class PDFAppMcpAnnotationUpdate
{
public:
    QString id;
    PDFObjectReference reference;
    std::optional<QString> contents;
    std::optional<QColor> color;
    std::optional<QColor> fillColor;
    std::optional<PDFReal> width;
    std::optional<PDFReal> opacity;
    std::optional<QRectF> rect;
    std::optional<bool> hidden;

    bool hasProperties() const { return color || fillColor || width || opacity; }
    bool hasGeometryOrState() const { return hasProperties() || rect || hidden; }
};

/// annotations.update, delete and reply (the targets are checked by the worker:
/// an annotation, which has disappeared meanwhile, fails the change)
class PDFAppMcpAnnotationsEditJob : public PDFAppMcpChangeJob
{
public:
    enum class Action
    {
        Update,
        Delete,
        Reply
    };

    PDFAppMcpAnnotationsEditJob(PDFAppMcpChangeContext context, Action action, std::vector<PDFAppMcpAnnotationUpdate> updates) :
        PDFAppMcpChangeJob(std::move(context)),
        m_action(action),
        m_updates(std::move(updates))
    {

    }

protected:
    virtual PDFAppMcpChange build() override
    {
        switch (m_action)
        {
            case Action::Update:
                return buildUpdate();
            case Action::Delete:
                return buildDelete();
            case Action::Reply:
                return buildReply();
        }
        return PDFAppMcpChange();
    }

    virtual std::shared_ptr<PDFAppMcpChangeJob> createRerun(PDFAppMcpChangeContext context) const override
    {
        return std::make_shared<PDFAppMcpAnnotationsEditJob>(std::move(context), m_action, m_updates);
    }

private:
    PDFAppMcpChange buildUpdate()
    {
        using Helper = PDFAppMcpAnnotationsToolHelper;

        PDFAppMcpChange change;
        const PDFDocumentPointer original = getSnapshot().document;
        const std::map<PDFObjectReference, PDFInteger> annotations = Helper::collectAnnotations(original.data());

        PDFDocumentPointer document = original;
        PDFModifiedDocument::ModificationFlags flags = PDFModifiedDocument::None;
        std::set<int> pages;
        QJsonArray changed;
        QStringList quotes;
        for (size_t i = 0; i < m_updates.size(); ++i)
        {
            if (getCall() && getCall()->isCanceled())
            {
                change.errorText = QStringLiteral("The request was canceled.");
                change.errorStatus = PDFMcpActivityStatus::Canceled;
                return change;
            }

            const PDFAppMcpAnnotationUpdate& update = m_updates[i];
            auto it = annotations.find(update.reference);
            if (it == annotations.end())
            {
                change.errorText = QStringLiteral("items[%1].id: the annotation %2 does not exist; call document.get_annotations for the current ids.").arg(i).arg(update.id);
                change.isTargetMissing = true;
                return change;
            }
            pages.insert(int(it->second) + 1);

            // ISO 32000-2 Table 167: a Locked annotation changes only its text
            if (update.hasGeometryOrState() && !PDFAppAnnotationOperations::canModifyAnnotation(document, update.reference))
            {
                change.errorText = QStringLiteral("items[%1]: the annotation %2 is locked (or read only); only its contents can change.").arg(i).arg(update.id);
                return change;
            }
            if (update.contents && !PDFAppAnnotationOperations::canModifyContents(document, update.reference))
            {
                change.errorText = QStringLiteral("items[%1]: the text of the annotation %2 is locked.").arg(i).arg(update.id);
                return change;
            }

            PDFAppAnnotationResult result;
            if (update.hasProperties())
            {
                PDFAnnotationProperties properties = PDFAnnotationPropertyEditor::read(&document->getStorage(), update.reference);
                PDFAnnotationProperties::Fields fields = PDFAnnotationProperties::None;
                if (update.color)
                {
                    properties.color = *update.color;
                    fields |= PDFAnnotationProperties::Color;
                }
                if (update.fillColor)
                {
                    properties.interiorColor = *update.fillColor;
                    fields |= PDFAnnotationProperties::InteriorColor;
                }
                if (update.width)
                {
                    properties.borderWidth = *update.width;
                    fields |= PDFAnnotationProperties::Border;
                }
                if (update.opacity)
                {
                    properties.opacity = *update.opacity;
                    fields |= PDFAnnotationProperties::Opacity;
                }
                if (update.contents)
                {
                    properties.contents = *update.contents;
                    fields |= PDFAnnotationProperties::Contents;
                }
                result = PDFAppAnnotationOperations::setProperties(document, update.reference, properties, fields);
                if (!applyResult(result, &document, &flags))
                {
                    change.errorText = QStringLiteral("items[%1]: the properties of the annotation %2 could not be changed.").arg(i).arg(update.id);
                    return change;
                }
            }
            else if (update.contents)
            {
                result = PDFAppAnnotationOperations::setContents(document, update.reference, *update.contents);
                if (!applyResult(result, &document, &flags))
                {
                    change.errorText = QStringLiteral("items[%1]: the text of the annotation %2 could not be changed.").arg(i).arg(update.id);
                    return change;
                }
            }

            if (update.rect)
            {
                result = PDFAppAnnotationOperations::setRectangle(document, update.reference, *update.rect);
                if (!applyResult(result, &document, &flags))
                {
                    change.errorText = QStringLiteral("items[%1].rect: the annotation %2 could not be moved there.").arg(i).arg(update.id);
                    return change;
                }
            }

            if (update.hidden)
            {
                result = PDFAppAnnotationOperations::setAnnotationsHidden(document, { update.reference }, *update.hidden);
                if (result && !applyResult(result, &document, &flags))
                {
                    change.errorText = QStringLiteral("items[%1].hidden: the annotation %2 could not be changed.").arg(i).arg(update.id);
                    return change;
                }
            }

            if (update.contents && quotes.size() < Helper::MAXIMUM_QUOTES)
            {
                // An emptied text has no quote (no empty item in the row)
                const QString quoted = Helper::quote(*update.contents);
                if (!quoted.isEmpty())
                {
                    quotes << quoted;
                }
            }
            changed.append(update.id);
            change.annotations.push_back(update.reference);
        }

        if (document == original)
        {
            change.output.emplace_back(QStringLiteral("changed"), QJsonArray());
            return change;
        }

        const int count = int(m_updates.size());
        PDFAppMcpChangeRow row;
        row.iconName = QStringLiteral("edit");
        row.text = (count == 1 ? Helper::tr("Change %n annotation", count) : Helper::tr("Change %n annotations", count));
        row.page = Helper::formatPages(pages);
        row.detail = quotes.join(QStringLiteral(", "));
        change.rows.push_back(row);

        change.modified = PDFModifiedDocument(document, nullptr, flags | PDFModifiedDocument::Annotation);
        change.changeCount = count;
        change.panelId = QStringLiteral("notes");
        change.summary = row.text;
        change.undoDescription = (count == 1 ? Helper::tr("%1 changed %n annotation", count) : Helper::tr("%1 changed %n annotations", count)).arg(getContext().authorName);
        change.activityDetail.insert(QStringLiteral("count"), count);
        change.output.emplace_back(QStringLiteral("changed"), changed);
        return change;
    }

    PDFAppMcpChange buildDelete()
    {
        using Helper = PDFAppMcpAnnotationsToolHelper;

        PDFAppMcpChange change;
        const PDFDocumentPointer document = getSnapshot().document;
        const std::map<PDFObjectReference, PDFInteger> annotations = Helper::collectAnnotations(document.data());

        std::vector<PDFObjectReference> references;
        std::set<int> pages;
        QStringList quotes;
        QJsonArray deleted;
        for (size_t i = 0; i < m_updates.size(); ++i)
        {
            const PDFAppMcpAnnotationUpdate& update = m_updates[i];
            auto it = annotations.find(update.reference);
            if (it == annotations.end())
            {
                change.errorText = QStringLiteral("ids[%1]: the annotation %2 does not exist; call document.get_annotations for the current ids.").arg(i).arg(update.id);
                change.isTargetMissing = true;
                return change;
            }
            if (!PDFAppAnnotationOperations::canModifyAnnotation(document, update.reference))
            {
                change.errorText = QStringLiteral("ids[%1]: the annotation %2 is locked and cannot be deleted.").arg(i).arg(update.id);
                return change;
            }
            if (std::find(references.begin(), references.end(), update.reference) != references.end())
            {
                continue;
            }
            references.push_back(update.reference);
            pages.insert(int(it->second) + 1);
            if (quotes.size() < Helper::MAXIMUM_QUOTES)
            {
                const QString quoted = Helper::quote(Helper::getContents(document.data(), update.reference));
                if (!quoted.isEmpty())
                {
                    quotes << quoted;
                }
            }
            deleted.append(update.id);
        }

        change.modified = PDFAppAnnotationOperations::deleteAnnotations(document, references);
        if (!change.modified)
        {
            change.errorText = QStringLiteral("The annotations could not be deleted.");
            return change;
        }

        const int count = int(references.size());
        PDFAppMcpChangeRow row;
        row.iconName = QStringLiteral("trash");
        row.text = (count == 1 ? Helper::tr("Delete %n annotation", count) : Helper::tr("Delete %n annotations", count));
        row.page = Helper::formatPages(pages);
        row.detail = quotes.join(QStringLiteral(", "));
        change.rows.push_back(row);
        change.changeCount = count;
        change.summary = row.text;
        change.undoDescription = (count == 1 ? Helper::tr("%1 deleted %n annotation", count) : Helper::tr("%1 deleted %n annotations", count)).arg(getContext().authorName);
        change.activityDetail.insert(QStringLiteral("count"), count);
        change.output.emplace_back(QStringLiteral("deleted"), deleted);
        return change;
    }

    PDFAppMcpChange buildReply()
    {
        using Helper = PDFAppMcpAnnotationsToolHelper;

        PDFAppMcpChange change;
        const PDFDocumentPointer document = getSnapshot().document;
        const std::map<PDFObjectReference, PDFInteger> annotations = Helper::collectAnnotations(document.data());
        if (m_updates.empty() || !m_updates.front().contents)
        {
            change.errorText = QStringLiteral("reply needs id and contents.");
            return change;
        }

        const PDFAppMcpAnnotationUpdate& update = m_updates.front();
        auto it = annotations.find(update.reference);
        if (it == annotations.end())
        {
            change.errorText = QStringLiteral("id: the annotation %1 does not exist; call document.get_annotations for the current ids.").arg(update.id);
            change.isTargetMissing = true;
            return change;
        }

        PDFObjectReference created;
        change.modified = PDFAppAnnotationOperations::addReply(document, update.reference, *update.contents, getContext().authorName, &created);
        if (!change.modified)
        {
            change.errorText = QStringLiteral("The reply could not be added to the annotation %1.").arg(update.id);
            return change;
        }

        PDFAppMcpChangeRow row;
        row.iconName = QStringLiteral("message-square");
        row.text = Helper::tr("Reply to a comment");
        row.page = Helper::formatPages({ int(it->second) + 1 });
        row.detail = Helper::quote(*update.contents);
        change.rows.push_back(row);
        change.changeCount = 1;
        change.annotations = { created };
        change.panelId = QStringLiteral("notes");
        change.summary = row.text;
        change.undoDescription = Helper::tr("%1 replied to a comment").arg(getContext().authorName);
        change.activityDetail.insert(QStringLiteral("count"), 1);
        change.output.emplace_back(QStringLiteral("id"), PDFAppMcpArguments::formatAnnotationId(created.objectNumber, created.generation));
        return change;
    }

    static bool applyResult(const PDFAppAnnotationResult& result, PDFDocumentPointer* document, PDFModifiedDocument::ModificationFlags* flags)
    {
        if (!result)
        {
            return false;
        }
        *document = PDFDocumentPointer(*result);
        *flags |= result->getFlags();
        return !document->isNull();
    }

    Action m_action = Action::Update;
    std::vector<PDFAppMcpAnnotationUpdate> m_updates;
};

void PDFAppMcpAnnotationsTool::handle(const PDFAppMcpToolInvocation& invocation)
{
    using Helper = PDFAppMcpAnnotationsToolHelper;

    PDFAppMcpHost* host = invocation.host;
    const std::shared_ptr<PDFMcpCall>& call = invocation.call;
    const QString actionId = invocation.getActionId();

    PDFAppDocument* document = PDFAppMcpChangeJob::prepareDocument(invocation);
    if (!document)
    {
        return;
    }

    // Restricted permissions of the document (O8): no annotations
    const PDFDocument* pdfDocument = document->getDocument().data();
    const PDFSecurityHandler* securityHandler = pdfDocument ? pdfDocument->getStorage().getSecurityHandler() : nullptr;
    if (securityHandler && !securityHandler->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems))
    {
        host->finishWithError(call, actionId, document, QStringLiteral("The document does not allow changes of annotations (its security settings). Ask the user to use Edit a Copy, if the user may change it."),
                              PDFMcpActivityStatus::Failed);
        return;
    }

    const PDFInteger pageCount = document->getPageCount();
    const QJsonObject& arguments = invocation.actionArguments;
    PDFAppMcpChangeContext context = PDFAppMcpChangeJob::createContext(invocation, document);
    QString error;

    if (invocation.actionName == QLatin1String("add"))
    {
        std::vector<PDFAppAnnotationItem> items;
        QStringList types;
        const QJsonArray array = arguments.value(QStringLiteral("items")).toArray();
        for (qsizetype i = 0; i < array.size() && error.isEmpty(); ++i)
        {
            const QJsonObject object = array.at(i).toObject();
            const QString path = QStringLiteral("items[%1]").arg(i);
            const QString type = object.value(QStringLiteral("type")).toString();
            const int page = object.value(QStringLiteral("page")).toInt(0);
            if (page < 1 || page > pageCount)
            {
                error = QStringLiteral("%1.page: page %2 does not exist; the document has %3 pages.").arg(path).arg(page).arg(pageCount);
                break;
            }

            PDFAppAnnotationItem item;
            item.pageIndex = page - 1;
            if (object.contains(QStringLiteral("contents")))
            {
                item.data.contents = PDFAppMcpArguments::sanitizeDocumentText(object.value(QStringLiteral("contents")).toString(), PDFAppMcpToolSet::MAXIMUM_TEXT_LENGTH);
            }
            for (const char* key : { "color", "fillColor" })
            {
                if (!object.contains(QLatin1String(key)))
                {
                    continue;
                }
                QString colorError;
                const std::optional<QColor> color = PDFAppMcpArguments::parseColor(object.value(QLatin1String(key)).toString(), &colorError);
                if (!color)
                {
                    error = QStringLiteral("%1.%2: %3").arg(path, QLatin1String(key), colorError);
                    break;
                }
                (QLatin1String(key) == QLatin1String("color") ? item.data.color : item.data.fillColor) = *color;
            }
            if (!error.isEmpty())
            {
                break;
            }
            item.data.penWidth = object.value(QStringLiteral("width")).toDouble(1.0);
            item.data.opacity = object.value(QStringLiteral("opacity")).toDouble(1.0);

            if (Helper::isMarkupType(type))
            {
                const std::optional<QPolygonF> quads = Helper::readQuads(object.value(QStringLiteral("quads")).toArray());
                if (!object.contains(QStringLiteral("quads")) || !quads || quads->isEmpty())
                {
                    error = QStringLiteral("%1: %2 needs quads (from text.search)").arg(path, type);
                    break;
                }
                item.kind = PDFAppAnnotationItem::Kind::TextMarkup;
                item.markupType = Helper::getMarkupType(type);
                item.quadrilaterals = *quads;
            }
            else if (type == QLatin1String("note"))
            {
                const std::optional<QPointF> point = Helper::readPoint(object.value(QStringLiteral("point")));
                if (!point)
                {
                    error = QStringLiteral("%1: note needs point {x, y} (the top-left corner of the icon)").arg(path);
                    break;
                }
                item.kind = PDFAppAnnotationItem::Kind::StickyNote;
                item.point = *point;
                item.icon = Helper::getNoteIcon(object.value(QStringLiteral("icon")).toString(QStringLiteral("comment"))).value_or(TextAnnotationIcon::Comment);
            }
            else if (type == QLatin1String("line") || type == QLatin1String("arrow") || type == QLatin1String("polygon") || type == QLatin1String("polyline"))
            {
                const std::optional<QPolygonF> points = Helper::readPoints(object.value(QStringLiteral("points")).toArray());
                const bool isLine = type == QLatin1String("line") || type == QLatin1String("arrow");
                const int minimum = type == QLatin1String("polygon") ? 3 : 2;
                if (!points || points->size() < minimum || (isLine && points->size() != 2))
                {
                    error = isLine ? QStringLiteral("%1: %2 needs points with 2 points").arg(path, type)
                                   : QStringLiteral("%1: %2 needs points with %3 to 1000 points").arg(path, type, QString::number(minimum));
                    break;
                }
                item.kind = type == QLatin1String("line") ? PDFAppAnnotationItem::Kind::Line
                                                          : type == QLatin1String("arrow") ? PDFAppAnnotationItem::Kind::Arrow
                                                                                           : type == QLatin1String("polygon") ? PDFAppAnnotationItem::Kind::Polygon
                                                                                                                              : PDFAppAnnotationItem::Kind::Polyline;
                item.points = *points;
            }
            else if (type == QLatin1String("ink"))
            {
                const QJsonArray strokes = object.value(QStringLiteral("strokes")).toArray();
                for (const QJsonValue& value : strokes)
                {
                    const std::optional<QPolygonF> stroke = Helper::readPoints(value.toArray());
                    if (!stroke || stroke->size() < 2)
                    {
                        item.strokes.clear();
                        break;
                    }
                    item.strokes.push_back(*stroke);
                }
                if (item.strokes.empty())
                {
                    error = QStringLiteral("%1: ink needs strokes (each with at least 2 points)").arg(path);
                    break;
                }
                item.kind = PDFAppAnnotationItem::Kind::Ink;
            }
            else
            {
                // text_box, rectangle, ellipse, stamp, link: a rectangle
                const std::optional<QRectF> rect = Helper::readRect(object.value(QStringLiteral("rect")));
                if (!rect)
                {
                    error = QStringLiteral("%1: %2 needs rect {left, bottom, right, top} with a positive area").arg(path, type);
                    break;
                }
                item.rectangle = *rect;

                if (type == QLatin1String("text_box"))
                {
                    if (item.data.contents.trimmed().isEmpty())
                    {
                        error = QStringLiteral("%1: text_box needs contents (its text)").arg(path);
                        break;
                    }
                    item.kind = PDFAppAnnotationItem::Kind::FreeText;
                    item.fontSize = object.value(QStringLiteral("fontSize")).toDouble(12.0);
                }
                else if (type == QLatin1String("rectangle"))
                {
                    item.kind = PDFAppAnnotationItem::Kind::Square;
                }
                else if (type == QLatin1String("ellipse"))
                {
                    item.kind = PDFAppAnnotationItem::Kind::Circle;
                }
                else if (type == QLatin1String("stamp"))
                {
                    item.kind = PDFAppAnnotationItem::Kind::Stamp;
                    item.stamp = Helper::getStamp(object.value(QStringLiteral("stamp")).toString(QStringLiteral("approved"))).value_or(Stamp::Approved);
                }
                else if (type == QLatin1String("link"))
                {
                    if (object.contains(QStringLiteral("url")))
                    {
                        item.kind = PDFAppAnnotationItem::Kind::WebLink;
                        item.url = object.value(QStringLiteral("url")).toString().trimmed();
                        if (!PDFAppAnnotationOperations::isWebLinkAllowed(item.url))
                        {
                            error = QStringLiteral("%1.url: only http, https and mailto addresses without control characters are allowed").arg(path);
                            break;
                        }
                    }
                    else if (object.contains(QStringLiteral("targetPage")))
                    {
                        const int targetPage = object.value(QStringLiteral("targetPage")).toInt(0);
                        if (targetPage < 1 || targetPage > pageCount)
                        {
                            error = QStringLiteral("%1.targetPage: page %2 does not exist; the document has %3 pages.").arg(path).arg(targetPage).arg(pageCount);
                            break;
                        }
                        item.kind = PDFAppAnnotationItem::Kind::PageLink;
                        item.targetPageIndex = targetPage - 1;
                    }
                    else
                    {
                        error = QStringLiteral("%1: link needs url or targetPage").arg(path);
                        break;
                    }
                }
                else
                {
                    error = QStringLiteral("%1.type: unknown type \"%2\"").arg(path, PDFAppMcpArguments::sanitizeDocumentText(type, 32));
                    break;
                }
            }

            items.push_back(std::move(item));
            types << type;
        }

        if (error.isEmpty() && items.empty())
        {
            error = QStringLiteral("items: at least one annotation is needed");
        }
        if (!error.isEmpty())
        {
            host->finishWithError(call, actionId, document, QStringLiteral("Invalid arguments: %1. See pdf4qt://docs/annotations.md").arg(error), PDFMcpActivityStatus::Failed);
            return;
        }

        host->startJob(std::make_shared<PDFAppMcpAnnotationsAddJob>(std::move(context), std::move(items), std::move(types)));
        return;
    }

    std::vector<PDFAppMcpAnnotationUpdate> updates;
    PDFAppMcpAnnotationsEditJob::Action action = PDFAppMcpAnnotationsEditJob::Action::Update;
    if (invocation.actionName == QLatin1String("update"))
    {
        const QJsonArray array = arguments.value(QStringLiteral("items")).toArray();
        for (qsizetype i = 0; i < array.size() && error.isEmpty(); ++i)
        {
            const QJsonObject object = array.at(i).toObject();
            const QString path = QStringLiteral("items[%1]").arg(i);
            PDFAppMcpAnnotationUpdate update;
            update.id = object.value(QStringLiteral("id")).toString();
            const std::optional<PDFObjectReference> reference = Helper::parseId(update.id, path + QStringLiteral(".id"), &error);
            if (!reference)
            {
                break;
            }
            update.reference = *reference;

            if (object.contains(QStringLiteral("contents")))
            {
                update.contents = PDFAppMcpArguments::sanitizeDocumentText(object.value(QStringLiteral("contents")).toString(), PDFAppMcpToolSet::MAXIMUM_TEXT_LENGTH);
            }
            for (const char* key : { "color", "fillColor" })
            {
                if (!object.contains(QLatin1String(key)))
                {
                    continue;
                }
                QString colorError;
                const std::optional<QColor> color = PDFAppMcpArguments::parseColor(object.value(QLatin1String(key)).toString(), &colorError);
                if (!color)
                {
                    error = QStringLiteral("%1.%2: %3").arg(path, QLatin1String(key), colorError);
                    break;
                }
                (QLatin1String(key) == QLatin1String("color") ? update.color : update.fillColor) = *color;
            }
            if (object.contains(QStringLiteral("width")))
            {
                update.width = qBound(0.0, object.value(QStringLiteral("width")).toDouble(1.0), 100.0);
            }
            if (object.contains(QStringLiteral("opacity")))
            {
                update.opacity = qBound(0.0, object.value(QStringLiteral("opacity")).toDouble(1.0), 1.0);
            }
            if (object.contains(QStringLiteral("rect")))
            {
                update.rect = Helper::readRect(object.value(QStringLiteral("rect")));
                if (!update.rect)
                {
                    error = QStringLiteral("%1.rect: a rectangle {left, bottom, right, top} with a positive area is needed").arg(path);
                    break;
                }
            }
            if (object.contains(QStringLiteral("hidden")))
            {
                update.hidden = object.value(QStringLiteral("hidden")).toBool();
            }
            if (!update.contents && !update.hasGeometryOrState() && error.isEmpty())
            {
                error = QStringLiteral("%1: nothing to change; pass contents, color, fillColor, width, opacity, rect or hidden").arg(path);
            }
            updates.push_back(std::move(update));
        }
    }
    else if (invocation.actionName == QLatin1String("delete"))
    {
        action = PDFAppMcpAnnotationsEditJob::Action::Delete;
        const QJsonArray array = arguments.value(QStringLiteral("ids")).toArray();
        for (qsizetype i = 0; i < array.size() && error.isEmpty(); ++i)
        {
            PDFAppMcpAnnotationUpdate update;
            update.id = array.at(i).toString();
            const std::optional<PDFObjectReference> reference = Helper::parseId(update.id, QStringLiteral("ids[%1]").arg(i), &error);
            if (reference)
            {
                update.reference = *reference;
                updates.push_back(std::move(update));
            }
        }
    }
    else if (invocation.actionName == QLatin1String("reply"))
    {
        action = PDFAppMcpAnnotationsEditJob::Action::Reply;
        PDFAppMcpAnnotationUpdate update;
        update.id = arguments.value(QStringLiteral("id")).toString();
        const std::optional<PDFObjectReference> reference = Helper::parseId(update.id, QStringLiteral("id"), &error);
        const QString contents = PDFAppMcpArguments::sanitizeDocumentText(arguments.value(QStringLiteral("contents")).toString(), PDFAppMcpToolSet::MAXIMUM_TEXT_LENGTH);
        if (reference && contents.trimmed().isEmpty())
        {
            error = QStringLiteral("contents: the text of the reply is empty");
        }
        if (reference)
        {
            update.reference = *reference;
            update.contents = contents;
            updates.push_back(std::move(update));
        }
    }

    if (error.isEmpty() && updates.empty())
    {
        error = QStringLiteral("no annotation is given");
    }
    if (!error.isEmpty())
    {
        host->finishWithError(call, actionId, document, QStringLiteral("Invalid arguments: %1. See pdf4qt://docs/annotations.md").arg(error), PDFMcpActivityStatus::Failed);
        return;
    }

    host->startJob(std::make_shared<PDFAppMcpAnnotationsEditJob>(std::move(context), action, std::move(updates)));
}

}   // namespace pdf
