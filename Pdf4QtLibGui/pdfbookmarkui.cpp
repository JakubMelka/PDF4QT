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

#include "pdfbookmarkui.h"
#include "pdfwidgetutils.h"
#include "pdfpainterutils.h"

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
    if (parent.isValid())
    {
        return QModelIndex();
    }

    return createIndex(row, column, nullptr);
}

QModelIndex PDFBookmarkItemModel::parent(const QModelIndex& child) const
{
    Q_UNUSED(child);
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
    if (parent.isValid())
    {
        return 0;
    }

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

    QColor color = bookmark.isAuto ? QColor(0, 123, 255) : QColor(255, 159, 0);

    if (options.state.testFlag(QStyle::State_Selected))
    {
        color = Qt::yellow;
    }

    QRect iconRect = rect;
    iconRect.setWidth(iconSize);
    iconRect.setHeight(iconSize);
    iconRect.moveCenter(QPoint(rect.left() + iconSize / 2, rect.center().y()));
    drawStar(*painter, iconRect.center(), iconRect.width() * 0.5, color);

    QRect textRect = rect;
    textRect.setLeft(iconRect.right() + margin);
    textRect.moveTop(rect.top() + (rect.height() - 2 * options.fontMetrics.lineSpacing()) / 2);

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
    pdf::PDFPainterStateGuard guard(&painter);

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    QPainterPath path;
    double angle = M_PI / 5;
    double phase = -M_PI / 10;

    for (int i = 0; i < 10; ++i)
    {
        double radius = (i % 2 == 0) ? size : size / 2.5;
        QPointF point(radius * cos(i * angle + phase), radius * sin(i * angle + phase));
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
}

QString PDFBookmarkItemDelegate::getPageText(const PDFBookmarkManager::Bookmark& bookmark) const
{
    if (bookmark.isAuto)
    {
        return tr("Page %1 | Generated").arg(bookmark.pageIndex + 1);
    }
    else
    {
        return tr("Page %1").arg(bookmark.pageIndex + 1);
    }
}

}   // namespace pdfviewer
