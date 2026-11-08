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

// The tools Merge (pages.merge) and Blank PDF (convert.blank) of the core (plan of step 2 of
// phase 2, chapters 5.4, 7, 9 "Pevná rozhraní", K22-K26, Q11, Q14): the parameters, the plan of
// the pages, the validation, the results with their written bytes, a partial result, and the
// command PdfTool unite on the same merger (Q12, B15).

#include "mergetestpdf.h"

#include "pdfdocumenttool_compress.h"
#include "pdfdocumenttool_merge.h"
#include "pdfdocumenttoolcatalog.h"

#include <QImageWriter>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cmath>

using namespace pdf;
using mergetest::MergeTestPdf;
using mergetest::MergeTestRead;
using Merge = PDFDocumentToolMerge;
using Blank = PDFDocumentToolBlank;
using Read = MergeTestRead;

class DocumentToolMergeTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void inputLetters();
    void mergeParameters();
    void planParsing();
    void planErrors();
    void planRoundTrip();
    void inputOrder();
    void mergeValidation();
    void mergeGivenParameters();
    void mergeVerifiedOwnerPassword();
    void verifyOwnerPasswordDamagedDictionary();
    void mergeResult();
    void mergePartialResult();
    void mergeImageSettings();
    void blankParameters();
    void blankSizes();
    void blankValidation();
    void blankResult();
    void catalogRows();
    void pdfToolUnite();

private:
    static PDFDocumentToolInputDocument input(const PDFDocumentPointer& document, const QString& fileName, const QString& inputId = QString());
    static QVariantMap parameters(std::initializer_list<std::pair<const char*, QVariant>> values);
};

void DocumentToolMergeTest::initTestCase()
{
    // The texts of the numbers follow the default locale (O17): fixed for the expectations
    QLocale::setDefault(QLocale::c());
}

PDFDocumentToolInputDocument DocumentToolMergeTest::input(const PDFDocumentPointer& document, const QString& fileName, const QString& inputId)
{
    PDFDocumentToolInputDocument result;
    result.document = document;
    result.fileName = fileName;
    result.inputId = inputId;
    return result;
}

QVariantMap DocumentToolMergeTest::parameters(std::initializer_list<std::pair<const char*, QVariant>> values)
{
    QVariantMap result;
    for (const auto& [name, value] : values)
    {
        result.insert(QString::fromLatin1(name), value);
    }
    return result;
}

void DocumentToolMergeTest::inputLetters()
{
    // R20: A to Z, then AA, AB, ... (a bijective base 26)
    QCOMPARE(Merge::getInputLetters(0), QStringLiteral("A"));
    QCOMPARE(Merge::getInputLetters(25), QStringLiteral("Z"));
    QCOMPARE(Merge::getInputLetters(26), QStringLiteral("AA"));
    QCOMPARE(Merge::getInputLetters(27), QStringLiteral("AB"));
    QCOMPARE(Merge::getInputLetters(701), QStringLiteral("ZZ"));
    QCOMPARE(Merge::getInputLetters(702), QStringLiteral("AAA"));
    QCOMPARE(Merge::getInputLetters(-1), QString());
    for (int i = 0; i < 2000; ++i)
    {
        QCOMPARE(Merge::getInputIndex(Merge::getInputLetters(i)), i);
    }
    QCOMPARE(Merge::getInputIndex(u"a"), -1);
    QCOMPARE(Merge::getInputIndex(u""), -1);
    QCOMPARE(Merge::getInputIndex(u"A1"), -1);
    QCOMPARE(Merge::getInputIndex(u"AAAAAAA"), -1);
}

void DocumentToolMergeTest::mergeParameters()
{
    // Plan chapter 9 "Pevná rozhraní", Q11: inputs and plan are values of the panels only (not
    // kept, not in the schema of MCP); outline and optimizeImages with the menus of 378:1879
    Merge tool;
    const PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QString error;
    QVERIFY2(PDFDocumentToolParameters::isValid(definitions, &error), qPrintable(error));

    const PDFDocumentToolParameterDefinition* inputs = PDFDocumentToolParameters::find(definitions, QStringLiteral("inputs"));
    const PDFDocumentToolParameterDefinition* plan = PDFDocumentToolParameters::find(definitions, QStringLiteral("plan"));
    const PDFDocumentToolParameterDefinition* outline = PDFDocumentToolParameters::find(definitions, QStringLiteral("outline"));
    const PDFDocumentToolParameterDefinition* optimize = PDFDocumentToolParameters::find(definitions, QStringLiteral("optimizeImages"));
    QVERIFY(inputs && plan && outline && optimize);
    QVERIFY(!inputs->isStored() && !plan->isStored());
    QVERIFY(inputs->isPanelOnly() && plan->isPanelOnly());
    QCOMPARE(plan->getMaximumLength(), Merge::MAXIMUM_PLAN_LENGTH);
    QCOMPARE(outline->getDefaultValue().toString(), QStringLiteral("document-parts"));
    QCOMPARE(optimize->getDefaultValue().toString(), QStringLiteral("off"));

    QStringList outlineIds;
    for (const PDFDocumentToolParameterOption& option : outline->getOptions())
    {
        outlineIds << option.id;
    }
    QCOMPARE(outlineIds, QStringList({ QStringLiteral("document-parts"), QStringLiteral("join-outlines"), QStringLiteral("no-outline") }));
    QStringList optimizeIds;
    for (const PDFDocumentToolParameterOption& option : optimize->getOptions())
    {
        optimizeIds << option.id;
    }
    QCOMPARE(optimizeIds, QStringList({ QStringLiteral("off"), QStringLiteral("prefer-quality"), QStringLiteral("minimum-size") }));

    const QJsonObject schema = PDFDocumentToolParameters::toJsonSchema(definitions);
    QCOMPARE(schema.value(QStringLiteral("properties")).toObject().keys(), QStringList({ QStringLiteral("optimizeImages"), QStringLiteral("outline") }));
}

void DocumentToolMergeTest::planParsing()
{
    // The syntax of the plan: ranges (also reversed), turns @90, @180, @270, + of a further copy
    std::optional<std::vector<Merge::PlanItem>> items = Merge::parsePlan(QStringLiteral("A1-2,A3@90,A4-6,B9-7,C1,A3+"), { 6, 9, 1 }, nullptr);
    QVERIFY(items);
    QCOMPARE(items->size(), size_t(11));
    QCOMPARE((*items)[0], (Merge::PlanItem{ 0, 0, PageRotation::None, false }));
    QCOMPARE((*items)[2], (Merge::PlanItem{ 0, 2, PageRotation::Rotate90, false }));
    QCOMPARE((*items)[6], (Merge::PlanItem{ 1, 8, PageRotation::None, false }));
    QCOMPARE((*items)[8], (Merge::PlanItem{ 1, 6, PageRotation::None, false }));
    QCOMPARE((*items)[9], (Merge::PlanItem{ 2, 0, PageRotation::None, false }));
    QCOMPARE((*items)[10], (Merge::PlanItem{ 0, 2, PageRotation::None, true }));

    items = Merge::parsePlan(QStringLiteral(" A1@180+ , A1@270+ ,A1+"), { 1 }, nullptr);
    QVERIFY(items);
    QCOMPARE(items->size(), size_t(3));
    QCOMPARE((*items)[0], (Merge::PlanItem{ 0, 0, PageRotation::Rotate180, false }));
    QCOMPARE((*items)[1], (Merge::PlanItem{ 0, 0, PageRotation::Rotate270, true }));
    QCOMPARE((*items)[2], (Merge::PlanItem{ 0, 0, PageRotation::None, true }));

    // The input 27 is AA
    std::vector<PDFInteger> counts(27, 1);
    items = Merge::parsePlan(QStringLiteral("AA1,Z1"), counts, nullptr);
    QVERIFY(items);
    QCOMPARE((*items)[0].input, 26);
    QCOMPARE((*items)[1].input, 25);

    const std::vector<Merge::PlanItem> defaultPlan = Merge::createDefaultPlan({ 2, 1 });
    QCOMPARE(defaultPlan.size(), size_t(3));
    QCOMPARE(defaultPlan[2], (Merge::PlanItem{ 1, 0, PageRotation::None, false }));
}

void DocumentToolMergeTest::planErrors()
{
    for (const QString& plan : { QStringLiteral("A0"), QStringLiteral("A7"), QStringLiteral("D1"), QStringLiteral("A1,A1"), QStringLiteral("a1"), QStringLiteral("A1@45"),
                                 QStringLiteral("A"), QStringLiteral("A1-"), QStringLiteral("A1-3x"), QStringLiteral("1"), QStringLiteral("A99999999999"), QStringLiteral(","), QString() })
    {
        QString error;
        QVERIFY2(!Merge::parsePlan(plan, { 6, 9, 1 }, &error), qPrintable(plan));
        QVERIFY2(!error.isEmpty(), qPrintable(plan));
    }

    QString error;
    QVERIFY(!Merge::parsePlan(QString(Merge::MAXIMUM_PLAN_LENGTH + 1, QLatin1Char('A')), { 1 }, &error));
    QVERIFY(!error.isEmpty());
}

void DocumentToolMergeTest::planRoundTrip()
{
    // createPlan writes runs as ranges; parsePlan reads it back to the same items
    std::vector<Merge::PlanItem> items;
    for (PDFInteger page = 0; page < 6; ++page)
    {
        items.push_back({ 0, page, PageRotation::None, false });
    }
    QCOMPARE(Merge::createPlan(items), QStringLiteral("A1-6"));

    items.push_back({ 1, 3, PageRotation::None, false });
    items.push_back({ 1, 2, PageRotation::None, false });
    items.push_back({ 1, 1, PageRotation::None, false });
    items.push_back({ 0, 2, PageRotation::Rotate90, true });
    items.push_back({ 0, 2, PageRotation::None, true });
    items.push_back({ 2, 0, PageRotation::Rotate270, false });
    const QString plan = Merge::createPlan(items);
    QCOMPARE(plan, QStringLiteral("A1-6,B4-2,A3@90+,A3+,C1@270"));
    QCOMPARE(*Merge::parsePlan(plan, { 6, 4, 1 }, nullptr), items);

    // Sorted by the page numbers (K13): A1, B1, A2, B2, ... is a long plan, within its limit
    std::vector<Merge::PlanItem> interleaved;
    for (PDFInteger page = 0; page < 25000; ++page)
    {
        interleaved.push_back({ 0, page, PageRotation::None, false });
        interleaved.push_back({ 1, page, PageRotation::None, false });
    }
    const QString longPlan = Merge::createPlan(interleaved);
    QVERIFY(longPlan.size() < Merge::MAXIMUM_PLAN_LENGTH);
    QCOMPARE(Merge::parsePlan(longPlan, { 25000, 25000 }, nullptr)->size(), size_t(50000));
}

void DocumentToolMergeTest::inputOrder()
{
    // "inputs" orders the documents and the images of the context by their ids; empty = the
    // documents, then the images
    PDFDocumentToolContext context;
    context.documents.push_back(input(MergeTestPdf::createPages("A", 2).readPointer(), QStringLiteral("C:/x/Alpha.pdf"), QStringLiteral("id-a")));
    context.documents.push_back(input(MergeTestPdf::createPages("B", 1).readPointer(), QStringLiteral("Beta.pdf"), QStringLiteral("id-b")));
    QImage image(10, 10, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QBuffer::WriteOnly);
    image.save(&buffer, "png");
    PDFDocumentToolInputImage inputImage;
    inputImage.data = png;
    inputImage.fileName = QStringLiteral("photo.png");
    inputImage.inputId = QStringLiteral("id-i");
    context.images.push_back(inputImage);

    std::optional<std::vector<Merge::Input>> inputs = Merge::resolveInputs(context, parameters({ { "inputs", QStringLiteral("id-i, id-b,id-a") } }), nullptr);
    QVERIFY(inputs);
    QCOMPARE(inputs->size(), size_t(3));
    QVERIFY((*inputs)[0].isImage);
    QCOMPARE((*inputs)[0].pageCount, 1);
    QCOMPARE((*inputs)[1].name, QStringLiteral("Beta.pdf"));
    QCOMPARE((*inputs)[2].name, QStringLiteral("Alpha.pdf"));
    QCOMPARE((*inputs)[2].pageCount, 2);

    inputs = Merge::resolveInputs(context, QVariantMap(), nullptr);
    QVERIFY(inputs);
    QCOMPARE((*inputs)[0].inputId, QStringLiteral("id-a"));
    QCOMPARE((*inputs)[2].inputId, QStringLiteral("id-i"));

    for (const QString& order : { QStringLiteral("id-a,id-b"), QStringLiteral("id-a,id-b,id-i,id-a"), QStringLiteral("id-a,id-b,id-x,id-i") })
    {
        QString error;
        QVERIFY2(!Merge::resolveInputs(context, parameters({ { "inputs", order } }), &error), qPrintable(order));
        QVERIFY(!error.isEmpty());
    }
}

void DocumentToolMergeTest::mergeValidation()
{
    // K22: at least two inputs; every input has a page; Q2: the permissions of each document
    Merge tool;
    const PDFDocumentPointer a = MergeTestPdf::createPages("A", 2).readPointer();
    const PDFDocumentPointer b = MergeTestPdf::createPages("B", 1).readPointer();

    PDFDocumentToolContext one;
    one.documents.push_back(input(a, QStringLiteral("a.pdf")));
    QVERIFY(!tool.validate(one, QVariantMap()).empty());

    PDFDocumentToolContext two;
    two.documents.push_back(input(a, QStringLiteral("a.pdf")));
    two.documents.push_back(input(b, QStringLiteral("b.pdf")));
    QVERIFY(tool.validate(two, QVariantMap()).empty());
    QVERIFY(tool.validate(two, parameters({ { "plan", QStringLiteral("B1,A2") } })).empty());
    const std::vector<PDFDocumentToolParameterError> planErrors = tool.validate(two, parameters({ { "plan", QStringLiteral("C1") } }));
    QCOMPARE(planErrors.size(), size_t(1));
    QCOMPARE(planErrors.front().parameter, QStringLiteral("plan"));

    // A document without pages
    PDFDocumentToolContext empty = two;
    empty.documents[1] = input(MergeTestPdf::createPages("E", 0).readPointer(), QStringLiteral("empty.pdf"));
    QVERIFY(!tool.validate(empty, QVariantMap()).empty());

    // An image, which cannot be read
    PDFDocumentToolContext broken = one;
    PDFDocumentToolInputImage image;
    image.data = "not an image";
    image.fileName = QStringLiteral("x.png");
    broken.images.push_back(image);
    const std::vector<PDFDocumentToolParameterError> imageErrors = tool.validate(broken, QVariantMap());
    QCOMPARE(imageErrors.size(), size_t(1));
    QVERIFY(imageErrors.front().message.contains(QStringLiteral("x.png")));

    // A document, which restricts printing, opened without the owner password
    using Permission = PDFSecurityHandler::Permission;
    const QByteArray encrypted = MergeTestPdf::encrypt(*b, QString(), QStringLiteral("owner"), uint32_t(Permission::Assemble) | uint32_t(Permission::CopyContent) | uint32_t(Permission::Modify) |
                                                       uint32_t(Permission::ModifyInteractiveItems) | uint32_t(Permission::ModifyFormFields));
    PDFDocumentToolContext restricted = one;
    restricted.documents.push_back(input(PDFDocumentPointer(new PDFDocument(MergeTestPdf::readBytes(encrypted))), QStringLiteral("locked.pdf")));
    const std::vector<PDFDocumentToolParameterError> permissionErrors = tool.validate(restricted, QVariantMap());
    QCOMPARE(permissionErrors.size(), size_t(1));
    QVERIFY(permissionErrors.front().message.contains(QStringLiteral("locked.pdf")));
    QCOMPARE(tool.run(restricted, QVariantMap(), nullptr, nullptr).status, PDFDocumentToolResultStatus::Failed);
}

void DocumentToolMergeTest::mergeVerifiedOwnerPassword()
{
    // Owner answer Q15: a snapshot of an open tab with unsaved changes was opened by its user
    // password; the owner password is verified against its original file (its encryption
    // dictionary, ISO 32000-2, 7.6.4.4), and the input with the flag passes the check of Q2
    using Permission = PDFSecurityHandler::Permission;
    const PDFDocument plain = MergeTestPdf::createPages("B", 1).read();
    const QByteArray original = MergeTestPdf::encrypt(plain, QString(), QStringLiteral("owner"), uint32_t(Permission::Assemble) | uint32_t(Permission::CopyContent) | uint32_t(Permission::Modify));
    const PDFDocumentPointer snapshot(new PDFDocument(MergeTestPdf::readBytes(original)));
    QCOMPARE(PDFDocumentMerger::getPermission(snapshot.data()), PDFDocumentMerger::Permission::OwnerPasswordRequired);

    // The verification: the owner password only (the empty user password does not count)
    const PDFSecurityHandler* handler = snapshot->getStorage().getSecurityHandler();
    QVERIFY(PDFDocumentMerger::verifyOwnerPassword(handler, QStringLiteral("owner")));
    QVERIFY(!PDFDocumentMerger::verifyOwnerPassword(handler, QStringLiteral("wrong")));
    QVERIFY(!PDFDocumentMerger::verifyOwnerPassword(handler, QString()));
    QCOMPARE(handler->getAuthorizationResult(), PDFSecurityHandler::AuthorizationResult::UserAuthorized);
    QVERIFY(PDFDocumentMerger::verifyOwnerPassword(original, QStringLiteral("owner")));
    QVERIFY(!PDFDocumentMerger::verifyOwnerPassword(original, QStringLiteral("wrong")));
    QVERIFY(!PDFDocumentMerger::verifyOwnerPassword(QByteArray("not a pdf"), QStringLiteral("owner")));
    QVERIFY(PDFDocumentMerger::verifyOwnerPassword(MergeTestPdf::createPages("A", 1).write(), QStringLiteral("anything")));
    QVERIFY(PDFDocumentMerger::verifyOwnerPassword(static_cast<const PDFSecurityHandler*>(nullptr), QString()));

    Merge tool;
    PDFDocumentToolContext context;
    context.documents.push_back(input(MergeTestPdf::createPages("A", 1).readPointer(), QStringLiteral("a.pdf")));
    context.documents.push_back(input(snapshot, QStringLiteral("locked.pdf")));
    QVERIFY(!tool.validate(context, QVariantMap()).empty());
    QCOMPARE(tool.run(context, QVariantMap(), nullptr, nullptr).status, PDFDocumentToolResultStatus::Failed);

    context.documents[1].isOwnerPasswordVerified = true;
    QVERIFY(tool.validate(context, QVariantMap()).empty());
    const PDFDocumentToolResult result = tool.run(context, QVariantMap(), nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    QCOMPARE(MergeTestPdf::readBytes(result.documents.front().writtenData).getCatalog()->getPageCount(), size_t(2));

    // The merger itself (every caller): the flag of the source
    std::vector<PDFDocumentMerger::Source> sources = { PDFDocumentMerger::Source::createDocument(context.documents[0].document, QString()), PDFDocumentMerger::Source::createDocument(snapshot, QString()) };
    std::vector<PDFDocumentMerger::Item> items(2);
    items[1].source = 1;
    QVERIFY(!PDFDocumentMerger::merge(sources, items, PDFDocumentMerger::Settings(), nullptr, nullptr).isSuccess);
    sources[1].isOwnerPasswordVerified = true;
    QVERIFY(PDFDocumentMerger::merge(sources, items, PDFDocumentMerger::Settings(), nullptr, nullptr).isSuccess);
}

void DocumentToolMergeTest::verifyOwnerPasswordDamagedDictionary()
{
    // ISO 32000-2, 7.6.4.4, Algorithm 2.A: revision 6 decrypts /Perms by the key of the password
    // and checks it. A file, whose /OE is damaged, opens by its empty user password (the key of
    // /UE confirms /Perms), but its owner password gives another key: it is not unlocked. The
    // loader of the application verifies in a worker thread - an exception would end the program.
    using Permission = PDFSecurityHandler::Permission;
    const PDFDocument plain = MergeTestPdf::createPages("B", 1).read();
    const PDFDocument opened = MergeTestPdf::readBytes(MergeTestPdf::encrypt(plain, QString(), QStringLiteral("owner"), uint32_t(Permission::Assemble) | uint32_t(Permission::Modify)));
    const PDFObject encryption = opened.getObject(Read::trailer(opened)->get("Encrypt"));
    QVERIFY(encryption.isDictionary());
    QCOMPARE(encryption.getDictionary()->get("R").getInteger(), 6);

    PDFDictionaryBuilder damaged(*encryption.getDictionary());
    damaged.setEntry(PDFInplaceOrMemoryString("OE"), PDFObject::createString(QByteArray(32, 'x')));
    PDFSecurityHandlerPointer handler = PDFSecurityHandler::createSecurityHandler(PDFObject::createDictionary(std::move(damaged)), QByteArray());
    QVERIFY(handler);
    auto noPassword = [](bool* ok) -> QString
    {
        *ok = false;
        return QString();
    };
    QCOMPARE(handler->authenticate(noPassword, false), PDFSecurityHandler::AuthorizationResult::UserAuthorized);

    bool isVerified = true;
    try
    {
        isVerified = PDFDocumentMerger::verifyOwnerPassword(handler.data(), QStringLiteral("owner"));
    }
    catch (const PDFException&)
    {
        QFAIL("The verification of the owner password threw an exception.");
    }
    QVERIFY(!isVerified);
    QCOMPARE(handler->getAuthorizationResult(), PDFSecurityHandler::AuthorizationResult::UserAuthorized);

    // The undamaged handler is verified by the same password
    QVERIFY(PDFDocumentMerger::verifyOwnerPassword(opened.getStorage().getSecurityHandler(), QStringLiteral("owner")));
}

void DocumentToolMergeTest::mergeGivenParameters()
{
    // Q11: an assistant gives documentIds, never the inputs or the plan
    Merge tool;
    QVERIFY(tool.checkGivenParameters(parameters({ { "outline", QStringLiteral("no-outline") } })).empty());
    const std::vector<PDFDocumentToolParameterError> errors = tool.checkGivenParameters(parameters({ { "plan", QStringLiteral("A1") }, { "inputs", QStringLiteral("x") } }));
    QCOMPARE(errors.size(), size_t(2));
}

void DocumentToolMergeTest::mergeResult()
{
    // Chapter 4.5 of the plan, 378:3674: a New document with its written bytes (one write), the name
    // "<first>_merged.pdf", the facts of the result dialog
    Merge tool;
    PDFDocumentToolContext context;
    context.documents.push_back(input(MergeTestPdf::createPages("A", 2).readPointer(), QStringLiteral("D:/docs/Contract.pdf")));
    context.documents.push_back(input(MergeTestPdf::createPages("B", 3).readPointer(), QStringLiteral("Annex.pdf")));

    const PDFDocumentToolResult result = tool.run(context, QVariantMap(), nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    QVERIFY(result.leftOutInputs.empty());
    QCOMPARE(result.documents.size(), size_t(1));
    const PDFDocumentToolOutputDocument& output = result.documents.front();
    QCOMPARE(output.kind, PDFDocumentToolOutputDocument::Kind::New);
    QCOMPARE(output.suggestedFileName, QStringLiteral("Contract_merged.pdf"));
    QVERIFY(!output.writtenData.isEmpty());
    QCOMPARE(output.writtenData, MergeTestPdf::writeDocument(*output.document));
    QCOMPARE(MergeTestPdf::readBytes(output.writtenData).getCatalog()->getPageCount(), size_t(5));

    std::map<QString, QString> facts;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        facts[fact.id] = fact.value;
    }
    QCOMPARE(facts[QStringLiteral("pages")], QStringLiteral("5 from 2 files"));
    QCOMPARE(facts[QStringLiteral("outline")], QStringLiteral("2 document parts"));
    QCOMPARE(facts[QStringLiteral("size")], PDFDocumentToolFormat::formatSize(output.writtenData.size()));

    // Optimize off (the default): the dialog shows only Pages, Outline and Size (378:3674,
    // 412:32654), no fact of the images
    QStringList factIds;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        factIds << fact.id;
    }
    QCOMPARE(factIds, QStringList({ QStringLiteral("pages"), QStringLiteral("outline"), QStringLiteral("size") }));
    QCOMPARE(result.data.value(QStringLiteral("pageCount")).toInt(), 5);
    QCOMPARE(result.data.value(QStringLiteral("sizeAfter")).toInteger(), qint64(output.writtenData.size()));

    // The plan of the pages: B3, A1 (turned) and a clone of A1
    const PDFDocumentToolResult planned = tool.run(context, parameters({ { "plan", QStringLiteral("B3,A1@90,A1+") }, { "outline", QStringLiteral("no-outline") } }), nullptr, nullptr);
    QCOMPARE(planned.status, PDFDocumentToolResultStatus::Success);
    const PDFDocument document = MergeTestPdf::readBytes(planned.documents.front().writtenData);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(3));
    QCOMPARE(document.getCatalog()->getPage(1)->getPageRotation(), PageRotation::Rotate90);
    QCOMPARE(document.getCatalog()->getPage(2)->getPageRotation(), PageRotation::None);
    const QByteArray contents = Read::pageContents(document);
    QVERIFY(contents.startsWith("% BPAGE3"));
    QVERIFY(!Read::catalog(document)->hasKey("Outlines"));

    // An image with a document
    QImage image(30, 30, QImage::Format_RGB32);
    image.fill(Qt::green);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QBuffer::WriteOnly);
    image.save(&buffer, "png");
    PDFDocumentToolInputImage inputImage;
    inputImage.data = png;
    inputImage.fileName = QStringLiteral("photo.png");
    inputImage.dotsPerInch = 72.0;
    context.images.push_back(inputImage);
    const PDFDocumentToolResult withImage = tool.run(context, QVariantMap(), nullptr, nullptr);
    QCOMPARE(withImage.status, PDFDocumentToolResultStatus::Success);
    const PDFDocument imageDocument = MergeTestPdf::readBytes(withImage.documents.front().writtenData);
    QCOMPARE(imageDocument.getCatalog()->getPageCount(), size_t(6));
    QCOMPARE(imageDocument.getCatalog()->getPage(5)->getMediaBox(), QRectF(0, 0, 30, 30));

    // Optimize on: the images are a fact, the number of the recompressed ones
    const PDFDocumentToolResult optimized = tool.run(context, parameters({ { "optimizeImages", QStringLiteral("prefer-quality") } }), nullptr, nullptr);
    QCOMPARE(optimized.status, PDFDocumentToolResultStatus::Success);
    const auto imagesFact = std::find_if(optimized.facts.cbegin(), optimized.facts.cend(), [](const PDFDocumentToolFact& fact) { return fact.id == QLatin1String("images"); });
    QVERIFY(imagesFact != optimized.facts.cend());
    QCOMPARE(imagesFact->title, QStringLiteral("Images"));
    QVERIFY2(QRegularExpression(QStringLiteral("^\\d+ recompressed$")).match(imagesFact->value).hasMatch(), qPrintable(imagesFact->value));
}

void DocumentToolMergeTest::mergePartialResult()
{
    // Owner answer Q5, 378:4376: an input, whose copy fails during the run, is left out - the
    // result is a success with leftOutInputs (id, index, name, pages, reason) and the fact
    PDFDocument broken = MergeTestPdf::createPages("B", 4).read();
    {
        PDFDocumentBuilder builder(&broken);
        PDFObject deep = PDFObject::createInteger(1);
        for (int i = 0; i < 600; ++i)
        {
            PDFArrayBuilder array;
            array.appendItem(deep);
            deep = PDFObject::createArray(std::move(array));
        }
        PDFDictionaryBuilder catalog;
        catalog.addEntry(PDFInplaceOrMemoryString("Deep"), PDFObject::createReference(builder.addObject(deep)));
        builder.mergeTo(builder.getCatalogReference(), PDFObject::createDictionary(std::move(catalog)));
        broken = builder.build();
    }

    Merge tool;
    PDFDocumentToolContext context;
    context.documents.push_back(input(MergeTestPdf::createPages("A", 1).readPointer(), QStringLiteral("Contract.pdf"), QStringLiteral("a")));
    context.documents.push_back(input(PDFDocumentPointer(new PDFDocument(broken)), QStringLiteral("Annex_A.pdf"), QStringLiteral("b")));
    context.documents.push_back(input(MergeTestPdf::createPages("C", 1).readPointer(), QStringLiteral("Invoice.pdf"), QStringLiteral("c")));

    const PDFDocumentToolResult result = tool.run(context, QVariantMap(), nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.leftOutInputs.size(), size_t(1));
    const PDFDocumentToolLeftOutInput& leftOut = result.leftOutInputs.front();
    QCOMPARE(leftOut.inputId, QStringLiteral("b"));
    QCOMPARE(leftOut.inputIndex, 1);
    QCOMPARE(leftOut.name, QStringLiteral("Annex_A.pdf"));
    QCOMPARE(leftOut.pageCount, 4);
    QVERIFY(!leftOut.reason.isEmpty());
    QCOMPARE(MergeTestPdf::readBytes(result.documents.front().writtenData).getCatalog()->getPageCount(), size_t(2));

    bool hasFact = false;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        hasFact = hasFact || (fact.id == QStringLiteral("leftOut") && fact.value == QStringLiteral("Annex_A.pdf · 4 pages"));
    }
    QVERIFY(hasFact);
    QCOMPARE(result.toJson().value(QStringLiteral("status")).toString(), QStringLiteral("success"));
}

void DocumentToolMergeTest::mergeImageSettings()
{
    // K24, R19: Prefer quality and Minimum size are the images of the presets Balanced and Smaller
    // file of Compress (one definition)
    QVERIFY(!Merge::createImageSettings(parameters({ { "optimizeImages", QStringLiteral("off") } })));

    const std::optional<PDFImageOptimizer::Settings> quality = Merge::createImageSettings(parameters({ { "optimizeImages", QStringLiteral("prefer-quality") } }));
    const PDFImageOptimizer::Settings balanced = PDFDocumentToolCompress::createSettings(parameters({ { "preset", QStringLiteral("balanced") } })).images;
    QVERIFY(quality);
    QCOMPARE(quality->enabled, balanced.enabled);
    QCOMPARE(quality->goal, balanced.goal);
    QCOMPARE(quality->colorProfile.targetDpi, balanced.colorProfile.targetDpi);
    QCOMPARE(quality->colorProfile.targetDpi, 200);
    QVERIFY(quality->enabled);

    const std::optional<PDFImageOptimizer::Settings> minimum = Merge::createImageSettings(parameters({ { "optimizeImages", QStringLiteral("minimum-size") } }));
    const PDFImageOptimizer::Settings smaller = PDFDocumentToolCompress::createSettings(parameters({ { "preset", QStringLiteral("smaller-file") } })).images;
    QVERIFY(minimum);
    QCOMPARE(minimum->goal, smaller.goal);
    QCOMPARE(minimum->colorProfile.targetDpi, 110);
    QVERIFY(minimum->goal != quality->goal);

    QCOMPARE(Merge::createSettings(parameters({ { "outline", QStringLiteral("join-outlines") } })).outline, PDFDocumentMerger::OutlineMode::Join);
    QCOMPARE(Merge::createSettings(parameters({ { "outline", QStringLiteral("no-outline") } })).outline, PDFDocumentMerger::OutlineMode::NoOutline);
    QCOMPARE(Merge::createSettings(QVariantMap()).outline, PDFDocumentMerger::OutlineMode::DocumentParts);
    QCOMPARE(Merge::getSuggestedFileName(QString()), QStringLiteral("merged.pdf"));
    QCOMPARE(Merge::getSuggestedFileName(QStringLiteral("a.b.pdf")), QStringLiteral("a.b_merged.pdf"));
}

void DocumentToolMergeTest::blankParameters()
{
    // Plan chapter 9 and 5.4, K26: pageCount 1 to 10 000 (default 1), the sizes of 337:2221,
    // width and height in millimeters of 3 to 14 400 points, the orientation
    Blank tool;
    const PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QString error;
    QVERIFY2(PDFDocumentToolParameters::isValid(definitions, &error), qPrintable(error));

    const PDFDocumentToolParameterDefinition* count = PDFDocumentToolParameters::find(definitions, QStringLiteral("pageCount"));
    QCOMPARE(count->getIntegerMinimum(), 1);
    QCOMPARE(count->getIntegerMaximum(), 10000);
    QCOMPARE(count->getDefaultValue().toLongLong(), 1);

    const PDFDocumentToolParameterDefinition* size = PDFDocumentToolParameters::find(definitions, QStringLiteral("pageSize"));
    QStringList sizes;
    for (const PDFDocumentToolParameterOption& option : size->getOptions())
    {
        sizes << option.id;
    }
    QCOMPARE(sizes, QStringList({ QStringLiteral("a4"), QStringLiteral("a3"), QStringLiteral("a5"), QStringLiteral("letter"), QStringLiteral("legal"), QStringLiteral("16x9"), QStringLiteral("custom") }));
    QCOMPARE(size->getDefaultValue().toString(), QStringLiteral("a4"));

    const PDFDocumentToolParameterDefinition* width = PDFDocumentToolParameters::find(definitions, QStringLiteral("width"));
    QVERIFY(std::abs(width->getRealMinimum() - 3.0 * 25.4 / 72.0) < 1e-9);
    QVERIFY(std::abs(width->getRealMaximum() - 5080.0) < 1e-9);
    QCOMPARE(PDFDocumentToolParameters::find(definitions, QStringLiteral("orientation"))->getDefaultValue().toString(), QStringLiteral("portrait"));

    QVariantMap normalized;
    QVERIFY(!PDFDocumentToolParameters::normalize(definitions, parameters({ { "pageCount", 10001 } }), &normalized).empty());
    QVERIFY(!PDFDocumentToolParameters::normalize(definitions, parameters({ { "pageCount", 0 } }), &normalized).empty());
    QVERIFY(PDFDocumentToolParameters::normalize(definitions, parameters({ { "pageCount", 10000 } }), &normalized).empty());
}

void DocumentToolMergeTest::blankSizes()
{
    // 5.4 of the plan: ISO 216 sizes (mm x 72 / 25.4), Letter 612 x 792, Legal 612 x 1008; Q14: 16:9
    // is 960 x 540 in Landscape, 540 x 960 in Portrait; Landscape swaps the sides
    auto sizeOf = [](const char* size, const char* orientation)
    {
        return Blank::getPageSize(parameters({ { "pageSize", QString::fromLatin1(size) }, { "orientation", QString::fromLatin1(orientation) } }), nullptr);
    };
    auto isClose = [](QSizeF size, double width, double height)
    {
        return std::abs(size.width() - width) < 0.001 && std::abs(size.height() - height) < 0.001;
    };
    QVERIFY(isClose(sizeOf("a4", "portrait"), 595.276, 841.890));
    QVERIFY(isClose(sizeOf("a4", "landscape"), 841.890, 595.276));
    QVERIFY(isClose(sizeOf("a3", "portrait"), 841.890, 1190.551));
    QVERIFY(isClose(sizeOf("a5", "portrait"), 419.528, 595.276));
    QVERIFY(isClose(sizeOf("letter", "portrait"), 612.0, 792.0));
    QVERIFY(isClose(sizeOf("legal", "portrait"), 612.0, 1008.0));
    QVERIFY(isClose(sizeOf("16x9", "landscape"), 960.0, 540.0));
    QVERIFY(isClose(sizeOf("16x9", "portrait"), 540.0, 960.0));

    // Custom: width x height as given
    QVERIFY(isClose(Blank::getPageSize(parameters({ { "pageSize", QStringLiteral("custom") }, { "width", 200.0 }, { "height", 280.0 } }), nullptr), 566.929, 793.701));
    QVERIFY(isClose(Blank::getPageSize(parameters({ { "pageSize", QStringLiteral("custom") }, { "width", 280.0 }, { "height", 200.0 }, { "orientation", QStringLiteral("portrait") } }), nullptr), 793.701, 566.929));

    // Table C.1: 3 and 14 400 points pass, 2.9 and 14 401 do not
    QVERIFY(Blank::isValidSide(3.0));
    QVERIFY(Blank::isValidSide(14400.0));
    QVERIFY(!Blank::isValidSide(2.9));
    QVERIFY(!Blank::isValidSide(14401.0));
    QVERIFY(!Blank::isValidSide(std::nan("")));
    QString error;
    QVERIFY(Blank::getPageSize(parameters({ { "pageSize", QStringLiteral("custom") }, { "width", 2.9 * 25.4 / 72.0 }, { "height", 100.0 } }), &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(!Blank::getPageSize(parameters({ { "pageSize", QStringLiteral("custom") }, { "width", 3.0 * 25.4 / 72.0 }, { "height", 5080.0 } }), &error).isEmpty());
}

void DocumentToolMergeTest::blankValidation()
{
    // A value, which an assistant gives and which would be ignored, is an error (newapp_mcp.md 6)
    Blank tool;
    QVERIFY(tool.checkGivenParameters(parameters({ { "pageSize", QStringLiteral("a4") }, { "orientation", QStringLiteral("landscape") } })).empty());
    QVERIFY(tool.checkGivenParameters(parameters({ { "pageSize", QStringLiteral("custom") }, { "width", 100.0 }, { "height", 100.0 } })).empty());
    QCOMPARE(tool.checkGivenParameters(parameters({ { "pageSize", QStringLiteral("a4") }, { "width", 100.0 } })).size(), size_t(1));
    QCOMPARE(tool.checkGivenParameters(parameters({ { "width", 100.0 } })).size(), size_t(1));
    QCOMPARE(tool.checkGivenParameters(parameters({ { "pageSize", QStringLiteral("custom") }, { "width", 100.0 }, { "height", 100.0 }, { "orientation", QStringLiteral("portrait") } })).size(), size_t(1));
    QCOMPARE(tool.checkGivenParameters(parameters({ { "pageSize", QStringLiteral("custom") }, { "width", 100.0 } })).size(), size_t(1));

    PDFDocumentToolContext context;
    QVERIFY(tool.validate(context, PDFDocumentToolParameters::getDefaultValues(tool.getParameters())).empty());
}

void DocumentToolMergeTest::blankResult()
{
    // 337:1976, 4.6 of the plan: a New document of the pages, named "Untitled.pdf", written once
    Blank tool;
    PDFDocumentToolContext context;
    const PDFDocumentToolResult result = tool.run(context, parameters({ { "pageCount", 3 }, { "pageSize", QStringLiteral("letter") } }), nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.documents.size(), size_t(1));
    const PDFDocumentToolOutputDocument& output = result.documents.front();
    QCOMPARE(output.kind, PDFDocumentToolOutputDocument::Kind::New);
    QCOMPARE(output.suggestedFileName, QStringLiteral("Untitled.pdf"));
    QCOMPARE(output.writtenData, MergeTestPdf::writeDocument(*output.document));

    const PDFDocument document = MergeTestPdf::readBytes(output.writtenData);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(3));
    QCOMPARE(document.getCatalog()->getPage(2)->getMediaBox(), QRectF(0, 0, 612, 792));
    QCOMPARE(document.getInfo()->version, PDFVersion(1, 7));

    std::map<QString, QString> facts;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        facts[fact.id] = fact.value;
    }
    QCOMPARE(facts[QStringLiteral("pages")], QStringLiteral("3 blank pages"));
    // K25: at most one decimal place, no trailing zero
    QCOMPARE(facts[QStringLiteral("pageSize")], QStringLiteral("215.9 × 279.4 mm"));
    QCOMPARE(result.data.value(QStringLiteral("orientation")).toString(), QStringLiteral("portrait"));

    const PDFDocumentToolResult a4 = tool.run(context, QVariantMap(), nullptr, nullptr);
    QCOMPARE(a4.status, PDFDocumentToolResultStatus::Success);
    QCOMPARE(MergeTestPdf::readBytes(a4.documents.front().writtenData).getCatalog()->getPageCount(), size_t(1));
    bool hasSize = false;
    for (const PDFDocumentToolFact& fact : a4.facts)
    {
        hasSize = hasSize || (fact.id == QStringLiteral("pageSize") && fact.value == QStringLiteral("210 × 297 mm"));
    }
    QVERIFY(hasSize);

    QCOMPARE(tool.run(context, parameters({ { "pageSize", QStringLiteral("custom") }, { "width", 1.0 }, { "height", 100.0 } }), nullptr, nullptr).status, PDFDocumentToolResultStatus::Failed);

    // Owner answer Q22 (as Q14 of the GUI): 16:9 given without an orientation is landscape,
    // 960 x 540 points; a given portrait stays 540 x 960; another size keeps the default portrait
    auto mediaBoxOf = [&tool, &context](const QVariantMap& given)
    {
        const PDFDocumentToolResult run = tool.run(context, given, nullptr, nullptr);
        return run.documents.empty() ? QRectF() : MergeTestPdf::readBytes(run.documents.front().writtenData).getCatalog()->getPage(0)->getMediaBox();
    };
    QCOMPARE(mediaBoxOf(parameters({ { "pageSize", QStringLiteral("16x9") } })), QRectF(0, 0, 960, 540));
    QCOMPARE(mediaBoxOf(parameters({ { "pageSize", QStringLiteral("16x9") }, { "orientation", QStringLiteral("portrait") } })), QRectF(0, 0, 540, 960));
    QCOMPARE(mediaBoxOf(parameters({ { "pageSize", QStringLiteral("16x9") }, { "orientation", QStringLiteral("landscape") } })), QRectF(0, 0, 960, 540));
    QCOMPARE(mediaBoxOf(parameters({ { "pageSize", QStringLiteral("letter") } })), QRectF(0, 0, 612, 792));
    QCOMPARE(tool.run(context, parameters({ { "pageSize", QStringLiteral("16x9") } }), nullptr, nullptr).data.value(QStringLiteral("orientation")).toString(), QStringLiteral("landscape"));
    QVERIFY(!tool.completeGivenParameters(parameters({ { "pageSize", QStringLiteral("a4") } })).contains(QStringLiteral("orientation")));
}

void DocumentToolMergeTest::catalogRows()
{
    // K1, R14: Blank PDF has its own tab; K35: Merge needs bit 11 (Table 22), Blank nothing
    const PDFDocumentToolCatalogEntry* blank = PDFDocumentToolCatalog::findTool(QStringLiteral("convert.blank"));
    QVERIFY(blank->hasOwnTab());
    QVERIFY(blank->getRequiredPermissions().empty());
    QCOMPARE(blank->getInputKind(), PDFDocumentToolInputKind::None);
    const PDFDocumentToolCatalogEntry* merge = PDFDocumentToolCatalog::findTool(QStringLiteral("pages.merge"));
    QVERIFY(merge->hasOwnTab());
    QCOMPARE(merge->getRequiredPermissions(), std::vector<PDFSecurityHandler::Permission>({ PDFSecurityHandler::Permission::Assemble }));
    QCOMPARE(merge->getResultKind(), PDFDocumentToolResultKind::NewDocument);
}

void DocumentToolMergeTest::pdfToolUnite()
{
    // Q12, B15: PdfTool unite is the merger of Merge - its password option opens an encrypted
    // document, a document, which restricts printing, needs the owner password (Q2); the named
    // destinations are readable after the merge (B2)
#ifndef PDF4QT_TEST_PDFTOOL
    QSKIP("PdfTool is not built.");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto writeFile = [&directory](const QString& name, const QByteArray& data)
    {
        QFile file(directory.filePath(name));
        if (!file.open(QFile::WriteOnly) || file.write(data) != data.size())
        {
            return QString();
        }
        return file.fileName();
    };
    auto unite = [](const QStringList& arguments)
    {
        QProcess process;
        process.setProgram(QStringLiteral(PDF4QT_TEST_PDFTOOL));
        process.setArguments(QStringList({ QStringLiteral("unite") }) + arguments);
        process.start();
        process.waitForFinished(60000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR")));
        return process.exitCode();
    };

    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/Names << /Dests << /Names [(intro) [3 0 R /Fit]] >> >>");
    MergeTestPdf b = MergeTestPdf::createPages("B", 2, "/Names << /Dests << /Names [(intro) [3 0 R /Fit]] >> >>");
    const QString fileA = writeFile(QStringLiteral("a.pdf"), a.write());
    using Permission = PDFSecurityHandler::Permission;
    const uint32_t noPrint = uint32_t(Permission::Assemble) | uint32_t(Permission::CopyContent) | uint32_t(Permission::Modify) | uint32_t(Permission::ModifyInteractiveItems) | uint32_t(Permission::ModifyFormFields);
    const QString fileB = writeFile(QStringLiteral("b.pdf"), MergeTestPdf::encrypt(b.read(), QStringLiteral("user"), QStringLiteral("owner"), noPrint));

    QVERIFY(unite({ fileA, fileB, directory.filePath(QStringLiteral("nopassword.pdf")) }) != 0);
    QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("nopassword.pdf"))));
    QVERIFY(unite({ QStringLiteral("--pswd"), QStringLiteral("user"), fileA, fileB, directory.filePath(QStringLiteral("user.pdf")) }) != 0);
    QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("user.pdf"))));

    const QString target = directory.filePath(QStringLiteral("merged.pdf"));
    QCOMPARE(unite({ QStringLiteral("--pswd"), QStringLiteral("owner"), fileA, fileB, target }), 0);
    QFile file(target);
    QVERIFY(file.open(QFile::ReadOnly));
    const PDFDocument result = MergeTestPdf::readBytes(file.readAll());
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(3));
    const PDFDictionary* names = Read::path(result, { "Names", "Dests" });
    QVERIFY(names);
    const std::vector<PDFObject> pairs = Read::items(result, names->get("Names"));
    QCOMPARE(pairs.size(), size_t(4));
    QVERIFY(Read::object(result, pairs[0]).isString());
    QCOMPARE(Read::object(result, pairs[0]).getString(), QByteArray("intro"));
    QCOMPARE(Read::object(result, pairs[2]).getString(), QByteArray("intro_2"));
    QVERIFY(Read::catalog(result)->hasKey("Outlines"));

    // Owner answer Q2': most restricted files have an empty user password - they open by it
    // without asking (7.6.4.4), and --pswd is then their owner password: it unlocks the merge;
    // another password does not
    const QString fileC = writeFile(QStringLiteral("c.pdf"), MergeTestPdf::encrypt(b.read(), QString(), QStringLiteral("owner"), noPrint));
    QVERIFY(unite({ fileA, fileC, directory.filePath(QStringLiteral("c-none.pdf")) }) != 0);
    QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("c-none.pdf"))));
    QVERIFY(unite({ QStringLiteral("--pswd"), QStringLiteral("wrong"), fileA, fileC, directory.filePath(QStringLiteral("c-wrong.pdf")) }) != 0);
    QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("c-wrong.pdf"))));
    const QString targetC = directory.filePath(QStringLiteral("c-owner.pdf"));
    QCOMPARE(unite({ QStringLiteral("--pswd"), QStringLiteral("owner"), fileA, fileC, targetC }), 0);
    QFile fileOfC(targetC);
    QVERIFY(fileOfC.open(QFile::ReadOnly));
    bool isRead = false;
    const PDFDocument resultC = MergeTestPdf::readBytes(fileOfC.readAll(), QString(), &isRead);
    QVERIFY(isRead);
    QCOMPARE(resultC.getCatalog()->getPageCount(), size_t(3));
    QVERIFY(!Read::trailer(resultC)->hasKey("Encrypt"));
#endif
}

QTEST_GUILESS_MAIN(DocumentToolMergeTest)

#include "tst_documenttoolmergetest.moc"
