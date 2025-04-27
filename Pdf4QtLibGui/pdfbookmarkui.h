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

#ifndef PDFBOOKMARKUI_H
#define PDFBOOKMARKUI_H

#include "pdfviewerglobal.h"
#include "pdfbookmarkmanager.h"

#include <QStyledItemDelegate>
#include <QAbstractItemModel>

namespace pdfviewer
{

class PDFBookmarkItemModel : public QAbstractItemModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractItemModel;

public:
    PDFBookmarkItemModel(PDFBookmarkManager* bookmarkManager, QObject* parent);

    virtual QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    virtual QModelIndex parent(const QModelIndex& child) const override;
    virtual int rowCount(const QModelIndex& parent) const override;
    virtual int columnCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;

private:
    PDFBookmarkManager* m_bookmarkManager = nullptr;
};

class PDFBookmarkItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

private:
    using BaseClass = QStyledItemDelegate;

public:
    PDFBookmarkItemDelegate(PDFBookmarkManager* bookmarkManager, QObject* parent);

    virtual void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    virtual QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    static constexpr int MARGIN = 6;
    static constexpr int ICON_SIZE = 32;

    void drawStar(QPainter& painter, const QPointF& center, double size, const QColor& color) const;

    QString getPageText(const PDFBookmarkManager::Bookmark& bookmark) const;

    PDFBookmarkManager* m_bookmarkManager = nullptr;
};

}   // namespace pdfviewer

#endif // PDFBOOKMARKUI_H
