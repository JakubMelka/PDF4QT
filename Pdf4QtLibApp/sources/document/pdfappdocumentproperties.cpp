// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "pdfappdocumentproperties.h"
#include "pdfappcommand.h"
#include "pdfappdeveloperoverlay.h"
#include "pdfappdocument.h"
#include "pdfappdocumentactions.h"
#include "pdfappdocumentpropertiespage.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfapprenderingissues.h"
#include "pdfapproute.h"

#include <QTimer>

namespace pdf
{

PDFAppDocumentPropertiesCommandHandler::PDFAppDocumentPropertiesCommandHandler(PDFAppDocumentTab* tab) :
    BaseClass(PDFAppDocumentPackage::Properties, tab)
{
    PDFAppDocumentView* view = tab ? tab->getView() : nullptr;
    PDFAppDocument* document = tab ? tab->getDocument() : nullptr;

    m_actionHandler = new PDFAppDocumentActionHandler(tab, this);
    m_renderingIssues = new PDFAppRenderingIssues(view, document, this);
    m_developerOverlay = new PDFAppDeveloperOverlay(tab ? tab->getContext() : nullptr, view, this);

    connect(m_renderingIssues.data(), &PDFAppRenderingIssues::issuesChanged, this, &PDFAppDocumentPropertiesCommandHandler::onIssuesChanged);
    connect(m_renderingIssues.data(), &PDFAppRenderingIssues::statusItemClicked, this, &PDFAppDocumentPropertiesCommandHandler::showRenderingIssues);
}

PDFAppDocumentPropertiesCommandHandler::~PDFAppDocumentPropertiesCommandHandler()
{
    // The parts go before the base (they use the tab, the view and the document)
    delete m_actionHandler.data();
    delete m_renderingIssues.data();
    delete m_developerOverlay.data();
}

PDFAppDocumentActionHandler* PDFAppDocumentPropertiesCommandHandler::getActionHandler() const
{
    return m_actionHandler.data();
}

PDFAppRenderingIssues* PDFAppDocumentPropertiesCommandHandler::getRenderingIssues() const
{
    return m_renderingIssues.data();
}

PDFAppDeveloperOverlay* PDFAppDocumentPropertiesCommandHandler::getDeveloperOverlay() const
{
    return m_developerOverlay.data();
}

void PDFAppDocumentPropertiesCommandHandler::execute(const QString& commandId)
{
    if (commandId == PDFAppCommands::properties())
    {
        showProperties(QStringLiteral("general"));
    }
    else if (commandId == PDFAppCommands::renderingIssues())
    {
        showRenderingIssues();
    }
}

bool PDFAppDocumentPropertiesCommandHandler::isEnabled(const QString& commandId) const
{
    const PDFAppDocument* document = getDocument();
    if (!document || !document->isReady())
    {
        return false;
    }

    if (commandId == PDFAppCommands::properties())
    {
        return true;
    }
    if (commandId == PDFAppCommands::renderingIssues())
    {
        return m_renderingIssues && m_renderingIssues->getIssueCount() > 0;
    }
    return false;
}

void PDFAppDocumentPropertiesCommandHandler::showProperties(const QString& section)
{
    PDFAppDocumentTab* tab = getTab();
    if (!tab || !PDFAppDocumentPropertiesPage::getSectionIds().contains(section))
    {
        return;
    }

    // The page of the tab is replaced by the navigator (the view stays)
    Q_EMIT tab->navigationRequested(PDFAppRoutes::documentProperties(tab->getDocumentId(), section), PDFAppNavigationMode::Push);
}

void PDFAppDocumentPropertiesCommandHandler::showRenderingIssues()
{
    PDFAppDocumentTab* tab = getTab();
    if (!tab || !m_renderingIssues)
    {
        return;
    }

    if (tab->isViewShown())
    {
        m_renderingIssues->showPopover();
        return;
    }

    // From the page Document properties: the document first, then the popover
    // (after the navigation, which may hide and show widgets)
    QPointer<PDFAppDocumentPropertiesCommandHandler> guard(this);
    Q_EMIT tab->navigationRequested(PDFAppRoutes::document(tab->getDocumentId()), PDFAppNavigationMode::Push);
    if (guard)
    {
        QTimer::singleShot(0, this, &PDFAppDocumentPropertiesCommandHandler::onDeferredRenderingIssues);
    }
}

void PDFAppDocumentPropertiesCommandHandler::onDeferredRenderingIssues()
{
    PDFAppDocumentTab* tab = getTab();
    if (tab && tab->isViewShown() && m_renderingIssues)
    {
        m_renderingIssues->showPopover();
    }
}

void PDFAppDocumentPropertiesCommandHandler::onIssuesChanged()
{
    Q_EMIT stateChanged();
}

PDFAppPage* PDFAppDocumentPropertiesPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppDocumentPropertiesPage(context, parent);
}

}   // namespace pdf
