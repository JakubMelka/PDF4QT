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

#ifndef PDFPROGRESS_H
#define PDFPROGRESS_H

#include "pdfglobal.h"

#include <QObject>

#include <atomic>
#include <memory>
#include <vector>

namespace pdf
{

struct ProgressStartupInfo
{
    bool showDialog = false;
    QString text;
};

/// One phase of a run (a tool of the catalog): its part of the bar and its text
class PDF4QTLIBCORESHARED_EXPORT PDFProgressPhase
{
public:
    int weight = 0;             ///< Part of the bar (the weights of the phases are relative)
    QString text;               ///< Translated text of the phase ("Recompressing images")
    bool showCount = false;     ///< The reader shows the count of the phase ("Recompressing images · 152 of 412")
};

/// Receiver of the progress of a computation (an interface, not a QObject). The
/// computation calls it, the user interface never runs in it: a reader polls the
/// state in its own thread (PDFProgressCounter).
///
/// Contract of an implementation: every function may be called from any thread at
/// once (the body of a parallel loop steps), it never blocks and it never runs
/// foreign code (no signal of a direct connection, no callback). start, finish and
/// the functions of the phases are called by the thread of the run outside of its
/// parallel loops; step is called from any thread.
class PDF4QTLIBCORESHARED_EXPORT PDFProgressSink
{
public:
    PDFProgressSink() = default;
    virtual ~PDFProgressSink();

    PDFProgressSink(const PDFProgressSink&) = delete;
    PDFProgressSink& operator=(const PDFProgressSink&) = delete;

    /// Starts a part of \p stepCount steps (of the current phase, when there are phases)
    virtual void start(size_t stepCount, ProgressStartupInfo startupInfo) = 0;

    /// One step of the part is done
    virtual void step() = 0;

    /// The part is done
    virtual void finish() = 0;

    /// Sets the phases of the run, before the first of them begins. The default ignores them.
    virtual void setPhases(std::vector<PDFProgressPhase> phases);

    /// Begins the phase \p index: start, step and finish belong to it then (the phases go
    /// in their order, the parts of the phases before it are full). The default ignores it.
    virtual void beginPhase(int index);

    /// All phases are done (the bar is full). The default ignores it.
    virtual void finishPhases();

    /// A text of the done work ("Unused objects removed: 12"), for a log of the caller.
    /// The default ignores it.
    virtual void addMessage(const QString& message);
};

/// Progress of the old applications: the signals tell the start, the percentage and the
/// end. They are emitted in the thread, which calls start, step and finish - also in the
/// threads of a parallel loop -, so the receivers live in the GUI thread and take them
/// queued (the automatic connection), never by Qt::DirectConnection. The new
/// application reads a PDFProgressCounter instead.
class PDF4QTLIBCORESHARED_EXPORT PDFProgress : public QObject, public PDFProgressSink
{
    Q_OBJECT

public:
    explicit PDFProgress(QObject* parent);

    virtual void start(size_t stepCount, ProgressStartupInfo startupInfo) override;
    virtual void step() override;
    virtual void finish() override;

    /// Number of the finished steps
    size_t getCurrentStep() const { return m_currentStep.load(); }

    /// Number of the steps given to start
    size_t getStepCount() const { return m_stepCount.load(); }

signals:
    // Namespace pdf:: must be here, due to queued signals calling
    void progressStarted(pdf::ProgressStartupInfo info);
    void progressStep(int percentage);
    void progressFinished();

private:
    std::atomic<size_t> m_currentStep = 0;
    std::atomic<size_t> m_stepCount = 0;
    std::atomic<int> m_percentage = 0;
};

/// Progress of a run read by polling (the pull model of the new application): the
/// computation steps it by atomics only, a reader in another thread (a timer of the
/// GUI, of MCP) takes a snapshot whenever it wants. The table of the phases is
/// immutable, once it is set; the current phase, the done steps and the step count of
/// the phase are atomics, the percentage is monotonic (an atomic maximum). Without a
/// table of the phases the whole bar is one part (start, step, finish).
class PDF4QTLIBCORESHARED_EXPORT PDFProgressCounter : public PDFProgressSink
{
public:
    /// State of the progress at one moment (a plain value)
    class Snapshot
    {
    public:
        int phase = -1;             ///< Index of the current phase, -1 = none
        QString text;               ///< Text of the current phase (empty = none)
        bool showCount = false;     ///< The count of the phase belongs to its text
        size_t done = 0;            ///< Done steps of the current part (at most count)
        size_t count = 0;           ///< Steps of the current part, 0 = not started
        int percentage = 0;         ///< Progress of the run, 0 to 100, never goes back
    };

    PDFProgressCounter();
    virtual ~PDFProgressCounter() override;

    virtual void start(size_t stepCount, ProgressStartupInfo startupInfo) override;
    virtual void step() override;
    virtual void finish() override;
    virtual void setPhases(std::vector<PDFProgressPhase> phases) override;
    virtual void beginPhase(int index) override;
    virtual void finishPhases() override;

    /// The current state; any thread, at any time
    Snapshot snapshot() const;

private:
    /// The phases with the start of each of them in the weights; immutable, once set
    class PhaseTable
    {
    public:
        std::vector<PDFProgressPhase> phases;
        std::vector<int> startWeights;
        int totalWeight = 0;
    };

    /// Percentage of \p done of \p count steps of the phase \p phase (count 0: the start of the phase)
    static int getPercentage(const PhaseTable* table, int phase, size_t done, size_t count);

    /// The percentage grows to \p percentage, when it is less
    void raisePercentage(int percentage);

    std::vector<std::unique_ptr<const PhaseTable>> m_tables;    ///< Every table set (the thread of the run); a reader may still hold an older one
    std::atomic<const PhaseTable*> m_table = nullptr;           ///< The current table, nullptr = no phases
    std::atomic<int> m_phase = -1;
    std::atomic<size_t> m_done = 0;
    std::atomic<size_t> m_count = 0;
    std::atomic<int> m_percentage = 0;
};

/// The phases of one run over a progress (a tool of phase 2, which runs the image
/// optimizer, the structure optimizer and the measuring): the table of the phases is
/// given to the target at start, every phase has its own sink for the code of the
/// phase (start, step, finish as usual), which begins the phase at its start. A phase,
/// which does not run, is just passed. The object lives in the thread of the run, on
/// its stack. A null target makes everything a no-op: getPhase returns nullptr, which
/// every function taking a PDFProgressSink accepts.
class PDF4QTLIBCORESHARED_EXPORT PDFProgressPhases
{
public:
    /// \param target Progress of the run (may be null), must outlive the object
    explicit PDFProgressPhases(PDFProgressSink* target);
    ~PDFProgressPhases();

    PDFProgressPhases(const PDFProgressPhases&) = delete;
    PDFProgressPhases& operator=(const PDFProgressPhases&) = delete;

    /// Adds a phase, returns its index. All phases are added before start.
    /// \param weight Part of the bar (the weights of the phases are relative)
    /// \param text Translated text of the phase ("Recompressing images")
    /// \param showCount The reader shows the count of the phase ("· 152 of 412")
    int addPhase(int weight, const QString& text, bool showCount);

    /// Gives the phases to the target (once)
    void start();

    /// Returns the progress of the phase, nullptr without a target or for an invalid index
    PDFProgressSink* getPhase(int index);

    /// Begins a phase, which has no steps of its own
    void beginPhase(int index);

    /// Fills the bar
    void finish();

private:
    class Phase;

    PDFProgressSink* m_target;
    std::vector<PDFProgressPhase> m_phases;
    std::vector<std::unique_ptr<Phase>> m_phaseSinks;
    bool m_isStarted = false;
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::ProgressStartupInfo)

#endif // PDFPROGRESS_H
