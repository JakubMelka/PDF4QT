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

#include "pdfappcontext.h"
#include "pdfapphelpcatalog.h"
#include "pdfappmcpactivity.h"
#include "pdfappmcpclientnames.h"
#include "pdfappmcpclientsetup.h"
#include "pdfappmcpconfirmationui.h"
#include "pdfappmcpcontent.h"
#include "pdfappmcphost.h"
#include "pdfappmcpprompts.h"
#include "pdfappmcpresources.h"
#include "pdfappmcptoolset.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingdefinitions.h"

#include "pdfmcpjson.h"
#include "pdfmcplimits.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtTest>

#include <set>

// Package D of step 7 (plan 9 D): the docs, skills and workflows for the
// assistants, the prompts, the instructions, the setup texts of the clients and
// the help topics of the AI page. The reference tables of docs/tools/<tool>.md
// are generated from the contract; PDF4QT_MCP_DOCS_DUMP=<folder> writes the
// generated reference of every tool there (to update the files after a change
// of the contract).
class AppMcpContentTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void instructions();
    void staticResources();
    void staticResourcesHttp();
    void docsLinksAndNames();
    void examplesValidate();
    void skillsFormat();
    void workflowsEqualPrompts();
    void promptTextHostile();
    void setupTexts();
    void setupTextsHostile();
    void helpTopicsMatchCode();

private:
    /// Every file of the content for the assistants (not the help topics): uri → text
    static std::vector<std::pair<QString, QString>> getContentFiles();

    /// Names, which the contract knows (properties, enum values, actions, tools, prompts)
    static std::set<QString> getContractNames();
    static void collectNames(const pdf::PDFMcpSchema& schema, std::set<QString>& names);

    /// Section "## <title>" of a Markdown text (up to the next heading of the same level)
    static QString getSection(const QString& markdown, const QString& title);

    /// Input schema of a phase 1 tool with every action available
    static pdf::PDFMcpSchema getFullInputSchema(const QString& toolName);

    /// Minimal TOML check of the Codex configuration: a table header, key = value
    /// lines with basic strings, integers or inline tables of basic strings
    static bool isWellFormedToml(const QString& text, QString* error);

    static QString readResourceFile(const QString& path);
};

void AppMcpContentTest::initTestCase()
{
    // The content is read in the thread of the user interface (the help catalog)
    QVERIFY(!pdf::PDFAppMcpContent::getStaticResources().empty());

    // The generated reference for the maintainers of the docs
    const QString dumpDirectory = qEnvironmentVariable("PDF4QT_MCP_DOCS_DUMP");
    if (!dumpDirectory.isEmpty())
    {
        QDir().mkpath(dumpDirectory);
        for (const pdf::PDFAppMcpToolSpec& tool : pdf::PDFAppMcpToolSet::getTools())
        {
            QFile file(QDir(dumpDirectory).filePath(tool.name + QStringLiteral(".md")));
            QVERIFY(file.open(QFile::WriteOnly | QFile::Truncate));
            QString text = pdf::PDFAppMcpContent::createToolSummary(tool);
            for (const pdf::PDFAppMcpActionSpec& action : tool.actions)
            {
                text += QStringLiteral("\n@@ACTION %1@@\n").arg(action.name);
                text += pdf::PDFAppMcpContent::createActionReference(tool, action);
            }
            file.write(text.toUtf8());
        }
    }
}

QString AppMcpContentTest::readResourceFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QFile::ReadOnly))
    {
        return QString();
    }
    // The files are stored with CR LF and served with LF
    return QString::fromUtf8(file.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
}

std::vector<std::pair<QString, QString>> AppMcpContentTest::getContentFiles()
{
    std::vector<std::pair<QString, QString>> files;
    for (const pdf::PDFMcpResourceDefinition& resource : pdf::PDFAppMcpContent::getStaticResources())
    {
        if (!resource.uri.startsWith(QStringLiteral("pdf4qt://help/")))
        {
            files.emplace_back(resource.uri, QString::fromUtf8(resource.content));
        }
    }
    return files;
}

void AppMcpContentTest::collectNames(const pdf::PDFMcpSchema& schema, std::set<QString>& names)
{
    for (const QString& value : schema.getEnum())
    {
        names.insert(value);
    }
    for (const auto& property : schema.getProperties())
    {
        names.insert(property.first);
        collectNames(property.second, names);
    }
    if (schema.getItems())
    {
        collectNames(*schema.getItems(), names);
    }
}

std::set<QString> AppMcpContentTest::getContractNames()
{
    std::set<QString> names;
    for (const pdf::PDFAppMcpToolSpec& tool : pdf::PDFAppMcpToolSet::getTools())
    {
        names.insert(tool.name);
        for (const auto& property : tool.commonProperties)
        {
            names.insert(property.first);
            collectNames(property.second, names);
        }
        for (const pdf::PDFAppMcpActionSpec& action : tool.actions)
        {
            names.insert(action.name);
            collectNames(action.input, names);
            collectNames(action.output, names);
        }
    }
    for (const pdf::PDFMcpPromptDefinition& prompt : pdf::PDFAppMcpPrompts::createDefinitions({}))
    {
        names.insert(prompt.name);
        for (const pdf::PDFMcpPromptArgument& argument : prompt.arguments)
        {
            names.insert(argument.name);
        }
    }
    return names;
}

QString AppMcpContentTest::getSection(const QString& markdown, const QString& title)
{
    const QString heading = QStringLiteral("## ") + title + QLatin1Char('\n');
    const qsizetype start = markdown.indexOf(heading);
    if (start < 0)
    {
        return QString();
    }
    const qsizetype bodyStart = start + heading.size();
    qsizetype end = markdown.indexOf(QStringLiteral("\n## "), bodyStart);
    if (end < 0)
    {
        end = markdown.size();
    }
    return markdown.mid(bodyStart, end - bodyStart);
}

pdf::PDFMcpSchema AppMcpContentTest::getFullInputSchema(const QString& toolName)
{
    const pdf::PDFAppMcpToolSpec* tool = pdf::PDFAppMcpToolSet::findTool(toolName);
    if (!tool)
    {
        return pdf::PDFMcpSchema();
    }
    pdf::PDFAppMcpPermissions permissions;
    permissions.read = pdf::PDFAppMcpPermission::Allow;
    permissions.edit = pdf::PDFAppMcpPermission::Allow;
    permissions.files = pdf::PDFAppMcpPermission::Allow;
    return pdf::PDFAppMcpToolSet::createInputSchema(*tool, permissions);
}

bool AppMcpContentTest::isWellFormedToml(const QString& text, QString* error)
{
    static const QRegularExpression table(QStringLiteral("^\\[[A-Za-z0-9_]+(\\.[A-Za-z0-9_]+)*\\]$"));
    static const QRegularExpression basicString(QStringLiteral("^\"([^\"\\\\\\x00-\\x1F]|\\\\[\"\\\\nrtbf]|\\\\u[0-9A-Fa-f]{4})*\"$"));
    static const QRegularExpression key(QStringLiteral("^[A-Za-z0-9_-]+$"));
    static const QRegularExpression integer(QStringLiteral("^[+-]?(0|[1-9][0-9]*)$"));

    bool hasTable = false;
    std::set<QString> keys;
    for (const QString& line : text.split(QLatin1Char('\n')))
    {
        if (line.isEmpty())
        {
            continue;
        }
        if (line.startsWith(QLatin1Char('[')))
        {
            if (!table.match(line).hasMatch())
            {
                *error = QStringLiteral("table: ") + line;
                return false;
            }
            hasTable = true;
            keys.clear();
            continue;
        }

        const qsizetype equals = line.indexOf(QStringLiteral(" = "));
        if (equals <= 0 || !hasTable)
        {
            *error = QStringLiteral("line: ") + line;
            return false;
        }
        const QString name = line.left(equals);
        const QString value = line.mid(equals + 3);
        if (!key.match(name).hasMatch() || !keys.insert(name).second)
        {
            *error = QStringLiteral("key: ") + name;
            return false;
        }
        if (value.startsWith(QLatin1Char('{')))
        {
            // { "Key" = "value" } - one pair of basic strings
            if (!value.endsWith(QLatin1Char('}')))
            {
                *error = QStringLiteral("inline table: ") + value;
                return false;
            }
            const QString inner = value.mid(1, value.size() - 2).trimmed();
            const qsizetype innerEquals = inner.indexOf(QStringLiteral("\" = \""));
            if (innerEquals < 0 || !basicString.match(inner.left(innerEquals + 1)).hasMatch() || !basicString.match(inner.mid(innerEquals + 4)).hasMatch())
            {
                *error = QStringLiteral("inline table: ") + value;
                return false;
            }
        }
        else if (!basicString.match(value).hasMatch() && !integer.match(value).hasMatch())
        {
            *error = QStringLiteral("value: ") + value;
            return false;
        }
    }
    return hasTable;
}

void AppMcpContentTest::instructions()
{
    // Plan 4.4: the first 512 characters are self-contained (Codex shows only them)
    const QString instructions = pdf::PDFAppMcpResources::getInstructions();
    const QString head = instructions.left(512);
    QVERIFY(instructions.size() <= 1024);
    QVERIFY(head.contains(QStringLiteral("application.get_state")));
    QVERIFY(head.contains(QStringLiteral("documentId")));
    QVERIFY(head.contains(QStringLiteral("annotations.add")));
    QVERIFY(head.contains(QStringLiteral("untrusted")));
    QVERIFY(head.contains(QStringLiteral("pdf4qt://docs/index.md")));
    QVERIFY(pdf::PDFAppMcpContent::findStaticResource(QStringLiteral("pdf4qt://docs/index.md")));

    // The names of the instructions are names of the contract
    const pdf::PDFAppMcpToolSpec* application = pdf::PDFAppMcpToolSet::findTool(QStringLiteral("application"));
    const pdf::PDFAppMcpToolSpec* annotations = pdf::PDFAppMcpToolSet::findTool(QStringLiteral("annotations"));
    QVERIFY(application && application->findAction(QStringLiteral("get_state")));
    QVERIFY(annotations && annotations->findAction(QStringLiteral("add")));
}

void AppMcpContentTest::staticResources()
{
    // Every file of plan 4.1 is a static resource with its content
    QStringList expected;
    for (const QString& name : pdf::PDFAppMcpContent::getDocumentNames())
    {
        expected << QStringLiteral("pdf4qt://docs/%1.md").arg(name);
    }
    for (const pdf::PDFAppMcpToolSpec& tool : pdf::PDFAppMcpToolSet::getTools())
    {
        expected << QStringLiteral("pdf4qt://docs/tools/%1.md").arg(tool.name);
    }
    for (const QString& name : pdf::PDFAppMcpContent::getSkillNames())
    {
        expected << QStringLiteral("pdf4qt://skills/%1/SKILL.md").arg(name);
    }
    for (const QString& name : pdf::PDFAppMcpContent::getWorkflowNames())
    {
        expected << QStringLiteral("pdf4qt://workflows/%1.md").arg(name);
    }
    const std::shared_ptr<const pdf::PDFAppHelpCatalog> help = pdf::PDFAppHelpCatalog::getBuiltin();
    QVERIFY(help);
    for (const pdf::PDFAppHelpTopic& topic : help->getTopics())
    {
        expected << QStringLiteral("pdf4qt://help/%1.md").arg(topic.id);
    }
    QCOMPARE(pdf::PDFAppMcpContent::getDocumentNames().size(), 12);
    QCOMPARE(pdf::PDFAppMcpContent::getSkillNames().size(), 4);
    QCOMPARE(pdf::PDFAppMcpContent::getWorkflowNames().size(), 6);

    const std::vector<pdf::PDFMcpResourceDefinition> resources = pdf::PDFAppMcpResources::createStaticResources();
    QStringList uris;
    std::set<QString> names;
    for (const pdf::PDFMcpResourceDefinition& resource : resources)
    {
        uris << resource.uri;
        QVERIFY2(names.insert(resource.name).second, qPrintable(resource.name));
        QVERIFY(resource.isStatic);
        QCOMPARE(resource.mimeType, QStringLiteral("text/markdown"));
        QCOMPARE(resource.size, qint64(resource.content.size()));
        QVERIFY2(!resource.title.isEmpty(), qPrintable(resource.uri));
        QVERIFY2(!resource.description.isEmpty(), qPrintable(resource.uri));
        QVERIFY2(resource.description.size() <= 1024, qPrintable(resource.uri));
        QVERIFY2(resource.content.size() > 200, qPrintable(resource.uri));

        // Valid UTF-8 without a BOM and without CR (the files are served as they are)
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString text = decoder.decode(resource.content);
        QVERIFY2(!decoder.hasError(), qPrintable(resource.uri));
        QVERIFY2(!resource.content.startsWith("\xEF\xBB\xBF"), qPrintable(resource.uri));
        QVERIFY2(!resource.content.contains('\r'), qPrintable(resource.uri));

        // American English
        const QString lower = text.toLower();
        for (const QString& british : { QStringLiteral("colour"), QStringLiteral("behaviour"), QStringLiteral("cancelled"), QStringLiteral("centre"),
                                        QStringLiteral("licence"), QStringLiteral("grey"), QStringLiteral("summarise"), QStringLiteral("organise") })
        {
            QVERIFY2(!lower.contains(british), qPrintable(resource.uri + QStringLiteral(": ") + british));
        }
    }
    QStringList sortedUris = uris;
    QStringList sortedExpected = expected;
    sortedUris.sort();
    sortedExpected.sort();
    QCOMPARE(sortedUris, sortedExpected);

    // The resources are read once and shared
    QCOMPARE(pdf::PDFAppMcpContent::getStaticResources().data(), pdf::PDFAppMcpContent::getStaticResources().data());
    QVERIFY(!pdf::PDFAppMcpContent::findStaticResource(QStringLiteral("pdf4qt://docs/unknown.md")));
    QVERIFY(!pdf::PDFAppMcpContent::findStaticResource(QString()));

    // The help topics are the English files of the manual unchanged
    const pdf::PDFMcpResourceDefinition* helpResource = pdf::PDFAppMcpContent::findStaticResource(QStringLiteral("pdf4qt://help/ai-tools.md"));
    QVERIFY(helpResource);
    QCOMPARE(QString::fromUtf8(helpResource->content), readResourceFile(QStringLiteral(":/help/ai-tools.md")));

    // The static templates match their files and are valid
    const std::vector<pdf::PDFMcpResourceTemplateDefinition> templates = pdf::PDFAppMcpContent::createStaticTemplates();
    QCOMPARE(templates.size(), size_t(4));
    for (const pdf::PDFMcpResourceTemplateDefinition& resourceTemplate : templates)
    {
        QVERIFY(resourceTemplate.uriTemplate.isValid());
        QVERIFY(resourceTemplate.isStatic);
        QCOMPARE(resourceTemplate.mimeType, QStringLiteral("text/markdown"));
    }
    int matched = 0;
    for (const pdf::PDFMcpResourceDefinition& resource : resources)
    {
        if (resource.uri.startsWith(QStringLiteral("pdf4qt://docs/")) && !resource.uri.startsWith(QStringLiteral("pdf4qt://docs/tools/")))
        {
            continue;
        }
        bool isMatched = false;
        for (const pdf::PDFMcpResourceTemplateDefinition& resourceTemplate : templates)
        {
            isMatched = isMatched || resourceTemplate.uriTemplate.match(resource.uri).has_value();
        }
        QVERIFY2(isMatched, qPrintable(resource.uri));
        ++matched;
    }
    QVERIFY(matched > 40);

    // The catalog of the host has them (and the templates and prompts)
    pdftest::AppMcpFixture fixture;
    pdf::PDFAppMcpHost* host = fixture.host();
    QVERIFY(host);
    const std::shared_ptr<const pdf::PDFMcpCatalog> catalog = host->getCatalog();
    QVERIFY(catalog);
    for (const QString& uri : expected)
    {
        const pdf::PDFMcpResourceDefinition* resource = catalog->findResource(uri);
        QVERIFY2(resource && resource->isStatic && !resource->content.isEmpty(), qPrintable(uri));
    }
    QVERIFY(catalog->findPrompt(QStringLiteral("summarize-document")));
    QMap<QString, QString> values;
    const pdf::PDFMcpResourceTemplateDefinition* toolTemplate = catalog->matchTemplate(QStringLiteral("pdf4qt://docs/tools/text.md"), &values);
    QVERIFY(toolTemplate && toolTemplate->isStatic);
    QCOMPARE(values.value(QStringLiteral("tool")), QStringLiteral("text"));

    // The serialized list (both eras) names every static resource with its size
    for (pdf::PDFMcpEra era : { pdf::PDFMcpEra::Modern, pdf::PDFMcpEra::Legacy20251125, pdf::PDFMcpEra::Legacy20250618 })
    {
        const QJsonArray list = QJsonDocument::fromJson(catalog->getResourcesJson(era)).array();
        int found = 0;
        for (const QJsonValue& value : list)
        {
            const QJsonObject object = value.toObject();
            if (expected.contains(object.value(QStringLiteral("uri")).toString()))
            {
                ++found;
                QCOMPARE(object.value(QStringLiteral("mimeType")).toString(), QStringLiteral("text/markdown"));
                QVERIFY(object.value(QStringLiteral("size")).toInteger() > 0);
                QVERIFY(!object.value(QStringLiteral("title")).toString().isEmpty());
            }
        }
        QCOMPARE(found, int(expected.size()));
    }
}

void AppMcpContentTest::staticResourcesHttp()
{
    // End to end over HTTP in both eras: resources/list pages the static files,
    // resources/read returns them (modern: cacheable for an hour, public),
    // prompts/list has the workflows
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.startServer());

    for (pdftest::McpTestEra era : { pdftest::McpTestEra::Modern, pdftest::McpTestEra::Legacy20251125, pdftest::McpTestEra::Legacy20250618 })
    {
        std::unique_ptr<pdftest::McpTestClient> client = fixture.client(era);
        if (era != pdftest::McpTestEra::Modern)
        {
            // initialize answers the request and accepts notifications/initialized (202)
            const QJsonObject initialized = client->initialize();
            QVERIFY2(initialized.contains(QStringLiteral("protocolVersion")), qPrintable(QString::fromUtf8(QJsonDocument(initialized).toJson(QJsonDocument::Compact))));
            QVERIFY(initialized.value(QStringLiteral("instructions")).toString().contains(QStringLiteral("pdf4qt://docs/index.md")));
            QVERIFY(!client->getSessionId().isEmpty());
        }

        QStringList uris;
        QString cursor;
        for (int page = 0; page < 100; ++page)
        {
            QJsonObject params;
            if (!cursor.isEmpty())
            {
                params.insert(QStringLiteral("cursor"), cursor);
            }
            const QJsonObject response = client->request(QStringLiteral("resources/list"), params);
            if (client->lastResponse().status != 200)
            {
                QSKIP(qPrintable(QStringLiteral("The transport of package A does not answer yet (HTTP %1)").arg(client->lastResponse().status)));
            }
            const QJsonObject result = response.value(QStringLiteral("result")).toObject();
            for (const QJsonValue& value : result.value(QStringLiteral("resources")).toArray())
            {
                uris << value.toObject().value(QStringLiteral("uri")).toString();
            }
            cursor = result.value(QStringLiteral("nextCursor")).toString();
            if (cursor.isEmpty())
            {
                break;
            }
        }
        for (const pdf::PDFMcpResourceDefinition& resource : pdf::PDFAppMcpContent::getStaticResources())
        {
            QVERIFY2(uris.contains(resource.uri), qPrintable(resource.uri));
        }

        const QString uri = QStringLiteral("pdf4qt://docs/index.md");
        const QJsonObject read = client->request(QStringLiteral("resources/read"), QJsonObject{ { QStringLiteral("uri"), uri } });
        QCOMPARE(client->lastResponse().status, 200);
        const QJsonObject result = read.value(QStringLiteral("result")).toObject();
        const QJsonArray contents = result.value(QStringLiteral("contents")).toArray();
        QCOMPARE(contents.size(), 1);
        const QJsonObject content = contents.first().toObject();
        QCOMPARE(content.value(QStringLiteral("uri")).toString(), uri);
        QCOMPARE(content.value(QStringLiteral("mimeType")).toString(), QStringLiteral("text/markdown"));
        QCOMPARE(content.value(QStringLiteral("text")).toString().toUtf8(), pdf::PDFAppMcpContent::findStaticResource(uri)->content);
        if (era == pdftest::McpTestEra::Modern)
        {
            QCOMPARE(result.value(QStringLiteral("ttlMs")).toInteger(), qint64(3600000));
            QCOMPARE(result.value(QStringLiteral("cacheScope")).toString(), QStringLiteral("public"));
        }
        else
        {
            QVERIFY(!result.contains(QStringLiteral("ttlMs")));
        }

        const QJsonObject prompts = client->request(QStringLiteral("prompts/list"));
        QCOMPARE(client->lastResponse().status, 200);
        QStringList promptNames;
        for (const QJsonValue& value : prompts.value(QStringLiteral("result")).toObject().value(QStringLiteral("prompts")).toArray())
        {
            promptNames << value.toObject().value(QStringLiteral("name")).toString();
        }
        QCOMPARE(promptNames, pdf::PDFAppMcpContent::getWorkflowNames());

        const QJsonObject templates = client->request(QStringLiteral("resources/templates/list"));
        QCOMPARE(client->lastResponse().status, 200);
        QStringList templateTexts;
        for (const QJsonValue& value : templates.value(QStringLiteral("result")).toObject().value(QStringLiteral("resourceTemplates")).toArray())
        {
            templateTexts << value.toObject().value(QStringLiteral("uriTemplate")).toString();
        }
        for (const pdf::PDFMcpResourceTemplateDefinition& resourceTemplate : pdf::PDFAppMcpContent::createStaticTemplates())
        {
            QVERIFY2(templateTexts.contains(resourceTemplate.uriTemplate.getText()), qPrintable(resourceTemplate.uriTemplate.getText()));
        }
    }
}

void AppMcpContentTest::docsLinksAndNames()
{
    const std::set<QString> contractNames = getContractNames();
    const std::vector<pdf::PDFMcpResourceTemplateDefinition> templates = pdf::PDFAppMcpResources::createTemplates();

    // Words of the protocol and of the docs, which are not names of the contract
    const std::set<QString> allowed = {
        QStringLiteral("isError"), QStringLiteral("structuredContent"), QStringLiteral("resultType"), QStringLiteral("inputRequests"),
        QStringLiteral("inputResponses"), QStringLiteral("requestState"), QStringLiteral("resource_link"), QStringLiteral("clientInfo"),
        QStringLiteral("progressToken"), QStringLiteral("input_required"), QStringLiteral("tool_timeout_sec"), QStringLiteral("readOnlyHint"),
        QStringLiteral("destructiveHint"), QStringLiteral("idempotentHint"), QStringLiteral("list_changed"), QStringLiteral("mimeType"),
        QStringLiteral("ttlMs"), QStringLiteral("cacheScope"), QStringLiteral("logLevel"), QStringLiteral("taskId"), QStringLiteral("statusMessage"),
        QStringLiteral("pollIntervalMs"), QStringLiteral("http_headers"), QStringLiteral("mcpServers"), QStringLiteral("httpUrl"),
        QStringLiteral("nextCursor"), QStringLiteral("save_file"), QStringLiteral("open_new_tab"), QStringLiteral("save_copy"),
        QStringLiteral("documentIds"), QStringLiteral("outputSchema"), QStringLiteral("inputSchema"), QStringLiteral("denied_locked"),
        QStringLiteral("denied_busy"), QStringLiteral("denied_dialog"), QStringLiteral("denied_timeout"), QStringLiteral("denied_user"),
        QStringLiteral("denied_closed"), QStringLiteral("denied_disconnected"), QStringLiteral("denied_stopped"), QStringLiteral("subscriptionId"),
        QStringLiteral("resourceSubscriptions")
    };

    static const QRegularExpression code(QStringLiteral("`([^`\\n]+)`"));
    static const QRegularExpression camelOrSnake(QStringLiteral("^[a-z][a-z0-9]*([A-Z][a-zA-Z0-9]*|(_[a-z0-9]+)+)$"));
    static const QRegularExpression toolAction(QStringLiteral("^([a-z_]+)\\.([a-z_]+)$"));

    const std::vector<std::pair<QString, QString>> files = getContentFiles();
    QVERIFY(files.size() >= 31);
    int checkedNames = 0;
    int checkedReferences = 0;
    for (const auto& file : files)
    {
        const QString& uri = file.first;
        const QString& text = file.second;

        // Every pdf4qt:// address is a static resource, a template or a URI of a template
        for (const QString& link : pdf::PDFAppMcpContent::extractUris(text))
        {
            bool isKnown = pdf::PDFAppMcpContent::findStaticResource(link) != nullptr;
            for (const pdf::PDFMcpResourceTemplateDefinition& resourceTemplate : templates)
            {
                isKnown = isKnown || resourceTemplate.uriTemplate.getText() == link || resourceTemplate.uriTemplate.match(link).has_value();
            }
            isKnown = isKnown || link == QStringLiteral("pdf4qt://application/state");
            QVERIFY2(isKnown, qPrintable(uri + QStringLiteral(": ") + link));
        }

        // Names in code spans: tool.action references exist, camelCase and
        // snake_case names are names of the contract (or of the protocol)
        QRegularExpressionMatchIterator iterator = code.globalMatch(text);
        while (iterator.hasNext())
        {
            const QString span = iterator.next().captured(1).trimmed();
            const QRegularExpressionMatch reference = toolAction.match(span);
            if (reference.hasMatch())
            {
                const pdf::PDFAppMcpToolSpec* tool = pdf::PDFAppMcpToolSet::findTool(reference.captured(1));
                if (tool)
                {
                    ++checkedReferences;
                    QVERIFY2(tool->findAction(reference.captured(2)), qPrintable(uri + QStringLiteral(": ") + span));
                }
                continue;
            }
            if (camelOrSnake.match(span).hasMatch())
            {
                ++checkedNames;
                QVERIFY2(contractNames.count(span) || allowed.count(span), qPrintable(uri + QStringLiteral(": unknown name ") + span));
            }
        }
    }
    QVERIFY(checkedReferences > 100);
    QVERIFY(checkedNames > 100);

    // Every tool reference contains the generated reference of the contract
    for (const pdf::PDFAppMcpToolSpec& tool : pdf::PDFAppMcpToolSet::getTools())
    {
        const pdf::PDFMcpResourceDefinition* resource = pdf::PDFAppMcpContent::findStaticResource(QStringLiteral("pdf4qt://docs/tools/%1.md").arg(tool.name));
        QVERIFY2(resource, qPrintable(tool.name));
        const QString text = QString::fromUtf8(resource->content);
        QVERIFY2(text.contains(pdf::PDFAppMcpContent::createToolSummary(tool)),
                 qPrintable(QStringLiteral("docs/tools/%1.md: the generated summary differs, update it (PDF4QT_MCP_DOCS_DUMP)").arg(tool.name)));
        for (const pdf::PDFAppMcpActionSpec& action : tool.actions)
        {
            QVERIFY2(text.contains(pdf::PDFAppMcpContent::createActionReference(tool, action)),
                     qPrintable(QStringLiteral("docs/tools/%1.md: the generated reference of %2 differs, update it (PDF4QT_MCP_DOCS_DUMP)").arg(tool.name, action.name)));
        }
    }

    // The index links every document, tool reference, skill and workflow
    const QString index = QString::fromUtf8(pdf::PDFAppMcpContent::findStaticResource(QStringLiteral("pdf4qt://docs/index.md"))->content);
    for (const auto& file : files)
    {
        if (file.first != QStringLiteral("pdf4qt://docs/index.md"))
        {
            QVERIFY2(index.contains(file.first), qPrintable(file.first));
        }
    }
    for (const pdf::PDFAppMcpToolSpec& tool : pdf::PDFAppMcpToolSet::getTools())
    {
        QVERIFY2(index.contains(QStringLiteral("`%1`").arg(tool.name)), qPrintable(tool.name));
    }

    // The documents never name a reserved phase 2 tool as a phase 1 tool and
    // never contain a token-like secret
    for (const auto& file : files)
    {
        QVERIFY2(!file.second.contains(QStringLiteral("Bearer ey")), qPrintable(file.first));
    }

    // The generator itself: escaping of cells, empty objects, inline objects
    pdf::PDFMcpSchema schema = pdf::PDFMcpSchemaBuilder::object();
    QCOMPARE(pdf::PDFAppMcpContent::createParameterTable(schema), QStringLiteral("No parameters.\n"));
    QCOMPARE(pdf::PDFAppMcpContent::createResultTable(schema), QStringLiteral("No fields.\n"));
    schema.addProperty(QStringLiteral("a"), pdf::PDFMcpSchemaBuilder::string(QStringLiteral("x | y"), 5).setDefault(QStringLiteral("q")), true);
    const QString table = pdf::PDFAppMcpContent::createParameterTable(schema);
    QVERIFY(table.contains(QStringLiteral("| `a` | string, at most 5 characters, default `q` | yes | x \\| y |")));
    QCOMPARE(pdf::PDFAppMcpContent::getPermissionText(99), QString());
}

void AppMcpContentTest::examplesValidate()
{
    // Every documented tool call validates against the input schema of its tool
    // with the validator of the server; every action has at least two examples
    std::map<QString, int> examplesPerAction;
    int calls = 0;
    for (const auto& file : getContentFiles())
    {
        for (const pdf::PDFAppMcpDocumentedCall& call : pdf::PDFAppMcpContent::extractCalls(file.second))
        {
            const QString context = QStringLiteral("%1:%2").arg(file.first).arg(call.line);
            QVERIFY2(call.error.isEmpty(), qPrintable(context + QStringLiteral(": ") + call.error));
            const pdf::PDFAppMcpToolSpec* tool = pdf::PDFAppMcpToolSet::findTool(call.toolName);
            QVERIFY2(tool, qPrintable(context + QStringLiteral(": unknown tool ") + call.toolName));

            QStringList errors;
            const bool isValid = getFullInputSchema(call.toolName).validate(call.arguments, QString(), &errors);
            QVERIFY2(isValid, qPrintable(context + QStringLiteral(": ") + errors.join(QStringLiteral("; "))));

            // The action object is the one of the action; a document id has its form
            const QString action = call.arguments.value(QStringLiteral("action")).toString();
            QVERIFY2(tool->findAction(action), qPrintable(context));
            for (auto it = call.arguments.begin(); it != call.arguments.end(); ++it)
            {
                const bool isCommon = it.key() == QStringLiteral("action") || it.key() == QStringLiteral("documentId");
                QVERIFY2(isCommon || it.key() == action, qPrintable(context + QStringLiteral(": the object of another action ") + it.key()));
            }
            if (call.arguments.contains(QStringLiteral("documentId")))
            {
                QVERIFY2(QRegularExpression(QStringLiteral("^[A-Z2-7]{26}$")).match(call.arguments.value(QStringLiteral("documentId")).toString()).hasMatch(), qPrintable(context));
            }

            if (file.first == QStringLiteral("pdf4qt://docs/tools/%1.md").arg(call.toolName))
            {
                ++examplesPerAction[call.toolName + QLatin1Char('.') + action];
            }
            ++calls;
        }
    }
    QVERIFY(calls > 100);
    for (const pdf::PDFAppMcpToolSpec& tool : pdf::PDFAppMcpToolSet::getTools())
    {
        for (const pdf::PDFAppMcpActionSpec& action : tool.actions)
        {
            const QString id = action.getActionId(tool.name);
            QVERIFY2(examplesPerAction[id] >= 2, qPrintable(id));
        }
    }

    // The extraction itself: invalid JSON is reported, other blocks are not calls
    const std::vector<pdf::PDFAppMcpDocumentedCall> extracted = pdf::PDFAppMcpContent::extractCalls(
        QStringLiteral("a\n```json\n{\"name\": \"text\", \"arguments\": {\"action\": \"search\"}}\n```\n```json\n{\"mcpServers\": {}}\n```\n```json\n{oops\n```\n```json\n{\"name\": 1, \"arguments\": {}}\n```\n```json\n[1]\n```\n```json\n{\"name\": \"x\", \"arguments\": {}, \"extra\": 1}\n```\n```json\nnot closed"));
    QCOMPARE(extracted.size(), size_t(6));
    QCOMPARE(extracted[0].toolName, QStringLiteral("text"));
    QCOMPARE(extracted[0].line, 2);
    QVERIFY(extracted[0].error.isEmpty());
    QVERIFY(!extracted[1].error.isEmpty());
    QVERIFY(!extracted[2].error.isEmpty());
    QVERIFY(!extracted[3].error.isEmpty());
    QVERIFY(!extracted[4].error.isEmpty());
    QVERIFY(!extracted[5].error.isEmpty());

    // A hostile document never hangs the extraction
    QString hostile;
    for (int i = 0; i < 20000; ++i)
    {
        hostile += QStringLiteral("```json\n{\"name\":\"text\",\"arguments\":{}}\n```\npdf4qt://x.\n");
    }
    QCOMPARE(pdf::PDFAppMcpContent::extractCalls(hostile).size(), size_t(20000));
    QCOMPARE(pdf::PDFAppMcpContent::extractUris(hostile).size(), 20000);
    QCOMPARE(pdf::PDFAppMcpContent::extractUris(hostile).front(), QStringLiteral("pdf4qt://x"));
}

void AppMcpContentTest::skillsFormat()
{
    // Agent Skills: YAML front matter with name (= the folder) and description
    // (at most 1024 characters, when to use), then the body sections
    static const QRegularExpression skillName(QStringLiteral("^[a-z0-9]+(-[a-z0-9]+)*$"));
    for (const QString& name : pdf::PDFAppMcpContent::getSkillNames())
    {
        const QString uri = QStringLiteral("pdf4qt://skills/%1/SKILL.md").arg(name);
        const pdf::PDFMcpResourceDefinition* resource = pdf::PDFAppMcpContent::findStaticResource(uri);
        QVERIFY2(resource, qPrintable(uri));
        const QString text = QString::fromUtf8(resource->content);
        QVERIFY2(text.startsWith(QStringLiteral("---\n")), qPrintable(uri));
        const qsizetype end = text.indexOf(QStringLiteral("\n---\n"), 4);
        QVERIFY2(end > 0, qPrintable(uri));

        std::map<QString, QString> frontMatter;
        for (const QString& line : text.mid(4, end - 4).split(QLatin1Char('\n')))
        {
            const qsizetype colon = line.indexOf(QLatin1Char(':'));
            QVERIFY2(colon > 0, qPrintable(uri + QStringLiteral(": ") + line));
            QVERIFY2(frontMatter.emplace(line.left(colon), line.mid(colon + 1).trimmed()).second, qPrintable(uri));
        }
        QCOMPARE(frontMatter.size(), size_t(2));
        QCOMPARE(frontMatter[QStringLiteral("name")], name);
        QVERIFY(skillName.match(name).hasMatch());
        QVERIFY(name.size() <= 64);
        const QString description = frontMatter[QStringLiteral("description")];
        QVERIFY2(!description.isEmpty() && description.size() <= 1024, qPrintable(uri));
        QVERIFY2(description.contains(QStringLiteral("Use ")) || description.contains(QStringLiteral("use ")), qPrintable(uri));
        QVERIFY2(!description.contains(QLatin1Char('<')) && !description.contains(QLatin1Char('>')), qPrintable(uri));
        QCOMPARE(resource->description, description);

        for (const QString& section : { QStringLiteral("When to use"), QStringLiteral("Steps"), QStringLiteral("Tool calls"), QStringLiteral("Pitfalls"),
                                        QStringLiteral("Untrusted content") })
        {
            QVERIFY2(!getSection(text, section).trimmed().isEmpty(), qPrintable(uri + QStringLiteral(": ") + section));
        }
        QVERIFY2(!pdf::PDFAppMcpContent::extractCalls(getSection(text, QStringLiteral("Tool calls"))).empty(), qPrintable(uri));
    }
}

void AppMcpContentTest::workflowsEqualPrompts()
{
    // Plan 4.3: the prompts have the names of the workflows, every argument is
    // described in the inputs of its workflow, the workflow has its sections
    const std::vector<pdf::PDFMcpPromptDefinition> prompts = pdf::PDFAppMcpPrompts::createDefinitions({});
    QStringList promptNames;
    for (const pdf::PDFMcpPromptDefinition& prompt : prompts)
    {
        promptNames << prompt.name;
        QVERIFY(!prompt.title.isEmpty());
        QVERIFY(!prompt.description.isEmpty());

        const QString uri = pdf::PDFAppMcpContent::getWorkflowUri(prompt.name);
        QCOMPARE(uri, QStringLiteral("pdf4qt://workflows/%1.md").arg(prompt.name));
        const QString text = pdf::PDFAppMcpContent::getWorkflowText(prompt.name);
        QVERIFY2(!text.isEmpty(), qPrintable(uri));
        QCOMPARE(text.toUtf8(), pdf::PDFAppMcpContent::findStaticResource(uri)->content);

        for (const QString& section : { QStringLiteral("Goal"), QStringLiteral("Inputs"), QStringLiteral("Steps"), QStringLiteral("Output format"),
                                        QStringLiteral("Pitfalls") })
        {
            QVERIFY2(!getSection(text, section).trimmed().isEmpty(), qPrintable(uri + QStringLiteral(": ") + section));
        }
        const QString inputs = getSection(text, QStringLiteral("Inputs"));
        for (const pdf::PDFMcpPromptArgument& argument : prompt.arguments)
        {
            QVERIFY2(inputs.contains(QStringLiteral("`%1`").arg(argument.name)), qPrintable(uri + QStringLiteral(": ") + argument.name));
            QVERIFY(!argument.title.isEmpty());
            QVERIFY(!argument.description.isEmpty());
            if (argument.required)
            {
                QVERIFY2(inputs.contains(QStringLiteral("`%1` (required)").arg(argument.name)), qPrintable(uri + QStringLiteral(": ") + argument.name));
            }
        }

        // The steps are numbered and call tools
        QVERIFY2(getSection(text, QStringLiteral("Steps")).contains(QStringLiteral("\n1. ")), qPrintable(uri));
        QVERIFY2(!pdf::PDFAppMcpContent::extractCalls(text).empty(), qPrintable(uri));

        // The text of prompts/get names the workflow and the arguments
        QMap<QString, QString> arguments;
        for (const pdf::PDFMcpPromptArgument& argument : prompt.arguments)
        {
            arguments.insert(argument.name, QStringLiteral("value-of-") + argument.name);
        }
        const QString promptText = pdf::PDFAppMcpContent::createPromptText(prompt.name, arguments);
        QVERIFY(promptText.contains(uri));
        QVERIFY(promptText.contains(QStringLiteral("untrusted")));
        for (const pdf::PDFMcpPromptArgument& argument : prompt.arguments)
        {
            QVERIFY(promptText.contains(QStringLiteral("- %1: value-of-%1").arg(argument.name)));
        }
        QVERIFY(pdf::PDFAppMcpContent::createPromptText(prompt.name, {}).contains(QStringLiteral("no arguments")));
    }
    QCOMPARE(promptNames, pdf::PDFAppMcpContent::getWorkflowNames());
    QCOMPARE(promptNames, (QStringList{ QStringLiteral("summarize-document"), QStringLiteral("review-and-comment"), QStringLiteral("find-and-highlight"),
                                        QStringLiteral("fill-form"), QStringLiteral("build-outline"), QStringLiteral("cite-sources") }));

    // Required arguments of plan 4.3
    for (const pdf::PDFMcpPromptDefinition& prompt : prompts)
    {
        for (const pdf::PDFMcpPromptArgument& argument : prompt.arguments)
        {
            const bool isRequired = (prompt.name == QStringLiteral("find-and-highlight") && argument.name == QStringLiteral("query")) ||
                                    (prompt.name == QStringLiteral("cite-sources") && argument.name == QStringLiteral("question"));
            QCOMPARE(argument.required, isRequired);
        }
    }

    // Values of the completion: the criteria, the palette of the contract, the depth
    QCOMPARE(pdf::PDFAppMcpContent::getPromptArgumentValues(QStringLiteral("find-and-highlight"), QStringLiteral("color")), pdf::PDFAppMcpToolSet::getColorNames());
    QCOMPARE(pdf::PDFAppMcpContent::getPromptArgumentValues(QStringLiteral("build-outline"), QStringLiteral("depth")).size(), 4);
    QCOMPARE(pdf::PDFAppMcpContent::getPromptArgumentValues(QStringLiteral("review-and-comment"), QStringLiteral("criteria")).size(), 5);
    QVERIFY(pdf::PDFAppMcpContent::getPromptArgumentValues(QStringLiteral("fill-form"), QStringLiteral("data")).isEmpty());
    QVERIFY(pdf::PDFAppMcpContent::getPromptArgumentValues(QStringLiteral("unknown"), QStringLiteral("color")).isEmpty());

    // Unknown prompts
    QVERIFY(pdf::PDFAppMcpContent::getWorkflowUri(QStringLiteral("unknown")).isEmpty());
    QVERIFY(pdf::PDFAppMcpContent::getWorkflowText(QStringLiteral("../index")).isEmpty());
    QVERIFY(pdf::PDFAppMcpContent::createPromptText(QStringLiteral("unknown"), {}).isEmpty());

    // Icons are passed through
    pdf::PDFMcpIcon icon;
    icon.source = QStringLiteral("data:image/png;base64,AA==");
    for (const pdf::PDFMcpPromptDefinition& prompt : pdf::PDFAppMcpPrompts::createDefinitions({ icon }))
    {
        QCOMPARE(prompt.icons.size(), size_t(1));
    }
}

void AppMcpContentTest::promptTextHostile()
{
    // Arguments of prompts/get are untrusted: bounded, controls and
    // bidirectional overrides removed, unknown names ignored, never a crash
    QMap<QString, QString> arguments;
    arguments.insert(QStringLiteral("query"), QStringLiteral("a") + QChar(0x202E) + QStringLiteral("b\u0000c\u0007d") + QChar(0xD800) + QStringLiteral("e\nf"));
    arguments.insert(QStringLiteral("document"), QString(1000000, QLatin1Char('x')));
    arguments.insert(QStringLiteral("unknownArgument"), QStringLiteral("must not appear"));
    arguments.insert(QStringLiteral("color"), QStringLiteral("   "));
    const QString text = pdf::PDFAppMcpContent::createPromptText(QStringLiteral("find-and-highlight"), arguments);
    QVERIFY(!text.contains(QChar(0x202E)));
    QVERIFY(!text.contains(QChar(0x0000)));
    QVERIFY(!text.contains(QChar(0x0007)));
    QVERIFY(text.contains(QStringLiteral("- query: abcd�e\nf")));
    QVERIFY(!text.contains(QStringLiteral("must not appear")));
    QVERIFY(!text.contains(QStringLiteral("- color:")));
    QVERIFY(text.size() < pdf::PDFAppMcpContent::MAXIMUM_ARGUMENT_LENGTH + 2000);

    // The resolved document id of the caller is shown too
    arguments.clear();
    arguments.insert(QStringLiteral("documentId"), QStringLiteral("K3FQW2M7B4XDZR5T6YH5ZCANPE"));
    QVERIFY(pdf::PDFAppMcpContent::createPromptText(QStringLiteral("summarize-document"), arguments).contains(QStringLiteral("- documentId: K3FQW2M7B4XDZR5T6YH5ZCANPE")));
}

void AppMcpContentTest::setupTexts()
{
    // Plan 5.2: every client, local and remote address, with and without the token
    const QString token = QStringLiteral("q7LmT4abcDEF012_-ghiJKLmnoPQRstuVWXyz345x2Vd");
    QCOMPARE(pdf::PDFAppMcpClientSetup::getDisplayToken(token), QStringLiteral("q7LmT4…x2Vd"));
    QCOMPARE(pdf::PDFAppMcpClientSetup::getDisplayToken(QStringLiteral("short")), QStringLiteral("short"));

    // The code boxes show the shortened token, the copy buttons copy the whole one
    const pdf::PDFAppMcpSetup display = pdf::PDFAppMcpClientSetup::createForDisplay(pdf::PDFAppMcpSetupClient::ClaudeCode, QStringLiteral("http://127.0.0.1:8765/mcp"), token);
    QCOMPARE(display.steps[0].code, QStringLiteral("claude mcp add --transport http pdf4qt http://127.0.0.1:8765/mcp --header \"Authorization: Bearer q7LmT4%1x2Vd\"").arg(QChar(0x2026)));
    QVERIFY(!display.copyText.contains(token));
    QVERIFY(pdf::PDFAppMcpClientSetup::createForDisplay(pdf::PDFAppMcpSetupClient::Other, QStringLiteral("http://127.0.0.1:8765/mcp"), QStringLiteral("a\"b")).copyText.contains(QStringLiteral("<token>")));
    QVERIFY(!pdf::PDFAppMcpClientSetup::createForDisplay(pdf::PDFAppMcpSetupClient::Other, QStringLiteral("http://127.0.0.1:8765/mcp"), QString()).containsToken);

    // The verbatim texts of Figma 653:5730 (Claude Code) and 955:149400 (Other client)
    const QString local = QStringLiteral("http://127.0.0.1:8765/mcp");
    {
        const pdf::PDFAppMcpSetup setup = pdf::PDFAppMcpClientSetup::create(pdf::PDFAppMcpSetupClient::ClaudeCode, local, QStringLiteral("q7LmT4x2Vd"));
        QCOMPARE(setup.title, QStringLiteral("Set up Claude"));
        QCOMPARE(setup.subtitle, QStringLiteral("Adds this server to Claude Code. It takes about a minute."));
        QCOMPARE(setup.steps.size(), size_t(3));
        QCOMPARE(setup.steps[0].text, QStringLiteral("Run this command in a terminal. It registers PDF4QT as an MCP server of Claude Code."));
        QCOMPARE(setup.steps[0].code, QStringLiteral("claude mcp add --transport http pdf4qt http://127.0.0.1:8765/mcp --header \"Authorization: Bearer q7LmT4x2Vd\""));
        QCOMPARE(setup.steps[1].text, QStringLiteral("Check that Claude Code lists the server as connected."));
        QCOMPARE(setup.steps[1].code, QStringLiteral("claude mcp list"));
        QCOMPARE(setup.steps[2].text, QStringLiteral("Open a document in PDF4QT and ask Claude about it, for example:"));
        QCOMPARE(setup.steps[2].code, QStringLiteral("Summarize the document that is open in PDF4QT."));
        QCOMPARE(setup.steps[2].codeKind, pdf::PDFAppMcpSetupCodeKind::Prompt);
        QCOMPARE(setup.copyButtonText, QStringLiteral("Copy Command"));
        QCOMPARE(setup.copyText, setup.steps[0].code);
        QCOMPARE(setup.documentationText, QStringLiteral("Claude Code Documentation"));
        QCOMPARE(setup.note, QStringLiteral("The command contains your access token. Run it again after you create a new token."));

        // 955:150877: without the token the command has no --header part
        const pdf::PDFAppMcpSetup noToken = pdf::PDFAppMcpClientSetup::create(pdf::PDFAppMcpSetupClient::ClaudeCode, local, QString());
        QCOMPARE(noToken.steps[0].code, QStringLiteral("claude mcp add --transport http pdf4qt http://127.0.0.1:8765/mcp"));
        QVERIFY(!noToken.containsToken);
    }
    {
        const pdf::PDFAppMcpSetup setup = pdf::PDFAppMcpClientSetup::create(pdf::PDFAppMcpSetupClient::Other, local, QStringLiteral("q7LmT4x2Vd"));
        QCOMPARE(setup.title, QStringLiteral("Set up another MCP client"));
        QCOMPARE(setup.subtitle, QStringLiteral("For any client that supports MCP over Streamable HTTP. Enter these values in its server settings."));
        QCOMPARE(setup.steps.size(), size_t(3));
        QCOMPARE(setup.steps[0].text, QStringLiteral("Add an MCP server of type Streamable HTTP (called “http” in some clients) with this address."));
        QCOMPARE(setup.steps[0].code, local);
        QCOMPARE(setup.steps[1].text, QStringLiteral("Let the client send the access token in this request header."));
        QCOMPARE(setup.steps[1].code, QStringLiteral("Authorization: Bearer q7LmT4x2Vd"));
        QCOMPARE(setup.steps[2].text, QStringLiteral("For a client configured by a file, add this entry to its MCP configuration. Key names differ between clients."));
        QCOMPARE(setup.steps[2].code, QStringLiteral("{\n  \"mcpServers\": {\n    \"pdf4qt\": {\n      \"type\": \"http\",\n      \"url\": \"http://127.0.0.1:8765/mcp\",\n"
                                                     "      \"headers\": {\n        \"Authorization\": \"Bearer q7LmT4x2Vd\"\n      }\n    }\n  }\n}"));
        QCOMPARE(setup.copyButtonText, QStringLiteral("Copy Configuration"));
        QCOMPARE(setup.documentationText, QStringLiteral("Setup Guide"));
        QCOMPARE(setup.note, QStringLiteral("The header and the configuration contain your access token – copy them again after you create a new token. "
                                            "Without Require access token, leave the header out."));

        // Without the token: no header step, no headers in the configuration
        const pdf::PDFAppMcpSetup noToken = pdf::PDFAppMcpClientSetup::create(pdf::PDFAppMcpSetupClient::Other, local, QString());
        QCOMPARE(noToken.steps.size(), size_t(2));
        QVERIFY(!noToken.copyText.contains(QStringLiteral("headers")));
    }

    // The setup guide of Other client is a route of the help topic with its anchor
    const std::shared_ptr<const pdf::PDFAppHelpCatalog> help = pdf::PDFAppHelpCatalog::getBuiltin();
    const pdf::PDFAppHelpTopic* assistantTopic = help->findTopic(QStringLiteral("ai-assistant"));
    QVERIFY(assistantTopic);
    bool hasAnchor = false;
    for (const pdf::PDFAppMarkdownHeading& heading : assistantTopic->headings)
    {
        hasAnchor = hasAnchor || heading.anchor == QStringLiteral("other-mcp-clients");
    }
    QVERIFY(hasAnchor);

    // Every client and every combination
    const std::vector<pdf::PDFAppMcpSetupClient> clients = { pdf::PDFAppMcpSetupClient::ClaudeCode, pdf::PDFAppMcpSetupClient::ClaudeDesktop, pdf::PDFAppMcpSetupClient::Codex,
                                                             pdf::PDFAppMcpSetupClient::Gemini, pdf::PDFAppMcpSetupClient::Copilot, pdf::PDFAppMcpSetupClient::Cursor,
                                                             pdf::PDFAppMcpSetupClient::Other };
    QCOMPARE(pdf::PDFAppMcpClientSetup::getTileClients().size(), size_t(6));
    QCOMPARE(pdf::PDFAppMcpClientSetup::getVariants(pdf::PDFAppMcpSetupClient::ClaudeDesktop).size(), size_t(2));
    QCOMPARE(pdf::PDFAppMcpClientSetup::getVariants(pdf::PDFAppMcpSetupClient::Codex).size(), size_t(1));
    const QStringList tiles = { QStringLiteral("Claude"), QStringLiteral("Codex"), QStringLiteral("Gemini"), QStringLiteral("Copilot"), QStringLiteral("Cursor"), QStringLiteral("Other client") };
    const QStringList vendors = { QStringLiteral("Anthropic"), QStringLiteral("OpenAI"), QStringLiteral("Google"), QStringLiteral("GitHub"), QStringLiteral("Anysphere"), QStringLiteral("Any MCP client") };
    for (size_t i = 0; i < pdf::PDFAppMcpClientSetup::getTileClients().size(); ++i)
    {
        QCOMPARE(pdf::PDFAppMcpClientSetup::getTileName(pdf::PDFAppMcpClientSetup::getTileClients()[i]), tiles[qsizetype(i)]);
        QCOMPARE(pdf::PDFAppMcpClientSetup::getVendor(pdf::PDFAppMcpClientSetup::getTileClients()[i]), vendors[qsizetype(i)]);
    }

    for (pdf::PDFAppMcpSetupClient client : clients)
    {
        // The clientInfo name gives the monogram of the tile (Other client: the mcp icon)
        const QString clientInfoName = pdf::PDFAppMcpClientSetup::getClientInfoName(client);
        QCOMPARE(clientInfoName.isEmpty(), client == pdf::PDFAppMcpSetupClient::Other);
        if (!clientInfoName.isEmpty())
        {
            QVERIFY2(pdf::PDFAppMcpClientNames::describe(clientInfoName).isKnown, qPrintable(clientInfoName));
        }
        QVERIFY(!pdf::PDFAppMcpClientSetup::getVariantName(client).isEmpty());

        for (const QString& address : { local, QStringLiteral("http://192.168.1.24:8765/mcp"), QStringLiteral("http://[::1]:9000/mcp") })
        {
            for (const QString& key : { token, QString() })
            {
                const QString context = QStringLiteral("%1 %2 %3").arg(int(client)).arg(address, key);
                const pdf::PDFAppMcpSetup setup = pdf::PDFAppMcpClientSetup::create(client, address, key);
                QCOMPARE(setup.client, client);
                QVERIFY2(!setup.title.isEmpty() && !setup.subtitle.isEmpty() && !setup.note.isEmpty(), qPrintable(context));
                QVERIFY2(setup.steps.size() >= 2 && setup.steps.size() <= 3, qPrintable(context));
                QVERIFY2(setup.copyText.contains(address), qPrintable(context));
                QCOMPARE(setup.containsToken, !key.isEmpty());
                QCOMPARE(setup.copyText.contains(key.isEmpty() ? QStringLiteral("<never>") : key), !key.isEmpty());
                QVERIFY2(!setup.copyText.contains(QStringLiteral("Authorization")) || !key.isEmpty(), qPrintable(context));
                QVERIFY2(setup.documentationLink.startsWith(QStringLiteral("https://")) || setup.documentationLink.startsWith(QStringLiteral("/help/")), qPrintable(context));
                QVERIFY2(!setup.copyButtonText.isEmpty() && !setup.documentationText.isEmpty(), qPrintable(context));
                for (const pdf::PDFAppMcpSetupStep& step : setup.steps)
                {
                    QVERIFY2(!step.text.isEmpty(), qPrintable(context));
                    QCOMPARE(step.code.isEmpty(), step.codeKind == pdf::PDFAppMcpSetupCodeKind::None);
                    QVERIFY2(!step.code.contains(QLatin1Char('\r')), qPrintable(context));
                }

                // The copied text is a step of the card
                bool isStep = false;
                for (const pdf::PDFAppMcpSetupStep& step : setup.steps)
                {
                    isStep = isStep || step.code == setup.copyText;
                }
                QVERIFY2(isStep, qPrintable(context));

                // Configurations: JSON parsed by Qt, TOML well-formed
                const bool isJson = client == pdf::PDFAppMcpSetupClient::ClaudeDesktop || client == pdf::PDFAppMcpSetupClient::Copilot ||
                                    client == pdf::PDFAppMcpSetupClient::Cursor || client == pdf::PDFAppMcpSetupClient::Other;
                if (isJson)
                {
                    QJsonParseError error;
                    const QJsonObject object = QJsonDocument::fromJson(setup.copyText.toUtf8(), &error).object();
                    QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(context + error.errorString()));
                    const QString rootKey = client == pdf::PDFAppMcpSetupClient::Copilot ? QStringLiteral("servers") : QStringLiteral("mcpServers");
                    const QJsonObject server = object.value(rootKey).toObject().value(QStringLiteral("pdf4qt")).toObject();
                    QVERIFY2(!server.isEmpty(), qPrintable(context));
                    if (client == pdf::PDFAppMcpSetupClient::ClaudeDesktop)
                    {
                        QCOMPARE(server.value(QStringLiteral("command")).toString(), QStringLiteral("npx"));
                        const QJsonArray args = server.value(QStringLiteral("args")).toArray();
                        QVERIFY(args.contains(QStringLiteral("mcp-remote")));
                        QVERIFY(args.contains(address));
                        QCOMPARE(args.contains(QStringLiteral("--allow-http")), address.contains(QStringLiteral("192.168")));
                        QCOMPARE(args.contains(QStringLiteral("Authorization:${PDF4QT_AUTH}")), !key.isEmpty());
                        QCOMPARE(server.value(QStringLiteral("env")).toObject().value(QStringLiteral("PDF4QT_AUTH")).toString(), key.isEmpty() ? QString() : QStringLiteral("Bearer ") + key);
                    }
                    else
                    {
                        QCOMPARE(server.value(QStringLiteral("url")).toString(), address);
                        QCOMPARE(server.value(QStringLiteral("headers")).toObject().value(QStringLiteral("Authorization")).toString(), key.isEmpty() ? QString() : QStringLiteral("Bearer ") + key);
                        QCOMPARE(server.contains(QStringLiteral("type")), client != pdf::PDFAppMcpSetupClient::Cursor);
                    }
                }
                else if (client == pdf::PDFAppMcpSetupClient::Codex)
                {
                    QString error;
                    QVERIFY2(isWellFormedToml(setup.copyText, &error), qPrintable(context + error));
                    QVERIFY(setup.copyText.contains(QStringLiteral("tool_timeout_sec = 180")));
                    QCOMPARE(setup.copyText.contains(QStringLiteral("http_headers = { \"Authorization\" = \"Bearer ")), !key.isEmpty());
                }
                else
                {
                    // Commands: the address and the header quoted in double quotes
                    QVERIFY(setup.copyText.endsWith(address) || setup.copyText.endsWith(QStringLiteral("\"Authorization: Bearer %1\"").arg(key)));
                    QCOMPARE(setup.copyText.contains(QStringLiteral("--header \"Authorization: Bearer %1\"").arg(key)), !key.isEmpty());
                }
            }
        }
    }

    // The quoting helpers
    QCOMPARE(pdf::PDFAppMcpClientSetup::quoteJson(QStringLiteral("a\"b\\c\nd\u0001")), QStringLiteral("\"a\\\"b\\\\c\\nd\\u0001\""));
    QCOMPARE(pdf::PDFAppMcpClientSetup::quoteToml(QStringLiteral("a\"b\\c\td\u007F")), QStringLiteral("\"a\\\"b\\\\c\\td\\u007f\""));
    QCOMPARE(QJsonDocument::fromJson(QStringLiteral("[%1]").arg(pdf::PDFAppMcpClientSetup::quoteJson(QStringLiteral("x\"\\\r\t\u0002é"))).toUtf8()).array().first().toString(),
             QStringLiteral("x\"\\\r\t\u0002é"));
    QString error;
    QVERIFY(isWellFormedToml(QStringLiteral("[a]\nb = %1").arg(pdf::PDFAppMcpClientSetup::quoteToml(QStringLiteral("\"\\\n\u0003"))), &error));
    QVERIFY(!isWellFormedToml(QStringLiteral("[a]\nb = \"x\"y\""), &error));
    QVERIFY(!isWellFormedToml(QStringLiteral("b = \"x\""), &error));
}

void AppMcpContentTest::setupTextsHostile()
{
    // A token or an address, which a shell or a file could interpret, is never
    // put into a text: the placeholder stands there instead
    for (const QString& token : { QStringLiteral("a\"b"), QStringLiteral("$(rm -rf ~)"), QStringLiteral("`x`"), QStringLiteral("a b"), QStringLiteral("a%PATH%"),
                                  QStringLiteral("a\nb"), QStringLiteral("x'y"), QString(QChar(0x202E)) + QStringLiteral("abc"), QString(300, QLatin1Char('a')) })
    {
        QVERIFY(!pdf::PDFAppMcpClientSetup::isSafeToken(token));
        for (pdf::PDFAppMcpSetupClient client : { pdf::PDFAppMcpSetupClient::ClaudeCode, pdf::PDFAppMcpSetupClient::Codex, pdf::PDFAppMcpSetupClient::Other,
                                                  pdf::PDFAppMcpSetupClient::ClaudeDesktop, pdf::PDFAppMcpSetupClient::Gemini })
        {
            const pdf::PDFAppMcpSetup setup = pdf::PDFAppMcpClientSetup::create(client, QStringLiteral("http://127.0.0.1:8765/mcp"), token);
            QVERIFY(!setup.copyText.contains(token));
            QVERIFY(setup.copyText.contains(QStringLiteral("<token>")));
        }
    }
    QVERIFY(pdf::PDFAppMcpClientSetup::isSafeToken(QStringLiteral("abc-DEF_012.~+/=")));
    QVERIFY(!pdf::PDFAppMcpClientSetup::isSafeToken(QString()));

    for (const QString& address : { QStringLiteral("http://127.0.0.1:8765/mcp\"; rm -rf ~; \""), QStringLiteral("https://127.0.0.1:8765/mcp"), QStringLiteral("http://a b:1/mcp"),
                                    QStringLiteral("http://127.0.0.1:8765/mcp?x=1"), QStringLiteral("http://$(x):1/mcp"), QStringLiteral("javascript:alert(1)"), QString() })
    {
        QVERIFY2(!pdf::PDFAppMcpClientSetup::isSafeAddress(address), qPrintable(address));
        const pdf::PDFAppMcpSetup setup = pdf::PDFAppMcpClientSetup::create(pdf::PDFAppMcpSetupClient::ClaudeCode, address, QStringLiteral("abc"));
        QVERIFY(setup.copyText.contains(QStringLiteral("<address>")));
    }
    for (const QString& address : { QStringLiteral("http://127.0.0.1:8765/mcp"), QStringLiteral("http://[::1]:8765/mcp"), QStringLiteral("http://localhost:1024/mcp"),
                                    QStringLiteral("http://my-pc.local:8765/mcp") })
    {
        QVERIFY2(pdf::PDFAppMcpClientSetup::isSafeAddress(address), qPrintable(address));
    }

    // An invalid enumeration value gives empty texts, never a crash
    const pdf::PDFAppMcpSetupClient invalid = pdf::PDFAppMcpSetupClient(99);
    QVERIFY(pdf::PDFAppMcpClientSetup::getTileName(invalid).isEmpty());
    QVERIFY(pdf::PDFAppMcpClientSetup::getVendor(invalid).isEmpty());
    QVERIFY(pdf::PDFAppMcpClientSetup::getVariantName(invalid).isEmpty());
    QVERIFY(pdf::PDFAppMcpClientSetup::getClientInfoName(invalid).isEmpty());
    QVERIFY(pdf::PDFAppMcpClientSetup::create(invalid, QStringLiteral("http://127.0.0.1:1/mcp"), QString()).steps.empty());
}

void AppMcpContentTest::helpTopicsMatchCode()
{
    // H1: the help topics of the AI page agree with the code
    const std::shared_ptr<const pdf::PDFAppHelpCatalog> help = pdf::PDFAppHelpCatalog::getBuiltin();
    QVERIFY(help && help->getErrors().isEmpty());
    const pdf::PDFAppHelpTopic* assistant = help->findTopic(QStringLiteral("ai-assistant"));
    const pdf::PDFAppHelpTopic* permissions = help->findTopic(QStringLiteral("ai-permissions"));
    const pdf::PDFAppHelpTopic* tools = help->findTopic(QStringLiteral("ai-tools"));
    QVERIFY(assistant && permissions && tools);
    for (const pdf::PDFAppHelpTopic* topic : { assistant, permissions, tools })
    {
        QCOMPARE(topic->categoryId, QStringLiteral("ai-assistants"));
        QVERIFY(topic->related.contains(QStringLiteral("/ai")));
    }

    // Defaults and texts of the settings
    pdftest::AppMcpFixture fixture;
    const pdf::PDFSettingRegistry* registry = fixture.context->getSettingRegistry();
    const pdf::PDFSettingDefinition* port = registry->findDefinition(pdf::PDFSettingKeys::mcpPort());
    QVERIFY(port);
    QVERIFY(assistant->markdown.contains(QStringLiteral("| Port | %1 |").arg(port->getDefaultValue().toInt())));
    QVERIFY(assistant->markdown.contains(QStringLiteral("(%1–%2)").arg(port->getMinimum().toInt()).arg(port->getMaximum().toInt())));
    QVERIFY(assistant->markdown.contains(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port->getDefaultValue().toInt())));
    const pdf::PDFSettingDefinition* requireToken = registry->findDefinition(pdf::PDFSettingKeys::mcpRequireToken());
    QVERIFY(requireToken && requireToken->getDefaultValue().toBool());
    QVERIFY(assistant->markdown.contains(QStringLiteral("| %1 | On |").arg(requireToken->getTitle())));
    const pdf::PDFSettingDefinition* startWithApp = registry->findDefinition(pdf::PDFSettingKeys::mcpStartWithApp());
    QVERIFY(startWithApp && !startWithApp->getDefaultValue().toBool());
    QVERIFY(assistant->markdown.contains(QStringLiteral("**%1** starts it with the application (off by default)").arg(startWithApp->getTitle())));

    for (const QString& key : { pdf::PDFSettingKeys::mcpPermissionRead(), pdf::PDFSettingKeys::mcpPermissionEdit(), pdf::PDFSettingKeys::mcpPermissionFiles() })
    {
        const pdf::PDFSettingDefinition* definition = registry->findDefinition(key);
        QVERIFY(definition);
        QString defaultTitle;
        for (const pdf::PDFSettingOption& option : definition->getOptions())
        {
            if (option.id == definition->getDefaultValue().toString())
            {
                defaultTitle = option.getTitle();
            }
            QVERIFY2(permissions->markdown.contains(QStringLiteral("| %1 |").arg(option.getTitle())), qPrintable(option.getTitle()));
        }
        QVERIFY(!defaultTitle.isEmpty());
        const QRegularExpression row(QStringLiteral("\\| %1 \\| [^|\\n]+ \\| %2 \\|").arg(QRegularExpression::escape(definition->getTitle()), defaultTitle));
        QVERIFY2(row.match(permissions->markdown).hasMatch(), qPrintable(definition->getTitle()));

        // The same permission names are the names of the docs for the assistants
        const QString docs = QString::fromUtf8(pdf::PDFAppMcpContent::findStaticResource(QStringLiteral("pdf4qt://docs/permissions.md"))->content);
        QVERIFY2(docs.contains(definition->getTitle()), qPrintable(definition->getTitle()));
    }

    // The new wording of D2: approval per assistant, 30 minutes, disconnect
    QVERIFY(permissions->markdown.contains(QStringLiteral("until it has not used PDF4QT for 30 minutes or you disconnect it")));
    QVERIFY(!permissions->markdown.contains(QStringLiteral("once for each session")));
    QVERIFY(assistant->markdown.contains(QStringLiteral("30 minutes")));

    // The texts of the activity states are quoted as the application shows them
    const QJsonObject detail;
    for (pdf::PDFMcpActivityStatus status : { pdf::PDFMcpActivityStatus::Waiting, pdf::PDFMcpActivityStatus::Allowed, pdf::PDFMcpActivityStatus::DeniedUser,
                                              pdf::PDFMcpActivityStatus::DeniedTimeout, pdf::PDFMcpActivityStatus::DeniedClosed, pdf::PDFMcpActivityStatus::DeniedLocked,
                                              pdf::PDFMcpActivityStatus::DeniedBusy, pdf::PDFMcpActivityStatus::DeniedDialog, pdf::PDFMcpActivityStatus::DeniedDisconnected,
                                              pdf::PDFMcpActivityStatus::DeniedStopped, pdf::PDFMcpActivityStatus::Canceled, pdf::PDFMcpActivityStatus::Failed,
                                              pdf::PDFMcpActivityStatus::DeniedPermission })
    {
        const QString text = pdf::PDFAppMcpActivity::getStatusText(status, QStringLiteral("Claude Code"), detail);
        QVERIFY2(permissions->markdown.contains(text), qPrintable(text));
    }
    QJsonObject elicitation;
    elicitation.insert(QStringLiteral("reason"), QStringLiteral("elicitation"));
    QVERIFY(permissions->markdown.contains(pdf::PDFAppMcpActivity::getStatusText(pdf::PDFMcpActivityStatus::Waiting, QStringLiteral("<assistant>"), elicitation)));

    // The confirmation timeout of the docs and of the help is the limit of the contract
    QVERIFY(permissions->markdown.contains(QStringLiteral("2 minutes")));

    // ... counted from the dialog, and the bound of the queue of the user interface (items 15, 38)
    {
        const QString queueBound = QStringLiteral("denied after %1 minutes of waiting").arg(pdf::PDFAppMcpConfirmationUi::DEFAULT_QUEUE_TIMEOUT / 60000);
        const QString docs = QString::fromUtf8(pdf::PDFAppMcpContent::findStaticResource(QStringLiteral("pdf4qt://docs/permissions.md"))->content);
        QCOMPARE(pdf::PDFAppMcpConfirmationUi::DEFAULT_TIMEOUT, 120000);
        QVERIFY(permissions->markdown.contains(queueBound));
        QVERIFY(docs.contains(queueBound));
        QVERIFY(docs.contains(QStringLiteral("**2 minutes** from the moment the dialog appears")));
        const QString errors = QString::fromUtf8(pdf::PDFAppMcpContent::findStaticResource(QStringLiteral("pdf4qt://docs/errors.md"))->content);
        QVERIFY(errors.contains(QStringLiteral("at most %1 minutes waiting before it appears").arg(pdf::PDFAppMcpConfirmationUi::DEFAULT_QUEUE_TIMEOUT / 60000)));
    }

    // The clients of the setup cards are listed with their texts
    for (pdf::PDFAppMcpSetupClient client : pdf::PDFAppMcpClientSetup::getTileClients())
    {
        QVERIFY2(assistant->markdown.contains(pdf::PDFAppMcpClientSetup::getVariantName(client)), qPrintable(pdf::PDFAppMcpClientSetup::getVariantName(client)));
    }
    QVERIFY(assistant->markdown.contains(QStringLiteral("Claude Desktop")));
    QVERIFY(assistant->markdown.contains(pdf::PDFAppMcpClientSetup::create(pdf::PDFAppMcpSetupClient::ClaudeCode, QStringLiteral("http://127.0.0.1:8765/mcp"), QStringLiteral("TOKEN"))
                                             .copyText.replace(QStringLiteral("TOKEN"), QStringLiteral("<token>"))));
    QVERIFY(assistant->markdown.contains(QStringLiteral("shell history")));

    // ai-tools: every tool of phase 1 is described by its title
    for (const pdf::PDFAppMcpToolSpec& tool : pdf::PDFAppMcpToolSet::getTools())
    {
        QVERIFY2(tools->markdown.contains(QStringLiteral("| %1 |").arg(tool.title)), qPrintable(tool.title));
    }
    QVERIFY(tools->markdown.contains(QStringLiteral("/help/ai-permissions")));
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
    AppMcpContentTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_appmcpcontenttest.moc"
