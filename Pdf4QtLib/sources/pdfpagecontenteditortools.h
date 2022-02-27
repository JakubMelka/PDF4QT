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
class PDFPageContentElementRectangle;

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

}   // namespace pdf

#endif // PDFPAGECONTENTEDITORTOOLS_H
