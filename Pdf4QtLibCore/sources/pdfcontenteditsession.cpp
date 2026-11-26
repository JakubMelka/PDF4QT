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

// Edit Content, stage E0, package P03 (session, undo, basic commands): the editing session.
// Plan: GUI_overhaul/edit_tool/zadani_jadro.md 4.6, zadani_overeni.md 6.1, 6.3, zadani.md 12.7.
//
// Who owns what and in which order it dies:
//  - The session owns its Impl (the containers, the structure model, the undo stack with its
//    commands, the state of the editor). They die with the session, in its thread.
//  - PDFContentEditSessionShared is co-owned by the session and by every running task of a worker
//    (std::shared_ptr): it holds the results on their way to the session, the cancel flag and the
//    pointer to the session, which the destructor of the session clears under the mutex. A task
//    posts its queued call under the same mutex, so it never posts to a session being destroyed,
//    and the events posted before are dropped by Qt with the session.
//  - PDFContentEditSessionCaches (the font cache) is co-owned the same way; a task also co-owns
//    the document and the color management. Whoever drops the last reference destroys them: none
//    of them is a QObject, none belongs to a thread.
//  - A worker never touches the session and nothing the session changes: it gets plain values and
//    shared pointers of immutable data.

#include "pdfcontenteditsession.h"

#include "pdfcolorspaces.h"
#include "pdfcontenttreebuilder.h"
#include "pdfexecutionpolicy.h"
#include "pdffont.h"
#include "pdfoperationcontrol.h"
#include "pdfthreadannotations.h"

#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace pdf
{

/// The cancel flag of the builds of the workers of one session (set by the destructor of the
/// session). Thread-safe: one atomic flag, which only ever changes from false to true.
class PDFContentEditSessionControl : public PDFOperationControl
{
public:
    PDFContentEditSessionControl() = default;

    virtual bool isOperationCanceled() const override { return m_isCanceled.load(); }

    void cancel() { m_isCanceled.store(true); }

private:
    std::atomic_bool m_isCanceled = false;
};

/// What the session and its workers share. Ownership: std::shared_ptr of the session and of every
/// running task. Everything except the control is guarded by the mutex.
class PDFContentEditSessionShared
{
public:
    QMutex mutex;
    PDFContentEditSession* session = nullptr;                                   ///< Cleared by the destructor of the session
    std::map<quint64, PDFContentBuildResult> containerResults;                  ///< Request -> result, until the session takes it
    std::map<quint64, PDFContentStructureModelPointer> structureResults;        ///< Request -> model (nullptr: canceled)
    PDFContentEditSessionControl control;
};

/// The caches of the builds of one session. Ownership: std::shared_ptr of the session and of
/// every running task. The font cache locks itself; its shrinking is switched off (a font may be
/// deleted from a shrinking cache while another thread uses the cache), so it holds every font
/// the loaded contents use - the containers co-own the same fonts anyway.
class PDFContentEditSessionCaches
{
public:
    static constexpr size_t FONT_CACHE_LIMIT = 256;
    static constexpr size_t REALIZED_FONT_CACHE_LIMIT = 1024;

    explicit PDFContentEditSessionCaches() :
        fontCache(FONT_CACHE_LIMIT, REALIZED_FONT_CACHE_LIMIT)
    {
        fontCache.setCacheShrinkEnabled(this, false);
    }

    PDFFontCache fontCache;
};

/// The state of a session.
///
/// Ownership: owned by its session and destroyed with it; it co-owns the document, the color
/// management, the caches and the data shared with the workers (shared pointers) and owns the
/// undo stack, whose commands die with it.
class PDFContentEditSession::Impl
{
public:
    /// What the session knows about a form or a cell of a tiling pattern from the loaded
    /// content, which names it first (the builder needs it)
    struct UseInfo
    {
        PDFInteger pageIndex = -1;                  ///< The page of the content, which names it first
        PDFContentResolvedStatePointer state;       ///< The state of the place of the first use of a form (nullptr: the default state)
        int depth = 0;                              ///< Nesting: 1 for a form of a page
    };

    /// A request of a container, whose answer comes from the event loop
    struct Request
    {
        PDFContentContainerKey key;
        bool isBuilding = false;    ///< A worker builds the container
        QString errorText;          ///< No worker: the reason of the failure (empty: the container is loaded)
    };

    explicit Impl(PDFDocumentPointer sessionDocument, PDFCMSPointer sessionCms) :
        document(std::move(sessionDocument)),
        cms(std::move(sessionCms)),
        caches(std::make_shared<PDFContentEditSessionCaches>()),
        shared(std::make_shared<PDFContentEditSessionShared>())
    {
        if (!cms)
        {
            cms = PDFCMSPointer(new PDFCMSGeneric());
        }

        if (document)
        {
            caches->fontCache.setDocument(PDFModifiedDocument(document, nullptr));
        }
    }

    /// Builds a container (the session thread and the workers): reentrant, reads its arguments only
    static PDFContentBuildResult build(const PDFDocument* buildDocument,
                                       const PDFCMS* buildCms,
                                       const PDFFontCache* fontCache,
                                       const PDFContentContainerKey& key,
                                       const PDFContentResolvedStatePointer& initialState,
                                       PDFInteger pageIndex,
                                       const PDFOperationControl* control)
    {
        // The profile cache lives for one build: the container co-owns what it takes from it
        PDFICCProfileCache iccProfileCache;
        iccProfileCache.setDocument(buildDocument);

        PDFContentBuildContext context;
        context.fontCache = fontCache;
        context.iccProfileCache = &iccProfileCache;
        context.cms = buildCms;
        context.pageIndex = pageIndex;

        return PDFContentTreeBuilder::build(buildDocument, key, initialState, context, PDFContentBuildOptions(), control, nullptr);
    }

    /// The reason of a missing container
    static QString getFailureText(const PDFContentBuildResult& result)
    {
        if (result.isCanceled)
        {
            return PDFContentEditSession::tr("The loading of the content was canceled.");
        }

        for (const PDFContentIssue& issue : result.issues)
        {
            if (issue.kind == PDFContentIssue::Kind::InvalidContainer && !issue.text.isEmpty())
            {
                return issue.text;
            }
        }

        return PDFContentEditSession::tr("The content cannot be loaded.");
    }

    /// The form XObjects the nodes of a container use, each once, in the order of the tree, with
    /// the node of the first use. One walk without recursion (the depth of a tree is bounded by
    /// PDFContentContainer::MAXIMUM_DEPTH, the walk does not rely on it).
    static std::vector<std::pair<PDFContentContainerKey, PDFContentNodePointer>> getFormUses(const PDFContentContainer& container)
    {
        std::vector<std::pair<PDFContentContainerKey, PDFContentNodePointer>> result;
        std::set<PDFObjectReference> visited;

        std::vector<PDFContentNodePointer> stack;
        if (container.getRoot())
        {
            stack.push_back(container.getRoot());
        }

        while (!stack.empty())
        {
            const PDFContentNodePointer node = std::move(stack.back());
            stack.pop_back();

            if (node->getKind() == PDFContentNodeKind::FormUse)
            {
                const PDFObjectReference reference = node->getData().xobject;
                if (reference.isValid() && visited.insert(reference).second)
                {
                    result.emplace_back(PDFContentContainerKey::createForm(reference), node);
                }
            }

            const std::vector<PDFContentNodePointer>& children = node->getChildren();
            for (auto it = children.rbegin(); it != children.rend(); ++it)
            {
                stack.push_back(*it);
            }
        }

        return result;
    }

    /// The page and the nesting of a loaded container, or of a container the loaded ones name
    bool getPlace(const PDFContentContainerKey& key, PDFInteger* pageIndex, int* depth) const
    {
        if (key.kind == PDFContentContainerKey::Kind::Page)
        {
            *pageIndex = key.pageIndex;
            *depth = 0;
            return true;
        }

        auto it = uses.find(key);
        if (it == uses.end())
        {
            return false;
        }

        *pageIndex = it->second.pageIndex;
        *depth = it->second.depth;
        return true;
    }

    /// Remembers the forms and the cells of patterns a loaded container names (each key once
    /// for the whole session: the first content, which names it, gives its page and state)
    void registerUses(const PDFContentContainer& container, const PDFContentBuildResult& result)
    {
        PDFInteger pageIndex = -1;
        int depth = 0;
        if (!getPlace(container.getKey(), &pageIndex, &depth))
        {
            return;
        }

        // The node of the first use of each form, looked up by its key: a content may draw
        // a hundred thousand forms, so no search over all of them for each one
        std::map<PDFContentContainerKey, PDFContentNodePointer> formUses;
        for (auto& use : getFormUses(container))
        {
            formUses.emplace(use.first, std::move(use.second));
        }

        for (const PDFContentContainerKey& formKey : result.usedForms)
        {
            if (!formKey.isValid() || uses.count(formKey))
            {
                continue;
            }

            UseInfo info;
            info.pageIndex = pageIndex;
            info.depth = depth + 1;

            // A form used only inside of an opaque span has no node: it starts in the default state
            auto it = formUses.find(formKey);
            if (it != formUses.cend())
            {
                info.state = it->second->getState();
            }

            uses[formKey] = std::move(info);
        }

        for (const PDFContentContainerKey& patternKey : result.usedPatternCells)
        {
            if (!patternKey.isValid() || uses.count(patternKey))
            {
                continue;
            }

            UseInfo info;
            info.pageIndex = pageIndex;
            info.depth = depth + 1;
            uses[patternKey] = std::move(info);
        }
    }

    /// What a build of a container needs; false with the reason for a key, which cannot be built
    bool getBuildInput(const PDFContentContainerKey& key, PDFContentResolvedStatePointer* initialState, PDFInteger* pageIndex, QString* errorText) const
    {
        *initialState = nullptr;
        *pageIndex = -1;

        if (!document)
        {
            *errorText = PDFContentEditSession::tr("The session has no document.");
            return false;
        }

        if (!key.isValid())
        {
            *errorText = PDFContentEditSession::tr("The content to load is not valid.");
            return false;
        }

        if (key.kind == PDFContentContainerKey::Kind::Page)
        {
            return true;
        }

        auto it = uses.find(key);
        if (it == uses.end())
        {
            *errorText = PDFContentEditSession::tr("The form or the pattern is not used by a loaded content: load the page, which uses it, first.");
            return false;
        }

        if (it->second.depth > PDFContentEditSession::MAXIMUM_CONTAINER_NESTING)
        {
            *errorText = PDFContentEditSession::tr("The forms are nested too deep.");
            return false;
        }

        *initialState = it->second.state;
        *pageIndex = it->second.pageIndex;
        return true;
    }

    /// The containers, which differ from the content of the document
    std::set<PDFContentContainerKey> getChangedKeys() const
    {
        std::set<PDFContentContainerKey> result;
        for (const auto& item : containers)
        {
            if (item.second && !item.second->isReadOnly() && !item.second->isUnchanged())
            {
                result.insert(item.first);
            }
        }
        return result;
    }

    /// The pages with a changed container of their own, and the loaded pages, which use a changed
    /// form or pattern (through the loaded containers only).
    ///
    /// One walk for all the pages: every loaded container is read once for the forms and the
    /// patterns it uses, then the walk goes from the changed forms and patterns against the uses
    /// (who uses it, who uses that one ...) with one visited set and the depth limit of the
    /// session - a form shared by a thousand pages is walked once, a form using itself ends
    /// the walk.
    std::vector<PDFInteger> getDirtyPages(const std::set<PDFContentContainerKey>& changedKeys) const
    {
        std::set<PDFInteger> dirtyPages;
        std::vector<std::pair<PDFContentContainerKey, int>> queue;

        for (const PDFContentContainerKey& key : changedKeys)
        {
            if (key.kind == PDFContentContainerKey::Kind::Page)
            {
                dirtyPages.insert(key.pageIndex);
            }
            else
            {
                queue.emplace_back(key, 0);
            }
        }

        if (!queue.empty())
        {
            // Used container -> the loaded containers, which use it
            std::map<PDFContentContainerKey, std::vector<PDFContentContainerKey>> users;
            for (const auto& item : containers)
            {
                if (!item.second)
                {
                    continue;
                }

                for (const auto& use : getFormUses(*item.second))
                {
                    users[use.first].push_back(item.first);
                }

                auto itPatterns = usedPatterns.find(item.first);
                if (itPatterns != usedPatterns.end())
                {
                    for (const PDFContentContainerKey& patternKey : itPatterns->second)
                    {
                        users[patternKey].push_back(item.first);
                    }
                }
            }

            // Breadth first, so the depth of a container is its shortest way to a changed one
            std::set<PDFContentContainerKey> visited;
            for (const auto& item : queue)
            {
                visited.insert(item.first);
            }

            for (size_t i = 0; i < queue.size(); ++i)
            {
                const std::pair<PDFContentContainerKey, int> current = queue[i];
                if (current.second >= PDFContentEditSession::MAXIMUM_CONTAINER_NESTING)
                {
                    continue;
                }

                auto itUsers = users.find(current.first);
                if (itUsers == users.end())
                {
                    continue;
                }

                for (const PDFContentContainerKey& userKey : itUsers->second)
                {
                    if (!visited.insert(userKey).second)
                    {
                        continue;
                    }

                    if (userKey.kind == PDFContentContainerKey::Kind::Page)
                    {
                        // A page is used by nothing
                        dirtyPages.insert(userKey.pageIndex);
                    }
                    else
                    {
                        queue.emplace_back(userKey, current.second + 1);
                    }
                }
            }
        }

        return std::vector<PDFInteger>(dirtyPages.cbegin(), dirtyPages.cend());
    }

    /// Keeps a built container (unless the key has one already: a loaded container may hold
    /// work, it is never replaced by a build)
    void storeContainer(const PDFContentBuildResult& result)
    {
        if (!result.container)
        {
            return;
        }

        const PDFContentContainerKey key = result.container->getKey();
        if (containers.count(key))
        {
            return;
        }

        containers[key] = result.container;
        usedPatterns[key] = result.usedPatternCells;
        registerUses(*result.container, result);
    }

    /// Adds the change of one command to the change of a step
    static void addChange(PDFContentChange* target, const PDFContentChange& change);

    /// Tells a change of the model, when the model is in its new state: modelChanged, then the
    /// request of a selection. The last thing a function of the session does: a slot may delete
    /// the session, so nothing of it is touched after an emission without a check.
    /// \param session The session
    /// \param change The change
    /// \param isStep Undo or redo: the changed objects of the step are selected too
    static void emitModelChanged(PDFContentEditSession* session, const PDFContentChange& change, bool isStep);

    PDFDocumentPointer document;
    PDFCMSPointer cms;
    PDFContentUndoStack undoStack;
    std::shared_ptr<PDFContentEditSessionCaches> caches;
    std::shared_ptr<PDFContentEditSessionShared> shared;

    std::map<PDFContentContainerKey, PDFContentContainerPointer> containers;            ///< The current versions of the loaded containers
    std::map<PDFContentContainerKey, UseInfo> uses;                                     ///< Forms and cells of patterns named by the loaded containers
    std::map<PDFContentContainerKey, std::vector<PDFContentContainerKey>> usedPatterns; ///< Container -> the tiling patterns it used, when it was loaded
    PDFContentStructureModelPointer structure;

    std::map<quint64, Request> requests;                        ///< The requests waiting for their answer
    std::map<PDFContentContainerKey, quint64> buildingKeys;     ///< Key -> the request, whose worker builds it
    quint64 structureRequest = 0;                               ///< The request of the structure, 0 = none
    quint64 nextRequestId = 1;

    quint64 nodeIdCounter = PDFContentEditSession::FIRST_SESSION_NODE_ID;
    PDFInteger nextObjectNumber = 1;
    std::map<PDFObjectReference, PDFObject> newObjects;
    quint64 snapshotRevision = 0;

    std::set<PDFContentNodeReference> lockedNodes;
    std::set<PDFContentNodeReference> hiddenNodes;
    std::map<QString, PDFContentNodeReference> scriptNames;

    // ---- a change in progress ----

    bool isChanging = false;    ///< A command is being applied or reverted
    bool isEmitting = false;    ///< modelChanged is being emitted

    /// The versions the model had before the command being applied changed them (execute puts
    /// them back, when the command is refused)
    std::vector<std::pair<PDFContentContainerKey, PDFContentContainerPointer>> containerJournal;
    PDFContentStructureModelPointer structureBefore;
    bool isStructureJournaled = false;
    quint64 nodeIdCounterBefore = 0;
};

void PDFContentEditSession::Impl::addChange(PDFContentChange* target, const PDFContentChange& change)
{
    for (const PDFContentContainerKey& key : change.containers)
    {
        if (std::find(target->containers.cbegin(), target->containers.cend(), key) == target->containers.cend())
        {
            target->containers.push_back(key);
        }
    }

    target->addedNodes.insert(target->addedNodes.end(), change.addedNodes.cbegin(), change.addedNodes.cend());
    target->removedNodes.insert(target->removedNodes.end(), change.removedNodes.cbegin(), change.removedNodes.cend());
    target->changedNodes.insert(target->changedNodes.end(), change.changedNodes.cbegin(), change.changedNodes.cend());
    target->isLayersChanged = target->isLayersChanged || change.isLayersChanged;
    target->isStructureChanged = target->isStructureChanged || change.isStructureChanged;
}

void PDFContentEditSession::Impl::emitModelChanged(PDFContentEditSession* session, const PDFContentChange& change, bool isStep)
{
    Impl* impl = session->m_impl.get();

    // The nodes the views are asked to select: the objects a command added (the copies after
    // Duplicate, a new group); after undo and redo the objects of the step, which exist now.
    // State runs are no objects.
    QList<PDFContentNodeReference> selection;
    std::set<PDFContentNodeReference> selected;

    auto addToSelection = [impl, &selection, &selected](const std::vector<PDFContentNodeReference>& nodes)
    {
        for (const PDFContentNodeReference& reference : nodes)
        {
            auto it = impl->containers.find(reference.container);
            if (it == impl->containers.end() || !it->second)
            {
                continue;
            }

            const PDFContentNodePointer node = it->second->getNode(reference.node);
            if (node && node->getKind() != PDFContentNodeKind::StateRun && selected.insert(reference).second)
            {
                selection.push_back(reference);
            }
        }
    };

    addToSelection(change.addedNodes);
    if (isStep)
    {
        addToSelection(change.changedNodes);
    }

    // No command runs while the change is being told (isExecuting)
    impl->isChanging = false;
    impl->isEmitting = true;

    QPointer<PDFContentEditSession> self(session);
    Q_EMIT session->modelChanged(change);

    if (!self)
    {
        // A slot has deleted the session
        return;
    }

    impl->isEmitting = false;

    if (!selection.isEmpty())
    {
        // Emitted last: a slot may delete the session
        Q_EMIT session->selectionChangeRequested(selection);
    }
}

PDFContentEditSession::PDFContentEditSession(PDFDocumentPointer document, PDFCMSPointer cms, QObject* parent) :
    QObject(parent),
    m_impl(std::make_unique<Impl>(std::move(document), std::move(cms)))
{
    QMutexLocker locker(&m_impl->shared->mutex);
    m_impl->shared->session = this;
}

PDFContentEditSession::~PDFContentEditSession()
{
    // The running builds are canceled and not waited for. No task posts to this object any more:
    // the pointer is cleared under the mutex a task holds while it posts.
    m_impl->shared->control.cancel();

    QMutexLocker locker(&m_impl->shared->mutex);
    m_impl->shared->session = nullptr;
}

const PDFDocument* PDFContentEditSession::getDocument() const
{
    return m_impl->document.data();
}

PDFContentContainerPointer PDFContentEditSession::getContainer(const PDFContentContainerKey& key) const
{
    auto it = m_impl->containers.find(key);
    return it != m_impl->containers.end() ? it->second : PDFContentContainerPointer();
}

PDFContentContainerKey PDFContentEditSession::getPageKey(PDFInteger pageIndex) const
{
    const PDFDocument* document = m_impl->document.data();
    if (!document || pageIndex < 0 || pageIndex >= PDFInteger(document->getCatalog()->getPageCount()))
    {
        return PDFContentContainerKey();
    }

    return PDFContentContainerKey::createPage(pageIndex, document->getCatalog()->getPage(size_t(pageIndex))->getPageReference());
}

void PDFContentEditSession::requestContainer(const PDFContentContainerKey& key)
{
    Impl* impl = m_impl.get();

    if (impl->buildingKeys.count(key))
    {
        // A worker builds it already: its one answer is the answer of this request too
        return;
    }

    const quint64 requestId = impl->nextRequestId++;

    Impl::Request request;
    request.key = key;

    PDFContentResolvedStatePointer initialState;
    PDFInteger pageIndex = -1;

    if (!impl->containers.count(key) && impl->getBuildInput(key, &initialState, &pageIndex, &request.errorText))
    {
        request.isBuilding = true;
        impl->requests[requestId] = std::move(request);
        impl->buildingKeys[key] = requestId;

        // The task co-owns everything it reads; it never touches the session. A lambda: the
        // body of a task of a thread pool; its "context object" is the shared data, whose
        // pointer to the session is cleared by the destructor of the session.
        auto task = [shared = impl->shared, caches = impl->caches, document = impl->document, cms = impl->cms, key, initialState, pageIndex, requestId]() mutable
        {
            PDFContentBuildResult result = Impl::build(document.data(), cms.data(), &caches->fontCache, key, initialState, pageIndex, &shared->control);

            {
                QMutexLocker locker(&shared->mutex);
                if (shared->session)
                {
                    shared->containerResults[requestId] = std::move(result);

                    // Posted event: the session cannot be destroyed while the mutex is held,
                    // a destroyed session drops its posted events
                    PDFThreadAnnotations::happensBefore(shared.get());
                    PDFThreadAnnotations::TransportScope transportScope;
                    QMetaObject::invokeMethod(shared->session, "onContainerBuilt", Qt::QueuedConnection, Q_ARG(quint64, requestId));
                }
            }

            // The result (when nobody took it) and the co-owned data die before the task ends
            result = PDFContentBuildResult();
            initialState.reset();
            document.clear();
            cms.clear();
            caches.reset();
            shared.reset();
        };

        PDFExecutionPolicy::startTask(QThreadPool::globalInstance(), std::move(task));
        return;
    }

    // No worker is needed: the container is loaded (no error text), or it cannot be built. The
    // answer still comes from the event loop.
    impl->requests[requestId] = std::move(request);
    QMetaObject::invokeMethod(this, "onContainerBuilt", Qt::QueuedConnection, Q_ARG(quint64, requestId));
}

bool PDFContentEditSession::loadContainer(const PDFContentContainerKey& key, const PDFOperationControl* control, QString* errorText)
{
    Impl* impl = m_impl.get();

    if (impl->containers.count(key))
    {
        return true;
    }

    PDFContentResolvedStatePointer initialState;
    PDFInteger pageIndex = -1;
    QString text;

    if (impl->getBuildInput(key, &initialState, &pageIndex, &text))
    {
        const PDFContentBuildResult result = Impl::build(impl->document.data(), impl->cms.data(), &impl->caches->fontCache, key, initialState, pageIndex, control);
        if (result.container)
        {
            impl->storeContainer(result);
            return true;
        }

        text = Impl::getFailureText(result);
    }

    if (errorText)
    {
        *errorText = text;
    }
    return false;
}

bool PDFContentEditSession::execute(const PDFContentCommandPointer& command, QString* errorText)
{
    Impl* impl = m_impl.get();

    if (!command)
    {
        if (errorText)
        {
            *errorText = tr("There is no change to apply.");
        }
        return false;
    }

    if (isExecuting())
    {
        if (errorText)
        {
            *errorText = tr("Another change of the content is in progress.");
        }
        return false;
    }

    impl->isChanging = true;
    impl->containerJournal.clear();
    impl->isStructureJournaled = false;
    impl->structureBefore = nullptr;
    impl->nodeIdCounterBefore = impl->nodeIdCounter;
    const PDFInteger nextObjectNumberBefore = impl->nextObjectNumber;

    QString text;
    const bool isApplied = command->apply(this, &text);

    if (!isApplied)
    {
        // The new objects of a refused command go with it: nothing of the model refers to them
        // (the versions it computed are dropped), and their numbers are given again
        for (PDFInteger objectNumber = nextObjectNumberBefore; objectNumber < impl->nextObjectNumber; ++objectNumber)
        {
            impl->newObjects.erase(PDFObjectReference(objectNumber, NEW_OBJECT_GENERATION));
        }
        impl->nextObjectNumber = nextObjectNumberBefore;

        // A refused command leaves the model unchanged; whatever it has set nevertheless is put back
        for (auto it = impl->containerJournal.rbegin(); it != impl->containerJournal.rend(); ++it)
        {
            if (it->second)
            {
                impl->containers[it->first] = it->second;
            }
            else
            {
                impl->containers.erase(it->first);
            }
        }
        if (impl->isStructureJournaled)
        {
            impl->structure = impl->structureBefore;
        }
        impl->nodeIdCounter = impl->nodeIdCounterBefore;
    }

    impl->containerJournal.clear();
    impl->isStructureJournaled = false;
    impl->structureBefore = nullptr;

    if (!isApplied)
    {
        impl->isChanging = false;

        if (errorText)
        {
            *errorText = !text.isEmpty() ? text : tr("The change was refused.");
        }
        return false;
    }

    impl->undoStack.push(command);

    // The model is in its new state: the signals are the last thing done
    const PDFContentChange change = command->getChange();
    Impl::emitModelChanged(this, change, false);
    return true;
}

std::optional<PDFInteger> PDFContentEditSession::undo()
{
    Impl* impl = m_impl.get();

    if (isExecuting())
    {
        return std::nullopt;
    }

    const PDFContentUndoStack::Step* step = impl->undoStack.getUndoStep();
    if (!step || step->commands.empty())
    {
        return std::nullopt;
    }

    // The commands are co-owned for the time of the change
    const std::vector<PDFContentCommandPointer> commands = step->commands;
    const PDFInteger pageIndex = commands.front()->getPageIndex();

    impl->isChanging = true;

    PDFContentChange change;
    for (auto it = commands.rbegin(); it != commands.rend(); ++it)
    {
        (*it)->revert(this);
        Impl::addChange(&change, (*it)->getChange());
    }

    impl->undoStack.moveToPreviousStep();

    Impl::emitModelChanged(this, change, true);
    return pageIndex;
}

std::optional<PDFInteger> PDFContentEditSession::redo()
{
    Impl* impl = m_impl.get();

    if (isExecuting())
    {
        return std::nullopt;
    }

    const PDFContentUndoStack::Step* step = impl->undoStack.getRedoStep();
    if (!step || step->commands.empty())
    {
        return std::nullopt;
    }

    const std::vector<PDFContentCommandPointer> commands = step->commands;
    const PDFInteger pageIndex = commands.front()->getPageIndex();

    impl->isChanging = true;

    PDFContentChange change;
    for (const PDFContentCommandPointer& command : commands)
    {
        // The model is in the state the command was applied to first, so it cannot refuse
        QString text;
        if (command->apply(this, &text))
        {
            Impl::addChange(&change, command->getChange());
        }
    }

    impl->undoStack.moveToNextStep();

    Impl::emitModelChanged(this, change, true);
    return pageIndex;
}

bool PDFContentEditSession::isExecuting() const
{
    return m_impl->isChanging || m_impl->isEmitting;
}

PDFContentUndoStack* PDFContentEditSession::getUndoStack()
{
    return &m_impl->undoStack;
}

const PDFContentUndoStack* PDFContentEditSession::getUndoStack() const
{
    return &m_impl->undoStack;
}

PDFContentStructureModelPointer PDFContentEditSession::getStructure() const
{
    return m_impl->structure;
}

void PDFContentEditSession::requestStructure()
{
    Impl* impl = m_impl.get();

    if (impl->structureRequest != 0)
    {
        // The one answer of the running request is the answer of this one too
        return;
    }

    const quint64 requestId = impl->nextRequestId++;
    impl->structureRequest = requestId;

    if (impl->structure || !impl->document)
    {
        // Nothing to read: the answer still comes from the event loop
        QMetaObject::invokeMethod(this, "onStructureRead", Qt::QueuedConnection, Q_ARG(quint64, requestId));
        return;
    }

    // See requestContainer: the task co-owns what it reads and never touches the session
    auto task = [shared = impl->shared, document = impl->document, requestId]() mutable
    {
        PDFContentStructureModelPointer model = PDFContentStructureModel::read(document.data(), &shared->control);

        {
            QMutexLocker locker(&shared->mutex);
            if (shared->session)
            {
                shared->structureResults[requestId] = std::move(model);

                PDFThreadAnnotations::happensBefore(shared.get());
                PDFThreadAnnotations::TransportScope transportScope;
                QMetaObject::invokeMethod(shared->session, "onStructureRead", Qt::QueuedConnection, Q_ARG(quint64, requestId));
            }
        }

        model.reset();
        document.clear();
        shared.reset();
    };

    PDFExecutionPolicy::startTask(QThreadPool::globalInstance(), std::move(task));
}

bool PDFContentEditSession::loadStructure(const PDFOperationControl* control)
{
    Impl* impl = m_impl.get();

    if (impl->structure)
    {
        // A loaded model may hold work: it is never replaced by a reading
        return true;
    }

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(impl->document.data(), control);
    if (!model)
    {
        return false;
    }

    impl->structure = std::move(model);
    return true;
}

PDFContentEditSnapshotPointer PDFContentEditSession::createSnapshot()
{
    Impl* impl = m_impl.get();

    std::shared_ptr<PDFContentEditSnapshot> snapshot = std::make_shared<PDFContentEditSnapshot>();
    snapshot->revision = ++impl->snapshotRevision;

    const std::set<PDFContentContainerKey> changedKeys = impl->getChangedKeys();
    for (const PDFContentContainerKey& key : changedKeys)
    {
        snapshot->containers[key] = impl->containers[key];
    }

    if (impl->structure && impl->structure->isModified())
    {
        snapshot->structure = impl->structure;
    }

    snapshot->dirtyPages = impl->getDirtyPages(changedKeys);
    snapshot->newObjects = impl->newObjects;
    return snapshot;
}

std::vector<PDFInteger> PDFContentEditSession::getDirtyPages() const
{
    return m_impl->getDirtyPages(m_impl->getChangedKeys());
}

void PDFContentEditSession::setContainer(const PDFContentContainerPointer& container)
{
    Impl* impl = m_impl.get();

    // Only a command inside of apply or revert changes the model
    Q_ASSERT(impl->isChanging);
    if (!container || !impl->isChanging)
    {
        return;
    }

    const PDFContentContainerKey key = container->getKey();
    auto it = impl->containers.find(key);
    impl->containerJournal.emplace_back(key, it != impl->containers.end() ? it->second : PDFContentContainerPointer());
    impl->containers[key] = container;
}

void PDFContentEditSession::setStructure(const PDFContentStructureModelPointer& structure)
{
    Impl* impl = m_impl.get();

    Q_ASSERT(impl->isChanging);
    if (!impl->isChanging)
    {
        return;
    }

    if (!impl->isStructureJournaled)
    {
        impl->isStructureJournaled = true;
        impl->structureBefore = impl->structure;
    }

    impl->structure = structure;
}

PDFContentNodeId PDFContentEditSession::allocateNodeId()
{
    return PDFContentNodeId{ m_impl->nodeIdCounter++ };
}

quint64 PDFContentEditSession::getNodeIdCounter() const
{
    return m_impl->nodeIdCounter;
}

void PDFContentEditSession::setNodeIdCounter(quint64 counter)
{
    // The identifiers of the session never go below their first one (the builder's are below)
    m_impl->nodeIdCounter = qMax(counter, FIRST_SESSION_NODE_ID);
}

PDFObjectReference PDFContentEditSession::addNewObject(const PDFObject& object)
{
    // The reserved generation: no reference of the document, also not one to an object, which
    // does not exist, can be taken for a new object of the session
    const PDFObjectReference reference(m_impl->nextObjectNumber++, NEW_OBJECT_GENERATION);
    m_impl->newObjects[reference] = object;
    return reference;
}

PDFObject PDFContentEditSession::getNewObject(PDFObjectReference reference) const
{
    auto it = m_impl->newObjects.find(reference);
    return it != m_impl->newObjects.end() ? it->second : PDFObject();
}

void PDFContentEditSession::setScriptName(const QString& name, const PDFContentNodeReference& node)
{
    m_impl->scriptNames[name] = node;
}

PDFContentNodeReference PDFContentEditSession::getScriptName(const QString& name) const
{
    auto it = m_impl->scriptNames.find(name);
    return it != m_impl->scriptNames.end() ? it->second : PDFContentNodeReference();
}

void PDFContentEditSession::clearScriptNames()
{
    m_impl->scriptNames.clear();
}

bool PDFContentEditSession::isNodeLocked(const PDFContentNodeReference& node) const
{
    return m_impl->lockedNodes.count(node) > 0;
}

void PDFContentEditSession::setNodeLocked(const PDFContentNodeReference& node, bool locked)
{
    if (!node.isValid())
    {
        return;
    }

    const bool isChanged = locked ? m_impl->lockedNodes.insert(node).second : m_impl->lockedNodes.erase(node) > 0;
    if (isChanged)
    {
        // Emitted last: a slot may delete the session
        Q_EMIT editorStateChanged(node);
    }
}

bool PDFContentEditSession::isNodeHidden(const PDFContentNodeReference& node) const
{
    return m_impl->hiddenNodes.count(node) > 0;
}

void PDFContentEditSession::setNodeHidden(const PDFContentNodeReference& node, bool hidden)
{
    if (!node.isValid())
    {
        return;
    }

    const bool isChanged = hidden ? m_impl->hiddenNodes.insert(node).second : m_impl->hiddenNodes.erase(node) > 0;
    if (isChanged)
    {
        // Emitted last: a slot may delete the session
        Q_EMIT editorStateChanged(node);
    }
}

void PDFContentEditSession::onContainerBuilt(quint64 requestId)
{
    Impl* impl = m_impl.get();

    auto itRequest = impl->requests.find(requestId);
    if (itRequest == impl->requests.end())
    {
        // An outdated answer
        return;
    }

    const Impl::Request request = std::move(itRequest->second);
    impl->requests.erase(itRequest);

    QString errorText = request.errorText;
    std::vector<PDFContentContainerKey> nestedForms;

    if (request.isBuilding)
    {
        impl->buildingKeys.erase(request.key);

        PDFContentBuildResult result;
        {
            QMutexLocker locker(&impl->shared->mutex);
            PDFThreadAnnotations::happensAfter(impl->shared.get());

            auto itResult = impl->shared->containerResults.find(requestId);
            if (itResult != impl->shared->containerResults.end())
            {
                result = std::move(itResult->second);
                impl->shared->containerResults.erase(itResult);
            }
        }

        if (result.container)
        {
            // A container loaded in this thread meanwhile stays (it may hold work)
            impl->storeContainer(result);
            nestedForms = result.usedForms;
        }
        else if (!impl->containers.count(request.key))
        {
            errorText = Impl::getFailureText(result);
        }
    }

    const bool isLoaded = impl->containers.count(request.key) > 0;

    if (isLoaded)
    {
        // The forms of the container are queued too, each once (a form, which is loaded, being
        // built or too deep, is not)
        for (const PDFContentContainerKey& formKey : nestedForms)
        {
            PDFInteger pageIndex = -1;
            int depth = 0;
            if (!impl->containers.count(formKey) && !impl->buildingKeys.count(formKey) &&
                impl->getPlace(formKey, &pageIndex, &depth) && depth <= MAXIMUM_CONTAINER_NESTING)
            {
                requestContainer(formKey);
            }
        }
    }

    // Emitted last: a slot may delete the session
    if (isLoaded)
    {
        Q_EMIT containerLoaded(request.key);
    }
    else
    {
        Q_EMIT containerFailed(request.key, !errorText.isEmpty() ? errorText : tr("The content cannot be loaded."));
    }
}

void PDFContentEditSession::onStructureRead(quint64 requestId)
{
    Impl* impl = m_impl.get();

    if (impl->structureRequest != requestId)
    {
        // An outdated answer
        return;
    }
    impl->structureRequest = 0;

    PDFContentStructureModelPointer model;
    {
        QMutexLocker locker(&impl->shared->mutex);
        PDFThreadAnnotations::happensAfter(impl->shared.get());

        auto itResult = impl->shared->structureResults.find(requestId);
        if (itResult != impl->shared->structureResults.end())
        {
            model = std::move(itResult->second);
            impl->shared->structureResults.erase(itResult);
        }
    }

    // A model loaded in this thread meanwhile stays (it may hold work)
    if (!impl->structure && model)
    {
        impl->structure = std::move(model);
    }

    if (impl->structure)
    {
        // Emitted last: a slot may delete the session
        Q_EMIT structureLoaded();
    }
}

}   // namespace pdf

#include "moc_pdfcontenteditsession.cpp"
