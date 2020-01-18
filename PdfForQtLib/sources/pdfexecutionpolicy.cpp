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


#include "pdfexecutionpolicy.h"

#include <QThread>

namespace pdf
{

struct PDFExecutionPolicyHolder
{
    PDFExecutionPolicy policy;
} s_execution_policy;

void PDFExecutionPolicy::setStrategy(Strategy strategy)
{
    s_execution_policy.policy.m_strategy.store(strategy, std::memory_order_relaxed);
}

bool PDFExecutionPolicy::isParallelizing(Scope scope)
{
    const Strategy strategy = s_execution_policy.policy.m_strategy.load(std::memory_order_relaxed);
    switch (strategy)
    {
        case Strategy::SingleThreaded:
            return false;

        case Strategy::PageMultithreaded:
        {
            switch (scope)
            {
                case Scope::Page:
                case Scope::Unknown:
                    return true; // We are parallelizing pages...

                case Scope::Content:
                {
                    // Jakub Melka: this is a bit complicated. We must count number of content streams
                    // being processed and if it is large enough, then do not parallelize.
                    const size_t threadLimit = s_execution_policy.policy.m_threadLimit.load(std::memory_order_relaxed);
                    const size_t contentStreamsCount = s_execution_policy.policy.m_contentStreamsCount.load(std::memory_order_seq_cst);
                    return contentStreamsCount < threadLimit;
                }
            }

            break;
        }

        case Strategy::AlwaysMultithreaded:
            return true;
    }

    // It should never go here...
    Q_ASSERT(false);
    return false;
}

void PDFExecutionPolicy::startProcessingContentStream()
{
    ++s_execution_policy.policy.m_contentStreamsCount;
}

void PDFExecutionPolicy::endProcessingContentStream()
{
    --s_execution_policy.policy.m_contentStreamsCount;
}

PDFExecutionPolicy::PDFExecutionPolicy() :
    m_contentStreamsCount(0),
    m_threadLimit(QThread::idealThreadCount()),
    m_strategy(Strategy::PageMultithreaded)
{

}

}   // namespace pdf
