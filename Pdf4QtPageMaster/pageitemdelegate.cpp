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

#include "pageitemdelegate.h"
#include "pageitemmodel.h"
#include "pageitempreviewrenderer.h"
#include "pdfwidgetutils.h"
#include "pdfpainterutils.h"

#include <QPainter>
#include <QSortFilterProxyModel>
#include <QStringList>

#include <algorithm>

namespace pdfpagemaster
{

PageItemDelegate::PageItemDelegate(PageItemModel* model, PageItemPreviewRenderer* previewRenderer, QObject* parent) :
    BaseClass(parent),
    m_model(model),
    m_previewRenderer(previewRenderer)
{
    Q_ASSERT(m_previewRenderer);
}

PageItemDelegate::~PageItemDelegate() = default;

void PageItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QModelIndex sourceIndex = index;
    if (const QSortFilterProxyModel* proxyModel = qobject_cast<const QSortFilterProxyModel*>(index.model()))
    {
        sourceIndex = proxyModel->mapToSource(index);
    }

    const PageGroupItem* item = m_model->getItem(sourceIndex);

    if (!item)
    {
        return;
    }

    QRect rect = option.rect;

    m_dpiScaleRatio = option.widget->devicePixelRatioF();
    QSize scaledSize = pdf::PDFWidgetUtils::scaleDPI(option.widget, m_pageImageSize);
    const int verticalSpacing = pdf::PDFWidgetUtils::scaleDPI_y(option.widget, getVerticalSpacing());
    const int horizontalSpacing = pdf::PDFWidgetUtils::scaleDPI_x(option.widget, getHorizontalSpacing());

    QRect pageBoundingRect = QRect(QPoint(rect.left() + (rect.width() - scaledSize.width()) / 2, rect.top() + verticalSpacing), scaledSize);

    QRect pageImageRect;

    // Draw page preview
    if (!item->groups.empty())
    {
        pageImageRect = getPageImageRect(item, rect, option.widget);

        painter->fillRect(pageImageRect, Qt::white);

        QPixmap pageImagePixmap = m_previewRenderer->getPageImagePixmap(item, pageImageRect);
        if (!pageImagePixmap.isNull())
        {
            painter->drawPixmap(pageImageRect, pageImagePixmap);
        }
        else
        {
            m_previewRenderer->requestPreview(item, pageImageRect, sourceIndex.row(), m_dpiScaleRatio);
        }

        painter->setPen(QPen(Qt::black));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(pageImageRect);
    }

    int textOffset = pageBoundingRect.bottom() + verticalSpacing;
    QRect textRect = option.rect;
    textRect.setTop(textOffset);
    textRect.setHeight(option.fontMetrics.lineSpacing());
    painter->setPen(option.palette.color(QPalette::Normal, QPalette::Text));
    const QString displayText = option.fontMetrics.elidedText(m_model->getItemDisplayText(item), Qt::ElideMiddle, textRect.width() - horizontalSpacing * 2);
    painter->drawText(textRect, Qt::AlignCenter | Qt::TextSingleLine, displayText);
    textRect.translate(0, textRect.height());
    const QString pageText = option.fontMetrics.elidedText(item->pagesCaption, Qt::ElideRight, textRect.width() - horizontalSpacing * 2);
    painter->drawText(textRect, Qt::AlignCenter | Qt::TextSingleLine, pageText);

    if (option.state.testFlag(QStyle::State_Selected))
    {
        QColor selectedColor = option.palette.color(QPalette::Active, QPalette::Highlight);
        selectedColor.setAlphaF(0.3f);
        painter->fillRect(rect, selectedColor);
    }

    QPoint tagPoint = rect.topRight() + QPoint(-horizontalSpacing, verticalSpacing);
    for (const QString& tag : item->tags)
    {
        QStringList splitted = tag.split('@', Qt::KeepEmptyParts);
        if (splitted.size() != 2 || splitted.back().isEmpty())
        {
            continue;
        }

        QColor color = QColor::fromString(splitted.front());
        QRect bubbleRect = pdf::PDFPainterHelper::drawBubble(painter, tagPoint, color, splitted.back(), Qt::AlignLeft | Qt::AlignBottom);
        tagPoint.ry() += bubbleRect.height() + verticalSpacing;
    }

    if (item->groups.cend() != std::find_if(item->groups.cbegin(), item->groups.cend(), [](const PageGroupItem::GroupItem& groupItem) { return groupItem.pageAdditionalRotation != pdf::PageRotation::None; }))
    {
        QStringList rotations;
        for (const PageGroupItem::GroupItem& groupItem : item->groups)
        {
            QString rotationText;
            switch (groupItem.pageAdditionalRotation)
            {
                case pdf::PageRotation::None:
                    break;
                case pdf::PageRotation::Rotate90:
                    rotationText = tr("90°");
                    break;
                case pdf::PageRotation::Rotate180:
                    rotationText = tr("180°");
                    break;
                case pdf::PageRotation::Rotate270:
                    rotationText = tr("270°");
                    break;
                default:
                    rotationText = QString::number(int(groupItem.pageAdditionalRotation));
                    break;
            }

            if (!rotationText.isEmpty() && !rotations.contains(rotationText))
            {
                rotations << rotationText;
            }
        }

        QRect rotationRect = pageImageRect.adjusted(horizontalSpacing, horizontalSpacing, -horizontalSpacing, -horizontalSpacing);
        const QString rotationText = rotations.join(", ");
        const int maximumBadgeWidth = qMax(horizontalSpacing * 4, pageImageRect.width() - horizontalSpacing * 2);
        rotationRect.setSize(QSize(qMin(maximumBadgeWidth, option.fontMetrics.horizontalAdvance(rotationText) + horizontalSpacing * 2), option.fontMetrics.lineSpacing() + verticalSpacing));
        QColor badgeColor = option.palette.color(QPalette::Active, QPalette::Highlight);
        badgeColor.setAlphaF(0.85f);
        painter->setPen(Qt::NoPen);
        painter->setBrush(badgeColor);
        painter->drawRoundedRect(rotationRect, 4, 4);
        painter->setPen(option.palette.color(QPalette::Active, QPalette::HighlightedText));
        painter->drawText(rotationRect, Qt::AlignCenter, option.fontMetrics.elidedText(rotationText, Qt::ElideRight, rotationRect.width() - horizontalSpacing));
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

void PageItemDelegate::setPageImageSize(QSize pageImageSize)
{
    if (m_pageImageSize != pageImageSize)
    {
        m_pageImageSize = pageImageSize;
        m_previewRenderer->setPageImageSize(pageImageSize);
        Q_EMIT sizeHintChanged(QModelIndex());
    }
}

QRect PageItemDelegate::getPageImageRect(const PageGroupItem* item, const QRect& itemRect, const QWidget* widget) const
{
    QSize scaledSize = pdf::PDFWidgetUtils::scaleDPI(widget, m_pageImageSize);
    const int verticalSpacing = pdf::PDFWidgetUtils::scaleDPI_y(widget, getVerticalSpacing());

    QRect pageBoundingRect(QPoint(itemRect.left() + (itemRect.width() - scaledSize.width()) / 2, itemRect.top() + verticalSpacing), scaledSize);

    if (item->groups.empty())
    {
        return pageBoundingRect;
    }

    const PageGroupItem::GroupItem& groupItem = item->groups.front();
    QSizeF rotatedPageSize = PageItemModel::getDisplayedPageDimensionsMM(groupItem);
    QSize pageImageSize = rotatedPageSize.scaled(pageBoundingRect.size(), Qt::KeepAspectRatio).toSize();
    return QRect(pageBoundingRect.topLeft() + QPoint((pageBoundingRect.width() - pageImageSize.width()) / 2, (pageBoundingRect.height() - pageImageSize.height()) / 2), pageImageSize);
}

}   // namespace pdfpagemaster
