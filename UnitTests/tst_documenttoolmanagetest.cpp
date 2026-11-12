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

// The tool Manage Pages of the core (step 3b of phase 2, package M1; plan 4.4, 4.5, 7, 8, RB2, RB3 a,
// RB9, QB1, QB3, QB4, QB7). The expectations come from the plan (4.4 the table of the outputs, 4.5,
// 7, 8), the frames (387:17762 R2 "17 from 3 sources", 396:26763 "01_Introduction.pdf …
// 10_Appendix.pdf", "One bookmark per group or run of a file"), tools.md 9.3 point 11, and ISO
// 32000-2 (12.3.3 the outline, 7.7.3.2 the page objects, 7.11.4 the attachments).
//
// The tests of the document parts, of Join and of This document need the parts and the arrangement
// of PDFDocumentMerger (package M2, RB10 a): outlinePartsOfOnePdf, outlineJoinUnderParts,
// perGroupFileHasGroupBookmark, documentArranged, mcpPagesArrangeDocument.

#include "pdfdocumentassembly.h"
#include "pdfdocumenttool_manage.h"
#include "pdfdocumenttool_split.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfobjectutils.h"
#include "pdfsecurityhandler.h"

#include "mergetestpdf.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QRegularExpression>
#include <QtTest>

#include <functional>
#include <set>

using namespace mergetest;

/// A file sink of the memory
class ManageToolTestSink : public pdf::PDFDocumentToolFileSink
{
public:
    ManageToolTestSink() : pdf::PDFDocumentToolFileSink(ExistingFiles::Overwrite) { }

    virtual bool begin(const QStringList& fileNames, QString* errorText) override
    {
        Q_UNUSED(errorText);
        begunNames = fileNames;
        return true;
    }

    virtual bool write(const QString& fileName, const QByteArray& data, QString* errorText) override
    {
        Q_UNUSED(errorText);
        files.emplace_back(fileName, data);
        addWrittenFile(QStringLiteral("/memory/") + fileName);
        return true;
    }

    QStringList begunNames;
    std::vector<std::pair<QString, QByteArray>> files;
};

class DocumentToolManageTest : public QObject
{
    Q_OBJECT

private slots:
    void catalogRowOfManage();
    void outputTraitsByOutput();
    void outputTraitsByOutput_data();
    void parametersAndSchema();
    void filesOfFigmaExample();
    void mcpPagesOfDocument();
    void mcpGivenParameters();
    void sourcesByOrderWithoutIds();
    void runOnePdf();
    void runPerGroupAndPerPage();
    void runPerPageTenThousand();
    void validateSourcesAndPermissions();
    void documentNothingToDoAndChanged();

    // Waiting for PDFDocumentMerger::Settings::parts and arrange (package M2, RB10 a)
    void outlinePartsOfOnePdf();
    void outlineJoinUnderParts();
    void perGroupFileHasGroupBookmark();
    void documentArranged();
    void documentFacts();
    void documentPartsAfterRemoval();
    void documentLockedFieldsFact();
    void documentCropByUserUnit();
    void documentConformanceDeclarations();
    void mcpPagesArrangeDocument();

private:
    using Assembly = pdf::PDFDocumentAssembly;
    using Tool = pdf::PDFDocumentToolManage;

    static QString name(const char* parameter) { return QString::fromLatin1(parameter); }

    static pdf::PDFDocumentToolInputDocument createInput(const MergeTestPdf& pdf, const QString& fileName, const QString& inputId)
    {
        pdf::PDFDocumentToolInputDocument input = pdf::PDFDocumentToolInputDocument::create(pdf.readPointer(), QByteArray(), fileName, true);
        input.inputId = inputId;
        return input;
    }

    static Assembly::Page createPage(int input, pdf::PDFInteger pageIndex)
    {
        Assembly::Page page;
        page.plan.input = input;
        page.plan.pageIndex = pageIndex;
        return page;
    }

    static Assembly::Item createItem(const QString& id, std::vector<Assembly::Page> pages, const QString& itemName = QString(), bool isGroup = false)
    {
        Assembly::Item item;
        item.id = id;
        item.pages = std::move(pages);
        item.name = itemName;
        item.isCreatedGroup = isGroup;
        return item;
    }

    static Assembly::Source createSource(const QString& id, const QString& sourceName, int pageCount)
    {
        Assembly::Source source;
        source.id = id;
        source.name = sourceName;
        source.pageCount = pageCount;
        return source;
    }

    /// Two files: a.pdf (input "a", 3 pages, an attachment, a bookmark "Chapter A2" at its page 2)
    /// and b.pdf (input "b", 2 pages); the assembly: Intro = {A1, A2}, A3, B2, a blank page, B1
    static pdf::PDFDocumentToolContext createTwoFileContext(Assembly* assembly)
    {
        MergeTestPdf a = MergeTestPdf::createPages("A", 3, "/Outlines 30 0 R /Names << /EmbeddedFiles << /Names [(note.txt) 20 0 R] >> >>");
        a.set(30, "<< /Type /Outlines /First 31 0 R /Last 31 0 R /Count 1 >>");
        a.set(31, "<< /Title (Chapter A2) /Parent 30 0 R /Dest [" + MergeTestPdf::page(2) + " /Fit] >>");
        a.set(20, "<< /Type /Filespec /F (note.txt) /EF << /F 21 0 R >> >>");
        a.set(21, MergeTestPdf::stream("ATTACHMENTMARKER", "/Type /EmbeddedFile"));
        const MergeTestPdf b = MergeTestPdf::createPages("B", 2);

        pdf::PDFDocumentToolContext context;
        context.documents.push_back(createInput(a, QStringLiteral("a.pdf"), QStringLiteral("a")));
        context.documents.push_back(createInput(b, QStringLiteral("b.pdf"), QStringLiteral("b")));

        Assembly::Page blank;
        blank.plan.input = Assembly::BLANK_INPUT;
        blank.plan.pageSize = QSizeF(612.0, 792.0);
        QString error;
        *assembly = Assembly::create({ createSource(QStringLiteral("a"), QStringLiteral("a.pdf"), 3), createSource(QStringLiteral("b"), QStringLiteral("b.pdf"), 2) },
                                     { createItem(QStringLiteral("p1"), { createPage(0, 0), createPage(0, 1) }, QStringLiteral("Intro"), true),
                                       createItem(QStringLiteral("p2"), { createPage(0, 2) }),
                                       createItem(QStringLiteral("p3"), { createPage(1, 1) }),
                                       createItem(QStringLiteral("p4"), { blank }),
                                       createItem(QStringLiteral("p5"), { createPage(1, 0) }) },
                                     { }, &error);
        return context;
    }

    static QVariantMap createParameters(const Assembly& assembly, const char* output)
    {
        QVariantMap parameters;
        parameters.insert(name(Tool::PARAMETER_ASSEMBLY), QVariant::fromValue(assembly));
        parameters.insert(name(Tool::PARAMETER_OUTPUT), QString::fromLatin1(output));
        return parameters;
    }

    /// The markers of the contents of the pages ("APAGE1", ...) in their order
    static QStringList getPageMarkers(const pdf::PDFDocument& document)
    {
        QStringList markers;
        for (size_t i = 0; i < document.getCatalog()->getPageCount(); ++i)
        {
            const pdf::PDFDictionary* page = document.getDictionaryFromObject(pdf::PDFObject::createReference(document.getCatalog()->getPage(i)->getPageReference()));
            const pdf::PDFObject& contents = document.getObject(page->get("Contents"));
            const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("% ([A-Z]+PAGE\\d+)")).match(contents.isStream() ? QString::fromLatin1(*contents.getStream()->getContent()) : QString());
            markers << (match.hasMatch() ? match.captured(1) : QStringLiteral("blank"));
        }
        return markers;
    }

    /// The titles of the outline (12.3.3): "Title" and its children "Title/Child", one visited set
    static QStringList getOutlineTitles(const pdf::PDFDocument& document)
    {
        QStringList titles;
        const pdf::PDFDictionary* outlines = MergeTestRead::path(document, { "Outlines" });
        std::set<pdf::PDFObjectReference> visited;
        std::vector<std::pair<pdf::PDFObject, QString>> stack;
        if (outlines)
        {
            stack.emplace_back(outlines->get("First"), QString());
        }
        while (!stack.empty())
        {
            auto [itemObject, prefix] = stack.back();
            stack.pop_back();
            std::vector<std::pair<pdf::PDFObject, QString>> level;
            pdf::PDFObject current = itemObject;
            while (current.isReference() && visited.insert(current.getReference()).second)
            {
                const pdf::PDFDictionary* item = document.getDictionaryFromObject(current);
                if (!item)
                {
                    break;
                }
                const QString title = prefix + MergeTestRead::text(document, item->get("Title"));
                titles << title;
                if (item->get("First").isReference())
                {
                    level.emplace_back(item->get("First"), title + QLatin1Char('/'));
                }
                current = item->get("Next");
            }
            // Children after their parent in the order of the outline is not needed: the titles
            // carry their path
            stack.insert(stack.end(), level.rbegin(), level.rend());
        }
        return titles;
    }

    static int getTimeoutFactor()
    {
        bool ok = false;
        const int factor = qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR", &ok);
        return ok && factor > 0 ? factor : 1;
    }

    /// Runs This document over \p document with the assembly \p assembly; \p facts gets the facts by their ids
    static pdf::PDFDocumentToolResult runDocument(const pdf::PDFDocument& document, const Assembly& assembly, QMap<QString, QString>* facts)
    {
        const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), QByteArray(), QStringLiteral("a.pdf"), true));
        pdf::PDFDocumentToolResult result = Tool().run(context, createParameters(assembly, Tool::OUTPUT_DOCUMENT), nullptr, nullptr);
        facts->clear();
        for (const pdf::PDFDocumentToolFact& fact : result.facts)
        {
            facts->insert(fact.id, fact.value);
        }
        return result;
    }
};

void DocumentToolManageTest::catalogRowOfManage()
{
    using Kind = pdf::PDFDocumentToolResultKind;
    const pdf::PDFDocumentToolCatalogEntry* manage = pdf::PDFDocumentToolCatalog::findTool(u"pages.manage");
    QVERIFY(manage);

    // RB3 a: the row is the output This document of the tab of a document - a change of its pages
    // (bit 11 Assemble, Table 22), Keep, an incremental update of a signed file
    QCOMPARE(manage->getResultKind(), Kind::ChangeDocument);
    QCOMPARE(manage->getPreselectedResult(), pdf::PDFDocumentToolPreselectedResult::Keep);
    QCOMPARE(manage->getSignatureImpact(), pdf::PDFDocumentToolSignatureImpact::IncrementalSave);
    QVERIFY(manage->getRequiredPermissions() == std::vector<pdf::PDFSecurityHandler::Permission>({ pdf::PDFSecurityHandler::Permission::Assemble }));
    QVERIFY(!manage->isNewDocumentOfInputs());

    // 4.4: This document, One PDF, One PDF per group or per page; plan 1 point 7: 1 to 1000 inputs,
    // available for a locked document (only This document is Disabled, 4.5)
    QCOMPARE(manage->getPossibleResultKinds(), std::vector<Kind>({ Kind::ChangeDocument, Kind::NewDocument, Kind::Files }));
    QCOMPARE(manage->getInputCountRange(), (pdf::PDFDocumentToolInputCountRange{ 1, 1000 }));
    QCOMPARE(manage->getLockedAccess(), pdf::PDFDocumentToolLockedAccess::Available);

    // QB1 B: only Manage Pages runs in the tab of a document and has its own tab too
    QStringList documentHosts;
    for (const pdf::PDFDocumentToolCatalogEntry& tool : pdf::PDFDocumentToolCatalog::getTools())
    {
        if (tool.hasDocumentHost())
        {
            QVERIFY(tool.hasOwnTab());
            documentHosts << tool.getId();
        }
    }
    QCOMPARE(documentHosts, QStringList({ QStringLiteral("pages.manage") }));
}

void DocumentToolManageTest::outputTraitsByOutput_data()
{
    QTest::addColumn<QString>("output");
    QTest::addColumn<int>("resultKind");
    QTest::addColumn<int>("preselected");
    QTest::addColumn<int>("signatureImpact");
    QTest::addColumn<bool>("isNewDocumentOfInputs");

    using Kind = pdf::PDFDocumentToolResultKind;
    using Preselected = pdf::PDFDocumentToolPreselectedResult;
    using Signature = pdf::PDFDocumentToolSignatureImpact;

    // 4.4 and 4.5: This document (R1 Keep, the incremental update keeps a signed revision), One PDF
    // (R2 Open in a new tab, 387:17762), the files of per group and per page (R3, 396:27563); the
    // new documents are not encrypted (the rule Q2 of step 2); a missing output is the row of the
    // catalog (This document)
    QTest::newRow("document") << QStringLiteral("document") << int(Kind::ChangeDocument) << int(Preselected::Keep) << int(Signature::IncrementalSave) << false;
    QTest::newRow("missing") << QString() << int(Kind::ChangeDocument) << int(Preselected::Keep) << int(Signature::IncrementalSave) << false;
    QTest::newRow("one-pdf") << QStringLiteral("one-pdf") << int(Kind::NewDocument) << int(Preselected::NewTab) << int(Signature::None) << true;
    QTest::newRow("per-group") << QStringLiteral("per-group") << int(Kind::Files) << int(Preselected::File) << int(Signature::None) << true;
    QTest::newRow("per-page") << QStringLiteral("per-page") << int(Kind::Files) << int(Preselected::File) << int(Signature::None) << true;
}

void DocumentToolManageTest::outputTraitsByOutput()
{
    QFETCH(QString, output);
    QFETCH(int, resultKind);
    QFETCH(int, preselected);
    QFETCH(int, signatureImpact);
    QFETCH(bool, isNewDocumentOfInputs);

    QVariantMap parameters;
    if (!output.isEmpty())
    {
        parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolManage::PARAMETER_OUTPUT), output);
    }

    const pdf::PDFDocumentToolManage tool;
    const pdf::PDFDocumentToolOutputTraits traits = tool.getOutputTraits(parameters);
    QCOMPARE(traits.entry, pdf::PDFDocumentToolCatalog::findTool(u"pages.manage"));
    QCOMPARE(int(traits.resultKind), resultKind);
    QCOMPARE(int(traits.preselectedResult), preselected);
    QCOMPARE(int(traits.signatureImpact), signatureImpact);
    QCOMPARE(traits.isNewDocumentOfInputs, isNewDocumentOfInputs);
    QVERIFY(!tool.hasPreview());
}

void DocumentToolManageTest::parametersAndSchema()
{
    // Plan 9.3: the parameters; the assembly and the inputs belong to the panels (RB7), pages and
    // assemblyPath to MCP (QB4, Q9 b)
    const Tool tool;
    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QVERIFY(pdf::PDFDocumentToolParameters::isValid(definitions));
    QStringList names;
    for (const pdf::PDFDocumentToolParameterDefinition& definition : definitions)
    {
        names << definition.getName();
        if (definition.getName() == QLatin1String(Tool::PARAMETER_OUTLINE))
        {
            // QB3, KB1: the corrected text of 396:26763
            QCOMPARE(definition.getDefaultValue().toString(), QStringLiteral("document-parts"));
            QCOMPARE(definition.getOptions().front().getDescription(), QStringLiteral("One bookmark per group or run of a file"));
        }
        if (definition.getName() == QLatin1String(pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME))
        {
            QCOMPARE(definition.getDefaultValue().toString(), QStringLiteral("##_{group_name}"));
        }
    }
    QCOMPARE(names, QStringList({ QStringLiteral("inputs"), QStringLiteral("assembly"), QStringLiteral("output"), QStringLiteral("outline"), QStringLiteral("optimizeImages"),
                                  QStringLiteral("folder"), QStringLiteral("fileName"), QStringLiteral("existingFiles"), QStringLiteral("assemblyPath"), QStringLiteral("pages") }));

    const QJsonObject properties = pdf::PDFDocumentToolParameters::toJsonSchema(definitions).value(QStringLiteral("properties")).toObject();
    QVERIFY(!properties.contains(QStringLiteral("inputs")));
    QVERIFY(!properties.contains(QStringLiteral("assembly")));
    QVERIFY(properties.contains(QStringLiteral("pages")));
    QVERIFY(properties.contains(QStringLiteral("assemblyPath")));
    const QJsonArray outputs = properties.value(QStringLiteral("output")).toObject().value(QStringLiteral("enum")).toArray();
    QCOMPARE(outputs, QJsonArray({ QStringLiteral("document"), QStringLiteral("one-pdf"), QStringLiteral("per-group"), QStringLiteral("per-page") }));
}

void DocumentToolManageTest::filesOfFigmaExample()
{
    // 396:26763: the assembly of 387:12569 as One PDF per group or item, "##_{group_name}" -
    // "01_Introduction.pdf … 10_Appendix.pdf", 10 files; per page 17 files
    std::vector<Assembly::Item> items;
    items.push_back(createItem(QStringLiteral("p1"), { createPage(0, 0), createPage(0, 1), createPage(0, 2), createPage(0, 3), createPage(0, 4) }, QStringLiteral("Introduction"), true));
    items.push_back(createItem(QStringLiteral("p2"), { createPage(0, 5) }));
    for (int i = 0; i < 3; ++i)
    {
        items.push_back(createItem(QStringLiteral("q%1").arg(i), { createPage(1, i) }));
    }
    items.push_back(createItem(QStringLiteral("p6"), { createPage(1, 3) }, QStringLiteral("Site plan")));
    items.push_back(createItem(QStringLiteral("p7"), { createPage(2, 0) }));
    Assembly::Page blank;
    blank.plan.input = Assembly::BLANK_INPUT;
    blank.plan.pageSize = QSizeF(595.0, 842.0);
    items.push_back(createItem(QStringLiteral("p8"), { blank }));
    items.push_back(createItem(QStringLiteral("p9"), { createPage(1, 4) }));
    items.push_back(createItem(QStringLiteral("p10"), { createPage(1, 5), createPage(1, 6), createPage(1, 7), createPage(1, 8) }, QStringLiteral("Appendix"), true));
    Assembly::Source image = createSource(QStringLiteral("s3"), QStringLiteral("signature_page.png"), 1);
    image.kind = Assembly::SourceKind::Image;
    image.imageData = "png";
    QString error;
    const Assembly assembly = Assembly::create({ createSource(QStringLiteral("s1"), QStringLiteral("Contract.pdf"), 6), createSource(QStringLiteral("s2"), QStringLiteral("Annex_A.pdf"), 24), image },
                                               std::move(items), { }, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QVariantMap parameters;
    parameters.insert(name(Tool::PARAMETER_OUTPUT), QString::fromLatin1(Tool::OUTPUT_PER_GROUP));
    parameters.insert(name(pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME), QStringLiteral("##_{group_name}"));
    std::vector<pdf::PDFDocumentToolFileOutput::File> files;
    QVERIFY2(Tool::createFiles(assembly, parameters, QDate(2026, 11, 11), &files, &error), qPrintable(error));
    QCOMPARE(files.size(), size_t(10));
    QCOMPARE(files.front().fileName, QStringLiteral("01_Introduction.pdf"));
    QCOMPARE(files.back().fileName, QStringLiteral("10_Appendix.pdf"));
    QCOMPARE(files[5].fileName, QStringLiteral("06_Site plan.pdf"));
    QCOMPARE(files.front().pages, std::vector<pdf::PDFInteger>({ 0, 1, 2, 3, 4 }));
    QCOMPARE(files.back().pages, std::vector<pdf::PDFInteger>({ 13, 14, 15, 16 }));
    QCOMPARE(pdf::PDFDocumentToolFileOutput::formatFileNames({ files.front().fileName, files[1].fileName, files.back().fileName }), QStringLiteral("01_Introduction.pdf … 10_Appendix.pdf"));

    // Per page: 17 files; "#" the file, "{group_index}" its item (B26), "@" its page, "%" its source
    parameters.insert(name(Tool::PARAMETER_OUTPUT), QString::fromLatin1(Tool::OUTPUT_PER_PAGE));
    parameters.insert(name(pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME), QStringLiteral("#-{group_index}-@-%-{source_base}"));
    QVERIFY2(Tool::createFiles(assembly, parameters, QDate(2026, 11, 11), &files, &error), qPrintable(error));
    QCOMPARE(files.size(), size_t(17));
    QCOMPARE(files[3].fileName, QStringLiteral("4-1-4-1-Contract.pdf"));
    QCOMPARE(files[10].fileName, QStringLiteral("11-7-11-3-signature_page.pdf"));
    QCOMPARE(files[16].fileName, QStringLiteral("17-10-17-2-Annex_A.pdf"));

    // 383:9088: one name for all files
    parameters.insert(name(pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME), QStringLiteral("part"));
    QVERIFY(!Tool::createFiles(assembly, parameters, QDate(2026, 11, 11), &files, &error));
    QCOMPARE(error, QStringLiteral("Every file would be named part.pdf"));
}

void DocumentToolManageTest::mcpPagesOfDocument()
{
    // QB4 with RB9: pages names every page (a permutation, a repetition is a clone), else the
    // missing pages are listed - the removal is pages.remove
    const pdf::PDFDocument document = MergeTestPdf::createPages("A", 5).read();
    QString error;
    const std::optional<Assembly> reordered = Tool::createAssemblyOfPages(&document, QStringLiteral("a.pdf"), QStringLiteral("3, 1-2, 4-"), &error);
    QVERIFY2(reordered.has_value(), qPrintable(error));
    QStringList pages;
    for (const Assembly::Item& item : reordered->getItems())
    {
        pages << QString::number(item.pages.front().plan.pageIndex + 1) + (item.pages.front().plan.isClone ? QStringLiteral("+") : QString());
        QCOMPARE(item.pages.front().targetPage, document.getCatalog()->getPage(size_t(item.pages.front().plan.pageIndex))->getPageReference());
    }
    QCOMPARE(pages, QStringList({ QStringLiteral("3"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("4"), QStringLiteral("5") }));
    QVERIFY(reordered->getSources().front().isTarget);

    const std::optional<Assembly> cloned = Tool::createAssemblyOfPages(&document, QStringLiteral("a.pdf"), QStringLiteral("1-5, 2"), &error);
    QVERIFY2(cloned.has_value(), qPrintable(error));
    QCOMPARE(cloned->getItems().size(), size_t(6));
    QVERIFY(!cloned->getItems()[1].pages.front().plan.isClone);
    QVERIFY(cloned->getItems()[5].pages.front().plan.isClone);

    QVERIFY(!Tool::createAssemblyOfPages(&document, QStringLiteral("a.pdf"), QStringLiteral("1-2, 5"), &error));
    QCOMPARE(error, QString(u"pages must name every page of the document, missing: 3\u20134. A repeated page is a clone; remove pages by pages.remove."));
    QVERIFY(!Tool::createAssemblyOfPages(&document, QStringLiteral("a.pdf"), QStringLiteral("1-6"), &error));
    QVERIFY(!error.isEmpty());

    // Owner 2026-11-12 (plan of step 3b, 13 point 3): an assembly has at most 65 536 pages, also of
    // pages - 13 107 runs of the 5 pages and one page more are 65 536 pages (accepted), a page
    // more is refused with the limit (the text is below the 65 536 characters of pages)
    QStringList runs;
    for (int i = 0; i < 13107; ++i)
    {
        runs << QStringLiteral("1-5");
    }
    runs << QStringLiteral("1");
    const std::optional<Assembly> atLimit = Tool::createAssemblyOfPages(&document, QStringLiteral("a.pdf"), runs.join(QLatin1Char(',')), &error);
    QVERIFY2(atLimit.has_value(), qPrintable(error));
    QCOMPARE(atLimit->getItems().size(), size_t(65536));
    runs << QStringLiteral("2");
    QVERIFY(runs.join(QLatin1Char(',')).size() <= Tool::MAXIMUM_PAGES_LENGTH);
    QVERIFY(!Tool::createAssemblyOfPages(&document, QStringLiteral("a.pdf"), runs.join(QLatin1Char(',')), &error));
    QVERIFY2(error.startsWith(QStringLiteral("An assembly has at most ")), qPrintable(error));

    // The run: completeGivenParameters makes the assembly of This document; validate names the
    // missing pages
    const Tool tool;
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), QByteArray(), QStringLiteral("a.pdf"), true));
    QVariantMap given;
    given.insert(name(Tool::PARAMETER_PAGES), QStringLiteral("5-1"));
    const QVariantMap completed = tool.completeGivenParameters(context, given);
    QCOMPARE(completed.value(name(Tool::PARAMETER_OUTPUT)).toString(), QStringLiteral("document"));
    QCOMPARE(Tool::getAssembly(completed).getItems().size(), size_t(5));
    QCOMPARE(Tool::getAssembly(completed).getItems().front().pages.front().plan.pageIndex, pdf::PDFInteger(4));

    given.insert(name(Tool::PARAMETER_PAGES), QStringLiteral("1-4"));
    QVERIFY(!tool.completeGivenParameters(context, given).contains(name(Tool::PARAMETER_ASSEMBLY)));
    const pdf::PDFDocumentToolResult refused = tool.run(context, given, nullptr, nullptr);
    QCOMPARE(refused.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY2(refused.message.contains(QStringLiteral("missing: 5")), qPrintable(refused.message));
}

void DocumentToolManageTest::mcpGivenParameters()
{
    // Plan 7, QB4, Q9 b: pages - the open document, assemblyPath - new documents with an explicit
    // output; the panel parameters are no parameters of an assistant
    const Tool tool;
    auto errors = [&tool](const QVariantMap& given)
    {
        QStringList names;
        for (const pdf::PDFDocumentToolParameterError& error : tool.checkGivenParameters(given))
        {
            names << error.parameter;
        }
        return names;
    };
    QCOMPARE(errors({ { QStringLiteral("pages"), QStringLiteral("2,1") } }), QStringList());
    QCOMPARE(errors({ { QStringLiteral("pages"), QStringLiteral("2,1") }, { QStringLiteral("output"), QStringLiteral("document") } }), QStringList());
    QCOMPARE(errors({ { QStringLiteral("pages"), QStringLiteral("2,1") }, { QStringLiteral("output"), QStringLiteral("one-pdf") } }), QStringList({ QStringLiteral("output") }));
    QCOMPARE(errors({ { QStringLiteral("assemblyPath"), QStringLiteral("C:/a.pdf4qtassembly") }, { QStringLiteral("output"), QStringLiteral("per-group") } }), QStringList());
    QCOMPARE(errors({ { QStringLiteral("assemblyPath"), QStringLiteral("C:/a.pdf4qtassembly") } }), QStringList({ QStringLiteral("output") }));
    QCOMPARE(errors({ { QStringLiteral("assemblyPath"), QStringLiteral("C:/a.pdf4qtassembly") }, { QStringLiteral("pages"), QStringLiteral("1") } }), QStringList({ QStringLiteral("pages") }));
    QCOMPARE(errors({ }), QStringList({ QStringLiteral("pages") }));
    QCOMPARE(errors({ { QStringLiteral("pages"), QStringLiteral("1") }, { QStringLiteral("inputs"), QStringLiteral("a") } }), QStringList({ QStringLiteral("inputs") }));

    // The MCP bridge hands over the assembly it has read of assemblyPath (Q9 b); without the file
    // the assembly is no parameter of an assistant
    const QVariant assembly = QVariant::fromValue(Assembly());
    QCOMPARE(errors({ { QStringLiteral("assemblyPath"), QStringLiteral("C:/a.pdf4qtassembly") }, { QStringLiteral("output"), QStringLiteral("one-pdf") }, { QStringLiteral("assembly"), assembly } }), QStringList());
    QCOMPARE(errors({ { QStringLiteral("pages"), QStringLiteral("1") }, { QStringLiteral("assembly"), assembly } }), QStringList({ QStringLiteral("assembly") }));
}

void DocumentToolManageTest::sourcesByOrderWithoutIds()
{
    // The MCP bridge gives the open documents without input ids in the order of the PDF sources of
    // the assembly: the k-th PDF source is the k-th document (an image source is no document)
    QImage image(30, 40, QImage::Format_RGB32);
    image.fill(Qt::white);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QBuffer::WriteOnly);
    image.save(&buffer, "PNG");
    QString error;
    Assembly::Source imageSource = Assembly::createImageSource(png, QByteArray(), QStringLiteral("c.png"), QString(), &error);
    imageSource.id = QStringLiteral("s2");
    const Assembly assembly = Assembly::create({ createSource(QStringLiteral("s1"), QStringLiteral("a.pdf"), 3), imageSource, createSource(QStringLiteral("s3"), QStringLiteral("b.pdf"), 2) },
                                               { createItem(QStringLiteral("p1"), { createPage(2, 1) }), createItem(QStringLiteral("p2"), { createPage(1, 0) }), createItem(QStringLiteral("p3"), { createPage(0, 0) }) },
                                               { }, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    pdf::PDFDocumentToolContext context;
    context.documents.push_back(createInput(MergeTestPdf::createPages("A", 3), QStringLiteral("a.pdf"), QString()));
    context.documents.push_back(createInput(MergeTestPdf::createPages("B", 2), QStringLiteral("b.pdf"), QString()));
    const pdf::PDFDocumentToolResult result = Tool().run(context, createParameters(assembly, Tool::OUTPUT_ONE_PDF), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(getPageMarkers(*result.documents.front().document), QStringList({ QStringLiteral("BPAGE2"), QStringLiteral("blank"), QStringLiteral("APAGE1") }));

    // A document of the GUI keeps its id: a document of another id is no source by its order
    context.documents[1].inputId = QStringLiteral("other");
    const pdf::PDFDocumentToolResult refused = Tool().run(context, createParameters(assembly, Tool::OUTPUT_ONE_PDF), nullptr, nullptr);
    QCOMPARE(refused.status, pdf::PDFDocumentToolResultStatus::Failed);
    QCOMPARE(refused.message, QStringLiteral("The file b.pdf of the assembly is not open."));
}

void DocumentToolManageTest::runOnePdf()
{
    // 4.4: One PDF - one merge of the plan of the assembly, the pages in the order of the canvas
    // with the blank page of its size; 387:17762 "Pages · 6 from 2 sources" (a blank page has no source)
    Assembly assembly;
    const pdf::PDFDocumentToolContext context = createTwoFileContext(&assembly);
    const Tool tool;
    const pdf::PDFDocumentToolResult result = tool.run(context, createParameters(assembly, Tool::OUTPUT_ONE_PDF), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.documents.size(), size_t(1));
    QCOMPARE(result.documents.front().kind, pdf::PDFDocumentToolOutputDocument::Kind::New);
    QVERIFY(!result.documents.front().writtenData.isEmpty());
    QCOMPARE(result.documents.front().suggestedFileName, QStringLiteral("a_assembled.pdf"));

    const pdf::PDFDocument& document = *result.documents.front().document;
    QCOMPARE(getPageMarkers(document), QStringList({ QStringLiteral("APAGE1"), QStringLiteral("APAGE2"), QStringLiteral("APAGE3"), QStringLiteral("BPAGE2"), QStringLiteral("blank"), QStringLiteral("BPAGE1") }));
    QCOMPARE(document.getCatalog()->getPage(4)->getMediaBox().size(), QSizeF(612.0, 792.0));

    auto fact = [&result](const QString& id)
    {
        for (const pdf::PDFDocumentToolFact& item : result.facts)
        {
            if (item.id == id)
            {
                return item.value;
            }
        }
        return QString();
    };
    QCOMPARE(fact(QStringLiteral("pages")), QStringLiteral("6 from 2 sources"));
    QCOMPARE(fact(QStringLiteral("outline")), QStringLiteral("4 document parts"));
    QVERIFY(!fact(QStringLiteral("size")).isEmpty());

    // The sources are never changed (the documents of the context are immutable values)
    QCOMPARE(context.documents.front().document->getCatalog()->getPageCount(), size_t(3));
}

void DocumentToolManageTest::runPerGroupAndPerPage()
{
    // 4.4: a file per item of the assembly (one merge, then the parts in the order of the result),
    // Q6: the attachments of the document only in the first file
    Assembly assembly;
    pdf::PDFDocumentToolContext context = createTwoFileContext(&assembly);
    ManageToolTestSink sink;
    context.fileSink = &sink;

    QVariantMap parameters = createParameters(assembly, Tool::OUTPUT_PER_GROUP);
    parameters.insert(name(pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME), QStringLiteral("##_{group_name}"));
    const Tool tool;
    const pdf::PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(sink.begunNames, QStringList({ QStringLiteral("01_Intro.pdf"), QStringLiteral("02_a.pdf, p. 3.pdf"), QStringLiteral("03_b.pdf, p. 2.pdf"),
                                            QStringLiteral("04_Blank page.pdf"), QStringLiteral("05_b.pdf, p. 1.pdf") }));
    QCOMPARE(result.writtenFiles.size(), 5);

    std::vector<QStringList> markers;
    for (const auto& [fileName, data] : sink.files)
    {
        markers.push_back(getPageMarkers(MergeTestPdf::readBytes(data)));
    }
    QCOMPARE(markers[0], QStringList({ QStringLiteral("APAGE1"), QStringLiteral("APAGE2") }));
    QCOMPARE(markers[1], QStringList({ QStringLiteral("APAGE3") }));
    QCOMPARE(markers[2], QStringList({ QStringLiteral("BPAGE2") }));
    QCOMPARE(markers[3], QStringList({ QStringLiteral("blank") }));
    QCOMPARE(markers[4], QStringList({ QStringLiteral("BPAGE1") }));

    // Q6
    QVERIFY(sink.files[0].second.contains("ATTACHMENTMARKER"));
    for (size_t i = 1; i < sink.files.size(); ++i)
    {
        QVERIFY(!sink.files[i].second.contains("ATTACHMENTMARKER"));
    }

    // Per page: a file of every page of the result
    ManageToolTestSink pageSink;
    context.fileSink = &pageSink;
    parameters = createParameters(assembly, Tool::OUTPUT_PER_PAGE);
    parameters.insert(name(pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME), QStringLiteral("page_#"));
    const pdf::PDFDocumentToolResult pageResult = tool.run(context, parameters, nullptr, nullptr);
    QVERIFY2(pageResult.isSuccess(), qPrintable(pageResult.message));
    QCOMPARE(pageSink.begunNames, QStringList({ QStringLiteral("page_1.pdf"), QStringLiteral("page_2.pdf"), QStringLiteral("page_3.pdf"),
                                                QStringLiteral("page_4.pdf"), QStringLiteral("page_5.pdf"), QStringLiteral("page_6.pdf") }));
    QCOMPARE(getPageMarkers(MergeTestPdf::readBytes(pageSink.files[1].second)), QStringList({ QStringLiteral("APAGE2") }));

    // Without a file sink the files have no place
    context.fileSink = nullptr;
    QCOMPARE(tool.run(context, parameters, nullptr, nullptr).status, pdf::PDFDocumentToolResultStatus::Failed);
}

void DocumentToolManageTest::runPerPageTenThousand()
{
    // Plan 6, 8, R2: per page of 10 000 pages - each file has only the objects of its part (its
    // cross-reference table = the objects it reaches + the entry 0), within a time limit
    constexpr int pageCount = 10000;
    MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    QByteArray kids;
    for (int page = 1; page <= pageCount; ++page)
    {
        // The pages 3 ... 10 002, their contents 10 003 ... 20 002 (createPages has room for 97 pages)
        kids += QByteArray::number(page + 2) + " 0 R ";
        pdf.set(page + 2, "<< /Type /Page /Parent 2 0 R /Contents " + QByteArray::number(page + 2 + pageCount) + " 0 R >>");
        pdf.set(page + 2 + pageCount, MergeTestPdf::stream("% APAGE" + QByteArray::number(page) + "\n0 0 m 10 10 l S"));
    }
    pdf.set(2, "<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(pageCount) + " /MediaBox [0 0 600 800] /Resources << >> >>");
    pdf::PDFDocumentToolContext context;
    context.documents.push_back(createInput(pdf, QStringLiteral("big.pdf"), QStringLiteral("a")));
    std::vector<Assembly::Item> items;
    for (int i = 0; i < pageCount; ++i)
    {
        items.push_back(createItem(QStringLiteral("p%1").arg(i + 1), { createPage(0, pageCount - 1 - i) }));
    }
    QString error;
    const Assembly assembly = Assembly::create({ createSource(QStringLiteral("a"), QStringLiteral("big.pdf"), pageCount) }, std::move(items), { }, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    ManageToolTestSink sink;
    context.fileSink = &sink;
    QVariantMap parameters = createParameters(assembly, Tool::OUTPUT_PER_PAGE);
    parameters.insert(name(pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME), QStringLiteral("#"));
    parameters.insert(name(Tool::PARAMETER_OUTLINE), QString::fromLatin1(Tool::OUTLINE_NONE));

    QElapsedTimer timer;
    timer.start();
    const pdf::PDFDocumentToolResult result = Tool().run(context, parameters, nullptr, nullptr);
    const qint64 elapsed = timer.elapsed();
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(sink.files.size(), size_t(pageCount));
    for (const size_t index : { size_t(0), size_t(4999), size_t(pageCount - 1) })
    {
        const pdf::PDFDocument part = MergeTestPdf::readBytes(sink.files[index].second);
        QCOMPARE(part.getCatalog()->getPageCount(), size_t(1));
        QCOMPARE(getPageMarkers(part), QStringList({ QStringLiteral("APAGE%1").arg(pageCount - int(index)) }));
        const qint64 reachable = qint64(pdf::PDFObjectUtils::getReferences({ part.getStorage().getTrailerDictionary() }, part.getStorage()).size());
        QCOMPARE(qint64(part.getStorage().getObjects().size()), reachable + 1);
    }
    QVERIFY2(elapsed < 240000 * getTimeoutFactor(), qPrintable(QString::number(elapsed)));
}

void DocumentToolManageTest::validateSourcesAndPermissions()
{
    Assembly assembly;
    pdf::PDFDocumentToolContext context = createTwoFileContext(&assembly);
    const Tool tool;

    // The sources are the inputs of the context by their ids: a missing one is named
    pdf::PDFDocumentToolContext withoutB = context;
    withoutB.documents.pop_back();
    pdf::PDFDocumentToolResult result = tool.run(withoutB, createParameters(assembly, Tool::OUTPUT_ONE_PDF), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QCOMPARE(result.message, QStringLiteral("The file b.pdf of the assembly is not open."));

    // A page, which the open file does not have (the file has changed)
    pdf::PDFDocumentToolContext shorter = context;
    shorter.documents[1] = createInput(MergeTestPdf::createPages("B", 1), QStringLiteral("b.pdf"), QStringLiteral("b"));
    result = tool.run(shorter, createParameters(assembly, Tool::OUTPUT_ONE_PDF), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("Page 2 does not exist")), qPrintable(result.message));

    // Q2 of step 2: a new document is not encrypted - a source restricting the copy needs its
    // owner password (Table 22)
    const uint32_t permissions = uint32_t(pdf::PDFSecurityHandler::Permission::Assemble) | uint32_t(pdf::PDFSecurityHandler::Permission::PrintHighResolution) |
                                 uint32_t(pdf::PDFSecurityHandler::Permission::PrintLowResolution);
    const QByteArray encrypted = MergeTestPdf::encrypt(MergeTestPdf::createPages("B", 2).read(), QString(), QStringLiteral("owner"), permissions);
    pdf::PDFDocumentToolContext restricted = context;
    restricted.documents[1] = pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(MergeTestPdf::readBytes(encrypted))), encrypted, QStringLiteral("b.pdf"), false);
    restricted.documents[1].inputId = QStringLiteral("b");
    result = tool.run(restricted, createParameters(assembly, Tool::OUTPUT_ONE_PDF), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.contains(QStringLiteral("b.pdf")), qPrintable(result.message));

    // An empty assembly has nothing to make
    result = tool.run(context, createParameters(Assembly(), Tool::OUTPUT_ONE_PDF), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QCOMPARE(result.message, QStringLiteral("The assembly has no pages."));

    // This document needs the target among the sources
    result = tool.run(context, createParameters(assembly, Tool::OUTPUT_DOCUMENT), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QCOMPARE(result.message, QStringLiteral("This document is not a source of the assembly."));
}

void DocumentToolManageTest::documentNothingToDoAndChanged()
{
    // 4.5: the pages in their order, nothing turned - NothingToDo (a use of the tool, no change)
    const pdf::PDFDocument version1 = MergeTestPdf::createPages("A", 4).read();
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(version1)), QByteArray(), QStringLiteral("a.pdf"), true));
    const Assembly assembly = Assembly::createForDocument(&version1, 1, QStringLiteral("a.pdf"));
    const Tool tool;
    pdf::PDFDocumentToolResult result = tool.run(context, createParameters(assembly, Tool::OUTPUT_DOCUMENT), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::NothingToDo);
    QCOMPARE(result.message, QStringLiteral("The pages are in this order already."));

    // RB2: the assembly of another version (a page inserted meanwhile) is refused, never applied
    MergeTestPdf changed = MergeTestPdf::createPages("A", 4);
    changed.set(2, "<< /Type /Pages /Kids [3 0 R 4 0 R 50 0 R 5 0 R 6 0 R] /Count 5 /MediaBox [0 0 600 800] /Resources << >> >>");
    changed.set(50, "<< /Type /Page /Parent 2 0 R >>");
    const pdf::PDFDocumentToolContext changedContext = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(changed.readPointer(), QByteArray(), QStringLiteral("a.pdf"), true));
    Assembly::Edit edit;
    const Assembly reversed = Assembly::sort(assembly, { }, Assembly::SortKey::Reverse, &edit);
    result = tool.run(changedContext, createParameters(reversed, Tool::OUTPUT_DOCUMENT), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QCOMPARE(result.message, QStringLiteral("The document has changed."));
    const std::vector<pdf::PDFDocumentToolParameterError> errors = tool.validate(changedContext, createParameters(reversed, Tool::OUTPUT_DOCUMENT));
    QCOMPARE(errors.size(), size_t(1));
    QCOMPARE(errors.front().parameter, QStringLiteral("assembly"));

    // After mapToDocument (the GUI follows the change) the new page is at the end and the plan fits
    pdf::PDFInteger added = 0;
    const Assembly mapped = Assembly::mapToDocument(reversed, changedContext.documents.front().document.data(), 2, &added);
    QCOMPARE(added, pdf::PDFInteger(1));
    QCOMPARE(Assembly::getTargetPages(mapped).size(), size_t(5));
    QVERIFY(tool.validate(changedContext, createParameters(mapped, Tool::OUTPUT_DOCUMENT)).empty());
}

void DocumentToolManageTest::outlinePartsOfOnePdf()
{
    // QB3 b: a bookmark per part - the group with its name, the runs of a file outside of groups,
    // the blank page in the part before it (12.3.3)
    Assembly assembly;
    const pdf::PDFDocumentToolContext context = createTwoFileContext(&assembly);
    const pdf::PDFDocumentToolResult result = Tool().run(context, createParameters(assembly, Tool::OUTPUT_ONE_PDF), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(getOutlineTitles(*result.documents.front().document), QStringList({ QStringLiteral("Intro"), QStringLiteral("a.pdf, p. 3"), QStringLiteral("b.pdf, p. 2"), QStringLiteral("b.pdf, p. 1") }));

    // No outline: none
    QVariantMap parameters = createParameters(assembly, Tool::OUTPUT_ONE_PDF);
    parameters.insert(name(Tool::PARAMETER_OUTLINE), QString::fromLatin1(Tool::OUTLINE_NONE));
    const pdf::PDFDocumentToolResult none = Tool().run(context, parameters, nullptr, nullptr);
    QVERIFY2(none.isSuccess(), qPrintable(none.message));
    QVERIFY(getOutlineTitles(*none.documents.front().document).isEmpty());

    // R3 of step 2: the pages of one PDF without a group keep its catalog and its outline; a group
    // is a structure of the user - its part (QB3)
    QString error;
    const Assembly single = Assembly::create({ createSource(QStringLiteral("a"), QStringLiteral("a.pdf"), 3) },
                                             { createItem(QStringLiteral("p1"), { createPage(0, 2) }), createItem(QStringLiteral("p2"), { createPage(0, 1) }), createItem(QStringLiteral("p3"), { createPage(0, 0) }) },
                                             { }, &error);
    pdf::PDFDocumentToolContext singleContext;
    singleContext.documents.push_back(context.documents.front());
    const pdf::PDFDocumentToolResult kept = Tool().run(singleContext, createParameters(single, Tool::OUTPUT_ONE_PDF), nullptr, nullptr);
    QVERIFY2(kept.isSuccess(), qPrintable(kept.message));
    QCOMPARE(getOutlineTitles(*kept.documents.front().document), QStringList({ QStringLiteral("Chapter A2") }));
    const auto outlineFact = std::find_if(kept.facts.cbegin(), kept.facts.cend(), [](const pdf::PDFDocumentToolFact& fact) { return fact.id == QLatin1String("outline"); });
    QVERIFY(outlineFact != kept.facts.cend());
    QCOMPARE(outlineFact->value, QStringLiteral("Kept from the document"));

    Assembly::Edit edit;
    const Assembly grouped = Assembly::group(single, { single.getItems()[0].id, single.getItems()[1].id }, &edit);
    const pdf::PDFDocumentToolResult parts = Tool().run(singleContext, createParameters(grouped, Tool::OUTPUT_ONE_PDF), nullptr, nullptr);
    QVERIFY2(parts.isSuccess(), qPrintable(parts.message));
    QCOMPARE(getOutlineTitles(*parts.documents.front().document).first(), QString(u"a.pdf, p. 2–3"));
}

void DocumentToolManageTest::outlineJoinUnderParts()
{
    // 4.4 Join outlines: below each part the bookmarks of the files, which point at its pages -
    // "Chapter A2" points at the page 2 of a.pdf, which is in Intro
    Assembly assembly;
    const pdf::PDFDocumentToolContext context = createTwoFileContext(&assembly);
    QVariantMap parameters = createParameters(assembly, Tool::OUTPUT_ONE_PDF);
    parameters.insert(name(Tool::PARAMETER_OUTLINE), QString::fromLatin1(Tool::OUTLINE_JOIN));
    const pdf::PDFDocumentToolResult result = Tool().run(context, parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const QStringList titles = getOutlineTitles(*result.documents.front().document);
    QVERIFY2(titles.contains(QStringLiteral("Intro/Chapter A2")), qPrintable(titles.join(QStringLiteral(" | "))));
    QVERIFY(!titles.contains(QStringLiteral("a.pdf, p. 3/Chapter A2")));
}

void DocumentToolManageTest::perGroupFileHasGroupBookmark()
{
    // 4.4: each file has the outline of the whole result cut to its pages - the file of a group
    // has one bookmark of the group
    Assembly assembly;
    pdf::PDFDocumentToolContext context = createTwoFileContext(&assembly);
    ManageToolTestSink sink;
    context.fileSink = &sink;
    const pdf::PDFDocumentToolResult result = Tool().run(context, createParameters(assembly, Tool::OUTPUT_PER_GROUP), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(getOutlineTitles(MergeTestPdf::readBytes(sink.files[0].second)), QStringList({ QStringLiteral("Intro") }));
    QCOMPARE(getOutlineTitles(MergeTestPdf::readBytes(sink.files[4].second)), QStringList({ QStringLiteral("b.pdf, p. 1") }));
}

void DocumentToolManageTest::documentArranged()
{
    // 4.5: This document - the pages arranged keeping their objects (7.7.3.2), a clone a new page
    // object; the facts of the result
    const pdf::PDFDocument document = MergeTestPdf::createPages("A", 3).read();
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), QByteArray(), QStringLiteral("a.pdf"), true));
    const Assembly original = Assembly::createForDocument(&document, 1, QStringLiteral("a.pdf"));
    Assembly::Edit edit;
    const Assembly moved = Assembly::move(original, { original.getItems()[2].id }, original.getItems()[0].id, &edit);
    const Assembly cloned = Assembly::clone(moved, { moved.getItems()[1].id }, &edit);   // A3 A1 A1+ A2

    const pdf::PDFDocumentToolResult result = Tool().run(context, createParameters(cloned, Tool::OUTPUT_DOCUMENT), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.documents.front().kind, pdf::PDFDocumentToolOutputDocument::Kind::Changed);
    const pdf::PDFDocument& arranged = *result.documents.front().document;
    QCOMPARE(arranged.getCatalog()->getPageCount(), size_t(4));
    QCOMPARE(arranged.getCatalog()->getPage(0)->getPageReference(), pdf::PDFObjectReference(5, 0));
    QCOMPARE(arranged.getCatalog()->getPage(1)->getPageReference(), pdf::PDFObjectReference(3, 0));
    QVERIFY(arranged.getCatalog()->getPage(2)->getPageReference() != pdf::PDFObjectReference(3, 0));
    QCOMPARE(arranged.getCatalog()->getPage(3)->getPageReference(), pdf::PDFObjectReference(4, 0));
    QCOMPARE(getPageMarkers(arranged), QStringList({ QStringLiteral("APAGE3"), QStringLiteral("APAGE1"), QStringLiteral("APAGE1"), QStringLiteral("APAGE2") }));

    QStringList facts;
    for (const pdf::PDFDocumentToolFact& fact : result.facts)
    {
        facts << fact.id;
    }
    QVERIFY(facts.contains(QStringLiteral("order")));
    QVERIFY(facts.contains(QStringLiteral("cloned")));
    QCOMPARE(result.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("incremental-save"));
}

void DocumentToolManageTest::documentFacts()
{
    // 4.5, RB1, RB5, RB13, QB6 a: the facts of This document. The document is tagged (14.7), has
    // document parts (14.12), usage rights (/Perms /UR3, 12.8.2.3) and a signed signature field on
    // its page 1 (12.7.5.5)
    auto createDocument = [](const QByteArray& permissions)
    {
        MergeTestPdf pdf = MergeTestPdf::createPages("A", 3, "/AcroForm << /Fields [41 0 R] /SigFlags 3 >> /StructTreeRoot 40 0 R /DPartRoot 44 0 R /Perms << " + permissions + " >>",
                                                     { { 1, "/Annots [41 0 R]" } });
        pdf.set(40, "<< /Type /StructTreeRoot >>");
        pdf.set(41, "<< /FT /Sig /T (Signature1) /Type /Annot /Subtype /Widget /Rect [0 0 10 10] /P " + MergeTestPdf::page(1) + " /V 42 0 R >>");
        pdf.set(42, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /Contents <00> /ByteRange [0 0 0 0] "
                    "/Reference [ << /Type /SigRef /TransformMethod /DocMDP /TransformParams << /Type /TransformParams /P 2 /V /1.2 >> >> ] >>");
        pdf.set(43, "<< /Type /Sig /Filter /Adobe.PPKLite >>");
        pdf.set(44, "<< /Type /DPartRoot /DPartRootNode 45 0 R >>");
        pdf.set(45, "<< /Type /DPart >>");
        return pdf.read();
    };
    auto runFacts = [](const pdf::PDFDocument& document, const std::function<Assembly(const Assembly&)>& change, pdf::PDFDocumentToolResult* result)
    {
        const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), QByteArray(), QStringLiteral("a.pdf"), true));
        *result = Tool().run(context, createParameters(change(Assembly::createForDocument(&document, 1, QStringLiteral("a.pdf"))), Tool::OUTPUT_DOCUMENT), nullptr, nullptr);
        QMap<QString, QString> facts;
        for (const pdf::PDFDocumentToolFact& fact : result->facts)
        {
            facts.insert(fact.id, fact.value);
        }
        return facts;
    };

    // Moved and the signed page cloned: the tags and their reading order stay, the clone is not
    // tagged and has no signature field, the signed revision stays
    const pdf::PDFDocument signed_ = createDocument("/UR3 43 0 R");
    pdf::PDFDocumentToolResult result;
    QMap<QString, QString> facts = runFacts(signed_, [](const Assembly& assembly)
    {
        Assembly::Edit edit;
        const Assembly moved = Assembly::move(assembly, { assembly.getItems()[1].id }, assembly.getItems()[0].id, &edit);   // A2 A1 A3
        return Assembly::clone(moved, { moved.getItems()[1].id }, &edit);                                                    // A2 A1 A1+ A3
    }, &result);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(facts.value(QStringLiteral("pages")), QStringLiteral("4 pages (was 3)"));
    QCOMPARE(facts.value(QStringLiteral("order")), QStringLiteral("Pages moved"));
    QCOMPARE(facts.value(QStringLiteral("cloned")), QStringLiteral("1 pages"));
    QCOMPARE(facts.value(QStringLiteral("tags")), QStringLiteral("The reading order of the tags is not changed"));
    QCOMPARE(facts.value(QStringLiteral("clonedTags")), QStringLiteral("Cloned pages are not tagged"));
    QCOMPARE(facts.value(QStringLiteral("documentParts")), QStringLiteral("Document parts removed"));
    QCOMPARE(facts.value(QStringLiteral("signatureFields")), QStringLiteral("Signature fields are not cloned"));
    QCOMPARE(facts.value(QStringLiteral("signedRevisions")), QStringLiteral("Signed revisions stay unchanged; viewers list the page changes made after signing"));
    QCOMPARE(facts.value(QStringLiteral("usageRights")), QStringLiteral("Usage rights of the document stop being valid"));
    QVERIFY(!facts.contains(QStringLiteral("removedSignature")));
    QVERIFY(!facts.contains(QStringLiteral("certification")));
    QCOMPARE(result.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("incremental-save"));

    // The signed page removed: the signature is gone with it, the tags go as by Remove Pages
    facts = runFacts(signed_, [](const Assembly& assembly)
    {
        Assembly::Edit edit;
        return Assembly::remove(assembly, { assembly.getItems()[0].id }, &edit);
    }, &result);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(facts.value(QStringLiteral("removed")), QStringLiteral("1 pages · 1"));
    QCOMPARE(facts.value(QStringLiteral("removedSignature")), QStringLiteral("The signature on a removed page is gone"));
    QCOMPARE(facts.value(QStringLiteral("tags")), QStringLiteral("Removed: the structure of the whole document goes with the removed pages"));
    QVERIFY(!facts.contains(QStringLiteral("signedRevisions")));
    QVERIFY(!facts.contains(QStringLiteral("order")));
    QCOMPARE(facts.value(QStringLiteral("documentParts")), QStringLiteral("Document parts removed"));

    // A clone at the end, the order kept: the document parts stay (the clone joins the part of
    // its place, 14.12) - no fact says they were removed
    facts = runFacts(signed_, [](const Assembly& assembly)
    {
        Assembly::Edit edit;
        return Assembly::clone(assembly, { assembly.getItems()[2].id }, &edit);
    }, &result);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(facts.value(QStringLiteral("cloned")), QStringLiteral("1 pages"));
    QVERIFY(!facts.contains(QStringLiteral("documentParts")));
    QVERIFY(MergeTestRead::catalog(*result.documents.front().document)->hasKey("DPartRoot"));

    // Q12, Table 257: a certified document (every /P) allows no change of its pages - a copy only
    const pdf::PDFDocument certified = createDocument("/DocMDP 42 0 R");
    facts = runFacts(certified, [](const Assembly& assembly)
    {
        Assembly::Edit edit;
        return Assembly::sort(assembly, { }, Assembly::SortKey::Reverse, &edit);
    }, &result);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QVERIFY(facts.contains(QStringLiteral("certification")));
    QVERIFY(!facts.contains(QStringLiteral("signedRevisions")));
    QVERIFY(result.data.value(QStringLiteral("certificationRemoved")).toBool());
    QCOMPARE(result.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("lost-copy-only"));
}

void DocumentToolManageTest::documentPartsAfterRemoval()
{
    // 14.12 (the fix wave of 3b): the parts 72 (pages 1-2) and 73 (pages 3-4). A removed page with
    // the order kept leaves its part - removePages moves /Start to the page, which stays, /End goes
    // for one page (Table 409) - and the parts stay, no fact; another order removes them, the fact
    // says so
    MergeTestPdf pdf = MergeTestPdf::createPages("A", 4, "/DPartRoot 70 0 R", { { 1, "/DPart 72 0 R" }, { 2, "/DPart 72 0 R" }, { 3, "/DPart 73 0 R" }, { 4, "/DPart 73 0 R" } });
    pdf.set(70, "<< /Type /DPartRoot /DPartRootNode 71 0 R >>");
    pdf.set(71, "<< /Type /DPart /Parent 70 0 R /DParts [[72 0 R 73 0 R]] >>");
    pdf.set(72, "<< /Type /DPart /Parent 71 0 R /Start " + MergeTestPdf::page(1) + " /End " + MergeTestPdf::page(2) + " >>");
    pdf.set(73, "<< /Type /DPart /Parent 71 0 R /Start " + MergeTestPdf::page(3) + " /End " + MergeTestPdf::page(4) + " >>");
    const pdf::PDFDocument document = pdf.read();
    const Assembly original = Assembly::createForDocument(&document, 1, QStringLiteral("a.pdf"));
    Assembly::Edit edit;

    QMap<QString, QString> facts;
    pdf::PDFDocumentToolResult result = runDocument(document, Assembly::remove(original, { original.getItems()[0].id }, &edit), &facts);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(facts.value(QStringLiteral("removed")), QStringLiteral("1 pages · 1"));
    QVERIFY(!facts.contains(QStringLiteral("documentParts")));
    const pdf::PDFDocument& removed = *result.documents.front().document;
    QCOMPARE(MergeTestRead::catalog(removed)->get("DPartRoot"), pdf::PDFObject::createReference(pdf::PDFObjectReference(70, 0)));
    const pdf::PDFDictionary* part = MergeTestRead::dictionary(removed, pdf::PDFObject::createReference(pdf::PDFObjectReference(72, 0)));
    QVERIFY(part);
    QCOMPARE(part->get("Start"), pdf::PDFObject::createReference(pdf::PDFObjectReference(4, 0)));
    QVERIFY(!part->hasKey("End"));

    result = runDocument(document, Assembly::sort(original, { }, Assembly::SortKey::Reverse, &edit), &facts);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(facts.value(QStringLiteral("documentParts")), QStringLiteral("Document parts removed"));
    QVERIFY(!MergeTestRead::catalog(*result.documents.front().document)->hasKey("DPartRoot"));
}

void DocumentToolManageTest::documentLockedFieldsFact()
{
    // L1 of RS3BCORE: a clone of a page in the document gets no widget of a signature field (RB1)
    // and no widget of a field, which a signed signature field locks (its /Lock, 12.7.5.5, Table
    // 236) - the facts say both. The text field "name" and the signed field "sig" are on page 1.
    auto createDocument = [](const QByteArray& lockedField)
    {
        MergeTestPdf pdf = MergeTestPdf::createPages("A", 2, "/AcroForm << /Fields [50 0 R 60 0 R] /SigFlags 3 >>", { { 1, "/Annots [50 0 R 60 0 R]" } });
        pdf.set(50, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (name) /Rect [0 0 50 10] /P " + MergeTestPdf::page(1) + " >>");
        pdf.set(60, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (sig) /Rect [0 0 0 0] /P " + MergeTestPdf::page(1) + " /V 61 0 R /Lock 62 0 R >>");
        pdf.set(61, "<< /Type /Sig /Filter /Adobe.PPKLite /ByteRange [0 0 0 0] /Contents <00> >>");
        pdf.set(62, "<< /Type /SigFieldLock /Action /Include /Fields [(" + lockedField + ")] >>");
        return pdf.read();
    };
    auto cloneFirstPage = [](const pdf::PDFDocument& document)
    {
        const Assembly original = Assembly::createForDocument(&document, 1, QStringLiteral("a.pdf"));
        Assembly::Edit edit;
        return Assembly::clone(original, { original.getItems()[0].id }, &edit);
    };

    const pdf::PDFDocument locked = createDocument("name");
    QMap<QString, QString> facts;
    pdf::PDFDocumentToolResult result = runDocument(locked, cloneFirstPage(locked), &facts);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(facts.value(QStringLiteral("signatureFields")), QStringLiteral("Signature fields are not cloned"));
    QCOMPARE(facts.value(QStringLiteral("lockedFields")), QStringLiteral("Fields locked by a signature are not cloned"));
    QCOMPARE(result.documents.front().document->getCatalog()->getPage(1)->getAnnotations().size(), size_t(0));

    // The lock names another field: "name" is cloned, only the signature field is not
    const pdf::PDFDocument unlocked = createDocument("other");
    result = runDocument(unlocked, cloneFirstPage(unlocked), &facts);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(facts.value(QStringLiteral("signatureFields")), QStringLiteral("Signature fields are not cloned"));
    QVERIFY(!facts.contains(QStringLiteral("lockedFields")));
    QCOMPARE(result.documents.front().document->getCatalog()->getPage(1)->getAnnotations().size(), size_t(1));
}

void DocumentToolManageTest::documentCropByUserUnit()
{
    // L2 of RS3BCORE: the crop of the assembly is in points of the page as it is shown (the size of
    // a page with /UserUnit 2 is 1200 x 1600 points, Table 31: a unit is /UserUnit points); the
    // page cuts it from its crop box in its own units. 100 points left and 40 points top are 50 and
    // 20 units of page 1 and 100 and 40 units of page 2 (no /UserUnit, 14.11.2)
    const pdf::PDFDocument document = MergeTestPdf::createPages("A", 2, QByteArray(), { { 1, "/UserUnit 2" } }).read();
    const Assembly original = Assembly::createForDocument(&document, 1, QStringLiteral("a.pdf"));
    QCOMPARE(original.getSources().front().pageSizes.front(), QSizeF(1200.0, 1600.0));
    Assembly::Edit edit;
    const Assembly cropped = Assembly::crop(original, { original.getItems()[0].id, original.getItems()[1].id }, QMarginsF(100.0, 40.0, 0.0, 0.0), false, &edit);
    QVERIFY2(edit.errorMessage.isEmpty(), qPrintable(edit.errorMessage));

    QMap<QString, QString> facts;
    const pdf::PDFDocumentToolResult result = runDocument(document, cropped, &facts);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const pdf::PDFDocument& arranged = *result.documents.front().document;
    QCOMPARE(arranged.getCatalog()->getPage(0)->getCropBox(), QRectF(50.0, 0.0, 550.0, 780.0));
    QCOMPARE(arranged.getCatalog()->getPage(1)->getCropBox(), QRectF(100.0, 0.0, 500.0, 760.0));
    QCOMPARE(facts.value(QStringLiteral("cropped")), QStringLiteral("2 pages"));
}

void DocumentToolManageTest::documentConformanceDeclarations()
{
    // Q18: a document, which declares PDF/X by its output intent (ISO 32000-2, 14.11.5: the
    // subtype GTS_PDFX), keeps the declaration; a cloned page was not checked, so the result says
    // so. Only moved pages are the checked pages of the document: no such fact.
    const pdf::PDFDocument document = MergeTestPdf::createPages("A", 3, "/OutputIntents [<< /Type /OutputIntent /S /GTS_PDFX /OutputConditionIdentifier (FOGRA39) >>]").read();
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), QByteArray(), QStringLiteral("a.pdf"), true));
    const Assembly original = Assembly::createForDocument(&document, 1, QStringLiteral("a.pdf"));
    Assembly::Edit edit;
    const Assembly moved = Assembly::move(original, { original.getItems()[2].id }, original.getItems()[0].id, &edit);
    const Assembly cloned = Assembly::clone(moved, { moved.getItems()[1].id }, &edit);

    const pdf::PDFDocumentToolResult clonedResult = Tool().run(context, createParameters(cloned, Tool::OUTPUT_DOCUMENT), nullptr, nullptr);
    QVERIFY2(clonedResult.isSuccess(), qPrintable(clonedResult.message));
    QString conformance;
    for (const pdf::PDFDocumentToolFact& fact : clonedResult.facts)
    {
        if (fact.id == QLatin1String("conformance"))
        {
            conformance = fact.value;
        }
    }
    QCOMPARE(conformance, QStringLiteral("Pages were not checked for PDF/X"));
    QCOMPARE(MergeTestRead::catalog(*clonedResult.documents.front().document)->get("OutputIntents"), MergeTestRead::catalog(document)->get("OutputIntents"));

    const pdf::PDFDocumentToolResult movedResult = Tool().run(context, createParameters(moved, Tool::OUTPUT_DOCUMENT), nullptr, nullptr);
    QVERIFY2(movedResult.isSuccess(), qPrintable(movedResult.message));
    for (const pdf::PDFDocumentToolFact& fact : movedResult.facts)
    {
        QVERIFY2(fact.id != QLatin1String("conformance"), qPrintable(fact.value));
    }
}

void DocumentToolManageTest::mcpPagesArrangeDocument()
{
    // QB4: the assistant gives the new order of the pages; "3, 1-2" of 3 pages
    const pdf::PDFDocument document = MergeTestPdf::createPages("A", 3).read();
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), QByteArray(), QStringLiteral("a.pdf"), true));
    QVariantMap given;
    given.insert(name(Tool::PARAMETER_PAGES), QStringLiteral("3, 1-2"));
    const pdf::PDFDocumentToolResult result = Tool().run(context, given, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(getPageMarkers(*result.documents.front().document), QStringList({ QStringLiteral("APAGE3"), QStringLiteral("APAGE1"), QStringLiteral("APAGE2") }));
}

QTEST_GUILESS_MAIN(DocumentToolManageTest)

#include "tst_documenttoolmanagetest.moc"
