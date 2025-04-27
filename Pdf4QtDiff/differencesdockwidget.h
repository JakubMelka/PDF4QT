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

#ifndef PDFDIFF_DIFFERENCESDOCKWIDGET_H
#define PDFDIFF_DIFFERENCESDOCKWIDGET_H

#include "settings.h"

#include <QDockWidget>
#include <QItemDelegate>

namespace Ui
{
class DifferencesDockWidget;
}

class QTreeWidgetItem;

namespace pdf
{
class PDFDiffResult;
class PDFDiffResultNavigator;
}

namespace pdfdiff
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
                                   pdf::PDFDiffResult* unfilteredDiffResult,
                                   pdf::PDFDiffResult* diffResult,
                                   pdf::PDFDiffResultNavigator* diffNavigator,
                                   Settings* settings);
    virtual ~DifferencesDockWidget() override;

    void update();

private slots:
    void onSelectionChanged(size_t currentIndex);
    void onCurrentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);

private:
    Ui::DifferencesDockWidget* ui;

    QColor getColorForIndex(size_t index) const;
    QModelIndex findResultIndex(size_t index) const;

    pdf::PDFDiffResult* m_unfilteredDiffResult;
    pdf::PDFDiffResult* m_diffResult;
    pdf::PDFDiffResultNavigator* m_diffNavigator;
    Settings* m_settings;
    bool m_disableChangeSelectedResultIndex;
};

}   // namespace pdfdiff

#endif // PDFDIFF_DIFFERENCESDOCKWIDGET_H
