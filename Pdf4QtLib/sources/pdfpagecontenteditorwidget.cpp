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
#include "pdfpagecontenteditorstylesettings.h"
#include "ui_pdfpagecontenteditorwidget.h"
#include "pdfwidgetutils.h"
#include "pdfpagecontentelements.h"
#include "pdfutils.h"

#include <QAction>
#include <QToolButton>

namespace pdf
{

PDFPageContentEditorWidget::PDFPageContentEditorWidget(QWidget* parent) :
    QDockWidget(parent),
    ui(new Ui::PDFPageContentEditorWidget),
    m_toolBoxColumnCount(6),
    m_scene(nullptr),
    m_selectionChangeEnabled(true),
    m_updatesEnabled(true)
{
    ui->setupUi(this);

    m_toolButtonIconSize = PDFWidgetUtils::scaleDPI(this, QSize(32, 32));

    for (QToolButton* button : findChildren<QToolButton*>())
    {
        button->setIconSize(m_toolButtonIconSize);
    }

    m_settingsWidget = new PDFPageContentEditorStyleSettings(this);
    ui->appearanceLayout->addWidget(m_settingsWidget);

    m_operationMapper.setMapping(ui->alignVertTopButton, static_cast<int>(PDFPageContentElementManipulator::Operation::AlignTop));
    m_operationMapper.setMapping(ui->alignVertMiddleButton, static_cast<int>(PDFPageContentElementManipulator::Operation::AlignCenterVertically));
    m_operationMapper.setMapping(ui->alignVertBottomButton, static_cast<int>(PDFPageContentElementManipulator::Operation::AlignBottom));
    m_operationMapper.setMapping(ui->alignHorLeftButton, static_cast<int>(PDFPageContentElementManipulator::Operation::AlignLeft));
    m_operationMapper.setMapping(ui->alignHorMiddleButton, static_cast<int>(PDFPageContentElementManipulator::Operation::AlignCenterHorizontally));
    m_operationMapper.setMapping(ui->alignHorRightButton, static_cast<int>(PDFPageContentElementManipulator::Operation::AlignRight));
    m_operationMapper.setMapping(ui->setSameWidthButton, static_cast<int>(PDFPageContentElementManipulator::Operation::SetSameWidth));
    m_operationMapper.setMapping(ui->setSameHeightButton, static_cast<int>(PDFPageContentElementManipulator::Operation::SetSameHeight));
    m_operationMapper.setMapping(ui->setSameSizeButton, static_cast<int>(PDFPageContentElementManipulator::Operation::SetSameSize));
    m_operationMapper.setMapping(ui->centerHorizontallyButton, static_cast<int>(PDFPageContentElementManipulator::Operation::CenterHorizontally));
    m_operationMapper.setMapping(ui->centerVerticallyButton, static_cast<int>(PDFPageContentElementManipulator::Operation::CenterVertically));
    m_operationMapper.setMapping(ui->centerRectButton, static_cast<int>(PDFPageContentElementManipulator::Operation::CenterHorAndVert));
    m_operationMapper.setMapping(ui->layoutHorizontallyButton, static_cast<int>(PDFPageContentElementManipulator::Operation::LayoutHorizontally));
    m_operationMapper.setMapping(ui->layoutVerticallyButton, static_cast<int>(PDFPageContentElementManipulator::Operation::LayoutVertically));
    m_operationMapper.setMapping(ui->layoutFormButton, static_cast<int>(PDFPageContentElementManipulator::Operation::LayoutForm));
    m_operationMapper.setMapping(ui->layoutGridButton, static_cast<int>(PDFPageContentElementManipulator::Operation::LayoutGrid));

    connect(ui->alignVertTopButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));
    connect(ui->alignVertMiddleButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));
    connect(ui->alignVertBottomButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));
    connect(ui->alignHorLeftButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));
    connect(ui->alignHorMiddleButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));
    connect(ui->alignHorRightButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));
    connect(ui->setSameWidthButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));
    connect(ui->setSameHeightButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));
    connect(ui->setSameSizeButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));
    connect(ui->centerHorizontallyButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));
    connect(ui->centerVerticallyButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));
    connect(ui->centerRectButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));
    connect(ui->layoutHorizontallyButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));
    connect(ui->layoutVerticallyButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));
    connect(ui->layoutFormButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));
    connect(ui->layoutGridButton, &QToolButton::clicked, &m_operationMapper, QOverload<>::of(&QSignalMapper::map));

    connect(&m_actionMapper, &QSignalMapper::mappedObject, this, &PDFPageContentEditorWidget::onActionTriggerRequest);
    connect(&m_operationMapper, &QSignalMapper::mappedInt, this, &PDFPageContentEditorWidget::operationTriggered);
    connect(ui->itemsListWidget->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PDFPageContentEditorWidget::onItemSelectionChanged);

    connect(m_settingsWidget, &PDFPageContentEditorStyleSettings::penChanged, this, &PDFPageContentEditorWidget::penChanged);
    connect(m_settingsWidget, &PDFPageContentEditorStyleSettings::brushChanged, this, &PDFPageContentEditorWidget::brushChanged);
    connect(m_settingsWidget, &PDFPageContentEditorStyleSettings::fontChanged, this, &PDFPageContentEditorWidget::fontChanged);
    connect(m_settingsWidget, &PDFPageContentEditorStyleSettings::alignmentChanged, this, &PDFPageContentEditorWidget::alignmentChanged);
    connect(m_settingsWidget, &PDFPageContentEditorStyleSettings::textAngleChanged, this, &PDFPageContentEditorWidget::textAngleChanged);
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
    button->setIconSize(m_toolButtonIconSize);
    m_actionMapper.setMapping(button, action);
    connect(button, &QToolButton::clicked, &m_actionMapper, QOverload<>::of(&QSignalMapper::map));
    connect(action, &QAction::changed, this, &PDFPageContentEditorWidget::onActionChanged);

    ui->toolGroupBoxLayout->addWidget(button, row, column, Qt::AlignCenter);
}

QToolButton* PDFPageContentEditorWidget::getToolButtonForOperation(int operation) const
{
    return qobject_cast<QToolButton*>(m_operationMapper.mapping(operation));
}

void PDFPageContentEditorWidget::updateItemsInListWidget()
{
    if (!m_updatesEnabled)
    {
        return;
    }

    pdf::PDFTemporaryValueChange guard(&m_updatesEnabled, false);
    ui->itemsListWidget->setUpdatesEnabled(false);

    if (m_scene)
    {
        std::set<PDFInteger> presentElementIds;
        std::set<PDFInteger> elementIds = m_scene->getElementIds();

        // Remove items which are not here
        for (int i = 0; i < ui->itemsListWidget->count();)
        {
            QListWidgetItem* item = ui->itemsListWidget->item(i);
            const PDFInteger elementId = item->data(Qt::UserRole).toLongLong();
            if (!elementIds.count(elementId))
            {
                delete ui->itemsListWidget->takeItem(i);
            }
            else
            {
                presentElementIds.insert(elementId);
                ++i;
            }
        }

        // Add items which are here
        for (PDFInteger elementId : elementIds)
        {
            if (presentElementIds.count(elementId))
            {
                continue;
            }

            const PDFPageContentElement* element = m_scene->getElementById(elementId);
            Q_ASSERT(element);

            QListWidgetItem* item = new QListWidgetItem(element->getDescription());
            item->setData(Qt::UserRole, int(elementId));

            ui->itemsListWidget->addItem(item);
        }
    }
    else
    {
        ui->itemsListWidget->clear();
    }

    ui->itemsListWidget->setUpdatesEnabled(true);
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

void PDFPageContentEditorWidget::onItemSelectionChanged()
{
    if (m_selectionChangeEnabled)
    {
        emit itemSelectionChangedByUser();
    }
}

PDFPageContentScene* PDFPageContentEditorWidget::scene() const
{
    return m_scene;
}

void PDFPageContentEditorWidget::setScene(PDFPageContentScene* newScene)
{
    if (m_scene != newScene)
    {
        m_scene = newScene;
        updateItemsInListWidget();
    }
}

std::set<PDFInteger> PDFPageContentEditorWidget::getSelection() const
{
    std::set<PDFInteger> result;

    for (int i = 0; i < ui->itemsListWidget->count(); ++i)
    {
        QListWidgetItem* item = ui->itemsListWidget->item(i);
        if (item->isSelected())
        {
            const PDFInteger elementId = item->data(Qt::UserRole).toLongLong();
            result.insert(elementId);
        }
    }

    return result;
}

void PDFPageContentEditorWidget::setSelection(const std::set<PDFInteger>& selection)
{
    pdf::PDFTemporaryValueChange guard(&m_selectionChangeEnabled, false);

    for (int i = 0; i < ui->itemsListWidget->count(); ++i)
    {
        QListWidgetItem* item = ui->itemsListWidget->item(i);
        const PDFInteger elementId = item->data(Qt::UserRole).toLongLong();
        item->setSelected(selection.count(elementId));
    }
}

void PDFPageContentEditorWidget::loadStyleFromElement(const PDFPageContentElement* element)
{
    m_settingsWidget->loadFromElement(element, false);
}

}   // namespace pdf
