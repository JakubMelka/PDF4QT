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

#include "pdfapphomebasepage.h"
#include "pdfappdocumentregistry.h"
#include "pdfappcommand.h"
#include "pdfappfiledrop.h"
#include "pdfapphomedialogs.h"
#include "pdfapphomewidgets.h"
#include "pdfappmainwindow.h"
#include "pdfappminuteticker.h"
#include "pdfappsysteminfo.h"
#include "pdfappthumbnailer.h"
#include "pdfdatabasenotifier.h"
#include "pdfrepositoryhelper.h"
#include "pdfshortcutrepository.h"
#include "pdftoolrepository.h"
#include "pdfworkspacerepository.h"

#include "pdfcomponentbuttons.h"
#include "pdfmessagebox.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QMenu>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>

namespace pdf
{

/// Helpers of the base of the pages Home, Open and Recent
class PDFAppHomeBasePageHelper
{
public:
    PDFAppHomeBasePageHelper() = delete;

    /// Most paths, whose information is kept for one showing of the page
    static constexpr int MAXIMUM_PATH_INFOS = 4096;

    static QVariant toData(const PDFAppHomeItem& item, PDFAppHomeAction action)
    {
        return QVariantList({ int(item.kind), int(action), item.path, item.id, item.name, item.isPinned, item.isRemovable, item.documentCount });
    }

    static bool fromData(const QVariant& data, PDFAppHomeItem* item, PDFAppHomeAction* action)
    {
        const QVariantList list = data.toList();
        if (list.size() != 8)
        {
            return false;
        }

        const int kind = list[0].toInt();
        const int actionValue = list[1].toInt();
        if (kind < int(PDFAppHomeItemKind::File) || kind > int(PDFAppHomeItemKind::Location) ||
            actionValue < int(PDFAppHomeAction::Open) || actionValue > int(PDFAppHomeAction::RemoveLocation))
        {
            return false;
        }

        item->kind = static_cast<PDFAppHomeItemKind>(kind);
        *action = static_cast<PDFAppHomeAction>(actionValue);
        item->path = list[2].toString();
        item->id = list[3].toLongLong();
        item->name = list[4].toString();
        item->isPinned = list[5].toBool();
        item->isRemovable = list[6].toBool();
        item->documentCount = list[7].toInt();
        return true;
    }

    /// The widget was hidden by the code (not only a new widget, which its layout shows later)
    static bool isHiddenExplicitly(const QWidget* widget)
    {
        return widget->isHidden() && widget->testAttribute(Qt::WA_WState_ExplicitShowHide);
    }

    static bool isSeparatedAction(PDFAppHomeAction action)
    {
        return action == PDFAppHomeAction::RemoveFromList || action == PDFAppHomeAction::Delete || action == PDFAppHomeAction::RemoveLocation;
    }
};

PDFAppHomeBasePage::PDFAppHomeBasePage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent)
{
    // The notifications of a burst (a thumbnail, a pin, another instance) read the database once
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(0);
    connect(m_refreshTimer, &QTimer::timeout, this, &PDFAppHomeBasePage::onRefreshTimer);

    // Requests refused by the busy folder information service are asked again
    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    m_retryTimer->setInterval(RETRY_INTERVAL);
    connect(m_retryTimer, &QTimer::timeout, this, &PDFAppHomeBasePage::onRetryTimer);

    // O25: the times of recent activity follow the clock while the page is visible
    m_minuteSubscription = new PDFAppMinuteSubscription(context ? context->getMinuteTicker() : nullptr, this);
    connect(m_minuteSubscription, &PDFAppMinuteSubscription::minuteChanged, this, &PDFAppHomeBasePage::onMinuteChanged);

    if (context)
    {
        if (PDFDatabaseNotifier* notifier = context->getNotifier())
        {
            connect(notifier, &PDFDatabaseNotifier::changed, this, &PDFAppHomeBasePage::onDatabaseChanged);
            connect(notifier, &PDFDatabaseNotifier::everythingChanged, this, &PDFAppHomeBasePage::onEverythingChanged);
        }
        if (PDFAppFolderInfoService* folderInfo = context->getFolderInfo())
        {
            connect(folderInfo, &PDFAppFolderInfoService::infoReady, this, &PDFAppHomeBasePage::onInfoReady);
        }

        // The open documents (Save open documents as workspace) follow the document tabs
        if (PDFAppDocumentRegistry* registry = context->getDocumentRegistry())
        {
            connect(registry, &PDFAppDocumentRegistry::documentAdded, this, &PDFAppHomeBasePage::scheduleRefresh);
            connect(registry, &PDFAppDocumentRegistry::documentRemoved, this, &PDFAppHomeBasePage::scheduleRefresh);
        }
    }
}

PDFAppHomeBasePage::~PDFAppHomeBasePage()
{
    // A dialog of the page lives in the window: it goes with the page
    for (QPointer<QDialog>* dialog : { &m_dialog, &m_errorBox })
    {
        if (QDialog* openDialog = dialog->data())
        {
            openDialog->disconnect(this);
            openDialog->hide();
            openDialog->deleteLater();
        }
    }
}

QList<PDFAppHomeAction> PDFAppHomeBasePage::getMenuActions(const PDFAppHomeItem& item)
{
    QList<PDFAppHomeAction> actions;
    switch (item.kind)
    {
        case PDFAppHomeItemKind::File:
            actions << PDFAppHomeAction::Open << (item.isPinned ? PDFAppHomeAction::Unpin : PDFAppHomeAction::Pin) << PDFAppHomeAction::ShowInFolder
                    << PDFAppHomeAction::CopyPath << PDFAppHomeAction::RemoveFromList;
            break;

        case PDFAppHomeItemKind::Folder:
            actions << PDFAppHomeAction::BrowseHere << PDFAppHomeAction::ShowInFolder << PDFAppHomeAction::CopyPath
                    << (item.isPinned ? PDFAppHomeAction::UnpinLocation : PDFAppHomeAction::PinLocation);
            if (item.isRemovable)
            {
                actions << PDFAppHomeAction::RemoveFromList;
            }
            break;

        case PDFAppHomeItemKind::Workspace:
            actions << PDFAppHomeAction::Open << PDFAppHomeAction::Rename << PDFAppHomeAction::Delete;
            break;

        case PDFAppHomeItemKind::Location:
            actions << PDFAppHomeAction::BrowseHere << PDFAppHomeAction::ShowInFolder << PDFAppHomeAction::CopyPath << PDFAppHomeAction::Rename
                    << PDFAppHomeAction::RemoveLocation;
            break;
    }
    return actions;
}

QString PDFAppHomeBasePage::getActionText(PDFAppHomeAction action)
{
    switch (action)
    {
        case PDFAppHomeAction::Open:
            return tr("Open");
        case PDFAppHomeAction::Pin:
            return tr("Pin");
        case PDFAppHomeAction::Unpin:
            return tr("Unpin");
        case PDFAppHomeAction::ShowInFolder:
            return tr("Show in Folder");
        case PDFAppHomeAction::CopyPath:
            return tr("Copy Path");
        case PDFAppHomeAction::RemoveFromList:
            return tr("Remove from List");
        case PDFAppHomeAction::BrowseHere:
            return tr("Browse files here…");
        case PDFAppHomeAction::PinLocation:
            return tr("Pin to Pinned Locations");
        case PDFAppHomeAction::UnpinLocation:
            return tr("Unpin from Pinned Locations");
        case PDFAppHomeAction::Rename:
            return tr("Rename…");
        case PDFAppHomeAction::Delete:
            return tr("Delete…");
        case PDFAppHomeAction::RemoveLocation:
            return tr("Remove from Pinned Locations");
    }
    return QString();
}

QString PDFAppHomeBasePage::getActionIconName(PDFAppHomeAction action)
{
    switch (action)
    {
        case PDFAppHomeAction::Open:
        case PDFAppHomeAction::BrowseHere:
            return QStringLiteral("folder-open");
        case PDFAppHomeAction::Pin:
        case PDFAppHomeAction::PinLocation:
            return QStringLiteral("star");
        case PDFAppHomeAction::Unpin:
        case PDFAppHomeAction::UnpinLocation:
            return QStringLiteral("star-filled");
        case PDFAppHomeAction::ShowInFolder:
            return QStringLiteral("show-in-folder");
        case PDFAppHomeAction::CopyPath:
            return QStringLiteral("copy");
        case PDFAppHomeAction::RemoveFromList:
        case PDFAppHomeAction::RemoveLocation:
            return QStringLiteral("list-remove");
        case PDFAppHomeAction::Rename:
            return QStringLiteral("rename");
        case PDFAppHomeAction::Delete:
            return QStringLiteral("trash");
    }
    return QString();
}

QMenu* PDFAppHomeBasePage::createItemMenu(const PDFAppHomeItem& item, QWidget* parent)
{
    QMenu* menu = new QMenu(parent ? parent : this);
    menu->setObjectName(QStringLiteral("itemMenu"));

    for (PDFAppHomeAction action : getMenuActions(item))
    {
        if (PDFAppHomeBasePageHelper::isSeparatedAction(action) && !menu->isEmpty())
        {
            menu->addSeparator();
        }

        QAction* menuAction = menu->addAction(PDFThemeIcons::getIcon(getActionIconName(action)), getActionText(action));
        menuAction->setData(PDFAppHomeBasePageHelper::toData(item, action));
        if (action == PDFAppHomeAction::Delete)
        {
            menuAction->setProperty(PDFWidgetStyleProperties::DESTRUCTIVE, true);
        }
        connect(menuAction, &QAction::triggered, this, &PDFAppHomeBasePage::onMenuActionTriggered);
    }
    return menu;
}

void PDFAppHomeBasePage::onMenuActionTriggered()
{
    // The actions do not identify themselves in the signal: the item is their data
    const QAction* action = qobject_cast<const QAction*>(sender());
    PDFAppHomeItem item;
    PDFAppHomeAction homeAction = PDFAppHomeAction::Open;
    if (!action || !PDFAppHomeBasePageHelper::fromData(action->data(), &item, &homeAction))
    {
        return;
    }

    performAction(item, homeAction);
}

bool PDFAppHomeBasePage::performAction(const PDFAppHomeItem& item, PDFAppHomeAction action)
{
    QPointer<PDFAppHomeBasePage> guard(this);
    PDFAppContext* context = getContext();
    const bool hasDatabase = context && context->getDatabase();
    const bool isFile = item.kind == PDFAppHomeItemKind::File;

    switch (action)
    {
        case PDFAppHomeAction::Open:
            if (item.kind == PDFAppHomeItemKind::Workspace)
            {
                openWorkspace(item.id);
            }
            else if (isFile)
            {
                openFile(item.path);
            }
            else
            {
                browseFiles(item.path);
            }
            break;

        case PDFAppHomeAction::Pin:
        case PDFAppHomeAction::Unpin:
            setFilePinned(item.path, action == PDFAppHomeAction::Pin);
            break;

        case PDFAppHomeAction::ShowInFolder:
        {
            // A file is shown by its folder; the navigator opens only an existing local folder
            const QString folder = isFile ? PDFRepositoryHelper::getFolder(item.path) : item.path;
            if (!folder.isEmpty())
            {
                openExternalUrl(QUrl::fromLocalFile(folder));
            }
            break;
        }

        case PDFAppHomeAction::CopyPath:
            if (QClipboard* clipboard = QGuiApplication::clipboard())
            {
                clipboard->setText(QDir::toNativeSeparators(item.path));
            }
            break;

        case PDFAppHomeAction::RemoveFromList:
            if (hasDatabase)
            {
                showDatabaseError(isFile ? context->getRecent().removeFile(item.path) : context->getRecent().removeFolder(item.path));
            }
            break;

        case PDFAppHomeAction::BrowseHere:
            browseFiles(item.path);
            break;

        case PDFAppHomeAction::PinLocation:
        case PDFAppHomeAction::UnpinLocation:
            setFolderPinned(item.path, action == PDFAppHomeAction::PinLocation);
            break;

        case PDFAppHomeAction::Rename:
            if (item.kind == PDFAppHomeItemKind::Workspace)
            {
                showRenameWorkspaceDialog(item.id);
            }
            else
            {
                showRenameLocationDialog(item.id);
            }
            break;

        case PDFAppHomeAction::Delete:
            showDeleteWorkspaceDialog(item.id);
            break;

        case PDFAppHomeAction::RemoveLocation:
            if (hasDatabase)
            {
                showDatabaseError(context->getRecent().removePinnedLocation(item.id));
            }
            break;
    }

    return !guard.isNull();
}

void PDFAppHomeBasePage::openFile(const QString& path, int page)
{
    if (!PDFRepositoryHelper::isValidStoredPath(path))
    {
        return;
    }
    PDFAppDocumentActions::openFiles(this, { path }, page);
}

void PDFAppHomeBasePage::browseFiles(const QString& folder)
{
    // The command Open… of the window: the window opens the files, also when the
    // page is gone meanwhile (the native dialog runs its own event loop); a page
    // outside of a window has nothing to open them
    if (PDFAppMainWindow* window = qobject_cast<PDFAppMainWindow*>(this->window()))
    {
        window->browseDocuments(folder);
    }
}

void PDFAppHomeBasePage::openWorkspace(qint64 id)
{
    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase())
    {
        return;
    }

    const std::optional<PDFWorkspace> workspace = context->getWorkspaces().getWorkspace(id);
    if (!workspace)
    {
        // Deleted by another instance meanwhile
        scheduleRefresh();
        return;
    }

    QPointer<PDFAppHomeBasePage> guard(this);
    showDatabaseError(context->getWorkspaces().markOpened(id));
    if (!guard)
    {
        return;
    }

    // The documents open in their own tabs at their pages
    const std::vector<PDFAppOpenRequest> requests = PDFAppDocumentActions::getRequests(workspace->documents);
    if (!requests.empty())
    {
        PDFAppDocumentActions::openRequests(this, requests);
    }
}

void PDFAppHomeBasePage::restoreLastSession()
{
    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase())
    {
        return;
    }

    const std::optional<PDFWorkspace> session = context->getWorkspaces().getLastSession();
    if (!session)
    {
        scheduleRefresh();
        return;
    }

    // The session is restored once (PDFWorkspaceRepository::deleteLastSession), the
    // documents at their pages
    const std::vector<PDFAppOpenRequest> requests = PDFAppDocumentActions::getRequests(session->documents);
    QPointer<PDFAppHomeBasePage> guard(this);
    showDatabaseError(context->getWorkspaces().deleteLastSession());
    if (guard && !requests.empty())
    {
        PDFAppDocumentActions::openRequests(this, requests);
    }
}

void PDFAppHomeBasePage::setFilePinned(const QString& path, bool isPinned)
{
    PDFAppContext* context = getContext();
    if (context && context->getDatabase())
    {
        showDatabaseError(context->getRecent().setPinned(path, isPinned));
    }
}

void PDFAppHomeBasePage::setFolderPinned(const QString& path, bool isPinned)
{
    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase() || path.isEmpty())
    {
        return;
    }

    PDFRecentRepository recent = context->getRecent();
    if (isPinned)
    {
        bool isDuplicate = false;
        const PDFDatabaseError error = recent.addPinnedLocation(path, QString(), nullptr, &isDuplicate);
        if (!isDuplicate)
        {
            showDatabaseError(error);
        }
        return;
    }

    for (const PDFPinnedLocation& location : recent.getPinnedLocations())
    {
        if (PDFRepositoryHelper::isSamePath(location.path, path))
        {
            QPointer<PDFAppHomeBasePage> guard(this);
            showDatabaseError(recent.removePinnedLocation(location.id));
            if (!guard)
            {
                return;
            }
        }
    }
}

void PDFAppHomeBasePage::addPinnedLocation()
{
    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase() || m_isBrowsing)
    {
        return;
    }

    QPointer<PDFAppHomeBasePage> guard(this);
    m_isBrowsing = true;
    const QString folder = PDFAppFileDialogs::getExistingDirectory(window(), QString());
    if (!guard)
    {
        return;
    }
    m_isBrowsing = false;

    context = getContext();
    if (folder.isEmpty() || !context || !context->getDatabase())
    {
        return;
    }

    bool isDuplicate = false;
    const PDFDatabaseError error = context->getRecent().addPinnedLocation(folder, QString(), nullptr, &isDuplicate);
    if (isDuplicate)
    {
        PDFMessageBox* box = new PDFMessageBox(PDFThemeIcons::Badge::Information, tr("The folder is already pinned"),
                                               tr("“%1” is already one of the pinned locations.").arg(PDFAppHomeFormat::toDisplayPath(folder)), QDialogButtonBox::Ok, window());
        box->setObjectName(QStringLiteral("duplicateLocationBox"));
        box->setAttribute(Qt::WA_DeleteOnClose);
        PDFAppMainWindow::prepareDialog(box, this);
        m_errorBox = box;
        box->open();
        return;
    }
    showDatabaseError(error);
}

void PDFAppHomeBasePage::showSaveWorkspaceDialog()
{
    const std::vector<PDFAppOpenDocument> documents = PDFAppDocumentActions::getOpenDocuments(this);
    showDialog(new PDFAppSaveWorkspaceDialog(getContext(), documents, window()));
}

void PDFAppHomeBasePage::showRenameWorkspaceDialog(qint64 id)
{
    PDFAppContext* context = getContext();
    const std::optional<PDFWorkspace> workspace = context && context->getDatabase() ? context->getWorkspaces().getWorkspace(id) : std::nullopt;
    if (!workspace || workspace->isLastSession)
    {
        scheduleRefresh();
        return;
    }
    showDialog(new PDFAppRenameWorkspaceDialog(context, id, workspace->name, window()));
}

void PDFAppHomeBasePage::showDeleteWorkspaceDialog(qint64 id)
{
    PDFAppContext* context = getContext();
    const std::optional<PDFWorkspace> workspace = context && context->getDatabase() ? context->getWorkspaces().getWorkspace(id) : std::nullopt;
    if (!workspace || workspace->isLastSession)
    {
        scheduleRefresh();
        return;
    }
    showDialog(new PDFAppDeleteWorkspaceDialog(workspace->name, workspace->documentCount, id, window()));
}

void PDFAppHomeBasePage::showRenameLocationDialog(qint64 id)
{
    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase())
    {
        return;
    }

    for (const PDFPinnedLocation& location : context->getRecent().getPinnedLocations())
    {
        if (location.id == id)
        {
            showDialog(new PDFAppRenameLocationDialog(context, id, location.name, window()));
            return;
        }
    }
    scheduleRefresh();
}

void PDFAppHomeBasePage::showClearHistoryDialog()
{
    showDialog(new PDFAppClearHistoryDialog(getContext(), window()));
}

void PDFAppHomeBasePage::showDialog(QDialog* dialog)
{
    if (!dialog)
    {
        return;
    }

    if (QDialog* previous = m_dialog.data())
    {
        previous->disconnect(this);
        previous->hide();
        previous->deleteLater();
    }

    m_dialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, &PDFAppHomeBasePage::onDialogFinished);
    dialog->open();
}

void PDFAppHomeBasePage::onDialogFinished(int result)
{
    // The dialog does not identify itself in the signal
    QDialog* dialog = qobject_cast<QDialog*>(sender());
    PDFAppContext* context = getContext();
    if (!dialog || result != QDialog::Accepted || !context || !context->getDatabase())
    {
        return;
    }

    if (PDFAppDeleteWorkspaceDialog* deleteDialog = qobject_cast<PDFAppDeleteWorkspaceDialog*>(dialog))
    {
        showDatabaseError(context->getWorkspaces().deleteWorkspace(deleteDialog->getWorkspaceId()));
    }
    else if (PDFAppClearHistoryDialog* clearDialog = qobject_cast<PDFAppClearHistoryDialog*>(dialog))
    {
        showDatabaseError(context->getRecent().clearHistory(clearDialog->isPinnedIncluded()));
    }
}

void PDFAppHomeBasePage::showDatabaseError(const PDFDatabaseError& error)
{
    if (!error.isError())
    {
        return;
    }

    qWarning("A change of the history was not saved: %s", qPrintable(error.toString()));
    if (QDialog* previous = m_errorBox.data())
    {
        previous->hide();
        previous->deleteLater();
    }

    const QString text = error.isBusy() ? tr("Another PDF4QT window is saving its settings. Try it again in a moment.") : error.getMessage();
    PDFMessageBox* box = new PDFMessageBox(PDFThemeIcons::Badge::Warning, tr("The change was not saved"), text, QDialogButtonBox::Ok, window());
    box->setObjectName(QStringLiteral("databaseErrorBox"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    PDFAppMainWindow::prepareDialog(box, this);
    m_errorBox = box;
    box->open();
}

void PDFAppHomeBasePage::refreshNow()
{
    if (m_refreshTimer)
    {
        m_refreshTimer->stop();
    }
    ++m_refreshCount;

    // The mounts may change between two refreshes (a share mounted meanwhile)
    m_networkPaths = std::make_unique<PDFAppNetworkPaths>();

    // A refresh may rebuild the rows: the focused item gets the focus back
    const FocusState focus = captureFocus();
    refresh();

    // The rows kept by the refresh (the same data) get the texts of the current minute
    const qint64 now = getNow();
    updateTimeTexts(now);
    if (m_minuteSubscription)
    {
        m_minuteSubscription->setUpdated(now);
    }
    restoreFocus(focus);
}

bool PDFAppHomeBasePage::isNetworkPath(const QString& path) const
{
    if (!m_networkPaths)
    {
        return PDFAppNetworkPaths::isNetworkPathByName(path);
    }
    return m_networkPaths->isNetworkPath(path);
}

PDFAppHomeBasePage::FocusState PDFAppHomeBasePage::captureFocus() const
{
    FocusState state;
    QWidget* focused = QApplication::focusWidget();
    if (!focused || !isAncestorOf(focused))
    {
        return state;
    }

    // The item is the nearest ancestor with the path or the id (a row, a card)
    for (QWidget* widget = focused; widget && widget != this; widget = widget->parentWidget())
    {
        for (const char* property : { PATH_PROPERTY, ID_PROPERTY })
        {
            const QVariant value = widget->property(property);
            if (value.isValid() && !value.toString().isEmpty())
            {
                state.item = widget;
                state.container = widget->parentWidget();
                state.focused = focused;
                state.property = property;
                state.value = value;
                state.itemName = widget->objectName();
                if (focused != widget)
                {
                    state.focusedName = focused->objectName();
                    state.focusedClass = focused->metaObject()->className();
                }
                return state;
            }
        }
    }
    return state;
}

void PDFAppHomeBasePage::restoreFocus(const FocusState& state)
{
    // Nothing was focused, or the focused item was kept by the refresh
    if (!state.property || (state.item && state.focused && state.item->isVisibleTo(this) && state.focused->isVisibleTo(this)))
    {
        return;
    }

    // The new widget of the same item: in the same container first (a pinned file
    // is also in the list of the recent files), then anywhere on the page. A new
    // row is not shown yet (its layout shows it from the event loop), it must not
    // be hidden explicitly (the old rows are) and its container must be visible.
    QWidget* target = nullptr;
    const QList<QWidget*> widgets = findChildren<QWidget*>();
    for (int pass = 0; pass < 2 && !target; ++pass)
    {
        for (QWidget* widget : widgets)
        {
            if (widget == state.item || widget->objectName() != state.itemName || widget->property(state.property) != state.value ||
                PDFAppHomeBasePageHelper::isHiddenExplicitly(widget) || !widget->parentWidget() || !widget->parentWidget()->isVisibleTo(this))
            {
                continue;
            }
            if (pass == 0 && widget->parentWidget() != state.container.data())
            {
                continue;
            }
            target = widget;
            break;
        }
    }

    if (!target)
    {
        return;
    }

    // The new row is shown now (its layout would show it later), so it takes the focus
    if (target->isHidden())
    {
        target->show();
    }

    // The nested widget (the star of a row) of the same name and class
    if (!state.focusedClass.isEmpty())
    {
        for (QWidget* child : target->findChildren<QWidget*>())
        {
            if (child->objectName() == state.focusedName && child->metaObject()->className() == state.focusedClass &&
                child->isVisibleTo(this) && child->focusPolicy() != Qt::NoFocus)
            {
                target = child;
                break;
            }
        }
    }

    if (target->focusPolicy() != Qt::NoFocus && target->isEnabled())
    {
        target->setFocus(state.reason);
    }
}

bool PDFAppHomeBasePage::isRefreshPending() const
{
    return m_refreshTimer && m_refreshTimer->isActive();
}

QString PDFAppHomeBasePage::getBrowseShortcutText() const
{
    // The effective shortcut of the command Open… (the shortcut of the user, when
    // there is one), as the window binds it
    const PDFAppContext* context = getContext();
    const PDFAppCommandRegistry* registry = context ? context->getCommandRegistry() : nullptr;
    if (!registry)
    {
        return QString();
    }

    PDFAppCommandRegistry::UserShortcuts userShortcuts;
    if (context->getDatabase())
    {
        userShortcuts = context->getShortcuts().getShortcuts();
    }
    return registry->getShortcut(PDFAppCommands::open(), userShortcuts).toString(QKeySequence::NativeText);
}

bool PDFAppHomeBasePage::isTopicRelevant(const QString& topic) const
{
    return topic == PDFRecentRepository::getFileTopic() || topic == PDFRecentRepository::getFolderTopic() || topic == PDFRecentRepository::getPinnedLocationTopic() ||
           topic == PDFWorkspaceRepository::getWorkspaceTopic() || topic == PDFWorkspaceRepository::getDocumentTopic() || topic == PDFShortcutRepository::getTopic();
}

void PDFAppHomeBasePage::onPathInfoChanged(const QString& path)
{
    Q_UNUSED(path);
}

void PDFAppHomeBasePage::scheduleRefresh()
{
    // A hidden page reads the database, when it is shown
    if (m_refreshTimer && isVisible())
    {
        m_refreshTimer->start();
    }
}

std::optional<PDFAppFolderInfo> PDFAppHomeBasePage::getPathInfo(const QString& path) const
{
    auto it = m_pathInfos.constFind(path);
    if (it == m_pathInfos.cend())
    {
        return std::nullopt;
    }
    return *it;
}

void PDFAppHomeBasePage::requestPathInfo(const QString& path)
{
    requestInfo(path, false);
}

void PDFAppHomeBasePage::requestFolderInfo(const QString& path)
{
    requestInfo(path, true);
}

void PDFAppHomeBasePage::requestInfo(const QString& path, bool countPdfFiles)
{
    // A network path is never contacted by the page (an explicit opening checks it)
    PDFAppFolderInfoService* service = getContext() ? getContext()->getFolderInfo() : nullptr;
    if (!service || path.isEmpty() || m_pathInfos.contains(path) || m_pathInfos.size() >= PDFAppHomeBasePageHelper::MAXIMUM_PATH_INFOS || isNetworkPath(path))
    {
        return;
    }

    const bool isAccepted = countPdfFiles ? service->requestFolderInfo(path) : service->requestPathInfo(path);
    if (!isAccepted)
    {
        // The service is busy: the path is asked again later (it stays unknown
        // meanwhile, not pending forever); an invalid path is never known
        if (service->isFull() && m_refusedRequests.size() < PDFAppHomeBasePageHelper::MAXIMUM_PATH_INFOS)
        {
            m_refusedRequests.insert(path, countPdfFiles);
            if (m_retryTimer && !m_retryTimer->isActive())
            {
                m_retryTimer->start();
            }
        }
        return;
    }

    // An empty record marks the request (the answer replaces it)
    m_refusedRequests.remove(path);
    PDFAppFolderInfo pending;
    pending.path = path;
    pending.exists = true;
    pending.pdfCount = -2;
    m_pathInfos.insert(path, pending);
}

void PDFAppHomeBasePage::onRetryTimer()
{
    const QHash<QString, bool> refused = std::exchange(m_refusedRequests, QHash<QString, bool>());
    for (auto it = refused.cbegin(); it != refused.cend(); ++it)
    {
        requestInfo(it.key(), it.value());
    }
}

void PDFAppHomeBasePage::onInfoReady(const PDFAppFolderInfo& info)
{
    // Only the paths, which this page has asked for (other pages ask the same service)
    auto it = m_pathInfos.find(info.path);
    if (it == m_pathInfos.end() || it->pdfCount != -2)
    {
        return;
    }

    *it = info;
    onPathInfoChanged(info.path);
}

void PDFAppHomeBasePage::requestThumbnail(const PDFRecentFile& file)
{
    // A network file gets its thumbnail, when it is opened (PDFAppMainWindow::openDocuments)
    PDFAppThumbnailer* thumbnailer = getContext() ? getContext()->getThumbnailer() : nullptr;
    if (thumbnailer && !file.path.isEmpty() && !isNetworkPath(file.path))
    {
        thumbnailer->requestThumbnail(file.path);
    }
}

QPixmap PDFAppHomeBasePage::getThumbnail(const PDFRecentFile& file)
{
    PDFAppContext* context = getContext();
    if (!file.hasThumbnail || file.thumbnailState != PDFRecentThumbnailState::Ready || !context || !context->getDatabase())
    {
        m_thumbnails.remove(file.path);
        return QPixmap();
    }

    // The picture is decoded once per version of the thumbnail (thumbnail_mtime)
    auto it = m_thumbnails.find(file.path);
    if (it != m_thumbnails.end() && it->version == file.thumbnailModified)
    {
        it->lastUse = ++m_thumbnailUse;
        return it->pixmap;
    }

    // The least recently used picture goes, when the limit is reached (the shown
    // rows keep theirs - a refresh uses all of them)
    if (it == m_thumbnails.end() && m_thumbnails.size() >= MAXIMUM_THUMBNAILS)
    {
        auto oldest = m_thumbnails.begin();
        for (auto candidate = m_thumbnails.begin(); candidate != m_thumbnails.end(); ++candidate)
        {
            if (candidate->lastUse < oldest->lastUse)
            {
                oldest = candidate;
            }
        }
        m_thumbnails.erase(oldest);
    }

    const QImage image = context->getRecent().getThumbnail(file.path).toImage();
    ThumbnailEntry entry;
    entry.pixmap = image.isNull() ? QPixmap() : QPixmap::fromImage(image);
    entry.version = file.thumbnailModified;
    entry.lastUse = ++m_thumbnailUse;
    m_thumbnails.insert(file.path, entry);
    return entry.pixmap;
}

qint64 PDFAppHomeBasePage::getNow() const
{
    return PDFAppHomeFormat::getCurrentTime();
}

void PDFAppHomeBasePage::updateTimeTexts(qint64 now)
{
    PDFAppHomeFormat::updateTimeTexts(this, now);
}

void PDFAppHomeBasePage::onMinuteChanged(qint64 now)
{
    // Only the texts: no read of the database, no rebuild of the rows
    updateTimeTexts(now);
}

PDFTextLink* PDFAppHomeBasePage::createLink(const QString& text, const PDFAppRoute& route, QWidget* parent)
{
    PDFTextLink* link = new PDFTextLink(text, parent);
    withLink(link, route.toString());
    return link;
}

void PDFAppHomeBasePage::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);

    // The data are read, when the page is shown; the existence of the files is asked again
    if (!event->spontaneous())
    {
        m_pathInfos.clear();
        m_refusedRequests.clear();
        refreshNow();
    }
}

void PDFAppHomeBasePage::onDatabaseChanged(const QString& topic, const QString& key)
{
    Q_UNUSED(key);
    if (isTopicRelevant(topic))
    {
        scheduleRefresh();
    }
}

void PDFAppHomeBasePage::onEverythingChanged()
{
    scheduleRefresh();
}

void PDFAppHomeBasePage::onRefreshTimer()
{
    refreshNow();
}

}   // namespace pdf
