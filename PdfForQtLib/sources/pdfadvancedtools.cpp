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
#include "pdfdocumentbuilder.h"
#include "pdfdrawwidget.h"
#include "pdfutils.h"

#include <QActionGroup>
#include <QInputDialog>

namespace pdf
{

PDFCreateStickyNoteTool::PDFCreateStickyNoteTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent) :
    BaseClass(proxy, parent),
    m_toolManager(toolManager),
    m_actionGroup(actionGroup),
    m_pickTool(nullptr),
    m_icon(pdf::TextAnnotationIcon::Comment)
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
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
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

    if (action)
    {
        m_icon = static_cast<TextAnnotationIcon>(action->data().toInt());
    }
}

void PDFCreateStickyNoteTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    bool ok = false;
    QString text = QInputDialog::getText(getProxy()->getWidget(), tr("Sticky note"), tr("Enter text to be displayed in the sticky note"), QLineEdit::Normal, QString(), &ok);

    if (ok && !text.isEmpty())
    {
        PDFDocumentModifier modifier(getDocument());

        QString userName = PDFSysUtils::getUserName();
        PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
        modifier.getBuilder()->createAnnotationText(page, QRectF(pagePoint, QSizeF(0, 0)), m_icon, userName, QString(), text, false);
        modifier.markAnnotationsChanged();

        if (modifier.finalize())
        {
            emit m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }

        setActive(false);
    }
    else
    {
        m_pickTool->resetTool();
    }
}

PDFCreateHyperlinkTool::PDFCreateHyperlinkTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_pickTool(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreateHyperlinkTool::onRectanglePicked);

    updateActions();
}

LinkHighlightMode PDFCreateHyperlinkTool::getHighlightMode() const
{
    return m_highlightMode;
}

void PDFCreateHyperlinkTool::setHighlightMode(const LinkHighlightMode& highlightMode)
{
    m_highlightMode = highlightMode;
}

void PDFCreateHyperlinkTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    bool ok = false;
    QString url = QInputDialog::getText(getProxy()->getWidget(), tr("Hyperlink"), tr("Enter url address of the hyperlink"), QLineEdit::Normal, QString(), &ok);

    if (ok && !url.isEmpty())
    {
        PDFDocumentModifier modifier(getDocument());

        QString userName = PDFSysUtils::getUserName();
        PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
        modifier.getBuilder()->createAnnotationLink(page, pageRectangle, url, m_highlightMode);
        modifier.markAnnotationsChanged();

        if (modifier.finalize())
        {
            emit m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }

        setActive(false);
    }
}

PDFCreateFreeTextTool::PDFCreateFreeTextTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_pickTool(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreateFreeTextTool::onRectanglePicked);

    updateActions();
}

void PDFCreateFreeTextTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    bool ok = false;
    QString text = QInputDialog::getMultiLineText(getProxy()->getWidget(), tr("Text"), tr("Enter text for free text panel"), QString(), &ok);

    if (ok && !text.isEmpty())
    {
        PDFDocumentModifier modifier(getDocument());

        QString userName = PDFSysUtils::getUserName();
        PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
        modifier.getBuilder()->createAnnotationFreeText(page, pageRectangle, PDFSysUtils::getUserName(), QString(), text, TextAlignment(Qt::AlignLeft | Qt::AlignTop));
        modifier.markAnnotationsChanged();

        if (modifier.finalize())
        {
            emit m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }

        setActive(false);
    }
}

PDFCreateAnnotationTool::PDFCreateAnnotationTool(PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent)
{

}

void PDFCreateAnnotationTool::updateActions()
{
    if (QAction* action = getAction())
    {
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
        action->setChecked(isActive());
        action->setEnabled(isEnabled);
    }
}

} // namespace pdf
