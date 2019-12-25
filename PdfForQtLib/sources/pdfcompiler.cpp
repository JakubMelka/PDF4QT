//    Copyright (C) 2019 Jakub Melka
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

#include "pdfcompiler.h"
#include "pdfcms.h"
#include "pdfdrawspacecontroller.h"

#include <QtConcurrent/QtConcurrent>

namespace pdf
{

PDFAsynchronousPageCompiler::PDFAsynchronousPageCompiler(PDFDrawWidgetProxy* proxy) :
    BaseClass(proxy),
    m_proxy(proxy)
{
    // TODO: Create setting for this
    m_cache.setMaxCost(128 * 1024 * 1024);
}

void PDFAsynchronousPageCompiler::start()
{
    switch (m_state)
    {
        case State::Inactive:
        {
            m_state = State::Active;
            break;
        }

        case State::Active:
            break; // We have nothing to do...

        case State::Stopping:
        {
            // We shouldn't call this function while stopping!
            Q_ASSERT(false);
            break;
        }
    }
}

void PDFAsynchronousPageCompiler::stop()
{
    switch (m_state)
    {
        case State::Inactive:
            break; // We have nothing to do...

        case State::Active:
        {
            // Stop the engine
            m_state = State::Stopping;

            for (const auto& taskItem : m_tasks)
            {
                disconnect(taskItem.second.taskWatcher, &QFutureWatcher<PDFPrecompiledPage>::finished, this, &PDFAsynchronousPageCompiler::onPageCompiled);
                taskItem.second.taskWatcher->waitForFinished();
            }
            m_tasks.clear();
            m_cache.clear();

            m_state = State::Inactive;
            break;
        }

        case State::Stopping:
        {
            // We shouldn't call this function while stopping!
            Q_ASSERT(false);
            break;
        }
    }
}

void PDFAsynchronousPageCompiler::reset()
{
    stop();
    start();
}

void PDFAsynchronousPageCompiler::setCacheLimit(int limit)
{
    m_cache.setMaxCost(limit);
}

const PDFPrecompiledPage* PDFAsynchronousPageCompiler::getCompiledPage(PDFInteger pageIndex, bool compile)
{
    if (m_state != State::Active || !m_proxy->getDocument())
    {
        // Engine is not active, always return nullptr
        return nullptr;
    }

    const PDFPrecompiledPage* page = m_cache.object(pageIndex);
    if (!page && compile && !m_tasks.count(pageIndex))
    {
        // Compile the page
        auto compilePage = [this, pageIndex]() -> PDFPrecompiledPage
        {
            PDFPrecompiledPage compiledPage;
            PDFCMSPointer cms = m_proxy->getCMSManager()->getCurrentCMS();
            PDFRenderer renderer(m_proxy->getDocument(), m_proxy->getFontCache(), cms.data(), m_proxy->getOptionalContentActivity(), m_proxy->getFeatures(), m_proxy->getMeshQualitySettings());
            renderer.compile(&compiledPage, pageIndex);
            return compiledPage;
        };

        m_proxy->getFontCache()->setCacheShrinkEnabled(false);
        CompileTask& task = m_tasks[pageIndex];
        task.taskFuture = QtConcurrent::run(compilePage);
        task.taskWatcher = new QFutureWatcher<PDFPrecompiledPage>(this);
        connect(task.taskWatcher, &QFutureWatcher<PDFPrecompiledPage>::finished, this, &PDFAsynchronousPageCompiler::onPageCompiled);
        task.taskWatcher->setFuture(task.taskFuture);
    }

    return page;
}

void PDFAsynchronousPageCompiler::onPageCompiled()
{
    std::vector<PDFInteger> compiledPages;

    // Search all tasks for finished tasks
    for (auto it = m_tasks.begin(); it != m_tasks.end();)
    {
        CompileTask& task = it->second;
        if (task.taskWatcher->isFinished())
        {
            if (m_state == State::Active)
            {
                // If we are in active state, try to store precompiled page
                PDFPrecompiledPage* page = new PDFPrecompiledPage(task.taskWatcher->result());
                qint64 memoryConsumptionEstimate = page->getMemoryConsumptionEstimate();
                if (m_cache.insert(it->first, page, memoryConsumptionEstimate))
                {
                    compiledPages.push_back(it->first);
                }
                else
                {
                    // We can't insert page to the cache, because cache size is too small. We will
                    // emit error string to inform the user, that cache is too small.
                    QString message = PDFTranslationContext::tr("Precompiled page size is too high (%1 kB). Cache size is %2 kB. Increase the cache size!").arg(memoryConsumptionEstimate / 1024).arg(m_cache.maxCost() / 1024);
                    emit renderingError(it->first, { PDFRenderError(RenderErrorType::Error, message) });
                }
            }

            task.taskWatcher->deleteLater();
            it = m_tasks.erase(it);
        }
        else
        {
            // Just increment the counter
            ++it;
        }
    }

    // We allow font cache shrinking, when we aren't doing something in parallel.
    m_proxy->getFontCache()->setCacheShrinkEnabled(m_tasks.empty());

    if (!compiledPages.empty())
    {
        Q_ASSERT(std::is_sorted(compiledPages.cbegin(), compiledPages.cend()));
        emit pageImageChanged(false, compiledPages);
    }
}

}   // namespace pdf
