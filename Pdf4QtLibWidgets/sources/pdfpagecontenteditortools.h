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

#ifndef PDFPAGECONTENTEDITORTOOLS_H
#define PDFPAGECONTENTEDITORTOOLS_H

#include "pdfwidgetsglobal.h"
#include "pdfwidgettool.h"

namespace pdf
{

class PDFPageContentScene;
class PDFPageContentElement;
class PDFPageContentImageElement;
class PDFPageContentElementDot;
class PDFPageContentElementLine;
class PDFPageContentElementTextBox;
class PDFPageContentElementRectangle;
class PDFPageContentElementFreehandCurve;
class PDFTextEditPseudowidget;

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreatePCElementTool : public PDFWidgetTool
{
    Q_OBJECT
public:
    PDFCreatePCElementTool(PDFDrawWidgetProxy* proxy,
                           PDFPageContentScene* scene,
                           QAction* action,
                           QObject* parent);

    virtual const PDFPageContentElement* getElement() const = 0;
    virtual PDFPageContentElement* getElement() = 0;

    virtual void setPen(const QPen& pen);
    virtual void setBrush(const QBrush& brush);
    virtual void setFont(const QFont& font);
    virtual void setAlignment(Qt::Alignment alignment);
    virtual void setTextAngle(pdf::PDFReal angle);

protected:
    static QRectF getRectangleFromPickTool(PDFPickTool* pickTool, const QTransform& pagePointToDevicePointMatrix);

    PDFPageContentScene* m_scene;
};

/// Tool that creates rectangle element.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreatePCElementRectangleTool : public PDFCreatePCElementTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreatePCElementTool;

public:
    explicit PDFCreatePCElementRectangleTool(PDFDrawWidgetProxy* proxy,
                                             PDFPageContentScene* scene,
                                             QAction* action,
                                             bool isRounded,
                                             QObject* parent);
    virtual ~PDFCreatePCElementRectangleTool() override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual const PDFPageContentElement* getElement() const override;
    virtual PDFPageContentElement* getElement() override;

private:
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);

    PDFPickTool* m_pickTool;
    PDFPageContentElementRectangle* m_element;
};

/// Tool that displays SVG image (or raster image)
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreatePCElementImageTool : public PDFCreatePCElementTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreatePCElementTool;

public:
    explicit PDFCreatePCElementImageTool(PDFDrawWidgetProxy* proxy,
                                         PDFPageContentScene* scene,
                                         QAction* action,
                                         QByteArray content,
                                         bool askSelectImage,
                                         QObject* parent);
    virtual ~PDFCreatePCElementImageTool() override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual const PDFPageContentElement* getElement() const override;
    virtual PDFPageContentElement* getElement() override;

protected:
    virtual void setActiveImpl(bool active) override;

private:
    void selectImage();
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);

    PDFPickTool* m_pickTool;
    PDFPageContentImageElement* m_element;
    bool m_askSelectImage;
    QString m_imageDirectory;
};

/// Tool that creates line element.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreatePCElementLineTool : public PDFCreatePCElementTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreatePCElementTool;

public:
    explicit PDFCreatePCElementLineTool(PDFDrawWidgetProxy* proxy,
                                        PDFPageContentScene* scene,
                                        QAction* action,
                                        bool isHorizontal,
                                        bool isVertical,
                                        QObject* parent);
    virtual ~PDFCreatePCElementLineTool() override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual const PDFPageContentElement* getElement() const override;
    virtual PDFPageContentElement* getElement() override;

private:
    void clear();
    void onPointPicked(pdf::PDFInteger pageIndex, QPointF pagePoint);

    PDFPickTool* m_pickTool;
    PDFPageContentElementLine* m_element;
    std::optional<QPointF> m_startPoint;
};

/// Tool that creates dot element.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreatePCElementDotTool : public PDFCreatePCElementTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreatePCElementTool;

public:
    explicit PDFCreatePCElementDotTool(PDFDrawWidgetProxy* proxy,
                                       PDFPageContentScene* scene,
                                       QAction* action,
                                       QObject* parent);
    virtual ~PDFCreatePCElementDotTool() override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual const PDFPageContentElement* getElement() const override;
    virtual PDFPageContentElement* getElement() override;

private:
    void onPointPicked(pdf::PDFInteger pageIndex, QPointF pagePoint);

    PDFPickTool* m_pickTool;
    PDFPageContentElementDot* m_element;
};

/// Tool that creates freehand curve element.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreatePCElementFreehandCurveTool : public PDFCreatePCElementTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreatePCElementTool;

public:
    explicit PDFCreatePCElementFreehandCurveTool(PDFDrawWidgetProxy* proxy,
                                         PDFPageContentScene* scene,
                                         QAction* action,
                                         QObject* parent);
    virtual ~PDFCreatePCElementFreehandCurveTool() override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual const PDFPageContentElement* getElement() const override;
    virtual PDFPageContentElement* getElement() override;

    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

protected:
    virtual void setActiveImpl(bool active) override;

private:
    void resetTool();

    PDFPageContentElementFreehandCurve* m_element;
};

/// Tool that displays SVG image
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreatePCElementTextTool : public PDFCreatePCElementTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreatePCElementTool;

public:
    explicit PDFCreatePCElementTextTool(PDFDrawWidgetProxy* proxy,
                                        PDFPageContentScene* scene,
                                        QAction* action,
                                        QObject* parent);
    virtual ~PDFCreatePCElementTextTool() override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual const PDFPageContentElement* getElement() const override;
    virtual PDFPageContentElement* getElement() override;

    virtual void setPen(const QPen& pen) override;
    virtual void setFont(const QFont& font) override;
    virtual void setAlignment(Qt::Alignment alignment) override;

    virtual void setActiveImpl(bool active) override;
    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event) override;

private:
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);

    void finishEditing();
    void resetTool();
    std::optional<QPointF> getPagePointUnderMouse(QMouseEvent* event) const;

    bool isEditing() const;

    PDFPickTool* m_pickTool;
    PDFPageContentElementTextBox* m_element;
    PDFTextEditPseudowidget* m_textEditWidget;
};

}   // namespace pdf

#endif // PDFPAGECONTENTEDITORTOOLS_H
