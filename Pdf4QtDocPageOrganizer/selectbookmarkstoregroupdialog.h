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

#ifndef PDFDOCPAGEORGANIZER_SELECTBOOKMARKSTOREGROUPDIALOG_H
#define PDFDOCPAGEORGANIZER_SELECTBOOKMARKSTOREGROUPDIALOG_H

#include "pdfdocument.h"

#include <QDialog>

namespace Ui
{
class SelectBookmarksToRegroupDialog;
}

namespace pdf
{
class PDFSelectableOutlineTreeItemModel;
}

namespace pdfdocpage
{

class SelectBookmarksToRegroupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SelectBookmarksToRegroupDialog(const pdf::PDFDocument* document, QWidget* parent);
    virtual ~SelectBookmarksToRegroupDialog() override;

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

    Ui::SelectBookmarksToRegroupDialog* ui;
    const pdf::PDFDocument* m_document;
    pdf::PDFSelectableOutlineTreeItemModel* m_model;
    QModelIndex m_menuIndex;
};

}   // namespace pdfdocpage

#endif // PDFDOCPAGEORGANIZER_SELECTBOOKMARKSTOREGROUPDIALOG_H
