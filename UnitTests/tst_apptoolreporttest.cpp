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

// The GUI of Document Report (step 7 of phase 2, plan 2.8, 4.4, 8; package P2): the frames 484:15472 … 484:20937
// - the sections and the output (484:15472), nothing checked (484:20937), Structure as XML with its options
// (484:18196), the permission of the extraction (Q7.2), the run, whose file opens the dialog of the export, and the
// kept result (484:18811, R7-2), a section the document has nothing of (484:20505) and the preview. The run is
// given by a test tool of the parameters of review.report (its file is known); the preview and the empty sections
// are of the core (PDFDocumentReport, package P1b).

#include "apptoolshellfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumentsearchindex.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfappreportpreview.h"
#include "pdfappstatisticstable.h"
#include "pdfapptoolgui_report.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_report.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QCheckBox>
#include <QFile>
#include <QPushButton>
#include <QScopeGuard>
#include <QToolButton>
#include <QtTest>

using pdftest::AppToolShellFixture;
using Section = pdf::PDFDocumentReportSection;
using ResultState = pdf::PDFAppToolResultState;

/// The core tool of the test: the parameters and the permissions of review.report, a known file
class ReportTestTool : public pdf::PDFDocumentToolReport
{
public:
    static QByteArray content() { return QByteArrayLiteral("<html><body>report</body></html>"); }

    virtual pdf::PDFDocumentToolResult execute(const pdf::PDFDocumentToolContext& context, const QVariantMap& parameters, const pdf::PDFOperationControl* control, pdf::PDFProgressSink* progress) const override
    {
        Q_UNUSED(context);
        Q_UNUSED(parameters);
        Q_UNUSED(control);
        Q_UNUSED(progress);
        pdf::PDFDocumentToolOutputFile output;
        output.suggestedFileName = QStringLiteral("Project_Report - report.html");
        output.mimeType = QStringLiteral("text/html");
        output.data = content();
        pdf::PDFDocumentToolResult result;
        result.outputs.push_back(std::move(output));
        return result;
    }
};

/// The test tool instead of the built-in one and the GUI of the package, both given back at the end
class ReportTestRegistration
{
public:
    explicit ReportTestRegistration(bool isToolReplaced)
    {
        const QString id = QString::fromLatin1(pdf::PDFDocumentToolReport::TOOL_ID);
        m_builtin = pdf::PDFDocumentToolRegistry::getTool(id);
        if (isToolReplaced)
        {
            pdf::PDFDocumentToolRegistry::unregisterTool(id);
            pdf::PDFDocumentToolRegistry::registerTool(std::make_shared<ReportTestTool>());
        }
        pdf::PDFDocumentToolGUIRegistry::unregisterFactory(id);
        pdf::PDFDocumentToolGUIRegistry::registerFactory(id, std::make_shared<pdf::PDFDocumentToolGUIFactoryOf<pdf::PDFAppToolGUIReport>>());
        pdf::PDFAppToolGUIReport::setFileOpener(&ReportTestRegistration::open);
        s_openedFiles.clear();
    }

    ~ReportTestRegistration()
    {
        pdf::PDFAppToolGUIReport::setFileOpener(nullptr);
        const QString id = QString::fromLatin1(pdf::PDFDocumentToolReport::TOOL_ID);
        pdf::PDFDocumentToolRegistry::unregisterTool(id);
        if (m_builtin)
        {
            pdf::PDFDocumentToolRegistry::registerTool(m_builtin);
        }
    }

    static bool open(const QString& filePath)
    {
        s_openedFiles << filePath;
        return true;
    }

    static QStringList s_openedFiles;

private:
    std::shared_ptr<const pdf::PDFDocumentTool> m_builtin;
};

QStringList ReportTestRegistration::s_openedFiles;

/// Helpers of the test
class AppToolReportTestHelper
{
public:
    AppToolReportTestHelper() = delete;

    using GUI = pdf::PDFAppToolGUIReport;

    static pdf::PDFDocumentToolShell* openTool(AppToolShellFixture& fixture)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 3));
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        return fixture.openTool(tab, QString::fromLatin1(pdf::PDFDocumentToolReport::TOOL_ID));
    }

    static GUI* gui(pdf::PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<GUI*>(shell->getGUI()) : nullptr;
    }

    static bool waitPreview(GUI* gui)
    {
        return AppToolShellFixture::waitFor([gui]() { return !gui->isPreviewPending(); });
    }

    static QStringList values(const pdf::PDFKeyValueList* list)
    {
        QStringList result;
        for (int i = 0; list && i < list->getRowCount(); ++i)
        {
            result << list->getKey(i) + QStringLiteral("=") + list->getValue(i);
        }
        return result;
    }

    static bool isConfirmEnabled(pdf::PDFDocumentToolShell* shell)
    {
        return shell->getToolbar()->getConfirmButton()->isEnabled();
    }

    static void setSection(pdf::PDFDocumentToolShell* shell, Section section, bool value)
    {
        shell->getModel()->setValue(pdf::PDFDocumentReport::getSectionId(section), value);
        AppToolShellFixture::settle();
    }
};

class AppToolReportTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void defaultFrame();
    void nothingChecked();
    void structureAsXml();
    void permissionsOfRun();
    void runExportsAndKeepsResult();
    void discardedReportCreatedAgain();
    void sectionWithoutContent();
    void previewOfSections();
    void previewTableHeads();
    void xmlPreviewStopsWithItsJob();
    void screenshots();

private:
    QLocale m_locale;
};

void AppToolReportTest::initTestCase()
{
    m_locale = pdf::PDFAppLocale::getUiLocale();
    pdf::PDFAppLocale::setUiLocale(QLocale(QLocale::English, QLocale::UnitedStates));

    // The geometry of the frames is the geometry of the Fluent style (review RS7FIGMA)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
}

void AppToolReportTest::cleanupTestCase()
{
    pdf::PDFAppLocale::setUiLocale(m_locale);
}

void AppToolReportTest::defaultFrame()
{
    // 484:15472: 6 of 9 sections (Basic information, Metadata, JavaScript, Named destinations, Page boxes, Structure
    // tree), Output Report / HTML / Short / 6 of 9 / All, Close and Create Report; the format "none" of MCP is not
    // offered
    using H = AppToolReportTestHelper;
    ReportTestRegistration registration(false);
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    H::GUI* gui = H::gui(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    QCOMPARE(shell->getToolbar()->getCancelButton()->text(), QStringLiteral("Close"));
    QCOMPARE(shell->getToolbar()->getConfirmButton()->text(), QStringLiteral("Create Report"));
    QCOMPARE(gui->getKindMenu()->getButton()->text(), QStringLiteral("Report"));
    QCOMPARE(gui->getFormatMenu()->getButton()->text(), QStringLiteral("HTML"));
    QCOMPARE(gui->getDateMenu()->getButton()->text(), QStringLiteral("Short dates"));
    QVERIFY(gui->getFormatMenu()->getRow(QStringLiteral("none"))->isHidden());

    const std::vector<Section> included = { Section::BasicInformation, Section::Metadata, Section::JavaScript, Section::NamedDestinations, Section::PageBoxes, Section::StructureTree };
    for (const Section section : pdf::PDFDocumentReport::getSections())
    {
        const bool isIncluded = std::find(included.cbegin(), included.cend(), section) != included.cend();
        QCOMPARE(shell->getModel()->getValue(pdf::PDFDocumentReport::getSectionId(section)).toBool(), isIncluded);
        QVERIFY(gui->getSectionRow(section));
        QCOMPARE(gui->getSectionRow(section)->getTitle(), H::GUI::getSectionTitle(section));
    }
    QCOMPARE(gui->getSectionRow(Section::BasicInformation)->getTitle(), QStringLiteral("Basic information"));
    QCOMPARE(gui->getSectionRow(Section::Fonts)->getDescription(), QStringLiteral("Used fonts and how they are embedded"));

    // The sections of the document of now (the empty ones are left out, 484:20505)
    QVERIFY(H::waitPreview(gui));
    AppToolShellFixture::settle();
    const QString count = QString::number(gui->getIncludedSections().size());
    QCOMPARE(gui->getSectionsHeader()->getSubtitleLabel()->text(), count + QStringLiteral(" of 9 included"));
    const QStringList output = H::values(gui->getOutputRows());
    QCOMPARE(output, QStringList({ "Kind=Report", "Format=HTML", "Dates=Short", "Sections=" + count + " of 9", "Pages=All 3" }));
    QVERIFY(gui->getPageSelection()->isVisible());
    QVERIFY(!gui->getXmlCallout()->isVisible());
    QVERIFY(H::isConfirmEnabled(shell));
    QCOMPARE(gui->getStatusText(), QStringLiteral("The report will have %1 sections. Create Report puts it together and asks for the file.").arg(count));

    // 484:20021 (RS7FIGMA): a range being written, which is not valid - Create Report Disabled and the status bar
    // of the tab says it (the range never reaches the model, so only the page selection can tell the status)
    pdf::PDFAppDocumentView* view = fixture.window->getDocumentTabs().front()->getView();
    gui->getPageSelection()->setMode(pdf::PDFPageSelection::Mode::Range);
    gui->getPageSelection()->setRange(QStringLiteral("3-1"));
    AppToolShellFixture::settle();
    QVERIFY(!H::isConfirmEnabled(shell));
    QCOMPARE(view->getToolMessage(), QStringLiteral("The page selection is not valid."));
}

void AppToolReportTest::nothingChecked()
{
    // 484:20937: no section - the canvas "Check at least one section", Sections None, Create Report Disabled
    using H = AppToolReportTestHelper;
    ReportTestRegistration registration(false);
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    H::GUI* gui = H::gui(shell);
    for (const Section section : pdf::PDFDocumentReport::getSections())
    {
        H::setSection(shell, section, false);
    }

    QCOMPARE(gui->getSectionsHeader()->getSubtitleLabel()->text(), QStringLiteral("0 of 9 included"));
    QCOMPARE(gui->getCanvasMessage()->getTitle(), QStringLiteral("Check at least one section"));
    QCOMPARE(gui->getCanvasMessage()->getText(), QStringLiteral("The report is put together from the sections checked on the left."));
    QVERIFY(!gui->getPreview()->isVisible());
    QCOMPARE(H::values(gui->getOutputRows())[3], QStringLiteral("Sections=None"));
    QVERIFY(!H::isConfirmEnabled(shell));
    QCOMPARE(gui->getStatusText(), QStringLiteral("No section is checked."));

    // A check box of a row writes its parameter
    gui->getSectionRow(Section::Fonts)->setCheckBoxChecked(true);
    AppToolShellFixture::settle();
    QVERIFY(shell->getModel()->getValue(pdf::PDFDocumentReport::getSectionId(Section::Fonts)).toBool());
    QVERIFY(H::isConfirmEnabled(shell));
    QVERIFY(gui->getCharacterMapsCheckBox()->isVisible());
}

void AppToolReportTest::structureAsXml()
{
    // 484:18196: Structure as XML - Export XML, Format and Dates Disabled, the description and the callout, the XML
    // options; the two check boxes of the streams are one choice (none, hexadecimal, text)
    using H = AppToolReportTestHelper;
    using Tool = pdf::PDFDocumentToolReport;
    ReportTestRegistration registration(false);
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    H::GUI* gui = H::gui(shell);
    shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_KIND), QString::fromLatin1(Tool::KIND_STRUCTURE_XML));
    AppToolShellFixture::settle();

    QVERIFY(gui->isStructureXml());
    QCOMPARE(shell->getToolbar()->getConfirmButton()->text(), QStringLiteral("Export XML"));
    QCOMPARE(gui->getKindMenu()->getButton()->text(), QStringLiteral("Structure as XML"));
    QVERIFY(!gui->getFormatMenu()->getButton()->isEnabled());
    QVERIFY(!gui->getDateMenu()->getButton()->isEnabled());
    QVERIFY(gui->getXmlHeader()->isVisible());
    QCOMPARE(gui->getXmlCallout()->getTitleLabel()->text(), QStringLiteral("The file can be large"));
    QVERIFY(!gui->getPageSelection()->isVisible());
    QVERIFY(!gui->getSectionsHeader()->isVisible());
    QVERIFY(H::isConfirmEnabled(shell));
    QCOMPARE(gui->getStatusText(), QStringLiteral("The whole internal structure will be exported as XML."));
    QCOMPARE(H::values(gui->getOutputRows()).first(), QStringLiteral("Kind=Structure as XML"));

    // The defaults: no streams, indented
    QCheckBox* hex = gui->getXmlOption("streamsHex");
    QCheckBox* text = gui->getXmlOption("streamsText");
    QVERIFY(hex && text && hex->isVisible());
    QVERIFY(!hex->isChecked());
    QVERIFY(!text->isChecked());
    QVERIFY(gui->getXmlOption("indent")->isChecked());

    text->click();
    AppToolShellFixture::settle();
    QVERIFY(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_EXPORT_STREAMS)).toBool());
    QVERIFY(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_STREAMS_AS_TEXT)).toBool());
    QVERIFY(text->isChecked());
    QVERIFY(!hex->isChecked());

    hex->click();
    AppToolShellFixture::settle();
    QVERIFY(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_EXPORT_STREAMS)).toBool());
    QVERIFY(!shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_STREAMS_AS_TEXT)).toBool());
    QVERIFY(hex->isChecked());
    QVERIFY(!text->isChecked());

    hex->click();
    AppToolShellFixture::settle();
    QVERIFY(!shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_EXPORT_STREAMS)).toBool());
    QVERIFY(!hex->isChecked());
    QVERIFY(!text->isChecked());

    gui->getXmlOption("alwaysBinaryStrings")->click();
    AppToolShellFixture::settle();
    QVERIFY(shell->getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_ALWAYS_BINARY_STRINGS)).toBool());
}

void AppToolReportTest::permissionsOfRun()
{
    // Q7.2: the JavaScript and the XMP of a report, the streams of the XML are extraction - bit 5 (CopyContent);
    // the other sections are reading the document
    using H = AppToolReportTestHelper;
    using Tool = pdf::PDFDocumentToolReport;
    using Permission = pdf::PDFSecurityHandler::Permission;
    ReportTestRegistration registration(false);
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    H::GUI* gui = H::gui(shell);

    QCOMPARE(gui->getRunPermissions(), std::vector<Permission>({ Permission::CopyContent }));
    H::setSection(shell, Section::JavaScript, false);
    QCOMPARE(gui->getRunPermissions(), std::vector<Permission>({ Permission::CopyContent }));
    H::setSection(shell, Section::Metadata, false);
    QVERIFY(gui->getRunPermissions().empty());

    shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_KIND), QString::fromLatin1(Tool::KIND_STRUCTURE_XML));
    AppToolShellFixture::settle();
    QVERIFY(gui->getRunPermissions().empty());
    shell->getModel()->setValues(H::GUI::getStreamValues(false, true));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getRunPermissions(), std::vector<Permission>({ Permission::CopyContent }));
}

void AppToolReportTest::runExportsAndKeepsResult()
{
    // 484:18811: Create Report - the dialog of the export "The report is ready" (FileOnly, Save Report, the check box
    // "Open the report when it is saved"); the saved file is opened; the tool keeps its result: Create Report
    // Disabled (R7-2) until a setting changes (stale, the amber callout)
    using H = AppToolReportTestHelper;
    ReportTestRegistration registration(true);
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    H::GUI* gui = H::gui(shell);
    QVERIFY(H::waitPreview(gui));
    AppToolShellFixture::settle();

    shell->getToolbar()->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
    AppToolShellFixture::settle();
    QPointer<pdf::PDFAppToolResultDialog> dialog = shell->getResultDialog();
    QCOMPARE(dialog->getResultKind(), pdf::PDFAppToolResultDialog::ResultKind::FileOnly);
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Save Report"));
    QVERIFY(dialog->getOpenCheckBox()->isVisible());
    QCOMPARE(dialog->getOpenCheckBox()->text(), QStringLiteral("Open the report when it is saved"));
    QCOMPARE(dialog->getFileNameField()->text(), QStringLiteral("Project_Report - report.html"));

    dialog->getOpenCheckBox()->setChecked(true);
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&dialog]() { return dialog.isNull(); }));
    AppToolShellFixture::settle();

    const QString filePath = QDir(fixture.folder()).filePath(QStringLiteral("Project_Report - report.html"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), ReportTestTool::content());
    QCOMPARE(ReportTestRegistration::s_openedFiles.size(), 1);
    QCOMPARE(QFileInfo(ReportTestRegistration::s_openedFiles.front()).fileName(), QStringLiteral("Project_Report - report.html"));

    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QCOMPARE(shell->getResultState(), ResultState::Current);
    QVERIFY(!H::isConfirmEnabled(shell));

    // A setting changed: stale - the callout and Create Report again
    H::setSection(shell, Section::Fonts, true);
    QCOMPARE(shell->getResultState(), ResultState::Stale);
    QVERIFY(shell->getStaleCallout());
    QVERIFY(H::isConfirmEnabled(shell));
}

void AppToolReportTest::discardedReportCreatedAgain()
{
    // 484:18811 Discard: the report is not saved, so there is no result - the tool stays in its ready state, Create
    // Report is enabled without a change of a setting (R7-2 disables it only after a result; RS7APP: it stayed
    // Disabled and the report could not be created again), no stale callout; the file is not written
    using H = AppToolReportTestHelper;
    ReportTestRegistration registration(true);
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    H::GUI* gui = H::gui(shell);
    QVERIFY(H::waitPreview(gui));
    AppToolShellFixture::settle();

    shell->getToolbar()->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
    AppToolShellFixture::settle();
    QPointer<pdf::PDFAppToolResultDialog> dialog = shell->getResultDialog();
    QVERIFY(dialog->isExport());
    dialog->reject();
    QVERIFY(AppToolShellFixture::waitFor([&dialog]() { return dialog.isNull(); }));
    AppToolShellFixture::settle();

    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QCOMPARE(shell->getResultState(), ResultState::None);
    QVERIFY(!shell->getStaleCallout());
    QVERIFY(H::isConfirmEnabled(shell));
    QVERIFY(ReportTestRegistration::s_openedFiles.isEmpty());
    QVERIFY(!QFileInfo::exists(QDir(fixture.folder()).filePath(QStringLiteral("Project_Report - report.html"))));

    // Create Report again: the dialog of the export again
    shell->getToolbar()->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
    AppToolShellFixture::settle();
    dialog = shell->getResultDialog();
    QVERIFY(dialog && dialog->isExport());
    dialog->reject();
    QVERIFY(AppToolShellFixture::waitFor([&dialog]() { return dialog.isNull(); }));
    AppToolShellFixture::settle();
}

void AppToolReportTest::sectionWithoutContent()
{
    // 484:20505: a section the document has nothing of (the sample has no structure tree) - its check box Disabled
    // with the reason, left out of the count and of the run
    using H = AppToolReportTestHelper;
    ReportTestRegistration registration(false);
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    H::GUI* gui = H::gui(shell);
    QVERIFY(H::waitPreview(gui));
    AppToolShellFixture::settle();

    QVERIFY(gui->getFacts().has_value());
    QVERIFY(!gui->isSectionAvailable(Section::StructureTree));
    pdf::PDFTileRow* row = gui->getSectionRow(Section::StructureTree);
    QVERIFY(!row->isCheckBoxEnabled());
    QVERIFY(!row->isCheckBoxChecked());
    QCOMPARE(row->getDescription(), gui->getFacts()->emptyReasons.at(Section::StructureTree));
    QVERIFY(shell->getModel()->getValue(pdf::PDFDocumentReport::getSectionId(Section::StructureTree)).toBool());
    QVERIFY(!gui->getRunParameters().value(pdf::PDFDocumentReport::getSectionId(Section::StructureTree)).toBool());

    gui->setCurrentSection(Section::StructureTree);
    QCOMPARE(gui->getStatusText(), QStringLiteral("This document has no structure tree; the section is left out."));
}

void AppToolReportTest::previewOfSections()
{
    // 484:15472: the preview "Report on Project_Report.pdf" of the data of the sections; a click on a section shows
    // it; the preview is the model of the core (never the HTML)
    using H = AppToolReportTestHelper;
    ReportTestRegistration registration(false);
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    H::GUI* gui = H::gui(shell);
    QVERIFY(H::waitPreview(gui));
    AppToolShellFixture::settle();

    pdf::PDFAppReportPreview* preview = gui->getPreview();
    QVERIFY(preview->isVisible());
    QVERIFY(!gui->getPreviewData().isEmpty());
    const QStringList blocks = preview->getBlockTexts();
    QVERIFY(!blocks.isEmpty());
    QCOMPARE(blocks.front(), QStringLiteral("Report on Project_Report.pdf"));
    QVERIFY(preview->getSectionTop(pdf::PDFDocumentReport::getSectionId(Section::BasicInformation)) >= 0);
    QVERIFY(blocks.join(QLatin1Char('\n')).contains(QStringLiteral("Project_Report.pdf")));

    // 484:20505 (RS7FIGMA): a checked section the document has nothing of (the sample has no XMP and no named
    // destination) is left out of the first preview as well - only the included sections are shown
    QVERIFY(!gui->isSectionAvailable(Section::Metadata));
    QVERIFY(!gui->isSectionAvailable(Section::NamedDestinations));
    for (const Section section : pdf::PDFDocumentReport::getSections())
    {
        const std::vector<Section> included = gui->getIncludedSections();
        const bool isIncluded = std::find(included.cbegin(), included.cend(), section) != included.cend();
        QCOMPARE(preview->getSectionTop(pdf::PDFDocumentReport::getSectionId(section)) >= 0, isIncluded);
    }

    // The dates change only the model: no new job
    shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolReport::PARAMETER_DATE_FORMAT), QStringLiteral("iso"));
    QVERIFY(!gui->isPreviewPending());
    QCOMPARE(gui->getDateMenu()->getButton()->text(), QStringLiteral("ISO dates"));
}

void AppToolReportTest::previewTableHeads()
{
    // 484:17261 (RS7FIGMA): the head of a numeric column is right-aligned above its values, and the rest of the
    // width goes to the last text column ("Name"), so a number never stands far from its head: in every numeric
    // column the head and the value end at the same x (the rightmost dark pixel of each row of the cell)
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    pdf::PDFDocumentToolTable table;
    table.addColumn(QStringLiteral("box"), QStringLiteral("Box"), pdf::PDFDocumentToolTableValueType::Text);
    table.addColumn(QStringLiteral("top"), QStringLiteral("Top"), pdf::PDFDocumentToolTableValueType::Real);
    table.addRow({ QStringLiteral("Media box"), 123456.78 });
    pdf::PDFDocumentToolTableSection report;
    report.title = QStringLiteral("Report");
    report.addTable(std::move(table));

    pdf::PDFAppReportPreview preview;
    preview.setReport(report);
    preview.resize(774, preview.heightForWidth(774));
    const QImage image = preview.grab().toImage();

    // The rows of the table: the head and the first value (the card of the table is below the title)
    const QStringList blocks = preview.getBlockTexts();
    QVERIFY(!blocks.isEmpty());
    int tableTop = -1;
    for (int y = 0; y < image.height() && tableTop < 0; ++y)
    {
        // The border of the card: a full row of non-white pixels across the middle
        if (qGray(image.pixel(image.width() / 2, y)) < 245 && qGray(image.pixel(image.width() / 3, y)) < 245 && qGray(image.pixel(2 * image.width() / 3, y)) < 245)
        {
            tableTop = y;
        }
    }
    QVERIFY(tableTop >= 0);
    const int rowHeight = pdf::PDFAppReportPreview::ROW_HEIGHT;
    const auto rightmostInk = [&image](int top, int bottom) -> int
    {
        int result = -1;
        for (int y = top; y < bottom && y < image.height(); ++y)
        {
            for (int x = image.width() - 30; x >= 0; --x)
            {
                if (qGray(image.pixel(x, y)) < 160)
                {
                    result = qMax(result, x);
                    break;
                }
            }
        }
        return result;
    };
    const int headRight = rightmostInk(tableTop + 4, tableTop + rowHeight - 4);
    const int valueRight = rightmostInk(tableTop + rowHeight + 4, tableTop + 2 * rowHeight - 4);
    QVERIFY(headRight > image.width() / 2);
    QVERIFY2(qAbs(headRight - valueRight) <= 3, qPrintable(QStringLiteral("%1 %2").arg(headRight).arg(valueRight)));
}

void AppToolReportTest::xmlPreviewStopsWithItsJob()
{
    // 484:18196 "Preview of the first lines"; RS7APP L5: the XML preview is one job, which a closed preview or a
    // change of the options cancels - the export stops at once with it (it ran on to the end of a huge file, the
    // cancellation of the job did not reach the export, only the full preview did)
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 595, 842));
    const pdf::PDFDocument document = builder.build();
    const pdf::PDFDocumentXmlExport::Options options;

    const QString preview = pdf::PDFAppToolGUIReport::createXmlPreview(&document, options, nullptr);
    QVERIFY(preview.contains(QStringLiteral("<trailer")));
    QVERIFY(preview.contains(QStringLiteral("<pdfobject")));

    // The job canceled (the preview closed, other options): no object is written any more
    pdf::PDFAppCancelFlag job;
    job.cancel();
    const QString canceled = pdf::PDFAppToolGUIReport::createXmlPreview(&document, options, &job);
    QVERIFY(!canceled.contains(QStringLiteral("<pdfobject")));
}

void AppToolReportTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder> (review RS7FIGMA): the states of Document Report under the ids of their frames
    // (484:15472 … 484:20937) in both themes, at 1x / 2x and right-to-left, for the comparison with Figma; no pixel
    // of the leak color in any of them (Fluent)
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    using H = AppToolReportTestHelper;
    using Tool = pdf::PDFDocumentToolReport;
    for (const char* variant : { "light", "dark", "rtl" })
    {
        const QString variantName = QString::fromLatin1(variant);
        const QString suffix = variantName == QStringLiteral("light") ? QString() : QStringLiteral("-") + variantName;
        QApplication::setLayoutDirection(variantName == QStringLiteral("rtl") ? Qt::RightToLeft : Qt::LeftToRight);
        const QScopeGuard guard([]() { QApplication::setLayoutDirection(Qt::LeftToRight); pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light); });

        ReportTestRegistration registration(true);
        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFThemeManager::getInstance()->setScheme(variantName == QStringLiteral("dark") ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
        AppToolShellFixture::settle();
        QStringList leaks;
        const auto shot = [&fixture, &suffix, &leaks](const QString& frame)
        {
            AppToolShellFixture::settle();
            if (AppToolShellFixture::countLeakPixels(fixture.screenshot(frame + suffix)) > 0)
            {
                leaks << frame + suffix;
            }
        };
        const auto openMenu = [&shot](QToolButton* button, const QString& frame)
        {
            QVERIFY(button);
            QTest::mouseClick(button, Qt::LeftButton);
            AppToolShellFixture::settle();
            shot(frame);
            if (QWidget* popup = QApplication::activePopupWidget())
            {
                popup->close();
            }
            AppToolShellFixture::settle();
        };

        pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
        QVERIFY(shell);
        H::GUI* gui = H::gui(shell);
        QVERIFY(H::waitPreview(gui));
        shot(QStringLiteral("484-15472"));
        openMenu(gui->getKindMenu()->getButton(), QStringLiteral("484-15916"));
        openMenu(gui->getFormatMenu()->getButton(), QStringLiteral("497-20544"));
        openMenu(gui->getDateMenu()->getButton(), QStringLiteral("484-16401"));

        H::setSection(shell, Section::Fonts, true);
        QVERIFY(H::waitPreview(gui));
        shot(QStringLiteral("497-22345"));
        H::setSection(shell, Section::Fonts, false);

        gui->getPageSelection()->setMode(pdf::PDFPageSelection::Mode::Range);
        gui->getPageSelection()->setRange(QStringLiteral("3-1"));
        shot(QStringLiteral("484-20021"));
        gui->getPageSelection()->setRange(QString());
        gui->getPageSelection()->setMode(pdf::PDFPageSelection::Mode::All);

        // 484:18811: the dialog of the report
        QVERIFY(H::waitPreview(gui));
        shell->getToolbar()->getConfirmButton()->click();
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
        shot(QStringLiteral("484-18811"));
        AppToolShellFixture::screenshotDialog(shell->getResultDialog(), QStringLiteral("484-18811-dialog") + suffix);
        shell->getResultDialog()->reject();
        AppToolShellFixture::settle();

        shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_KIND), QString::fromLatin1(Tool::KIND_STRUCTURE_XML));
        AppToolShellFixture::settle();
        QVERIFY(H::waitPreview(gui));
        shot(QStringLiteral("484-18196"));
        shell->getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_KIND), QString::fromLatin1(Tool::KIND_REPORT));

        for (const Section section : pdf::PDFDocumentReport::getSections())
        {
            H::setSection(shell, section, false);
        }
        shot(QStringLiteral("484-20937"));

        fixture.closeMessageBoxes();
        QVERIFY2(leaks.isEmpty(), qPrintable(leaks.join(QStringLiteral(", "))));
    }
}

QTEST_MAIN(AppToolReportTest)

#include "tst_apptoolreporttest.moc"
