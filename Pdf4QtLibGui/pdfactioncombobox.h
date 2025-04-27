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

#ifndef PDFACTIONCOMBOBOX_H
#define PDFACTIONCOMBOBOX_H

#include "pdfwidgetsglobal.h"

#include <QAction>
#include <QComboBox>
#include <QLineEdit>

class QStandardItemModel;
class QSortFilterProxyModel;

namespace pdfviewer
{

class PDFActionComboBox : public QLineEdit
{
    Q_OBJECT

private:
    using BaseClass = QLineEdit;

public:
    PDFActionComboBox(QWidget* parent);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
    virtual bool event(QEvent* event) override;

    void addQuickFindAction(QAction* action);

private:
    static constexpr int DEFAULT_WIDTH = 220;

    void onActionChanged();
    void performExecuteAction();

    void updateAction(QAction* action);
    int findAction(QAction* action);

    std::vector<QAction*> m_actions;
    QStandardItemModel* m_model;
};

}   // namespace pdfviewer

#endif // PDFACTIONCOMBOBOX_H
