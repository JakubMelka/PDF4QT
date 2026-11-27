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

#include "pdfappeditcontentmenus.h"

#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentpanels_objects.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentselection.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfdocumenttoolgui.h"

#include "pdfcomponentrows.h"
#include "pdfthemeiconengine.h"

#include <QAction>
#include <QCoreApplication>
#include <QEvent>
#include <QTransform>

#include <algorithm>
#include <map>
#include <memory>
#include <set>

namespace pdf
{

/// What the actions of the selection act on (zadani_gui.md 4.18, 4.27): the selected objects of the edited container
/// inside of the edited root, without the nodes of a selected group (the group carries them), without unknown content
/// (never alone) and - for a change of the content - without locked objects
class PDFAppEditContentMenusHelper
{
public:
    PDFAppEditContentMenusHelper() = delete;

    /// Levels of the walks to the root (the builder goes to PDFContentContainer::MAXIMUM_DEPTH)
    static constexpr int MAXIMUM_DEPTH = PDFContentContainer::MAXIMUM_DEPTH + 4;

    /// The move of Duplicate in the default space of the container (to the right and down)
    static constexpr PDFReal DUPLICATE_OFFSET = 10.0;

    /// The selected objects in the order of the content; \p isLockedIncluded keeps locked objects (Lock, Hide)
    static std::vector<PDFContentNodeReference> getObjects(const PDFAppEditContentContext* context, bool isLockedIncluded)
    {
        std::vector<PDFContentNodeReference> result;
        const PDFAppEditContentSelection* selection = context ? context->getSelection() : nullptr;
        const PDFContentContainerPointer container = context ? context->getContainer() : PDFContentContainerPointer();
        const PDFContentEditSession* session = context ? context->getSession() : nullptr;
        if (!selection || !container || !session || selection->isEmpty())
        {
            return result;
        }

        const PDFContentNodeReference root = context->getEditedRoot();
        std::set<PDFContentNodeId> selected;
        for (const PDFContentNodeReference& node : selection->getNodes())
        {
            if (node.container == container->getKey())
            {
                selected.insert(node.node);
            }
        }

        std::vector<std::pair<std::vector<PDFInteger>, PDFContentNodeReference>> ordered;
        for (const PDFContentNodeId& id : selected)
        {
            const PDFContentNodePointer node = container->getNode(id);
            if (!node || id == root.node || !PDFAppEditContentObjectsModel::getRowNodeOfChild(*node) || node->getKind() == PDFContentNodeKind::Opaque)
            {
                continue;
            }
            const PDFContentNodeReference reference{ container->getKey(), id };
            if (!isLockedIncluded && session->isNodeLocked(reference))
            {
                continue;
            }

            // Inside of the edited root, and no selected ancestor; the position in the content on the way up
            std::vector<PDFInteger> position;
            bool isInsideRoot = false;
            bool hasSelectedAncestor = false;
            PDFContentNodeId current = id;
            for (int depth = 0; depth < MAXIMUM_DEPTH && current.isValid(); ++depth)
            {
                if (current == root.node)
                {
                    isInsideRoot = true;
                    break;
                }
                if (current != id && selected.count(current))
                {
                    hasSelectedAncestor = true;
                }
                position.push_back(container->getIndexInParent(current));
                const PDFContentNodePointer parent = container->getParent(current);
                current = parent ? parent->getId() : PDFContentNodeId();
            }
            if (isInsideRoot && !hasSelectedAncestor)
            {
                std::reverse(position.begin(), position.end());
                ordered.emplace_back(std::move(position), reference);
            }
        }

        std::sort(ordered.begin(), ordered.end());
        for (auto& item : ordered)
        {
            result.push_back(item.second);
        }
        return result;
    }

    /// The selected objects grouped by the parent of their places in the tree (getPlaceId): the places in the order
    /// of the content
    static std::map<PDFContentNodeId, std::vector<PDFContentNodeId>> getPlacesByParent(const PDFAppEditContentContext* context)
    {
        std::map<PDFContentNodeId, std::vector<PDFContentNodeId>> result;
        const PDFContentContainerPointer container = context ? context->getContainer() : PDFContentContainerPointer();
        if (!container)
        {
            return result;
        }
        for (const PDFContentNodeReference& node : getObjects(context, false))
        {
            const PDFContentNodeId place = PDFAppEditContentObjectsModel::getPlaceId(*container, node.node);
            const PDFContentNodePointer parent = container->getParent(place);
            if (parent)
            {
                std::vector<PDFContentNodeId>& places = result[parent->getId()];
                if (std::find(places.cbegin(), places.cend(), place) == places.cend())
                {
                    places.push_back(place);
                }
            }
        }
        return result;
    }

    enum class Order
    {
        BringToFront,
        BringForward,
        SendBackward,
        SendToBack
    };

    /// One Reorder of the places of one parent
    struct OrderStep
    {
        PDFContentNodeId parent;
        std::vector<PDFContentNodeId> nodes;
        PDFInteger index = -1;
    };

    /// The child is an object, which an order step passes: not moved, not a state run
    static bool isOrderObject(const std::vector<PDFContentNodePointer>& children, const std::set<PDFContentNodeId>& moved, PDFInteger index)
    {
        const PDFContentNode& child = *children[size_t(index)];
        return !moved.count(child.getId()) && child.getKind() != PDFContentNodeKind::StateRun;
    }

    /// The Reorder commands of an order action, one per parent, which changes something. The index is the index of the
    /// child of the parent before the command, in front of which the nodes go (a moved child there is skipped:
    /// PDFContentPlaceCommand::getAnchor); a state run is no object to step over, the content, which must stay last
    /// (PDFContentContainer::getAppendIndex), neither.
    static std::vector<OrderStep> getOrderSteps(const PDFAppEditContentContext* context, Order order)
    {
        std::vector<OrderStep> steps;
        const PDFContentContainerPointer container = context ? context->getContainer() : PDFContentContainerPointer();
        if (!container)
        {
            return steps;
        }

        for (const auto& [parentId, places] : getPlacesByParent(context))
        {
            const PDFContentNodePointer parent = container->getNode(parentId);
            if (!parent)
            {
                continue;
            }
            const std::vector<PDFContentNodePointer>& children = parent->getChildren();
            const std::set<PDFContentNodeId> moved(places.cbegin(), places.cend());
            const PDFInteger childCount = PDFInteger(children.size());
            const PDFInteger appendIndex = qBound<PDFInteger>(0, container->getAppendIndex(parentId), childCount);
            PDFInteger first = childCount;
            PDFInteger last = -1;
            for (PDFInteger i = 0; i < childCount; ++i)
            {
                if (moved.count(children[size_t(i)]->getId()))
                {
                    first = qMin(first, i);
                    last = qMax(last, i);
                }
            }
            if (last < 0)
            {
                continue;
            }

            OrderStep step;
            step.parent = parentId;
            step.nodes = places;
            bool isChanged = false;
            switch (order)
            {
                case Order::BringToFront:
                {
                    // An object of another node between the first moved one and the end
                    for (PDFInteger i = first; i < appendIndex && !isChanged; ++i)
                    {
                        isChanged = isOrderObject(children, moved, i);
                    }
                    step.index = -1;
                    break;
                }
                case Order::SendToBack:
                {
                    for (PDFInteger i = 0; i <= last && !isChanged; ++i)
                    {
                        isChanged = isOrderObject(children, moved, i);
                    }
                    step.index = 0;
                    break;
                }
                case Order::BringForward:
                {
                    // Behind the next object after the last moved one
                    for (PDFInteger i = last + 1; i < appendIndex; ++i)
                    {
                        if (isOrderObject(children, moved, i))
                        {
                            step.index = i + 1;
                            isChanged = true;
                            break;
                        }
                    }
                    break;
                }
                case Order::SendBackward:
                {
                    // In front of the object before the first moved one
                    for (PDFInteger i = first - 1; i >= 0; --i)
                    {
                        if (isOrderObject(children, moved, i))
                        {
                            step.index = i;
                            isChanged = true;
                            break;
                        }
                    }
                    break;
                }
            }
            if (isChanged)
            {
                steps.push_back(std::move(step));
            }
        }
        return steps;
    }

    /// The places of the objects to group: two or more objects of one parent
    static std::vector<PDFContentNodeReference> getGroupNodes(const PDFAppEditContentContext* context)
    {
        std::vector<PDFContentNodeReference> result;
        const std::map<PDFContentNodeId, std::vector<PDFContentNodeId>> places = getPlacesByParent(context);
        if (places.size() == 1 && places.begin()->second.size() >= 2)
        {
            for (const PDFContentNodeId& place : places.begin()->second)
            {
                result.push_back(PDFContentNodeReference{ context->getContainerKey(), place });
            }
        }
        return result;
    }

    /// The selected groups (q ... Q) to ungroup
    static std::vector<PDFContentNodeReference> getGroups(const PDFAppEditContentContext* context)
    {
        std::vector<PDFContentNodeReference> result;
        const PDFContentContainerPointer container = context ? context->getContainer() : PDFContentContainerPointer();
        for (const PDFContentNodeReference& node : getObjects(context, false))
        {
            const PDFContentNodePointer contentNode = container->getNode(node.node);
            if (contentNode && contentNode->getKind() == PDFContentNodeKind::Group)
            {
                result.push_back(node);
            }
        }
        return result;
    }

    /// The commands as one undo step: one command alone, more of them in a macro
    static void execute(PDFAppEditContentContext* context, const std::vector<PDFContentCommandPointer>& commands, const QString& text)
    {
        if (commands.empty())
        {
            return;
        }
        if (commands.size() == 1)
        {
            context->execute(commands.front());
            return;
        }

        // The context guards itself: a slot of a command may close the tool (QPointer)
        const QPointer<PDFAppEditContentContext> guard(context);
        context->beginMacro(text);
        for (const PDFContentCommandPointer& command : commands)
        {
            if (!guard || !guard->execute(command))
            {
                break;
            }
        }
        if (guard)
        {
            guard->endMacro();
        }
    }

    static PDFAppEditContentActionEntry::Key key(QKeyCombination combination)
    {
        PDFAppEditContentActionEntry::Key result;
        result.sequence = QKeySequence(combination);
        result.scope = PDFAppEditContentKeyScope::Tool;
        return result;
    }

    static PDFAppEditContentActionEntry::Placement placement(const char* menuId, int group, int order)
    {
        PDFAppEditContentActionEntry::Placement result;
        result.menuId = QString::fromLatin1(menuId);
        result.group = group;
        result.order = order;
        return result;
    }
};

/// The base of the entries of this file: a description of the row of a panel (the menu View, -menus-view)
class PDFAppEditContentMenuEntry : public PDFAppEditContentActionEntry
{
public:
    virtual QString getDescription() const { return QString(); }
};

/// Delete: the one owner of "delete the selection" (pe1_skeleton_notes.md 11, R3-7); the key Delete of the tool
/// Select and of the tree, the row of the context menu and the command edit.delete of the application
class PDFAppEditContentDeleteEntry : public PDFAppEditContentMenuEntry
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentDeleteEntry)

public:
    virtual QString getId() const override { return QString::fromLatin1(PDFAppEditContentActionIds::DELETE_SELECTION); }
    virtual QString getTitle(const PDFAppEditContentContext* context) const override { Q_UNUSED(context); return tr("Delete"); }
    virtual QString getIconName() const override { return QStringLiteral("trash"); }
    virtual QStringList getApplicationCommandIds() const override { return QStringList{ PDFAppCommands::deleteSelection() }; }

    virtual std::vector<Placement> getPlacements() const override
    {
        // edit-content-v2-objects-context-menu: Cut, Copy, Paste, Paste in Place (P23), Delete
        return { PDFAppEditContentMenusHelper::placement(PDFAppEditContentMenuIds::CONTEXT, PDFAppEditContentMenuIds::GROUP_CLIPBOARD, 500) };
    }

    virtual bool isEnabled(const PDFAppEditContentContext* context) const override
    {
        return context && context->isEditingAllowed() && !PDFAppEditContentMenusHelper::getObjects(context, false).empty();
    }

    virtual void trigger(PDFAppEditContentContext* context) const override
    {
        std::vector<PDFContentNodeReference> nodes = PDFAppEditContentMenusHelper::getObjects(context, false);
        if (!nodes.empty())
        {
            context->execute(PDFContentCommandFactory::createDelete(std::move(nodes)));
        }
    }
};

/// Select All (edit.selectAll): every object of the edited root, which can be selected - not unknown content, not a
/// locked object, not a hidden one
class PDFAppEditContentSelectAllEntry : public PDFAppEditContentMenuEntry
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentSelectAllEntry)

public:
    virtual QString getId() const override { return QString::fromLatin1(PDFAppEditContentActionIds::SELECT_ALL); }
    virtual QString getTitle(const PDFAppEditContentContext* context) const override { Q_UNUSED(context); return tr("Select All"); }
    virtual QString getIconName() const override { return QStringLiteral("select-all"); }
    virtual QStringList getApplicationCommandIds() const override { return QStringList{ PDFAppCommands::selectAll() }; }

    virtual bool isEnabled(const PDFAppEditContentContext* context) const override
    {
        // Cheap (asked at every refresh of the window): a root with children
        const PDFContentContainerPointer container = context ? context->getContainer() : PDFContentContainerPointer();
        const PDFContentNodePointer root = container ? container->getNode(context->getEditedRoot().node) : PDFContentNodePointer();
        return context && context->isEditingAllowed() && root && !root->getChildren().empty();
    }

    virtual void trigger(PDFAppEditContentContext* context) const override
    {
        const PDFContentContainerPointer container = context->getContainer();
        const PDFContentNodePointer root = container ? container->getNode(context->getEditedRoot().node) : PDFContentNodePointer();
        const PDFContentEditSession* session = context->getSession();
        if (!root || !session || !context->getSelection())
        {
            return;
        }

        std::vector<PDFContentNodeReference> nodes;
        for (const PDFContentNodePointer& child : root->getChildren())
        {
            const PDFContentNode* rowNode = PDFAppEditContentObjectsModel::getRowNodeOfChild(*child);
            if (!rowNode || rowNode->getKind() == PDFContentNodeKind::Opaque)
            {
                continue;
            }
            const PDFContentNodeReference reference{ container->getKey(), rowNode->getId() };
            if (!session->isNodeLocked(reference) && !session->isNodeHidden(reference))
            {
                nodes.push_back(reference);
            }
        }
        context->getSelection()->setNodes(nodes);
    }
};

/// Bring to Front, Bring Forward, Send Backward, Send to Back (Reorder, the layer and the tag stay)
class PDFAppEditContentOrderEntry : public PDFAppEditContentMenuEntry
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentOrderEntry)

public:
    using Order = PDFAppEditContentMenusHelper::Order;

    explicit PDFAppEditContentOrderEntry(Order order) : m_order(order) { }

    virtual QString getId() const override
    {
        switch (m_order)
        {
            case Order::BringToFront:
                return QString::fromLatin1(PDFAppEditContentActionIds::ORDER_BRING_TO_FRONT);
            case Order::BringForward:
                return QString::fromLatin1(PDFAppEditContentActionIds::ORDER_BRING_FORWARD);
            case Order::SendBackward:
                return QString::fromLatin1(PDFAppEditContentActionIds::ORDER_SEND_BACKWARD);
            case Order::SendToBack:
                return QString::fromLatin1(PDFAppEditContentActionIds::ORDER_SEND_TO_BACK);
        }
        return QString();
    }

    virtual QString getTitle(const PDFAppEditContentContext* context) const override
    {
        Q_UNUSED(context);
        switch (m_order)
        {
            case Order::BringToFront:
                return tr("Bring to Front");
            case Order::BringForward:
                return tr("Bring Forward");
            case Order::SendBackward:
                return tr("Send Backward");
            case Order::SendToBack:
                return tr("Send to Back");
        }
        return QString();
    }

    virtual QString getIconName() const override
    {
        switch (m_order)
        {
            case Order::BringToFront:
                return QStringLiteral("bring-front");
            case Order::BringForward:
                return QStringLiteral("bring-forward");
            case Order::SendBackward:
                return QStringLiteral("send-backward");
            case Order::SendToBack:
                return QStringLiteral("send-back");
        }
        return QString();
    }

    virtual std::vector<Key> getKeys() const override
    {
        // zadani_gui.md 4.20: Ctrl+Shift+], Ctrl+], Ctrl+[, Ctrl+Shift+[
        using H = PDFAppEditContentMenusHelper;
        switch (m_order)
        {
            case Order::BringToFront:
                return { H::key(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight) };
            case Order::BringForward:
                return { H::key(Qt::CTRL | Qt::Key_BracketRight) };
            case Order::SendBackward:
                return { H::key(Qt::CTRL | Qt::Key_BracketLeft) };
            case Order::SendToBack:
                return { H::key(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft) };
        }
        return { };
    }

    virtual std::vector<Placement> getPlacements() const override
    {
        // edit-content-v2-objects-context-menu has Bring to Front and Send to Back; the steps by one are keys and
        // buttons of the card Arrange
        using H = PDFAppEditContentMenusHelper;
        switch (m_order)
        {
            case Order::BringToFront:
                return { H::placement(PDFAppEditContentMenuIds::CONTEXT, PDFAppEditContentMenuIds::GROUP_ORDER, 100) };
            case Order::SendToBack:
                return { H::placement(PDFAppEditContentMenuIds::CONTEXT, PDFAppEditContentMenuIds::GROUP_ORDER, 400) };
            default:
                return { };
        }
    }

    virtual bool isEnabled(const PDFAppEditContentContext* context) const override
    {
        return context && context->isEditingAllowed() && !PDFAppEditContentMenusHelper::getOrderSteps(context, m_order).empty();
    }

    virtual void trigger(PDFAppEditContentContext* context) const override
    {
        std::vector<PDFContentCommandPointer> commands;
        for (const PDFAppEditContentMenusHelper::OrderStep& step : PDFAppEditContentMenusHelper::getOrderSteps(context, m_order))
        {
            std::vector<PDFContentNodeReference> nodes;
            for (const PDFContentNodeId& node : step.nodes)
            {
                nodes.push_back(PDFContentNodeReference{ context->getContainerKey(), node });
            }
            commands.push_back(PDFContentCommandFactory::createReorder(std::move(nodes), PDFContentNodeReference{ context->getContainerKey(), step.parent },
                                                                       step.index, PDFContentCommandFactory::ReorderMode::KeepLayerAndTag));
        }
        PDFAppEditContentMenusHelper::execute(context, commands, getTitle(context));
    }

private:
    Order m_order;
};

/// Group (q ... Q around two or more objects of one parent)
class PDFAppEditContentGroupEntry : public PDFAppEditContentMenuEntry
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentGroupEntry)

public:
    virtual QString getId() const override { return QString::fromLatin1(PDFAppEditContentActionIds::GROUP); }
    virtual QString getTitle(const PDFAppEditContentContext* context) const override { Q_UNUSED(context); return tr("Group"); }
    virtual QString getIconName() const override { return QStringLiteral("group"); }
    virtual std::vector<Key> getKeys() const override { return { PDFAppEditContentMenusHelper::key(Qt::CTRL | Qt::ALT | Qt::Key_G) }; }

    virtual std::vector<Placement> getPlacements() const override
    {
        return { PDFAppEditContentMenusHelper::placement(PDFAppEditContentMenuIds::CONTEXT, PDFAppEditContentMenuIds::GROUP_GROUPING, 100) };
    }

    virtual bool isEnabled(const PDFAppEditContentContext* context) const override
    {
        return context && context->isEditingAllowed() && !PDFAppEditContentMenusHelper::getGroupNodes(context).empty();
    }

    virtual void trigger(PDFAppEditContentContext* context) const override
    {
        std::vector<PDFContentNodeReference> nodes = PDFAppEditContentMenusHelper::getGroupNodes(context);
        if (!nodes.empty())
        {
            context->execute(PDFContentCommandFactory::createGroup(std::move(nodes)));
        }
    }
};

/// Ungroup (the row is there, when a group is selected)
class PDFAppEditContentUngroupEntry : public PDFAppEditContentMenuEntry
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentUngroupEntry)

public:
    virtual QString getId() const override { return QString::fromLatin1(PDFAppEditContentActionIds::UNGROUP); }
    virtual QString getTitle(const PDFAppEditContentContext* context) const override { Q_UNUSED(context); return tr("Ungroup"); }
    virtual QString getIconName() const override { return QStringLiteral("ungroup"); }
    virtual std::vector<Key> getKeys() const override { return { PDFAppEditContentMenusHelper::key(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_G) }; }

    virtual std::vector<Placement> getPlacements() const override
    {
        return { PDFAppEditContentMenusHelper::placement(PDFAppEditContentMenuIds::CONTEXT, PDFAppEditContentMenuIds::GROUP_GROUPING, 200) };
    }

    virtual bool isVisible(const PDFAppEditContentContext* context) const override
    {
        return !PDFAppEditContentMenusHelper::getGroups(context).empty();
    }

    virtual void trigger(PDFAppEditContentContext* context) const override
    {
        std::vector<PDFContentCommandPointer> commands;
        for (const PDFContentNodeReference& group : PDFAppEditContentMenusHelper::getGroups(context))
        {
            commands.push_back(PDFContentCommandFactory::createUngroup(group));
        }
        PDFAppEditContentMenusHelper::execute(context, commands, getTitle(context));
    }
};

/// Hide / Show and Lock / Unlock: the state of the editor, no command and no undo step (zadani_gui.md 4.18, 4.27)
class PDFAppEditContentStateEntry : public PDFAppEditContentMenuEntry
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentStateEntry)

public:
    explicit PDFAppEditContentStateEntry(bool isLock) : m_isLock(isLock) { }

    virtual QString getId() const override { return QString::fromLatin1(m_isLock ? PDFAppEditContentActionIds::LOCK : PDFAppEditContentActionIds::HIDE); }

    virtual QString getTitle(const PDFAppEditContentContext* context) const override
    {
        const bool isOn = isAllOn(context);
        if (m_isLock)
        {
            return isOn ? tr("Unlock") : tr("Lock");
        }
        return isOn ? tr("Show") : tr("Hide");
    }

    virtual QString getIconName() const override { return m_isLock ? QStringLiteral("lock") : QStringLiteral("eye-off"); }

    virtual std::vector<Key> getKeys() const override
    {
        // KE.23: Ctrl+Alt+L, Ctrl+Alt+H
        return { PDFAppEditContentMenusHelper::key(Qt::CTRL | Qt::ALT | (m_isLock ? Qt::Key_L : Qt::Key_H)) };
    }

    virtual std::vector<Placement> getPlacements() const override
    {
        return { PDFAppEditContentMenusHelper::placement(PDFAppEditContentMenuIds::CONTEXT, PDFAppEditContentMenuIds::GROUP_STATE, m_isLock ? 200 : 100) };
    }

    virtual bool isEnabled(const PDFAppEditContentContext* context) const override
    {
        return context && context->isEditingAllowed() && !PDFAppEditContentMenusHelper::getObjects(context, true).empty();
    }

    virtual void trigger(PDFAppEditContentContext* context) const override
    {
        PDFContentEditSession* session = context->getSession();
        if (!session)
        {
            return;
        }

        // Every selected object gets the state, unless all have it already (then all lose it); the session tells each
        // change by editorStateChanged - its slots may close the tool
        const bool isOn = !isAllOn(context);
        const QPointer<PDFContentEditSession> guard(session);
        for (const PDFContentNodeReference& node : PDFAppEditContentMenusHelper::getObjects(context, true))
        {
            if (!guard)
            {
                break;
            }
            if (m_isLock)
            {
                guard->setNodeLocked(node, isOn);
            }
            else
            {
                guard->setNodeHidden(node, isOn);
            }
        }
    }

private:
    bool isAllOn(const PDFAppEditContentContext* context) const
    {
        const PDFContentEditSession* session = context ? context->getSession() : nullptr;
        const std::vector<PDFContentNodeReference> nodes = PDFAppEditContentMenusHelper::getObjects(context, true);
        if (!session || nodes.empty())
        {
            return false;
        }
        for (const PDFContentNodeReference& node : nodes)
        {
            if (!(m_isLock ? session->isNodeLocked(node) : session->isNodeHidden(node)))
            {
                return false;
            }
        }
        return true;
    }

    bool m_isLock;
};

/// Duplicate (Ctrl+Shift+D; Ctrl+D is Document properties of the application): copies of the selected objects right
/// after the last of them, moved by DUPLICATE_OFFSET to the right and down; the session asks the views to select the
/// copies
class PDFAppEditContentDuplicateEntry : public PDFAppEditContentMenuEntry
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentDuplicateEntry)

public:
    virtual QString getId() const override { return QString::fromLatin1(PDFAppEditContentActionIds::DUPLICATE); }
    virtual QString getTitle(const PDFAppEditContentContext* context) const override { Q_UNUSED(context); return tr("Duplicate"); }
    virtual QString getIconName() const override { return QStringLiteral("copy"); }
    virtual std::vector<Key> getKeys() const override { return { PDFAppEditContentMenusHelper::key(Qt::CTRL | Qt::SHIFT | Qt::Key_D) }; }

    virtual bool isEnabled(const PDFAppEditContentContext* context) const override
    {
        return context && context->isEditingAllowed() && !PDFAppEditContentMenusHelper::getObjects(context, false).empty();
    }

    virtual void trigger(PDFAppEditContentContext* context) const override
    {
        const PDFContentContainerPointer container = context->getContainer();
        std::vector<PDFContentNodeReference> nodes = PDFAppEditContentMenusHelper::getObjects(context, false);
        if (!container || nodes.empty())
        {
            return;
        }

        // The parent of the places of all objects, when they have one, else the edited root; the copies right behind
        // the last place there
        PDFContentNodeId parentId;
        PDFInteger index = -1;
        bool isOneParent = true;
        for (const PDFContentNodeReference& node : nodes)
        {
            const PDFContentNodeId place = PDFAppEditContentObjectsModel::getPlaceId(*container, node.node);
            const PDFContentNodePointer parent = container->getParent(place);
            if (!parent || (parentId.isValid() && parent->getId() != parentId))
            {
                isOneParent = false;
                break;
            }
            parentId = parent->getId();
            index = qMax(index, container->getIndexInParent(place) + 1);
        }
        if (!isOneParent || !parentId.isValid())
        {
            parentId = context->getEditedRoot().node;
            index = -1;
        }

        const PDFReal offset = PDFAppEditContentMenusHelper::DUPLICATE_OFFSET;
        context->execute(PDFContentCommandFactory::createCopy(std::move(nodes), PDFContentNodeReference{ container->getKey(), parentId }, index,
                                                              QTransform::fromTranslate(offset, -offset)));
    }
};

/// A check row of the menu View (edit-content-v2-menus-view): an option of the display (zadani_gui.md 4.26), no work
class PDFAppEditContentViewEntry : public PDFAppEditContentMenuEntry
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentViewEntry)

public:
    enum class View
    {
        ClippingPaths,
        InvisibleText,
        Annotations
    };

    explicit PDFAppEditContentViewEntry(View view) : m_view(view) { }

    virtual QString getId() const override
    {
        switch (m_view)
        {
            case View::ClippingPaths:
                return QString::fromLatin1(PDFAppEditContentActionIds::VIEW_CLIPPING_PATHS);
            case View::InvisibleText:
                return QString::fromLatin1(PDFAppEditContentActionIds::VIEW_INVISIBLE_TEXT);
            case View::Annotations:
                return QString::fromLatin1(PDFAppEditContentActionIds::VIEW_ANNOTATIONS);
        }
        return QString();
    }

    virtual QString getTitle(const PDFAppEditContentContext* context) const override
    {
        Q_UNUSED(context);
        switch (m_view)
        {
            case View::ClippingPaths:
                return tr("Clipping paths");
            case View::InvisibleText:
                return tr("Invisible text");
            case View::Annotations:
                return tr("Annotations and fields");
        }
        return QString();
    }

    virtual QString getDescription() const override
    {
        switch (m_view)
        {
            case View::ClippingPaths:
                return tr("Blue dashed outlines");
            case View::InvisibleText:
                return tr("Text with rendering mode Invisible");
            case View::Annotations:
                return tr("Dimmed underlay, cannot be edited here");
        }
        return QString();
    }

    virtual QString getIconName() const override
    {
        switch (m_view)
        {
            case View::ClippingPaths:
                return QStringLiteral("clip-path");
            case View::InvisibleText:
                return QStringLiteral("text-hidden");
            case View::Annotations:
                return QStringLiteral("annotations-underlay");
        }
        return QString();
    }

    virtual std::vector<Placement> getPlacements() const override
    {
        // The rows of SHOW in the order of the frame: Rulers 100, Grid 200, Guides 300 (P32), Clipping paths 400,
        // Invisible text 500, Annotations and fields 600, Reading order 700 (P52)
        const int order = m_view == View::ClippingPaths ? 400 : (m_view == View::InvisibleText ? 500 : 600);
        return { PDFAppEditContentMenusHelper::placement(PDFAppEditContentMenuIds::VIEW, PDFAppEditContentMenuIds::GROUP_SHOW, order) };
    }

    /// A display option: also in Preview and during a run (it changes no content)
    virtual bool isEnabled(const PDFAppEditContentContext* context) const override { return context && context->getOptions(); }
    virtual bool isCheckable() const override { return true; }

    virtual bool isChecked(const PDFAppEditContentContext* context) const override
    {
        return context && context->getOptions() && context->getOptions()->getBool(getOptionName());
    }

    virtual void trigger(PDFAppEditContentContext* context) const override
    {
        if (PDFAppEditContentOptions* options = context->getOptions())
        {
            const QString name = getOptionName();
            options->setValue(name, !options->getBool(name));
        }
    }

private:
    QString getOptionName() const
    {
        switch (m_view)
        {
            case View::ClippingPaths:
                return QString::fromLatin1(PDFAppEditContentOptions::SHOW_CLIPPING_PATHS);
            case View::InvisibleText:
                return QString::fromLatin1(PDFAppEditContentOptions::SHOW_INVISIBLE_TEXT);
            case View::Annotations:
                return QString::fromLatin1(PDFAppEditContentOptions::SHOW_ANNOTATIONS);
        }
        return QString();
    }

    View m_view;
};

/// The rows of a panel of the toolbar filled by fillPanel: a child of the panel (it dies with it), it keeps the rows it
/// made (a refill retires them) and follows the state of the actions while the panel is open
class PDFAppEditContentMenusPanelBinding : public QObject
{
public:
    PDFAppEditContentMenusPanelBinding(PDFAppEditContentContext* context, PDFAppToolPanelPopup* panel, const QString& menuId) :
        QObject(panel),
        m_context(context),
        m_panel(panel),
        m_menuId(menuId)
    {
        setObjectName(QStringLiteral("editContentMenusBinding"));
        if (context)
        {
            PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::editingAllowedChanged, this, &PDFAppEditContentMenusPanelBinding::updateRows);
            if (PDFAppEditContentOptions* options = context->getOptions())
            {
                PDFDeferredDeletion::connect(options, &PDFAppEditContentOptions::changed, this, &PDFAppEditContentMenusPanelBinding::onOptionChanged);
            }
        }
    }

    const QString& getMenuId() const { return m_menuId; }

    /// The rows of the menu of the registry
    void fill()
    {
        for (const QPointer<QWidget>& widget : m_widgets)
        {
            if (widget)
            {
                PDFDeferredDeletion::retire(widget.data());
            }
        }
        m_widgets.clear();
        m_rows.clear();

        const PDFAppEditContentRegistry* registry = m_context ? m_context->getRegistry() : nullptr;
        if (!registry || !m_panel)
        {
            return;
        }

        int lastGroup = 0;
        bool isFirst = true;
        for (const PDFAppEditContentActionEntry* entry : registry->getMenuActions(m_menuId))
        {
            // getPlacements returns a value: the placement points into this local vector
            const std::vector<PDFAppEditContentActionEntry::Placement> placements = entry->getPlacements();
            const PDFAppEditContentActionEntry::Placement* placement = PDFAppEditContentRegistry::findPlacement(placements, m_menuId);
            if (!placement || !entry->isVisible(m_context) || !entry->getChoices(m_context).empty())
            {
                // A submenu has no row in a panel (no entry of stage E1 has one)
                continue;
            }

            if (isFirst || placement->group != lastGroup)
            {
                if (!isFirst)
                {
                    m_widgets.push_back(m_panel->addGroupDivider());
                }
                const QString caption = getCaption(placement->group);
                if (!caption.isEmpty())
                {
                    m_widgets.push_back(m_panel->addGroupCaption(caption));
                }
                lastGroup = placement->group;
                isFirst = false;
            }

            PDFTileRow* row = m_panel->addRow(entry->getTitle(m_context), PDFAppEditContentMenus::getDescription(entry), entry->getIconName(), PDFTileRow::Kind::MenuItem);
            row->setObjectName(QStringLiteral("editContentMenuRow-") + entry->getId());
            row->setProperty(PDFAppEditContentMenus::ACTION_PROPERTY, entry->getId());
            const QKeySequence key = PDFAppEditContentMenus::getShownKey(entry, m_context);
            row->setShortcutText(key.toString(QKeySequence::NativeText));
            if (entry->isCheckable())
            {
                row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Toggle);
            }
            PDFDeferredDeletion::connect(row, &PDFTileRow::clicked, this, &PDFAppEditContentMenusPanelBinding::onRowClicked);
            m_widgets.push_back(row);
            m_rows.push_back(row);
        }
        updateRows();
    }

    /// The checks, the titles and the enabled state of the rows follow the actions
    void updateRows()
    {
        const PDFAppEditContentRegistry* registry = m_context ? m_context->getRegistry() : nullptr;
        if (!registry)
        {
            return;
        }
        for (const QPointer<PDFTileRow>& row : m_rows)
        {
            const PDFAppEditContentActionEntry* entry = row ? registry->findAction(row->property(PDFAppEditContentMenus::ACTION_PROPERTY).toString()) : nullptr;
            if (!entry)
            {
                continue;
            }
            row->setTitle(entry->getTitle(m_context));
            row->setEnabled(entry->isEnabled(m_context));
            if (entry->isCheckable())
            {
                row->setChecked(entry->isChecked(m_context));
            }
        }
    }

private:
    QString getCaption(int group) const
    {
        // -menus-view: SHOW, SNAP TO; the settings below a divider without a caption
        if (m_menuId == QLatin1String(PDFAppEditContentMenuIds::VIEW))
        {
            if (group == PDFAppEditContentMenuIds::GROUP_SHOW)
            {
                return PDFAppEditContentMenus::tr("SHOW");
            }
            if (group == PDFAppEditContentMenuIds::GROUP_SNAP)
            {
                return PDFAppEditContentMenus::tr("SNAP TO");
            }
        }
        return QString();
    }

    void onOptionChanged(const QString& name)
    {
        Q_UNUSED(name);
        updateRows();
    }

    void onRowClicked()
    {
        const PDFTileRow* row = qobject_cast<const PDFTileRow*>(sender());
        const PDFAppEditContentRegistry* registry = m_context ? m_context->getRegistry() : nullptr;
        const QString actionId = row ? row->property(PDFAppEditContentMenus::ACTION_PROPERTY).toString() : QString();
        const PDFAppEditContentActionEntry* entry = registry ? registry->findAction(actionId) : nullptr;
        if (!entry)
        {
            return;
        }

        // A check row stays open (its check follows the option), another row closes the panel; the action runs from
        // the event loop in both cases (triggerAction queues it) - the last call
        if (!entry->isCheckable() && m_panel)
        {
            m_panel->close();
        }
        if (m_context)
        {
            m_context->triggerAction(actionId);
        }
    }

    QPointer<PDFAppEditContentContext> m_context;
    QPointer<PDFAppToolPanelPopup> m_panel;
    QString m_menuId;
    std::vector<QPointer<QWidget>> m_widgets;
    std::vector<QPointer<PDFTileRow>> m_rows;
};

// ---------------------------------------------------------------- PDFAppEditContentMenus

PDFAppEditContentMenus::PDFAppEditContentMenus(PDFAppEditContentContext* context, QObject* parent) :
    BaseClass(parent),
    PDFRetirable(this),
    m_context(context)
{
    if (context)
    {
        // The context menus of the canvas and of the tabs (P11, P12, P42, P52 ask, this object opens them)
        PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::contextMenuRequested, this, &PDFAppEditContentMenus::onContextMenuRequested);
    }
}

PDFAppEditContentMenus::~PDFAppEditContentMenus() = default;

QKeySequence PDFAppEditContentMenus::getShownKey(const PDFAppEditContentActionEntry* entry, const PDFAppEditContentContext* context)
{
    if (!entry)
    {
        return QKeySequence();
    }
    const std::vector<PDFAppEditContentActionEntry::Key> keys = entry->getKeys();
    if (!keys.empty())
    {
        return keys.front().sequence;
    }

    // An entry, which answers a command of the application, shows its key (the default one; the view of the tool turns
    // the key into the command, pe1_skeleton_notes.md 8.2)
    const QStringList commandIds = entry->getApplicationCommandIds();
    const PDFDocumentToolGUI* gui = context ? context->getGUI() : nullptr;
    const PDFAppContext* appContext = gui ? gui->getContext() : nullptr;
    const PDFAppCommand* command = (appContext && !commandIds.isEmpty()) ? appContext->getCommandRegistry()->findCommand(commandIds.front()) : nullptr;
    if (command)
    {
        return command->defaultShortcut;
    }
    if (!commandIds.isEmpty() && commandIds.front() == PDFAppCommands::deleteSelection())
    {
        // Without the application (a test of the context alone): the key of the command
        return QKeySequence(QKeySequence::Delete);
    }
    return QKeySequence();
}

QString PDFAppEditContentMenus::getDescription(const PDFAppEditContentActionEntry* entry)
{
    const PDFAppEditContentMenuEntry* ownEntry = dynamic_cast<const PDFAppEditContentMenuEntry*>(entry);
    return ownEntry ? ownEntry->getDescription() : QString();
}

QMenu* PDFAppEditContentMenus::openMenu(const QString& menuId, const QPoint& globalPosition)
{
    // One menu at a time: the shown one is closed and retired (its slot may be on the stack)
    closeMenu();

    const PDFAppEditContentRegistry* registry = m_context ? m_context->getRegistry() : nullptr;
    if (!registry || PDFDeferredDeletion::isRetired(this))
    {
        return nullptr;
    }

    QWidget* dialogParent = m_context->getDialogParent();
    QMenu* menu = new QMenu(dialogParent ? dialogParent->window() : nullptr);
    menu->setObjectName(QStringLiteral("editContentMenu-") + menuId);

    // Parented to the window, the menu belongs to this object: the end of the tool retires it
    PDFDeferredDeletion::setOwner(menu, this);
    if (dialogParent)
    {
        menu->setLayoutDirection(dialogParent->layoutDirection());
    }

    int lastGroup = 0;
    bool isFirst = true;
    for (const PDFAppEditContentActionEntry* entry : registry->getMenuActions(menuId))
    {
        // getPlacements returns a value: the placement points into this local vector
        const std::vector<PDFAppEditContentActionEntry::Placement> placements = entry->getPlacements();
        const PDFAppEditContentActionEntry::Placement* placement = PDFAppEditContentRegistry::findPlacement(placements, menuId);
        if (!placement || !entry->isVisible(m_context))
        {
            // Rows of a later stage and rows, which do not apply to the selection, are hidden (zadani_gui.md 4.16)
            continue;
        }
        if (!isFirst && placement->group != lastGroup)
        {
            menu->addSeparator();
        }
        lastGroup = placement->group;
        isFirst = false;

        const QString iconName = entry->getIconName();
        const QIcon icon = iconName.isEmpty() ? QIcon() : PDFThemeIcons::getIcon(iconName);
        const std::vector<PDFAppEditContentActionChoice> choices = entry->getChoices(m_context);
        if (!choices.empty())
        {
            QMenu* submenu = menu->addMenu(icon, entry->getTitle(m_context));
            submenu->setObjectName(QStringLiteral("editContentSubmenu-") + entry->getId());
            submenu->menuAction()->setProperty(ACTION_PROPERTY, entry->getId());
            submenu->setEnabled(entry->isEnabled(m_context));
            for (const PDFAppEditContentActionChoice& choice : choices)
            {
                QAction* action = submenu->addAction(choice.iconName.isEmpty() ? QIcon() : PDFThemeIcons::getIcon(choice.iconName), choice.title);
                action->setProperty(ACTION_PROPERTY, entry->getId());
                action->setProperty(CHOICE_PROPERTY, choice.id);
                action->setCheckable(choice.isChecked);
                action->setChecked(choice.isChecked);
                action->setEnabled(choice.isEnabled);
            }
            continue;
        }

        QAction* action = menu->addAction(icon, entry->getTitle(m_context));
        action->setObjectName(QStringLiteral("editContentMenuAction-") + entry->getId());
        action->setProperty(ACTION_PROPERTY, entry->getId());
        action->setEnabled(entry->isEnabled(m_context));
        if (entry->isCheckable())
        {
            action->setCheckable(true);
            action->setChecked(entry->isChecked(m_context));
        }

        // The key is shown only: the shortcuts of the tool are actions of its key widget (P14)
        const QKeySequence key = getShownKey(entry, m_context);
        if (!key.isEmpty())
        {
            action->setShortcut(key);
            action->setShortcutContext(Qt::WidgetShortcut);
        }
    }

    if (menu->actions().isEmpty())
    {
        PDFDeferredDeletion::retire(menu);
        return nullptr;
    }

    // QMenu::triggered comes also for the rows of the submenus
    connect(menu, &QMenu::triggered, this, &PDFAppEditContentMenus::onMenuTriggered);
    PDFDeferredDeletion::installEventFilter(menu, this);
    m_menu = menu;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(globalPosition);
    return menu;
}

void PDFAppEditContentMenus::fillPanel(PDFAppToolPanelPopup* panel, const QString& menuId)
{
    if (!panel)
    {
        return;
    }

    // One binding of the panel (a child of it); a second fill replaces its rows
    PDFAppEditContentMenusPanelBinding* binding = nullptr;
    for (QObject* child : panel->children())
    {
        PDFAppEditContentMenusPanelBinding* candidate = dynamic_cast<PDFAppEditContentMenusPanelBinding*>(child);
        if (candidate && candidate->getMenuId() == menuId && !PDFDeferredDeletion::isRetired(candidate))
        {
            binding = candidate;
            break;
        }
    }
    if (!binding)
    {
        binding = new PDFAppEditContentMenusPanelBinding(m_context, panel, menuId);
    }
    binding->fill();
}

void PDFAppEditContentMenus::registerParts(PDFAppEditContentRegistry* registry)
{
    using Order = PDFAppEditContentMenusHelper::Order;
    using View = PDFAppEditContentViewEntry::View;

    registry->addAction(std::make_unique<PDFAppEditContentDeleteEntry>());
    registry->addAction(std::make_unique<PDFAppEditContentSelectAllEntry>());
    registry->addAction(std::make_unique<PDFAppEditContentOrderEntry>(Order::BringToFront));
    registry->addAction(std::make_unique<PDFAppEditContentOrderEntry>(Order::BringForward));
    registry->addAction(std::make_unique<PDFAppEditContentOrderEntry>(Order::SendBackward));
    registry->addAction(std::make_unique<PDFAppEditContentOrderEntry>(Order::SendToBack));
    registry->addAction(std::make_unique<PDFAppEditContentGroupEntry>());
    registry->addAction(std::make_unique<PDFAppEditContentUngroupEntry>());
    registry->addAction(std::make_unique<PDFAppEditContentStateEntry>(false));
    registry->addAction(std::make_unique<PDFAppEditContentStateEntry>(true));
    registry->addAction(std::make_unique<PDFAppEditContentDuplicateEntry>());
    registry->addAction(std::make_unique<PDFAppEditContentViewEntry>(View::ClippingPaths));
    registry->addAction(std::make_unique<PDFAppEditContentViewEntry>(View::InvisibleText));
    registry->addAction(std::make_unique<PDFAppEditContentViewEntry>(View::Annotations));
}

void PDFAppEditContentMenus::retire()
{
    // The menu is owned by this object: the retire closes and retires it with this object
    m_menu = nullptr;
}

bool PDFAppEditContentMenus::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_menu && event->type() == QEvent::LanguageChange)
    {
        // A change of the language closes the menu (its rows have the texts of the old language); from the event loop,
        // the menu is inside of its own event
        QMetaObject::invokeMethod(this, &PDFAppEditContentMenus::closeMenu, Qt::QueuedConnection);
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppEditContentMenus::closeMenu()
{
    if (m_menu)
    {
        QMenu* menu = m_menu.data();
        m_menu = nullptr;
        menu->close();
        PDFDeferredDeletion::retire(menu);
    }
}

void PDFAppEditContentMenus::onContextMenuRequested(const QString& menuId, const QPoint& globalPosition)
{
    openMenu(menuId, globalPosition);
}

void PDFAppEditContentMenus::onMenuTriggered(QAction* action)
{
    // Only the request (Qt reads the menu after triggered; the action runs from the event loop)
    if (action && m_context)
    {
        m_context->triggerAction(action->property(ACTION_PROPERTY).toString(), action->property(CHOICE_PROPERTY).toString());
    }
}

}   // namespace pdf

#include "moc_pdfappeditcontentmenus.cpp"
