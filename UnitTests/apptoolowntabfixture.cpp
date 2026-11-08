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

#include "apptoolowntabfixture.h"

#include "pdfappmainwindow.h"
#include "pdfapptoolinputlist.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolregistry.h"

#include "pdfcatalog.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfoperationcontrol.h"

#include <QBuffer>
#include <QLabel>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>

namespace pdftest
{

pdf::PDFDocumentToolParameterDefinitions OwnTabTestTool::getParameters() const
{
    using Definition = pdf::PDFDocumentToolParameterDefinition;

    pdf::PDFDocumentToolParameterDefinitions definitions;
    std::vector<pdf::PDFDocumentToolParameterOption> modes = {
        { QStringLiteral("new"), QT_TR_NOOP("New document") },
        { QStringLiteral("partial"), QT_TR_NOOP("Partial result") },
        { QStringLiteral("fail"), QT_TR_NOOP("Fail") },
    };
    definitions.push_back(Definition::createEnum(tr, QStringLiteral("mode"), QT_TR_NOOP("Mode"), QT_TR_NOOP("What the tool does"), std::move(modes), QStringLiteral("new")));
    definitions.push_back(Definition::createInteger(tr, QStringLiteral("pages"), QT_TR_NOOP("Pages"), QT_TR_NOOP("Pages without an input"), 1, 1, 100));
    return definitions;
}

bool OwnTabTestTool::waitWhileHeld(const pdf::PDFOperationControl* control) const
{
    ++m_heldCount;
    while (m_hold.load())
    {
        if (pdf::PDFOperationControl::isOperationCancelled(control))
        {
            --m_heldCount;
            return false;
        }
        QThread::msleep(1);
    }
    --m_heldCount;
    return true;
}

pdf::PDFDocumentPointer OwnTabTestTool::createDocument(int pageCount)
{
    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    for (int i = 0; i < pageCount; ++i)
    {
        builder.appendPage(QRectF(0, 0, 595, 842));
    }
    return pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build()));
}

pdf::PDFDocumentToolResult OwnTabTestTool::computePreview(const pdf::PDFDocumentToolContext& context, const QVariantMap& parameters, const pdf::PDFOperationControl* control, pdf::PDFProgressSink* progress) const
{
    Q_UNUSED(parameters);
    Q_UNUSED(control);
    Q_UNUSED(progress);
    ++m_previewCount;
    m_lastInputCount.store(int(context.documents.size()));

    pdf::PDFDocumentToolResult result;
    result.facts.push_back({ QStringLiteral("inputs"), QStringLiteral("Inputs"), QString::number(context.documents.size()) });
    return result;
}

pdf::PDFDocumentToolResult OwnTabTestTool::execute(const pdf::PDFDocumentToolContext& context, const QVariantMap& parameters, const pdf::PDFOperationControl* control, pdf::PDFProgressSink* progress) const
{
    ++m_executeCount;
    m_lastInputCount.store(int(context.documents.size()));
    if (!waitWhileHeld(control))
    {
        return pdf::PDFDocumentToolResult::createStopped();
    }

    const QString mode = parameters.value(QStringLiteral("mode")).toString();
    if (mode == QLatin1String("fail"))
    {
        return pdf::PDFDocumentToolResult::createFailed(QStringLiteral("Failed on purpose"));
    }

    // The pages of the inputs (the last one left out of a partial result)
    const bool isPartial = mode == QLatin1String("partial");
    const size_t usedInputs = isPartial && !context.documents.empty() ? context.documents.size() - 1 : context.documents.size();
    int pageCount = 0;
    for (size_t i = 0; i < usedInputs; ++i)
    {
        pageCount += int(context.documents[i].document->getCatalog()->getPageCount());
    }
    if (context.documents.empty())
    {
        pageCount = parameters.value(QStringLiteral("pages")).toInt();
    }
    if (progress)
    {
        progress->start(1, pdf::ProgressStartupInfo());
        progress->step();
        progress->finish();
    }

    pdf::PDFDocumentToolOutputDocument output;
    output.kind = pdf::PDFDocumentToolOutputDocument::Kind::New;
    output.document = createDocument(qMax(1, pageCount));
    output.suggestedFileName = QStringLiteral("merged.pdf");

    // Bytes of another document: the caller opens these, never a second write
    if (const int writtenPages = m_writtenPages.load(); writtenPages > 0)
    {
        const pdf::PDFDocumentPointer written = createDocument(writtenPages);
        QBuffer buffer(&output.writtenData);
        buffer.open(QIODevice::WriteOnly);
        pdf::PDFDocumentWriter writer(nullptr);
        writer.write(&buffer, written.data());
    }

    pdf::PDFDocumentToolResult result;
    result.documents.push_back(output);
    result.facts.push_back({ QStringLiteral("pages"), QStringLiteral("Pages"), QString::number(pageCount) });
    if (isPartial && !context.documents.empty())
    {
        pdf::PDFDocumentToolLeftOutInput leftOut;
        leftOut.inputIndex = int(context.documents.size() - 1);
        leftOut.name = context.documents.back().fileName.isEmpty() ? QStringLiteral("Annex_A.pdf") : context.documents.back().fileName;
        leftOut.pageCount = context.documents.back().document->getCatalog()->getPageCount();
        leftOut.reason = QStringLiteral("The file cannot be read: test");
        result.leftOutInputs.push_back(leftOut);
    }
    return result;
}

OwnTabTestGUI::OwnTabTestGUI(bool isDialogSkipped, QString title) :
    m_isDialogSkipped(isDialogSkipped),
    m_title(std::move(title))
{

}

QWidget* OwnTabTestGUI::createToolActions(QWidget* parent)
{
    QToolButton* button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("ownTabTestAction"));
    button->setText(QStringLiteral("Action"));
    return button;
}

QWidget* OwnTabTestGUI::createRightPanel(QWidget* parent)
{
    rightLabel = new QLabel(QStringLiteral("Right"), parent);
    rightLabel->setObjectName(QStringLiteral("ownTabTestRight"));
    return rightLabel;
}

QWidget* OwnTabTestGUI::createLeftPanel(QWidget* parent)
{
    QLabel* label = new QLabel(QStringLiteral("Files"), parent);
    label->setObjectName(QStringLiteral("ownTabTestLeft"));
    return label;
}

QWidget* OwnTabTestGUI::createCanvasPage(QWidget* parent)
{
    canvasPage = new QWidget(parent);
    canvasPage->setObjectName(QStringLiteral("ownTabTestCanvas"));
    canvasPage->setFocusPolicy(Qt::StrongFocus);
    return canvasPage;
}

bool OwnTabTestGUI::isConfirmEnabled() const
{
    // K22: a tool of more inputs needs a usable input (the test tool one, Merge two)
    const pdf::PDFDocumentToolCatalogEntry* tool = getTool();
    if (tool && tool->getInputKind() == pdf::PDFDocumentToolInputKind::Multiple)
    {
        const pdf::PDFAppToolInputList* list = getInputList();
        if (!list || !list->isRunnable(1))
        {
            return false;
        }
    }
    return PDFDocumentToolGUI::isConfirmEnabled();
}

QString OwnTabTestGUI::getConfirmText() const
{
    return QStringLiteral("Create PDF");
}

void OwnTabTestGUI::prepareEnvironment(pdf::PDFAppToolRunEnvironment* environment) const
{
    // R16: the inputs of the list go to the run as the documents of the context
    if (const pdf::PDFAppToolInputList* list = getInputList())
    {
        for (int i = 0; i < list->getCount(); ++i)
        {
            const pdf::PDFAppToolInput* input = list->getInput(i);
            if (input && input->isUsable() && input->isPdf())
            {
                environment->additionalDocuments.push_back(input->createToolInputDocument());
            }
        }
    }
}

QString OwnTabTestGUI::getOwnTabTitle() const
{
    return m_title.isEmpty() ? PDFDocumentToolGUI::getOwnTabTitle() : m_title;
}

QString OwnTabTestGUI::getOwnTabIconName() const
{
    return m_title.isEmpty() ? PDFDocumentToolGUI::getOwnTabIconName() : QStringLiteral("file");
}

QString OwnTabTestGUI::getOwnTabLossText() const
{
    const pdf::PDFAppToolInputList* list = getInputList();
    const int count = list ? list->getCount() : 0;
    return count > 0 ? QStringLiteral("The list of %1 files is lost.").arg(count) : QString();
}

QString OwnTabTestGUI::getOwnTabPageText() const
{
    const pdf::PDFAppToolInputList* list = getInputList();
    return list ? QStringLiteral("%1 pages").arg(list->getTotalPageCount()) : QString();
}

void OwnTabTestGUI::onRunNotAccepted(const pdf::PDFDocumentToolResult& result)
{
    ++notAcceptedCount;
    PDFDocumentToolGUI::onRunNotAccepted(result);
}

AppToolOwnTabFixture::AppToolOwnTabFixture(QSize size) :
    AppToolShellFixture(size)
{

}

AppToolOwnTabFixture::~AppToolOwnTabFixture()
{
    // The tool tabs go before their tools (a page asks the registry in its destructor)
    if (window)
    {
        for (pdf::PDFAppToolTab* tab : window->getToolTabs())
        {
            tab->setCloseConfirmed(true);
            window->closeTab(tab);
        }
        settle();
    }

    for (const RegisteredTool& item : m_registeredTools)
    {
        pdf::PDFDocumentToolGUIRegistry::unregisterFactory(item.id);
        pdf::PDFDocumentToolRegistry::unregisterTool(item.id);
        if (item.previousTool)
        {
            pdf::PDFDocumentToolRegistry::registerTool(item.previousTool);
        }
        if (item.previousFactory)
        {
            pdf::PDFDocumentToolGUIRegistry::registerFactory(item.id, item.previousFactory);
        }
    }
}

std::shared_ptr<OwnTabTestTool> AppToolOwnTabFixture::registerOwnTabTool(const QString& toolId, bool isDialogSkipped, const QString& title)
{
    RegisteredTool item;
    item.id = toolId;
    item.previousTool = pdf::PDFDocumentToolRegistry::getTool(toolId);
    item.previousFactory = pdf::PDFDocumentToolGUIRegistry::getFactory(toolId);
    pdf::PDFDocumentToolRegistry::unregisterTool(toolId);
    pdf::PDFDocumentToolGUIRegistry::unregisterFactory(toolId);
    m_registeredTools.push_back(item);

    std::shared_ptr<OwnTabTestTool> tool = std::make_shared<OwnTabTestTool>(toolId);
    if (pdf::PDFDocumentToolRegistry::registerTool(tool) != pdf::PDFDocumentToolRegistration::Registered ||
        !pdf::PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<OwnTabTestGUIFactory>(isDialogSkipped, title)))
    {
        return nullptr;
    }
    return tool;
}

pdf::PDFAppToolTab* AppToolOwnTabFixture::createToolTab(const QString& toolId, pdf::PDFAppToolInputList* inputList)
{
    QPointer<pdf::PDFAppToolTab> tab = window ? window->createToolTab(toolId, inputList) : nullptr;
    settle();
    return tab.data();
}

pdf::PDFDocumentToolShell* AppToolOwnTabFixture::getShell(const pdf::PDFAppToolTab* tab)
{
    return tab ? tab->getShell() : nullptr;
}

OwnTabTestGUI* AppToolOwnTabFixture::getOwnTabGUI(const pdf::PDFAppToolTab* tab)
{
    pdf::PDFDocumentToolShell* shell = getShell(tab);
    return shell ? qobject_cast<OwnTabTestGUI*>(shell->getGUI()) : nullptr;
}

}   // namespace pdftest
