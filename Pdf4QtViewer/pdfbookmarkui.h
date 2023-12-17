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
