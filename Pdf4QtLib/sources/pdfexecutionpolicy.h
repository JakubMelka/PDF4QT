//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFEXECUTIONPOLICY_H
#define PDFEXECUTIONPOLICY_H

#include "pdfglobal.h"

#include <QSemaphore>
#include <QThreadPool>

#include <atomic>
#include <execution>

namespace pdf
{
struct PDFExecutionPolicyHolder;

/// Defines thread execution policy based on settings and actual number of page content
/// streams being processed. It can regulate number of threads executed at each
/// point, where execution policy is used.
class Pdf4QtLIBSHARED_EXPORT PDFExecutionPolicy
{
public:

    enum class Scope
    {
        Page,       ///< Used, when we are processing page objects
        Content,    ///< Used, when we are processing objects from page content streams
        Unknown     ///< Unknown scope, usually tries to parallelize
    };

    enum class Strategy
    {
        SingleThreaded,
        PageMultithreaded,
        AlwaysMultithreaded
    };

    /// Sets multithreading strategy
    /// \param strategy Strategy
    static void setStrategy(Strategy strategy);

    /// Determines, if we should parallelize for scope
    /// \param scope Scope for which we want to determine execution policy
    static bool isParallelizing(Scope scope);

    template<typename ForwardIt, typename UnaryFunction>
    class Runnable : public QRunnable
    {
    public:
        explicit inline Runnable(ForwardIt it, UnaryFunction* function, QSemaphore* semaphore) :
            m_forwardIt(qMove(it)),
            m_function(function),
            m_semaphore(semaphore)
        {
            setAutoDelete(true);
        }

        virtual void run() override
        {
            QSemaphoreReleaser semaphoreReleaser(m_semaphore);
            (*m_function)(*m_forwardIt);
        }

    private:
        ForwardIt m_forwardIt;
        UnaryFunction* m_function;
        QSemaphore* m_semaphore;
    };

    template<typename ForwardIt, typename UnaryFunction>
    static void execute(Scope scope, ForwardIt first, ForwardIt last, UnaryFunction f)
    {
        if (isParallelizing(scope))
        {
            QSemaphore semaphore(0);
            int count = static_cast<int>(std::distance(first, last));

            QThreadPool* pool = getThreadPool(scope);
            for (auto it = first; it != last; ++it)
            {
                pool->start(new Runnable(it, &f, &semaphore));
            }

            semaphore.acquire(count);
        }
        else
        {
            std::for_each(std::execution::sequenced_policy(), first, last, f);
        }
    }

    template<typename ForwardIt, typename Comparator>
    static void sort(Scope scope, ForwardIt first, ForwardIt last, Comparator f)
    {
        Q_UNUSED(scope);

        // We always sort by single thread
        std::sort(std::execution::sequenced_policy(), first, last, f);
    }

    /// Returns number of active threads for given scope
    static int getActiveThreadCount(Scope scope);

    /// Returns maximal number of threads for given scope
    static int getMaxThreadCount(Scope scope);

    /// Sets maximal number of threads for given scope
    static void setMaxThreadCount(Scope scope, int count);

    /// Returns ideal thread count for given scope
    static int getIdealThreadCount(Scope scope);

    /// Returns number of currently processed content streams
    static int getContentStreamCount();

    /// Starts processing content stream
    static void startProcessingContentStream();

    /// Ends processing content stream
    static void endProcessingContentStream();

    /// Finalize multithreading - must be called at the end of program
    static void finalize();

private:
    friend struct PDFExecutionPolicyHolder;

    /// Returns thread pool based on scope
    static QThreadPool* getThreadPool(Scope scope);

    explicit PDFExecutionPolicy();

    std::atomic<int> m_contentStreamsCount;
    std::atomic<Strategy> m_strategy;
};

}   // namespace pdf

#endif // PDFEXECUTIONPOLICY_H
