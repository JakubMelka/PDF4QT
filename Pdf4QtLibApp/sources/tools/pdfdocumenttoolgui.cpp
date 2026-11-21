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

#include "pdfdocumenttoolgui.h"

#include "pdfappdocument.h"
#include "pdfappmainwindow.h"
#include "pdfapppage.h"
#include "pdfapptooldragsource.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"

#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttooltablewriter.h"
#include "pdfmessagebox.h"
#include "pdfthemeiconengine.h"

#include <QPushButton>

#include <limits>

namespace pdf
{

PDFDocumentToolGUI::PDFDocumentToolGUI(QObject* parent) :
    BaseClass(parent)
{

}

PDFDocumentToolGUI::~PDFDocumentToolGUI()
{
    // The generic question goes with the tool (its answer would reach nobody); the shell waiting for it
    // (requestLeave) learns it by the end of the dialog
    if (PDFMessageBox* question = m_closeQuestion.data())
    {
        m_closeQuestion = nullptr;
        question->disconnect(this);
        question->close();
    }
}

PDFDocumentToolShell* PDFDocumentToolGUI::getShell() const
{
    return m_shell.data();
}

void PDFDocumentToolGUI::attach(PDFDocumentToolShell* shell)
{
    m_shell = shell;

    // The inputs, which the own tab came with (the start page, the document of the tab): no work of the user
    const PDFAppToolInputList* list = getInputList();
    m_entryInputIds = list ? list->getInputIds() : QStringList();
}

bool PDFDocumentToolGUI::isInputListChanged() const
{
    const PDFAppToolInputList* list = getInputList();
    return list && list->getInputIds() != m_entryInputIds;
}

PDFAppToolHost* PDFDocumentToolGUI::getHost() const
{
    return m_shell ? m_shell->getHost() : nullptr;
}

PDFAppContext* PDFDocumentToolGUI::getContext() const
{
    const PDFAppToolHost* host = getHost();
    return host ? host->getContext() : nullptr;
}

PDFAppDocument* PDFDocumentToolGUI::getDocument() const
{
    const PDFAppToolHost* host = getHost();
    return host ? host->getDocument() : nullptr;
}

PDFAppToolParameterModel* PDFDocumentToolGUI::getModel() const
{
    return m_shell ? m_shell->getModel() : nullptr;
}

PDFDocumentToolRunner* PDFDocumentToolGUI::getRunner() const
{
    return m_shell ? m_shell->getRunner() : nullptr;
}

PDFAppToolToolbar* PDFDocumentToolGUI::getToolbar() const
{
    return m_shell ? m_shell->getToolbar() : nullptr;
}

const PDFDocumentToolCatalogEntry* PDFDocumentToolGUI::getTool() const
{
    return m_shell ? m_shell->getCatalogEntry() : nullptr;
}

QWidget* PDFDocumentToolGUI::createToolActions(QWidget* parent)
{
    Q_UNUSED(parent);
    return nullptr;
}

QWidget* PDFDocumentToolGUI::createRightPanel(QWidget* parent)
{
    Q_UNUSED(parent);
    return nullptr;
}

QWidget* PDFDocumentToolGUI::createLeftPanel(QWidget* parent)
{
    Q_UNUSED(parent);
    return nullptr;
}

QWidget* PDFDocumentToolGUI::createCanvasPage(QWidget* parent)
{
    Q_UNUSED(parent);
    return nullptr;
}

QWidget* PDFDocumentToolGUI::createOverlay(QWidget* canvasArea)
{
    Q_UNUSED(canvasArea);
    return nullptr;
}

int PDFDocumentToolGUI::getRightPanelSpacing() const
{
    return 14;
}

QString PDFDocumentToolGUI::getInitialSidebarPanel() const
{
    return QString();
}

PDFAppToolProgressPlace PDFDocumentToolGUI::getProgressPlace() const
{
    return PDFAppToolProgressPlace();
}

PDFAppToolCalloutPlace PDFDocumentToolGUI::getCertificationCalloutPlace() const
{
    return PDFAppToolCalloutPlace();
}

void PDFDocumentToolGUI::setContentEnabled(bool isEnabled)
{
    if (!m_shell)
    {
        return;
    }

    for (QWidget* widget : { m_shell->getToolActions(), m_shell->getRightContent(), m_shell->getLeftContent(), m_shell->getCanvasPage() })
    {
        if (widget)
        {
            widget->setEnabled(isEnabled);
        }
    }
}

QString PDFDocumentToolGUI::getConfirmText() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    return tool ? tool->getTitle() : QString();
}

QString PDFDocumentToolGUI::getConfirmIconName() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    return tool ? tool->getIconName() : QString();
}

bool PDFDocumentToolGUI::isConfirmEnabled() const
{
    if (!m_inputErrors.empty())
    {
        return false;
    }
    const PDFAppToolParameterModel* model = getModel();
    return !model || model->getErrors().empty();
}

QString PDFDocumentToolGUI::getStatusText() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const PDFAppDocument* document = getDocument();
    const QString title = tool ? tool->getTitle() : QString();
    if (!document)
    {
        return title;
    }

    if (m_shell && m_shell->isPermissionRefused())
    {
        return tr("%1 · the document does not allow this tool").arg(title);
    }

    const PDFAppDocumentSnapshot snapshot = document->getSnapshot();
    QString text = QStringLiteral("%1 · %2").arg(title, document->getTitle());
    const QString detail = getStatusDetailText();
    if (!detail.isEmpty())
    {
        text += QStringLiteral(" · ") + detail;
    }
    if (!snapshot.originalData.isEmpty() && isFileSizeInStatusText())
    {
        text += QStringLiteral(" · ") + PDFDocumentToolFormat::formatSize(snapshot.originalData.size());
    }
    return text;
}

QString PDFDocumentToolGUI::getStatusDetailText() const
{
    const PDFAppDocument* document = getDocument();
    const PDFInteger pageCount = document ? document->getPageCount() : 0;
    if (pageCount <= 0)
    {
        return QString();
    }

    // The singular has its own text: right in English also without a translation
    const int count = int(qMin<PDFInteger>(pageCount, std::numeric_limits<int>::max()));
    return count == 1 ? tr("%Ln page", nullptr, count) : tr("%Ln pages", nullptr, count);
}

QString PDFDocumentToolGUI::getRunningStatusText(const QString& stepText) const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();
    return stepText.isEmpty() ? tr("%1 · press Stop or Esc to stop").arg(title) : tr("%1: %2 · press Stop or Esc to stop").arg(title, stepText);
}

QString PDFDocumentToolGUI::getRunningTitle(int percentage) const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    return tr("%1… %2 %").arg(tool ? tool->getTitle() : QString()).arg(percentage);
}

QString PDFDocumentToolGUI::getRunningCardTitle(int percentage, const QString& stepText) const
{
    Q_UNUSED(stepText);
    return getRunningTitle(percentage);
}

QString PDFDocumentToolGUI::getRunningCardStepText(const QString& stepText) const
{
    return stepText;
}

QString PDFDocumentToolGUI::getRunningTimeText(int seconds) const
{
    return PDFDocumentToolShell::formatRemainingTime(seconds);
}

QString PDFDocumentToolGUI::getBusyText() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    if (!tool || tool->getResultKind() != PDFDocumentToolResultKind::ChangeDocument)
    {
        return QString();
    }
    return tr("It cannot be edited and no other tool can run on it until %1 finishes. Other tabs stay available. Stop ends the job and writes nothing.").arg(tool->getTitle());
}

QString PDFDocumentToolGUI::getPermissionTitle() const
{
    return tr("This document does not allow this tool");
}

QString PDFDocumentToolGUI::getPermissionSubtitle() const
{
    return tr("Its author restricted what can be done with it.");
}

QString PDFDocumentToolGUI::getPermissionText() const
{
    return tr("The restriction can be removed in Restrict Permissions if you know the owner password. The document can still be read on screen.");
}

std::vector<PDFSecurityHandler::Permission> PDFDocumentToolGUI::getRunPermissions() const
{
    return { };
}

void PDFDocumentToolGUI::fillResultInput(PDFAppToolResultInput* input) const
{
    Q_UNUSED(input);
}

bool PDFDocumentToolGUI::confirmResult(const PDFDocumentToolResult& result)
{
    Q_UNUSED(result);
    return true;
}

bool PDFDocumentToolGUI::isResultDialogSkipped() const
{
    return false;
}

QString PDFDocumentToolGUI::getOwnTabTitle() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    return tool ? tool->getTitle() : QString();
}

QString PDFDocumentToolGUI::getOwnTabIconName() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    return tool ? tool->getIconName() : QString();
}

QString PDFDocumentToolGUI::getOwnTabLossText() const
{
    return QString();
}

QString PDFDocumentToolGUI::getLockedGlanceText() const
{
    return QString();
}

QString PDFDocumentToolGUI::getStartPageText(PDFAppToolStartPageText text) const
{
    Q_UNUSED(text);
    return QString();
}

QString PDFDocumentToolGUI::getOwnTabPageText() const
{
    return QString();
}

QString PDFDocumentToolGUI::getOwnTabZoomText() const
{
    return QString();
}

bool PDFDocumentToolGUI::hasOwnStatusItems() const
{
    return false;
}

PDFAppToolInputList* PDFDocumentToolGUI::getInputList() const
{
    const PDFAppToolHost* host = getHost();
    return host ? host->getInputList() : nullptr;
}

PDFAppToolInputRules PDFDocumentToolGUI::getInputRules() const
{
    // The rules of today (Merge: 2 to 1000 PDF files and images); a tool of the catalog with
    // more inputs has the range of its entry (K41)
    PDFAppToolInputRules rules;
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const PDFDocumentToolInputCountRange range = tool ? tool->getInputCountRange() : PDFDocumentToolInputCountRange();
    if (range.maximum > 1)
    {
        rules.minimum = range.minimum;
        rules.maximum = range.maximum;
    }
    return rules;
}

bool PDFDocumentToolGUI::isResultInThisTab() const
{
    return false;
}

bool PDFDocumentToolGUI::isTabKeptAfterResult() const
{
    return false;
}

bool PDFDocumentToolGUI::handlesPermissionRefusal() const
{
    return false;
}

bool PDFDocumentToolGUI::hasOwnTabCloseQuestion() const
{
    return false;
}

PDFAppLeaveResult PDFDocumentToolGUI::requestOwnTabClose(PDFAppLeaveFlow* flow)
{
    Q_UNUSED(flow);
    return PDFAppLeaveResult::Done;
}

QString PDFDocumentToolGUI::getCancelText() const
{
    return QString();
}

void PDFDocumentToolGUI::closeOwnTab()
{
    // Cancel of the toolbar closes the own tab (PDFAppToolTabPage::onCloseRequested): the last
    // statement, the slots may delete the shell and this GUI
    PDFDocumentToolShell* shell = getShell();
    const PDFAppToolHost* host = getHost();
    if (shell && host && host->isOwnTab() && shell->getState() == PDFDocumentToolShell::State::Editing)
    {
        shell->cancel();
    }
}

bool PDFDocumentToolGUI::isSingleOwnTab() const
{
    return false;
}

bool PDFDocumentToolGUI::hasUnsavedWork() const
{
    return false;
}

bool PDFDocumentToolGUI::isInteractive() const
{
    return false;
}

void PDFDocumentToolGUI::markUsed()
{
    if (PDFDocumentToolShell* shell = getShell())
    {
        shell->markInteractiveUse();
    }
}

PDFAppToolCloseQuestion PDFDocumentToolGUI::getCloseQuestion() const
{
    return PDFAppToolCloseQuestion();
}

bool PDFDocumentToolGUI::applyCloseQuestionAction()
{
    return false;
}

QDialog* PDFDocumentToolGUI::askClose()
{
    if (PDFMessageBox* openQuestion = m_closeQuestion.data())
    {
        if (openQuestion->isVisible())
        {
            openQuestion->raise();
            return openQuestion;
        }

        // An answered question is hidden until its deletion (WA_DeleteOnClose): it is no open question
        // (RESCCODE L1) - a new one is asked
        openQuestion->disconnect(this);
        m_closeQuestion = nullptr;
    }

    const PDFAppToolCloseQuestion question = getCloseQuestion();
    if (!question.isValid())
    {
        return nullptr;
    }

    // 435:10500: Keep (Primary) and Discard (trash) trailing; with a third choice 448:17473: Keep leading, the
    // choice as the Primary button, Discard. The dialog belongs to the window (it deletes itself when closed,
    // the destructor of this GUI closes it); open - never exec.
    QWidget* parent = getHost() && getHost()->getKeyWidget() ? getHost()->getKeyWidget()->window() : nullptr;
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Warning, question.title, question.text, QDialogButtonBox::NoButton, parent);
    dialog->setObjectName(QStringLiteral("toolCloseQuestion"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const bool hasAction = !question.actionText.isEmpty();
    QPushButton* keepButton = dialog->addActionButton(question.keepText, hasAction ? PDFMessageBox::ActionPosition::Leading : PDFMessageBox::ActionPosition::Trailing, !hasAction);
    keepButton->setObjectName(QStringLiteral("toolCloseKeep"));
    connect(keepButton, &QPushButton::clicked, dialog, &QDialog::reject);
    if (hasAction)
    {
        QPushButton* actionButton = dialog->addActionButton(question.actionText, PDFMessageBox::ActionPosition::Trailing, true);
        actionButton->setObjectName(QStringLiteral("toolCloseAction"));
        if (!question.actionIconName.isEmpty())
        {
            actionButton->setIcon(PDFThemeIcons::getIcon(question.actionIconName));
        }
        connect(actionButton, &QPushButton::clicked, this, &PDFDocumentToolGUI::onCloseQuestionActionClicked);
    }
    QPushButton* discardButton = dialog->addActionButton(question.discardText, PDFMessageBox::ActionPosition::Trailing, false);
    discardButton->setObjectName(QStringLiteral("toolCloseDiscard"));
    discardButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("trash")));
    connect(discardButton, &QPushButton::clicked, this, &PDFDocumentToolGUI::onCloseQuestionDiscardClicked);
    m_closeQuestion = dialog;

    if (parent)
    {
        PDFAppMainWindow::prepareDialog(dialog, parent);
    }
    QPointer<PDFDocumentToolGUI> guard(this);
    dialog->open();
    return guard ? m_closeQuestion.data() : nullptr;
}

void PDFDocumentToolGUI::onCloseQuestionDiscardClicked()
{
    QPointer<PDFDocumentToolGUI> guard(this);
    if (PDFMessageBox* dialog = m_closeQuestion.data())
    {
        m_closeQuestion = nullptr;
        dialog->disconnect(this);
        dialog->accept();
    }

    // The last call: the shell and this GUI go (as Cancel, without the question again)
    PDFDocumentToolShell* shell = guard ? getShell() : nullptr;
    if (shell)
    {
        shell->closeAfterQuestion();
    }
}

void PDFDocumentToolGUI::onCloseQuestionActionClicked()
{
    QPointer<PDFDocumentToolGUI> guard(this);
    if (PDFMessageBox* dialog = m_closeQuestion.data())
    {
        m_closeQuestion = nullptr;
        dialog->disconnect(this);
        dialog->accept();
    }
    if (!guard)
    {
        return;
    }

    // A leave waits for the answer (the close of the tab, the quit, the lock, another route): the shell only
    // remembers the choice and accepts the leave - the code, which started the leave, applies it, when the leave
    // really goes on (PDFDocumentToolShell::finishLeave; owner 2026-11-20, decision 8)
    PDFDocumentToolShell* shell = getShell();
    if (shell && shell->deferCloseQuestionAction())
    {
        return;
    }

    // The work is kept elsewhere (a commit: the slots of the document run); the tool closes only when all of
    // it was kept - the rest stays in the open tool
    const bool isKept = applyCloseQuestionAction();
    shell = guard ? getShell() : nullptr;
    if (!shell)
    {
        return;
    }

    // The last call either way: the tool closes, or the GUI asks about the work, which could not be kept (owner
    // 2026-11-21, askIncompleteClose) - its closing answer closes the tool later
    if (isKept)
    {
        shell->closeAfterQuestion();
    }
    else
    {
        shell->askIncompleteQuestion();
    }
}

QDialog* PDFDocumentToolGUI::askIncompleteClose()
{
    return nullptr;
}

bool PDFDocumentToolGUI::handleEscape()
{
    return false;
}

QString PDFDocumentToolGUI::getEscapeModeName() const
{
    return QString();
}

QString PDFDocumentToolGUI::getEscapeModeHint() const
{
    const QString modeName = getEscapeModeName();
    return modeName.isEmpty() ? QString() : tr("Esc ends %1").arg(modeName);
}

QString PDFDocumentToolGUI::getEscapeHint() const
{
    // The order of the shell: a drag of a widget, then the GUI (handleEscape), then the tool
    const PDFAppToolHost* host = getHost();
    if (IPDFAppToolDragSource::findActive(host ? host->getKeyWidget() : nullptr))
    {
        return tr("Esc cancels the drag");
    }
    const QString modeHint = getEscapeModeHint();
    if (!modeHint.isEmpty())
    {
        return modeHint;
    }
    return tr("Esc leaves the tool");
}

bool PDFDocumentToolGUI::isConfirmShown() const
{
    return true;
}

bool PDFDocumentToolGUI::isUndoShown() const
{
    return true;
}

bool PDFDocumentToolGUI::hasDocumentUndoSteps() const
{
    return false;
}

bool PDFDocumentToolGUI::commitDocumentChange(const PDFModifiedDocument& modified, const QString& description, quint64 expectedVersion)
{
    // The shell records the step for Undo of the tool after the commit (the slots of the document
    // may delete the shell and this GUI: the shell checks itself)
    PDFDocumentToolShell* shell = getShell();
    return shell && shell->commitDocumentChange(modified, description, expectedVersion);
}

bool PDFDocumentToolGUI::isResultKeptInTool() const
{
    return false;
}

void PDFDocumentToolGUI::onResultKept(const PDFDocumentToolResult& result)
{
    Q_UNUSED(result);
}

bool PDFDocumentToolGUI::isResultStaleBy(const QString& parameterName) const
{
    // The format is the one of a later export, the shown result does not depend on it
    return parameterName != QLatin1String(PDFDocumentToolTableWriter::PARAMETER_FORMAT);
}

QString PDFDocumentToolGUI::getStaleTitle() const
{
    return tr("The document changed");
}

QString PDFDocumentToolGUI::getStaleText() const
{
    // 492:109399 "The shown differences come from the previous settings. Press Compare Again to refresh them."
    return tr("The shown result comes from an earlier version. Press %1 to refresh it.").arg(getConfirmText());
}

PDFAppToolCalloutPlace PDFDocumentToolGUI::getStaleCalloutPlace() const
{
    return PDFAppToolCalloutPlace();
}

void PDFDocumentToolGUI::onExportDecided(const PDFAppToolResultOutcome& outcome)
{
    Q_UNUSED(outcome);
}

PDFAppToolResultState PDFDocumentToolGUI::getResultState() const
{
    const PDFDocumentToolShell* shell = getShell();
    return shell ? shell->getResultState() : PDFAppToolResultState::None;
}

void PDFDocumentToolGUI::markResultStale()
{
    if (PDFDocumentToolShell* shell = getShell())
    {
        shell->markResultStale();
    }
}

bool PDFDocumentToolGUI::openExport(const PDFDocumentToolResult& exportResult)
{
    PDFDocumentToolShell* shell = getShell();
    return shell && shell->openExportDialog(exportResult);
}

QVariantMap PDFDocumentToolGUI::getRunParameters() const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? model->getValues() : QVariantMap();
}

QVariantMap PDFDocumentToolGUI::getPreviewParameters() const
{
    return getRunParameters();
}

void PDFDocumentToolGUI::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    Q_UNUSED(environment);
}

void PDFDocumentToolGUI::onParametersChanged(const QString& name)
{
    Q_UNUSED(name);
}

void PDFDocumentToolGUI::onPreviewChanged(const PDFDocumentToolResult* preview)
{
    Q_UNUSED(preview);
}

void PDFDocumentToolGUI::onPreviewProgress(int percentage, const QString& stepText)
{
    Q_UNUSED(percentage);
    Q_UNUSED(stepText);
}

void PDFDocumentToolGUI::onRunStateChanged(bool isRunning)
{
    Q_UNUSED(isRunning);
}

void PDFDocumentToolGUI::onRunRefused()
{

}

bool PDFDocumentToolGUI::confirmWithoutRun()
{
    return false;
}

void PDFDocumentToolGUI::onRunNotAccepted(const PDFDocumentToolResult& result)
{
    PDFAppToolHost* host = getHost();
    if (!host)
    {
        return;
    }

    PDFAppInfoBarRequest request;
    request.id = QStringLiteral("tool-result");
    request.tone = result.status == PDFDocumentToolResultStatus::Failed ? PDFAppInfoBarTone::Error : PDFAppInfoBarTone::Information;
    request.title = result.status == PDFDocumentToolResultStatus::Failed ? tr("The tool has failed") : tr("Nothing to do");
    request.text = result.message;
    host->showInfoBar(request);
}

void PDFDocumentToolGUI::onDocumentChanged()
{

}

void PDFDocumentToolGUI::retranslate()
{

}

void PDFDocumentToolGUI::setInputError(const QString& key, const QString& error)
{
    const auto it = m_inputErrors.find(key);
    const bool hadError = it != m_inputErrors.end();
    if (error.isEmpty())
    {
        if (hadError)
        {
            m_inputErrors.erase(it);
            updateConfirmState();
        }
        return;
    }

    m_inputErrors[key] = error;
    if (!hadError)
    {
        updateConfirmState();
    }
}

void PDFDocumentToolGUI::updateConfirmState()
{
    Q_EMIT confirmStateChanged();
}

void PDFDocumentToolGUI::updateStatusText()
{
    Q_EMIT statusTextChanged();
}

void PDFDocumentToolGUI::updateOwnTabState()
{
    Q_EMIT ownTabStateChanged();
}

PDFDocumentToolGUIFactory::~PDFDocumentToolGUIFactory() = default;

}   // namespace pdf

#include "moc_pdfdocumenttoolgui.cpp"
