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

#include "apptoolshellfixture.h"

#include "pdfappdocumenttab.h"
#include "pdfappnavigator.h"
#include "pdfappdocumentview.h"
#include "pdfapptoolparametermodel.h"
#include "pdfcomponentdelegates.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"

#include <QDir>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>
#include <QtMath>

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pdftest
{

pdf::PDFDocumentToolParameterDefinitions ShellTestTool::getParameters() const
{
    using Definition = pdf::PDFDocumentToolParameterDefinition;

    pdf::PDFDocumentToolParameterDefinitions definitions = TestDocumentTool::getParameters();
    std::vector<pdf::PDFDocumentToolParameterOption> presets = {
        { QStringLiteral("smaller"), QT_TR_NOOP("Smaller file") },
        { QStringLiteral("balanced"), QT_TR_NOOP("Balanced") },
        { QStringLiteral("best"), QT_TR_NOOP("Best quality") },
        { QStringLiteral("custom"), QT_TR_NOOP("Custom…") },
    };
    definitions.push_back(Definition::createEnum(tr, QStringLiteral("preset"), QT_TR_NOOP("Preset"), nullptr, std::move(presets), QStringLiteral("balanced")));
    for (int i = 0; i < STEP_COUNT; ++i)
    {
        definitions.push_back(Definition::createBool(tr, getStepName(i), QT_TR_NOOP("Step"), nullptr, i != 4));
    }
    return definitions;
}

ShellTestGUI::ShellTestGUI(bool hasLeftPanel, const QString& initialPanel, bool onlySegments) :
    m_hasLeftPanel(hasLeftPanel),
    m_onlySegments(onlySegments),
    m_initialPanel(initialPanel)
{

}

std::vector<pdf::PDFAppToolFlagItem> ShellTestGUI::getStepItems()
{
    const QStringList icons = { QStringLiteral("embed"), QStringLiteral("eraser"), QStringLiteral("trash"), QStringLiteral("copy"), QStringLiteral("hash"), QStringLiteral("minimize-2") };
    std::vector<pdf::PDFAppToolFlagItem> items;
    for (int i = 0; i < ShellTestTool::STEP_COUNT; ++i)
    {
        pdf::PDFAppToolFlagItem item;
        item.parameter = ShellTestTool::getStepName(i);
        item.title = QStringLiteral("Step number %1").arg(i + 1);
        item.description = QStringLiteral("What the step does");
        item.iconName = icons[i];
        item.offDescription = QStringLiteral("Will be skipped");
        items.push_back(item);
    }
    return items;
}

void ShellTestGUI::describeToolbar(pdf::PDFAppToolPanelBuilder& builder)
{
    segments = builder.addSegments(QStringLiteral("preset"), QStringLiteral("Preset"));
    if (m_onlySegments)
    {
        return;
    }

    flagsMenu = builder.addFlagsMenu(getStepItems(), QStringLiteral("list"), QStringLiteral("Steps: %1 of %2"), 520, QStringLiteral("Optimization steps"));
    flagsMenu->getPanel()->addDividerRow();
    pdf::PDFTextLink* restoreLink = flagsMenu->getPanel()->addLink(QStringLiteral("Restore defaults"));
    connect(restoreLink, &pdf::PDFTextLink::clicked, flagsMenu, &pdf::PDFAppToolFlagsMenu::restoreDefaults);
    builder.addActionDivider();
    selectAllAction = builder.addAction(QStringLiteral("Select all"), QStringLiteral("check-circle"), this, &ShellTestGUI::selectAllSteps);
    enumMenu = builder.addEnumMenu(QStringLiteral("mode"), QStringLiteral("align-left"), QStringLiteral("Order: %1"), 270, QStringLiteral("ORDER"));
}

void ShellTestGUI::describeRightPanel(pdf::PDFAppToolPanelBuilder& builder)
{
    resultHeader = builder.addSection(QStringLiteral("Result"), QStringLiteral("measured"));
    keyValues = builder.addKeyValues();
    keyValues->addRow(QStringLiteral("Images (412)"), QStringLiteral("21.3 MB → 3.4 MB"));
    keyValues->addRow(QStringLiteral("Fonts"), QStringLiteral("1.9 MB → 1.9 MB"));
    keyValues->addRow(QStringLiteral("Page content"), QStringLiteral("1.2 MB → 0.6 MB"));
    keyValues->addRow(QStringLiteral("Structure"), QStringLiteral("0.4 MB → 0.2 MB"));
    note = builder.addNote(QStringLiteral("Balanced: images are reduced to 200 dpi."));
    builder.addSection(QStringLiteral("Steps to run"), QStringLiteral("5 of 6"));
    steps = builder.addSteps(getStepItems());
    builder.endSection();
    callout = builder.addCallout(pdf::PDFAccentHue::Blue, QStringLiteral("info"), QStringLiteral("The pages are turned in the file"), QStringLiteral("Rotate Pages changes the document for everyone who opens it."));
}

void ShellTestGUI::describeLeftPanel(pdf::PDFAppToolPanelBuilder& builder)
{
    if (!m_hasLeftPanel)
    {
        return;
    }
    pageSelection = builder.addPageSelection(QStringLiteral("pages"), QStringLiteral("Pages to rotate"));
    pendingList = builder.addPendingList(QStringLiteral("Pending rotation"), &pendingModel);
}

void ShellTestGUI::onParametersChanged(const QString& name)
{
    Q_UNUSED(name);
    ++parameterChanges;
}

void ShellTestGUI::onPreviewChanged(const pdf::PDFDocumentToolResult* preview)
{
    previews.push_back(preview);
    previewFacts.clear();
    if (preview)
    {
        for (const pdf::PDFDocumentToolFact& fact : preview->facts)
        {
            previewFacts << fact.value;
        }
    }
}

void ShellTestGUI::onRunStateChanged(bool isRunning)
{
    Q_UNUSED(isRunning);
    ++runStates;
}

void ShellTestGUI::onDocumentChanged()
{
    ++documentChanges;
}

void ShellTestGUI::selectAllSteps()
{
    flagsMenu->setAll(true);
}

AppToolShellFixture::AppToolShellFixture(QSize size) :
    AppToolsFixture(size)
{

}

AppToolShellFixture::~AppToolShellFixture()
{
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

std::shared_ptr<ShellTestTool> AppToolShellFixture::registerShellTool(const QString& toolId, bool hasLeftPanel, const QString& initialPanel, bool onlySegments)
{
    RegisteredTool item;
    item.id = toolId;
    item.previousTool = pdf::PDFDocumentToolRegistry::getTool(toolId);
    item.previousFactory = pdf::PDFDocumentToolGUIRegistry::getFactory(toolId);
    pdf::PDFDocumentToolRegistry::unregisterTool(toolId);
    m_registeredTools.push_back(item);

    std::shared_ptr<ShellTestTool> tool = std::make_shared<ShellTestTool>(toolId);
    tool->setResultKindModes(true);
    tool->setPreview(false, false);
    if (pdf::PDFDocumentToolRegistry::registerTool(tool) != pdf::PDFDocumentToolRegistration::Registered ||
        !pdf::PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<ShellTestGUIFactory>(hasLeftPanel, initialPanel, onlySegments)))
    {
        return nullptr;
    }
    return tool;
}

pdf::PDFDocumentToolShell* AppToolShellFixture::openTool(pdf::PDFAppDocumentTab* tab, const QString& toolId)
{
    if (!tab || !window || !window->getNavigator())
    {
        return nullptr;
    }
    window->getNavigator()->navigate(pdf::PDFAppRoutes::documentTool(tab->getDocumentId(), toolId));
    settle();
    return getShell(tab);
}

pdf::PDFDocumentToolShell* AppToolShellFixture::getShell(pdf::PDFAppDocumentTab* tab)
{
    pdf::PDFAppToolDocumentPage* page = tab ? qobject_cast<pdf::PDFAppToolDocumentPage*>(tab->getPage()) : nullptr;
    return page ? page->getShell() : nullptr;
}

ShellTestGUI* AppToolShellFixture::getGUI(pdf::PDFDocumentToolShell* shell)
{
    return shell ? qobject_cast<ShellTestGUI*>(shell->getGUI()) : nullptr;
}

bool AppToolShellFixture::waitForState(pdf::PDFDocumentToolShell* shell, pdf::PDFDocumentToolShell::State state)
{
    QPointer<pdf::PDFDocumentToolShell> guard(shell);
    return waitFor([&guard, state]() { return !guard || guard->getState() == state; }) && guard;
}

QVariantMap AppToolShellFixture::parameters(const QString& mode, const QString& title)
{
    QVariantMap values;
    values.insert(QStringLiteral("mode"), mode);
    values.insert(QStringLiteral("title"), title);
    return values;
}

bool AppToolShellFixture::waitFor(const std::function<bool()>& condition, int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    const qint64 limit = qint64(milliseconds) * getTimeoutFactor();
    while (!condition())
    {
        if (timer.elapsed() > limit)
        {
            return false;
        }
        QTest::qWait(1);
    }
    return true;
}

bool AppToolShellFixture::waitForRun(const pdf::PDFDocumentToolRunner* runner)
{
    return waitFor([runner]() { return runner->getState() != pdf::PDFDocumentToolRunner::State::Running; });
}

bool AppToolShellFixture::runTool(pdf::PDFDocumentToolRunner* runner, const QVariantMap& parameters, pdf::PDFDocumentToolResult* result, pdf::PDFDocumentToolRunner::State state)
{
    QSignalSpy spy(runner, &pdf::PDFDocumentToolRunner::runFinished);
    if (!runner->run(parameters) || !waitFor([&spy]() { return spy.size() >= 1; }))
    {
        return false;
    }

    if (result)
    {
        *result = spy.first().first().value<pdf::PDFDocumentToolResult>();
    }
    return runner->getState() == state;
}

pdf::PDFDocumentToolRunner* AppToolShellFixture::createRunner(pdf::PDFAppDocumentTab* tab, const QString& toolId, std::shared_ptr<TestDocumentTool>* tool)
{
    std::shared_ptr<TestDocumentTool> created = std::make_shared<TestDocumentTool>(toolId);
    created->setResultKindModes(true);
    if (tool)
    {
        *tool = created;
    }
    return new pdf::PDFDocumentToolRunner(tab->getDocument(), created, tab);
}

pdf::PDFAppToolResultDialog* AppToolShellFixture::openDialog(pdf::PDFAppDocumentTab* tab, const QString& toolId, const QString& mode, const QStringList& facts, pdf::PDFDocumentToolRunner** runner, const QString& title)
{
    pdf::PDFDocumentToolRunner* createdRunner = createRunner(tab, toolId);
    if (runner)
    {
        *runner = createdRunner;
    }

    pdf::PDFAppToolResultInput input;
    input.tool = pdf::PDFDocumentToolCatalog::findTool(toolId);
    input.title = title;
    if (!runTool(createdRunner, parameters(mode), &input.result))
    {
        return nullptr;
    }

    if (!facts.isEmpty())
    {
        input.result.facts.clear();
        for (qsizetype i = 0; i + 1 < facts.size(); i += 2)
        {
            input.result.facts.push_back({ QString::number(i), facts[i], facts[i + 1] });
        }
    }

    input.scrimTarget = tab->getView() ? tab->getView()->getCanvasArea() : nullptr;
    pdf::PDFAppToolResultDialog* dialog = pdf::PDFAppToolResultDialog::open(tab, createdRunner, std::move(input));
    settle();
    return dialog;
}

QRect AppToolShellFixture::getCardRect(const pdf::PDFAppToolResultDialog* dialog)
{
    return dialog->rect().marginsRemoved(dialog->getShadowMargins());
}

QImage AppToolShellFixture::render(QWidget* widget, qreal devicePixelRatio)
{
    QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
    widget->render(&image, QPoint(), QRegion(), QWidget::RenderFlags(QWidget::DrawChildren));
    return image;
}

int AppToolShellFixture::countLeakPixels(const QImage& image)
{
    // A filled area has pixels of exactly the leak color, a text does not: a glyph of 12 px has almost no
    // pixel of full coverage, so a text in the leak color has to be found as its blend with what it is
    // drawn over. The backgrounds are the colors covering the most of the picture; a pixel is a leak,
    // when it lies on the way from one of them to the leak color, at least at the coverage of a stem.
    constexpr int MAXIMUM_BACKGROUNDS = 6;
    constexpr double MINIMUM_COVERAGE = 0.4;
    constexpr double MAXIMUM_DISTANCE = 10.0;

    const QImage converted = image.convertToFormat(QImage::Format_RGB32);
    const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;

    std::unordered_map<QRgb, int> histogram;
    for (int y = 0; y < converted.height(); ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
        for (int x = 0; x < converted.width(); ++x)
        {
            ++histogram[line[x] & 0xFFFFFF];
        }
    }

    std::vector<std::pair<int, QRgb>> frequent;
    const int minimumArea = qMax(1, converted.width() * converted.height() / 100);
    for (const auto& [color, area] : histogram)
    {
        if (area >= minimumArea && color != leak)
        {
            frequent.emplace_back(area, color);
        }
    }
    std::sort(frequent.begin(), frequent.end(), std::greater<>());
    if (frequent.size() > size_t(MAXIMUM_BACKGROUNDS))
    {
        frequent.resize(MAXIMUM_BACKGROUNDS);
    }

    // The colors are few: the answer of each color is computed once
    int count = 0;
    for (const auto& [color, area] : histogram)
    {
        bool isLeak = color == leak;
        for (size_t i = 0; !isLeak && i < frequent.size(); ++i)
        {
            const QRgb background = frequent[i].second;
            if (color == background)
            {
                continue;
            }

            const double wayRed = qRed(leak) - qRed(background);
            const double wayGreen = qGreen(leak) - qGreen(background);
            const double wayBlue = qBlue(leak) - qBlue(background);
            const double wayLength = wayRed * wayRed + wayGreen * wayGreen + wayBlue * wayBlue;
            const double red = qRed(color) - qRed(background);
            const double green = qGreen(color) - qGreen(background);
            const double blue = qBlue(color) - qBlue(background);
            const double coverage = wayLength > 0.0 ? (red * wayRed + green * wayGreen + blue * wayBlue) / wayLength : 0.0;
            if (coverage >= MINIMUM_COVERAGE && coverage <= 1.0)
            {
                const double offRed = red - coverage * wayRed;
                const double offGreen = green - coverage * wayGreen;
                const double offBlue = blue - coverage * wayBlue;
                isLeak = offRed * offRed + offGreen * offGreen + offBlue * offBlue <= MAXIMUM_DISTANCE * MAXIMUM_DISTANCE;
            }
        }
        count += isLeak ? area : 0;
    }
    return count;
}

void AppToolShellFixture::screenshotDialog(QWidget* dialog, const QString& name)
{
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    if (folder.isEmpty() || !dialog)
    {
        return;
    }

    QDir().mkpath(folder);
    render(dialog, 1.0).save(QDir(folder).absoluteFilePath(name + QStringLiteral(".png")));
    render(dialog, 2.0).save(QDir(folder).absoluteFilePath(name + QStringLiteral("-2x.png")));
}

}   // namespace pdftest
