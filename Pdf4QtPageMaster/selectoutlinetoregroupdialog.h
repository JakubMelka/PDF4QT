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

#ifndef PDFPAGEMASTER_SELECTOUTLINETOREGROUPDIALOG_H
#define PDFPAGEMASTER_SELECTOUTLINETOREGROUPDIALOG_H

#include "pdfdocument.h"

#include <QDialog>
#include <QModelIndex>

namespace Ui
{
class SelectOutlineToRegroupDialog;
}

namespace pdf
{
class PDFSelectableOutlineTreeItemModel;
}

namespace pdfpagemaster
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

}   // namespace pdfpagemaster

#endif // PDFPAGEMASTER_SELECTOUTLINETOREGROUPDIALOG_H
