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

#include "pdfdocumenttool.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttoolsuggestions.h"

#include "testdocumenttool.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTranslator>
#include <QtTest>

#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <thread>

/// Helpers of the test
class DocumentToolTestHelper
{
public:
    DocumentToolTestHelper() = delete;

    static QString id(const char* text) { return QString::fromLatin1(text); }

    static QStringList ids(std::initializer_list<const char*> texts)
    {
        QStringList result;
        for (const char* text : texts)
        {
            result << QString::fromLatin1(text);
        }
        return result;
    }

    /// Document with the pages of the sizes (portrait / landscape)
    static pdf::PDFDocumentPointer createDocument(const std::vector<QSizeF>& sizes)
    {
        pdf::PDFDocumentBuilder builder;
        for (const QSizeF& size : sizes)
        {
            builder.appendPage(QRectF(QPointF(0, 0), size));
        }
        builder.setDocumentTitle(QStringLiteral("Original"));
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build()));
    }

    /// Five pages: 1 portrait, 2 landscape, 3 portrait, 4 landscape, 5 portrait
    static pdf::PDFDocumentPointer createMixedDocument()
    {
        const QSizeF portrait(595, 842);
        const QSizeF landscape(842, 595);
        return createDocument({ portrait, landscape, portrait, landscape, portrait });
    }

    static pdf::PDFDocumentToolContext createContext(pdf::PDFDocumentPointer document)
    {
        pdf::PDFDocumentToolContext context;
        pdf::PDFDocumentToolInputDocument input;
        input.document = std::move(document);
        input.fileName = QStringLiteral("input.pdf");
        context.documents.push_back(input);
        return context;
    }

    static pdf::PDFDocumentToolPair pair(const char* from, const char* to, qint64 count, qint64 lastUsed)
    {
        return { QString::fromLatin1(from), QString::fromLatin1(to), count, lastUsed };
    }

    static const pdf::PDFDocumentToolPair* findPair(const std::vector<pdf::PDFDocumentToolPair>& pairs, const char* to)
    {
        for (const pdf::PDFDocumentToolPair& pair : pairs)
        {
            if (pair.toTool == QLatin1String(to))
            {
                return &pair;
            }
        }
        return nullptr;
    }

    static QString normalizeError(const pdf::PDFDocumentToolParameterDefinition& definition, const QVariant& value)
    {
        QVariant normalized;
        return definition.normalize(value, &normalized);
    }

    static QVariant normalized(const pdf::PDFDocumentToolParameterDefinition& definition, const QVariant& value)
    {
        QVariant result;
        const QString error = definition.normalize(value, &result);
        return error.isEmpty() ? result : QVariant();
    }
};

/// Translator of the test: every text of the contexts of the tools gets the prefix "[T] "
class DocumentToolTestTranslator : public QTranslator
{
public:
    QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);

        const QByteArray contextName(context);
        if (contextName == "pdf::PDFDocumentToolParameterDefinition" || contextName == "pdf::PDFDocumentTool")
        {
            return QStringLiteral("[T] ") + QString::fromUtf8(sourceText);
        }
        return QString();
    }

    bool isEmpty() const override { return false; }
};

/// Tool with a hostile definition (a duplicate parameter name)
class InvalidParametersTool : public pdf::PDFDocumentTool
{
public:
    QString getId() const override { return QStringLiteral("review.measure"); }

    pdf::PDFDocumentToolParameterDefinitions getParameters() const override
    {
        return { pdf::PDFDocumentToolParameterDefinition::createBool(QStringLiteral("a"), QString(), QString(), false),
                 pdf::PDFDocumentToolParameterDefinition::createBool(QStringLiteral("a"), QString(), QString(), true) };
    }

    pdf::PDFDocumentToolResult execute(const pdf::PDFDocumentToolContext&, const QVariantMap&, const pdf::PDFOperationControl*, pdf::PDFProgress*) const override
    {
        return pdf::PDFDocumentToolResult();
    }
};

/// Tool, whose id is not in the catalog
class UnknownTool : public pdf::PDFDocumentTool
{
public:
    explicit UnknownTool(QString id) : m_id(std::move(id)) { }

    QString getId() const override { return m_id; }
    pdf::PDFDocumentToolParameterDefinitions getParameters() const override { return {}; }

    pdf::PDFDocumentToolResult execute(const pdf::PDFDocumentToolContext&, const QVariantMap&, const pdf::PDFOperationControl*, pdf::PDFProgress*) const override
    {
        return pdf::PDFDocumentToolResult();
    }

private:
    QString m_id;
};

/// Tool of the catalog without parameters, which returns a result inconsistent with its context
class InconsistentTool : public pdf::PDFDocumentTool
{
public:
    explicit InconsistentTool(bool isNullDocument = false) : m_isNullDocument(isNullDocument) { }

    QString getId() const override { return QStringLiteral("convert.blank"); }
    pdf::PDFDocumentToolParameterDefinitions getParameters() const override { return {}; }

    pdf::PDFDocumentToolResult execute(const pdf::PDFDocumentToolContext&, const QVariantMap&, const pdf::PDFOperationControl*, pdf::PDFProgress*) const override
    {
        pdf::PDFDocumentToolResult result;
        pdf::PDFDocumentToolOutputDocument output;
        output.kind = m_isNullDocument ? pdf::PDFDocumentToolOutputDocument::Kind::New : pdf::PDFDocumentToolOutputDocument::Kind::Changed;
        output.inputIndex = 3;
        if (!m_isNullDocument)
        {
            output.document = DocumentToolTestHelper::createDocument({ QSizeF(100, 100) });
        }
        result.documents.push_back(output);
        return result;
    }

private:
    bool m_isNullDocument = false;
};

/// The core of the tools of phase 2 prepared in step 6 (package D): the extended
/// data of the catalog, the suggestions of the next tools, the tool interface,
/// its parameters, results and registry, the test tool
class DocumentToolTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cleanup();

    void catalogExtendedInterface();
    void catalogExtendedData();
    void suggestionsInterface();
    void suggestionsO21();
    void suggestionsHostilePairs();
    void availability();
    void parameterDefinitionsEachType();
    void parameterDefinitionValidity();
    void pageSelection();
    void jsonSchemaExact();
    void validationHostile();
    void validationTranslated();
    void storageValues();
    void resultModel();
    void registryRules();
    void registryThreads();
    void testToolExecute();
    void testToolCancellation();
    void testToolPreview();
};

void DocumentToolTest::cleanup()
{
    pdf::PDFDocumentToolRegistry::clearForTests();
}

void DocumentToolTest::catalogExtendedInterface()
{
    // Every tool has its kinds; the default successors are valid, at most three, never the tool itself
    for (const pdf::PDFDocumentToolCatalogEntry& tool : pdf::PDFDocumentToolCatalog::getTools())
    {
        const QStringList successors = tool.getDefaultSuccessors();
        QVERIFY(successors.size() <= pdf::PDFDocumentToolCatalog::MAXIMUM_DEFAULT_SUCCESSORS);
        QVERIFY(!successors.contains(tool.getId()));
        for (const QString& successor : successors)
        {
            QVERIFY(pdf::PDFDocumentToolCatalog::isValidToolId(successor));
        }
        Q_UNUSED(tool.getInputKind());
        Q_UNUSED(tool.getResultKind());
        Q_UNUSED(tool.getExternalDependency());
        Q_UNUSED(tool.getPdfToolCommand());
    }

    // At most one row of the extended data per tool, only tools of the catalog
    QStringList ids;
    for (const pdf::PDFDocumentToolExtendedData& data : pdf::PDFDocumentToolCatalog::getExtendedData())
    {
        QVERIFY(pdf::PDFDocumentToolCatalog::isValidToolId(data.getId()));
        QVERIFY(!ids.contains(data.getId()));
        ids << data.getId();
        QCOMPARE(pdf::PDFDocumentToolCatalog::findExtendedData(data.getId()), &data);
    }
    QVERIFY(!pdf::PDFDocumentToolCatalog::findExtendedData(u"unknown.tool"));
    QVERIFY(!pdf::PDFDocumentToolCatalog::findExtendedData(QString(1000, QLatin1Char('a'))));
}

void DocumentToolTest::catalogExtendedData()
{
    using Helper = DocumentToolTestHelper;
    using Input = pdf::PDFDocumentToolInputKind;
    using Result = pdf::PDFDocumentToolResultKind;
    using Dependency = pdf::PDFDocumentToolExternalDependency;

    // Every tool has its row, in the order of the catalog
    const std::vector<pdf::PDFDocumentToolCatalogEntry>& tools = pdf::PDFDocumentToolCatalog::getTools();
    const std::vector<pdf::PDFDocumentToolExtendedData>& data = pdf::PDFDocumentToolCatalog::getExtendedData();
    QCOMPARE(data.size(), tools.size());
    for (size_t i = 0; i < tools.size(); ++i)
    {
        QCOMPARE(data[i].getId(), tools[i].getId());
    }

    // Default successors = Then continue with of the *-done frames of Figma (step6_figma_map.md 5), in their order
    const std::map<QString, QStringList> successors =
    {
        { Helper::id("pages.merge"), Helper::ids({ "edit.page-numbers", "optimize.compress", "protect.password" }) },
        { Helper::id("pages.extract"), Helper::ids({ "optimize.compress", "protect.password" }) },
        { Helper::id("pages.interleave"), Helper::ids({ "convert.ocr", "optimize.compress" }) },
        { Helper::id("pages.manage"), Helper::ids({ "edit.page-numbers", "optimize.compress", "protect.password" }) },
        { Helper::id("pages.insert"), Helper::ids({ "edit.page-numbers", "optimize.compress" }) },
        { Helper::id("pages.add-image"), Helper::ids({ "convert.ocr", "optimize.compress" }) },
        { Helper::id("pages.remove"), Helper::ids({ "optimize.compress", "edit.page-numbers" }) },
        { Helper::id("pages.rotate"), Helper::ids({ "convert.ocr", "optimize.compress" }) },
        { Helper::id("pages.crop"), Helper::ids({ "pages.resize", "optimize.compress" }) },
        { Helper::id("pages.resize"), Helper::ids({ "optimize.compress", "edit.page-numbers" }) },
        { Helper::id("pages.boxes"), Helper::ids({ "review.output-preview" }) },
        { Helper::id("convert.images-to-pdf"), Helper::ids({ "optimize.images" }) },
        { Helper::id("convert.scan"), Helper::ids({ "optimize.compress" }) },
        { Helper::id("convert.ocr"), Helper::ids({ "optimize.compress", "convert.extract-text" }) },
        { Helper::id("edit.content"), Helper::ids({ "edit.page-numbers", "optimize.compress" }) },
        { Helper::id("edit.page-numbers"), Helper::ids({ "optimize.compress", "protect.password" }) },
        { Helper::id("protect.sign-by-hand"), Helper::ids({ "protect.password", "optimize.compress" }) },
        { Helper::id("protect.redact"), Helper::ids({ "protect.sanitize", "protect.password", "optimize.compress" }) },
        { Helper::id("protect.sanitize"), Helper::ids({ "protect.password", "optimize.compress" }) },
        { Helper::id("protect.remove-links"), Helper::ids({ "protect.sanitize", "protect.password" }) },
        { Helper::id("optimize.compress"), Helper::ids({ "protect.password" }) },
        { Helper::id("optimize.images"), Helper::ids({ "optimize.structure" }) },
        { Helper::id("optimize.structure"), Helper::ids({ "optimize.images" }) },
        { Helper::id("optimize.bitonal"), Helper::ids({ "convert.ocr" }) },
        { Helper::id("optimize.grayscale"), Helper::ids({ "optimize.compress" }) },
    };

    // Commands of PdfTool (PdfTool/pdftool*.cpp), which a tool may name
    const QStringList pdfToolCommands = Helper::ids({ "attachments", "audio-book", "audio-book-voices", "bitonal", "cert-store", "cert-store-install",
                                                      "color-profiles", "decrypt", "diff", "encrypt", "fetch-images", "fetch-text", "info", "info-fonts",
                                                      "info-inks", "info-javascript", "info-metadata", "info-dests", "info-page-boxes", "info-struct-tree",
                                                      "ink-coverage", "ocr", "ocr-models", "optimize", "redact", "remove-external-links", "render",
                                                      "separate", "statistics", "unite", "verify-signatures", "xml" });

    for (const pdf::PDFDocumentToolCatalogEntry& tool : tools)
    {
        const QString toolId = tool.getId();
        auto it = successors.find(toolId);
        QCOMPARE(tool.getDefaultSuccessors(), it != successors.cend() ? it->second : QStringList());

        // The kinds agree with the access of a locked document (tools.md 4.7)
        switch (tool.getLockedAccess())
        {
            case pdf::PDFDocumentToolLockedAccess::NoDocument:
                QCOMPARE(tool.getInputKind(), Input::None);
                break;
            case pdf::PDFDocumentToolLockedAccess::Available:
                // A tool, which never changes the open document
                QVERIFY2(tool.getResultKind() != Result::ChangeDocument, qPrintable(toolId));
                break;
            case pdf::PDFDocumentToolLockedAccess::Unavailable:
                QVERIFY2(tool.getResultKind() == Result::ChangeDocument || tool.getResultKind() == Result::NewDocument, qPrintable(toolId));
                QVERIFY2(tool.getInputKind() != Input::None, qPrintable(toolId));
                break;
        }

        // Review & Inspect only analyzes
        QCOMPARE(tool.getResultKind() == Result::Analysis, tool.getCategory() == pdf::PDFDocumentToolCategory::Review);

        // The external dependencies
        const Dependency expectedDependency = toolId == QLatin1String("convert.ocr") ? Dependency::Tesseract
                                            : toolId == QLatin1String("convert.scan") ? Dependency::Scanner : Dependency::None;
        QCOMPARE(tool.getExternalDependency(), expectedDependency);

        const QString command = tool.getPdfToolCommand();
        QVERIFY2(command.isEmpty() || pdfToolCommands.contains(command), qPrintable(command));
    }

    // The input kinds of tools.md 5 ("Vstup -> výstup")
    const std::map<QString, Input> inputs =
    {
        { Helper::id("pages.merge"), Input::Multiple },
        { Helper::id("pages.interleave"), Input::Multiple },
        { Helper::id("pages.manage"), Input::Multiple },
        { Helper::id("review.compare"), Input::Multiple },
        { Helper::id("convert.images-to-pdf"), Input::None },
        { Helper::id("convert.blank"), Input::None },
        { Helper::id("convert.scan"), Input::None },
        { Helper::id("protect.certificates"), Input::None },
    };
    for (const pdf::PDFDocumentToolCatalogEntry& tool : tools)
    {
        auto it = inputs.find(tool.getId());
        QCOMPARE(tool.getInputKind(), it != inputs.cend() ? it->second : Input::One);
    }

    // The result kinds of tools.md 5
    const std::map<QString, Result> results =
    {
        { Helper::id("pages.merge"), Result::NewDocument },
        { Helper::id("pages.split"), Result::Files },
        { Helper::id("pages.extract"), Result::NewDocument },
        { Helper::id("pages.interleave"), Result::NewDocument },
        { Helper::id("convert.images-to-pdf"), Result::NewDocument },
        { Helper::id("convert.blank"), Result::NewDocument },
        { Helper::id("convert.scan"), Result::NewDocument },
        { Helper::id("convert.pdf-to-images"), Result::Files },
        { Helper::id("convert.extract-text"), Result::Files },
        { Helper::id("convert.extract-images"), Result::Files },
        { Helper::id("convert.audiobook"), Result::Files },
        { Helper::id("protect.certificates"), Result::Files },
        { Helper::id("protect.redact"), Result::NewDocument },
        // The signing tools write a new signed file, the open document stays (tools.md 4.7, 9.3 point 11, 10.3 point 31)
        { Helper::id("protect.sign"), Result::NewDocument },
        { Helper::id("protect.timestamp"), Result::NewDocument },
    };
    for (const pdf::PDFDocumentToolCatalogEntry& tool : tools)
    {
        auto it = results.find(tool.getId());
        const Result expected = it != results.cend() ? it->second : (tool.getCategory() == pdf::PDFDocumentToolCategory::Review ? Result::Analysis : Result::ChangeDocument);
        QCOMPARE(tool.getResultKind(), expected);
    }

    // The commands of PdfTool of tools.md 5 ("Dnes: ... CLI")
    QCOMPARE(pdf::PDFDocumentToolCatalog::findTool(u"pages.merge")->getPdfToolCommand(), QStringLiteral("unite"));
    QCOMPARE(pdf::PDFDocumentToolCatalog::findTool(u"pages.split")->getPdfToolCommand(), QStringLiteral("separate"));
    QCOMPARE(pdf::PDFDocumentToolCatalog::findTool(u"optimize.compress")->getPdfToolCommand(), QStringLiteral("optimize"));
    QCOMPARE(pdf::PDFDocumentToolCatalog::findTool(u"convert.ocr")->getPdfToolCommand(), QStringLiteral("ocr"));
    QCOMPARE(pdf::PDFDocumentToolCatalog::findTool(u"review.compare")->getPdfToolCommand(), QStringLiteral("diff"));
    QCOMPARE(pdf::PDFDocumentToolCatalog::findTool(u"pages.boxes")->getPdfToolCommand(), QString());
}

void DocumentToolTest::suggestionsInterface()
{
    // Nothing for nothing, at most three, never the tools of fromTools / excluded / unavailable
    pdf::PDFDocumentToolSuggestionInput input;
    QVERIFY(pdf::PDFDocumentToolSuggestions::suggest(input).isEmpty());

    input.fromTools = { QStringLiteral("pages.merge") };
    input.defaultSuccessors = QStringList({ QStringLiteral("edit.page-numbers"), QStringLiteral("optimize.compress"), QStringLiteral("protect.password") });
    // The default successors have equal counters: the order of the catalog (O21)
    QCOMPARE(pdf::PDFDocumentToolSuggestions::suggest(input), QStringList({ QStringLiteral("edit.page-numbers"), QStringLiteral("protect.password"), QStringLiteral("optimize.compress") }));

    input.unavailableTools = { QStringLiteral("optimize.compress") };
    QCOMPARE(pdf::PDFDocumentToolSuggestions::suggest(input), QStringList({ QStringLiteral("edit.page-numbers"), QStringLiteral("protect.password") }));
    input.unavailableTools.clear();

    input.maximumCount = 100;
    QVERIFY(pdf::PDFDocumentToolSuggestions::suggest(input).size() <= pdf::PDFDocumentToolSuggestions::MAXIMUM_SUGGESTIONS);
    input.maximumCount = 0;
    QVERIFY(pdf::PDFDocumentToolSuggestions::suggest(input).isEmpty());
    input.maximumCount = -5;
    QVERIFY(pdf::PDFDocumentToolSuggestions::suggest(input).isEmpty());
    input.maximumCount = 3;

    // The collections editor: the tools of the same category fill the suggestions up
    pdf::PDFDocumentToolSuggestionInput collection;
    collection.fromTools = { QStringLiteral("pages.rotate"), QStringLiteral("pages.crop") };
    collection.defaultSuccessors = QStringList();
    collection.excludedTools = { QStringLiteral("pages.merge") };
    collection.isSameCategoryFilled = true;
    const QStringList filled = pdf::PDFDocumentToolSuggestions::suggest(collection);
    QCOMPARE(filled, QStringList({ QStringLiteral("pages.split"), QStringLiteral("pages.extract"), QStringLiteral("pages.interleave") }));

    // recordPair: the pair of the run, invalid input unchanged
    const std::vector<pdf::PDFDocumentToolPair> recorded = pdf::PDFDocumentToolSuggestions::recordPair({}, QStringLiteral("pages.merge"), QStringLiteral("optimize.compress"), 10);
    QVERIFY(std::any_of(recorded.cbegin(), recorded.cend(), [](const pdf::PDFDocumentToolPair& pair)
                        { return pair.toTool == QLatin1String("optimize.compress") && pair.count >= 1 && pair.lastUsed == 10; }));
    QVERIFY(pdf::PDFDocumentToolSuggestions::recordPair({}, QStringLiteral("pages.merge"), QStringLiteral("pages.merge"), 10).empty());
    QVERIFY(pdf::PDFDocumentToolSuggestions::recordPair({}, QStringLiteral("unknown"), QStringLiteral("pages.merge"), 10).empty());

    // Aging over the limit halves the counters and removes the zeros
    std::vector<pdf::PDFDocumentToolPair> pairs;
    pairs.push_back({ QStringLiteral("pages.merge"), QStringLiteral("optimize.compress"), 50, 1 });
    pairs.push_back({ QStringLiteral("pages.merge"), QStringLiteral("protect.password"), 1, 1 });
    const std::vector<pdf::PDFDocumentToolPair> aged = pdf::PDFDocumentToolSuggestions::age(pairs);
    QCOMPARE(aged.size(), size_t(1));
    QCOMPARE(aged.front().count, qint64(25));
    pairs.pop_back();
    QCOMPARE(pdf::PDFDocumentToolSuggestions::age(pairs), pairs);
}

void DocumentToolTest::suggestionsO21()
{
    using Helper = DocumentToolTestHelper;
    using Suggestions = pdf::PDFDocumentToolSuggestions;

    // Defaults of the catalog until the first pair: equal counters, the order of the catalog
    pdf::PDFDocumentToolSuggestionInput input;
    input.fromTools = Helper::ids({ "pages.merge" });
    QCOMPARE(Suggestions::suggest(input), Helper::ids({ "edit.page-numbers", "protect.password", "optimize.compress" }));

    // First write of a pair: the defaults are inserted with 2, the pair of the user has 1
    std::vector<pdf::PDFDocumentToolPair> pairs = Suggestions::recordPair({}, Helper::id("pages.merge"), Helper::id("review.compare"), 100);
    QCOMPARE(pairs.size(), size_t(4));
    QCOMPARE(Helper::findPair(pairs, "edit.page-numbers")->count, Suggestions::DEFAULT_SUCCESSOR_COUNT);
    QCOMPARE(Helper::findPair(pairs, "optimize.compress")->count, Suggestions::DEFAULT_SUCCESSOR_COUNT);
    QCOMPARE(Helper::findPair(pairs, "protect.password")->count, Suggestions::DEFAULT_SUCCESSOR_COUNT);
    QCOMPARE(Helper::findPair(pairs, "review.compare")->count, qint64(1));
    QCOMPARE(Helper::findPair(pairs, "review.compare")->lastUsed, qint64(100));

    // A single attempt is not suggested
    input.pairs = pairs;
    QCOMPARE(Suggestions::suggest(input), Helper::ids({ "edit.page-numbers", "protect.password", "optimize.compress" }));

    // Second use: the counter 2 ties with the defaults, the defaults win the tie (O25) -
    // also against the newer last use of the pair of the user
    pairs = Suggestions::recordPair(pairs, Helper::id("pages.merge"), Helper::id("review.compare"), 200);
    input.pairs = pairs;
    QCOMPARE(Helper::findPair(pairs, "review.compare")->count, qint64(2));
    QCOMPARE(Suggestions::suggest(input), Helper::ids({ "edit.page-numbers", "protect.password", "optimize.compress" }));

    // Third use: ahead of every default, the last default (in the order of the catalog) is replaced
    pairs = Suggestions::recordPair(pairs, Helper::id("pages.merge"), Helper::id("review.compare"), 300);
    input.pairs = pairs;
    QCOMPARE(Helper::findPair(pairs, "review.compare")->count, qint64(3));
    QCOMPARE(Suggestions::suggest(input), Helper::ids({ "review.compare", "edit.page-numbers", "protect.password" }));

    // A default run again is a pair too (its counter grows from 2)
    pairs = Suggestions::recordPair(pairs, Helper::id("pages.merge"), Helper::id("optimize.compress"), 400);
    pairs = Suggestions::recordPair(pairs, Helper::id("pages.merge"), Helper::id("optimize.compress"), 500);
    input.pairs = pairs;
    QCOMPARE(Helper::findPair(pairs, "optimize.compress")->count, qint64(4));
    QCOMPARE(Suggestions::suggest(input), Helper::ids({ "optimize.compress", "review.compare", "edit.page-numbers" }));

    // Ties: the newer last use, then the order of the catalog
    pdf::PDFDocumentToolSuggestionInput ties;
    ties.fromTools = Helper::ids({ "review.compare" });
    ties.pairs = { Helper::pair("review.compare", "review.report", 3, 10), Helper::pair("review.compare", "pages.split", 3, 10),
                   Helper::pair("review.compare", "review.measure", 3, 20), Helper::pair("review.compare", "pages.merge", 3, 10) };
    QCOMPARE(Suggestions::suggest(ties), Helper::ids({ "review.measure", "pages.merge", "pages.split" }));

    // A tool without defaults: a single attempt suggests nothing, the second use does
    std::vector<pdf::PDFDocumentToolPair> compare = Suggestions::recordPair({}, Helper::id("review.compare"), Helper::id("review.report"), 1);
    QCOMPARE(compare.size(), size_t(1));
    pdf::PDFDocumentToolSuggestionInput once;
    once.fromTools = Helper::ids({ "review.compare" });
    once.pairs = compare;
    QVERIFY(Suggestions::suggest(once).isEmpty());
    once.pairs = Suggestions::recordPair(compare, Helper::id("review.compare"), Helper::id("review.report"), 2);
    QCOMPARE(Suggestions::suggest(once), Helper::ids({ "review.report" }));

    // Aging: a sum of 50 is kept, 51 halves every counter of the tool (rounded down), zeros are removed
    std::vector<pdf::PDFDocumentToolPair> heavy = { Helper::pair("review.compare", "review.report", 48, 1), Helper::pair("review.compare", "review.measure", 1, 1) };
    heavy = Suggestions::recordPair(heavy, Helper::id("review.compare"), Helper::id("review.report"), 2);
    QCOMPARE(heavy.size(), size_t(2));
    QCOMPARE(Helper::findPair(heavy, "review.report")->count, qint64(49));
    heavy = Suggestions::recordPair(heavy, Helper::id("review.compare"), Helper::id("review.report"), 3);
    QCOMPARE(heavy.size(), size_t(1));
    QCOMPARE(Helper::findPair(heavy, "review.report")->count, qint64(25));
    QCOMPARE(Helper::findPair(heavy, "review.report")->lastUsed, qint64(3));
    QVERIFY(!Helper::findPair(heavy, "review.measure"));

    // The run, which ages the counters, is never lost: a new successor of a tool at the
    // limit keeps 1 (halved from 1 it would be 0 and dropped)
    std::vector<pdf::PDFDocumentToolPair> full = { Helper::pair("review.compare", "review.report", 50, 1) };
    full = Suggestions::recordPair(full, Helper::id("review.compare"), Helper::id("review.measure"), 9);
    QCOMPARE(full.size(), size_t(2));
    QCOMPARE(Helper::findPair(full, "review.report")->count, qint64(25));
    QCOMPARE(Helper::findPair(full, "review.measure")->count, qint64(1));
    QCOMPARE(Helper::findPair(full, "review.measure")->lastUsed, qint64(9));
    full = Suggestions::recordPair(full, Helper::id("review.compare"), Helper::id("review.measure"), 10);
    QCOMPARE(Helper::findPair(full, "review.measure")->count, qint64(2));

    // An existing pair of 1 increased by its run and halved keeps 1
    const std::vector<pdf::PDFDocumentToolPair> edge = Suggestions::recordPair({ Helper::pair("review.compare", "review.report", 49, 1), Helper::pair("review.compare", "review.measure", 1, 1) },
                                   Helper::id("review.compare"), Helper::id("review.measure"), 11);
    QCOMPARE(Helper::findPair(edge, "review.measure")->count, qint64(1));
    QCOMPARE(Helper::findPair(edge, "review.report")->count, qint64(24));

    // An unavailable tool is skipped, the next one takes its place
    input.unavailableTools = Helper::ids({ "optimize.compress" });
    QCOMPARE(Suggestions::suggest(input), Helper::ids({ "review.compare", "edit.page-numbers", "protect.password" }));
    input.unavailableTools.clear();

    // Excluded tools and the tool itself are never suggested
    input.excludedTools = Helper::ids({ "review.compare" });
    QCOMPARE(Suggestions::suggest(input), Helper::ids({ "optimize.compress", "edit.page-numbers", "protect.password" }));
    input.excludedTools.clear();

    // Setting off: only the defaults, the counters of the user are not used
    input.isSuggestNextEnabled = false;
    QCOMPARE(Suggestions::suggest(input), Helper::ids({ "edit.page-numbers", "protect.password", "optimize.compress" }));
    input.isSuggestNextEnabled = true;

    // A collection: the counters of its tools are summed
    pdf::PDFDocumentToolSuggestionInput collection;
    collection.fromTools = Helper::ids({ "pages.rotate", "pages.crop", "pages.interleave", "pages.remove" });
    QCOMPARE(Suggestions::suggest(collection), Helper::ids({ "optimize.compress", "convert.ocr", "pages.resize" }));
    collection.excludedTools = Helper::ids({ "optimize.compress", "convert.ocr", "pages.resize", "edit.page-numbers" });
    collection.isSameCategoryFilled = true;
    QCOMPARE(Suggestions::suggest(collection), Helper::ids({ "pages.merge", "pages.split", "pages.extract" }));
}

void DocumentToolTest::suggestionsHostilePairs()
{
    using Helper = DocumentToolTestHelper;
    using Suggestions = pdf::PDFDocumentToolSuggestions;

    // Invalid ids, a tool with itself, counters 0 / negative / huge, duplicates: the first valid row of a pair wins
    pdf::PDFDocumentToolSuggestionInput input;
    input.fromTools = Helper::ids({ "review.compare", "unknown.tool", "review.compare", "" });
    input.pairs = { Helper::pair("review.compare", "unknown.tool", 100, 1), Helper::pair("review.compare", "review.compare", 100, 1),
                    Helper::pair("review.compare", "review.report", 0, 1), Helper::pair("review.compare", "review.measure", -5, 1),
                    Helper::pair("review.compare", "pages.split", std::numeric_limits<qint64>::max(), 1),
                    Helper::pair("review.compare", "pages.split", 3, 1),
                    Helper::pair("review.compare", "pages.merge", 5, std::numeric_limits<qint64>::max()),
                    Helper::pair("unknown.tool", "pages.extract", 1000, 1), Helper::pair("pages.merge", "pages.extract", 1000, 1) };
    QCOMPARE(Suggestions::suggest(input), Helper::ids({ "pages.split", "pages.merge" }));

    // Saturated sums of a collection never overflow
    pdf::PDFDocumentToolSuggestionInput saturated;
    saturated.fromTools = Helper::ids({ "review.compare", "review.measure" });
    saturated.pairs = { Helper::pair("review.compare", "pages.split", std::numeric_limits<qint64>::max(), 1),
                        Helper::pair("review.measure", "pages.split", std::numeric_limits<qint64>::max(), 1),
                        Helper::pair("review.measure", "pages.merge", 2, 1) };
    QCOMPARE(Suggestions::suggest(saturated), Helper::ids({ "pages.split", "pages.merge" }));

    // Hostile default successors given by the caller: the tool itself, unknown ids, duplicates
    pdf::PDFDocumentToolSuggestionInput defaults;
    defaults.fromTools = Helper::ids({ "pages.merge" });
    defaults.defaultSuccessors = Helper::ids({ "pages.merge", "unknown", "review.report", "review.report", "pages.split", "pages.extract", "pages.manage" });
    QCOMPARE(Suggestions::suggest(defaults), Helper::ids({ "pages.split", "pages.extract", "review.report" }));

    // recordPair keeps only the valid rows of the tool and saturates a huge counter
    std::vector<pdf::PDFDocumentToolPair> recorded = Suggestions::recordPair(input.pairs, Helper::id("review.compare"), Helper::id("pages.split"), -7);
    for (const pdf::PDFDocumentToolPair& pair : recorded)
    {
        QCOMPARE(pair.fromTool, Helper::id("review.compare"));
        QVERIFY(pair.count > 0);
        QVERIFY(pdf::PDFDocumentToolCatalog::isValidToolId(pair.toTool));
    }
    QCOMPARE(Helper::findPair(recorded, "pages.split")->lastUsed, qint64(0));

    // A list of 10 000 rows (the read limit of the repository) is fast
    std::vector<pdf::PDFDocumentToolPair> many;
    const std::vector<pdf::PDFDocumentToolCatalogEntry>& tools = pdf::PDFDocumentToolCatalog::getTools();
    for (int i = 0; i < 10000; ++i)
    {
        many.push_back({ tools[size_t(i) % tools.size()].getId(), tools[size_t(i * 7 + 3) % tools.size()].getId(), i % 9, i });
    }
    pdf::PDFDocumentToolSuggestionInput large;
    for (const pdf::PDFDocumentToolCatalogEntry& tool : tools)
    {
        large.fromTools << tool.getId();
    }
    large.pairs = many;
    QElapsedTimer timer;
    timer.start();
    const QStringList result = Suggestions::suggest(large);
    QVERIFY(result.isEmpty());   // every tool is a tool of the collection
    qInfo("suggestions of 46 tools over 10000 pairs: %lld ms", qint64(timer.elapsed()));
}

void DocumentToolTest::availability()
{
    using Helper = DocumentToolTestHelper;
    using Suggestions = pdf::PDFDocumentToolSuggestions;

    pdf::PDFDocumentToolAvailability all;
    QVERIFY(Suggestions::getUnavailableTools(all).isEmpty());
    QVERIFY(!Suggestions::isAvailable(Helper::id("unknown.tool"), all));

    pdf::PDFDocumentToolAvailability locked;
    locked.isDocumentLocked = true;
    const QStringList lockedTools = Suggestions::getUnavailableTools(locked);
    for (const pdf::PDFDocumentToolCatalogEntry& tool : pdf::PDFDocumentToolCatalog::getTools())
    {
        QCOMPARE(lockedTools.contains(tool.getId()), tool.getLockedAccess() == pdf::PDFDocumentToolLockedAccess::Unavailable);
    }

    pdf::PDFDocumentToolAvailability noTesseract;
    noTesseract.isTesseractAvailable = false;
    QCOMPARE(Suggestions::getUnavailableTools(noTesseract), Helper::ids({ "convert.ocr" }));

    pdf::PDFDocumentToolAvailability noScanner;
    noScanner.isScannerAvailable = false;
    QCOMPARE(Suggestions::getUnavailableTools(noScanner), Helper::ids({ "convert.scan" }));

    // Without a document only the tools without an input document can run
    pdf::PDFDocumentToolAvailability noDocument;
    noDocument.hasDocument = false;
    const QStringList noDocumentTools = Suggestions::getUnavailableTools(noDocument);
    for (const pdf::PDFDocumentToolCatalogEntry& tool : pdf::PDFDocumentToolCatalog::getTools())
    {
        QCOMPARE(noDocumentTools.contains(tool.getId()), tool.getInputKind() != pdf::PDFDocumentToolInputKind::None);
    }

    // Then continue with after OCR of a locked document without Tesseract
    pdf::PDFDocumentToolSuggestionInput input;
    input.fromTools = Helper::ids({ "pages.rotate" });
    input.unavailableTools = Suggestions::getUnavailableTools(noTesseract);
    QCOMPARE(Suggestions::suggest(input), Helper::ids({ "optimize.compress" }));
}

void DocumentToolTest::parameterDefinitionsEachType()
{
    using Helper = DocumentToolTestHelper;
    using Type = pdf::PDFDocumentToolParameterType;

    pdftest::TestDocumentTool tool;
    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QString error;
    QVERIFY2(pdf::PDFDocumentToolParameters::isValid(definitions, &error), qPrintable(error));

    // One parameter of each type
    QList<Type> types;
    for (const pdf::PDFDocumentToolParameterDefinition& definition : definitions)
    {
        QVERIFY(!types.contains(definition.getType()));
        types << definition.getType();
    }
    QCOMPARE(types.size(), 11);

    auto find = [&definitions](const char* name) -> const pdf::PDFDocumentToolParameterDefinition&
    {
        return *pdf::PDFDocumentToolParameters::find(definitions, QString::fromLatin1(name));
    };

    // Defaults: canonical types
    const QVariantMap defaults = pdf::PDFDocumentToolParameters::getDefaultValues(definitions);
    QCOMPARE(defaults.size(), 11);
    QCOMPARE(defaults.value(QStringLiteral("detailed")).typeId(), int(QMetaType::Bool));
    QCOMPARE(defaults.value(QStringLiteral("repeat")).typeId(), int(QMetaType::LongLong));
    QCOMPARE(defaults.value(QStringLiteral("scale")).typeId(), int(QMetaType::Double));
    QCOMPARE(defaults.value(QStringLiteral("color")).typeId(), int(QMetaType::QColor));
    QCOMPARE(defaults.value(QStringLiteral("pages")).typeId(), int(QMetaType::QVariantMap));
    QCOMPARE(defaults.value(QStringLiteral("password")).toString(), QString());

    // Bool
    QCOMPARE(Helper::normalized(find("detailed"), true), QVariant(true));
    QVERIFY(!Helper::normalizeError(find("detailed"), 1).isEmpty());
    QVERIFY(!Helper::normalizeError(find("detailed"), QStringLiteral("true")).isEmpty());

    // Integer from integer types and integral doubles of JSON
    QCOMPARE(Helper::normalized(find("repeat"), 5), QVariant(qint64(5)));
    QCOMPARE(Helper::normalized(find("repeat"), 7.0), QVariant(qint64(7)));
    QCOMPARE(Helper::normalized(find("repeat"), QVariant::fromValue(quint16(100))), QVariant(qint64(100)));
    QCOMPARE(Helper::normalized(find("repeat"), QVariant::fromValue(short(3))), QVariant(qint64(3)));
    QCOMPARE(Helper::normalized(find("repeat"), QVariant::fromValue(long(4))), QVariant(qint64(4)));
    QCOMPARE(Helper::normalized(find("repeat"), QVariant::fromValue(uint(5))), QVariant(qint64(5)));
    QCOMPARE(Helper::normalized(find("repeat"), QVariant::fromValue(ulong(6))), QVariant(qint64(6)));
    QCOMPARE(Helper::normalized(find("repeat"), QVariant::fromValue(qulonglong(7))), QVariant(qint64(7)));
    QCOMPARE(Helper::normalized(find("repeat"), QVariant::fromValue(float(8.0f))), QVariant(qint64(8)));
    QVERIFY(!Helper::normalizeError(find("repeat"), 7.5).isEmpty());
    QVERIFY(!Helper::normalizeError(find("repeat"), 0).isEmpty());
    QVERIFY(!Helper::normalizeError(find("repeat"), 101).isEmpty());
    QVERIFY(!Helper::normalizeError(find("repeat"), true).isEmpty());

    // Real
    QCOMPARE(Helper::normalized(find("scale"), 2), QVariant(2.0));
    QCOMPARE(Helper::normalized(find("scale"), 0.25), QVariant(0.25));
    QCOMPARE(Helper::normalized(find("scale"), QVariant::fromValue(float(1.5f))), QVariant(1.5));
    QCOMPARE(Helper::normalized(find("scale"), QVariant::fromValue(qulonglong(3))), QVariant(3.0));
    QVERIFY(!Helper::normalizeError(find("scale"), 0.2).isEmpty());

    // Enum
    QCOMPARE(Helper::normalized(find("mode"), QStringLiteral("title")), QVariant(QStringLiteral("title")));
    QVERIFY(!Helper::normalizeError(find("mode"), QStringLiteral("Title")).isEmpty());
    QCOMPARE(find("mode").getOptions().size(), size_t(7));
    QCOMPARE(find("mode").getOptions().front().iconName, QStringLiteral("hash"));

    // PageSelection from a text and from an object
    const QVariant selection = Helper::normalized(find("pages"), QStringLiteral(" 1-3, 5 "));
    QCOMPARE(selection.toMap().value(QStringLiteral("pages")).toString(), QStringLiteral("1-3, 5"));
    QVariantMap object;
    object.insert(QStringLiteral("pages"), QStringLiteral("2-"));
    object.insert(QStringLiteral("filters"), QVariantList({ QStringLiteral("odd"), QStringLiteral("landscape") }));
    const QVariantMap canonical = Helper::normalized(find("pages"), object).toMap();
    QCOMPARE(canonical.value(QStringLiteral("filters")).toStringList(), Helper::ids({ "odd", "landscape" }));

    // Color
    QCOMPARE(Helper::normalized(find("color"), QStringLiteral("#FF8000")), QVariant(QColor(255, 128, 0)));
    QCOMPARE(Helper::normalized(find("color"), QColor(10, 20, 30, 40)), QVariant(QColor(10, 20, 30)));
    QVERIFY(!Helper::normalizeError(find("color"), QStringLiteral("red")).isEmpty());
    QVERIFY(!Helper::normalizeError(find("color"), QStringLiteral("#ff8000ff")).isEmpty());
    QVERIFY(!Helper::normalizeError(find("color"), QColor()).isEmpty());

    // Texts, paths, a file name template and a secret
    QCOMPARE(Helper::normalized(find("title"), QStringLiteral("Report\nwith lines")), QVariant(QStringLiteral("Report\nwith lines")));
    QCOMPARE(Helper::normalized(find("reportFile"), QStringLiteral("C:/Reports/a.txt")), QVariant(QStringLiteral("C:/Reports/a.txt")));
    QVERIFY(!Helper::normalizeError(find("reportFile"), QStringLiteral("C:/Reports/a\n.txt")).isEmpty());
    QCOMPARE(Helper::normalized(find("outputFolder"), QStringLiteral("/home/user/out")), QVariant(QStringLiteral("/home/user/out")));
    QCOMPARE(Helper::normalized(find("fileName"), QStringLiteral("{name}-{page}.png")), QVariant(QStringLiteral("{name}-{page}.png")));
    QVERIFY(!Helper::normalizeError(find("fileName"), QStringLiteral("../{name}.png")).isEmpty());
    QVERIFY(!Helper::normalizeError(find("fileName"), QStringLiteral("a\\b.png")).isEmpty());

    // Review RS6: a share of the network, marks of right-to-left texts and dots inside
    // of a name stay valid (the run decides about a network path); a device path, a
    // drive or a stream of a template does not - QDir::filePath keeps "C:name" itself
    QCOMPARE(Helper::normalized(find("outputFolder"), QStringLiteral("\\\\server\\share\\out")), QVariant(QStringLiteral("\\\\server\\share\\out")));
    QCOMPARE(Helper::normalized(find("fileName"), QStringLiteral("\u05D0\u200F{name}..v2.pdf")), QVariant(QStringLiteral("\u05D0\u200F{name}..v2.pdf")));
    QVERIFY(Helper::normalizeError(find("reportFile"), QStringLiteral("\\\\.\\pipe\\x")).contains(QStringLiteral("device")));
    QVERIFY(!Helper::normalizeError(find("fileName"), QStringLiteral("C:{name}.pdf")).isEmpty());
#ifdef Q_OS_WIN
    QCOMPARE(QDir(QStringLiteral("D:/out")).filePath(QStringLiteral("C:name.pdf")), QStringLiteral("C:name.pdf"));
#endif
    QCOMPARE(Helper::normalized(find("password"), QStringLiteral("s3cr3t")), QVariant(QStringLiteral("s3cr3t")));
    QVERIFY(!find("password").isStored());
    QVERIFY(find("password").toJsonValue(QStringLiteral("s3cr3t")).isNull());
    QVERIFY(find("title").isStored());

    // The whole set: the defaults of the missing values
    QVariantMap values;
    values.insert(QStringLiteral("repeat"), 3);
    QVariantMap normalizedValues;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(definitions, values, &normalizedValues).empty());
    QCOMPARE(normalizedValues.size(), 11);
    QCOMPARE(normalizedValues.value(QStringLiteral("repeat")), QVariant(qint64(3)));
    QCOMPARE(normalizedValues.value(QStringLiteral("mode")), QVariant(QStringLiteral("count")));

    // JSON of canonical values: no secret, a color as text, a page selection as an object
    normalizedValues.insert(QStringLiteral("password"), QStringLiteral("s3cr3t"));
    const QJsonObject json = pdf::PDFDocumentToolParameters::toJson(definitions, normalizedValues);
    QVERIFY(!json.contains(QStringLiteral("password")));
    QCOMPARE(json.value(QStringLiteral("color")).toString(), QStringLiteral("#2563eb"));
    QVERIFY(json.value(QStringLiteral("pages")).isObject());
    QCOMPARE(json.value(QStringLiteral("repeat")).toInteger(), qint64(3));

    // A JSON round trip gives the same canonical values (MCP)
    QVariantMap roundTrip;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(definitions, json, &roundTrip).empty());
    normalizedValues.insert(QStringLiteral("password"), QString());
    QCOMPARE(roundTrip, normalizedValues);
}

void DocumentToolTest::parameterDefinitionValidity()
{
    using Definition = pdf::PDFDocumentToolParameterDefinition;

    // Names and option ids
    QVERIFY(Definition::isValidName(QStringLiteral("pageRange_2")));
    for (const char* name : { "", "2pages", "_a", "a-b", "a b", "á", "a.b" })
    {
        QVERIFY2(!Definition::isValidName(QString::fromUtf8(name)), name);
    }
    QVERIFY(!Definition::isValidName(QString(65, QLatin1Char('a'))));
    QVERIFY(Definition::isValidOptionId(QStringLiteral("jpeg-2000_x")));
    for (const char* id : { "", "-a", "a-", "A", "a b", "a.b" })
    {
        QVERIFY2(!Definition::isValidOptionId(QString::fromLatin1(id)), id);
    }

    // Invalid definitions
    QString error;
    QVERIFY(!Definition::createBool(QStringLiteral("1a"), QString(), QString(), false).isValid(&error));
    QVERIFY(!Definition::createInteger(QStringLiteral("a"), QString(), QString(), 5, 10, 1).isValid(&error));
    QVERIFY(!Definition::createInteger(QStringLiteral("a"), QString(), QString(), 50, 1, 10).isValid(&error));
    QVERIFY(!Definition::createReal(QStringLiteral("a"), QString(), QString(), 1.0, std::nan(""), 10.0).isValid(&error));
    QVERIFY(!Definition::createReal(QStringLiteral("a"), QString(), QString(), 1.0, 0.0, std::numeric_limits<double>::infinity()).isValid(&error));
    QVERIFY(!Definition::createEnum(QStringLiteral("a"), QString(), QString(), {}, QString()).isValid(&error));
    QVERIFY(!Definition::createEnum(QStringLiteral("a"), QString(), QString(), { { QStringLiteral("x"), QString(), QString(), QString() }, { QStringLiteral("x"), QString(), QString(), QString() } }, QStringLiteral("x")).isValid(&error));
    QVERIFY(!Definition::createEnum(QStringLiteral("a"), QString(), QString(), { { QStringLiteral("x"), QString(), QString(), QString() } }, QStringLiteral("y")).isValid(&error));
    QVERIFY(!Definition::createColor(QStringLiteral("a"), QString(), QString(), QColor()).isValid(&error));
    QVERIFY(!Definition::createString(QStringLiteral("a"), QString(), QString(), QString(20, QLatin1Char('x')), 10).isValid(&error));
    QVERIFY(!Definition::createFileNameTemplate(QStringLiteral("a"), QString(), QString(), QStringLiteral("a/b")).isValid(&error));
    QVERIFY(!Definition::createPageSelection(QStringLiteral("a"), QString(), QString(), { QStringLiteral("0-3"), {} }).isValid(&error));
    QVERIFY(!Definition::createPageSelection(QStringLiteral("a"), QString(), QString(), { QStringLiteral(" 1"), {} }).isValid(&error));

    // A required parameter needs no valid default; its missing value is an error
    const Definition required = Definition::createFilePath(QStringLiteral("input"), QStringLiteral("Input"), QString()).setRequired(true);
    QVERIFY(required.isValid(&error));
    QVariant value;
    QVERIFY(!required.normalize(QVariant(), &value).isEmpty());
    QVERIFY(!required.normalize(QString(), &value).isEmpty());
    QVERIFY(required.normalize(QStringLiteral("/a.pdf"), &value).isEmpty());

    // Maximum lengths are bounded by the type
    QCOMPARE(Definition::createString(QStringLiteral("a"), QString(), QString(), QString(), 1000000).getMaximumLength(), Definition::MAXIMUM_TEXT_LENGTH);
    QCOMPARE(Definition::createString(QStringLiteral("a"), QString(), QString(), QString(), -3).getMaximumLength(), 1);
    QCOMPARE(Definition::createSecret(QStringLiteral("a"), QString(), QString()).setMaximumLength(5000).getMaximumLength(), Definition::MAXIMUM_SECRET_LENGTH);
    QCOMPARE(Definition::createBool(QStringLiteral("a"), QString(), QString(), true).setMaximumLength(5).getMaximumLength(), 0);

    // Sets: duplicates, an invalid definition and too many
    QVERIFY(!pdf::PDFDocumentToolParameters::isValid(InvalidParametersTool().getParameters(), &error));
    QVERIFY(!pdf::PDFDocumentToolParameters::isValid({ Definition::createBool(QStringLiteral("a"), QString(), QString(), false), Definition() }, &error));
    pdf::PDFDocumentToolParameterDefinitions many;
    for (int i = 0; i <= pdf::PDFDocumentToolParameters::MAXIMUM_VALUE_COUNT; ++i)
    {
        many.push_back(Definition::createBool(QStringLiteral("p%1").arg(i), QString(), QString(), false));
    }
    QVERIFY(!pdf::PDFDocumentToolParameters::isValid(many, &error));

    // A default-constructed definition is not valid
    QVERIFY(!Definition().isValid(&error));
}

void DocumentToolTest::pageSelection()
{
    using Selection = pdf::PDFDocumentToolPageSelection;
    using Filter = Selection::Filter;

    const QSizeF portrait(100, 200);
    const QSizeF landscape(200, 100);
    const std::vector<QSizeF> sizes = { portrait, landscape, portrait, landscape, portrait, portrait };
    QString error;

    auto indices = [&sizes, &error](const QString& pages, Selection::Filters filters)
    {
        Selection selection;
        selection.pages = pages;
        selection.filters = filters;
        return selection.getPageIndices(sizes, &error);
    };

    using Indices = std::vector<pdf::PDFInteger>;
    QCOMPARE(indices(QString(), Filter::None), Indices({ 0, 1, 2, 3, 4, 5 }));
    QCOMPARE(indices(QStringLiteral("2-4, 6"), Filter::None), Indices({ 1, 2, 3, 5 }));
    QCOMPARE(indices(QStringLiteral("-2,5-"), Filter::None), Indices({ 0, 1, 4, 5 }));
    QCOMPARE(indices(QString(), Filter::Even), Indices({ 1, 3, 5 }));
    QCOMPARE(indices(QString(), Filter::Odd), Indices({ 0, 2, 4 }));
    QCOMPARE(indices(QString(), Filter::Even | Filter::Odd), Indices({ 0, 1, 2, 3, 4, 5 }));
    QCOMPARE(indices(QString(), Filter::Landscape), Indices({ 1, 3 }));
    QCOMPARE(indices(QString(), Filter::Portrait), Indices({ 0, 2, 4, 5 }));
    QCOMPARE(indices(QString(), Filter::Portrait | Filter::Landscape), Indices({ 0, 1, 2, 3, 4, 5 }));
    QCOMPARE(indices(QString(), Filter::Odd | Filter::Portrait), Indices({ 0, 2, 4 }));
    QCOMPARE(indices(QStringLiteral("1-4"), Filter::Even | Filter::Landscape), Indices({ 1, 3 }));
    QVERIFY(error.isEmpty());

    // Outside of the document, no page, invalid ranges
    QVERIFY(indices(QStringLiteral("7"), Filter::None).empty());
    QVERIFY(!error.isEmpty());
    QVERIFY(indices(QStringLiteral("0"), Filter::None).empty());
    QVERIFY(indices(QStringLiteral("5-9"), Filter::None).empty());
    QVERIFY(indices(QStringLiteral("1"), Filter::Even).empty());
    QVERIFY(!error.isEmpty());
    QVERIFY(indices(QStringLiteral("x"), Filter::None).empty());
    QVERIFY(indices(QString(5000, QLatin1Char('1')), Filter::None).empty());
    Selection empty;
    QVERIFY(empty.getPageIndices(std::vector<QSizeF>(), &error).empty());
    QVERIFY(!error.isEmpty());
    QVERIFY(empty.getPageIndices(static_cast<const pdf::PDFDocument*>(nullptr), nullptr).empty());

    // A document of the builder
    const pdf::PDFDocumentPointer document = DocumentToolTestHelper::createMixedDocument();
    Selection landscapes;
    landscapes.filters = Filter::Landscape;
    QCOMPARE(landscapes.getPageIndices(document.data(), &error), Indices({ 1, 3 }));

    // Hostile variants
    for (const QVariant& value : { QVariant(), QVariant(5), QVariant(QStringList({ QStringLiteral("1") })), QVariant(QStringLiteral("1--2")),
                                   QVariant(QStringLiteral("3-1")), QVariant(QStringLiteral("99999999999")), QVariant(QStringLiteral("1000000001")),
                                   QVariant(QStringLiteral("-0")), QVariant(QStringLiteral("1;2")), QVariant(QStringLiteral("１")) })
    {
        QVERIFY2(!Selection::fromVariant(value, &error), qPrintable(value.toString()));
        QVERIFY(!error.isEmpty());
    }

    QVariantMap unknownKey;
    unknownKey.insert(QStringLiteral("pagez"), QStringLiteral("1"));
    QVERIFY(!Selection::fromVariant(unknownKey, &error));
    QVariantMap badFilters;
    badFilters.insert(QStringLiteral("filters"), QVariantList({ QStringLiteral("odd"), QStringLiteral("square") }));
    QVERIFY(!Selection::fromVariant(badFilters, &error));
    badFilters.insert(QStringLiteral("filters"), QStringLiteral("odd"));
    QVERIFY(!Selection::fromVariant(badFilters, &error));
    badFilters.insert(QStringLiteral("filters"), QVariantList({ 1 }));
    QVERIFY(!Selection::fromVariant(badFilters, &error));
    QVariantList tooMany;
    for (int i = 0; i < 5; ++i)
    {
        tooMany << QStringLiteral("odd");
    }
    badFilters.insert(QStringLiteral("filters"), tooMany);
    QVERIFY(!Selection::fromVariant(badFilters, &error));
    QVariantMap badPages;
    badPages.insert(QStringLiteral("pages"), 3);
    QVERIFY(!Selection::fromVariant(badPages, &error));

    // Review RS6: a range longer than the pages by a multiple of 2^32 (the length of
    // PDFClosedIntervalSet is summed in an int, SECURITY_TODO 12) is refused, never
    // unfolded - unfold wrote billions of values behind its vector
    QVERIFY(!Selection::fromVariant(QStringLiteral("1-5294967296"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!Selection::fromVariant(QStringLiteral("1-3, 2000000000-6294967295"), &error));
    QVERIFY(indices(QStringLiteral("1-4294967302"), Filter::None).empty());
    QVERIFY(!error.isEmpty());
    QVERIFY(indices(QStringLiteral("1-3, 2000000000-6294967295"), Filter::None).empty());
    QVERIFY(indices(QStringLiteral("4294967297"), Filter::None).empty());

    // Valid special forms and a round trip
    QVERIFY(Selection::fromVariant(QStringLiteral("-"), &error));
    QVERIFY(Selection::fromVariant(QStringLiteral("1000000000"), &error));
    QVariantMap nullPages;
    nullPages.insert(QStringLiteral("pages"), QVariant::fromValue(nullptr));
    QCOMPARE(Selection::fromVariant(nullPages, nullptr)->pages, QString());
    Selection original;
    original.pages = QStringLiteral("1,3");
    original.filters = Filter::Even | Filter::Portrait;
    QCOMPARE(Selection::fromVariant(original.toVariant(), &error), std::optional<Selection>(original));

    // Review RC6-2: a placeholder in an untrusted range (a selection built without
    // fromVariant) never takes the number of the pages of the message
    Selection placeholder;
    placeholder.pages = QStringLiteral("%1");
    QVERIFY(placeholder.getPageIndices(std::vector<QSizeF>(3, QSizeF(100, 200)), &error).empty());
    QVERIFY2(error.contains(QStringLiteral("“%1”")) && error.contains(QStringLiteral("1 to 3")) && !error.contains(QStringLiteral("%2")), qPrintable(error));
}

void DocumentToolTest::jsonSchemaExact()
{
    pdftest::TestDocumentTool tool;
    const QJsonObject schema = pdf::PDFDocumentToolParameters::toJsonSchema(tool.getParameters());

    const char* expectedText = R"({
        "type": "object",
        "additionalProperties": false,
        "properties": {
            "pages": {
                "type": "object", "title": "Pages", "description": "Pages to count",
                "properties": {
                    "pages": { "type": "string", "maxLength": 4096, "description": "Pages numbered from 1, such as \"1-3, 5, 8-\"; empty = all pages" },
                    "filters": { "type": "array", "items": { "type": "string", "enum": [ "even", "odd", "portrait", "landscape" ] }, "uniqueItems": true,
                                 "description": "Even / odd pages and portrait / landscape pages; filters of one kind are joined, the two kinds narrow each other" }
                },
                "additionalProperties": false,
                "default": { "pages": "", "filters": [] }
            },
            "mode": {
                "type": "string", "title": "Mode", "enum": [ "count", "title", "nothing", "fail", "throw", "throw-memory", "throw-std" ],
                "description": "What the tool does\n- count: Count pages (Counts the selected pages)\n- title: Set title\n- nothing: Nothing to do\n- fail: Fail\n- throw: Throw\n- throw-memory: Throw bad_alloc\n- throw-std: Throw std::exception",
                "default": "count"
            },
            "detailed": { "type": "boolean", "title": "Detailed", "description": "List the page numbers", "default": false },
            "repeat": { "type": "integer", "title": "Repeat", "description": "Number of passes", "minimum": 1, "maximum": 100, "default": 1 },
            "scale": { "type": "number", "title": "Scale", "minimum": 0.25, "maximum": 4, "default": 1 },
            "title": { "type": "string", "title": "Title", "description": "Title of the document", "maxLength": 200, "default": "Statistics" },
            "color": { "type": "string", "title": "Color", "pattern": "^#[0-9a-fA-F]{6}$", "default": "#2563eb" },
            "reportFile": { "type": "string", "title": "Report file", "maxLength": 4096, "default": "" },
            "outputFolder": { "type": "string", "title": "Output folder", "maxLength": 4096, "default": "" },
            "fileName": { "type": "string", "title": "File name", "maxLength": 65536, "default": "{name}-statistics.txt" },
            "password": { "type": "string", "title": "Password", "maxLength": 1024, "writeOnly": true }
        }
    })";

    QJsonParseError parseError;
    const QJsonDocument expected = QJsonDocument::fromJson(QByteArray(expectedText), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY2(schema == expected.object(), QJsonDocument(schema).toJson(QJsonDocument::Indented).constData());

    // Never a combination of schemas at the top level (some clients refuse it)
    for (const char* key : { "oneOf", "anyOf", "allOf", "not", "if" })
    {
        QVERIFY(!schema.contains(QString::fromLatin1(key)));
    }

    // A required parameter is listed and has no default
    const pdf::PDFDocumentToolParameterDefinitions required = { pdf::PDFDocumentToolParameterDefinition::createFilePath(QStringLiteral("input"), QString(), QString()).setRequired(true) };
    const QJsonObject requiredSchema = pdf::PDFDocumentToolParameters::toJsonSchema(required);
    QCOMPARE(requiredSchema.value(QStringLiteral("required")).toArray(), QJsonArray({ QStringLiteral("input") }));
    QVERIFY(!requiredSchema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("input")).toObject().contains(QStringLiteral("default")));
    QVERIFY(!requiredSchema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("input")).toObject().contains(QStringLiteral("title")));
    QVERIFY(!pdf::PDFDocumentToolParameters::toJsonSchema({}).contains(QStringLiteral("required")));
}

void DocumentToolTest::validationHostile()
{
    pdftest::TestDocumentTool tool;
    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    auto errorsOf = [&definitions](const char* name, const QVariant& value)
    {
        QVariantMap values;
        values.insert(QString::fromLatin1(name), value);
        QVariantMap normalized;
        normalized.insert(QStringLiteral("stale"), 1);
        const std::vector<pdf::PDFDocumentToolParameterError> errors = pdf::PDFDocumentToolParameters::normalize(definitions, values, &normalized);
        if (!errors.empty() && !normalized.isEmpty())
        {
            return std::vector<pdf::PDFDocumentToolParameterError>({ { QStringLiteral("internal"), QStringLiteral("values kept after an error") } });
        }
        return errors;
    };

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const QString megabyte(1024 * 1024, QLatin1Char('x'));
    const std::vector<std::pair<const char*, QVariant>> hostile =
    {
        { "repeat", nan }, { "repeat", infinity }, { "repeat", 1e300 }, { "repeat", -1e300 }, { "repeat", 9007199254740993.0 },
        { "repeat", QVariant(std::numeric_limits<quint64>::max()) }, { "repeat", QStringLiteral("5") }, { "repeat", QVariantList({ 5 }) },
        { "scale", nan }, { "scale", infinity }, { "scale", -infinity }, { "scale", 1e300 }, { "scale", QStringLiteral("1.0") }, { "scale", false },
        { "detailed", 0 }, { "detailed", QStringLiteral("false") }, { "detailed", QVariantMap() },
        { "title", megabyte }, { "title", 5 }, { "title", QByteArray("bytes") }, { "title", QString(QChar(0)) },
        { "title", QString(QChar(0xD800)) + QStringLiteral("x") }, { "title", QString(QChar(0xDC00)) },
        { "reportFile", megabyte }, { "reportFile", QStringLiteral("a\u0001b") }, { "reportFile", QStringLiteral("a\u0085b") },
        { "outputFolder", QStringLiteral("a\tb") }, { "fileName", QStringLiteral("a/b") }, { "fileName", megabyte },
        // Review RS6: device namespaces of Windows, bidirectional controls of a spoofed
        // name, a template leaving its folder by a drive, a stream or a name of dots
        { "reportFile", QStringLiteral("\\\\.\\PhysicalDrive0") }, { "reportFile", QStringLiteral("//?/C:/a.txt") }, { "outputFolder", QStringLiteral("\\??\\C:\\out") },
        { "outputFolder", QStringLiteral("\\\\?\\GLOBALROOT\\Device") }, { "reportFile", QStringLiteral("C:/report\u202Etxt.exe") }, { "outputFolder", QStringLiteral("C:/a\u2067b") },
        { "fileName", QStringLiteral("C:{name}.pdf") }, { "fileName", QStringLiteral("{name}.pdf:stream") }, { "fileName", QStringLiteral("..") },
        { "fileName", QStringLiteral(". .") }, { "fileName", QStringLiteral("{name}\u202E.pdf") },
        { "password", QString(2000, QLatin1Char('p')) }, { "password", 1234 },
        { "mode", QString(10000, QLatin1Char('m')) }, { "mode", QStringLiteral("count ") }, { "mode", 1 },
        { "color", QStringLiteral("#12345") }, { "color", QStringLiteral("#1234567") }, { "color", QStringLiteral("#12345g") }, { "color", 0xFF0000 },
        { "pages", QStringLiteral("0") }, { "pages", QStringLiteral("-5-") }, { "pages", QStringLiteral("1-2-3") }, { "pages", QStringLiteral("99999999999") },
        { "pages", QString(5000, QLatin1Char('1')) }, { "pages", QStringLiteral("a") }, { "pages", 3 }, { "pages", QVariantList() },
    };

    for (const auto& item : hostile)
    {
        const std::vector<pdf::PDFDocumentToolParameterError> errors = errorsOf(item.first, item.second);
        QVERIFY2(errors.size() == 1, item.first);
        QCOMPARE(errors.front().parameter, QString::fromLatin1(item.first));
        QVERIFY(!errors.front().message.isEmpty());

        // The message never repeats a long untrusted value
        QVERIFY2(errors.front().message.size() < 200, qPrintable(errors.front().message.left(300)));
    }

    // Untrusted text in a message is sanitized and bounded
    const std::vector<pdf::PDFDocumentToolParameterError> modeErrors = errorsOf("mode", QStringLiteral("\u202Eevil\u0007\u2028text that is long enough to be elided in the message"));
    QCOMPARE(modeErrors.size(), size_t(1));
    QVERIFY(!modeErrors.front().message.contains(QChar(0x202E)));
    QVERIFY(!modeErrors.front().message.contains(QChar(0x0007)));
    QVERIFY(!modeErrors.front().message.contains(QChar(0x2028)));
    QVERIFY(modeErrors.front().message.contains(QChar(0x2026)));
    QCOMPARE(pdf::PDFDocumentToolParameterDefinition::sanitizeForMessage(QStringLiteral("abc"), 0), QString(QChar(0x2026)));
    QCOMPARE(pdf::PDFDocumentToolParameterDefinition::sanitizeForMessage(QStringLiteral("abc"), 3), QStringLiteral("abc"));
    QCOMPARE(pdf::PDFDocumentToolParameterDefinition::sanitizeForMessage(QStringLiteral("abcdef"), 3), QStringLiteral("ab\u2026"));

    // Unknown keys (bounded number of errors), too many keys, null of JSON = missing
    QVariantMap unknown;
    for (int i = 0; i < 100; ++i)
    {
        unknown.insert(QStringLiteral("unknown%1").arg(i), i);
    }
    QVariantMap normalized;
    const std::vector<pdf::PDFDocumentToolParameterError> unknownErrors = pdf::PDFDocumentToolParameters::normalize(definitions, unknown, &normalized);
    QCOMPARE(unknownErrors.size(), size_t(16));
    QVERIFY(normalized.isEmpty());

    QVariantMap tooMany;
    for (int i = 0; i < 300; ++i)
    {
        tooMany.insert(QStringLiteral("k%1").arg(i), i);
    }
    QCOMPARE(pdf::PDFDocumentToolParameters::normalize(definitions, tooMany, &normalized).size(), size_t(1));

    QJsonObject jsonTooMany;
    for (int i = 0; i < 300; ++i)
    {
        jsonTooMany.insert(QStringLiteral("k%1").arg(i), i);
    }
    normalized.insert(QStringLiteral("stale"), 1);
    QCOMPARE(pdf::PDFDocumentToolParameters::normalize(definitions, jsonTooMany, &normalized).size(), size_t(1));
    QVERIFY(normalized.isEmpty());

    // JSON from a client: integers, doubles, null and nested objects
    const QByteArray json = R"({ "repeat": 4, "scale": 2.5, "detailed": null, "pages": { "pages": "1-2", "filters": [ "odd" ] }, "color": "#00ff00" })";
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(definitions, QJsonDocument::fromJson(json).object(), &normalized).empty());
    QCOMPARE(normalized.value(QStringLiteral("repeat")), QVariant(qint64(4)));
    QCOMPARE(normalized.value(QStringLiteral("detailed")), QVariant(false));
    QCOMPARE(normalized.value(QStringLiteral("color")), QVariant(QColor(0, 255, 0)));

    const QByteArray hostileJson = R"({ "repeat": 1e300, "pages": { "pages": { "nested": [ [ [ ] ] ] } }, "mode": [ "count" ] })";
    QCOMPARE(pdf::PDFDocumentToolParameters::normalize(definitions, QJsonDocument::fromJson(hostileJson).object(), &normalized).size(), size_t(3));

    // Random values of random types never crash and never pass a wrong type
    QRandomGenerator random(20261026);
    for (int i = 0; i < 2000; ++i)
    {
        const pdf::PDFDocumentToolParameterDefinition& definition = definitions[random.bounded(int(definitions.size()))];
        QVariant value;
        switch (random.bounded(6))
        {
            case 0: value = random.generateDouble() * std::pow(10.0, random.bounded(-5, 30)); break;
            case 1: value = qint64(random.generate64()); break;
            case 2: value = bool(random.bounded(2)); break;
            case 3:
            {
                QString text;
                const int length = random.bounded(64);
                for (int j = 0; j < length; ++j)
                {
                    text += QChar(char16_t(random.bounded(0x10000)));
                }
                value = text;
                break;
            }
            case 4: value = QVariantList({ random.bounded(10) }); break;
            default: value = QVariant(); break;
        }

        QVariant result;
        if (definition.normalize(value, &result).isEmpty())
        {
            QVariant again;
            QVERIFY(definition.normalize(result, &again).isEmpty());
            QCOMPARE(again, result);
        }
    }

}

void DocumentToolTest::validationTranslated()
{
    DocumentToolTestTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));

    pdftest::TestDocumentTool tool;
    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QVariantMap values;
    values.insert(QStringLiteral("repeat"), QStringLiteral("x"));
    values.insert(QStringLiteral("pages"), QStringLiteral("0"));
    values.insert(QStringLiteral("whatever"), 1);
    QVariantMap normalized;
    const std::vector<pdf::PDFDocumentToolParameterError> errors = pdf::PDFDocumentToolParameters::normalize(definitions, values, &normalized);
    QCOMPARE(errors.size(), size_t(3));
    for (const pdf::PDFDocumentToolParameterError& error : errors)
    {
        QVERIFY2(error.message.startsWith(QStringLiteral("[T] ")), qPrintable(error.message));
    }

    // The checked run translates its own messages too
    const pdf::PDFDocumentToolResult result = tool.run(pdf::PDFDocumentToolContext(), QVariantMap(), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("[T] ")), qPrintable(result.message));

    QVERIFY(QCoreApplication::removeTranslator(&translator));
}

void DocumentToolTest::storageValues()
{
    pdftest::TestDocumentTool tool;
    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();

    // Every stored type survives the round trip through the storage values
    QVariantMap values;
    values.insert(QStringLiteral("pages"), QStringLiteral("2-3"));
    values.insert(QStringLiteral("mode"), QStringLiteral("title"));
    values.insert(QStringLiteral("detailed"), true);
    values.insert(QStringLiteral("repeat"), 9);
    values.insert(QStringLiteral("scale"), 0.5);
    values.insert(QStringLiteral("title"), QStringLiteral("Title é中"));
    values.insert(QStringLiteral("color"), QStringLiteral("#102030"));
    values.insert(QStringLiteral("reportFile"), QStringLiteral("C:/a.txt"));
    values.insert(QStringLiteral("outputFolder"), QStringLiteral("C:/out"));
    values.insert(QStringLiteral("fileName"), QStringLiteral("{name}.txt"));
    values.insert(QStringLiteral("password"), QStringLiteral("secret"));
    QVariantMap normalized;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(definitions, values, &normalized).empty());

    for (const pdf::PDFDocumentToolParameterDefinition& definition : definitions)
    {
        const QVariant stored = definition.toStorageValue(normalized.value(definition.getName()));
        if (!definition.isStored())
        {
            QVERIFY(!stored.isValid());
            QVERIFY(!definition.fromStorageValue(QStringLiteral("secret")));
            continue;
        }

        // The database gives integers as qint64
        const QVariant fromDatabase = stored.typeId() == QMetaType::LongLong ? QVariant(stored.toLongLong()) : stored;
        const std::optional<QVariant> restored = definition.fromStorageValue(fromDatabase);
        QVERIFY2(restored.has_value(), qPrintable(definition.getName()));
        QCOMPARE(*restored, normalized.value(definition.getName()));
    }

    // Hostile stored values are refused (the repository uses the default)
    auto find = [&definitions](const char* name) -> const pdf::PDFDocumentToolParameterDefinition&
    {
        return *pdf::PDFDocumentToolParameters::find(definitions, QString::fromLatin1(name));
    };
    QVERIFY(!find("detailed").fromStorageValue(qint64(2)));
    QVERIFY(!find("detailed").fromStorageValue(QStringLiteral("1")));
    QVERIFY(!find("repeat").fromStorageValue(qint64(1000)));
    QVERIFY(!find("repeat").fromStorageValue(1.0));
    QVERIFY(!find("scale").fromStorageValue(QStringLiteral("1")));
    QVERIFY(!find("scale").fromStorageValue(1e10));
    QCOMPARE(*find("scale").fromStorageValue(qint64(2)), QVariant(2.0));
    QVERIFY(!find("mode").fromStorageValue(QStringLiteral("unknown")));
    QVERIFY(!find("title").fromStorageValue(qint64(1)));
    QVERIFY(!find("color").fromStorageValue(QStringLiteral("blue")));
    QVERIFY(!find("pages").fromStorageValue(QStringLiteral("{")));
    QVERIFY(!find("pages").fromStorageValue(QStringLiteral("[1]")));
    QVERIFY(!find("pages").fromStorageValue(QStringLiteral("{\"pages\":\"0\"}")));
    QVERIFY(!find("pages").fromStorageValue(QString(20000, QLatin1Char(' '))));
    QVERIFY(!find("pages").fromStorageValue(qint64(1)));
    QVERIFY(!find("title").fromStorageValue(QVariant()));
    QVERIFY(!find("title").fromStorageValue(QVariant::fromValue(nullptr)));

}

void DocumentToolTest::resultModel()
{
    using Result = pdf::PDFDocumentToolResult;
    using Status = pdf::PDFDocumentToolResultStatus;

    QCOMPARE(Result().status, Status::Success);
    QVERIFY(Result().isSuccess());
    QVERIFY(Result().isUse());

    const Result failed = Result::createFailed(QStringLiteral("Broken"));
    QCOMPARE(failed.status, Status::Failed);
    QCOMPARE(failed.message, QStringLiteral("Broken"));
    QVERIFY(!failed.isUse());

    const Result stopped = Result::createStopped();
    QCOMPARE(stopped.status, Status::Stopped);
    QVERIFY(!stopped.isUse());
    QVERIFY(!stopped.message.isEmpty());

    const Result nothing = Result::createNothingToDo(QStringLiteral("File cannot be reduced"));
    QCOMPARE(nothing.status, Status::NothingToDo);
    QVERIFY(nothing.isUse());
    QVERIFY(!nothing.isSuccess());

    QCOMPARE(Result::getStatusName(Status::Success), QStringLiteral("success"));
    QCOMPARE(Result::getStatusName(Status::Stopped), QStringLiteral("stopped"));
    QCOMPARE(Result::getStatusName(Status::Failed), QStringLiteral("failed"));
    QCOMPARE(Result::getStatusName(Status::NothingToDo), QStringLiteral("nothing-to-do"));
    QCOMPARE(Result::getStatusName(Status(99)), QString());

    // Structured content of MCP
    Result result;
    result.message = QStringLiteral("Done");
    pdf::PDFDocumentToolOutputDocument changed;
    changed.document = DocumentToolTestHelper::createDocument({ QSizeF(100, 100), QSizeF(100, 100) });
    changed.flags = pdf::PDFModifiedDocument::ModificationFlags(pdf::PDFModifiedDocument::PageContents) | pdf::PDFModifiedDocument::Annotation;
    changed.suggestedFileName = QStringLiteral("out.pdf");
    result.documents.push_back(changed);
    pdf::PDFDocumentToolOutputDocument created;
    created.kind = pdf::PDFDocumentToolOutputDocument::Kind::New;
    created.flags = pdf::PDFModifiedDocument::ModificationFlags(0x1FF);
    result.documents.push_back(created);
    result.writtenFiles << QStringLiteral("/tmp/a.png");
    result.facts.push_back({ QStringLiteral("fileSize"), QStringLiteral("File size"), QStringLiteral("24.8 MB → 6.1 MB") });
    for (int state = 0; state < 5; ++state)
    {
        result.steps.push_back({ QStringLiteral("check"), QStringLiteral("Step %1").arg(state), pdf::PDFDocumentToolStep::State(state) });
    }
    result.data.insert(QStringLiteral("ratio"), 0.25);

    const QJsonObject json = result.toJson();
    QCOMPARE(json.value(QStringLiteral("status")).toString(), QStringLiteral("success"));
    QCOMPARE(json.value(QStringLiteral("message")).toString(), QStringLiteral("Done"));
    const QJsonArray documents = json.value(QStringLiteral("documents")).toArray();
    QCOMPARE(documents.size(), 2);
    QCOMPARE(documents[0].toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("changed"));
    QCOMPARE(documents[0].toObject().value(QStringLiteral("inputIndex")).toInt(), 0);
    QCOMPARE(documents[0].toObject().value(QStringLiteral("pageCount")).toInt(), 2);
    QCOMPARE(documents[0].toObject().value(QStringLiteral("flags")).toArray(), QJsonArray({ QStringLiteral("pageContents"), QStringLiteral("annotation") }));
    QCOMPARE(documents[0].toObject().value(QStringLiteral("suggestedFileName")).toString(), QStringLiteral("out.pdf"));
    QCOMPARE(documents[1].toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("new"));
    QVERIFY(!documents[1].toObject().contains(QStringLiteral("inputIndex")));
    QCOMPARE(documents[1].toObject().value(QStringLiteral("pageCount")).toInt(), 0);
    QCOMPARE(documents[1].toObject().value(QStringLiteral("flags")).toArray().size(), 9);
    QCOMPARE(json.value(QStringLiteral("files")).toArray(), QJsonArray({ QStringLiteral("/tmp/a.png") }));
    QCOMPARE(json.value(QStringLiteral("facts")).toArray().first().toObject().value(QStringLiteral("value")).toString(), QStringLiteral("24.8 MB → 6.1 MB"));
    const QJsonArray steps = json.value(QStringLiteral("steps")).toArray();
    QCOMPARE(steps.size(), 5);
    QCOMPARE(steps[0].toObject().value(QStringLiteral("state")).toString(), QStringLiteral("done"));
    QCOMPARE(steps[1].toObject().value(QStringLiteral("state")).toString(), QStringLiteral("warning"));
    QCOMPARE(steps[2].toObject().value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
    QCOMPARE(steps[3].toObject().value(QStringLiteral("state")).toString(), QStringLiteral("skipped"));
    QCOMPARE(steps[4].toObject().value(QStringLiteral("state")).toString(), QString());
    QCOMPARE(json.value(QStringLiteral("data")).toObject().value(QStringLiteral("ratio")).toDouble(), 0.25);
    QVERIFY(!Result::createFailed(QString()).toJson().contains(QStringLiteral("message")));
}

void DocumentToolTest::registryRules()
{
    using Registry = pdf::PDFDocumentToolRegistry;
    using Registration = pdf::PDFDocumentToolRegistration;

    QVERIFY(Registry::getImplementedTools().isEmpty());
    QCOMPARE(Registry::registerTool(nullptr), Registration::NullTool);
    QCOMPARE(Registry::registerTool(std::make_shared<UnknownTool>(QStringLiteral("review.unknown"))), Registration::UnknownTool);
    QCOMPARE(Registry::registerTool(std::make_shared<UnknownTool>(QString(100000, QLatin1Char('a')))), Registration::UnknownTool);
    QCOMPARE(Registry::registerTool(std::make_shared<UnknownTool>(QStringLiteral("Review.Statistics"))), Registration::UnknownTool);
    QCOMPARE(Registry::registerTool(std::make_shared<InvalidParametersTool>()), Registration::InvalidParameters);

    std::shared_ptr<pdftest::TestDocumentTool> tool = std::make_shared<pdftest::TestDocumentTool>();
    QCOMPARE(Registry::registerTool(tool), Registration::Registered);
    QCOMPARE(Registry::registerTool(std::make_shared<pdftest::TestDocumentTool>()), Registration::Duplicate);
    QCOMPARE(Registry::registerTool(std::make_shared<InconsistentTool>()), Registration::Registered);

    // Catalog order, lookups, hostile ids
    QCOMPARE(Registry::getImplementedTools(), QStringList({ QStringLiteral("convert.blank"), QStringLiteral("review.statistics") }));
    QCOMPARE(Registry::getTool(u"review.statistics").get(), tool.get());
    QVERIFY(Registry::isImplemented(u"review.statistics"));
    QVERIFY(!Registry::isImplemented(u"optimize.compress"));
    QVERIFY(!Registry::getTool(u""));
    QVERIFY(!Registry::getTool(QString(1000000, QLatin1Char('x'))));
    QVERIFY(!Registry::isImplemented(QString(QStringLiteral("review.statistics") + QChar(0))));

    // A tool held by a caller survives its removal
    std::weak_ptr<const pdf::PDFDocumentTool> weak = Registry::getTool(u"convert.blank");
    std::shared_ptr<const pdf::PDFDocumentTool> held = weak.lock();
    QVERIFY(Registry::unregisterTool(u"convert.blank"));
    QVERIFY(!Registry::unregisterTool(u"convert.blank"));
    QVERIFY(!Registry::unregisterTool(u"unknown.tool"));
    QVERIFY(!weak.expired());
    QCOMPARE(held->getId(), QStringLiteral("convert.blank"));
    held.reset();
    QVERIFY(weak.expired());

    // The checked run of a tool, whose result does not fit its context
    const pdf::PDFDocumentToolResult inconsistent = InconsistentTool().run(pdf::PDFDocumentToolContext(), QVariantMap(), nullptr, nullptr);
    QCOMPARE(inconsistent.status, pdf::PDFDocumentToolResultStatus::Failed);
    QCOMPARE(InconsistentTool(true).run(pdf::PDFDocumentToolContext(), QVariantMap(), nullptr, nullptr).status, pdf::PDFDocumentToolResultStatus::Failed);

    // A tool of an unknown id cannot run
    QCOMPARE(UnknownTool(QStringLiteral("x.y")).run(pdf::PDFDocumentToolContext(), QVariantMap(), nullptr, nullptr).status, pdf::PDFDocumentToolResultStatus::Failed);
    QCOMPARE(InvalidParametersTool().run(pdf::PDFDocumentToolContext(), QVariantMap(), nullptr, nullptr).status, pdf::PDFDocumentToolResultStatus::Failed);
    QCOMPARE(InvalidParametersTool().runPreview(pdf::PDFDocumentToolContext(), QVariantMap(), nullptr).status, pdf::PDFDocumentToolResultStatus::Failed);

    Registry::clearForTests();
    QVERIFY(Registry::getImplementedTools().isEmpty());
    QVERIFY(!Registry::getTool(u"review.statistics"));
}

void DocumentToolTest::registryThreads()
{
    using Registry = pdf::PDFDocumentToolRegistry;

    // 8 threads read while the main thread registers and removes the tools
    std::atomic<bool> stop = false;
    std::atomic<int> found = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back([&stop, &found]()
        {
            while (!stop.load())
            {
                std::shared_ptr<const pdf::PDFDocumentTool> tool = Registry::getTool(u"review.statistics");
                if (tool)
                {
                    // The tool is usable while held
                    if (tool->getId() == QLatin1String("review.statistics") && tool->getParameters().size() == 11)
                    {
                        ++found;
                    }
                }
                const QStringList implemented = Registry::getImplementedTools();
                if (implemented.size() > 2)
                {
                    found = -1000000;
                }
                Q_UNUSED(Registry::isImplemented(u"convert.blank"));
            }
        });
    }

    for (int i = 0; i < 2000; ++i)
    {
        Registry::registerTool(std::make_shared<pdftest::TestDocumentTool>());
        Registry::registerTool(std::make_shared<InconsistentTool>());
        Registry::unregisterTool(u"review.statistics");
        if (i % 3 == 0)
        {
            Registry::clearForTests();
        }
    }
    Registry::clearForTests();
    Registry::registerTool(std::make_shared<pdftest::TestDocumentTool>());

    // Let the readers see the final state (valgrind runs the threads one after another:
    // the limit honors PDF4QT_TEST_TIMEOUT_FACTOR)
    const qint64 timeLimit = qint64(30000) * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    QElapsedTimer timer;
    timer.start();
    while (found.load() <= 0 && timer.elapsed() < timeLimit)
    {
        QThread::yieldCurrentThread();
    }

    stop = true;
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    QVERIFY(found.load() > 0);
    QCOMPARE(Registry::getImplementedTools(), QStringList({ QStringLiteral("review.statistics") }));
}

void DocumentToolTest::testToolExecute()
{
    using Status = pdf::PDFDocumentToolResultStatus;

    pdftest::TestDocumentTool tool;
    QVERIFY(tool.getInfo());
    QCOMPARE(tool.getInfo()->getId(), QStringLiteral("review.statistics"));
    QCOMPARE(tool.getInfo()->getResultKind(), pdf::PDFDocumentToolResultKind::Analysis);

    const pdf::PDFDocumentPointer document = DocumentToolTestHelper::createMixedDocument();
    const pdf::PDFDocumentToolContext context = DocumentToolTestHelper::createContext(document);

    // Count all pages with the defaults
    pdf::PDFDocumentToolResult result = tool.run(context, QVariantMap(), nullptr, nullptr);
    QCOMPARE(result.status, Status::Success);
    QCOMPARE(result.data.value(QStringLiteral("selectedPages")).toInt(), 5);
    QCOMPARE(result.data.value(QStringLiteral("pageCount")).toInt(), 5);
    QCOMPARE(result.facts.size(), size_t(1));
    QCOMPARE(result.facts.front().value, QStringLiteral("5"));
    QVERIFY(result.documents.empty());

    // A page selection with filters, repeated passes, the page numbers
    QVariantMap pages;
    pages.insert(QStringLiteral("pages"), QStringLiteral("2-5"));
    pages.insert(QStringLiteral("filters"), QVariantList({ QStringLiteral("even") }));
    QVariantMap parameters;
    parameters.insert(QStringLiteral("pages"), pages);
    parameters.insert(QStringLiteral("repeat"), 3);
    parameters.insert(QStringLiteral("detailed"), true);

    pdf::PDFProgress progress(nullptr);
    QSignalSpy started(&progress, &pdf::PDFProgress::progressStarted);
    QSignalSpy steps(&progress, &pdf::PDFProgress::progressStep);
    QSignalSpy finished(&progress, &pdf::PDFProgress::progressFinished);
    result = tool.run(context, parameters, nullptr, &progress);
    QCOMPARE(result.status, Status::Success);
    QCOMPARE(result.data.value(QStringLiteral("counted")).toInt(), 6);
    QCOMPARE(result.data.value(QStringLiteral("pages")).toArray(), QJsonArray({ 2, 4 }));
    QCOMPARE(started.size(), 1);
    QCOMPARE(finished.size(), 1);
    QVERIFY(!steps.isEmpty());
    QCOMPARE(steps.last().value(0).toInt(), 100);

    // Copy only: the new version is a separate document, the input is unchanged
    parameters.clear();
    parameters.insert(QStringLiteral("mode"), QStringLiteral("title"));
    parameters.insert(QStringLiteral("title"), QStringLiteral("Changed"));
    result = tool.run(context, parameters, nullptr, nullptr);
    QCOMPARE(result.status, Status::Success);
    QCOMPARE(result.documents.size(), size_t(1));
    QVERIFY(result.documents.front().document != document);
    QCOMPARE(result.documents.front().document->getInfo()->title, QStringLiteral("Changed"));
    QCOMPARE(document->getInfo()->title, QStringLiteral("Original"));
    QCOMPARE(context.getDocument(), document.data());
    QCOMPARE(result.toJson().value(QStringLiteral("documents")).toArray().first().toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("changed"));

    // NothingToDo, Failed, an exception
    parameters.insert(QStringLiteral("mode"), QStringLiteral("nothing"));
    QCOMPARE(tool.run(context, parameters, nullptr, nullptr).status, Status::NothingToDo);
    parameters.insert(QStringLiteral("mode"), QStringLiteral("fail"));
    QCOMPARE(tool.run(context, parameters, nullptr, nullptr).message, QStringLiteral("Failed on purpose"));
    parameters.insert(QStringLiteral("mode"), QStringLiteral("throw"));
    result = tool.run(context, parameters, nullptr, nullptr);
    QCOMPARE(result.status, Status::Failed);
    QCOMPARE(result.message, QStringLiteral("Thrown on purpose"));
    parameters.insert(QStringLiteral("mode"), QStringLiteral("throw-memory"));
    result = tool.run(context, parameters, nullptr, nullptr);
    QCOMPARE(result.status, Status::Failed);
    QVERIFY(result.message.contains(QStringLiteral("memory")));
    parameters.insert(QStringLiteral("mode"), QStringLiteral("throw-std"));
    result = tool.run(context, parameters, nullptr, nullptr);
    QCOMPARE(result.status, Status::Failed);
    QVERIFY(result.message.contains(QStringLiteral("unexpected")));

    // A selection outside of the document fails in the tool
    parameters.clear();
    parameters.insert(QStringLiteral("pages"), QStringLiteral("7-9"));
    result = tool.run(context, parameters, nullptr, nullptr);
    QCOMPARE(result.status, Status::Failed);
    QVERIFY(!result.message.isEmpty());

    // Invalid parameters and contexts never reach execute
    const int executeCount = tool.getExecuteCount();
    parameters.clear();
    parameters.insert(QStringLiteral("repeat"), 1000);
    parameters.insert(QStringLiteral("bogus"), true);
    result = tool.run(context, parameters, nullptr, nullptr);
    QCOMPARE(result.status, Status::Failed);
    QCOMPARE(result.data.value(QStringLiteral("errors")).toArray().size(), 2);
    QCOMPARE(result.message.count(QLatin1Char('\n')), 1);

    pdf::PDFDocumentToolContext twoDocuments = context;
    twoDocuments.documents.push_back(twoDocuments.documents.front());
    QCOMPARE(tool.run(twoDocuments, QVariantMap(), nullptr, nullptr).status, Status::Failed);
    QCOMPARE(tool.run(pdf::PDFDocumentToolContext(), QVariantMap(), nullptr, nullptr).status, Status::Failed);
    pdf::PDFDocumentToolContext nullDocument;
    nullDocument.documents.push_back(pdf::PDFDocumentToolInputDocument());
    const std::vector<pdf::PDFDocumentToolParameterError> errors = tool.validate(nullDocument, QVariantMap());
    QCOMPARE(errors.size(), size_t(1));
    QCOMPARE(tool.getExecuteCount(), executeCount);

    // Input kinds of other tools: none needs nothing, multiple needs at least one
    InconsistentTool blank;
    QVERIFY(blank.validate(pdf::PDFDocumentToolContext(), QVariantMap()).empty());
    QVERIFY(blank.validate(twoDocuments, QVariantMap()).empty());
    UnknownTool merge(QStringLiteral("pages.merge"));
    QCOMPARE(merge.validate(pdf::PDFDocumentToolContext(), QVariantMap()).size(), size_t(1));
    QVERIFY(merge.validate(twoDocuments, QVariantMap()).empty());

    // execute is reentrant: 8 threads run the same instance at once
    std::vector<std::thread> threads;
    std::atomic<int> successes = 0;
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back([&tool, &context, &successes]()
        {
            for (int j = 0; j < 20; ++j)
            {
                QVariantMap threadParameters;
                threadParameters.insert(QStringLiteral("repeat"), 5);
                if (tool.run(context, threadParameters, nullptr, nullptr).data.value(QStringLiteral("counted")).toInt() == 25)
                {
                    ++successes;
                }
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    QCOMPARE(successes.load(), 160);
}

void DocumentToolTest::testToolCancellation()
{
    using Status = pdf::PDFDocumentToolResultStatus;

    pdftest::TestDocumentTool tool;
    const pdf::PDFDocumentToolContext context = DocumentToolTestHelper::createContext(DocumentToolTestHelper::createMixedDocument());

    // Canceled before the run: execute is not called
    pdftest::TestOperationControl canceled(0);
    const int executeCount = tool.getExecuteCount();
    QCOMPARE(tool.run(context, QVariantMap(), &canceled, nullptr).status, Status::Stopped);
    QCOMPARE(tool.getExecuteCount(), executeCount);
    QCOMPARE(tool.runPreview(context, QVariantMap(), &canceled).status, Status::Stopped);

    // Canceled during the run (at the third page of the first pass): Stopped, no facts
    pdftest::TestOperationControl during(3);
    QVariantMap parameters;
    parameters.insert(QStringLiteral("repeat"), 100);
    const pdf::PDFDocumentToolResult result = tool.run(context, parameters, &during, nullptr);
    QCOMPARE(result.status, Status::Stopped);
    QVERIFY(result.facts.empty());
    QCOMPARE(tool.getExecuteCount(), executeCount + 1);
    QVERIFY(during.getQuestionCount() < 10);

    // A result of a run canceled after its last page is not used either
    pdftest::TestOperationControl late(6);
    QCOMPARE(tool.run(context, QVariantMap(), &late, nullptr).status, Status::Stopped);

    // A failure stays a failure
    pdftest::TestOperationControl lateFail(1);
    parameters.clear();
    parameters.insert(QStringLiteral("mode"), QStringLiteral("fail"));
    QCOMPARE(tool.run(context, parameters, &lateFail, nullptr).status, Status::Failed);

    // A control, which never cancels
    pdftest::TestOperationControl never(-1);
    QCOMPARE(tool.run(context, QVariantMap(), &never, nullptr).status, Status::Success);
}

void DocumentToolTest::testToolPreview()
{
    pdftest::TestDocumentTool tool;
    QVERIFY(tool.hasPreview());
    const pdf::PDFDocumentToolContext context = DocumentToolTestHelper::createContext(DocumentToolTestHelper::createMixedDocument());
    const pdf::PDFDocumentToolResult preview = tool.runPreview(context, QVariantMap(), nullptr);
    QCOMPARE(preview.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(preview.facts.size(), size_t(1));
    QCOMPARE(preview.facts.front().value, QStringLiteral("5"));

    // Invalid parameters fail the preview too; the base class has no preview
    QVariantMap parameters;
    parameters.insert(QStringLiteral("color"), QStringLiteral("blue"));
    QCOMPARE(tool.runPreview(context, parameters, nullptr).status, pdf::PDFDocumentToolResultStatus::Failed);

    // Exceptions of the preview are failures
    for (const char* mode : { "throw", "throw-memory", "throw-std" })
    {
        QVariantMap throwing;
        throwing.insert(QStringLiteral("mode"), QString::fromLatin1(mode));
        const pdf::PDFDocumentToolResult failed = tool.runPreview(context, throwing, nullptr);
        QCOMPARE(failed.status, pdf::PDFDocumentToolResultStatus::Failed);
        QVERIFY(!failed.message.isEmpty());
    }

    // A preview canceled while it was computed is not used
    pdftest::TestOperationControl late(1);
    QCOMPARE(tool.runPreview(context, QVariantMap(), &late).status, pdf::PDFDocumentToolResultStatus::Stopped);
    InconsistentTool blank;
    QVERIFY(!blank.hasPreview());
    const pdf::PDFDocumentToolResult empty = blank.runPreview(pdf::PDFDocumentToolContext(), QVariantMap(), nullptr);
    QCOMPARE(empty.status, pdf::PDFDocumentToolResultStatus::Success);
    QVERIFY(empty.facts.empty());
    QVERIFY(empty.documents.empty());
}

QTEST_GUILESS_MAIN(DocumentToolTest)

#include "tst_documenttooltest.moc"
