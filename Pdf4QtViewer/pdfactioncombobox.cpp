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
#include <QAbstractItemView>
#include <QKeyEvent>

namespace pdfviewer
{

PDFActionComboBox::PDFActionComboBox(QWidget* parent) :
    BaseClass(parent),
    m_model(nullptr)
{
    setPlaceholderText(tr("Find action..."));
    setClearButtonEnabled(true);
    setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, DEFAULT_WIDTH));

    m_model = new QStandardItemModel(this);

    QCompleter* completer = new QCompleter(m_model, this);
    setFocusPolicy(Qt::StrongFocus);
    setCompleter(completer);

    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCompletionColumn(0);
    completer->setCompletionRole(Qt::DisplayRole);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
    completer->setWrapAround(false);
    completer->setMaxVisibleItems(20);

    connect(this, &QLineEdit::editingFinished, this, &PDFActionComboBox::performExecuteAction, Qt::QueuedConnection);
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

bool PDFActionComboBox::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride)
    {
        QKeyEvent* keyEvent = dynamic_cast<QKeyEvent*>(event);
        switch (keyEvent->key())
        {
        case Qt::Key_Down:
        case Qt::Key_Up:
            event->accept();
            return true;
        }
    }

    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent* keyEvent = dynamic_cast<QKeyEvent*>(event);

        // Redirect up and down arrows to the completer
        switch (keyEvent->key())
        {
        case Qt::Key_Down:
        case Qt::Key_Up:
        {
            if (completer())
            {
                if (completer()->popup()->isVisible())
                {
                    QCoreApplication::sendEvent(completer()->popup(), event);
                }
                else
                {
                    completer()->complete();
                }
            }
            return true;
        }

        case Qt::Key_Enter:
        case Qt::Key_Return:
            clearFocus();
            return true;

        default:
            break;
        }
    }

    return BaseClass::event(event);
}

void PDFActionComboBox::onActionChanged()
{
    QAction* action = qobject_cast<QAction*>(sender());
    updateAction(action);
}

void PDFActionComboBox::performExecuteAction()
{
    QString text = this->text();

    QAction* action = nullptr;
    for (QAction* currentAction : m_actions)
    {
        if (currentAction->text() == text)
        {
            action = currentAction;
        }
    }

    clear();
    completer()->setCompletionPrefix(QString());

    if (action)
    {
        action->trigger();
    }
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
