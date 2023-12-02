//    Copyright (C) 2018-2021 Jakub Melka
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

#ifndef PDFDRAWWIDGET_H
#define PDFDRAWWIDGET_H

#include "pdfglobal.h"
#include "pdfrenderer.h"

#include <QWidget>
#include <QScrollBar>
#include <QOpenGLWidget>

namespace pdf
{
class PDFDocument;
class PDFCMSManager;
class PDFToolManager;
class PDFDrawWidget;
class PDFFormManager;
class PDFDrawWidgetProxy;
class PDFModifiedDocument;
class PDFWidgetAnnotationManager;
class IDrawWidgetInputInterface;

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

class PDF4QTLIBSHARED_EXPORT PDFWidget : public QWidget
{
    Q_OBJECT

public:
    /// Constructs new PDFWidget.
    /// \param cmsManager Color management system manager
    /// \param engine Rendering engine type
    /// \param samplesCount Samples count for rendering engine MSAA antialiasing
    explicit PDFWidget(const PDFCMSManager* cmsManager, RendererEngine engine, int samplesCount, QWidget* parent);
    virtual ~PDFWidget() override;

    virtual bool focusNextPrevChild(bool next) override;

    using PageRenderingErrors = std::map<PDFInteger, QList<PDFRenderError>>;

    /// Sets the document to be viewed in this widget. Document can be nullptr,
    /// in that case, widget contents are cleared. Optional content activity can be nullptr,
    /// if this occurs, no content is suppressed.
    /// \param document Document
    void setDocument(const PDFModifiedDocument& document);

    /// Update rendering engine according the settings
    /// \param engine Engine type
    /// \param samplesCount Samples count for rendering engine MSAA antialiasing
    void updateRenderer(RendererEngine engine, int samplesCount);

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

    PDFFormManager* getFormManager() const;
    void setFormManager(PDFFormManager* formManager);

    void removeInputInterface(IDrawWidgetInputInterface* inputInterface);
    void addInputInterface(IDrawWidgetInputInterface* inputInterface);

signals:
    void pageRenderingErrorsChanged(pdf::PDFInteger pageIndex, int errorsCount);

private:
    RendererEngine getEffectiveRenderer(RendererEngine rendererEngine);

    void updateRendererImpl();
    void onRenderingError(PDFInteger pageIndex, const QList<PDFRenderError>& errors);
    void onPageImageChanged(bool all, const std::vector<PDFInteger>& pages);

    IDrawWidget* createDrawWidget(RendererEngine rendererEngine, int samplesCount);

    const PDFCMSManager* m_cmsManager;
    PDFToolManager* m_toolManager;
    PDFWidgetAnnotationManager* m_annotationManager;
    PDFFormManager* m_formManager;
    IDrawWidget* m_drawWidget;
    QScrollBar* m_horizontalScrollBar;
    QScrollBar* m_verticalScrollBar;
    PDFDrawWidgetProxy* m_proxy;
    PageRenderingErrors m_pageRenderingErrors;
    std::vector<IDrawWidgetInputInterface*> m_inputInterfaces;
};

template<typename BaseWidget>
class PDFDrawWidgetBase : public BaseWidget, public IDrawWidget
{
public:
    explicit PDFDrawWidgetBase(PDFWidget* widget, QWidget* parent);
    virtual ~PDFDrawWidgetBase() override = default;

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

    PDFWidget* getPDFWidget() const { return m_widget; }

private:
    void updateCursor();

    template<typename Event, void(IDrawWidgetInputInterface::* Function)(QWidget*, Event*)>
    bool processEvent(Event* event);

    enum class MouseOperation
    {
        None,
        Translate
    };

    /// Performs the mouse operation (under the current mouse position)
    /// \param currentMousePosition Current position of the mouse
    void performMouseOperation(QPoint currentMousePosition);

    PDFWidget* m_widget;
    QPoint m_lastMousePosition;
    MouseOperation m_mouseOperation;
};

class PDFOpenGLDrawWidget : public PDFDrawWidgetBase<QOpenGLWidget>
{
    Q_OBJECT

private:
    using BaseClass = PDFDrawWidgetBase<QOpenGLWidget>;

public:
    explicit PDFOpenGLDrawWidget(PDFWidget* widget, int samplesCount, QWidget* parent);
    virtual ~PDFOpenGLDrawWidget() override;

protected:
    virtual void resizeGL(int w, int h) override;
    virtual void initializeGL() override;
    virtual void paintGL() override;
};

class PDFDrawWidget : public PDFDrawWidgetBase<QWidget>
{
    Q_OBJECT

private:
    using BaseClass = PDFDrawWidgetBase<QWidget>;

public:
    explicit PDFDrawWidget(PDFWidget* widget, QWidget* parent);
    virtual ~PDFDrawWidget() override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
};

extern template class PDFDrawWidgetBase<QOpenGLWidget>;
extern template class PDFDrawWidgetBase<QWidget>;

}   // namespace pdf

#endif // PDFDRAWWIDGET_H
