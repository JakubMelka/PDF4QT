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

#include "differencesdockwidget.h"
#include "ui_differencesdockwidget.h"

#include "pdfdiff.h"
#include "pdfwidgetutils.h"

#include <QPixmap>
#include <QPainter>
#include <QTreeWidgetItem>

namespace pdfdocdiff
{

DifferenceItemDelegate::DifferenceItemDelegate(QObject* parent) :
    BaseClass(parent)
{

}

void DifferenceItemDelegate::paint(QPainter* painter,
                                   const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const
{
    BaseClass::paint(painter, option, index);
}

QSize DifferenceItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                       const QModelIndex& index) const
{
    QStyleOptionViewItem adjustedOption = option;

    if (!option.rect.isValid())
    {
        // Jakub Melka: Why this? We need to use text wrapping. Unfortunately,
        // standard delegate needs correct text rectangle (at least rectangle width),
        // for word wrap calculation. So we must manually calculate rectangle width.
        // Of course, we cant use visualRect of the tree widget, because of cyclical
        // dependence.

        const QTreeWidget* treeWidget = qobject_cast<const QTreeWidget*>(option.widget);
        int xOffset = treeWidget->columnViewportPosition(index.column());
        int height = option.fontMetrics.lineSpacing();
        int yOffset = 0;
        int width = treeWidget->columnWidth(index.column());

        int level = treeWidget->rootIsDecorated() ? 1 : 0;
        QModelIndex currentIndex = index.parent();
        while (currentIndex.isValid())
        {
            ++level;
            currentIndex = currentIndex.parent();
        }

        xOffset += level * treeWidget->indentation();
        adjustedOption.rect = QRect(xOffset, yOffset, width - xOffset, height);
    }

    return BaseClass::sizeHint(adjustedOption, index);
}

DifferencesDockWidget::DifferencesDockWidget(QWidget* parent,
                                             pdf::PDFDiffResult* diffResult,
                                             pdf::PDFDiffResultNavigator* diffNavigator,
                                             Settings* settings) :
    QDockWidget(parent),
    ui(new Ui::DifferencesDockWidget),
    m_diffResult(diffResult),
    m_diffNavigator(diffNavigator),
    m_settings(settings),
    m_disableChangeSelectedResultIndex(false)
{
    ui->setupUi(this);

    ui->differencesTreeWidget->setItemDelegate(new DifferenceItemDelegate(this));
    ui->differencesTreeWidget->setIconSize(pdf::PDFWidgetUtils::scaleDPI(ui->differencesTreeWidget, QSize(16, 16)));
    connect(diffNavigator, &pdf::PDFDiffResultNavigator::selectionChanged, this, &DifferencesDockWidget::onSelectionChanged);
    connect(ui->differencesTreeWidget, &QTreeWidget::currentItemChanged, this, &DifferencesDockWidget::onCurrentItemChanged);

    setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 120));
}

DifferencesDockWidget::~DifferencesDockWidget()
{
    delete ui;
}

QColor DifferencesDockWidget::getColorForIndex(size_t index) const
{
    QColor color;

    const size_t resultIndex = index;

    if (m_diffResult->isReplaceDifference(resultIndex))
    {
        color = m_settings->colorReplaced;
    }
    else if (m_diffResult->isRemoveDifference(resultIndex))
    {
        color = m_settings->colorRemoved;
    }
    else if (m_diffResult->isAddDifference(resultIndex))
    {
        color = m_settings->colorAdded;
    }
    else if (m_diffResult->isPageMoveDifference(resultIndex))
    {
        color = m_settings->colorPageMove;
    }

    return color;
}

QModelIndex DifferencesDockWidget::findResultIndex(size_t index) const
{
    QAbstractItemModel* model = ui->differencesTreeWidget->model();
    const int count = ui->differencesTreeWidget->topLevelItemCount();

    for (int i = 0; i < count; ++i)
    {
        QModelIndex parentIndex = model->index(i, 0);
        if (parentIndex.isValid())
        {
            const int childCount = model->rowCount(parentIndex);
            for (int j = 0; j < childCount; ++j)
            {
                QModelIndex childIndex = parentIndex.child(j, 0);
                QVariant data = childIndex.data(Qt::UserRole);
                if (data.isValid())
                {
                    if (data.toULongLong() == index)
                    {
                        return childIndex;
                    }
                }
            }
        }
    }

    return QModelIndex();
}

void DifferencesDockWidget::update()
{
    ui->differencesTreeWidget->clear();

    QList<QTreeWidgetItem*> topItems;

    QLocale locale;

    if (m_diffResult && !m_diffResult->isSame())
    {
        std::map<QRgb, QIcon> icons;
        auto getIcon = [this, &icons](QColor color)
        {
            auto it = icons.find(color.rgb());
            if (it == icons.end())
            {
                QPixmap pixmap(ui->differencesTreeWidget->iconSize());
                pixmap.fill(Qt::transparent);

                QPainter painter(&pixmap);
                painter.setPen(Qt::NoPen);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setBrush(QBrush(color));
                painter.drawEllipse(0, 0, pixmap.width(), pixmap.height());
                painter.end();

                QIcon icon(pixmap);
                it = icons.insert(std::make_pair(color.rgb(), std::move(icon))).first;
            }

            return it->second;
        };

        const size_t differenceCount = m_diffResult->getDifferencesCount();
        ui->infoTextLabel->setText(tr("%1 Differences").arg(differenceCount));

        pdf::PDFInteger lastLeftPageIndex = -1;
        pdf::PDFInteger lastRightPageIndex = -1;

        for (size_t i = 0; i < differenceCount; ++i)
        {
            pdf::PDFInteger pageIndexLeft = m_diffResult->getLeftPage(i);
            pdf::PDFInteger pageIndexRight = m_diffResult->getRightPage(i);

            if (lastLeftPageIndex != pageIndexLeft ||
                lastRightPageIndex != pageIndexRight ||
                topItems.empty())
            {
                // Create new top level item
                QStringList captionParts;
                captionParts << QString("#%1:").arg(topItems.size() + 1);

                if (pageIndexLeft == pageIndexRight)
                {
                    captionParts << tr("Page %1").arg(locale.toString(pageIndexLeft + 1));
                }
                else
                {
                    if (pageIndexLeft != -1)
                    {
                        captionParts << tr("Left %1").arg(locale.toString(pageIndexLeft + 1));
                    }

                    if (pageIndexRight != -1)
                    {
                        captionParts << tr("Right %1").arg(locale.toString(pageIndexRight + 1));
                    }
                }

                QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << captionParts.join(" "));
                topItems << item;

                lastLeftPageIndex = pageIndexLeft;
                lastRightPageIndex = pageIndexRight;
            }

            Q_ASSERT(!topItems.isEmpty());
            QTreeWidgetItem* parent = topItems.back();

            QTreeWidgetItem* item = new QTreeWidgetItem(parent, QStringList() << m_diffResult->getMessage(i));
            item->setData(0, Qt::UserRole, i);

            QColor color = getColorForIndex(i);

            if (color.isValid())
            {
                item->setData(0, Qt::DecorationRole, getIcon(color));
            }
        }
    }
    else
    {
        ui->infoTextLabel->setText(tr("No Differences Found!"));
    }

    ui->differencesTreeWidget->addTopLevelItems(topItems);
    ui->differencesTreeWidget->expandAll();
}

void DifferencesDockWidget::onSelectionChanged(size_t currentIndex)
{
    if (m_disableChangeSelectedResultIndex)
    {
        return;
    }

    pdf::PDFTemporaryValueChange guard(&m_disableChangeSelectedResultIndex, true);

    QModelIndex index = findResultIndex(currentIndex);
    if (index.isValid())
    {
        ui->differencesTreeWidget->scrollTo(index);
        ui->differencesTreeWidget->setCurrentIndex(index);
    }
}

void DifferencesDockWidget::onCurrentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous)
{
    Q_UNUSED(previous);

    if (m_disableChangeSelectedResultIndex || !current)
    {
        return;
    }

    pdf::PDFTemporaryValueChange guard(&m_disableChangeSelectedResultIndex, true);
    QVariant data = current->data(0, Qt::UserRole);

    if (data.isValid())
    {
        m_diffNavigator->select(data.toULongLong());
    }
}

}   // namespace pdfdocdiff
