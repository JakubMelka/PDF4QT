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

#ifndef PDFCONTENTEDITSESSION_H
#define PDFCONTENTEDITSESSION_H

#include "pdfcms.h"
#include "pdfcontentcommands.h"
#include "pdfcontentnode.h"
#include "pdfcontentstructure.h"
#include "pdfcontentundostack.h"
#include "pdfdocument.h"

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace pdf
{
class PDFOperationControl;

// Edit Content, stage E0, package P03. Plan: zadani_jadro.md 4.6 (session, snapshot); zadani.md
// 9.3, RE-15, RE-16; zadani_overeni.md 6.1 (queue of containers), 6.3 (threads and ownership).

/// An immutable copy of the work of a session: the changed containers, the structure model and
/// the revision. It is the parameter "session" of the tool (a Value in a QVariant) and the input
/// of PDFContentDocumentWriter.
///
/// Threads and ownership: created in the thread of the session, read in the thread of the run;
/// held by std::shared_ptr<const> - it outlives the GUI and the session (the shell destroys the GUI
/// before the run ends). Everything inside is immutable and co-owned (containers, models), nothing
/// points into the session, into a cache or into the document.
struct PDFContentEditSnapshot
{
    quint64 revision = 0;                                                       ///< Number of the snapshot of its session (grows with every one)
    std::map<PDFContentContainerKey, PDFContentContainerPointer> containers;    ///< Only the changed containers (PDFContentContainer::isUnchanged is false), with their fingerprints
    PDFContentStructureModelPointer structure;                                  ///< nullptr, when the structure was not read or not changed
    std::vector<PDFInteger> dirtyPages;                                         ///< Indices of the pages with a changed container of their own and of the loaded pages using a changed form or pattern, ascending (a page the session never loaded is not here: the document writer computes PDFContentWriteResult::pagesChanged over the whole document)
    std::map<PDFObjectReference, PDFObject> newObjects;                         ///< The new objects of the session by their provisional references (PDFContentEditSession::addNewObject); the document writer adds those the written containers reach

    bool isEmpty() const { return containers.empty() && !structure; }
};

using PDFContentEditSnapshotPointer = std::shared_ptr<const PDFContentEditSnapshot>;

/// The model of one editing of the content of a document: the loaded containers, the structure
/// model, the undo stack and the state of the editor, which is no work (locks, hidden nodes).
///
/// Changes go only through execute, undo and redo. A command must not be executed inside of
/// another command nor while modelChanged is being emitted: execute refuses it (isExecuting).
/// The session never emits in the middle of a change: every signal is emitted last, when the
/// model is in its new state, and a slot may delete the session.
///
/// Threads and ownership: a QObject of one thread - the GUI thread (owned by the GUI of the tool,
/// PDFAppToolGUIContent), or the thread of a run of the tool with a script (a local session
/// of PDFDocumentToolContent::execute, which uses only the synchronous functions). The asynchronous
/// build of a container runs in a worker (PDFExecutionPolicy::startTask, one task per container);
/// the worker gets shared pointers of the document and plain values only, its result comes back
/// through the event queue (PDFThreadAnnotations::TransportScope) and a result of an outdated
/// request is dropped. The session holds the document by a shared pointer.
///
/// A worker never touches the session: it gets copies of plain values and shared pointers of
/// immutable data, and delivers its result by a queued call to the session through a QPointer or
/// a context object, so Qt drops a result for a deleted session. The destructor cancels the
/// running builds and does not wait for them.
///
/// Signals and QVariant values never carry a std::vector of a core type: the unconstrained
/// stream templates of pdfutils.h make QMetaType instantiate QDataStream operators for it, which
/// do not compile for a type without them. Use QList.
class PDF4QTLIBCORESHARED_EXPORT PDFContentEditSession : public QObject
{
    Q_OBJECT

public:
    /// Limits (zadani_overeni.md 6.1): forms inside of forms deeper than this are not loaded
    static constexpr int MAXIMUM_CONTAINER_NESTING = 32;

    /// The first identifier of a node created by a command (allocateNodeId). The identifiers of
    /// the tree builder are below it in every container (PDFContentContainer::
    /// MAXIMUM_OPERATION_COUNT bounds the nodes of a container), so no container loaded later
    /// can collide with a node of the session.
    static constexpr quint64 FIRST_SESSION_NODE_ID = quint64(1) << 32;

    /// The generation of every provisional reference of a new object (addNewObject). It is the
    /// generation of the head of the free list of a cross-reference table (ISO 32000-2, 7.5.4):
    /// no object in use of a document can have it, so a reference of the document - also one to
    /// an object, which does not exist - is never taken for a new object of the session by its
    /// number. The document writer replaces exactly the references with this generation, which
    /// are keys of PDFContentEditSnapshot::newObjects.
    static constexpr PDFInteger NEW_OBJECT_GENERATION = 65535;

    /// \param document The document being edited (held)
    /// \param cms Color management (held; may be nullptr: the generic one is used)
    /// \param parent Parent object
    explicit PDFContentEditSession(PDFDocumentPointer document, PDFCMSPointer cms, QObject* parent);
    virtual ~PDFContentEditSession() override;

    const PDFDocument* getDocument() const;

    /// The current version of a container: nullptr, when it is not loaded
    PDFContentContainerPointer getContainer(const PDFContentContainerKey& key) const;

    /// The key of a page of the document (not valid for an index out of range)
    PDFContentContainerKey getPageKey(PDFInteger pageIndex) const;

    /// Starts the build of a container in a worker; containerLoaded or containerFailed follows
    /// from the event loop (also for a container, which is loaded already). The forms it uses
    /// are queued too, each once (one map key -> container for the whole session: a form using
    /// itself, also through a pattern, is built once), not deeper than MAXIMUM_CONTAINER_NESTING.
    void requestContainer(const PDFContentContainerKey& key);

    /// Builds a container in this thread (the run of the tool; tests).
    /// \returns false with \p errorText for a key, which names nothing, and for a cancellation
    bool loadContainer(const PDFContentContainerKey& key, const PDFOperationControl* control, QString* errorText);

    /// Applies a command and pushes it onto the undo stack; emits modelChanged last.
    /// \returns false with \p errorText, when the command is refused (also inside of another
    ///          command or of an emission of modelChanged); the model is unchanged then, and
    ///          PDFContentCommand::isRefusedAsUnchanged of the command tells, whether it was
    ///          refused only because it changes nothing
    bool execute(const PDFContentCommandPointer& command, QString* errorText);

    /// Reverts / applies again one step of the undo stack; emits modelChanged last.
    /// \returns no value, when there is nothing to do; otherwise the page of the step
    ///          (PDFContentCommand::getPageIndex of its first command), -1 for the document
    std::optional<PDFInteger> undo();
    std::optional<PDFInteger> redo();

    /// A command is being applied or modelChanged is being emitted
    bool isExecuting() const;

    PDFContentUndoStack* getUndoStack();
    const PDFContentUndoStack* getUndoStack() const;

    /// The structure model: nullptr, until it was read (requestStructure, loadStructure)
    PDFContentStructureModelPointer getStructure() const;

    /// Reads the structure model in a worker (structureLoaded follows) / in this thread
    void requestStructure();
    bool loadStructure(const PDFOperationControl* control);

    /// An immutable copy of the work for a run of the tool and for the preview
    PDFContentEditSnapshotPointer createSnapshot();

    /// Indices of the pages with unapplied changes, ascending
    std::vector<PDFInteger> getDirtyPages() const;

    // ---- for commands only (inside of apply / revert) ----

    /// Replaces the version of a container
    void setContainer(const PDFContentContainerPointer& container);

    /// Replaces the structure model
    void setStructure(const PDFContentStructureModelPointer& structure);

    /// A new identifier of a node: the value of the counter, which starts at
    /// FIRST_SESSION_NODE_ID and grows by one. The loaded containers never move the counter (a
    /// container loaded between undo and redo must not change the identifiers of redo). The
    /// counter is a part of the undo state: a command remembers the value before its first apply
    /// and restores it in revert (setNodeIdCounter), so apply after revert gives the same
    /// identifiers.
    PDFContentNodeId allocateNodeId();
    quint64 getNodeIdCounter() const;
    void setNodeIdCounter(quint64 counter);

    /// Keeps a new object, which must be an indirect object of the written document (the copy
    /// of a tiling pattern for R-5; later images, fonts, forms), and returns its provisional
    /// reference: object numbers counted from 1 with the generation NEW_OBJECT_GENERATION, unique
    /// for the life of the session. Undo does not remove the object: an object nothing refers to
    /// is simply not written. The one exception: the objects a refused command has added are
    /// dropped with it by execute (nothing refers to them), and their numbers are given again.
    /// The values of an override, the resources and other new objects may hold provisional
    /// references; the document writer replaces them by the real ones.
    PDFObjectReference addNewObject(const PDFObject& object);

    /// The new object of a provisional reference; a null object for any other reference
    PDFObject getNewObject(PDFObjectReference reference) const;

    // ---- state of the editor outside of undo (zadani.md RE-16, QE.8: until the tool closes) ----

    bool isNodeLocked(const PDFContentNodeReference& node) const;
    void setNodeLocked(const PDFContentNodeReference& node, bool locked);
    bool isNodeHidden(const PDFContentNodeReference& node) const;
    void setNodeHidden(const PDFContentNodeReference& node, bool hidden);

    // ---- state of a replay of a script (outside of undo; PDFContentJson::KEY_AS) ----

    /// Names a node for the later operations of the replayed script (the first added node of the
    /// executed command); a name used again gets the new node
    void setScriptName(const QString& name, const PDFContentNodeReference& node);

    /// The node of a temporary name; a reference, which is not valid, for an unknown name
    PDFContentNodeReference getScriptName(const QString& name) const;

    /// Forgets every temporary name (the beginning and the end of a replay)
    void clearScriptNames();

Q_SIGNALS:
    /// A requested container is loaded (getContainer gives it)
    void containerLoaded(const pdf::PDFContentContainerKey& key);

    /// A requested container cannot be loaded
    void containerFailed(const pdf::PDFContentContainerKey& key, const QString& errorText);

    /// The structure model was read
    void structureLoaded();

    /// The model changed by a command, by undo or by redo
    void modelChanged(const pdf::PDFContentChange& change);

    /// A lock or a "hidden" flag of a node changed (no work, no undo)
    void editorStateChanged(const pdf::PDFContentNodeReference& node);

    /// The model asks the views to select nodes (the copies after Duplicate, the nodes of a
    /// step after undo)
    void selectionChangeRequested(const QList<pdf::PDFContentNodeReference>& nodes);

private Q_SLOTS:
    /// A build of a worker has ended (queued from the worker)
    void onContainerBuilt(quint64 requestId);

    /// The structure of a worker was read (queued from the worker)
    void onStructureRead(quint64 requestId);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFContentEditSnapshotPointer)

#endif // PDFCONTENTEDITSESSION_H
