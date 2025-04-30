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

#ifndef PDFPAGEMASTER_PAGEITEMDELEGATE_H
#define PDFPAGEMASTER_PAGEITEMDELEGATE_H

#include "pdfrenderer.h"
#include "pdfcms.h"

#include <QAbstractItemDelegate>

namespace pdfpagemaster
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
    mutable double m_dpiScaleRatio = 1.0;
};

}   // namespace pdfpagemaster

#endif // PDFPAGEMASTER_PAGEITEMDELEGATE_H
