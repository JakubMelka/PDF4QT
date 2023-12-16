//    Copyright (C) 2023 Jakub Melka
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

#include "pdfbookmarkui.h"
#include "pdfwidgetutils.h"

#include <QPainter>
#include <QPainterPath>

namespace pdfviewer
{

PDFBookmarkItemModel::PDFBookmarkItemModel(PDFBookmarkManager* bookmarkManager, QObject* parent) :
    BaseClass(parent),
    m_bookmarkManager(bookmarkManager)
{
    connect(m_bookmarkManager, &PDFBookmarkManager::bookmarksAboutToBeChanged, this, &PDFBookmarkItemModel::beginResetModel);
    connect(m_bookmarkManager, &PDFBookmarkManager::bookmarksChanged, this, &PDFBookmarkItemModel::endResetModel);
}

QModelIndex PDFBookmarkItemModel::index(int row, int column, const QModelIndex& parent) const
{
    return createIndex(row, column, nullptr);
}

QModelIndex PDFBookmarkItemModel::parent(const QModelIndex& child) const
{
    return QModelIndex();
}

int PDFBookmarkItemModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
    {
        return 0;
    }

    return m_bookmarkManager ? m_bookmarkManager->getBookmarkCount() : 0;
}

int PDFBookmarkItemModel::columnCount(const QModelIndex& parent) const
{
    return 1;
}

QVariant PDFBookmarkItemModel::data(const QModelIndex& index, int role) const
{
    if (role == Qt::DisplayRole)
    {
        return m_bookmarkManager->getBookmark(index.row()).name;
    }

    return QVariant();
}

PDFBookmarkItemDelegate::PDFBookmarkItemDelegate(PDFBookmarkManager* bookmarkManager, QObject* parent) :
    BaseClass(parent),
    m_bookmarkManager(bookmarkManager)
{

}

void PDFBookmarkItemDelegate::paint(QPainter* painter,
                                    const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const
{
    QStyleOptionViewItem options = option;
    initStyleOption(&options, index);

    PDFBookmarkManager::Bookmark bookmark = m_bookmarkManager->getBookmark(index.row());

    options.text = QString();
    options.widget->style()->drawControl(QStyle::CE_ItemViewItem, &options, painter);

    const int margin = pdf::PDFWidgetUtils::scaleDPI_x(option.widget, MARGIN);
    const int iconSize = pdf::PDFWidgetUtils::scaleDPI_x(option.widget, ICON_SIZE);

    QRect rect = options.rect;
    rect.marginsRemoved(QMargins(margin, margin, margin, margin));

    QRect iconRect = rect;
    iconRect.setWidth(iconSize);
    iconRect.setHeight(iconSize);
    iconRect.moveCenter(QPoint(rect.left() + iconSize / 2, rect.center().y()));
    drawStar(*painter, iconRect.center(), iconRect.width() * 0.5, QColor(64, 64, 192));

    QRect textRect = rect;
    textRect.setLeft(iconRect.right() + margin);

    textRect.setHeight(options.fontMetrics.lineSpacing());

    QFont font = options.font;
    font.setBold(true);

    painter->setFont(font);
    painter->drawText(textRect, getPageText(bookmark));

    textRect.translate(0, textRect.height());

    painter->setFont(options.font);
    painter->drawText(textRect, bookmark.name);
}

QSize PDFBookmarkItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    PDFBookmarkManager::Bookmark bookmark = m_bookmarkManager->getBookmark(index.row());

    const int textWidthLine1 = option.fontMetrics.horizontalAdvance(getPageText(bookmark));
    const int textWidthLine2 = option.fontMetrics.horizontalAdvance(option.text);
    const int textWidth = qMax(textWidthLine1, textWidthLine2);
    const int textHeight = option.fontMetrics.lineSpacing() * 2;

    const int margin = pdf::PDFWidgetUtils::scaleDPI_x(option.widget, MARGIN);
    const int iconSize = pdf::PDFWidgetUtils::scaleDPI_x(option.widget, ICON_SIZE);

    const int requiredWidth = 3 * margin + iconSize + textWidth;
    const int requiredHeight = 2 * margin + qMax(iconSize, textHeight);

    return QSize(requiredWidth, requiredHeight);
}

void PDFBookmarkItemDelegate::drawStar(QPainter& painter, const QPointF& center, double size, const QColor& color) const
{
    painter.save();

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    QPainterPath path;
    double angle = M_PI / 5;

    for (int i = 0; i < 10; ++i)
    {
        double radius = (i % 2 == 0) ? size : size / 2.5;
        QPointF point(radius * cos(i * angle), radius * sin(i * angle));
        point += center;

        if (i == 0)
        {
            path.moveTo(point);
        }
        else
        {
            path.lineTo(point);
        }
    }
    path.closeSubpath();

    painter.drawPath(path);

    painter.restore();
}

QString PDFBookmarkItemDelegate::getPageText(const PDFBookmarkManager::Bookmark& bookmark) const
{
    return tr("Page %1").arg(bookmark.pageIndex + 1);
}

}   // namespace pdfviewer
