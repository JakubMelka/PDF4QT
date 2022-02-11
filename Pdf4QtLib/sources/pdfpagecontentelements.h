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

namespace pdf
{

class PDFPageContentElement
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

class PDFPageContentStyledElement : public PDFPageContentElement
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

class PDFPageContentElementRectangle : public PDFPageContentStyledElement
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

signals:
    /// This signal is emitted when scene has changed (including graphics)
    void sceneChanged();

private:
    std::vector<std::unique_ptr<PDFPageContentElement>> m_elements;
};

}   // namespace pdf

#endif // PDFPAGECONTENTELEMENTS_H
