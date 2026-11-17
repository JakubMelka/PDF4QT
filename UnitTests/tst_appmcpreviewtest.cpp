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

// The MCP actions of the tool review of step 7a + 7b of phase 2 over HTTP (plan of step 7, 7.1 and 8, with
// chapters 11 and 12): the schemas and their limits at both edges, the results (the data of the document
// marked and sanitized, return_text at most 1 MiB, the data at most 4 MiB, save_file only of a format),
// each tool on a small document with the values of ISO 32000-2 and of the plan, the refusal without bit 5
// of Table 22 (Q7.2), Soft Proofing without the soft proof of a tab (Q7.10), a locked and a busy document,
// and a hostile document (a cycle, a deep chain of /Next, a decode bomb), which never ends the process.

#include "appmcpstep4.h"
#include "icctestprofile.h"
#include "outputpreviewtestpdf.h"

#include "pdfappbuiltintools.h"
#include "pdfappdocumentsoftproof.h"
#include "pdfappdocumentview.h"
#include "pdfappmcppermissions.h"
#include "pdfmcpschema.h"
#include "pdfstreamfilters.h"

#include <QApplication>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QtTest>

#include <cmath>

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
    QCOMPARE(actions, QStringList({ QStringLiteral("output-preview"), QStringLiteral("ink-coverage"), QStringLiteral("soft-proof"), QStringLiteral("statistics"),
                                    QStringLiteral("object-inspector"), QStringLiteral("report") }));

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
    QCOMPARE(resultProperty.value(QStringLiteral("enum")).toArray(), QJsonArray({ QStringLiteral("return_text"), QStringLiteral("save_file") }));
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
