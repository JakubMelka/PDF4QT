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

#ifndef PDFAPPMINUTETICKER_H
#define PDFAPPMINUTETICKER_H

#include "pdfappglobal.h"

#include <QObject>
#include <QPointer>

#include <limits>
#include <vector>

class QTimer;
class QWidget;

namespace pdf
{

class PDFAppMinuteSubscription;

/// One shared minute timer of the relative times of recent activity (owner
/// decision O25, PDFAppLocale::formatRecentTime): "Just now", "5 minutes ago",
/// "Today 15:56" change with the clock. A page, a panel or a list, which shows
/// such texts, subscribes its widget (PDFAppMinuteSubscription); the timer runs
/// only while a subscribed widget is visible. It is a single-shot timer to the
/// next boundary of a minute of the clock (started again after each tick), so the
/// texts change together with the clock of the system. A tick notifies the visible
/// subscriptions, whose texts were updated in an older minute; a hidden widget is
/// notified, when it is shown again (from the event loop). The subscribers update
/// only their time texts - they read no database and rebuild no rows.
///
/// Owned by PDFAppContext (getMinuteTicker), GUI thread. Lifetime: a subscription
/// is a child of its widget; the ticker and the subscriptions hold each other by
/// QPointer, so either may go first, also from the signal of a tick.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMinuteTicker : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// A minute in milliseconds
    static constexpr qint64 MINUTE = 60 * 1000;

    /// Delay of the timer after the boundary of a minute (a timer of the system
    /// may fire a few milliseconds early)
    static constexpr int BOUNDARY_DELAY = 50;

    explicit PDFAppMinuteTicker(QObject* parent = nullptr);
    virtual ~PDFAppMinuteTicker() override;

    /// The timer is pending (a subscribed widget is visible)
    bool isRunning() const;

    /// Milliseconds until the pending tick (-1 = the timer is not running)
    int getRemainingTime() const;

    /// Number of the living subscriptions / of those, whose widgets are visible
    int getSubscriptionCount() const;
    int getActiveSubscriptionCount() const;

    /// Number of the ticks (tests)
    int getTickCount() const { return m_tickCount; }

    /// A minute has passed: notifies the visible subscriptions against the current
    /// time of PDFAppLocale::getCurrentTime and starts the timer again (the timer
    /// calls it; tests move the clock by PDFAppLocale::setCurrentTimeOverride and
    /// call it - no real waiting). The receivers may delete the ticker, the
    /// subscriptions and their widgets from the notification.
    void tick();

    /// Milliseconds from \p time (since the epoch) to the next boundary of a
    /// minute, with BOUNDARY_DELAY (1 ... MINUTE + BOUNDARY_DELAY)
    static int getIntervalToNextMinute(qint64 time);

    /// Minute of the time since the epoch (rounded down, also for negative times)
    static qint64 getMinute(qint64 time);

private:
    friend class PDFAppMinuteSubscription;

    void addSubscription(PDFAppMinuteSubscription* subscription);
    void removeSubscription(PDFAppMinuteSubscription* subscription);

    /// Starts / stops the timer by the visibility of the widgets from the event
    /// loop (a show or hide event is in progress, when it is asked for)
    void scheduleTimerUpdate();
    void updateTimer();

    void onTimeout();

    QPointer<QTimer> m_timer;
    std::vector<QPointer<PDFAppMinuteSubscription>> m_subscriptions;
    int m_tickCount = 0;
    bool m_isTimerUpdatePending = false;
};

/// Subscription of a widget to the minute ticker (a child of the widget, deleted
/// with it). It watches the show and hide events of the widget: while the widget
/// is visible, the ticker runs; minuteChanged comes after a tick of a new minute
/// and after the widget has been shown in a minute later than its last update.
/// The owner tells by setUpdated, that it has formatted its texts itself (a
/// refresh), so a show after it emits nothing.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMinuteSubscription : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Subscription of \p widget (its parent) to \p ticker (nullptr = no ticks:
    /// a widget without a context)
    explicit PDFAppMinuteSubscription(PDFAppMinuteTicker* ticker, QWidget* widget);
    virtual ~PDFAppMinuteSubscription() override;

    /// Watched widget (nullptr after it is gone)
    QWidget* getWidget() const;

    /// The widget is visible: the ticker runs for it
    bool isActive() const;

    /// The owner has formatted its time texts against \p time (since the epoch)
    void setUpdated(qint64 time);

    /// Minute of the last update (std::numeric_limits<qint64>::min() = never)
    qint64 getUpdatedMinute() const { return m_updatedMinute; }

Q_SIGNALS:
    /// The minute has changed since the last update of the texts of the visible
    /// widget: the owner formats its time texts against \p now again (it reads no
    /// database and rebuilds no rows). The receiver may delete the widget.
    void minuteChanged(qint64 now);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class PDFAppMinuteTicker;

    /// Emits minuteChanged, when the minute of \p now differs from the last update
    void notify(qint64 now);

    /// The widget has been shown (from the event loop)
    void onShown();

    QPointer<PDFAppMinuteTicker> m_ticker;
    QPointer<QWidget> m_widget;
    qint64 m_updatedMinute = std::numeric_limits<qint64>::min();
    bool m_isShowPending = false;
};

}   // namespace pdf

#endif // PDFAPPMINUTETICKER_H
