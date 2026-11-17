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

// The GUI of Document Statistics (step 7 of phase 2, plan 2.6, 4.2, 8; package P2): the frames 480:14219 …
// 480:15753, the recalculation after a change of the document (R7-22 (7)), the chart tokens (K7.25), the export
// (K7.20). The data of the core tool are given by a test tool of the same parameters (the data of the frames,
// whose numbers are consistent: 4,819,915 bytes of the twelve classes, 12,406 values of the ten types), so the
// GUI is checked independently of the classifier; exportOfShownStatistics uses the writer of the core
// (PDFDocumentToolStatistics::writeOutput, package P1a).

#include "apptoolshellfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfapplocale.h"
#include "pdfappstatisticstable.h"
#include "pdfapptoolgui_statistics.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttool_statistics.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpanels.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QFile>
#include <QJsonArray>
#include <QPushButton>
#include <QScopeGuard>
#include <QThread>
#include <QToolButton>
#include <QtTest>

#include <atomic>

using pdftest::AppToolShellFixture;

/// The core tool of the test: the parameters of review.statistics, the data of the frames, a count of the
/// computations (a computation, which is canceled, counts too)
class StatisticsTestTool : public pdf::PDFDocumentToolStatistics
{
public:
    mutable std::atomic_int computations = 0;
    std::atomic_int delay = 0;          ///< Milliseconds of a computation (it checks the cancellation)

    virtual pdf::PDFDocumentToolResult computePreview(const pdf::PDFDocumentToolContext& context, const QVariantMap& parameters, const pdf::PDFOperationControl* control, pdf::PDFProgressSink* progress) const override
    {
        return execute(context, parameters, control, progress);
    }

    virtual pdf::PDFDocumentToolResult execute(const pdf::PDFDocumentToolContext& context, const QVariantMap& parameters, const pdf::PDFOperationControl* control, pdf::PDFProgressSink* progress) const override
    {
        Q_UNUSED(context);
        Q_UNUSED(parameters);
        Q_UNUSED(progress);
        ++computations;
        for (int i = 0; i < delay.load() / 10; ++i)
        {
            if (pdf::PDFOperationControl::isOperationCanceled(control))
            {
                return pdf::PDFDocumentToolResult::createStopped();
            }
            QThread::msleep(10);
        }

        pdf::PDFDocumentToolResult result;
        result.data = createData();
        return result;
    }

    /// The data of 480:14219 and 480:14555 (the rows not in their order: the GUI sorts them)
    static QJsonObject createData()
    {
        struct ClassRow { const char* id; qint64 count; qint64 bytes; double share; };
        const ClassRow classes[] = {
            { "font", 14, 870954, 18.07 }, { "image", 38, 2960412, 61.42 }, { "content-stream", 24, 476201, 9.88 }, { "other", 9820, 198094, 4.11 },
            { "page", 24, 98807, 2.05 }, { "form", 12, 82419, 1.71 }, { "graphic-state", 96, 44343, 0.92 }, { "color-space", 8, 35667, 0.74 },
            { "annotation", 143, 26509, 0.55 }, { "action", 88, 14941, 0.31 }, { "pattern", 3, 6748, 0.14 }, { "shading", 5, 4820, 0.10 },
        };
        struct TypeRow { const char* type; qint64 count; double share; };
        const TypeRow types[] = {
            { "dictionary", 4739, 38.20 }, { "reference", 2717, 21.90 }, { "integer", 1811, 14.60 }, { "name", 1402, 11.30 }, { "array", 757, 6.10 },
            { "stream", 484, 3.90 }, { "string", 261, 2.10 }, { "real", 174, 1.40 }, { "boolean", 50, 0.40 }, { "null", 11, 0.10 },
        };

        QJsonArray classArray;
        for (const ClassRow& row : classes)
        {
            classArray.append(QJsonObject{ { "id", QString::fromLatin1(row.id) }, { "count", row.count }, { "bytes", row.bytes }, { "share", row.share } });
        }
        QJsonArray typeArray;
        for (const TypeRow& row : types)
        {
            typeArray.append(QJsonObject{ { "type", QString::fromLatin1(row.type) }, { "count", row.count }, { "share", row.share } });
        }

        QJsonObject data;
        data.insert(QString::fromLatin1(DATA_CLASSES), classArray);
        data.insert(QString::fromLatin1(DATA_TYPES), typeArray);
        data.insert(QString::fromLatin1(DATA_OBJECT_COUNT), qint64(10275));
        data.insert(QString::fromLatin1(DATA_VERSION), QStringLiteral("1.7"));
        data.insert(QString::fromLatin1(DATA_COMPLETE), true);
        return data;
    }
};

/// The test tool instead of the built-in one and the GUI of the package, both given back at the end
class StatisticsTestRegistration
{
public:
    StatisticsTestRegistration() :
        tool(std::make_shared<StatisticsTestTool>())
    {
        const QString id = QString::fromLatin1(pdf::PDFDocumentToolStatistics::TOOL_ID);
        m_builtin = pdf::PDFDocumentToolRegistry::getTool(id);
        pdf::PDFDocumentToolRegistry::unregisterTool(id);
        pdf::PDFDocumentToolRegistry::registerTool(tool);
        pdf::PDFDocumentToolGUIRegistry::unregisterFactory(id);
        pdf::PDFDocumentToolGUIRegistry::registerFactory(id, std::make_shared<pdf::PDFDocumentToolGUIFactoryOf<pdf::PDFAppToolGUIStatistics>>());
    }

    ~StatisticsTestRegistration()
    {
        const QString id = QString::fromLatin1(pdf::PDFDocumentToolStatistics::TOOL_ID);
        pdf::PDFDocumentToolRegistry::unregisterTool(id);
        if (m_builtin)
        {
            pdf::PDFDocumentToolRegistry::registerTool(m_builtin);
        }
    }

    std::shared_ptr<StatisticsTestTool> tool;

private:
    std::shared_ptr<const pdf::PDFDocumentTool> m_builtin;
};

/// Helpers of the test
class AppToolStatisticsTestHelper
{
public:
    AppToolStatisticsTestHelper() = delete;

    static pdf::PDFDocumentToolShell* openTool(AppToolShellFixture& fixture, pdf::PDFAppDocumentTab** tab = nullptr)
    {
        pdf::PDFAppDocumentTab* documentTab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 24));
        if (!documentTab || !AppToolShellFixture::waitFor([documentTab]() { return documentTab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        if (tab)
        {
            *tab = documentTab;
        }
        return fixture.openTool(documentTab, QString::fromLatin1(pdf::PDFDocumentToolStatistics::TOOL_ID));
    }

    static pdf::PDFAppToolGUIStatistics* gui(pdf::PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<pdf::PDFAppToolGUIStatistics*>(shell->getGUI()) : nullptr;
    }

    static bool waitForStatistics(pdf::PDFAppToolGUIStatistics* gui)
    {
        return AppToolShellFixture::waitFor([gui]() { return gui->hasStatistics(); });
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
};

class AppToolStatisticsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void byFunctionFrame();
    void byTypeWithoutRecalculation();
    void analyzingAfterChangeIsDebounced();
    void chartTokens();
    void rowsOfData();
    void repairedFileRow();
    void accessibleTable();
    void exportOfShownStatistics();
    void screenshots();

private:
    QLocale m_locale;
};

void AppToolStatisticsTest::initTestCase()
{
    // The numbers of the frames are of the US English user interface (O17)
    m_locale = pdf::PDFAppLocale::getUiLocale();
    pdf::PDFAppLocale::setUiLocale(QLocale(QLocale::English, QLocale::UnitedStates));

    // The geometry of the frames is the geometry of the Fluent style (review RS7FIGMA)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
}

void AppToolStatisticsTest::cleanupTestCase()
{
    pdf::PDFAppLocale::setUiLocale(m_locale);
}

void AppToolStatisticsTest::byFunctionFrame()
{
    // 480:14219: only Close, the table by function sorted by the share, the first row chosen, its card Selected,
    // the head "4,819,915 bytes in 10,275 objects", the status of the largest class
    using H = AppToolStatisticsTestHelper;
    StatisticsTestRegistration registration;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    pdf::PDFAppToolGUIStatistics* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(H::waitForStatistics(gui));
    AppToolShellFixture::settle();

    QVERIFY(!shell->getToolbar()->getConfirmButton()->isVisible());
    QCOMPARE(shell->getToolbar()->getCancelButton()->text(), QStringLiteral("Close"));
    QCOMPARE(gui->getViewButton()->text(), QStringLiteral("By Function"));
    QVERIFY(gui->getViewButton()->isEnabled());
    QVERIFY(gui->getExportButton()->isEnabled());

    pdf::PDFAppStatisticsTable* table = gui->getTable();
    QVERIFY(table && table->isVisible());
    QCOMPARE(table->getTitle(), QStringLiteral("Statistics by object function"));
    QCOMPARE(table->getSummary(), QStringLiteral("4,819,915 bytes in 10,275 objects"));
    QCOMPARE(table->getRows().size(), size_t(12));
    const QStringList order = { "Image", "Font", "Content Stream", "Other", "Page", "Form", "Graphic State", "Color Space", "Annotation", "Action", "Pattern", "Shading" };
    for (int i = 0; i < order.size(); ++i)
    {
        QCOMPARE(table->getRows()[i].title, order[i]);
    }
    QCOMPARE(table->getSelectedRow(), 0);

    QCOMPARE(gui->getSelectedHeader()->getTitleLabel()->text(), QStringLiteral("Selected: Image"));
    QCOMPARE(H::values(gui->getSelectedRows()), QStringList({ "Share of size=61.42 %", "Count=38", "Space usage=2,960,412 bytes", "Average size=77,906 bytes" }));
    QCOMPARE(gui->getStatusText(), QStringLiteral("Images take 61.42 % of the document."));
    QCOMPARE(gui->getDocumentHeader()->getTitleLabel()->text(), QStringLiteral("Document"));
    QCOMPARE(gui->getDocumentHeader()->getSubtitleLabel()->text(), QStringLiteral("Project_Report.pdf"));
    const QStringList file = H::values(gui->getFileRows());
    QCOMPARE(file.size(), 4);
    QCOMPARE(file[1], QStringLiteral("Pages=24"));
    QCOMPARE(file[3], QStringLiteral("Objects=10,275"));
    QVERIFY(!gui->getProgressCard()->isVisible());

    // Another row by the keyboard: the card follows
    QTest::keyClick(table, Qt::Key_Down);
    QCOMPARE(table->getSelectedRow(), 1);
    QCOMPARE(gui->getSelectedHeader()->getTitleLabel()->text(), QStringLiteral("Selected: Font"));
    QCOMPARE(H::values(gui->getSelectedRows()).first(), QStringLiteral("Share of size=18.07 %"));
}

void AppToolStatisticsTest::byTypeWithoutRecalculation()
{
    // 480:14555: the view By Object Type - "12,406 values in 10,275 objects", Share of values and Count; the view
    // chooses only the table shown: the statistics are not computed again
    using H = AppToolStatisticsTestHelper;
    StatisticsTestRegistration registration;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    pdf::PDFAppToolGUIStatistics* gui = H::gui(shell);
    QVERIFY(H::waitForStatistics(gui));
    const int computations = registration.tool->computations.load();

    shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolStatistics::PARAMETER_VIEW), QString::fromLatin1(pdf::PDFDocumentToolStatistics::VIEW_TYPE));
    QTest::qWait(3 * pdf::PDFDocumentToolRunner::PREVIEW_DELAY * pdftest::AppDocumentFixture::getTimeoutFactor());
    AppToolShellFixture::settle();
    QCOMPARE(registration.tool->computations.load(), computations);
    QVERIFY(gui->hasStatistics());

    pdf::PDFAppStatisticsTable* table = gui->getTable();
    QCOMPARE(gui->getViewButton()->text(), QStringLiteral("By Object Type"));
    QCOMPARE(table->getTitle(), QStringLiteral("Statistics by object type"));
    QCOMPARE(table->getSummary(), QStringLiteral("12,406 values in 10,275 objects"));
    QCOMPARE(table->getRows().size(), size_t(10));
    QCOMPARE(table->getRows().front().title, QStringLiteral("Dictionary"));
    QCOMPARE(table->getRows().back().title, QStringLiteral("Null"));
    QCOMPARE(gui->getSelectedHeader()->getTitleLabel()->text(), QStringLiteral("Selected: Dictionary"));
    QCOMPARE(H::values(gui->getSelectedRows()), QStringList({ "Share of values=38.20 %", "Count=4,739" }));
    QCOMPARE(gui->getStatusText(), QStringLiteral("Values counted by their PDF type."));
}

void AppToolStatisticsTest::analyzingAfterChangeIsDebounced()
{
    // R7-22 (7): after a change of the document the statistics are of the old version: "Analyzing the document…"
    // with Objects "Counting…" and View / Export Disabled (480:15218); a burst of changes computes them once,
    // after a short rest
    using H = AppToolStatisticsTestHelper;
    StatisticsTestRegistration registration;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture, &tab);
    QVERIFY(shell);
    pdf::PDFAppToolGUIStatistics* gui = H::gui(shell);
    QVERIFY(H::waitForStatistics(gui));
    const int computations = registration.tool->computations.load();
    registration.tool->delay = 200;

    pdf::PDFAppDocument* document = tab->getDocument();
    for (int i = 0; i < 3; ++i)
    {
        QVERIFY(document->commit(pdf::PDFModifiedDocument(document->getDocument(), nullptr, pdf::PDFModifiedDocument::Annotation), QStringLiteral("Assistant"),
                                 document->getVersion(), pdf::PDFAppChangeSource::Mcp));
    }
    QVERIFY(!gui->hasStatistics());
    QCOMPARE(gui->getCanvasMessage()->getTitle(), QStringLiteral("Analyzing the document…"));
    QVERIFY(!gui->getTable()->isVisible());
    QVERIFY(gui->getProgressCard()->isVisible());
    // 480:15218: the right panel has only the progress card - no empty card of the rows Selected (RS7FIGMA)
    QVERIFY(!gui->getSelectedHeader()->isVisible());
    QVERIFY(!gui->getSelectedRows()->parentWidget()->isVisible());
    QCOMPARE(H::values(gui->getFileRows())[3], QStringLiteral("Objects=Counting…"));
    QVERIFY(!gui->getViewButton()->isEnabled());
    QVERIFY(!gui->getExportButton()->isEnabled());
    QCOMPARE(gui->getStatusText(), QStringLiteral("Analyzing the document…"));
    QVERIFY(!gui->exportStatistics(pdf::PDFDocumentToolTableFormat::Html));

    QVERIFY(H::waitForStatistics(gui));
    AppToolShellFixture::settle();
    QCOMPARE(registration.tool->computations.load(), computations + 1);
    QVERIFY(gui->getTable()->isVisible());
    QCOMPARE(gui->getCanvasMessage()->getTitle(), QString());
}

void AppToolStatisticsTest::chartTokens()
{
    // K7.25: the rows take the categorical tokens chart/1 … chart/12 (Figma VariableID:1586:22043 … 22054) in
    // their order; the stacked bar is painted by them
    using Table = pdf::PDFAppStatisticsTable;
    using Color = pdf::PDFThemeColors;
    QCOMPARE(Table::getChartColor(0), Color::Chart1);
    QCOMPARE(Table::getChartColor(11), Color::Chart12);
    QCOMPARE(Table::getChartColor(12), Color::Chart1);
    QCOMPARE(Color::getBuiltinTokenValue(Color::Chart1, Color::Scheme::Light), QByteArray("#1d4ed8"));
    QCOMPARE(Color::getBuiltinTokenValue(Color::Chart1, Color::Scheme::Dark), QByteArray("#8ab4ff"));
    QCOMPARE(Color::getBuiltinTokenValue(Color::Chart10, Color::Scheme::Dark), QByteArray("#c3cbd6"));
    QCOMPARE(Color::getBuiltinTokenValue(Color::Chart12, Color::Scheme::Light), QByteArray("#0a84ff"));

    pdf::PDFThemeManager::getInstance()->setScheme(Color::Scheme::Light);
    Table table;
    table.setData(StatisticsTestTool::createData(), QString::fromLatin1(pdf::PDFDocumentToolStatistics::VIEW_FUNCTION));
    table.resize(774, table.sizeHint().height());
    const QImage image = table.grab().toImage();
    const QPoint first = table.getSegmentRect(0).center();
    const QPoint second = table.getSegmentRect(1).center();
    QCOMPARE(QColor(image.pixel(first)), pdf::PDFStylePainter::color(Color::Chart1));
    QCOMPARE(QColor(image.pixel(second)), pdf::PDFStylePainter::color(Color::Chart2));

    // The widths of the segments follow the shares (the image 61.42 % of the bar)
    const double ratio = double(table.getSegmentRect(0).width()) / double(table.getStackedBarRect().width());
    QVERIFY2(qAbs(ratio - 0.6142) < 0.02, qPrintable(QString::number(ratio)));
}

void AppToolStatisticsTest::rowsOfData()
{
    // The rows of the data: sorted by the share (the percent of the core), a class without any object left out, a
    // share out of 0 … 100 bounded, an unknown id shown as it is; the subject of the status of a class
    QJsonArray classes;
    classes.append(QJsonObject{ { "id", "font" }, { "count", 1 }, { "bytes", 100 }, { "share", 25.0 } });
    classes.append(QJsonObject{ { "id", "image" }, { "count", 2 }, { "bytes", 300 }, { "share", 75.0 } });
    classes.append(QJsonObject{ { "id", "shading" }, { "count", 0 }, { "bytes", 0 }, { "share", 0.0 } });
    classes.append(QJsonObject{ { "id", "file-structure" }, { "count", 1 }, { "bytes", 0 }, { "share", -3.0 } });
    classes.append(QJsonObject{ { "id", "mystery" }, { "count", 1 }, { "bytes", 0 }, { "share", 0.0 } });
    QJsonObject data;
    data.insert(QString::fromLatin1(pdf::PDFDocumentToolStatistics::DATA_CLASSES), classes);

    const std::vector<pdf::PDFAppStatisticsRow> rows = pdf::PDFAppStatisticsTable::createRows(data, QString::fromLatin1(pdf::PDFDocumentToolStatistics::VIEW_FUNCTION));
    QCOMPARE(rows.size(), size_t(4));
    QCOMPARE(rows[0].title, QStringLiteral("Image"));
    QCOMPARE(rows[0].share, 75.0);
    QCOMPARE(rows[1].title, QStringLiteral("Font"));
    QCOMPARE(rows[1].share, 25.0);
    QCOMPARE(rows[2].title, QStringLiteral("File Structure"));
    QCOMPARE(rows[2].share, 0.0);
    QCOMPARE(rows[3].title, QStringLiteral("mystery"));
    QCOMPARE(pdf::PDFAppStatisticsTable::getClassPluralTitle(QStringLiteral("content-stream")), QStringLiteral("Content streams"));
    QCOMPARE(pdf::PDFAppStatisticsTable::getClassPluralTitle(QStringLiteral("other")), QStringLiteral("Other objects"));
}

void AppToolStatisticsTest::repairedFileRow()
{
    // Plan 4.2 (FD-7): section File of a file, which the reader had to restore from its bytes (a wrong startxref),
    // has the row "Repaired when opened" - the flag of the reading of the tab through the data of the core
    // (DATA_REPAIRED, one source with the report); an intact file has no such row. The core tool of the catalog.
    using H = AppToolStatisticsTestHelper;
    AppToolShellFixture fixture;
    const QString intact = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Intact.pdf"), 2);
    const QString damaged = pdftest::AppDocumentSamples::damagedCopy(intact, QStringLiteral("Damaged.pdf"));
    QVERIFY(!damaged.isEmpty());

    for (const QString& file : { intact, damaged })
    {
        const bool isDamaged = file == damaged;
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
        QCOMPARE(tab->getDocument()->getRestoration() != pdf::PDFAppDocumentRestoration::None, isDamaged);
        pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(pdf::PDFDocumentToolStatistics::TOOL_ID));
        pdf::PDFAppToolGUIStatistics* gui = H::gui(shell);
        QVERIFY(gui);
        QVERIFY(H::waitForStatistics(gui));
        AppToolShellFixture::settle();

        const QStringList rows = H::values(gui->getFileRows());
        const QString repaired = pdf::PDFAppToolGUIStatistics::tr("Repaired when opened") + QStringLiteral("=") + pdf::PDFAppToolGUIStatistics::tr("Yes");
        QCOMPARE(rows.contains(repaired), isDamaged);
        QCOMPARE(rows.size(), isDamaged ? 5 : 4);
        shell->cancel();
        AppToolShellFixture::settle();
    }
}

void AppToolStatisticsTest::accessibleTable()
{
    // RS7APP L8: a screen reader reads the painted table (480:14219) - the head, the chosen row and every row with
    // the values of its cells in the order of the heads Share (%), Count, Size (bytes); by type without the size
    QJsonArray classes;
    classes.append(QJsonObject{ { "id", "font" }, { "count", 1 }, { "bytes", 1200 }, { "share", 25.0 } });
    classes.append(QJsonObject{ { "id", "image" }, { "count", 2 }, { "bytes", 3600 }, { "share", 75.0 } });
    QJsonObject data;
    data.insert(QString::fromLatin1(pdf::PDFDocumentToolStatistics::DATA_CLASSES), classes);

    pdf::PDFAppStatisticsTable table(nullptr);
    QVERIFY(table.accessibleDescription().isEmpty());
    table.setData(data, QString::fromLatin1(pdf::PDFDocumentToolStatistics::VIEW_FUNCTION));
    QCOMPARE(table.getSelectedRow(), 0);
    const QString row = pdf::PDFAppStatisticsTable::tr("%1: share %2 %, count %3, size %4 bytes", "Accessible text of a row of the statistics by function: the class, the share, the count, the size");
    const QLocale locale = pdf::PDFAppLocale::getUiLocale();
    const QString image = row.arg(QStringLiteral("Image"), locale.toString(75.0, 'f', 2), pdf::PDFAppLocale::formatNumber(2), pdf::PDFAppLocale::formatNumber(3600));
    const QString font = row.arg(QStringLiteral("Font"), locale.toString(25.0, 'f', 2), pdf::PDFAppLocale::formatNumber(1), pdf::PDFAppLocale::formatNumber(1200));
    const QString chosen = pdf::PDFAppStatisticsTable::tr("Chosen: %1", "Accessible text of the chosen row of the statistics");
    QCOMPARE(table.accessibleDescription(), QStringList({ table.getTitle(), table.getSummary(), chosen.arg(QStringLiteral("Image")), image, font }).join(QStringLiteral("; ")));

    // Another chosen row is said; no data, nothing to say
    table.setSelectedRow(1);
    QVERIFY(table.accessibleDescription().contains(chosen.arg(QStringLiteral("Font"))));
    table.clear();
    QVERIFY(table.accessibleDescription().isEmpty());
}

void AppToolStatisticsTest::exportOfShownStatistics()
{
    // 480:15368, K7.20: the export writes the shown data (never a new computation); CSV is the table of the view
    // shown, its facts name only that table; the name "Project_Report - statistics.csv" in the folder of the
    // document
    using H = AppToolStatisticsTestHelper;
    StatisticsTestRegistration registration;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    pdf::PDFAppToolGUIStatistics* gui = H::gui(shell);
    QVERIFY(H::waitForStatistics(gui));
    shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolStatistics::PARAMETER_VIEW), QString::fromLatin1(pdf::PDFDocumentToolStatistics::VIEW_TYPE));
    AppToolShellFixture::settle();
    const int computations = registration.tool->computations.load();

    gui->getExportRow(pdf::PDFDocumentToolTableFormat::Csv)->click();
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
    AppToolShellFixture::settle();
    QPointer<pdf::PDFAppToolResultDialog> dialog = shell->getResultDialog();
    QCOMPARE(dialog->getResultKind(), pdf::PDFAppToolResultDialog::ResultKind::FileOnly);
    QCOMPARE(dialog->getFileNameField()->text(), QStringLiteral("Project_Report - statistics.csv"));
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Save File"));
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&dialog]() { return dialog.isNull(); }));
    AppToolShellFixture::settle();

    QFile file(QDir(fixture.folder()).filePath(QStringLiteral("Project_Report - statistics.csv")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray csv = file.readAll();

    // RFC 4180 with ';' (the writer of the core), the C locale: the table of the types only
    QVERIFY2(csv.contains("Dictionary;38.20;4739"), csv.constData());
    QVERIFY(!csv.contains("Image"));
    QCOMPARE(registration.tool->computations.load(), computations);
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
}

void AppToolStatisticsTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder> (review RS7FIGMA): the states of Document Statistics under the ids of their
    // frames (480:14219 … 480:15368) in both themes, at 1x / 2x and right-to-left, for the comparison with Figma;
    // no pixel of the leak color in any of them (Fluent)
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    using H = AppToolStatisticsTestHelper;
    for (const char* variant : { "light", "dark", "rtl" })
    {
        const QString variantName = QString::fromLatin1(variant);
        const QString suffix = variantName == QStringLiteral("light") ? QString() : QStringLiteral("-") + variantName;
        QApplication::setLayoutDirection(variantName == QStringLiteral("rtl") ? Qt::RightToLeft : Qt::LeftToRight);
        const QScopeGuard guard([]() { QApplication::setLayoutDirection(Qt::LeftToRight); pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light); });

        StatisticsTestRegistration registration;
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

        pdf::PDFAppDocumentTab* tab = nullptr;
        pdf::PDFDocumentToolShell* shell = H::openTool(fixture, &tab);
        QVERIFY(shell);
        pdf::PDFAppToolGUIStatistics* gui = H::gui(shell);
        QVERIFY(H::waitForStatistics(gui));
        shot(QStringLiteral("480-14219"));
        openMenu(gui->getViewButton(), QStringLiteral("480-14833"));
        openMenu(gui->getExportButton(), QStringLiteral("497-20172"));

        shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolStatistics::PARAMETER_VIEW), QString::fromLatin1(pdf::PDFDocumentToolStatistics::VIEW_TYPE));
        shot(QStringLiteral("480-14555"));
        shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolStatistics::PARAMETER_VIEW), QString::fromLatin1(pdf::PDFDocumentToolStatistics::VIEW_FUNCTION));

        gui->getExportRow(pdf::PDFDocumentToolTableFormat::Html)->click();
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
        shot(QStringLiteral("480-15368"));
        AppToolShellFixture::screenshotDialog(shell->getResultDialog(), QStringLiteral("480-15368-dialog") + suffix);
        shell->getResultDialog()->reject();
        AppToolShellFixture::settle();

        // 480:15218: a new version is analyzed
        registration.tool->delay = 3000;
        pdf::PDFAppDocument* document = tab->getDocument();
        QVERIFY(document->commit(pdf::PDFModifiedDocument(document->getDocument(), nullptr, pdf::PDFModifiedDocument::Annotation), QStringLiteral("Assistant"),
                                 document->getVersion(), pdf::PDFAppChangeSource::Mcp));
        QTest::qWait(2 * pdf::PDFDocumentToolRunner::PREVIEW_DELAY);
        shot(QStringLiteral("480-15218"));
        registration.tool->delay = 0;
        QVERIFY(H::waitForStatistics(gui));

        fixture.closeMessageBoxes();
        QVERIFY2(leaks.isEmpty(), qPrintable(leaks.join(QStringLiteral(", "))));
    }
}

QTEST_MAIN(AppToolStatisticsTest)

#include "tst_apptoolstatisticstest.moc"
