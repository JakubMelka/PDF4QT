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

#include "pdfappeditcontentcontext.h"

#include "pdfappeditcontentpreview.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentscene.h"
#include "pdfappeditcontentselection.h"
#include "pdfappeditcontenttools.h"
#include "pdfappeditcontentview.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolparametermodel.h"
#include "pdfdocumenttoolgui.h"

#include "pdfcatalog.h"
#include "pdfcontentdisplay.h"
#include "pdfcontentundostack.h"

#include <QCoreApplication>
#include <QMetaObject>

#include <utility>

namespace pdf
{

// ---------------------------------------------------------------- PDFAppEditContentOptions

PDFAppEditContentOptions::PDFAppEditContentOptions(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppEditContentOptions::~PDFAppEditContentOptions() = default;

void PDFAppEditContentOptions::setModel(PDFAppToolParameterModel* model)
{
    if (m_model == model)
    {
        return;
    }

    if (m_model)
    {
        m_model->disconnect(this);
    }
    m_model = model;
    if (model)
    {
        // The model belongs to the shell (outside of the subtree of the context): cut by retire of the context
        PDFDeferredDeletion::connect(model, &PDFAppToolParameterModel::changed, this, &PDFAppEditContentOptions::onModelChanged);
    }
}

bool PDFAppEditContentOptions::hasModelParameter(const QString& name) const
{
    if (!m_model)
    {
        return false;
    }
    for (const PDFDocumentToolParameterDefinition& definition : m_model->getDefinitions())
    {
        if (definition.getName() == name)
        {
            return true;
        }
    }
    return false;
}

QVariant PDFAppEditContentOptions::getValue(const QString& name) const
{
    if (hasModelParameter(name))
    {
        return m_model->getValue(name);
    }

    const auto it = m_values.constFind(name);
    if (it != m_values.constEnd())
    {
        return it.value();
    }
    return getDefaultValue(name);
}

bool PDFAppEditContentOptions::getBool(const QString& name) const
{
    return getValue(name).toBool();
}

void PDFAppEditContentOptions::setValue(const QString& name, const QVariant& value)
{
    if (name.isEmpty() || getValue(name) == value)
    {
        return;
    }

    if (hasModelParameter(name))
    {
        // A value without an undo step of the parameters (zadani_gui.md 4.26: a change of a setting is no Undo
        // step); the model emits changed, which onModelChanged passes on - the last call
        const QString error = m_model->setFollowingValue(name, value);
        if (!error.isEmpty())
        {
            qWarning("PDFAppEditContentOptions: the value of '%s' was refused: %s", qPrintable(name), qPrintable(error));
        }
        return;
    }

    m_values.insert(name, value);
    Q_EMIT changed(name);
}

QVariant PDFAppEditContentOptions::getDefaultValue(const QString& name)
{
    // zadani_gui.md 4.26; the frames -menus-view (checked rows) and -grid-rulers (grid 36 pt, origin bottom left)
    const QLatin1StringView enabled[] = { QLatin1StringView(SHOW_GUIDES), QLatin1StringView(SHOW_ANNOTATIONS), QLatin1StringView(SNAP_TO_OBJECTS),
                                          QLatin1StringView(SNAP_TO_GRID), QLatin1StringView(PREVIEW_OVERPRINT), QLatin1StringView(PREVIEW_ANNOTATIONS) };
    for (const QLatin1StringView& item : enabled)
    {
        if (name == item)
        {
            return true;
        }
    }

    const QLatin1StringView disabled[] = { QLatin1StringView(SHOW_RULERS), QLatin1StringView(SHOW_GRID), QLatin1StringView(SHOW_CLIPPING_PATHS),
                                           QLatin1StringView(SHOW_INVISIBLE_TEXT), QLatin1StringView(SHOW_READING_ORDER) };
    for (const QLatin1StringView& item : disabled)
    {
        if (name == item)
        {
            return false;
        }
    }

    if (name == QLatin1StringView(UNITS))
    {
        return QStringLiteral("pt");
    }
    if (name == QLatin1StringView(GRID_SPACING))
    {
        return 36.0;
    }
    if (name == QLatin1StringView(GRID_ORIGIN))
    {
        return QStringLiteral("bottom-left");
    }
    if (name == QLatin1StringView(PREVIEW_PAPER_COLOR))
    {
        return QStringLiteral("#ffffff");
    }
    if (name == QLatin1StringView(LEFT_TAB))
    {
        return QStringLiteral("objects");
    }
    return QVariant();
}

void PDFAppEditContentOptions::onModelChanged(const QString& name)
{
    Q_EMIT changed(name);
}

// ---------------------------------------------------------------- PDFAppEditContentContext

PDFAppEditContentContext::PDFAppEditContentContext(PDFDocumentPointer document, PDFCMSPointer cms, const PDFAppEditContentRegistry* registry, QObject* parent) :
    BaseClass(parent),
    PDFRetirable(this),
    m_registry(registry ? registry : &PDFAppEditContentRegistry::getInstance()),
    m_document(document),
    m_cms(cms)
{
    // The destructor deletes the children in the reverse order of this creation: the tab states, the preview, the
    // controller and the selection go before the session they read
    m_session = new PDFContentEditSession(std::move(document), std::move(cms), this);
    m_options = new PDFAppEditContentOptions(this);
    m_selection = new PDFAppEditContentSelection(this, this);
    m_toolController = new PDFAppEditContentToolController(this, this);
    m_preview = new PDFAppEditContentPreview(this, this);

    connectSession();

    for (const PDFAppEditContentTabEntry* tab : m_registry->getTabs())
    {
        if (QObject* state = tab->createState(this, this))
        {
            state->setParent(this);
            m_tabStates[tab->getId()] = state;
        }
    }

    // The last step (R3-8): a tool finds every child of the context in its constructor
    m_toolController->createTools();
}

PDFAppEditContentContext::~PDFAppEditContentContext()
{
    // The children in the reverse order of their creation, while the context is whole (R3-3; Qt would delete them in
    // the order of the creation, the session first): each child may still read the children created before it. A
    // child deleted here leaves the list of the children of the context, ~QObject does not see it again.
    const std::vector<const PDFAppEditContentTabEntry*> tabs = m_registry->getTabs();
    for (auto it = tabs.crbegin(); it != tabs.crend(); ++it)
    {
        const auto state = m_tabStates.find((*it)->getId());
        if (state != m_tabStates.end())
        {
            delete state->second.data();
            m_tabStates.erase(state);
        }
    }
    // A state of a tab, which the registry does not have any more (a test registry changed meanwhile)
    for (auto& state : m_tabStates)
    {
        delete state.second.data();
    }
    m_tabStates.clear();

    delete m_preview;
    m_preview = nullptr;
    delete m_toolController;
    m_toolController = nullptr;
    delete m_selection;
    m_selection = nullptr;
    delete m_options;
    m_options = nullptr;
    delete m_session;
    m_session = nullptr;
}

QObject* PDFAppEditContentContext::getTabState(const QString& tabId) const
{
    const auto it = m_tabStates.find(tabId);
    return it != m_tabStates.cend() ? it->second.data() : nullptr;
}

void PDFAppEditContentContext::setGUI(PDFDocumentToolGUI* gui)
{
    m_gui = gui;
    m_options->setModel(gui ? gui->getModel() : nullptr);
}

QWidget* PDFAppEditContentContext::getDialogParent() const
{
    const PDFAppToolHost* host = m_gui ? m_gui->getHost() : nullptr;
    return host ? host->getKeyWidget() : nullptr;
}

void PDFAppEditContentContext::setCanvas(PDFAppEditContentCanvas* canvas)
{
    m_canvas = canvas;
}

PDFAppEditContentScene* PDFAppEditContentContext::getScene() const
{
    return m_canvas ? m_canvas->getScene() : nullptr;
}

PDFAppEditContentView* PDFAppEditContentContext::getView() const
{
    return m_canvas ? m_canvas->getView() : nullptr;
}

PDFInteger PDFAppEditContentContext::getPageCount() const
{
    const PDFDocument* document = m_session->getDocument();
    return document ? PDFInteger(document->getCatalog()->getPageCount()) : 0;
}

PDFContentContainerKey PDFAppEditContentContext::getPageKey() const
{
    return m_pageIndex >= 0 ? m_session->getPageKey(m_pageIndex) : PDFContentContainerKey();
}

PDFContentContainerKey PDFAppEditContentContext::getContainerKey() const
{
    if (!m_isolationPath.empty())
    {
        return m_isolationPath.back().root.container;
    }
    return getPageKey();
}

PDFContentContainerPointer PDFAppEditContentContext::getContainer() const
{
    const PDFContentContainerKey key = getContainerKey();
    return key.isValid() ? m_session->getContainer(key) : PDFContentContainerPointer();
}

PDFContentNodeReference PDFAppEditContentContext::getEditedRoot() const
{
    if (!m_isolationPath.empty())
    {
        return m_isolationPath.back().root;
    }

    PDFContentNodeReference root;
    root.container = getPageKey();
    if (const PDFContentContainerPointer container = getContainer(); container && container->getRoot())
    {
        root.node = container->getRoot()->getId();
    }
    return root;
}

void PDFAppEditContentContext::requestPage(PDFInteger pageIndex)
{
    if (pageIndex < 0 || pageIndex >= getPageCount())
    {
        return;
    }
    Q_EMIT pageRequested(pageIndex);
}

void PDFAppEditContentContext::setPage(PDFInteger pageIndex)
{
    if (pageIndex == m_pageIndex || pageIndex < 0 || pageIndex >= getPageCount())
    {
        return;
    }

    m_pageIndex = pageIndex;
    m_isolationPath.clear();

    // The container of the page is built in a worker; containerLoaded follows from the event loop (also for a
    // container, which is loaded already - onContainerLoaded then tells nothing new). The session emits nothing
    // inside of the request.
    const PDFContentContainerKey key = getPageKey();
    if (key.isValid() && !m_session->getContainer(key))
    {
        m_session->requestContainer(key);
    }

    // The slots may close the tool: the two emissions are guarded
    QPointer<PDFAppEditContentContext> guard(this);
    Q_EMIT pageChanged(pageIndex);
    if (guard && !PDFDeferredDeletion::isRetired(this))
    {
        Q_EMIT editingAllowedChanged();
    }
}

void PDFAppEditContentContext::setIsolationPath(std::vector<PDFAppEditContentIsolationLevel> path)
{
    if (m_isolationPath == path)
    {
        return;
    }

    m_isolationPath = std::move(path);
    const PDFContentContainerKey key = getContainerKey();
    if (key.isValid() && !m_session->getContainer(key))
    {
        m_session->requestContainer(key);
    }

    QPointer<PDFAppEditContentContext> guard(this);
    Q_EMIT containerChanged();
    if (guard && !PDFDeferredDeletion::isRetired(this))
    {
        Q_EMIT editingAllowedChanged();
    }
}

void PDFAppEditContentContext::requestIsolation(const PDFContentNodeReference& node)
{
    if (node.isValid())
    {
        Q_EMIT isolationRequested(node);
    }
}

void PDFAppEditContentContext::requestLeaveIsolation(int levelCount)
{
    if (levelCount > 0 && !m_isolationPath.empty())
    {
        Q_EMIT leaveIsolationRequested(levelCount);
    }
}

bool PDFAppEditContentContext::isEditingAllowed() const
{
    if (m_isPreviewActive || m_isRunning || PDFDeferredDeletion::isRetired(this) || m_session->isExecuting())
    {
        return false;
    }
    const PDFContentContainerPointer container = getContainer();
    return container && !container->isReadOnly();
}

std::vector<PDFContentNodeReference> PDFAppEditContentContext::getChangeableSelection() const
{
    std::vector<PDFContentNodeReference> result;
    if (!m_selection || m_selection->isEmpty())
    {
        return result;
    }

    result.reserve(m_selection->getNodes().size());
    for (const PDFContentNodeReference& node : m_selection->getNodes())
    {
        if (isNodeChangeable(node))
        {
            result.push_back(node);
        }
    }
    return result;
}

bool PDFAppEditContentContext::isNodeChangeable(const PDFContentNodeReference& node) const
{
    const PDFContentContainerPointer container = getContainer();
    if (!container || !node.isValid() || node.container != container->getKey())
    {
        return false;
    }

    const PDFContentNodePointer target = container->getNode(node.node);
    const PDFContentNodeReference root = getEditedRoot();
    if (!target || target->getKind() == PDFContentNodeKind::Opaque || node.node == root.node)
    {
        return false;
    }

    // Up to the edited root: the node and every ancestor unlocked; the root must be reached (a node outside of the
    // edited group is not the user's to change). The depth of a tree of the core is bounded by MAXIMUM_DEPTH.
    PDFContentNodeId current = node.node;
    for (int depth = 0; depth <= PDFContentContainer::MAXIMUM_DEPTH + 2 && current.isValid(); ++depth)
    {
        if (current == root.node)
        {
            return true;
        }
        if (m_session->isNodeLocked(PDFContentNodeReference{ node.container, current }))
        {
            return false;
        }
        const PDFContentNodePointer parent = container->getParent(current);
        current = parent ? parent->getId() : PDFContentNodeId();
    }
    return false;
}

bool PDFAppEditContentContext::isComplexPage(const PDFContentDisplayCounts& counts)
{
    return counts.nodeCount > PDFAppEditContentScene::COMPLEX_PAGE_OBJECTS || counts.pathSegmentCount > PDFAppEditContentScene::COMPLEX_PAGE_PATH_SEGMENTS;
}

void PDFAppEditContentContext::connectSession()
{
    connect(m_session, &PDFContentEditSession::containerLoaded, this, &PDFAppEditContentContext::onContainerLoaded);
    connect(m_session, &PDFContentEditSession::containerFailed, this, &PDFAppEditContentContext::onContainerFailed);
}

void PDFAppEditContentContext::reloadDocument(PDFDocumentPointer document)
{
    if (PDFDeferredDeletion::isRetired(this) || !document || document == m_document)
    {
        return;
    }

    // Nothing of the old version runs any more: the queued commands and actions name its nodes
    m_queuedCommands.clear();
    m_queuedActions.clear();
    while (m_macroDepth > 0)
    {
        --m_macroDepth;
        m_session->getUndoStack()->endMacro();
    }
    m_lastRefusalText.clear();
    m_isolationPath.clear();

    // The selection and the mode of the canvas end over the old session (their slots read it, and may close the
    // tool: guarded)
    QPointer<PDFAppEditContentContext> guard(this);
    m_selection->clear();
    if (!guard || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }
    setLockedClickNode(PDFContentNodeReference());
    if (!guard || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }
    m_toolController->cancel();
    m_toolController->activateSelectTool();
    if (!guard || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }

    // The new session; the old one is cut off at once and deleted from the event loop (a caller up the stack may
    // still be inside of one of its functions). Its workers hold their own data; their queued results reach the old
    // session only and die with it.
    PDFContentEditSession* oldSession = m_session;
    oldSession->disconnect();
    m_document = document;
    m_session = new PDFContentEditSession(std::move(document), m_cms, this);
    connectSession();
    oldSession->deleteLater();

    // The page stays (clamped to the new version), its container is requested again
    const PDFInteger pageCount = getPageCount();
    if (m_pageIndex >= pageCount)
    {
        m_pageIndex = pageCount - 1;
    }
    const PDFContentContainerKey key = getPageKey();
    if (key.isValid() && !m_session->getContainer(key))
    {
        m_session->requestContainer(key);
    }

    Q_EMIT sessionReplaced();
    if (!guard || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }
    Q_EMIT pageChanged(m_pageIndex);
    if (!guard || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }
    Q_EMIT editingAllowedChanged();
}

bool PDFAppEditContentContext::execute(const PDFContentCommandPointer& command)
{
    if (!command || !isEditingAllowed())
    {
        // Nothing is told: the caller has asked at a moment, when its control should have been disabled
        return false;
    }

    // The slots of modelChanged run inside (the tool may be closed by them): the context is guarded
    QPointer<PDFAppEditContentContext> guard(this);
    QString errorText;
    const bool isExecuted = m_session->execute(command, &errorText);
    if (!guard || PDFDeferredDeletion::isRetired(this))
    {
        return isExecuted;
    }

    if (isExecuted)
    {
        m_lastRefusalText.clear();
        return true;
    }

    m_lastRefusalText = errorText;
    Q_EMIT commandRefused(errorText);
    return false;
}

void PDFAppEditContentContext::requestExecute(const PDFContentCommandPointer& command)
{
    if (!command || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }

    m_queuedCommands.push_back(command);
    if (!m_isCommandsQueued)
    {
        m_isCommandsQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppEditContentContext::onQueuedCommands, Qt::QueuedConnection);
    }
}

void PDFAppEditContentContext::onQueuedCommands()
{
    m_isCommandsQueued = false;

    // The commands requested until now, in their order; a command requested by a slot of these runs in the next turn
    std::vector<PDFContentCommandPointer> commands;
    commands.swap(m_queuedCommands);

    QPointer<PDFAppEditContentContext> guard(this);
    for (const PDFContentCommandPointer& command : commands)
    {
        execute(command);
        if (!guard || PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }
}

void PDFAppEditContentContext::beginMacro(const QString& text)
{
    ++m_macroDepth;
    m_session->getUndoStack()->beginMacro(text);
}

void PDFAppEditContentContext::endMacro()
{
    if (m_macroDepth > 0)
    {
        --m_macroDepth;
        m_session->getUndoStack()->endMacro();
    }
}

void PDFAppEditContentContext::commitPendingEdits()
{
    if (!PDFDeferredDeletion::isRetired(this))
    {
        Q_EMIT pendingEditsCommitRequested();
    }
}

void PDFAppEditContentContext::discardPendingEdits()
{
    Q_EMIT pendingEditsDiscardRequested();
}

void PDFAppEditContentContext::setPreviewActive(bool isActive)
{
    if (m_isPreviewActive == isActive)
    {
        return;
    }

    m_isPreviewActive = isActive;
    QPointer<PDFAppEditContentContext> guard(this);
    Q_EMIT previewActiveChanged(isActive);
    if (guard && !PDFDeferredDeletion::isRetired(this))
    {
        Q_EMIT editingAllowedChanged();
    }
}

void PDFAppEditContentContext::setRunning(bool isRunning)
{
    if (m_isRunning == isRunning)
    {
        return;
    }

    m_isRunning = isRunning;
    Q_EMIT editingAllowedChanged();
}

void PDFAppEditContentContext::setCardRequested(const QString& cardId, bool isRequested)
{
    if (cardId.isEmpty() || m_requestedCards.contains(cardId) == isRequested)
    {
        return;
    }

    if (isRequested)
    {
        m_requestedCards.insert(cardId);
    }
    else
    {
        m_requestedCards.remove(cardId);
    }
    Q_EMIT cardsChanged();
}

void PDFAppEditContentContext::setSectionExpanded(const QString& sectionId, bool isExpanded)
{
    if (isExpanded)
    {
        m_expandedSections.insert(sectionId);
    }
    else
    {
        m_expandedSections.remove(sectionId);
    }
}

void PDFAppEditContentContext::setLockedClickNode(const PDFContentNodeReference& node)
{
    if (m_lockedClickNode == node)
    {
        return;
    }

    m_lockedClickNode = node;
    Q_EMIT cardsChanged();
}

void PDFAppEditContentContext::showNotice(const PDFAppEditContentNoticeRequest& request)
{
    if (request.isValid() && !PDFDeferredDeletion::isRetired(this))
    {
        Q_EMIT noticeRequested(request);
    }
}

void PDFAppEditContentContext::hideNotice(const QString& noticeId)
{
    if (!noticeId.isEmpty() && !PDFDeferredDeletion::isRetired(this))
    {
        Q_EMIT noticeHideRequested(noticeId);
    }
}

void PDFAppEditContentContext::requestContextMenu(const QString& menuId, const QPoint& globalPosition)
{
    if (!menuId.isEmpty() && !PDFDeferredDeletion::isRetired(this))
    {
        Q_EMIT contextMenuRequested(menuId, globalPosition);
    }
}

bool PDFAppEditContentContext::triggerAction(const QString& actionId, const QString& choiceId)
{
    if (PDFDeferredDeletion::isRetired(this) || !m_registry->findAction(actionId))
    {
        return false;
    }

    m_queuedActions.push_back(QueuedAction{ actionId, choiceId });
    if (!m_isActionsQueued)
    {
        m_isActionsQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppEditContentContext::onQueuedActions, Qt::QueuedConnection);
    }
    return true;
}

void PDFAppEditContentContext::onQueuedActions()
{
    m_isActionsQueued = false;

    std::vector<QueuedAction> actions;
    actions.swap(m_queuedActions);

    QPointer<PDFAppEditContentContext> guard(this);
    for (const QueuedAction& action : actions)
    {
        if (action.isApplicationCommand)
        {
            // The tool or the entry may close the tool
            runApplicationCommand(action.actionId);
            if (!guard || PDFDeferredDeletion::isRetired(this))
            {
                return;
            }
            continue;
        }

        // The state is read again now: the action may not apply any more (the selection changed, Preview is on)
        const PDFAppEditContentActionEntry* entry = m_registry->findAction(action.actionId);
        if (!entry || !entry->isVisible(this) || !entry->isEnabled(this))
        {
            continue;
        }

        // The entry is foreign code: it may close the tool
        if (action.choiceId.isEmpty())
        {
            entry->trigger(this);
        }
        else
        {
            entry->triggerChoice(this, action.choiceId);
        }
        if (!guard || PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }
}

PDFAppToolCommandState PDFAppEditContentContext::getApplicationCommandState(const QString& commandId) const
{
    // Undo, Redo, Save and Save As are the shell's (its own undo target, the save question)
    if (commandId.isEmpty() || PDFAppEditContentKeyTable::getShellCommandIds().contains(commandId))
    {
        return PDFAppToolCommandState::NotHandled;
    }

    // The active tool first (typing answers the clipboard commands for its characters)
    if (const PDFAppEditContentTool* tool = m_toolController ? m_toolController->getActiveTool() : nullptr)
    {
        const PDFAppToolCommandState state = tool->getApplicationCommandState(commandId);
        if (state != PDFAppToolCommandState::NotHandled)
        {
            return isEditingAllowed() ? state : PDFAppToolCommandState::Disabled;
        }
    }

    // Then the action entry, which answers the command; none = not the tool's ("hidden, not disabled")
    const PDFAppEditContentActionEntry* entry = m_registry->findActionByApplicationCommand(commandId);
    if (!entry)
    {
        return PDFAppToolCommandState::NotHandled;
    }
    if (PDFDeferredDeletion::isRetired(this))
    {
        return PDFAppToolCommandState::Disabled;
    }
    return entry->isVisible(this) && entry->isEnabled(this) ? PDFAppToolCommandState::Enabled : PDFAppToolCommandState::Disabled;
}

bool PDFAppEditContentContext::handleApplicationCommand(const QString& commandId)
{
    if (PDFDeferredDeletion::isRetired(this) || getApplicationCommandState(commandId) != PDFAppToolCommandState::Enabled)
    {
        return false;
    }

    // From the event loop as every action (the caller may be a slot of QMenu::triggered or a key event)
    m_queuedActions.push_back(QueuedAction{ commandId, QString(), true });
    if (!m_isActionsQueued)
    {
        m_isActionsQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppEditContentContext::onQueuedActions, Qt::QueuedConnection);
    }
    return true;
}

void PDFAppEditContentContext::runApplicationCommand(const QString& commandId)
{
    // The state is read again now (the selection, the tool, Preview may have changed since the request)
    if (getApplicationCommandState(commandId) != PDFAppToolCommandState::Enabled)
    {
        return;
    }

    PDFAppEditContentTool* tool = m_toolController ? m_toolController->getActiveTool() : nullptr;
    if (tool && tool->getApplicationCommandState(commandId) == PDFAppToolCommandState::Enabled)
    {
        tool->handleApplicationCommand(commandId);
        return;
    }

    if (const PDFAppEditContentActionEntry* entry = m_registry->findActionByApplicationCommand(commandId))
    {
        entry->trigger(this);
    }
}

void PDFAppEditContentContext::onContainerLoaded(const PDFContentContainerKey& key)
{
    if (key == getContainerKey())
    {
        QPointer<PDFAppEditContentContext> guard(this);
        Q_EMIT containerChanged();
        if (guard && !PDFDeferredDeletion::isRetired(this))
        {
            Q_EMIT editingAllowedChanged();
        }
    }
}

void PDFAppEditContentContext::onContainerFailed(const PDFContentContainerKey& key, const QString& errorText)
{
    Q_UNUSED(errorText);
    if (key == getContainerKey())
    {
        Q_EMIT containerChanged();
    }
}

void PDFAppEditContentContext::retire()
{
    // Nothing queued runs any more (the queued slots find the lists empty; the gate of the retired objects drops
    // them anyway); an open macro is closed so that the undo stack stays consistent for a run, which may still
    // read a snapshot. No signal.
    m_queuedCommands.clear();
    m_queuedActions.clear();
    while (m_macroDepth > 0)
    {
        --m_macroDepth;
        m_session->getUndoStack()->endMacro();
    }
}

// ---------------------------------------------------------------- PDFAppEditContentGeometryHelper

QRectF PDFAppEditContentGeometryHelper::getLeafBoundingBox(const PDFContentContainer& container, const PDFContentNode& node)
{
    const QRectF& box = node.getBoundingBox();
    const PDFContentResolvedStatePointer& state = node.getState();
    const bool isPath = node.getKind() == PDFContentNodeKind::Path;
    if ((box.isNull() && !isPath) || !state)
    {
        return box;
    }

    // The matrix T, with which the written content draws the node: T x CTM (CTM = the matrix of the state of the node;
    // its own override and the overrides of its ancestors, composed by the core). No value: a matrix on the way cannot
    // be inverted - the box as it was read.
    const std::optional<PDFContentStateOverride> override = container.getEffectiveOverride(node.getId());
    if (!override || override->transform.isIdentity())
    {
        return box;
    }

    const QTransform& matrix = state->getCurrentTransformationMatrix();
    if (isPath)
    {
        // Exactly as the builder measures a path, under the matrix of the override
        return node.getData().path.transformed(override->transform * matrix).getBoundingBox();
    }

    // Another leaf: its box moves by D = CTM^-1 x T x CTM in the default space of the container (a point c of the box is
    // the point c x CTM^-1 of the space of the node, drawn at c x CTM^-1 x T x CTM)
    if (!matrix.isInvertible())
    {
        return box;
    }
    return (matrix.inverted() * override->transform * matrix).mapRect(box);
}

QRectF PDFAppEditContentGeometryHelper::getNodeBoundingBox(const PDFContentContainer& container, PDFContentNodeId nodeId)
{
    const PDFContentNodePointer start = container.getNode(nodeId);
    if (!start)
    {
        return QRectF();
    }

    // A walk over the subtree (a tree: every node once); the nodes with children of their own are measured by their
    // children, everything else as a leaf
    struct Item
    {
        const PDFContentNode* node = nullptr;
        int depth = 0;
    };

    QRectF result;
    std::vector<Item> stack;
    stack.push_back(Item{ start.get(), 0 });
    while (!stack.empty())
    {
        const Item item = stack.back();
        stack.pop_back();

        const PDFContentNodeKind kind = item.node->getKind();
        const bool isContainerKind = kind == PDFContentNodeKind::Root || kind == PDFContentNodeKind::Group || kind == PDFContentNodeKind::MarkedContent;
        QRectF box;
        if (isContainerKind && item.depth <= PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            for (const PDFContentNodePointer& child : item.node->getChildren())
            {
                if (child)
                {
                    stack.push_back(Item{ child.get(), item.depth + 1 });
                }
            }
        }
        else if (isContainerKind)
        {
            // Deeper than the builder goes: the box of the builder of the whole subtree
            box = item.node->getBoundingBox();
        }
        else
        {
            box = getLeafBoundingBox(container, *item.node);
        }

        if (!box.isNull())
        {
            result = result.isNull() ? box : result.united(box);
        }
    }

    return result;
}

std::optional<QTransform> PDFAppEditContentGeometryHelper::getContainerToPageMatrix(const PDFContentEditSession& session,
                                                                                    const PDFContentContainerKey& key,
                                                                                    const std::vector<PDFAppEditContentIsolationLevel>& isolationPath)
{
    QTransform matrix;
    PDFContentContainerKey current = key;

    // From the innermost level outward, each level once
    for (auto it = isolationPath.crbegin(); it != isolationPath.crend() && current.kind != PDFContentContainerKey::Kind::Page; ++it)
    {
        const PDFAppEditContentIsolationLevel& level = *it;
        if (level.root.container != current || !level.entry.isValid() || level.entry.container == current)
        {
            // A group of the same container, or a level of another container (an outer one, reached later)
            continue;
        }

        const PDFContentContainerPointer container = session.getContainer(current);
        if (!container)
        {
            return std::nullopt;
        }

        if (current.kind == PDFContentContainerKey::Kind::PatternCell)
        {
            // Pattern space -> the default space of the content, which uses the pattern (ISO 32000-2, 8.7.3.1)
            matrix = matrix * container->getMatrix();
        }
        else
        {
            // Form space -> the space of its use by /Matrix, then as the content draws the use
            const PDFContentContainerPointer entryContainer = session.getContainer(level.entry.container);
            const PDFContentNodePointer entry = entryContainer ? entryContainer->getNode(level.entry.node) : PDFContentNodePointer();
            if (!entry)
            {
                return std::nullopt;
            }

            QTransform useMatrix = entry->getState() ? entry->getState()->getCurrentTransformationMatrix() : QTransform();
            const std::optional<PDFContentStateOverride> override = entryContainer->getEffectiveOverride(entry->getId());
            if (override)
            {
                useMatrix = override->transform * useMatrix;
            }
            matrix = matrix * container->getMatrix() * useMatrix;
        }
        current = level.entry.container;
    }

    if (current.kind != PDFContentContainerKey::Kind::Page)
    {
        return std::nullopt;
    }
    return matrix;
}

QRectF PDFAppEditContentGeometryHelper::getPageBoundingBox(const PDFAppEditContentContext* context, const PDFContentNodeReference& node)
{
    const PDFContentEditSession* session = context ? context->getSession() : nullptr;
    const PDFContentContainerPointer container = session && node.isValid() ? session->getContainer(node.container) : PDFContentContainerPointer();
    if (!container)
    {
        return QRectF();
    }

    const QRectF box = getNodeBoundingBox(*container, node.node);
    if (box.isNull() || node.container.kind == PDFContentContainerKey::Kind::Page)
    {
        return box;
    }

    const std::optional<QTransform> matrix = getContainerToPageMatrix(*session, node.container, context->getIsolationPath());
    return matrix ? matrix->mapRect(box) : QRectF();
}

}   // namespace pdf

#include "moc_pdfappeditcontentcontext.cpp"
