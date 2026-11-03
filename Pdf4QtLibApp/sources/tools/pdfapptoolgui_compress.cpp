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

#include "pdfapptoolgui_compress.h"

#include "pdfappdocument.h"
#include "pdfappdocumentview.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolbeforeafteroverlay.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfdocumentsizebreakdown.h"
#include "pdfdocumenttool_compress.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttoolrunner.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the tool Compress
class PDFAppToolGUICompressHelper
{
public:
    PDFAppToolGUICompressHelper() = delete;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// quality 294:1799: the slider 180, the value at the trailing edge. The width of the
    /// value is fixed: the slider stays at its place for 75 as for 100 (the 15 px of the
    /// frame do not hold three digits, the gap 8 of the frame shrinks for them).
    static constexpr int QUALITY_WIDTH = 203;
    static constexpr int QUALITY_VALUE_WIDTH = 23;

    static constexpr int ROW_LABEL_GAP = 12;
    static constexpr int COMBO_WIDTH = 150;
    static constexpr int COMBO_HEIGHT = 34;
};

// --------------------------------------------------- PDFAppToolCompressCustomPanel

PDFAppToolCompressCustomPanel::PDFAppToolCompressCustomPanel(PDFDocumentToolGUI* gui, QWidget* parent) :
    BaseClass(gui && gui->getHost() ? gui->getHost()->getView() : nullptr, PANEL_WIDTH, parent),
    m_gui(gui)
{
    using Helper = PDFAppToolGUICompressHelper;

    setObjectName(QStringLiteral("toolPanel-custom"));
    setAccessibleName(tr("Custom compression settings"));

    PDFAppToolParameterModel* model = gui ? gui->getModel() : nullptr;
    if (!model)
    {
        return;
    }

    PDFAppContext* context = gui->getContext();
    QVBoxLayout* layout = getContentLayout();

    addCaption(tr("Images"));

    // Downsample to: the offered resolutions in a combo box of 150 x 34
    const std::optional<PDFSettingDefinition> dpiDefinition = model->toChoiceDefinition(Helper::name(PDFDocumentToolCompress::PARAMETER_TARGET_DPI),
                                                                                        PDFDocumentToolCompress::getOfferedResolutions(), tr("%1 dpi"));
    if (dpiDefinition)
    {
        PDFAppSettingControl* control = addControl(new PDFAppSettingControl(context, *dpiDefinition, this, model->createChoiceValueStore()));
        control->getComboBox()->setFixedSize(Helper::COMBO_WIDTH, Helper::COMBO_HEIGHT);
        control->getComboBox()->setAccessibleName(getParameterTitle(PDFDocumentToolCompress::PARAMETER_TARGET_DPI));
        addRow(getParameterTitle(PDFDocumentToolCompress::PARAMETER_TARGET_DPI), control->getWidget(), Helper::COMBO_HEIGHT);
    }

    // JPEG quality: the slider with its value
    std::optional<PDFSettingDefinition> qualityDefinition = model->toSettingDefinition(Helper::name(PDFDocumentToolCompress::PARAMETER_JPEG_QUALITY));
    if (qualityDefinition)
    {
        qualityDefinition->setControl(PDFSettingDefinition::Control::Slider);
        PDFAppSettingControl* control = addControl(new PDFAppSettingControl(context, *qualityDefinition, this, model->createValueStore()));
        QWidget* widget = control->getWidget();
        widget->setFixedWidth(Helper::QUALITY_WIDTH);
        if (QBoxLayout* boxLayout = qobject_cast<QBoxLayout*>(widget->layout()))
        {
            boxLayout->setSpacing(0);
        }
        control->getSlider()->setAccessibleName(getParameterTitle(PDFDocumentToolCompress::PARAMETER_JPEG_QUALITY));
        control->getValueLabel()->setFixedWidth(Helper::QUALITY_VALUE_WIDTH);
        control->getValueLabel()->setAlignment(Qt::AlignTrailing | Qt::AlignVCenter);
        addRow(getParameterTitle(PDFDocumentToolCompress::PARAMETER_JPEG_QUALITY), widget, 20);
    }

    // Prefer quality / Minimum size
    std::optional<PDFSettingDefinition> goalDefinition = model->toSettingDefinition(Helper::name(PDFDocumentToolCompress::PARAMETER_GOAL));
    if (goalDefinition)
    {
        goalDefinition->setControl(PDFSettingDefinition::Control::Radio);
        PDFAppSettingControl* control = addControl(new PDFAppSettingControl(context, *goalDefinition, this, model->createValueStore()));
        layout->addWidget(control->getWidget());
    }

    createCheckBox(Helper::name(PDFDocumentToolCompress::PARAMETER_KEEP_ORIGINAL));
    createCheckBox(Helper::name(PDFDocumentToolCompress::PARAMETER_PRESERVE_TRANSPARENCY));
    layout->addWidget(createDivider());
    addCaption(tr("Structure"));
    createCheckBox(Helper::name(PDFDocumentToolCompress::PARAMETER_CLEAN_STRUCTURE));

    // help 294:1828: Regular 11 text/muted
    PDFAppParagraph* help = PDFAppPageWidgets::createText(tr("Lossless – the pages look exactly the same."), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextMuted, this);
    help->setObjectName(QStringLiteral("customHelp"));
    layout->addWidget(help);
}

PDFAppToolCompressCustomPanel::~PDFAppToolCompressCustomPanel() = default;

QString PDFAppToolCompressCustomPanel::getParameterTitle(const char* parameter) const
{
    const PDFAppToolParameterModel* model = m_gui ? m_gui->getModel() : nullptr;
    const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), QString::fromLatin1(parameter)) : nullptr;
    return definition ? definition->getTitle() : QString();
}

void PDFAppToolCompressCustomPanel::addCaption(const QString& text)
{
    // caption 294:1788: Medium 11 text/tertiary in lines of 13
    PDFAppParagraph* caption = PDFAppPageWidgets::createText(text, PDFThemeFonts::getFont(QFont::Medium, 11), 13, PDFThemeColors::TextTertiary, this);
    caption->setObjectName(QStringLiteral("customCaption"));
    getContentLayout()->addWidget(caption);
}

void PDFAppToolCompressCustomPanel::addRow(const QString& labelText, QWidget* control, int height)
{
    // row 294:1793: the label Regular 13 and the control at the trailing edge, gap 12
    QWidget* row = new QWidget(this);
    row->setFixedHeight(height);
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(PDFAppToolGUICompressHelper::ROW_LABEL_GAP);
    PDFLabel* label = PDFAppPageWidgets::createLabel(labelText, QFont::Normal, 13, PDFThemeColors::TextPrimary, row);
    rowLayout->addWidget(label, 1, Qt::AlignVCenter);
    control->setParent(row);
    rowLayout->addWidget(control, 0, Qt::AlignVCenter);
    getContentLayout()->addWidget(row);
}

PDFAppSettingControl* PDFAppToolCompressCustomPanel::addControl(PDFAppSettingControl* control)
{
    m_controls.push_back(control);
    if (m_gui && m_gui->getModel())
    {
        // A change of the user, Undo and Redo
        connect(m_gui->getModel(), &PDFAppToolParameterModel::changed, control, &PDFAppSettingControl::refresh);
    }
    return control;
}

PDFAppSettingControl* PDFAppToolCompressCustomPanel::createCheckBox(const QString& parameter)
{
    PDFAppToolParameterModel* model = m_gui ? m_gui->getModel() : nullptr;
    const std::optional<PDFSettingDefinition> definition = model ? model->toSettingDefinition(parameter) : std::nullopt;
    if (!definition)
    {
        return nullptr;
    }

    PDFAppSettingControl* control = addControl(new PDFAppSettingControl(m_gui->getContext(), *definition, this, model->createValueStore()));
    const PDFDocumentToolParameterDefinition* toolDefinition = PDFDocumentToolParameters::find(model->getDefinitions(), parameter);
    control->getCheckBox()->setText(toolDefinition ? toolDefinition->getTitle() : QString());
    getContentLayout()->addWidget(control->getWidget());
    return control;
}

PDFAppSettingControl* PDFAppToolCompressCustomPanel::getControl(const QString& parameter) const
{
    const QString key = QStringLiteral("tool.") + parameter;
    for (const QPointer<PDFAppSettingControl>& control : m_controls)
    {
        if (control && control->getKey() == key)
        {
            return control.data();
        }
    }
    return nullptr;
}

int PDFAppToolCompressCustomPanel::getPanelLeft(const QRect& anchorRect, int panelWidth) const
{
    // K13: the place of the frame from the segments, in a narrower window inside of the canvas
    int left = layoutDirection() == Qt::RightToLeft ? anchorRect.right() + 1 - SEGMENTS_OFFSET - panelWidth : anchorRect.left() + SEGMENTS_OFFSET;
    const PDFAppToolHost* host = m_gui ? m_gui->getHost() : nullptr;
    if (const QWidget* canvas = host ? host->getCanvasArea() : nullptr)
    {
        const int canvasLeft = canvas->mapToGlobal(QPoint(0, 0)).x();
        left = qBound(canvasLeft, left, qMax(canvasLeft, canvasLeft + canvas->width() - panelWidth));
    }
    return left;
}

// --------------------------------------------------------- PDFAppToolGUICompress

PDFAppToolGUICompress::PDFAppToolGUICompress(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolGUICompress::~PDFAppToolGUICompress() = default;

void PDFAppToolGUICompress::registerTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolCompress::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolCompress>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUICompress>>());
    }
}

int PDFAppToolGUICompress::getRightPanelSpacing() const
{
    return 16;
}

QString PDFAppToolGUICompress::getInitialSidebarPanel() const
{
    return QStringLiteral("thumbnails");
}

PDFAppToolProgressPlace PDFAppToolGUICompress::getProgressPlace() const
{
    // 295:1711: the card in the section Result below its header, the callout below the section
    PDFAppToolProgressPlace place;
    place.cardLayout = m_resultSectionLayout;
    place.cardIndex = 1;
    place.calloutLayout = m_rightContent ? qobject_cast<QBoxLayout*>(m_rightContent->layout()) : nullptr;
    place.calloutIndex = 1;
    return place;
}

void PDFAppToolGUICompress::setContentEnabled(bool isEnabled)
{
    // 295:1711: the segments disabled (the label "Preset" stays text/tertiary, 295:1732) and
    // What changes text/disabled; the section Result holds the card of the progress and stays
    m_isContentEnabled = isEnabled;
    updateSegmentsEnabled();
    for (QWidget* widget : std::initializer_list<QWidget*>{ m_changesHeader.data(), m_changesCard.data(), m_presetNote.data() })
    {
        if (widget)
        {
            widget->setEnabled(isEnabled);
        }
    }
}

void PDFAppToolGUICompress::updateSegmentsEnabled()
{
    // 295:1711 and 296:2761: nothing to choose during a run and below the result dialog
    const PDFDocumentToolShell* shell = getShell();
    const bool isDeciding = shell && shell->getState() == PDFDocumentToolShell::State::Deciding;
    if (PDFSegmentedControl* segments = m_presetControl ? m_presetControl->getSegmentedControl() : nullptr)
    {
        segments->setEnabled(m_isContentEnabled && !isDeciding);
    }
}

bool PDFAppToolGUICompress::isConfirmEnabled() const
{
    // 295:2182: nothing to keep; a failed measuring has said why in its card
    return BaseClass::isConfirmEnabled() && m_measureState != MeasureState::NotReduced && m_measureState != MeasureState::Failed;
}

QString PDFAppToolGUICompress::getRunningStatusText(const QString& stepText) const
{
    if (stepText.isEmpty())
    {
        return BaseClass::getRunningStatusText(stepText);
    }

    // 295:1821: "Compress: recompressing images 152 of 412 · press Stop or Esc to stop"
    QString step = stepText;
    step.replace(QStringLiteral(" · "), QStringLiteral(" "));
    if (!step.isEmpty())
    {
        step[0] = step[0].toLower();
    }
    return BaseClass::getRunningStatusText(step);
}

QString PDFAppToolGUICompress::getRunningTitle(int percentage) const
{
    return tr("Compressing… %1 %").arg(percentage);
}

void PDFAppToolGUICompress::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    m_presetControl = builder.addSegments(PDFAppToolGUICompressHelper::name(PDFDocumentToolCompress::PARAMETER_PRESET), tr("Preset"));
    m_customPanel = new PDFAppToolCompressCustomPanel(this, builder.getWidget());
    connect(m_customPanel.data(), &PDFAppToolCompressCustomPanel::closed, this, &PDFAppToolGUICompress::onCustomPanelClosed);
    m_isCustomPanelOpen = false;

    // The last segment opens the panel: a click on it also when it is chosen already; the
    // arrows choose it, without a panel over the canvas (Space or Enter opens it)
    const PDFSegmentedControl* segments = m_presetControl ? m_presetControl->getSegmentedControl() : nullptr;
    const QList<QToolButton*> buttons = segments ? segments->findChildren<QToolButton*>() : QList<QToolButton*>();
    if (!buttons.isEmpty())
    {
        m_customSegment = buttons.back();
        m_customSegment->setObjectName(QStringLiteral("toolSegment-custom"));
        m_customSegment->installEventFilter(this);
    }
    updateSegmentsEnabled();
}

void PDFAppToolGUICompress::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    m_rightContent = builder.getWidget();

    // Nothing is measured for a document, which refuses the tool (339:10328 is shown)
    const PDFDocumentToolShell* shell = getShell();
    if (shell && shell->isPermissionRefused() && m_measureState == MeasureState::Measuring)
    {
        m_measureState = MeasureState::Failed;
        m_errorText = getPermissionTitle();
    }

    // section-Result 293:1354
    m_resultHeader = builder.addSection(tr("Result"), tr("measured"));
    m_resultSectionLayout = builder.getSectionLayout();
    m_measuredCard = builder.addMeasuredCard();
    m_measuringCard = new PDFAppToolProgressCard(builder.getWidget());
    m_measuringCard->setObjectName(QStringLiteral("toolMeasuringCard"));
    builder.addWidget(m_measuringCard);
    builder.endSection();

    // callout-amber 295:2621 with Try Smaller file
    m_notReducedCallout = builder.addCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString());
    m_notReducedCallout->setObjectName(QStringLiteral("toolNotReducedCallout"));
    m_notReducedCallout->getTitleLabel()->setWordWrap(true);
    m_trySmallerButton = PDFAppPageWidgets::createButton(tr("Try Smaller file"), QString(), true, m_notReducedCallout);
    m_trySmallerButton->setObjectName(QStringLiteral("toolTrySmaller"));
    connect(m_trySmallerButton.data(), &QPushButton::clicked, this, &PDFAppToolGUICompress::onTrySmallerClicked);
    m_notReducedCallout->getBodyLayout()->addWidget(m_trySmallerButton, 0, Qt::AlignLeading);

    // section-What changes 293:1366
    m_changesHeader = builder.addSection(tr("What changes"));
    m_changeRows = builder.addKeyValues();
    m_changesCard = m_changeRows->parentWidget();
    m_presetNote = builder.addNote(QString());
    m_presetNote->setObjectName(QStringLiteral("toolPresetNote"));
    builder.endSection();

    updateParts();

    // A part made during a run (a change of the language) follows the state of the run
    if (!m_isContentEnabled)
    {
        setContentEnabled(false);
    }
}

QWidget* PDFAppToolGUICompress::createOverlay(QWidget* canvasArea)
{
    const PDFAppToolHost* host = getHost();
    PDFWidget* pdfWidget = host ? host->getPdfWidget() : nullptr;
    if (!pdfWidget || !canvasArea)
    {
        return nullptr;
    }

    m_overlay = new PDFAppToolBeforeAfterOverlay(pdfWidget, canvasArea);
    if (PDFDocumentToolShell* shell = getShell())
    {
        connect(shell, &PDFDocumentToolShell::stateChanged, this, &PDFAppToolGUICompress::onShellStateChanged);
    }
    updateOverlay();
    return m_overlay.data();
}

qint64 PDFAppToolGUICompress::getCurrentFileSize() const
{
    const PDFAppDocument* document = getDocument();
    return document ? document->getSnapshot().originalData.size() : -1;
}

void PDFAppToolGUICompress::setMeasureState(MeasureState state)
{
    m_measureState = state;
    updateParts();
    updateConfirmState();
}

void PDFAppToolGUICompress::updateChangeRows()
{
    if (!m_changeRows)
    {
        return;
    }

    std::vector<PDFDocumentToolFact> rows;
    if (m_result)
    {
        rows = PDFDocumentToolCompress::getChangeRows(*m_result);
    }
    else
    {
        // Before the first measuring the rows have no numbers (a number of another preset
        // would be wrong); the shell shows no frame for it (Q1)
        rows = PDFDocumentSizeBreakdown::createChangeRows(PDFDocumentSizeBreakdown(), PDFDocumentSizeBreakdown());
        for (PDFDocumentToolFact& row : rows)
        {
            row.value = QStringLiteral("–");
            if (row.id == QLatin1String("images"))
            {
                row.title = tr("Images");
            }
        }
    }

    if (m_changeRows->getRowCount() != int(rows.size()))
    {
        m_changeRows->clear();
        for (const PDFDocumentToolFact& row : rows)
        {
            m_changeRows->addRow(row.title, row.value);
        }
        return;
    }

    // The same rows are not laid out again; a key changes with its count
    bool isSameKeys = true;
    for (int i = 0; i < int(rows.size()); ++i)
    {
        isSameKeys = isSameKeys && m_changeRows->getKey(i) == rows[size_t(i)].title;
    }
    if (!isSameKeys)
    {
        m_changeRows->clear();
        for (const PDFDocumentToolFact& row : rows)
        {
            m_changeRows->addRow(row.title, row.value);
        }
        return;
    }
    for (int i = 0; i < int(rows.size()); ++i)
    {
        m_changeRows->setValue(i, rows[size_t(i)].value);
    }
}

void PDFAppToolGUICompress::updateParts()
{
    const bool isMeasured = m_measureState == MeasureState::Measured || m_measureState == MeasureState::NotReduced;
    if (m_measuredCard)
    {
        m_measuredCard->setVisible(!m_isRunning && isMeasured && m_result.has_value());
        if (m_result)
        {
            const qint64 sizeBefore = m_result->data.value(QStringLiteral("sizeBefore")).toInteger();
            const qint64 sizeAfter = m_result->data.value(QStringLiteral("sizeAfter")).toInteger();
            const int ratio = sizeBefore > 0 && sizeAfter < sizeBefore ? int(double(sizeAfter) * 100.0 / double(sizeBefore) + 0.5) : -1;
            m_measuredCard->setSizes(PDFDocumentToolFormat::formatSize(sizeBefore), PDFDocumentToolFormat::formatSize(sizeAfter), ratio, PDFDocumentToolCompress::getSavingText(*m_result));
        }
    }

    if (m_resultHeader)
    {
        m_resultHeader->setHint(m_measureState == MeasureState::Measuring && !m_isRunning ? tr("measuring") : tr("measured"));
    }

    if (m_measuringCard)
    {
        const bool isCardShown = !m_isRunning && !isMeasured;
        m_measuringCard->setVisible(isCardShown);
        switch (m_measureState)
        {
            case MeasureState::Measuring:
                m_measuringCard->setProgress(m_percentage, tr("Measuring… %1 %").arg(m_percentage), m_stepText, QString());
                m_measuringCard->getBar()->setVisible(true);
                break;
            case MeasureState::Stopped:
                m_measuringCard->setProgress(0, tr("Not measured"), tr("Press Compress or choose a preset to measure again."), QString());
                m_measuringCard->getBar()->setVisible(false);
                break;
            case MeasureState::Failed:
                m_measuringCard->setProgress(0, tr("Cannot be measured"), m_errorText, QString());
                m_measuringCard->getBar()->setVisible(false);
                break;
            default:
                break;
        }
    }

    const QString preset = getModel() ? getModel()->getValue(PDFAppToolGUICompressHelper::name(PDFDocumentToolCompress::PARAMETER_PRESET)).toString() : QString();
    const bool isSmallestPreset = preset == QLatin1String(PDFDocumentToolCompress::PRESET_SMALLER_FILE);
    if (m_notReducedCallout)
    {
        m_notReducedCallout->setVisible(!m_isRunning && m_measureState == MeasureState::NotReduced);
        m_notReducedCallout->setTexts(m_result ? m_result->message : QString(),
                                      isSmallestPreset ? tr("Rewriting the whole file can add as much as the images save. See the breakdown below.")
                                                       : tr("Rewriting the whole file can add as much as the images save. A stronger preset may help – see the breakdown below."));
    }
    if (m_trySmallerButton)
    {
        // There is no stronger preset than Smaller file
        m_trySmallerButton->setVisible(!isSmallestPreset);
    }

    updateChangeRows();
    if (m_presetNote && getModel())
    {
        m_presetNote->setText(PDFDocumentToolCompress::getPresetDescription(getModel()->getValues()));
    }
    updateOverlay();
}

void PDFAppToolGUICompress::updateOverlay()
{
    if (!m_overlay)
    {
        return;
    }

    qint64 sizeBefore = getCurrentFileSize();
    if (m_result)
    {
        sizeBefore = m_result->data.value(QStringLiteral("sizeBefore")).toInteger();
    }
    m_overlay->setBeforeText(sizeBefore > 0 ? tr("BEFORE · %1").arg(PDFDocumentToolFormat::formatSize(sizeBefore)) : QString());

    // 295:2182 has the line also without a smaller file, at both of its sides the original
    if (m_result && !m_isRunning && (m_measureState == MeasureState::Measured || m_measureState == MeasureState::NotReduced))
    {
        const PDFDocumentToolOutputDocument* output = PDFDocumentToolRunner::findChangedDocument(*m_result);
        m_overlay->setResult(output ? output->document : PDFDocumentPointer(), tr("AFTER · %1").arg(PDFDocumentToolFormat::formatSize(m_result->data.value(QStringLiteral("sizeAfter")).toInteger())));
    }
    else
    {
        m_overlay->setResult(PDFDocumentPointer(), QString());
    }
}

void PDFAppToolGUICompress::onParametersChanged(const QString& name)
{
    Q_UNUSED(name);

    // The shell asks for a new preview after this hook: a cached one comes at once
    m_result.reset();
    m_percentage = 0;
    m_stepText.clear();
    setMeasureState(MeasureState::Measuring);
}

void PDFAppToolGUICompress::onPreviewChanged(const PDFDocumentToolResult* preview)
{
    if (!preview)
    {
        // A new version of the document: the runner measures it again; the slots of the
        // state of the Primary button may delete this GUI
        m_result.reset();
        m_percentage = 0;
        m_stepText.clear();
        QPointer<PDFAppToolGUICompress> guard(this);
        setMeasureState(MeasureState::Measuring);
        if (guard)
        {
            updateStatusText();
        }
        return;
    }

    switch (preview->status)
    {
        case PDFDocumentToolResultStatus::Success:
            m_result = *preview;
            setMeasureState(MeasureState::Measured);
            break;
        case PDFDocumentToolResultStatus::NothingToDo:
            m_result = *preview;
            setMeasureState(MeasureState::NotReduced);
            break;
        case PDFDocumentToolResultStatus::Failed:
            m_result.reset();
            m_errorText = preview->message;
            setMeasureState(MeasureState::Failed);
            break;
        default:
            break;
    }
}

void PDFAppToolGUICompress::onPreviewProgress(int percentage, const QString& stepText)
{
    m_percentage = percentage;
    m_stepText = stepText;
    if (m_measureState == MeasureState::Measuring && m_measuringCard)
    {
        m_measuringCard->setProgress(percentage, tr("Measuring… %1 %").arg(percentage), stepText, QString());
    }
}

void PDFAppToolGUICompress::onRunStateChanged(bool isRunning)
{
    m_isRunning = isRunning;
    if (isRunning && m_customPanel && m_customPanel->isVisible())
    {
        m_customPanel->hide();
    }

    // The run has ended without a result here: Stop (a result of the dialog comes without
    // this state being shown, an error by onRunNotAccepted)
    if (!isRunning && m_measureState == MeasureState::Measuring && !m_result)
    {
        m_measureState = MeasureState::Stopped;
    }
    updateParts();
    updateConfirmState();
}

void PDFAppToolGUICompress::onRunNotAccepted(const PDFDocumentToolResult& result)
{
    // 295:2182 and an error are states of the panel, no info bar over the canvas
    onPreviewChanged(&result);
}

void PDFAppToolGUICompress::onDocumentChanged()
{
    // A new version is measured by the runner also after Stop or a failed measuring: the card
    // says so. The slots of the state of the Primary button may delete this GUI.
    const PDFDocumentToolShell* shell = getShell();
    const bool isRefused = shell && shell->isPermissionRefused();
    if (!m_isRunning && !isRefused && (m_measureState == MeasureState::Stopped || m_measureState == MeasureState::Failed))
    {
        m_percentage = 0;
        m_stepText.clear();
        setMeasureState(MeasureState::Measuring);
        return;
    }
    updateOverlay();
}

void PDFAppToolGUICompress::fillResultInput(PDFAppToolResultInput* input) const
{
    // 296:2761: the panel beside the dialog shows the result of the dialog, also of a run,
    // which has joined the preview (no preview is reported then)
    m_dialogResult = input->result;
}

void PDFAppToolGUICompress::onTrySmallerClicked()
{
    // The button stays (hidden): the model only says, that a value has changed
    if (PDFAppToolParameterModel* model = getModel())
    {
        model->setValue(PDFAppToolGUICompressHelper::name(PDFDocumentToolCompress::PARAMETER_PRESET), QString::fromLatin1(PDFDocumentToolCompress::PRESET_SMALLER_FILE));
    }
}

void PDFAppToolGUICompress::openCustomPanel()
{
    const PDFSegmentedControl* segments = m_presetControl ? m_presetControl->getSegmentedControl() : nullptr;
    const PDFAppToolToolbar* toolbar = getToolbar();
    if (!segments || !toolbar || !m_customPanel || m_isRunning || m_customPanel->isVisible() || !segments->isVisible())
    {
        return;
    }

    m_isCustomPanelOpen = true;
    m_customPanel->popup(const_cast<PDFSegmentedControl*>(segments), toolbar->mapToGlobal(QPoint(0, toolbar->height())).y() + PDFAppToolToolbar::PANEL_GAP);
}

void PDFAppToolGUICompress::openCustomPanelByMouse()
{
    // 294:1340 has no ring: the panel holds the focus itself, Tab goes to its first control
    openCustomPanel();
    if (m_customPanel && m_customPanel->isVisible())
    {
        m_customPanel->setFocus(Qt::MouseFocusReason);
    }
}

void PDFAppToolGUICompress::onCustomPanelClosed()
{
    m_isCustomPanelOpen = m_customPanel && m_customPanel->isVisible();
}

bool PDFAppToolGUICompress::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_customSegment)
    {
        switch (event->type())
        {
            case QEvent::MouseButtonPress:
                // An open panel closes by this press (its signal closed comes from the event
                // loop): the click on the segment only closes it, as a button of a panel
                m_isCustomPressClosing = m_isCustomPanelOpen;
                break;

            case QEvent::MouseButtonRelease:
            {
                const QMouseEvent* mouseEvent = static_cast<const QMouseEvent*>(event);
                const bool isClick = mouseEvent->button() == Qt::LeftButton && m_customSegment->rect().contains(mouseEvent->position().toPoint());
                if (isClick && !m_isCustomPressClosing)
                {
                    QMetaObject::invokeMethod(this, &PDFAppToolGUICompress::openCustomPanelByMouse, Qt::QueuedConnection);
                }
                m_isCustomPressClosing = false;
                break;
            }

            case QEvent::KeyRelease:
            {
                const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
                if (keyEvent->key() == Qt::Key_Space && !keyEvent->isAutoRepeat())
                {
                    QMetaObject::invokeMethod(this, &PDFAppToolGUICompress::openCustomPanel, Qt::QueuedConnection);
                }
                break;
            }

            case QEvent::KeyPress:
            {
                const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
                if (keyEvent->modifiers() == Qt::NoModifier && (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter))
                {
                    QMetaObject::invokeMethod(this, &PDFAppToolGUICompress::openCustomPanel, Qt::QueuedConnection);
                    return true;
                }
                break;
            }

            default:
                break;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppToolGUICompress::onShellStateChanged(PDFDocumentToolShell::State state)
{
    // 296:2761: the page below the dialog has no overlay
    if (m_overlay)
    {
        m_overlay->setOverlayShown(state != PDFDocumentToolShell::State::Deciding);
    }
    updateSegmentsEnabled();
    if (state == PDFDocumentToolShell::State::Deciding && m_dialogResult)
    {
        m_result = std::move(m_dialogResult);
        m_dialogResult.reset();
        m_measureState = MeasureState::Measured;
        updateParts();
    }
}

}   // namespace pdf

#include "moc_pdfapptoolgui_compress.cpp"
