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

#ifndef PDFPAGECONTENTELEMENTS_H
#define PDFPAGECONTENTELEMENTS_H

#include "pdfdocumentdrawinterface.h"

#include <QPen>
#include <QBrush>
#include <QCursor>
#include <QPainterPath>

class QSvgRenderer;

namespace pdf
{

class PDF4QTLIBSHARED_EXPORT PDFPageContentElement
{
public:
    explicit PDFPageContentElement() = default;
    virtual ~PDFPageContentElement() = default;

    virtual PDFPageContentElement* clone() const = 0;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const = 0;

    PDFInteger getPageIndex() const;
    void setPageIndex(PDFInteger newPageIndex);

protected:
    PDFInteger m_pageIndex = -1;
};

class PDF4QTLIBSHARED_EXPORT PDFPageContentStyledElement : public PDFPageContentElement
{
public:
    explicit PDFPageContentStyledElement() = default;
    virtual ~PDFPageContentStyledElement() = default;

    const QPen& getPen() const;
    void setPen(const QPen& newPen);

    const QBrush& getBrush() const;
    void setBrush(const QBrush& newBrush);

protected:
    QPen m_pen;
    QBrush m_brush;
};

class PDF4QTLIBSHARED_EXPORT PDFPageContentElementRectangle : public PDFPageContentStyledElement
{
public:
    virtual ~PDFPageContentElementRectangle() = default;

    virtual PDFPageContentElementRectangle* clone() const override;

    bool isRounded() const;
    void setRounded(bool newRounded);

    const QRectF& getRectangle() const;
    void setRectangle(const QRectF& newRectangle);

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

private:
    bool m_rounded = false;
    QRectF m_rectangle;
};

class PDF4QTLIBSHARED_EXPORT PDFPageContentElementLine : public PDFPageContentStyledElement
{
public:
    virtual ~PDFPageContentElementLine() = default;

    virtual PDFPageContentElementLine* clone() const override;

    enum class LineGeometry
    {
        General,
        Horizontal,
        Vertical
    };

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

    LineGeometry getGeometry() const;
    void setGeometry(LineGeometry newGeometry);

    const QLineF& getLine() const;
    void setLine(const QLineF& newLine);

private:
    LineGeometry m_geometry = LineGeometry::General;
    QLineF m_line;
};

class PDF4QTLIBSHARED_EXPORT PDFPageContentElementDot : public PDFPageContentStyledElement
{
public:
    virtual ~PDFPageContentElementDot() = default;

    virtual PDFPageContentElementDot* clone() const override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;


    QPointF getPoint() const;
    void setPoint(QPointF newPoint);

private:
    QPointF m_point;
};

class PDF4QTLIBSHARED_EXPORT PDFPageContentElementFreehandCurve : public PDFPageContentStyledElement
{
public:
    virtual ~PDFPageContentElementFreehandCurve() = default;

    virtual PDFPageContentElementFreehandCurve* clone() const override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

    QPainterPath getCurve() const;
    void setCurve(QPainterPath newCurve);

    bool isEmpty() const { return m_curve.isEmpty(); }
    void addStartPoint(const QPointF& point);
    void addPoint(const QPointF& point);
    void clear();

private:
    QPainterPath m_curve;
};

class PDF4QTLIBSHARED_EXPORT PDFPageContentSvgElement : public PDFPageContentElement
{
public:
    PDFPageContentSvgElement();
    virtual ~PDFPageContentSvgElement();

    virtual PDFPageContentSvgElement* clone() const override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

    const QByteArray& getContent() const;
    void setContent(const QByteArray& newContent);

    const QRectF& getRectangle() const;
    void setRectangle(const QRectF& newRectangle);

private:
    QRectF m_rectangle;
    QByteArray m_content;
    std::unique_ptr<QSvgRenderer> m_renderer;
};

class PDF4QTLIBSHARED_EXPORT PDFPageContentScene : public QObject,
                                                   public IDocumentDrawInterface,
                                                   public IDrawWidgetInputInterface
{
    Q_OBJECT

public:
    explicit PDFPageContentScene(QObject* parent);
    virtual ~PDFPageContentScene();

    /// Add new element to page content scene, scene
    /// takes ownership over the element.
    /// \param element Element
    void addElement(PDFPageContentElement* element);

    /// Clear whole scene - remove all page content elements
    void clear();

    /// Returns true, if scene is empty
    bool isEmpty() const { return m_elements.empty(); }

    // IDrawWidgetInputInterface interface
public:
    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event) override;
    virtual QString getTooltip() const override;
    virtual const std::optional<QCursor>& getCursor() const override;
    virtual int getInputPriority() const override;

    // IDocumentDrawInterface interface
public:
    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

    bool isActive() const;
    void setActive(bool newIsActive);

signals:
    /// This signal is emitted when scene has changed (including graphics)
    void sceneChanged();

private:
    bool m_isActive;
    std::vector<std::unique_ptr<PDFPageContentElement>> m_elements;
    std::optional<QCursor> m_cursor;
};

}   // namespace pdf

#endif // PDFPAGECONTENTELEMENTS_H
