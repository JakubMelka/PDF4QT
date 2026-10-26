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

#include "pdfappminuteticker.h"
#include "pdfapplocale.h"

#include <QDateTime>
#include <QEvent>
#include <QTimer>
#include <QWidget>

namespace pdf
{

PDFAppMinuteTicker::PDFAppMinuteTicker(QObject* parent) :
    BaseClass(parent)
{
    // The texts change with the minutes of the clock: a precise single-shot timer
    // to the next boundary (a coarse timer may be late by seconds)
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &PDFAppMinuteTicker::onTimeout);
}

PDFAppMinuteTicker::~PDFAppMinuteTicker()
{
    // The subscriptions see the ticker gone by their QPointer
    m_subscriptions.clear();
}

bool PDFAppMinuteTicker::isRunning() const
{
    return m_timer && m_timer->isActive();
}

int PDFAppMinuteTicker::getRemainingTime() const
{
    return isRunning() ? m_timer->remainingTime() : -1;
}

int PDFAppMinuteTicker::getSubscriptionCount() const
{
    int count = 0;
    for (const QPointer<PDFAppMinuteSubscription>& subscription : m_subscriptions)
    {
        count += subscription ? 1 : 0;
    }
    return count;
}

int PDFAppMinuteTicker::getActiveSubscriptionCount() const
{
    int count = 0;
    for (const QPointer<PDFAppMinuteSubscription>& subscription : m_subscriptions)
    {
        count += subscription && subscription->isActive() ? 1 : 0;
    }
    return count;
}

void PDFAppMinuteTicker::tick()
{
    ++m_tickCount;
    const qint64 now = PDFAppLocale::getCurrentTime();

    // A receiver may delete the ticker (the context), a subscription or its widget,
    // or subscribe another widget: a copy of the list, the ticker guarded
    QPointer<PDFAppMinuteTicker> self(this);
    const std::vector<QPointer<PDFAppMinuteSubscription>> subscriptions = m_subscriptions;
    for (const QPointer<PDFAppMinuteSubscription>& subscription : subscriptions)
    {
        if (subscription && subscription->isActive())
        {
            subscription->notify(now);
            if (!self)
            {
                return;
            }
        }
    }

    updateTimer();
}

int PDFAppMinuteTicker::getIntervalToNextMinute(qint64 time)
{
    const qint64 elapsed = time - getMinute(time) * MINUTE;
    return int(MINUTE - elapsed) + BOUNDARY_DELAY;
}

qint64 PDFAppMinuteTicker::getMinute(qint64 time)
{
    // Rounded down also before the epoch (a clock set to a wrong date)
    return time >= 0 ? time / MINUTE : -((-time + MINUTE - 1) / MINUTE);
}

void PDFAppMinuteTicker::addSubscription(PDFAppMinuteSubscription* subscription)
{
    m_subscriptions.emplace_back(subscription);
    scheduleTimerUpdate();
}

void PDFAppMinuteTicker::removeSubscription(PDFAppMinuteSubscription* subscription)
{
    // The destroyed subscriptions go too (their pointers are null)
    std::vector<QPointer<PDFAppMinuteSubscription>> subscriptions;
    subscriptions.reserve(m_subscriptions.size());
    for (const QPointer<PDFAppMinuteSubscription>& item : m_subscriptions)
    {
        if (item && item.data() != subscription)
        {
            subscriptions.push_back(item);
        }
    }
    m_subscriptions = std::move(subscriptions);
    scheduleTimerUpdate();
}

void PDFAppMinuteTicker::scheduleTimerUpdate()
{
    if (m_isTimerUpdatePending)
    {
        return;
    }

    m_isTimerUpdatePending = true;
    QMetaObject::invokeMethod(this, &PDFAppMinuteTicker::updateTimer, Qt::QueuedConnection);
}

void PDFAppMinuteTicker::updateTimer()
{
    m_isTimerUpdatePending = false;
    if (!m_timer)
    {
        return;
    }

    if (getActiveSubscriptionCount() == 0)
    {
        m_timer->stop();
    }
    else if (!m_timer->isActive())
    {
        // The boundary of a minute of the real clock (the injected time of the
        // tests does not move by itself)
        m_timer->start(getIntervalToNextMinute(QDateTime::currentMSecsSinceEpoch()));
    }
}

void PDFAppMinuteTicker::onTimeout()
{
    tick();
}

PDFAppMinuteSubscription::PDFAppMinuteSubscription(PDFAppMinuteTicker* ticker, QWidget* widget) :
    BaseClass(widget),
    m_ticker(ticker),
    m_widget(widget)
{
    if (m_widget)
    {
        m_widget->installEventFilter(this);
    }
    if (m_ticker)
    {
        m_ticker->addSubscription(this);
    }
}

PDFAppMinuteSubscription::~PDFAppMinuteSubscription()
{
    // The event filter of a destroyed object is skipped by Qt; the widget is not
    // touched (it may be being destroyed - the subscription is its child)
    if (m_ticker)
    {
        m_ticker->removeSubscription(this);
    }
}

QWidget* PDFAppMinuteSubscription::getWidget() const
{
    return m_widget.data();
}

bool PDFAppMinuteSubscription::isActive() const
{
    return m_widget && m_widget->isVisible();
}

void PDFAppMinuteSubscription::setUpdated(qint64 time)
{
    m_updatedMinute = PDFAppMinuteTicker::getMinute(time);
}

bool PDFAppMinuteSubscription::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_widget.data() && event && (event->type() == QEvent::Show || event->type() == QEvent::Hide))
    {
        // Nothing of the owner runs inside of the show or hide event (it may delete
        // the widget): the timer and the texts follow from the event loop
        if (m_ticker)
        {
            m_ticker->scheduleTimerUpdate();
        }
        if (event->type() == QEvent::Show && !m_isShowPending)
        {
            m_isShowPending = true;
            QMetaObject::invokeMethod(this, &PDFAppMinuteSubscription::onShown, Qt::QueuedConnection);
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppMinuteSubscription::notify(qint64 now)
{
    const qint64 minute = PDFAppMinuteTicker::getMinute(now);
    if (minute == m_updatedMinute)
    {
        return;
    }

    // The receiver may delete the widget and this subscription: nothing after the emit
    m_updatedMinute = minute;
    Q_EMIT minuteChanged(now);
}

void PDFAppMinuteSubscription::onShown()
{
    m_isShowPending = false;
    if (isActive())
    {
        notify(PDFAppLocale::getCurrentTime());
    }
}

}   // namespace pdf
