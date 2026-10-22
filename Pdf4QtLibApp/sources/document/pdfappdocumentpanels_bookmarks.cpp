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

#include "pdfappdocumentpanels_bookmarks.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumentview.h"
#include "pdfbookmarkrepository.h"
#include "pdfdatabasenotifier.h"
#include "pdfrepositoryhelper.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfcatalog.h"
#include "pdfcomponentdelegates.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMenu>
#include <QSaveFile>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace pdf
{

/// Helpers of the bookmarks
class PDFAppBookmarksHelper
{
public:
    PDFAppBookmarksHelper() = delete;

    /// Comparator of the items by their pages
    static bool isPageBefore(const PDFAppBookmarkItem& left, const PDFAppBookmarkItem& right)
    {
        return left.page < right.page;
    }

    static void showStatus(PDFAppDocumentView* view, const QString& text)
    {
        if (PDFAppDocumentStatusBar* statusBar = view ? view->getStatusBar() : nullptr)
        {
            statusBar->showMessage(text, 5000);
        }
    }

    /// The file name of the export of the bookmarks of the document
    static QString getExportPath(const PDFAppDocument* document)
    {
        const QFileInfo info(document->getFilePath());
        return info.dir().filePath(info.completeBaseName() + QStringLiteral("-bookmarks.json"));
    }
};

bool PDFAppBookmarks::canStore(PDFAppContext* context, const PDFAppDocument* document)
{
    return context && context->getDatabase() && document && document->isReady() && PDFRepositoryHelper::isValidStoredPath(document->getFilePath());
}

std::vector<PDFAppBookmarkItem> PDFAppBookmarks::getGeneratedItems(const PDFDocument* document)
{
    std::vector<PDFAppBookmarkItem> items;
    const QSharedPointer<PDFOutlineItem> root = document ? document->getCatalog()->getOutlineRootPtr() : QSharedPointer<PDFOutlineItem>();
    if (!root)
    {
        return items;
    }

    // The main chapters: the items of the first level of the outline
    const size_t count = qMin<size_t>(root->getChildCount(), size_t(PDFBookmarkRepository::MAXIMUM_BOOKMARKS));
    for (size_t i = 0; i < count; ++i)
    {
        const PDFOutlineItem* item = root->getChild(i);
        const PDFInteger page = item ? PDFAppPanelWidgets::getActionPage(document->getCatalog(), item->getAction()) : -1;
        if (page < 0)
        {
            continue;
        }

        PDFAppBookmarkItem bookmark;
        bookmark.page = page;
        bookmark.title = PDFBookmarkRepository::normalizeTitle(item->getTitle());
        bookmark.isAuto = true;
        items.push_back(std::move(bookmark));
    }

    std::stable_sort(items.begin(), items.end(), &PDFAppBookmarksHelper::isPageBefore);
    return items;
}

std::vector<PDFAppBookmarkItem> PDFAppBookmarks::getItems(PDFAppContext* context, const PDFAppDocument* document)
{
    std::vector<PDFAppBookmarkItem> items;
    PDFDocumentPointer pointer = document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
    if (!pointer)
    {
        return items;
    }

    const PDFInteger pageCount = PDFInteger(pointer->getCatalog()->getPageCount());
    std::vector<PDFAppBookmarkItem> personal;
    if (canStore(context, document))
    {
        for (const PDFBookmark& bookmark : PDFBookmarkRepository(context->getDatabase()).getBookmarks(document->getFilePath()))
        {
            if (bookmark.page < pageCount)
            {
                PDFAppBookmarkItem item;
                item.id = bookmark.id;
                item.page = bookmark.page;
                item.title = bookmark.title;
                personal.push_back(std::move(item));
            }
        }
    }

    std::vector<PDFAppBookmarkItem> generated;
    if (context && context->getSettings().getBool(PDFSettingKeys::bookmarksAutoGenerate()))
    {
        // A page with a personal bookmark has no generated one
        std::set<PDFInteger> personalPages;
        for (const PDFAppBookmarkItem& item : personal)
        {
            personalPages.insert(item.page);
        }
        for (PDFAppBookmarkItem& item : getGeneratedItems(pointer.data()))
        {
            if (!personalPages.count(item.page))
            {
                generated.push_back(std::move(item));
            }
        }
    }

    // A generated bookmark goes before the first personal bookmark of a later page
    size_t generatedIndex = 0;
    for (PDFAppBookmarkItem& item : personal)
    {
        while (generatedIndex < generated.size() && generated[generatedIndex].page < item.page)
        {
            items.push_back(generated[generatedIndex++]);
        }
        items.push_back(std::move(item));
    }
    while (generatedIndex < generated.size())
    {
        items.push_back(generated[generatedIndex++]);
    }
    return items;
}

QString PDFAppBookmarks::getDefaultTitle(const PDFDocument* document, PDFInteger page)
{
    const QSharedPointer<PDFOutlineItem> root = document ? document->getCatalog()->getOutlineRootPtr() : QSharedPointer<PDFOutlineItem>();
    if (!root)
    {
        return QString();
    }

    // The deepest item with the greatest page not after the page (pre-order,
    // without recursion, bounded)
    QString title;
    PDFInteger bestPage = -1;
    std::vector<const PDFOutlineItem*> stack;
    for (size_t i = root->getChildCount(); i > 0; --i)
    {
        stack.push_back(root->getChild(i - 1));
    }

    int visited = 0;
    while (!stack.empty() && visited < 100000)
    {
        const PDFOutlineItem* item = stack.back();
        stack.pop_back();
        ++visited;
        if (!item)
        {
            continue;
        }

        const PDFInteger itemPage = PDFAppPanelWidgets::getActionPage(document->getCatalog(), item->getAction());
        if (itemPage >= 0 && itemPage <= page && itemPage >= bestPage)
        {
            bestPage = itemPage;
            title = item->getTitle();
        }

        for (size_t i = item->getChildCount(); i > 0; --i)
        {
            stack.push_back(item->getChild(i - 1));
        }
    }
    return PDFBookmarkRepository::normalizeTitle(title);
}

void PDFAppBookmarks::showError(QWidget* parent, const QString& title, const QString& text)
{
    PDFMessageBox* box = new PDFMessageBox(PDFThemeIcons::Badge::Warning, title, text, QDialogButtonBox::Ok, parent ? parent->window() : nullptr);
    PDFAppPanelWidgets::prepareMessageBox(box, parent, QStringLiteral("bookmarkErrorBox"));
    box->open();
}

qint64 PDFAppBookmarks::addCurrentPage(PDFAppDocumentView* view, QWidget* parent)
{
    PDFAppDocument* document = view ? view->getDocument() : nullptr;
    PDFAppContext* context = view ? view->getContext() : nullptr;
    if (!canStore(context, document))
    {
        PDFAppBookmarksHelper::showStatus(view, QCoreApplication::translate("pdf::PDFAppBookmarks", "Bookmarks are kept for saved files only."));
        return 0;
    }

    const PDFInteger page = view->getCurrentPageIndex();
    if (page < 0)
    {
        return 0;
    }

    for (const PDFAppBookmarkItem& item : getItems(context, document))
    {
        if (!item.isAuto && item.page == page)
        {
            PDFAppBookmarksHelper::showStatus(view, QCoreApplication::translate("pdf::PDFAppBookmarks", "The page has a bookmark already."));
            return item.id;
        }
    }

    qint64 id = 0;
    const PDFDatabaseError error = PDFBookmarkRepository(context->getDatabase()).addBookmark(document->getFilePath(), page, getDefaultTitle(document->getDocument().data(), page), &id);
    if (error.isError())
    {
        showError(parent, QCoreApplication::translate("pdf::PDFAppBookmarks", "The bookmark was not added"), error.getMessage());
        return 0;
    }

    PDFAppBookmarksHelper::showStatus(view, QCoreApplication::translate("pdf::PDFAppBookmarks", "Bookmark added: %1").arg(PDFAppPanelWidgets::getPageTitle(document->getDocument()->getCatalog(), page)));
    return id;
}

bool PDFAppBookmarks::goToNext(PDFAppDocumentView* view, bool isForward)
{
    PDFAppDocument* document = view ? view->getDocument() : nullptr;
    if (!document || !document->isReady())
    {
        return false;
    }

    std::vector<PDFAppBookmarkItem> items = getItems(view->getContext(), document);
    std::stable_sort(items.begin(), items.end(), &PDFAppBookmarksHelper::isPageBefore);

    const PDFInteger currentPage = view->getCurrentPageIndex();
    const PDFAppBookmarkItem* target = nullptr;
    if (isForward)
    {
        for (const PDFAppBookmarkItem& item : items)
        {
            if (item.page > currentPage)
            {
                target = &item;
                break;
            }
        }
    }
    else
    {
        for (auto it = items.rbegin(); it != items.rend(); ++it)
        {
            if (it->page < currentPage)
            {
                target = &*it;
                break;
            }
        }
    }

    if (!target)
    {
        PDFAppBookmarksHelper::showStatus(view, isForward ? QCoreApplication::translate("pdf::PDFAppBookmarks", "There is no bookmark after this page.")
                                                          : QCoreApplication::translate("pdf::PDFAppBookmarks", "There is no bookmark before this page."));
        return false;
    }

    view->goToPage(target->page);
    return true;
}

void PDFAppBookmarks::exportBookmarks(PDFAppDocumentView* view, QWidget* parent)
{
    QPointer<PDFAppDocumentView> guard(view);
    PDFAppDocument* document = view ? view->getDocument() : nullptr;
    PDFAppContext* context = view ? view->getContext() : nullptr;
    if (!canStore(context, document))
    {
        PDFAppBookmarksHelper::showStatus(view, QCoreApplication::translate("pdf::PDFAppBookmarks", "Bookmarks are kept for saved files only."));
        return;
    }

    const std::vector<PDFBookmark> bookmarks = PDFBookmarkRepository(context->getDatabase()).getBookmarks(document->getFilePath());
    if (bookmarks.empty())
    {
        PDFAppBookmarksHelper::showStatus(view, QCoreApplication::translate("pdf::PDFAppBookmarks", "The document has no bookmarks to export."));
        return;
    }

    const QByteArray data = PDFBookmarkRepository::toJson(bookmarks);
    QPointer<QWidget> parentGuard(parent);
    const QString filePath = PDFAppPanelFileDialogs::getSaveFileName(parent, QCoreApplication::translate("pdf::PDFAppBookmarks", "Export Bookmarks"),
                                                                     PDFAppBookmarksHelper::getExportPath(document),
                                                                     QCoreApplication::translate("pdf::PDFAppBookmarks", "Bookmarks (*.json)"));
    if (filePath.isEmpty() || !guard || !parentGuard)
    {
        return;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        showError(parent, QCoreApplication::translate("pdf::PDFAppBookmarks", "The bookmarks were not exported"), file.errorString());
        return;
    }
    PDFAppBookmarksHelper::showStatus(view, (bookmarks.size() == 1 ? QCoreApplication::translate("pdf::PDFAppBookmarks", "%Ln bookmark exported.", nullptr, 1) : QCoreApplication::translate("pdf::PDFAppBookmarks", "%Ln bookmarks exported.", nullptr, int(bookmarks.size()))));
}

void PDFAppBookmarks::importBookmarks(PDFAppDocumentView* view, QWidget* parent)
{
    QPointer<PDFAppDocumentView> guard(view);
    PDFAppDocument* document = view ? view->getDocument() : nullptr;
    PDFAppContext* context = view ? view->getContext() : nullptr;
    if (!canStore(context, document))
    {
        PDFAppBookmarksHelper::showStatus(view, QCoreApplication::translate("pdf::PDFAppBookmarks", "Bookmarks are kept for saved files only."));
        return;
    }

    QPointer<QWidget> parentGuard(parent);
    QPointer<PDFAppDocument> documentGuard(document);
    const QString filePath = PDFAppPanelFileDialogs::getOpenFileName(parent, QCoreApplication::translate("pdf::PDFAppBookmarks", "Import Bookmarks"),
                                                                     QFileInfo(document->getFilePath()).absolutePath(),
                                                                     QCoreApplication::translate("pdf::PDFAppBookmarks", "Bookmarks (*.json)"));
    if (filePath.isEmpty() || !guard || !parentGuard || !documentGuard || !documentGuard->isReady())
    {
        return;
    }

    const QString title = QCoreApplication::translate("pdf::PDFAppBookmarks", "The bookmarks were not imported");
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        showError(parent, title, file.errorString());
        return;
    }

    // The file is untrusted: its size is limited before it is read
    if (file.size() > PDFBookmarkRepository::MAXIMUM_IMPORT_SIZE)
    {
        showError(parent, title, QCoreApplication::translate("pdf::PDFBookmarkRepository", "The file is too large."));
        return;
    }
    const QByteArray data = file.read(PDFBookmarkRepository::MAXIMUM_IMPORT_SIZE + 1);

    std::vector<PDFBookmarkEntry> entries;
    QString error;
    if (!PDFBookmarkRepository::fromJson(data, documentGuard->getPageCount(), &entries, &error))
    {
        showError(parent, title, error);
        return;
    }

    int added = 0;
    const PDFDatabaseError databaseError = PDFBookmarkRepository(context->getDatabase()).importBookmarks(documentGuard->getFilePath(), entries, &added);
    if (databaseError.isError())
    {
        showError(parent, title, databaseError.getMessage());
        return;
    }
    PDFAppBookmarksHelper::showStatus(view, (added == 1 ? QCoreApplication::translate("pdf::PDFAppBookmarks", "%Ln bookmark imported.", nullptr, added) : QCoreApplication::translate("pdf::PDFAppBookmarks", "%Ln bookmarks imported.", nullptr, added)));
}

PDFAppBookmarksModel::PDFAppBookmarksModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppBookmarksModel::~PDFAppBookmarksModel() = default;

void PDFAppBookmarksModel::setItems(std::vector<PDFAppBookmarkItem> items, PDFDocumentPointer document)
{
    beginResetModel();
    m_items = std::move(items);
    m_document = std::move(document);
    endResetModel();
}

const PDFAppBookmarkItem* PDFAppBookmarksModel::getItem(int row) const
{
    return (row >= 0 && row < int(m_items.size())) ? &m_items[size_t(row)] : nullptr;
}

int PDFAppBookmarksModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

Qt::ItemFlags PDFAppBookmarksModel::flags(const QModelIndex& index) const
{
    return index.isValid() ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren) : Qt::NoItemFlags;
}

QVariant PDFAppBookmarksModel::data(const QModelIndex& index, int role) const
{
    const PDFAppBookmarkItem* item = index.isValid() ? getItem(index.row()) : nullptr;
    if (!item || !m_document)
    {
        return QVariant();
    }

    switch (role)
    {
        case Qt::DisplayRole:
            return PDFAppPanelWidgets::getPageTitle(m_document->getCatalog(), item->page);

        case PDFComponentRoles::SubtitleRole:
            return item->title;

        case PDFComponentRoles::TagRole:
            return item->isAuto ? PDFAppBookmarksPanel::tr("Auto") : QString();

        case Qt::ToolTipRole:
        case Qt::AccessibleTextRole:
        {
            const QString page = PDFAppPanelWidgets::getPageTitle(m_document->getCatalog(), item->page);
            return item->title.isEmpty() ? page : QStringLiteral("%1 – %2").arg(page, item->title);
        }

        default:
            break;
    }

    return QVariant();
}

PDFAppRenameBookmarkDialog::PDFAppRenameBookmarkDialog(PDFAppContext* context, qint64 bookmarkId, const QString& title, QWidget* parent) :
    BaseClass(tr("Rename bookmark"), QString(), title, tr("Rename"), parent),
    m_context(context),
    m_bookmarkId(bookmarkId)
{
    setObjectName(QStringLiteral("renameBookmarkDialog"));
}

PDFAppRenameBookmarkDialog::~PDFAppRenameBookmarkDialog() = default;

bool PDFAppRenameBookmarkDialog::submitName(const QString& name, QString* error)
{
    if (!m_context || !m_context->getDatabase())
    {
        *error = tr("The bookmarks are not available.");
        return false;
    }

    const PDFDatabaseError databaseError = PDFBookmarkRepository(m_context->getDatabase()).renameBookmark(m_bookmarkId, name);
    if (databaseError.isError())
    {
        *error = databaseError.getMessage();
        return false;
    }
    return true;
}

PDFAppBookmarksPanel::PDFAppBookmarksPanel(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("bookmarksPanel"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // sidebar-content 195:3853: rows with the gap 2
    m_view = new PDFAppPanelListView(this);
    m_view->setObjectName(QStringLiteral("bookmarksView"));
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setAccessibleName(tr("Bookmarks"));
    PDFAppPanelWidgets::prepareList(m_view);

    m_model = new PDFAppBookmarksModel(m_view);
    m_delegate = new PDFBookmarkDelegate(m_view);
    m_view->setModel(m_model);
    m_view->setItemDelegate(m_delegate);
    m_view->installEventFilter(this);
    layout->addWidget(m_view, 1);

    connect(m_view, &QAbstractItemView::clicked, this, &PDFAppBookmarksPanel::onClicked);
    connect(m_view, &QWidget::customContextMenuRequested, this, &PDFAppBookmarksPanel::onContextMenuRequested);
}

PDFAppBookmarksPanel::~PDFAppBookmarksPanel()
{
    if (m_connectedDocument)
    {
        m_connectedDocument->disconnect(this);
    }
    if (m_connectedView)
    {
        m_connectedView->disconnect(this);
    }
    if (m_renameDialog)
    {
        m_renameDialog->disconnect(this);
        m_renameDialog->close();
    }
}

QString PDFAppBookmarksPanel::getId() const
{
    return PDFAppDocumentPanelIds::bookmarks();
}

QString PDFAppBookmarksPanel::getTitle() const
{
    return tr("Bookmarks");
}

QString PDFAppBookmarksPanel::getSubtitle() const
{
    // 195:3445 "7 bookmarks"
    const int count = m_model ? m_model->rowCount() : 0;
    return count > 0 ? (count == 1 ? tr("%Ln bookmark", nullptr, count) : tr("%Ln bookmarks", nullptr, count)) : tr("No bookmarks");
}

QString PDFAppBookmarksPanel::getIconName() const
{
    return QStringLiteral("bookmark");
}

PDFAppContext* PDFAppBookmarksPanel::getContext() const
{
    PDFAppDocumentView* view = getView();
    return view ? view->getContext() : nullptr;
}

QWidget* PDFAppBookmarksPanel::createHeaderWidget(QWidget* parent)
{
    // bookmark-add 195:3845 and bookmark-more 195:3849 (32 x 32, gap 8)
    QWidget* widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("bookmarksHeader"));
    QHBoxLayout* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_addButton = PDFAppPanelWidgets::createHeaderButton(widget, QStringLiteral("bookmark-plus"), tr("Add bookmark"), QStringLiteral("bookmarkAdd"));
    m_moreButton = PDFAppPanelWidgets::createHeaderButton(widget, QStringLiteral("more-horizontal"), tr("More"), QStringLiteral("bookmarkMore"));
    layout->addWidget(m_addButton);
    layout->addWidget(m_moreButton);
    connect(m_addButton, &QToolButton::clicked, this, &PDFAppBookmarksPanel::onAddClicked);
    connect(m_moreButton, &QToolButton::clicked, this, &PDFAppBookmarksPanel::onMoreClicked);
    updateButtons();
    return widget;
}

void PDFAppBookmarksPanel::setDocument(PDFAppDocument* document, PDFAppDocumentView* view)
{
    if (m_connectedDocument && m_connectedDocument != document)
    {
        m_connectedDocument->disconnect(this);
    }
    if (m_connectedView && m_connectedView != view)
    {
        m_connectedView->disconnect(this);
    }

    const bool isNewView = view && m_connectedView != view;
    BaseClass::setDocument(document, view);

    if (document && m_connectedDocument != document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppBookmarksPanel::onDocumentChanged);
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppBookmarksPanel::reload);
        connect(document, &QObject::destroyed, this, &PDFAppBookmarksPanel::onDocumentDestroyed);
    }
    if (isNewView)
    {
        connect(view, &PDFAppDocumentView::currentPageChanged, this, &PDFAppBookmarksPanel::onCurrentPageChanged);
        PDFAppContext* context = view->getContext();
        if (context && context->getNotifier())
        {
            connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppBookmarksPanel::onStorageChanged);
            connect(context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppBookmarksPanel::onEverythingChanged);
        }
    }
    m_connectedDocument = document;
    m_connectedView = view;
    reload();
}

void PDFAppBookmarksPanel::reload()
{
    if (!m_model)
    {
        return;
    }

    PDFAppDocument* document = getDocument();
    PDFDocumentPointer pointer = document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
    m_model->setItems(PDFAppBookmarks::getItems(getContext(), document), pointer);
    selectCurrentPage();
    updateButtons();
    Q_EMIT titleChanged();
}

void PDFAppBookmarksPanel::onDocumentChanged(const PDFModifiedDocument& modified)
{
    Q_UNUSED(modified);
    reload();
}

void PDFAppBookmarksPanel::onDocumentDestroyed()
{
    m_connectedDocument = nullptr;
    if (m_model)
    {
        m_model->setItems({}, PDFDocumentPointer());
    }
}

void PDFAppBookmarksPanel::onCurrentPageChanged(PDFInteger pageIndex)
{
    Q_UNUSED(pageIndex);
    selectCurrentPage();
}

void PDFAppBookmarksPanel::onStorageChanged(const QString& topic, const QString& key)
{
    PDFAppDocument* document = getDocument();
    if (topic == PDFBookmarkRepository::getTopic())
    {
        if (document && PDFRepositoryHelper::isSamePath(key, document->getFilePath()))
        {
            reload();
        }
    }
    else if (topic == PDFSettingsRepository::getTopic() && key == PDFSettingKeys::bookmarksAutoGenerate())
    {
        reload();
    }
}

void PDFAppBookmarksPanel::onEverythingChanged()
{
    reload();
}

void PDFAppBookmarksPanel::selectCurrentPage()
{
    PDFAppDocumentView* view = getView();
    if (!m_view || !m_model || !m_view->selectionModel())
    {
        return;
    }

    // The bookmark of the current page is selected (195:3445 "Page 308")
    const PDFInteger page = view ? view->getCurrentPageIndex() : -1;
    const bool wasSelecting = m_isSelecting;
    m_isSelecting = true;
    m_view->selectionModel()->clearSelection();
    for (int row = 0; row < m_model->rowCount(); ++row)
    {
        if (m_model->getItem(row)->page == page)
        {
            m_view->selectionModel()->setCurrentIndex(m_model->index(row), QItemSelectionModel::ClearAndSelect);
            break;
        }
    }
    m_isSelecting = wasSelecting;
}

void PDFAppBookmarksPanel::updateButtons()
{
    const bool canStore = PDFAppBookmarks::canStore(getContext(), getDocument());
    if (m_addButton)
    {
        m_addButton->setEnabled(canStore);
    }
}

void PDFAppBookmarksPanel::onClicked(const QModelIndex& index)
{
    if (index.isValid() && !m_isSelecting)
    {
        goToBookmark(index.row());
    }
}

void PDFAppBookmarksPanel::goToBookmark(int row)
{
    const PDFAppBookmarkItem* item = m_model ? m_model->getItem(row) : nullptr;
    PDFAppDocumentView* view = getView();
    if (item && view)
    {
        view->goToPage(item->page);
    }
}

void PDFAppBookmarksPanel::renameBookmark(int row)
{
    const PDFAppBookmarkItem* item = m_model ? m_model->getItem(row) : nullptr;
    PDFAppContext* context = getContext();
    if (!item || item->isAuto || !context)
    {
        return;
    }

    if (m_renameDialog)
    {
        m_renameDialog->disconnect(this);
        m_renameDialog->close();
    }

    PDFAppRenameBookmarkDialog* dialog = new PDFAppRenameBookmarkDialog(context, item->id, item->title, window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_renameDialog = dialog;
    dialog->open();
}

void PDFAppBookmarksPanel::moveBookmark(int row, int offset)
{
    const PDFAppBookmarkItem* item = m_model ? m_model->getItem(row) : nullptr;
    PDFAppContext* context = getContext();
    PDFAppDocument* document = getDocument();
    if (!item || item->isAuto || !PDFAppBookmarks::canStore(context, document))
    {
        return;
    }

    // The order of the personal bookmarks (the generated ones are not moved)
    std::vector<qint64> ids;
    for (const PDFAppBookmarkItem& current : m_model->getItems())
    {
        if (!current.isAuto)
        {
            ids.push_back(current.id);
        }
    }

    auto it = std::find(ids.begin(), ids.end(), item->id);
    if (it == ids.end())
    {
        return;
    }

    const ptrdiff_t position = it - ids.begin();
    const ptrdiff_t target = position + offset;
    if (target < 0 || target >= ptrdiff_t(ids.size()))
    {
        return;
    }

    std::swap(ids[size_t(position)], ids[size_t(target)]);
    const PDFDatabaseError error = PDFBookmarkRepository(context->getDatabase()).setOrder(document->getFilePath(), ids);
    if (error.isError())
    {
        PDFAppBookmarks::showError(this, tr("The bookmarks were not moved"), error.getMessage());
    }
}

void PDFAppBookmarksPanel::removeBookmark(int row)
{
    const PDFAppBookmarkItem* item = m_model ? m_model->getItem(row) : nullptr;
    PDFAppContext* context = getContext();
    if (!item || item->isAuto || !context || !context->getDatabase())
    {
        return;
    }

    const PDFDatabaseError error = PDFBookmarkRepository(context->getDatabase()).removeBookmark(item->id);
    if (error.isError())
    {
        PDFAppBookmarks::showError(this, tr("The bookmark was not removed"), error.getMessage());
    }
}

QMenu* PDFAppBookmarksPanel::createContextMenu(int row)
{
    const PDFAppBookmarkItem* item = m_model ? m_model->getItem(row) : nullptr;
    if (!item)
    {
        return nullptr;
    }

    m_menuRow = row;
    QMenu* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("bookmarkContextMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);

    QAction* goTo = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("corner-down-right")), tr("Go to Page"));
    goTo->setObjectName(QStringLiteral("goToBookmark"));
    connect(goTo, &QAction::triggered, this, &PDFAppBookmarksPanel::onMenuGoTo);

    if (!item->isAuto)
    {
        int personalIndex = 0;
        int personalCount = 0;
        for (const PDFAppBookmarkItem& current : m_model->getItems())
        {
            if (!current.isAuto)
            {
                if (current.id == item->id)
                {
                    personalIndex = personalCount;
                }
                ++personalCount;
            }
        }

        QAction* rename = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("rename")), tr("Rename…"));
        rename->setObjectName(QStringLiteral("renameBookmark"));
        connect(rename, &QAction::triggered, this, &PDFAppBookmarksPanel::onMenuRename);

        menu->addSeparator();
        QAction* moveUp = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("arrow-up")), tr("Move Up"));
        moveUp->setObjectName(QStringLiteral("moveBookmarkUp"));
        moveUp->setEnabled(personalIndex > 0);
        connect(moveUp, &QAction::triggered, this, &PDFAppBookmarksPanel::onMenuMoveUp);
        QAction* moveDown = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("arrow-down")), tr("Move Down"));
        moveDown->setObjectName(QStringLiteral("moveBookmarkDown"));
        moveDown->setEnabled(personalIndex + 1 < personalCount);
        connect(moveDown, &QAction::triggered, this, &PDFAppBookmarksPanel::onMenuMoveDown);

        menu->addSeparator();
        QAction* remove = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("trash")), tr("Remove"));
        remove->setObjectName(QStringLiteral("removeBookmark"));
        remove->setProperty(PDFWidgetStyleProperties::DESTRUCTIVE, true);
        connect(remove, &QAction::triggered, this, &PDFAppBookmarksPanel::onMenuRemove);
    }
    return menu;
}

QMenu* PDFAppBookmarksPanel::createMoreMenu()
{
    QMenu* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("bookmarkMoreMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);

    const bool canStore = PDFAppBookmarks::canStore(getContext(), getDocument());
    QAction* exportAction = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("export")), tr("Export bookmarks…"));
    exportAction->setObjectName(QStringLiteral("exportBookmarks"));
    exportAction->setEnabled(canStore);
    connect(exportAction, &QAction::triggered, this, &PDFAppBookmarksPanel::onExport);

    QAction* importAction = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("import")), tr("Import bookmarks…"));
    importAction->setObjectName(QStringLiteral("importBookmarks"));
    importAction->setEnabled(canStore);
    connect(importAction, &QAction::triggered, this, &PDFAppBookmarksPanel::onImport);

    menu->addSeparator();
    QAction* generate = menu->addAction(tr("Generate automatically"));
    generate->setObjectName(QStringLiteral("generateBookmarks"));
    generate->setCheckable(true);
    PDFAppContext* context = getContext();
    generate->setChecked(context && context->getSettings().getBool(PDFSettingKeys::bookmarksAutoGenerate()));
    generate->setEnabled(context && context->getDatabase());
    connect(generate, &QAction::toggled, this, &PDFAppBookmarksPanel::onGenerateToggled);
    return menu;
}

void PDFAppBookmarksPanel::onContextMenuRequested(const QPoint& position)
{
    if (!m_view)
    {
        return;
    }

    QModelIndex index = m_view->indexAt(position);
    QPoint globalPosition = m_view->viewport()->mapToGlobal(position);
    if (!index.isValid())
    {
        // The keyboard: the menu of the current row
        index = m_view->currentIndex();
        const QRect rect = index.isValid() ? m_view->visualRect(index) : QRect();
        if (!rect.isValid())
        {
            return;
        }
        globalPosition = m_view->viewport()->mapToGlobal(rect.center());
    }

    if (QMenu* menu = createContextMenu(index.row()))
    {
        menu->popup(globalPosition);
    }
}

void PDFAppBookmarksPanel::onAddClicked()
{
    PDFAppBookmarks::addCurrentPage(getView(), this);
}

void PDFAppBookmarksPanel::onMoreClicked()
{
    if (!m_moreButton)
    {
        return;
    }

    QMenu* menu = createMoreMenu();
    const QRect anchor(m_moreButton->mapToGlobal(QPoint(0, 0)), m_moreButton->size());
    menu->setProperty(PDFWidgetStyleProperties::POPUP_ANCHOR, anchor);
    menu->popup(anchor.bottomLeft());
}

void PDFAppBookmarksPanel::onExport()
{
    PDFAppBookmarks::exportBookmarks(getView(), this);
}

void PDFAppBookmarksPanel::onImport()
{
    PDFAppBookmarks::importBookmarks(getView(), this);
}

void PDFAppBookmarksPanel::onGenerateToggled(bool checked)
{
    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase())
    {
        return;
    }

    const PDFDatabaseError error = context->getSettings().setValue(PDFSettingKeys::bookmarksAutoGenerate(), checked);
    if (error.isError())
    {
        PDFAppBookmarks::showError(this, tr("The setting was not saved"), error.getMessage());
    }
}

void PDFAppBookmarksPanel::onMenuGoTo()
{
    goToBookmark(m_menuRow);
}

void PDFAppBookmarksPanel::onMenuRename()
{
    renameBookmark(m_menuRow);
}

void PDFAppBookmarksPanel::onMenuMoveUp()
{
    moveBookmark(m_menuRow, -1);
}

void PDFAppBookmarksPanel::onMenuMoveDown()
{
    moveBookmark(m_menuRow, 1);
}

void PDFAppBookmarksPanel::onMenuRemove()
{
    removeBookmark(m_menuRow);
}

bool PDFAppBookmarksPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view && m_view && event->type() == QEvent::KeyPress)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        const QModelIndex current = m_view->currentIndex();
        if (current.isValid() && keyEvent->modifiers() == Qt::NoModifier)
        {
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            {
                goToBookmark(current.row());
                return true;
            }
            if (keyEvent->key() == Qt::Key_Delete)
            {
                removeBookmark(current.row());
                return true;
            }
            if (keyEvent->key() == Qt::Key_F2)
            {
                renameBookmark(current.row());
                return true;
            }
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppBookmarksPanel::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        if (m_addButton)
        {
            m_addButton->setToolTip(tr("Add bookmark"));
            m_addButton->setAccessibleName(tr("Add bookmark"));
        }
        if (m_moreButton)
        {
            m_moreButton->setToolTip(tr("More"));
            m_moreButton->setAccessibleName(tr("More"));
        }
        if (m_view)
        {
            m_view->setAccessibleName(tr("Bookmarks"));
        }
        Q_EMIT titleChanged();
    }
}

QString PDFAppBookmarksPanelFactory::getId() const
{
    return PDFAppDocumentPanelIds::bookmarks();
}

int PDFAppBookmarksPanelFactory::getOrder() const
{
    return 70;
}

QString PDFAppBookmarksPanelFactory::getTitle() const
{
    return PDFAppBookmarksPanel::tr("Bookmarks");
}

QString PDFAppBookmarksPanelFactory::getIconName() const
{
    return QStringLiteral("bookmark");
}

PDFAppDocumentPanel* PDFAppBookmarksPanelFactory::createPanel(QWidget* parent) const
{
    return new PDFAppBookmarksPanel(parent);
}

}   // namespace pdf
