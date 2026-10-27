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

#include "pdfappmcptool_outline.h"
#include "pdfappmcparguments.h"
#include "pdfappmcpchange.h"
#include "pdfappmcphost.h"
#include "pdfappmcptoolset.h"
#include "pdfappdocument.h"

#include "pdfaction.h"
#include "pdfmcpjson.h"
#include "pdfoutline.h"

#include <QCoreApplication>
#include <QJsonArray>

#include <cmath>

namespace pdf
{

/// Request of an action of the tool outline (checked in the GUI thread)
class PDFAppMcpOutlineRequest
{
public:
    enum class Action
    {
        Add,
        Update,
        Move,
        Delete,
        Replace
    };

    /// One item of replace
    class Node
    {
    public:
        QString title;
        int level = 1;
        PDFInteger pageIndex = -1;
    };

    Action action = Action::Add;
    std::optional<PDFAppAnnotationOperations::OutlinePath> item;    ///< update / move (empty with title)
    QString itemTitle;                                              ///< update / delete / move given by the title
    std::optional<PDFAppAnnotationOperations::OutlinePath> parent;  ///< add / move; std::nullopt = the top level
    std::optional<size_t> index;
    QString title;
    std::optional<QString> newTitle;
    PDFInteger pageIndex = -1;
    QString zoom = QStringLiteral("keep");
    std::optional<bool> bold;
    std::optional<bool> italic;
    std::vector<PDFAppAnnotationOperations::OutlinePath> items;     ///< delete
    std::vector<Node> nodes;                                        ///< replace
};

/// Helpers of the tool outline
class PDFAppMcpOutlineToolHelper
{
public:
    PDFAppMcpOutlineToolHelper() = delete;

    using OutlinePath = PDFAppAnnotationOperations::OutlinePath;

    static QString tr(const char* text, int count = -1)
    {
        return QCoreApplication::translate("pdf::PDFAppMcpOutlineTool", text, nullptr, count);
    }

    static QString quote(const QString& text)
    {
        QString bounded = PDFAppMcpArguments::sanitizeDocumentText(text, 200).simplified();
        if (bounded.size() > 60)
        {
            bounded = bounded.left(59) + QChar(0x2026);
        }
        return QStringLiteral("“%1”").arg(bounded);
    }

    /// Item of the path (nullptr = none; the root for an empty path)
    static const PDFOutlineItem* findItem(const PDFOutlineItem* root, const OutlinePath& path)
    {
        const PDFOutlineItem* item = root;
        for (size_t index : path)
        {
            if (!item || index >= item->getChildCount())
            {
                return nullptr;
            }
            item = item->getChild(index);
        }
        return item;
    }

    /// Paths of the items with the title (compared without the case of the letters)
    static void findItemsByTitle(const PDFOutlineItem* item, const QString& title, OutlinePath& path, std::vector<OutlinePath>* result, size_t depth)
    {
        if (!item || depth > PDFAppAnnotationOperations::MAXIMUM_OUTLINE_DEPTH || result->size() >= 100)
        {
            return;
        }
        for (size_t i = 0; i < item->getChildCount(); ++i)
        {
            const PDFOutlineItem* child = item->getChild(i);
            path.push_back(i);
            if (child && child->getTitle().trimmed().compare(title, Qt::CaseInsensitive) == 0)
            {
                result->push_back(path);
            }
            findItemsByTitle(child, title, path, result, depth + 1);
            path.pop_back();
        }
    }

    /// "Chapter 2 › Results" of a path
    static QString getTitlePath(const PDFOutlineItem* root, const OutlinePath& path)
    {
        QStringList titles;
        const PDFOutlineItem* item = root;
        for (size_t index : path)
        {
            if (!item || index >= item->getChildCount())
            {
                break;
            }
            item = item->getChild(index);
            titles << PDFAppMcpArguments::sanitizeDocumentText(item ? item->getTitle() : QString(), 80).simplified();
        }
        return titles.join(QStringLiteral(" › "));
    }

    /// Destination of a page (zoom fit_page / fit_width / keep)
    static PDFDestination createDestination(const PDFDocument* document, PDFInteger pageIndex, const QString& zoom)
    {
        const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
        const PDFPage* page = catalog && pageIndex >= 0 && size_t(pageIndex) < catalog->getPageCount() ? catalog->getPage(size_t(pageIndex)) : nullptr;
        if (!page)
        {
            return PDFDestination();
        }

        const QRectF box = page->getCropBox();
        const PDFReal top = qMax(box.top(), box.bottom());
        if (zoom == QLatin1String("fit_page"))
        {
            return PDFDestination::createFit(page->getPageReference());
        }
        if (zoom == QLatin1String("fit_width"))
        {
            return PDFDestination::createFitH(page->getPageReference(), top);
        }

        // keep: the top of the page with the zoom of the reader (a null zoom)
        return PDFDestination::createXYZ(page->getPageReference(), qQNaN(), top, qQNaN());
    }

    static PDFAppAnnotationResult chain(const PDFAppAnnotationResult& result, PDFDocumentPointer* document, PDFModifiedDocument::ModificationFlags* flags)
    {
        if (result)
        {
            *document = PDFDocumentPointer(*result);
            *flags |= result->getFlags();
        }
        return result;
    }
};

std::optional<PDFAppAnnotationOperations::OutlinePath> PDFAppMcpOutlineTool::parseItemId(const QString& id)
{
    const QStringList parts = id.trimmed().split(QLatin1Char('.'));
    if (parts.isEmpty() || parts.size() > int(PDFAppAnnotationOperations::MAXIMUM_OUTLINE_DEPTH))
    {
        return std::nullopt;
    }

    PDFAppAnnotationOperations::OutlinePath path;
    for (const QString& part : parts)
    {
        bool ok = false;
        const int number = part.toInt(&ok);
        if (!ok || number < 1 || number > int(PDFAppAnnotationOperations::MAXIMUM_OUTLINE_ITEMS) || part.size() > 7)
        {
            return std::nullopt;
        }
        path.push_back(size_t(number - 1));
    }
    return path;
}

QString PDFAppMcpOutlineTool::formatItemId(const PDFAppAnnotationOperations::OutlinePath& path)
{
    QStringList parts;
    for (size_t index : path)
    {
        parts << QString::number(index + 1);
    }
    return parts.join(QLatin1Char('.'));
}

/// The actions of the tool outline on a snapshot
class PDFAppMcpOutlineJob : public PDFAppMcpChangeJob
{
public:
    /// Created in the GUI thread: the version of the outline of the snapshot
    PDFAppMcpOutlineJob(PDFAppMcpChangeContext context, PDFAppMcpOutlineRequest request) :
        PDFAppMcpChangeJob(std::move(context)),
        m_request(std::move(request))
    {
        const PDFAppDocument* document = getContext().document;
        m_outlineVersion = document ? document->getOutlineVersion() : 0;
    }

    /// The request addresses items by their positions (ids "2.1", a parent, an
    /// index), which are valid only for the outline of the request
    bool isPositional() const
    {
        return (m_request.item && m_request.itemTitle.isEmpty()) || m_request.parent || m_request.index || !m_request.items.empty();
    }

protected:
    virtual PDFAppMcpChange build() override
    {
        using Helper = PDFAppMcpOutlineToolHelper;

        PDFAppMcpChange change;
        if (m_isOutlineChanged)
        {
            // Rebuilt after the outline changed (the user, another request): an id
            // may name another item now - never changed in its place
            change.errorText = QStringLiteral("the outline was changed in PDF4QT, so the item ids are not valid any more. Call document.get_outline and try again.");
            change.isTargetMissing = true;
            return change;
        }

        const PDFDocumentPointer original = getSnapshot().document;
        const PDFCatalog* catalog = original ? original->getCatalog() : nullptr;
        if (!catalog || !PDFAppAnnotationOperations::isOutlineEditable(original.data()))
        {
            change.errorText = QStringLiteral("The outline of the document cannot be edited (it is damaged, has shared items or is too large).");
            return change;
        }

        // An empty outline is a root without children
        QSharedPointer<PDFOutlineItem> root = catalog->getOutlineRootPtr();
        if (!root)
        {
            root.reset(new PDFOutlineItem());
        }

        // An item given by its title: the only item of the title, or a choice
        PDFAppMcpOutlineRequest request = m_request;
        if (!request.item && !request.itemTitle.isEmpty() && request.action != PDFAppMcpOutlineRequest::Action::Replace)
        {
            std::vector<PDFAppAnnotationOperations::OutlinePath> matches;
            PDFAppAnnotationOperations::OutlinePath path;
            Helper::findItemsByTitle(root.data(), request.itemTitle.trimmed(), path, &matches, 0);
            if (matches.empty())
            {
                change.errorText = QStringLiteral("title: no outline item has the title \"%1\"; call document.get_outline for the items.").arg(PDFAppMcpArguments::sanitizeDocumentText(request.itemTitle, 80));
                change.isTargetMissing = true;
                return change;
            }

            if (matches.size() == 1)
            {
                request.item = matches.front();
            }
            else
            {
                bool isDeclined = false;
                const std::optional<QJsonObject> answer = PDFAppMcpChoice::getAnswer(getCall()->getRequest(), &isDeclined);
                const QString chosen = answer ? answer->value(QStringLiteral("item")).toString() : QString();
                QStringList candidates;
                PDFAppMcpChoice::Property property;
                property.name = QStringLiteral("item");
                property.title = QStringLiteral("Outline item");
                for (const PDFAppAnnotationOperations::OutlinePath& match : matches)
                {
                    const QString id = PDFAppMcpOutlineTool::formatItemId(match);
                    if (id == chosen)
                    {
                        request.item = match;
                    }
                    property.options.emplace_back(id, Helper::getTitlePath(root.data(), match));
                    candidates << QStringLiteral("%1 (%2)").arg(id, Helper::getTitlePath(root.data(), match));
                }

                if (isDeclined)
                {
                    change.errorText = QStringLiteral("The user declined to choose the outline item. Pass item (its id).");
                    return change;
                }
                if (!request.item)
                {
                    change.choices.push_back(std::move(property));
                    change.choiceMessage = QStringLiteral("Several outline items have the title \"%1\". Choose the item.").arg(PDFAppMcpArguments::sanitizeDocumentText(request.itemTitle, 80));
                    change.choiceFallback = QStringLiteral("Several outline items have the title: %1. Pass item (its id).").arg(candidates.join(QStringLiteral(", ")));
                    return change;
                }
            }
        }

        PDFDocumentPointer document = original;
        PDFModifiedDocument::ModificationFlags flags = PDFModifiedDocument::None;
        QString detail;
        PDFAppAnnotationOperations::OutlinePath resultPath;
        int count = 0;

        switch (request.action)
        {
            case PDFAppMcpOutlineRequest::Action::Add:
            {
                const PDFAppAnnotationOperations::OutlinePath parent = request.parent.value_or(PDFAppAnnotationOperations::OutlinePath());
                const PDFOutlineItem* parentItem = Helper::findItem(root.data(), parent);
                if (!parentItem)
                {
                    change.errorText = QStringLiteral("parent: the item %1 does not exist; call document.get_outline for the items.").arg(PDFAppMcpOutlineTool::formatItemId(parent));
                    change.isTargetMissing = true;
                    return change;
                }

                const size_t index = qMin(request.index.value_or(parentItem->getChildCount()), parentItem->getChildCount());
                const PDFDestination destination = Helper::createDestination(document.data(), request.pageIndex, request.zoom);
                if (!Helper::chain(PDFAppAnnotationOperations::insertOutlineItem(document, parent, index, request.title, destination), &document, &flags))
                {
                    change.errorText = QStringLiteral("The outline item could not be added.");
                    return change;
                }
                resultPath = parent;
                resultPath.push_back(index);
                detail = Helper::tr("Add %1").arg(Helper::quote(request.title));
                count = 1;
                break;
            }

            case PDFAppMcpOutlineRequest::Action::Update:
            {
                const PDFAppAnnotationOperations::OutlinePath item = request.item.value_or(PDFAppAnnotationOperations::OutlinePath());
                const PDFOutlineItem* outlineItem = item.empty() ? nullptr : Helper::findItem(root.data(), item);
                if (!outlineItem)
                {
                    change.errorText = QStringLiteral("item: the item %1 does not exist; call document.get_outline for the items.").arg(PDFAppMcpOutlineTool::formatItemId(item));
                    change.isTargetMissing = true;
                    return change;
                }

                const QString oldTitle = outlineItem->getTitle();
                const bool isBold = outlineItem->isFontBold();
                const bool isItalic = outlineItem->isFontItalics();
                QStringList parts;
                if (request.newTitle)
                {
                    if (!Helper::chain(PDFAppAnnotationOperations::renameOutlineItem(document, item, *request.newTitle), &document, &flags))
                    {
                        change.errorText = QStringLiteral("newTitle: the item could not be renamed.");
                        return change;
                    }
                    parts << Helper::tr("Rename %1 to %2").arg(Helper::quote(oldTitle), Helper::quote(*request.newTitle));
                }
                if (request.pageIndex >= 0)
                {
                    const PDFDestination destination = Helper::createDestination(document.data(), request.pageIndex, QStringLiteral("keep"));
                    if (!Helper::chain(PDFAppAnnotationOperations::setOutlineItemDestination(document, item, destination), &document, &flags))
                    {
                        change.errorText = QStringLiteral("page: the target of the item could not be changed.");
                        return change;
                    }
                    parts << Helper::tr("Target page %1").arg(request.pageIndex + 1);
                }
                if (request.bold || request.italic)
                {
                    if (!Helper::chain(PDFAppAnnotationOperations::setOutlineItemStyle(document, item, request.bold.value_or(isBold), request.italic.value_or(isItalic)), &document, &flags))
                    {
                        change.errorText = QStringLiteral("The style of the item could not be changed.");
                        return change;
                    }
                    parts << Helper::tr("Style of %1").arg(Helper::quote(request.newTitle.value_or(oldTitle)));
                }
                if (parts.isEmpty())
                {
                    change.errorText = QStringLiteral("Nothing to change: pass newTitle, page, bold or italic.");
                    return change;
                }
                resultPath = item;
                detail = parts.join(QStringLiteral(", "));
                count = 1;
                break;
            }

            case PDFAppMcpOutlineRequest::Action::Move:
            {
                const PDFAppAnnotationOperations::OutlinePath item = request.item.value_or(PDFAppAnnotationOperations::OutlinePath());
                const PDFAppAnnotationOperations::OutlinePath parent = request.parent.value_or(PDFAppAnnotationOperations::OutlinePath());
                const PDFOutlineItem* outlineItem = item.empty() ? nullptr : Helper::findItem(root.data(), item);
                const PDFOutlineItem* parentItem = Helper::findItem(root.data(), parent);
                if (!outlineItem || !parentItem)
                {
                    change.errorText = QStringLiteral("%1: the item %2 does not exist; call document.get_outline for the items.")
                                           .arg(outlineItem ? QStringLiteral("parent") : QStringLiteral("item"), PDFAppMcpOutlineTool::formatItemId(outlineItem ? parent : item));
                    change.isTargetMissing = true;
                    return change;
                }

                const size_t index = qMin(request.index.value_or(parentItem->getChildCount()), parentItem->getChildCount());
                if (!Helper::chain(PDFAppAnnotationOperations::moveOutlineItem(document, item, parent, index, &resultPath), &document, &flags))
                {
                    change.errorText = QStringLiteral("The item cannot be moved there (a parent inside of the moved item?).");
                    return change;
                }
                detail = Helper::tr("Move %1").arg(Helper::quote(outlineItem->getTitle()));
                count = 1;
                break;
            }

            case PDFAppMcpOutlineRequest::Action::Delete:
            {
                std::vector<PDFAppAnnotationOperations::OutlinePath> items = request.items;
                if (items.empty() && request.item)
                {
                    items.push_back(*request.item);
                }

                QStringList titles;
                for (size_t i = 0; i < items.size(); ++i)
                {
                    const PDFOutlineItem* outlineItem = Helper::findItem(root.data(), items[i]);
                    if (!outlineItem || items[i].empty())
                    {
                        change.errorText = QStringLiteral("items[%1]: the item %2 does not exist; call document.get_outline for the items.").arg(i).arg(PDFAppMcpOutlineTool::formatItemId(items[i]));
                        change.isTargetMissing = true;
                        return change;
                    }

                    // An item inside of another removed item goes with it
                    bool isNested = false;
                    for (const PDFAppAnnotationOperations::OutlinePath& other : items)
                    {
                        isNested = isNested || (other.size() < items[i].size() && std::equal(other.begin(), other.end(), items[i].begin()));
                    }
                    if (!isNested && std::count(items.begin(), items.begin() + i, items[i]) == 0)
                    {
                        count += 1 + int(outlineItem->getTotalCount());
                        if (titles.size() < 3)
                        {
                            titles << Helper::quote(outlineItem->getTitle());
                        }
                    }
                }

                if (!Helper::chain(PDFAppAnnotationOperations::removeOutlineItems(document, items), &document, &flags))
                {
                    change.errorText = QStringLiteral("The items could not be deleted.");
                    return change;
                }
                detail = titles.join(QStringLiteral(", "));
                change.output.emplace_back(QStringLiteral("deleted"), count);
                break;
            }

            case PDFAppMcpOutlineRequest::Action::Replace:
            {
                // The flat list with levels gives the tree (wave 0 contract)
                QSharedPointer<PDFOutlineItem> newRoot(new PDFOutlineItem());
                std::vector<PDFOutlineItem*> stack = { newRoot.data() };
                for (const PDFAppMcpOutlineRequest::Node& node : request.nodes)
                {
                    QSharedPointer<PDFOutlineItem> item(new PDFOutlineItem());
                    item->setTitle(PDFAppAnnotationOperations::sanitizeOutlineTitle(node.title));
                    if (node.pageIndex >= 0)
                    {
                        const PDFDestination destination = Helper::createDestination(document.data(), node.pageIndex, QStringLiteral("keep"));
                        item->setAction(PDFActionPtr(new PDFActionGoTo(destination, PDFDestination())));
                    }

                    stack.resize(size_t(node.level));
                    stack.back()->addChild(item);
                    stack.push_back(item.data());
                }

                if (!Helper::chain(PDFAppAnnotationOperations::setOutline(document, newRoot), &document, &flags))
                {
                    change.errorText = QStringLiteral("The outline could not be replaced.");
                    return change;
                }
                count = int(request.nodes.size());
                detail = (count == 1 ? Helper::tr("Replace the outline with %n item", count) : Helper::tr("Replace the outline with %n items", count));
                change.output.emplace_back(QStringLiteral("itemCount"), count);
                break;
            }
        }

        if (request.action == PDFAppMcpOutlineRequest::Action::Add || request.action == PDFAppMcpOutlineRequest::Action::Update ||
            request.action == PDFAppMcpOutlineRequest::Action::Move)
        {
            change.output.emplace_back(QStringLiteral("item"), PDFAppMcpOutlineTool::formatItemId(resultPath));
        }

        PDFAppMcpChangeRow row;
        row.iconName = QStringLiteral("list");
        switch (request.action)
        {
            case PDFAppMcpOutlineRequest::Action::Delete:
                row.text = (count == 1 ? Helper::tr("Delete %n outline item", count) : Helper::tr("Delete %n outline items", count));
                break;
            case PDFAppMcpOutlineRequest::Action::Replace:
                row.text = Helper::tr("Replace the outline");
                break;
            default:
                row.text = Helper::tr("Edit the outline");
                break;
        }
        row.detail = detail;
        change.rows.push_back(row);
        change.modified = PDFModifiedDocument(document, nullptr, flags | PDFModifiedDocument::Outline);
        change.changeCount = 1;
        change.panelId = QStringLiteral("outline");
        change.summary = row.text;
        change.undoDescription = Helper::tr("%1 edited the outline").arg(getContext().authorName);
        change.activityDetail.insert(QStringLiteral("count"), qMax(1, count));
        return change;
    }

    virtual std::shared_ptr<PDFAppMcpChangeJob> createRerun(PDFAppMcpChangeContext context) const override
    {
        std::shared_ptr<PDFAppMcpOutlineJob> job = std::make_shared<PDFAppMcpOutlineJob>(std::move(context), m_request);
        job->m_isOutlineChanged = m_isOutlineChanged || (isPositional() && job->m_outlineVersion != m_outlineVersion);
        return job;
    }

private:
    PDFAppMcpOutlineRequest m_request;
    quint64 m_outlineVersion = 0;
    bool m_isOutlineChanged = false;
};

void PDFAppMcpOutlineTool::handle(const PDFAppMcpToolInvocation& invocation)
{
    PDFAppMcpHost* host = invocation.host;
    const std::shared_ptr<PDFMcpCall>& call = invocation.call;
    const QString actionId = invocation.getActionId();

    PDFAppDocument* document = PDFAppMcpChangeJob::prepareDocument(invocation);
    if (!document)
    {
        return;
    }

    const PDFDocument* pdfDocument = document->getDocument().data();
    const PDFSecurityHandler* securityHandler = pdfDocument ? pdfDocument->getStorage().getSecurityHandler() : nullptr;
    if (securityHandler && !securityHandler->isAllowed(PDFSecurityHandler::Permission::Modify) && !securityHandler->isAllowed(PDFSecurityHandler::Permission::Assemble))
    {
        host->finishWithError(call, actionId, document, QStringLiteral("The document does not allow changes of its outline (its security settings)."), PDFMcpActivityStatus::Failed);
        return;
    }

    const QJsonObject& arguments = invocation.actionArguments;
    const PDFInteger pageCount = document->getPageCount();
    PDFAppMcpOutlineRequest request;
    QString error;


    const QString& action = invocation.actionName;
    if (action == QLatin1String("add") || action == QLatin1String("update") || action == QLatin1String("move"))
    {
        request.action = action == QLatin1String("add") ? PDFAppMcpOutlineRequest::Action::Add
                                                        : action == QLatin1String("update") ? PDFAppMcpOutlineRequest::Action::Update : PDFAppMcpOutlineRequest::Action::Move;

        for (const char* key : { "item", "parent" })
        {
            if (!arguments.contains(QLatin1String(key)))
            {
                continue;
            }
            const QString id = arguments.value(QLatin1String(key)).toString();
            const std::optional<PDFAppAnnotationOperations::OutlinePath> path = parseItemId(id);
            if (!path)
            {
                error = QStringLiteral("%1: \"%2\" is not an item id (a dotted path such as \"2.1.4\")").arg(QLatin1String(key), PDFAppMcpArguments::sanitizeDocumentText(id, 64));
                break;
            }
            (QLatin1String(key) == QLatin1String("item") ? request.item : request.parent) = *path;
        }

        if (arguments.contains(QStringLiteral("index")))
        {
            request.index = size_t(qMax(0, arguments.value(QStringLiteral("index")).toInt(0)));
        }
        if (action == QLatin1String("update") && !request.item)
        {
            request.itemTitle = PDFMcpJson::sanitizeString(arguments.value(QStringLiteral("title")).toString(), PDFMcpJson::SanitizeFlag::RemoveBidi, 1024);
            if (request.itemTitle.trimmed().isEmpty() && error.isEmpty())
            {
                error = QStringLiteral("update needs item (its id) or title");
            }
        }
        if (action == QLatin1String("add"))
        {
            request.title = PDFAppAnnotationOperations::sanitizeOutlineTitle(arguments.value(QStringLiteral("title")).toString());
            request.zoom = arguments.value(QStringLiteral("zoom")).toString(QStringLiteral("keep"));
            if (request.title.trimmed().isEmpty() && error.isEmpty())
            {
                error = QStringLiteral("title: the title of the new item is empty");
            }
        }
        if (arguments.contains(QStringLiteral("newTitle")))
        {
            request.newTitle = PDFAppAnnotationOperations::sanitizeOutlineTitle(arguments.value(QStringLiteral("newTitle")).toString());
            if (request.newTitle->trimmed().isEmpty() && error.isEmpty())
            {
                error = QStringLiteral("newTitle: the title is empty");
            }
        }
        if (arguments.contains(QStringLiteral("bold")))
        {
            request.bold = arguments.value(QStringLiteral("bold")).toBool();
        }
        if (arguments.contains(QStringLiteral("italic")))
        {
            request.italic = arguments.value(QStringLiteral("italic")).toBool();
        }

        // The target page: page, else pageLabel (the only page of the label)
        if (arguments.contains(QStringLiteral("page")))
        {
            const int page = arguments.value(QStringLiteral("page")).toInt(0);
            if ((page < 1 || page > pageCount) && error.isEmpty())
            {
                error = QStringLiteral("page: page %1 does not exist; the document has %2 pages").arg(page).arg(pageCount);
            }
            request.pageIndex = page - 1;
        }
        else if (arguments.contains(QStringLiteral("pageLabel")))
        {
            const QString label = arguments.value(QStringLiteral("pageLabel")).toString();
            const std::vector<PDFInteger> pages = PDFAppMcpArguments::findPagesByLabel(document->getDocument().data(), label);
            if (pages.size() != 1 && error.isEmpty())
            {
                QStringList numbers;
                for (PDFInteger pageIndex : pages)
                {
                    if (numbers.size() < 20)
                    {
                        numbers << QString::number(pageIndex + 1);
                    }
                }
                error = pages.empty() ? QStringLiteral("pageLabel: no page has the label \"%1\"").arg(PDFAppMcpArguments::sanitizeDocumentText(label, 64))
                                      : QStringLiteral("pageLabel: several pages have the label (%1); pass page").arg(numbers.join(QStringLiteral(", ")));
            }
            request.pageIndex = pages.size() == 1 ? pages.front() : -1;
        }
        else if (action == QLatin1String("add") && error.isEmpty())
        {
            error = QStringLiteral("add needs page or pageLabel (the target of the item)");
        }

        if (action == QLatin1String("move") && !request.item && error.isEmpty())
        {
            error = QStringLiteral("move needs item (its id)");
        }
    }
    else if (action == QLatin1String("delete"))
    {
        request.action = PDFAppMcpOutlineRequest::Action::Delete;
        const QJsonArray items = arguments.value(QStringLiteral("items")).toArray();
        for (qsizetype i = 0; i < items.size(); ++i)
        {
            const std::optional<PDFAppAnnotationOperations::OutlinePath> path = parseItemId(items.at(i).toString());
            if (!path)
            {
                error = QStringLiteral("items[%1]: \"%2\" is not an item id (a dotted path such as \"2.1.4\")").arg(i).arg(PDFAppMcpArguments::sanitizeDocumentText(items.at(i).toString(), 64));
                break;
            }
            request.items.push_back(*path);
        }
        if (request.items.empty() && error.isEmpty())
        {
            error = QStringLiteral("items: at least one item id is needed");
        }
    }
    else if (action == QLatin1String("replace"))
    {
        request.action = PDFAppMcpOutlineRequest::Action::Replace;
        const QJsonArray items = arguments.value(QStringLiteral("items")).toArray();
        int previousLevel = 0;
        for (qsizetype i = 0; i < items.size() && error.isEmpty(); ++i)
        {
            const QJsonObject object = items.at(i).toObject();
            PDFAppMcpOutlineRequest::Node node;
            node.title = PDFAppAnnotationOperations::sanitizeOutlineTitle(object.value(QStringLiteral("title")).toString());
            node.level = object.value(QStringLiteral("level")).toInt(0);
            if (node.title.trimmed().isEmpty())
            {
                error = QStringLiteral("items[%1].title: the title is empty").arg(i);
            }
            else if (node.level < 1 || node.level > PDFAppMcpToolSet::MAXIMUM_OUTLINE_DEPTH || node.level > previousLevel + 1)
            {
                error = QStringLiteral("items[%1].level: %2 is not valid; the first item has level 1 and each item at most one more than the previous one").arg(i).arg(node.level);
            }
            else if (object.contains(QStringLiteral("page")))
            {
                const int page = object.value(QStringLiteral("page")).toInt(0);
                if (page < 1 || page > pageCount)
                {
                    error = QStringLiteral("items[%1].page: page %2 does not exist; the document has %3 pages").arg(i).arg(page).arg(pageCount);
                }
                node.pageIndex = page - 1;
            }
            previousLevel = node.level;
            request.nodes.push_back(std::move(node));
        }
    }

    if (!error.isEmpty())
    {
        host->finishWithError(call, actionId, document, QStringLiteral("Invalid arguments: %1. See pdf4qt://docs/outline.md").arg(error), PDFMcpActivityStatus::Failed);
        return;
    }

    host->startJob(std::make_shared<PDFAppMcpOutlineJob>(PDFAppMcpChangeJob::createContext(invocation, document), std::move(request)));
}

}   // namespace pdf
