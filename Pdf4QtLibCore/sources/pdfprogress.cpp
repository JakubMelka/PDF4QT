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

#include <algorithm>

namespace pdf
{

PDFProgressSink::~PDFProgressSink() = default;

void PDFProgressSink::setPhases(std::vector<PDFProgressPhase> phases)
{
    Q_UNUSED(phases);
}

void PDFProgressSink::beginPhase(int index)
{
    Q_UNUSED(index);
}

void PDFProgressSink::finishPhases()
{

}

void PDFProgressSink::addMessage(const QString& message)
{
    Q_UNUSED(message);
}

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

PDFProgressCounter::PDFProgressCounter() = default;

PDFProgressCounter::~PDFProgressCounter() = default;

void PDFProgressCounter::start(size_t stepCount, ProgressStartupInfo startupInfo)
{
    Q_UNUSED(startupInfo);

    // The steps of the part come after its start (the same thread, or a parallel loop started after it)
    m_done.store(0, std::memory_order_relaxed);
    m_count.store(stepCount, std::memory_order_release);
}

void PDFProgressCounter::step()
{
    const size_t done = m_done.fetch_add(1, std::memory_order_acq_rel) + 1;
    const size_t count = m_count.load(std::memory_order_acquire);
    if (count > 0)
    {
        raisePercentage(getPercentage(m_table.load(std::memory_order_acquire), m_phase.load(std::memory_order_acquire), done, count));
    }
}

void PDFProgressCounter::finish()
{
    // The end of the current part: the end of its phase (or of the whole bar)
    raisePercentage(getPercentage(m_table.load(std::memory_order_acquire), m_phase.load(std::memory_order_acquire), 1, 1));
}

void PDFProgressCounter::setPhases(std::vector<PDFProgressPhase> phases)
{
    std::unique_ptr<PhaseTable> table = std::make_unique<PhaseTable>();
    table->phases = std::move(phases);
    for (PDFProgressPhase& phase : table->phases)
    {
        phase.weight = qMax(phase.weight, 0);
        table->startWeights.push_back(table->totalWeight);
        table->totalWeight += phase.weight;
    }

    // An older table stays owned: a reader may be reading it now
    const PhaseTable* published = table.get();
    m_tables.push_back(std::move(table));
    m_phase.store(-1, std::memory_order_relaxed);
    m_table.store(published, std::memory_order_release);
}

void PDFProgressCounter::beginPhase(int index)
{
    const PhaseTable* table = m_table.load(std::memory_order_acquire);
    if (!table || index < 0 || index >= int(table->phases.size()))
    {
        return;
    }

    // The counts are reset before the phase is published: a reader, which sees the
    // phase, sees the counts of it
    m_count.store(0, std::memory_order_relaxed);
    m_done.store(0, std::memory_order_relaxed);
    m_phase.store(index, std::memory_order_release);
    raisePercentage(getPercentage(table, index, 0, 0));
}

void PDFProgressCounter::finishPhases()
{
    raisePercentage(100);
}

PDFProgressCounter::Snapshot PDFProgressCounter::snapshot() const
{
    Snapshot result;
    result.percentage = m_percentage.load(std::memory_order_acquire);

    // The phase is read again after its counts: a phase begun meanwhile reads them again
    // (a few times at most - a run does not switch the phases so fast)
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        result.phase = m_phase.load(std::memory_order_acquire);
        result.count = m_count.load(std::memory_order_acquire);
        result.done = m_done.load(std::memory_order_acquire);
        if (m_phase.load(std::memory_order_acquire) == result.phase)
        {
            break;
        }
    }
    result.done = std::min(result.done, result.count);

    const PhaseTable* table = m_table.load(std::memory_order_acquire);
    if (table && result.phase >= 0 && result.phase < int(table->phases.size()))
    {
        const PDFProgressPhase& phase = table->phases[size_t(result.phase)];
        result.text = phase.text;
        result.showCount = phase.showCount;
    }
    else
    {
        result.phase = -1;
    }
    return result;
}

int PDFProgressCounter::getPercentage(const PhaseTable* table, int phase, size_t done, size_t count)
{
    const double fraction = count > 0 ? double(std::min(done, count)) / double(count) : 0.0;
    if (!table)
    {
        // No phases: the part is the whole bar
        return qBound(0, int(fraction * 100.0), 100);
    }

    if (phase < 0 || phase >= int(table->phases.size()) || table->totalWeight <= 0)
    {
        return 0;
    }

    const double start = table->startWeights[size_t(phase)];
    const double weight = table->phases[size_t(phase)].weight;
    return qBound(0, int((start + weight * fraction) * 100.0 / double(table->totalWeight)), 100);
}

void PDFProgressCounter::raisePercentage(int percentage)
{
    // An atomic maximum: two threads may step in the other order, the bar never goes back
    int current = m_percentage.load(std::memory_order_relaxed);
    while (current < percentage && !m_percentage.compare_exchange_weak(current, percentage, std::memory_order_release, std::memory_order_relaxed))
    {
    }
}

/// The progress of one phase: its start begins the phase of the target, its steps are
/// the steps of the target (any thread, nothing of its own is changed)
class PDFProgressPhases::Phase : public PDFProgressSink
{
public:
    Phase(PDFProgressSink* target, int index) :
        m_target(target),
        m_index(index)
    {

    }

    virtual void start(size_t stepCount, ProgressStartupInfo startupInfo) override
    {
        m_target->beginPhase(m_index);
        m_target->start(stepCount, std::move(startupInfo));
    }

    virtual void step() override
    {
        m_target->step();
    }

    virtual void finish() override
    {
        m_target->finish();
    }

    virtual void addMessage(const QString& message) override
    {
        m_target->addMessage(message);
    }

private:
    PDFProgressSink* m_target;
    int m_index;
};

PDFProgressPhases::PDFProgressPhases(PDFProgressSink* target) :
    m_target(target)
{

}

PDFProgressPhases::~PDFProgressPhases() = default;

int PDFProgressPhases::addPhase(int weight, const QString& text, bool showCount)
{
    Q_ASSERT(!m_isStarted);

    const int index = int(m_phases.size());
    PDFProgressPhase phase;
    phase.weight = qMax(weight, 0);
    phase.text = text;
    phase.showCount = showCount;
    m_phases.push_back(std::move(phase));
    m_phaseSinks.push_back(std::make_unique<Phase>(m_target, index));
    return index;
}

void PDFProgressPhases::start()
{
    if (m_target && !m_isStarted)
    {
        m_isStarted = true;
        m_target->setPhases(m_phases);
    }
}

PDFProgressSink* PDFProgressPhases::getPhase(int index)
{
    if (!m_target || index < 0 || index >= int(m_phaseSinks.size()))
    {
        return nullptr;
    }

    start();
    return m_phaseSinks[size_t(index)].get();
}

void PDFProgressPhases::beginPhase(int index)
{
    if (m_target && index >= 0 && index < int(m_phases.size()))
    {
        start();
        m_target->beginPhase(index);
    }
}

void PDFProgressPhases::finish()
{
    if (m_target)
    {
        start();
        m_target->finishPhases();
    }
}

} // namespace pdf

#include "moc_pdfprogress.cpp"
