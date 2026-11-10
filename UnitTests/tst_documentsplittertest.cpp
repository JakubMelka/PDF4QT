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

// The splitter of the core (package A of step 3a of phase 2, plan 5.6, 6, 8, R2, K23, K25, Q6):
// the parts (PDFDocumentSplitter over PDFDocumentPageOperations::keepPages), the finishing of a
// new document (PDFNewDocumentFinisher), the template of the names (PDFFileNameTemplate, B25,
// B26), PDFPageRangeList, PDFOutlineBreakPoints, the folder sink and 10 000 pages. The documents
// are written as PDF text by the test, so every object, which points at a page, is known; the
// expected values come from ISO 32000-2 (the outline 12.3.3, destinations 12.3.2, page labels
// 12.4.2, fields 12.7, attachments 7.11.4, the identifier 14.4, the cross-reference table 7.5.4)
// and from the plan.

#include "pdfcatalog.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumentpageoperations.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentsplitter.h"
#include "pdfdocumenttool.h"
#include "pdffilenametemplate.h"
#include "pdfform.h"
#include "pdfnewdocumentfinisher.h"
#include "pdfobjectutils.h"
#include "pdfoperationcontrol.h"
#include "pdfoutlinebreakpoints.h"
#include "pdfpagerangelist.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <map>

using namespace pdf;

/// Writes PDF text of numbered objects (a classic cross-reference table) and reads it
class SplitterTestPdf
{
public:
    void set(int number, const QByteArray& text) { m_objects[number] = text; }

    static QByteArray stream(const QByteArray& content)
    {
        return "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "\nendstream";
    }

    QByteArray write() const
    {
        QByteArray data = "%PDF-1.7\n";
        const int size = m_objects.empty() ? 1 : m_objects.rbegin()->first + 1;
        std::vector<qint64> offsets(size_t(size), -1);
        for (const auto& [number, text] : m_objects)
        {
            offsets[size_t(number)] = data.size();
            data += QByteArray::number(number) + " 0 obj\n" + text + "\nendobj\n";
        }
        const qint64 xref = data.size();
        data += "xref\n0 " + QByteArray::number(size) + "\n0000000000 65535 f \n";
        for (int i = 1; i < size; ++i)
        {
            data += offsets[size_t(i)] < 0 ? QByteArray("0000000000 65535 f \n") : QByteArray::number(offsets[size_t(i)]).rightJustified(10, '0') + " 00000 n \n";
        }
        data += "trailer\n<< /Size " + QByteArray::number(size) + " /Root 1 0 R /Info 2 0 R /ID [<00112233445566778899AABBCCDDEEFF> <00112233445566778899AABBCCDDEEFF>] >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
        return data;
    }

    PDFDocument read() const
    {
        PDFDocumentReader reader(nullptr, nullptr, false, false);
        PDFDocument document = reader.readFromBuffer(write());
        if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
        {
            qWarning() << "The test document cannot be read:" << reader.getErrorMessage();
        }
        return document;
    }

    /// The object number of the page n (from 1) and of its content
    static int page(int n) { return 100 + 2 * (n - 1); }
    static int content(int n) { return 101 + 2 * (n - 1); }

    /// \p count pages below the root of the page tree 3 (it gives the media box and the
    /// resources - inherited, Table 31); the catalog 1 with \p catalogEntries, /Info 2. Page n is
    /// object page(n), its content content(n) has the marker PAGEMARKER<n>; \p pageEntries are
    /// extra entries of the pages.
    static SplitterTestPdf createPages(int count, const QByteArray& catalogEntries = QByteArray(), const std::map<int, QByteArray>& pageEntries = {})
    {
        SplitterTestPdf pdf;
        pdf.set(1, "<< /Type /Catalog /Pages 3 0 R " + catalogEntries + " >>");
        pdf.set(2, "<< /Producer (Test) /CreationDate (D:20200101000000Z) >>");
        QByteArray kids;
        for (int n = 1; n <= count; ++n)
        {
            kids += QByteArray::number(page(n)) + " 0 R ";
            const auto it = pageEntries.find(n);
            pdf.set(page(n), "<< /Type /Page /Parent 3 0 R /Contents " + QByteArray::number(content(n)) + " 0 R " + (it != pageEntries.end() ? it->second : QByteArray()) + " >>");
            pdf.set(content(n), stream("% PAGEMARKER" + QByteArray::number(n) + "X\n0 0 m 10 10 l S"));
        }
        pdf.set(3, "<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(count) + " /MediaBox [0 0 600 800] /Resources << /Font << /F1 4 0 R >> >> >>");
        pdf.set(4, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
        return pdf;
    }

private:
    std::map<int, QByteArray> m_objects;
};

/// A file sink of the memory: the files by their names
class SplitterTestMemorySink : public PDFDocumentToolFileSink
{
public:
    explicit SplitterTestMemorySink(int stopAfter = -1) : PDFDocumentToolFileSink(ExistingFiles::Overwrite), m_stopAfter(stopAfter) { }

    virtual bool write(const QString& fileName, const QByteArray& data, QString* errorText) override
    {
        Q_UNUSED(errorText);
        files.emplace_back(fileName, data);
        addWrittenFile(QStringLiteral("/memory/") + fileName);
        return true;
    }

    virtual bool isStopped() const override { return m_stopAfter >= 0 && int(files.size()) >= m_stopAfter; }

    std::vector<std::pair<QString, QByteArray>> files;

private:
    int m_stopAfter;
};

/// Helpers of the test
class SplitterTestHelper
{
public:
    SplitterTestHelper() = delete;

    static PDFDocument readBack(const QByteArray& data)
    {
        PDFDocumentReader reader(nullptr, nullptr, false, false);
        return reader.readFromBuffer(data);
    }

    static const PDFDictionary* getCatalog(const PDFDocument& document)
    {
        return document.getDictionaryFromObject(document.getTrailerDictionary()->get("Root"));
    }

    /// The parts of a split (indices of the pages from 1)
    static std::vector<PDFDocumentSplitter::PartResult> split(const PDFDocument& document, const std::vector<std::vector<PDFInteger>>& pageNumbers,
                                                              PDFDocumentSplitter::Result* splitResult = nullptr)
    {
        std::vector<PDFDocumentSplitter::Part> parts;
        for (const std::vector<PDFInteger>& numbers : pageNumbers)
        {
            PDFDocumentSplitter::Part part;
            for (const PDFInteger number : numbers)
            {
                part.pages.push_back(number - 1);
            }
            parts.push_back(std::move(part));
        }

        std::vector<PDFDocumentSplitter::PartResult> results;
        const PDFDocumentSplitter::Result result = PDFDocumentSplitter::split(&document, parts, PDFDocumentSplitter::Settings(), nullptr, nullptr,
                                                                              [&](PDFDocumentSplitter::PartResult part) { results.push_back(std::move(part)); return true; });
        if (splitResult)
        {
            *splitResult = result;
        }
        return results;
    }

    /// Every reference reachable from the trailer leads to an object
    static QStringList getDanglingReferences(const PDFDocument& document)
    {
        QStringList dangling;
        const std::set<PDFObjectReference> references = PDFObjectUtils::getReferences({ document.getStorage().getTrailerDictionary() }, document.getStorage());
        for (const PDFObjectReference& reference : references)
        {
            if (document.getObjectByReference(reference).isNull())
            {
                dangling << QStringLiteral("%1 %2 R").arg(reference.objectNumber).arg(reference.generation);
            }
        }
        return dangling;
    }

    /// Entries of the cross-reference table of a written file: the first line "0 n" of the
    /// subsection of a classic table (7.5.4), the /Size of a stream
    static qint64 getCrossReferenceSize(const PDFDocument& reread)
    {
        return qint64(reread.getStorage().getObjects().size());
    }

    /// Number of the objects reachable from the trailer
    static qint64 getReachableCount(const PDFDocument& document)
    {
        return qint64(PDFObjectUtils::getReferences({ document.getStorage().getTrailerDictionary() }, document.getStorage()).size());
    }

    /// Multiplier of the time limits (PDF4QT_TEST_TIMEOUT_FACTOR)
    static int getTimeoutFactor()
    {
        bool ok = false;
        const int factor = qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR", &ok);
        return ok && factor > 0 ? factor : 1;
    }
};

/// A control, which is canceled from the beginning
class SplitterTestCanceledControl : public PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return true; }
};

/// A folder sink with protected paths and a stop after some files
class SplitterTestFolderSink : public PDFDocumentToolFolderSink
{
public:
    SplitterTestFolderSink(QString folder, ExistingFiles policy, QStringList protectedPaths = QStringList()) :
        PDFDocumentToolFolderSink(std::move(folder), policy),
        m_protectedPaths(std::move(protectedPaths))
    {

    }

    int stopAfter = -1;

    virtual bool isStopped() const override { return stopAfter >= 0 && getWrittenFiles().size() >= stopAfter; }

protected:
    virtual bool isProtectedPath(const QString& filePath) const override
    {
        return m_protectedPaths.contains(QDir::cleanPath(filePath));
    }

private:
    QStringList m_protectedPaths;
};

class DocumentSplitterTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void templateTokens();
    void templateEveryOccurrence();
    void templateGroupIndex();
    void templatePadding();
    void templateSanitize_data();
    void templateSanitize();
    void templateValidity();
    void templateDuplicates();

    void rangeListOrder();
    void rangeListErrors_data();
    void rangeListErrors();
    void rangeListText();

    void bookmarksRead();
    void bookmarksHostileOutline();
    void bookmarksGroups();

    void splitLeavesNoMarker();
    void splitFieldsInEveryPart();
    void splitOutlineAndLabels();
    void splitAttachmentsFirstOnly();
    void splitNewDocumentKeys();
    void splitPartDates();
    void splitCrossReferenceOfPart();
    void splitStopAndCancel();
    void splitInvalidParts();
    void splitTenThousandPages();
    void finisherKeys();

    void folderSinkPolicies();
    void folderSinkRenameAvoidsRun();
    void folderSinkProtectedAndStop();
    void folderSinkBegin();
};

void DocumentSplitterTest::templateTokens()
{
    // K25: every token of Figma, PageMaster and the plan
    PDFFileNameTemplate::Values values;
    values.outputIndex = 3;
    values.outputCount = 4;
    values.pageNumber = 25;
    values.sourceIndex = 2;
    values.groupIndex = 7;
    values.groupName = QStringLiteral("Introduction");
    values.sourceFileName = QStringLiteral("Handbook.final.pdf");
    values.date = QDate(2026, 11, 9);

    const PDFFileNameTemplate nameTemplate(QStringLiteral("{source_base}|{source_name}|{source_ext}|{source_page}|{output_index}|{group_index}|{group_name}|{date}|#|@|%"), QStringLiteral(".pdf"));
    // '|' is forbidden in a file name: it becomes '_' after the expansion
    QCOMPARE(nameTemplate.createFileName(values), QStringLiteral("Handbook.final_Handbook.final.pdf_pdf_25_3_7_Introduction_2026-11-09_3_25_2.pdf"));

    // The extension is a parameter (R14)
    const PDFFileNameTemplate pngTemplate(QStringLiteral("Report_#"), QStringLiteral(".png"));
    QCOMPARE(pngTemplate.createFileName(values), QStringLiteral("Report_3.png"));

    // An unknown token in braces stays as the text
    const PDFFileNameTemplate unknown(QStringLiteral("{unknown}_#"), QStringLiteral(".pdf"));
    QCOMPARE(unknown.createFileName(values), QStringLiteral("{unknown}_3.pdf"));
}

void DocumentSplitterTest::templateEveryOccurrence()
{
    // B25: the old PageMaster replaced the text from the first to the last '#' ("part-#-of-#" ->
    // "part-1"); every occurrence is a token, the text between stays
    PDFFileNameTemplate::Values values;
    values.outputIndex = 1;
    values.outputCount = 4;
    values.pageNumber = 13;
    const PDFFileNameTemplate nameTemplate(QStringLiteral("part-#-of-#"), QStringLiteral(".pdf"));
    QCOMPARE(nameTemplate.createFileName(values), QStringLiteral("part-1-of-1.pdf"));

    const PDFFileNameTemplate pages(QStringLiteral("p@ to @"), QStringLiteral(".pdf"));
    QCOMPARE(pages.createFileName(values), QStringLiteral("p13 to 13.pdf"));
}

void DocumentSplitterTest::templateGroupIndex()
{
    // B26: {group_index} is the number of the group, not of the output
    PDFFileNameTemplate::Values values;
    values.outputIndex = 5;
    values.outputCount = 9;
    values.groupIndex = 2;
    const PDFFileNameTemplate nameTemplate(QStringLiteral("{group_index}-{output_index}"), QStringLiteral(".pdf"));
    QCOMPARE(nameTemplate.createFileName(values), QStringLiteral("2-5.pdf"));
}

void DocumentSplitterTest::templatePadding()
{
    // 383:4447 "## pads with zeros": a run of '#' is padded to its width or to the width of the
    // number of the outputs, whichever is wider; one '#' is the plain number
    PDFFileNameTemplate::Values values;
    values.outputIndex = 7;
    values.outputCount = 120;
    QCOMPARE(PDFFileNameTemplate(QStringLiteral("a#"), QStringLiteral(".pdf")).createFileName(values), QStringLiteral("a7.pdf"));
    QCOMPARE(PDFFileNameTemplate(QStringLiteral("a##"), QStringLiteral(".pdf")).createFileName(values), QStringLiteral("a007.pdf"));
    QCOMPARE(PDFFileNameTemplate(QStringLiteral("a####"), QStringLiteral(".pdf")).createFileName(values), QStringLiteral("a0007.pdf"));
    values.outputCount = 4;
    QCOMPARE(PDFFileNameTemplate(QStringLiteral("a##"), QStringLiteral(".pdf")).createFileName(values), QStringLiteral("a07.pdf"));

    // '@' and '%' are padded to the width of their run (PageMaster)
    values.pageNumber = 5;
    values.sourceIndex = 3;
    QCOMPARE(PDFFileNameTemplate(QStringLiteral("@@@_%%"), QStringLiteral(".pdf")).createFileName(values), QStringLiteral("005_03.pdf"));
}

void DocumentSplitterTest::templateSanitize_data()
{
    QTest::addColumn<QString>("groupName");
    QTest::addColumn<QString>("expected");

    // The title of a bookmark is hostile input (plan 6): never a folder, a device, ".."
    QTest::newRow("parent") << QStringLiteral("..\\x") << QStringLiteral("._x.pdf");
    QTest::newRow("device") << QStringLiteral("CON") << QStringLiteral("_CON.pdf");
    QTest::newRow("device-with-extension") << QStringLiteral("com1.txt") << QStringLiteral("_com1.txt.pdf");
    QTest::newRow("folder") << QStringLiteral("a/b") << QStringLiteral("a_b.pdf");
    QTest::newRow("stream") << QStringLiteral("a:b") << QStringLiteral("a_b.pdf");
    QTest::newRow("bidi") << QStringLiteral("abc") + QChar(0x202E) + QStringLiteral("fdp.exe") << QStringLiteral("abcfdp.exe.pdf");
    QTest::newRow("control") << QStringLiteral("a\tb\nc") << QStringLiteral("abc.pdf");
    QTest::newRow("trailing") << QStringLiteral("name. . ") << QStringLiteral("name.pdf");
    QTest::newRow("empty") << QString() << QStringLiteral("_.pdf");
    QTest::newRow("long") << QString(300, QLatin1Char('x')) << QString(251, QLatin1Char('x')) + QStringLiteral(".pdf");
}

void DocumentSplitterTest::templateSanitize()
{
    QFETCH(QString, groupName);
    QFETCH(QString, expected);

    PDFFileNameTemplate::Values values;
    values.outputIndex = 1;
    values.outputCount = 1;
    values.groupName = groupName;
    const QString fileName = PDFFileNameTemplate(QStringLiteral("{group_name}"), QStringLiteral(".pdf")).createFileName(values);
    QCOMPARE(fileName, expected);
    QVERIFY(fileName.size() <= PDFFileNameTemplate::MAXIMUM_FILE_NAME_LENGTH);
    QCOMPARE(PDFFileNameTemplate::sanitize(fileName), fileName);
}

void DocumentSplitterTest::templateValidity()
{
    QString error;
    QVERIFY(PDFFileNameTemplate(QStringLiteral("Handbook_#"), QStringLiteral(".pdf")).isValid(&error));
    QVERIFY(error.isEmpty());

    for (const QString& pattern : { QString(), QStringLiteral("   "), QStringLiteral("a/b_#"), QStringLiteral("a\\b"), QStringLiteral("a:b"), QStringLiteral("a?b"), QStringLiteral("a") + QChar(0x202E) + QStringLiteral("b") })
    {
        QVERIFY2(!PDFFileNameTemplate(pattern, QStringLiteral(".pdf")).isValid(&error), qPrintable(pattern));
        QVERIFY(!error.isEmpty());
    }
    QVERIFY(!PDFFileNameTemplate(QString(260, QLatin1Char('a')), QStringLiteral(".pdf")).isValid(&error));
}

void DocumentSplitterTest::templateDuplicates()
{
    // Collisions are case insensitive (Windows, macOS): the later names are the duplicates
    const QStringList names = { QStringLiteral("Report.pdf"), QStringLiteral("a.pdf"), QStringLiteral("REPORT.PDF"), QStringLiteral("b.pdf"), QStringLiteral("report.pdf") };
    QCOMPARE(PDFFileNameTemplate::findDuplicates(names), std::vector<int>({ 2, 4 }));
    QVERIFY(PDFFileNameTemplate::findDuplicates({ QStringLiteral("a.pdf"), QStringLiteral("b.pdf") }).empty());
}

void DocumentSplitterTest::rangeListOrder()
{
    // K23 with R13: the order of the text, descending ranges, further occurrences, odd and even
    QString error;
    std::optional<PDFPageRangeList> list = PDFPageRangeList::parse(QStringLiteral("8, 1-3"), 48, &error);
    QVERIFY(list);
    QCOMPARE(list->getPageIndices(), std::vector<PDFInteger>({ 7, 0, 1, 2 }));

    list = PDFPageRangeList::parse(QStringLiteral("5-3, 3"), 48, &error);
    QCOMPARE(list->getPageIndices(), std::vector<PDFInteger>({ 4, 3, 2, 2 }));

    list = PDFPageRangeList::parse(QStringLiteral("odd"), 5, &error);
    QCOMPARE(list->getPageIndices(), std::vector<PDFInteger>({ 0, 2, 4 }));
    list = PDFPageRangeList::parse(QStringLiteral("even"), 5, &error);
    QCOMPARE(list->getPageIndices(), std::vector<PDFInteger>({ 1, 3 }));
    list = PDFPageRangeList::parse(QStringLiteral("even"), 1, &error);
    QVERIFY(list && list->isEmpty());

    list = PDFPageRangeList::parse(QStringLiteral(" 46- , -2 "), 48, &error);
    QCOMPARE(list->getPageIndices(), std::vector<PDFInteger>({ 45, 46, 47, 0, 1 }));

    list = PDFPageRangeList::parse(QString(), 48, &error);
    QVERIFY(list && list->isEmpty());
}

void DocumentSplitterTest::rangeListErrors_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<qint64>("pageCount");
    QTest::addColumn<QString>("error");

    // 385:11855: "Page 60 does not exist – the document has 48 pages"
    QTest::newRow("outside") << QStringLiteral("1-3, 60") << qint64(48) << QStringLiteral("Page 60 does not exist – the document has 48 pages");
    QTest::newRow("zero") << QStringLiteral("0") << qint64(48) << QStringLiteral("Page 0 does not exist – the document has 48 pages");
    QTest::newRow("word") << QStringLiteral("1, abc") << qint64(48) << QStringLiteral("\"abc\" is not a page or a range of pages. Examples: 1-5, 7, 10-, odd, even");
    QTest::newRow("empty-item") << QStringLiteral("1,,2") << qint64(48) << QStringLiteral("\"\" is not a page or a range of pages. Examples: 1-5, 7, 10-, odd, even");
    // K42: the core is ASCII only, the binder writes the canonical text
    QTest::newRow("en-dash") << QStringLiteral("1–3") << qint64(48) << QStringLiteral("\"1–3\" is not a page or a range of pages. Examples: 1-5, 7, 10-, odd, even");
    QTest::newRow("dash") << QStringLiteral("-") << qint64(48) << QStringLiteral("\"-\" is not a page or a range of pages. Examples: 1-5, 7, 10-, odd, even");
    QTest::newRow("huge-number") << QStringLiteral("99999999999") << qint64(48) << QStringLiteral("\"99999999999\" is not a page or a range of pages. Examples: 1-5, 7, 10-, odd, even");
    QTest::newRow("too-many") << QStringLiteral("1-1000000, 1") << qint64(1000000) << QStringLiteral("The list has more than %1 pages.").arg(QLocale().toString(1000000));
}

void DocumentSplitterTest::rangeListErrors()
{
    QFETCH(QString, text);
    QFETCH(qint64, pageCount);
    QFETCH(QString, error);

    QString message;
    QVERIFY(!PDFPageRangeList::parse(text, pageCount, &message));
    QCOMPARE(message, error);
}

void DocumentSplitterTest::rangeListText()
{
    // The canonical text reads back to the same pages
    QString error;
    for (const QString& text : { QStringLiteral("1-3, 8, 10-12"), QStringLiteral("9-7, 7"), QStringLiteral("2, 2, 4"), QStringLiteral("5") })
    {
        const std::optional<PDFPageRangeList> list = PDFPageRangeList::parse(text, 48, &error);
        QVERIFY(list);
        QCOMPARE(list->toText(), text);
        QCOMPARE(PDFPageRangeList::parse(list->toText(), 48, &error)->getPageIndices(), list->getPageIndices());
    }
    QCOMPARE(PDFPageRangeList::parse(QStringLiteral("odd"), 6, &error)->toText(), QStringLiteral("1, 3, 5"));
}

void DocumentSplitterTest::bookmarksRead()
{
    // A -> page 1 by /Dest; A1 -> page 3 by a named destination of the name tree (12.3.2.3);
    // B -> page 5 by a GoTo action (12.6.4.2); C without a target; D -> a URI action
    SplitterTestPdf pdf = SplitterTestPdf::createPages(6, "/Outlines 20 0 R /Names << /Dests << /Names [(chap3) [104 0 R /Fit]] >> >>");
    pdf.set(20, "<< /Type /Outlines /First 21 0 R /Last 24 0 R >>");
    pdf.set(21, "<< /Title (A) /Parent 20 0 R /Next 22 0 R /First 25 0 R /Last 25 0 R /Dest [100 0 R /Fit] >>");
    pdf.set(22, "<< /Title (B) /Parent 20 0 R /Prev 21 0 R /Next 23 0 R /A << /S /GoTo /D [108 0 R /XYZ 0 0 0] >> >>");
    pdf.set(23, "<< /Title (C) /Parent 20 0 R /Prev 22 0 R /Next 24 0 R >>");
    pdf.set(24, "<< /Title (D) /Parent 20 0 R /Prev 23 0 R /A << /S /URI /URI (https://example.com) >> >>");
    pdf.set(25, "<< /Title (A1) /Parent 21 0 R /Dest (chap3) >>");
    const PDFDocument document = pdf.read();

    const std::vector<PDFOutlineBreakPoints::Bookmark> bookmarks = PDFOutlineBreakPoints::readBookmarks(&document);
    const std::vector<PDFOutlineBreakPoints::Bookmark> expected = {
        { QStringLiteral("A"), 0, 0 },
        { QStringLiteral("A1"), 1, 2 },
        { QStringLiteral("B"), 0, 4 },
        { QStringLiteral("C"), 0, -1 },
        { QStringLiteral("D"), 0, -1 },
    };
    QCOMPARE(bookmarks, expected);
}

void DocumentSplitterTest::bookmarksHostileOutline()
{
    // A cycle (/Next of B is A) and a node shared by two paths (/First of A and of B is X):
    // every item is read once, the walk ends (one visited set for the whole walk)
    SplitterTestPdf pdf = SplitterTestPdf::createPages(3, "/Outlines 20 0 R");
    pdf.set(20, "<< /Type /Outlines /First 21 0 R /Last 22 0 R >>");
    pdf.set(21, "<< /Title (A) /Parent 20 0 R /Next 22 0 R /First 23 0 R /Last 23 0 R /Dest [100 0 R /Fit] >>");
    pdf.set(22, "<< /Title (B) /Parent 20 0 R /Next 21 0 R /First 23 0 R /Last 23 0 R /Dest [102 0 R /Fit] >>");
    pdf.set(23, "<< /Title (X) /Next 20 0 R /Dest [104 0 R /Fit] >>");
    const PDFDocument document = pdf.read();

    const std::vector<PDFOutlineBreakPoints::Bookmark> bookmarks = PDFOutlineBreakPoints::readBookmarks(&document);
    const std::vector<PDFOutlineBreakPoints::Bookmark> expected = {
        { QStringLiteral("A"), 0, 0 },
        { QStringLiteral("X"), 1, 2 },
        { QStringLiteral("B"), 0, 1 },
    };
    QCOMPARE(bookmarks, expected);
}

void DocumentSplitterTest::bookmarksGroups()
{
    // 383:6771: the bookmarks sorted by their pages, two of one page are one file, the pages
    // before the first bookmark join the first file, a bookmark without a page is named
    std::vector<PDFInteger> pages;
    for (PDFInteger i = 0; i < 48; ++i)
    {
        pages.push_back(i);
    }
    const std::vector<PDFOutlineBreakPoints::Bookmark> bookmarks = {
        { QStringLiteral("Safety"), 0, 19 },
        { QStringLiteral("Introduction"), 0, 2 },
        { QStringLiteral("Lost"), 0, -1 },
        { QStringLiteral("Setup"), 0, 6 },
        { QStringLiteral("Setup again"), 1, 6 },
    };
    QStringList skipped;
    const std::vector<PDFOutlineBreakPoints::Group> groups = PDFOutlineBreakPoints::createGroups(bookmarks, pages, &skipped);
    const std::vector<PDFOutlineBreakPoints::Group> expected = {
        { 0, 5, QStringLiteral("Introduction") },
        { 6, 18, QStringLiteral("Setup") },
        { 19, 47, QStringLiteral("Safety") },
    };
    QCOMPARE(groups, expected);
    QCOMPARE(skipped, QStringList({ QStringLiteral("Lost") }));

    // Only the odd pages (0, 2, 4, ...): a bookmark of page 6 starts at the selected page 6,
    // one after the last selected page is left out
    std::vector<PDFInteger> odd;
    for (PDFInteger i = 0; i < 10; i += 2)
    {
        odd.push_back(i);
    }
    skipped.clear();
    const std::vector<PDFOutlineBreakPoints::Group> oddGroups = PDFOutlineBreakPoints::createGroups({ { QStringLiteral("S"), 0, 5 }, { QStringLiteral("Z"), 0, 9 } }, odd, &skipped);
    const std::vector<PDFOutlineBreakPoints::Group> oddExpected = { { 0, 8, QStringLiteral("S") } };
    QCOMPARE(oddGroups, oddExpected);
    QCOMPARE(skipped, QStringList({ QStringLiteral("Z") }));
    QVERIFY(PDFOutlineBreakPoints::createGroups({ { QStringLiteral("L"), 0, -1 } }, pages, nullptr).empty());
}

void DocumentSplitterTest::splitLeavesNoMarker()
{
    // R4 of step 2 as the plan 5.6 asks: page 2 has a link to page 3, a named destination and
    // an outline item lead to page 3 - the file of page 2 has no object of page 3
    SplitterTestPdf pdf = SplitterTestPdf::createPages(4, "/Outlines 20 0 R /Names << /Dests << /Names [(DESTMARKER) [104 0 R /Fit]] >> >>",
                                                       { { 2, "/Annots [30 0 R 31 0 R]" }, { 3, "/Annots [32 0 R]" } });
    pdf.set(20, "<< /Type /Outlines /First 21 0 R /Last 22 0 R /Count 2 >>");
    pdf.set(21, "<< /Title (OUTLINE2) /Parent 20 0 R /Next 22 0 R /Dest [102 0 R /Fit] >>");
    pdf.set(22, "<< /Title (OUTLINE3) /Parent 20 0 R /Prev 21 0 R /Dest [104 0 R /Fit] >>");
    pdf.set(30, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest [104 0 R /Fit] >>");
    pdf.set(31, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest [102 0 R /XYZ 0 0 0] >>");
    pdf.set(32, "<< /Type /Annot /Subtype /Text /Contents (ANNOTMARKER3) /Rect [0 0 10 10] /P 104 0 R >>");
    const PDFDocument document = pdf.read();

    PDFDocumentSplitter::Result result;
    const std::vector<PDFDocumentSplitter::PartResult> parts = SplitterTestHelper::split(document, { { 1 }, { 2 }, { 3 }, { 4 } }, &result);
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    QCOMPARE(parts.size(), size_t(4));
    for (size_t i = 0; i < parts.size(); ++i)
    {
        QCOMPARE(parts[i].partIndex, i);
        QCOMPARE(parts[i].pageCount, 1);
        for (int page = 1; page <= 4; ++page)
        {
            QCOMPARE(parts[i].data.contains("PAGEMARKER" + QByteArray::number(page) + "X"), page == int(i + 1));
        }
    }

    // The file of page 2: the link to page 3 and the item of page 3 are gone, its own stay
    const QByteArray& second = parts[1].data;
    QVERIFY(!second.contains("OUTLINE3") && !second.contains("DESTMARKER") && !second.contains("ANNOTMARKER3"));
    QVERIFY(second.contains("OUTLINE2"));
    QCOMPARE(parts[1].statistics.linksRemoved, 1);
    const PDFDocument reread = SplitterTestHelper::readBack(second);
    QCOMPARE(reread.getCatalog()->getPageCount(), size_t(1));
    QCOMPARE(reread.getCatalog()->getPage(0)->getAnnotations().size(), size_t(1));
    QVERIFY(SplitterTestHelper::getDanglingReferences(reread).isEmpty());

    // The inherited media box and resources of the root (Table 31) are in the page
    QCOMPARE(reread.getCatalog()->getPage(0)->getMediaBox(), QRectF(0, 0, 600, 800));
    QVERIFY(reread.getDictionaryFromObject(reread.getCatalog()->getPage(0)->getResources()));

    // The file of page 3 has its destination and its annotation
    QVERIFY(parts[2].data.contains("DESTMARKER") && parts[2].data.contains("ANNOTMARKER3") && parts[2].data.contains("OUTLINE3"));
}

void DocumentSplitterTest::splitFieldsInEveryPart()
{
    // 5.6: a field with widgets on pages 1 and 3 is in the files of both, each with one widget;
    // a field of page 2 only is in its file only
    SplitterTestPdf pdf = SplitterTestPdf::createPages(3, "/AcroForm << /Fields [40 0 R 43 0 R] >>",
                                                       { { 1, "/Annots [41 0 R]" }, { 2, "/Annots [43 0 R]" }, { 3, "/Annots [42 0 R]" } });
    pdf.set(40, "<< /FT /Tx /T (shared) /V (SHAREDVALUE) /Kids [41 0 R 42 0 R] >>");
    pdf.set(41, "<< /Type /Annot /Subtype /Widget /Parent 40 0 R /Rect [0 0 10 10] /P 100 0 R >>");
    pdf.set(42, "<< /Type /Annot /Subtype /Widget /Parent 40 0 R /Rect [0 0 10 10] /P 104 0 R >>");
    pdf.set(43, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (FIELDMARKER2) /Rect [0 0 10 10] /P 102 0 R >>");
    const PDFDocument document = pdf.read();

    const std::vector<PDFDocumentSplitter::PartResult> parts = SplitterTestHelper::split(document, { { 1 }, { 2 }, { 3 } });
    QCOMPARE(parts.size(), size_t(3));
    for (const size_t index : { size_t(0), size_t(2) })
    {
        const PDFDocument reread = SplitterTestHelper::readBack(parts[index].data);
        QVERIFY(!parts[index].data.contains("FIELDMARKER2"));
        const PDFDictionary* form = reread.getDictionaryFromObject(SplitterTestHelper::getCatalog(reread)->get("AcroForm"));
        QVERIFY(form);
        const PDFArray* fields = reread.getObject(form->get("Fields")).getArray();
        QCOMPARE(fields->getCount(), size_t(1));
        const PDFDictionary* shared = reread.getDictionaryFromObject(fields->getItem(0));
        QCOMPARE(reread.getObject(shared->get("Kids")).getArray()->getCount(), size_t(1));
        QVERIFY(SplitterTestHelper::getDanglingReferences(reread).isEmpty());
    }
    QVERIFY(parts[1].data.contains("FIELDMARKER2") && !parts[1].data.contains("SHAREDVALUE"));
}

void DocumentSplitterTest::splitOutlineAndLabels()
{
    // Labels i, ii, 1, 2, 3, 4 (12.4.2): the first page of the second part is page 3 of the
    // source, its label stays "1"; the outline of each part keeps the items of its pages
    SplitterTestPdf pdf = SplitterTestPdf::createPages(6, "/PageLabels << /Nums [0 << /S /r >> 2 << /S /D >>] >> /Outlines 20 0 R");
    pdf.set(20, "<< /Type /Outlines /First 21 0 R /Last 22 0 R /Count 2 >>");
    pdf.set(21, "<< /Title (Front) /Parent 20 0 R /Next 22 0 R /Dest [100 0 R /Fit] >>");
    pdf.set(22, "<< /Title (Body) /Parent 20 0 R /Prev 21 0 R /Dest [104 0 R /Fit] >>");
    const PDFDocument document = pdf.read();

    const std::vector<PDFDocumentSplitter::PartResult> parts = SplitterTestHelper::split(document, { { 1, 2 }, { 3, 4, 5, 6 } });
    QCOMPARE(parts.size(), size_t(2));
    const PDFDocument first = SplitterTestHelper::readBack(parts[0].data);
    const PDFDocument second = SplitterTestHelper::readBack(parts[1].data);
    QCOMPARE(first.getCatalog()->getPageLabel(1), QStringLiteral("ii"));
    QCOMPARE(second.getCatalog()->getPageLabel(0), QStringLiteral("1"));
    QCOMPARE(second.getCatalog()->getPageLabel(3), QStringLiteral("4"));
    QCOMPARE(first.getCatalog()->getOutlineRootPtr()->getChildCount(), size_t(1));
    QCOMPARE(first.getCatalog()->getOutlineRootPtr()->getChild(0)->getTitle(), QStringLiteral("Front"));
    QCOMPARE(second.getCatalog()->getOutlineRootPtr()->getChildCount(), size_t(1));
    QCOMPARE(second.getCatalog()->getOutlineRootPtr()->getChild(0)->getTitle(), QStringLiteral("Body"));
}

void DocumentSplitterTest::splitAttachmentsFirstOnly()
{
    // Owner answer Q6: the attachments of the document (/Names /EmbeddedFiles, /AF of the
    // catalog) stay only in the first file; a file attachment of a page goes with its page
    SplitterTestPdf pdf = SplitterTestPdf::createPages(3, "/Names << /EmbeddedFiles << /Names [(data.csv) 50 0 R] >> >> /AF [52 0 R]", { { 3, "/Annots [53 0 R]" } });
    pdf.set(50, "<< /Type /Filespec /F (data.csv) /EF << /F 51 0 R >> >>");
    pdf.set(51, SplitterTestPdf::stream("DOCUMENTATTACHMENT"));
    pdf.set(52, "<< /Type /Filespec /F (af.txt) /EF << /F 54 0 R >> >>");
    pdf.set(53, "<< /Type /Annot /Subtype /FileAttachment /Rect [0 0 10 10] /FS << /Type /Filespec /F (page.txt) /EF << /F 55 0 R >> >> >>");
    pdf.set(54, SplitterTestPdf::stream("CATALOGAF"));
    pdf.set(55, SplitterTestPdf::stream("PAGEATTACHMENT"));
    const PDFDocument document = pdf.read();

    const std::vector<PDFDocumentSplitter::PartResult> parts = SplitterTestHelper::split(document, { { 1 }, { 2 }, { 3 } });
    QCOMPARE(parts.size(), size_t(3));
    QVERIFY(parts[0].data.contains("DOCUMENTATTACHMENT") && parts[0].data.contains("CATALOGAF"));
    QVERIFY(!parts[0].attachmentsRemoved);
    for (const size_t index : { size_t(1), size_t(2) })
    {
        QVERIFY(!parts[index].data.contains("DOCUMENTATTACHMENT") && !parts[index].data.contains("CATALOGAF"));
        QVERIFY(parts[index].attachmentsRemoved);
        const PDFDocument reread = SplitterTestHelper::readBack(parts[index].data);
        QVERIFY(!SplitterTestHelper::getCatalog(reread)->hasKey("AF"));
    }
    QVERIFY(parts[2].data.contains("PAGEATTACHMENT") && !parts[0].data.contains("PAGEATTACHMENT"));
}

void DocumentSplitterTest::splitNewDocumentKeys()
{
    // A part is a new document (R2): a new identifier of two equal strings (14.4), different in
    // each part, no /Encrypt, no /Perms, no declaration of PDF/UA (the tags are removed, Q5 a)
    SplitterTestPdf pdf = SplitterTestPdf::createPages(2, "/Perms << /DocMDP 60 0 R >> /StructTreeRoot 61 0 R /MarkInfo << /Marked true >> /Metadata 62 0 R");
    pdf.set(60, "<< /Type /Sig /ByteRange [0 0 0 0] /Contents <00> >>");
    pdf.set(61, "<< /Type /StructTreeRoot >>");
    pdf.set(62, SplitterTestPdf::stream("<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                                         "<rdf:Description rdf:about=\"\" xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\"><pdfuaid:part>1</pdfuaid:part></rdf:Description></rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>"));
    const PDFDocument document = pdf.read();

    const std::vector<PDFDocumentSplitter::PartResult> parts = SplitterTestHelper::split(document, { { 1 }, { 2 } });
    QCOMPARE(parts.size(), size_t(2));
    QByteArray previousId;
    for (const PDFDocumentSplitter::PartResult& part : parts)
    {
        const PDFDocument reread = SplitterTestHelper::readBack(part.data);
        const PDFDictionary* trailer = reread.getTrailerDictionary();
        QVERIFY(!trailer->hasKey("Encrypt"));
        const PDFArray* id = reread.getObject(trailer->get("ID")).getArray();
        QVERIFY(id && id->getCount() == 2);
        QCOMPARE(id->getItem(0).getString(), id->getItem(1).getString());
        QVERIFY(id->getItem(0).getString() != QByteArray::fromHex("00112233445566778899AABBCCDDEEFF"));
        QVERIFY(id->getItem(0).getString() != previousId);
        previousId = id->getItem(0).getString();

        const PDFDictionary* catalog = SplitterTestHelper::getCatalog(reread);
        QVERIFY(!catalog->hasKey("Perms") && !catalog->hasKey("StructTreeRoot"));
        QVERIFY(part.statistics.structureTreeRemoved);
        QCOMPARE(part.conformanceRemoved, QStringList({ QStringLiteral("PDF/UA") }));
        QVERIFY(!part.data.contains("pdfuaid:part"));
    }
}

void DocumentSplitterTest::splitPartDates()
{
    // Plan 5.6: a part is a new document - /CreationDate and /ModDate of /Info are the time of
    // its creation, not the dates of the source (14.3.3). The kept XMP packet (PDF 1.7) loses
    // the counterparts of /Info (Table 349), which would contradict them; its other properties
    // stay. Extract one file (the merger of one source, isNewFile) gets the same dates.
    SplitterTestPdf pdf = SplitterTestPdf::createPages(3, "/Metadata 62 0 R");
    pdf.set(62, SplitterTestPdf::stream("<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                                         "<rdf:Description rdf:about=\"\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\" xmlns:test=\"http://example.com/test/\">"
                                         "<xmp:CreateDate>2020-01-01T00:00:00Z</xmp:CreateDate><test:Label>KEEPXMP</test:Label></rdf:Description></rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>"));
    const PDFDocument document = pdf.read();
    QCOMPARE(document.getInfo()->creationDate, QDateTime(QDate(2020, 1, 1), QTime(0, 0), QTimeZone::UTC));

    const QDateTime creationTime(QDate(2026, 11, 9), QTime(10, 0), QTimeZone::UTC);
    std::vector<PDFDocumentSplitter::Part> parts(2);
    parts[0].pages = { 0 };
    parts[1].pages = { 1, 2 };
    PDFDocumentSplitter::Settings settings;
    settings.creationTime = creationTime;
    std::vector<PDFDocumentSplitter::PartResult> results;
    const PDFDocumentSplitter::Result result = PDFDocumentSplitter::split(&document, parts, settings, nullptr, nullptr,
                                                                          [&](PDFDocumentSplitter::PartResult part) { results.push_back(std::move(part)); return true; });
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    QCOMPARE(results.size(), size_t(2));
    for (const PDFDocumentSplitter::PartResult& part : results)
    {
        const PDFDocument reread = SplitterTestHelper::readBack(part.data);
        QCOMPARE(reread.getInfo()->creationDate, creationTime);
        QCOMPARE(reread.getInfo()->modifiedDate, creationTime);
        QVERIFY(!part.data.contains("2020-01-01T00:00:00Z"));
        QVERIFY(part.data.contains("KEEPXMP"));
    }

    // Extract one file: the merger of the one source with the pages 1 and 3
    PDFDocumentMerger::Source source = PDFDocumentMerger::Source::createDocument(PDFDocumentPointer(new PDFDocument(document)), QStringLiteral("Handbook.pdf"));
    std::vector<PDFDocumentMerger::Item> items(2);
    items[0].pageIndex = 0;
    items[1].pageIndex = 2;
    PDFDocumentMerger::Settings mergerSettings;
    mergerSettings.isNewFile = true;
    const PDFDocumentMerger::Result merged = PDFDocumentMerger::merge({ source }, items, mergerSettings, nullptr, nullptr);
    QVERIFY2(merged.isSuccess, qPrintable(merged.errorMessage));
    const QByteArray extracted = PDFDocumentTool::writeDocumentData(&merged.document, nullptr);
    const PDFDocument extractedDocument = SplitterTestHelper::readBack(extracted);
    QCOMPARE(extractedDocument.getCatalog()->getPageCount(), size_t(2));
    QVERIFY(extractedDocument.getInfo()->creationDate.isValid());
    QVERIFY(qAbs(extractedDocument.getInfo()->creationDate.secsTo(QDateTime::currentDateTimeUtc())) < 3600);
    QVERIFY(!extracted.contains("2020-01-01T00:00:00Z"));
    QVERIFY(extracted.contains("KEEPXMP"));

    // Merge of one whole PDF keeps its catalog and its dates (R3 of step 2: unchanged)
    std::vector<PDFDocumentMerger::Item> wholeItems(3);
    for (size_t i = 0; i < wholeItems.size(); ++i)
    {
        wholeItems[i].pageIndex = PDFInteger(i);
    }
    const PDFDocumentMerger::Result whole = PDFDocumentMerger::merge({ source }, wholeItems, PDFDocumentMerger::Settings(), nullptr, nullptr);
    QVERIFY2(whole.isSuccess, qPrintable(whole.errorMessage));
    QCOMPARE(SplitterTestHelper::readBack(PDFDocumentTool::writeDocumentData(&whole.document, nullptr)).getInfo()->creationDate, QDateTime(QDate(2020, 1, 1), QTime(0, 0), QTimeZone::UTC));
}

void DocumentSplitterTest::splitCrossReferenceOfPart()
{
    // R2: the objects of a part are numbered anew - its cross-reference table has its own
    // objects + the entry 0, never the numbers of the source (40 pages, each part 2 pages)
    SplitterTestPdf pdf = SplitterTestPdf::createPages(40);
    const PDFDocument document = pdf.read();
    std::vector<std::vector<PDFInteger>> partPages;
    for (PDFInteger page = 1; page <= 40; page += 2)
    {
        partPages.push_back({ page, page + 1 });
    }
    const std::vector<PDFDocumentSplitter::PartResult> parts = SplitterTestHelper::split(document, partPages);
    QCOMPARE(parts.size(), size_t(20));
    for (const PDFDocumentSplitter::PartResult& part : parts)
    {
        const PDFDocument reread = SplitterTestHelper::readBack(part.data);
        QCOMPARE(reread.getCatalog()->getPageCount(), size_t(2));
        // catalog, info, page tree, font, 2 pages, 2 contents = 8 objects + the entry 0
        QCOMPARE(SplitterTestHelper::getReachableCount(reread), qint64(8));
        QCOMPARE(SplitterTestHelper::getCrossReferenceSize(reread), qint64(9));
    }
}

void DocumentSplitterTest::splitStopAndCancel()
{
    const PDFDocument document = SplitterTestPdf::createPages(5).read();
    std::vector<PDFDocumentSplitter::Part> parts;
    for (PDFInteger page = 0; page < 5; ++page)
    {
        parts.push_back({ { page } });
    }

    // The receiver stops after the second part: the parts after it are not handed over
    int received = 0;
    PDFDocumentSplitter::Result result = PDFDocumentSplitter::split(&document, parts, PDFDocumentSplitter::Settings(), nullptr, nullptr,
                                                                    [&](PDFDocumentSplitter::PartResult) { ++received; return received < 2; });
    QVERIFY(result.isStopped && !result.isSuccess);
    QCOMPARE(received, 2);
    QCOMPARE(result.receivedCount, size_t(1));

    // A canceled split hands over nothing
    SplitterTestCanceledControl control;
    received = 0;
    result = PDFDocumentSplitter::split(&document, parts, PDFDocumentSplitter::Settings(), &control, nullptr,
                                        [&](PDFDocumentSplitter::PartResult) { ++received; return true; });
    QVERIFY(result.isCanceled && !result.isSuccess);
    QCOMPARE(received, 0);
}

void DocumentSplitterTest::splitInvalidParts()
{
    const PDFDocument document = SplitterTestPdf::createPages(3).read();
    auto receiver = [](PDFDocumentSplitter::PartResult) { return true; };
    PDFDocumentSplitter::Result result = PDFDocumentSplitter::split(&document, { { { 0 } }, { {} } }, PDFDocumentSplitter::Settings(), nullptr, nullptr, receiver);
    QCOMPARE(result.errorMessage, QStringLiteral("A file would have no page."));
    result = PDFDocumentSplitter::split(&document, { { { 3 } } }, PDFDocumentSplitter::Settings(), nullptr, nullptr, receiver);
    QCOMPARE(result.errorMessage, QStringLiteral("Page 4 does not exist."));
    result = PDFDocumentSplitter::split(&document, {}, PDFDocumentSplitter::Settings(), nullptr, nullptr, receiver);
    QVERIFY(!result.isSuccess && !result.errorMessage.isEmpty());

    // More than 10 000 files (plan 6)
    std::vector<PDFDocumentSplitter::Part> tooMany(PDFDocumentSplitter::MAXIMUM_PART_COUNT + 1, PDFDocumentSplitter::Part{ { 0 } });
    result = PDFDocumentSplitter::split(&document, tooMany, PDFDocumentSplitter::Settings(), nullptr, nullptr, receiver);
    QCOMPARE(result.errorMessage, QStringLiteral("The document would be split into more than %1 files.").arg(QLocale().toString(10000)));
}

void DocumentSplitterTest::splitTenThousandPages()
{
    // R2 and plan 6: Every page of 10 000 pages - one index for all parts, each part only its
    // objects (a cross-reference table of 6 entries: the entry 0, catalog, info, page tree,
    // font, page, content), within a time limit
    constexpr int pageCount = 10000;
    const PDFDocument document = SplitterTestPdf::createPages(pageCount).read();
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(pageCount));

    std::vector<PDFDocumentSplitter::Part> parts;
    for (PDFInteger page = 0; page < pageCount; ++page)
    {
        parts.push_back({ { page } });
    }

    QElapsedTimer timer;
    timer.start();
    size_t received = 0;
    bool isEveryPartOwn = true;
    const PDFDocumentSplitter::Result result = PDFDocumentSplitter::split(&document, parts, PDFDocumentSplitter::Settings(), nullptr, nullptr,
                                                                          [&](PDFDocumentSplitter::PartResult part)
    {
        if (part.partIndex != received)
        {
            isEveryPartOwn = false;
        }
        ++received;
        if (part.partIndex % 997 == 0 || part.partIndex + 1 == size_t(pageCount))
        {
            const PDFDocument reread = SplitterTestHelper::readBack(part.data);
            const QByteArray marker = "PAGEMARKER" + QByteArray::number(qint64(part.partIndex) + 1) + "X";
            isEveryPartOwn = isEveryPartOwn && reread.getCatalog()->getPageCount() == 1 && part.data.contains(marker) &&
                             SplitterTestHelper::getCrossReferenceSize(reread) == 7;
        }
        else if (part.data.size() > 4096)
        {
            isEveryPartOwn = false;
        }
        return true;
    });
    const qint64 elapsed = timer.elapsed();
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    QCOMPARE(received, size_t(pageCount));
    QVERIFY(isEveryPartOwn);
    QVERIFY2(elapsed < 120000 * SplitterTestHelper::getTimeoutFactor(), qPrintable(QString::number(elapsed)));
}

void DocumentSplitterTest::finisherKeys()
{
    // R2: the one finishing - /ID new, /Encrypt gone, the objects nothing refers to removed and
    // the rest numbered without a gap; a whole single document keeps /CreationDate of /Info
    SplitterTestPdf pdf = SplitterTestPdf::createPages(1);
    pdf.set(90, "<< /Unreferenced (DEADMARKER) >>");
    const PDFDocument document = pdf.read();
    QVERIFY(document.getStorage().getObjects().size() > 90);

    PDFNewDocumentFinisher::Settings settings;
    settings.isWholeSingleDocument = true;
    PDFNewDocumentFinisher::Result result = PDFNewDocumentFinisher::finish(document, settings, nullptr);
    QVERIFY(result.isSuccess);
    QCOMPARE(result.document.getStorage().getObjects().size(), size_t(7));
    const QByteArray written = PDFDocumentTool::writeDocumentData(&result.document, nullptr);
    QVERIFY(!written.contains("DEADMARKER"));
    const PDFDocument reread = SplitterTestHelper::readBack(written);
    QCOMPARE(reread.getInfo()->creationDate, QDateTime(QDate(2020, 1, 1), QTime(0, 0), QTimeZone::UTC));

    // A new document of more inputs gets the time of its creation in /Info (14.3.3)
    settings.isWholeSingleDocument = false;
    settings.creationTime = QDateTime(QDate(2026, 11, 9), QTime(10, 0), QTimeZone::UTC);
    result = PDFNewDocumentFinisher::finish(document, settings, nullptr);
    QVERIFY(result.isSuccess);
    const PDFDocument rereadNew = SplitterTestHelper::readBack(PDFDocumentTool::writeDocumentData(&result.document, nullptr));
    QCOMPARE(rereadNew.getInfo()->creationDate, settings.creationTime);

    // A canceled finishing gives no document
    SplitterTestCanceledControl control;
    result = PDFNewDocumentFinisher::finish(document, settings, &control);
    QVERIFY(result.isCanceled && !result.isSuccess);
}

void DocumentSplitterTest::folderSinkPolicies()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString folder = directory.path();
    auto writeFile = [&](const QString& name, const QByteArray& data)
    {
        QFile file(QDir(folder).filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(data);
    };
    auto readFile = [&](const QString& name)
    {
        QFile file(QDir(folder).filePath(name));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };

    QString error;
    writeFile(QStringLiteral("a.pdf"), "OLD");

    // Overwrite: replaced, listed as overwritten (Delete Written Files keeps it, K46)
    {
        PDFDocumentToolFolderSink sink(folder, PDFDocumentToolFileSink::ExistingFiles::Overwrite);
        QVERIFY(sink.begin({ QStringLiteral("a.pdf"), QStringLiteral("b.pdf") }, &error));
        QCOMPARE(sink.getConflictFiles(), QStringList({ QStringLiteral("a.pdf") }));
        QVERIFY(sink.write(QStringLiteral("a.pdf"), "NEW", &error));
        QVERIFY(sink.write(QStringLiteral("b.pdf"), "B", &error));
        QCOMPARE(readFile(QStringLiteral("a.pdf")), QByteArray("NEW"));
        QCOMPARE(sink.getWrittenFiles().size(), 2);
        QCOMPARE(sink.getOverwrittenFiles(), QStringList({ QDir::cleanPath(QDir(folder).filePath(QStringLiteral("a.pdf"))) }));
    }

    // Skip: the existing file stays, its name is not written
    {
        PDFDocumentToolFolderSink sink(folder, PDFDocumentToolFileSink::ExistingFiles::Skip);
        QVERIFY(sink.begin({ QStringLiteral("a.pdf"), QStringLiteral("c.pdf") }, &error));
        QVERIFY(!sink.write(QStringLiteral("a.pdf"), "SKIPPED", &error));
        QVERIFY(sink.write(QStringLiteral("c.pdf"), "C", &error));
        QCOMPARE(readFile(QStringLiteral("a.pdf")), QByteArray("NEW"));
        QCOMPARE(sink.getNotWrittenFiles(), QStringList({ QStringLiteral("a.pdf") }));
        QCOMPARE(sink.getWrittenFiles().size(), 1);
    }

    // Ask of the core cannot ask: the file is skipped (the application asks, 356:11997)
    {
        PDFDocumentToolFolderSink sink(folder, PDFDocumentToolFileSink::ExistingFiles::Ask);
        QVERIFY(sink.begin({ QStringLiteral("a.pdf") }, &error));
        QVERIFY(!sink.write(QStringLiteral("a.pdf"), "ASKED", &error));
        QCOMPARE(readFile(QStringLiteral("a.pdf")), QByteArray("NEW"));
    }

    // Rename: "a (2).pdf" (383:4447 "Add (2), (3) …")
    {
        PDFDocumentToolFolderSink sink(folder, PDFDocumentToolFileSink::ExistingFiles::Rename);
        QVERIFY(sink.begin({ QStringLiteral("a.pdf") }, &error));
        QVERIFY(sink.write(QStringLiteral("a.pdf"), "RENAMED", &error));
        QCOMPARE(readFile(QStringLiteral("a.pdf")), QByteArray("NEW"));
        QCOMPARE(readFile(QStringLiteral("a (2).pdf")), QByteArray("RENAMED"));
        QCOMPARE(QFileInfo(sink.getWrittenFiles().front()).fileName(), QStringLiteral("a (2).pdf"));
        QVERIFY(sink.getOverwrittenFiles().isEmpty());
    }
}

void DocumentSplitterTest::folderSinkRenameAvoidsRun()
{
    // A renamed file never takes the name of a later file of the same run
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile existing(QDir(directory.path()).filePath(QStringLiteral("a.pdf")));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.close();

    QString error;
    PDFDocumentToolFolderSink sink(directory.path(), PDFDocumentToolFileSink::ExistingFiles::Rename);
    QVERIFY(sink.begin({ QStringLiteral("a.pdf"), QStringLiteral("a (2).pdf") }, &error));
    QVERIFY(sink.write(QStringLiteral("a.pdf"), "FIRST", &error));
    QVERIFY(sink.write(QStringLiteral("a (2).pdf"), "SECOND", &error));
    QStringList names;
    for (const QString& path : sink.getWrittenFiles())
    {
        names << QFileInfo(path).fileName();
    }
    QCOMPARE(names, QStringList({ QStringLiteral("a (3).pdf"), QStringLiteral("a (2).pdf") }));
}

void DocumentSplitterTest::folderSinkProtectedAndStop()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir::cleanPath(QDir(directory.path()).filePath(QStringLiteral("Handbook.pdf")));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("SOURCE");
    file.close();

    // R14: a source (or a file open in a tab) is never overwritten, also by Overwrite
    QString error;
    SplitterTestFolderSink sink(directory.path(), PDFDocumentToolFileSink::ExistingFiles::Overwrite, { source });
    QVERIFY(sink.begin({ QStringLiteral("Handbook.pdf"), QStringLiteral("b.pdf"), QStringLiteral("c.pdf") }, &error));
    QVERIFY(!sink.write(QStringLiteral("Handbook.pdf"), "PART", &error));
    QVERIFY(error.contains(QStringLiteral("not overwritten")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("SOURCE"));
    file.close();

    // Stop: the sink writes no further file
    sink.stopAfter = 1;
    QVERIFY(sink.write(QStringLiteral("b.pdf"), "B", &error));
    QVERIFY(sink.isStopped());
    QVERIFY(!sink.write(QStringLiteral("c.pdf"), "C", &error));
    QVERIFY(!QFileInfo::exists(QDir(directory.path()).filePath(QStringLiteral("c.pdf"))));
    QCOMPARE(sink.getNotWrittenFiles(), QStringList({ QStringLiteral("Handbook.pdf"), QStringLiteral("c.pdf") }));
}

void DocumentSplitterTest::folderSinkBegin()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString error;

    PDFDocumentToolFolderSink missing(QDir(directory.path()).filePath(QStringLiteral("missing")), PDFDocumentToolFileSink::ExistingFiles::Rename);
    QVERIFY(!missing.begin({ QStringLiteral("a.pdf") }, &error));
    QCOMPARE(error, QStringLiteral("The folder does not exist."));

    PDFDocumentToolFolderSink sink(directory.path(), PDFDocumentToolFileSink::ExistingFiles::Rename);
    QVERIFY(!sink.begin({ QStringLiteral("a.pdf"), QStringLiteral("A.PDF") }, &error));
    QCOMPARE(error, QStringLiteral("More files would be named A.PDF."));
    QVERIFY(!sink.begin({ QStringLiteral("../a.pdf") }, &error));
    QVERIFY(!sink.begin({ QStringLiteral("CON.pdf") }, &error));
    QVERIFY(sink.begin({ QStringLiteral("a.pdf") }, &error));
}

QTEST_GUILESS_MAIN(DocumentSplitterTest)

#include "tst_documentsplittertest.moc"
