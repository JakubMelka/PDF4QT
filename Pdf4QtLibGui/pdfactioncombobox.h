//    Copyright (C) 2023 Jakub Melka
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
