//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfadvancedtools.h"

#include <QActionGroup>

namespace pdf
{

PDFCreateStickyNoteTool::PDFCreateStickyNoteTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent) :
    BaseClass(proxy, parent),
    m_toolManager(toolManager),
    m_actionGroup(actionGroup),
    m_pickTool(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Points, this);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFCreateStickyNoteTool::onPointPicked);
    connect(m_actionGroup, &QActionGroup::triggered, this, &PDFCreateStickyNoteTool::onActionTriggered);

    updateActions();
}

void PDFCreateStickyNoteTool::updateActions()
{
    BaseClass::updateActions();

    if (m_actionGroup)
    {
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::Modify);
        m_actionGroup->setEnabled(isEnabled);

        if (!isActive() && m_actionGroup->checkedAction())
        {
            m_actionGroup->checkedAction()->setChecked(false);
        }
    }
}

void PDFCreateStickyNoteTool::onActionTriggered(QAction* action)
{
    setActive(action && action->isChecked());
}

void PDFCreateStickyNoteTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{

}

} // namespace pdf
