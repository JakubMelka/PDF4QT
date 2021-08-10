//    Copyright (C) 2019-2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFCOMPILER_H
#define PDFCOMPILER_H

#include "pdfrenderer.h"
#include "pdfpainter.h"
#include "pdftextlayout.h"

#include <QCache>
#include <QFuture>
#include <QFutureWatcher>

namespace pdf
{
class PDFDrawWidgetProxy;

/// Asynchronous page compiler compiles pages asynchronously, and stores them in the
/// cache. Cache size can be set. This object is designed to cooperate with
/// draw widget proxy.
class PDFAsynchronousPageCompiler : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAsynchronousPageCompiler(PDFDrawWidgetProxy* proxy);

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

    /// Tries to retrieve precompiled page from the cache. If page is not found,
    /// then nullptr is returned (no exception is thrown). If \p compile is set to true,
    /// and page is not found, and compiler is active, then new asynchronous compile
    /// task is performed.
    /// \param pageIndex Index of page
    /// \param compile Compile the page, if it is not found in the cache
    const PDFPrecompiledPage* getCompiledPage(PDFInteger pageIndex, bool compile);

signals:
    void pageImageChanged(bool all, const std::vector<PDFInteger>& pages);
    void renderingError(PDFInteger pageIndex, const QList<PDFRenderError>& errors);

private:
    void onPageCompiled();

    struct CompileTask
    {
        QFuture<PDFPrecompiledPage> taskFuture;
        QFutureWatcher<PDFPrecompiledPage>* taskWatcher = nullptr;
    };

    PDFDrawWidgetProxy* m_proxy;
    State m_state = State::Inactive;
    QCache<PDFInteger, PDFPrecompiledPage> m_cache;
    std::map<PDFInteger, CompileTask> m_tasks;
};

class PDF4QTLIBSHARED_EXPORT PDFAsynchronousTextLayoutCompiler : public QObject
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

class PDFTextLayoutGenerator : public PDFPageContentProcessor
{
    using BaseClass = PDFPageContentProcessor;

public:
    explicit PDFTextLayoutGenerator(PDFRenderer::Features features,
                                    const PDFPage* page,
                                    const PDFDocument* document,
                                    const PDFFontCache* fontCache,
                                    const PDFCMS* cms,
                                    const PDFOptionalContentActivity* optionalContentActivity,
                                    QMatrix pagePointToDevicePointMatrix,
                                    const PDFMeshQualitySettings& meshQualitySettings) :
        BaseClass(page, document, fontCache, cms, optionalContentActivity, pagePointToDevicePointMatrix, meshQualitySettings),
        m_features(features)
    {

    }

    /// Creates text layout from the text
    PDFTextLayout createTextLayout();

protected:
    virtual bool isContentSuppressedByOC(PDFObjectReference ocgOrOcmd) override;
    virtual bool isContentKindSuppressed(ContentKind kind) const override;
    virtual void performOutputCharacter(const PDFTextCharacterInfo& info) override;

private:
    PDFRenderer::Features m_features;
    PDFTextLayout m_textLayout;
};

}   // namespace pdf

#endif // PDFCOMPILER_H
