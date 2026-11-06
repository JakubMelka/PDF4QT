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

#ifndef TESTPROGRESSRECORDER_H
#define TESTPROGRESSRECORDER_H

#include "pdfprogress.h"

#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

#include <functional>

namespace pdftest
{

/// The progress counter of a run, which records every call of the computation: the
/// state after it (the percentage, the text of the step as the runner builds it) under
/// its own lock, so the records of the threads of a parallel loop come one after another
/// in the order of the counter. The callbacks run under the lock, in the thread of the
/// call (a test may cancel the run there). Read the records after the run.
class TestProgressRecorder : public pdf::PDFProgressCounter
{
public:
    QList<int> percentages;     ///< The percentage after each call
    QStringList texts;          ///< The texts of the step, a change only ("Recompressing images · 2 of 4")
    int startCount = 0;         ///< Calls of start
    int stepCount = 0;          ///< Calls of step
    int finishCount = 0;        ///< Calls of finish
    QList<int> phases;          ///< The begun phases in their order

    std::function<void()> onStep;                       ///< After each step
    std::function<void(const QString&)> onText;         ///< After each call, with the text of the step

    /// Text of the step of a snapshot ("Recompressing images · 152 of 412"), the format of
    /// PDFDocumentToolRunner::getStepText in the default locale
    static QString getStepText(const pdf::PDFProgressCounter::Snapshot& snapshot)
    {
        if (!snapshot.showCount || snapshot.count == 0 || snapshot.text.isEmpty())
        {
            return snapshot.text;
        }
        const QLocale locale;
        return QStringLiteral("%1 · %2 of %3").arg(snapshot.text, locale.toString(qulonglong(snapshot.done)), locale.toString(qulonglong(snapshot.count)));
    }

    virtual void start(size_t count, pdf::ProgressStartupInfo startupInfo) override
    {
        QMutexLocker locker(&m_mutex);
        pdf::PDFProgressCounter::start(count, std::move(startupInfo));
        ++startCount;
        record();
    }

    virtual void step() override
    {
        QMutexLocker locker(&m_mutex);
        pdf::PDFProgressCounter::step();
        ++stepCount;
        record();
        if (onStep)
        {
            onStep();
        }
    }

    virtual void finish() override
    {
        QMutexLocker locker(&m_mutex);
        pdf::PDFProgressCounter::finish();
        ++finishCount;
        record();
    }

    virtual void beginPhase(int index) override
    {
        QMutexLocker locker(&m_mutex);
        pdf::PDFProgressCounter::beginPhase(index);
        phases << index;
        record();
    }

    virtual void finishPhases() override
    {
        QMutexLocker locker(&m_mutex);
        pdf::PDFProgressCounter::finishPhases();
        record();
    }

    /// The percentages never go back
    bool isMonotonic() const
    {
        for (qsizetype i = 1; i < percentages.size(); ++i)
        {
            if (percentages[i] < percentages[i - 1])
            {
                return false;
            }
        }
        return true;
    }

private:
    void record()
    {
        const pdf::PDFProgressCounter::Snapshot state = snapshot();
        const QString text = getStepText(state);
        percentages << state.percentage;
        if (texts.isEmpty() || texts.back() != text)
        {
            texts << text;
        }
        if (onText)
        {
            onText(text);
        }
    }

    QMutex m_mutex;
};

}   // namespace pdftest

#endif // TESTPROGRESSRECORDER_H
