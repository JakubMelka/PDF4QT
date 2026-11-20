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

// The MCP actions of the tool review of step 7 of phase 2 over HTTP (plan of step 7, 7.1 and 8, with
// chapters 11 and 12): the schemas and their limits at both edges, the results (the data of the document
// marked and sanitized, return_text at most 1 MiB, the data at most 4 MiB, save_file only of a format),
// each tool on a small document with the values of ISO 32000-2 and of the plan, the refusal without bit 5
// of Table 22 (Q7.2), Soft Proofing without the soft proof of a tab (Q7.10), a locked and a busy document,
// and a hostile document (a cycle, a deep chain of /Next, a decode bomb), which never ends the process.
//
// Step 7c: Compare of two documents (documentIds, the summary with the counts of the core, the texts left out
// without bit 5, the XML only with bit 5, the report as a new document of the rule of Merge by open_new_tab or
// save_file, at most 1 000 differences) and Measure (the values of 12.9 by a viewport and by a scale, the CSV of
// Q7.20, the annotations always confirmed with their ghosts - declined nothing changes, allowed one step of Undo
// with the annotations of Q7.5 on different pages -, 500 measurements and 200 points at both edges, the refusals
// of a locked, a busy, a restricted (bit 6) and a certified document), two hostile documents compared.

#include "appmcpreviewdocuments.h"
#include "appmcpstep4.h"
#include "icctestprofile.h"
#include "outputpreviewtestpdf.h"

#include "pdfappbuiltintools.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumentsoftproof.h"
#include "pdfappdocumentview.h"
#include "pdfappmcpchange.h"
#include "pdfappmcppermissions.h"
#include "pdfdocumenttool_compare.h"
#include "pdfdocumenttool_measure.h"
#include "pdfmcpschema.h"
#include "pdfstreamfilters.h"

#include <QApplication>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QtTest>

#include <cmath>
#include <numbers>

using pdftest::AppMcpFixture;
using pdftest::AppMcpImageDocument;
using pdftest::AppMcpStep4;

class AppMcpReviewTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void schemas();
    void statistics();
    void objectInspector();
    void report();
    void reportLimits();
    void inkCoverageAndOutputPreview();
    void softProof();
    void contentPermission();
    void lockedAndBusyDocument();
    void factsInCLocale();
    void hostileDocument();
    void compareSummary();
    void compareXmlAndReport();
    void compareContentPermission();
    void compareLimits();
    void compareHostileDocuments();
    void measureList();
    void measureAddConfirmed();
    void measureLimits();
    void measureRefusals();

private:
    /// A call of review.compare of the documents of the tabs (documentIds) with the common arguments \p common
    static std::optional<pdf::PDFMcpCallResult> compare(AppMcpFixture& fixture, pdf::PDFAppDocumentTab* original, pdf::PDFAppDocumentTab* revised, const QJsonObject& parameters,
                                                        const QJsonObject& common = QJsonObject());

    /// The JSON text of measurements of the parameter "measurements" of review.measure
    static QString measurements(const QJsonArray& items) { return QString::fromUtf8(QJsonDocument(items).toJson(QJsonDocument::Compact)); }

    /// A measurement of the parameter "measurements"
    static QJsonObject measurement(int page, const QString& kind, const std::vector<QPointF>& points, const QJsonObject& scale = QJsonObject());

    /// The scale 1 mm = \p real mm of a measurement
    static QJsonObject scale(double real);

    /// The annotations of the subtype of the page of the document
    static std::vector<const pdf::PDFDictionary*> annotations(const pdf::PDFDocument* document, size_t pageIndex, const QByteArray& subtype);

    /// The open document of the id (the result of open_new_tab)
    static pdf::PDFAppDocument* findDocument(AppMcpFixture& fixture, const QString& documentId);

    /// The values of the measurements of the data of review.measure in their order
    static std::vector<double> values(const QJsonObject& data);

private:
    /// Every permission Allow, the server and the HTTP transport
    static bool start(AppMcpFixture& fixture);

    /// Writes the bytes into a file of the fixture and opens it
    static pdf::PDFAppDocumentTab* open(AppMcpFixture& fixture, const QString& name, const QByteArray& data);

    /// A call of an action of review on the document of the tab with the common arguments \p common
    static std::optional<pdf::PDFMcpCallResult> call(AppMcpFixture& fixture, pdf::PDFAppDocumentTab* tab, const QString& action, const QJsonObject& parameters,
                                                     const QJsonObject& common = QJsonObject());

    /// The object of the action of a result
    static QJsonObject content(const pdf::PDFMcpCallResult& result, const QString& action) { return AppMcpStep4::content(result, action); }

    /// The input schema of review, every permission Allow
    static pdf::PDFMcpSchema inputSchema();

    /// The arguments of review are valid by its input schema
    static bool isValid(const QString& action, const QJsonObject& parameters);

    /// mm² of a rectangle of points (1 pt = 25.4 / 72 mm)
    static double areaMM(double width, double height) { return width * height * (25.4 / 72.0) * (25.4 / 72.0); }

    /// A page of 100 x 100 pt with 100 % cyan on the quarter 50 x 50 pt (ISO 32000-2, 8.6.4.4 DeviceCMYK)
    static QByteArray createCyanQuarter();

    /// One page of 100 x 100 pt for every script of \p scriptLength characters of /AA /O (Table 198)
    static QByteArray createScripts(int count, int scriptLength);
};

void AppMcpReviewTest::initTestCase()
{
    pdf::PDFAppBuiltinTools::registerAll();
}

bool AppMcpReviewTest::start(AppMcpFixture& fixture)
{
    if (!fixture.setPermissions(pdf::PDFAppMcpPermission::Allow) || !fixture.startServer())
    {
        return false;
    }
    fixture.setTransport(pdftest::AppMcpTransport::Http);
    return true;
}

pdf::PDFAppDocumentTab* AppMcpReviewTest::open(AppMcpFixture& fixture, const QString& name, const QByteArray& data)
{
    const QString filePath = fixture.path(name);
    QFile file(filePath);
    if (!file.open(QFile::WriteOnly | QFile::Truncate) || file.write(data) != data.size())
    {
        return nullptr;
    }
    file.close();
    return AppMcpStep4::open(fixture, filePath);
}

std::optional<pdf::PDFMcpCallResult> AppMcpReviewTest::call(AppMcpFixture& fixture, pdf::PDFAppDocumentTab* tab, const QString& action, const QJsonObject& parameters, const QJsonObject& common)
{
    QJsonObject arguments = common;
    arguments.insert(QStringLiteral("documentId"), tab->getDocument()->getId());
    return AppMcpStep4::call(fixture, QStringLiteral("review"), action, parameters, arguments);
}

pdf::PDFMcpSchema AppMcpReviewTest::inputSchema()
{
    return pdf::PDFAppMcpCatalogTools::createInputSchema(pdf::PDFDocumentToolCategory::Review, pdf::PDFAppMcpPermissions::createAll(pdf::PDFAppMcpPermission::Allow)).value_or(pdf::PDFMcpSchema());
}

bool AppMcpReviewTest::isValid(const QString& action, const QJsonObject& parameters)
{
    QStringList errors;
    return inputSchema().validate(QJsonObject({ { QStringLiteral("action"), action }, { action, parameters } }), QString(), &errors);
}

QByteArray AppMcpReviewTest::createCyanQuarter()
{
    OutputPreviewTestPdf pdf;
    pdf.addPage("1 0 0 0 k 0 0 50 50 re f");
    return pdf.write();
}

QByteArray AppMcpReviewTest::createScripts(int count, int scriptLength)
{
    OutputPreviewTestPdf pdf;
    for (int i = 0; i < count; ++i)
    {
        const int script = pdf.add("<< /S /JavaScript /JS (" + QByteArray(scriptLength, char('a' + i % 26)) + ") >>");
        pdf.addPage("0 0 0 1 k 0 0 10 10 re f", "<< >>", "/AA << /O " + OutputPreviewTestPdf::ref(script) + " >>");
    }
    return pdf.write();
}

void AppMcpReviewTest::schemas()
{
    // Plan 7.1 (chapters 11, 12): the six tools of 7a + 7b are actions of review in the order of the catalog;
    // a tool with a format returns the text of its file or saves it (return_text, save_file), the others only
    // their data (no result); the limits of the plan at both edges by the validator of the server
    const std::optional<pdf::PDFAppMcpToolSpec> spec = pdf::PDFAppMcpCatalogTools::createToolSpec(pdf::PDFDocumentToolCategory::Review);
    QVERIFY(spec);
    QStringList actions;
    for (const pdf::PDFAppMcpActionSpec& action : spec->actions)
    {
        actions << action.name;
    }
    QCOMPARE(actions, QStringList({ QStringLiteral("compare"), QStringLiteral("measure"), QStringLiteral("output-preview"), QStringLiteral("ink-coverage"), QStringLiteral("soft-proof"),
                                    QStringLiteral("statistics"), QStringLiteral("object-inspector"), QStringLiteral("report") }));

    // Step 7c (plan 7.1): Compare gives its data, its XML (return_text, save_file) or its report (open_new_tab,
    // save_file); Measure its data, its CSV (return_text, save_file) or its annotations (keep, save_copy,
    // overwrite) - the annotations make the tool review destructive (the annotations of the MCP spec)
    QVERIFY(spec->annotations.destructiveHint);
    const std::shared_ptr<const pdf::PDFDocumentTool> compareTool = pdf::PDFDocumentToolRegistry::getTool(QStringLiteral("review.compare"));
    const std::shared_ptr<const pdf::PDFDocumentTool> measureTool = pdf::PDFDocumentToolRegistry::getTool(QStringLiteral("review.measure"));
    QVERIFY(compareTool && measureTool);
    QCOMPARE(pdf::PDFAppMcpCatalogTools::getToolResultValues(*compareTool->getInfo(), compareTool.get()),
             QStringList({ QStringLiteral("return_text"), QStringLiteral("save_file"), QStringLiteral("open_new_tab") }));
    QCOMPARE(pdf::PDFAppMcpCatalogTools::getToolResultValues(*measureTool->getInfo(), measureTool.get()),
             QStringList({ QStringLiteral("return_text"), QStringLiteral("save_file"), QStringLiteral("keep"), QStringLiteral("save_copy"), QStringLiteral("overwrite") }));
    for (const QString& name : { QStringLiteral("compare"), QStringLiteral("measure") })
    {
        const pdf::PDFAppMcpActionSpec* action = spec->findAction(name);
        QVERIFY(action);
        QCOMPARE(action->permission, pdf::PDFAppMcpPermissionKind::Read);
        QVERIFY(action->usesResult);
        const QJsonObject outputProperties = action->output.toQJson().value(QStringLiteral("properties")).toObject();
        QVERIFY(outputProperties.contains(QStringLiteral("origin")));
        QVERIFY(outputProperties.contains(QStringLiteral("undoDescription")));
    }
    QVERIFY(spec->findAction(QStringLiteral("compare"))->permissionText.contains(QStringLiteral("`open_new_tab` (the report) also Edit documents")));
    QVERIFY(spec->findAction(QStringLiteral("measure"))->permissionText.contains(QStringLiteral("Edit documents (`keep`)")));

    // The panel keeps its kind of drawing, its storage and its scale memory: no parameters of an assistant; the
    // measurements are one JSON text of at most 4 MiB (the limit of the core, 500 measurements of 200 points)
    QVERIFY(isValid(QStringLiteral("measure"), QJsonObject({ { QStringLiteral("mode"), QStringLiteral("add") } })));
    QVERIFY(!isValid(QStringLiteral("measure"), QJsonObject({ { QStringLiteral("kind"), QStringLiteral("linear") } })));
    QVERIFY(!isValid(QStringLiteral("measure"), QJsonObject({ { QStringLiteral("storage"), QStringLiteral("annotations") } })));
    QVERIFY(!isValid(QStringLiteral("measure"), QJsonObject({ { QStringLiteral("rememberScale"), true } })));
    QVERIFY(isValid(QStringLiteral("measure"), QJsonObject({ { QStringLiteral("measurements"), QString(pdf::PDFDocumentToolMeasure::MAXIMUM_MEASUREMENTS_LENGTH, QLatin1Char(' ')) } })));
    QVERIFY(!isValid(QStringLiteral("measure"), QJsonObject({ { QStringLiteral("measurements"), QString(pdf::PDFDocumentToolMeasure::MAXIMUM_MEASUREMENTS_LENGTH + 1, QLatin1Char(' ')) } })));
    QVERIFY(isValid(QStringLiteral("compare"), QJsonObject({ { QStringLiteral("output"), QStringLiteral("report") }, { QStringLiteral("view"), QStringLiteral("overlay") } })));
    QVERIFY(!isValid(QStringLiteral("compare"), QJsonObject({ { QStringLiteral("output"), QStringLiteral("pdf") } })));

    const QStringList fileResults({ QStringLiteral("return_text"), QStringLiteral("save_file") });
    for (const QString& id : { QStringLiteral("review.statistics"), QStringLiteral("review.ink-coverage"), QStringLiteral("review.report"),
                               QStringLiteral("review.object-inspector"), QStringLiteral("review.output-preview"), QStringLiteral("review.soft-proof") })
    {
        const std::shared_ptr<const pdf::PDFDocumentTool> tool = pdf::PDFDocumentToolRegistry::getTool(id);
        QVERIFY2(tool, qPrintable(id));
        const bool hasFile = id == QLatin1String("review.statistics") || id == QLatin1String("review.ink-coverage") || id == QLatin1String("review.report");
        QCOMPARE(pdf::PDFAppMcpCatalogTools::getToolResultValues(*tool->getInfo(), tool.get()), hasFile ? fileResults : QStringList());
        QCOMPARE(pdf::PDFAppMcpCatalogTools::getDefaultResult(*tool->getInfo()), QStringLiteral("return_text"));

        // The permission: Read only, save_file also Open and save files (plan 7.1)
        const pdf::PDFAppMcpActionSpec* action = spec->findAction(id.section(QLatin1Char('.'), 1));
        QVERIFY(action);
        QCOMPARE(action->permission, pdf::PDFAppMcpPermissionKind::Read);
        QCOMPARE(action->usesResult, hasFile);
        QCOMPARE(action->permissionText, hasFile ? QStringLiteral("Read documents; `save_file` also Open and save files") : QStringLiteral("Read documents"));

        // The result marks the data of the document and says what was left out
        const QJsonObject output = action->output.toQJson();
        const QJsonObject outputProperties = output.value(QStringLiteral("properties")).toObject();
        QVERIFY(outputProperties.contains(QStringLiteral("origin")));
        QVERIFY(outputProperties.contains(QStringLiteral("dataOmitted")));
        QVERIFY(outputProperties.value(QStringLiteral("truncated")).toObject().value(QStringLiteral("description")).toString().contains(QStringLiteral("1 MiB")));
    }

    const QJsonObject input = inputSchema().toQJson();
    const QJsonObject resultProperty = input.value(QStringLiteral("properties")).toObject().value(QStringLiteral("result")).toObject();
    QCOMPARE(resultProperty.value(QStringLiteral("enum")).toArray(), QJsonArray({ QStringLiteral("return_text"), QStringLiteral("save_file"), QStringLiteral("open_new_tab"), QStringLiteral("keep"),
                                                                                  QStringLiteral("save_copy"), QStringLiteral("overwrite") }));
    QVERIFY(input.value(QStringLiteral("properties")).toObject().contains(QStringLiteral("documentIds")));
    QVERIFY2(resultProperty.value(QStringLiteral("description")).toString().contains(QStringLiteral("at most 1 MiB")), qPrintable(resultProperty.value(QStringLiteral("description")).toString()));

    // Object Inspector: depth at most 4, maxBytes at most 1 MiB, limit at most 1000 (plan 7.1)
    const QString inspector = QStringLiteral("object-inspector");
    QVERIFY(isValid(inspector, QJsonObject({ { QStringLiteral("depth"), 4 } })));
    QVERIFY(!isValid(inspector, QJsonObject({ { QStringLiteral("depth"), 5 } })));
    QVERIFY(!isValid(inspector, QJsonObject({ { QStringLiteral("depth"), 0 } })));
    QVERIFY(isValid(inspector, QJsonObject({ { QStringLiteral("maxBytes"), 1024 * 1024 } })));
    QVERIFY(!isValid(inspector, QJsonObject({ { QStringLiteral("maxBytes"), 1024 * 1024 + 1 } })));
    QVERIFY(isValid(inspector, QJsonObject({ { QStringLiteral("limit"), 1000 } })));
    QVERIFY(!isValid(inspector, QJsonObject({ { QStringLiteral("limit"), 1001 } })));
    QVERIFY(isValid(inspector, QJsonObject({ { QStringLiteral("offset"), 0 } })));
    QVERIFY(!isValid(inspector, QJsonObject({ { QStringLiteral("offset"), -1 } })));

    // The resolution: one limit 4096 everywhere (R7-22 (5))
    for (const QString& action : { QStringLiteral("ink-coverage"), QStringLiteral("output-preview"), QStringLiteral("soft-proof") })
    {
        QVERIFY2(isValid(action, QJsonObject({ { QStringLiteral("resolution"), 4096 } })), qPrintable(action));
        QVERIFY2(!isValid(action, QJsonObject({ { QStringLiteral("resolution"), 4097 } })), qPrintable(action));
        QVERIFY2(isValid(action, QJsonObject({ { QStringLiteral("resolution"), 72 } })), qPrintable(action));
        QVERIFY2(!isValid(action, QJsonObject({ { QStringLiteral("resolution"), 71 } })), qPrintable(action));
    }

    // The alarm color of Soft Proofing belongs to the view of a tab (Q7.10), the mode and the paper of Output
    // Preview to its picture: no parameters of an assistant (the run gives no picture, they would be ignored);
    // the separations change the measured inks (the spot inks), they stay
    QVERIFY(isValid(QStringLiteral("soft-proof"), QJsonObject({ { QStringLiteral("intent"), QStringLiteral("perceptual") } })));
    QVERIFY(!isValid(QStringLiteral("soft-proof"), QJsonObject({ { QStringLiteral("alarmColor"), QStringLiteral("#00ff00") } })));
    QVERIFY(isValid(QStringLiteral("output-preview"), QJsonObject({ { QStringLiteral("simulateSeparations"), false } })));
    QVERIFY(!isValid(QStringLiteral("output-preview"), QJsonObject({ { QStringLiteral("mode"), QStringLiteral("separations") } })));
    QVERIFY(!isValid(QStringLiteral("output-preview"), QJsonObject({ { QStringLiteral("simulatePaperColor"), true } })));
    QVERIFY(!isValid(QStringLiteral("output-preview"), QJsonObject({ { QStringLiteral("paperColor"), QStringLiteral("#ffffe0") } })));
}

void AppMcpReviewTest::statistics()
{
    // Plan 4.2: the objects by their function - the page (4), its content stream (3); the catalog (1) and the
    // page tree (2) are no root of a class (Other); the size of the file of a saved document (FD-2)
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    const QByteArray file = createCyanQuarter();
    pdf::PDFAppDocumentTab* tab = open(fixture, QStringLiteral("Cyan.pdf"), file);
    QVERIFY(tab);

    std::optional<pdf::PDFMcpCallResult> result = call(fixture, tab, QStringLiteral("statistics"), QJsonObject());
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QJsonObject statistics = content(*result, QStringLiteral("statistics"));
    QCOMPARE(statistics.value(QStringLiteral("status")).toString(), QStringLiteral("success"));
    QCOMPARE(statistics.value(QStringLiteral("origin")).toString(), QStringLiteral("document"));
    QVERIFY(result->getFirstText().startsWith(pdf::PDFAppMcpToolSet::getUntrustedContentLine() + QLatin1Char('\n')));
    QVERIFY(!statistics.contains(QStringLiteral("outputs")));

    const QJsonObject data = statistics.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("objectCount")).toInteger(), qint64(4));
    QCOMPARE(data.value(QStringLiteral("pageCount")).toInteger(), qint64(1));
    QCOMPARE(data.value(QStringLiteral("fileSize")).toInteger(), qint64(file.size()));
    std::map<QString, qint64> counts;
    for (const QJsonValue& row : data.value(QStringLiteral("classes")).toArray())
    {
        counts[row.toObject().value(QStringLiteral("id")).toString()] = row.toObject().value(QStringLiteral("count")).toInteger();
    }
    QCOMPARE(counts, (std::map<QString, qint64>({ { QStringLiteral("page"), 1 }, { QStringLiteral("content-stream"), 1 }, { QStringLiteral("other"), 2 } })));

    // CSV of the current view (K7.20): the text of the file, separated by semicolons (RFC 4180 with ";")
    result = call(fixture, tab, QStringLiteral("statistics"), QJsonObject({ { QStringLiteral("format"), QStringLiteral("csv") } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    statistics = content(*result, QStringLiteral("statistics"));
    QVERIFY2(statistics.value(QStringLiteral("mimeType")).toString().startsWith(QStringLiteral("text/csv")), qPrintable(statistics.value(QStringLiteral("mimeType")).toString()));
    QCOMPARE(statistics.value(QStringLiteral("truncated")).toBool(), false);
    const QString text = result->getFirstText();
    QVERIFY(text.startsWith(pdf::PDFAppMcpToolSet::getUntrustedContentLine()));
    QVERIFY2(text.contains(QStringLiteral("Content Stream")) && text.contains(QLatin1Char(';')), qPrintable(text));
    QCOMPARE(statistics.value(QStringLiteral("data")).toObject().value(QStringLiteral("objectCount")).toInteger(), qint64(4));

    // save_file writes the file of a format; without a format nothing is written (no file of the data only)
    const QString emptyPath = fixture.path(QStringLiteral("Statistics-none.csv"));
    result = call(fixture, tab, QStringLiteral("statistics"), QJsonObject(), QJsonObject({ { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), emptyPath } }));
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("save_file writes the file of a format")), qPrintable(result->getFirstText()));
    QVERIFY(!QFileInfo::exists(emptyPath));

    const QString csvPath = fixture.path(QStringLiteral("Statistics.csv"));
    result = call(fixture, tab, QStringLiteral("statistics"), QJsonObject({ { QStringLiteral("format"), QStringLiteral("csv") } }),
                  QJsonObject({ { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), csvPath } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QCOMPARE(QDir::cleanPath(content(*result, QStringLiteral("statistics")).value(QStringLiteral("path")).toString()), QDir::cleanPath(csvPath));
    const QByteArray csv = AppMcpStep4::readFile(csvPath);
    QVERIFY(csv.contains("Content Stream"));
    QVERIFY(csv.contains("\r\n"));
}

void AppMcpReviewTest::objectInspector()
{
    // Plan 4.3, 7.1: an object as JSON (ISO 32000-2, 7.7.2: the catalog /Type /Catalog /Pages), the depth
    // follows the references, a list of a view by offset / limit, the content of a stream at most maxBytes
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFAppDocumentTab* tab = open(fixture, QStringLiteral("Cyan.pdf"), createCyanQuarter());
    QVERIFY(tab);
    const QString action = QStringLiteral("object-inspector");

    std::optional<pdf::PDFMcpCallResult> result = call(fixture, tab, action, QJsonObject({ { QStringLiteral("object"), QStringLiteral("1 0 R") } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QJsonObject object = content(*result, action).value(QStringLiteral("data")).toObject().value(QStringLiteral("object")).toObject();
    QCOMPARE(object.value(QStringLiteral("object")).toString(), QStringLiteral("1 0 R"));
    QCOMPARE(object.value(QStringLiteral("type")).toString(), QStringLiteral("dictionary"));
    std::map<QString, QJsonObject> entries;
    for (const QJsonValue& entry : object.value(QStringLiteral("entries")).toArray())
    {
        entries[entry.toObject().value(QStringLiteral("key")).toString()] = entry.toObject().value(QStringLiteral("value")).toObject();
    }
    QCOMPARE(entries[QStringLiteral("Type")].value(QStringLiteral("value")).toString(), QStringLiteral("Catalog"));
    QCOMPARE(entries[QStringLiteral("Pages")].value(QStringLiteral("reference")).toString(), QStringLiteral("2 0 R"));
    QVERIFY(!entries[QStringLiteral("Pages")].contains(QStringLiteral("object")));

    // Depth 2: the page tree with its /Count 1 (7.7.3.2)
    result = call(fixture, tab, action, QJsonObject({ { QStringLiteral("object"), QStringLiteral("catalog") }, { QStringLiteral("depth"), 2 } }));
    QVERIFY(result && !result->isError);
    object = content(*result, action).value(QStringLiteral("data")).toObject().value(QStringLiteral("object")).toObject();
    QJsonObject pages;
    for (const QJsonValue& entry : object.value(QStringLiteral("entries")).toArray())
    {
        if (entry.toObject().value(QStringLiteral("key")).toString() == QLatin1String("Pages"))
        {
            pages = entry.toObject().value(QStringLiteral("value")).toObject().value(QStringLiteral("object")).toObject();
        }
    }
    bool hasCount = false;
    for (const QJsonValue& entry : pages.value(QStringLiteral("entries")).toArray())
    {
        hasCount = hasCount || (entry.toObject().value(QStringLiteral("key")).toString() == QLatin1String("Count") &&
                                entry.toObject().value(QStringLiteral("value")).toObject().value(QStringLiteral("value")).toInteger() == 1);
    }
    QVERIFY(hasCount);

    // The list of the objects: the trailer and the 4 objects; a window of 2, then the rest
    result = call(fixture, tab, action, QJsonObject({ { QStringLiteral("object"), QString() }, { QStringLiteral("view"), QStringLiteral("objects") }, { QStringLiteral("limit"), 2 } }));
    QVERIFY(result && !result->isError);
    QJsonObject data = content(*result, action).value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("total")).toInteger(), qint64(5));
    QCOMPARE(data.value(QStringLiteral("truncated")).toBool(), true);
    QJsonArray roots = data.value(QStringLiteral("roots")).toArray();
    QCOMPARE(roots.size(), 2);
    QCOMPARE(roots.at(0).toObject().value(QStringLiteral("object")).toString(), QStringLiteral("trailer"));
    QCOMPARE(roots.at(1).toObject().value(QStringLiteral("object")).toString(), QStringLiteral("1 0 R"));
    result = call(fixture, tab, action, QJsonObject({ { QStringLiteral("object"), QString() }, { QStringLiteral("view"), QStringLiteral("objects") }, { QStringLiteral("offset"), 4 },
                                                     { QStringLiteral("limit"), 2 } }));
    QVERIFY(result && !result->isError);
    data = content(*result, action).value(QStringLiteral("data")).toObject();
    roots = data.value(QStringLiteral("roots")).toArray();
    QCOMPARE(roots.size(), 1);
    QCOMPARE(roots.at(0).toObject().value(QStringLiteral("object")).toString(), QStringLiteral("4 0 R"));
    QCOMPARE(data.value(QStringLiteral("truncated")).toBool(), false);

    // The content stream "1 0 0 0 k 0 0 50 50 re f" (24 bytes): 5 bytes of it, then all of it with maxBytes 1 MiB
    result = call(fixture, tab, action, QJsonObject({ { QStringLiteral("object"), QStringLiteral("3 0 R") }, { QStringLiteral("decodeStream"), true }, { QStringLiteral("maxBytes"), 5 } }));
    QVERIFY(result && !result->isError);
    data = content(*result, action).value(QStringLiteral("data")).toObject();
    object = data.value(QStringLiteral("object")).toObject();
    QCOMPARE(object.value(QStringLiteral("decodeStatus")).toString(), QStringLiteral("decoded"));
    QCOMPARE(object.value(QStringLiteral("decodedLength")).toInteger(), qint64(24));
    QCOMPARE(object.value(QStringLiteral("content")).toString(), QStringLiteral("1 0 0"));
    QCOMPARE(object.value(QStringLiteral("contentTruncated")).toBool(), true);
    QCOMPARE(data.value(QStringLiteral("truncated")).toBool(), true);
    result = call(fixture, tab, action, QJsonObject({ { QStringLiteral("object"), QStringLiteral("3 0 R") }, { QStringLiteral("decodeStream"), true }, { QStringLiteral("maxBytes"), 1024 * 1024 } }));
    QVERIFY(result && !result->isError);
    object = content(*result, action).value(QStringLiteral("data")).toObject().value(QStringLiteral("object")).toObject();
    QCOMPARE(object.value(QStringLiteral("content")).toString(), QStringLiteral("1 0 0 0 k 0 0 50 50 re f"));
    QCOMPARE(object.value(QStringLiteral("contentTruncated")).toBool(), false);

    // Over the limits: the error of the arguments names the parameter, nothing runs
    for (const QJsonObject& parameters : { QJsonObject({ { QStringLiteral("depth"), 5 } }), QJsonObject({ { QStringLiteral("limit"), 1001 } }),
                                          QJsonObject({ { QStringLiteral("maxBytes"), 1024 * 1024 + 1 } }) })
    {
        result = call(fixture, tab, action, parameters);
        QVERIFY(result);
        QVERIFY(result->isError);
        QVERIFY2(result->getFirstText().startsWith(QStringLiteral("Invalid arguments")) && result->getFirstText().contains(QStringLiteral("object-inspector.") + parameters.begin().key()),
                 qPrintable(result->getFirstText()));
    }

    // Its data are the result: result and path are errors
    result = call(fixture, tab, action, QJsonObject(), QJsonObject({ { QStringLiteral("result"), QStringLiteral("return_text") } }));
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("returns its data; it takes neither result nor path")), qPrintable(result->getFirstText()));
}

void AppMcpReviewTest::report()
{
    // Plan 4.4: the sections asked for; Basic information of a page of 100 x 100 pt = 35.28 mm (Table 31 with
    // /UserUnit 1), the version of the header; the text of the format with the data
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFAppDocumentTab* tab = open(fixture, QStringLiteral("Cyan.pdf"), createCyanQuarter());
    QVERIFY(tab);
    QJsonObject onlyBasic({ { QStringLiteral("basicInformation"), true }, { QStringLiteral("metadata"), false }, { QStringLiteral("javascript"), false },
                            { QStringLiteral("namedDestinations"), false }, { QStringLiteral("pageBoxes"), false }, { QStringLiteral("structureTree"), false },
                            { QStringLiteral("format"), QStringLiteral("text") } });

    std::optional<pdf::PDFMcpCallResult> result = call(fixture, tab, QStringLiteral("report"), onlyBasic);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const QJsonObject reportResult = content(*result, QStringLiteral("report"));
    QVERIFY2(reportResult.value(QStringLiteral("mimeType")).toString().startsWith(QStringLiteral("text/plain")), qPrintable(reportResult.value(QStringLiteral("mimeType")).toString()));
    QCOMPARE(reportResult.value(QStringLiteral("truncated")).toBool(), false);
    QCOMPARE(reportResult.value(QStringLiteral("origin")).toString(), QStringLiteral("document"));
    const QJsonObject basic = reportResult.value(QStringLiteral("data")).toObject().value(QStringLiteral("sections")).toObject().value(QStringLiteral("basicInformation")).toObject();
    QCOMPARE(basic.value(QStringLiteral("pageCount")).toInteger(), qint64(1));
    QCOMPARE(basic.value(QStringLiteral("pageWidth")).toDouble(), 35.28);
    QCOMPARE(basic.value(QStringLiteral("version")).toString(), QStringLiteral("1.7"));
    QVERIFY2(result->getFirstText().contains(QStringLiteral("35.3")), qPrintable(result->getFirstText()));
    QVERIFY(!reportResult.value(QStringLiteral("data")).toObject().value(QStringLiteral("sections")).toObject().contains(QStringLiteral("javascript")));

    // Structure as XML: always a file (7.5 of ISO 32000-2 as XML), saved by save_file as .xml only
    const QString xmlPath = fixture.path(QStringLiteral("Structure.xml"));
    result = call(fixture, tab, QStringLiteral("report"), QJsonObject({ { QStringLiteral("kind"), QStringLiteral("structure-xml") } }),
                  QJsonObject({ { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), fixture.path(QStringLiteral("Structure.txt")) } }));
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("extension")), qPrintable(result->getFirstText()));
    result = call(fixture, tab, QStringLiteral("report"), QJsonObject({ { QStringLiteral("kind"), QStringLiteral("structure-xml") }, { QStringLiteral("format"), QStringLiteral("none") } }),
                  QJsonObject({ { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), xmlPath } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const QByteArray xml = AppMcpStep4::readFile(xmlPath);
    QVERIFY(xml.startsWith("<?xml"));
    QVERIFY(xml.contains("Catalog"));
}

void AppMcpReviewTest::reportLimits()
{
    // Plan 7.1: return_text gives at most 1 MiB of the text (save_file all of it); the data of a result at most
    // 4 MiB - with more they are left out and a note says what to do. Scripts of 1 MiB each (the scanner keeps
    // a script of PDFJavaScriptScanner::MAXIMUM_SCRIPT_LENGTH whole).
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    constexpr int MIB = 1024 * 1024;
    const QJsonObject onlyScripts({ { QStringLiteral("basicInformation"), false }, { QStringLiteral("metadata"), false }, { QStringLiteral("javascript"), true },
                                    { QStringLiteral("namedDestinations"), false }, { QStringLiteral("pageBoxes"), false }, { QStringLiteral("structureTree"), false },
                                    { QStringLiteral("format"), QStringLiteral("text") } });

    // One script of 1 MiB: the text is longer than 1 MiB and cut, the data (about 1 MiB) stay
    pdf::PDFAppDocumentTab* tab = open(fixture, QStringLiteral("OneScript.pdf"), createScripts(1, MIB));
    QVERIFY(tab);
    std::optional<pdf::PDFMcpCallResult> result = call(fixture, tab, QStringLiteral("report"), onlyScripts);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText().left(500)));
    QJsonObject reportResult = content(*result, QStringLiteral("report"));
    QVERIFY(reportResult.value(QStringLiteral("size")).toInteger() > MIB);
    QCOMPARE(reportResult.value(QStringLiteral("truncated")).toBool(), true);
    QVERIFY2(result->getFirstText().size() <= MIB + 256, qPrintable(QString::number(result->getFirstText().size())));
    QVERIFY(result->getFirstText().contains(QStringLiteral("The text is cut at 1 MiB")));
    QVERIFY(!reportResult.contains(QStringLiteral("dataOmitted")));
    QJsonArray scripts = reportResult.value(QStringLiteral("data")).toObject().value(QStringLiteral("sections")).toObject().value(QStringLiteral("javascript")).toObject().value(QStringLiteral("scripts")).toArray();
    QCOMPARE(scripts.size(), 1);
    QCOMPARE(scripts.at(0).toObject().value(QStringLiteral("code")).toString().size(), MIB);

    // Three scripts (3 MiB of data): kept; five scripts (5 MiB): left out with the note
    tab = open(fixture, QStringLiteral("ThreeScripts.pdf"), createScripts(3, MIB));
    QVERIFY(tab);
    QJsonObject dataOnly = onlyScripts;
    dataOnly.insert(QStringLiteral("format"), QStringLiteral("none"));
    result = call(fixture, tab, QStringLiteral("report"), dataOnly);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText().left(500)));
    reportResult = content(*result, QStringLiteral("report"));
    QVERIFY(!reportResult.contains(QStringLiteral("dataOmitted")));
    scripts = reportResult.value(QStringLiteral("data")).toObject().value(QStringLiteral("sections")).toObject().value(QStringLiteral("javascript")).toObject().value(QStringLiteral("scripts")).toArray();
    QCOMPARE(scripts.size(), 3);

    tab = open(fixture, QStringLiteral("FiveScripts.pdf"), createScripts(5, MIB));
    QVERIFY(tab);
    result = call(fixture, tab, QStringLiteral("report"), dataOnly);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText().left(500)));
    reportResult = content(*result, QStringLiteral("report"));
    QCOMPARE(reportResult.value(QStringLiteral("dataOmitted")).toBool(), true);
    QVERIFY(reportResult.value(QStringLiteral("data")).toObject().isEmpty());
    QVERIFY2(reportResult.value(QStringLiteral("note")).toString().contains(QStringLiteral("more than 4 MiB")), qPrintable(reportResult.value(QStringLiteral("note")).toString()));
    QCOMPARE(reportResult.value(QStringLiteral("status")).toString(), QStringLiteral("success"));
}

void AppMcpReviewTest::inkCoverageAndOutputPreview()
{
    // Plan 4.5, 4.6, 8: 100 % cyan on a quarter of the crop box (no TrimBox, Q7.6 b) is 25 % of the box, its
    // area (50 x 25.4 / 72)² = 311.13 mm²; one pixel per point at the resolution 100; the total area coverage
    // 100 % is over a limit of 50 % on the whole quarter (8.6.4.4, 474:9818)
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFAppDocumentTab* tab = open(fixture, QStringLiteral("Cyan.pdf"), createCyanQuarter());
    QVERIFY(tab);

    std::optional<pdf::PDFMcpCallResult> result = call(fixture, tab, QStringLiteral("ink-coverage"), QJsonObject({ { QStringLiteral("resolution"), 100 } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QJsonObject data = content(*result, QStringLiteral("ink-coverage")).value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("inks")).toArray().at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Cyan"));
    const QJsonObject page = data.value(QStringLiteral("pages")).toArray().at(0).toObject();
    QVERIFY2(std::abs(page.value(QStringLiteral("ratio")).toArray().at(0).toDouble() - 25.0) < 0.01, qPrintable(QString::number(page.value(QStringLiteral("ratio")).toArray().at(0).toDouble())));
    QVERIFY(std::abs(page.value(QStringLiteral("area")).toArray().at(0).toDouble() - areaMM(50, 50)) < 0.01);
    QCOMPARE(page.value(QStringLiteral("ratio")).toArray().at(3).toDouble(), 0.0);
    QCOMPARE(data.value(QStringLiteral("box")).toString(), QStringLiteral("crop"));

    // The table as CSV in a file (save_file, Open and save files)
    const QString csvPath = fixture.path(QStringLiteral("Coverage.csv"));
    result = call(fixture, tab, QStringLiteral("ink-coverage"), QJsonObject({ { QStringLiteral("resolution"), 100 }, { QStringLiteral("format"), QStringLiteral("csv") } }),
                  QJsonObject({ { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), csvPath } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QVERIFY(AppMcpStep4::readFile(csvPath).contains("Cyan"));

    result = call(fixture, tab, QStringLiteral("output-preview"), QJsonObject({ { QStringLiteral("resolution"), 100 }, { QStringLiteral("coverageLimit"), 50 } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    data = content(*result, QStringLiteral("output-preview")).value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("page")).toInteger(), qint64(1));
    QCOMPARE(qRound(data.value(QStringLiteral("maximumCoverage")).toDouble()), 100);
    QVERIFY(std::abs(data.value(QStringLiteral("areaOverCoverageLimit")).toDouble() - areaMM(50, 50)) < 0.01);
    QCOMPARE(data.value(QStringLiteral("richBlackArea")).toDouble(), 0.0);
    const QJsonObject cyan = data.value(QStringLiteral("inks")).toArray().at(0).toObject();
    QCOMPARE(cyan.value(QStringLiteral("name")).toString(), QStringLiteral("Cyan"));
    QVERIFY(std::abs(cyan.value(QStringLiteral("area")).toDouble() - areaMM(50, 50)) < 0.01);
    QVERIFY(std::abs(cyan.value(QStringLiteral("ratio")).toDouble() - 25.0) < 0.01);

    // A page, which does not exist, is an error of the run (the document has 1 page)
    result = call(fixture, tab, QStringLiteral("output-preview"), QJsonObject({ { QStringLiteral("page"), 2 } }));
    QVERIFY(result);
    QVERIFY(result->isError);
}

void AppMcpReviewTest::softProof()
{
    // Plan 4.7: the output intent of the document is the profile (an empty "profile"); a gray page (a* = b* = 0,
    // in the gamut of the test profile) with a pure green rectangle of a quarter of the crop box (out of it):
    // 25 %, (150 x 25.4 / 72) x (200 x 25.4 / 72) mm². Q7.10: the proof belongs to the tab - a call through
    // MCP neither reads nor changes the soft proof of a tab.
    AppMcpFixture fixture;
    QVERIFY(start(fixture));

    AppMcpImageDocument document;
    const pdf::PDFObjectReference profile = document.addStream("<< /N 4 >>", pdftest::IccTestProfile::createCmykProfile("Test Printing Gray"));
    document.builder.mergeTo(document.builder.getCatalogReference(),
                             AppMcpImageDocument::parse("<< /OutputIntents [<< /Type /OutputIntent /S /GTS_PDFX /OutputConditionIdentifier (Test CMYK) /DestOutputProfile " +
                                                        AppMcpImageDocument::ref(profile) + " >>] >>"));
    document.addPage("<< >>", "0.5 g 0 0 300 400 re f 0 1 0 rg 0 0 150 200 re f");
    const QString filePath = document.write(fixture.path(QStringLiteral("Poster.pdf")));
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, filePath);
    QVERIFY(tab);

    // The tab proofs by its own state (perceptual, a blue alarm color)
    pdf::PDFAppDocumentSoftProof* proof = pdf::PDFAppDocumentSoftProof::getSoftProof(tab->getView());
    QVERIFY(proof);
    pdf::PDFAppDocumentSoftProof::State state;
    state.isProofing = true;
    state.isGamutChecking = true;
    state.intent = pdf::RenderingIntent::Perceptual;
    state.alarmColor = Qt::blue;
    proof->setState(state);
    const pdf::PDFAppDocumentSoftProof::State before = proof->getState();

    std::optional<pdf::PDFMcpCallResult> result = call(fixture, tab, QStringLiteral("soft-proof"), QJsonObject({ { QStringLiteral("resolution"), 400 },
                                                                                                                 { QStringLiteral("intent"), QStringLiteral("absolute-colorimetric") } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const QJsonObject data = content(*result, QStringLiteral("soft-proof")).value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("intent")).toString(), QStringLiteral("absolute-colorimetric"));
    QCOMPARE(data.value(QStringLiteral("profile")).toString(), QStringLiteral("Test Printing Gray"));
    const QJsonObject page = data.value(QStringLiteral("pages")).toArray().at(0).toObject();
    QCOMPARE(page.value(QStringLiteral("page")).toInteger(), qint64(1));
    QVERIFY2(std::abs(page.value(QStringLiteral("share")).toDouble() - 25.0) < 0.01, qPrintable(QString::number(page.value(QStringLiteral("share")).toDouble())));
    QVERIFY2(std::abs(page.value(QStringLiteral("area")).toDouble() - areaMM(150, 200)) < 0.1, qPrintable(QString::number(page.value(QStringLiteral("area")).toDouble())));
    QCOMPARE(data.value(QStringLiteral("pagesOutOfGamut")).toInteger(), qint64(1));
    QVERIFY(proof->getState() == before);

    // A tab without a proof gets none (no state is created by the call)
    pdf::PDFAppDocumentTab* second = AppMcpStep4::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Pages.pdf"), 51));
    QVERIFY(second);

    // At most 50 pages (plan 7.1): 51 are refused before the profile is looked for, 50 pass the check (this
    // document has no output intent - the next error of the run)
    result = call(fixture, second, QStringLiteral("soft-proof"), QJsonObject({ { QStringLiteral("resolution"), 72 },
                                                                               { QStringLiteral("pages"), QJsonObject({ { QStringLiteral("pages"), QStringLiteral("1-51") } }) } }));
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("at most 50 pages")), qPrintable(result->getFirstText()));
    result = call(fixture, second, QStringLiteral("soft-proof"), QJsonObject({ { QStringLiteral("resolution"), 72 },
                                                                               { QStringLiteral("pages"), QJsonObject({ { QStringLiteral("pages"), QStringLiteral("1-50") } }) } }));
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(!result->getFirstText().contains(QStringLiteral("at most 50 pages")) && result->getFirstText().contains(QStringLiteral("output intent")), qPrintable(result->getFirstText()));
    QVERIFY(!pdf::PDFAppDocumentSoftProof::findSoftProof(second->getView()));
}

void AppMcpReviewTest::contentPermission()
{
    // Q7.2, ISO 32000-2 Table 22: viewing is reading; the JavaScript, the XMP, the data of the streams are content
    // (bit 5). Without bit 5 such a call is refused before the run with the text of the unlock by the owner in
    // PDF4QT and what works without it; the same tools without the content run.
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    AppMcpImageDocument document;
    document.addPage("<< >>", "0 0 0 1 k 0 0 10 10 re f");
    document.encrypt(AppMcpStep4::allowedWithout(5));
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, document.write(fixture.path(QStringLiteral("Restricted.pdf"))));
    QVERIFY(tab);

    const auto isRefused = [](const std::optional<pdf::PDFMcpCallResult>& result, const QString& hint)
    {
        return result && result->isError && result->getFirstText().contains(QStringLiteral("Table 22")) && result->getFirstText().contains(QStringLiteral("owner password")) &&
               result->getFirstText().contains(hint);
    };
    const QString reportHint = QStringLiteral("javascript and metadata set to false");

    // The default report has the JavaScript and the XMP
    std::optional<pdf::PDFMcpCallResult> result = call(fixture, tab, QStringLiteral("report"), QJsonObject());
    QVERIFY2(isRefused(result, reportHint), qPrintable(result ? result->getFirstText() : QString()));
    result = call(fixture, tab, QStringLiteral("report"), QJsonObject({ { QStringLiteral("metadata"), false } }));
    QVERIFY2(isRefused(result, reportHint), qPrintable(result ? result->getFirstText() : QString()));
    result = call(fixture, tab, QStringLiteral("report"), QJsonObject({ { QStringLiteral("javascript"), false } }));
    QVERIFY2(isRefused(result, reportHint), qPrintable(result ? result->getFirstText() : QString()));
    result = call(fixture, tab, QStringLiteral("report"), QJsonObject({ { QStringLiteral("javascript"), false }, { QStringLiteral("metadata"), false } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));

    // Structure as XML: the streams are the content
    result = call(fixture, tab, QStringLiteral("report"), QJsonObject({ { QStringLiteral("kind"), QStringLiteral("structure-xml") }, { QStringLiteral("exportStreams"), true } }));
    QVERIFY2(isRefused(result, reportHint), qPrintable(result ? result->getFirstText() : QString()));
    result = call(fixture, tab, QStringLiteral("report"), QJsonObject({ { QStringLiteral("kind"), QStringLiteral("structure-xml") } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));

    // Object Inspector: the content of a stream is the content, the objects are not
    result = call(fixture, tab, QStringLiteral("object-inspector"), QJsonObject({ { QStringLiteral("decodeStream"), true } }));
    QVERIFY2(isRefused(result, QStringLiteral("decodeStream false")), qPrintable(result ? result->getFirstText() : QString()));
    result = call(fixture, tab, QStringLiteral("object-inspector"), QJsonObject());
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));

    // Numbers, no content: no permission
    for (const QString& action : { QStringLiteral("statistics"), QStringLiteral("ink-coverage"), QStringLiteral("output-preview") })
    {
        const QJsonObject parameters = action == QLatin1String("statistics") ? QJsonObject() : QJsonObject({ { QStringLiteral("resolution"), 72 } });
        result = call(fixture, tab, action, parameters);
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(action + QStringLiteral(": ") + result->getFirstText()));
    }
}

void AppMcpReviewTest::lockedAndBusyDocument()
{
    // The catalog: the review tools work on a locked document (they only read it, Q7.2); a review tool reads the
    // snapshot of a document, on which another operation (a tool of the shell) runs, as document.get_info does
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFAppDocumentTab* tab = open(fixture, QStringLiteral("Cyan.pdf"), createCyanQuarter());
    QVERIFY(tab);

    tab->getDocument()->setLocked(true);
    std::optional<pdf::PDFMcpCallResult> result = call(fixture, tab, QStringLiteral("statistics"), QJsonObject());
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    tab->getDocument()->setLocked(false);

    QPointer<pdf::PDFAppDocumentOperation> operation = tab->getDocument()->beginOperation(pdf::PDFAppDocumentOperationKind::Tool, QStringLiteral("Running"), true);
    QVERIFY(operation);
    QVERIFY(tab->getDocument()->isBlockedByOperation());
    result = call(fixture, tab, QStringLiteral("statistics"), QJsonObject());
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QCOMPARE(content(*result, QStringLiteral("statistics")).value(QStringLiteral("data")).toObject().value(QStringLiteral("objectCount")).toInteger(), qint64(4));
    if (operation)
    {
        operation->finish();
    }
}

void AppMcpReviewTest::factsInCLocale()
{
    // P2S7MCP Low: the facts of a review tool are facts for an assistant - its numbers in the C locale, whatever the
    // default locale of the process is (German: "311,13" and "1.234" would be read wrong); the cyan quarter over a
    // coverage limit of 50 %: 311.13 mm² (inkCoverageAndOutputPreview)
    const QLocale defaultLocale;
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    const QScopeGuard restoreLocale([defaultLocale]() { QLocale::setDefault(defaultLocale); });

    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFAppDocumentTab* tab = open(fixture, QStringLiteral("Cyan.pdf"), createCyanQuarter());
    QVERIFY(tab);
    const std::optional<pdf::PDFMcpCallResult> result = call(fixture, tab, QStringLiteral("output-preview"), QJsonObject({ { QStringLiteral("resolution"), 100 }, { QStringLiteral("coverageLimit"), 50 } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const QJsonArray facts = content(*result, QStringLiteral("output-preview")).value(QStringLiteral("facts")).toArray();
    QVERIFY(!facts.isEmpty());

    const QRegularExpression localized(QStringLiteral("\\d,\\d"));
    const QRegularExpression decimal(QStringLiteral("\\d\\.\\d"));
    int decimals = 0;
    for (const QJsonValue& fact : facts)
    {
        const QString value = fact.toObject().value(QStringLiteral("value")).toString();
        QVERIFY2(!localized.match(value).hasMatch(), qPrintable(value));
        decimals += decimal.match(value).hasMatch() ? 1 : 0;
    }
    QVERIFY(decimals > 0);
}

void AppMcpReviewTest::hostileDocument()
{
    // A hostile document never ends the process (plan 6.1, 6.2, 8): /AA /O of the page (Table 198) starts a
    // cycle of two scripts by /Next, /AA /C a chain of 20 000 actions ending by a script (12.6.2: each action
    // of a chain once); a stream of 300 MiB of zeros (RunLength in Flate, a few kilobytes) against the limit
    // 256 MiB of the inspector (Q7.18); two objects referring to each other. The first script has a
    // right-to-left override and a control character, which the result never passes on.
    AppMcpFixture fixture;
    QVERIFY(start(fixture));

    OutputPreviewTestPdf pdf;
    const int first = pdf.reserve();
    const int second = pdf.reserve();
    pdf.set(first, "<< /S /JavaScript /JS <FEFF006F006E0065202E0001> /Next " + OutputPreviewTestPdf::ref(second) + " >>");
    pdf.set(second, "<< /S /JavaScript /JS (two) /Next " + OutputPreviewTestPdf::ref(first) + " >>");

    constexpr int CHAIN_LENGTH = 20000;
    std::vector<int> chain;
    for (int i = 0; i < CHAIN_LENGTH; ++i)
    {
        chain.push_back(pdf.reserve());
    }
    for (int i = 0; i + 1 < CHAIN_LENGTH; ++i)
    {
        pdf.set(chain[i], "<< /S /Named /N /NextPage /Next " + OutputPreviewTestPdf::ref(chain[i + 1]) + " >>");
    }
    pdf.set(chain.back(), "<< /S /JavaScript /JS (tail) >>");

    // RunLength (7.4.5): the length byte 129 copies the next byte 128 times; 128 is the end of the data
    QByteArray runs;
    const qsizetype pairCount = qsizetype(300) * 1024 * 1024 / 128;
    runs.reserve(pairCount * 2 + 1);
    for (qsizetype i = 0; i < pairCount; ++i)
    {
        runs.append('\x81');
        runs.append('\0');
    }
    runs.append('\x80');
    const int bomb = pdf.add(OutputPreviewTestPdf::stream(pdf::PDFFlateDecodeFilter::compress(runs), "/Filter [/FlateDecode /RunLengthDecode]"));
    runs.clear();

    const int cycleA = pdf.reserve();
    const int cycleB = pdf.reserve();
    pdf.set(cycleA, "<< /Self " + OutputPreviewTestPdf::ref(cycleA) + " /Other " + OutputPreviewTestPdf::ref(cycleB) + " >>");
    pdf.set(cycleB, "<< /Back " + OutputPreviewTestPdf::ref(cycleA) + " >>");

    pdf.addPage("0 0 0 1 k 0 0 10 10 re f", "<< >>", "/AA << /O " + OutputPreviewTestPdf::ref(first) + " /C " + OutputPreviewTestPdf::ref(chain.front()) + " >> /Bomb " +
                OutputPreviewTestPdf::ref(bomb) + " /Cycle " + OutputPreviewTestPdf::ref(cycleA));
    pdf::PDFAppDocumentTab* tab = open(fixture, QStringLiteral("Hostile.pdf"), pdf.write());
    QVERIFY(tab);

    // The scripts: each once, the controls of the first one removed
    std::optional<pdf::PDFMcpCallResult> result = call(fixture, tab, QStringLiteral("report"), QJsonObject({ { QStringLiteral("basicInformation"), false }, { QStringLiteral("metadata"), false },
                                                                                                             { QStringLiteral("namedDestinations"), false }, { QStringLiteral("pageBoxes"), false },
                                                                                                             { QStringLiteral("structureTree"), false }, { QStringLiteral("format"), QStringLiteral("text") } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText().left(500)));
    QStringList codes;
    for (const QJsonValue& script : content(*result, QStringLiteral("report")).value(QStringLiteral("data")).toObject().value(QStringLiteral("sections")).toObject()
                                        .value(QStringLiteral("javascript")).toObject().value(QStringLiteral("scripts")).toArray())
    {
        codes << script.toObject().value(QStringLiteral("code")).toString();
    }
    codes.sort();
    QCOMPARE(codes, QStringList({ QStringLiteral("one"), QStringLiteral("tail"), QStringLiteral("two") }));
    QVERIFY(!result->getFirstText().contains(QChar(0x202E)));
    QVERIFY(!result->getFirstText().contains(QChar(0x0001)));

    // The bomb: the limit is reached, nothing of it is returned
    result = call(fixture, tab, QStringLiteral("object-inspector"), QJsonObject({ { QStringLiteral("object"), QString::fromLatin1(OutputPreviewTestPdf::ref(bomb)) },
                                                                                 { QStringLiteral("decodeStream"), true } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QJsonObject object = content(*result, QStringLiteral("object-inspector")).value(QStringLiteral("data")).toObject().value(QStringLiteral("object")).toObject();
    QCOMPARE(object.value(QStringLiteral("decodeStatus")).toString(), QStringLiteral("limit-exceeded"));
    QVERIFY(!object.contains(QStringLiteral("content")));

    // The cycle at the largest depth: each object once (one visited set), a reference back has no object
    result = call(fixture, tab, QStringLiteral("object-inspector"), QJsonObject({ { QStringLiteral("object"), QString::fromLatin1(OutputPreviewTestPdf::ref(cycleA)) },
                                                                                 { QStringLiteral("depth"), 4 } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    object = content(*result, QStringLiteral("object-inspector")).value(QStringLiteral("data")).toObject().value(QStringLiteral("object")).toObject();
    QJsonObject self;
    QJsonObject other;
    for (const QJsonValue& entry : object.value(QStringLiteral("entries")).toArray())
    {
        const QString key = entry.toObject().value(QStringLiteral("key")).toString();
        (key == QLatin1String("Self") ? self : other) = entry.toObject().value(QStringLiteral("value")).toObject();
    }
    QCOMPARE(self.value(QStringLiteral("reference")).toString(), QString::fromLatin1(OutputPreviewTestPdf::ref(cycleA)));
    QVERIFY(!self.contains(QStringLiteral("object")));
    const QJsonObject back = other.value(QStringLiteral("object")).toObject().value(QStringLiteral("entries")).toArray().at(0).toObject().value(QStringLiteral("value")).toObject();
    QCOMPARE(back.value(QStringLiteral("reference")).toString(), QString::fromLatin1(OutputPreviewTestPdf::ref(cycleA)));
    QVERIFY(!back.contains(QStringLiteral("object")));

    // Every other tool answers; the XML has the bomb as its error and is cut at 1 MiB
    result = call(fixture, tab, QStringLiteral("statistics"), QJsonObject());
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QCOMPARE(content(*result, QStringLiteral("statistics")).value(QStringLiteral("data")).toObject().value(QStringLiteral("pageCount")).toInteger(), qint64(1));
    result = call(fixture, tab, QStringLiteral("ink-coverage"), QJsonObject({ { QStringLiteral("resolution"), 100 } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const QJsonArray ratio = content(*result, QStringLiteral("ink-coverage")).value(QStringLiteral("data")).toObject().value(QStringLiteral("pages")).toArray().at(0).toObject()
                                 .value(QStringLiteral("ratio")).toArray();
    QVERIFY(std::abs(ratio.at(3).toDouble() - 1.0) < 0.01);
    result = call(fixture, tab, QStringLiteral("output-preview"), QJsonObject({ { QStringLiteral("resolution"), 100 } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    result = call(fixture, tab, QStringLiteral("report"), QJsonObject({ { QStringLiteral("kind"), QStringLiteral("structure-xml") }, { QStringLiteral("exportStreams"), true } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText().left(500)));
    const QJsonObject xml = content(*result, QStringLiteral("report"));
    QVERIFY(xml.value(QStringLiteral("mimeType")).toString().contains(QStringLiteral("xml")));
    QCOMPARE(xml.value(QStringLiteral("truncated")).toBool(), xml.value(QStringLiteral("size")).toInteger() > 1024 * 1024);
    QVERIFY(result->getFirstText().contains(QStringLiteral("<?xml")));
    result = call(fixture, tab, QStringLiteral("soft-proof"), QJsonObject({ { QStringLiteral("resolution"), 72 } }));
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("output intent")), qPrintable(result->getFirstText()));
}

std::optional<pdf::PDFMcpCallResult> AppMcpReviewTest::compare(AppMcpFixture& fixture, pdf::PDFAppDocumentTab* original, pdf::PDFAppDocumentTab* revised, const QJsonObject& parameters,
                                                               const QJsonObject& common)
{
    QJsonObject arguments = common;
    arguments.insert(QStringLiteral("documentIds"), QJsonArray({ original->getDocument()->getId(), revised->getDocument()->getId() }));
    return AppMcpStep4::call(fixture, QStringLiteral("review"), QStringLiteral("compare"), parameters, arguments);
}

QJsonObject AppMcpReviewTest::measurement(int page, const QString& kind, const std::vector<QPointF>& points, const QJsonObject& scale)
{
    QJsonArray pointArray;
    for (const QPointF& point : points)
    {
        pointArray.append(QJsonArray({ point.x(), point.y() }));
    }
    QJsonObject object({ { QStringLiteral("page"), page }, { QStringLiteral("kind"), kind }, { QStringLiteral("points"), pointArray } });
    if (!scale.isEmpty())
    {
        object.insert(QStringLiteral("scale"), scale);
    }
    return object;
}

QJsonObject AppMcpReviewTest::scale(double real)
{
    return QJsonObject({ { QStringLiteral("paperValue"), 1 }, { QStringLiteral("paperUnit"), QStringLiteral("mm") }, { QStringLiteral("realValue"), real },
                         { QStringLiteral("realUnit"), QStringLiteral("mm") } });
}

std::vector<const pdf::PDFDictionary*> AppMcpReviewTest::annotations(const pdf::PDFDocument* document, size_t pageIndex, const QByteArray& subtype)
{
    std::vector<const pdf::PDFDictionary*> result;
    const pdf::PDFPage* page = document && pageIndex < document->getCatalog()->getPageCount() ? document->getCatalog()->getPage(pageIndex) : nullptr;
    for (const pdf::PDFObjectReference reference : page ? page->getAnnotations() : std::vector<pdf::PDFObjectReference>())
    {
        const pdf::PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(reference));
        if (dictionary && document->getObject(dictionary->get("Subtype")).getString() == subtype)
        {
            result.push_back(dictionary);
        }
    }
    return result;
}

pdf::PDFAppDocument* AppMcpReviewTest::findDocument(AppMcpFixture& fixture, const QString& documentId)
{
    for (pdf::PDFAppDocument* document : fixture.context->getDocumentRegistry()->getDocuments())
    {
        if (document && document->getId() == documentId)
        {
            return document;
        }
    }
    return nullptr;
}

std::vector<double> AppMcpReviewTest::values(const QJsonObject& data)
{
    std::vector<double> result;
    for (const QJsonValue& row : data.value(QStringLiteral("measurements")).toArray())
    {
        result.push_back(row.toObject().value(QStringLiteral("value")).toDouble());
    }
    return result;
}

void AppMcpReviewTest::compareSummary()
{
    // Plan 4.9, 7.1: two open documents by documentIds - the original first. The pair of the core tests (page 1:
    // a rectangle removed and one added, page 2: the word "AAAA" replaced by "BBBB" and the shapes of its 4 + 4
    // Type 3 glyphs, ISO 32000-2 9.6.4): 11 differences, the counts complete also when a filter hides some
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFAppDocumentTab* original = open(fixture, QStringLiteral("Report_v1.pdf"), pdftest::AppMcpReviewDocuments::createOriginal().write());
    pdf::PDFAppDocumentTab* revised = open(fixture, QStringLiteral("Report_v2.pdf"), pdftest::AppMcpReviewDocuments::createRevised().write());
    QVERIFY(original && revised);
    const quint64 originalVersion = original->getDocument()->getVersion();
    const quint64 revisedVersion = revised->getDocument()->getVersion();

    // Exactly two documents (the inputs {2, 2} of the catalog), never documentId
    std::optional<pdf::PDFMcpCallResult> result = call(fixture, original, QStringLiteral("compare"), QJsonObject());
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().startsWith(QStringLiteral("documentId: Compare works with more documents")), qPrintable(result->getFirstText()));
    for (const QJsonArray& ids : { QJsonArray({ original->getDocument()->getId() }),
                                   QJsonArray({ original->getDocument()->getId(), revised->getDocument()->getId(), original->getDocument()->getId() }) })
    {
        result = AppMcpStep4::call(fixture, QStringLiteral("review"), QStringLiteral("compare"), QJsonObject(), QJsonObject({ { QStringLiteral("documentIds"), ids } }));
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("exactly 2 open documents")), qPrintable(result->getFirstText()));
    }

    result = compare(fixture, original, revised, QJsonObject({ { QStringLiteral("includeVectorGraphics"), false } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const QJsonObject answer = AppMcpReviewTest::content(*result, QStringLiteral("compare"));
    QCOMPARE(answer.value(QStringLiteral("origin")).toString(), QStringLiteral("document"));
    QVERIFY(result->getFirstText().startsWith(pdf::PDFAppMcpToolSet::getUntrustedContentLine() + QLatin1Char('\n')));
    QVERIFY(answer.value(QStringLiteral("documents")).toArray().isEmpty());
    QVERIFY(!answer.contains(QStringLiteral("outputs")));

    const QJsonObject data = answer.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("isSame")).toBool(), false);
    const QJsonObject counts = data.value(QStringLiteral("counts")).toObject();
    QCOMPARE(counts.value(QStringLiteral("total")).toInteger(), qint64(11));
    QCOMPARE(counts.value(QStringLiteral("shown")).toInteger(), qint64(1));
    QCOMPARE(counts.value(QStringLiteral("hidden")).toInteger(), qint64(10));
    QCOMPARE(counts.value(QStringLiteral("text")).toInteger(), qint64(1));
    QCOMPARE(counts.value(QStringLiteral("vectorGraphics")).toInteger(), qint64(10));
    QCOMPARE(counts.value(QStringLiteral("replaced")).toInteger(), qint64(1));
    QCOMPARE(counts.value(QStringLiteral("removed")).toInteger(), qint64(5));
    QCOMPARE(counts.value(QStringLiteral("added")).toInteger(), qint64(5));
    const QJsonArray differences = data.value(QStringLiteral("differences")).toArray();
    QCOMPARE(differences.size(), qsizetype(1));
    const QJsonObject difference = differences.first().toObject();
    QCOMPARE(difference.value(QStringLiteral("kind")).toString(), QStringLiteral("text-replaced"));
    QCOMPARE(difference.value(QStringLiteral("originalPage")).toInteger(), qint64(2));
    QCOMPARE(difference.value(QStringLiteral("revisedPage")).toInteger(), qint64(2));
    QCOMPARE(difference.value(QStringLiteral("originalText")).toString(), QStringLiteral("AAAA"));
    QCOMPARE(difference.value(QStringLiteral("revisedText")).toString(), QStringLiteral("BBBB"));
    QCOMPARE(difference.value(QStringLiteral("originalRectPages")).toArray().first().toInteger(), qint64(2));
    QCOMPARE(data.value(QStringLiteral("isTextOmitted")).toBool(), false);
    QCOMPARE(data.value(QStringLiteral("truncated")).toBool(), false);
    QCOMPARE(data.value(QStringLiteral("pageSequence")).toArray().size(), qsizetype(2));

    // The same document twice: no difference; the documents never change
    result = compare(fixture, original, original, QJsonObject());
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QCOMPARE(AppMcpReviewTest::content(*result, QStringLiteral("compare")).value(QStringLiteral("data")).toObject().value(QStringLiteral("isSame")).toBool(), true);
    QCOMPARE(original->getDocument()->getVersion(), originalVersion);
    QCOMPARE(revised->getDocument()->getVersion(), revisedVersion);

    // A locked document is compared too (it is only read)
    revised->getDocument()->setLocked(true);
    result = compare(fixture, original, revised, QJsonObject());
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QCOMPARE(AppMcpReviewTest::content(*result, QStringLiteral("compare")).value(QStringLiteral("data")).toObject().value(QStringLiteral("counts")).toObject().value(QStringLiteral("total")).toInteger(), qint64(11));
    revised->getDocument()->setLocked(false);

    // The summary is data: neither a file nor a new tab
    result = compare(fixture, original, revised, QJsonObject(), QJsonObject({ { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), fixture.path(QStringLiteral("Summary.xml")) } }));
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().startsWith(QStringLiteral("result: save_file writes the XML of the differences")), qPrintable(result->getFirstText()));
    QVERIFY(!QFileInfo::exists(fixture.path(QStringLiteral("Summary.xml"))));
    result = compare(fixture, original, revised, QJsonObject(), QJsonObject({ { QStringLiteral("result"), QStringLiteral("open_new_tab") } }));
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().startsWith(QStringLiteral("result: open_new_tab opens the report PDF")), qPrintable(result->getFirstText()));
}

void AppMcpReviewTest::compareXmlAndReport()
{
    // Plan 4.9, 7.1, R7-13: the XML of the differences by return_text and save_file; the report PDF is a new document -
    // open_new_tab (its default) opens it in a new tab, save_file writes it (.pdf), return_text is no result of it.
    // The report (core: PDFDiffReportBuilder) has a /Polygon in the color of its kind over every shown difference
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFAppDocumentTab* original = open(fixture, QStringLiteral("Report_v1.pdf"), pdftest::AppMcpReviewDocuments::createOriginal().write());
    pdf::PDFAppDocumentTab* revised = open(fixture, QStringLiteral("Report_v2.pdf"), pdftest::AppMcpReviewDocuments::createRevised().write());
    QVERIFY(original && revised);

    const QJsonObject xml({ { QStringLiteral("output"), QStringLiteral("xml") } });
    std::optional<pdf::PDFMcpCallResult> result = compare(fixture, original, revised, xml);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QJsonObject answer = AppMcpReviewTest::content(*result, QStringLiteral("compare"));
    QVERIFY2(answer.value(QStringLiteral("mimeType")).toString().contains(QStringLiteral("xml")), qPrintable(answer.value(QStringLiteral("mimeType")).toString()));
    QCOMPARE(answer.value(QStringLiteral("truncated")).toBool(), false);
    const QString text = result->getFirstText();
    QVERIFY(text.startsWith(pdf::PDFAppMcpToolSet::getUntrustedContentLine()));
    QVERIFY(text.contains(QStringLiteral("<?xml")));
    QVERIFY(text.contains(QStringLiteral("AAAA")) && text.contains(QStringLiteral("BBBB")));

    // save_file: the file of the XML, its extension only
    result = compare(fixture, original, revised, xml, QJsonObject({ { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), fixture.path(QStringLiteral("Differences.txt")) } }));
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("must have the extension xml")), qPrintable(result->getFirstText()));
    const QString xmlPath = fixture.path(QStringLiteral("Differences.xml"));
    result = compare(fixture, original, revised, xml, QJsonObject({ { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), xmlPath } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QVERIFY(AppMcpStep4::readFile(xmlPath).contains("<text-removed>AAAA</text-removed>"));

    // The report: return_text is refused before the run
    const QJsonObject reportOutput({ { QStringLiteral("output"), QStringLiteral("report") } });
    result = compare(fixture, original, revised, reportOutput, QJsonObject({ { QStringLiteral("result"), QStringLiteral("return_text") } }));
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().startsWith(QStringLiteral("result: output report makes a new PDF document")), qPrintable(result->getFirstText()));

    // Without a result it opens in a new tab: side by side, original 1, revised 1, original 2, revised 2; the
    // removed rectangle of page 1 in the color diff/removed (#FF3232), the added one in diff/added (#7DFA00)
    const size_t documentCount = fixture.context->getDocumentRegistry()->getDocuments().size();
    result = compare(fixture, original, revised, reportOutput);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    answer = AppMcpReviewTest::content(*result, QStringLiteral("compare"));
    QCOMPARE(answer.value(QStringLiteral("origin")).toString(), QStringLiteral("document"));
    QVERIFY(result->getFirstText().startsWith(pdf::PDFAppMcpToolSet::getUntrustedContentLine() + QLatin1Char('\n')));
    QCOMPARE(answer.value(QStringLiteral("data")).toObject().value(QStringLiteral("counts")).toObject().value(QStringLiteral("total")).toInteger(), qint64(11));
    QCOMPARE(fixture.context->getDocumentRegistry()->getDocuments().size(), documentCount + 1);
    const QString reportId = answer.value(QStringLiteral("documentId")).toString();
    QVERIFY(reportId != original->getDocument()->getId() && reportId != revised->getDocument()->getId());
    pdf::PDFAppDocument* reportDocument = findDocument(fixture, reportId);
    QVERIFY(reportDocument);
    QTRY_VERIFY(reportDocument->isReady());
    const pdf::PDFDocument* reportPdf = reportDocument->getDocument().data();
    QVERIFY(reportPdf);
    QCOMPARE(reportPdf->getCatalog()->getPageCount(), size_t(4));
    const std::vector<const pdf::PDFDictionary*> removed = annotations(reportPdf, 0, "Polygon");
    const std::vector<const pdf::PDFDictionary*> added = annotations(reportPdf, 1, "Polygon");
    QCOMPARE(removed.size(), size_t(1));
    QCOMPARE(added.size(), size_t(1));
    pdf::PDFDocumentDataLoaderDecorator loader(reportPdf);
    const auto colorName = [&loader](const pdf::PDFDictionary* annotation)
    {
        const std::vector<pdf::PDFReal> color = loader.readNumberArrayFromDictionary(annotation, "IC");
        return color.size() == 3 ? QColor::fromRgbF(float(color[0]), float(color[1]), float(color[2])).name() : QString();
    };
    QCOMPARE(colorName(removed.front()), QStringLiteral("#ff3232"));
    QCOMPARE(colorName(added.front()), QStringLiteral("#7dfa00"));
    QVERIFY(qAbs(loader.readNumberFromDictionary(removed.front(), "CA", 1.0) - 0.3) < 1e-6);

    // save_file: the report of the view Revised (two pages, the added rectangle on page 1), a .pdf file
    result = compare(fixture, original, revised, QJsonObject({ { QStringLiteral("output"), QStringLiteral("report") }, { QStringLiteral("view"), QStringLiteral("revised") } }),
                     QJsonObject({ { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), fixture.path(QStringLiteral("Report.xml")) } }));
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("extension pdf")), qPrintable(result->getFirstText()));
    const QString reportPath = fixture.path(QStringLiteral("Report.pdf"));
    result = compare(fixture, original, revised, QJsonObject({ { QStringLiteral("output"), QStringLiteral("report") }, { QStringLiteral("view"), QStringLiteral("revised") } }),
                     QJsonObject({ { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), reportPath } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QCOMPARE(QDir::cleanPath(AppMcpReviewTest::content(*result, QStringLiteral("compare")).value(QStringLiteral("path")).toString()), QDir::cleanPath(reportPath));
    const pdf::PDFDocument written = mergetest::MergeTestPdf::readBytes(AppMcpStep4::readFile(reportPath));
    QCOMPARE(written.getCatalog()->getPageCount(), size_t(2));
    QCOMPARE(annotations(&written, 0, "Polygon").size(), size_t(1));
    QCOMPARE(fixture.context->getDocumentRegistry()->getDocuments().size(), documentCount + 1);
}

void AppMcpReviewTest::compareContentPermission()
{
    // Q7.2, ISO 32000-2 Table 22: the texts of the differences are the content (bit 5) - an input without it
    // leaves them out of the summary, the XML is refused before the run; the report is a new unencrypted document
    // of the inputs, the rule of Merge: an input with any restriction (here only bit 4, changes) is refused, also
    // when its content may be copied. The refusal names the input and what works without the permission.
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFAppDocumentTab* original = open(fixture, QStringLiteral("Report_v1.pdf"), pdftest::AppMcpReviewDocuments::createOriginal().write());
    pdf::PDFAppDocumentTab* noCopy = open(fixture, QStringLiteral("NoCopy.pdf"), pdftest::AppMcpReviewDocuments::encrypt(pdftest::AppMcpReviewDocuments::createRevised(), AppMcpStep4::allowedWithout(5)));
    pdf::PDFAppDocumentTab* noChanges = open(fixture, QStringLiteral("NoChanges.pdf"), pdftest::AppMcpReviewDocuments::encrypt(pdftest::AppMcpReviewDocuments::createRevised(), AppMcpStep4::allowedWithout(4)));
    QVERIFY(original && noCopy && noChanges);

    std::optional<pdf::PDFMcpCallResult> result = compare(fixture, original, noCopy, QJsonObject());
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const QJsonObject data = AppMcpReviewTest::content(*result, QStringLiteral("compare")).value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("isTextOmitted")).toBool(), true);
    QCOMPARE(data.value(QStringLiteral("counts")).toObject().value(QStringLiteral("total")).toInteger(), qint64(11));
    for (const QJsonValue& difference : data.value(QStringLiteral("differences")).toArray())
    {
        QVERIFY(!difference.toObject().contains(QStringLiteral("originalText")));
        QVERIFY(!difference.toObject().contains(QStringLiteral("revisedText")));
    }
    QVERIFY(!result->getFirstText().contains(QStringLiteral("AAAA")));

    const auto isRefused = [](const std::optional<pdf::PDFMcpCallResult>& refusal, const QString& part)
    {
        return refusal && refusal->isError && refusal->getFirstText().startsWith(QStringLiteral("documentIds[1]: ")) && refusal->getFirstText().contains(QStringLiteral("Table 22")) &&
               refusal->getFirstText().contains(QStringLiteral("review.compare works with output summary")) && refusal->getFirstText().contains(part);
    };
    const QString xmlPath = fixture.path(QStringLiteral("NoCopy.xml"));
    result = compare(fixture, original, noCopy, QJsonObject({ { QStringLiteral("output"), QStringLiteral("xml") } }),
                     QJsonObject({ { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), xmlPath } }));
    QVERIFY2(isRefused(result, QStringLiteral("copying text and images")), qPrintable(result ? result->getFirstText() : QString()));
    QVERIFY(!QFileInfo::exists(xmlPath));

    // Bit 4 only: the XML is allowed, the report is not (its restriction would be lost)
    result = compare(fixture, original, noChanges, QJsonObject({ { QStringLiteral("output"), QStringLiteral("xml") } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const size_t documentCount = fixture.context->getDocumentRegistry()->getDocuments().size();
    result = compare(fixture, original, noChanges, QJsonObject({ { QStringLiteral("output"), QStringLiteral("report") } }));
    QVERIFY2(isRefused(result, QStringLiteral("is not encrypted")), qPrintable(result ? result->getFirstText() : QString()));
    QCOMPARE(fixture.context->getDocumentRegistry()->getDocuments().size(), documentCount);
}

void AppMcpReviewTest::compareLimits()
{
    // Plan 4.9, 7.1: at most 1 000 differences in the data - 1 000 all of them, 1 001 the first 1 000 and truncated
    // (the counts complete); the ranges of the pages are checked before the run (B174): a page outside of a document
    // and a huge range are errors, quickly
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFAppDocumentTab* base = open(fixture, QStringLiteral("Base.pdf"), pdftest::AppMcpReviewDocuments::createSquares(0).write());
    pdf::PDFAppDocumentTab* thousand = open(fixture, QStringLiteral("Thousand.pdf"), pdftest::AppMcpReviewDocuments::createSquares(pdf::PDFDocumentToolCompare::MAXIMUM_DIFFERENCES).write());
    pdf::PDFAppDocumentTab* more = open(fixture, QStringLiteral("More.pdf"), pdftest::AppMcpReviewDocuments::createSquares(pdf::PDFDocumentToolCompare::MAXIMUM_DIFFERENCES + 1).write());
    QVERIFY(base && thousand && more);

    for (const auto& [tab, count] : { std::pair<pdf::PDFAppDocumentTab*, qint64>(thousand, pdf::PDFDocumentToolCompare::MAXIMUM_DIFFERENCES),
                                      std::pair<pdf::PDFAppDocumentTab*, qint64>(more, pdf::PDFDocumentToolCompare::MAXIMUM_DIFFERENCES + 1) })
    {
        const std::optional<pdf::PDFMcpCallResult> result = compare(fixture, base, tab, QJsonObject());
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText().left(500)));
        const QJsonObject data = AppMcpReviewTest::content(*result, QStringLiteral("compare")).value(QStringLiteral("data")).toObject();
        QCOMPARE(data.value(QStringLiteral("counts")).toObject().value(QStringLiteral("total")).toInteger(), count);
        QCOMPARE(data.value(QStringLiteral("counts")).toObject().value(QStringLiteral("added")).toInteger(), count);
        QCOMPARE(data.value(QStringLiteral("differences")).toArray().size(), qsizetype(pdf::PDFDocumentToolCompare::MAXIMUM_DIFFERENCES));
        QCOMPARE(data.value(QStringLiteral("truncated")).toBool(), count > pdf::PDFDocumentToolCompare::MAXIMUM_DIFFERENCES);
    }

    QElapsedTimer timer;
    timer.start();
    for (const QString& pages : { QStringLiteral("2"), QStringLiteral("1-2147483647") })
    {
        const std::optional<pdf::PDFMcpCallResult> result = compare(fixture, base, more, QJsonObject({ { QStringLiteral("revisedPages"), QJsonObject({ { QStringLiteral("pages"), pages } }) } }));
        QVERIFY(result);
        QVERIFY2(result->isError, qPrintable(pages));
        QVERIFY(!result->getFirstText().contains(QStringLiteral("differences")));
    }
    QVERIFY(timer.elapsed() < 10000 * pdftest::AppDocumentFixture::getTimeoutFactor());
}

void AppMcpReviewTest::compareHostileDocuments()
{
    // Plan 6, 6.2: two hostile documents never end the process. The first: a broken content stream (unbalanced q and
    // BT, a string not closed, operators without operands) and a page tree, whose /Count claims a billion pages
    // (7.7.3.2: the kids are the pages); the second: a form drawing itself (a cycle of the content, 8.10) used by two
    // pages (a node shared by two paths). Each comparison answers; the server still answers afterwards.
    AppMcpFixture fixture;
    QVERIFY(start(fixture));

    mergetest::MergeTestPdf broken = pdftest::AppMcpReviewDocuments::createTextPdf({ "q q BT /F1 10 Tf (unclosed Tj re f l m 0 0 1 rg 10 10 50 50 re f Q Q Q ET", "1 0 0 rg 20 20 40 40 re f" });
    broken.set(2, "<< /Type /Pages /Kids [10 0 R 12 0 R] /Count 1000000000 >>");
    mergetest::MergeTestPdf cycle = pdftest::AppMcpReviewDocuments::createTextPdf({ pdftest::AppMcpReviewDocuments::rect(100, 100, 50, 50) + "/Fm1 Do", "/Fm1 Do" });
    cycle.set(10, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << /XObject << /Fm1 50 0 R >> >> /Contents 11 0 R >>");
    cycle.set(12, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << /XObject << /Fm1 50 0 R >> >> /Contents 13 0 R >>");
    cycle.set(50, mergetest::MergeTestPdf::stream("0 0 1 rg 5 5 5 5 re f /Fm1 Do", "/Type /XObject /Subtype /Form /BBox [0 0 600 800] /Resources << /XObject << /Fm1 50 0 R >> >>"));

    pdf::PDFAppDocumentTab* first = open(fixture, QStringLiteral("Broken.pdf"), broken.write());
    pdf::PDFAppDocumentTab* second = open(fixture, QStringLiteral("Cycle.pdf"), cycle.write());
    QVERIFY(first && second);
    QCOMPARE(first->getDocument()->getDocument()->getCatalog()->getPageCount(), size_t(2));

    for (const QString& output : { QStringLiteral("summary"), QStringLiteral("xml"), QStringLiteral("report") })
    {
        QJsonObject common;
        if (output == QLatin1String("report"))
        {
            common.insert(QStringLiteral("result"), QStringLiteral("save_file"));
            common.insert(QStringLiteral("path"), fixture.path(QStringLiteral("Hostile.pdf")));
        }
        const std::optional<pdf::PDFMcpCallResult> result = compare(fixture, first, second, QJsonObject({ { QStringLiteral("output"), output } }), common);
        QVERIFY2(result, qPrintable(output));
        QVERIFY2(!result->isError, qPrintable(output + QStringLiteral(": ") + result->getFirstText().left(500)));
        QCOMPARE(AppMcpReviewTest::content(*result, QStringLiteral("compare")).value(QStringLiteral("data")).toObject().value(QStringLiteral("originalPageCount")).toInteger(), qint64(2));
    }

    // The server answers: the same hostile document twice has no difference
    const std::optional<pdf::PDFMcpCallResult> result = compare(fixture, second, second, QJsonObject());
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QCOMPARE(AppMcpReviewTest::content(*result, QStringLiteral("compare")).value(QStringLiteral("data")).toObject().value(QStringLiteral("isSame")).toBool(), true);
}

void AppMcpReviewTest::measureList()
{
    // ISO 32000-2, 12.9.2: a measurement, whose first point is in a viewport of the page with a measure, takes its
    // scale - 72 pt = 25.4 mm on the paper, 1:100 = 2540 mm (Table 268 /C); else the scale of the measurement (1:50:
    // 1270 mm, (25.4 × 50 / 1000)² = 1.6129 sq. m), else 1:1 (a perimeter of 4 × 72 pt = 101.6 mm); an angle of
    // perpendicular arms is 90°. The measurements may lie on different pages (FM-9 cancelled), the rows by pages.
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFAppDocumentTab* tab = open(fixture, QStringLiteral("Plan.pdf"), pdftest::AppMcpReviewDocuments::createMeasurePages().write());
    QVERIFY(tab);

    const QJsonArray items({ measurement(2, QStringLiteral("distance"), { QPointF(0, 0), QPointF(72, 0) }, scale(50)),
                             measurement(2, QStringLiteral("area"), { QPointF(100, 100), QPointF(172, 100), QPointF(172, 172), QPointF(100, 172) }, scale(50)),
                             measurement(1, QStringLiteral("distance"), { QPointF(50, 50), QPointF(122, 50) }),
                             measurement(2, QStringLiteral("angle"), { QPointF(172, 400), QPointF(100, 400), QPointF(100, 472) }),
                             measurement(2, QStringLiteral("perimeter"), { QPointF(300, 300), QPointF(372, 300), QPointF(372, 372), QPointF(300, 372) }) });
    const QJsonObject parameters({ { QStringLiteral("measurements"), measurements(items) }, { QStringLiteral("lengthUnit"), QStringLiteral("mm") } });
    std::optional<pdf::PDFMcpCallResult> result = call(fixture, tab, QStringLiteral("measure"), parameters);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QJsonObject answer = AppMcpReviewTest::content(*result, QStringLiteral("measure"));
    QCOMPARE(answer.value(QStringLiteral("origin")).toString(), QStringLiteral("document"));
    const QJsonObject data = answer.value(QStringLiteral("data")).toObject();
    const std::vector<double> listed = values(data);
    const std::vector<double> expected({ 2540.0, 1270.0, 1.6129, 90.0, 101.6 });
    QCOMPARE(listed.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        QVERIFY2(qAbs(listed[i] - expected[i]) < 1e-6 * qMax(1.0, expected[i]), qPrintable(QStringLiteral("%1: %2").arg(i).arg(listed[i])));
    }
    const QJsonArray rows = data.value(QStringLiteral("measurements")).toArray();
    QCOMPARE(rows.at(0).toObject().value(QStringLiteral("page")).toInteger(), qint64(1));
    QCOMPARE(rows.at(0).toObject().value(QStringLiteral("viewport")).toString(), QStringLiteral("Plan"));
    QCOMPARE(rows.at(0).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("2540.00 mm"));
    QCOMPARE(rows.at(1).toObject().value(QStringLiteral("unit")).toString(), QStringLiteral("mm"));
    QCOMPARE(rows.at(2).toObject().value(QStringLiteral("unit")).toString(), QStringLiteral("sqm"));
    QCOMPARE(rows.at(3).toObject().value(QStringLiteral("unit")).toString(), QStringLiteral("deg"));
    QCOMPARE(rows.at(1).toObject().value(QStringLiteral("source")).toString(), QStringLiteral("new"));
    const QJsonArray viewports = data.value(QStringLiteral("viewports")).toArray();
    QCOMPARE(viewports.size(), qsizetype(1));
    QCOMPARE(viewports.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Plan"));
    QCOMPARE(viewports.at(0).toObject().value(QStringLiteral("subtype")).toString(), QStringLiteral("RL"));

    // The pages of the viewports: page 2 has none
    result = call(fixture, tab, QStringLiteral("measure"), QJsonObject({ { QStringLiteral("pages"), QJsonObject({ { QStringLiteral("pages"), QStringLiteral("2") } }) } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QVERIFY(AppMcpReviewTest::content(*result, QStringLiteral("measure")).value(QStringLiteral("data")).toObject().value(QStringLiteral("viewports")).toArray().isEmpty());

    // Q7.20: the CSV of six columns, separated by semicolons, a decimal point
    QJsonObject csvParameters = parameters;
    csvParameters.insert(QStringLiteral("mode"), QStringLiteral("csv"));
    result = call(fixture, tab, QStringLiteral("measure"), csvParameters);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    answer = AppMcpReviewTest::content(*result, QStringLiteral("measure"));
    QVERIFY2(answer.value(QStringLiteral("mimeType")).toString().startsWith(QStringLiteral("text/csv")), qPrintable(answer.value(QStringLiteral("mimeType")).toString()));
    QVERIFY(result->getFirstText().startsWith(pdf::PDFAppMcpToolSet::getUntrustedContentLine() + QLatin1Char('\n')));
    QStringList lines = result->getFirstText().section(QLatin1Char('\n'), 1).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString& line : lines)
    {
        line.remove(QLatin1Char('\r'));
    }
    QVERIFY2(lines.size() == 6, qPrintable(result->getFirstText()));
    QCOMPARE(lines.at(0), QStringLiteral("Page;Type;Value;Unit;Text;Scale"));
    QVERIFY2(lines.at(1).startsWith(QStringLiteral("1;Length;2540.0000;mm;2540.00 mm;")), qPrintable(lines.at(1)));

    const QString csvPath = fixture.path(QStringLiteral("Plan.csv"));
    result = call(fixture, tab, QStringLiteral("measure"), csvParameters, QJsonObject({ { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), csvPath } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QVERIFY(AppMcpStep4::readFile(csvPath).startsWith("Page;Type;Value;Unit;Text;Scale\r\n"));

    // The data of list is no file, and no change of the document
    result = call(fixture, tab, QStringLiteral("measure"), parameters, QJsonObject({ { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), fixture.path(QStringLiteral("List.csv")) } }));
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().startsWith(QStringLiteral("result: save_file writes the CSV")), qPrintable(result->getFirstText()));
    result = call(fixture, tab, QStringLiteral("measure"), parameters, QJsonObject({ { QStringLiteral("result"), QStringLiteral("keep") } }));
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().startsWith(QStringLiteral("result: keep, save_copy and overwrite take the annotations of mode add")), qPrintable(result->getFirstText()));
    QVERIFY(fixture.confirmations()->shown.empty());
}

void AppMcpReviewTest::measureAddConfirmed()
{
    // Plan 7.1, Q7.5: the annotations of mode add are always confirmed, also with every permission Allow, with their
    // ghosts on their pages (a distance and an angle by their points, a perimeter by its closed polyline, an area by
    // its polygon, in the color of their lines) and a row per page; declined, nothing changes. Allowed: one step of
    // Undo, the annotations of 12.5.6.9 / Table 181 on two pages - a distance /Line /IT /LineDimension, an area
    // /Polygon /IT /PolygonDimension, a perimeter /PolyLine /IT /PolyLineDimension, an angle /PolyLine without /IT -
    // each with /Measure /Subtype /RL (Table 266); listed afterwards as annotations with their values.
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFAppDocumentTab* tab = open(fixture, QStringLiteral("Plan.pdf"), mergetest::MergeTestPdf::createPages("M", 2).write());
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const quint64 version = document->getVersion();
    const int undoCount = pdftest::AppDocumentFixture::undoCount(document);

    const QJsonArray items({ measurement(1, QStringLiteral("distance"), { QPointF(100, 100), QPointF(172, 100) }, scale(10)),
                             measurement(2, QStringLiteral("area"), { QPointF(100, 100), QPointF(172, 100), QPointF(172, 172), QPointF(100, 172) }),
                             measurement(2, QStringLiteral("perimeter"), { QPointF(300, 300), QPointF(372, 300), QPointF(300, 372) }),
                             measurement(1, QStringLiteral("angle"), { QPointF(172, 400), QPointF(100, 400), QPointF(100, 472) }) });
    const QJsonObject parameters({ { QStringLiteral("mode"), QStringLiteral("add") }, { QStringLiteral("measurements"), measurements(items) },
                                   { QStringLiteral("lengthUnit"), QStringLiteral("mm") }, { QStringLiteral("textColor"), QStringLiteral("#1f3a93") } });

    fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::DeniedUser);
    std::optional<pdf::PDFMcpCallResult> result = call(fixture, tab, QStringLiteral("measure"), parameters);
    QVERIFY(result && result->isError);
    QCOMPARE(int(fixture.confirmations()->shown.size()), 1);
    std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmations()->shown.back();
    QVERIFY(confirmation->isAlwaysConfirmed);
    QCOMPARE(confirmation->kind, pdf::PDFAppMcpConfirmationKind::Change);
    QCOMPARE(document->getVersion(), version);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoCount);
    QVERIFY(annotations(document->getDocument().data(), 0, "Line").empty());

    const QJsonArray ghosts = confirmation->details.value(QStringLiteral("ghosts")).toArray();
    QCOMPARE(ghosts.size(), qsizetype(4));
    std::vector<pdf::PDFAppMcpGhost> parsed;
    for (const QJsonValue& value : ghosts)
    {
        const std::optional<pdf::PDFAppMcpGhost> ghost = pdf::PDFAppMcpGhost::fromJson(value.toObject());
        QVERIFY(ghost);
        QCOMPARE(ghost->color, QColor(QStringLiteral("#1f3a93")));
        parsed.push_back(*ghost);
    }
    QCOMPARE(parsed[0].shape, pdf::PDFAppMcpGhost::Shape::Polyline);
    QCOMPARE(parsed[0].pageIndex, pdf::PDFInteger(0));
    QCOMPARE(parsed[0].points, QPolygonF({ QPointF(100, 100), QPointF(172, 100) }));
    QCOMPARE(parsed[1].shape, pdf::PDFAppMcpGhost::Shape::Polygon);
    QCOMPARE(parsed[1].pageIndex, pdf::PDFInteger(1));
    QCOMPARE(parsed[1].getBoundingRect(), QRectF(100, 100, 72, 72));
    QCOMPARE(parsed[2].shape, pdf::PDFAppMcpGhost::Shape::Polyline);
    QCOMPARE(parsed[2].points, QPolygonF({ QPointF(300, 300), QPointF(372, 300), QPointF(300, 372), QPointF(300, 300) }));
    QCOMPARE(parsed[3].shape, pdf::PDFAppMcpGhost::Shape::Polyline);
    QCOMPARE(parsed[3].pageIndex, pdf::PDFInteger(0));
    QCOMPARE(parsed[3].points, QPolygonF({ QPointF(172, 400), QPointF(100, 400), QPointF(100, 472) }));

    const QJsonArray rows = confirmation->details.value(QStringLiteral("rows")).toArray();
    QCOMPARE(rows.size(), qsizetype(2));
    const pdf::PDFAppMcpChangeRow firstRow = pdf::PDFAppMcpChangeRow::fromJson(rows.at(0).toObject());
    QCOMPARE(firstRow.page, QStringLiteral("Page 1"));
    QCOMPARE(firstRow.text, QStringLiteral("Add 2 measurements"));
    QVERIFY2(firstRow.detail.contains(QStringLiteral("254.00 mm")), qPrintable(firstRow.detail));
    QCOMPARE(pdf::PDFAppMcpChangeRow::fromJson(rows.at(1).toObject()).page, QStringLiteral("Page 2"));

    // A copy is confirmed with the ghosts too (Show on Page); declined, nothing is written
    const QString copyPath = fixture.path(QStringLiteral("Plan_measured.pdf"));
    result = call(fixture, tab, QStringLiteral("measure"), parameters, QJsonObject({ { QStringLiteral("result"), QStringLiteral("save_copy") }, { QStringLiteral("path"), copyPath } }));
    QVERIFY(result && result->isError);
    QCOMPARE(int(fixture.confirmations()->shown.size()), 2);
    QCOMPARE(fixture.confirmations()->shown.back()->kind, pdf::PDFAppMcpConfirmationKind::SaveCopy);
    QVERIFY(fixture.confirmations()->shown.back()->isAlwaysConfirmed);
    QCOMPARE(fixture.confirmations()->shown.back()->details.value(QStringLiteral("ghosts")).toArray().size(), qsizetype(4));
    QVERIFY(!QFileInfo::exists(copyPath));
    QCOMPARE(document->getVersion(), version);

    // Allowed: the annotations as one step of Undo
    fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::Allowed);
    result = call(fixture, tab, QStringLiteral("measure"), parameters);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QCOMPARE(int(fixture.confirmations()->shown.size()), 3);
    QCOMPARE(AppMcpReviewTest::content(*result, QStringLiteral("measure")).value(QStringLiteral("data")).toObject().value(QStringLiteral("added")).toInt(), 4);
    QVERIFY(document->getVersion() != version);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoCount + 1);

    const pdf::PDFDocument* changed = document->getDocument().data();
    const std::vector<const pdf::PDFDictionary*> lines = annotations(changed, 0, "Line");
    const std::vector<const pdf::PDFDictionary*> angles = annotations(changed, 0, "PolyLine");
    const std::vector<const pdf::PDFDictionary*> areas = annotations(changed, 1, "Polygon");
    const std::vector<const pdf::PDFDictionary*> perimeters = annotations(changed, 1, "PolyLine");
    QCOMPARE(lines.size(), size_t(1));
    QCOMPARE(angles.size(), size_t(1));
    QCOMPARE(areas.size(), size_t(1));
    QCOMPARE(perimeters.size(), size_t(1));
    QCOMPARE(changed->getObject(lines.front()->get("IT")).getString(), QByteArray("LineDimension"));
    QCOMPARE(changed->getObject(areas.front()->get("IT")).getString(), QByteArray("PolygonDimension"));
    QCOMPARE(changed->getObject(perimeters.front()->get("IT")).getString(), QByteArray("PolyLineDimension"));
    QVERIFY(!angles.front()->hasKey("IT"));
    for (const pdf::PDFDictionary* annotation : { lines.front(), angles.front(), areas.front(), perimeters.front() })
    {
        const pdf::PDFDictionary* measure = changed->getDictionaryFromObject(annotation->get("Measure"));
        QVERIFY(measure);
        QCOMPARE(changed->getObject(measure->get("Subtype")).getString(), QByteArray("RL"));
    }

    // The annotations are listed with their values (72 pt at 1:10 = 254 mm)
    result = call(fixture, tab, QStringLiteral("measure"), QJsonObject({ { QStringLiteral("lengthUnit"), QStringLiteral("mm") } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const QJsonArray listed = AppMcpReviewTest::content(*result, QStringLiteral("measure")).value(QStringLiteral("data")).toObject().value(QStringLiteral("measurements")).toArray();
    QCOMPARE(listed.size(), qsizetype(4));
    bool isDistanceFound = false;
    for (const QJsonValue& row : listed)
    {
        QCOMPARE(row.toObject().value(QStringLiteral("source")).toString(), QStringLiteral("annotation"));
        if (row.toObject().value(QStringLiteral("kind")).toString() == QLatin1String("distance"))
        {
            isDistanceFound = qAbs(row.toObject().value(QStringLiteral("value")).toDouble() - 254.0) < 1e-6;
        }
    }
    QVERIFY(isDistanceFound);
}

void AppMcpReviewTest::measureLimits()
{
    // Plan 6.2, 7.1: at most 500 measurements in a call (each one shown as a ghost - PDFAppMcpGhost::MAXIMUM_GHOSTS) and
    // 200 points of a measurement, at both edges; over a limit the call is refused with the reason, no question
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFAppDocumentTab* tab = open(fixture, QStringLiteral("Plan.pdf"), mergetest::MergeTestPdf::createPages("M", 1).write());
    QVERIFY(tab);
    const quint64 version = tab->getDocument()->getVersion();
    fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::DeniedUser);

    const auto add = [&fixture, tab](const QJsonArray& items)
    {
        return call(fixture, tab, QStringLiteral("measure"), QJsonObject({ { QStringLiteral("mode"), QStringLiteral("add") }, { QStringLiteral("measurements"), measurements(items) } }));
    };

    QJsonArray distances;
    for (int i = 0; i < pdf::PDFDocumentToolMeasure::MAXIMUM_MEASUREMENT_COUNT; ++i)
    {
        distances.append(measurement(1, QStringLiteral("distance"), { QPointF(10, 10 + i), QPointF(100, 10 + i) }));
    }
    std::optional<pdf::PDFMcpCallResult> result = add(distances);
    QVERIFY(result && result->isError);
    QCOMPARE(int(fixture.confirmations()->shown.size()), 1);
    QCOMPARE(fixture.confirmations()->shown.back()->details.value(QStringLiteral("ghosts")).toArray().size(), qsizetype(pdf::PDFDocumentToolMeasure::MAXIMUM_MEASUREMENT_COUNT));

    distances.append(measurement(1, QStringLiteral("distance"), { QPointF(10, 600), QPointF(100, 600) }));
    result = add(distances);
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("At most 500 measurements")), qPrintable(result->getFirstText()));
    QCOMPARE(int(fixture.confirmations()->shown.size()), 1);

    // The corners of a polygon on a circle, all different
    const auto corners = [](int count)
    {
        std::vector<QPointF> points;
        for (int i = 0; i < count; ++i)
        {
            const double angle = 2.0 * std::numbers::pi * i / count;
            points.emplace_back(300.0 + 200.0 * std::cos(angle), 400.0 + 200.0 * std::sin(angle));
        }
        return points;
    };
    result = add(QJsonArray({ measurement(1, QStringLiteral("area"), corners(pdf::PDFDocumentToolMeasure::MAXIMUM_POINT_COUNT)) }));
    QVERIFY(result && result->isError);
    QCOMPARE(int(fixture.confirmations()->shown.size()), 2);
    const std::optional<pdf::PDFAppMcpGhost> polygon = pdf::PDFAppMcpGhost::fromJson(fixture.confirmations()->shown.back()->details.value(QStringLiteral("ghosts")).toArray().at(0).toObject());
    QVERIFY(polygon);
    QCOMPARE(polygon->points.size(), qsizetype(pdf::PDFDocumentToolMeasure::MAXIMUM_POINT_COUNT));

    result = add(QJsonArray({ measurement(1, QStringLiteral("area"), corners(pdf::PDFDocumentToolMeasure::MAXIMUM_POINT_COUNT + 1)) }));
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("Measurement 1:")), qPrintable(result->getFirstText()));
    QCOMPARE(int(fixture.confirmations()->shown.size()), 2);
    QCOMPARE(tab->getDocument()->getVersion(), version);
}

void AppMcpReviewTest::measureRefusals()
{
    // Plan 4.8, 7.1, Q7.2: the annotations are refused before any run and any question on a locked document (also as a
    // copy), on a busy one (a tool of the shell runs), without bit 6 of Table 22 (with what works without it) and on a
    // document certified with no annotations allowed (12.8.2.2, Table 257: P 1, 2; P 3 allows them). Listing works on
    // all of them.
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::DeniedUser);
    const QJsonObject add({ { QStringLiteral("mode"), QStringLiteral("add") },
                            { QStringLiteral("measurements"), measurements(QJsonArray({ measurement(1, QStringLiteral("distance"), { QPointF(10, 10), QPointF(82, 10) }) })) } });

    pdf::PDFAppDocumentTab* tab = open(fixture, QStringLiteral("Plan.pdf"), mergetest::MergeTestPdf::createPages("M", 1).write());
    QVERIFY(tab);
    tab->getDocument()->setLocked(true);
    std::optional<pdf::PDFMcpCallResult> result = call(fixture, tab, QStringLiteral("measure"), add);
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText() == pdf::PDFAppMcpChangeJob::getLockedText(), qPrintable(result->getFirstText()));
    result = call(fixture, tab, QStringLiteral("measure"), add, QJsonObject({ { QStringLiteral("result"), QStringLiteral("save_copy") }, { QStringLiteral("path"), fixture.path(QStringLiteral("Locked_copy.pdf")) } }));
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText() == pdf::PDFAppMcpChangeJob::getLockedText(), qPrintable(result->getFirstText()));
    result = call(fixture, tab, QStringLiteral("measure"), QJsonObject());
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    tab->getDocument()->setLocked(false);

    QPointer<pdf::PDFAppDocumentOperation> operation = tab->getDocument()->beginOperation(pdf::PDFAppDocumentOperationKind::Tool, QStringLiteral("Running"), true);
    QVERIFY(operation);
    result = call(fixture, tab, QStringLiteral("measure"), add);
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("PDF4QT is busy")), qPrintable(result->getFirstText()));
    result = call(fixture, tab, QStringLiteral("measure"), QJsonObject());
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    if (operation)
    {
        operation->finish();
    }

    pdf::PDFAppDocumentTab* restricted = open(fixture, QStringLiteral("NoAnnotations.pdf"), pdftest::AppMcpReviewDocuments::encrypt(mergetest::MergeTestPdf::createPages("M", 1), AppMcpStep4::allowedWithout(6)));
    QVERIFY(restricted);
    result = call(fixture, restricted, QStringLiteral("measure"), add);
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("Table 22")) && result->getFirstText().contains(QStringLiteral("review.measure works with mode list or csv")), qPrintable(result->getFirstText()));
    result = call(fixture, restricted, QStringLiteral("measure"), QJsonObject({ { QStringLiteral("mode"), QStringLiteral("csv") } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));

    for (int level : { 1, 2 })
    {
        pdf::PDFAppDocumentTab* certified = open(fixture, QStringLiteral("Certified%1.pdf").arg(level), pdftest::AppMcpReviewDocuments::createCertified(level).write());
        QVERIFY(certified);
        result = call(fixture, certified, QStringLiteral("measure"), add);
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("12.8.2.2, Table 257")), qPrintable(result->getFirstText()));
        result = call(fixture, certified, QStringLiteral("measure"), QJsonObject());
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    }
    QVERIFY(fixture.confirmations()->shown.empty());

    pdf::PDFAppDocumentTab* allowing = open(fixture, QStringLiteral("Certified3.pdf"), pdftest::AppMcpReviewDocuments::createCertified(3).write());
    QVERIFY(allowing);
    result = call(fixture, allowing, QStringLiteral("measure"), add);
    QVERIFY(result && result->isError);
    QCOMPARE(int(fixture.confirmations()->shown.size()), 1);
}

int main(int argc, char* argv[])
{
    // Child processes of the tests of two instances (AppMcpFixture::secondInstance)
    const int childResult = pdftest::AppMcpFixture::runChild(argc, argv);
    if (childResult >= 0)
    {
        return childResult;
    }

    QApplication application(argc, argv);
    application.setAttribute(Qt::AA_Use96Dpi, true);
    AppMcpReviewTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_appmcpreviewtest.moc"
