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

#ifndef PDFAPPMCPCONFIRMATIONUI_H
#define PDFAPPMCPCONFIRMATIONUI_H

#include "pdfappglobal.h"
#include "pdfappmcpchange.h"
#include "pdfappmcpconfirmation.h"

#include "pdfcomponentbadges.h"

#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>
#include <vector>

class QAction;
class QPushButton;
class QTabBar;
class QTimer;
class QToolButton;

namespace pdf
{

class PDFAppDocumentTab;
class PDFAppDocumentView;
class PDFAppMainWindow;
class PDFAppMcpConfirmDialog;
class PDFAppMcpConfirmScrim;
class PDFAppMcpGhostOverlay;
class PDFAppMcpHost;
class PDFAppMcpNotificationBar;
class PDFAppMcpPreviewBar;
class PDFAppTab;
class PDFAppWorkspaceTab;
class PDFDrawWidgetProxy;
class PDFLabel;

/// Banner of a request waiting in a document tab, at the top of the Home tab and
/// of a New tab (967:12873): blue card, the monogram 36 of the assistant, "Claude
/// Code is waiting for your confirmation · Invoice.pdf", the body with the
/// countdown and Show
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpWaitingBanner : public PDFTintedCard
{
    Q_OBJECT

private:
    using BaseClass = PDFTintedCard;

public:
    explicit PDFAppMcpWaitingBanner(QWidget* parent);
    virtual ~PDFAppMcpWaitingBanner() override;

    /// The request shown by the banner
    void setConfirmation(const std::shared_ptr<PDFAppMcpConfirmation>& confirmation);
    quint64 getConfirmationId() const { return m_confirmationId; }

    void setRemainingTime(qint64 milliseconds);

    QString getTitleText() const;
    QString getBodyText() const;
    QPushButton* getShowButton() const;

Q_SIGNALS:
    void showRequested(quint64 confirmationId);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void updateTexts();
    void onShowClicked();

    quint64 m_confirmationId = 0;
    PDFAppMcpConfirmationKind m_kind = PDFAppMcpConfirmationKind::Change;
    QString m_clientName;
    QString m_clientNameRaw;
    QString m_documentTitle;
    qint64 m_remainingTime = 0;
    QPointer<QWidget> m_monogram;
    QPointer<PDFLabel> m_titleLabel;
    QPointer<PDFLabel> m_bodyLabel;
    QPointer<QPushButton> m_showButton;
};

/// The user interface of the confirmations of the assistants (plan 5.4, package
/// C), installed into the host as its confirmation provider: one queue per tab
/// (the requests of a document wait in its tab, the others - open, the read
/// approval without a document tab - on the active tab), the dialog of the
/// oldest request of the current tab with "1 of N", the countdown and the 2 min
/// timeout (an injected clock for the tests), Show on Page with the ghosts and
/// the preview bar, the status item "<client> is waiting for your confirmation"
/// in every document tab, the banner of the Home tab and of a New tab, the alert
/// of a minimized window, and the notification bar after every committed change
/// of an assistant (also with Edit documents = Allow). A closed tab denies its
/// requests (denied_closed). GUI thread; a child of the host.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpConfirmationUi : public QObject, public PDFAppMcpConfirmationProvider
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// No answer within 2 minutes = denied (955:150808); the time counts from the
    /// moment the dialog of the request appears (item 38)
    static constexpr int DEFAULT_TIMEOUT = 120000;

    /// A request, whose dialog has not appeared yet (behind other requests of its
    /// tab, in a tab, which is not shown, or behind a modal dialog of the
    /// application - plan 2.5), is denied after 10 minutes of waiting (the
    /// overall bound of the queue; its dialog has then its whole DEFAULT_TIMEOUT)
    static constexpr int DEFAULT_QUEUE_TIMEOUT = 600000;

    /// The buttons ignore input for 1 s after the dialog appears
    static constexpr int DEFAULT_INPUT_DELAY = 1000;

    /// The notification bar hides itself after 30 s (owner Q3)
    static constexpr int DEFAULT_NOTIFICATION_TIMEOUT = 30000;

    /// Interval of the countdown
    static constexpr int TICK_INTERVAL = 1000;

    /// Creates the interface and installs it into the host as its provider
    explicit PDFAppMcpConfirmationUi(PDFAppMcpHost* host);
    virtual ~PDFAppMcpConfirmationUi() override;

    /// The interface of the host (its child), nullptr without one
    static PDFAppMcpConfirmationUi* find(const PDFAppMcpHost* host);

    /// A change of an assistant has been committed: the notification bar in the
    /// tab of its document (940:33875)
    static void notifyCommitted(PDFAppMcpHost* host, const PDFAppMcpCommitInfo& info);

    // PDFAppMcpConfirmationProvider
    virtual void showConfirmation(std::shared_ptr<PDFAppMcpConfirmation> confirmation) override;
    virtual void withdrawConfirmation(quint64 confirmationId, PDFAppMcpDecision decision) override;

    /// Clock of the countdown and of the timeout in ms (tests; the system clock by default)
    void setClock(std::function<qint64()> clock);
    qint64 getTime() const;

    void setTimeout(int timeout) { m_timeout = qMax(0, timeout); }
    int getTimeout() const { return m_timeout; }

    void setQueueTimeout(int timeout) { m_queueTimeout = qMax(0, timeout); }
    int getQueueTimeout() const { return m_queueTimeout; }

    /// Remaining time of a waiting request in ms (its dialog shown: of the
    /// DEFAULT_TIMEOUT, otherwise of the DEFAULT_QUEUE_TIMEOUT; -1 = no such request)
    qint64 getRemainingTime(quint64 confirmationId) const;

    /// The dialog of the request has appeared (its timeout runs)
    bool isShown(quint64 confirmationId) const;

    /// No new dialog appears while a modal dialog of the application is open:
    /// a request waits for it (plan 2.5 - a first read with Ask, files.open)
    bool isWaitingForModalDialog() const { return m_isWaitingForModal; }

    void setInputDelay(int delay) { m_inputDelay = qMax(0, delay); }
    int getInputDelay() const { return m_inputDelay; }

    void setNotificationTimeout(int timeout) { m_notificationTimeout = qMax(0, timeout); }

    /// Processes the countdown and the timeouts now (the timer does it each second)
    void tick();

    /// Updates the dialog, the preview, the status items and the banners now
    void refresh();

    /// Ids of the waiting requests in their order
    std::vector<quint64> getWaitingIds() const;

    /// Shows the request: switches to its tab and gives the dialog back from its
    /// preview (the status item, the banner, Show of the AI page)
    void showDialog(quint64 confirmationId);

    /// Shows the request of a waiting activity row (Show of the AI page)
    bool showDialogOfActivity(qint64 activityId);

    /// Answers a request as its buttons do
    void answer(quint64 confirmationId, PDFAppMcpDecision decision);

    // Parts of the user interface (tests)
    PDFAppMcpConfirmDialog* getDialog() const;
    PDFAppMcpConfirmScrim* getScrim() const;
    PDFAppMcpPreviewBar* getPreviewBar() const;
    const PDFAppMcpGhostOverlay* getGhostOverlay() const { return m_ghostOverlay.get(); }
    PDFAppMcpNotificationBar* findNotificationBar(const PDFAppDocument* document) const;
    QToolButton* findStatusItem(const PDFAppDocumentView* view) const;
    PDFAppMcpWaitingBanner* findBanner(const PDFAppTab* tab) const;

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    class Entry
    {
    public:
        std::shared_ptr<PDFAppMcpConfirmation> confirmation;
        qint64 queuedAt = 0;        ///< The request came (the overall bound of the queue)
        qint64 shownAt = 0;         ///< Its dialog appeared first (valid when isShown)
        bool isShown = false;
        bool hasDocument = false;
        bool isPreview = false;
    };

    void scheduleRefresh();
    void onRefreshQueued();
    void onDialogAllowed(quint64 confirmationId);
    void onDialogDenied(quint64 confirmationId);
    void onShowOnPageRequested(quint64 confirmationId);
    void onStatusItemClicked();
    void onBannerShowRequested(quint64 confirmationId);
    void onNotificationShowChanges();
    void onNotificationUndo();
    void onNotificationClose();
    void showNotification(const PDFAppMcpCommitInfo& info);

    PDFAppMainWindow* getWindow() const;
    PDFAppDocumentTab* findTab(const Entry& entry) const;
    Entry* findEntry(quint64 confirmationId);
    bool removeEntry(quint64 confirmationId);
    qint64 getRemainingTime(const Entry& entry) const;
    void removeClosedEntries();
    void watchWindow(PDFAppMainWindow* window);
    bool isDocumentShortcutBlocked() const;
    void showDialogEntry(Entry& entry, PDFAppTab* tab, int index, int count);
    void closeDialog();
    void restoreFocus(QWidget* tab);
    void onFocusWindowChanged();
    void showPreview(Entry& entry, PDFAppDocumentTab* tab);
    void closePreview();
    void updateStatusItems(PDFAppMainWindow* window, PDFAppTab* currentTab);
    void updateBanners(PDFAppMainWindow* window, PDFAppTab* currentTab);
    static QString getStatusText(const PDFAppMcpConfirmation& confirmation, bool withDocument);

    /// Message of the status bar of the tab while the proposed changes are shown (955:151950)
    static QString getPreviewStatusText();
    PDFAppMcpNotificationBar* getNotificationBarOfSender() const;

    QPointer<PDFAppMcpHost> m_host;
    std::vector<Entry> m_entries;
    std::function<qint64()> m_clock;
    int m_timeout = DEFAULT_TIMEOUT;
    int m_queueTimeout = DEFAULT_QUEUE_TIMEOUT;
    bool m_isWaitingForModal = false;   ///< A dialog waits for a modal dialog of the application to close
    QPointer<QWidget> m_focusBeforeDialog;  ///< The focused widget of the tab before the dialog took the focus
    int m_inputDelay = DEFAULT_INPUT_DELAY;
    int m_notificationTimeout = DEFAULT_NOTIFICATION_TIMEOUT;
    QPointer<QTimer> m_tickTimer;
    bool m_isRefreshQueued = false;
    bool m_hasSeenWindow = false;      ///< A window has shown the requests (its loss denies the requests without a document)

    QPointer<QTabBar> m_watchedTabBar;

    /// The actions of the commands of the documents of the watched window: their
    /// keys do nothing while the dialog covers the tab (modal for its tab)
    std::vector<QPointer<QAction>> m_watchedActions;
    QPointer<PDFAppMcpConfirmScrim> m_scrim;
    QPointer<PDFAppMcpConfirmDialog> m_dialog;

    quint64 m_previewId = 0;
    std::unique_ptr<PDFAppMcpGhostOverlay> m_ghostOverlay;
    QPointer<PDFDrawWidgetProxy> m_previewProxy;
    QPointer<PDFAppMcpPreviewBar> m_previewBar;
    QPointer<PDFAppDocumentView> m_previewView;

    std::vector<std::pair<QPointer<PDFAppDocumentView>, QPointer<QToolButton>>> m_statusItems;
    std::vector<std::pair<QPointer<PDFAppWorkspaceTab>, QPointer<PDFAppMcpWaitingBanner>>> m_banners;
    std::vector<QPointer<PDFAppMcpNotificationBar>> m_notificationBars;
};

}   // namespace pdf

#endif // PDFAPPMCPCONFIRMATIONUI_H
