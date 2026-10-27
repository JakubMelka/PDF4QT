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

#include "pdfappmcpconfirmationui.h"
#include "pdfappmcpclientnames.h"
#include "pdfappmcpconfirmdialog.h"
#include "pdfappmcphost.h"
#include "pdfappmcppreview.h"
#include "pdfappannotationcontroller.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanel.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfapptab.h"

#include "pdfcomponentassistants.h"
#include "pdfcomponentbadges.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdffluentwidgets.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QPushButton>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the user interface of the confirmations
class PDFAppMcpConfirmationUiHelper
{
public:
    PDFAppMcpConfirmationUiHelper() = delete;

    static qint64 getSystemTime()
    {
        return QDateTime::currentMSecsSinceEpoch();
    }

    static QString getDocumentTitle(const PDFAppMcpConfirmation& confirmation)
    {
        return confirmation.document ? PDFAppMcpConfirmDialog::sanitize(confirmation.document->getTitle()) : QString();
    }

    static QString getClientName(const PDFAppMcpConfirmation& confirmation)
    {
        return PDFAppMcpConfirmDialog::sanitize(confirmation.clientName.isEmpty() ? PDFAppMcpConfirmationUi::tr("The assistant") : confirmation.clientName, 64);
    }

    /// Removes the pairs of a deleted owner or a deleted widget
    template<typename Owner, typename Widget>
    static void removeDeleted(std::vector<std::pair<QPointer<Owner>, QPointer<Widget>>>& items)
    {
        std::vector<std::pair<QPointer<Owner>, QPointer<Widget>>> kept;
        for (const auto& item : items)
        {
            if (item.first && item.second)
            {
                kept.push_back(item);
            }
        }
        items = std::move(kept);
    }
};

PDFAppMcpWaitingBanner::PDFAppMcpWaitingBanner(QWidget* parent) :
    BaseClass(PDFAccentHue::Blue, parent)
{
    setObjectName(QStringLiteral("mcpWaitingBanner"));

    // 967:12873: padding 14 / 16 / 12 inside of the border 1 (the card 62 high),
    // gap 12, items centered
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(15, 13, 17, 13);
    layout->setSpacing(12);

    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    m_titleLabel = new PDFLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, this);
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->setObjectName(QStringLiteral("title"));
    m_bodyLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this);
    m_bodyLabel->setTextFormat(Qt::PlainText);
    m_bodyLabel->setObjectName(QStringLiteral("body"));
    m_bodyLabel->setWordWrap(true);
    m_bodyLabel->setLineHeight(16);
    textLayout->addWidget(m_titleLabel);
    textLayout->addWidget(m_bodyLabel);
    layout->addLayout(textLayout, 1);

    m_showButton = new QPushButton(this);
    m_showButton->setObjectName(QStringLiteral("showButton"));
    m_showButton->setAutoDefault(false);
    connect(m_showButton, &QPushButton::clicked, this, &PDFAppMcpWaitingBanner::onShowClicked);
    layout->addWidget(m_showButton, 0, Qt::AlignVCenter);
    hide();
}

PDFAppMcpWaitingBanner::~PDFAppMcpWaitingBanner() = default;

void PDFAppMcpWaitingBanner::setConfirmation(const std::shared_ptr<PDFAppMcpConfirmation>& confirmation)
{
    if (!confirmation)
    {
        return;
    }

    const bool isNewClient = m_clientNameRaw != confirmation->clientNameRaw || !m_monogram;
    m_confirmationId = confirmation->id;
    m_kind = confirmation->kind;
    m_clientName = PDFAppMcpConfirmationUiHelper::getClientName(*confirmation);
    m_clientNameRaw = confirmation->clientNameRaw;
    m_documentTitle = PDFAppMcpConfirmationUiHelper::getDocumentTitle(*confirmation);

    if (isNewClient)
    {
        // The monogram 36 of the assistant (no CLI badge, 967:12874)
        delete m_monogram.data();
        const PDFAppMcpClientInfo client = PDFAppMcpClientNames::describe(m_clientNameRaw);
        // (accent/<hue>/bg without a border; the mcp icon in teal of an unknown client)
        PDFComponentPainter::AssistantAvatar avatarData;
        avatarData.monogram = client.monogram;
        avatarData.hue = client.monogram.isEmpty() ? PDFAccentHue::Teal : client.hue;
        avatarData.extent = 36;
        avatarData.fontPixelSize = 13;
        QWidget* monogram = new PDFAssistantAvatar(avatarData, this);
        monogram->setObjectName(QStringLiteral("monogram"));
        if (QHBoxLayout* layout = qobject_cast<QHBoxLayout*>(this->layout()))
        {
            layout->insertWidget(0, monogram, 0, Qt::AlignVCenter);
        }
        m_monogram = monogram;
    }
    updateTexts();
}

void PDFAppMcpWaitingBanner::setRemainingTime(qint64 milliseconds)
{
    m_remainingTime = qMax<qint64>(0, milliseconds);
    updateTexts();
}

QPushButton* PDFAppMcpWaitingBanner::getShowButton() const
{
    return m_showButton.data();
}

QString PDFAppMcpWaitingBanner::getTitleText() const
{
    return m_titleLabel ? m_titleLabel->text() : QString();
}

QString PDFAppMcpWaitingBanner::getBodyText() const
{
    return m_bodyLabel ? m_bodyLabel->text() : QString();
}

void PDFAppMcpWaitingBanner::updateTexts()
{
    const QString time = PDFAppMcpConfirmDialog::formatRemainingTime(m_remainingTime);
    QString request;
    switch (m_kind)
    {
        case PDFAppMcpConfirmationKind::ReadDocuments:
            request = tr("%1 asks to read your open documents.").arg(m_clientName);
            break;
        case PDFAppMcpConfirmationKind::Change:
            request = tr("%1 asks to change %2.").arg(m_clientName, m_documentTitle);
            break;
        case PDFAppMcpConfirmationKind::Save:
            request = tr("%1 asks to save %2.").arg(m_clientName, m_documentTitle);
            break;
        case PDFAppMcpConfirmationKind::SaveCopy:
            request = tr("%1 asks to save a copy of %2.").arg(m_clientName, m_documentTitle);
            break;
        case PDFAppMcpConfirmationKind::Open:
            request = tr("%1 asks to open a document.").arg(m_clientName);
            break;
        case PDFAppMcpConfirmationKind::Close:
            request = tr("%1 asks to close %2.").arg(m_clientName, m_documentTitle);
            break;
    }

    if (m_titleLabel)
    {
        m_titleLabel->setText(m_documentTitle.isEmpty() ? tr("%1 is waiting for your confirmation").arg(m_clientName)
                                                        : tr("%1 is waiting for your confirmation · %2").arg(m_clientName, m_documentTitle));
    }
    if (m_bodyLabel)
    {
        m_bodyLabel->setText(tr("%1 The request waits in its tab and is denied automatically in %2 if you do not answer.").arg(request, time));
    }
    if (m_showButton)
    {
        m_showButton->setText(tr("Show"));
    }
    setAccessibleName(getTitleText());
}

void PDFAppMcpWaitingBanner::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

void PDFAppMcpWaitingBanner::onShowClicked()
{
    Q_EMIT showRequested(m_confirmationId);
}

PDFAppMcpConfirmationUi::PDFAppMcpConfirmationUi(PDFAppMcpHost* host) :
    BaseClass(host),
    m_host(host),
    m_clock(&PDFAppMcpConfirmationUiHelper::getSystemTime),
    m_ghostOverlay(std::make_unique<PDFAppMcpGhostOverlay>())
{
    setObjectName(QStringLiteral("mcpConfirmationUi"));

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(TICK_INTERVAL);
    connect(m_tickTimer, &QTimer::timeout, this, &PDFAppMcpConfirmationUi::tick);

    // A modal dialog of the application closed: a waiting request shows its
    // dialog at once (the tick checks it too, plan 2.5)
    if (QGuiApplication* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
    {
        connect(application, &QGuiApplication::focusWindowChanged, this, &PDFAppMcpConfirmationUi::onFocusWindowChanged);
    }

    if (m_host)
    {
        m_host->setConfirmationProvider(this);
        if (PDFAppContext* context = m_host->getContext())
        {
            if (PDFAppDocumentRegistry* registry = context->getDocumentRegistry())
            {
                connect(registry, &PDFAppDocumentRegistry::documentRemoved, this, &PDFAppMcpConfirmationUi::scheduleRefresh);
            }
        }
    }
}

PDFAppMcpConfirmationUi::~PDFAppMcpConfirmationUi()
{
    // No signal of the application reaches the object being destroyed
    if (QCoreApplication* application = QCoreApplication::instance())
    {
        disconnect(application, nullptr, this, nullptr);
    }

    if (m_host && m_host->getConfirmationProvider() == this)
    {
        m_host->setConfirmationProvider(nullptr);
    }

    closePreview();
    closeDialog();
    for (const auto& item : m_statusItems)
    {
        delete item.second.data();
    }
    for (const auto& item : m_banners)
    {
        delete item.second.data();
    }
    for (const QPointer<PDFAppMcpNotificationBar>& bar : m_notificationBars)
    {
        delete bar.data();
    }
}

PDFAppMcpConfirmationUi* PDFAppMcpConfirmationUi::find(const PDFAppMcpHost* host)
{
    return host ? host->findChild<PDFAppMcpConfirmationUi*>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

void PDFAppMcpConfirmationUi::notifyCommitted(PDFAppMcpHost* host, const PDFAppMcpCommitInfo& info)
{
    if (PDFAppMcpConfirmationUi* ui = find(host))
    {
        ui->showNotification(info);
    }
}

void PDFAppMcpConfirmationUi::setClock(std::function<qint64()> clock)
{
    m_clock = clock ? std::move(clock) : std::function<qint64()>(&PDFAppMcpConfirmationUiHelper::getSystemTime);
}

qint64 PDFAppMcpConfirmationUi::getTime() const
{
    return m_clock ? m_clock() : PDFAppMcpConfirmationUiHelper::getSystemTime();
}

void PDFAppMcpConfirmationUi::showConfirmation(std::shared_ptr<PDFAppMcpConfirmation> confirmation)
{
    if (!confirmation)
    {
        return;
    }

    Entry entry;
    entry.confirmation = std::move(confirmation);
    entry.queuedAt = getTime();
    entry.hasDocument = entry.confirmation->document != nullptr;
    m_entries.push_back(std::move(entry));

    if (m_tickTimer && !m_tickTimer->isActive())
    {
        m_tickTimer->start();
    }

    // A minimized window asks for the attention of the user
    PDFAppMainWindow* window = getWindow();
    if (window && window->isMinimized())
    {
        QApplication::alert(window);
    }
    scheduleRefresh();
}

void PDFAppMcpConfirmationUi::withdrawConfirmation(quint64 confirmationId, PDFAppMcpDecision decision)
{
    Q_UNUSED(decision);

    // The host resolves it itself (canceled, disconnected, stopped)
    if (removeEntry(confirmationId))
    {
        scheduleRefresh();
    }
}

PDFAppMcpConfirmationUi::Entry* PDFAppMcpConfirmationUi::findEntry(quint64 confirmationId)
{
    for (Entry& entry : m_entries)
    {
        if (entry.confirmation && entry.confirmation->id == confirmationId)
        {
            return &entry;
        }
    }
    return nullptr;
}

bool PDFAppMcpConfirmationUi::removeEntry(quint64 confirmationId)
{
    auto it = m_entries.begin();
    while (it != m_entries.end() && !(it->confirmation && it->confirmation->id == confirmationId))
    {
        ++it;
    }
    if (it == m_entries.end())
    {
        return false;
    }

    m_entries.erase(it);
    if (m_dialog && m_dialog->getConfirmationId() == confirmationId)
    {
        closeDialog();
    }
    if (m_previewId == confirmationId)
    {
        closePreview();
    }
    if (m_entries.empty() && m_tickTimer)
    {
        m_tickTimer->stop();
    }
    return true;
}

qint64 PDFAppMcpConfirmationUi::getRemainingTime(const Entry& entry) const
{
    // The 2 minutes of a request count from its dialog (a request queued behind
    // others gets its whole time - item 38); before, the bound of the queue
    if (entry.isShown)
    {
        return qMax<qint64>(0, qint64(m_timeout) - (getTime() - entry.shownAt));
    }
    return qMax<qint64>(0, qint64(m_queueTimeout) - (getTime() - entry.queuedAt));
}

qint64 PDFAppMcpConfirmationUi::getRemainingTime(quint64 confirmationId) const
{
    for (const Entry& entry : m_entries)
    {
        if (entry.confirmation && entry.confirmation->id == confirmationId)
        {
            return getRemainingTime(entry);
        }
    }
    return -1;
}

bool PDFAppMcpConfirmationUi::isShown(quint64 confirmationId) const
{
    for (const Entry& entry : m_entries)
    {
        if (entry.confirmation && entry.confirmation->id == confirmationId)
        {
            return entry.isShown;
        }
    }
    return false;
}

std::vector<quint64> PDFAppMcpConfirmationUi::getWaitingIds() const
{
    std::vector<quint64> ids;
    for (const Entry& entry : m_entries)
    {
        ids.push_back(entry.confirmation->id);
    }
    return ids;
}

void PDFAppMcpConfirmationUi::answer(quint64 confirmationId, PDFAppMcpDecision decision)
{
    if (!removeEntry(confirmationId))
    {
        return;
    }

    QPointer<PDFAppMcpConfirmationUi> guard(this);
    if (m_host)
    {
        m_host->resolveConfirmation(confirmationId, decision);
    }
    if (guard)
    {
        scheduleRefresh();
    }
}

void PDFAppMcpConfirmationUi::tick()
{
    std::vector<quint64> expired;
    for (const Entry& entry : m_entries)
    {
        if (getRemainingTime(entry) <= 0)
        {
            expired.push_back(entry.confirmation->id);
        }
    }

    QPointer<PDFAppMcpConfirmationUi> guard(this);
    for (quint64 id : expired)
    {
        // No answer in 2 min = denied (activity "Denied automatically · no answer in 2 min")
        answer(id, PDFAppMcpDecision::DeniedTimeout);
        if (!guard)
        {
            return;
        }
    }

    // The modal dialog, for which a request waits, has closed (a window without
    // the focus does not change the focus window)
    if (m_isWaitingForModal && !QApplication::activeModalWidget())
    {
        refresh();
        if (!guard)
        {
            return;
        }
    }

    if (m_dialog)
    {
        if (const Entry* entry = findEntry(m_dialog->getConfirmationId()))
        {
            m_dialog->setRemainingTime(getRemainingTime(*entry));
        }
    }
    for (const auto& item : m_banners)
    {
        if (PDFAppMcpWaitingBanner* banner = item.second.data())
        {
            if (const Entry* entry = findEntry(banner->getConfirmationId()))
            {
                banner->setRemainingTime(getRemainingTime(*entry));
            }
        }
    }
}

void PDFAppMcpConfirmationUi::scheduleRefresh()
{
    if (!m_isRefreshQueued)
    {
        m_isRefreshQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppMcpConfirmationUi::onRefreshQueued, Qt::QueuedConnection);
    }
}

void PDFAppMcpConfirmationUi::onRefreshQueued()
{
    if (m_isRefreshQueued)
    {
        refresh();
    }
}

void PDFAppMcpConfirmationUi::onFocusWindowChanged()
{
    if (m_isWaitingForModal)
    {
        scheduleRefresh();
    }
}

PDFAppMainWindow* PDFAppMcpConfirmationUi::getWindow() const
{
    return m_host ? m_host->getResolver().getWindow() : nullptr;
}

PDFAppDocumentTab* PDFAppMcpConfirmationUi::findTab(const Entry& entry) const
{
    if (!m_host || !entry.confirmation || !entry.confirmation->document)
    {
        return nullptr;
    }
    return m_host->getResolver().findTab(entry.confirmation->document);
}

void PDFAppMcpConfirmationUi::removeClosedEntries()
{
    // A request of a document, whose tab was closed, is denied (denied_closed)
    // (a tab closed by the window leaves it at once, the document goes later)
    std::vector<quint64> closed;
    std::vector<quint64> unavailable;
    PDFAppDocumentRegistry* registry = m_host && m_host->getContext() ? m_host->getContext()->getDocumentRegistry() : nullptr;
    const bool hasWindow = getWindow() != nullptr;
    m_hasSeenWindow = m_hasSeenWindow || hasWindow;
    for (const Entry& entry : m_entries)
    {
        const PDFAppDocument* document = entry.confirmation->document;
        if (entry.hasDocument && (!document || (registry && !registry->findDocument(document->getId())) || (hasWindow && !findTab(entry))))
        {
            closed.push_back(entry.confirmation->id);
        }
        else if (!entry.hasDocument && !hasWindow && m_hasSeenWindow)
        {
            // The window is gone (closed, quitting): nobody can answer a request
            // without a document - it would wait for its timeout (UnitTestsAppMcpLifetime)
            unavailable.push_back(entry.confirmation->id);
        }
    }

    QPointer<PDFAppMcpConfirmationUi> guard(this);
    for (quint64 id : closed)
    {
        answer(id, PDFAppMcpDecision::DeniedClosed);
        if (!guard)
        {
            return;
        }
    }
    for (quint64 id : unavailable)
    {
        answer(id, PDFAppMcpDecision::Unavailable);
        if (!guard)
        {
            return;
        }
    }
}

void PDFAppMcpConfirmationUi::watchWindow(PDFAppMainWindow* window)
{
    QTabBar* tabBar = window ? window->getTabBar() : nullptr;
    if (m_watchedTabBar == tabBar)
    {
        return;
    }

    if (m_watchedTabBar)
    {
        disconnect(m_watchedTabBar, nullptr, this, nullptr);
    }
    m_watchedTabBar = tabBar;
    if (tabBar)
    {
        connect(tabBar, &QTabBar::currentChanged, this, &PDFAppMcpConfirmationUi::scheduleRefresh);
    }

    // The keys of the commands of the documents (window shortcuts dispatched to
    // the current tab - Ctrl+Z, Ctrl+P, ...) are watched: the dialog is modal for
    // its tab, the commands of the window (tabs, search) stay live
    for (const QPointer<QAction>& action : m_watchedActions)
    {
        if (action)
        {
            action->removeEventFilter(this);
        }
    }
    m_watchedActions.clear();

    const PDFAppCommandRegistry* registry = m_host && m_host->getContext() ? m_host->getContext()->getCommandRegistry() : nullptr;
    if (window && registry)
    {
        for (const PDFAppCommand* command : registry->getCommands())
        {
            QAction* action = command && command->scope == PDFAppCommandScope::Document ? window->getCommandAction(command->id) : nullptr;
            if (action)
            {
                action->installEventFilter(this);
                m_watchedActions.emplace_back(action);
            }
        }
    }
}

bool PDFAppMcpConfirmationUi::isDocumentShortcutBlocked() const
{
    // The scrim is a child of the tab: visible only while its tab is the current one
    return m_dialog && m_scrim && m_scrim->isVisible();
}

void PDFAppMcpConfirmationUi::refresh()
{
    m_isRefreshQueued = false;

    QPointer<PDFAppMcpConfirmationUi> guard(this);
    removeClosedEntries();
    if (!guard)
    {
        return;
    }

    PDFAppMainWindow* window = getWindow();
    watchWindow(window);
    PDFAppTab* currentTab = window ? window->getCurrentTab() : nullptr;

    // The requests of the current tab: of its document, and the requests without
    // a document (open, a read approval from the Home tab) - the oldest first
    std::vector<Entry*> targets;
    for (Entry& entry : m_entries)
    {
        if (!currentTab)
        {
            break;
        }
        PDFAppDocumentTab* tab = findTab(entry);
        if ((!entry.hasDocument) || (tab && tab == currentTab))
        {
            targets.push_back(&entry);
        }
    }

    Entry* dialogEntry = targets.empty() ? nullptr : targets.front();
    PDFAppDocumentTab* documentTab = qobject_cast<PDFAppDocumentTab*>(currentTab);

    // While a modal dialog of the application is open, no new dialog appears:
    // the user could not answer it, its time would run out behind the modal
    // dialog (plan 2.5 - a first read with Ask and files.open wait for it)
    const bool isDialogShownFor = dialogEntry && m_dialog && m_scrim && m_dialog->getConfirmationId() == dialogEntry->confirmation->id &&
                                  m_scrim->parentWidget() == currentTab;
    m_isWaitingForModal = dialogEntry && !dialogEntry->isShown && !isDialogShownFor && QApplication::activeModalWidget() != nullptr;
    if (m_isWaitingForModal)
    {
        closeDialog();
        closePreview();
    }
    else if (dialogEntry && dialogEntry->isPreview && documentTab && dialogEntry->hasDocument)
    {
        closeDialog();
        showPreview(*dialogEntry, documentTab);
    }
    else if (dialogEntry)
    {
        dialogEntry->isPreview = false;
        closePreview();
        showDialogEntry(*dialogEntry, currentTab, 0, int(targets.size()));
    }
    else
    {
        closeDialog();
        closePreview();
    }

    updateStatusItems(window, currentTab);
    updateBanners(window, currentTab);
}

void PDFAppMcpConfirmationUi::showDialogEntry(Entry& entry, PDFAppTab* tab, int index, int count)
{
    if (!tab)
    {
        closeDialog();
        return;
    }

    if (m_dialog && m_scrim && m_dialog->getConfirmationId() == entry.confirmation->id && m_scrim->parentWidget() == tab)
    {
        m_dialog->setQueuePosition(index, count);
        m_dialog->setRemainingTime(getRemainingTime(entry));
        return;
    }

    // The widget of the tab, which has the focus, gets it back when the dialog
    // closes (item 39; a dialog after a dialog keeps the first one)
    QWidget* focusWidget = QApplication::focusWidget();
    const bool isFocusInDialog = focusWidget && m_scrim && (focusWidget == m_scrim || m_scrim->isAncestorOf(focusWidget));
    if (!isFocusInDialog)
    {
        m_focusBeforeDialog = focusWidget && tab->isAncestorOf(focusWidget) ? focusWidget : nullptr;
    }

    closeDialog();

    // Its 2 minutes start now (item 38)
    if (!entry.isShown)
    {
        entry.isShown = true;
        entry.shownAt = getTime();
    }

    PDFAppMcpConfirmScrim* scrim = new PDFAppMcpConfirmScrim(tab);
    PDFAppMcpConfirmDialog* dialog = new PDFAppMcpConfirmDialog(entry.confirmation, m_host ? &m_host->getResolver() : nullptr, scrim);
    dialog->setInputDelay(m_inputDelay);
    dialog->setQueuePosition(index, count);
    dialog->setRemainingTime(getRemainingTime(entry));
    connect(dialog, &PDFAppMcpConfirmDialog::allowed, this, &PDFAppMcpConfirmationUi::onDialogAllowed);
    connect(dialog, &PDFAppMcpConfirmDialog::denied, this, &PDFAppMcpConfirmationUi::onDialogDenied);
    connect(dialog, &PDFAppMcpConfirmDialog::showOnPageRequested, this, &PDFAppMcpConfirmationUi::onShowOnPageRequested);
    m_scrim = scrim;
    m_dialog = dialog;

    scrim->setDialog(dialog);
    scrim->raise();
    scrim->show();
    if (tab->isVisible())
    {
        dialog->setFocus(Qt::OtherFocusReason);
    }
}

void PDFAppMcpConfirmationUi::closeDialog()
{
    if (PDFAppMcpConfirmScrim* scrim = m_scrim.data())
    {
        // The dialog may be emitting its signal: it is deleted later
        m_scrim = nullptr;
        if (PDFAppMcpConfirmDialog* dialog = m_dialog.data())
        {
            dialog->disconnect(this);
        }

        // Qt gives the focus of a hidden dialog to the next widget of the chain:
        // it goes back to where it was (item 39)
        QWidget* focusWidget = QApplication::focusWidget();
        const bool hadFocus = focusWidget && (focusWidget == scrim || scrim->isAncestorOf(focusWidget));
        QPointer<QWidget> tab = scrim->parentWidget();
        scrim->hide();
        scrim->deleteLater();
        if (hadFocus && tab)
        {
            restoreFocus(tab);
        }
    }
    m_dialog = nullptr;
}

void PDFAppMcpConfirmationUi::restoreFocus(QWidget* tab)
{
    if (!tab || !tab->isVisible())
    {
        return;
    }

    QWidget* target = m_focusBeforeDialog.data();
    m_focusBeforeDialog = nullptr;
    if (!target || !tab->isAncestorOf(target) || !target->isVisible() || !target->isEnabled())
    {
        // The canvas of a document tab
        PDFAppDocumentTab* documentTab = qobject_cast<PDFAppDocumentTab*>(tab);
        PDFAppDocumentView* view = documentTab ? documentTab->getView() : nullptr;
        target = view ? view->getPdfWidget() : nullptr;
    }

    if (target && target->isVisible() && target->isEnabled())
    {
        target->setFocus(Qt::OtherFocusReason);
    }
}

void PDFAppMcpConfirmationUi::showPreview(Entry& entry, PDFAppDocumentTab* tab)
{
    PDFAppDocumentView* view = tab ? tab->getView() : nullptr;
    if (!view)
    {
        closePreview();
        return;
    }

    if (m_previewId == entry.confirmation->id && m_previewView == view && m_previewBar)
    {
        return;
    }

    closePreview();

    std::vector<PDFAppMcpGhost> ghosts;
    for (const QJsonValue& value : entry.confirmation->details.value(QStringLiteral("ghosts")).toArray())
    {
        if (int(ghosts.size()) >= PDFAppMcpGhost::MAXIMUM_GHOSTS)
        {
            break;
        }
        if (std::optional<PDFAppMcpGhost> ghost = PDFAppMcpGhost::fromJson(value.toObject()))
        {
            ghosts.push_back(std::move(*ghost));
        }
    }

    const PDFInteger firstPage = ghosts.empty() ? -1 : ghosts.front().pageIndex;
    m_ghostOverlay->setGhosts(std::move(ghosts));
    m_previewId = entry.confirmation->id;
    m_previewView = view;
    m_previewProxy = view->getProxy();
    if (m_previewProxy)
    {
        m_previewProxy->registerDrawInterface(m_ghostOverlay.get());
        Q_EMIT m_previewProxy->repaintNeeded();
    }

    PDFAppMcpPreviewBar* bar = new PDFAppMcpPreviewBar(entry.confirmation->id, entry.confirmation->clientName, entry.confirmation->clientNameRaw,
                                                       entry.confirmation->details.value(QStringLiteral("summary")).toString(), view->getCanvasArea());
    connect(bar, &PDFAppMcpPreviewBar::allowed, this, &PDFAppMcpConfirmationUi::onDialogAllowed);
    connect(bar, &PDFAppMcpPreviewBar::denied, this, &PDFAppMcpConfirmationUi::onDialogDenied);
    m_previewBar = bar;
    bar->placeAtTop();
    bar->show();
    bar->raise();

    // The message of the status bar while the proposed changes are shown (955:151950)
    if (PDFAppDocumentStatusBar* statusBar = view->getStatusBar())
    {
        statusBar->showMessage(getPreviewStatusText());
    }

    // The page of the first proposed annotation
    QPointer<PDFAppMcpConfirmationUi> guard(this);
    if (firstPage >= 0 && firstPage < view->getPageCount())
    {
        view->goToPage(firstPage);
    }
    Q_UNUSED(guard);
}

QString PDFAppMcpConfirmationUi::getPreviewStatusText()
{
    return tr("Preview of the requested changes – nothing is changed until you allow them");
}

void PDFAppMcpConfirmationUi::closePreview()
{
    // The message of the preview goes with it (the message of a ready document back)
    if (PDFAppDocumentView* view = m_previewView.data())
    {
        PDFAppDocumentStatusBar* statusBar = view->getStatusBar();
        if (statusBar && statusBar->getMessage() == getPreviewStatusText())
        {
            statusBar->showMessage(QCoreApplication::translate("pdf::PDFAppDocumentView", "Ready"));
        }
    }

    if (m_previewProxy)
    {
        m_previewProxy->unregisterDrawInterface(m_ghostOverlay.get());
        Q_EMIT m_previewProxy->repaintNeeded();
    }
    m_previewProxy = nullptr;
    m_previewView = nullptr;
    m_previewId = 0;
    if (m_ghostOverlay)
    {
        m_ghostOverlay->setGhosts(std::vector<PDFAppMcpGhost>());
    }

    if (PDFAppMcpPreviewBar* bar = m_previewBar.data())
    {
        m_previewBar = nullptr;
        bar->disconnect(this);
        bar->hide();
        bar->deleteLater();
    }
}

QString PDFAppMcpConfirmationUi::getStatusText(const PDFAppMcpConfirmation& confirmation, bool withDocument)
{
    const QString client = PDFAppMcpConfirmationUiHelper::getClientName(confirmation);
    const QString document = PDFAppMcpConfirmationUiHelper::getDocumentTitle(confirmation);
    if (withDocument && !document.isEmpty())
    {
        return tr("%1 is waiting for your confirmation · %2").arg(client, document);
    }
    return tr("%1 is waiting for your confirmation").arg(client);
}

void PDFAppMcpConfirmationUi::updateStatusItems(PDFAppMainWindow* window, PDFAppTab* currentTab)
{
    Q_UNUSED(currentTab);

    // Forgotten views
    PDFAppMcpConfirmationUiHelper::removeDeleted(m_statusItems);

    const QList<PDFAppDocumentTab*> tabs = window ? window->getDocumentTabs() : QList<PDFAppDocumentTab*>();
    for (PDFAppDocumentTab* tab : tabs)
    {
        PDFAppDocumentView* view = tab ? tab->getView() : nullptr;
        if (!view)
        {
            continue;
        }

        // The oldest request of this tab, else the oldest request of another tab
        const Entry* own = nullptr;
        const Entry* other = nullptr;
        for (const Entry& entry : m_entries)
        {
            if (!entry.hasDocument)
            {
                continue;
            }
            PDFAppDocumentTab* entryTab = findTab(entry);
            if (entryTab == tab)
            {
                own = own ? own : &entry;
            }
            else if (entryTab)
            {
                other = other ? other : &entry;
            }
        }
        const Entry* shown = own ? own : other;

        QToolButton* item = findStatusItem(view);
        if (!shown)
        {
            if (item)
            {
                item->hide();
            }
            continue;
        }

        if (!item)
        {
            // Status bar item 955:154904: icon/ai 14, label Medium 11 text/tertiary
            item = new QToolButton(view);
            item->setObjectName(QStringLiteral("statusMcpWaiting"));
            item->setProperty(PDFWidgetStyleProperties::STATUS_ITEM, true);
            item->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            item->setIcon(PDFThemeIcons::getIcon(QStringLiteral("ai")));
            item->setIconSize(QSize(14, 14));
            item->setCursor(Qt::PointingHandCursor);
            item->setFocusPolicy(Qt::TabFocus);
            item->installEventFilter(this);
            connect(item, &QToolButton::clicked, this, &PDFAppMcpConfirmationUi::onStatusItemClicked);
            // The first trailing item, the message keeps the free space (955:151949)
            view->addStatusItem(item, PDFAppDocumentStatusBar::Area::TrailingFirst);
            m_statusItems.emplace_back(view, item);
        }

        const QString text = getStatusText(*shown->confirmation, !own);
        item->setText(text);
        item->setToolTip(text);
        item->setAccessibleName(text);
        item->setProperty("confirmationId", QVariant::fromValue(shown->confirmation->id));
        item->show();
    }
}

void PDFAppMcpConfirmationUi::updateBanners(PDFAppMainWindow* window, PDFAppTab* currentTab)
{
    Q_UNUSED(window);

    PDFAppMcpConfirmationUiHelper::removeDeleted(m_banners);

    // The oldest request of a document (its tab is not the current one)
    const Entry* waiting = nullptr;
    for (const Entry& entry : m_entries)
    {
        if (entry.hasDocument && findTab(entry))
        {
            waiting = &entry;
            break;
        }
    }

    PDFAppWorkspaceTab* workspaceTab = qobject_cast<PDFAppWorkspaceTab*>(currentTab);
    const bool isBannerTab = workspaceTab && (workspaceTab->getKind() == PDFAppTabKind::Home || workspaceTab->getKind() == PDFAppTabKind::NewTab);
    for (const auto& item : m_banners)
    {
        if (item.first != workspaceTab || !isBannerTab || !waiting)
        {
            item.second->hide();
        }
    }

    if (!isBannerTab || !waiting)
    {
        return;
    }

    PDFAppMcpWaitingBanner* banner = findBanner(workspaceTab);
    if (!banner)
    {
        banner = new PDFAppMcpWaitingBanner(workspaceTab);
        connect(banner, &PDFAppMcpWaitingBanner::showRequested, this, &PDFAppMcpConfirmationUi::onBannerShowRequested);
        workspaceTab->insertTopWidget(banner);
        m_banners.emplace_back(workspaceTab, banner);
    }
    banner->setConfirmation(waiting->confirmation);
    banner->setRemainingTime(getRemainingTime(*waiting));
    banner->show();
}

void PDFAppMcpConfirmationUi::showDialog(quint64 confirmationId)
{
    Entry* entry = findEntry(confirmationId);
    if (!entry)
    {
        return;
    }

    entry->isPreview = false;
    PDFAppMainWindow* window = getWindow();
    PDFAppDocumentTab* tab = findTab(*entry);
    if (window && tab && window->getCurrentTab() != tab)
    {
        QPointer<PDFAppMcpConfirmationUi> guard(this);
        window->activateTab(tab);
        if (!guard)
        {
            return;
        }
    }
    refresh();
}

bool PDFAppMcpConfirmationUi::showDialogOfActivity(qint64 activityId)
{
    for (const Entry& entry : m_entries)
    {
        if (activityId > 0 && entry.confirmation->activityId == activityId)
        {
            showDialog(entry.confirmation->id);
            return true;
        }
    }
    return false;
}

PDFAppMcpConfirmDialog* PDFAppMcpConfirmationUi::getDialog() const
{
    return m_dialog.data();
}

PDFAppMcpConfirmScrim* PDFAppMcpConfirmationUi::getScrim() const
{
    return m_scrim.data();
}

PDFAppMcpPreviewBar* PDFAppMcpConfirmationUi::getPreviewBar() const
{
    return m_previewBar.data();
}

PDFAppMcpNotificationBar* PDFAppMcpConfirmationUi::findNotificationBar(const PDFAppDocument* document) const
{
    for (const QPointer<PDFAppMcpNotificationBar>& bar : m_notificationBars)
    {
        if (bar && !bar->isHidden() && bar->getInfo().document == document)
        {
            return bar.data();
        }
    }
    return nullptr;
}

QToolButton* PDFAppMcpConfirmationUi::findStatusItem(const PDFAppDocumentView* view) const
{
    for (const auto& item : m_statusItems)
    {
        if (item.first == view && item.second)
        {
            return item.second.data();
        }
    }
    return nullptr;
}

PDFAppMcpWaitingBanner* PDFAppMcpConfirmationUi::findBanner(const PDFAppTab* tab) const
{
    for (const auto& item : m_banners)
    {
        if (item.first == tab && item.second)
        {
            return item.second.data();
        }
    }
    return nullptr;
}

void PDFAppMcpConfirmationUi::onDialogAllowed(quint64 confirmationId)
{
    answer(confirmationId, PDFAppMcpDecision::Allowed);
}

void PDFAppMcpConfirmationUi::onDialogDenied(quint64 confirmationId)
{
    answer(confirmationId, PDFAppMcpDecision::DeniedUser);
}

void PDFAppMcpConfirmationUi::onShowOnPageRequested(quint64 confirmationId)
{
    if (Entry* entry = findEntry(confirmationId))
    {
        entry->isPreview = true;
        scheduleRefresh();
    }
}

void PDFAppMcpConfirmationUi::onStatusItemClicked()
{
    QToolButton* item = qobject_cast<QToolButton*>(sender());
    if (!item)
    {
        return;
    }
    showDialog(item->property("confirmationId").value<quint64>());
}

void PDFAppMcpConfirmationUi::onBannerShowRequested(quint64 confirmationId)
{
    showDialog(confirmationId);
}

void PDFAppMcpConfirmationUi::showNotification(const PDFAppMcpCommitInfo& info)
{
    PDFAppDocumentTab* tab = m_host ? m_host->getResolver().findTab(info.document) : nullptr;
    PDFAppDocumentView* view = tab ? tab->getView() : nullptr;
    QWidget* canvas = view ? view->getCanvasArea() : nullptr;
    if (!canvas)
    {
        return;
    }

    // One bar per document: a new change replaces the previous bar
    std::vector<QPointer<PDFAppMcpNotificationBar>> kept;
    for (const QPointer<PDFAppMcpNotificationBar>& bar : m_notificationBars)
    {
        if (!bar)
        {
            continue;
        }
        if (bar->getInfo().document == info.document || !bar->getInfo().document)
        {
            bar->disconnect(this);
            bar->hide();
            bar->deleteLater();
            continue;
        }
        kept.push_back(bar);
    }
    m_notificationBars = std::move(kept);

    PDFAppMcpNotificationBar* bar = new PDFAppMcpNotificationBar(info, canvas);
    connect(bar, &PDFAppMcpNotificationBar::showChangesRequested, this, &PDFAppMcpConfirmationUi::onNotificationShowChanges);
    connect(bar, &PDFAppMcpNotificationBar::undoRequested, this, &PDFAppMcpConfirmationUi::onNotificationUndo);
    connect(bar, &PDFAppMcpNotificationBar::closeRequested, this, &PDFAppMcpConfirmationUi::onNotificationClose);
    bar->startAutoHide(m_notificationTimeout);
    bar->placeAtTop();
    bar->show();
    bar->raise();
    m_notificationBars.emplace_back(bar);
}

PDFAppMcpNotificationBar* PDFAppMcpConfirmationUi::getNotificationBarOfSender() const
{
    return qobject_cast<PDFAppMcpNotificationBar*>(sender());
}

void PDFAppMcpConfirmationUi::onNotificationShowChanges()
{
    PDFAppMcpNotificationBar* bar = getNotificationBarOfSender();
    if (!bar || !m_host)
    {
        return;
    }

    const PDFAppMcpCommitInfo info = bar->getInfo();
    PDFAppDocumentTab* tab = m_host->getResolver().findTab(info.document);
    QPointer<PDFAppDocumentView> view = tab ? tab->getView() : nullptr;
    if (!view)
    {
        return;
    }

    // The Notes panel with the new annotations selected (955:151457), the
    // outline for its changes
    view->showPanel(info.panelId.isEmpty() ? PDFAppDocumentPanelIds::notes() : info.panelId);
    if (view && !info.annotations.empty())
    {
        if (PDFAppAnnotationController* controller = view->getAnnotationController())
        {
            controller->selectAnnotations(info.annotations);
        }
    }
}

void PDFAppMcpConfirmationUi::onNotificationUndo()
{
    QPointer<PDFAppMcpNotificationBar> bar = getNotificationBarOfSender();
    if (!bar || !m_host || !bar->isChangeOnTop())
    {
        return;
    }

    PDFAppDocumentTab* tab = m_host->getResolver().findTab(bar->getInfo().document);
    if (tab)
    {
        // Undo of the document removes the whole request in one step; the bar
        // closes by the change of the document (a slot may delete the tab)
        tab->executeCommand(PDFAppCommands::undo());
    }
}

void PDFAppMcpConfirmationUi::onNotificationClose()
{
    if (PDFAppMcpNotificationBar* bar = getNotificationBarOfSender())
    {
        bar->disconnect(this);
        bar->hide();
        bar->deleteLater();
    }
}

bool PDFAppMcpConfirmationUi::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Shortcut && isDocumentShortcutBlocked() && qobject_cast<QAction*>(watched))
    {
        // A key of a command of the document below the scrim: consumed, nothing done
        event->accept();
        return true;
    }

    if (event->type() == QEvent::LanguageChange && qobject_cast<QToolButton*>(watched))
    {
        // The texts of the status items in the new language
        scheduleRefresh();
    }
    return BaseClass::eventFilter(watched, event);
}

}   // namespace pdf
