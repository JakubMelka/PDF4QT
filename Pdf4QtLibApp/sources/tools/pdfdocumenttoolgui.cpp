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
#include "pdfapptoolhost.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"

#include "pdfdocumenttoolcatalog.h"

#include <limits>

namespace pdf
{

PDFDocumentToolGUI::PDFDocumentToolGUI(QObject* parent) :
    BaseClass(parent)
{

}

PDFDocumentToolGUI::~PDFDocumentToolGUI() = default;

PDFDocumentToolShell* PDFDocumentToolGUI::getShell() const
{
    return m_shell.data();
}

void PDFDocumentToolGUI::attach(PDFDocumentToolShell* shell)
{
    m_shell = shell;
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
    const PDFInteger pageCount = document->getPageCount();
    if (pageCount > 0)
    {
        // The singular has its own text: right in English also without a translation
        const int count = int(qMin<PDFInteger>(pageCount, std::numeric_limits<int>::max()));
        text += QStringLiteral(" · ") + (count == 1 ? tr("%Ln page", nullptr, count) : tr("%Ln pages", nullptr, count));
    }
    if (!snapshot.originalData.isEmpty())
    {
        text += QStringLiteral(" · ") + PDFDocumentToolFormat::formatSize(snapshot.originalData.size());
    }
    return text;
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

void PDFDocumentToolGUI::fillResultInput(PDFAppToolResultInput* input) const
{
    Q_UNUSED(input);
}

QVariantMap PDFDocumentToolGUI::getRunParameters() const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? model->getValues() : QVariantMap();
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

void PDFDocumentToolGUI::onRunNotAccepted(const PDFDocumentToolResult& result)
{
    // The shell has its dialog Not enough memory for every tool
    PDFAppToolHost* host = getHost();
    if (!host || result.isOutOfMemory())
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

PDFDocumentToolGUIFactory::~PDFDocumentToolGUIFactory() = default;

}   // namespace pdf

#include "moc_pdfdocumenttoolgui.cpp"
