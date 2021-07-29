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

#ifndef PDFDOCPAGEORGANIZER_PAGEITEMDELEGATE_H
#define PDFDOCPAGEORGANIZER_PAGEITEMDELEGATE_H

#include "pdfrenderer.h"
#include "pdfcms.h"

#include <QAbstractItemDelegate>

namespace pdfdocpage
{

class PageItemModel;
struct PageGroupItem;

class PageItemDelegate : public QAbstractItemDelegate
{
    Q_OBJECT

private:
    using BaseClass = QAbstractItemDelegate;

public:
    explicit PageItemDelegate(PageItemModel* model, QObject* parent);
    virtual ~PageItemDelegate() override;

    virtual void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    virtual QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    QSize getPageImageSize() const;
    void setPageImageSize(QSize pageImageSize);

private:
    static constexpr int getVerticalSpacing() { return 5; }
    static constexpr int getHorizontalSpacing() { return 5; }

    QPixmap getPageImagePixmap(const PageGroupItem* item, QRect rect) const;

    PageItemModel* m_model;
    QSize m_pageImageSize;
    pdf::PDFRasterizer* m_rasterizer;
};

}   // namespace pdfdocpage

#endif // PDFDOCPAGEORGANIZER_PAGEITEMDELEGATE_H
