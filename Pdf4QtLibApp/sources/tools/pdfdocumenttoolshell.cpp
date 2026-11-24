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

#include "pdfdocumenttoolshell.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentpermissions.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfapppage.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptooldragsource.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptooloutputfiles.h"
#include "pdfdocumenttool_split.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfappundostack.h"
#include "pdfdocumenttoolgui.h"

#include "pdfdocmdppermissions.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolusage.h"
#include "pdfmessagebox.h"
#include "pdfthemefonts.h"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>

namespace pdf
{

/// The question of a navigation during a run (F14): as close-running 955:145813, with
/// the texts of a tool, which is left - the tab stays
class PDFAppToolLeaveRunningDialog : public PDFAppDocumentChoiceDialog
{
public:
    /// \param title Title of the tool
    /// \param document The document of the run
    /// \param percentage Progress of the run
    /// \param parent The window
    explicit PDFAppToolLeaveRunningDialog(const QString& title, const PDFAppDocument* document, int percentage, QWidget* parent) :
        PDFAppDocumentChoiceDialog(PDFThemeIcons::Badge::Warning,
                                   PDFDocumentToolShell::tr("Stop %1 and leave the tool?").arg(title.left(256)),
                                   getText(title, document, percentage),
                                   440, parent)
    {
        setObjectName(QStringLiteral("toolLeaveRunningDialog"));
        addChoice(PDFDocumentToolShell::tr("Stop and Leave"), PDFAppDocumentChoice::Stop, false, true, QStringLiteral("stopAndLeaveButton"));
        addChoice(PDFDocumentToolShell::tr("Keep Running"), PDFAppDocumentChoice::Keep, true, false, QStringLiteral("keepRunningButton"));
    }

    /// The text: the document of the run, or none (the own tab of a tool, plan of step 2 of
    /// phase 2, 4.2: "Merge is still running (37 % done)")
    static QString getText(const QString& title, const PDFAppDocument* document, int percentage)
    {
        if (!document)
        {
            return PDFDocumentToolShell::tr("%1 is still running (%2 % done). Leaving the tool stops it. Nothing has been written yet.").arg(title.left(256), QString::number(percentage));
        }

        // One call: the title of a document is no text of further arguments
        return PDFDocumentToolShell::tr("%1 is still running on %2 (%3 % done). Leaving the tool stops it. The document and the file stay unchanged.")
            .arg(title.left(256), document->getTitle(), QString::number(percentage));
    }
};

/// The tool mode of a host held by a shell (step A, newapp_robustness.md 14.4: a registration outside the subtree of
/// the shell as a token): the parts of the tool in the places of the view and the target of its Undo
/// (PDFAppToolHost::enterMode). The release gives the view back - the parts go back to their holder, the view drops
/// the Undo target of the tool and leaves the mode, then it gets its state back (PDFAppToolHost::restoreViewState). It
/// runs once: by PDFDocumentToolShell::close, by PDFDeferredDeletion::retire of the shell (after the hooks, step 4) or
/// by the destruction of the token. The slots of the view run (a slot may delete the shell with the token).
class PDFAppToolModeToken : public PDFLifetimeToken
{
private:
    using BaseClass = PDFLifetimeToken;

public:
    /// \param owner The shell (the parent of the token)
    /// \param host The host, whose view shows the parts of the tool
    explicit PDFAppToolModeToken(QObject* owner, PDFAppToolHost* host) :
        BaseClass(owner),
        m_host(host)
    {
        setObjectName(QStringLiteral("toolModeToken"));
    }

    virtual ~PDFAppToolModeToken() override
    {
        releaseToken();
    }

protected:
    virtual void release() override
    {
        // The release gives the view back also inside of retire of the shell (step 4): a check of isRetired would
        // skip the state of the view there, the guard stays (PA6)
        QPointer<PDFAppToolModeToken> guard(this);
        if (PDFAppToolHost* host = m_host.data())
        {
            host->leaveMode();
        }
        if (guard && m_host)
        {
            m_host->restoreViewState();
        }
    }

private:
    QPointer<PDFAppToolHost> m_host;
};

/// Helpers of PDFDocumentToolShell
class PDFDocumentToolShellHelper
{
public:
    PDFDocumentToolShellHelper() = delete;

    /// The shell is closed: retired (step A), or its page is closed - replaced by the navigation of its tab and
    /// waiting for its retire from the event loop (PDFAppPage::isClosed). A closed shell asks nothing and opens no
    /// dialog; a call queued before the close, or work going on after foreign code, ends there.
    static bool isClosed(const PDFDocumentToolShell* shell)
    {
        if (PDFDeferredDeletion::isRetired(shell))
        {
            return true;
        }
        for (const QObject* parent = shell->parent(); parent; parent = parent->parent())
        {
            if (const PDFAppPage* page = qobject_cast<const PDFAppPage*>(parent))
            {
                return page->isClosed();
            }
        }
        return false;
    }
};

// ---------------------------------------------------------------- PDFAppToolCompositeUndoTarget

/// Limits of the composite target of Undo of a tool
class PDFAppToolCompositeUndoTargetHelper
{
public:
    PDFAppToolCompositeUndoTargetHelper() = delete;

    /// Most steps of each direction (the model keeps 100, the document its limit; an older step,
    /// which neither of them keeps, could not be taken back anyway)
    static constexpr size_t MAXIMUM_STEPS = 1000;
};

PDFAppToolCompositeUndoTarget::PDFAppToolCompositeUndoTarget(PDFAppToolParameterModel* model, PDFAppDocument* document, QObject* parent) :
    BaseClass(parent),
    m_model(model),
    m_document(document)
{
    if (m_model)
    {
        m_modelStepCount = m_model->getRecordedStepCount();
        connect(m_model.data(), &PDFAppToolUndoTarget::undoStateChanged, this, &PDFAppToolCompositeUndoTarget::onModelUndoStateChanged);
    }
    if (m_document)
    {
        m_documentVersion = m_document->getVersion();
    }
}

PDFAppToolCompositeUndoTarget::~PDFAppToolCompositeUndoTarget() = default;

void PDFAppToolCompositeUndoTarget::recordDocumentStep(quint64 versionAfter)
{
    // A change of somebody else came after the last step of the tool: its earlier steps of the
    // document are no longer on the top of the undo stack of the document
    if (versionAfter != m_documentVersion + 1)
    {
        for (std::vector<Source>* steps : { &m_undoSteps, &m_redoSteps })
        {
            steps->erase(std::remove(steps->begin(), steps->end(), Source::Document), steps->end());
        }
    }

    m_documentVersion = versionAfter;
    m_undoSteps.push_back(Source::Document);
    if (m_undoSteps.size() > PDFAppToolCompositeUndoTargetHelper::MAXIMUM_STEPS)
    {
        m_undoSteps.erase(m_undoSteps.begin());
    }

    // A new step: nothing to do again (the commit has cleared the redo steps of the document)
    m_redoSteps.clear();
    Q_EMIT undoStateChanged();
}

bool PDFAppToolCompositeUndoTarget::isAvailable(Source source, bool isUndo) const
{
    switch (source)
    {
        case Source::Parameters:
            return m_model && (isUndo ? m_model->canUndo() : m_model->canRedo());

        case Source::Document:
        {
            // Only while the top of the undo stack is the last step of the tool (no change of somebody
            // else since) and the document takes changes of the tool (not locked, no blocking operation)
            const PDFAppDocument* document = m_document.data();
            const PDFAppUndoStack* stack = document ? document->getUndoStack() : nullptr;
            if (!stack || document->getVersion() != m_documentVersion || !document->isReady() || document->isLocked() || document->isBlockedByOperation())
            {
                return false;
            }
            return isUndo ? stack->canUndoToolStep() : stack->canRedoToolStep();
        }
    }
    return false;
}

bool PDFAppToolCompositeUndoTarget::isWaitingForDocument(bool isUndo) const
{
    // The step of the tool is still the top of the undo stack of the document (no change of somebody
    // else), the document only does not take changes now (an operation blocking them, its lock)
    const PDFAppDocument* document = m_document.data();
    const PDFAppUndoStack* stack = document ? document->getUndoStack() : nullptr;
    if (!stack || document->getVersion() != m_documentVersion || !(isUndo ? stack->canUndoToolStep() : stack->canRedoToolStep()))
    {
        return false;
    }
    return !document->isReady() || document->isLocked() || document->isBlockedByOperation();
}

int PDFAppToolCompositeUndoTarget::findStep(bool isUndo) const
{
    const std::vector<Source>& steps = isUndo ? m_undoSteps : m_redoSteps;
    for (int i = int(steps.size()) - 1; i >= 0; --i)
    {
        if (isAvailable(steps[size_t(i)], isUndo))
        {
            return i;
        }

        // A step of the document, which waits only for the document, is still the next step (review
        // RS6APP): nothing is done now - an older step is never taken back before it (apply would drop it)
        if (steps[size_t(i)] == Source::Document && isWaitingForDocument(isUndo))
        {
            return -1;
        }
    }
    return -1;
}

bool PDFAppToolCompositeUndoTarget::canUndo() const
{
    return findStep(true) >= 0;
}

bool PDFAppToolCompositeUndoTarget::canRedo() const
{
    return findStep(false) >= 0;
}

void PDFAppToolCompositeUndoTarget::undo()
{
    apply(true);
}

void PDFAppToolCompositeUndoTarget::redo()
{
    apply(false);
}

QString PDFAppToolCompositeUndoTarget::getUndoText() const
{
    const int index = findStep(true);
    if (index < 0)
    {
        return QString();
    }
    if (m_undoSteps[size_t(index)] == Source::Parameters)
    {
        return m_model ? m_model->getUndoText() : QString();
    }
    return m_document ? m_document->getUndoStack()->getUndoDescription() : QString();
}

QString PDFAppToolCompositeUndoTarget::getRedoText() const
{
    const int index = findStep(false);
    if (index < 0)
    {
        return QString();
    }
    if (m_redoSteps[size_t(index)] == Source::Parameters)
    {
        return m_model ? m_model->getRedoText() : QString();
    }
    return m_document ? m_document->getUndoStack()->getRedoDescription() : QString();
}

void PDFAppToolCompositeUndoTarget::apply(bool isUndo)
{
    const int index = findStep(isUndo);
    if (index < 0 || m_isApplying)
    {
        return;
    }

    // The state first: the steps above the found one can never be reached (their model or document
    // step is gone), the step goes to the other direction
    std::vector<Source>& from = isUndo ? m_undoSteps : m_redoSteps;
    std::vector<Source>& to = isUndo ? m_redoSteps : m_undoSteps;
    const Source source = from[size_t(index)];
    from.erase(from.begin() + index, from.end());
    to.push_back(source);

    // The model and the document run their slots (the view, the GUI; a slot may close the tab - step A: retired,
    // deleted later; a retired target tells nothing more)
    m_isApplying = true;
    if (source == Source::Parameters)
    {
        if (isUndo)
        {
            m_model->undo();
        }
        else
        {
            m_model->redo();
        }
    }
    else
    {
        // PDFAppDocument::restore increments the version once: a change of somebody else made by a slot
        // of the document ends the steps of the tool (isAvailable)
        PDFAppUndoStack* stack = m_document->getUndoStack();
        const quint64 version = m_document->getVersion();
        m_documentVersion = version + 1;
        const bool isApplied = isUndo ? stack->undoToolStep() : stack->redoToolStep();
        if (!isApplied)
        {
            to.pop_back();
            m_documentVersion = version;
        }
    }
    if (PDFDeferredDeletion::isRetired(this))
    {
        return;
    }
    m_isApplying = false;
    Q_EMIT undoStateChanged();
}

void PDFAppToolCompositeUndoTarget::onModelUndoStateChanged()
{
    // apply says it at its end
    if (m_isApplying || !m_model)
    {
        return;
    }

    // A new step of the user (a following value and a mapped value are no step)
    const quint64 count = m_model->getRecordedStepCount();
    if (count != m_modelStepCount)
    {
        const quint64 newSteps = qMin<quint64>(count - m_modelStepCount, PDFAppToolParameterModel::MAXIMUM_HISTORY);
        m_modelStepCount = count;
        for (quint64 i = 0; i < newSteps; ++i)
        {
            m_undoSteps.push_back(Source::Parameters);
        }
        while (m_undoSteps.size() > PDFAppToolCompositeUndoTargetHelper::MAXIMUM_STEPS)
        {
            m_undoSteps.erase(m_undoSteps.begin());
        }
        m_redoSteps.clear();
    }
    Q_EMIT undoStateChanged();
}

// ---------------------------------------------------------------- PDFDocumentToolShell

IPDFAppToolUseClock::~IPDFAppToolUseClock() = default;

const IPDFAppToolUseClock* PDFDocumentToolShell::s_useClock = nullptr;

void PDFDocumentToolShell::setUseClock(const IPDFAppToolUseClock* clock)
{
    s_useClock = clock;
}

qint64 PDFDocumentToolShell::getUseClockTime()
{
    if (s_useClock)
    {
        return s_useClock->getMilliseconds();
    }

    // The monotonic clock of the application (started at the first use, only differences are read)
    static QElapsedTimer timer;
    if (!timer.isValid())
    {
        timer.start();
    }
    return timer.elapsed();
}

bool PDFDocumentToolShell::isToolVisible() const
{
    // Only the QWidget part of the watched widget (an event of a tab being torn down may come here, while the
    // host is half destroyed)
    const QWidget* widget = m_visibleKeyWidget.data();
    const QWidget* window = widget ? widget->window() : nullptr;
    return widget && window && widget->isVisible() && !window->isMinimized();
}

void PDFDocumentToolShell::watchVisibility()
{
    QWidget* keyWidget = m_host ? m_host->getKeyWidget() : nullptr;
    if (m_visibleKeyWidget.data() != keyWidget)
    {
        if (QWidget* previous = m_visibleKeyWidget.data())
        {
            previous->removeEventFilter(this);
        }
        m_visibleKeyWidget = keyWidget;
        if (keyWidget)
        {
            // A foreign widget: retire of the shell removes the filter (step A)
            PDFDeferredDeletion::installEventFilter(keyWidget, this, this);
        }
    }
    watchVisibleWindow();
}

void PDFDocumentToolShell::watchVisibleWindow()
{
    // The window of the key widget: its minimizing hides nothing of the tab (a spontaneous Hide at most)
    QWidget* keyWidget = m_visibleKeyWidget.data();
    QWidget* window = keyWidget ? keyWidget->window() : nullptr;
    if (window == keyWidget)
    {
        window = nullptr;
    }
    if (m_visibleWindow.data() != window)
    {
        if (QWidget* previous = m_visibleWindow.data())
        {
            previous->removeEventFilter(this);
        }
        m_visibleWindow = window;
        if (window)
        {
            PDFDeferredDeletion::installEventFilter(window, this, this);
        }
    }
}

void PDFDocumentToolShell::unwatchVisibility()
{
    for (QPointer<QWidget>* watched : { &m_visibleKeyWidget, &m_visibleWindow })
    {
        if (QWidget* widget = watched->data())
        {
            widget->removeEventFilter(this);
        }
        *watched = nullptr;
    }
}

bool PDFDocumentToolShell::isVisibilityEvent(const QObject* watched, const QEvent* event) const
{
    if (!watched || !event)
    {
        return false;
    }

    const QEvent::Type type = event->type();
    if (watched == m_visibleKeyWidget.data())
    {
        // The tab becomes current or not (the stack shows / hides its page with the key widget), the key widget
        // moves into another window
        return type == QEvent::Show || type == QEvent::Hide || type == QEvent::ParentChange;
    }
    if (watched == m_visibleWindow.data())
    {
        return type == QEvent::WindowStateChange || type == QEvent::Show || type == QEvent::Hide;
    }
    return false;
}

void PDFDocumentToolShell::accountVisibleTime()
{
    const qint64 now = getUseClockTime();
    if (m_wasVisible && now > m_lastVisibleCheck)
    {
        m_visibleTime += now - m_lastVisibleCheck;
    }
    m_lastVisibleCheck = now;
    m_wasVisible = m_state == State::Editing || m_state == State::Running || m_state == State::Deciding ? isToolVisible() : false;
}

void PDFDocumentToolShell::markInteractiveUse()
{
    m_isInteractiveUsed = true;
}

void PDFDocumentToolShell::recordInteractiveUse()
{
    // Only an opening, which has been entered and not left yet; never from the destructor (its tab is not whole)
    if (m_isUseRecorded || m_isDestroying || m_state == State::Created || m_state == State::Closed || !m_gui || !m_gui->isInteractive())
    {
        return;
    }

    // Owner 2026-11-20: an action of the user (also a change of a parameter of his), or 10 s of the tool shown;
    // opening and closing at once is no use
    accountVisibleTime();
    const bool isChanged = m_model && m_model->getRecordedStepCount() > 0;
    if (m_isInteractiveUsed || isChanged || m_visibleTime >= INTERACTIVE_USE_VISIBLE_MS)
    {
        // The database notifies its readers from the event loop (PDFDatabase::notifyCommitted)
        recordUse();
    }
}

PDFDocumentToolShell::PDFDocumentToolShell(PDFAppToolHost* host, std::shared_ptr<const PDFDocumentTool> tool, PDFDocumentToolGUI* gui, QObject* parent) :
    BaseClass(parent),
    PDFRetirable(this),
    m_host(host),
    m_tool(std::move(tool)),
    m_gui(gui)
{
    if (m_gui)
    {
        m_gui->setParent(this);
    }
}

PDFDocumentToolShell::~PDFDocumentToolShell()
{
    // The mode ends silently: nobody is asked and nothing is reported any more. A dying shell
    // emits nothing (it dies with its page and tab: a slot of its signal could delete them a
    // second time, the lifetime sweep of the own tab of a tool, step 2 of phase 2). The shell dies with its
    // page, often inside of the destruction of its tab: nothing reads the tab (getContext) or writes the database
    // here - the use of an interactive tool has been recorded before the teardown (close, PDFAppPage::prepareClose)
    blockSignals(true);
    m_isDestroying = true;
    if (m_state != State::Closed && m_state != State::Created)
    {
        m_isCloseRequested = true;
        close();
    }

    deleteParts();

    // The GUI goes first: it uses the model and the runner. Each pointer is cleared first:
    // a slot of the end of an object may ask this shell (the destructor runs only once).
    PDFDocumentToolGUI* gui = m_gui.data();
    m_gui = nullptr;
    delete gui;
    PDFDocumentToolRunner* runner = m_runner.data();
    m_runner = nullptr;
    if (runner)
    {
        runner->setParent(nullptr);
    }
    delete runner;
    PDFAppToolCompositeUndoTarget* undoTarget = m_undoTarget.data();
    m_undoTarget = nullptr;
    delete undoTarget;
    PDFAppToolParameterModel* model = m_model.data();
    m_model = nullptr;
    delete model;
}

PDFAppToolUndoTarget* PDFDocumentToolShell::getUndoTarget() const
{
    if (m_undoTarget)
    {
        return m_undoTarget.data();
    }
    return m_model.data();
}

const PDFDocumentToolCatalogEntry* PDFDocumentToolShell::getCatalogEntry() const
{
    return m_tool ? m_tool->getInfo() : nullptr;
}

bool PDFDocumentToolShell::enter()
{
    // A retired shell (its page or tab closed) enters nothing and creates nothing (step A)
    if (m_state != State::Created || !m_host || !m_tool || !m_gui || !getCatalogEntry() || PDFDeferredDeletion::isRetired(this))
    {
        return false;
    }

    PDFAppDocument* document = m_host->getDocument();

    m_model = new PDFAppToolParameterModel(m_host->getContext(), m_tool->getId(), m_tool->getParameters(), this);
    connect(m_model.data(), &PDFAppToolParameterModel::changed, this, &PDFDocumentToolShell::onParameterChanged);

    m_runner = new PDFDocumentToolRunner(document, m_tool, this);
    connect(m_runner.data(), &PDFDocumentToolRunner::previewProgress, this, &PDFDocumentToolShell::onPreviewProgress);
    connect(m_runner.data(), &PDFDocumentToolRunner::previewFinished, this, &PDFDocumentToolShell::onPreviewFinished);
    connect(m_runner.data(), &PDFDocumentToolRunner::previewDropped, this, &PDFDocumentToolShell::onPreviewDropped);
    connect(m_runner.data(), &PDFDocumentToolRunner::runStarted, this, &PDFDocumentToolShell::onRunStarted);
    connect(m_runner.data(), &PDFDocumentToolRunner::runProgress, this, &PDFDocumentToolShell::onRunProgress);
    connect(m_runner.data(), &PDFDocumentToolRunner::runFinished, this, &PDFDocumentToolShell::onRunFinished);
    connect(m_runner.data(), &PDFDocumentToolRunner::documentLost, this, &PDFDocumentToolShell::onDocumentLost);
    connect(m_runner.data(), &PDFDocumentToolRunner::fileQuestionChanged, this, &PDFDocumentToolShell::onFileQuestionChanged);

    m_gui->attach(this);
    connect(m_gui.data(), &PDFDocumentToolGUI::confirmStateChanged, this, &PDFDocumentToolShell::onConfirmStateChanged);

    // A tool, which changes the document of its tab (Redact Content), has its steps of the document
    // in one Undo order with the parameters; the own tab has no document of its own
    if (document && !m_host->isOwnTab() && m_gui->hasDocumentUndoSteps())
    {
        m_undoTarget = new PDFAppToolCompositeUndoTarget(m_model, document, this);
    }
    connect(m_gui.data(), &PDFDocumentToolGUI::statusTextChanged, this, &PDFDocumentToolShell::onWorkChanged);
    connect(m_gui.data(), &PDFDocumentToolGUI::ownTabStateChanged, this, &PDFDocumentToolShell::onWorkChanged);
    connect(m_gui.data(), &PDFDocumentToolGUI::statusTextChanged, this, &PDFDocumentToolShell::updateStatus);

    m_isPermissionRefused = isRefusedByPermissions();

    // The parts wait in a hidden holder, until the view takes them (and it gives them back). The holder has no
    // parent and the parts live in the layout of the view during the mode: they belong to the shell logically
    // (step A, owner decision: the tool mode parts belong to the shell, the view holds them only by QPointer) -
    // retire of the shell retires them with it, their connections into the GUI and the shell stay internal
    m_partsParent = new QWidget();
    m_partsParent->setObjectName(QStringLiteral("toolPartsHolder"));
    PDFDeferredDeletion::setOwner(m_partsParent, this);
    m_toolbar = new PDFAppToolToolbar(m_host, *getCatalogEntry(), m_partsParent);
    PDFDeferredDeletion::setOwner(m_toolbar, this);
    connect(m_toolbar.data(), &PDFAppToolToolbar::cancelClicked, this, &PDFDocumentToolShell::cancel);
    connect(m_toolbar.data(), &PDFAppToolToolbar::confirmClicked, this, &PDFDocumentToolShell::confirm);
    connect(m_toolbar.data(), &PDFAppToolToolbar::stopClicked, this, &PDFDocumentToolShell::stop);
    m_toolbar->installEventFilter(this);

    createContent();
    updateCertificationCallout(false);

    PDFAppToolModeParts parts;
    parts.toolbar = m_toolbar;
    parts.leftPanel = m_leftPanel;
    parts.canvasPage = m_canvasPage;
    parts.rightPanel = m_rightPanel;
    parts.undoTarget = getUndoTarget();

    m_host->saveViewState();

    const bool isEntered = m_host->enterMode(parts);
    if (PDFDeferredDeletion::isRetired(this))
    {
        // A slot of the entry has closed the page or the tab of the tool (step A): the hook has run already, nothing
        // more is created; a mode entered meanwhile is given back at once (no token of the mode is made). The view is
        // given back on purpose by this retired shell: a check of isRetired after the call would skip its state, the
        // guard stays (PA6)
        m_state = State::Closed;
        if (isEntered && m_host)
        {
            QPointer<PDFDocumentToolShell> guard(this);
            m_isLeavingMode = true;
            m_host->leaveMode();
            if (!guard)
            {
                return false;
            }
            m_isLeavingMode = false;
            if (m_host)
            {
                m_host->restoreViewState();
            }
        }
        return false;
    }
    if (!isEntered)
    {
        m_state = State::Closed;
        return false;
    }

    // The mode is a registration in the view of the host (the parts, the Undo target): a token of the shell ends it
    m_modeToken = new PDFAppToolModeToken(this, m_host);
    m_state = State::Editing;
    m_context = m_host->getContext();

    // An interactive tool counts the time it is shown (owner 2026-11-20): from the events, which change its
    // visibility (the event filter), never by a timer
    if (m_gui->isInteractive())
    {
        watchVisibility();
        m_lastVisibleCheck = getUseClockTime();
        m_wasVisible = isToolVisible();
    }
    // The host, the document and the list of the inputs are outside of the subtree of the shell (step A): their
    // connections are cut by retire of the shell (close disconnects the host as before)
    PDFDeferredDeletion::connect(m_host.data(), &PDFAppToolHost::modeLeft, this, &PDFDocumentToolShell::onModeLeft);
    PDFDeferredDeletion::connect(m_host.data(), &PDFAppToolHost::documentNotReady, this, &PDFDocumentToolShell::onDocumentLost);
    PDFDeferredDeletion::connect(m_host.data(), &PDFAppToolHost::lockChanged, this, &PDFDocumentToolShell::onLockChanged);
    if (document)
    {
        PDFDeferredDeletion::connect(document, &PDFAppDocument::documentChanged, this, &PDFDocumentToolShell::onDocumentChanged);
        m_knownDocumentVersion = document->getVersion();
    }

    // The inputs of a tool of more inputs in its own tab (K33): a change of their content
    // gives a new generation, the preview is computed again
    if (PDFAppToolInputList* inputList = m_host->getInputList())
    {
        PDFDeferredDeletion::connect(inputList, &PDFAppToolInputList::inputsChanged, this, &PDFDocumentToolShell::onInputsChanged);
        PDFDeferredDeletion::connect(inputList, &PDFAppToolInputList::inputChanged, this, &PDFDocumentToolShell::onInputsChanged);
    }

    // The keys of the tool: the actions of the view reach them from every part of the tool
    if (QWidget* keyWidget = m_host->getKeyWidget())
    {
        m_escapeAction = new QAction(this);
        m_escapeAction->setObjectName(QStringLiteral("toolEscape"));
        m_escapeAction->setShortcut(QKeySequence(Qt::Key_Escape));
        m_escapeAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(m_escapeAction.data(), &QAction::triggered, this, &PDFDocumentToolShell::onEscape);
        keyWidget->addAction(m_escapeAction);

        m_confirmAction = new QAction(this);
        m_confirmAction->setObjectName(QStringLiteral("toolConfirm"));
        m_confirmAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
        m_confirmAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(m_confirmAction.data(), &QAction::triggered, this, &PDFDocumentToolShell::confirm);
        keyWidget->addAction(m_confirmAction);

        m_confirmKeypadAction = new QAction(this);
        m_confirmKeypadAction->setObjectName(QStringLiteral("toolConfirmKeypad"));
        m_confirmKeypadAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Enter));
        m_confirmKeypadAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(m_confirmKeypadAction.data(), &QAction::triggered, this, &PDFDocumentToolShell::confirm);
        keyWidget->addAction(m_confirmKeypadAction);
    }

    // The canvas area raises what it shows: the overlay lies over the canvas of the mode
    if (m_overlay)
    {
        m_overlay->raise();
        m_overlay->show();
    }

    const QString panel = m_gui->getInitialSidebarPanel();
    if (!panel.isEmpty())
    {
        m_host->showPanel(panel);
        if (PDFDeferredDeletion::isRetired(this))
        {
            // Retired by a slot of the panel: its hook and the token of the mode have ended the tool
            return false;
        }
    }

    updateToolbar();
    updateStatus();
    Q_EMIT stateChanged(m_state);
    if (PDFDeferredDeletion::isRetired(this))
    {
        // Retired by a slot of the state (the tool was entered and is closed by its page): no preview
        return true;
    }

    if (m_isPermissionRefused)
    {
        // The window shows the new page first; a modal dialog waits for the event loop
        QMetaObject::invokeMethod(this, &PDFDocumentToolShell::showPermissionRefusal, Qt::QueuedConnection);
    }
    else
    {
        updatePreview();
    }

    // Entered (also when a slot of the preview has closed it meanwhile, as after the state above)
    return true;
}

void PDFDocumentToolShell::createContent()
{
    if (!m_gui || !m_partsParent || !m_toolbar)
    {
        return;
    }

    // The toolbar keeps the place of the actions
    QWidget* actions = m_gui->createToolActions(m_toolbar);
    m_toolbar->setToolActions(actions);
    m_toolActions = actions;

    const int spacing = m_gui->getRightPanelSpacing();
    const bool isEntering = m_state == State::Created;
    if (isEntering)
    {
        // The panels move into the layout of the view: they belong to the shell logically (see enter)
        m_rightPanel = new PDFAppToolSidePanel(PDFAppToolSidePanel::Side::Right, spacing, m_partsParent);
        m_rightPanel->setObjectName(QStringLiteral("toolRightPanel"));
        PDFDeferredDeletion::setOwner(m_rightPanel, this);
        m_leftPanel = new PDFAppToolSidePanel(PDFAppToolSidePanel::Side::Left, 10, m_partsParent);
        m_leftPanel->setObjectName(QStringLiteral("toolLeftPanel"));
        PDFDeferredDeletion::setOwner(m_leftPanel, this);
    }

    // A panel without a content is no part of the tool (the view keeps its place)
    if (m_rightPanel)
    {
        QWidget* rightContent = m_gui->createRightPanel(m_rightPanel->getContentParent());
        m_rightPanel->setContent(rightContent);
        m_rightContent = rightContent;
        if (!rightContent && isEntering)
        {
            delete m_rightPanel.data();
        }
    }
    if (m_leftPanel)
    {
        QWidget* leftContent = m_gui->createLeftPanel(m_leftPanel->getContentParent());
        m_leftPanel->setContent(leftContent);
        m_leftContent = leftContent;
        if (!leftContent && isEntering)
        {
            delete m_leftPanel.data();
        }
    }

    if (isEntering)
    {
        // The canvas page moves into the canvas area of the view and the overlay is a child of that area from its
        // start: both belong to the shell logically (see enter)
        m_canvasPage = m_gui->createCanvasPage(m_partsParent);
        PDFDeferredDeletion::setOwner(m_canvasPage, this);
        if (m_host && m_host->getCanvasArea())
        {
            m_overlay = m_gui->createOverlay(m_host->getCanvasArea());
            PDFDeferredDeletion::setOwner(m_overlay, this);
        }
    }
}

void PDFDocumentToolShell::close()
{
    if (m_state == State::Closed)
    {
        return;
    }

    // Every way of leaving, while the tab is whole, ends here (Cancel, Esc, Discard, another route, a lock, a lost
    // document); the close of the tab and the quit record by PDFAppPage::prepareClose, the destructor never
    recordInteractiveUse();
    m_state = State::Closed;
    unwatchVisibility();

    for (const QPointer<QAction>& action : { m_escapeAction, m_confirmAction, m_confirmKeypadAction })
    {
        if (action)
        {
            action->setEnabled(false);
        }
    }

    if (m_host)
    {
        m_host->disconnect(this);
        m_host->hideInfoBar(QStringLiteral("tool-result"));
    }
    if (m_gui)
    {
        m_gui->disconnect(this);
    }

    // The dialog and the run end first: the operation frees the document
    if (m_resultDialog)
    {
        m_resultDialog->disconnect(this);
    }
    m_pendingResult.reset();
    m_isWaitingForModal = false;
    watchModalDialog(nullptr);
    if (PDFAppToolResultWriter* writer = m_skippedWriter.data())
    {
        // The write of a result without the dialog: its late bytes go nowhere
        m_skippedWriter = nullptr;
        writer->disconnect(this);
        writer->deleteLater();
    }
    if (m_leaveDialog)
    {
        // The question of a run, which has ended without the user: it goes as Keep Running (the leave, which waits for
        // it, is refused; its waiter runs code of the application)
        m_leaveDialog->reject();
        if (PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }
    QDialog* openQuestion = isCloseQuestionOpen() ? m_closeQuestion.data() : nullptr;
    m_closeQuestion = nullptr;
    if (openQuestion && !m_isDestroying)
    {
        // The question of the unsaved work of a tool, which has ended without the user (a lock, a lost document):
        // it goes as its Keep (the GUI's slots run, a leave waiting for it goes on); an answered one is left alone;
        // the destructor of the GUI closes it in the destructor of the shell
        openQuestion->reject();
        if (PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }
    if (m_permissionDialog)
    {
        // Closed without its answer: retired (the non-virtual close of retire, no finished), deleted later
        m_permissionDialog->disconnect(this);
        PDFDeferredDeletion::retire(m_permissionDialog.data());
    }

    // The close of the tab during the question of a file = Stop Export (R3): the end of the
    // runner cancels the run, which stops the sink and wakes the waiting thread
    closeFileConflictDialog();
    if (PDFAppToolResultDialog* dialog = m_resultDialog.data(); dialog && dialog->isVisible())
    {
        // The result has no runner any more: the open dialog closes now without its answer, as it did with the
        // deletion of the runner, whose end it watches (a decided dialog is hidden already and left alone)
        PDFDeferredDeletion::retire(dialog);
    }
    if (m_runner)
    {
        // Retired, never deleted here (step A): its work ends at once - the workers are canceled and the operation of
        // the document ends (the document is free when close returns); its object lives until the event loop, so a
        // close from its own signal finds it whole. The slots of the end of the operation may close this shell: the
        // pointer first. Inside of an emission of the runner (a slot of this shell connected to it runs; RA-L7) the
        // runner is retired from the event loop: retire now would cut the later receivers of the emission (the GUI).
        // QObject::sender reads the connections: before they are cut.
        PDFDocumentToolRunner* runner = m_runner.data();
        const bool isInsideRunnerEmission = sender() == runner;
        m_runner->disconnect(this);
        m_runner = nullptr;
        if (isInsideRunnerEmission)
        {
            runner->shutDown();
            PDFDeferredDeletion::retireLater(runner);
        }
        else
        {
            PDFDeferredDeletion::retire(runner);
        }
        if (PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }
    if (QWidget* overlay = m_overlay.data())
    {
        // Retired, never deleted here: close may run from the input of the overlay itself (Esc, a button of it) and
        // its code goes on after this call (the ASan finding of step 7c); it is hidden and inert at once
        m_overlay = nullptr;
        PDFDeferredDeletion::retire(overlay);
    }

    if (PDFLifetimeToken* modeToken = m_modeToken.data())
    {
        // The token of the mode gives the view the parts and its state back (a slot may close this shell: retired, it
        // reports nothing)
        m_isLeavingMode = true;
        modeToken->releaseToken();
        if (PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
        m_isLeavingMode = false;
    }
    Q_EMIT stateChanged(State::Closed);
}

void PDFDocumentToolShell::deleteParts()
{
    // The parts back in the holder go with it; a part still in the view (the view has
    // not given it back) is deleted alone
    const QPointer<QWidget> parts[] = { m_toolbar.data(), m_leftPanel.data(), m_rightPanel.data(), m_canvasPage.data(), m_overlay.data(), m_partsParent.data() };
    m_toolbar = nullptr;
    m_leftPanel = nullptr;
    m_rightPanel = nullptr;
    m_canvasPage = nullptr;
    m_overlay = nullptr;
    m_partsParent = nullptr;
    for (const QPointer<QWidget>& part : parts)
    {
        // The view is just moving the parts back to their holder, while a slot deletes this
        // shell: Qt does not survive the deletion of the new parent inside of setParent
        if (m_isLeavingMode && part)
        {
            part->deleteLater();
        }
        else
        {
            delete part.data();
        }
    }
}

void PDFDocumentToolShell::setState(State state)
{
    if (m_state == state)
    {
        return;
    }

    m_state = state;
    updateToolbar();
    updateStatus();
    Q_EMIT stateChanged(state);
}

void PDFDocumentToolShell::updateToolbar()
{
    if (!m_toolbar || !m_gui)
    {
        return;
    }

    // K6.11: a tool without the Primary button (Manage Certificates) has none in any mode
    m_toolbar->setConfirmShown(m_gui->isConfirmShown());
    m_toolbar->setUndoShown(m_gui->isUndoShown());
    switch (m_state)
    {
        case State::Running:
            m_toolbar->setMode(PDFAppToolToolbar::Mode::Running);
            break;
        case State::Deciding:
            // The export of the data of the tool without a run (Measure 473:91038, 473:91414: Save as Annotations stays
            // Default while temporary measurements exist) keeps the Primary button of the GUI; after a run it is
            // Disabled (R7-2)
            m_toolbar->setMode(m_isExportDialog && !m_isRunExportDialog ? PDFAppToolToolbar::Mode::Editing : PDFAppToolToolbar::Mode::Finished);
            break;
        default:
            m_toolbar->setMode(PDFAppToolToolbar::Mode::Editing);
            break;
    }

    m_toolbar->setConfirmText(m_gui->getConfirmText(), m_gui->getConfirmIconName());

    // R7-2 (owner 9. 11.): a result kept in the tool disables the Primary button until it is stale
    m_toolbar->setConfirmEnabled(!m_isPermissionRefused && m_gui->isConfirmEnabled() && m_resultState != PDFAppToolResultState::Current);
    m_toolbar->setCancelText(m_gui->getCancelText());
}

QString PDFDocumentToolShell::getStatusMessage() const
{
    if (!m_gui)
    {
        return QString();
    }

    switch (m_state)
    {
        case State::Running:
            return m_gui->getRunningStatusText(m_stepText);
        case State::Deciding:
            return m_resultDialog ? m_resultDialog->getStatusText() : QString();
        default:
            return m_gui->getStatusText();
    }
}

void PDFDocumentToolShell::updateStatus()
{
    if (m_host && m_state != State::Closed && m_state != State::Created)
    {
        m_host->setMessage(getStatusMessage());

        // The own tab has no view: its page and zoom items come from the GUI (K12). In the
        // document host a page text of the GUI replaces the page item of the view (Manage
        // Pages "12 pages", 1479:34632); the other tools have none and keep "Page 1 / 10". A tool
        // with its own items replaces both (K7.27 of step 7: Statistics none, Output Preview its zoom).
        if (m_gui && m_host)
        {
            m_host->setStatusItems(m_gui->getOwnTabPageText(), m_gui->getOwnTabZoomText(), m_gui->hasOwnStatusItems());
        }
    }
}

void PDFDocumentToolShell::applyEnvironment()
{
    if (m_runner && m_host && m_gui)
    {
        PDFAppToolRunEnvironment environment = m_host->createEnvironment();
        m_gui->prepareEnvironment(&environment);
        m_runner->setEnvironment(environment);

        // K33: the content of the inputs is a part of the key of the previews
        if (const PDFAppToolInputList* inputList = m_host->getInputList())
        {
            m_runner->setInputGeneration(inputList->getGeneration());
        }
    }
}

void PDFDocumentToolShell::refreshPreview()
{
    if (m_state == State::Editing)
    {
        updatePreview();
    }
}

void PDFDocumentToolShell::resumeEditing()
{
    if (m_state == State::Editing)
    {
        m_isCloseRequested = false;
        return;
    }

    // A decided result, whose close the page has refused (the written file of Save as a file
    // has not opened in the own tab): the tool stays with its inputs and its parameters
    if (m_state == State::Deciding && m_isCloseRequested)
    {
        m_isCloseRequested = false;
        returnToEditing();
    }
}

void PDFDocumentToolShell::onInputsChanged()
{
    // The GUI rebuilds its parts by its own connections to the list; the shell says the
    // Primary button, the status and a new preview (a shell retired by their slots computes nothing)
    updateToolbar();
    updateStatus();
    if (PDFDeferredDeletion::isRetired(this))
    {
        return;
    }
    if (m_state == State::Editing)
    {
        updatePreview();
    }

    // The title and the dot of the own tab follow the list (K16); the tab only reads
    if (m_gui && !PDFDeferredDeletion::isRetired(this))
    {
        m_gui->updateOwnTabState();
    }
}

void PDFDocumentToolShell::updatePreview()
{
    if (!m_tool || !m_tool->hasPreview() || !m_runner || !m_gui || m_isPermissionRefused || m_state == State::Closed)
    {
        return;
    }

    applyEnvironment();
    m_runner->requestPreview(m_gui->getPreviewParameters());
}

void PDFDocumentToolShell::confirm()
{
    // Ctrl+Enter inside of an open panel (see onEscape): the panel closes first
    if (QWidget* popup = QApplication::activePopupWidget())
    {
        popup->close();
    }

    if (m_state != State::Editing || !m_gui || !m_runner || !m_host || m_isPermissionRefused || !m_gui->isConfirmShown() || !m_gui->isConfirmEnabled() || m_runPermissionDialog ||
        m_resultState == PDFAppToolResultState::Current)
    {
        return;
    }

    // A Primary button, which never runs the tool (Keep Proofing On, RS7APP L4): the GUI takes it, nothing of a
    // run follows (the last call - its action may close the tool)
    if (m_gui->confirmWithoutRun())
    {
        return;
    }

    // A permission, which only the chosen values need (Remove Hidden Data), is asked as every
    // other action asks it (owner 2026-11-08): the owner password dialog of the shared check, or
    // its refusal; the user confirms again after the unlock. The dialog runs no nested event
    // loop, the function ends right after it is opened (the shell is checked after the foreign
    // code of the dialog anyway; the dialog is owned by its window and deletes itself on close). A shell retired by
    // foreign code (step A) runs nothing.
    const std::optional<PDFAppPermissionRequest> runRequest = getRunPermissionRequest();
    PDFAppDocument* permissionDocument = m_host->getDocument();
    if (runRequest && permissionDocument)
    {
        QWidget* parent = m_host->getKeyWidget() ? m_host->getKeyWidget()->window() : nullptr;
        QDialog* permissionDialog = nullptr;
        const bool isAllowed = PDFAppDocumentPermissions::request(permissionDocument, *runRequest, parent, &permissionDialog);
        if (PDFDeferredDeletion::isRetired(this) || !m_host || !m_runner || m_state != State::Editing)
        {
            return;
        }
        if (!isAllowed)
        {
            m_runPermissionDialog = permissionDialog;
            return;
        }
    }

    m_host->hideInfoBar(QStringLiteral("tool-result"));

    // An open editor is a change of the document (F4 has committed it at the entry, the
    // model of an application with another way in may have one)
    if (PDFAppDocument* document = m_host->getDocument())
    {
        document->commitPendingEdits();
        if (PDFDeferredDeletion::isRetired(this) || !m_runner || !m_host || m_state != State::Editing)
        {
            return;
        }
    }

    // A value being typed is a value of the run: Ctrl+Enter does not move the focus
    commitPendingValues();
    if (PDFDeferredDeletion::isRetired(this) || !m_runner || !m_host || !m_gui || m_state != State::Editing || !m_gui->isConfirmEnabled())
    {
        return;
    }

    // The environment may refuse the run (Encrypt with Certificate: the password does not open the
    // chosen .pfx file, 433:3972 - the GUI shows it at its field and the Primary button is Disabled)
    applyEnvironment();
    if (PDFDeferredDeletion::isRetired(this) || !m_runner || !m_host || !m_gui)
    {
        return;
    }
    if (!m_gui->isConfirmEnabled())
    {
        // The values prepared for the run are dropped (S5.1: the password of a .pfx file)
        m_gui->onRunRefused();
        return;
    }
    if (!m_runner->run(m_gui->getRunParameters()) && !PDFDeferredDeletion::isRetired(this) && m_host && m_gui)
    {
        // A foreign blocking operation, a lost document: the commit would be refused; the values
        // prepared for the run are dropped first (S5.1)
        m_gui->onRunRefused();
        if (!PDFDeferredDeletion::isRetired(this) && m_host)
        {
            m_host->showTransientMessage(tr("The document is busy with another operation. Try it again when it ends."));
        }
    }
}

void PDFDocumentToolShell::retryFiles(const QStringList& fileNames, const QString& folder)
{
    if (m_state != State::Editing || !m_gui || !m_runner || !m_host || fileNames.isEmpty())
    {
        return;
    }

    // The parameters of the panel with only the failed files (and the other folder)
    QVariantMap parameters = m_gui->getRunParameters();
    parameters.insert(QString::fromLatin1(PDFDocumentToolFileOutput::PARAMETER_ONLY_OUTPUTS), PDFDocumentToolFileOutput::createOnlyOutputs(fileNames));
    if (!folder.isEmpty())
    {
        parameters.insert(QString::fromLatin1(PDFDocumentToolFileOutput::PARAMETER_FOLDER), folder);
    }

    applyEnvironment();
    if (PDFDeferredDeletion::isRetired(this) || !m_runner || !m_host)
    {
        return;
    }
    if (!m_runner->run(parameters) && !PDFDeferredDeletion::isRetired(this) && m_host)
    {
        if (m_gui)
        {
            m_gui->onRunRefused();
        }
        if (!PDFDeferredDeletion::isRetired(this) && m_host)
        {
            m_host->showTransientMessage(tr("The document is busy with another operation. Try it again when it ends."));
        }
    }
}

void PDFDocumentToolShell::commitPendingValues()
{
    for (const QPointer<QWidget>& part : { m_toolActions, m_rightContent, m_leftContent, m_canvasPage })
    {
        if (!part)
        {
            continue;
        }

        // A write runs the slots of the model, which may delete the controls (a shell retired by them writes nothing
        // more)
        const QList<PDFAppSettingControl*> controls = part->findChildren<PDFAppSettingControl*>();
        std::vector<QPointer<PDFAppSettingControl>> guards(controls.cbegin(), controls.cend());
        for (const QPointer<PDFAppSettingControl>& control : guards)
        {
            if (control)
            {
                control->commitPendingValue();
            }
            if (PDFDeferredDeletion::isRetired(this))
            {
                return;
            }
        }
    }
}

void PDFDocumentToolShell::stop()
{
    if (m_state != State::Running || !m_runner)
    {
        return;
    }

    // The new document of a result without the dialog is being written (4.6): Stop drops it
    if (PDFAppToolResultWriter* writer = m_skippedWriter.data())
    {
        m_skippedWriter = nullptr;
        writer->disconnect(this);
        writer->deleteLater();
        m_runner->endDecision();
        if (!PDFDeferredDeletion::isRetired(this))
        {
            finishRun(PDFDocumentToolResult::createStopped());
        }
        return;
    }

    // The result waits for the answer of the question of the tool (confirmResult): Stop drops it;
    // a late answer finds no result and does nothing
    if (m_confirmingResult)
    {
        m_confirmingResult.reset();
        m_runner->endDecision();
        if (!PDFDeferredDeletion::isRetired(this))
        {
            finishRun(PDFDocumentToolResult::createStopped());
        }
        return;
    }

    // The result waits for a modal dialog of another window: the run has ended, Stop drops it
    if (m_isWaitingForModal && m_pendingResult)
    {
        m_pendingResult.reset();
        m_isWaitingForModal = false;
        watchModalDialog(nullptr);
        m_runner->endDecision();
        if (!PDFDeferredDeletion::isRetired(this))
        {
            finishRun(PDFDocumentToolResult::createStopped());
        }
        return;
    }

    m_runner->stop();
}

void PDFDocumentToolShell::cancel()
{
    if (m_state == State::Deciding)
    {
        cancelResult();
        return;
    }

    if (m_state == State::Editing)
    {
        // Sign by Hand 435:10500, Redact 448:17473: the tool asks first, its answer closes it
        if (askCloseQuestion())
        {
            return;
        }

        PDFAppToolCloseRequest request;
        request.reason = PDFAppToolCloseRequest::Reason::Canceled;
        requestClose(request);
    }
}

bool PDFDocumentToolShell::askCloseQuestion()
{
    // The own tab has its own question (requestOwnTabClose); a question being asked or open is not asked
    // again and the tool stays (a second Esc, a key repeat, an Esc while the question opens)
    if (m_isAskingClose || isCloseQuestionOpen())
    {
        return true;
    }
    if (!m_gui || !m_host || m_host->isOwnTab() || m_isCloseRequested || PDFDocumentToolShellHelper::isClosed(this))
    {
        return false;
    }

    // Owner 2026-11-20: a tool, in which the user has changed nothing, closes without a question
    if (!m_gui->hasUnsavedWork())
    {
        return false;
    }

    // A shell retired meanwhile asks nothing more (as asked: nothing is closed by the caller)
    m_isAskingClose = true;
    QDialog* question = m_gui->askClose();
    if (PDFDeferredDeletion::isRetired(this))
    {
        return true;
    }
    m_isAskingClose = false;
    m_closeQuestion = question;
    return question != nullptr;
}

bool PDFDocumentToolShell::hasUnsavedWork() const
{
    if (m_state != State::Editing || !m_gui || (m_host && m_host->isOwnTab()))
    {
        return false;
    }
    return isCloseQuestionOpen() || m_gui->hasUnsavedWork();
}

bool PDFDocumentToolShell::isCloseQuestionOpen() const
{
    // A question answered already is hidden until it is deleted (WA_DeleteOnClose, deleteLater)
    return m_closeQuestion && m_closeQuestion->isVisible();
}

bool PDFDocumentToolShell::isQuestionOpen() const
{
    return m_isAskingClose || isWaitingForLeave() || m_isAskingLeave || isCloseQuestionOpen();
}

bool PDFDocumentToolShell::isWaitingForLeave() const
{
    return m_leaveFlow && m_leaveFlow->isWaitingFor(this);
}

bool PDFDocumentToolShell::checkLeaveWait()
{
    if (isWaitingForLeave())
    {
        return true;
    }

    // The flow has ended without the answer (canceled from outside, its step dropped): nobody waits any more
    disconnectLeaveQuestion();
    m_leaveFlow = nullptr;
    m_leaveQuestion = nullptr;
    ++m_leaveWaitId;
    return false;
}

PDFAppLeaveResult PDFDocumentToolShell::requestLeaveWork(PDFAppLeaveFlow* flow)
{
    // The own tab asks by its page (requestOwnTabClose, K16); a tool without work goes without a question
    if (!m_gui || !m_host || m_host->isOwnTab() || m_isCloseRequested)
    {
        return PDFAppLeaveResult::Done;
    }
    if (checkLeaveWait())
    {
        // A second leave while the first one waits for the answer: the first one decides
        return PDFAppLeaveResult::Refused;
    }
    if (!isCloseQuestionOpen() && !m_gui->hasUnsavedWork())
    {
        return PDFAppLeaveResult::Done;
    }

    // The tab is shown, so the user sees, what the question is about (a shell retired by a slot of the activation
    // answers nothing: refused, as by its end). The guard stays (RC6-M1): a test deletes the tab synchronously from the
    // switch of the tabs (shellDeletedFromActivation), the callers of the tools keep theirs too.
    QPointer<PDFDocumentToolShell> activateGuard(this);
    m_host->activate();
    if (!activateGuard || PDFDeferredDeletion::isRetired(this) || !m_host)
    {
        return PDFAppLeaveResult::Refused;
    }
    if (m_state != State::Editing)
    {
        // The tool has ended meanwhile (a slot of the activation): nothing is left to lose, the leave goes on
        return PDFAppLeaveResult::Done;
    }

    // The question of Esc may be open already: the leave waits for its answer too (never two questions)
    if (!isCloseQuestionOpen() && !askCloseQuestion())
    {
        // The GUI has nothing to ask after all: the tool may go
        return !activateGuard || PDFDeferredDeletion::isRetired(this) ? PDFAppLeaveResult::Refused : PDFAppLeaveResult::Done;
    }
    if (!activateGuard || PDFDeferredDeletion::isRetired(this))
    {
        return PDFAppLeaveResult::Refused;
    }

    clearLeaveAnswer();
    return waitForLeaveQuestion(m_closeQuestion.data(), flow);
}

PDFAppLeaveResult PDFDocumentToolShell::waitForLeaveQuestion(QDialog* question, PDFAppLeaveFlow* flow)
{
    if (!question)
    {
        return PDFAppLeaveResult::Refused;
    }
    if (!question->isVisible())
    {
        // Nothing to wait for: the tool, which has ended meanwhile, has nothing left to lose (RESCCODE L2)
        return m_state != State::Editing ? PDFAppLeaveResult::Done : PDFAppLeaveResult::Refused;
    }
    if (!flow || !flow->isRunning())
    {
        // Nobody could take the answer: the question stays as the question of the tool, the leave is refused
        return PDFAppLeaveResult::Refused;
    }

    // A0: no event loop. The answer comes by the buttons of the question of the GUI (open, never exec): its closing
    // answer calls closeAfterQuestion (or deferCloseQuestionAction), which accepts the leave now; the end of the
    // question without it (Keep, Esc, the question deleted) refuses the leave from the event loop - the GUIs close
    // the dialog before they call the shell. The shell deleted meanwhile refuses it too (PDFAppLeaveFlowStep).
    m_leaveFlow = flow;
    m_leaveQuestion = question;
    ++m_leaveWaitId;

    // The question is the GUI's, parented to the window (step A: the handles are cut by retire of the shell); a
    // question retired with its owner sends no finished - its aboutToBeDeleted ends the wait (also for a question
    // deleted without retire: the notifier emits from its destructor then; no connection to destroyed, PA6)
    PDFDeferredDeletion::connect(question, &QDialog::finished, this, &PDFDocumentToolShell::onLeaveQuestionEnded);
    m_leaveQuestionEndConnection = PDFDeferredDeletion::connect(PDFDeferredDeletion::getNotifier(question), &PDFLifetimeNotifier::aboutToBeDeleted, this,
                                                                &PDFDocumentToolShell::onLeaveQuestionEnded);
    flow->setQuestion(question);
    return PDFAppLeaveResult::Pending;
}

void PDFDocumentToolShell::disconnectLeaveQuestion()
{
    // The end of a question waited for earlier never ends a later wait; its notifier by the stored handle (getNotifier
    // would create a notifier for a dying question)
    disconnect(m_leaveQuestionEndConnection);
    m_leaveQuestionEndConnection = QMetaObject::Connection();
    if (QDialog* question = m_leaveQuestion.data())
    {
        disconnect(question, nullptr, this, nullptr);
    }
}

void PDFDocumentToolShell::onLeaveQuestionEnded()
{
    // The slot only notes the end (the question may be in its destructor): the answer is read from the event loop,
    // after the GUI has called closeAfterQuestion, if it was a closing answer
    if (!m_leaveFlow || m_isAskingLeave)
    {
        return;
    }
    m_endedLeaveWaitId = m_leaveWaitId;
    QMetaObject::invokeMethod(this, &PDFDocumentToolShell::onLeaveQuestionSettled, Qt::QueuedConnection);
}

void PDFDocumentToolShell::onLeaveQuestionSettled()
{
    if (!m_leaveFlow || m_isAskingLeave || m_endedLeaveWaitId != m_leaveWaitId)
    {
        return;
    }

    // No closing answer has come (Keep, Esc, the question deleted). The tool has ended during the wait (a lock, a
    // lost document; its question went with it): nothing is left to lose, the leave goes on (RESCCODE L2)
    reportLeave(m_state != State::Editing ? PDFAppLeaveResult::Done : PDFAppLeaveResult::Refused);
}

void PDFDocumentToolShell::reportLeave(PDFAppLeaveResult result)
{
    // The wait ends first; the waiter of the flow is the last call (it may delete anything). The tool's own Keep
    // (Back to Redacting, Keep Measuring, Esc of its question) keeps the tool with its work: no closing answer stands.
    QPointer<PDFAppLeaveFlow> flow = m_leaveFlow;
    if (result != PDFAppLeaveResult::Done)
    {
        clearLeaveAnswer();
    }
    disconnectLeaveQuestion();
    m_leaveFlow = nullptr;
    m_leaveQuestion = nullptr;
    ++m_leaveWaitId;
    if (flow && !m_isDestroying)
    {
        // A dying shell reports nothing: the step of its flow is refused by the end of its reporter (from the event
        // loop, PDFAppLeaveFlowStep), the waiter never runs inside of this destructor
        flow->finishStep(this, result);
    }
}

void PDFDocumentToolShell::closeAfterQuestion()
{
    // A leave (the close of the tab, another route) waits for the answer: it goes on and closes the tool (the last
    // call: the waiter may delete the shell) - in every state: a leave, which reached the shell, always gets its
    // answer (the tool may have left Editing while the question was open: a run started, a result dialog). A flow
    // ended meanwhile waits no more: the answer closes the tool.
    if (!m_isAskingLeave && checkLeaveWait())
    {
        // The closing answer is final (owner 2026-11-24): a later cancel of the leave still closes the tool - with the
        // work, which the answer has seen (RLC1 M1: later work keeps the tool)
        m_isLeaveAnswered = true;
        m_leaveAnsweredWork = getLeaveWorkSignature();
        reportLeave(PDFAppLeaveResult::Done);
        return;
    }
    if (m_state != State::Editing)
    {
        return;
    }

    // The answer of the question of the GUI: as Cancel, without the question
    PDFAppToolCloseRequest request;
    request.reason = PDFAppToolCloseRequest::Reason::Canceled;
    requestClose(request);
}

bool PDFDocumentToolShell::deferCloseQuestionAction()
{
    if (m_isAskingLeave || !checkLeaveWait())
    {
        // Nobody waits (a flow ended meanwhile included): the GUI applies the choice as without a leave
        return false;
    }

    // Owner 2026-11-20, decision 8: the choice changes nothing, until the end of the leave (finishLeave); the leave
    // is accepted now (the last call: the waiter may delete the shell; the GUI does nothing more). The answer is final
    // (owner 2026-11-24): the leave going on applies the choice before its next question, a leave canceled later
    // applies it too and closes the tool. A tool, which has left Editing while the question was open, applies nothing
    // then (requestFinishLeave) - the leave still gets its answer.
    m_isCloseQuestionActionDeferred = true;
    m_isLeaveAnswered = true;
    m_leaveAnsweredWork = getLeaveWorkSignature();
    reportLeave(PDFAppLeaveResult::Done);
    return true;
}

bool PDFDocumentToolShell::finishLeave(bool isProceeding)
{
    return requestFinishLeave(isProceeding, nullptr) == PDFAppLeaveResult::Done;
}

PDFAppLeaveResult PDFDocumentToolShell::requestFinishLeave(bool isProceeding, PDFAppLeaveFlow* flow)
{
    if (!isProceeding)
    {
        // The leave was canceled by a later step (another question, a save, a lock): the closing answer of the question
        // of the tool still stands (owner 2026-11-24) - its choice is applied and the tool closes from the event loop
        // (the flow, which calls this, ends first; no navigation runs inside of it). Without such an answer the tool
        // stays as it is.
        if (m_isLeaveAnswered && m_state == State::Editing && m_gui)
        {
            if (!m_isCanceledLeaveQueued)
            {
                m_isCanceledLeaveQueued = true;
                QMetaObject::invokeMethod(this, &PDFDocumentToolShell::onCanceledLeaveQueued, Qt::QueuedConnection);
            }
        }
        else
        {
            clearLeaveAnswer();
        }
        return PDFAppLeaveResult::Done;
    }

    // The leave goes on: nothing of the user can change the work since the answer (the questions of the leave are
    // modal), the work is read again after the choice applied below - a later cancel compares the work with it
    const bool isDeferred = m_isCloseQuestionActionDeferred;
    m_isCloseQuestionActionDeferred = false;
    if (!isDeferred || m_state != State::Editing || !m_gui)
    {
        m_leaveAnsweredWork = getLeaveWorkSignature();
        return PDFAppLeaveResult::Done;
    }

    // The leave was asked inside of the change notification of the document (a slot of its documentChanged), whose
    // version this shell and its GUI have not seen yet: the choice is applied from the event loop, after the GUI has
    // seen the change (the step of the flow waits for it, onFinishLeaveQueued)
    const PDFAppDocument* document = m_host ? m_host->getDocument() : nullptr;
    if (flow && flow->isRunning() && document && document->getVersion() != m_knownDocumentVersion)
    {
        m_isCloseQuestionActionDeferred = true;
        m_leaveFlow = flow;
        m_leaveQuestion = nullptr;
        ++m_leaveWaitId;
        QMetaObject::invokeMethod(this, &PDFDocumentToolShell::onFinishLeaveQueued, Qt::QueuedConnection);
        return PDFAppLeaveResult::Pending;
    }

    // The leave goes on with the tool still open: the choice commits through the shell as it does outside of a
    // wait (one Undo step of the tool). The slots of the commit may close this shell - then nothing is left to keep.
    // The guard stays (RC6-M1): the activation below may delete the tab synchronously in a test (as
    // shellDeletedFromActivation), the callers of the tools keep theirs too.
    QPointer<PDFDocumentToolShell> guard(this);
    const quint64 versionBefore = document ? document->getVersion() : 0;
    const bool isKept = m_gui->applyCloseQuestionAction();
    if (!guard || PDFDeferredDeletion::isRetired(this))
    {
        return PDFAppLeaveResult::Done;
    }
    const PDFAppDocument* changedDocument = m_host ? m_host->getDocument() : nullptr;
    m_isLeaveChoiceApplied = changedDocument && changedDocument->getVersion() != versionBefore;
    m_leaveAnsweredWork = getLeaveWorkSignature();
    if (isKept)
    {
        return PDFAppLeaveResult::Done;
    }

    // Owner 2026-11-21: a part of the work could not be kept (a measurement, which cannot be stored as an
    // annotation; a locked mark). The tab of the tool is shown; the GUI asks about the rest (Measure: Keep Measuring
    // cancels the leave, Discard and Close lets it go on) as the open question of the tool, waited for as by
    // requestLeave (A0: the step stays Pending until its answer). A GUI without such a question keeps its work and
    // has told why: the leave is canceled. Without a flow nobody waits: the question stays as the question of the
    // tool (its closing answer closes the tool) and the leave is canceled. A refused end keeps the tool with the rest
    // (owner 2026-11-21): no closing answer stands for a later cancel of the leave.
    if (m_host)
    {
        m_host->activate();
    }
    if (!guard || PDFDeferredDeletion::isRetired(this) || m_state != State::Editing)
    {
        return PDFAppLeaveResult::Done;
    }
    QDialog* question = askIncompleteQuestion();
    if (!guard || PDFDeferredDeletion::isRetired(this))
    {
        return PDFAppLeaveResult::Refused;
    }
    const PDFAppLeaveResult result = question ? waitForLeaveQuestion(question, flow) : PDFAppLeaveResult::Refused;
    if (result == PDFAppLeaveResult::Refused)
    {
        clearLeaveAnswer();
    }
    return result;
}

void PDFDocumentToolShell::onCanceledLeaveQueued()
{
    // The leave, to which the question of the tool was answered by closing, was canceled by a later step (owner
    // 2026-11-24): the answer is honored now. A tool, which has ended meanwhile, a leave or a question of the tool
    // started meanwhile (it decides anew), a retired shell (its posted call is removed) do nothing. A leave, which waits
    // for this tool, owns the answer (requestLeave has forgotten the old one); every other early end forgets it too
    // (RLC1 L1: a later leave, which does not ask the tool, never applies or closes by an old answer).
    m_isCanceledLeaveQueued = false;
    if (!m_isLeaveAnswered || checkLeaveWait())
    {
        return;
    }
    const bool isDeferred = m_isCloseQuestionActionDeferred;
    const bool isChangeApplied = m_isLeaveChoiceApplied;
    const bool isWorkUnchanged = getLeaveWorkSignature() == m_leaveAnsweredWork;
    clearLeaveAnswer();
    if (m_state != State::Editing || !m_gui || !m_host || m_isCloseRequested || PDFDocumentToolShellHelper::isClosed(this) || m_isAskingClose ||
        isCloseQuestionOpen())
    {
        return;
    }

    // RLC1 M1: the answer covered the work, which existed then (and its applied choice). Work of the user made later
    // (a measurement added while the file of the close or of Save All was written) was never answered: the tool stays
    // with it - nothing happens, which the user did not choose.
    if (!isWorkUnchanged)
    {
        return;
    }

    // RLC1 L2: a choice, which the document cannot take now (an operation, which blocks the changes, kept by Keep
    // Running; the document locked meanwhile), is not applied: the tool stays with its work and says the true reason
    const PDFDocumentToolCatalogEntry* tool = getCatalogEntry();
    const QString toolTitle = tool ? tool->getTitle() : QString();
    PDFAppDocument* document = m_host->getDocument();
    if (isDeferred && document && (document->isLocked() || document->isBlockedByOperation()))
    {
        m_host->showTransientMessage(document->isLocked() ? tr("%1 stays open. The document is locked. Unlock it to make changes.").arg(toolTitle)
                                                          : tr("%1 stays open. The document cannot be changed while an operation is running.").arg(toolTitle));
        return;
    }

    // The remembered choice (Redact "Remove New Marks", Measure "Save and Close") commits through the shell as without
    // a leave (one Undo step of the tool); the slots of the commit may close the tool. Work, which the choice could not
    // keep, keeps the tool open (owner 2026-11-21): the GUI asks about the rest as the question of the tool (Keep
    // Measuring / Discard and Close), one question of the tools at a time, or has told why (a locked mark).
    QPointer<PDFDocumentToolShell> guard(this);
    bool isDocumentChanged = isChangeApplied;
    if (isDeferred)
    {
        const quint64 versionBefore = document ? document->getVersion() : 0;
        const bool isKept = m_gui->applyCloseQuestionAction();
        if (!guard || PDFDeferredDeletion::isRetired(this) || m_state != State::Editing)
        {
            return;
        }
        if (!isKept)
        {
            m_isIncompleteQuestionPending = true;
            askIncompleteAfterCanceledLeave();
            return;
        }
        const PDFAppDocument* changedDocument = m_host ? m_host->getDocument() : nullptr;
        isDocumentChanged = isDocumentChanged || (changedDocument && changedDocument->getVersion() != versionBefore);
    }

    // The tool ends as by its closing answer without a leave (closeAfterQuestion): the page leaves it for the document
    // and shows the message (decision 3 of 24. 11.: the user sees why the tool has gone and how to take the change
    // back); the last call: the shell, the GUI and the page may go
    PDFAppToolCloseRequest request;
    request.reason = PDFAppToolCloseRequest::Reason::Canceled;
    request.statusMessage = getCanceledLeaveMessage(isDocumentChanged);
    requestClose(request);
}

void PDFDocumentToolShell::clearLeaveAnswer()
{
    m_isLeaveAnswered = false;
    m_isCloseQuestionActionDeferred = false;
    m_isLeaveChoiceApplied = false;
    m_leaveAnsweredWork.clear();
}

QString PDFDocumentToolShell::getLeaveWorkSignature() const
{
    // What leaving the tool loses, as its question says it: the unsaved work and the texts of the question, which name
    // it ("Discard the 2 temporary measurements?"). A reading of the document, which ends later (the annotations listed
    // again after a commit), changes neither - the stamp of the work would change by it (RLC1 M1)
    if (!m_gui)
    {
        return QString();
    }
    const PDFAppToolCloseQuestion question = m_gui->getCloseQuestion();
    return QString::number(m_gui->hasUnsavedWork() ? 1 : 0) + QLatin1Char('\n') + question.title + QLatin1Char('\n') + question.text;
}

void PDFDocumentToolShell::forgetLeaveAnswer()
{
    if (!m_isAskingLeave && !checkLeaveWait())
    {
        clearLeaveAnswer();
    }
}

QString PDFDocumentToolShell::getCanceledLeaveMessage(bool isChangeApplied) const
{
    const PDFDocumentToolCatalogEntry* tool = getCatalogEntry();
    const QString toolTitle = tool ? tool->getTitle() : QString();
    return isChangeApplied ? tr("%1 has closed as you chose in its question. Ctrl+Z undoes its change.").arg(toolTitle)
                           : tr("%1 has closed as you chose in its question.").arg(toolTitle);
}

void PDFDocumentToolShell::askIncompleteAfterCanceledLeave()
{
    stopWaitingForOtherQuestion();
    if (!m_isIncompleteQuestionPending)
    {
        return;
    }

    // A tool, which has ended or is asked again meanwhile (a new leave asks its own question about all of the work),
    // asks nothing more
    if (m_state != State::Editing || !m_gui || !m_host || m_isCloseRequested || PDFDocumentToolShellHelper::isClosed(this) || checkLeaveWait() ||
        m_isAskingClose || isCloseQuestionOpen())
    {
        m_isIncompleteQuestionPending = false;
        return;
    }

    // RLC1 L3: one question at a time (as the quit, which goes on, asks them one by one) - the open question of another
    // tool is answered first; its end (finished, or its deletion without it) tries again
    if (QDialog* other = findOtherOpenQuestion())
    {
        m_otherQuestionFinishedConnection = PDFDeferredDeletion::connect(other, &QDialog::finished, this, &PDFDocumentToolShell::onOtherQuestionEnded);
        m_otherQuestionEndConnection = PDFDeferredDeletion::connect(PDFDeferredDeletion::getNotifier(other), &PDFLifetimeNotifier::aboutToBeDeleted, this,
                                                                    &PDFDocumentToolShell::onOtherQuestionEnded);
        return;
    }

    // The tab of the tool is shown, so the user sees what the question is about (a slot of the activation may close the
    // tool); the question is the last call
    m_isIncompleteQuestionPending = false;
    QPointer<PDFDocumentToolShell> guard(this);
    m_host->activate();
    if (guard && !PDFDocumentToolShellHelper::isClosed(this) && m_state == State::Editing)
    {
        askIncompleteQuestion();
    }
}

void PDFDocumentToolShell::onOtherQuestionEnded()
{
    // The slot only notes the end (the question may be in its destructor, its tool closes after it): the next try from
    // the event loop
    stopWaitingForOtherQuestion();
    if (m_isIncompleteQuestionPending)
    {
        QMetaObject::invokeMethod(this, &PDFDocumentToolShell::askIncompleteAfterCanceledLeave, Qt::QueuedConnection);
    }
}

void PDFDocumentToolShell::stopWaitingForOtherQuestion()
{
    disconnect(m_otherQuestionFinishedConnection);
    disconnect(m_otherQuestionEndConnection);
    m_otherQuestionFinishedConnection = QMetaObject::Connection();
    m_otherQuestionEndConnection = QMetaObject::Connection();
}

QDialog* PDFDocumentToolShell::findOtherOpenQuestion() const
{
    // The shells of the tools in the tabs of the window (the pages, children of the tabs); a retired or closed shell asks
    // nothing
    const QWidget* keyWidget = m_host ? m_host->getKeyWidget() : nullptr;
    const QWidget* window = keyWidget ? keyWidget->window() : nullptr;
    for (const PDFDocumentToolShell* shell : window ? window->findChildren<PDFDocumentToolShell*>() : QList<PDFDocumentToolShell*>())
    {
        if (shell != this && !PDFDocumentToolShellHelper::isClosed(shell) && shell->isCloseQuestionOpen())
        {
            return shell->getCloseQuestion();
        }
    }
    return nullptr;
}

void PDFDocumentToolShell::onFinishLeaveQueued()
{
    // The flow still waits for this step (a flow ended meanwhile, a closed shell: nothing is applied)
    QPointer<PDFAppLeaveFlow> flow = m_leaveFlow;
    if (!flow || !isWaitingForLeave() || m_leaveQuestion || !m_isCloseQuestionActionDeferred || PDFDocumentToolShellHelper::isClosed(this))
    {
        return;
    }

    // The choice is applied once, now with the version seen; its question about the rest (Pending) is waited for as
    // by requestLeave, else the step is reported (the last call; a shell retired meanwhile reports nothing - the end
    // of the shell refuses the step, PDFAppLeaveFlowStep; the guard: its activation, RC6-M1)
    QPointer<PDFDocumentToolShell> guard(this);
    const PDFAppLeaveResult result = requestFinishLeave(true, flow.data());
    if (!guard || PDFDeferredDeletion::isRetired(this) || result == PDFAppLeaveResult::Pending)
    {
        return;
    }
    reportLeave(result);
}

QDialog* PDFDocumentToolShell::askIncompleteQuestion()
{
    if (!m_gui || m_state != State::Editing || PDFDocumentToolShellHelper::isClosed(this))
    {
        return nullptr;
    }

    // The GUI opens its question (open, never exec) and returns it; it never deletes the shell (a retired one asks
    // nothing more)
    m_isAskingClose = true;
    QDialog* question = m_gui->askIncompleteClose();
    if (PDFDeferredDeletion::isRetired(this))
    {
        return nullptr;
    }
    m_isAskingClose = false;
    m_closeQuestion = question;
    return question;
}

bool PDFDocumentToolShell::commitDocumentChange(const PDFModifiedDocument& modified, const QString& description, quint64 expectedVersion)
{
    PDFAppDocument* document = m_host ? m_host->getDocument() : nullptr;
    if (!document || m_state != State::Editing)
    {
        return false;
    }

    // The slots of the document run (the GUI, the view; a slot may close the tab): the step is
    // recorded after the commit by the version the commit has made (+1), so a change of somebody
    // else made by a slot meanwhile ends the steps of the tool instead of becoming one (a retired tool records none)
    const bool isCommitted = document->commit(modified, description, expectedVersion, PDFAppChangeSource::Tool);
    if (isCommitted && m_undoTarget && !PDFDeferredDeletion::isRetired(this))
    {
        m_undoTarget->recordDocumentStep(expectedVersion + 1);
    }
    return isCommitted;
}

void PDFDocumentToolShell::cancelResult()
{
    PDFAppToolResultDialog* dialog = m_resultDialog.data();
    if (!dialog)
    {
        return;
    }

    // 383:11374, 403:29978: Cancel and Close of the written files lead to the same frame, so
    // Cancel is Close there; the other after-run frames (296:2761, 336:3572) give Cancel no
    // target: the result is canceled as by the dialog and the tool is left as Cancel leaves it
    const bool isCancelClose = dialog->getResultKind() == PDFAppToolResultDialog::ResultKind::WrittenFiles;

    // The decision runs onDialogDecided, which may close the tool and delete this shell; a
    // dialog, which is writing, refuses it and the state stays Deciding
    dialog->reject();
    if (PDFDeferredDeletion::isRetired(this) || isCancelClose || m_state != State::Editing)
    {
        return;
    }

    // As Cancel in Editing: the question of the tool (open already after Discard result) is not
    // passed by (435:10500, 448:17473)
    cancel();
}

void PDFDocumentToolShell::onEscape()
{
    // A shortcut of a widget matches also inside of a popup below it (Qt walks to the
    // parents through the popups): Esc of an open panel closes only the panel
    if (QWidget* popup = QApplication::activePopupWidget())
    {
        popup->close();
        return;
    }

    // A modal dialog takes the keys itself (Esc belongs to it, never to the tool below it)
    if (QApplication::activeModalWidget())
    {
        return;
    }

    // Owner 2026-11-17: Esc ends a drag of a widget of the tool first (the shortcut takes the key
    // before the widget), the tool stays open; its signal is the last call
    if (IPDFAppToolDragSource* dragSource = IPDFAppToolDragSource::findActive(m_host ? m_host->getKeyWidget() : nullptr))
    {
        dragSource->cancelToolDrag();
        return;
    }

    // Then a drag of the GUI, which is no widget, or its active widget tool / input mode of the
    // canvas; only without them Esc leaves the tool (the GUI never deletes the shell)
    if (m_state == State::Editing && m_gui && m_gui->handleEscape())
    {
        return;
    }

    if (m_state == State::Running)
    {
        stop();
    }
    else
    {
        cancel();
    }
}

PDFAppLeaveResult PDFDocumentToolShell::requestLeave(PDFAppLeaveFlow* flow)
{
    // A new leave asks anew: an answer given to an earlier leave is not this one's (owner 2026-11-24), nor is a question
    // about the rest of an earlier leave still to come (the question of this leave covers all of the work)
    if (!m_isAskingLeave && !checkLeaveWait())
    {
        clearLeaveAnswer();
        m_isIncompleteQuestionPending = false;
        stopWaitingForOtherQuestion();
    }

    switch (m_state)
    {
        case State::Created:
        case State::Closed:
            return PDFAppLeaveResult::Done;

        case State::Editing:
            // Owner 2026-11-20: every way of leaving asks the question of the unsaved work
            return requestLeaveWork(flow);

        case State::Deciding:
            // The modal dialog decides: nothing goes around it
            return PDFAppLeaveResult::Refused;

        case State::Running:
            break;
    }

    if (m_isAskingLeave || checkLeaveWait() || !m_host || !flow || !flow->isRunning())
    {
        return PDFAppLeaveResult::Refused;
    }

    // The tab is shown, so the user sees, what the question is about (a slot of the activation may close the tool:
    // isClosed covers a retired shell). The guard stays (RC6-M1): a test deletes the tab synchronously from the switch
    // of the tabs (shellDeletedFromActivation), the callers of the tools keep theirs too.
    QPointer<PDFDocumentToolShell> activateGuard(this);
    m_host->activate();
    if (!activateGuard || !m_host || PDFDocumentToolShellHelper::isClosed(this))
    {
        return PDFAppLeaveResult::Refused;
    }

    // A0: the question is shown (exec's modality, no event loop); its answer comes to onLeaveRunningChosen
    const PDFDocumentToolCatalogEntry* tool = getCatalogEntry();
    m_isAskingLeave = true;
    m_leaveFlow = flow;
    ++m_leaveWaitId;
    PDFAppToolLeaveRunningDialog* dialog = new PDFAppToolLeaveRunningDialog(tool ? tool->getTitle() : QString(), m_host->getDocument(), m_percentage, m_host->getKeyWidget());
    m_leaveDialog = dialog;

    // A child of the key widget of the host, which belongs to the tool: retire of the shell closes it unanswered (its
    // step of the flow is refused by the end of the shell, PDFAppLeaveFlowStep)
    PDFDeferredDeletion::setOwner(dialog, this);
    flow->setQuestion(dialog);
    PDFAppDocumentChoiceDialog::ask(dialog, this, &PDFDocumentToolShell::onLeaveRunningChosen);
    return PDFAppLeaveResult::Pending;
}

void PDFDocumentToolShell::onLeaveRunningChosen(PDFAppDocumentChoice choice)
{
    m_isAskingLeave = false;
    if (m_isDestroying)
    {
        // The question was rejected by the destructor of the shell (the tab or the window deleted): nothing is
        // decided and nothing is reported (the step is refused by the end of the shell)
        m_pendingResult.reset();
        reportLeave(PDFAppLeaveResult::Refused);
        return;
    }
    if (m_state == State::Closed)
    {
        // The tool has ended meanwhile (F2, a lock): its page goes on
        reportLeave(PDFAppLeaveResult::Refused);
        return;
    }

    if (choice != PDFAppDocumentChoice::Stop)
    {
        // Keep Running: a result, which came while the user was asked, is decided now
        if (m_pendingResult)
        {
            const PDFDocumentToolResult result = *m_pendingResult;
            m_pendingResult.reset();
            finishRun(result);
            if (PDFDeferredDeletion::isRetired(this))
            {
                return;
            }
        }
        reportLeave(PDFAppLeaveResult::Refused);
        return;
    }

    m_pendingResult.reset();
    reportLeave(PDFAppLeaveResult::Done);
}

void PDFDocumentToolShell::requestClose(const PDFAppToolCloseRequest& request)
{
    if (m_isCloseRequested || m_state == State::Closed)
    {
        return;
    }
    m_isCloseRequested = true;
    Q_EMIT closeRequested(request);
}

void PDFDocumentToolShell::showProgress(bool isShown)
{
    // A shown row goes by retire (step A): hidden and inert at once, deleted from the event loop
    PDFDeferredDeletion::retire(m_progressCard.data());
    PDFDeferredDeletion::retire(m_busyCallout.data());
    m_progressCard = nullptr;
    m_busyCallout = nullptr;
    if (!isShown || !m_gui || !m_rightPanel || PDFDocumentToolShellHelper::isClosed(this))
    {
        return;
    }

    const PDFAppToolProgressPlace place = m_gui->getProgressPlace();
    m_progressCard = new PDFAppToolProgressCard(m_rightPanel->getContentParent());
    m_progressCard->setObjectName(QStringLiteral("toolProgressCard"));
    if (place.cardLayout)
    {
        place.cardLayout->insertWidget(place.cardIndex, m_progressCard);
    }
    else
    {
        m_rightPanel->getBodyLayout()->insertWidget(0, m_progressCard);
    }

    const QString busyText = m_gui->getBusyText();
    if (!busyText.isEmpty())
    {
        m_busyCallout = new PDFAppToolCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("The document is busy"), busyText, m_rightPanel->getContentParent());
        m_busyCallout->setObjectName(QStringLiteral("toolBusyCallout"));
        if (place.calloutLayout)
        {
            place.calloutLayout->insertWidget(place.calloutIndex, m_busyCallout);
        }
        else if (place.cardLayout)
        {
            place.cardLayout->insertWidget(place.cardIndex + 1, m_busyCallout);
        }
        else
        {
            m_rightPanel->getBodyLayout()->insertWidget(1, m_busyCallout);
        }
    }
}

bool PDFDocumentToolShell::isCertificationRemoved() const
{
    const PDFAppDocument* document = m_host ? m_host->getDocument() : nullptr;
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    if (!m_tool || !m_gui || !pdfDocument)
    {
        return false;
    }

    // The values a run would get now; the run decides by the same function (Table 257: no level of
    // a certification allows a change of the pages)
    return PDFDocMDPPermissions::isCertificationRemovedBy(pdfDocument.data(), m_tool->getOutputTraits(m_gui->getRunParameters()));
}

void PDFDocumentToolShell::updateCertificationCallout(bool isPlacedAgain)
{
    const bool isShown = m_rightPanel && m_gui && m_state != State::Closed && !PDFDocumentToolShellHelper::isClosed(this) && isCertificationRemoved();
    if (m_certificationCallout && (!isShown || isPlacedAgain))
    {
        PDFDeferredDeletion::retire(m_certificationCallout.data());
        m_certificationCallout = nullptr;
    }
    if (!isShown || m_certificationCallout)
    {
        return;
    }

    // Owner decision of 13. 11.: the user learns it while working in the tool, running stays
    // allowed. The callout of Manage Pages 1479:35631 (compact) in the hue of a warning: the run
    // is possible, its result is only a copy (the result dialog says it again, Q12, R10).
    PDFAppToolCallout* callout = new PDFAppToolCallout(PDFAccentHue::Amber, QStringLiteral("certificate"), tr("This document is certified"),
                                                       tr("Its pages cannot change, so the result will be a copy without the certification and the signatures."),
                                                       m_rightPanel->getContentParent());
    callout->setObjectName(QStringLiteral("toolCertificationCallout"));
    callout->setCompactStyle();

    const PDFAppToolCalloutPlace place = m_gui->getCertificationCalloutPlace();
    if (place.layout)
    {
        place.layout->insertWidget(place.index, callout);
    }
    else
    {
        // Around the content of the right panel: above it, or below it before the stretch
        QVBoxLayout* body = m_rightPanel->getBodyLayout();
        const int contentIndex = m_rightContent ? body->indexOf(m_rightContent.data()) : -1;
        int index = contentIndex >= 0 ? contentIndex : 0;
        if (place.index < 0)
        {
            index = contentIndex >= 0 ? contentIndex + 1 : body->count() - 1;
        }
        body->insertWidget(index, callout);
    }
    m_certificationCallout = callout;
}

int PDFDocumentToolShell::getRemainingSeconds(int percentage) const
{
    // The first percents say nothing about the rest of the run
    constexpr int MINIMUM_PERCENTAGE = 5;
    constexpr qint64 MINIMUM_ELAPSED = 1000;
    if (percentage < MINIMUM_PERCENTAGE || percentage > 100 || !m_runTimer.isValid() || m_runTimer.elapsed() < MINIMUM_ELAPSED)
    {
        return -1;
    }
    return int(m_runTimer.elapsed() * (100 - percentage) / percentage / 1000);
}

QString PDFDocumentToolShell::formatRemainingTime(int seconds)
{
    if (seconds < 0)
    {
        return QString();
    }
    if (seconds >= 90)
    {
        return tr("About %Ln minutes left", nullptr, (seconds + 30) / 60);
    }
    return seconds <= 1 ? tr("About a second left") : tr("About %Ln seconds left", nullptr, seconds);
}

void PDFDocumentToolShell::updateProgress(int percentage, const QString& stepText)
{
    m_percentage = percentage;
    m_stepText = stepText;
    if (m_progressCard && m_gui)
    {
        m_progressCard->setProgress(percentage, m_gui->getRunningCardTitle(percentage, stepText), m_gui->getRunningCardStepText(stepText), m_gui->getRunningTimeText(getRemainingSeconds(percentage)));
    }
    updateStatus();
}

void PDFDocumentToolShell::openResultDialog(const PDFDocumentToolResult& result, bool isStoppedFilesResult)
{
    // RS3APP: a flag of an earlier dialog, which was not opened, never reaches a later one
    m_isStoppedFilesResult = false;
    if (!m_host || !m_runner || !m_gui || PDFDocumentToolShellHelper::isClosed(this))
    {
        return;
    }

    // The run may have gone on behind another tab (F1): the user decides about a document, which is shown. A slot of
    // the activation may close the tool (step A): a closed shell opens no dialog. The guard stays (PA6): the test
    // shellDeletedFromActivation deletes the tab synchronously from the switch of the tabs.
    QPointer<PDFDocumentToolShell> activateGuard(this);
    m_host->activate();
    if (!activateGuard || !m_host || !m_runner || !m_gui || PDFDocumentToolShellHelper::isClosed(this))
    {
        return;
    }

    PDFAppToolResultInput input;
    input.tool = getCatalogEntry();
    input.result = result;
    input.scrimTarget = m_host->getCanvasArea();
    if (m_host->isOwnTab())
    {
        // A new tab of the result opens next to the own tab of the tool (4.5)
        input.anchorTab = m_host->getTab();
    }
    if (const PDFAppDocument* document = m_host->getDocument())
    {
        input.previousToolId = document->getLastFinishedToolId();
    }
    input.isResultInThisTab = m_host->isOwnTab() && m_gui->isResultInThisTab();
    if (const PDFAppDocument* document = m_isExportDialog ? m_host->getDocument() : nullptr)
    {
        // The export of a kept result has no snapshot of a run with the file: the folder of the document
        const QString filePath = document->getFilePath();
        input.initialFolder = filePath.isEmpty() ? QString() : QFileInfo(filePath).absolutePath();
    }
    m_gui->fillResultInput(&input);

    m_state = State::Deciding;
    m_isStoppedFilesResult = isStoppedFilesResult;
    updateToolbar();

    // Connected before it is shown: a slot of its show may delete anything
    QWidget* keyWidget = m_host->getKeyWidget();
    PDFAppDocumentTab* tab = m_host->getDocumentTab();
    PDFAppToolResultDialog* dialog = new PDFAppToolResultDialog(m_host->getContext(), tab, m_runner, std::move(input), keyWidget ? keyWidget->window() : nullptr);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_resultDialog = dialog;

    // A child of the window, which belongs to the tool: retire of the shell closes it (step A)
    PDFDeferredDeletion::setOwner(dialog, this);
    connect(dialog, &PDFAppToolResultDialog::decided, this, &PDFDocumentToolShell::onDialogDecided);
    connect(dialog, &PDFAppToolResultDialog::statusTextChanged, this, &PDFDocumentToolShell::onDialogStatusChanged);
    updateStatus();

    dialog->QDialog::open();
    if (!PDFDeferredDeletion::isRetired(this))
    {
        Q_EMIT stateChanged(m_state);
    }
}

void PDFDocumentToolShell::showPermissionRefusal()
{
    // Queued by the entry: a page closed meanwhile (the navigation went on) asks nothing
    if (m_state == State::Closed || !m_gui || !m_host || m_permissionDialog || PDFDocumentToolShellHelper::isClosed(this))
    {
        return;
    }

    // A document, which its owner password can unlock, asks for it (owner 2026-11-08): the
    // dialog of the shared check, the tool continues after the unlock (onDocumentChanged),
    // Cancel closes the tool as Close of the refusal does
    QWidget* parent = m_host->getKeyWidget() ? m_host->getKeyWidget()->window() : nullptr;
    PDFAppDocument* document = m_host->getDocument();
    QDialog* ownerDialog = nullptr;
    if (document && PDFAppDocumentPermissions::canUnlock(document->getDocument().data()))
    {
        PDFAppDocumentPermissions::request(document, PDFAppPermissionRequest::createTool(getCatalogEntry()->getId()), parent, &ownerDialog);
        if (PDFMessageBox* box = qobject_cast<PDFMessageBox*>(ownerDialog))
        {
            // A child of the window, which belongs to the tool (close closes it): retire of the shell closes it too
            m_permissionDialog = box;
            PDFDeferredDeletion::setOwner(box, this);
            connect(box, &QDialog::finished, this, &PDFDocumentToolShell::onPermissionDialogFinished);
            return;
        }
    }

    // 339:10328: 480 wide, the title and its line, the text, Close (the button Open
    // Restrict Permissions comes with that tool, Q7). The scrim covers the canvas as
    // the other dialogs of a tool. A document of the public-key handler has no owner
    // password: its permissions come from the certificate.
    const PDFDocument* pdfDocument = document ? document->getDocument().data() : nullptr;
    const QString text = PDFAppDocumentPermissions::isCertificateSecurity(pdfDocument)
                       ? PDFAppDocumentPermissions::getRefusalText(pdfDocument, PDFAppPermissionRequest::createTool(getCatalogEntry()->getId()))
                       : m_gui->getPermissionText();
    PDFMessageBox* box = new PDFMessageBox(PDFThemeIcons::Badge::Warning, m_gui->getPermissionTitle(), m_gui->getPermissionSubtitle(), QDialogButtonBox::NoButton, parent);
    box->setObjectName(QStringLiteral("toolPermissionDialog"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setCardWidth(480);
    box->setScrimTarget(m_host->getCanvasArea());
    box->addContent(PDFAppPageWidgets::createText(text, PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, box));

    QPushButton* closeButton = box->addActionButton(tr("Close"), PDFMessageBox::ActionPosition::Leading, false);
    closeButton->setObjectName(QStringLiteral("toolPermissionClose"));
    connect(closeButton, &QPushButton::clicked, box, &QDialog::reject);
    connect(box, &QDialog::finished, this, &PDFDocumentToolShell::onPermissionDialogFinished);

    m_permissionDialog = box;
    PDFDeferredDeletion::setOwner(box, this);
    box->open();
}

void PDFDocumentToolShell::onPermissionDialogFinished()
{
    // Unlocked by the owner password: the tool goes on (onDocumentChanged has updated it)
    m_permissionDialog = nullptr;
    if (!isRefusedByPermissions())
    {
        return;
    }
    cancel();
}

std::optional<PDFAppPermissionRequest> PDFDocumentToolShell::getRunPermissionRequest() const
{
    const PDFDocumentToolCatalogEntry* entry = getCatalogEntry();
    if (!m_gui || !entry)
    {
        return std::nullopt;
    }

    // ISO 32000-2, Table 22 (owner 2026-11-08): a run, which needs one of more permissions (an existing
    // empty signature field: bit 9 or bit 6), asks them too - a document, which allows none of them,
    // offers the owner password, never only the refusal of the validation of the core
    const std::vector<PDFSecurityHandler::Permission> runPermissions = m_gui->getRunPermissions();
    const QVariantMap runParameters = m_gui->getRunParameters();
    const bool hasAlternatives = m_tool && !m_tool->getAnyOfPermissions(runParameters).empty();
    if (runPermissions.empty() && !hasAlternatives)
    {
        return std::nullopt;
    }
    return PDFAppPermissionRequest::createTool(entry->getId(), runPermissions, runParameters);
}

bool PDFDocumentToolShell::isRefusedByPermissions() const
{
    // The one check of the new application (PDFAppDocumentPermissions): the permissions of
    // the catalog entry of the tool (ISO 32000-2, Table 22). A GUI, which shows the refusal
    // itself (Manage Pages in the tab of a document, 1479:35631), is not refused at the entry:
    // its Primary button stays Disabled and the validation of the tool refuses the run.
    if (m_gui && m_gui->handlesPermissionRefusal())
    {
        return false;
    }
    const PDFDocumentToolCatalogEntry* entry = getCatalogEntry();
    const PDFAppDocument* document = m_host ? m_host->getDocument() : nullptr;
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    return entry && pdfDocument && !PDFAppDocumentPermissions::isAllowed(pdfDocument.data(), PDFAppPermissionRequest::createTool(entry->getId()));
}

void PDFDocumentToolShell::onParameterChanged(const QString& name)
{
    ++m_workStamp;
    if (m_gui)
    {
        m_gui->onParametersChanged(name);
    }
    if (!PDFDeferredDeletion::isRetired(this))
    {
        // R7-2: a changed input makes the kept result stale (the format of a later export does not)
        if (m_resultState == PDFAppToolResultState::Current && m_gui && m_gui->isResultStaleBy(name))
        {
            setResultState(PDFAppToolResultState::Stale);
        }

        // Manage Pages: only the output This document changes the pages
        updateCertificationCallout(false);
        updateToolbar();
        updateStatus();
        updatePreview();
    }
}

void PDFDocumentToolShell::onConfirmStateChanged()
{
    ++m_workStamp;
    updateToolbar();
}

void PDFDocumentToolShell::onWorkChanged()
{
    ++m_workStamp;
}

void PDFDocumentToolShell::onPreviewProgress(int percentage, const QString& stepText)
{
    if (m_gui && m_state == State::Editing)
    {
        m_gui->onPreviewProgress(percentage, stepText);
    }
}

void PDFDocumentToolShell::onPreviewFinished(const PDFDocumentToolResult& result)
{
    // The hook says, that the Primary button has changed (a slot may close this shell: retired, deleted later - only
    // its own toolbar follows)
    if (m_gui)
    {
        m_gui->onPreviewChanged(&result);
    }
    if (m_toolbar)
    {
        updateToolbar();
    }
}

void PDFDocumentToolShell::onPreviewDropped()
{
    if (m_gui)
    {
        m_gui->onPreviewChanged(nullptr);
    }
}

void PDFDocumentToolShell::onRunStarted()
{
    m_runTimer.restart();
    m_percentage = 0;
    m_stepText.clear();
    m_state = State::Running;

    if (m_gui)
    {
        m_gui->setContentEnabled(false);
        showProgress(true);
        m_gui->onRunStateChanged(true);
        if (PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }
    updateProgress(0, QString());
    updateToolbar();

    Q_EMIT stateChanged(m_state);
}

void PDFDocumentToolShell::onRunProgress(int percentage, const QString& stepText)
{
    if (m_state == State::Running)
    {
        updateProgress(percentage, stepText);
    }
}

void PDFDocumentToolShell::onRunFinished(const PDFDocumentToolResult& result)
{
    // A result, which comes while the user is asked about the leaving (F14), waits for the answer
    if (m_isAskingLeave && result.isSuccess())
    {
        m_pendingResult = result;
        return;
    }

    finishRun(result);
}

bool PDFDocumentToolShell::waitForModalDialog(const PDFDocumentToolResult& result)
{
    // No new dialog while a modal dialog of the application is open (the rule of the opening of
    // documents and of the requests of an assistant): the user is deciding about something else
    // there, also in another tab, which is not taken away from below the dialog. The operation of
    // the runner keeps the document.
    QWidget* modal = result.isSuccess() ? PDFAppMainWindow::getBlockingModalWidget() : nullptr;
    if (!modal)
    {
        return false;
    }

    m_pendingResult = result;
    m_isWaitingForModal = true;
    watchModalDialog(modal);
    updateProgress(100, m_stepText);
    return true;
}

void PDFDocumentToolShell::watchModalDialog(QWidget* dialog)
{
    if (m_watchedModal == dialog)
    {
        return;
    }

    if (m_watchedModal)
    {
        m_watchedModal->removeEventFilter(this);
    }

    // A dialog hides itself also in its destructor: its event tells the end of every dialog
    m_watchedModal = dialog;
    if (dialog)
    {
        // A dialog of somebody else: retire of the shell removes the filter (step A)
        PDFDeferredDeletion::installEventFilter(dialog, this, this);
    }
}

void PDFDocumentToolShell::onWatchedModalClosed()
{
    if (!m_isWaitingForModal || !m_pendingResult || m_state != State::Running)
    {
        return;
    }

    watchModalDialog(nullptr);
    m_isWaitingForModal = false;

    // The operation may have been canceled meanwhile (Stop of a dialog close-running): the result
    // has no document to go to
    const bool isHeld = m_runner && m_runner->getState() == PDFDocumentToolRunner::State::Waiting;
    const PDFDocumentToolResult result = isHeld ? *m_pendingResult : PDFDocumentToolResult::createStopped();
    m_pendingResult.reset();

    // Another modal dialog may have come: the result waits for it again
    finishRun(result);
}

void PDFDocumentToolShell::finishRun(const PDFDocumentToolResult& result)
{
    // The question of a file belongs to the run, which has ended
    closeFileConflictDialog();

    if (waitForModalDialog(result))
    {
        return;
    }

    showProgress(false);
    if (m_gui)
    {
        m_gui->setContentEnabled(true);
        m_gui->onRunStateChanged(false);
        if (PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }

    if (result.isSuccess())
    {
        // Blank PDF (4.6): no dialog, the new document replaces the own tab
        if (m_gui && m_gui->isResultDialogSkipped() && m_host && m_host->isOwnTab() && PDFAppToolResultDialog::getResultKind(result) == PDFAppToolResultDialog::ResultKind::NewDocument)
        {
            acceptWithoutDialog(result);
            return;
        }

        // Step 7 (plan 4.1, R7-2): an analysis shows its result itself and stays open
        if (m_gui && m_gui->isResultKeptInTool())
        {
            keepResult(result);
            return;
        }

        // The tool may ask first (307:6388, the partial result of Black & White): the result and
        // the operation of the runner wait for its answer (answerResult). The result is kept
        // before the foreign code, which may delete this shell.
        if (m_gui)
        {
            m_confirmingResult = result;
            const bool isAccepted = m_gui->confirmResult(result);
            if (PDFDeferredDeletion::isRetired(this) || !isAccepted)
            {
                return;
            }
            m_confirmingResult.reset();
        }

        openResultDialog(result, false);
        return;
    }

    // R3, 403:29978: a stopped run of more files says, which files it has written (they stay on
    // the disk); the tool keeps its settings after the dialog
    if (result.status == PDFDocumentToolResultStatus::Stopped && !result.writtenFiles.isEmpty() &&
        PDFAppToolResultDialog::getResultKind(result) == PDFAppToolResultDialog::ResultKind::WrittenFiles)
    {
        openResultDialog(result, true);
        return;
    }

    // A run, which has nothing to do, is a use of the tool (newapp_tools.md 3): the user has
    // run it and the tool has said its answer; a preview is none
    if (result.status == PDFDocumentToolResultStatus::NothingToDo)
    {
        recordUse();

        // The pair is recorded: the next tool over this document is not the first one after that
        // tool, nor is a second try of this tool a new pair (newapp_tools.md 3)
        if (PDFAppDocument* document = m_host ? m_host->getDocument() : nullptr)
        {
            document->setLastFinishedToolId(QString());
        }
    }

    setState(State::Editing);
    if (!PDFDeferredDeletion::isRetired(this) && m_gui && result.status != PDFDocumentToolResultStatus::Stopped)
    {
        m_gui->onRunNotAccepted(result);
    }
}

void PDFDocumentToolShell::answerResult(bool isAccepted)
{
    if (!m_confirmingResult || m_state != State::Running)
    {
        return;
    }

    const PDFDocumentToolResult result = std::move(*m_confirmingResult);
    m_confirmingResult.reset();

    // The operation may have ended meanwhile (the runner has gone): the result has no document to go to
    const bool isHeld = m_runner && m_runner->getState() == PDFDocumentToolRunner::State::Waiting;
    if (!isHeld)
    {
        finishRun(PDFDocumentToolResult::createStopped());
        return;
    }

    if (isAccepted)
    {
        openResultDialog(result, false);
        return;
    }

    // No, discard (307:6388): as Discard of the result dialog - the decision ends, the document
    // stays as it is
    m_runner->endDecision();
    if (PDFDeferredDeletion::isRetired(this))
    {
        return;
    }

    PDFAppToolResultOutcome outcome;
    outcome.action = PDFAppToolResultOutcome::Action::Discarded;
    onDialogDecided(outcome);
}

void PDFDocumentToolShell::recordUse()
{
    // The context of the entry (the host of an own tab may die before the shell, the children of its page; the
    // host of a document tab reads its tab)
    const PDFAppContext* context = m_context ? m_context.data() : (m_host ? m_host->getContext() : nullptr);
    const PDFAppDocument* document = m_host ? m_host->getDocument() : nullptr;
    if (!context || !context->getDatabase() || !m_tool)
    {
        return;
    }

    m_isUseRecorded = true;
    const PDFDatabaseError error = PDFDocumentToolUsage(context->getDatabase()).recordRun(m_tool->getId(), document ? document->getLastFinishedToolId() : QString());
    if (error.isError())
    {
        qWarning("The use of the tool was not recorded: %s", qPrintable(error.toString()));
    }
}

void PDFDocumentToolShell::onDocumentLost()
{
    // F2: the tool ends. From the event loop: the signal comes also from the destructor of
    // the document (its tab is going away, the page goes with it and nothing is left to do)
    QMetaObject::invokeMethod(this, &PDFDocumentToolShell::onDocumentLostQueued, Qt::QueuedConnection);
}

void PDFDocumentToolShell::onDocumentLostQueued()
{
    PDFAppToolCloseRequest request;
    request.reason = PDFAppToolCloseRequest::Reason::Lost;
    requestClose(request);
}

void PDFDocumentToolShell::onDocumentChanged()
{
    if (const PDFAppDocument* document = m_host ? m_host->getDocument() : nullptr)
    {
        m_knownDocumentVersion = document->getVersion();
    }

    // The owner password has lifted the restriction (or a version restricts it again):
    // the state is changed first, the hooks of the tool see it
    const bool wasRefused = m_isPermissionRefused;
    m_isPermissionRefused = isRefusedByPermissions();

    // R7-2: a result kept in the tool is of the earlier version (the state first, the hooks see it); the
    // status message goes to the host, whose slots may close the shell (retired: it follows nothing more)
    if (m_resultState == PDFAppToolResultState::Current)
    {
        setResultState(PDFAppToolResultState::Stale);
        if (PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }

    if (m_gui)
    {
        m_gui->onDocumentChanged();
    }
    if (PDFDeferredDeletion::isRetired(this))
    {
        return;
    }

    // Another document, the certification removed by another tool, Undo (owner 13. 11.)
    updateCertificationCallout(false);
    updateStatus();
    if (wasRefused != m_isPermissionRefused)
    {
        updateToolbar();
        if (!m_isPermissionRefused && !PDFDeferredDeletion::isRetired(this))
        {
            updatePreview();
        }
    }
}

void PDFDocumentToolShell::onDialogDecided(const PDFAppToolResultOutcome& outcome)
{
    const PDFAppToolResultOutcome decided = outcome;

    // The export of a result kept in the tool (step 7, R7-13): the tool stays, whatever was decided
    if (m_isExportDialog)
    {
        m_isExportDialog = false;
        if (decided.action != PDFAppToolResultOutcome::Action::Rerun)
        {
            finishExport(decided);
        }
        else
        {
            m_isRunExportDialog = false;
        }
        return;
    }

    // Retry N Files / Choose Folder… (339:7293, K4.11, R4-20): the tool stays with its settings and
    // runs again only for the files, which failed
    if (decided.action == PDFAppToolResultOutcome::Action::Retry)
    {
        m_isStoppedFilesResult = false;
        returnToEditing();
        if (!PDFDeferredDeletion::isRetired(this))
        {
            retryFiles(decided.retryFileNames, decided.retryFolder);
        }
        return;
    }

    // The written files of a stopped run: the tool stays with its settings (403:29978)
    if (m_isStoppedFilesResult)
    {
        m_isStoppedFilesResult = false;
        returnToEditing();
        return;
    }

    switch (decided.action)
    {
        case PDFAppToolResultOutcome::Action::Rerun:
            // The document has another version: the runner reports the new result
            return;

        case PDFAppToolResultOutcome::Action::Discarded:
        {
            // The own tab keeps the tool with its inputs and its parameters (Q9)
            if (m_host && m_host->isOwnTab())
            {
                returnToEditing();
                return;
            }

            // Discard result leaves the tool with what it has (the drawing of Sign by Hand, the marks
            // of Redact): its question comes as at Cancel (435:10500, 448:17473 "No redacted copy was
            // created yet"); the tool waits in Editing for the answer (closeAfterQuestion)
            if (askCloseQuestion())
            {
                if (!PDFDeferredDeletion::isRetired(this))
                {
                    returnToEditing();
                }
                return;
            }
            if (PDFDeferredDeletion::isRetired(this))
            {
                return;
            }

            PDFAppToolCloseRequest request;
            request.reason = PDFAppToolCloseRequest::Reason::Discarded;
            requestClose(request);
            return;
        }

        default:
            break;
    }

    // The result is accepted: the values become the last values of the tool, the pair of
    // the suggestions goes on
    if (m_model)
    {
        const PDFDatabaseError error = m_model->storeLastValues();
        if (error.isError())
        {
            qWarning("The last values of the tool were not stored: %s", qPrintable(error.toString()));
        }
    }
    if (m_host && m_tool)
    {
        // The next tool over the result of this one is the pair: the document, which holds
        // the result - this tab after Keep or Overwrite, the opened copy or new tab otherwise
        // (a copy, which was not opened, is nowhere to go on from)
        PDFAppDocument* resultDocument = nullptr;
        const PDFAppContext* context = m_host->getContext();
        switch (decided.action)
        {
            case PDFAppToolResultOutcome::Action::Kept:
            case PDFAppToolResultOutcome::Action::Overwritten:
                resultDocument = m_host->getDocument();
                break;

            default:
                if (context && context->getDocumentRegistry() && !decided.continueDocumentId.isEmpty())
                {
                    resultDocument = context->getDocumentRegistry()->findDocument(decided.continueDocumentId);
                }
                break;
        }

        // The tool has been used on the document of this tab: the next tool there is not the
        // first one after the tool before this one (newapp_tools.md 3)
        if (PDFAppDocument* document = m_host->getDocument())
        {
            document->setLastFinishedToolId(QString());
        }

        if (resultDocument)
        {
            resultDocument->setLastFinishedToolId(m_tool->getId());
        }
    }

    PDFAppToolCloseRequest request;
    request.reason = PDFAppToolCloseRequest::Reason::Accepted;
    request.continueToolId = decided.continueToolId;
    request.continueDocumentId = decided.continueDocumentId;
    if (decided.isOpenFolderRequested && !decided.filePath.isEmpty())
    {
        request.folderToOpen = QFileInfo(decided.filePath).absolutePath();
    }

    // R16 (336:3572): the new document replaces the own tab - its bytes, or the written file
    if (decided.action == PDFAppToolResultOutcome::Action::OpenedInThisTab)
    {
        request.newDocumentData = decided.newDocumentData;
        request.newDocumentTitle = decided.newDocumentTitle.isEmpty() ? tr("Untitled") : decided.newDocumentTitle;
    }
    if (decided.isFileOpenedInThisTab && !decided.filePath.isEmpty())
    {
        request.newDocumentFilePath = decided.filePath;
    }
    requestClose(request);
}

void PDFDocumentToolShell::onDialogStatusChanged()
{
    updateStatus();
}

void PDFDocumentToolShell::keepResult(const PDFDocumentToolResult& result)
{
    // The run is the use of the tool and its values are its last values, as of an accepted result; the
    // next tool over this document is not the first one after the tool before this one (newapp_tools.md 3)
    recordUse();
    if (m_model)
    {
        const PDFDatabaseError error = m_model->storeLastValues();
        if (error.isError())
        {
            qWarning("The last values of the tool were not stored: %s", qPrintable(error.toString()));
        }
    }
    if (PDFAppDocument* document = m_host ? m_host->getDocument() : nullptr)
    {
        document->setLastFinishedToolId(QString());
    }

    // The state first: the GUI sees the result as current
    m_resultState = PDFAppToolResultState::Current;
    const bool isExport = m_runner && PDFAppToolResultDialog::isAnalysisExport(m_runner->getOutputTraits(), result);

    if (m_gui)
    {
        m_gui->onResultKept(result);
        if (PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }

    // Document Report writes its file in the run (484:18811): the dialog of the export holds the decision
    // of the runner until it closes
    if (isExport && m_runner && m_runner->getState() == PDFDocumentToolRunner::State::Waiting)
    {
        m_isExportDialog = true;
        m_isRunExportDialog = true;

        // The activation of openResultDialog may delete the tab synchronously in a test (its guard; RC6-M1)
        QPointer<PDFDocumentToolShell> activateGuard(this);
        openResultDialog(result, false);
        if (!activateGuard)
        {
            return;
        }
        if (m_state != State::Deciding && !PDFDeferredDeletion::isRetired(this))
        {
            // The dialog was not opened (the host or the GUI is gone): the decision ends here
            m_isExportDialog = false;
            m_isRunExportDialog = false;
            if (m_runner)
            {
                m_runner->endDecision();
            }
            if (!PDFDeferredDeletion::isRetired(this))
            {
                setState(State::Editing);
            }
        }
        return;
    }

    if (m_runner)
    {
        // The slots of a resumed preview may close the shell (retired: it changes nothing more)
        m_runner->endDecision();
        if (PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }
    updateStaleCallout(false);
    setState(State::Editing);
}

void PDFDocumentToolShell::setResultState(PDFAppToolResultState state)
{
    if (m_resultState == state)
    {
        return;
    }

    m_resultState = state;
    updateStaleCallout(false);
    updateToolbar();
    updateStatus();
}

void PDFDocumentToolShell::markResultStale()
{
    if (m_resultState == PDFAppToolResultState::Current)
    {
        setResultState(PDFAppToolResultState::Stale);
    }
}

void PDFDocumentToolShell::updateStaleCallout(bool isPlacedAgain)
{
    const bool isShown = m_rightPanel && m_gui && m_state != State::Closed && !PDFDocumentToolShellHelper::isClosed(this) && m_resultState == PDFAppToolResultState::Stale;
    if (m_staleCallout && (!isShown || isPlacedAgain))
    {
        PDFDeferredDeletion::retire(m_staleCallout.data());
        m_staleCallout = nullptr;
    }
    if (!isShown || m_staleCallout)
    {
        return;
    }

    // 492:109399: the amber callout of the options, which changed; the GUI says what changed
    PDFAppToolCallout* callout = new PDFAppToolCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), m_gui->getStaleTitle(), m_gui->getStaleText(), m_rightPanel->getContentParent());
    callout->setObjectName(QStringLiteral("toolStaleCallout"));

    const PDFAppToolCalloutPlace place = m_gui->getStaleCalloutPlace();
    if (place.layout)
    {
        place.layout->insertWidget(place.index, callout);
    }
    else
    {
        // Around the content of the right panel: above it, or below it before the stretch
        QVBoxLayout* body = m_rightPanel->getBodyLayout();
        const int contentIndex = m_rightContent ? body->indexOf(m_rightContent.data()) : -1;
        int index = contentIndex >= 0 ? contentIndex : 0;
        if (place.index < 0)
        {
            index = contentIndex >= 0 ? contentIndex + 1 : body->count() - 1;
        }
        body->insertWidget(index, callout);
    }
    m_staleCallout = callout;
}

bool PDFDocumentToolShell::openExportDialog(const PDFDocumentToolResult& exportResult)
{
    if (m_state != State::Editing || !m_host || !m_gui || !m_runner || exportResult.outputs.size() != 1 || !exportResult.documents.empty())
    {
        return false;
    }

    // An export is an action of the user in an interactive tool (owner 2026-11-20)
    m_isInteractiveUsed = true;

    // The activation of the host runs slots of the application: the shell may be closed after it (a test deletes the
    // tab synchronously there: the guard stays, as in openResultDialog - RC6-M1)
    QPointer<PDFDocumentToolShell> activateGuard(this);
    m_isExportDialog = true;
    openResultDialog(exportResult, false);
    if (!activateGuard || PDFDeferredDeletion::isRetired(this))
    {
        return false;
    }
    if (m_state != State::Deciding)
    {
        m_isExportDialog = false;
        return false;
    }
    return true;
}

void PDFDocumentToolShell::finishExport(const PDFAppToolResultOutcome& outcome)
{
    // The file of a run (Document Report, 484:18811), which was not saved, is no result: nothing is kept, the
    // Primary button is the one of the ready state again (R7-2 disables it only after a result - else the tool
    // could not make its file again without a change of a setting; RS7APP). An export of a shown result keeps it.
    const bool isRunExport = m_isRunExportDialog;
    m_isRunExportDialog = false;
    if (isRunExport && outcome.action != PDFAppToolResultOutcome::Action::FileSaved && m_resultState == PDFAppToolResultState::Current)
    {
        m_resultState = PDFAppToolResultState::None;
    }

    // The tool stays with its result; the GUI learns the outcome last (it may open the written file)
    returnToEditing();
    if (m_gui && !PDFDeferredDeletion::isRetired(this))
    {
        m_gui->onExportDecided(outcome);
    }
}

void PDFDocumentToolShell::returnToEditing()
{
    // The dialog deletes itself; the runner has ended the decision already
    m_resultDialog = nullptr;
    setState(State::Editing);
    if (!PDFDeferredDeletion::isRetired(this))
    {
        updatePreview();
    }
}

void PDFDocumentToolShell::acceptWithoutDialog(const PDFDocumentToolResult& result)
{
    const PDFDocumentToolOutputDocument* output = nullptr;
    for (const PDFDocumentToolOutputDocument& document : result.documents)
    {
        if (document.kind == PDFDocumentToolOutputDocument::Kind::New && document.document)
        {
            output = &document;
            break;
        }
    }

    // The bytes measured by the run are the file (plan 4.5: one write)
    if (output && !output->writtenData.isEmpty())
    {
        finishAccepted(output->writtenData);
        return;
    }

    // Otherwise the writer writes the document in a worker; the run is not over until then
    // (the operation of the runner is held, Stop drops the write)
    if (output && m_runner)
    {
        m_skippedWriter = new PDFAppToolResultWriter(this);
        connect(m_skippedWriter.data(), &PDFAppToolResultWriter::finished, this, &PDFDocumentToolShell::onSkippedWriteFinished);
        if (m_skippedWriter->writeDocumentToMemory(output->document))
        {
            m_state = State::Running;
            updateToolbar();
            updateStatus();
            return;
        }
        delete m_skippedWriter.data();
    }

    PDFAppToolResultWriter::Result failure;
    failure.errorMessage = tr("The new document could not be written.");
    onSkippedWriteFinished(failure);
}

void PDFDocumentToolShell::onSkippedWriteFinished(const PDFAppToolResultWriter::Result& result)
{
    if (PDFAppToolResultWriter* writer = m_skippedWriter.data())
    {
        m_skippedWriter = nullptr;
        writer->disconnect(this);
        writer->deleteLater();
    }

    if (result.isWritten && !result.data.isEmpty())
    {
        finishAccepted(result.data);
        return;
    }

    // The tool stays open with the reason (a state of its panel, 4.6)
    if (m_runner)
    {
        m_runner->endDecision();
        if (PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }

    const PDFDocumentToolResult failed = PDFDocumentToolResult::createFailed(result.errorMessage.isEmpty() ? tr("The new document could not be written.") : result.errorMessage);
    setState(State::Editing);
    if (m_gui && !PDFDeferredDeletion::isRetired(this))
    {
        m_gui->onRunNotAccepted(failed);
    }
}

void PDFDocumentToolShell::finishAccepted(const QByteArray& data)
{
    if (m_runner)
    {
        m_runner->endDecision();
        if (PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }

    // The accepted result: the last values of the tool and its use (newapp_tools.md 3); no
    // pair of the suggestions (no Then continue with, K2)
    if (m_model)
    {
        const PDFDatabaseError error = m_model->storeLastValues();
        if (error.isError())
        {
            qWarning("The last values of the tool were not stored: %s", qPrintable(error.toString()));
        }
    }
    recordUse();

    PDFAppToolCloseRequest request;
    request.reason = PDFAppToolCloseRequest::Reason::Accepted;
    request.newDocumentData = data;
    request.newDocumentTitle = tr("Untitled");
    requestClose(request);
}

void PDFDocumentToolShell::onModeLeft()
{
    // Also from the destructor of the view, see onDocumentLost
    QMetaObject::invokeMethod(this, &PDFDocumentToolShell::onDocumentLostQueued, Qt::QueuedConnection);
}

void PDFDocumentToolShell::onLockChanged(bool isLocked)
{
    const PDFDocumentToolCatalogEntry* tool = getCatalogEntry();
    if (isLocked && tool && tool->getLockedAccess() == PDFDocumentToolLockedAccess::Unavailable)
    {
        PDFAppToolCloseRequest request;
        request.reason = PDFAppToolCloseRequest::Reason::Refused;
        request.statusMessage = tr("%1 was closed, because the document has been locked.").arg(tool->getTitle());
        requestClose(request);
    }
}

void PDFDocumentToolShell::onLanguageChanged()
{
    m_isRebuilding = false;
    if (m_state == State::Closed || m_state == State::Created || !m_gui || !m_tool || !m_model)
    {
        return;
    }

    m_gui->retranslate();
    m_model->updateDefinitionTexts(m_tool->getParameters());

    // The parts are made of translated texts; the values live in the model
    const bool isRunning = m_state == State::Running;
    showProgress(false);
    createContent();
    updateCertificationCallout(true);
    updateStaleCallout(true);
    if (isRunning)
    {
        m_gui->setContentEnabled(false);
        showProgress(true);
        updateProgress(m_percentage, m_stepText);
    }
    updateToolbar();
    updateStatus();
}

bool PDFDocumentToolShell::eventFilter(QObject* watched, QEvent* event)
{
    // The visibility of an interactive tool may change (its tab, its window): the time shown is accounted before
    // the change takes effect for the user; a key widget in another window is watched there (no signal, no foreign
    // code - also during the teardown of the tab, isToolVisible reads only the watched widgets)
    if (m_state != State::Created && m_state != State::Closed && isVisibilityEvent(watched, event))
    {
        if ((event->type() == QEvent::ParentChange || event->type() == QEvent::Show) && watched == m_visibleKeyWidget.data())
        {
            watchVisibleWindow();
        }
        accountVisibleTime();
    }

    // Also a dialog only hidden: the event comes before the dialog leaves the list of the modal widgets
    if (watched == m_watchedModal.data() && (event->type() == QEvent::Hide || event->type() == QEvent::Close))
    {
        QMetaObject::invokeMethod(this, &PDFDocumentToolShell::onWatchedModalClosed, Qt::QueuedConnection);
    }

    // The toolbar is a part of the tool always shown: one change of the language
    if (watched == m_toolbar && event->type() == QEvent::LanguageChange && !m_isRebuilding)
    {
        m_isRebuilding = true;
        QMetaObject::invokeMethod(this, &PDFDocumentToolShell::onLanguageChanged, Qt::QueuedConnection);
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFDocumentToolShell::retire()
{
    // The inert part of close (see the declaration): no signal of the shell, no answer of a question, no report of a
    // leave. The runner, the GUI, the parts, the overlay and the dialogs of the tool are retired with the shell; the
    // token of the mode gives the view back after the hooks.
    if (m_state == State::Closed)
    {
        return;
    }

    const bool wasEntered = m_state != State::Created;
    m_state = State::Closed;
    m_isCloseRequested = true;
    unwatchVisibility();

    // A result, which waits for a modal dialog or for the answer of the GUI, has no tool to go to
    m_pendingResult.reset();
    m_confirmingResult.reset();
    m_isWaitingForModal = false;
    watchModalDialog(nullptr);

    // A leave waiting for an answer of this shell learns its end by PDFAppLeaveFlowStep (aboutToBeDeleted of the
    // shell, refused from the event loop): nothing is reported here; the questions are retired with their owners
    m_leaveFlow = nullptr;
    m_leaveQuestion = nullptr;
    m_closeQuestion = nullptr;
    ++m_leaveWaitId;
    m_isAskingLeave = false;
    m_isAskingClose = false;
    clearLeaveAnswer();
    m_isIncompleteQuestionPending = false;
    stopWaitingForOtherQuestion();

    // The info bar of a result of the tool belongs to the tool (the host may be retired with the page: its view is not)
    if (wasEntered && m_host)
    {
        m_host->hideInfoBar(QStringLiteral("tool-result"));
    }
}

void PDFDocumentToolShell::onFileQuestionChanged()
{
    const std::optional<PDFAppToolFileSink::Question> question = m_runner ? m_runner->getFileQuestion() : std::nullopt;
    if (!question || m_state != State::Running || !m_host)
    {
        closeFileConflictDialog();
        return;
    }
    if (m_fileConflictDialog && m_fileConflictDialog->getQuestion() == *question)
    {
        return;
    }
    closeFileConflictDialog();

    // 356:11997: the run waits, the question is a modal dialog over the window (new + QPointer,
    // it deletes itself)
    QWidget* keyWidget = m_host->getKeyWidget();
    PDFAppToolFileConflictDialog* dialog = new PDFAppToolFileConflictDialog(*question, keyWidget ? keyWidget->window() : nullptr);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_fileConflictDialog = dialog;
    PDFDeferredDeletion::setOwner(dialog, this);
    connect(dialog, &PDFAppToolFileConflictDialog::answered, this, &PDFDocumentToolShell::onFileConflictAnswered);
    dialog->QDialog::open();
}

void PDFDocumentToolShell::onFileConflictAnswered(PDFAppToolFileSink::Answer answer, bool isForAll)
{
    m_fileConflictDialog = nullptr;
    if (m_runner)
    {
        m_runner->answerFileQuestion(answer, isForAll);
    }
}

void PDFDocumentToolShell::closeFileConflictDialog()
{
    // The dialog answers nothing any more: the run has ended or is canceled (retired: closed without its answer at
    // once, deleted later)
    if (PDFAppToolFileConflictDialog* dialog = m_fileConflictDialog.data())
    {
        m_fileConflictDialog = nullptr;
        dialog->disconnect(this);
        PDFDeferredDeletion::retire(dialog);
    }
}

}   // namespace pdf

#include "moc_pdfdocumenttoolshell.cpp"
