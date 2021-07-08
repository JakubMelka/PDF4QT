//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pageitemdelegate.h"
#include "pageitemmodel.h"
#include "pdfwidgetutils.h"

#include <QPainter>

namespace pdfdocpage
{

PageItemDelegate::PageItemDelegate(PageItemModel* model, QObject* parent) :
    BaseClass(parent),
    m_model(model)
{

}

PageItemDelegate::~PageItemDelegate()
{

}

void PageItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const PageGroupItem* item = m_model->getItem(index);

    if (!item)
    {
        return;
    }

    QRect rect = option.rect;

    QSize scaledSize = pdf::PDFWidgetUtils::scaleDPI(option.widget, m_pageImageSize);
    int verticalSpacing = pdf::PDFWidgetUtils::scaleDPI_y(option.widget, getVerticalSpacing());

    QRect pageBoundingRect = QRect(QPoint(rect.left() + (rect.width() - scaledSize.width()) / 2, rect.top() + verticalSpacing), scaledSize);

    // Draw page preview
    if (!item->groups.empty())
    {
        const PageGroupItem::GroupItem& groupItem = item->groups.front();
        QSize pageImageSize = groupItem.rotatedPageDimensionsMM.scaled(pageBoundingRect.size(), Qt::KeepAspectRatio).toSize();
        QRect pageImageRect(pageBoundingRect.topLeft() + QPoint((pageBoundingRect.width() - pageImageSize.width()) / 2, (pageBoundingRect.height() - pageImageSize.height()) / 2), pageImageSize);

        painter->setPen(QPen(Qt::black));
        painter->setBrush(Qt::white);
        painter->drawRect(pageImageRect);
    }

    int textOffset = pageBoundingRect.bottom() + verticalSpacing;
    QRect textRect = option.rect;
    textRect.setTop(textOffset);
    textRect.setHeight(option.fontMetrics.lineSpacing());
    painter->drawText(textRect, Qt::AlignCenter | Qt::TextSingleLine, item->groupName);
    textRect.translate(0, textRect.height());
    painter->drawText(textRect, Qt::AlignCenter | Qt::TextSingleLine, item->pagesCaption);

    if (option.state.testFlag(QStyle::State_Selected))
    {
        QColor selectedColor = option.palette.color(QPalette::Active, QPalette::Highlight);
        selectedColor.setAlphaF(0.3);
        painter->fillRect(rect, selectedColor);
    }
}

QSize PageItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    Q_UNUSED(index);

    QSize scaledSize = pdf::PDFWidgetUtils::scaleDPI(option.widget, m_pageImageSize);
    int height = scaledSize.height() + option.fontMetrics.lineSpacing() * 2 + 2 * pdf::PDFWidgetUtils::scaleDPI_y(option.widget, getVerticalSpacing());
    int width = qMax(pdf::PDFWidgetUtils::scaleDPI_x(option.widget, 40), scaledSize.width() + 2 * pdf::PDFWidgetUtils::scaleDPI_x(option.widget, getHorizontalSpacing()));
    return QSize(width, height);
}

QSize PageItemDelegate::getPageImageSize() const
{
    return m_pageImageSize;
}

void PageItemDelegate::setPageImageSize(const QSize& pageImageSize)
{
    m_pageImageSize = pageImageSize;
}

}   // namespace pdfdocpage
