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

#include "pdfpagecontentelements.h"
#include "pdfpainterutils.h"

#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QCursor>

namespace pdf
{

PDFInteger PDFPageContentElement::getPageIndex() const
{
    return m_pageIndex;
}

void PDFPageContentElement::setPageIndex(PDFInteger newPageIndex)
{
    m_pageIndex = newPageIndex;
}

const QPen& PDFPageContentStyledElement::getPen() const
{
    return m_pen;
}

void PDFPageContentStyledElement::setPen(const QPen& newPen)
{
    m_pen = newPen;
}

const QBrush& PDFPageContentStyledElement::getBrush() const
{
    return m_brush;
}

void PDFPageContentStyledElement::setBrush(const QBrush& newBrush)
{
    m_brush = newBrush;
}

PDFPageContentElementRectangle* PDFPageContentElementRectangle::clone() const
{
    PDFPageContentElementRectangle* copy = new PDFPageContentElementRectangle();
    copy->setPageIndex(getPageIndex());
    copy->setPen(getPen());
    copy->setBrush(getBrush());
    copy->setRectangle(getRectangle());
    copy->setRounded(isRounded());
    return copy;
}

bool PDFPageContentElementRectangle::isRounded() const
{
    return m_rounded;
}

void PDFPageContentElementRectangle::setRounded(bool newRounded)
{
    m_rounded = newRounded;
}

const QRectF& PDFPageContentElementRectangle::getRectangle() const
{
    return m_rectangle;
}

void PDFPageContentElementRectangle::setRectangle(const QRectF& newRectangle)
{
    m_rectangle = newRectangle;
}

void PDFPageContentElementRectangle::drawPage(QPainter* painter,
                                              PDFInteger pageIndex,
                                              const PDFPrecompiledPage* compiledPage,
                                              PDFTextLayoutGetter& layoutGetter,
                                              const QMatrix& pagePointToDevicePointMatrix,
                                              QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    if (pageIndex != getPageIndex())
    {
        return;
    }

    PDFPainterStateGuard guard(painter);
    painter->setWorldMatrix(pagePointToDevicePointMatrix, true);
    painter->setPen(getPen());
    painter->setBrush(getBrush());
    painter->setRenderHint(QPainter::Antialiasing);

    QRectF rect = getRectangle();
    if (isRounded())
    {
        qreal radius = qMin(rect.width(), rect.height()) * 0.25;
        painter->drawRoundedRect(rect, radius, radius, Qt::AbsoluteSize);
    }
    else
    {
        painter->drawRect(rect);
    }
}

PDFPageContentScene::PDFPageContentScene(QObject* parent) :
    QObject(parent)
{

}

PDFPageContentScene::~PDFPageContentScene()
{

}

void PDFPageContentScene::addElement(PDFPageContentElement* element)
{
    m_elements.emplace_back(element);
    emit sceneChanged();
}

void PDFPageContentScene::clear()
{
    if (!m_elements.empty())
    {
        m_elements.clear();
        emit sceneChanged();
    }
}

void PDFPageContentScene::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

void PDFPageContentScene::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

void PDFPageContentScene::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

void PDFPageContentScene::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

void PDFPageContentScene::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

void PDFPageContentScene::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

void PDFPageContentScene::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

void PDFPageContentScene::wheelEvent(QWidget* widget, QWheelEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

QString PDFPageContentScene::getTooltip() const
{
    return QString();
}

const std::optional<QCursor>& PDFPageContentScene::getCursor() const
{
    return std::nullopt;
}

int PDFPageContentScene::getInputPriority() const
{
    return ToolPriority + 1;
}

void PDFPageContentScene::drawPage(QPainter* painter,
                                        PDFInteger pageIndex,
                                        const PDFPrecompiledPage* compiledPage,
                                        PDFTextLayoutGetter& layoutGetter,
                                        const QMatrix& pagePointToDevicePointMatrix,
                                        QList<PDFRenderError>& errors) const
{
}

}   // namespace pdf
