//    Copyright (C) 2022 Jakub Melka
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
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#include "pdfpagecontenteditorwidget.h"
#include "ui_pdfpagecontenteditorwidget.h"

#include <QAction>
#include <QToolButton>

namespace pdf
{

PDFPageContentEditorWidget::PDFPageContentEditorWidget(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::PDFPageContentEditorWidget),
    m_toolBoxColumnCount(6)
{
    ui->setupUi(this);

    connect(&m_actionMapper, &QSignalMapper::mappedObject, this, &PDFPageContentEditorWidget::onActionTriggerRequest);
}

PDFPageContentEditorWidget::~PDFPageContentEditorWidget()
{
    delete ui;
}

void PDFPageContentEditorWidget::addAction(QAction* action)
{
    // First, find position for our action
    int row = 0;
    int column = 0;

    while (true)
    {
        if (!ui->toolGroupBoxLayout->itemAtPosition(row, column))
        {
            break;
        }

        ++column;

        if (column == m_toolBoxColumnCount)
        {
            column = 0;
            ++row;
        }
    }

    QToolButton* button = new QToolButton(this);
    button->setIcon(action->icon());
    button->setText(action->text());
    button->setToolTip(action->toolTip());
    button->setCheckable(action->isCheckable());
    button->setChecked(action->isChecked());
    button->setEnabled(action->isEnabled());
    button->setShortcut(action->shortcut());
    m_actionMapper.setMapping(button, action);
    connect(button, &QToolButton::clicked, &m_actionMapper, QOverload<>::of(&QSignalMapper::map));
    connect(action, &QAction::changed, this, &PDFPageContentEditorWidget::onActionChanged);

    ui->toolGroupBoxLayout->addWidget(button, row, column, Qt::AlignCenter);
}

void PDFPageContentEditorWidget::onActionTriggerRequest(QObject* actionObject)
{
    QAction* action = qobject_cast<QAction*>(actionObject);
    Q_ASSERT(action);

    action->trigger();
}

void PDFPageContentEditorWidget::onActionChanged()
{
    QAction* action = qobject_cast<QAction*>(sender());
    QToolButton* button = qobject_cast<QToolButton*>(m_actionMapper.mapping(action));

    Q_ASSERT(action);
    Q_ASSERT(button);

    button->setChecked(action->isChecked());
    button->setEnabled(action->isEnabled());
}

}   // namespace pdf
