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

#include "pdfprogress.h"
#include "pdfdbgheap.h"

#include <QLocale>

namespace pdf
{

PDFProgress::PDFProgress(QObject* parent) :
    QObject(parent)
{
    qRegisterMetaType<pdf::ProgressStartupInfo>();
}

void PDFProgress::start(size_t stepCount, ProgressStartupInfo startupInfo)
{
    Q_ASSERT(stepCount > 0);

    m_currentStep = 0;
    m_stepCount = stepCount;
    m_percentage = 0;

    Q_EMIT progressStarted(qMove(startupInfo));
}

void PDFProgress::step()
{
    // Atomically increment by one. We must add + 1 to the current step,
    // because fetch_add will return old value. Then we must test, if percentage
    // has been changed.
    size_t currentStep = m_currentStep.fetch_add(1) + 1;

    int newPercentage = int((size_t(100) * currentStep) / m_stepCount);
    int oldPercentage = m_percentage.load(std::memory_order_acquire);
    bool emitSignal = oldPercentage < newPercentage;
    do
    {
        emitSignal = oldPercentage < newPercentage;
    } while (oldPercentage < newPercentage && !m_percentage.compare_exchange_weak(oldPercentage, newPercentage, std::memory_order_release, std::memory_order_relaxed));

    if (emitSignal)
    {
        Q_EMIT progressStep(newPercentage);
    }
}

void PDFProgress::finish()
{
    Q_EMIT progressFinished();
}

void PDFProgress::setStepText(const QString& text)
{
    Q_EMIT progressStepText(text);
}

/// One phase of PDFProgressPhases: its own progress and the connection to the owner
class PDFProgressPhases::Phase : public QObject
{
public:
    Phase(PDFProgressPhases* owner, int weight, int startWeight, const QString& text, bool showCount) :
        m_owner(owner),
        m_progress(nullptr),
        m_weight(weight),
        m_startWeight(startWeight),
        m_text(text),
        m_showCount(showCount)
    {
        connect(&m_progress, &PDFProgress::progressStarted, this, &Phase::onStarted, Qt::DirectConnection);
        connect(&m_progress, &PDFProgress::progressStep, this, &Phase::onStep, Qt::DirectConnection);
    }

    PDFProgress* getProgress() { return &m_progress; }
    int getWeight() const { return m_weight; }
    int getStartWeight() const { return m_startWeight; }

    /// Text of the step of the current state of the phase
    QString getStepText() const
    {
        const size_t count = m_progress.getStepCount();
        if (!m_showCount || count == 0)
        {
            return m_text;
        }

        const QLocale locale;
        return PDFTranslationContext::tr("%1 · %2 of %3").arg(m_text, locale.toString(qulonglong(m_progress.getCurrentStep())), locale.toString(qulonglong(count)));
    }

private:
    void onStarted(pdf::ProgressStartupInfo)
    {
        m_owner->updatePhase(this, 0);
    }

    void onStep(int percentage)
    {
        m_owner->updatePhase(this, percentage);
    }

    PDFProgressPhases* m_owner;
    PDFProgress m_progress;
    int m_weight;
    int m_startWeight;
    QString m_text;
    bool m_showCount;
};

PDFProgressPhases::PDFProgressPhases(PDFProgress* target, const QString& title) :
    m_target(target),
    m_title(title)
{

}

PDFProgressPhases::~PDFProgressPhases()
{
    // A run left early (stopped, failed) does not fill the bar, its start stays balanced
    if (m_target && m_isStarted && !m_isFinished)
    {
        m_target->finish();
    }
}

int PDFProgressPhases::addPhase(int weight, const QString& text, bool showCount)
{
    const int index = int(m_phases.size());
    m_phases.push_back(std::make_unique<Phase>(this, qMax(weight, 0), m_totalWeight, text, showCount));
    m_totalWeight += qMax(weight, 0);
    return index;
}

void PDFProgressPhases::start()
{
    if (m_target && !m_isStarted)
    {
        m_isStarted = true;

        ProgressStartupInfo info;
        info.text = m_title;
        m_target->start(100, std::move(info));
    }
}

PDFProgress* PDFProgressPhases::getPhase(int index)
{
    if (!m_target || index < 0 || index >= int(m_phases.size()))
    {
        return nullptr;
    }

    start();
    return m_phases[size_t(index)]->getProgress();
}

void PDFProgressPhases::beginPhase(int index)
{
    if (m_target && index >= 0 && index < int(m_phases.size()))
    {
        start();
        updatePhase(m_phases[size_t(index)].get(), 0);
    }
}

void PDFProgressPhases::finish()
{
    if (m_target && m_isStarted && !m_isFinished)
    {
        m_isFinished = true;
        for (; m_percentage < 100; ++m_percentage)
        {
            m_target->step();
        }
        m_target->finish();
    }
}

void PDFProgressPhases::updatePhase(const Phase* phase, int phasePercentage)
{
    if (!m_target || m_isFinished)
    {
        return;
    }

    m_target->setStepText(phase->getStepText());

    if (m_totalWeight > 0)
    {
        const int percentage = qBound(0, (phase->getStartWeight() * 100 + phase->getWeight() * qBound(0, phasePercentage, 100)) / m_totalWeight, 100);
        for (; m_percentage < percentage; ++m_percentage)
        {
            m_target->step();
        }
    }
}

} // namespace pdf

#include "moc_pdfprogress.cpp"
