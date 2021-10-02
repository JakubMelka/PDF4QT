//    Copyright (C) 2021 Jakub Melka
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

#ifndef DIFFERENCESDOCKWIDGET_H
#define DIFFERENCESDOCKWIDGET_H

#include "settings.h"

#include <QDockWidget>
#include <QItemDelegate>

namespace Ui
{
class DifferencesDockWidget;
}

namespace pdf
{
class PDFDiffResult;
class PDFDiffResultNavigator;
}

namespace pdfdocdiff
{

class DifferenceItemDelegate : public QItemDelegate
{
    Q_OBJECT

private:
    using BaseClass = QItemDelegate;

public:
    DifferenceItemDelegate(QObject* parent);

    virtual void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    virtual QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

class DifferencesDockWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit DifferencesDockWidget(QWidget* parent,
                                   pdf::PDFDiffResult* diffResult,
                                   pdf::PDFDiffResultNavigator* diffNavigator,
                                   Settings* settings);
    virtual ~DifferencesDockWidget() override;

    void update();

private:
    Ui::DifferencesDockWidget* ui;

    QColor getColorForIndex(size_t index) const;

    pdf::PDFDiffResult* m_diffResult;
    pdf::PDFDiffResultNavigator* m_diffNavigator;
    Settings* m_settings;
};

}   // namespace pdfdocdiff

#endif // DIFFERENCESDOCKWIDGET_H
