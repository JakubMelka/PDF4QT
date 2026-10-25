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

#ifndef PDFAPPOUTLINEEDITOR_H
#define PDFAPPOUTLINEEDITOR_H

#include "pdfappglobal.h"
#include "pdfappannotationoperations.h"

#include <QObject>
#include <QPointer>

#include <optional>

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentView;
class PDFAppNamedDestinationDialog;
class PDFAppOutlinePanel;
class PDFAppOutlineTargetPicker;
class PDFMessageBox;

/// Editing of the outline of a document view (package C of step 4): the "+"
/// button of the Outline panel (195:1485), the Insert › Outline item tile of
/// package A, inline rename, delete, move, indent, target and style of the items,
/// each one undo step with the flag Outline (PDFAppAnnotationOperations). Locked
/// document or a document, which does not allow changes: no editing (O20).
///
/// The editor works on the current item of the Outline panel (the panel tells it
/// by setCurrentItem) and on the paths the panel gives it; the commands of the
/// package NotesOutline call it. A new item (createItemAtCurrentView) is not
/// written until its title is confirmed in the inline editor of the panel: Enter
/// or a click elsewhere writes it (one undo step), Esc leaves no trace.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOutlineEditor : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    using OutlinePath = PDFAppAnnotationOperations::OutlinePath;

    /// Status message timeout (milliseconds)
    static constexpr int STATUS_MESSAGE_TIMEOUT = 5000;

    explicit PDFAppOutlineEditor(PDFAppDocumentView* view, QObject* parent);
    virtual ~PDFAppOutlineEditor() override;

    PDFAppDocumentView* getView() const;
    PDFAppDocument* getDocument() const;

    /// The view has a new document model (PDFAppDocumentView::setDocument)
    void setDocument(PDFAppDocument* document);

    /// The outline can be edited: a shown, unlocked document, which allows the
    /// changes (Modify or Assemble), with an outline the operations can edit
    bool isEditingAllowed() const;

    /// Reason, why the outline cannot be edited (empty when it can)
    QString getEditingRefusalReason() const;

    /// Destination of the current view: XYZ of the top of the view on the current
    /// page (the top of the page, when the view shows the area above it), the
    /// left side and the zoom kept by the reader. nullopt without a page.
    std::optional<PDFDestination> getCurrentViewDestination() const;

    /// Top edge of the view \p view in the coordinates of the page \p pageIndex
    /// (in the middle of the view, where a page lies in every layout); nullopt,
    /// when the top edge of the view does not lie on the page
    static std::optional<PDFReal> getViewTop(const PDFAppDocumentView* view, PDFInteger pageIndex);

    /// Left edge of the view \p view in the coordinates of the page \p pageIndex
    /// (in the middle of the view); nullopt, when the left edge does not lie on the page
    static std::optional<PDFReal> getViewLeft(const PDFAppDocumentView* view, PDFInteger pageIndex);

    /// Destination of the type for the current view on the current page (Set Target ›
    /// Whole Page, Page Width - the top of the view, Page Height - the left side of
    /// the view, Visible Content and its width and height); nullopt for another
    /// type or without a page
    std::optional<PDFDestination> getCurrentViewDestination(DestinationType type) const;

    /// The type of Set Target is picked on a page (Area… and Position…), the other
    /// types take the current view
    static bool isPickedOnPage(DestinationType type);

    // ---------------------------------------------------------------- creation

    /// Creates a new outline item with the destination XYZ of the current view
    /// after the current item of the Outline panel (at the end without one), shows
    /// the Outline panel and starts the inline rename (Esc during the creation =
    /// no undo step). \returns false, when the outline cannot be edited.
    bool createItemAtCurrentView();

    /// A new item waits for its title in the panel
    bool isCreationPending() const { return m_isCreationPending; }
    const OutlinePath& getPendingParent() const { return m_pendingParent; }
    size_t getPendingIndex() const { return m_pendingIndex; }

    /// The document waits for the title of a new outline item (the Outline tab is
    /// shown also for a document without an outline)
    static bool isCreationPending(const PDFAppDocument* document);

    /// Writes the new item with the title (one undo step); an empty title is the
    /// default title. \returns false, when the item has not been written.
    bool finishCreation(const QString& title);

    /// Ends the creation without a change
    void cancelCreation();

    /// Default title of a new item ("New item")
    static QString getDefaultTitle();

    /// Title offered by the inline editor of a new item: the text selected on the
    /// page of the new item (usually its heading - as the old Editor), else the
    /// default title
    const QString& getPendingTitle() const { return m_pendingTitle; }

    /// Longest title taken from the selected text
    static constexpr int MAXIMUM_SELECTED_TITLE_LENGTH = 128;

    // ---------------------------------------------------------------- current item

    /// Current item of the Outline panel (empty = none)
    void setCurrentItem(const OutlinePath& path);
    const OutlinePath& getCurrentItem() const { return m_currentItem; }

    /// The current item exists in the current document
    bool hasCurrentItem() const;

    /// The operations of the commands are possible for the current item
    bool canMoveUp() const;
    bool canMoveDown() const;
    bool canIndent() const;
    bool canOutdent() const;
    bool isCurrentItemBold() const;
    bool isCurrentItemItalic() const;

    /// Operations on the current item (the commands outline.*); each one is one
    /// undo step. \returns false, when nothing has been changed.
    bool renameCurrentItem();
    bool deleteCurrentItem();
    bool moveCurrentItemUp();
    bool moveCurrentItemDown();
    bool indentCurrentItem();
    bool outdentCurrentItem();
    bool setCurrentItemTargetToCurrentView();
    bool setCurrentItemBold(bool bold);
    bool setCurrentItemItalic(bool italic);

    // ---------------------------------------------------------------- selection

    /// Items selected in the Outline panel (more items: Ctrl / Shift + click); the
    /// current item is one of them. Delete removes all of them as one undo step,
    /// the other commands act on the current item.
    void setSelectedItems(const std::vector<OutlinePath>& items);
    const std::vector<OutlinePath>& getSelectedItems() const { return m_selectedItems; }

    /// Items, which Delete removes: the selected items of the shown outline (also,
    /// when the current item has been deselected by Ctrl + click - the highlighted
    /// items go, not the deselected one), else the current item
    std::vector<OutlinePath> getItemsToDelete() const;

    // ---------------------------------------------------------------- targets (Set Target)

    /// Starts the picking of the target of the type for the current item on a page
    /// (Whole page, Page width, Page height, Area, Position, Visible content, its
    /// width and height - PDFAppOutlineTargetPicker); the picked target is one undo
    /// step. \returns false, when the picking cannot start.
    bool pickCurrentItemTarget(DestinationType type);

    /// Set Target of the type for the current item: Area… and Position… start the
    /// picking (pickCurrentItemTarget), the other types take the current view at
    /// once (one undo step)
    bool setCurrentItemTarget(DestinationType type);

    /// The target of an item is being picked
    bool isPickingTarget() const;

    /// Ends the picking of a target without a change
    void cancelTargetPicking();

    /// Picker of the targets (nullptr before the first picking)
    PDFAppOutlineTargetPicker* getTargetPicker() const;

    /// Opens the dialog Named Destination… for the current item; the chosen name
    /// is one undo step. \returns false, when it cannot be opened.
    bool chooseCurrentItemNamedDestination();

    /// The open dialog of the named destinations (nullptr if none)
    PDFAppNamedDestinationDialog* getNamedDestinationDialog() const;

    /// The target of the current item is a position (XYZ of a page)
    bool isCurrentItemTargetPosition() const;

    /// The position of the current item keeps the zoom of the reader (null zoom)
    bool isCurrentItemKeepingZoom() const;

    /// Keep the Reader's Zoom of the current item: on = the zoom of its position is
    /// removed, off = the zoom of the view is stored (one undo step)
    bool setCurrentItemKeepZoom(bool keepZoom);

    /// Number of the items, whose position sets its own zoom (Keep the Reader's
    /// Zoom for All Items…), read once per version of the outline
    int getItemsWithZoomCount() const;

    /// Keep the Reader's Zoom for All Items…: a confirmation with the number of
    /// the items, then all of them as one undo step. \returns false, when there
    /// is nothing to change or the outline cannot be edited.
    bool keepZoomForAllItems();

    /// The confirmation of keepZoomForAllItems (nullptr if none is open)
    PDFMessageBox* getKeepZoomConfirmation() const;

    /// Sets the destination of an item (one undo step)
    bool setItemDestination(const OutlinePath& item, const PDFDestination& destination, const QString& description);

    // ---------------------------------------------------------------- paths

    /// Operations on an item of the path (the panel: inline rename, drag and drop)
    bool renameItem(const OutlinePath& item, const QString& title);
    bool deleteItem(const OutlinePath& item);
    bool moveItem(const OutlinePath& item, const OutlinePath& newParent, size_t index);
    bool deleteItems(const std::vector<OutlinePath>& items);
    bool moveItems(const std::vector<OutlinePath>& items, const OutlinePath& newParent, size_t index);
    bool setItemTargetToCurrentView(const OutlinePath& item);
    bool setItemStyle(const OutlinePath& item, bool bold, bool italic);

Q_SIGNALS:
    /// The outline has been changed by the editor (one undo step)
    void outlineChanged();

    /// The state of the commands may have changed (current item, document, lock)
    void stateChanged();

private:
    const PDFOutlineItem* findItem(const OutlinePath& path) const;
    PDFAppOutlinePanel* findPanel() const;
    bool commit(const PDFAppAnnotationResult& result, const QString& description, const OutlinePath& selectedItem);
    void showStatusMessage(const QString& text) const;
    void setPendingProperty(bool pending);
    void onDocumentChanged();
    void onDocumentDestroyed();
    void onCurrentItemChangeQueued();
    void onLockChanged();
    void onTargetPicked(pdf::PDFDestination destination);
    void onNamedDestinationFinished(int result);
    void onKeepZoomConfirmationFinished(int result);
    void onDialogResultQueued();
    void closeTargetUserInterface();
    const PDFOutlineItem* findCurrentPositionItem() const;
    QWidget* getDialogParent() const;

    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppDocument> m_document;
    OutlinePath m_currentItem;
    bool m_isCreationPending = false;
    bool m_isCurrentItemChangePending = false;   ///< stateChanged of a new current item waits for the event loop
    OutlinePath m_pendingParent;
    size_t m_pendingIndex = 0;
    PDFDestination m_pendingDestination;
    QString m_pendingTitle;
    std::vector<OutlinePath> m_selectedItems;
    quint64 m_selectedOutlineVersion = 0;       ///< Outline version, in which m_selectedItems address the items
    QPointer<PDFAppOutlineTargetPicker> m_targetPicker;
    QPointer<PDFAppNamedDestinationDialog> m_namedDestinationDialog;
    QPointer<PDFMessageBox> m_keepZoomConfirmation;
    OutlinePath m_targetItem;                   ///< Item of the picking / of the dialog of the named destinations
    quint64 m_targetOutlineVersion = 0;         ///< Outline version, in which m_targetItem addresses the item
    std::vector<OutlinePath> m_keepZoomItems;   ///< Items of the open confirmation

    /// Result of a dialog of Set Target committed from the event loop (QDialog
    /// emits more signals after finished: the commit, whose receivers may delete
    /// the tab with the dialog, does not run inside of them)
    struct DialogResult
    {
        bool isPending = false;
        quint64 outlineVersion = 0;
        OutlinePath namedItem;                  ///< Item of a chosen named destination
        QByteArray name;                        ///< The chosen named destination
        std::vector<OutlinePath> keepZoomItems; ///< Items of the accepted confirmation of the zoom
    };
    DialogResult m_dialogResult;
    mutable QPointer<const PDFAppDocument> m_zoomCountDocument;
    mutable quint64 m_zoomCountVersion = 0;
    mutable int m_zoomCount = 0;
    QString m_previousPanelId;                  ///< Panel shown before the creation (shown again after a canceled creation of the first item)
    mutable QPointer<const PDFAppDocument> m_editableDocument;  ///< Document model of the cache of PDFAppAnnotationOperations::isOutlineEditable
    mutable quint64 m_editableVersion = 0;                      ///< Outline version of the cache (PDFAppDocument::getOutlineVersion)
    mutable bool m_isOutlineEditable = false;
};

}   // namespace pdf

#endif // PDFAPPOUTLINEEDITOR_H
