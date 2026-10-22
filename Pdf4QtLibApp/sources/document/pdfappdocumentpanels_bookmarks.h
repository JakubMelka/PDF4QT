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

#ifndef PDFAPPDOCUMENTPANELS_BOOKMARKS_H
#define PDFAPPDOCUMENTPANELS_BOOKMARKS_H

#include "pdfappglobal.h"
#include "pdfappdocumentpanel.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfapphomedialogs.h"

#include "pdfdocument.h"

#include <QAbstractListModel>
#include <QPointer>

#include <vector>

#include <QListView>
#include <QToolButton>

class QMenu;

namespace pdf
{

class PDFAppContext;
class PDFBookmarkDelegate;

/// Bookmark of the list: a personal bookmark (id > 0) or a bookmark generated
/// from a main chapter of the outline (isAuto, tag "Auto")
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBookmarkItem
{
public:
    qint64 id = 0;              ///< Row id of the personal bookmark (0 = generated)
    PDFInteger page = 0;        ///< Page index from 0
    QString title;              ///< Title (the subtitle of the row)
    bool isAuto = false;        ///< Generated from the outline
};

/// Personal bookmarks of a document ("My Bookmarks", 195:3445, the Bookmarks
/// section of the Go to panel 196:3437): the list of the repository by the
/// canonical path of the file, the bookmarks generated from the main chapters
/// of the outline (setting bookmarks.autoGenerate, default off - O10; not for a
/// page with a personal bookmark) merged by their pages, and the actions of the commands bookmarks.* - shared by the panel
/// and the command handler of package C (the panel need not exist).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBookmarks
{
public:
    PDFAppBookmarks() = delete;

    /// Bookmarks of the document in the order of the list. The personal ones keep
    /// their order; a generated one is placed before the first personal bookmark
    /// of a later page. Pages outside of the document are left out.
    static std::vector<PDFAppBookmarkItem> getItems(PDFAppContext* context, const PDFAppDocument* document);

    /// Bookmarks generated from the main chapters of the outline (sorted by pages)
    static std::vector<PDFAppBookmarkItem> getGeneratedItems(const PDFDocument* document);

    /// Title of a new bookmark of the page: the deepest outline item of the page
    /// (the item with the greatest page not after it), empty without an outline
    static QString getDefaultTitle(const PDFDocument* document, PDFInteger page);

    /// The document can have personal bookmarks (it has a file and the storage is open)
    static bool canStore(PDFAppContext* context, const PDFAppDocument* document);

    /// Adds a bookmark of the current page of the view (bookmarks.add). A page,
    /// which has one, is not added twice. \returns the id of the bookmark (0 on
    /// a failure, the error is shown)
    static qint64 addCurrentPage(PDFAppDocumentView* view, QWidget* parent);

    /// Goes to the next / previous bookmark by the pages (bookmarks.next,
    /// bookmarks.previous). \returns false, when there is none (a status message)
    static bool goToNext(PDFAppDocumentView* view, bool isForward);

    /// Exports the personal bookmarks to a JSON file (the save dialog)
    static void exportBookmarks(PDFAppDocumentView* view, QWidget* parent);

    /// Imports the bookmarks of a JSON file (the open dialog)
    static void importBookmarks(PDFAppDocumentView* view, QWidget* parent);

    /// Shows the error of the storage (nothing for no error)
    static void showError(QWidget* parent, const QString& title, const QString& text);
};

/// Rows of the bookmarks (roles of PDFBookmarkDelegate: title "Page 17", the
/// subtitle, the tag "Auto")
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBookmarksModel : public QAbstractListModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractListModel;

public:
    explicit PDFAppBookmarksModel(QObject* parent);
    virtual ~PDFAppBookmarksModel() override;

    void setItems(std::vector<PDFAppBookmarkItem> items, PDFDocumentPointer document);
    const PDFAppBookmarkItem* getItem(int row) const;
    const std::vector<PDFAppBookmarkItem>& getItems() const { return m_items; }

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    std::vector<PDFAppBookmarkItem> m_items;
    PDFDocumentPointer m_document;
};

/// Rename of a personal bookmark (the layout of Rename workspace 955:150171)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRenameBookmarkDialog : public PDFAppNameDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppNameDialog;

public:
    explicit PDFAppRenameBookmarkDialog(PDFAppContext* context, qint64 bookmarkId, const QString& title, QWidget* parent);
    virtual ~PDFAppRenameBookmarkDialog() override;

    qint64 getBookmarkId() const { return m_bookmarkId; }

protected:
    virtual bool submitName(const QString& name, QString* error) override;

private:
    QPointer<PDFAppContext> m_context;
    qint64 m_bookmarkId = 0;
};

/// Bookmarks panel (pdf4qt-document-bookmarks 195:3445): "Bookmarks / 7
/// bookmarks", the header buttons Add bookmark (bookmark-plus, 195:3845) and
/// More (more-horizontal, 195:3849: Export bookmarks…, Import bookmarks…,
/// Generate automatically), the rows of PDFBookmarkDelegate (spacing 2). The
/// bookmark of the current page is selected. A click goes to its page; the
/// context menu of a personal bookmark: Go to Page, Rename…, Move Up, Move Down,
/// Remove. The list follows the changes of the storage (another window or
/// process) and of the setting bookmarks.autoGenerate.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBookmarksPanel : public PDFAppDocumentPanel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanel;

public:
    explicit PDFAppBookmarksPanel(QWidget* parent);
    virtual ~PDFAppBookmarksPanel() override;

    virtual QString getId() const override;
    virtual QString getTitle() const override;
    virtual QString getSubtitle() const override;
    virtual QString getIconName() const override;
    virtual void setDocument(PDFAppDocument* document, PDFAppDocumentView* view) override;
    virtual QWidget* createHeaderWidget(QWidget* parent) override;

    /// Reads the bookmarks again
    void reload();

    /// Creates the context menu of the row (nullptr for an invalid row; deleted
    /// when it is closed)
    QMenu* createContextMenu(int row);

    /// Creates the menu of the button More (deleted when it is closed)
    QMenu* createMoreMenu();

    /// Actions of a row (also of the context menu)
    void goToBookmark(int row);
    void renameBookmark(int row);
    void moveBookmark(int row, int offset);
    void removeBookmark(int row);

    QListView* getListView() const { return m_view; }
    PDFAppBookmarksModel* getModel() const { return m_model; }
    QToolButton* getAddButton() const { return m_addButton; }
    QToolButton* getMoreButton() const { return m_moreButton; }

protected:
    virtual void changeEvent(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onDocumentChanged(const PDFModifiedDocument& modified);
    void onDocumentDestroyed();
    void onCurrentPageChanged(PDFInteger pageIndex);
    void onStorageChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void onClicked(const QModelIndex& index);
    void onContextMenuRequested(const QPoint& position);
    void onAddClicked();
    void onMoreClicked();
    void onExport();
    void onImport();
    void onGenerateToggled(bool checked);
    void onMenuGoTo();
    void onMenuRename();
    void onMenuMoveUp();
    void onMenuMoveDown();
    void onMenuRemove();
    void selectCurrentPage();
    void updateButtons();
    PDFAppContext* getContext() const;

    QPointer<PDFAppPanelListView> m_view;
    QPointer<PDFAppBookmarksModel> m_model;
    QPointer<PDFBookmarkDelegate> m_delegate;
    QPointer<QToolButton> m_addButton;
    QPointer<QToolButton> m_moreButton;
    QPointer<PDFAppDocument> m_connectedDocument;
    QPointer<PDFAppDocumentView> m_connectedView;
    QPointer<PDFAppRenameBookmarkDialog> m_renameDialog;
    int m_menuRow = -1;
    bool m_isSelecting = false;
};

/// Factory of the bookmarks panel (always available)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBookmarksPanelFactory : public PDFAppDocumentPanelFactory
{
public:
    virtual QString getId() const override;
    virtual int getOrder() const override;
    virtual QString getTitle() const override;
    virtual QString getIconName() const override;
    virtual PDFAppDocumentPanel* createPanel(QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTPANELS_BOOKMARKS_H
