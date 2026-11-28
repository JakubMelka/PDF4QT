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

#ifndef PDFAPPEDITCONTENTPANELS_OBJECTS_H
#define PDFAPPEDITCONTENTPANELS_OBJECTS_H

#include "pdfappglobal.h"
#include "pdfcontentcommands.h"
#include "pdfdeferreddeletion.h"

#include <QAbstractItemModel>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class QTimer;
class QToolButton;
class QItemSelection;

namespace pdf
{

class PDFAppEditContentContext;
class PDFAppEditContentRegistry;
class PDFAppHeaderText;
class PDFAppParagraph;
class PDFContentEditSession;
class PDFDocument;
class PDFLineEdit;
class PDFTreeView;

// Edit Content, stage E1, package P12 (tabs Objects and Pages). Plan: zadani_gui.md 4.18
// (Objects), 4.23 (the tree is the accessible list of the content), 4.24 (complex page).

/// The tree of the objects of the edited root (the tab Objects; the state object of its tab
/// entry, PDFAppEditContentContext::getTabState(PDFAppEditContentTabEntry::TAB_OBJECTS)).
///
/// - The rows are the children of PDFAppEditContentContext::getEditedRoot in the order of the
///   content stream (the first row is painted first). Mapping (zadani_gui.md 4.18): a StateRun
///   has no row; a Group with a clipping path is a Clipping group; a text or a path with a
///   clipping effect has the clipping badge; a MarkedContent with a layer is Layer content, with
///   an MCID Tagged content; the TextShow nodes of a TextObject have no rows.
/// - The model follows the context (pageChanged, containerChanged) and the session
///   (containerLoaded, modelChanged, editorStateChanged) by PDFDeferredDeletion::connect and
///   changes its rows from the event loop, never inside of a signal of a view.
/// - The model never changes the content itself: setData of the toggle roles sets the lock /
///   hidden state of the session (no command); dropMimeData only queues a Reorder command
///   (PDFAppEditContentContext::requestExecute) and **returns false**, so the view moves no row -
///   the rows change when modelChanged comes.
/// - It holds node identifiers (the internal id of an index is a number of the model), never
///   pointers to nodes.
///
/// Rows of P12 beyond the mapping above (frames edit-content-v2, -objects-nested): a tagged
/// MarkedContent (an MCID), whose only object is one node, has no row of its own - the node is
/// the row and shows the tag of the sequence as its badge ("P", "H1"); the node in the tree is
/// then the "place" of the row (getPlaceId). A MarkedPoint is Marked content. Rows inside of a
/// TextObject are none (the text is one row).
///
/// Updates: a command, an undo, a redo changes the rows incrementally (rows removed, inserted
/// and moved, dataChanged of the changed rows; subtrees, whose nodes did not change, are not
/// compared); more than LARGE_CHANGE rows removed, inserted or moved, another page, another
/// edited root and a change of the filter reset the model; more than LARGE_CHANGE changed rows
/// (a Transform of many objects) are told by one dataChanged of the range of each parent. The
/// lock and the hidden flag (editorStateChanged, one per node) are one dataChanged of the row
/// each, up to LARGE_CHANGE rows in one turn of the event loop, then one dataChanged of all top
/// rows from the event loop. The kind and the description of a row are computed once per node
/// object (a node, which a command did not change, keeps them over versions). After
/// PDFAppEditContentContext::sessionReplaced the model is connected to the new session.
/// Bounds (hostile input): the walk over the tree visits every node once and stops at
/// PDFContentContainer::MAXIMUM_DEPTH + 2 levels; the text of a row is read up to
/// MAXIMUM_TEXT_LENGTH characters, its lines are counted over all its glyphs once per node.
///
/// Threads and ownership: a child of the context (it lives as long as the tool), GUI thread. The
/// panels (parts) hold it by a QPointer.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentObjectsModel : public QAbstractItemModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractItemModel;

public:
    /// Roles besides those of PDFComponentRoles the delegate of the row reads
    enum Role
    {
        NodeIdRole = Qt::UserRole + 4000,   ///< quint64: PDFContentNodeId::value
        KindTextRole,                       ///< QString: translated kind ("Text", "Path", "Clipping group", ...)
        DescriptionRole,                    ///< QString: the text of a text, the size of an image, ...
        BadgeRole,                          ///< QString: the tag of the structure ("H1"), or "New" for content created in this session
        IsNewRole,                          ///< bool
        IsLockedRole,                       ///< bool; setData toggles it (PDFContentEditSession::setNodeLocked)
        IsHiddenRole,                       ///< bool; setData toggles it (PDFContentEditSession::setNodeHidden)
        IsClippingRole,                     ///< bool: the node sets a clipping path
        IsSelectableRole                    ///< bool: false for Unknown content (Opaque)
    };

    /// Kinds of rows a filter may keep (zadani_gui.md 4.18: the filter button of the header)
    enum class FilterKind
    {
        Text = 0x01,
        Path = 0x02,
        Image = 0x04,
        Shading = 0x08,
        Form = 0x10,
        Group = 0x20,
        Other = 0x40
    };
    Q_DECLARE_FLAGS(FilterKinds, FilterKind)

    /// A command, which changes more rows than this, resets the model (the view would take
    /// longer for the single signals than for a reset)
    static constexpr int LARGE_CHANGE = 200;

    /// The text of a text row is read up to this number of characters
    static constexpr int MAXIMUM_TEXT_LENGTH = 256;

    /// The MIME type of dragged rows (the container key, its version and the node identifiers)
    static constexpr const char* MIME_TYPE = "application/x-pdf4qt-content-rows";

    explicit PDFAppEditContentObjectsModel(PDFAppEditContentContext* context, QObject* parent);
    virtual ~PDFAppEditContentObjectsModel() override;

    virtual QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    virtual QModelIndex parent(const QModelIndex& child) const override;
    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    virtual bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
    virtual Qt::DropActions supportedDropActions() const override;
    virtual QStringList mimeTypes() const override;
    virtual QMimeData* mimeData(const QModelIndexList& indexes) const override;
    virtual bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) const override;
    virtual bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;

    /// The node of the row (not valid for an index, which is not valid) and the row of a node
    /// (not valid, when the node has no row: filtered out, a state run, another container)
    PDFContentNodeReference getNode(const QModelIndex& index) const;
    QModelIndex getIndex(const PDFContentNodeReference& node) const;

    /// The filter of the rows: a text contained in the kind or in the description (case
    /// insensitive; empty = none) and the kinds kept (all by default). A group stays, when a row
    /// inside of it stays.
    void setFilter(const QString& text, FilterKinds kinds);
    const QString& getFilterText() const { return m_filterText; }
    FilterKinds getFilterKinds() const { return m_filterKinds; }
    bool isFiltered() const;

    /// The counts of the header "Page 3 - 20 objects - 1 new": every row without the filter,
    /// the clipping paths and the children of collapsed groups included
    int getObjectCount() const;
    int getNewObjectCount() const;

    // ---- P12's own functions (the panel, the menus and the tests of P12)

    /// The rows, which match the filter themselves (the header "2 of 41,820 objects match");
    /// 0 without a filter
    int getMatchCount() const { return m_matchCount; }

    /// The container the rows show (nullptr before the first one)
    const PDFContentContainerPointer& getContainer() const { return m_container; }

    /// The expanded rows of the tree by their nodes: the panel (a part) restores them after it was
    /// built again and after a reset of the model
    bool isNodeExpanded(const PDFContentNodeReference& node) const { return m_expanded.count(node) > 0; }
    void setNodeExpanded(const PDFContentNodeReference& node, bool isExpanded);

    /// The filter field of the panel is shown (the filter button of the header)
    bool isFilterShown() const { return m_isFilterShown; }
    void setFilterShown(bool isShown) { m_isFilterShown = isShown; }

    /// The node, which has the row of the child \p child of a node: nullptr for a StateRun and a
    /// TextShow (no row), the only object of a tagged MarkedContent (the tagged sequence of one
    /// object has no row of its own), else the child itself
    static const PDFContentNode* getRowNodeOfChild(const PDFContentNode& child);

    /// The node, which stands for the row of \p nodeId among the children of its parent in the
    /// tree: the tagged MarkedContent of a node without a row of its own, else the node itself
    static PDFContentNodeId getPlaceId(const PDFContentContainer& container, PDFContentNodeId nodeId);

    /// Rows of the node are the rows of its children (a Group, a MarkedContent; the Root)
    static bool hasChildRows(const PDFContentNode& node);

    /// The translated kind of a row node ("Clipping group") and its description; the document
    /// is read for the name of a layer, the size of an image and a transparency group (nullptr:
    /// none of them)
    static QString getKindText(const PDFContentNode& node, const PDFDocument* document);
    static QString getDescription(const PDFContentNode& node, const PDFDocument* document);

    // ---- One source of truth of the kind and of the counts, which the row and the card
    //      Selection both show (R7-10)

    /// A Group, one of whose children (state runs aside) sets a clipping path
    /// (PDFContentNode::hasClipEffect: a clipping path, a path with W / W*, a text of the
    /// rendering mode 4 - 7); the clip holds for what follows it up to the Q
    static bool isClippingGroup(const PDFContentNode& node);

    /// A MarkedContent of optional content: /OC BDC (ISO 32000-2, 14.11.3.2), also when its
    /// OCG cannot be resolved
    static bool isLayerContent(const PDFContentNode& node);

    /// The drawn segments of a Path (ISO 32000-2, 8.5.2): every l / c / v / y and every h, which
    /// draws a line back to the start of its subpath (an h at the start point draws nothing); m
    /// draws nothing. A closed triangle "m l l h" = 3, a rectangle "re" = 4. 0 for another kind.
    static int getSegmentCount(const PDFContentNode& node);

    /// The lines of a text object by the rule of the core (PDFContentDisplay::getText,
    /// splitLines): the next glyph is on the line of the previous one, when its text space is
    /// the text space of the previous one moved along the writing direction (|dy| <= 0.001 |Tfs|
    /// for a horizontal font, |dx| for a vertical one, no rotation); computed from the text items
    /// without laying out the glyphs. 0 for another kind or a text without glyphs. Cost: the
    /// glyphs of the text object (one matrix inversion each).
    static int getLineCount(const PDFContentNode& textObject);

    /// The objects of a Group / MarkedContent as the tree has them: its children with a row
    /// (getRowNodeOfChild)
    static int getChildRowCount(const PDFContentNode& node);

    // ---- P12's own functions (tests)

    /// The kind and the description of the rows are computed once per node of a version (a
    /// node, which a command did not change, keeps them); the number of the computations
    int getTextComputationCount() const { return m_textComputationCount; }

    /// The texts of the rows are computed again (a change of the language: the panel calls it)
    void clearTextCache();

Q_SIGNALS:
    /// The counts of the header may have changed
    void countsChanged();

    /// The rows follow the model of the session again (after a command, a page, a filter): the
    /// panel selects the rows of the selection again
    void refreshed();

private:
    /// A row: the node is its key in m_entries
    struct Entry
    {
        quint64 placeId = 0;                ///< The node among the children of the parent node (getPlaceId)
        quint64 parentId = 0;               ///< The row of the parent (the root entry for a top row)
        quintptr serial = 0;                ///< The internal id of its indices
        std::vector<quint64> children;      ///< The rows of the children in their order
        mutable int row = 0;                ///< The row in the parent (valid, when the parent is not in m_dirtyParents)
    };

    using EntryMap = std::unordered_map<quint64, Entry>;

    /// The rows of a container
    struct Tree
    {
        EntryMap entries;
        quint64 rootId = 0;
        int objectCount = 0;
        int newObjectCount = 0;
        int matchCount = 0;
    };

    /// The texts of a row node; valid while the container has this node object (the pointer is
    /// held: a node of the cache is never freed and its address never reused meanwhile)
    struct TextEntry
    {
        PDFContentNodePointer node;
        QString kind;
        QString description;
    };

    /// The counts of countChanges: rows removed, inserted and moved (a reset above
    /// LARGE_CHANGE), and kept rows, whose node changed (one dataChanged each up to LARGE_CHANGE,
    /// one dataChanged of the range of each parent above it)
    struct ChangeCount
    {
        int structure = 0;
        int data = 0;
    };

    void onPageChanged();
    void onContainerChanged();
    void onContainerLoaded(const pdf::PDFContentContainerKey& key);
    void onModelChanged(const pdf::PDFContentChange& change);
    void onEditorStateChanged(const pdf::PDFContentNodeReference& node);
    void onSessionReplaced();
    void onQueuedRefresh();
    void onQueuedStateRefresh();

    /// The connections to the session of the context (the constructor, sessionReplaced)
    void connectSession();

    /// The kind and the description of the row node \p id of \p container (cached)
    const TextEntry& getTexts(const PDFContentContainer& container, quint64 id) const;

    /// Drops the texts of the nodes, which the current container does not have any more
    void pruneTextCache();

    /// Queues the refresh of the rows (one for more signals)
    void queueRefresh();

    /// The rows follow the context now (a reset or the incremental changes)
    void refresh(bool isReset);

    Tree buildTree(const PDFContentContainerPointer& container, PDFContentNodeId rootId) const;
    bool buildRows(Tree& tree, const PDFContentContainer& container, const PDFContentNode& node, quint64 rowId, int depth, std::vector<quint64>* keptChildren) const;
    bool isMatched(const PDFContentContainer& container, const PDFContentNode& node) const;

    void resetTo(Tree tree, PDFContentContainerPointer container, const PDFContentNodeReference& root);
    void countChanges(const Tree& tree, quint64 parentId, const PDFContentContainer* oldContainer, const PDFContentContainer& newContainer, ChangeCount* count) const;
    void removeMissing(const Tree& tree, quint64 parentId, const PDFContentContainer* oldContainer, const PDFContentContainer& newContainer);
    void insertAndMove(const Tree& tree, quint64 parentId, const PDFContentContainer* oldContainer, const PDFContentContainer& newContainer);
    void insertSubtree(const Tree& tree, quint64 id, quint64 parentId, std::vector<quint64>* siblings, size_t position);
    void removeSubtree(quint64 id);
    bool isSameNode(quint64 id, const PDFContentContainer* oldContainer, const PDFContentContainer& newContainer) const;

    quint64 getNodeId(const QModelIndex& index) const;
    QModelIndex createParentIndex(quint64 id) const;
    int getRow(quint64 id) const;
    const PDFContentNode* getRowNode(const QModelIndex& index) const;
    const PDFDocument* getDocument() const;
    std::vector<PDFContentNodeId> decodeNodes(const QMimeData* data) const;

    QPointer<PDFAppEditContentContext> m_context;
    QPointer<PDFContentEditSession> m_session;  ///< The session the model is connected to
    QString m_filterText;
    FilterKinds m_filterKinds = FilterKinds(0x7F);

    PDFContentContainerPointer m_container;     ///< The version the rows show
    PDFContentNodeReference m_root;             ///< The edited root of the rows
    EntryMap m_entries;
    std::unordered_map<quintptr, quint64> m_serialToNode;
    quintptr m_nextSerial = 1;
    mutable std::unordered_set<quint64> m_dirtyParents;
    std::set<PDFContentNodeReference> m_expanded;
    mutable std::unordered_map<quint64, TextEntry> m_textCache;
    mutable int m_textComputationCount = 0;
    int m_objectCount = 0;
    int m_newObjectCount = 0;
    int m_matchCount = 0;
    int m_stateChangeCount = 0;                 ///< The rows told by editorStateChanged in this turn of the event loop
    bool m_isFilterShown = false;
    bool m_isRefreshQueued = false;
    bool m_isResetQueued = false;
    bool m_isStateRefreshQueued = false;
    bool m_isBulkDataChange = false;            ///< The refresh tells the changed rows of a parent by one range
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PDFAppEditContentObjectsModel::FilterKinds)

/// The panel of the tab Objects (zadani_gui.md 4.18): the header with the counts and the filter
/// button, the tree. Selection of the tree <-> PDFAppEditContentSelection through
/// PDFAppItemViewSignalRelay; the context menu by PDFAppEditContentContext::requestContextMenu(
/// PDFAppEditContentMenuIds::CONTEXT, ...); keys of the tree: Delete calls
/// PDFAppEditContentContext::triggerAction(PDFAppEditContentActionIds::DELETE_SELECTION) (the
/// one owner of what is deleted; the same entry answers the application command edit.delete),
/// Enter starts the editor tool of the object,
/// F2 nothing.
///
/// Threads and ownership: a part - created by the tab entry for the left panel, built again
/// after a change of the language, deleted by the shell or with the view of the document. It
/// keeps no state: the model, the filter (in the model) and the selection live in the context;
/// the expanded rows are restored from the model by node identifiers kept in the model. GUI
/// thread; the hook retire() closes its popups.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentObjectsPanel : public QWidget, public PDFRetirable
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppEditContentObjectsPanel(PDFAppEditContentContext* context, PDFAppEditContentObjectsModel* model, QWidget* parent);
    virtual ~PDFAppEditContentObjectsPanel() override;

    /// The entries of this file for the registry (the tab Objects)
    static void registerParts(PDFAppEditContentRegistry* registry);

    // ---- P12's own functions (tests)

    PDFTreeView* getTree() const;
    PDFLineEdit* getFilterField() const;
    QToolButton* getFilterButton() const;
    PDFAppHeaderText* getSubtitle() const;
    PDFAppParagraph* getHint() const;

    /// The time, after which a typed filter text is given to the model
    static constexpr int FILTER_DELAY = 250;

    /// The work of the last change of the selection by the tree (R7-3): the rows and the
    /// selected nodes read, and the comparisons of the sets of nodes - O(n log n) in the rows
    quint64 getLastTreeSelectionWork() const { return m_treeSelectionWork; }

protected:
    virtual void retire() override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void updateTexts();
    void updateEnabled();
    void syncTreeFromSelection();
    void restoreExpanded(const QModelIndex& parent);
    void activate(const QModelIndex& index);

    void onCountsChanged();
    void onRefreshed();
    void onSelectionChanged();
    void onTreeSelectionChanged();
    void onTreeActivated(const QModelIndex& index);
    void onEnterKey();
    void onContextMenuRequested(const QPoint& position);
    void onFilterTextEdited(const QString& text);
    void onFilterTimeout();
    void onFilterButtonClicked();
    void onExpanded(const QModelIndex& index);
    void onCollapsed(const QModelIndex& index);
    void onModelReset();
    void onRowsInserted(const QModelIndex& parent, int first, int last);

    QPointer<PDFAppEditContentContext> m_context;
    QPointer<PDFAppEditContentObjectsModel> m_model;
    QPointer<PDFTreeView> m_tree;
    QPointer<PDFAppHeaderText> m_title;
    QPointer<PDFAppHeaderText> m_subtitle;
    QPointer<QToolButton> m_filterButton;
    QPointer<PDFLineEdit> m_filterField;
    QPointer<PDFAppParagraph> m_hint;
    QPointer<QTimer> m_filterTimer;
    quint64 m_treeSelectionWork = 0;
    bool m_isSyncing = false;
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTPANELS_OBJECTS_H
