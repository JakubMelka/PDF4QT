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

#include "pdfactioncombobox.h"
#include "pdfwidgetutils.h"

#include <QLineEdit>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QCompleter>

namespace pdfviewer
{

PDFActionComboBox::PDFActionComboBox(QWidget* parent) :
    BaseClass(parent),
    m_model(nullptr)
{
    setEditable(true);
    lineEdit()->setPlaceholderText(tr("Find action..."));
    lineEdit()->setClearButtonEnabled(true);
    setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, DEFAULT_WIDTH));

    m_model = new QStandardItemModel(this);
    m_proxyModel = new QSortFilterProxyModel(this);

    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setDynamicSortFilter(true);
    m_proxyModel->setFilterKeyColumn(0);
    m_proxyModel->setFilterRole(Qt::DisplayRole);
    m_proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setSortLocaleAware(true);
    m_proxyModel->setSortRole(Qt::DisplayRole);
    m_proxyModel->setSourceModel(m_model);

    setFocusPolicy(Qt::StrongFocus);
    setCompleter(nullptr);
    setInsertPolicy(QComboBox::NoInsert);
/*
    completer()->setCompletionMode(QCompleter::PopupCompletion);
    completer()->setCompletionColumn(-1);
    completer()->setFilterMode(Qt::MatchContains | Qt::MatchWildcard);
    completer()->setCaseSensitivity(Qt::CaseInsensitive);
    completer()->setModelSorting(QCompleter::UnsortedModel);*/

    connect(this, &PDFActionComboBox::activated, this, &PDFActionComboBox::onActionActivated);
    connect(this, &PDFActionComboBox::editTextChanged, this, &PDFActionComboBox::onEditTextChanged, Qt::QueuedConnection);

    setModel(m_proxyModel);
}

QSize PDFActionComboBox::sizeHint() const
{
    QSize sizeHint = BaseClass::sizeHint();
    sizeHint.setWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, DEFAULT_WIDTH));
    return sizeHint;
}

QSize PDFActionComboBox::minimumSizeHint() const
{
    QSize sizeHint = BaseClass::minimumSizeHint();
    sizeHint.setWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, DEFAULT_WIDTH));
    return sizeHint;
}

void PDFActionComboBox::addQuickFindAction(QAction* action)
{
    if (std::find(m_actions.begin(), m_actions.end(), action) == m_actions.end())
    {
        m_actions.push_back(action);
        connect(action, &QAction::changed, this, &PDFActionComboBox::onActionChanged);
        updateAction(action);
    }
}

void PDFActionComboBox::onActionChanged()
{
    QAction* action = qobject_cast<QAction*>(sender());
    updateAction(action);
}

void PDFActionComboBox::onActionActivated(int index)
{
    QVariant actionData = itemData(index, Qt::UserRole);
    QAction* action = actionData.value<QAction*>();

    lineEdit()->clear();
    setCurrentIndex(-1);

    if (action && action->isEnabled())
    {
        action->trigger();
    }
}

void PDFActionComboBox::onEditTextChanged(const QString& text)
{
    if (text.isEmpty())
    {
        m_proxyModel->setFilterFixedString(QString());
    }
    else if (text.contains(QChar('*')) || text.contains(QChar('?')))
    {
        m_proxyModel->setFilterWildcard(text);
    }
    else
    {
        m_proxyModel->setFilterFixedString(text);
    }

    if (!text.isEmpty())
    {
        showPopup();
    }
    else
    {
        hidePopup();
    }

    lineEdit()->setFocus();
    lineEdit()->setCursorPosition(text.size());
}

void PDFActionComboBox::updateAction(QAction* action)
{
    if (!action)
    {
        return;
    }

    int actionIndex = findAction(action);
    if (action->isEnabled())
    {
        if (actionIndex == -1)
        {
            QStandardItem* item = new QStandardItem(action->icon(), action->text());
            item->setData(QVariant::fromValue(action), Qt::UserRole);
            m_model->appendRow(item);
        }
        else
        {
            QStandardItem* item = m_model->item(actionIndex);
            item->setIcon(action->icon());
            item->setText(action->text());
        }
    }
    else
    {
        // Remove action from the model
        if (actionIndex != -1)
        {
            m_model->removeRow(actionIndex);
        }
    }

    setCurrentIndex(-1);
}

int PDFActionComboBox::findAction(QAction* action)
{
    const int rowCount = m_model->rowCount();

    for (int i = 0; i < rowCount; ++i)
    {
        QModelIndex index = m_model->index(i, 0);
        QAction* currentAction = index.data(Qt::UserRole).value<QAction*>();

        if (currentAction == action)
        {
            return i;
        }
    }

    return -1;
}

}   // namespace pdfviewer
