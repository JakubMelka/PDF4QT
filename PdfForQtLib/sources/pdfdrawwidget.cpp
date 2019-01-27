//    Copyright (C) 2018 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"

#include <QPainter>
#include <QGridLayout>
#include <QKeyEvent>

namespace pdf
{

PDFWidget::PDFWidget(QWidget* parent) :
    QWidget(parent),
    m_drawWidget(nullptr),
    m_horizontalScrollBar(nullptr),
    m_verticalScrollBar(nullptr),
    m_proxy(nullptr)
{
    m_drawWidget = new PDFDrawWidget(this, this);
    m_horizontalScrollBar = new QScrollBar(Qt::Horizontal, this);
    m_verticalScrollBar = new QScrollBar(Qt::Vertical, this);

    QGridLayout* layout = new QGridLayout(this);
    layout->setSpacing(0);
    layout->addWidget(m_drawWidget, 0, 0);
    layout->addWidget(m_horizontalScrollBar, 1, 0);
    layout->addWidget(m_verticalScrollBar, 0, 1);
    layout->setMargin(0);

    setLayout(layout);
    setFocusProxy(m_drawWidget);

    m_proxy = new PDFDrawWidgetProxy(this);
    m_proxy->init(this);
}

PDFWidget::~PDFWidget()
{

}

void PDFWidget::setDocument(const PDFDocument* document)
{
    m_proxy->setDocument(document);
}

PDFDrawWidget::PDFDrawWidget(PDFWidget* widget, QWidget* parent) :
    QWidget(parent),
    m_widget(widget)
{
    setFocusPolicy(Qt::StrongFocus);
}

PDFDrawWidget::~PDFDrawWidget()
{

}

QSize PDFDrawWidget::minimumSizeHint() const
{
    return QSize(200, 200);
}

void PDFDrawWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    m_widget->getDrawWidgetProxy()->draw(&painter, this->rect());
}

void PDFDrawWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    m_widget->getDrawWidgetProxy()->update();
}

void PDFDrawWidget::keyPressEvent(QKeyEvent* event)
{
    QScrollBar* verticalScrollbar = m_widget->getVerticalScrollbar();

    // Vertical navigation
    if (verticalScrollbar->isVisible())
    {
        if (event->matches(QKeySequence::MoveToStartOfDocument))
        {
            verticalScrollbar->setValue(0);
        }
        else if (event->matches(QKeySequence::MoveToEndOfDocument))
        {
            verticalScrollbar->setValue(verticalScrollbar->maximum());
        }
        else if (event->matches(QKeySequence::MoveToNextPage))
        {
            verticalScrollbar->setValue(verticalScrollbar->value() + verticalScrollbar->pageStep());
        }
    }

    event->accept();
}

}   // namespace pdf
