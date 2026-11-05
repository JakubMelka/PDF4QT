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

#include "pdfapptoolgui_structure.h"

#include "pdfappdocument.h"
#include "pdfapplocale.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolparametermodel.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfcomponentbuttons.h"

#include <QClipboard>
#include <QGuiApplication>

#include <algorithm>
#include <limits>

namespace pdf
{

/// Helpers of the tool Optimize Structure in the shell
class PDFAppToolGUIStructureHelper
{
public:
    PDFAppToolGUIStructureHelper() = delete;

    /// A count for %Ln
    static int toCount(qint64 count)
    {
        return int(std::clamp(count, qint64(0), qint64(std::numeric_limits<int>::max())));
    }
};

PDFAppToolGUIStructure::PDFAppToolGUIStructure(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolGUIStructure::~PDFAppToolGUIStructure() = default;

void PDFAppToolGUIStructure::registerTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolStructure::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolStructure>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIStructure>>());
    }
}

int PDFAppToolGUIStructure::getCheckedStepCount() const
{
    const PDFAppToolParameterModel* model = getModel();
    int count = 0;
    for (const PDFDocumentToolStructure::Step& step : PDFDocumentToolStructure::getSteps())
    {
        count += model && model->getValue(QString::fromLatin1(step.parameter)).toBool() ? 1 : 0;
    }
    return count;
}

std::vector<PDFAppToolFlagItem> PDFAppToolGUIStructure::getMenuItems() const
{
    // panel-steps 303:4560: the texts of the parameters, the icons of the steps
    const PDFAppToolParameterModel* model = getModel();
    std::vector<PDFAppToolFlagItem> items;
    for (const PDFDocumentToolStructure::Step& step : PDFDocumentToolStructure::getSteps())
    {
        const QString parameter = QString::fromLatin1(step.parameter);
        const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), parameter) : nullptr;

        PDFAppToolFlagItem item;
        item.parameter = parameter;
        item.title = definition ? definition->getTitle() : PDFDocumentToolStructure::getStepTitle(step);
        item.description = definition ? definition->getDescription() : PDFDocumentToolStructure::getStepDescription(step);
        item.iconName = QString::fromLatin1(step.iconName);
        items.push_back(std::move(item));
    }
    return items;
}

std::vector<PDFAppToolFlagItem> PDFAppToolGUIStructure::getStepItems()
{
    // steps 319:7288: the short texts, a step off "Will be skipped" (319:7386)
    std::vector<PDFAppToolFlagItem> items;
    for (const PDFDocumentToolStructure::Step& step : PDFDocumentToolStructure::getSteps())
    {
        PDFAppToolFlagItem item;
        item.parameter = QString::fromLatin1(step.parameter);
        item.title = PDFDocumentToolStructure::getStepTitle(step);
        item.description = PDFDocumentToolStructure::getStepDescription(step);
        item.iconName = QString::fromLatin1(step.iconName);
        item.offDescription = tr("Will be skipped");
        items.push_back(std::move(item));
    }
    return items;
}

int PDFAppToolGUIStructure::getRightPanelSpacing() const
{
    return 16;
}

QString PDFAppToolGUIStructure::getInitialSidebarPanel() const
{
    return QStringLiteral("thumbnails");
}

QString PDFAppToolGUIStructure::getConfirmIconName() const
{
    // btn-confirm 302:4105: the icon layers (the tool icon optimize-structure is in the title)
    return QStringLiteral("layers");
}

bool PDFAppToolGUIStructure::isConfirmEnabled() const
{
    // No frame: Clear leaves nothing to run, the core refuses it too
    return BaseClass::isConfirmEnabled() && getCheckedStepCount() > 0;
}

QString PDFAppToolGUIStructure::getStatusDetailText() const
{
    // 302:4182: "Optimize Structure · Project_Report.pdf · 48,210 objects · 24.8 MB"
    const int count = PDFAppToolGUIStructureHelper::toCount(m_counts.objects);
    return count == 1 ? tr("%Ln object", nullptr, count) : tr("%Ln objects", nullptr, count);
}

void PDFAppToolGUIStructure::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    // tool-actions 302:4092: Steps (panel-steps 303:4560), the divider, Select all, Clear
    m_toolActions = builder.getWidget();
    m_stepsMenu = builder.addFlagsMenu(getMenuItems(), QStringLiteral("list"), tr("Steps: %1 of %2"), STEPS_PANEL_WIDTH, tr("Optimization steps"));
    if (PDFAppToolPanelPopup* panel = m_stepsMenu ? m_stepsMenu->getPanel() : nullptr)
    {
        panel->addDividerRow();
        m_restoreLink = panel->addLink(tr("Restore defaults"));
        m_restoreLink->setObjectName(QStringLiteral("toolStepsRestoreDefaults"));
        connect(m_restoreLink.data(), &PDFTextLink::clicked, m_stepsMenu.data(), &PDFAppToolFlagsMenu::restoreDefaults);
    }
    builder.addActionDivider();
    m_selectAllButton = builder.addAction(tr("Select all"), QStringLiteral("check-circle"), this, &PDFAppToolGUIStructure::selectAllSteps);
    m_clearButton = builder.addAction(tr("Clear"), QStringLiteral("x-circle"), this, &PDFAppToolGUIStructure::clearSteps);

    // 303:4897: nothing to choose below the result dialog (also for actions made again then)
    const PDFDocumentToolShell* shell = getShell();
    if (shell && shell->getState() == PDFDocumentToolShell::State::Deciding && m_toolActions)
    {
        m_toolActions->setEnabled(false);
    }
}

void PDFAppToolGUIStructure::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    updateCounts();

    // section-Document 302:4546
    m_documentHeader = builder.addSection(tr("Document"));
    m_documentFacts = builder.addKeyValues();
    m_documentFacts->setObjectName(QStringLiteral("toolStructureDocumentFacts"));

    // section-Steps to run 302:4562 with the count 302:4565
    m_stepsHeader = builder.addSection(tr("Steps to run"));
    m_stepList = builder.addSteps(getStepItems());

    // section-Result 302:4591: a text until the run
    m_resultHeader = builder.addSection(tr("Result"));
    m_resultText = builder.addTextCard(QString());
    m_resultText->setObjectName(QStringLiteral("toolStructureResultText"));

    // The result of the dialog 303:4897: Result 303:4998 and Steps performed 303:5370
    m_doneResultHeader = builder.addSection(tr("Result"));
    m_doneResultHeader->setObjectName(QStringLiteral("toolStructureDoneResultHeader"));
    m_resultRows = builder.addKeyValues();
    m_resultRows->setObjectName(QStringLiteral("toolStructureResultRows"));
    m_performedHeader = builder.addSection(tr("Steps performed"));
    m_performedHeader->setObjectName(QStringLiteral("toolStructurePerformedHeader"));
    m_reportLink = new PDFTextLink(tr("Copy report"), m_performedHeader);
    m_reportLink->setObjectName(QStringLiteral("toolStructureCopyReport"));
    connect(m_reportLink.data(), &PDFTextLink::clicked, this, &PDFAppToolGUIStructure::onCopyReportClicked);
    m_performedHeader->addTrailingWidget(m_reportLink);
    m_performedList = builder.addSteps(getStepItems());
    m_performedList->setObjectName(QStringLiteral("toolStructurePerformedSteps"));
    builder.endSection();

    updateParts();
}

QWidget* PDFAppToolGUIStructure::createOverlay(QWidget* canvasArea)
{
    if (!canvasArea)
    {
        return nullptr;
    }

    // chip-lossless 302:4542 over the canvas
    m_chip = new PDFAppToolCanvasChip(canvasArea);
    if (PDFDocumentToolShell* shell = getShell())
    {
        connect(shell, &PDFDocumentToolShell::stateChanged, this, &PDFAppToolGUIStructure::onShellStateChanged);
    }
    updateParts();
    return m_chip.data();
}

void PDFAppToolGUIStructure::updateCounts()
{
    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    m_counts = PDFDocumentToolStructure::getCounts(pdfDocument.data());
}

void PDFAppToolGUIStructure::updateDocumentFacts()
{
    if (!m_documentFacts)
    {
        return;
    }

    // document-facts 302:4549: File size, Objects, Streams, PDF version
    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    const qint64 fileSize = document ? document->getSnapshot().originalData.size() : 0;
    const PDFVersion version = pdfDocument ? pdfDocument->getInfo()->version : PDFVersion();

    const QStringList keys = { tr("File size"), tr("Objects"), tr("Streams"), tr("PDF version") };
    const QStringList values = { fileSize > 0 ? PDFDocumentToolFormat::formatSize(fileSize) : QStringLiteral("–"),
                                 PDFAppLocale::formatNumber(m_counts.objects),
                                 PDFAppLocale::formatNumber(m_counts.streams),
                                 version.isValid() ? QStringLiteral("%1.%2").arg(version.major).arg(version.minor) : QStringLiteral("–") };

    // The same rows are not laid out again
    if (m_documentFacts->getRowCount() != keys.size())
    {
        m_documentFacts->clear();
        for (qsizetype i = 0; i < keys.size(); ++i)
        {
            m_documentFacts->addRow(keys[i], values[i]);
        }
        return;
    }
    for (qsizetype i = 0; i < keys.size(); ++i)
    {
        m_documentFacts->setValue(int(i), values[i]);
    }
}

void PDFAppToolGUIStructure::updateParts()
{
    const bool isDone = m_result.has_value();
    const int checkedCount = getCheckedStepCount();
    const int stepCount = int(PDFDocumentToolStructure::getSteps().size());

    // The parts of the editing (302:4072) or of the result of the dialog (303:4897)
    const std::initializer_list<QWidget*> editingParts = { m_documentHeader.data(), m_documentFacts ? m_documentFacts->parentWidget() : nullptr, m_stepsHeader.data(),
                                                           m_stepList.data(), m_resultHeader.data(), m_resultText ? m_resultText->parentWidget() : nullptr };
    const std::initializer_list<QWidget*> doneParts = { m_doneResultHeader.data(), m_resultRows ? m_resultRows->parentWidget() : nullptr, m_performedHeader.data(), m_performedList.data() };
    for (QWidget* widget : editingParts)
    {
        if (widget)
        {
            widget->setVisible(!isDone);
        }
    }
    for (QWidget* widget : doneParts)
    {
        if (widget)
        {
            widget->setVisible(isDone);
        }
    }

    updateDocumentFacts();
    if (m_stepsHeader)
    {
        m_stepsHeader->setHint(tr("%1 of %2").arg(checkedCount).arg(stepCount));
    }
    if (m_resultText)
    {
        if (checkedCount == 0)
        {
            m_resultText->setText(tr("Choose at least one step to run."));
        }
        else if (!m_notReducedText.isEmpty())
        {
            m_resultText->setText(m_notReducedText);
        }
        else
        {
            m_resultText->setText(tr("Run the tool to see how much the file shrinks. The steps are lossless and can be undone."));
        }
    }

    if (isDone && m_resultRows)
    {
        m_resultRows->clear();
        for (const PDFDocumentToolFact& row : PDFDocumentToolStructure::getResultRows(*m_result))
        {
            m_resultRows->addRow(row.title, row.value);
        }
    }
    if (m_performedList)
    {
        std::vector<PDFAppToolStepResult> results;
        if (isDone)
        {
            for (const PDFDocumentToolStructure::StepRow& row : PDFDocumentToolStructure::getStepRows(*m_result))
            {
                results.push_back({ row.parameter, row.text, row.isRun });
            }
        }
        m_performedList->setResults(std::move(results));
    }

    if (m_chip)
    {
        m_chip->setChip(tr("Lossless · pages look exactly the same"), QStringLiteral("shield-check"));
    }
}

void PDFAppToolGUIStructure::selectAllSteps()
{
    // One undo step of the tool
    if (m_stepsMenu)
    {
        m_stepsMenu->setAll(true);
    }
}

void PDFAppToolGUIStructure::clearSteps()
{
    if (m_stepsMenu)
    {
        m_stepsMenu->setAll(false);
    }
}

void PDFAppToolGUIStructure::onParametersChanged(const QString& name)
{
    Q_UNUSED(name);

    // A run of the old steps said nothing about the new ones
    m_notReducedText.clear();
    updateParts();
}

void PDFAppToolGUIStructure::onRunNotAccepted(const PDFDocumentToolResult& result)
{
    if (result.status != PDFDocumentToolResultStatus::NothingToDo)
    {
        BaseClass::onRunNotAccepted(result);
        return;
    }

    // No frame: the card Result says it, as the panel of Compress (295:2182), with the sizes
    QString sizes;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        sizes = fact.id == QLatin1String("size") ? fact.value : sizes;
    }
    m_notReducedText = sizes.isEmpty() ? result.message : tr("%1: %2").arg(result.message, sizes);
    updateParts();
}

void PDFAppToolGUIStructure::onDocumentChanged()
{
    // An assistant may change the document below the open tool
    updateCounts();
    m_notReducedText.clear();
    updateParts();
}

void PDFAppToolGUIStructure::fillResultInput(PDFAppToolResultInput* input) const
{
    // 303:4897: the panel beside the dialog shows its result; the report also in the dialog,
    // whose modality keeps the panel out of reach
    m_dialogResult = input->result;
    input->reportText = PDFDocumentToolStructure::createReport(input->result);
}

void PDFAppToolGUIStructure::onCopyReportClicked()
{
    if (!m_result)
    {
        return;
    }

    // The clipboard is the last thing: its signals reach the application
    const QString report = PDFDocumentToolStructure::createReport(*m_result);
    if (PDFAppToolHost* host = getHost())
    {
        host->showTransientMessage(tr("The report is copied."));
    }
    if (QClipboard* clipboard = QGuiApplication::clipboard())
    {
        clipboard->setText(report);
    }
}

void PDFAppToolGUIStructure::onShellStateChanged(PDFDocumentToolShell::State state)
{
    switch (state)
    {
        case PDFDocumentToolShell::State::Deciding:
            if (m_dialogResult)
            {
                m_result = std::move(m_dialogResult);
                m_dialogResult.reset();
            }
            break;

        case PDFDocumentToolShell::State::Editing:
        case PDFDocumentToolShell::State::Running:
            m_result.reset();
            break;

        default:
            break;
    }

    // 303:4897: the actions below the result dialog are disabled; a run disables them itself
    if (m_toolActions && (state == PDFDocumentToolShell::State::Deciding || state == PDFDocumentToolShell::State::Editing))
    {
        m_toolActions->setEnabled(state == PDFDocumentToolShell::State::Editing);
    }
    updateParts();
}

}   // namespace pdf

#include "moc_pdfapptoolgui_structure.cpp"
