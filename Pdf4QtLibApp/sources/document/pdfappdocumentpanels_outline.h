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

#ifndef PDFAPPDOCUMENTPANELS_OUTLINE_H
#define PDFAPPDOCUMENTPANELS_OUTLINE_H

#include "pdfappglobal.h"
#include "pdfappannotationoperations.h"
#include "pdfappdocumentpanel.h"

#include "pdfdocument.h"
#include "pdffluentwidgets.h"

#include <QAbstractItemDelegate>
#include <QAction>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QToolButton>

#include <limits>
#include <optional>
#include <vector>

class QMenu;
class QMimeData;
class QStandardItem;

namespace pdf
{

class PDFAppOutlineEditor;
class PDFLineEdit;
class PDFTreeView;

/// Filter of the outline: an item matching the text (without case) is shown with
/// its parents (recursive filtering)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOutlineFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

private:
    using BaseClass = QSortFilterProxyModel;

public:
    explicit PDFAppOutlineFilterModel(QObject* parent);
    virtual ~PDFAppOutlineFilterModel() override;
};

/// Items of the Outline panel: a QStandardItemModel, whose drag carries the path
/// of the dragged item and the key of the document version (a drop of another
/// document or of another version is refused); the panel performs the drop itself
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOutlineItemModel : public QStandardItemModel
{
    Q_OBJECT

private:
    using BaseClass = QStandardItemModel;

public:
    /// Mime type of a dragged outline item
    static QString getMimeType() { return QStringLiteral("application/x-pdf4qt-outline-item"); }

    explicit PDFAppOutlineItemModel(QObject* parent);
    virtual ~PDFAppOutlineItemModel() override;

    /// Key of the document version of the items (the id of the document and its version)
    void setDocumentKey(const QString& key) { m_documentKey = key; }
    const QString& getDocumentKey() const { return m_documentKey; }

    /// Path of the item (the rows from the root)
    static PDFAppAnnotationOperations::OutlinePath getPath(const QModelIndex& index);

    /// Path of a dragged item of the data, when its key is \p documentKey
    static std::optional<PDFAppAnnotationOperations::OutlinePath> getDraggedPath(const QMimeData* data, const QString& documentKey);

    virtual QStringList mimeTypes() const override;
    virtual QMimeData* mimeData(const QModelIndexList& indexes) const override;
    virtual Qt::DropActions supportedDragActions() const override;
    virtual Qt::DropActions supportedDropActions() const override;
    virtual bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) const override;
    virtual bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;

private:
    QString m_documentKey;
};

/// Outline panel (pdf4qt-document-outline 195:1085): the header "Outline / 86
/// items" with the button outline-new 195:1485 (step 4), the field "Search
/// outline…" and the tree of PDFTreeView with the preset Outline. A click (Enter)
/// goes to the destination of the item; the item of the current page is selected
/// when the view reaches another page (findCurrentItemIndex), its parents are
/// expanded. An item the user has activated stays selected, while the current page
/// is its page.
///
/// Editing (step 4, package C, PDFAppOutlineEditor): "+" and Insert create an
/// item at the current view with the inline editor (Esc = nothing), F2 renames,
/// Del deletes, the context menu has the commands outline.*, an item is moved by
/// drag and drop (above, below or into another item). The expansion and the
/// current item are kept over the changes and undo / redo (the items are known by
/// their objects). A locked document hides the editing.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOutlinePanel : public PDFAppDocumentPanel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanel;

public:
    /// Role of the page of an item (index from 0, -1 = no page in the document)
    static constexpr int PageRole = Qt::UserRole + 1;

    /// Most items read from an outline (the rest is left out)
    static constexpr int MAXIMUM_ITEMS = 100000;

    explicit PDFAppOutlinePanel(QWidget* parent);
    virtual ~PDFAppOutlinePanel() override;

    virtual QString getId() const override;
    virtual QString getTitle() const override;
    virtual QString getSubtitle() const override;
    virtual QString getIconName() const override;
    virtual void setDocument(PDFAppDocument* document, PDFAppDocumentView* view) override;

    /// Number of the items of the outline
    int getItemCount() const { return int(m_items.size()); }

    /// Position of an item: its page and the top of its destination in the
    /// coordinates of the page (+infinity: the top of the page - a destination
    /// without a position, Fit, a page)
    struct ItemPosition
    {
        PDFInteger page = -1;
        PDFReal top = std::numeric_limits<PDFReal>::infinity();
    };

    /// Index of the item of the position of the view (-1 if none): the items of
    /// the greatest page not after \p currentPage; of them the item, whose
    /// destination is closest above the top of the viewport \p viewTop (in the
    /// coordinates of the page, +infinity = the top of the page is not reached),
    /// of equal ones the later; when none of the page lies above, the topmost
    /// item of the page. Items of an earlier page: the lowest of its page.
    static int findCurrentItemIndex(const std::vector<ItemPosition>& items, PDFInteger currentPage, PDFReal viewTop);

    /// Filters the items by the text
    void setFilterText(const QString& text);

    /// Index (of the filter model of the view) of the item selected for the
    /// current page, an invalid index if none
    QModelIndex getCurrentItem() const;

    /// Activates the item (of the filter model of the view): goes to its page
    void activateItem(const QModelIndex& index);

    PDFTreeView* getTreeView() const { return m_tree; }
    PDFLineEdit* getSearchField() const { return m_searchField; }
    QAbstractItemModel* getViewModel() const;

    /// The outline of the document has items
    static bool hasOutline(const PDFDocument* document);

    virtual QWidget* createHeaderWidget(QWidget* parent) override;

    // ---------------------------------------------------------------- editing

    using OutlinePath = PDFAppAnnotationOperations::OutlinePath;

    /// Place of a drop relative to the item under the pointer
    enum class DropPosition
    {
        Above,      ///< Before the item
        Below,      ///< After the item
        OnItem,     ///< The last child of the item
        OnViewport  ///< The last top level item
    };

    /// Index (of the view model) of the item of the path, invalid when it is not shown
    QModelIndex getItemIndex(const OutlinePath& path) const;

    /// Path of the item (an index of the view model or of the source model)
    OutlinePath getItemPath(const QModelIndex& index) const;

    /// Shows a temporary item at the place of PDFAppOutlineEditor::getPendingParent /
    /// getPendingIndex and opens its inline editor (the editor writes the item, when
    /// the title is confirmed)
    void beginPendingItem();

    /// Removes the temporary item (the creation has ended)
    void endPendingItem();

    /// The temporary item of a creation (nullptr if none)
    QStandardItem* getPendingItem() const { return m_pendingItem; }

    /// Opens the inline editor of the item. \returns false, when it cannot be edited.
    bool editItem(const OutlinePath& path);

    /// An open inline rename of an item is confirmed now (as by Enter - the typed
    /// title is written, one undo step); called before a new item is created, whose
    /// temporary item would close the editor without its title. A slot of the
    /// change of the document may delete the panel.
    void finishInlineEditing();

    /// Item, which is selected after the next change of the outline (the editor
    /// sets the changed item before its commit)
    void setItemToSelect(const OutlinePath& path) { m_itemToSelect = path; }

    /// Place of a drop of the position \p point in the item rectangle \p rect
    /// (the upper quarter above, the lower quarter below, else into the item)
    static DropPosition getDropPosition(const QRect& rect, const QPoint& point);

    /// Moves the item \p source to the place of the drop at the item \p target
    /// (indices of the view model; an invalid target = the end); one undo step
    bool dropItem(const QModelIndex& source, const QModelIndex& target, DropPosition position);

    /// Context menu of the item (an index of the view model; nullptr if none)
    QMenu* createContextMenu(const QModelIndex& index);

    /// Button outline-new of the header (nullptr before the header is created)
    QToolButton* getNewItemButton() const { return m_newItemButton; }

    /// Action of the rename key (F2 or the key of the user) of the panel
    QAction* getRenameAction() const { return m_renameAction; }

    /// Model of the items (the source model of the view)
    PDFAppOutlineItemModel* getSourceModel() const { return m_model; }

protected:
    virtual void changeEvent(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Item
    {
        QStandardItem* item = nullptr;
        PDFInteger page = -1;
        PDFReal top = std::numeric_limits<PDFReal>::infinity();
        PDFObjectReference reference;   ///< Object of the item (invalid, when the outline cannot be edited)
    };

    /// Top of the viewport in the coordinates of the current page (+infinity,
    /// when the top edge of the view does not lie on the page)
    PDFReal getViewTop(PDFInteger currentPage) const;

    void onDocumentChanged(const PDFModifiedDocument& modified);
    void onDocumentDestroyed();
    void onCurrentPageChanged(PDFInteger pageIndex);
    void onSearchTextChanged(const QString& text);
    void onItemClicked(const QModelIndex& index);
    void onCurrentIndexChanged(const QModelIndex& current, const QModelIndex& previous);
    void onEditorClosed(QWidget* editor, QAbstractItemDelegate::EndEditHint hint);
    void onEditFinishedQueued();
    void openPendingEditor();
    void onDropQueued();
    void onEditingStateChanged();
    void onContextMenuRequested(const QPoint& position);
    void onMenuActionTriggered();
    void onNewItemClicked();
    void onRenameTriggered();
    void onDatabaseChanged(const QString& topic, const QString& key);
    void rebuild();
    void selectCurrentItem();
    void updateEditingState();
    void updateRenameShortcut();
    void updateTexts();
    void selectItem(const QModelIndex& index);
    QModelIndex getSourceIndex(const OutlinePath& path) const;
    PDFAppOutlineEditor* getEditor() const;
    bool isEditingAllowed() const;
    QString getDocumentKey() const;
    void requestCommand(const QString& commandId);

    QPointer<PDFLineEdit> m_searchField;
    QPointer<PDFTreeView> m_tree;
    QPointer<PDFAppOutlineItemModel> m_model;
    QPointer<PDFAppOutlineFilterModel> m_filterModel;
    QPointer<PDFAppDocument> m_connectedDocument;
    QPointer<PDFAppDocumentView> m_connectedView;
    QSharedPointer<PDFOutlineItem> m_outlineRoot;   ///< Keeps the items of the actions alive
    PDFDocumentPointer m_document;
    std::vector<Item> m_items;                      ///< Items in the order of the document (pre-order)
    QPersistentModelIndex m_activatedItem;          ///< Item of m_model, which the user has activated (it stays selected on its page)
    bool m_isSelecting = false;
    PDFInteger m_lastPageIndex = -1;                ///< Page of the last selection by the view (a new page moves the selection)

    // Editing (step 4)
    QPointer<PDFAppOutlineEditor> m_connectedEditor;
    QPointer<QToolButton> m_newItemButton;
    QPointer<QAction> m_renameAction;
    QStandardItem* m_pendingItem = nullptr;         ///< Temporary item of a creation (owned by the model)
    QPersistentModelIndex m_editedIndex;            ///< Item of m_model in the inline editor
    bool m_isEditedPending = false;                 ///< The edited item is the temporary item of a creation
    OutlinePath m_itemToSelect;                     ///< Item selected after the next change (PDFAppOutlineEditor)

    /// Inline editor closed, processed from the event loop
    struct EditResult
    {
        bool isValid = false;
        bool isPending = false;
        bool isAccepted = false;
        OutlinePath path;
        QString title;
    };
    EditResult m_editResult;

    /// Drop processed from the event loop
    struct DropRequest
    {
        bool isValid = false;
        OutlinePath item;
        OutlinePath parent;
        size_t index = 0;
    };
    DropRequest m_dropRequest;
};

/// Factory of the outline panel (the tab only for a document with an outline)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOutlinePanelFactory : public PDFAppDocumentPanelFactory
{
public:
    virtual QString getId() const override;
    virtual int getOrder() const override;
    virtual QString getTitle() const override;
    virtual QString getIconName() const override;
    virtual bool isAvailable(const PDFAppDocument* document) const override;
    virtual PDFAppDocumentPanel* createPanel(QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTPANELS_OUTLINE_H
