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

#ifndef PDFDOCPAGEORGANIZER_SELECTOUTLINETOREGROUPDIALOG_H
#define PDFDOCPAGEORGANIZER_SELECTOUTLINETOREGROUPDIALOG_H

#include "pdfdocument.h"

#include <QDialog>

namespace Ui
{
class SelectOutlineToRegroupDialog;
}

namespace pdf
{
class PDFSelectableOutlineTreeItemModel;
}

namespace pdfdocpage
{

class SelectOutlineToRegroupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SelectOutlineToRegroupDialog(const pdf::PDFDocument* document, QWidget* parent);
    virtual ~SelectOutlineToRegroupDialog() override;

    std::vector<const pdf::PDFOutlineItem*> getSelectedOutlineItems() const;

private:
    void selectAll();
    void deselectAll();
    void invertSelection();

    void selectLevel1();
    void selectLevel2();

    void selectSubtree();
    void deselectSubtree();

    void onViewContextMenuRequested(const QPoint& pos);

    void manipulateTree(const QModelIndex& index, const std::function<void (QModelIndex)>& manipulator);

    std::function<void (QModelIndex)> createCheckByDepthManipulator(int targetDepth) const;

    Ui::SelectOutlineToRegroupDialog* ui;
    const pdf::PDFDocument* m_document;
    pdf::PDFSelectableOutlineTreeItemModel* m_model;
    QModelIndex m_menuIndex;
};

}   // namespace pdfdocpage

#endif // PDFDOCPAGEORGANIZER_SELECTOUTLINETOREGROUPDIALOG_H
