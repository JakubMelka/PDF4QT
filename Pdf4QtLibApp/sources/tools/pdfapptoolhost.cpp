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

#include "pdfapptoolhost.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"

#include "pdfutils.h"
#include "pdfcms.h"

namespace pdf
{

PDFAppToolHost::PDFAppToolHost(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolHost::~PDFAppToolHost() = default;

void PDFAppToolHost::setStatusItems(const QString& pageText, const QString& zoomText)
{
    Q_UNUSED(pageText);
    Q_UNUSED(zoomText);
}

PDFAppTab* PDFAppToolHost::getTab() const
{
    return nullptr;
}

PDFAppToolInputList* PDFAppToolHost::getInputList() const
{
    return nullptr;
}

std::vector<PDFInteger> PDFAppToolHost::getExplicitlySelectedPages() const
{
    return std::vector<PDFInteger>();
}

bool PDFAppToolHost::isOwnTab() const
{
    return false;
}

PDFAppDocumentToolHost::PDFAppDocumentToolHost(PDFAppDocumentTab* tab, QObject* parent) :
    BaseClass(parent),
    m_tab(tab),
    m_view(tab ? tab->getView() : nullptr),
    m_document(tab ? tab->getDocument() : nullptr)
{
    if (m_view)
    {
        connect(m_view.data(), &PDFAppDocumentView::toolModeChanged, this, &PDFAppDocumentToolHost::onToolModeChanged);
        connect(m_view.data(), &PDFAppDocumentView::toolModeLockChanged, this, &PDFAppToolHost::lockChanged);
        connect(m_view.data(), &PDFAppDocumentView::commandStatesChanged, this, &PDFAppToolHost::commandStatesChanged);
        connect(m_view.data(), &PDFAppDocumentView::toolUndoStateChanged, this, &PDFAppToolHost::commandStatesChanged);
        connect(m_view.data(), &PDFAppDocumentView::displayedPageChanged, this, &PDFAppToolHost::pagesChanged);
        connect(m_view.data(), &PDFAppDocumentView::selectedPagesChanged, this, &PDFAppToolHost::pagesChanged);
    }
    if (m_document)
    {
        connect(m_document.data(), &PDFAppDocument::stateChanged, this, &PDFAppDocumentToolHost::onDocumentStateChanged);
    }
}

PDFAppDocumentToolHost::~PDFAppDocumentToolHost() = default;

PDFAppContext* PDFAppDocumentToolHost::getContext() const
{
    return m_tab ? m_tab->getContext() : nullptr;
}

PDFAppDocument* PDFAppDocumentToolHost::getDocument() const
{
    return m_document.data();
}

PDFAppDocumentTab* PDFAppDocumentToolHost::getDocumentTab() const
{
    return m_tab.data();
}

PDFAppDocumentView* PDFAppDocumentToolHost::getView() const
{
    return m_view.data();
}

PDFWidget* PDFAppDocumentToolHost::getPdfWidget() const
{
    return m_view ? m_view->getPdfWidget() : nullptr;
}

QWidget* PDFAppDocumentToolHost::getCanvasArea() const
{
    return m_view ? m_view->getCanvasArea() : nullptr;
}

QWidget* PDFAppDocumentToolHost::getKeyWidget() const
{
    return m_view.data();
}

void PDFAppDocumentToolHost::activate()
{
    if (PDFAppMainWindow* window = m_tab ? qobject_cast<PDFAppMainWindow*>(m_tab->window()) : nullptr)
    {
        window->activateTab(m_tab);
    }
}

bool PDFAppDocumentToolHost::enterMode(const PDFAppToolModeParts& parts)
{
    return m_view && m_view->enterToolMode(parts);
}

void PDFAppDocumentToolHost::leaveMode()
{
    if (m_view)
    {
        // The guard: a slot of the leaving may delete this host
        QPointer<PDFAppDocumentToolHost> guard(this);
        m_isLeaving = true;
        m_view->leaveToolMode();
        if (guard)
        {
            m_isLeaving = false;
        }
    }
}

bool PDFAppDocumentToolHost::isModeActive() const
{
    return m_view && m_view->isToolModeActive();
}

void PDFAppDocumentToolHost::setMessage(const QString& text)
{
    if (m_view)
    {
        m_view->setToolMessage(text);
    }
}

void PDFAppDocumentToolHost::showTransientMessage(const QString& text)
{
    if (m_view && m_view->getStatusBar())
    {
        m_view->getStatusBar()->showMessage(text, MESSAGE_TIMEOUT);
    }
}

void PDFAppDocumentToolHost::showPanel(const QString& panelId)
{
    if (m_view)
    {
        m_view->showPanel(panelId);
    }
}

void PDFAppDocumentToolHost::showInfoBar(const PDFAppInfoBarRequest& request)
{
    if (m_view)
    {
        m_view->showInfoBar(request);
    }
}

void PDFAppDocumentToolHost::hideInfoBar(const QString& id)
{
    if (m_view)
    {
        m_view->hideInfoBar(id);
    }
}

void PDFAppDocumentToolHost::saveViewState()
{
    if (m_view)
    {
        m_savedViewState = m_view->getViewState();
        m_savedPage = m_view->getCurrentPageIndex();
    }
}

void PDFAppDocumentToolHost::restoreViewState()
{
    if (!m_view || !m_savedViewState)
    {
        return;
    }

    const PDFAppViewState state = *m_savedViewState;
    const PDFInteger page = m_savedPage;
    m_savedViewState.reset();

    // Code of the application runs by the signals of the view (a slot may delete the view)
    QPointer<PDFAppDocumentView> view = m_view;
    view->applyViewState(state);
    if (view && page >= 0 && page != view->getCurrentPageIndex())
    {
        view->goToPage(page);
    }
}

bool PDFAppDocumentToolHost::isCommandEnabled(const QString& commandId) const
{
    return m_view && m_view->isCommandEnabled(commandId);
}

bool PDFAppDocumentToolHost::isCommandChecked(const QString& commandId) const
{
    return m_view && m_view->isCommandChecked(commandId);
}

void PDFAppDocumentToolHost::requestCommand(const QString& commandId)
{
    if (m_view)
    {
        m_view->requestCommand(commandId);
    }
}

QString PDFAppDocumentToolHost::getCommandToolTip(const QString& commandId) const
{
    return m_view ? m_view->getCommandToolTip(commandId, false) : QString();
}

PDFAppToolRunEnvironment PDFAppDocumentToolHost::createEnvironment() const
{
    PDFAppToolRunEnvironment environment;
    if (m_view)
    {
        environment.selectedPages = m_view->getSelectedPages();
        environment.currentPage = m_view->getDisplayedPageIndex();

        // A shared immutable copy, never a pointer to the manager of the view (F12)
        if (const PDFCMSManager* manager = m_view->getCMSManager())
        {
            environment.cms = manager->getCurrentCMS();
        }
    }
    environment.authorName = PDFAuthorSettings::getAuthorName();
    return environment;
}

PDFInteger PDFAppDocumentToolHost::getCurrentPage() const
{
    return m_view ? m_view->getDisplayedPageIndex() : -1;
}

std::vector<PDFInteger> PDFAppDocumentToolHost::getSelectedPages() const
{
    return m_view ? m_view->getSelectedPages() : std::vector<PDFInteger>();
}

std::vector<PDFInteger> PDFAppDocumentToolHost::getExplicitlySelectedPages() const
{
    return m_view ? m_view->getExplicitlySelectedPages() : std::vector<PDFInteger>();
}

PDFAppTab* PDFAppDocumentToolHost::getTab() const
{
    return m_tab.data();
}

void PDFAppDocumentToolHost::onToolModeChanged(bool isActive)
{
    if (!isActive && !m_isLeaving)
    {
        Q_EMIT modeLeft();
    }
}

void PDFAppDocumentToolHost::onDocumentStateChanged()
{
    if (m_document && !m_document->isReady())
    {
        Q_EMIT documentNotReady();
    }
}

}   // namespace pdf

#include "moc_pdfapptoolhost.cpp"
