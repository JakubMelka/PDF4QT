// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef PDFSIDEBARWIDGET_H
#define PDFSIDEBARWIDGET_H

#include "pdfglobal.h"
#include "pdfbookmarkmanager.h"

#include <QWidget>
#include <QAbstractItemDelegate>

class QAction;
class QPushButton;
class QToolButton;
class QWidget;
class QStandardItemModel;
class QSortFilterProxyModel;

namespace Ui
{
class PDFSidebarWidget;
}

namespace pdf
{
class PDFAction;
class PDFDocument;
class PDFCertificateInfo;
class PDFDrawWidgetProxy;
class PDFCertificateStore;
class PDFModifiedDocument;
class PDFThumbnailsItemModel;
class PDFOutlineTreeItemModel;
class PDFOptionalContentActivity;
class PDFAttachmentsTreeItemModel;
class PDFSignatureVerificationResult;
class PDFOptionalContentTreeItemModel;
}

namespace pdfviewer
{
class PDFTextToSpeech;
class PDFViewerSettings;
class PDFBookmarkItemModel;

class PDFSidebarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PDFSidebarWidget(pdf::PDFDrawWidgetProxy* proxy,
                              PDFTextToSpeech* textToSpeech,
                              pdf::PDFCertificateStore* certificateStore,
                              PDFBookmarkManager* bookmarkManager,
                              PDFViewerSettings* settings,
                              bool editableOutline,
                              QWidget* parent);
    virtual ~PDFSidebarWidget() override;

    enum Page
    {
        Invalid,
        _BEGIN,
        Outline = _BEGIN,
        Thumbnails,
        OptionalContent,
        Attachments,
        Speech,
        Signatures,
        Bookmarks,
        Notes,
        _END
    };

    void setDocument(const pdf::PDFModifiedDocument& document, const std::vector<pdf::PDFSignatureVerificationResult>& signatures);

    /// Returns true, if all items in sidebar are empty
    bool isEmpty() const;

    /// Returns true, if page is empty
    bool isEmpty(Page page) const;

    /// Selects current page
    void selectPage(Page page);

    /// Returns list of valid pages (nonempty pages)
    std::vector<Page> getValidPages() const;

    /// Sets current pages (for example, selects the correct thumbnail)
    void setCurrentPages(const std::vector<pdf::PDFInteger>& currentPages);

    /// Returns list of actions operating on the currently selected outline item
    /// (available only when outline editing is enabled). These are the same
    /// actions used to build the outline item's context menu, so they can be
    /// registered with the action manager to make them remappable/persistent.
    std::vector<QAction*> getOutlineActions() const;

    /// Returns the action creating a new outline item at the current view
    /// position, or nullptr, when outline editing is disabled. The action is
    /// intended to be also placed into the main window's menu, so that new
    /// outline items can be created without leaving the page being read.
    QAction* getOutlineNewItemAction() const { return m_outlineActionInsert; }

signals:
    void actionTriggered(const pdf::PDFAction* action);
    void documentModified(pdf::PDFModifiedDocument document);

    /// Emitted, when the sidebar must become visible, because the user is about
    /// to work with it - for example a new outline item has been created and its
    /// title is being edited in place.
    void sidebarVisibilityRequested();

private:
    void updateGUI(Page preferredPage);
    void updateButtons();
    void updatePageButtonIconSize();
    void updateSignatures(const std::vector<pdf::PDFSignatureVerificationResult>& signatures);
    void updateNotes();

    void onOutlineSearchText();
    void onNotesSearchText();
    void onPageButtonClicked();
    void onOutlineItemClicked(const QModelIndex& index);
    void onThumbnailsSizeChanged(int size);
    void onAttachmentDoubleClicked(const QModelIndex& index);
    void onAttachmentCustomContextMenuRequested(const QPoint& pos);
    void onThumbnailClicked(const QModelIndex& index);
    void onSignatureCustomContextMenuRequested(const QPoint& pos);
    void onOutlineTreeViewContextMenuRequested(const QPoint& pos);
    void onNotesTreeViewContextMenuRequested(const QPoint& pos);
    void onOutlineItemsChanged();
    void onBookmarkActivated(int index, PDFBookmarkManager::Bookmark bookmark);
    void onBookmarsCurrentIndexChanged(const QModelIndex& current, const QModelIndex& previous);
    void onBookmarkClicked(const QModelIndex& index);
    void onNotesItemClicked(const QModelIndex& index);

    // Outline item actions (created once, shared between the context menu and,
    // once registered with the action manager, keyboard shortcuts)
    void createOutlineActions();
    void updateOutlineActions();
    QModelIndex getCurrentOutlineSourceIndex() const;
    int countInheritableZoomLinks() const;

    void onOutlineActionFollow();
    void onOutlineActionInsert();
    void onOutlineActionDelete();
    void onOutlineActionRename();
    void onOutlineActionMoveUp();
    void onOutlineActionMoveDown();
    void onOutlineActionMoveLeft();
    void onOutlineActionMoveRight();
    void onOutlineItemEditorClosed(QWidget* editor, QAbstractItemDelegate::EndEditHint hint);
    void onOutlineModelAboutToBeReset();
    void onOutlineActionFontBold();
    void onOutlineActionFontItalic();
    void onOutlineActionSetTargetNamedDestination();
    void onOutlineActionSetTargetByType();
    void onOutlineActionInheritZoom();
    void onOutlineActionInheritZoomForAllChapters();

    /// Creates a new outline item pointing to the currently viewed page, places
    /// it after the currently selected item (or at the end of the outline, when
    /// nothing is selected) and starts editing of its title.
    void insertOutlineItem();

    /// Moves the currently selected outline item, so it becomes a child
    /// of \p destinationSourceParent at the given row.
    void moveOutlineItem(const QModelIndex& destinationSourceParent, int destinationRow);

    /// Returns text selected in the document, adjusted to be used as a title
    /// of the outline item. Empty string is returned, when nothing is selected.
    /// \param pageIndices Pages, on which the selected text is accepted. Text
    ///        selected on other pages is ignored, because the selection
    ///        survives scrolling to a completely different page.
    QString getSelectedTextAsOutlineItemTitle(const std::vector<pdf::PDFInteger>& pageIndices) const;

    /// Finishes the creation of an outline item, which is being named by the
    /// user. Item is either committed to the document, or, when the naming has
    /// been cancelled or no title has been given, removed again. Does nothing,
    /// when no item is being created.
    /// \param hint Hint, with which the item's editor has been closed
    void finishOutlineItemCreation(QAbstractItemDelegate::EndEditHint hint);

    /// Propagates outline changes, which have been deferred while a new item
    /// was being created, to the document.
    void commitOutlineChanges();

    /// Clears the outline search filter, so that all outline items are visible.
    /// Outline is edited in the source model, so items hidden by the filter
    /// would be modified without the user seeing it.
    void clearOutlineFilter();

    struct PageInfo
    {
        QToolButton* button = nullptr;
        QWidget* page = nullptr;
    };

    bool saveAttachmentToFile(const pdf::PDFFileSpecification* fileSpecification, const QString& fileName);
    void openAttachment(const pdf::PDFFileSpecification* fileSpecification);

    Ui::PDFSidebarWidget* ui;
    pdf::PDFDrawWidgetProxy* m_proxy;
    PDFTextToSpeech* m_textToSpeech;
    pdf::PDFCertificateStore* m_certificateStore;
    PDFBookmarkManager* m_bookmarkManager;
    PDFViewerSettings* m_settings;
    pdf::PDFOutlineTreeItemModel* m_outlineTreeModel;
    QSortFilterProxyModel* m_outlineSortProxyTreeModel;
    pdf::PDFThumbnailsItemModel* m_thumbnailsModel;
    pdf::PDFOptionalContentTreeItemModel* m_optionalContentTreeModel;
    PDFBookmarkItemModel* m_bookmarkItemModel;
    QStandardItemModel* m_notesTreeModel;
    QSortFilterProxyModel* m_notesSortProxyTreeModel;
    const pdf::PDFDocument* m_document;
    pdf::PDFOptionalContentActivity* m_optionalContentActivity;
    pdf::PDFAttachmentsTreeItemModel* m_attachmentsTreeModel;
    std::map<Page, PageInfo> m_pageInfo;
    std::vector<pdf::PDFSignatureVerificationResult> m_signatures;
    std::vector<pdf::PDFCertificateInfo> m_certificateInfos;
    std::vector<std::pair<pdf::PDFObjectReference, pdf::PDFInteger>> m_markupAnnotations;
    Page m_currentPage = Invalid;
    bool m_bookmarkChangeInProgress = false;

    // Outline item actions (valid only when outline editing is enabled)
    QAction* m_outlineActionFollow = nullptr;
    QAction* m_outlineActionDelete = nullptr;
    QAction* m_outlineActionInsert = nullptr;
    QAction* m_outlineActionRename = nullptr;
    QAction* m_outlineActionMoveUp = nullptr;
    QAction* m_outlineActionMoveDown = nullptr;
    QAction* m_outlineActionMoveLeft = nullptr;
    QAction* m_outlineActionMoveRight = nullptr;
    QAction* m_outlineActionFontBold = nullptr;
    QAction* m_outlineActionFontItalic = nullptr;
    QAction* m_outlineActionSetTargetNamedDestination = nullptr;
    QAction* m_outlineActionSetTargetFitPage = nullptr;
    QAction* m_outlineActionSetTargetFitPageHorizontally = nullptr;
    QAction* m_outlineActionSetTargetFitPageVertically = nullptr;
    QAction* m_outlineActionSetTargetFitRectangle = nullptr;
    QAction* m_outlineActionSetTargetFitBoundingBox = nullptr;
    QAction* m_outlineActionSetTargetFitBoundingBoxHorizontally = nullptr;
    QAction* m_outlineActionSetTargetFitBoundingBoxVertically = nullptr;
    QAction* m_outlineActionSetTargetXYZ = nullptr;
    QAction* m_outlineActionInheritZoom = nullptr;
    QAction* m_outlineActionInheritZoomForAllChapters = nullptr;
    std::vector<QAction*> m_outlineSetTargetActions;
    std::vector<QAction*> m_outlineActions;

    /// Item, which has just been created and is being named by the user. When
    /// the user cancels the editing, or leaves the title empty, then the item
    /// is removed again, so an accidentally triggered action leaves no trace.
    QPersistentModelIndex m_newOutlineItemIndex;

    /// True, when a new outline item is being inserted into the outline model.
    /// Together with the index of the created item it marks the period, in
    /// which the outline changes are not propagated to the document.
    bool m_isOutlineItemBeingCreated = false;

    /// True, when the outline has been changed, but the change hasn't been
    /// propagated to the document yet, because a new item is being created.
    bool m_isOutlineChangeDeferred = false;
};

}   // namespace pdfviewer

#endif // PDFSIDEBARWIDGET_H
