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

#ifndef PDFPAGEMASTER_ITEMPROPERTIESDIALOG_H
#define PDFPAGEMASTER_ITEMPROPERTIESDIALOG_H

#include "pageitemmodel.h"

#include <QDialog>
#include <QModelIndex>

namespace Ui
{
class ItemPropertiesDialog;
}

class QLabel;

namespace pdfpagemaster
{

class ItemPropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ItemPropertiesDialog(const PageItemModel* model, const QModelIndex& index, QWidget* parent);
    virtual ~ItemPropertiesDialog() override;

    bool isImageDisplayNameEditable() const { return m_editableImageIndex != -1; }
    int getEditableImageIndex() const { return m_editableImageIndex; }
    QString getImageDisplayName() const;

private:
    QString getTypeText(const PageGroupItem::GroupItem& groupItem) const;
    QString getSourcePathText(const PageGroupItem::GroupItem& groupItem) const;
    QString getImagePixelDimensionsText(const PageGroupItem::GroupItem& groupItem) const;
    QString getPageSizeText(const PageGroupItem::GroupItem& groupItem) const;
    QString getRotationText(pdf::PageRotation rotation) const;
    void setValueLabelText(QLabel* label, const QString& text) const;
    void loadItem(const QModelIndex& index);

    Ui::ItemPropertiesDialog* ui;
    const PageItemModel* m_model;
    int m_editableImageIndex;
};

}   // namespace pdfpagemaster

#endif // PDFPAGEMASTER_ITEMPROPERTIESDIALOG_H
