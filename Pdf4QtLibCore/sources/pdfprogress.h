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

class PDF4QTLIBCORESHARED_EXPORT PDFProgress : public QObject
{
    Q_OBJECT

public:
    explicit PDFProgress(QObject* parent);

    void start(size_t stepCount, ProgressStartupInfo startupInfo);
    void step();
    void finish();

    /// Sets the text of the current step, which a listener shows next to the bar
    /// ("Recompressing images · 152 of 412", Figma 295:1711)
    void setStepText(const QString& text);

    /// Number of the finished steps
    size_t getCurrentStep() const { return m_currentStep.load(); }

    /// Number of the steps given to start
    size_t getStepCount() const { return m_stepCount.load(); }

signals:
    // Namespace pdf:: must be here, due to queued signals calling
    void progressStarted(pdf::ProgressStartupInfo info);
    void progressStep(int percentage);
    void progressFinished();
    void progressStepText(QString text);

private:
    std::atomic<size_t> m_currentStep = 0;
    std::atomic<size_t> m_stepCount = 0;
    std::atomic<int> m_percentage = 0;
};

/// One bar over the phases of one run (a tool of phase 2, which runs the image
/// optimizer, the structure optimizer and the measuring): every phase has its
/// weight, its text and its own PDFProgress, which the code of the phase fills as
/// usual (start, step, finish), and the target progress gets one growing
/// percentage from 0 to 100 with the text of the current step. A phase begins, when
/// its progress is started (or by beginPhase); a phase, which does not run, is
/// just passed. The object lives in the thread of the run. A null target makes
/// everything a no-op: getPhase returns nullptr, which every function taking a
/// PDFProgress accepts.
class PDF4QTLIBCORESHARED_EXPORT PDFProgressPhases : public QObject
{
public:
    /// \param target Progress of the run (may be null)
    /// \param title Text of the start of the target
    explicit PDFProgressPhases(PDFProgress* target, const QString& title);
    virtual ~PDFProgressPhases() override;

    /// Adds a phase, returns its index. All phases are added before start.
    /// \param weight Part of the bar (the weights of the phases are relative)
    /// \param text Text of the phase ("Recompressing images")
    /// \param showCount The text of the step has the count of the phase ("· 152 of 412")
    int addPhase(int weight, const QString& text, bool showCount);

    /// Starts the target
    void start();

    /// Returns the progress of the phase, nullptr without a target or for an invalid index
    PDFProgress* getPhase(int index);

    /// Begins a phase, which has no steps of its own (the phases run in the order
    /// they were added, the parts of the phases before it are filled)
    void beginPhase(int index);

    /// Fills the bar and finishes the target
    void finish();

private:
    class Phase;

    void updatePhase(const Phase* phase, int phasePercentage);

    PDFProgress* m_target;
    QString m_title;
    std::vector<std::unique_ptr<Phase>> m_phases;
    int m_totalWeight = 0;
    int m_percentage = 0;
    bool m_isStarted = false;
    bool m_isFinished = false;
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::ProgressStartupInfo)

#endif // PDFPROGRESS_H
