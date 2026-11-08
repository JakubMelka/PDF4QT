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

#include "pdfappowntabtoolhost.h"

#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappmainwindow.h"
#include "pdfapptoolmodelayout.h"
#include "pdfapptooltab.h"
#include "pdfapptoolundotarget.h"

#include "pdfutils.h"

#include <QEvent>
#include <QMetaObject>
#include <QSplitter>
#include <QVBoxLayout>

namespace pdf
{

PDFAppToolTabView::PDFAppToolTabView(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolTabView"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // The places of the tool mode, one implementation with the document view (4.2). Outside
    // of the mode the canvas area is empty: its bg/track is drawn by the layout.
    m_layout = new PDFAppToolModeLayout(this, layout);
    connect(m_layout.data(), &PDFAppToolModeLayout::partDestroyed, this, &PDFAppToolTabView::onPartDestroyed);
    connect(m_layout.data(), &PDFAppToolModeLayout::infoBarActionTriggered, this, &PDFAppToolTabView::infoBarActionTriggered);

    m_emptyCanvas = new QWidget(m_layout->getCanvasArea());
    m_emptyCanvas->setObjectName(QStringLiteral("emptyCanvas"));
    m_emptyCanvas->setFocusPolicy(Qt::StrongFocus);
    m_layout->setBaseCanvas(m_emptyCanvas);
    m_layout->getSplitter()->installEventFilter(this);

    m_layout->createStatusBar(layout);
}

PDFAppToolTabView::~PDFAppToolTabView()
{
    // The parts of the tool go while the view is whole (they are its children during the
    // mode and they may ask it); they report nothing to the dying view
    m_isToolModeActive = false;
    if (m_undoTarget)
    {
        m_undoTarget->disconnect(this);
    }
    if (m_layout)
    {
        m_layout->disconnect(this);
        for (QWidget* part : { m_layout->getToolbarPart(), m_layout->getLeftPanelPart(), m_layout->getCanvasPagePart(), m_layout->getRightPanelPart() })
        {
            delete part;
        }
    }
}

QWidget* PDFAppToolTabView::getCanvasArea() const
{
    return m_layout ? m_layout->getCanvasArea() : nullptr;
}

PDFAppDocumentStatusBar* PDFAppToolTabView::getStatusBar() const
{
    return m_layout ? m_layout->getStatusBar() : nullptr;
}

bool PDFAppToolTabView::enterToolMode(const PDFAppToolModeParts& parts)
{
    if (m_isToolModeActive || !m_layout)
    {
        return false;
    }

    m_isToolModeActive = true;
    m_hasParts = parts.toolbar || parts.leftPanel || parts.canvasPage || parts.rightPanel || parts.undoTarget;
    m_toolMessage.clear();

    m_undoTarget = parts.undoTarget;
    if (parts.undoTarget)
    {
        connect(parts.undoTarget, &PDFAppToolUndoTarget::undoStateChanged, this, &PDFAppToolTabView::onUndoStateChanged);
        connect(parts.undoTarget, &QObject::destroyed, this, &PDFAppToolTabView::onPartDestroyed);
    }

    m_layout->installParts(parts);
    if (layout())
    {
        layout()->activate();
    }
    m_layout->applySplitterSizes(PDFAppToolModeLayout::DEFAULT_LEFT_WIDTH);
    m_layout->layoutInfoBars();

    // The keys of the mode are bound to the view: a focus outside of it would not reach them
    const QWidget* focusWidget = window() ? window()->focusWidget() : nullptr;
    if (isVisible() && (!focusWidget || !isAncestorOf(focusWidget) || !focusWidget->isVisible()))
    {
        if (QWidget* canvas = m_layout->getCanvasWidget())
        {
            canvas->setFocus(Qt::OtherFocusReason);
        }
    }

    if (PDFAppDocumentStatusBar* statusBar = m_layout->getStatusBar())
    {
        statusBar->showMessage(QString());
    }
    return true;
}

void PDFAppToolTabView::leaveToolMode()
{
    if (!m_isToolModeActive)
    {
        return;
    }

    m_isToolModeActive = false;
    m_toolMessage.clear();
    if (m_undoTarget)
    {
        m_undoTarget->disconnect(this);
    }
    m_undoTarget = nullptr;

    if (m_layout)
    {
        const bool isFocusInPart = m_layout->removeParts();
        if (layout())
        {
            layout()->activate();
        }
        m_layout->layoutInfoBars();
        if (PDFAppDocumentStatusBar* statusBar = m_layout->getStatusBar())
        {
            statusBar->showMessage(QString());
            statusBar->setPageText(QString());
            statusBar->setZoomText(QString());
        }
        if (isFocusInPart && m_emptyCanvas)
        {
            m_emptyCanvas->setFocus(Qt::OtherFocusReason);
        }
    }

    Q_EMIT toolModeChanged(false);
}

PDFAppToolUndoTarget* PDFAppToolTabView::getUndoTarget() const
{
    return m_undoTarget.data();
}

void PDFAppToolTabView::setToolMessage(const QString& text)
{
    if (!m_isToolModeActive)
    {
        return;
    }

    m_toolMessage = text.left(512);
    if (PDFAppDocumentStatusBar* statusBar = getStatusBar())
    {
        statusBar->showMessage(m_toolMessage);
    }
}

void PDFAppToolTabView::setStatusItems(const QString& pageText, const QString& zoomText)
{
    if (PDFAppDocumentStatusBar* statusBar = getStatusBar())
    {
        // The items of the own tab only show their texts (no Go to, no zoom menu: no document)
        statusBar->setPageItemInteractive(false);
        statusBar->setPageText(m_isToolModeActive ? pageText.left(128) : QString());
        statusBar->setZoomText(m_isToolModeActive ? zoomText.left(32) : QString());
    }
}

void PDFAppToolTabView::showTransientMessage(const QString& text, int timeout)
{
    if (PDFAppDocumentStatusBar* statusBar = getStatusBar())
    {
        statusBar->showMessage(text.left(512), timeout);
    }
}

bool PDFAppToolTabView::eventFilter(QObject* watched, QEvent* event)
{
    // The right panel of a tool leaves less to the splitter: the left panel gives its
    // width up in a narrow window, as in the document view
    if (m_isToolModeActive && m_layout && watched == m_layout->getSplitter() && event->type() == QEvent::Resize)
    {
        m_layout->applySplitterSizes(PDFAppToolModeLayout::DEFAULT_LEFT_WIDTH);
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppToolTabView::onPartDestroyed()
{
    // The part may be inside of its destructor: only the fallback is queued
    if (!m_isPartsUpdateQueued)
    {
        m_isPartsUpdateQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppToolTabView::onPartsChangedQueued, Qt::QueuedConnection);
    }
}

void PDFAppToolTabView::onPartsChangedQueued()
{
    m_isPartsUpdateQueued = false;
    if (!m_isToolModeActive)
    {
        return;
    }

    // Nothing of the tool is left: the mode ends (it emits last)
    if (m_hasParts && !m_undoTarget && m_layout && !m_layout->isPartAlive())
    {
        leaveToolMode();
        return;
    }

    if (m_layout)
    {
        m_layout->layoutInfoBars();
    }
    Q_EMIT undoStateChanged();
}

void PDFAppToolTabView::onUndoStateChanged()
{
    Q_EMIT undoStateChanged();
}

// ------------------------------------------------------------------ host

PDFAppOwnTabToolHost::PDFAppOwnTabToolHost(PDFAppToolTab* tab, PDFAppToolTabView* view, QObject* parent) :
    BaseClass(parent),
    m_tab(tab),
    m_view(view)
{
    if (m_view)
    {
        connect(m_view.data(), &PDFAppToolTabView::toolModeChanged, this, &PDFAppOwnTabToolHost::onToolModeChanged);
        connect(m_view.data(), &PDFAppToolTabView::undoStateChanged, this, &PDFAppToolHost::commandStatesChanged);
    }
}

PDFAppOwnTabToolHost::~PDFAppOwnTabToolHost() = default;

PDFAppContext* PDFAppOwnTabToolHost::getContext() const
{
    return m_tab ? m_tab->getContext() : nullptr;
}

PDFAppDocument* PDFAppOwnTabToolHost::getDocument() const
{
    return nullptr;
}

PDFAppDocumentTab* PDFAppOwnTabToolHost::getDocumentTab() const
{
    return nullptr;
}

PDFAppDocumentView* PDFAppOwnTabToolHost::getView() const
{
    return nullptr;
}

PDFWidget* PDFAppOwnTabToolHost::getPdfWidget() const
{
    return nullptr;
}

QWidget* PDFAppOwnTabToolHost::getCanvasArea() const
{
    return m_view ? m_view->getCanvasArea() : nullptr;
}

QWidget* PDFAppOwnTabToolHost::getKeyWidget() const
{
    return m_view.data();
}

void PDFAppOwnTabToolHost::activate()
{
    if (PDFAppMainWindow* window = m_tab ? qobject_cast<PDFAppMainWindow*>(m_tab->window()) : nullptr)
    {
        window->activateTab(m_tab);
    }
}

bool PDFAppOwnTabToolHost::enterMode(const PDFAppToolModeParts& parts)
{
    if (!m_view || !m_view->enterToolMode(parts))
    {
        return false;
    }

    // The sidebar button follows the installed left panel (emitted last: nothing of this
    // host is used after it)
    Q_EMIT commandStatesChanged();
    return true;
}

void PDFAppOwnTabToolHost::leaveMode()
{
    if (m_view)
    {
        // The guard: a slot of the leaving may delete this host
        QPointer<PDFAppOwnTabToolHost> guard(this);
        m_isLeaving = true;
        m_view->leaveToolMode();
        if (guard)
        {
            m_isLeaving = false;
        }
    }
}

bool PDFAppOwnTabToolHost::isModeActive() const
{
    return m_view && m_view->isToolModeActive();
}

void PDFAppOwnTabToolHost::setMessage(const QString& text)
{
    if (m_view)
    {
        m_view->setToolMessage(text);
    }
}

void PDFAppOwnTabToolHost::setStatusItems(const QString& pageText, const QString& zoomText)
{
    if (m_view)
    {
        m_view->setStatusItems(pageText, zoomText);
    }
}

void PDFAppOwnTabToolHost::showTransientMessage(const QString& text)
{
    if (m_view)
    {
        m_view->showTransientMessage(text, MESSAGE_TIMEOUT);
    }
}

void PDFAppOwnTabToolHost::showPanel(const QString& panelId)
{
    // No sidebar of a document
    Q_UNUSED(panelId);
}

void PDFAppOwnTabToolHost::showInfoBar(const PDFAppInfoBarRequest& request)
{
    if (m_view && m_view->getLayout())
    {
        m_view->getLayout()->showInfoBar(request);
    }
}

void PDFAppOwnTabToolHost::hideInfoBar(const QString& id)
{
    if (m_view && m_view->getLayout())
    {
        m_view->getLayout()->hideInfoBar(id);
    }
}

void PDFAppOwnTabToolHost::saveViewState()
{
    // No view of a document: nothing to give back
}

void PDFAppOwnTabToolHost::restoreViewState()
{
}

bool PDFAppOwnTabToolHost::isCommandEnabled(const QString& commandId) const
{
    // The sidebar button shows and hides the left panel of the tool, as in the tool mode of a
    // document (378:1049, 337:1976 and the running state 378:3038 have it enabled)
    if (commandId == PDFAppCommands::toggleSidebar())
    {
        return getLeftPanel() != nullptr;
    }

    // Undo and Redo of the tab act on the tool (the parameter model), not while it runs or
    // while the result dialog is open
    const PDFAppToolUndoTarget* target = m_view ? m_view->getUndoTarget() : nullptr;
    if (!target || m_isBusy)
    {
        return false;
    }
    if (commandId == PDFAppCommands::undo())
    {
        return target->canUndo();
    }
    if (commandId == PDFAppCommands::redo())
    {
        return target->canRedo();
    }
    return false;
}

bool PDFAppOwnTabToolHost::isCommandChecked(const QString& commandId) const
{
    const QWidget* leftPanel = getLeftPanel();
    return commandId == PDFAppCommands::toggleSidebar() && leftPanel && !leftPanel->isHidden();
}

QWidget* PDFAppOwnTabToolHost::getLeftPanel() const
{
    PDFAppToolModeLayout* layout = m_view ? m_view->getLayout() : nullptr;
    return layout ? layout->getLeftPanelPart() : nullptr;
}

void PDFAppOwnTabToolHost::requestCommand(const QString& commandId)
{
    if (!isCommandEnabled(commandId))
    {
        return;
    }

    // The left panel keeps its width: the splitter gives it back when it is shown again
    if (commandId == PDFAppCommands::toggleSidebar())
    {
        QWidget* leftPanel = getLeftPanel();
        leftPanel->setVisible(leftPanel->isHidden());
        return;
    }

    // The target may change the parameters, whose slots may delete the tab
    PDFAppToolUndoTarget* target = m_view->getUndoTarget();
    if (commandId == PDFAppCommands::undo())
    {
        target->undo();
    }
    else
    {
        target->redo();
    }
}

QString PDFAppOwnTabToolHost::getCommandToolTip(const QString& commandId) const
{
    // The title of the command with the shortcut of the user, as the document view says it
    PDFAppContext* context = getContext();
    const PDFAppCommandRegistry::UserShortcuts shortcuts = context && context->getDatabase() ? context->getShortcuts().getShortcuts() : PDFAppCommandRegistry::UserShortcuts();
    return PDFAppDocumentView::formatCommandToolTip(context ? context->getCommandRegistry() : nullptr, commandId, shortcuts, false);
}

PDFAppToolRunEnvironment PDFAppOwnTabToolHost::createEnvironment() const
{
    // No selection and no current page (no document of a view). The color management is
    // null = the default settings: no pointer to an object of a view ever goes to a run (F12).
    // The inputs come from the list of the tab by PDFDocumentToolGUI::prepareEnvironment (R16).
    PDFAppToolRunEnvironment environment;
    environment.authorName = PDFAuthorSettings::getAuthorName();
    return environment;
}

PDFInteger PDFAppOwnTabToolHost::getCurrentPage() const
{
    return -1;
}

std::vector<PDFInteger> PDFAppOwnTabToolHost::getSelectedPages() const
{
    return std::vector<PDFInteger>();
}

PDFAppTab* PDFAppOwnTabToolHost::getTab() const
{
    return m_tab.data();
}

PDFAppToolInputList* PDFAppOwnTabToolHost::getInputList() const
{
    return m_tab ? m_tab->getInputList() : nullptr;
}

bool PDFAppOwnTabToolHost::isOwnTab() const
{
    return true;
}

void PDFAppOwnTabToolHost::setBusy(bool isBusy)
{
    if (m_isBusy != isBusy)
    {
        m_isBusy = isBusy;
        Q_EMIT commandStatesChanged();
    }
}

void PDFAppOwnTabToolHost::onToolModeChanged(bool isActive)
{
    if (!isActive && !m_isLeaving)
    {
        Q_EMIT modeLeft();
    }
}

}   // namespace pdf

#include "moc_pdfappowntabtoolhost.cpp"
