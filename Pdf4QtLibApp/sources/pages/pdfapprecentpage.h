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

#ifndef PDFAPPRECENTPAGE_H
#define PDFAPPRECENTPAGE_H

#include "pdfappglobal.h"
#include "pdfapphomebasepage.h"
#include "pdfapphomewidgets.h"

#include "pdfcomponentcards.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"

#include <QAbstractListModel>
#include <QButtonGroup>
#include <QPointer>
#include <QPushButton>

#include <vector>

class QStackedWidget;
class QVBoxLayout;

namespace pdf
{

/// Item of the Recent list: a header of a group ("Today 3") or a file, a folder
/// or a workspace (component Recent item row 79:1026)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRecentItem
{
public:
    enum class Type
    {
        Header,
        File,
        Folder,
        Workspace
    };

    Type type = Type::File;
    PDFAppHomeItem item;        ///< The item of the menu (kind, path, id)
    QString name;
    QString details;
    QString progressText;
    qreal progress = -1.0;
    QString opened;
    qint64 time = 0;            ///< Time of opening / of the use (sort, groups)
    QString sortFolder;         ///< Folder for the sort by folder
    bool isPinned = false;
    bool isMissing = false;     ///< The file no longer exists
    bool isLocked = false;      ///< The document needs a password (no thumbnail, the lock of the library)
    int count = 0;              ///< Items of a group (header)
    QPixmap pixmap;
};

/// Model of the Recent list (the history card of 80:910): the roles of
/// PDFComponentRoles read by PDFRecentItemDelegate; the headers of the groups have
/// HeaderRole. The star writes PinnedRole: the model does not change itself, it
/// asks the page (pinRequested), which writes the database (the change comes back
/// by the notification).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRecentModel : public QAbstractListModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractListModel;

public:
    enum Role
    {
        HeaderRole = PDFComponentRoles::LastRole + 1,   ///< bool: the item is a header of a group
        CountRole                                       ///< int: number of the items of the group of a header
    };

    explicit PDFAppRecentModel(QObject* parent = nullptr);
    virtual ~PDFAppRecentModel() override;

    /// Replaces the items (a reset of the model only when they are different)
    void setItems(std::vector<PDFAppRecentItem> items);
    const std::vector<PDFAppRecentItem>& getItems() const { return m_items; }

    /// Item of the row (nullptr for an invalid row)
    const PDFAppRecentItem* getItem(int row) const;

    /// Row of the item of the type with the path and the id (-1 = none)
    int findItem(PDFAppRecentItem::Type type, const QString& path, qint64 id) const;

    /// Updates the existence / the details of the item of the path
    void setMissing(const QString& path, bool isMissing);
    void setDetails(const QString& path, const QString& details);

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;

Q_SIGNALS:
    /// The star of the item has been clicked
    void pinRequested(const pdf::PDFAppHomeItem& item, bool isPinned);

private:
    static bool isSameContent(const PDFAppRecentItem& first, const PDFAppRecentItem& second);

    std::vector<PDFAppRecentItem> m_items;
};

/// Delegate of the Recent list: the items by PDFRecentItemDelegate (a workspace
/// without the star - StarVisibleRole, workspaces are not pinned; a missing file
/// with the badge "No longer exists" - MissingRole; a document protected by a
/// password with the lock - LockedRole), the headers of the groups (group-header
/// of 80:910: the label Semi Bold 12 text/secondary and the count Regular 12
/// text/muted, 33 px; a frame of Main page, not a component) drawn by the delegate
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRecentDelegate : public PDFRecentItemDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFRecentItemDelegate;

public:
    static constexpr int HEADER_HEIGHT = 33;

    explicit PDFAppRecentDelegate(QObject* parent = nullptr);
    virtual ~PDFAppRecentDelegate() override;

    virtual QList<Action> getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

protected:
    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const override;
    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const override;
};

/// View of the Recent list: the paddings of the history card of 80:910 around
/// the rows (4 / 8 / 8 / 8, the vertical scroll bar beside them)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRecentListView : public PDFListView
{
    Q_OBJECT

private:
    using BaseClass = PDFListView;

public:
    explicit PDFAppRecentListView(QWidget* parent = nullptr);
    virtual ~PDFAppRecentListView() override;
};

/// Page Recent /recent (pdf4qt-recent 80:910, menu of the sort 976:12914, empty
/// state 955:142865, Clear history 941:135742, portable 955:150880 - the banner is
/// the one of the Home tab): the search by name or path, the sort (Last opened,
/// Name, Folder), Clear history…, the filters All / Files / Folders / Workspaces /
/// Pinned with their counts, the pinned files as cards, the history (files,
/// folders, workspaces) grouped by the day of opening, Continue where you left
/// off (the first file in progress, which exists) and the workspaces with
/// Restore last session. The filter, the sort and the search are the route
/// (/recent?filter=&sort=&q=): the current entry of the history of the tab is
/// replaced, the page stays.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRecentPage : public PDFAppHomeBasePage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppHomeBasePage;

public:
    /// Longest search text
    static constexpr int MAXIMUM_SEARCH_LENGTH = 256;

    /// Pinned cards shown above the history
    static constexpr int MAXIMUM_PINNED_CARDS = 3;

    /// Candidates of Continue where you left off
    static constexpr int MAXIMUM_CONTINUE_CANDIDATES = 5;

    enum class Filter
    {
        All,
        Files,
        Folders,
        Workspaces,
        Pinned
    };

    explicit PDFAppRecentPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppRecentPage() override;

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual QString getHeaderTitle() const override;
    virtual QString getHeaderSubtitle() const override;

    /// The storage banner is at the top of the main column (958:28036, 955:150880)
    virtual QBoxLayout* getBannerLayout() const override;
    virtual QVariantMap saveViewState() const override;
    virtual void restoreViewState(const QVariantMap& state) override;

    /// Route of the filter, the sort and the search (defaults are left out)
    static PDFAppRoute createRoute(Filter filter, PDFRecentSort sort, const QString& searchText);

    /// Ids of the route ("files", ...; All has none)
    static QString getFilterId(Filter filter);
    static std::optional<Filter> parseFilter(const QString& id);

    Filter getFilter() const { return m_filter; }
    PDFRecentSort getSort() const { return m_sort; }
    const QString& getSearchText() const { return m_searchText; }

    /// Sets the filter, the sort and the search (the route of the history follows)
    void setFilter(Filter filter, PDFRecentSort sort, const QString& searchText);

    // Parts of the page (tests)
    PDFLineEdit* getSearchField() const { return m_searchField; }
    PDFComboBox* getSortBox() const { return m_sortBox; }
    QPushButton* getClearHistoryButton() const { return m_clearButton; }
    QPushButton* getChip(Filter filter) const;
    QWidget* getPinnedSection() const { return m_pinnedSection; }
    QList<PDFPinnedCard*> getPinnedCards() const;
    PDFTextLink* getShowAllPinnedLink() const { return m_showAllPinned; }
    PDFAppRecentListView* getListView() const { return m_listView; }
    PDFAppRecentModel* getModel() const { return m_model; }
    PDFAppRecentDelegate* getDelegate() const { return m_delegate; }
    PDFAppEmptyState* getEmptyState() const { return m_emptyState; }
    bool isEmptyStateShown() const;
    QWidget* getHistoryCard() const { return m_historyCard; }
    QWidget* getContinueSection() const { return m_continueSection; }
    PDFAppContinueCard* getContinueCard() const { return m_continueCard; }
    PDFAppSessionRow* getSessionRow() const { return m_sessionRow; }
    QList<PDFWorkspaceRow*> getWorkspaceRows() const;
    QWidget* getWorkspacesCard() const { return m_workspacesCard; }
    PDFAppColumns* getColumns() const { return m_columns; }

    /// Menu of an item of the list shown by the menu button of the delegate (nullptr = none)
    QMenu* getListMenu() const { return m_listMenu; }

    /// Shows the menu of the item of the row of the list (at its menu button)
    void showListMenu(int row);

protected:
    virtual void refresh() override;
    virtual void onPathInfoChanged(const QString& path) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* createToolbar(QWidget* parent);
    QWidget* createPinned(QWidget* parent);
    QWidget* createHistory(QWidget* parent);
    QWidget* createContinue(QWidget* parent);
    QWidget* createWorkspaces(QWidget* parent);
    void refreshList();
    void refreshPinned(const std::vector<PDFRecentFile>& pinned);
    void refreshContinue();
    void refreshWorkspaces();
    void updateChips(int all, int files, int folders, int workspaces, int pinned);
    void updateEmptyState();
    void updateControls();
    void updateRouteInHistory();
    void applyPendingListScroll();
    void openItem(const QModelIndex& index);

    void onSearchTextEdited(const QString& text);
    void onSortActivated(int index);
    void onChipClicked(QAbstractButton* button);
    void onClearHistoryClicked();
    void onListClicked(const QModelIndex& index);
    void onDelegateAction(const QModelIndex& index, int action);
    void onPinRequested(const pdf::PDFAppHomeItem& item, bool isPinned);
    void onPinnedCardClicked();
    void onPinnedCardPinnedChanged(bool isPinned);
    void onContinueRequested();
    void onWorkspaceClicked();
    void onSessionRestoreRequested();
    void onBrowseRequested();

    Filter m_filter = Filter::All;
    PDFRecentSort m_sort = PDFRecentSort::LastOpened;
    QString m_searchText;
    bool m_isUpdatingControls = false;
    std::optional<int> m_pendingListScroll;

    QPointer<PDFAppColumns> m_columns;
    QPointer<PDFLineEdit> m_searchField;
    QPointer<PDFComboBox> m_sortBox;
    QPointer<QPushButton> m_clearButton;
    QPointer<QButtonGroup> m_chipGroup;
    std::map<int, QPointer<QPushButton>> m_chips;
    QPointer<QWidget> m_pinnedSection;
    QPointer<QWidget> m_pinnedGrid;
    QPointer<PDFTextLink> m_showAllPinned;
    QList<QPointer<PDFPinnedCard>> m_pinnedCards;
    QPointer<QWidget> m_historyCard;
    QPointer<QStackedWidget> m_historyStack;
    QPointer<PDFAppRecentListView> m_listView;
    QPointer<PDFAppRecentModel> m_model;
    QPointer<PDFAppRecentDelegate> m_delegate;
    QPointer<PDFAppEmptyState> m_emptyState;
    QPointer<QMenu> m_listMenu;
    QPointer<QWidget> m_continueSection;
    QPointer<PDFAppContinueCard> m_continueCard;
    std::vector<PDFRecentFile> m_continueCandidates;
    QPointer<QWidget> m_workspacesCard;
    QPointer<QVBoxLayout> m_workspacesLayout;
    QPointer<PDFAppSessionRow> m_sessionRow;
    QList<QPointer<PDFWorkspaceRow>> m_workspaceRows;
    QString m_pinnedSignature;
    QString m_workspacesSignature;
};

/// Factory of the page Recent
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRecentPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFAppHomeItem)

#endif // PDFAPPRECENTPAGE_H
