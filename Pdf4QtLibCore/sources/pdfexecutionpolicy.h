// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef PDFEXECUTIONPOLICY_H
#define PDFEXECUTIONPOLICY_H

#include "pdfglobal.h"

#include <QMutex>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThreadPool>

#include <atomic>
#include <execution>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace pdf
{
struct PDFExecutionPolicyHolder;

/// Defines thread execution policy based on settings and actual number of page content
/// streams being processed. It can regulate number of threads executed at each
/// point, where execution policy is used.
class PDF4QTLIBCORESHARED_EXPORT PDFExecutionPolicy
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
        explicit inline Runnable(ForwardIt it, ForwardIt itEnd, UnaryFunction* function, QSemaphore* semaphore) :
            m_forwardIt(qMove(it)),
            m_forwardItEnd(qMove(itEnd)),
            m_function(function),
            m_semaphore(semaphore),
            m_packSize(static_cast<int>(std::distance(m_forwardIt, m_forwardItEnd)))
        {
            setAutoDelete(true);
        }

        virtual void run() override
        {
            QSemaphoreReleaser semaphoreReleaser(m_semaphore, m_packSize);
            for (auto it = m_forwardIt; it != m_forwardItEnd; ++it)
            {
                (*m_function)(*it);
            }
        }

    private:
        ForwardIt m_forwardIt;
        ForwardIt m_forwardItEnd;
        UnaryFunction* m_function;
        QSemaphore* m_semaphore;
        int m_packSize;
    };

    template<typename ForwardIt, typename UnaryFunction>
    static void execute(Scope scope, ForwardIt first, ForwardIt last, UnaryFunction f)
    {
        if (isParallelizing(scope))
        {
            QSemaphore semaphore(0);
            const auto distance = std::distance(first, last);
            if (distance < 0 || distance > std::numeric_limits<int>::max())
            {
                throw std::length_error("Too many tasks for the execution policy.");
            }
            const int count = static_cast<int>(distance);
            int remainder = count;

            // Exceptions must reach the calling thread only after every worker has
            // stopped using the callable and the semaphore on this stack.
            QMutex exceptionMutex;
            std::exception_ptr exception;
            auto guardedFunction = [&](auto&& value)
            {
                try
                {
                    f(std::forward<decltype(value)>(value));
                }
                catch (...)
                {
                    QMutexLocker lock(&exceptionMutex);
                    if (!exception)
                    {
                        exception = std::current_exception();
                    }
                }
            };
            int scheduledCount = 0;
            auto waitGuard = qScopeGuard([&]() { semaphore.acquire(scheduledCount); });

            int bucketSize = 1;

            // For page scope, we do not divide the tasks into buckets, i.e.
            // each bucket will have size 1. But if we are in a content scope,
            // then we are processing smaller task, so we divide the work
            // into buckets of appropriate size.
            if (scope != Scope::Page)
            {
                const int buckets = 8 * qBound(1, QThread::idealThreadCount(), std::numeric_limits<int>::max() / 8);
                bucketSize = qMax(1, count / buckets);
            }

            QThreadPool* pool = getThreadPool(scope);

            // Divide tasks into buckets with given bucket size
            auto it = first;
            while (remainder > 0)
            {
                const int currentSize = qMin(remainder, bucketSize);

                auto itStart = it;
                auto itEnd = std::next(it, currentSize);
                pool->start(new Runnable(itStart, itEnd, &guardedFunction, &semaphore));
                scheduledCount += currentSize;

                remainder -= currentSize;
                std::advance(it, currentSize);
            }

            Q_ASSERT(it == last);

            semaphore.acquire(scheduledCount);
            waitGuard.dismiss();
            if (exception)
            {
                std::rethrow_exception(exception);
            }
        }
        else
        {
            std::for_each(first, last, f);
        }
    }

    template<typename ForwardIt, typename Comparator>
    static void sort(Scope scope, ForwardIt first, ForwardIt last, Comparator f)
    {
        Q_UNUSED(scope);

        // We always sort by single thread
        std::sort(std::execution::seq, first, last, f);
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
