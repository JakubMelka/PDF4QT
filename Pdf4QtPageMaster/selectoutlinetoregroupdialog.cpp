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

#include "selectoutlinetoregroupdialog.h"
#include "ui_selectoutlinetoregroupdialog.h"

#include "pdfitemmodels.h"
#include "pdfwidgetutils.h"

#include <QMenu>

namespace pdfpagemaster
{

SelectOutlineToRegroupDialog::SelectOutlineToRegroupDialog(const pdf::PDFDocument* document, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::SelectOutlineToRegroupDialog),
    m_document(document),
    m_model(nullptr)
{
    ui->setupUi(this);

    QIcon bookmarkIcon(":/pdfpagemaster/resources/bookmark.svg");
    m_model = new pdf::PDFSelectableOutlineTreeItemModel(qMove(bookmarkIcon), this);
    ui->outlineView->setModel(m_model);
    ui->outlineView->header()->hide();

    m_model->setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(document), nullptr));
    ui->outlineView->expandToDepth(2);
    ui->outlineView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->outlineView, &QTreeView::customContextMenuRequested, this, &SelectOutlineToRegroupDialog::onViewContextMenuRequested);

    QSize size = pdf::PDFWidgetUtils::scaleDPI(this, QSize(400, 600));
    setMinimumSize(size);
    pdf::PDFWidgetUtils::style(this);
}

SelectOutlineToRegroupDialog::~SelectOutlineToRegroupDialog()
{
    delete ui;
}

std::vector<const pdf::PDFOutlineItem*> SelectOutlineToRegroupDialog::getSelectedOutlineItems() const
{
    return m_model->getSelectedItems();
}

void SelectOutlineToRegroupDialog::onViewContextMenuRequested(const QPoint& pos)
{
    QMenu menu;
    menu.addAction(tr("Select All"), this, &SelectOutlineToRegroupDialog::selectAll);
    menu.addAction(tr("Deselect All"), this, &SelectOutlineToRegroupDialog::deselectAll);
    menu.addAction(tr("Invert Selection"), this, &SelectOutlineToRegroupDialog::invertSelection);

    menu.addSeparator();
    menu.addAction(tr("Select Level 1"), this, &SelectOutlineToRegroupDialog::selectLevel1);
    menu.addAction(tr("Select Level 2"), this, &SelectOutlineToRegroupDialog::selectLevel2);

    QModelIndex index = ui->outlineView->indexAt(pos);
    if (index.isValid())
    {
        m_menuIndex = index;

        menu.addSeparator();
        menu.addAction(tr("Select subtree"), this, &SelectOutlineToRegroupDialog::selectSubtree);
        menu.addAction(tr("Deselect subtree"), this, &SelectOutlineToRegroupDialog::deselectSubtree);
    }
    menu.exec(ui->outlineView->mapToGlobal(pos));
}

void SelectOutlineToRegroupDialog::manipulateTree(const QModelIndex& index,
                                                    const std::function<void (QModelIndex)>& manipulator)
{
    if (index.isValid())
    {
        manipulator(index);
    }

    const int count = m_model->rowCount(index);
    for (int i = 0; i < count; ++i)
    {
        QModelIndex childIndex = m_model->index(i, 0, index);
        manipulateTree(childIndex, manipulator);
    }
}

std::function<void (QModelIndex)> SelectOutlineToRegroupDialog::createCheckByDepthManipulator(int targetDepth) const
{
    auto manipulator = [this, targetDepth](QModelIndex index)
    {
        int depth = 1;
        QModelIndex parentIndex = index.parent();
        while (parentIndex.isValid())
        {
            ++depth;
            parentIndex = parentIndex.parent();
        }

        if (depth == targetDepth)
        {
            m_model->setData(index, Qt::Checked, Qt::CheckStateRole);
        }
    };

    return manipulator;
}

void SelectOutlineToRegroupDialog::selectAll()
{
    manipulateTree(ui->outlineView->rootIndex(), [this](QModelIndex index) { m_model->setData(index, Qt::Checked, Qt::CheckStateRole); });
}

void SelectOutlineToRegroupDialog::deselectAll()
{
    manipulateTree(ui->outlineView->rootIndex(), [this](QModelIndex index) { m_model->setData(index, Qt::Unchecked, Qt::CheckStateRole); });
}

void SelectOutlineToRegroupDialog::invertSelection()
{
    auto manipulator = [this](QModelIndex index)
    {
        const bool isChecked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        m_model->setData(index, isChecked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    };
    manipulateTree(ui->outlineView->rootIndex(), manipulator);
}

void SelectOutlineToRegroupDialog::selectLevel1()
{
    manipulateTree(ui->outlineView->rootIndex(), createCheckByDepthManipulator(1));
}

void SelectOutlineToRegroupDialog::selectLevel2()
{
    manipulateTree(ui->outlineView->rootIndex(), createCheckByDepthManipulator(2));
}

void SelectOutlineToRegroupDialog::selectSubtree()
{
    manipulateTree(m_menuIndex, [this](QModelIndex index) { m_model->setData(index, Qt::Checked, Qt::CheckStateRole); });
}

void SelectOutlineToRegroupDialog::deselectSubtree()
{
    manipulateTree(m_menuIndex, [this](QModelIndex index) { m_model->setData(index, Qt::Unchecked, Qt::CheckStateRole); });
}

}   // namespace pdfpagemaster
