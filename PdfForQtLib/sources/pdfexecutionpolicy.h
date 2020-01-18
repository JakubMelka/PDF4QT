//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFEXECUTIONPOLICY_H
#define PDFEXECUTIONPOLICY_H

#include "pdfglobal.h"

#include <atomic>
#include <execution>

namespace pdf
{
struct PDFExecutionPolicyHolder;

/// Defines thread execution policy based on settings and actual number of page content
/// streams being processed. It can regulate number of threads executed at each
/// point, where execution policy is used.
class PDFFORQTLIBSHARED_EXPORT PDFExecutionPolicy
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
    /// \param scope Scope for which we want to determine exectution policy
    static bool isParallelizing(Scope scope);

    template<typename ForwardIt, typename UnaryFunction>
    static void execute(Scope scope, ForwardIt first, ForwardIt last, UnaryFunction f)
    {
        if (isParallelizing(scope))
        {
            std::for_each(std::execution::parallel_policy(), first, last, f);
        }
        else
        {
            std::for_each(std::execution::sequenced_policy(), first, last, f);
        }
    }

    template<typename ForwardIt, typename Comparator>
    static void sort(Scope scope, ForwardIt first, ForwardIt last, Comparator f)
    {
        if (isParallelizing(scope))
        {
            std::sort(std::execution::parallel_policy(), first, last, f);
        }
        else
        {
            std::sort(std::execution::sequenced_policy(), first, last, f);
        }
    }

    /// Starts processing content stream
    static void startProcessingContentStream();

    /// Ends processing content stream
    static void endProcessingContentStream();

private:
    friend struct PDFExecutionPolicyHolder;

    explicit PDFExecutionPolicy();

    std::atomic<size_t> m_contentStreamsCount;
    std::atomic<size_t> m_threadLimit;
    std::atomic<Strategy> m_strategy;
};

}   // namespace pdf

#endif // PDFEXECUTIONPOLICY_H
