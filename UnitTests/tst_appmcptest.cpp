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

#include "appmcpfixture.h"
#include "appmcpreadtests.h"
#include "testdocumenttool.h"

#include "pdfappaipage.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappmainwindow.h"
#include "pdfappmcpactivity.h"
#include "pdfappmcparguments.h"
#include "pdfappmcpcatalogtools.h"
#include "pdfappmcpclientnames.h"
#include "pdfappmcpcontroller.h"
#include "pdfappmcphost.h"
#include "pdfappmcpjob.h"
#include "pdfappmcpdocumentresources.h"
#include "pdfappmcptoolhandler.h"
#include "pdfappmcptoolset.h"
#include "pdfappnavigator.h"
#include "pdfapproute.h"
#include "pdfapptab.h"
#include "pdfmcprepository.h"
#include "pdfdatabase.h"
#include "pdfdatabasestatement.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"

#include "pdfdocumenttoolregistry.h"
#include "pdfmcperrorcodes.h"
#include "pdfmcplimits.h"
#include "pdfmcpserver.h"
#include "pdfmcpjson.h"
#include "pdfmcpschema.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>

// Wave 0 of step 7 (plan 8 item 7) in the application: the frozen contract of
// the tools (toolSetContract), the bridge of the tools of the catalog with the
// test tool (catalogToolBridge), the controller and the ownership of the server
// (controllerOwnership), the host (permissions, read approvals, stubs), the
// route /ai, the client names and the activity texts. Package B adds its read
// tools here and the end-to-end tests over HTTP after package A.

/// Helpers of the tests of the MCP application layer
class AppMcpTestHelper
{
public:
    AppMcpTestHelper() = delete;

    /// A link of the folder \p link to the folder \p target: a junction on Windows
    /// (no privilege needed), a symbolic link elsewhere. \returns false, when the
    /// link cannot be created here.
    static bool createDirectoryLink(const QString& target, const QString& link)
    {
#ifdef Q_OS_WIN
        const int exitCode = QProcess::execute(QStringLiteral("cmd"), QStringList({ QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                                                                                    QDir::toNativeSeparators(link), QDir::toNativeSeparators(target) }));
        // (QFileInfo does not resolve a junction: its canonical path is the link)
        return exitCode == 0 && QFileInfo(link).isJunction();
#else
        return QFile::link(target, link) && QFileInfo(link).canonicalFilePath() == QFileInfo(target).canonicalFilePath();
#endif
    }

    /// Every key of every object of a JSON value (recursively)
    static void collectKeywords(const QJsonValue& value, bool isPropertiesMap, QSet<QString>* keywords)
    {
        if (value.isObject())
        {
            const QJsonObject object = value.toObject();
            for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            {
                if (!isPropertiesMap)
                {
                    keywords->insert(it.key());
                }
                collectKeywords(it.value(), !isPropertiesMap && it.key() == QLatin1String("properties"), keywords);
            }
        }
        else if (value.isArray())
        {
            for (const QJsonValue& item : value.toArray())
            {
                collectKeywords(item, false, keywords);
            }
        }
    }

    /// Every input object is closed (additionalProperties false)
    static bool isClosedRecursively(const QJsonObject& schema, QString* path, const QString& current = QStringLiteral("$"))
    {
        if (schema.value(QStringLiteral("type")).toString() == QLatin1String("object") && schema.value(QStringLiteral("additionalProperties")) != QJsonValue(false))
        {
            *path = current;
            return false;
        }

        const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
        for (auto it = properties.constBegin(); it != properties.constEnd(); ++it)
        {
            if (!isClosedRecursively(it.value().toObject(), path, current + QLatin1Char('.') + it.key()))
            {
                return false;
            }
        }
        if (schema.contains(QStringLiteral("items")) && !isClosedRecursively(schema.value(QStringLiteral("items")).toObject(), path, current + QStringLiteral("[]")))
        {
            return false;
        }
        return true;
    }

    static QJsonObject structured(const pdf::PDFMcpCallResult& result)
    {
        return result.toQJson().value(QStringLiteral("structuredContent")).toObject();
    }

    static QJsonObject arguments(const char* json)
    {
        return QJsonDocument::fromJson(QByteArray(json)).object();
    }

    /// RAII registration of the test tool
    class ToolRegistration
    {
    public:
        ToolRegistration() : tool(std::make_shared<pdftest::TestDocumentTool>())
        {
            pdf::PDFDocumentToolRegistry::registerTool(tool);
        }

        ~ToolRegistration()
        {
            pdf::PDFDocumentToolRegistry::unregisterTool(QString::fromLatin1(pdftest::TestDocumentTool::TOOL_ID));
        }

        std::shared_ptr<pdftest::TestDocumentTool> tool;
    };
};

class AppMcpTest : public QObject
{
    Q_OBJECT

private slots:
    void toolSetContract();
    void toolSetPermissionsFilter();
    void catalogToolBridge();
    void catalogToolBridgeConfirmations();
    void hostCallRules();
    void readApproval();
    void controllerOwnership();
    void controllerSecondProcess();
    void aiPageRoute();
    void clientNames();
    void activityTexts();
    void argumentsRules();
    void reviewCatalogConfirmationReleasedAtQuit();
    void reviewReadApprovalRecheckedAfterAnswer();
    void reviewHeartbeatWriteRefused();
    void reviewActivityAfterClear();
    void reviewSaveCopyNeverReplaces();
    void reviewReadApprovalCanceledAfterDecision();
    void reviewActivityStatusAfterLongWait();
    void reviewEverythingChangedRestartsServer();
    void reviewCatalogSaveCopyRecheckedAfterConfirmation();
    void reviewIndexWaiterCanceled();
    void reviewHeavyJobsNeverBlockWorkers();
    void reviewUntrustedPlaceholders();
    void reviewSubscribeWithoutListing();
    void reviewStateWithoutReadHidesDocuments();
    void reviewConfirmationQueueLimit();
    void fixOpenOfOpenFileAsksRead();
    void fixListedTitlesWhileReadAsk();
    void fixConfirmationAfterShutdown();
    void fixJunctionsResolved();
    void fixOwnerRequests();

    // Package B: the read tools, resources, prompts, completion (appmcpreadtests.h)
    void documentActions() { AppMcpReadTests::documentActions(); }
    void applicationState() { AppMcpReadTests::applicationState(); }
    void textActions() { AppMcpReadTests::textActions(); }
    void textGeometryAndSearch() { AppMcpReadTests::textGeometryAndSearch(); }
    void indexBuildProgress() { AppMcpReadTests::indexBuildProgress(); }
    void renderGeometry() { AppMcpReadTests::renderGeometry(); }
    void documentResolution() { AppMcpReadTests::documentResolution(); }
    void readPermissionMatrix() { AppMcpReadTests::readPermissionMatrix(); }
    void resourcesDocument() { AppMcpReadTests::resourcesDocument(); }
    void completionDisclosure() { AppMcpReadTests::completionDisclosure(); }
    void promptsGet() { AppMcpReadTests::promptsGet(); }
    void viewActions() { AppMcpReadTests::viewActions(); }
    void fastPath() { AppMcpReadTests::fastPath(); }
    void concurrentReads() { AppMcpReadTests::concurrentReads(); }
    void hostileInput() { AppMcpReadTests::hostileInput(); }
    void deletionMatrix() { AppMcpReadTests::deletionMatrix(); }
    void resourceSubscriptionsHttp() { AppMcpReadTests::resourceSubscriptionsHttp(); }
    void httpEndToEnd() { AppMcpReadTests::httpEndToEnd(); }
    void catalogToolBridgeHttp() { AppMcpReadTests::catalogToolBridgeHttp(); }
    void readGateEverySurface() { AppMcpReadTests::readGateEverySurface(); }
    void jobCancelAndExceptions() { AppMcpReadTests::jobCancelAndExceptions(); }
    void denialAudit() { AppMcpReadTests::denialAudit(); }
    void activityRefusedWrite() { AppMcpReadTests::activityRefusedWrite(); }
    void readerPagesAndOutline() { AppMcpReadTests::readerPagesAndOutline(); }
    void readerFormFields() { AppMcpReadTests::readerFormFields(); }
    void readerDocumentInfoVariants() { AppMcpReadTests::readerDocumentInfoVariants(); }
    void readerAttachmentsAndSignatures() { AppMcpReadTests::readerAttachmentsAndSignatures(); }
    void readerTextTruncatedAndHostileFiles() { AppMcpReadTests::readerTextTruncatedAndHostileFiles(); }
    void readerHostileReadArguments() { AppMcpReadTests::readerHostileReadArguments(); }
};

void AppMcpTest::toolSetContract()
{
    using Tool = pdf::PDFAppMcpToolSpec;
    const std::vector<Tool>& tools = pdf::PDFAppMcpToolSet::getTools();

    // The tools and actions of plan chapter 3 (O27: outline and read_attachment)
    const QStringList expectedTools = { QStringLiteral("application"), QStringLiteral("document"), QStringLiteral("text"), QStringLiteral("render"), QStringLiteral("view"),
                                        QStringLiteral("annotations"), QStringLiteral("forms"), QStringLiteral("outline"), QStringLiteral("files") };
    QStringList names;
    for (const Tool& tool : tools)
    {
        names << tool.name;
        QVERIFY2(!pdf::PDFAppMcpToolSet::getReservedNames().contains(tool.name), qPrintable(tool.name));
        QVERIFY(!pdf::PDFAppMcpCatalogTools::isCatalogToolName(tool.name));
        QVERIFY(!tool.actions.empty());
        QVERIFY(!tool.description.isEmpty());
        QVERIFY(!tool.annotations.openWorldHint);
    }
    QCOMPARE(names, expectedTools);

    const std::map<QString, QStringList> expectedActions =
    {
        { QStringLiteral("application"), { QStringLiteral("get_state") } },
        { QStringLiteral("document"), { QStringLiteral("get_info"), QStringLiteral("get_pages"), QStringLiteral("get_outline"), QStringLiteral("get_annotations"),
                                        QStringLiteral("get_form_fields"), QStringLiteral("get_attachments"), QStringLiteral("read_attachment"),
                                        QStringLiteral("get_signatures"), QStringLiteral("get_selection"), QStringLiteral("get_layers"), QStringLiteral("get_bookmarks") } },
        { QStringLiteral("text"), { QStringLiteral("get_text"), QStringLiteral("search"), QStringLiteral("get_text_in_area") } },
        { QStringLiteral("render"), { QStringLiteral("page"), QStringLiteral("overview") } },
        { QStringLiteral("view"), { QStringLiteral("activate"), QStringLiteral("go_to"), QStringLiteral("show_area"), QStringLiteral("show_search"), QStringLiteral("navigate"), QStringLiteral("close") } },
        { QStringLiteral("annotations"), { QStringLiteral("add"), QStringLiteral("update"), QStringLiteral("delete"), QStringLiteral("reply") } },
        { QStringLiteral("forms"), { QStringLiteral("set_values"), QStringLiteral("reset") } },
        { QStringLiteral("outline"), { QStringLiteral("add"), QStringLiteral("update"), QStringLiteral("move"), QStringLiteral("delete"), QStringLiteral("replace") } },
        { QStringLiteral("files"), { QStringLiteral("open"), QStringLiteral("save"), QStringLiteral("save_copy") } },
    };

    // Permissions, annotations (1.4) and the actions
    for (const Tool& tool : tools)
    {
        QStringList actions;
        for (const pdf::PDFAppMcpActionSpec& action : tool.actions)
        {
            actions << action.name;
            QVERIFY(!action.description.isEmpty());
            QCOMPARE(action.input.getType(), pdf::PDFMcpSchema::Type::Object);
            QCOMPARE(action.output.getType(), pdf::PDFMcpSchema::Type::Object);

            pdf::PDFAppMcpPermissionKind expected = pdf::PDFAppMcpPermissionKind::Read;
            if (tool.name == QLatin1String("application"))
            {
                expected = pdf::PDFAppMcpPermissionKind::None;
            }
            else if (tool.name == QLatin1String("annotations") || tool.name == QLatin1String("forms") || tool.name == QLatin1String("outline"))
            {
                expected = pdf::PDFAppMcpPermissionKind::Edit;
            }
            else if (tool.name == QLatin1String("files"))
            {
                expected = pdf::PDFAppMcpPermissionKind::Files;
            }
            QVERIFY2(action.permission == expected, qPrintable(action.getActionId(tool.name)));
        }
        QCOMPARE(actions, expectedActions.at(tool.name));

        const bool isReadOnly = tool.name == QLatin1String("application") || tool.name == QLatin1String("document") || tool.name == QLatin1String("text") || tool.name == QLatin1String("render");
        const bool isDestructive = tool.name == QLatin1String("annotations") || tool.name == QLatin1String("forms") || tool.name == QLatin1String("outline") || tool.name == QLatin1String("files");
        QCOMPARE(tool.annotations.readOnlyHint, isReadOnly);
        QCOMPARE(tool.annotations.destructiveHint, isDestructive);
        QCOMPARE(tool.annotations.idempotentHint, tool.name == QLatin1String("view") || tool.name == QLatin1String("application"));
    }

    // Schemas: object roots, the action first and required, no forbidden
    // keyword, every input object closed, deterministic bytes, both eras valid
    const QStringList allowed = pdf::PDFMcpSchema::getAllowedKeywords();
    const pdf::PDFAppMcpPermissions all = pdf::PDFAppMcpPermissions::createAll(pdf::PDFAppMcpPermission::Allow);
    const std::vector<pdf::PDFMcpToolDefinition> definitions = pdf::PDFAppMcpToolSet::createDefinitions(all);
    const std::vector<pdf::PDFMcpToolDefinition> again = pdf::PDFAppMcpToolSet::createDefinitions(all);
    QCOMPARE(definitions.size(), tools.size());
    for (size_t i = 0; i < definitions.size(); ++i)
    {
        const pdf::PDFMcpToolDefinition& definition = definitions[i];
        QCOMPARE(definition.inputSchema, again[i].inputSchema);
        QCOMPARE(definition.outputSchema, again[i].outputSchema);
        QVERIFY(definition.inputSchema.startsWith(R"({"type":"object","properties":{"action":{"type":"string")"));

        for (const QByteArray& json : { definition.inputSchema, definition.outputSchema })
        {
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(json, &error);
            QVERIFY2(error.error == QJsonParseError::NoError && document.isObject(), json.left(200).constData());
            QCOMPARE(document.object().value(QStringLiteral("type")).toString(), QStringLiteral("object"));
            QCOMPARE(document.object().value(QStringLiteral("required")).toArray(), QJsonArray({ QStringLiteral("action") }));

            QSet<QString> keywords;
            AppMcpTestHelper::collectKeywords(document.object(), false, &keywords);
            for (const QString& keyword : keywords)
            {
                QVERIFY2(allowed.contains(keyword), qPrintable(QStringLiteral("%1: %2").arg(definition.name, keyword)));
            }

            // The schema of the server parses it (it validates the arguments)
            QString schemaError;
            QVERIFY2(pdf::PDFMcpSchema::fromQJson(document.object(), &schemaError).has_value(), qPrintable(schemaError));
        }

        const QJsonObject input = QJsonDocument::fromJson(definition.inputSchema).object();
        QString openPath;
        QVERIFY2(AppMcpTestHelper::isClosedRecursively(input, &openPath), qPrintable(definition.name + QLatin1Char(' ') + openPath));
        QCOMPARE(input.value(QStringLiteral("additionalProperties")), QJsonValue(false));

        // Every action has its object in the input and the output
        const QJsonObject properties = input.value(QStringLiteral("properties")).toObject();
        const QJsonObject outputProperties = QJsonDocument::fromJson(definition.outputSchema).object().value(QStringLiteral("properties")).toObject();
        for (const QJsonValue& action : properties.value(QStringLiteral("action")).toObject().value(QStringLiteral("enum")).toArray())
        {
            QVERIFY(properties.value(action.toString()).isObject());
            QVERIFY(outputProperties.value(action.toString()).isObject());
        }
    }

    // A document action returns documentId and title; documentId is a common property
    const pdf::PDFAppMcpToolSpec* text = pdf::PDFAppMcpToolSet::findTool(u"text");
    QVERIFY(text);
    QVERIFY(text->findAction(u"search")->output.findProperty(QStringLiteral("documentId")));
    QVERIFY(text->findAction(u"search")->output.findProperty(QStringLiteral("origin")));
    QCOMPARE(text->findAction(u"search")->input.getRequired(), QStringList({ QStringLiteral("query") }));
    QVERIFY(pdf::PDFAppMcpToolSet::createInputSchema(*text, all).findProperty(QStringLiteral("documentId")));
    QVERIFY(!pdf::PDFAppMcpToolSet::findTool(u"application")->commonProperties.size());
    QVERIFY(!pdf::PDFAppMcpToolSet::findTool(u"pages"));

    // Search quads feed annotations.add (the same shape)
    const pdf::PDFMcpSchema* quads = pdf::PDFAppMcpToolSet::findTool(u"annotations")->findAction(u"add")->input.findProperty(QStringLiteral("items"))->getItems()->findProperty(QStringLiteral("quads"));
    QVERIFY(quads);
    QCOMPARE(quads->getItems()->getType(), pdf::PDFMcpSchema::Type::Array);

    // Valid examples validate, hostile ones are refused with their paths
    pdf::PDFMcpSchema annotations = pdf::PDFAppMcpToolSet::createInputSchema(*pdf::PDFAppMcpToolSet::findTool(u"annotations"), all);
    QStringList errors;
    QVERIFY(annotations.validate(AppMcpTestHelper::arguments(R"({"action":"add","add":{"items":[{"type":"highlight","page":1,"quads":[[0,10,10,10,0,0,10,0]],"color":"yellow"}]}})"),
                                 QStringLiteral("arguments"), &errors));
    QVERIFY(!annotations.validate(AppMcpTestHelper::arguments(R"({"action":"add","add":{"items":[{"type":"star","page":0,"quads":[[1,2]]}]},"extra":1})"),
                                  QStringLiteral("arguments"), &errors, 16));
    const QString text1 = errors.join(QLatin1Char('\n'));
    QVERIFY2(text1.contains(QStringLiteral("arguments.add.items[0].type")), qPrintable(text1));
    QVERIFY2(text1.contains(QStringLiteral("arguments.add.items[0].page")), qPrintable(text1));
    QVERIFY2(text1.contains(QStringLiteral("arguments.add.items[0].quads[0]")), qPrintable(text1));
    QVERIFY2(text1.contains(QStringLiteral("arguments.extra: unknown property")), qPrintable(text1));

    QCOMPARE(pdf::PDFAppMcpToolSet::getAnnotationTypes().size(), 15);
    QCOMPARE(pdf::PDFAppMcpToolSet::getStampNames().size(), 14);
    QCOMPARE(pdf::PDFAppMcpToolSet::getColorNames().size(), 9);
}

void AppMcpTest::toolSetPermissionsFilter()
{
    using P = pdf::PDFAppMcpPermission;

    // Every combination of the three permissions: the denied actions are not in
    // the enum, a tool without an action is absent, get_state always stays
    for (P read : { P::Allow, P::Ask, P::Deny })
    {
        for (P edit : { P::Allow, P::Ask, P::Deny })
        {
            for (P files : { P::Allow, P::Ask, P::Deny })
            {
                pdf::PDFAppMcpPermissions permissions;
                permissions.read = read;
                permissions.edit = edit;
                permissions.files = files;

                QStringList names;
                for (const pdf::PDFMcpToolDefinition& definition : pdf::PDFAppMcpToolSet::createDefinitions(permissions))
                {
                    names << definition.name;
                    const QJsonArray actions = QJsonDocument::fromJson(definition.inputSchema).object().value(QStringLiteral("properties")).toObject()
                                                   .value(QStringLiteral("action")).toObject().value(QStringLiteral("enum")).toArray();
                    QVERIFY(!actions.isEmpty());
                }

                QVERIFY(names.contains(QStringLiteral("application")));
                for (const char* name : { "document", "text", "render", "view" })
                {
                    QCOMPARE(names.contains(QString::fromLatin1(name)), read != P::Deny);
                }
                for (const char* name : { "annotations", "forms", "outline" })
                {
                    QCOMPARE(names.contains(QString::fromLatin1(name)), edit != P::Deny);
                }
                QCOMPARE(names.contains(QStringLiteral("files")), files != P::Deny);
            }
        }
    }

    // The host publishes a new catalog when a permission changes; a denied tool
    // is an unknown tool (-32602)
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.host());
    QVERIFY(fixture.setPermissions(P::Allow));
    const quint64 generation = fixture.host()->getCatalogGeneration();
    QSignalSpy spy(fixture.host(), &pdf::PDFAppMcpHost::catalogChanged);
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, P::Deny));
    QVERIFY(fixture.host()->getCatalogGeneration() > generation);
    QVERIFY(!spy.isEmpty());
    QVERIFY(!fixture.host()->getCatalog()->findTool(u"annotations"));

    const std::optional<pdf::PDFMcpCallResult> denied = fixture.callTool(QStringLiteral("annotations"), AppMcpTestHelper::arguments(R"({"action":"delete","delete":{"ids":["1:0"]}})"));
    QVERIFY(denied);
    QCOMPARE(denied->kind, pdf::PDFMcpCallResult::Kind::Error);
    QCOMPARE(denied->errorCode, pdf::PDFMcpErrorCodes::INVALID_PARAMS);

    // The same permissions publish nothing
    const quint64 current = fixture.host()->getCatalogGeneration();
    fixture.host()->setPermissions(fixture.host()->getPermissions());
    QCOMPARE(fixture.host()->getCatalogGeneration(), current);
}

void AppMcpTest::catalogToolBridge()
{
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.host());
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("bridge.pdf"), 3));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());
    pdf::PDFAppDocument* document = tab->getDocument();

    // Without an implementation the category tool is absent
    pdf::PDFDocumentToolRegistry::clearForTests();
    fixture.host()->checkToolRegistry();
    QVERIFY(!fixture.host()->getCatalog()->findTool(u"review"));
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("review"), AppMcpTestHelper::arguments(R"({"action":"statistics"})"));
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("not available")), qPrintable(result->getFirstText()));

    {
        // Registration: a new generation of the registry gives a new catalog
        // (the timer of the host checks it every second; checked here at once)
        AppMcpTestHelper::ToolRegistration registration;
        QSignalSpy spy(fixture.host(), &pdf::PDFAppMcpHost::catalogChanged);
        QTRY_VERIFY_WITH_TIMEOUT(fixture.host()->getCatalog()->findTool(u"review"), 5000);
        QVERIFY(!spy.isEmpty());

        const pdf::PDFMcpToolDefinition* review = fixture.host()->getCatalog()->findTool(u"review");
        const QJsonObject input = QJsonDocument::fromJson(review->inputSchema).object();
        const QJsonObject properties = input.value(QStringLiteral("properties")).toObject();
        QCOMPARE(properties.value(QStringLiteral("action")).toObject().value(QStringLiteral("enum")).toArray(), QJsonArray({ QStringLiteral("statistics") }));
        QVERIFY(properties.contains(QStringLiteral("documentId")));
        QCOMPARE(properties.value(QStringLiteral("result")).toObject().value(QStringLiteral("enum")).toArray(), QJsonArray({ QStringLiteral("keep"), QStringLiteral("save_copy") }));
        QVERIFY(properties.contains(QStringLiteral("path")));

        // The object of the action is the schema of the parameters of the core
        QJsonObject expected = pdf::PDFDocumentToolParameters::toJsonSchema(registration.tool->getParameters());
        QJsonObject actual = properties.value(QStringLiteral("statistics")).toObject();
        actual.remove(QStringLiteral("description"));
        expected.remove(QStringLiteral("description"));
        QCOMPARE(actual, expected);
        QVERIFY(actual.value(QStringLiteral("properties")).toObject().value(QStringLiteral("password")).toObject().value(QStringLiteral("writeOnly")).toBool());

        // mode=count: an analysis under Read, structuredContent = PDFDocumentToolResult::toJson
        result = fixture.callTool(QStringLiteral("review"), AppMcpTestHelper::arguments(R"({"action":"statistics","statistics":{"mode":"count","detailed":true}})"));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QJsonObject structured = AppMcpTestHelper::structured(*result);
        QCOMPARE(structured.value(QStringLiteral("action")).toString(), QStringLiteral("statistics"));
        QCOMPARE(structured.value(QStringLiteral("statistics")).toObject().value(QStringLiteral("status")).toString(), QStringLiteral("success"));
        QCOMPARE(structured.value(QStringLiteral("statistics")).toObject().value(QStringLiteral("documentId")).toString(), document->getId());
        QCOMPARE(structured.value(QStringLiteral("statistics")).toObject().value(QStringLiteral("data")).toObject().value(QStringLiteral("counted")).toInt(), 3);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);

        // Invalid parameters are execution errors naming the parameter
        result = fixture.callTool(QStringLiteral("review"), AppMcpTestHelper::arguments(R"({"action":"statistics","statistics":{"repeat":1000}})"));
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("repeat")), qPrintable(result->getFirstText()));

        // mode=title changes the document: Edit (Allow) - one undo step of the assistant
        result = fixture.callTool(QStringLiteral("review"), AppMcpTestHelper::arguments(R"({"action":"statistics","statistics":{"mode":"title","title":"Bridged"}})"));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
        QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Bridged"));
        QVERIFY(AppMcpTestHelper::structured(*result).value(QStringLiteral("statistics")).toObject().value(QStringLiteral("undoDescription")).toString().startsWith(QStringLiteral("Claude Code ran")));

        // Edit = Deny: the changed document is refused
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Deny));
        result = fixture.callTool(QStringLiteral("review"), AppMcpTestHelper::arguments(R"({"action":"statistics","statistics":{"mode":"title","title":"Denied"}})"));
        QVERIFY(result && result->isError);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Allow));

        // result=save_copy with a path needs Files; the tab is unchanged
        const QString copyPath = fixture.path(QStringLiteral("copy.pdf"));
        QJsonObject saveCopy = AppMcpTestHelper::arguments(R"({"action":"statistics","result":"save_copy","statistics":{"mode":"title","title":"Copy"}})");
        saveCopy.insert(QStringLiteral("path"), copyPath);
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Deny));
        result = fixture.callTool(QStringLiteral("review"), saveCopy);
        QVERIFY(result && result->isError);
        QVERIFY(!QFile::exists(copyPath));
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Allow));
        result = fixture.callTool(QStringLiteral("review"), saveCopy);
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QVERIFY(QFile::exists(copyPath));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
        result = fixture.callTool(QStringLiteral("review"), saveCopy);
        QVERIFY(result && result->isError);

        // repeat=50: progress per page
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("review"), AppMcpTestHelper::arguments(R"({"action":"statistics","statistics":{"repeat":50}})"), true);
        result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result && !result->isError);
        QVERIFY(call->getLastProgress() > 0);
        int progressEvents = 0;
        for (const pdf::PDFMcpCallEvent& event : call->takeEvents())
        {
            progressEvents += event.type == pdf::PDFMcpCallEvent::Type::Progress ? 1 : 0;
        }
        QVERIFY(progressEvents > 1);

        // A closed stream stops the run (canceled, nothing committed)
        std::shared_ptr<pdf::PDFMcpCall> canceled = fixture.startCall(QStringLiteral("review"), AppMcpTestHelper::arguments(R"({"action":"statistics","statistics":{"repeat":100}})"));
        canceled->cancel(pdf::PDFMcpCancelReason::StreamClosed);
        QVERIFY(pdftest::AppMcpFixture::waitResult(canceled).has_value());
        QVERIFY(canceled->getResult()->isError);
        QVERIFY2(fixture.waitActivity(pdf::PDFMcpActivityStatus::Canceled), qPrintable(canceled->getResult()->getFirstText()));

        // MCP runs are not counted as uses of the tool (O21)
        QVERIFY(fixture.context->getTools().getUsage().empty());
    }

    // Unregistered: a new catalog without the tool; a call is an execution error
    fixture.host()->checkToolRegistry();
    QVERIFY(!fixture.host()->getCatalog()->findTool(u"review"));
    result = fixture.callTool(QStringLiteral("review"), AppMcpTestHelper::arguments(R"({"action":"statistics"})"));
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("This tool is not available")), qPrintable(result->getFirstText()));
}

void AppMcpTest::catalogToolBridgeConfirmations()
{
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("ask.pdf"), 2));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());
    pdf::PDFAppDocument* document = tab->getDocument();
    AppMcpTestHelper::ToolRegistration registration;
    fixture.host()->checkToolRegistry();

    // Edit = Ask: the change waits for the user (a waiting activity row), Deny
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("review"), AppMcpTestHelper::arguments(R"({"action":"statistics","statistics":{"mode":"title","title":"Asked"}})"));
    std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmDialog();
    QVERIFY(confirmation);
    QCOMPARE(confirmation->kind, pdf::PDFAppMcpConfirmationKind::Change);
    QVERIFY(confirmation->permissions.testFlag(pdf::PDFAppMcpPermissionKind::Edit));
    QCOMPARE(confirmation->clientName, QStringLiteral("Claude Code"));
    QCOMPARE(confirmation->document.data(), document);
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Waiting));
    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));
    std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result && result->isError);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedUser));

    // Allow: one undo step
    call = fixture.startCall(QStringLiteral("review"), AppMcpTestHelper::arguments(R"({"action":"statistics","statistics":{"mode":"title","title":"Allowed"}})"));
    QVERIFY(fixture.confirmDialog());
    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
    result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Allowed));

    // A change of the document while the question waits: the tool runs again on
    // the current version (plan 2.5)
    call = fixture.startCall(QStringLiteral("review"), AppMcpTestHelper::arguments(R"({"action":"statistics","statistics":{"mode":"title","title":"Rebuilt"}})"));
    QVERIFY(fixture.confirmDialog());
    document->getUndoStack();
    QVERIFY(document->commit(pdf::PDFModifiedDocument(document->getDocument(), document->getOptionalContentActivity(), pdf::PDFModifiedDocument::Reset),
                             QStringLiteral("User change"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
    result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Rebuilt"));

    // No confirmation interface: the request ends as unavailable
    fixture.host()->setConfirmationProvider(nullptr);
    call = fixture.startCall(QStringLiteral("review"), AppMcpTestHelper::arguments(R"({"action":"statistics","statistics":{"mode":"title","title":"Nobody"}})"));
    result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("cannot ask")), qPrintable(result->getFirstText()));
    fixture.host()->setConfirmationProvider(fixture.confirmations());

    // A canceled call withdraws its question
    call = fixture.startCall(QStringLiteral("review"), AppMcpTestHelper::arguments(R"({"action":"statistics","statistics":{"mode":"title","title":"Withdrawn"}})"));
    confirmation = fixture.confirmDialog();
    QVERIFY(confirmation);
    call->cancel(pdf::PDFMcpCancelReason::Disconnected);
    QTRY_VERIFY(fixture.host()->getPendingConfirmations().empty());
    QVERIFY(!fixture.confirmations()->withdrawn.empty());
    QCOMPARE(fixture.confirmations()->withdrawn.back().second, pdf::PDFAppMcpDecision::DeniedDisconnected);
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedDisconnected));

    // The locked document refuses the change without a question
    document->setLocked(true);
    call = fixture.startCall(QStringLiteral("review"), AppMcpTestHelper::arguments(R"({"action":"statistics","statistics":{"mode":"title","title":"Locked"}})"));
    result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result && result->isError);
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedLocked));
    document->setLocked(false);
}

void AppMcpTest::hostCallRules()
{
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));

    // An unknown tool or action is -32602
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("unknown"), QJsonObject());
    QVERIFY(result);
    QCOMPARE(result->errorCode, pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    result = fixture.callTool(QStringLiteral("text"), AppMcpTestHelper::arguments(R"({"action":"nothing"})"));
    QCOMPARE(result->errorCode, pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    result = fixture.callTool(QStringLiteral("text"), AppMcpTestHelper::arguments(R"({"action":1})"));
    QCOMPARE(result->errorCode, pdf::PDFMcpErrorCodes::INVALID_PARAMS);

    // Invalid arguments are execution errors with the path (SEP-1303)
    result = fixture.callTool(QStringLiteral("text"), AppMcpTestHelper::arguments(R"({"action":"search","search":{"query":5},"extra":true})"));
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("arguments.search.query")), qPrintable(result->getFirstText()));
    QVERIFY2(result->getFirstText().contains(QStringLiteral("arguments.extra")), qPrintable(result->getFirstText()));

    // Wave 0: the handlers of the tools are stubs
    for (const pdf::PDFAppMcpToolSpec& tool : pdf::PDFAppMcpToolSet::getTools())
    {
        QJsonObject arguments;
        arguments.insert(QStringLiteral("action"), tool.actions.front().name);
        result = fixture.callTool(tool.name, arguments);
        QVERIFY2(result.has_value(), qPrintable(tool.name));
        if (result->kind == pdf::PDFMcpCallResult::Kind::Complete && result->isError &&
            result->getFirstText().contains(QStringLiteral("not implemented in wave 0")))
        {
            continue;
        }
        // A package has replaced the stub - its own tests check it
    }

    // The shut down host refuses calls
    pdf::PDFAppMcpHost* host = fixture.host();
    host->shutdown();
    result = fixture.callTool(QStringLiteral("text"), AppMcpTestHelper::arguments(R"({"action":"search","search":{"query":"x"}})"));
    QVERIFY(result);
    QCOMPARE(result->errorCode, pdf::PDFMcpErrorCodes::SERVER_STOPPING);
}

void AppMcpTest::readApproval()
{
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Ask));
    const QString key = fixture.createRequest(QStringLiteral("text"), QJsonObject()).client.assistantKey;

    // No document open: no approval is needed
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("document"), AppMcpTestHelper::arguments(R"({"action":"get_info"})"));
    QVERIFY(result);
    QVERIFY(fixture.confirmations()->shown.empty());

    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("read.pdf"), 1));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());

    // Deny answers this request only; the next read asks again (O21)
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("document"), AppMcpTestHelper::arguments(R"({"action":"get_info"})"));
    std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmDialog();
    QVERIFY(confirmation);
    QCOMPARE(confirmation->kind, pdf::PDFAppMcpConfirmationKind::ReadDocuments);
    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));
    result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result && result->isError);
    QVERIFY(!fixture.host()->getPolicy().isReadApproved(key));

    call = fixture.startCall(QStringLiteral("document"), AppMcpTestHelper::arguments(R"({"action":"get_info"})"));
    QVERIFY(fixture.confirmDialog());
    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
    result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result);
    QVERIFY(fixture.host()->getPolicy().isReadApproved(key));

    // Approved: later reads do not ask
    const size_t shown = fixture.confirmations()->shown.size();
    result = fixture.callTool(QStringLiteral("text"), AppMcpTestHelper::arguments(R"({"action":"search","search":{"query":"x"}})"));
    QVERIFY(result);
    QCOMPARE(fixture.confirmations()->shown.size(), shown);

    // Another assistant asks again; idle / disconnect forgets the approval [D2]
    fixture.setClient(QStringLiteral("gemini-cli-mcp-client"));
    call = fixture.startCall(QStringLiteral("document"), AppMcpTestHelper::arguments(R"({"action":"get_info"})"));
    QVERIFY(fixture.confirmDialog());
    pdf::PDFMcpAssistantEvent event;
    event.kind = pdf::PDFMcpAssistantEvent::Kind::Disconnected;
    event.key = call->getRequest().client.assistantKey;
    fixture.host()->assistantEvent(event);
    result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result && result->isError);
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedDisconnected));

    event.kind = pdf::PDFMcpAssistantEvent::Kind::Idle;
    event.key = key;
    fixture.host()->assistantEvent(event);
    QVERIFY(!fixture.host()->getPolicy().isReadApproved(key));

    // The policy itself
    pdf::PDFAppMcpPermissionPolicy policy;
    pdf::PDFAppMcpPermissions permissions;
    permissions.read = pdf::PDFAppMcpPermission::Ask;
    permissions.edit = pdf::PDFAppMcpPermission::Deny;
    permissions.files = pdf::PDFAppMcpPermission::Allow;
    policy.setPermissions(permissions);
    QCOMPARE(policy.evaluate(pdf::PDFAppMcpPermissionKind::Read, QStringLiteral("k"), false), pdf::PDFAppMcpPolicyResult::Allowed);
    QCOMPARE(policy.evaluate(pdf::PDFAppMcpPermissionKind::Read, QStringLiteral("k"), true), pdf::PDFAppMcpPolicyResult::Ask);
    QCOMPARE(policy.evaluate(pdf::PDFAppMcpPermissionKind::Read | pdf::PDFAppMcpPermissionKind::Edit, QStringLiteral("k"), true), pdf::PDFAppMcpPolicyResult::Denied);
    QCOMPARE(policy.evaluate(pdf::PDFAppMcpPermissionKind::Files, QStringLiteral("k"), true), pdf::PDFAppMcpPolicyResult::Allowed);
    policy.approveRead(QStringLiteral("k"));
    QCOMPARE(policy.evaluate(pdf::PDFAppMcpPermissionKind::Read, QStringLiteral("k"), true), pdf::PDFAppMcpPolicyResult::Allowed);
    QCOMPARE(policy.evaluate(pdf::PDFAppMcpPermissionKind::Read, QStringLiteral("other"), true), pdf::PDFAppMcpPolicyResult::Ask);
    policy.forgetAll();
    QVERIFY(!policy.isReadApproved(QStringLiteral("k")));

    // Unknown names of the permissions are no permissions (a hostile stored value is the default)
    QVERIFY(!pdf::PDFAppMcpPermissions::fromName(QStringLiteral("everything")).has_value());
    QCOMPARE(*pdf::PDFAppMcpPermissions::fromName(QStringLiteral("ask")), pdf::PDFAppMcpPermission::Ask);
}

void AppMcpTest::controllerOwnership()
{
    // Two instances of one database file (two contexts of this process)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("pdf4qt-1.db"));
    const QStringList arguments = { QStringLiteral("PDF4QT") };

    pdf::PDFDatabaseOptions options;
    options.filePath = databasePath;

    pdf::PDFAppContext first(pdf::PDFAppCommandLine::parse(arguments, QDir::currentPath()));
    first.open(options);
    QCOMPARE(first.getDatabase()->getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    first.getNotifier()->setPollInterval(20);
    pdf::PDFAppContext second(pdf::PDFAppCommandLine::parse(arguments, QDir::currentPath()));
    second.open(options);
    second.getNotifier()->setPollInterval(20);

    pdf::PDFAppMcpController* a = first.getMcpController();
    pdf::PDFAppMcpController* b = second.getMcpController();
    QVERIFY(a && b);
    QVERIFY(a->getInstanceId() != b->getInstanceId());
    QCOMPARE(a->getState(), pdf::PDFAppMcpServerState::Stopped);

    // A token is generated at the first start (256 bits in base64url)
    QVERIFY(!first.getSettings().setValue(pdf::PDFSettingKeys::mcpPort(), int(pdftest::AppMcpFixture::findFreePort())).isError());
    QVERIFY(a->getToken().isEmpty());
    QVERIFY(a->start());
    QCOMPARE(a->getToken().size(), 43);
    QTRY_COMPARE_WITH_TIMEOUT(a->getState(), pdf::PDFAppMcpServerState::Running, 10000);
    QVERIFY(a->isOwner());
    QCOMPARE(a->getAddress(), QStringLiteral("http://127.0.0.1:%1/mcp").arg(a->getPort()));

    pdf::PDFMcpServerRow row = pdf::PDFMcpRepository(first.getDatabase()).getServer();
    QCOMPARE(row.instance, a->getInstanceId());
    QCOMPARE(row.state, pdf::PDFMcpServerRowState::Running);
    QCOMPARE(row.port, int(a->getPort()));

    // The second instance sees the owner and does not bind
    QTRY_COMPARE_WITH_TIMEOUT(b->getState(), pdf::PDFAppMcpServerState::OtherInstance, 10000);
    QVERIFY(!b->start());
    QCOMPARE(b->getState(), pdf::PDFAppMcpServerState::OtherInstance);

    // Requests of the other instance go to the owner through mcp_request
    pdf::PDFMcpClientRow client;
    client.key = QStringLiteral("a:0123456789abcdef0123456789abcdef");
    client.name = QStringLiteral("Claude Code");
    client.address = QStringLiteral("127.0.0.1");
    client.connected = 1;
    QVERIFY(!pdf::PDFMcpRepository(first.getDatabase()).upsertClient(client).isError());
    b->disconnectAll();
    QTRY_VERIFY_WITH_TIMEOUT(pdf::PDFMcpRepository(first.getDatabase()).getClients().empty(), 10000);

    QSignalSpy showSpy(a, &pdf::PDFAppMcpController::showWindowRequested);
    b->showOtherWindow();
    QTRY_COMPARE_WITH_TIMEOUT(showSpy.count(), 1, 10000);
    QTRY_VERIFY(pdf::PDFMcpRepository(first.getDatabase()).takeRequests().empty());

    // A new token disconnects everybody and keeps the server running with it
    const QString oldToken = a->getToken();
    const QString newToken = a->createNewToken();
    QVERIFY(newToken != oldToken);
    QCOMPARE(a->getToken(), newToken);
    QTRY_COMPARE_WITH_TIMEOUT(a->getState(), pdf::PDFAppMcpServerState::Running, 10000);
    QCOMPARE(a->getServer()->getConfig().token, newToken.toLatin1());

    // A change of Connection restarts the server (another port)
    const quint16 newPort = pdftest::AppMcpFixture::findFreePort();
    QVERIFY(!second.getSettings().setValue(pdf::PDFSettingKeys::mcpPort(), int(newPort)).isError());
    QTRY_COMPARE_WITH_TIMEOUT(a->getPort(), newPort, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(a->getState(), pdf::PDFAppMcpServerState::Running, 10000);

    // The port of the owner, when the row is released but the server still
    // runs: the probe finds PDF4QT (another instance)
    QVERIFY(!pdf::PDFMcpRepository(second.getDatabase()).releaseServer(a->getInstanceId()).isError());
    QVERIFY(b->start());
    QTRY_COMPARE_WITH_TIMEOUT(b->getState(), pdf::PDFAppMcpServerState::OtherInstance, 10000);
    b->stop();

    // The heartbeat of the owner refused (another instance took the row): the
    // server stops
    pdf::PDFMcpRepository stealer(second.getDatabase());
    bool claimed = false;
    stealer.setCurrentTime(QDateTime::currentMSecsSinceEpoch() + 60000);
    QVERIFY(!stealer.claimServer(QStringLiteral("stealer"), QCoreApplication::applicationPid() + 1000000, 1, pdf::PDFMcpServerRowState::Running, QString(), &claimed).isError());
    QVERIFY(claimed);
    a->setHeartbeatInterval(20);
    QTRY_VERIFY_WITH_TIMEOUT(!a->isRunning(), 10000);
    QVERIFY(!a->isOwner());

    // Quit order: the owner releases the row and deletes the client rows
    QVERIFY(!pdf::PDFMcpRepository(first.getDatabase()).releaseServer(QStringLiteral("stealer")).isError());
    QVERIFY(a->start());
    QTRY_COMPARE_WITH_TIMEOUT(a->getState(), pdf::PDFAppMcpServerState::Running, 10000);
    QVERIFY(!pdf::PDFMcpRepository(first.getDatabase()).upsertClient(client).isError());
    first.close();
    row = pdf::PDFMcpRepository(second.getDatabase()).getServer();
    QCOMPARE(row.state, pdf::PDFMcpServerRowState::Stopped);
    QVERIFY(pdf::PDFMcpRepository(second.getDatabase()).getClients().empty());
    QVERIFY(!first.getMcpController());

    // The second instance may start now
    QTRY_COMPARE_WITH_TIMEOUT(b->getState(), pdf::PDFAppMcpServerState::Stopped, 10000);
    QVERIFY(b->start());
    QTRY_COMPARE_WITH_TIMEOUT(b->getState(), pdf::PDFAppMcpServerState::Running, 10000);
    second.close();
}

void AppMcpTest::controllerSecondProcess()
{
    // A live owner of another process blocks the start; when it is killed, its
    // row becomes stale (a process, which does not run) and the start succeeds
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("pdf4qt-1.db"));

    pdf::PDFDatabaseOptions options;
    options.filePath = databasePath;
    pdf::PDFAppContext context(pdf::PDFAppCommandLine::parse({ QStringLiteral("PDF4QT") }, QDir::currentPath()));
    context.open(options);
    QCOMPARE(context.getDatabase()->getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    context.getNotifier()->setPollInterval(20);
    pdf::PDFAppMcpController* controller = context.getMcpController();
    controller->setHeartbeatInterval(50);
    QVERIFY(!context.getSettings().setValue(pdf::PDFSettingKeys::mcpPort(), int(pdftest::AppMcpFixture::findFreePort())).isError());

    std::unique_ptr<QProcess> owner(pdftest::AppMcpFixture::secondInstance({ QStringLiteral("--mcp-child-claim"), databasePath, QStringLiteral("child") }));
    QTRY_VERIFY_WITH_TIMEOUT(pdf::PDFMcpRepository(context.getDatabase()).getServer().instance == QLatin1String("child"), 30000);
    QTRY_COMPARE_WITH_TIMEOUT(controller->getState(), pdf::PDFAppMcpServerState::OtherInstance, 10000);
    QVERIFY(!controller->start());

    owner->kill();
    QVERIFY(owner->waitForFinished(10000));
    QVERIFY(controller->start());
    QTRY_COMPARE_WITH_TIMEOUT(controller->getState(), pdf::PDFAppMcpServerState::Running, 10000);
    QCOMPARE(pdf::PDFMcpRepository(context.getDatabase()).getServer().instance, controller->getInstanceId());
    context.close();
}

void AppMcpTest::aiPageRoute()
{
    // /ai is the page AI (wave 0 stub of package E), no longer Page Not Found
    pdftest::AppMcpFixture fixture;
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::ai());
    pdftest::AppDocumentFixture::settle();
    pdf::PDFAppAiPage* page = fixture.window->findChild<pdf::PDFAppAiPage*>();
    QVERIFY(page);
    QCOMPARE(page->getTitle(), QStringLiteral("AI"));
    QCOMPARE(page->getStateText(), QStringLiteral("The MCP server is stopped."));

    QVERIFY(fixture.startServer());
    QTRY_VERIFY(page->getStateText().startsWith(QStringLiteral("The MCP server is running at http://127.0.0.1:")));

    // The started server answers with Server: PDF4QT/<version> (package A: the dispatcher answers server/discover)
    std::unique_ptr<pdftest::McpTestClient> client = fixture.client();
    const pdftest::McpTestResponse response = client->post(QJsonDocument(client->createRequest(QStringLiteral("server/discover"))).toJson(QJsonDocument::Compact));
    QCOMPARE(response.status, 200);
    QVERIFY(response.json().value(QStringLiteral("result")).toObject().contains(QStringLiteral("supportedVersions")));
    QVERIFY(response.header("Server").startsWith("PDF4QT/"));

    fixture.controller()->stop();
    QTRY_COMPARE(page->getStateText(), QStringLiteral("The MCP server is stopped."));

    // A query is refused (Page Not Found)
    QVERIFY(!pdf::PDFAppAiPage(fixture.context.get()).setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/ai?x=1"))));
}

void AppMcpTest::clientNames()
{
    // The names of the clients and their sources (plan 2.7)
    pdf::PDFAppMcpClientInfo info = pdf::PDFAppMcpClientNames::describe(QStringLiteral("claude-code"));
    QCOMPARE(info.displayName, QStringLiteral("Claude Code"));
    QCOMPARE(info.monogram, QStringLiteral("Cl"));
    QCOMPARE(info.hue, pdf::PDFAccentHue::Amber);
    QVERIFY(info.isCommandLine);
    QVERIFY(info.isKnown);

    QCOMPARE(pdf::PDFAppMcpClientNames::getDisplayName(QStringLiteral("gemini-cli-mcp-client")), QStringLiteral("Gemini CLI"));
    QCOMPARE(pdf::PDFAppMcpClientNames::describe(QStringLiteral("gemini-cli-mcp-client")).monogram, QStringLiteral("Ge"));
    QCOMPARE(pdf::PDFAppMcpClientNames::describe(QStringLiteral("codex-mcp-client")).monogram, QStringLiteral("Cx"));
    QCOMPARE(pdf::PDFAppMcpClientNames::describe(QStringLiteral("Visual Studio Code")).displayName, QStringLiteral("GitHub Copilot"));
    QCOMPARE(pdf::PDFAppMcpClientNames::describe(QStringLiteral("CURSOR-VSCODE")).monogram, QStringLiteral("Cu"));
    for (const pdf::PDFAppMcpKnownClient& client : pdf::PDFAppMcpClientNames::getKnownClients())
    {
        QVERIFY(!client.source.isEmpty());
        QVERIFY(!client.clientNames.isEmpty());
    }

    // Unknown clients: the title, else the sanitized name with the mcp icon
    info = pdf::PDFAppMcpClientNames::describe(QStringLiteral("my-agent"), QStringLiteral("My Agent"));
    QCOMPARE(info.displayName, QStringLiteral("My Agent"));
    QVERIFY(!info.isKnown);
    QCOMPARE(info.hue, pdf::PDFAccentHue::Teal);
    QVERIFY(info.monogram.isEmpty());
    QCOMPARE(pdf::PDFAppMcpClientNames::getDisplayName(QStringLiteral("evil") + QChar(0x202E) + QStringLiteral("name\n")), QStringLiteral("evilname"));
    QCOMPARE(pdf::PDFAppMcpClientNames::getDisplayName(QString()), QStringLiteral("MCP client"));
    QCOMPARE(pdf::PDFAppMcpClientNames::getDisplayName(QString(500, QLatin1Char('x'))).size(), pdf::PDFAppMcpClientNames::MAXIMUM_NAME_LENGTH);
    QCOMPARE(pdf::PDFAppMcpClientNames::createMonogram(QStringLiteral("  my agent")), QStringLiteral("My"));
}

void AppMcpTest::activityTexts()
{
    using Activity = pdf::PDFAppMcpActivity;
    using Status = pdf::PDFMcpActivityStatus;

    // Texts of Figma 653:5231 / 955:150808
    QCOMPARE(Activity::getTitle(QStringLiteral("text.get_text"), QJsonObject()), QStringLiteral("Read text"));
    QCOMPARE(Activity::getTitle(QStringLiteral("render.page"), QJsonObject()), QStringLiteral("Rendered page image"));
    QCOMPARE(Activity::getTitle(QStringLiteral("files.open"), QJsonObject()), QStringLiteral("Open document"));
    QCOMPARE(Activity::getTitle(QStringLiteral("files.save"), QJsonObject()), QStringLiteral("Save document"));
    QCOMPARE(Activity::getTitle(QStringLiteral("files.save_copy"), QJsonObject()), QStringLiteral("Save a copy"));
    QCOMPARE(Activity::getTitle(QStringLiteral("annotations.add"), QJsonObject({ { QStringLiteral("count"), 3 }, { QStringLiteral("kind"), QStringLiteral("highlight") } })), QStringLiteral("Add 3 highlights"));
    QCOMPARE(Activity::getTitle(QStringLiteral("annotations.add"), QJsonObject({ { QStringLiteral("count"), 1 }, { QStringLiteral("kind"), QStringLiteral("note") } })), QStringLiteral("Add 1 note"));
    QCOMPARE(Activity::getTitle(QStringLiteral("annotations.delete"), QJsonObject({ { QStringLiteral("count"), 2 } })), QStringLiteral("Delete 2 annotations"));
    QCOMPARE(Activity::getTitle(QStringLiteral("forms.set_values"), QJsonObject({ { QStringLiteral("count"), 2 } })), QStringLiteral("Fill 2 form fields"));
    QVERIFY(Activity::getTitle(QStringLiteral("review.statistics"), QJsonObject()).startsWith(QStringLiteral("Run ")));
    QCOMPARE(Activity::getTitle(QStringLiteral("unknown"), QJsonObject()), QStringLiteral("Request of the assistant"));

    QCOMPARE(Activity::getMetaText(QStringLiteral("Claude Code"), QStringLiteral("Project_Report.pdf"), QJsonObject({ { QStringLiteral("pages"), QStringLiteral("1\u201312") } })),
             QStringLiteral("Claude Code \u00B7 Project_Report.pdf \u00B7 pages 1\u201312"));
    QCOMPARE(Activity::getMetaText(QStringLiteral("Gemini CLI"), QStringLiteral("Invoice.pdf"), QJsonObject({ { QStringLiteral("pages"), QStringLiteral("1") } })),
             QStringLiteral("Gemini CLI \u00B7 Invoice.pdf \u00B7 page 1"));

    QCOMPARE(Activity::getStatusText(Status::Ok, QString(), QJsonObject()), QString());
    QCOMPARE(Activity::getStatusText(Status::Waiting, QString(), QJsonObject()), QStringLiteral("Waiting for your confirmation"));
    QCOMPARE(Activity::getStatusText(Status::Waiting, QString(), QJsonObject(), true), QStringLiteral("Waiting for confirmation in the other PDF4QT window"));
    QCOMPARE(Activity::getStatusText(Status::Waiting, QStringLiteral("Claude Code"), QJsonObject({ { QStringLiteral("reason"), QStringLiteral("elicitation") } })), QStringLiteral("Waiting for a choice in Claude Code"));
    QCOMPARE(Activity::getStatusText(Status::Allowed, QString(), QJsonObject()), QStringLiteral("Allowed by you"));
    QCOMPARE(Activity::getStatusText(Status::DeniedUser, QString(), QJsonObject()), QStringLiteral("Denied by you"));
    QCOMPARE(Activity::getStatusText(Status::DeniedTimeout, QString(), QJsonObject()), QStringLiteral("Denied automatically \u00B7 no answer in 2 min"));
    QCOMPARE(Activity::getStatusText(Status::DeniedLocked, QString(), QJsonObject()), QStringLiteral("Denied \u00B7 the document is locked"));
    QCOMPARE(Activity::getStatusText(Status::DeniedClosed, QString(), QJsonObject()), QStringLiteral("Denied \u00B7 the tab was closed"));
    QCOMPARE(Activity::getStatusText(Status::DeniedDisconnected, QString(), QJsonObject()), QStringLiteral("Denied \u00B7 the assistant disconnected"));
    QCOMPARE(Activity::getStatusText(Status::DeniedBusy, QString(), QJsonObject()), QStringLiteral("Denied \u00B7 a tool is running on the document"));
    QCOMPARE(Activity::getStatusText(Status::DeniedDialog, QString(), QJsonObject()), QStringLiteral("Denied \u00B7 a dialog was open in PDF4QT"));
    QCOMPARE(Activity::getStatusText(Status::DeniedStopped, QString(), QJsonObject()), QStringLiteral("Denied \u00B7 the server stopped"));
    QCOMPARE(Activity::getStatusText(Status::Canceled, QString(), QJsonObject()), QStringLiteral("Canceled by the assistant"));
    QCOMPARE(Activity::getStatusText(Status::Failed, QString(), QJsonObject({ { QStringLiteral("reason"), QStringLiteral("The document changed in the meantime") } })),
             QStringLiteral("Failed \u00B7 The document changed in the meantime"));

    // Pages: formatting and parsing
    QCOMPARE(Activity::formatPages({ 1, 2, 3, 5, 7, 8 }), QStringLiteral("1\u20133, 5, 7\u20138"));
    QCOMPARE(Activity::parsePages(QStringLiteral("1\u20133, 5, 7\u20138")), std::set<int>({ 1, 2, 3, 5, 7, 8 }));
    QVERIFY(Activity::parsePages(QStringLiteral("x, 0, 5\u20131, 1\u20132\u20133")).empty());
    QCOMPARE(Activity::parsePages(QStringLiteral("1\u20131000000")).size(), size_t(10000));
    QVERIFY(!Activity::isLoggedAction(QStringLiteral("application.get_state")));
    QVERIFY(Activity::isReadAction(QStringLiteral("text.search")));

    // The log: batched, reads coalesced within 60 s, status changes by the local id
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppMcpActivityLog log(fixture.context->getDatabase());
    log.setCurrentTime(1000000);
    pdf::PDFAppMcpActivityEntry entry;
    entry.assistantKey = QStringLiteral("a:1");
    entry.client = QStringLiteral("Claude Code");
    entry.action = QStringLiteral("text.get_text");
    entry.document = QStringLiteral("Report.pdf");
    entry.documentId = QStringLiteral("doc");
    entry.pages = { 1, 2 };
    const qint64 first = log.record(entry);
    entry.pages = { 3, 4 };
    QCOMPARE(log.record(entry), first);
    QCOMPARE(log.getPendingCount(), 1);
    QCOMPARE(log.record([&]() { pdf::PDFAppMcpActivityEntry state = entry; state.action = QStringLiteral("application.get_state"); return state; }()), qint64(0));

    entry.action = QStringLiteral("annotations.add");
    entry.pages.clear();
    entry.status = Status::Waiting;
    const qint64 waiting = log.record(entry);
    QVERIFY(waiting != first);
    log.flush();
    QCOMPARE(log.getPendingCount(), 0);

    std::vector<pdf::PDFMcpActivityRow> rows = pdf::PDFMcpRepository(fixture.context->getDatabase()).getActivity(10);
    QCOMPARE(rows.size(), size_t(2));
    log.update(waiting, Status::Allowed, QJsonObject({ { QStringLiteral("count"), 3 } }));
    log.flush();
    rows = pdf::PDFMcpRepository(fixture.context->getDatabase()).getActivity(10);
    bool hasAllowed = false;
    bool hasPages = false;
    for (const pdf::PDFMcpActivityRow& row : rows)
    {
        hasAllowed = hasAllowed || (row.status == Status::Allowed && row.detail.value(QStringLiteral("count")).toInt() == 3);
        hasPages = hasPages || row.detail.value(QStringLiteral("pages")).toString() == QStringLiteral("1\u20134");
    }
    QVERIFY(hasAllowed);
    QVERIFY(hasPages);

    // After 60 s a read is a new row
    log.setCurrentTime(1000000 + 61000);
    entry.action = QStringLiteral("text.get_text");
    entry.status = Status::Ok;
    QVERIFY(log.record(entry) != first);
}

void AppMcpTest::argumentsRules()
{
    QString error;
    QCOMPARE(pdf::PDFAppMcpArguments::parseColor(QStringLiteral("#12AbEf"), &error)->name(), QStringLiteral("#12abef"));
    QVERIFY(pdf::PDFAppMcpArguments::parseColor(QStringLiteral(" Yellow "), &error).has_value());
    QCOMPARE(pdf::PDFAppMcpArguments::formatColor(*pdf::PDFAppMcpArguments::parseColor(QStringLiteral("green"), &error)), QStringLiteral("green"));
    QCOMPARE(pdf::PDFAppMcpArguments::formatColor(QColor(1, 2, 3)), QStringLiteral("#010203"));
    for (const char* invalid : { "", "#12345", "#1234567", "#xyzxyz", "teal", "rgb(1,2,3)" })
    {
        QVERIFY2(!pdf::PDFAppMcpArguments::parseColor(QString::fromLatin1(invalid), &error).has_value(), invalid);
    }

    // Pages
    QCOMPARE(*pdf::PDFAppMcpArguments::parsePages(QStringLiteral("1-3,7"), 10, 0, &error), std::vector<pdf::PDFInteger>({ 0, 1, 2, 6 }));
    QCOMPARE(pdf::PDFAppMcpArguments::parsePages(QString(), 3, 0, &error)->size(), size_t(3));
    QVERIFY(!pdf::PDFAppMcpArguments::parsePages(QString(), 3000, 2000, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("2000")));
    QVERIFY(!pdf::PDFAppMcpArguments::parsePages(QStringLiteral("12"), 10, 0, &error).has_value());
    QVERIFY(!pdf::PDFAppMcpArguments::parsePages(QStringLiteral("1-60"), 100, 50, &error).has_value());
    QVERIFY(!pdf::PDFAppMcpArguments::parsePages(QStringLiteral("1-99999999999999"), 10, 0, &error).has_value());
    QVERIFY(!pdf::PDFAppMcpArguments::parsePages(QStringLiteral("abc"), 10, 0, &error).has_value());
    QVERIFY(!pdf::PDFAppMcpArguments::parsePages(QStringLiteral("1"), 0, 0, &error).has_value());

    // Annotation ids
    qint64 object = 0;
    qint64 generation = 0;
    QVERIFY(pdf::PDFAppMcpArguments::parseAnnotationId(QStringLiteral("12:0"), &object, &generation));
    QCOMPARE(object, qint64(12));
    QCOMPARE(pdf::PDFAppMcpArguments::formatAnnotationId(12, 0), QStringLiteral("12:0"));
    for (const char* invalid : { "", "12", "0:0", "-1:0", "1:-1", "1:70000", "a:b", "1:2:3", "9999999999999:0" })
    {
        QVERIFY2(!pdf::PDFAppMcpArguments::parseAnnotationId(QString::fromLatin1(invalid), nullptr, nullptr), invalid);
    }

    // Paths of the assistants
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!pdf::PDFAppMcpArguments::checkPath(directory.filePath(QStringLiteral("out.pdf")), true, &error).isEmpty());
    for (const QString& invalid : { QString(), QStringLiteral("relative.pdf"), directory.filePath(QStringLiteral("out.txt")), directory.filePath(QStringLiteral("missing/out.pdf")),
                                    directory.path(), QStringLiteral("\\\\server\\share\\a.pdf"), QStringLiteral("//server/share/a.pdf"), QStringLiteral("\\\\?\\C:\\a.pdf"),
                                    QStringLiteral("\\\\.\\PhysicalDrive0"), directory.filePath(QStringLiteral("CON.pdf")), directory.filePath(QStringLiteral("a.pdf:stream")),
                                    QStringLiteral("smb://server/a.pdf"), QString(5000, QLatin1Char('a')) })
    {
        QVERIFY2(pdf::PDFAppMcpArguments::checkPath(invalid, true, &error).isEmpty(), qPrintable(invalid));
        QVERIFY(!error.isEmpty());
    }

    // Texts written into a document
    QCOMPARE(pdf::PDFAppMcpArguments::sanitizeDocumentText(QStringLiteral("a\u202Eb\nc") + QChar(0), 100), QStringLiteral("ab\nc"));
}

void AppMcpTest::reviewCatalogConfirmationReleasedAtQuit()
{
    // Review RW7-0: a run of a catalog tool waiting for its confirmation, when
    // the application quits. The decision of the quit is never delivered (the
    // host is destroyed with its queued events) - the job must not hold itself,
    // its call and its document alive (a cycle - a leak reported by LSan)
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("quit.pdf"), 2));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());
    AppMcpTestHelper::ToolRegistration registration;
    fixture.host()->checkToolRegistry();

    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("review"), AppMcpTestHelper::arguments(R"({"action":"statistics","statistics":{"mode":"title","title":"Quit"}})"));
    QVERIFY(fixture.confirmDialog());
    const std::weak_ptr<pdf::PDFMcpCall> weakCall = call;
    call.reset();
    fixture.confirmations()->shown.clear();

    // The quit order of plan 2.6 (the host is destroyed)
    fixture.controller()->shutdown();
    QVERIFY(!fixture.host());
    pdftest::AppDocumentFixture::settle();
    QVERIFY(weakCall.expired());
}

void AppMcpTest::reviewReadApprovalRecheckedAfterAnswer()
{
    // Review RW7-0: Read documents set to Deny while the read approval waits -
    // Allow of the question does not run the action (plan 7: a change during a
    // call is an execution error)
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("recheck.pdf"), 1));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());

    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("document"), AppMcpTestHelper::arguments(R"({"action":"get_info"})"));
    QVERIFY(fixture.confirmDialog());
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Deny));
    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
    std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("does not allow")), qPrintable(result->getFirstText()));
}

void AppMcpTest::reviewHeartbeatWriteRefused()
{
    // Review RW7-0: a refused write of the heartbeat (the write lock of another
    // process, a full disk) is not a loss of the row - the server runs on and the
    // heartbeat continues when the database accepts it again
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.startServer());
    pdf::PDFAppMcpController* controller = fixture.controller();
    QTRY_VERIFY(controller->isOwner());
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    const qint64 heartbeat = pdf::PDFMcpRepository(database).getServer().heartbeat;

    QVERIFY(pdf::PDFDatabaseStatement(database->getConnection(),
                                      "CREATE TEMP TRIGGER review_refuse_heartbeat BEFORE UPDATE ON mcp_server BEGIN SELECT RAISE(ABORT, 'refused'); END").execute());
    controller->setHeartbeatInterval(20);

    // Nothing may happen: several heartbeats fail meanwhile
    QTest::qWait(400);
    QVERIFY(controller->isRunning());
    QVERIFY(controller->isOwner());
    QCOMPARE(pdf::PDFMcpRepository(database).getServer().heartbeat, heartbeat);

    QVERIFY(pdf::PDFDatabaseStatement(database->getConnection(), "DROP TRIGGER temp.review_refuse_heartbeat").execute());
    QTRY_VERIFY_WITH_TIMEOUT(pdf::PDFMcpRepository(database).getServer().heartbeat > heartbeat, 10000);
    QVERIFY(controller->isRunning());
    QVERIFY(controller->isOwner());
}

void AppMcpTest::reviewActivityAfterClear()
{
    // Review RW7-0: a read coalesced into a row, which Clear (of any instance) or
    // the trim has deleted meanwhile, was written by an UPDATE of nothing - the
    // reads of the next 60 s were missing in the audit (plan 7)
    pdftest::AppMcpFixture fixture;
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    pdf::PDFAppMcpActivityLog log(database);
    log.setCurrentTime(2000000);

    pdf::PDFAppMcpActivityEntry entry;
    entry.assistantKey = QStringLiteral("a:1");
    entry.client = QStringLiteral("Claude Code");
    entry.action = QStringLiteral("text.get_text");
    entry.document = QStringLiteral("Report.pdf");
    entry.documentId = QStringLiteral("doc");
    entry.pages = { 1 };
    const qint64 first = log.record(entry);
    log.flush();
    QCOMPARE(pdf::PDFMcpRepository(database).getActivity(10).size(), size_t(1));

    QVERIFY(!pdf::PDFMcpRepository(database).clearActivity().isError());
    QVERIFY(pdf::PDFMcpRepository(database).getActivity(10).empty());

    log.setCurrentTime(2000000 + 1000);
    entry.pages = { 2 };
    QCOMPARE(log.record(entry), first);
    log.flush();
    std::vector<pdf::PDFMcpActivityRow> rows = pdf::PDFMcpRepository(database).getActivity(10);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows.front().action, QStringLiteral("text.get_text"));
    QCOMPARE(rows.front().time, qint64(2000000 + 1000));

    // The row written again is the row of the next coalesced read (no third row)
    log.setCurrentTime(2000000 + 2000);
    entry.pages = { 3 };
    QCOMPARE(log.record(entry), first);
    log.flush();
    rows = pdf::PDFMcpRepository(database).getActivity(10);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows.front().detail.value(QStringLiteral("pages")).toString(), QStringLiteral("1–3"));
}

void AppMcpTest::reviewSaveCopyNeverReplaces()
{
    // Review RW7-0: the path of save_copy was checked only when the call came; a
    // file, which appeared while the confirmation waited, was replaced by the copy
    // (an overwrite is always confirmed as such - plan 3.9)
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("source.pdf"), 1));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());
    AppMcpTestHelper::ToolRegistration registration;
    fixture.host()->checkToolRegistry();

    const QString copyPath = fixture.path(QStringLiteral("appeared.pdf"));
    QJsonObject saveCopy = AppMcpTestHelper::arguments(R"({"action":"statistics","result":"save_copy","statistics":{"mode":"title","title":"Copy"}})");
    saveCopy.insert(QStringLiteral("path"), copyPath);
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("review"), saveCopy);
    QVERIFY(fixture.confirmDialog());

    const QByteArray userData = QByteArrayLiteral("%PDF-1.7 the file of the user");
    {
        QFile file(copyPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(userData), qint64(userData.size()));
    }

    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
    std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("exists already")), qPrintable(result->getFirstText()));

    QFile file(copyPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), userData);
}

void AppMcpTest::reviewConfirmationQueueLimit()
{
    // Review RQ7: at most 16 requests wait for the user in one tab (plan 2.5 / 7);
    // more are refused at once with "Too many requests are waiting for the user"
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("queue.pdf"), 1));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentTab* otherTab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("other.pdf"), 1));
    QVERIFY(otherTab);
    QTRY_VERIFY(otherTab->getDocument()->isReady());

    // The read approval first (Read = Ask): one question, allowed
    std::shared_ptr<pdf::PDFMcpCall> read = fixture.startCall(QStringLiteral("document"), AppMcpTestHelper::arguments(R"({"action":"get_info"})"));
    QVERIFY(fixture.confirmDialog());
    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
    QVERIFY(pdftest::AppMcpFixture::waitResult(read));

    const auto createNote = [](const QString& documentId)
    {
        QJsonObject arguments = AppMcpTestHelper::arguments(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":50,"y":200},"contents":"Queued"}]}})");
        arguments.insert(QStringLiteral("documentId"), documentId);
        return arguments;
    };

    std::vector<std::shared_ptr<pdf::PDFMcpCall>> calls;
    for (int i = 0; i < pdf::PDFAppMcpHost::MAXIMUM_CONFIRMATIONS_PER_TAB; ++i)
    {
        calls.push_back(fixture.startCall(QStringLiteral("annotations"), createNote(document->getId())));
    }
    QTRY_COMPARE(int(fixture.confirmations()->waiting.size()), pdf::PDFAppMcpHost::MAXIMUM_CONFIRMATIONS_PER_TAB);
    QVERIFY(fixture.host()->isConfirmationQueueFull(document));
    QVERIFY(!fixture.host()->isConfirmationQueueFull(otherTab->getDocument()));
    QVERIFY(!fixture.host()->isConfirmationQueueFull(nullptr));

    // The 17th change and a save of the same tab are refused without a question
    for (const QString& tool : { QStringLiteral("annotations"), QStringLiteral("files") })
    {
        QJsonObject arguments = createNote(document->getId());
        if (tool == QLatin1String("files"))
        {
            arguments = AppMcpTestHelper::arguments(R"({"action":"save"})");
            arguments.insert(QStringLiteral("documentId"), document->getId());
        }
        std::shared_ptr<pdf::PDFMcpCall> refused = fixture.startCall(tool, arguments);
        std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(refused);
        QVERIFY(result);
        QVERIFY(result->isError);
        QVERIFY2(result->getFirstText().startsWith(QStringLiteral("Too many requests are waiting for the user")), qPrintable(result->getFirstText()));
        QCOMPARE(int(fixture.confirmations()->waiting.size()), pdf::PDFAppMcpHost::MAXIMUM_CONFIRMATIONS_PER_TAB);
    }

    // Another tab has its own queue
    std::shared_ptr<pdf::PDFMcpCall> other = fixture.startCall(QStringLiteral("annotations"), createNote(otherTab->getDocument()->getId()));
    QTRY_COMPARE(int(fixture.confirmations()->waiting.size()), pdf::PDFAppMcpHost::MAXIMUM_CONFIRMATIONS_PER_TAB + 1);

    // An answer frees a place of the queue
    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));
    QTRY_VERIFY(!fixture.host()->isConfirmationQueueFull(document));
    calls.push_back(fixture.startCall(QStringLiteral("annotations"), createNote(document->getId())));
    QTRY_VERIFY(fixture.host()->isConfirmationQueueFull(document));

    while (fixture.answer(pdf::PDFAppMcpDecision::DeniedUser))
    {
    }
    calls.push_back(other);
    for (const std::shared_ptr<pdf::PDFMcpCall>& call : calls)
    {
        QVERIFY(pdftest::AppMcpFixture::waitResult(call));
    }
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
}

void AppMcpTest::reviewReadApprovalCanceledAfterDecision()
{
    // Review RC7-H: a call canceled after its read approval was answered and
    // before the decision was delivered was never finished (the dispatcher waited
    // its grace time, no activity row); a stopped server or a canceled call is
    // not reported as a denial of the user
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("cancel.pdf"), 1));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());

    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("document"), AppMcpTestHelper::arguments(R"({"action":"get_info"})"));
    QVERIFY(fixture.confirmDialog());
    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
    call->cancel(pdf::PDFMcpCancelReason::ClientCanceled);
    std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call, 5000);
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("canceled")), qPrintable(result->getFirstText()));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Canceled));

    // The approval of the first question is forgotten: each next call asks again
    const std::pair<pdf::PDFAppMcpDecision, const char*> decisions[] = {
        { pdf::PDFAppMcpDecision::DeniedStopped, "stopping" },
        { pdf::PDFAppMcpDecision::DeniedDisconnected, "canceled" },
        { pdf::PDFAppMcpDecision::DeniedUser, "denied" } };
    for (const auto& decision : decisions)
    {
        fixture.host()->getPolicy().forgetRead(call->getRequest().client.assistantKey);
        std::shared_ptr<pdf::PDFMcpCall> denied = fixture.startCall(QStringLiteral("document"), AppMcpTestHelper::arguments(R"({"action":"get_info"})"));
        QVERIFY(fixture.confirmDialog());
        QVERIFY(fixture.answer(decision.first));
        result = pdftest::AppMcpFixture::waitResult(denied, 5000);
        QVERIFY(result);
        QVERIFY(result->isError);
        QVERIFY2(result->getFirstText().contains(QLatin1String(decision.second)), qPrintable(result->getFirstText()));
    }
}

void AppMcpTest::reviewActivityStatusAfterLongWait()
{
    // Review RC7-H: a written row was forgotten 60 s after it was recorded - the
    // decision of a confirmation answered after 70 s and the final status of the
    // change after it were dropped (the audit showed Allowed for a failed change)
    pdftest::AppMcpFixture fixture;
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    pdf::PDFAppMcpActivityLog log(database);
    const qint64 start = 4000000;
    log.setCurrentTime(start);

    pdf::PDFAppMcpActivityEntry entry;
    entry.assistantKey = QStringLiteral("a:1");
    entry.client = QStringLiteral("Claude Code");
    entry.action = QStringLiteral("files.save_copy");
    entry.document = QStringLiteral("Report.pdf");
    entry.documentId = QStringLiteral("doc");
    entry.status = pdf::PDFMcpActivityStatus::Waiting;
    const qint64 waiting = log.record(entry);
    log.flush();

    // The user answers after 70 s; another assistant reads meanwhile
    log.setCurrentTime(start + 70000);
    log.update(waiting, pdf::PDFMcpActivityStatus::Allowed);
    log.flush();
    log.setCurrentTime(start + 71000);
    pdf::PDFAppMcpActivityEntry read = entry;
    read.assistantKey = QStringLiteral("a:2");
    read.action = QStringLiteral("text.get_text");
    read.status = pdf::PDFMcpActivityStatus::Ok;
    QVERIFY(log.record(read) > 0);

    // The write fails two minutes later
    log.setCurrentTime(start + 190000);
    QVERIFY(log.record(read) > 0);
    log.update(waiting, pdf::PDFMcpActivityStatus::Failed, QJsonObject({ { QStringLiteral("reason"), QStringLiteral("disk full") } }));
    log.flush();

    bool isFailed = false;
    for (const pdf::PDFMcpActivityRow& row : pdf::PDFMcpRepository(database).getActivity(100))
    {
        if (row.action == QLatin1String("files.save_copy"))
        {
            QCOMPARE(row.status, pdf::PDFMcpActivityStatus::Failed);
            QCOMPARE(row.detail.value(QStringLiteral("reason")).toString(), QStringLiteral("disk full"));
            isFailed = true;
        }
    }
    QVERIFY(isFailed);

    // A row, which has not changed for KEEP_TIME, is forgotten (bounded memory)
    log.setCurrentTime(start + 190000 + pdf::PDFAppMcpActivityLog::KEEP_TIME + 1000);
    QVERIFY(log.record(read) > 0);
    log.update(waiting, pdf::PDFMcpActivityStatus::Ok);
    QCOMPARE(log.getPendingCount(), 1);
}

void AppMcpTest::reviewEverythingChangedRestartsServer()
{
    // Review RC7-H: everythingChanged (an imported backup) applied only the
    // permissions - a server kept running without the token required by the
    // settings, which the AI page already showed
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.startServer());
    pdf::PDFAppMcpController* controller = fixture.controller();
    QTRY_VERIFY(controller->isOwner());
    const bool requireToken = controller->getServer()->getConfig().requireToken;

    // The setting written without its own notification (as a whole import)
    pdf::PDFDatabaseNotifier* notifier = fixture.context->getNotifier();
    notifier->blockSignals(true);
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::mcpRequireToken(), !requireToken).isError());
    notifier->poll();
    notifier->blockSignals(false);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(controller->getServer()->getConfig().requireToken, requireToken);

    Q_EMIT notifier->everythingChanged();
    QTRY_COMPARE(controller->getServer()->getConfig().requireToken, !requireToken);
    QTRY_VERIFY(controller->isRunning());
    QVERIFY(controller->isOwner());
}

void AppMcpTest::reviewCatalogSaveCopyRecheckedAfterConfirmation()
{
    // Review RC7-H: save_copy of a catalog tool checked the rules of the path only
    // when the call came - a folder replaced while the confirmation waited (up to
    // 2 minutes) was not checked again; a stopped server or a disconnected
    // assistant was reported as a denial of the user
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("source.pdf"), 1));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());
    AppMcpTestHelper::ToolRegistration registration;
    fixture.host()->checkToolRegistry();

    QVERIFY(QDir(fixture.folder()).mkpath(QStringLiteral("target")));
    QJsonObject saveCopy = AppMcpTestHelper::arguments(R"({"action":"statistics","result":"save_copy","statistics":{"mode":"title","title":"Copy"}})");
    saveCopy.insert(QStringLiteral("path"), fixture.path(QStringLiteral("target/copy.pdf")));
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("review"), saveCopy);
    QVERIFY(fixture.confirmDialog());
    QVERIFY(QDir(fixture.path(QStringLiteral("target"))).removeRecursively());

    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
    std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().startsWith(QStringLiteral("path: ")), qPrintable(result->getFirstText()));
    QVERIFY(!QFileInfo::exists(fixture.path(QStringLiteral("target/copy.pdf"))));

    // The texts of the decisions, which are not a denial of the user
    saveCopy.insert(QStringLiteral("path"), fixture.path(QStringLiteral("other.pdf")));
    call = fixture.startCall(QStringLiteral("review"), saveCopy);
    QVERIFY(fixture.confirmDialog());
    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedDisconnected));
    result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("disconnected")), qPrintable(result->getFirstText()));

    // A folder of the path replaced by a link to another folder while the
    // confirmation waits: a removed folder alone fails at the write anyway - the
    // link would be written through, only the check of the path refuses it
    // (fix wave FW7-H, item 25: the revert of the fix passed the first part)
    QVERIFY(QDir(fixture.folder()).mkpath(QStringLiteral("linked")));
    QVERIFY(QDir(fixture.folder()).mkpath(QStringLiteral("elsewhere")));
    saveCopy.insert(QStringLiteral("path"), fixture.path(QStringLiteral("linked/copy.pdf")));
    call = fixture.startCall(QStringLiteral("review"), saveCopy);
    QVERIFY(fixture.confirmDialog());
    QVERIFY(QDir(fixture.path(QStringLiteral("linked"))).removeRecursively());
    if (!AppMcpTestHelper::createDirectoryLink(fixture.path(QStringLiteral("elsewhere")), fixture.path(QStringLiteral("linked"))))
    {
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));
        QVERIFY(pdftest::AppMcpFixture::waitResult(call));
        QSKIP("A link of a folder cannot be created here");
    }
    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
    result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().startsWith(QStringLiteral("path: ")), qPrintable(result->getFirstText()));
    QVERIFY(!QFileInfo::exists(fixture.path(QStringLiteral("elsewhere/copy.pdf"))));
    QFile::remove(fixture.path(QStringLiteral("linked")));
    QDir().rmdir(fixture.path(QStringLiteral("linked")));
}

void AppMcpTest::reviewIndexWaiterCanceled()
{
    // Review RC7-H: a search canceled while it waited for the text index of the
    // tab was never finished (the waiter is not a job, nobody else answers it)
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::large(fixture.folder(), 300));
    QVERIFY(tab);
    QTRY_VERIFY_WITH_TIMEOUT(tab->getDocument()->isReady(), 30000);
    QVERIFY(!tab->getDocument()->getTextIndex());

    QJsonObject arguments = AppMcpTestHelper::arguments(R"({"action":"search","search":{"query":"lazy dog"}})");
    arguments.insert(QStringLiteral("documentId"), tab->getDocument()->getId());
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("text"), arguments);
    QVERIFY(!call->isFinished());
    call->cancel(pdf::PDFMcpCancelReason::ClientCanceled);

    std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call, 5000);
    QVERIFY(result);
    QVERIFY(result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("canceled")), qPrintable(result->getFirstText()));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Canceled));
}

/// A job of the test: a heavy one waits for its release, a light one ends at once
class AppMcpTestJob : public pdf::PDFAppMcpJob
{
public:
    AppMcpTestJob(std::shared_ptr<pdf::PDFMcpCall> call, bool isHeavy, std::shared_ptr<std::atomic<bool>> release) :
        pdf::PDFAppMcpJob(std::move(call), pdf::PDFAppDocumentSnapshot()),
        m_isHeavy(isHeavy),
        m_release(std::move(release))
    {

    }

    virtual bool isHeavy() const override { return m_isHeavy; }

    virtual void execute() override
    {
        while (m_isHeavy && !m_release->load())
        {
            QThread::msleep(5);
        }
        getCall()->finish(pdf::PDFMcpCallResult::createToolError(QStringLiteral("done")));
    }

private:
    bool m_isHeavy = false;
    std::shared_ptr<std::atomic<bool>> m_release;
};

/// Releases the heavy jobs of the test also when it fails (the pool is waited for)
class AppMcpTestJobRelease
{
public:
    explicit AppMcpTestJobRelease(std::shared_ptr<std::atomic<bool>> flag) : m_flag(std::move(flag)) { }
    ~AppMcpTestJobRelease() { m_flag->store(true); }

private:
    std::shared_ptr<std::atomic<bool>> m_flag;
};

void AppMcpTest::reviewHeavyJobsNeverBlockWorkers()
{
    // Review RC7-H: a heavy job above MAXIMUM_HEAVY_JOBS waited for its place in
    // a worker of the pool - renders of one assistant blocked every worker and
    // the light reads of all assistants waited behind them ("nobody blocks")
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppMcpHost* host = fixture.host();
    QVERIFY(host);
    host->getThreadPool()->setMaxThreadCount(pdf::PDFMcpLimits::MAXIMUM_HEAVY_JOBS + 1);

    std::shared_ptr<std::atomic<bool>> release = std::make_shared<std::atomic<bool>>(false);
    AppMcpTestJobRelease releaseGuard(release);
    auto createCall = [&fixture]() { return pdf::PDFMcpCall::create(fixture.createRequest(QStringLiteral("document"), AppMcpTestHelper::arguments(R"({"action":"get_info"})"))); };

    std::vector<std::shared_ptr<pdf::PDFMcpCall>> heavyCalls;
    for (int i = 0; i < pdf::PDFMcpLimits::MAXIMUM_HEAVY_JOBS + 2; ++i)
    {
        heavyCalls.push_back(createCall());
        host->startJob(std::make_shared<AppMcpTestJob>(heavyCalls.back(), true, release));
    }

    // The light job gets the free worker at once
    std::shared_ptr<pdf::PDFMcpCall> light = createCall();
    host->startJob(std::make_shared<AppMcpTestJob>(light, false, release));
    QVERIFY(pdftest::AppMcpFixture::waitResult(light, 5000));
    for (const std::shared_ptr<pdf::PDFMcpCall>& call : heavyCalls)
    {
        QVERIFY(!call->isFinished());
    }
    QCOMPARE(host->getRunningHeavyJobCount(), pdf::PDFMcpLimits::MAXIMUM_HEAVY_JOBS);
    QCOMPARE(host->getWaitingHeavyJobCount(), 2);

    // Released: the waiting ones run in their turn
    release->store(true);
    for (const std::shared_ptr<pdf::PDFMcpCall>& call : heavyCalls)
    {
        QVERIFY(pdftest::AppMcpFixture::waitResult(call, 10000));
    }
    QTRY_COMPARE(host->getRunningJobCount(), 0);
    QCOMPARE(host->getRunningHeavyJobCount(), 0);
    QCOMPARE(host->getWaitingHeavyJobCount(), 0);

    // At the stop the waiting heavy jobs end without running
    release->store(false);
    heavyCalls.clear();
    for (int i = 0; i < pdf::PDFMcpLimits::MAXIMUM_HEAVY_JOBS + 1; ++i)
    {
        heavyCalls.push_back(createCall());
        host->startJob(std::make_shared<AppMcpTestJob>(heavyCalls.back(), true, release));
    }
    QCOMPARE(host->getWaitingHeavyJobCount(), 1);
    release->store(true);
    host->shutdown();
    QVERIFY(heavyCalls.back()->isFinished());
    QCOMPARE(heavyCalls.back()->getResult()->errorCode, pdf::PDFMcpErrorCodes::SERVER_STOPPING);
}

void AppMcpTest::reviewUntrustedPlaceholders()
{
    // Followups item 2 (review RC7-H): an untrusted text was inserted before the
    // next .arg() - its "%1" was replaced by the page count
    QString error;
    QVERIFY(!pdf::PDFAppMcpArguments::parsePages(QStringLiteral("%1-%2"), 10, 0, &error).has_value());
    QVERIFY2(error.startsWith(QStringLiteral("invalid page range \"%1-%2\" (the document has 10 pages)")), qPrintable(error));

    // The name of an argument of a prompt
    pdftest::AppMcpFixture fixture;
    pdf::PDFMcpCallRequest request = fixture.createRequest(QString(), QJsonObject());
    request.kind = pdf::PDFMcpCallKind::PromptGet;
    request.name = QStringLiteral("summarize-document");
    request.arguments = QJsonObject({ { QStringLiteral("%1x"), 5 } });
    std::shared_ptr<pdf::PDFMcpCall> call = pdf::PDFMcpCall::create(request);
    fixture.host()->handleCall(call);
    std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result);
    QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::Error);
    QVERIFY2(result->errorMessage.startsWith(QStringLiteral("Invalid argument %1x: ")), qPrintable(result->errorMessage));
}

void AppMcpTest::reviewSubscribeWithoutListing()
{
    // Followups item 5 (review RC7-H): the watcher of the documents exists with
    // the host, so subscriptions/listen and resources/subscribe of a document
    // resource are updated without any resources/list before them
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    QVERIFY(pdf::PDFAppMcpDocumentWatcher::find(fixture.host()));
    QVERIFY(fixture.startServer());
    pdf::PDFAppDocumentTab* tab = AppMcpReadTests::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("watched.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const QString infoUri = QStringLiteral("pdf4qt://documents/%1/info").arg(document->getId());

    std::unique_ptr<pdftest::McpTestClient> client = fixture.client(pdftest::McpTestEra::Modern);
    QJsonObject message = client->createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject({
        { QStringLiteral("resourceSubscriptions"), QJsonArray({ infoUri }) } }) } }));
    std::unique_ptr<pdftest::McpTestStream> stream = client->openStream(message);
    QVERIFY(stream);
    QVERIFY(stream->waitForMessages(1, 5000 * pdftest::AppDocumentFixture::getTimeoutFactor()));

    std::unique_ptr<pdftest::McpTestClient> legacy = fixture.client(pdftest::McpTestEra::Legacy20251125);
    legacy->initialize();
    QVERIFY(legacy->request(QStringLiteral("resources/subscribe"), QJsonObject({ { QStringLiteral("uri"), infoUri } })).contains(QStringLiteral("result")));
    std::unique_ptr<pdftest::McpTestStream> getStream = legacy->openGetStream();
    QVERIFY(getStream);
    QVERIFY(getStream->waitForComments(1, 5000 * pdftest::AppDocumentFixture::getTimeoutFactor()));

    pdf::PDFDocumentBuilder builder(document->getDocument().data());
    builder.setDocumentTitle(QStringLiteral("Watched"));
    pdf::PDFDocumentPointer changed(new pdf::PDFDocument(builder.build()));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::None), QStringLiteral("Title"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QVERIFY2(AppMcpReadTests::waitForStream(*stream, infoUri), qPrintable(AppMcpReadTests::streamMethods(*stream).join(QStringLiteral(", "))));
    QVERIFY2(AppMcpReadTests::waitForStream(*getStream, infoUri), qPrintable(AppMcpReadTests::streamMethods(*getStream).join(QStringLiteral(", "))));
}

void AppMcpTest::reviewStateWithoutReadHidesDocuments()
{
    // Review RC7-H: the state without the read permission omitted the documents,
    // but its activeRoute named the document (its id, the search text of the user)
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("state.pdf"), 1));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());
    const QString id = tab->getDocument()->getId();

    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("application"), AppMcpTestHelper::arguments(R"({"action":"get_state"})"));
    QVERIFY(result);
    QVERIFY2(AppMcpTestHelper::structured(*result).value(QStringLiteral("get_state")).toObject().value(QStringLiteral("activeRoute")).toString().contains(id),
             qPrintable(result->getFirstText()));

    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Deny));
    result = fixture.callTool(QStringLiteral("application"), AppMcpTestHelper::arguments(R"({"action":"get_state"})"));
    QVERIFY(result);
    const QJsonObject state = AppMcpTestHelper::structured(*result).value(QStringLiteral("get_state")).toObject();
    QVERIFY(!state.contains(QStringLiteral("documents")));
    QCOMPARE(state.value(QStringLiteral("activeRoute")).toString(), QStringLiteral("/document"));
    QVERIFY(!result->getFirstText().contains(id));
}

void AppMcpTest::fixOpenOfOpenFileAsksRead()
{
    // Fix wave FW7-H, item 26 (owner decision A): files.open of a file, which is
    // open already, reads its document (id, title, pages) - with Read = Ask it
    // asks the read approval of the assistant first, one approval shared with
    // every other read; Deny refuses as before
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Already_Open.pdf"), 3));
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument()->isReady());
    const QString id = tab->getDocument()->getId();

    QJsonObject open;
    open.insert(QStringLiteral("action"), QStringLiteral("open"));
    open.insert(QStringLiteral("open"), QJsonObject({ { QStringLiteral("path"), tab->getDocument()->getFilePath() }, { QStringLiteral("page"), 2 } }));

    // Ask: the question of the read approval; Deny answers only this request
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("files"), open);
    std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmDialog();
    QVERIFY(confirmation);
    QCOMPARE(confirmation->kind, pdf::PDFAppMcpConfirmationKind::ReadDocuments);
    QCOMPARE(confirmation->actionId, QStringLiteral("files.open"));
    QCOMPARE(confirmation->document.data(), tab->getDocument());
    QVERIFY(!call->isFinished());
    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));
    std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result && result->isError);
    QVERIFY2(!result->getFirstText().contains(id), qPrintable(result->getFirstText()));
    QVERIFY(!result->getFirstText().contains(QStringLiteral("Already_Open")));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedUser));

    // Allow: the tab is activated at its page, the approval covers the next reads
    call = fixture.startCall(QStringLiteral("files"), open);
    QVERIFY(fixture.confirmDialog());
    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
    result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY2(result && !result->isError, result ? qPrintable(result->getFirstText()) : "no result");
    QCOMPARE(AppMcpTestHelper::structured(*result).value(QStringLiteral("open")).toObject().value(QStringLiteral("documentId")).toString(), id);
    QCOMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(1));
    QVERIFY(fixture.host()->getPolicy().isReadApproved(call->getRequest().client.assistantKey));
    const size_t shown = fixture.confirmations()->shown.size();
    QVERIFY(!fixture.callTool(QStringLiteral("files"), open)->isError);
    QVERIFY(!fixture.callTool(QStringLiteral("document"), AppMcpTestHelper::arguments(R"({"action":"get_info"})"))->isError);
    QCOMPARE(fixture.confirmations()->shown.size(), shown);

    // An approval of a read tool covers files.open of another assistant? No - its own
    fixture.setClient(QStringLiteral("gemini-cli-mcp-client"));
    call = fixture.startCall(QStringLiteral("files"), open);
    QVERIFY(fixture.confirmDialog());
    QCOMPARE(fixture.confirmations()->shown.size(), shown + 1);
    QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedTimeout));
    result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("2 minutes")), qPrintable(result->getFirstText()));

    // Deny: refused at once without a question
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Deny));
    result = fixture.callTool(QStringLiteral("files"), open);
    QVERIFY(result && result->isError);
    QVERIFY2(result->getFirstText().contains(QStringLiteral("Deny")), qPrintable(result->getFirstText()));
    QCOMPARE(fixture.confirmations()->shown.size(), shown + 1);

    // The tab closed while the question waits: no activation of a closed document
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Ask));
    fixture.setClient(QStringLiteral("codex-mcp-client"));
    call = fixture.startCall(QStringLiteral("files"), open);
    QVERIFY(fixture.confirmDialog());
    QVERIFY(fixture.window->closeTab(tab));
    pdftest::AppDocumentFixture::settle();
    if (!call->isFinished())
    {
        fixture.answer(pdf::PDFAppMcpDecision::Allowed);
    }
    result = pdftest::AppMcpFixture::waitResult(call);
    QVERIFY(result && result->isError);
}

void AppMcpTest::fixListedTitlesWhileReadAsk()
{
    // Fix wave FW7-H, item 27 (owner decision 2026-10-27): the catalog is shared
    // by every assistant - with Read = Ask resources/list names the open documents
    // without their titles (a file name is document data); Allow lists the titles
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = AppMcpReadTests::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Secret_Merger_Plan.pdf"), 1));
    QVERIFY(tab);
    const QString id = tab->getDocument()->getId();
    const QString infoUri = pdf::PDFAppMcpDocumentResources::getDocumentPrefix(id) + QStringLiteral("info");
    QTRY_VERIFY(fixture.host()->getCatalog()->findResource(infoUri));
    QVERIFY(fixture.host()->getCatalog()->findResource(infoUri)->title.contains(QStringLiteral("Secret_Merger_Plan")));

    auto isTitleListed = [&fixture]()
    {
        for (const pdf::PDFMcpResourceDefinition& resource : fixture.host()->getCatalog()->getResources())
        {
            if (resource.title.contains(QStringLiteral("Secret_Merger")) || resource.name.contains(QStringLiteral("Secret_Merger")) ||
                resource.description.contains(QStringLiteral("Secret_Merger")))
            {
                return true;
            }
        }
        return false;
    };

    // Ask: the generic names (a new catalog - list_changed), the ids stay listed
    quint64 generation = fixture.host()->getCatalogGeneration();
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Ask));
    QTRY_VERIFY(fixture.host()->getCatalogGeneration() > generation);
    QVERIFY(!isTitleListed());
    const pdf::PDFMcpResourceDefinition* info = fixture.host()->getCatalog()->findResource(infoUri);
    QVERIFY(info);
    QVERIFY2(info->title.startsWith(pdf::PDFAppMcpDocumentResources::getGenericTitle(id)), qPrintable(info->title));
    QCOMPARE(pdf::PDFAppMcpDocumentResources::getGenericTitle(id), QStringLiteral("Open document %1").arg(id));

    // The serialized listing of the server (both eras) has no title either
    for (pdf::PDFMcpEra era : { pdf::PDFMcpEra::Modern, pdf::PDFMcpEra::Legacy20251125 })
    {
        const QByteArray listing = fixture.host()->getCatalog()->getResourcesJson(era);
        QVERIFY(!listing.isEmpty());
        QVERIFY(!listing.contains("Secret_Merger"));
        QVERIFY(listing.contains(id.toUtf8()));
    }

    // A change of the document, which does not change the listing, publishes no
    // catalog (the watcher follows the catalogs built for the permissions)
    pdf::PDFAppMcpDocumentWatcher* watcher = pdf::PDFAppMcpDocumentWatcher::find(fixture.host());
    QVERIFY(watcher);
    generation = fixture.host()->getCatalogGeneration();
    const int rebuilds = watcher->getRebuildCount();
    tab->getDocument()->setLocked(true);
    QTest::qWait(100);
    QTRY_VERIFY(!watcher->isRebuildPending());
    QCOMPARE(watcher->getRebuildCount(), rebuilds);
    QCOMPARE(fixture.host()->getCatalogGeneration(), generation);
    tab->getDocument()->setLocked(false);

    // Completion withholds the titles and the ids before the approval
    const QJsonObject completionRequest = AppMcpTestHelper::arguments(R"({"ref":{"type":"ref/prompt","name":"summarize-document"},"argument":{"name":"document","value":""}})");
    pdf::PDFMcpCallRequest request = fixture.createRequest(QString(), QJsonObject());
    request.kind = pdf::PDFMcpCallKind::Completion;
    request.arguments = completionRequest;
    std::shared_ptr<pdf::PDFMcpCall> completion = pdf::PDFMcpCall::create(request);
    fixture.host()->handleCall(completion);
    std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(completion);
    QVERIFY(result);
    QVERIFY(!QString::fromUtf8(result->result).contains(QStringLiteral("Secret_Merger")));

    // Allow: the titles again
    generation = fixture.host()->getCatalogGeneration();
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Allow));
    QTRY_VERIFY(fixture.host()->getCatalogGeneration() > generation);
    QVERIFY(isTitleListed());

    // Deny: no document at all
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Deny));
    QTRY_VERIFY(!fixture.host()->getCatalog()->findResource(infoUri));
}

/// Receiver of a confirmation of the test: records the decisions
class AppMcpTestReceiver : public pdf::PDFAppMcpConfirmationReceiver
{
public:
    virtual void confirmationFinished(quint64 confirmationId, pdf::PDFAppMcpDecision decision) override
    {
        Q_UNUSED(confirmationId);
        decisions.push_back(decision);
    }

    std::vector<pdf::PDFAppMcpDecision> decisions;
};

void AppMcpTest::fixConfirmationAfterShutdown()
{
    // Fix wave FW7-H, item 45 (review RT7): a confirmation requested from a host,
    // which is shut down already, was denied by a queued call - the controller
    // deletes such a host before the event loop runs again, the decision was
    // dropped and the receiver (kept alive by itself while it waits) never ended
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppMcpHost* host = new pdf::PDFAppMcpHost(fixture.context.get());
    QPointer<pdf::PDFAppMcpHost> hostGuard(host);
    host->shutdown();

    std::shared_ptr<AppMcpTestReceiver> receiver = std::make_shared<AppMcpTestReceiver>();
    std::shared_ptr<pdf::PDFMcpCall> call = pdf::PDFMcpCall::create(fixture.createRequest(QStringLiteral("document"), AppMcpTestHelper::arguments(R"({"action":"get_info"})")));
    std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = std::make_shared<pdf::PDFAppMcpConfirmation>();
    confirmation->kind = pdf::PDFAppMcpConfirmationKind::ReadDocuments;
    confirmation->permissions = pdf::PDFAppMcpPermissionKind::Read;
    confirmation->call = call;
    confirmation->receiver = receiver;
    confirmation->receiverHolder = receiver;
    std::weak_ptr<pdf::PDFAppMcpConfirmation> weakConfirmation = confirmation;

    QVERIFY(host->requestConfirmation(confirmation) > 0);
    QCOMPARE(receiver->decisions.size(), size_t(1));
    QCOMPARE(receiver->decisions.front(), pdf::PDFAppMcpDecision::DeniedStopped);
    QVERIFY(host->getPendingConfirmations().empty());
    QVERIFY(!confirmation->receiverHolder);

    // Deleted before the event loop: nothing is delivered twice, nothing leaks
    delete host;
    confirmation.reset();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!hostGuard);
    QCOMPARE(receiver->decisions.size(), size_t(1));
    QVERIFY(weakConfirmation.expired());
}

void AppMcpTest::fixJunctionsResolved()
{
    // Fix wave FW7-H: QFileInfo resolves symbolic links, not the junctions of NTFS
    // (mklink /J needs no privilege) - the rules of the paths see the folder, to
    // which a junction leads (a junction to the data of PDF4QT was not refused)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkpath(QStringLiteral("elsewhere")));
    const QString elsewhere = QFileInfo(directory.filePath(QStringLiteral("elsewhere"))).canonicalFilePath();
    if (!AppMcpTestHelper::createDirectoryLink(directory.filePath(QStringLiteral("elsewhere")), directory.filePath(QStringLiteral("linked"))))
    {
        QSKIP("A link of a folder cannot be created here");
    }

    QString error;
    const QString checked = pdf::PDFAppMcpArguments::checkPath(directory.filePath(QStringLiteral("linked/a.pdf")), true, &error);
    QVERIFY2(!checked.isEmpty(), qPrintable(error));
    QCOMPARE(QDir::fromNativeSeparators(checked).toLower(), (elsewhere + QStringLiteral("/a.pdf")).toLower());

    // An existing file behind the junction, and a junction of a junction
    QFile file(directory.filePath(QStringLiteral("elsewhere/b.pdf")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArrayLiteral("%PDF-1.7 test"));
    file.close();
    QVERIFY(AppMcpTestHelper::createDirectoryLink(directory.filePath(QStringLiteral("linked")), directory.filePath(QStringLiteral("linked2"))));
    const QString nested = pdf::PDFAppMcpArguments::checkPath(directory.filePath(QStringLiteral("linked2/b.pdf")), true, &error);
    QCOMPARE(QDir::fromNativeSeparators(nested).toLower(), (elsewhere + QStringLiteral("/b.pdf")).toLower());

    // A junction without its target is refused
    QVERIFY(QDir(directory.filePath(QStringLiteral("elsewhere"))).removeRecursively());
    QVERIFY(pdf::PDFAppMcpArguments::checkPath(directory.filePath(QStringLiteral("linked/a.pdf")), true, &error).isEmpty());
}

void AppMcpTest::fixOwnerRequests()
{
    // Fix wave FW7-H, item 47: the requests of other instances to the owner of
    // the server (plan 2.6) - Show Other Window restores a minimized window,
    // Disconnect of an unknown key and Disconnect All, each taken once
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.startServer());
    pdf::PDFAppMcpController* controller = fixture.controller();
    QTRY_VERIFY(controller->isOwner());
    QSignalSpy showSpy(controller, &pdf::PDFAppMcpController::showWindowRequested);

    fixture.window->showMinimized();
    pdftest::AppDocumentFixture::settle();
    pdf::PDFMcpRepository repository(fixture.context->getDatabase());
    QVERIFY(!repository.postRequest(pdf::PDFMcpRequestKind::ShowWindow).isError());
    QTRY_COMPARE(showSpy.count(), 1);
    QTRY_VERIFY(!fixture.window->isMinimized());

    QVERIFY(!repository.postRequest(pdf::PDFMcpRequestKind::Disconnect, QStringLiteral("a:0123456789abcdef0123456789abcdef")).isError());
    QVERIFY(!repository.postRequest(pdf::PDFMcpRequestKind::DisconnectAll).isError());
    QTRY_VERIFY(repository.takeRequests().empty());
    pdftest::AppDocumentFixture::settle();
    QVERIFY(controller->isRunning());
    QCOMPARE(showSpy.count(), 1);
}

int main(int argc, char* argv[])
{
    // Child processes of the tests of two instances
    const int childResult = pdftest::AppMcpFixture::runChild(argc, argv);
    if (childResult >= 0)
    {
        return childResult;
    }

    QApplication application(argc, argv);
    application.setAttribute(Qt::AA_Use96Dpi, true);
    AppMcpTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_appmcptest.moc"
