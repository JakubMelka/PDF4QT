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

#include "selectbookmarkstoregroupdialog.h"
#include "ui_selectbookmarkstoregroupdialog.h"

#include "pdfitemmodels.h"
#include "pdfwidgetutils.h"

#include <QMenu>

namespace pdfdocpage
{

SelectBookmarksToRegroupDialog::SelectBookmarksToRegroupDialog(const pdf::PDFDocument* document, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::SelectBookmarksToRegroupDialog),
    m_document(document),
    m_model(nullptr)
{
    ui->setupUi(this);

    QIcon bookmarkIcon(":/pdfdocpage/resources/bookmark.svg");
    m_model = new pdf::PDFSelectableOutlineTreeItemModel(qMove(bookmarkIcon), this);
    ui->bookmarksView->setModel(m_model);
    ui->bookmarksView->header()->hide();

    m_model->setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(document), nullptr));
    ui->bookmarksView->expandToDepth(2);
    ui->bookmarksView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->bookmarksView, &QTreeView::customContextMenuRequested, this, &SelectBookmarksToRegroupDialog::onViewContextMenuRequested);

    QSize size = pdf::PDFWidgetUtils::scaleDPI(this, QSize(400, 600));
    setMinimumSize(size);
    pdf::PDFWidgetUtils::style(this);
}

SelectBookmarksToRegroupDialog::~SelectBookmarksToRegroupDialog()
{
    delete ui;
}

std::vector<const pdf::PDFOutlineItem*> SelectBookmarksToRegroupDialog::getSelectedOutlineItems() const
{
    return m_model->getSelectedItems();
}

void SelectBookmarksToRegroupDialog::onViewContextMenuRequested(const QPoint& pos)
{
    QMenu menu;
    menu.addAction(tr("Select All"), this, &SelectBookmarksToRegroupDialog::selectAll);
    menu.addAction(tr("Deselect All"), this, &SelectBookmarksToRegroupDialog::deselectAll);
    menu.addAction(tr("Invert Selection"), this, &SelectBookmarksToRegroupDialog::invertSelection);

    menu.addSeparator();
    menu.addAction(tr("Select Level 1"), this, &SelectBookmarksToRegroupDialog::selectLevel1);
    menu.addAction(tr("Select Level 2"), this, &SelectBookmarksToRegroupDialog::selectLevel2);

    QModelIndex index = ui->bookmarksView->indexAt(pos);
    if (index.isValid())
    {
        m_menuIndex = index;

        menu.addSeparator();
        menu.addAction(tr("Select subtree"), this, &SelectBookmarksToRegroupDialog::selectSubtree);
        menu.addAction(tr("Deselect subtree"), this, &SelectBookmarksToRegroupDialog::deselectSubtree);
    }
    menu.exec(ui->bookmarksView->mapToGlobal(pos));
}

void SelectBookmarksToRegroupDialog::manipulateTree(const QModelIndex& index,
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

std::function<void (QModelIndex)> SelectBookmarksToRegroupDialog::createCheckByDepthManipulator(int targetDepth) const
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

void SelectBookmarksToRegroupDialog::selectAll()
{
    manipulateTree(ui->bookmarksView->rootIndex(), [this](QModelIndex index) { m_model->setData(index, Qt::Checked, Qt::CheckStateRole); });
}

void SelectBookmarksToRegroupDialog::deselectAll()
{
    manipulateTree(ui->bookmarksView->rootIndex(), [this](QModelIndex index) { m_model->setData(index, Qt::Unchecked, Qt::CheckStateRole); });
}

void SelectBookmarksToRegroupDialog::invertSelection()
{
    auto manipulator = [this](QModelIndex index)
    {
        const bool isChecked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        m_model->setData(index, isChecked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    };
    manipulateTree(ui->bookmarksView->rootIndex(), manipulator);
}

void SelectBookmarksToRegroupDialog::selectLevel1()
{
    manipulateTree(ui->bookmarksView->rootIndex(), createCheckByDepthManipulator(1));
}

void SelectBookmarksToRegroupDialog::selectLevel2()
{
    manipulateTree(ui->bookmarksView->rootIndex(), createCheckByDepthManipulator(2));
}

void SelectBookmarksToRegroupDialog::selectSubtree()
{
    manipulateTree(m_menuIndex, [this](QModelIndex index) { m_model->setData(index, Qt::Checked, Qt::CheckStateRole); });
}

void SelectBookmarksToRegroupDialog::deselectSubtree()
{
    manipulateTree(m_menuIndex, [this](QModelIndex index) { m_model->setData(index, Qt::Unchecked, Qt::CheckStateRole); });
}

}   // namespace pdfdocpage
