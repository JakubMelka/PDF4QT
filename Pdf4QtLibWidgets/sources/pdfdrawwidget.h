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

#ifndef PDFDRAWWIDGET_H
#define PDFDRAWWIDGET_H

#include "pdfsignaturehandler.h"
#include "pdfwidgetsglobal.h"
#include "pdfglobal.h"
#include "pdfrenderer.h"

#include <QWidget>
#include <QScrollBar>
#include <QTimer>
#include <QElapsedTimer>

namespace pdf
{
class PDFDocument;
class PDFCMSManager;
class PDFToolManager;
class PDFDrawWidget;
class PDFWidgetFormManager;
class PDFDrawWidgetProxy;
class PDFModifiedDocument;
class PDFWidgetAnnotationManager;
class IDrawWidgetInputInterface;
class PDFPageContentScene;

class IDrawWidget
{
public:
    virtual ~IDrawWidget() = default;

    virtual QWidget* getWidget() = 0;

    /// Returns page indices, which are currently displayed in the widget
    virtual std::vector<PDFInteger> getCurrentPages() const = 0;

    /// Runs event on this widget
    virtual bool doEvent(QEvent* event) = 0;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFWidget : public QWidget
{
    Q_OBJECT

public:
    /// Constructs new PDFWidget.
    /// \param cmsManager Color management system manager
    /// \param engine Rendering engine type
    explicit PDFWidget(const PDFCMSManager* cmsManager, RendererEngine engine, QWidget* parent);
    virtual ~PDFWidget() override;

    virtual bool focusNextPrevChild(bool next) override;

    using PageRenderingErrors = std::map<PDFInteger, QList<PDFRenderError>>;

    /// Sets the document to be viewed in this widget. Document can be nullptr,
    /// in that case, widget contents are cleared. Optional content activity can be nullptr,
    /// if this occurs, no content is suppressed.
    /// \param document Document
    void setDocument(const PDFModifiedDocument& document, std::vector<PDFSignatureVerificationResult> signatureVerificationResult);

    /// Update rendering engine according the settings
    /// \param engine Engine type
    void updateRenderer(RendererEngine engine);

    /// Updates cache limits
    /// \param compiledPageCacheLimit Compiled page cache limit [bytes]
    /// \param thumbnailsCacheLimit Thumbnail image cache limit [kB]
    /// \param fontCacheLimit Font cache limit [-]
    /// \param instancedFontCacheLimit Instanced font cache limit [-]
    void updateCacheLimits(int compiledPageCacheLimit, int thumbnailsCacheLimit, int fontCacheLimit, int instancedFontCacheLimit);

    const PDFCMSManager* getCMSManager() const { return m_cmsManager; }
    PDFToolManager* getToolManager() const { return m_toolManager; }
    PDFWidgetAnnotationManager* getAnnotationManager() const { return m_annotationManager; }
    IDrawWidget* getDrawWidget() const { return m_drawWidget; }
    QScrollBar* getHorizontalScrollbar() const { return m_horizontalScrollBar; }
    QScrollBar* getVerticalScrollbar() const { return m_verticalScrollBar; }
    PDFDrawWidgetProxy* getDrawWidgetProxy() const { return m_proxy; }
    const PageRenderingErrors* getPageRenderingErrors() const { return &m_pageRenderingErrors; }
    int getPageRenderingErrorCount() const;
    const std::vector<IDrawWidgetInputInterface*>& getInputInterfaces() const { return m_inputInterfaces; }

    void setToolManager(PDFToolManager* toolManager);
    void setAnnotationManager(PDFWidgetAnnotationManager* annotationManager);

    PDFWidgetFormManager* getFormManager() const;
    void setFormManager(PDFWidgetFormManager* formManager);

    void removeInputInterface(IDrawWidgetInputInterface* inputInterface);
    void addInputInterface(IDrawWidgetInputInterface* inputInterface);

    /// Returns true, if any scene is active
    bool isAnySceneActive(PDFPageContentScene* sceneToSkip) const;

signals:
    void sceneActivityChanged();
    void pageRenderingErrorsChanged(pdf::PDFInteger pageIndex, int errorsCount);

private:
    void onRenderingError(PDFInteger pageIndex, const QList<PDFRenderError>& errors);
    void onPageImageChanged(bool all, const std::vector<PDFInteger>& pages);
    void onSceneActiveStateChanged(bool);

    const PDFCMSManager* m_cmsManager;
    PDFToolManager* m_toolManager;
    PDFWidgetAnnotationManager* m_annotationManager;
    PDFWidgetFormManager* m_formManager;
    IDrawWidget* m_drawWidget;
    QScrollBar* m_horizontalScrollBar;
    QScrollBar* m_verticalScrollBar;
    PDFDrawWidgetProxy* m_proxy;
    PageRenderingErrors m_pageRenderingErrors;
    std::vector<IDrawWidgetInputInterface*> m_inputInterfaces;
    std::vector<PDFPageContentScene*> m_scenes;
    RendererEngine m_rendererEngine;
};

class PDFDrawWidget : public QWidget, public IDrawWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFDrawWidget(PDFWidget* widget, QWidget* parent);
    virtual ~PDFDrawWidget() override = default;

    /// Returns page indices, which are currently displayed in the widget
    virtual std::vector<PDFInteger> getCurrentPages() const override;

    virtual QSize minimumSizeHint() const override;
    virtual QWidget* getWidget() override { return this; }
    virtual bool doEvent(QEvent* event) override { return this->event(event); }

protected:
    virtual bool event(QEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void keyReleaseEvent(QKeyEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QMouseEvent *event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void wheelEvent(QWheelEvent* event) override;
    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dragMoveEvent(QDragMoveEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;

    PDFWidget* getPDFWidget() const { return m_widget; }

private:
    void updateCursor();
    void onAutoScrollTimeout();

    template<typename Event, void(IDrawWidgetInputInterface::* Function)(QWidget*, Event*)>
    bool processEvent(Event* event);

    enum class MouseOperation
    {
        None,
        Translate,
        AutoScroll
    };

    /// Performs the mouse operation (under the current mouse position)
    /// \param currentMousePosition Current position of the mouse
    void performMouseOperation(QPoint currentMousePosition);

    PDFWidget* m_widget;
    QPoint m_lastMousePosition;
    QPoint m_autoScrollMousePosition;
    MouseOperation m_mouseOperation;
    QTimer m_autoScrollTimer;
    QPointF m_autoScrollOffset;
    QElapsedTimer m_autoScrollLastElapsedTimer;
    QImage m_blend2DframeBuffer;
};

}   // namespace pdf

#endif // PDFDRAWWIDGET_H
