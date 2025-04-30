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

#ifndef PDFCOMPILER_H
#define PDFCOMPILER_H

#include "pdfglobal.h"
#include "pdfwidgetsglobal.h"
#include "pdfrenderer.h"
#include "pdfpainter.h"
#include "pdftextlayout.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QWaitCondition>

template <class Key, class T>
class QCache;

namespace pdf
{
class PDFDrawWidgetProxy;
class PDFAsynchronousPageCompiler;

class PDFAsynchronousPageCompilerWorkerThread : public QThread
{
    Q_OBJECT

public:
    explicit PDFAsynchronousPageCompilerWorkerThread(PDFAsynchronousPageCompiler* parent);

signals:
    void pageCompiled();

protected:
    virtual void run() override;

private:
    PDFAsynchronousPageCompiler* m_compiler;
    QMutex* m_mutex;
    QWaitCondition* m_waitCondition;
};

/// Asynchronous page compiler compiles pages asynchronously, and stores them in the
/// cache. Cache size can be set. This object is designed to cooperate with
/// draw widget proxy.
class PDFAsynchronousPageCompiler : public QObject, public PDFOperationControl
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAsynchronousPageCompiler(PDFDrawWidgetProxy* proxy);
    virtual ~PDFAsynchronousPageCompiler();

    /// Starts the engine. Call this function only if the engine
    /// is stopped.
    void start();

    /// Stops the engine and all underlying asynchronous tasks. Also
    /// clears the cache if needed. Call this function only if engine is active.
    /// Cache is cleared only, if \p clearCache parameter is being set to true.
    /// Set it to false, if "soft" document update occurs (this means change
    /// to the document, which doesn't modify page content in precompiled
    /// pages (graphic content / number of pages change).
    /// \param clearCache Clear cache
    void stop(bool clearCache);

    /// Resets the engine - calls stop and then calls start.
    void reset();

    /// Sets cache limit in bytes
    /// \param limit Cache limit [bytes]
    void setCacheLimit(int limit);

    enum class State
    {
        Inactive,
        Active,
        Stopping
    };

    /// Returns current state of compiler
    State getState() const { return m_state; }

    /// Return proxy
    PDFDrawWidgetProxy* getProxy() const { return m_proxy; }

    /// Tries to retrieve precompiled page from the cache. If page is not found,
    /// then nullptr is returned (no exception is thrown). If \p compile is set to true,
    /// and page is not found, and compiler is active, then new asynchronous compile
    /// task is performed.
    /// \param pageIndex Index of page
    /// \param compile Compile the page, if it is not found in the cache
    const PDFPrecompiledPage* getCompiledPage(PDFInteger pageIndex, bool compile);

    /// Performs smart cache clear. Too old pages are removed from the cache,
    /// but only if these pages are not in active pages. Use this function to
    /// clear cache to avoid huge memory consumption.
    /// \param milisecondsLimit Pages with access time above this limit will be erased
    /// \param activePages Sorted vector of active pages, which should remain in cache
    void smartClearCache(const int milisecondsLimit, const std::vector<PDFInteger>& activePages);

    /// Is operation being cancelled?
    virtual bool isOperationCancelled() const override;

signals:
    void pageImageChanged(bool all, const std::vector<pdf::PDFInteger>& pages);
    void renderingError(pdf::PDFInteger pageIndex, const QList<pdf::PDFRenderError>& errors);

private:
    friend class PDFAsynchronousPageCompilerWorkerThread;

    void onPageCompiled();

    struct CompileTask
    {
        CompileTask() = default;
        CompileTask(PDFInteger pageIndex) : pageIndex(pageIndex) { }

        PDFInteger pageIndex = 0;
        bool finished = false;
        PDFPrecompiledPage precompiledPage;
    };

    State m_state = State::Inactive;
    QMutex m_mutex;
    QWaitCondition m_waitCondition;
    PDFAsynchronousPageCompilerWorkerThread* m_thread = nullptr;

    PDFDrawWidgetProxy* m_proxy;
    QCache<PDFInteger, PDFPrecompiledPage>* m_cache;

    /// This task is protected by mutex. Every access to this
    /// variable must be done with locked mutex.
    std::map<PDFInteger, CompileTask> m_tasks;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFAsynchronousTextLayoutCompiler : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAsynchronousTextLayoutCompiler(PDFDrawWidgetProxy* proxy);

    /// Starts the engine. Call this function only if the engine
    /// is stopped.
    void start();

    /// Stops the engine and all underlying asynchronous tasks. Also
    /// clears the cache if parameter \p clearCache. Call this function
    /// only if engine is active. Clear cache should be set to false,
    /// only if "soft" document update appears (no text on page is being
    /// changed).
    /// \param clearCache Clear cache
    void stop(bool clearCache);

    /// Resets the engine - calls stop and then calls start.
    void reset();

    enum class State
    {
        Inactive,
        Active,
        Stopping
    };

    /// Creates text layout of the page synchronously. If page index is invalid,
    /// then empty text layout is returned. Compiler must be active to get
    /// valid text layout.
    /// \param pageIndex Page index
    PDFTextLayout createTextLayout(PDFInteger pageIndex);

    /// Returns text layout of the page. If page index is invalid,
    /// then empty text layout is returned.
    /// \param pageIndex Page index
    PDFTextLayout getTextLayout(PDFInteger pageIndex);

    /// Returns getter for text layout of the page. If page index is invalid,
    /// then empty text layout getter is returned.
    /// \param pageIndex Page index
    PDFTextLayoutGetter getTextLayoutLazy(PDFInteger pageIndex);

    /// Select all texts on all pages using \p color color.
    /// \param color Color to be used for text selection
    PDFTextSelection getTextSelectionAll(QColor color) const;

    /// Create text layout for the document. Function is asynchronous,
    /// it returns immediately. After text layout is created, signal
    /// \p textLayoutChanged is emitted.
    void makeTextLayout();

    /// Returns true, if text layout is ready
    bool isTextLayoutReady() const { return m_textLayouts.has_value(); }

    /// Returns text layout storage (if it is ready), or nullptr
    const PDFTextLayoutStorage* getTextLayoutStorage() const { return isTextLayoutReady() ? &m_textLayouts.value() : nullptr; }

signals:
    void textLayoutChanged();

private:
    void onTextLayoutCreated();

    PDFDrawWidgetProxy* m_proxy;
    State m_state = State::Inactive;
    bool m_isRunning;
    std::optional<PDFTextLayoutStorage> m_textLayouts;
    QFuture<PDFTextLayoutStorage> m_textLayoutCompileFuture;
    QFutureWatcher<PDFTextLayoutStorage> m_textLayoutCompileFutureWatcher;
    PDFTextLayoutCache m_cache;
};

}   // namespace pdf

#endif // PDFCOMPILER_H
