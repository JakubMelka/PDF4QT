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
