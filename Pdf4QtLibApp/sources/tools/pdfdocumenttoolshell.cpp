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
#include "pdfapppagelayout.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptooloutputfiles.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolgui.h"

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

PDFDocumentToolShell::PDFDocumentToolShell(PDFAppToolHost* host, std::shared_ptr<const PDFDocumentTool> tool, PDFDocumentToolGUI* gui, QObject* parent) :
    BaseClass(parent),
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
    // second time, the lifetime sweep of the own tab of a tool, step 2 of phase 2)
    blockSignals(true);
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
    PDFAppToolParameterModel* model = m_model.data();
    m_model = nullptr;
    delete model;
}

const PDFDocumentToolCatalogEntry* PDFDocumentToolShell::getCatalogEntry() const
{
    return m_tool ? m_tool->getInfo() : nullptr;
}

bool PDFDocumentToolShell::enter()
{
    if (m_state != State::Created || !m_host || !m_tool || !m_gui || !getCatalogEntry())
    {
        return false;
    }

    PDFAppDocument* document = m_host->getDocument();
    QPointer<PDFDocumentToolShell> guard(this);

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
    connect(m_gui.data(), &PDFDocumentToolGUI::statusTextChanged, this, &PDFDocumentToolShell::updateStatus);

    m_isPermissionRefused = isRefusedByPermissions();

    // The parts wait in a hidden holder, until the view takes them (and it gives them back)
    m_partsParent = new QWidget();
    m_partsParent->setObjectName(QStringLiteral("toolPartsHolder"));
    m_toolbar = new PDFAppToolToolbar(m_host, *getCatalogEntry(), m_partsParent);
    connect(m_toolbar.data(), &PDFAppToolToolbar::cancelClicked, this, &PDFDocumentToolShell::cancel);
    connect(m_toolbar.data(), &PDFAppToolToolbar::confirmClicked, this, &PDFDocumentToolShell::confirm);
    connect(m_toolbar.data(), &PDFAppToolToolbar::stopClicked, this, &PDFDocumentToolShell::stop);
    m_toolbar->installEventFilter(this);

    createContent();

    PDFAppToolModeParts parts;
    parts.toolbar = m_toolbar;
    parts.leftPanel = m_leftPanel;
    parts.canvasPage = m_canvasPage;
    parts.rightPanel = m_rightPanel;
    parts.undoTarget = m_model;

    m_host->saveViewState();

    const bool isEntered = m_host->enterMode(parts);
    if (!guard)
    {
        return false;
    }
    if (!isEntered)
    {
        m_state = State::Closed;
        return false;
    }

    m_state = State::Editing;
    connect(m_host.data(), &PDFAppToolHost::modeLeft, this, &PDFDocumentToolShell::onModeLeft);
    connect(m_host.data(), &PDFAppToolHost::documentNotReady, this, &PDFDocumentToolShell::onDocumentLost);
    connect(m_host.data(), &PDFAppToolHost::lockChanged, this, &PDFDocumentToolShell::onLockChanged);
    if (document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFDocumentToolShell::onDocumentChanged);
    }

    // The inputs of a tool of more inputs in its own tab (K33): a change of their content
    // gives a new generation, the preview is computed again
    if (PDFAppToolInputList* inputList = m_host->getInputList())
    {
        connect(inputList, &PDFAppToolInputList::inputsChanged, this, &PDFDocumentToolShell::onInputsChanged);
        connect(inputList, &PDFAppToolInputList::inputChanged, this, &PDFDocumentToolShell::onInputsChanged);
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
        if (!guard)
        {
            return false;
        }
    }

    updateToolbar();
    updateStatus();
    Q_EMIT stateChanged(m_state);
    if (!guard)
    {
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
    return guard != nullptr;
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
        m_rightPanel = new PDFAppToolSidePanel(PDFAppToolSidePanel::Side::Right, spacing, m_partsParent);
        m_rightPanel->setObjectName(QStringLiteral("toolRightPanel"));
        m_leftPanel = new PDFAppToolSidePanel(PDFAppToolSidePanel::Side::Left, 10, m_partsParent);
        m_leftPanel->setObjectName(QStringLiteral("toolLeftPanel"));
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
        m_canvasPage = m_gui->createCanvasPage(m_partsParent);
        if (m_host && m_host->getCanvasArea())
        {
            m_overlay = m_gui->createOverlay(m_host->getCanvasArea());
        }
    }
}

void PDFDocumentToolShell::close()
{
    if (m_state == State::Closed)
    {
        return;
    }

    const bool wasEntered = m_state != State::Created;
    m_state = State::Closed;
    QPointer<PDFDocumentToolShell> guard(this);

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
        // The question of a run, which has ended without the user: its own loop ends
        m_leaveDialog->reject();
    }
    if (m_permissionDialog)
    {
        m_permissionDialog->disconnect(this);
        m_permissionDialog->hide();
        m_permissionDialog->deleteLater();
    }

    // The close of the tab during the question of a file = Stop Export (R3): the deletion of
    // the runner cancels the run, which stops the sink and wakes the waiting thread
    closeFileConflictDialog();
    if (m_runner)
    {
        // Its deletion cancels the worker, ends the operation and closes the dialog; a slot
        // of its end may delete this shell, so the pointer is cleared first
        m_runner->disconnect(this);
        PDFDocumentToolRunner* runner = m_runner.data();
        m_runner = nullptr;
        runner->setParent(nullptr);
        delete runner;
        if (!guard)
        {
            return;
        }
    }
    if (QWidget* overlay = m_overlay.data())
    {
        m_overlay = nullptr;
        delete overlay;
    }

    if (wasEntered && m_host)
    {
        m_isLeavingMode = true;
        m_host->leaveMode();
        if (!guard)
        {
            return;
        }
        m_isLeavingMode = false;
        if (m_host)
        {
            m_host->restoreViewState();
        }
        if (!guard)
        {
            return;
        }
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

    switch (m_state)
    {
        case State::Running:
            m_toolbar->setMode(PDFAppToolToolbar::Mode::Running);
            break;
        case State::Deciding:
            m_toolbar->setMode(PDFAppToolToolbar::Mode::Finished);
            break;
        default:
            m_toolbar->setMode(PDFAppToolToolbar::Mode::Editing);
            break;
    }

    m_toolbar->setConfirmText(m_gui->getConfirmText(), m_gui->getConfirmIconName());
    m_toolbar->setConfirmEnabled(!m_isPermissionRefused && m_gui->isConfirmEnabled());
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
        // Pages "12 pages", 1479:34632); the other tools have none and keep "Page 1 / 10".
        if (m_gui && m_host)
        {
            m_host->setStatusItems(m_gui->getOwnTabPageText(), m_gui->getOwnTabZoomText());
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
    // Primary button, the status and a new preview
    QPointer<PDFDocumentToolShell> guard(this);
    updateToolbar();
    updateStatus();
    if (guard && m_state == State::Editing)
    {
        updatePreview();
    }

    // The title and the dot of the own tab follow the list (K16); the tab only reads
    if (guard && m_gui)
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

    if (m_state != State::Editing || !m_gui || !m_runner || !m_host || m_isPermissionRefused || !m_gui->isConfirmEnabled() || m_runPermissionDialog)
    {
        return;
    }

    // A permission, which only the chosen values need (Remove Hidden Data), is asked as every
    // other action asks it (owner 2026-11-08): the owner password dialog of the shared check, or
    // its refusal; the user confirms again after the unlock. The dialog runs no nested event
    // loop, the function ends right after it is opened (the shell is checked after the foreign
    // code of the dialog anyway; the dialog is owned by its window and deletes itself on close).
    QPointer<PDFDocumentToolShell> guard(this);
    const std::vector<PDFSecurityHandler::Permission> runPermissions = m_gui->getRunPermissions();
    PDFAppDocument* permissionDocument = m_host->getDocument();
    if (!runPermissions.empty() && permissionDocument)
    {
        QWidget* parent = m_host->getKeyWidget() ? m_host->getKeyWidget()->window() : nullptr;
        QDialog* permissionDialog = nullptr;
        const bool isAllowed = PDFAppDocumentPermissions::request(permissionDocument, PDFAppPermissionRequest::createTool(getCatalogEntry()->getId(), runPermissions, m_gui->getRunParameters()), parent, &permissionDialog);
        if (!guard || !m_host || !m_runner || m_state != State::Editing)
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
        if (!guard || !m_runner || !m_host || m_state != State::Editing)
        {
            return;
        }
    }

    // A value being typed is a value of the run: Ctrl+Enter does not move the focus
    commitPendingValues();
    if (!guard || !m_runner || !m_host || !m_gui || m_state != State::Editing || !m_gui->isConfirmEnabled())
    {
        return;
    }

    applyEnvironment();
    if (!m_runner->run(m_gui->getRunParameters()) && guard && m_host)
    {
        // A foreign blocking operation, a lost document: the commit would be refused
        m_host->showTransientMessage(tr("The document is busy with another operation. Try it again when it ends."));
    }
}

void PDFDocumentToolShell::commitPendingValues()
{
    QPointer<PDFDocumentToolShell> guard(this);
    for (const QPointer<QWidget>& part : { m_toolActions, m_rightContent, m_leftContent, m_canvasPage })
    {
        if (!part)
        {
            continue;
        }

        // A write runs the slots of the model, which may delete the controls
        const QList<PDFAppSettingControl*> controls = part->findChildren<PDFAppSettingControl*>();
        std::vector<QPointer<PDFAppSettingControl>> guards(controls.cbegin(), controls.cend());
        for (const QPointer<PDFAppSettingControl>& control : guards)
        {
            if (control)
            {
                control->commitPendingValue();
            }
            if (!guard)
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
        QPointer<PDFDocumentToolShell> guard(this);
        m_skippedWriter = nullptr;
        writer->disconnect(this);
        writer->deleteLater();
        m_runner->endDecision();
        if (guard)
        {
            finishRun(PDFDocumentToolResult::createStopped());
        }
        return;
    }

    // The result waits for a modal dialog of another window: the run has ended, Stop drops it
    if (m_isWaitingForModal && m_pendingResult)
    {
        QPointer<PDFDocumentToolShell> guard(this);
        m_pendingResult.reset();
        m_isWaitingForModal = false;
        watchModalDialog(nullptr);
        m_runner->endDecision();
        if (guard)
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
        PDFAppToolCloseRequest request;
        request.reason = PDFAppToolCloseRequest::Reason::Canceled;
        requestClose(request);
    }
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
    QPointer<PDFDocumentToolShell> guard(this);
    dialog->reject();
    if (!guard || isCancelClose || m_state != State::Editing)
    {
        return;
    }

    PDFAppToolCloseRequest request;
    request.reason = PDFAppToolCloseRequest::Reason::Canceled;
    requestClose(request);
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

    if (m_state == State::Running)
    {
        stop();
    }
    else
    {
        cancel();
    }
}

bool PDFDocumentToolShell::confirmLeave()
{
    switch (m_state)
    {
        case State::Created:
        case State::Closed:
        case State::Editing:
            return true;

        case State::Deciding:
            // The modal dialog decides: nothing goes around it
            return false;

        case State::Running:
            break;
    }

    if (m_isAskingLeave || !m_host)
    {
        return false;
    }

    // The tab is shown, so the user sees, what the question is about
    QPointer<PDFDocumentToolShell> guard(this);
    m_host->activate();
    if (!guard || !m_host)
    {
        return false;
    }

    const PDFDocumentToolCatalogEntry* tool = getCatalogEntry();
    m_isAskingLeave = true;
    PDFAppToolLeaveRunningDialog* dialog = new PDFAppToolLeaveRunningDialog(tool ? tool->getTitle() : QString(), m_host->getDocument(), m_percentage, m_host->getKeyWidget());
    m_leaveDialog = dialog;
    const PDFAppDocumentChoice choice = PDFAppDocumentChoiceDialog::run(dialog);
    if (!guard)
    {
        return false;
    }
    m_isAskingLeave = false;

    if (m_state == State::Closed)
    {
        // The tool has ended meanwhile (F2, a lock): its page goes on
        return false;
    }

    if (choice != PDFAppDocumentChoice::Stop)
    {
        // Keep Running: a result, which came while the user was asked, is decided now
        if (m_pendingResult)
        {
            const PDFDocumentToolResult result = *m_pendingResult;
            m_pendingResult.reset();
            finishRun(result);
        }
        return false;
    }

    m_pendingResult.reset();
    return true;
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
    delete m_progressCard.data();
    delete m_busyCallout.data();
    if (!isShown || !m_gui || !m_rightPanel)
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
    if (!m_host || !m_runner || !m_gui)
    {
        return;
    }

    // The run may have gone on behind another tab (F1): the user decides about a document, which is shown
    QPointer<PDFDocumentToolShell> activateGuard(this);
    m_host->activate();
    if (!activateGuard || !m_host || !m_runner || !m_gui)
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
    connect(dialog, &PDFAppToolResultDialog::decided, this, &PDFDocumentToolShell::onDialogDecided);
    connect(dialog, &PDFAppToolResultDialog::statusTextChanged, this, &PDFDocumentToolShell::onDialogStatusChanged);
    updateStatus();

    QPointer<PDFDocumentToolShell> guard(this);
    dialog->QDialog::open();
    if (guard)
    {
        Q_EMIT stateChanged(m_state);
    }
}

void PDFDocumentToolShell::showPermissionRefusal()
{
    if (m_state == State::Closed || !m_gui || !m_host || m_permissionDialog)
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
            m_permissionDialog = box;
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
    QPointer<PDFDocumentToolShell> guard(this);
    if (m_gui)
    {
        m_gui->onParametersChanged(name);
    }
    if (guard)
    {
        updateToolbar();
        updateStatus();
        updatePreview();
    }
}

void PDFDocumentToolShell::onConfirmStateChanged()
{
    updateToolbar();
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
    // The hook says, that the Primary button has changed: a slot may delete this shell
    QPointer<PDFDocumentToolShell> guard(this);
    if (m_gui)
    {
        m_gui->onPreviewChanged(&result);
    }
    if (guard && m_toolbar)
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

    QPointer<PDFDocumentToolShell> guard(this);
    if (m_gui)
    {
        m_gui->setContentEnabled(false);
        showProgress(true);
        m_gui->onRunStateChanged(true);
        if (!guard)
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
        dialog->installEventFilter(this);
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

    QPointer<PDFDocumentToolShell> guard(this);
    showProgress(false);
    if (m_gui)
    {
        m_gui->setContentEnabled(true);
        m_gui->onRunStateChanged(false);
        if (!guard)
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
    if (guard && m_gui && result.status != PDFDocumentToolResultStatus::Stopped)
    {
        m_gui->onRunNotAccepted(result);
    }
}

void PDFDocumentToolShell::recordUse()
{
    const PDFAppContext* context = m_host ? m_host->getContext() : nullptr;
    const PDFAppDocument* document = m_host ? m_host->getDocument() : nullptr;
    if (!context || !context->getDatabase() || !m_tool)
    {
        return;
    }

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
    // The owner password has lifted the restriction (or a version restricts it again):
    // the state is changed first, the hooks of the tool see it
    const bool wasRefused = m_isPermissionRefused;
    m_isPermissionRefused = isRefusedByPermissions();

    QPointer<PDFDocumentToolShell> guard(this);
    if (m_gui)
    {
        m_gui->onDocumentChanged();
    }
    if (!guard)
    {
        return;
    }

    updateStatus();
    if (wasRefused != m_isPermissionRefused)
    {
        updateToolbar();
        if (guard && !m_isPermissionRefused)
        {
            updatePreview();
        }
    }
}

void PDFDocumentToolShell::onDialogDecided(const PDFAppToolResultOutcome& outcome)
{
    const PDFAppToolResultOutcome decided = outcome;
    QPointer<PDFDocumentToolShell> guard(this);

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

void PDFDocumentToolShell::returnToEditing()
{
    // The dialog deletes itself; the runner has ended the decision already
    QPointer<PDFDocumentToolShell> guard(this);
    m_resultDialog = nullptr;
    setState(State::Editing);
    if (guard)
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
    QPointer<PDFDocumentToolShell> guard(this);
    if (m_runner)
    {
        m_runner->endDecision();
        if (!guard)
        {
            return;
        }
    }

    const PDFDocumentToolResult failed = PDFDocumentToolResult::createFailed(result.errorMessage.isEmpty() ? tr("The new document could not be written.") : result.errorMessage);
    setState(State::Editing);
    if (guard && m_gui)
    {
        m_gui->onRunNotAccepted(failed);
    }
}

void PDFDocumentToolShell::finishAccepted(const QByteArray& data)
{
    QPointer<PDFDocumentToolShell> guard(this);
    if (m_runner)
    {
        m_runner->endDecision();
        if (!guard)
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
    // The dialog answers nothing any more: the run has ended or is canceled
    if (PDFAppToolFileConflictDialog* dialog = m_fileConflictDialog.data())
    {
        m_fileConflictDialog = nullptr;
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }
}

}   // namespace pdf

#include "moc_pdfdocumenttoolshell.cpp"
