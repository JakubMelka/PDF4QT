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

// The tools Split and Extract Pages of the core (package A of step 3a of phase 2, plan 5.6, 7,
// 8, K49, Q4, R2, R3): the rules of Split by the frames 383:3315 ... 383:9088 on 48 pages, the
// names of the files, getResultKind, the permissions, the stopped run, Extract one file and one
// file per page, PdfTool separate (Q16, B36-B38).

#include "mergetestpdf.h"

#include "pdfdocumentmerger.h"
#include "pdfdocumenttool_split.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfoperationcontrol.h"
#include "pdfprogress.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace pdf;
using namespace mergetest;
using Read = mergetest::MergeTestRead;

/// A file sink of the memory, which stops after some files
class SplitToolTestSink : public PDFDocumentToolFileSink
{
public:
    explicit SplitToolTestSink(int stopAfter = -1) : PDFDocumentToolFileSink(ExistingFiles::Rename), m_stopAfter(stopAfter) { }

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

    virtual bool isStopped() const override { return m_stopAfter >= 0 && int(files.size()) >= m_stopAfter; }

    QStringList begunNames;
    std::vector<std::pair<QString, QByteArray>> files;

private:
    int m_stopAfter;
};

/// A progress, which records its phases and, at the beginning of each phase, the files given to
/// the file sink before it
class SplitToolTestProgress : public PDFProgressSink
{
public:
    explicit SplitToolTestProgress(const SplitToolTestSink* sink) : m_sink(sink) { }

    virtual void start(size_t count, ProgressStartupInfo startupInfo) override { Q_UNUSED(count); Q_UNUSED(startupInfo); ++startCount; }
    virtual void step() override { ++stepCount; }
    virtual void finish() override { }
    virtual void setPhases(std::vector<PDFProgressPhase> phases) override
    {
        for (const PDFProgressPhase& phase : phases)
        {
            phaseTexts << phase.text;
        }
    }
    virtual void beginPhase(int index) override { begunPhases.emplace_back(index, int(m_sink->files.size())); }
    virtual void finishPhases() override { ++finishCount; }

    QStringList phaseTexts;
    std::vector<std::pair<int, int>> begunPhases;   ///< The phase and the files written before it began
    int startCount = 0;
    int stepCount = 0;
    int finishCount = 0;

private:
    const SplitToolTestSink* m_sink;
};

/// A control, which is canceled from the beginning
class SplitToolTestCanceledControl : public PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return true; }
};

/// Helpers of the test
class SplitToolTestHelper
{
public:
    SplitToolTestHelper() = delete;

    static PDFDocumentToolContext context(const PDFDocument& document, PDFDocumentToolFileSink* sink = nullptr)
    {
        PDFDocumentToolContext context = PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(PDFDocumentPointer(new PDFDocument(document)), QByteArray(), QStringLiteral("Handbook.pdf"), true));
        context.fileSink = sink;
        return context;
    }

    static QVariant pages(const QString& range)
    {
        PDFDocumentToolPageSelection selection;
        selection.pages = range;
        return selection.toVariant();
    }

    /// The pages of the files as "1-12 13-24" (numbers from 1)
    static QString describe(const std::vector<PDFDocumentToolFileOutput::File>& files)
    {
        QStringList parts;
        for (const PDFDocumentToolFileOutput::File& file : files)
        {
            QStringList numbers;
            const bool isRun = file.pages.back() - file.pages.front() + 1 == PDFInteger(file.pages.size());
            if (isRun && file.pages.size() > 1)
            {
                numbers << QStringLiteral("%1-%2").arg(file.pages.front() + 1).arg(file.pages.back() + 1);
            }
            else
            {
                for (const PDFInteger page : file.pages)
                {
                    numbers << QString::number(page + 1);
                }
            }
            parts << numbers.join(QLatin1Char(','));
        }
        return parts.join(QLatin1Char(' '));
    }

    /// The order of the page markers ("PAGE<n>") in the contents of a written document
    static QStringList getPageMarkers(const PDFDocument& document)
    {
        QStringList markers;
        const QByteArray contents = Read::pageContents(document);
        for (qsizetype position = contents.indexOf("% PAGE"); position >= 0; position = contents.indexOf("% PAGE", position + 1))
        {
            qsizetype end = position + 6;
            while (end < contents.size() && QChar::fromLatin1(contents[end]).isDigit())
            {
                ++end;
            }
            markers << QString::fromLatin1(contents.mid(position + 2, end - position - 2));
        }
        return markers;
    }

    static QStringList getKeys(const PDFDictionary* dictionary)
    {
        QStringList keys;
        for (size_t i = 0; dictionary && i < dictionary->getCount(); ++i)
        {
            keys << QString::fromLatin1(dictionary->getKey(i).getString());
        }
        keys.sort();
        return keys;
    }
};

class DocumentToolSplitTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void splitRules_data();
    void splitRules();
    void splitAtBookmarks();
    void splitBySize();
    void splitRuleErrors();
    void splitNames();
    void splitDuplicateNames();
    void splitTooManyFiles();
    void splitRun();
    void splitProgressPhases();
    void splitStopped();
    void splitWithoutSink();
    void splitPermissions();
    void extractResultKind();
    void extractOneFile();
    void extractFilePerPage();
    void extractSameKeys();
    void documentPartsFact_data();
    void documentPartsFact();
    void pdfToolSeparate();
};

void DocumentToolSplitTest::splitRules_data()
{
    QTest::addColumn<QVariantMap>("parameters");
    QTest::addColumn<QString>("files");

    // 383:3315 Every N pages "12 pages per file": Handbook 48 pages -> 4 files
    QTest::newRow("every-n") << QVariantMap({ { "rule", "every-n-pages" }, { "pagesPerFile", 12 } }) << QStringLiteral("1-12 13-24 25-36 37-48");
    QTest::newRow("every-n-rest") << QVariantMap({ { "rule", "every-n-pages" }, { "pagesPerFile", 20 } }) << QStringLiteral("1-20 21-40 41-48");
    // 383:5715 At selected pages "10, 20, 35": 1-9, 10-19, 20-34, 35-48 (plan 5.6)
    QTest::newRow("at-pages") << QVariantMap({ { "rule", "at-pages" }, { "cuts", "35, 10, 20, 20" } }) << QStringLiteral("1-9 10-19 20-34 35-48");
    QTest::newRow("at-pages-first") << QVariantMap({ { "rule", "at-pages" }, { "cuts", "1, 47" } }) << QStringLiteral("1-46 47-48");
    QTest::newRow("odd-even") << QVariantMap({ { "rule", "odd-even" }, { "pages", SplitToolTestHelper::pages(QStringLiteral("1-6")) } }) << QStringLiteral("1,3,5 2,4,6");
    QTest::newRow("every-page") << QVariantMap({ { "rule", "every-page" }, { "pages", SplitToolTestHelper::pages(QStringLiteral("2-4, 9")) } }) << QStringLiteral("2 3 4 9");
    // The pages to split (Page selection, "Pages to split"): only they are in the files
    QTest::newRow("selection") << QVariantMap({ { "rule", "every-n-pages" }, { "pagesPerFile", 4 }, { "pages", SplitToolTestHelper::pages(QStringLiteral("1-10")) } }) << QStringLiteral("1-4 5-8 9-10");
    QTest::newRow("selection-cuts") << QVariantMap({ { "rule", "at-pages" }, { "cuts", "5" }, { "pages", SplitToolTestHelper::pages(QStringLiteral("1-3, 7-9")) } }) << QStringLiteral("1-3 7-9");
}

void DocumentToolSplitTest::splitRules()
{
    QFETCH(QVariantMap, parameters);
    QFETCH(QString, files);

    const PDFDocument document = MergeTestPdf::createPages("", 48).read();
    QString error;
    const std::optional<std::vector<PDFDocumentToolFileOutput::File>> result = PDFDocumentToolSplit::createFiles(&document, parameters, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(SplitToolTestHelper::describe(*result), files);
}

void DocumentToolSplitTest::splitAtBookmarks()
{
    // 383:6771: the files start at the chosen bookmarks, the pages before the first join it,
    // {group_name} is the title of the bookmark
    MergeTestPdf pdf = MergeTestPdf::createPages("", 20, "/Outlines 30 0 R");
    pdf.set(30, "<< /Type /Outlines /First 31 0 R /Last 33 0 R >>");
    pdf.set(31, "<< /Title (Introduction) /Parent 30 0 R /Next 32 0 R /First 34 0 R /Last 34 0 R /Dest [" + MergeTestPdf::page(3) + " /Fit] >>");
    pdf.set(32, "<< /Title (Setup) /Parent 30 0 R /Prev 31 0 R /Next 33 0 R /Dest [" + MergeTestPdf::page(7) + " /Fit] >>");
    pdf.set(33, "<< /Title (Safety) /Parent 30 0 R /Prev 32 0 R /Dest [" + MergeTestPdf::page(15) + " /Fit] >>");
    pdf.set(34, "<< /Title (Details) /Parent 31 0 R /Dest [" + MergeTestPdf::page(5) + " /Fit] >>");
    const PDFDocument document = pdf.read();

    QString error;
    QVariantMap parameters = { { "rule", "at-bookmarks" }, { "bookmarks", "top-level" } };
    std::optional<std::vector<PDFDocumentToolFileOutput::File>> files = PDFDocumentToolSplit::createFiles(&document, parameters, &error);
    QVERIFY2(files, qPrintable(error));
    QCOMPARE(SplitToolTestHelper::describe(*files), QStringLiteral("1-6 7-14 15-20"));
    QCOMPARE((*files)[0].title, QStringLiteral("Introduction"));
    QCOMPARE((*files)[2].title, QStringLiteral("Safety"));

    // The numbers of the bookmarks in the order of the outline: 2 = Details (a level below)
    parameters.insert("bookmarks", "2, 4");
    files = PDFDocumentToolSplit::createFiles(&document, parameters, &error);
    QVERIFY2(files, qPrintable(error));
    QCOMPARE(SplitToolTestHelper::describe(*files), QStringLiteral("1-14 15-20"));
    QCOMPARE((*files)[0].title, QStringLiteral("Details"));

    parameters.insert("fileName", "{group_name}");
    QVERIFY(PDFDocumentToolFileOutput::createFileNames(&*files, QStringLiteral("Handbook.pdf"), parameters, QDate(), &error));
    QCOMPARE((*files)[1].fileName, QStringLiteral("Safety.pdf"));

    parameters.insert("bookmarks", "9");
    QVERIFY(!PDFDocumentToolSplit::createFiles(&document, parameters, &error));
    QCOMPARE(error, QStringLiteral("The bookmarks are \"top-level\" or their numbers from 1 to 4."));

    const PDFDocument withoutOutline = MergeTestPdf::createPages("", 3).read();
    QVERIFY(!PDFDocumentToolSplit::createFiles(&withoutOutline, { { "rule", "at-bookmarks" } }, &error));
    QCOMPARE(error, QStringLiteral("The document has no bookmarks."));
}

void DocumentToolSplitTest::splitBySize()
{
    // 383:7775 By file size: an object shared by more pages of a file counts once - a 200 kB
    // image on every page and a small content: a file of 300 kB takes many pages; without the
    // sharing each page alone would be over the limit
    QByteArray image(200 * 1024, 'x');
    MergeTestPdf pdf = MergeTestPdf::createPages("", 6, QByteArray(),
                                                 { { 1, "/Resources << /XObject << /Im 40 0 R >> >>" }, { 2, "/Resources << /XObject << /Im 40 0 R >> >>" },
                                                   { 3, "/Resources << /XObject << /Im 40 0 R >> >>" }, { 4, "/Resources << /XObject << /Im 41 0 R >> >>" },
                                                   { 5, "/Resources << /XObject << /Im 41 0 R >> >>" }, { 6, "/Resources << /XObject << /Im 41 0 R >> >>" } });
    pdf.set(40, MergeTestPdf::stream(image, "/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray"));
    pdf.set(41, MergeTestPdf::stream(image, "/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray"));
    const PDFDocument document = pdf.read();

    QString error;
    std::optional<std::vector<PDFDocumentToolFileOutput::File>> files = PDFDocumentToolSplit::createFiles(&document, { { "rule", "by-size" }, { "sizeMB", 0.3 } }, &error);
    QVERIFY2(files, qPrintable(error));
    QCOMPARE(SplitToolTestHelper::describe(*files), QStringLiteral("1-3 4-6"));
    QVERIFY((*files)[0].estimatedSize > 200 * 1024 && (*files)[0].estimatedSize < 210 * 1024);

    // A file has at least one page, also when the page is larger than the limit; a file over the
    // limit takes no further page
    files = PDFDocumentToolSplit::createFiles(&document, { { "rule", "by-size" }, { "sizeMB", 0.1 } }, &error);
    QVERIFY2(files, qPrintable(error));
    QCOMPARE(SplitToolTestHelper::describe(*files), QStringLiteral("1 2 3 4 5 6"));
}

void DocumentToolSplitTest::splitRuleErrors()
{
    const PDFDocument document = MergeTestPdf::createPages("", 48).read();
    QString error;
    QVERIFY(!PDFDocumentToolSplit::createFiles(&document, { { "rule", "every-n-pages" }, { "pagesPerFile", 0 } }, &error));
    QCOMPARE(error, QStringLiteral("Enter a number of pages from 1 to 10000."));
    QVERIFY(!PDFDocumentToolSplit::createFiles(&document, { { "rule", "at-pages" }, { "cuts", "" } }, &error));
    QCOMPARE(error, QStringLiteral("Enter the page numbers, where a new file starts."));
    QVERIFY(!PDFDocumentToolSplit::createFiles(&document, { { "rule", "at-pages" }, { "cuts", "60" } }, &error));
    QCOMPARE(error, QStringLiteral("Page 60 does not exist – the document has 48 pages"));
    QVERIFY(!PDFDocumentToolSplit::createFiles(&document, { { "rule", "by-size" }, { "sizeMB", 0.0 } }, &error));
    QVERIFY(!PDFDocumentToolSplit::createFiles(&document, { { "rule", "unknown" } }, &error));
    QVERIFY(!PDFDocumentToolSplit::createFiles(&document, { { "rule", "every-page" }, { "pages", SplitToolTestHelper::pages(QStringLiteral("60")) } }, &error));
}

void DocumentToolSplitTest::splitNames()
{
    // 383:3315: "Handbook_#" -> Handbook_1.pdf ... Handbook_4.pdf; 385:11029: "Handbook_p@"
    const PDFDocument document = MergeTestPdf::createPages("", 48).read();
    QString error;
    std::optional<std::vector<PDFDocumentToolFileOutput::File>> files = PDFDocumentToolSplit::createFiles(&document, { { "rule", "every-n-pages" }, { "pagesPerFile", 12 } }, &error);
    QVERIFY(files);
    QVERIFY(PDFDocumentToolFileOutput::createFileNames(&*files, QStringLiteral("Handbook.pdf"), { { "fileName", "{source_base}_#" } }, QDate(), &error));
    QStringList names;
    for (const PDFDocumentToolFileOutput::File& file : *files)
    {
        names << file.fileName;
    }
    QCOMPARE(names, QStringList({ "Handbook_1.pdf", "Handbook_2.pdf", "Handbook_3.pdf", "Handbook_4.pdf" }));
    QCOMPARE(PDFDocumentToolFileOutput::formatFileNames(names), QStringLiteral("Handbook_1.pdf … Handbook_4.pdf"));

    QVERIFY(PDFDocumentToolFileOutput::createFileNames(&*files, QStringLiteral("Handbook.pdf"), { { "fileName", "Handbook_p@" } }, QDate(), &error));
    QCOMPARE((*files)[1].fileName, QStringLiteral("Handbook_p13.pdf"));
}

void DocumentToolSplitTest::splitDuplicateNames()
{
    // 383:9088: the name without a token - "Every file would be named Handbook.pdf", the run is
    // refused by the validation
    const PDFDocument document = MergeTestPdf::createPages("", 48).read();
    PDFDocumentToolSplit tool;
    SplitToolTestSink sink;
    const PDFDocumentToolResult result = tool.run(SplitToolTestHelper::context(document, &sink),
                                                  { { "rule", "every-n-pages" }, { "pagesPerFile", 12 }, { "fileName", "Handbook" } }, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Failed);
    QCOMPARE(result.message, QStringLiteral("Every file would be named Handbook.pdf"));
    QVERIFY(sink.files.empty());

    // Some equal names only
    std::vector<PDFDocumentToolFileOutput::File> files(3);
    files[0].pages = { 0 };
    files[1].pages = { 1 };
    files[2].pages = { 0 };
    QString error;
    QVERIFY(!PDFDocumentToolFileOutput::createFileNames(&files, QStringLiteral("Handbook.pdf"), { { "fileName", "p@" } }, QDate(), &error));
    QCOMPARE(error, QStringLiteral("1 files would have the same name as another file"));
}

void DocumentToolSplitTest::splitTooManyFiles()
{
    // Plan 6: at most 10 000 files ("more than 10,000 files" is an error)
    std::vector<PDFDocumentToolFileOutput::File> files(10001);
    for (size_t i = 0; i < files.size(); ++i)
    {
        files[i].pages = { PDFInteger(i) };
    }
    QString error;
    QVERIFY(!PDFDocumentToolFileOutput::createFileNames(&files, QStringLiteral("Handbook.pdf"), { { "fileName", "#" } }, QDate(), &error));
    QCOMPARE(error, QStringLiteral("The pages would be split into more than %1 files.").arg(QLocale().toString(10000)));
    files.pop_back();
    QVERIFY(PDFDocumentToolFileOutput::createFileNames(&files, QStringLiteral("Handbook.pdf"), { { "fileName", "#" } }, QDate(), &error));
    QCOMPARE(files.back().fileName, QStringLiteral("10000.pdf"));
}

void DocumentToolSplitTest::splitRun()
{
    // The whole run (383:11374): 4 files given to the sink in their order, each a new document of
    // its pages; the result names the written files
    const PDFDocument document = MergeTestPdf::createPages("", 48).read();
    PDFDocumentToolSplit tool;
    SplitToolTestSink sink;
    const PDFDocumentToolResult result = tool.run(SplitToolTestHelper::context(document, &sink), { { "rule", "every-n-pages" }, { "pagesPerFile", 12 } }, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(sink.begunNames, QStringList({ "Handbook_1.pdf", "Handbook_2.pdf", "Handbook_3.pdf", "Handbook_4.pdf" }));
    QCOMPARE(sink.files.size(), size_t(4));
    QCOMPARE(result.writtenFiles.size(), 4);
    QVERIFY(result.notWrittenFiles.isEmpty());
    QVERIFY(result.documents.empty());

    const PDFDocument third = MergeTestPdf::readBytes(sink.files[2].second);
    QCOMPARE(third.getCatalog()->getPageCount(), size_t(12));
    QCOMPARE(SplitToolTestHelper::getPageMarkers(third).front(), QStringLiteral("PAGE25"));
    QCOMPARE(SplitToolTestHelper::getPageMarkers(third).back(), QStringLiteral("PAGE36"));

    const auto files = std::find_if(result.facts.cbegin(), result.facts.cend(), [](const PDFDocumentToolFact& fact) { return fact.id == QLatin1String("files"); });
    QVERIFY(files != result.facts.cend());
    QCOMPARE(files->value, QStringLiteral("4 · Handbook_1.pdf … Handbook_4.pdf"));
    QCOMPARE(result.data.value("writtenCount").toInteger(), 4);
    QCOMPARE(result.toJson().value("files").toArray().size(), 4);
}

void DocumentToolSplitTest::splitProgressPhases()
{
    // 383:10232 "Handbook_3.pdf · file 3 of 4": a phase of the progress per file, named by the
    // file; the phase of a file begins after the files before it are written, the phases end with
    // the run; no other part of the progress (a reader sees the file being written)
    const PDFDocument document = MergeTestPdf::createPages("", 48).read();
    PDFDocumentToolSplit tool;
    SplitToolTestSink sink;
    SplitToolTestProgress progress(&sink);
    const PDFDocumentToolResult result = tool.run(SplitToolTestHelper::context(document, &sink), { { "rule", "every-n-pages" }, { "pagesPerFile", 12 } }, nullptr, &progress);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(progress.phaseTexts, QStringList({ "Handbook_1.pdf", "Handbook_2.pdf", "Handbook_3.pdf", "Handbook_4.pdf" }));
    const std::vector<std::pair<int, int>> phases = { { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 3 } };
    QCOMPARE(progress.begunPhases, phases);
    QVERIFY(progress.finishCount >= 1);
    QCOMPARE(progress.startCount, 0);

    // Extract one file per page names its files the same way
    SplitToolTestSink pageSink;
    SplitToolTestProgress pageProgress(&pageSink);
    PDFDocumentToolExtract extract;
    const PDFDocumentToolResult extracted = extract.run(SplitToolTestHelper::context(document, &pageSink), { { "pages", SplitToolTestHelper::pages(QStringLiteral("2, 5")) }, { "output", "one-file-per-page" } }, nullptr, &pageProgress);
    QVERIFY2(extracted.isSuccess(), qPrintable(extracted.message));
    QCOMPARE(pageProgress.phaseTexts, QStringList({ "Handbook_p2.pdf", "Handbook_p5.pdf" }));
}

void DocumentToolSplitTest::splitStopped()
{
    // 403:29978: Stop after 2 of 4 files - the 2 written are named, the 2 others not written
    const PDFDocument document = MergeTestPdf::createPages("", 48).read();
    PDFDocumentToolSplit tool;
    SplitToolTestSink sink(2);
    const PDFDocumentToolResult result = tool.run(SplitToolTestHelper::context(document, &sink), { { "rule", "every-n-pages" }, { "pagesPerFile", 12 } }, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Stopped);
    QCOMPARE(result.writtenFiles, QStringList({ "/memory/Handbook_1.pdf", "/memory/Handbook_2.pdf" }));
    QCOMPARE(result.notWrittenFiles, QStringList({ "Handbook_3.pdf", "Handbook_4.pdf" }));
    QCOMPARE(result.toJson().value("notWrittenFiles").toArray().size(), 2);

    // A run canceled by its control keeps the names of the files written before it (R3)
    SplitToolTestCanceledControl control;
    SplitToolTestSink canceledSink;
    const PDFDocumentToolResult canceled = tool.run(SplitToolTestHelper::context(document, &canceledSink), { { "rule", "every-page" } }, &control, nullptr);
    QCOMPARE(canceled.status, PDFDocumentToolResultStatus::Stopped);
    QVERIFY(canceledSink.files.empty());
}

void DocumentToolSplitTest::splitWithoutSink()
{
    const PDFDocument document = MergeTestPdf::createPages("", 4).read();
    PDFDocumentToolSplit tool;
    const PDFDocumentToolResult result = tool.run(SplitToolTestHelper::context(document), { { "rule", "every-page" } }, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Failed);
    QCOMPARE(result.message, QStringLiteral("The files have no place to be written."));
}

void DocumentToolSplitTest::splitPermissions()
{
    // Owner answer Q4 (the rule of Merge, Q2 of step 2): the files are not encrypted - a document,
    // which restricts printing, needs its owner password; Assemble denied is refused too
    using Permission = PDFSecurityHandler::Permission;
    const PDFDocument plain = MergeTestPdf::createPages("", 4).read();
    const uint32_t noPrint = uint32_t(Permission::Assemble) | uint32_t(Permission::CopyContent) | uint32_t(Permission::Modify) |
                             uint32_t(Permission::ModifyInteractiveItems) | uint32_t(Permission::ModifyFormFields);
    const PDFDocument restricted = MergeTestPdf::readBytes(MergeTestPdf::encrypt(plain, QString(), QStringLiteral("owner"), noPrint));
    QCOMPARE(PDFDocumentMerger::getPermission(&restricted), PDFDocumentMerger::Permission::OwnerPasswordRequired);

    PDFDocumentToolSplit split;
    PDFDocumentToolExtract extract;
    SplitToolTestSink sink;
    const PDFDocumentToolResult splitResult = split.run(SplitToolTestHelper::context(restricted, &sink), { { "rule", "every-page" } }, nullptr, nullptr);
    QCOMPARE(splitResult.status, PDFDocumentToolResultStatus::Failed);
    QCOMPARE(splitResult.message, PDFDocumentMerger::getPermissionText(PDFDocumentMerger::Permission::OwnerPasswordRequired, QStringLiteral("Handbook.pdf")));
    QVERIFY(sink.files.empty());
    const PDFDocumentToolResult extractResult = extract.run(SplitToolTestHelper::context(restricted), { { "pages", SplitToolTestHelper::pages(QStringLiteral("1")) } }, nullptr, nullptr);
    QCOMPARE(extractResult.status, PDFDocumentToolResultStatus::Failed);

    // The owner password verified against the original file (Q15 of step 2) permits it
    PDFDocumentToolContext verified = SplitToolTestHelper::context(restricted, &sink);
    verified.documents.front().isOwnerPasswordVerified = true;
    QVERIFY(split.run(verified, { { "rule", "every-page" } }, nullptr, nullptr).isSuccess());
    QCOMPARE(sink.files.size(), size_t(4));

    // The catalog rows (skeleton): Split and Extract need Assemble and are new documents of the inputs
    for (const char* id : { "pages.split", "pages.extract" })
    {
        const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(QString::fromLatin1(id));
        QVERIFY(entry && entry->isNewDocumentOfInputs());
    }
}

void DocumentToolSplitTest::extractResultKind()
{
    // 4.2 of the plan: one file = a new document (R2), one file per page = files (R3)
    PDFDocumentToolExtract extract;
    QCOMPARE(extract.getResultKind({ { "output", "one-file" } }), PDFDocumentToolResultKind::NewDocument);
    QCOMPARE(extract.getResultKind({ { "output", "one-file-per-page" } }), PDFDocumentToolResultKind::Files);
    const std::vector<PDFDocumentToolResultKind> kinds = extract.getPossibleResultKinds();
    QVERIFY(std::find(kinds.cbegin(), kinds.cend(), PDFDocumentToolResultKind::NewDocument) != kinds.cend());
    QVERIFY(std::find(kinds.cbegin(), kinds.cend(), PDFDocumentToolResultKind::Files) != kinds.cend());
    PDFDocumentToolSplit split;
    QCOMPARE(split.getResultKind({}), PDFDocumentToolResultKind::Files);
}

void DocumentToolSplitTest::extractOneFile()
{
    // 385:9405, K49: "8, 1-3" gives the pages in the order of the document; the source stays
    const PDFDocument document = MergeTestPdf::createPages("", 12).read();
    PDFDocumentToolExtract extract;
    const PDFDocumentToolResult result = extract.run(SplitToolTestHelper::context(document), { { "pages", SplitToolTestHelper::pages(QStringLiteral("8, 1-3")) } }, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.documents.size(), size_t(1));
    QCOMPARE(result.documents.front().kind, PDFDocumentToolOutputDocument::Kind::New);
    QCOMPARE(result.documents.front().suggestedFileName, QStringLiteral("Handbook_extracted.pdf"));
    QVERIFY(!result.documents.front().writtenData.isEmpty());
    const PDFDocument written = MergeTestPdf::readBytes(result.documents.front().writtenData);
    QCOMPARE(SplitToolTestHelper::getPageMarkers(written), QStringList({ "PAGE1", "PAGE2", "PAGE3", "PAGE8" }));

    // 385:12395: Pages "4 · 1–3, 8", Source "Handbook.pdf · not changed"
    QCOMPARE(result.facts[0].value, QStringLiteral("4 · 1–3, 8"));
    QCOMPARE(result.facts[1].value, QStringLiteral("Handbook.pdf · not changed"));

    // Owner D3 (2026-11-10): a run of two pages is a range too ("13–14" of 388:17167)
    const PDFDocumentToolResult two = extract.run(SplitToolTestHelper::context(document), { { "pages", SplitToolTestHelper::pages(QStringLiteral("5-6, 9")) } }, nullptr, nullptr);
    QVERIFY2(two.isSuccess(), qPrintable(two.message));
    QCOMPARE(two.facts[0].value, QStringLiteral("3 · 5–6, 9"));

    // 385:11855: a page outside of the document is refused
    const PDFDocumentToolResult invalid = extract.run(SplitToolTestHelper::context(document), { { "pages", SplitToolTestHelper::pages(QStringLiteral("1-3, 60")) } }, nullptr, nullptr);
    QCOMPARE(invalid.status, PDFDocumentToolResultStatus::Failed);
}

void DocumentToolSplitTest::extractFilePerPage()
{
    // 385:11029: "Handbook_p@" - a file per selected page
    const PDFDocument document = MergeTestPdf::createPages("", 12).read();
    PDFDocumentToolExtract extract;
    SplitToolTestSink sink;
    const PDFDocumentToolResult result = extract.run(SplitToolTestHelper::context(document, &sink),
                                                     { { "pages", SplitToolTestHelper::pages(QStringLiteral("1-3, 8")) }, { "output", "one-file-per-page" } }, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QVERIFY(result.documents.empty());
    QCOMPARE(sink.begunNames, QStringList({ "Handbook_p1.pdf", "Handbook_p2.pdf", "Handbook_p3.pdf", "Handbook_p8.pdf" }));
    const PDFDocument fourth = MergeTestPdf::readBytes(sink.files[3].second);
    QCOMPARE(SplitToolTestHelper::getPageMarkers(fourth), QStringList({ "PAGE8" }));
}

void DocumentToolSplitTest::documentPartsFact_data()
{
    QTest::addColumn<bool>("isDamaged");
    QTest::newRow("intact") << false;
    QTest::newRow("cycle") << true;
}

void DocumentToolSplitTest::documentPartsFact()
{
    // ISO 32000-2, 14.12, Table 408 / 409: the parts of 6 pages, 1-3 and 4-6 below one node. A
    // file keeps the parts of its pages; a hierarchy, which is no tree (here a cycle: the
    // node 63 has its own parent as a child), is removed whole and the result says it with the
    // fact of Remove Pages, "Document parts removed" (fix wave 3b, F4 item 3)
    QFETCH(bool, isDamaged);
    MergeTestPdf pdf = MergeTestPdf::createPages("", 6, "/DPartRoot 60 0 R",
                                                 { { 1, "/DPart 62 0 R" }, { 2, "/DPart 62 0 R" }, { 3, "/DPart 62 0 R" },
                                                   { 4, "/DPart 63 0 R" }, { 5, "/DPart 63 0 R" }, { 6, "/DPart 63 0 R" } });
    pdf.set(60, "<< /Type /DPartRoot /DPartRootNode 61 0 R >>");
    pdf.set(61, "<< /Type /DPart /Parent 60 0 R /DParts [[62 0 R 63 0 R]] >>");
    pdf.set(62, "<< /Type /DPart /Parent 61 0 R /Start 3 0 R /End 5 0 R >>");
    pdf.set(63, isDamaged ? QByteArray("<< /Type /DPart /Parent 61 0 R /DParts [[61 0 R]] >>") : QByteArray("<< /Type /DPart /Parent 61 0 R /Start 6 0 R /End 8 0 R >>"));
    const PDFDocument document = pdf.read();
    QVERIFY(Read::catalog(document)->hasKey("DPartRoot"));

    struct Check
    {
        static QString findFact(const PDFDocumentToolResult& result)
        {
            QString value;
            for (const PDFDocumentToolFact& fact : result.facts)
            {
                value = fact.id == QLatin1String("documentParts") ? fact.value : value;
            }
            return value;
        }
    };
    const QString expectedFact = isDamaged ? QStringLiteral("Document parts removed") : QString();

    // Split: every file of 3 pages
    PDFDocumentToolSplit split;
    SplitToolTestSink sink;
    const PDFDocumentToolResult splitResult = split.run(SplitToolTestHelper::context(document, &sink), { { "rule", "every-n-pages" }, { "pagesPerFile", 3 } }, nullptr, nullptr);
    QVERIFY2(splitResult.isSuccess(), qPrintable(splitResult.message));
    QCOMPARE(sink.files.size(), size_t(2));
    QCOMPARE(Check::findFact(splitResult), expectedFact);
    QCOMPARE(splitResult.data.value("documentPartsRemoved").toBool(), isDamaged);
    for (const auto& [fileName, data] : sink.files)
    {
        // An intact tree keeps the part of the pages of the file, a damaged one is gone
        const PDFDocument written = MergeTestPdf::readBytes(data);
        QVERIFY2(Read::catalog(written)->hasKey("DPartRoot") != isDamaged, qPrintable(fileName));
    }

    // The result of MCP has the same facts (the rows of a result are its facts)
    bool hasJsonFact = false;
    for (const QJsonValue& fact : splitResult.toJson().value("facts").toArray())
    {
        hasJsonFact = hasJsonFact || fact.toObject().value("id").toString() == QLatin1String("documentParts");
    }
    QCOMPARE(hasJsonFact, isDamaged);

    // Extract one file (the merger of one source) and one file per page (the splitter)
    PDFDocumentToolExtract extract;
    const PDFDocumentToolResult one = extract.run(SplitToolTestHelper::context(document), { { "pages", SplitToolTestHelper::pages(QStringLiteral("1-2")) } }, nullptr, nullptr);
    QVERIFY2(one.isSuccess(), qPrintable(one.message));
    QCOMPARE(Check::findFact(one), expectedFact);
    QCOMPARE(one.data.value("documentPartsRemoved").toBool(), isDamaged);
    const PDFDocument oneWritten = MergeTestPdf::readBytes(one.documents.front().writtenData);
    QVERIFY(Read::catalog(oneWritten)->hasKey("DPartRoot") != isDamaged);

    SplitToolTestSink pageSink;
    const PDFDocumentToolResult perPage = extract.run(SplitToolTestHelper::context(document, &pageSink), { { "pages", SplitToolTestHelper::pages(QStringLiteral("2, 5")) }, { "output", "one-file-per-page" } }, nullptr, nullptr);
    QVERIFY2(perPage.isSuccess(), qPrintable(perPage.message));
    QCOMPARE(Check::findFact(perPage), expectedFact);
}

void DocumentToolSplitTest::extractSameKeys()
{
    // R2: Extract one file and one file per page have the same keys of the catalog and of the
    // trailer - one finishing of a new document
    const PDFDocument document = MergeTestPdf::createPages("", 3, "/PageLabels << /Nums [0 << /S /D >>] >> /OpenAction [3 0 R /Fit]").read();
    PDFDocumentToolExtract extract;
    const PDFDocumentToolResult one = extract.run(SplitToolTestHelper::context(document), { { "pages", SplitToolTestHelper::pages(QStringLiteral("2")) } }, nullptr, nullptr);
    SplitToolTestSink sink;
    const PDFDocumentToolResult perPage = extract.run(SplitToolTestHelper::context(document, &sink), { { "pages", SplitToolTestHelper::pages(QStringLiteral("2")) }, { "output", "one-file-per-page" } }, nullptr, nullptr);
    QVERIFY(one.isSuccess() && perPage.isSuccess());

    const PDFDocument a = MergeTestPdf::readBytes(one.documents.front().writtenData);
    const PDFDocument b = MergeTestPdf::readBytes(sink.files.front().second);
    QCOMPARE(SplitToolTestHelper::getKeys(Read::catalog(a)), SplitToolTestHelper::getKeys(Read::catalog(b)));
    QCOMPARE(SplitToolTestHelper::getKeys(Read::trailer(a)), SplitToolTestHelper::getKeys(Read::trailer(b)));
    QVERIFY(!Read::catalog(a)->hasKey("OpenAction"));
    QVERIFY(Read::trailer(a)->hasKey("ID") && !Read::trailer(a)->hasKey("Encrypt"));
}

void DocumentToolSplitTest::pdfToolSeparate()
{
    // Q16: PdfTool separate on the new core - '%' stays the page number, a page outside of the
    // document is an error (B36), no page of another file reaches a file (B37), the permission
    // is the rule of Merge and a page, which was not written, is an error (B38)
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
    auto readFile = [&directory](const QString& name)
    {
        QFile file(directory.filePath(name));
        return file.open(QFile::ReadOnly) ? file.readAll() : QByteArray();
    };
    auto separate = [](const QStringList& arguments)
    {
        QProcess process;
        process.setProgram(QStringLiteral(PDF4QT_TEST_PDFTOOL));
        process.setArguments(QStringList({ QStringLiteral("separate") }) + arguments);
        process.start();
        process.waitForFinished(60000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR")));
        return process.exitCode();
    };

    // Page 1 links to page 2 and a field has widgets on pages 1 and 2
    MergeTestPdf pdf = MergeTestPdf::createPages("", 3, "/AcroForm << /Fields [40 0 R] >>", { { 1, "/Annots [41 0 R 43 0 R]" }, { 2, "/Annots [42 0 R]" } });
    pdf.set(40, "<< /FT /Tx /T (shared) /Kids [41 0 R 42 0 R] >>");
    pdf.set(41, "<< /Type /Annot /Subtype /Widget /Parent 40 0 R /Rect [0 0 10 10] /P " + MergeTestPdf::page(1) + " >>");
    pdf.set(42, "<< /Type /Annot /Subtype /Widget /Parent 40 0 R /Rect [0 0 10 10] /P " + MergeTestPdf::page(2) + " /Contents (WIDGETOFPAGE2) >>");
    pdf.set(43, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest [" + MergeTestPdf::page(2) + " /Fit] >>");
    const QString source = writeFile(QStringLiteral("Handbook.pdf"), pdf.write());

    QCOMPARE(separate({ source, directory.filePath(QStringLiteral("page_%.pdf")) }), 0);
    for (int page = 1; page <= 3; ++page)
    {
        const QByteArray data = readFile(QStringLiteral("page_%1.pdf").arg(page));
        QVERIFY(!data.isEmpty());
        const PDFDocument written = MergeTestPdf::readBytes(data);
        QCOMPARE(SplitToolTestHelper::getPageMarkers(written), QStringList({ QStringLiteral("PAGE%1").arg(page) }));
        QCOMPARE(data.contains("WIDGETOFPAGE2"), page == 2);
    }
    const PDFDocument first = MergeTestPdf::readBytes(readFile(QStringLiteral("page_1.pdf")));
    QCOMPARE(first.getCatalog()->getPage(0)->getAnnotations().size(), size_t(1));

    // B36: page 50 of 3 and page 0 are errors, nothing is written
    QVERIFY(separate({ QStringLiteral("--page-select"), QStringLiteral("50"), source, directory.filePath(QStringLiteral("p50_%.pdf")) }) != 0);
    QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("p50_50.pdf"))));

    // B38: an existing file is not overwritten and the command fails
    QVERIFY(separate({ source, directory.filePath(QStringLiteral("page_%.pdf")) }) != 0);

    // B38, Q4: a document, which restricts printing, needs its owner password
    using Permission = PDFSecurityHandler::Permission;
    const uint32_t noPrint = uint32_t(Permission::Assemble) | uint32_t(Permission::CopyContent) | uint32_t(Permission::Modify) |
                             uint32_t(Permission::ModifyInteractiveItems) | uint32_t(Permission::ModifyFormFields);
    const QString restricted = writeFile(QStringLiteral("restricted.pdf"), MergeTestPdf::encrypt(pdf.read(), QString(), QStringLiteral("owner"), noPrint));
    QVERIFY(separate({ restricted, directory.filePath(QStringLiteral("r_%.pdf")) }) != 0);
    QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("r_1.pdf"))));
    QCOMPARE(separate({ QStringLiteral("--pswd"), QStringLiteral("owner"), restricted, directory.filePath(QStringLiteral("r_%.pdf")) }), 0);
    QVERIFY(QFile::exists(directory.filePath(QStringLiteral("r_3.pdf"))));
#endif
}

QTEST_GUILESS_MAIN(DocumentToolSplitTest)

#include "tst_documenttoolsplittest.moc"
