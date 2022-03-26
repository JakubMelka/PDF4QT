//    Copyright (C) 2022 Jakub Melka
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
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFPAGECONTENTEDITORTOOLS_H
#define PDFPAGECONTENTEDITORTOOLS_H

#include "pdfwidgettool.h"

namespace pdf
{

class PDFPageContentScene;
class PDFPageContentSvgElement;
class PDFPageContentElementDot;
class PDFPageContentElementLine;
class PDFPageContentElementTextBox;
class PDFPageContentElementRectangle;
class PDFPageContentElementFreehandCurve;
class PDFTextEditPseudowidget;

class PDFCreatePCElementTool : public PDFWidgetTool
{
public:
    PDFCreatePCElementTool(PDFDrawWidgetProxy* proxy,
                           PDFPageContentScene* scene,
                           QAction* action,
                           QObject* parent);

protected:
    static QRectF getRectangleFromPickTool(PDFPickTool* pickTool, const QMatrix& pagePointToDevicePointMatrix);

    PDFPageContentScene* m_scene;
};

/// Tool that creates rectangle element.
class PDF4QTLIBSHARED_EXPORT PDFCreatePCElementRectangleTool : public PDFCreatePCElementTool
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
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

private:
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);

    PDFPickTool* m_pickTool;
    PDFPageContentElementRectangle* m_element;
};

/// Tool that displays SVG image
class PDF4QTLIBSHARED_EXPORT PDFCreatePCElementSvgTool : public PDFCreatePCElementTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreatePCElementTool;

public:
    explicit PDFCreatePCElementSvgTool(PDFDrawWidgetProxy* proxy,
                                       PDFPageContentScene* scene,
                                       QAction* action,
                                       QByteArray content,
                                       QObject* parent);
    virtual ~PDFCreatePCElementSvgTool() override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

private:
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);

    PDFPickTool* m_pickTool;
    PDFPageContentSvgElement* m_element;
};

/// Tool that creates line element.
class PDF4QTLIBSHARED_EXPORT PDFCreatePCElementLineTool : public PDFCreatePCElementTool
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
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

private:
    void clear();
    void onPointPicked(pdf::PDFInteger pageIndex, QPointF pagePoint);

    PDFPickTool* m_pickTool;
    PDFPageContentElementLine* m_element;
    std::optional<QPointF> m_startPoint;
};

/// Tool that creates dot element.
class PDF4QTLIBSHARED_EXPORT PDFCreatePCElementDotTool : public PDFCreatePCElementTool
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
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

private:
    void onPointPicked(pdf::PDFInteger pageIndex, QPointF pagePoint);

    PDFPickTool* m_pickTool;
    PDFPageContentElementDot* m_element;
};

/// Tool that creates freehand curve element.
class PDF4QTLIBSHARED_EXPORT PDFCreatePCElementFreehandCurveTool : public PDFCreatePCElementTool
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
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

protected:
    virtual void setActiveImpl(bool active);

private:
    void resetTool();

    PDFPageContentElementFreehandCurve* m_element;
};

/// Tool that displays SVG image
class PDF4QTLIBSHARED_EXPORT PDFCreatePCElementTextTool : public PDFCreatePCElementTool
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
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

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
    std::optional<QPointF> getPagePointUnderMouse(QMouseEvent* event) const;

    bool isEditing() const;

    PDFPickTool* m_pickTool;
    PDFPageContentElementTextBox* m_element;
    PDFTextEditPseudowidget* m_textEditWidget;
};

}   // namespace pdf

#endif // PDFPAGECONTENTEDITORTOOLS_H
