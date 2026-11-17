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

// Document Report of the core (step 7 of phase 2, plan 4.4, 8; package P1b): the sections of
// PDFDocumentReport (R7-5: one implementation of Report, Properties, MCP and PdfTool info*), the JavaScript
// scanner over the raw objects (B201 - B203, R7-4), the XML of the structure, the tool review.report and
// PdfTool info*, xml (B164, B176 / B204, B178, B180, B189). Every document is written here byte by byte, so
// the expected values come from ISO 32000-2, never from a run.

#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentreport.h"
#include "pdfdocumenttool_report.h"
#include "pdfdocumentwriter.h"
#include "pdfdocumentxmlexport.h"
#include "pdfjavascriptscanner.h"
#include "pdfoperationcontrol.h"
#include "pdfsecurityhandler.h"
#include "pdftransparencyrenderer.h"
#include "testlocaleguard.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest>

#include <algorithm>
#include <atomic>
#include <limits>

class DocumentReportTestHelper
{
public:
    /// A PDF file of the objects (object i + 1 is objects[i]), the catalog is object 1
    static QByteArray createPdf(const std::vector<QByteArray>& objects, const QByteArray& trailerEntries = QByteArray())
    {
        QByteArray pdf("%PDF-1.7\n");
        std::vector<qint64> offsets;
        for (size_t i = 0; i < objects.size(); ++i)
        {
            offsets.push_back(pdf.size());
            pdf += QByteArray::number(qint64(i + 1)) + " 0 obj\n" + objects[i] + "\nendobj\n";
        }

        const qint64 xref = pdf.size();
        pdf += "xref\n0 " + QByteArray::number(qint64(objects.size() + 1)) + "\n0000000000 65535 f \n";
        for (const qint64 offset : offsets)
        {
            pdf += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
        }
        pdf += "trailer\n<< /Size " + QByteArray::number(qint64(objects.size() + 1)) + " /Root 1 0 R " + trailerEntries + " >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
        return pdf;
    }

    static pdf::PDFDocument read(const QByteArray& data, bool* isRead)
    {
        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, false, false);
        pdf::PDFDocument document = reader.readFromBuffer(data);
        *isRead = reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK;
        return document;
    }

    /// An unfiltered stream
    static QByteArray stream(const QByteArray& entries, const QByteArray& data)
    {
        return "<< " + entries + " /Length " + QByteArray::number(data.size()) + " >>\nstream\n" + data + "\nendstream";
    }

    /// A JavaScript action (12.6.4.17, Table 221)
    static QByteArray javaScript(const QByteArray& code, const QByteArray& entries = QByteArray())
    {
        return "<< /S /JavaScript /JS (" + code + ") " + entries + " >>";
    }

    static QStringList getScripts(const pdf::PDFJavaScriptScanner::Entries& entries)
    {
        QStringList scripts;
        for (const pdf::PDFJavaScriptEntry& entry : entries)
        {
            scripts << entry.javaScript;
        }
        return scripts;
    }

    static bool isWellFormed(const QByteArray& xml, QString* errorMessage)
    {
        QXmlStreamReader reader(xml);
        while (!reader.atEnd())
        {
            reader.readNext();
        }
        if (reader.hasError())
        {
            *errorMessage = reader.errorString();
            return false;
        }
        return true;
    }

    /// The text of the column "value" of the row \p row of the XML of PDFOutputFormatter (PdfTool)
    static QString getRowValue(const QByteArray& xml, const QString& row)
    {
        QXmlStreamReader reader(xml);
        bool isInRow = false;
        while (!reader.atEnd())
        {
            reader.readNext();
            if (reader.isStartElement() && reader.name() == row)
            {
                isInRow = true;
            }
            else if (isInRow && reader.isStartElement() && reader.name() == QLatin1String("value"))
            {
                return reader.readElementText();
            }
        }
        return QString();
    }

    static int countRows(const QByteArray& xml, const QString& row)
    {
        QXmlStreamReader reader(xml);
        int count = 0;
        while (!reader.atEnd())
        {
            reader.readNext();
            count += reader.isStartElement() && reader.name() == row ? 1 : 0;
        }
        return count;
    }

    static int runPdfTool(const QStringList& arguments, QByteArray* output, QByteArray* errorOutput)
    {
#ifdef PDF4QT_TEST_PDFTOOL
        QProcess process;
        process.setProgram(QStringLiteral(PDF4QT_TEST_PDFTOOL));
        process.setArguments(arguments);
        process.start();
        process.waitForFinished(60000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR")));
        *output = process.readAllStandardOutput();
        *errorOutput = process.readAllStandardError();
        return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
#else
        Q_UNUSED(arguments);
        Q_UNUSED(output);
        Q_UNUSED(errorOutput);
        return -1;
#endif
    }

    /// A page tree of \p count pages (objects 2, 3 ... count + 2), the catalog of \p catalogEntries
    static std::vector<QByteArray> createPages(int count, const QByteArray& catalogEntries, const QByteArray& pageEntries = QByteArray())
    {
        std::vector<QByteArray> objects;
        QByteArray kids;
        for (int i = 0; i < count; ++i)
        {
            kids += QByteArray::number(i + 3) + " 0 R ";
        }
        objects.push_back("<< /Type /Catalog /Pages 2 0 R " + catalogEntries + " >>");
        objects.push_back("<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(count) + " >>");
        for (int i = 0; i < count; ++i)
        {
            objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] " + pageEntries + " >>");
        }
        return objects;
    }
};

class DocumentReportTest : public QObject
{
    Q_OBJECT

private slots:
    void basicInformation();
    void emptySectionReasons();
    void javaScriptOpenActionAndNext();
    void javaScriptSharedAction();
    void javaScriptLongChain();
    void javaScriptOutlineAndAdditionalActions();
    void javaScriptXfa();
    void namedDestinations();
    void pageBoxesUserUnit();
    void structureTree();
    void fontsSharedForm();
    void metadataConformance();
    void xmlExport();
    void reportTool();
    void structureXmlTool();
    void pdfToolInfo();
    void pdfToolPageRange();
    void pdfToolTextCodec();
    void pdfToolJavaScriptAndXml();
    void javaScriptSharedLongStream();
    void fontsManyForms();
    void structureXmlPermission();
    void pdfToolContentPermission();
    void pdfToolOwnerPasswordOfEmptyUserPassword();
    void xmlRealsAndLargeStreams();
    void inksCanceled();
    void factsInCLocale();
};

/// A cancellation, which comes after the operation has asked for it a given number of times
class DocumentReportTestCountingControl : public pdf::PDFOperationControl
{
public:
    explicit DocumentReportTestCountingControl(int allowedChecks) : m_allowedChecks(allowedChecks) { }

    virtual bool isOperationCanceled() const override { return ++m_checks > m_allowedChecks; }

    int getChecks() const { return m_checks.load(); }

private:
    int m_allowedChecks;
    mutable std::atomic<int> m_checks = 0;
};

void DocumentReportTest::basicInformation()
{
    using H = DocumentReportTestHelper;

    // Tagged is /MarkInfo /Marked (ISO 32000-2, 14.7.1, Table 353), Trapped is /Trapped of Info (14.11.6,
    // Table 349) - B204: PdfTool wrote Trapped as Tagged; R7-5: MCP took a structure tree for Tagged
    std::vector<QByteArray> objects = H::createPages(1, "/MarkInfo << /Marked true >> /StructTreeRoot 4 0 R");
    objects.push_back("<< /Type /StructTreeRoot >>");
    objects.push_back("<< /Title (Report) /Author (Jane) /CreationDate (D:20260101120000Z) /Trapped /True /Department (Sales) /Note (A\nB) >>");
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects, "/Info 5 0 R"), &isRead);
    QVERIFY(isRead);

    const pdf::PDFDocumentReportBasicInformation information = pdf::PDFDocumentReport::getBasicInformation(&document);
    QVERIFY(information.isTagged);
    QVERIFY(information.hasStructureTree);
    QCOMPARE(information.trapped, QByteArray("True"));
    QCOMPARE(information.formType, QStringLiteral("None"));
    QCOMPARE(information.pageCount, pdf::PDFInteger(1));
    QCOMPARE(information.paperFormat, QStringLiteral("A4"));
    QVERIFY(qAbs(information.firstPageSizeMM.width() - 595.0 * 25.4 / 72.0) < 0.01);
    QVERIFY(!information.isEncrypted);
    QCOMPARE(information.permissions.size(), size_t(8));
    for (const auto& permission : information.permissions)
    {
        QVERIFY(permission.second);
    }

    // Table 349 order: the standard keys first, then the custom ones; a line break makes a value shortened
    QCOMPARE(information.info.size(), size_t(5));
    QCOMPARE(information.info[0].key, QByteArray("Title"));
    QCOMPARE(information.info[0].value, QStringLiteral("Report"));
    QCOMPARE(information.info[1].key, QByteArray("Author"));
    QCOMPARE(information.info[2].key, QByteArray("CreationDate"));
    QCOMPARE(information.info[2].dateTime.toUTC(), QDateTime(QDate(2026, 1, 1), QTime(12, 0), QTimeZone::UTC));
    const pdf::PDFDocumentReportBasicInformation::InfoEntry* department = information.findInfo("Department");
    QVERIFY(department && department->isCustom && department->value == QStringLiteral("Sales") && !department->isShortened);
    const pdf::PDFDocumentReportBasicInformation::InfoEntry* note = information.findInfo("Note");
    QVERIFY(note && note->isShortened);
    QCOMPARE(note->value, QStringLiteral("A B"));

    // A structure tree without /MarkInfo /Marked is not a tagged document; /Trapped false of PDF 1.3
    std::vector<QByteArray> untagged = H::createPages(1, "/StructTreeRoot 4 0 R");
    untagged.push_back("<< /Type /StructTreeRoot >>");
    untagged.push_back("<< /Trapped false >>");
    const pdf::PDFDocument untaggedDocument = H::read(H::createPdf(untagged, "/Info 5 0 R"), &isRead);
    QVERIFY(isRead);
    const pdf::PDFDocumentReportBasicInformation untaggedInformation = pdf::PDFDocumentReport::getBasicInformation(&untaggedDocument);
    QVERIFY(!untaggedInformation.isTagged);
    QVERIFY(untaggedInformation.hasStructureTree);
    QCOMPARE(untaggedInformation.trapped, QByteArray("False"));

    // /Marked false, no Info: not tagged, Trapped absent (its default is Unknown, written by the callers)
    const pdf::PDFDocument plain = H::read(H::createPdf(H::createPages(1, "/MarkInfo << /Marked false >>")), &isRead);
    QVERIFY(isRead);
    const pdf::PDFDocumentReportBasicInformation plainInformation = pdf::PDFDocumentReport::getBasicInformation(&plain);
    QVERIFY(!plainInformation.isTagged);
    QVERIFY(!plainInformation.hasStructureTree);
    QVERIFY(plainInformation.trapped.isEmpty());
    QVERIFY(plainInformation.info.empty());
    QVERIFY(pdf::PDFDocumentReport::getBasicInformation(nullptr).info.empty());
}

void DocumentReportTest::emptySectionReasons()
{
    using H = DocumentReportTestHelper;
    using Section = pdf::PDFDocumentReportSection;

    // 484:20505, FD-8: a section of nothing is Disabled with its reason; FD-2: no hashes of unsaved changes
    bool isRead = false;
    const pdf::PDFDocument plain = H::read(H::createPdf(H::createPages(1, QByteArray())), &isRead);
    QVERIFY(isRead);
    QVERIFY(!pdf::PDFDocumentReport::getEmptySectionReason(&plain, Section::StructureTree, true).isEmpty());
    QVERIFY(!pdf::PDFDocumentReport::getEmptySectionReason(&plain, Section::Metadata, true).isEmpty());
    QVERIFY(!pdf::PDFDocumentReport::getEmptySectionReason(&plain, Section::NamedDestinations, true).isEmpty());
    QVERIFY(!pdf::PDFDocumentReport::getEmptySectionReason(&plain, Section::FileHashes, false).isEmpty());
    QVERIFY(pdf::PDFDocumentReport::getEmptySectionReason(&plain, Section::FileHashes, true).isEmpty());
    QVERIFY(pdf::PDFDocumentReport::getEmptySectionReason(&plain, Section::PageBoxes, true).isEmpty());

    std::vector<QByteArray> objects = H::createPages(1, "/StructTreeRoot 4 0 R /Dests << /a [3 0 R /Fit] >>");
    objects.push_back("<< /Type /StructTreeRoot >>");
    const pdf::PDFDocument tagged = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);
    QVERIFY(pdf::PDFDocumentReport::getEmptySectionReason(&tagged, Section::StructureTree, true).isEmpty());
    QVERIFY(pdf::PDFDocumentReport::getEmptySectionReason(&tagged, Section::NamedDestinations, true).isEmpty());
}

void DocumentReportTest::javaScriptOpenActionAndNext()
{
    using H = DocumentReportTestHelper;

    // B201: /OpenAction (12.3.3); B202: /Next is a dictionary or an array performed in their order (12.6.2),
    // the chain has a cycle back to its first action (one visited set - entered once, never a hang)
    std::vector<QByteArray> objects = H::createPages(1, "/OpenAction 4 0 R");
    objects.push_back(H::javaScript("first()", "/Next [5 0 R 6 0 R]"));
    objects.push_back(H::javaScript("second()", "/Next 4 0 R"));
    objects.push_back("<< /S /URI /URI (https://example.com) /Next 7 0 R >>");
    objects.push_back(H::javaScript("third()"));
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    const pdf::PDFJavaScriptScanner scanner(&document);
    const pdf::PDFJavaScriptScanner::Result result = scanner.scanDocument({ 0 }, pdf::PDFJavaScriptScanner::ScanMask, nullptr);
    QCOMPARE(H::getScripts(result.entries), QStringList({ QStringLiteral("first()"), QStringLiteral("second()"), QStringLiteral("third()") }));
    for (const pdf::PDFJavaScriptEntry& entry : result.entries)
    {
        QCOMPARE(entry.type, pdf::PDFJavaScriptEntry::Type::Document);
        QCOMPARE(entry.trigger, QStringLiteral("OpenAction"));
    }
    QCOMPARE(result.entries.front().actionReference, pdf::PDFObjectReference(4, 0));
    QVERIFY(!result.isLimitReached);
    QVERIFY(scanner.hasJavaScript());

    // An /OpenAction, which is a destination (an array), is no action
    std::vector<QByteArray> destination = H::createPages(1, "/OpenAction [3 0 R /Fit]");
    const pdf::PDFDocument destinationDocument = H::read(H::createPdf(destination), &isRead);
    QVERIFY(isRead);
    QVERIFY(!pdf::PDFJavaScriptScanner(&destinationDocument).hasJavaScript());
}

void DocumentReportTest::javaScriptSharedAction()
{
    using H = DocumentReportTestHelper;

    // One action object reached by two paths: the tail of a chain /Next of a link on page 1 and the action
    // of a link on page 2 - each action is read once in the whole scan (by its first path, page 1); an
    // annotation listed by two pages is read once too
    std::vector<QByteArray> objects = H::createPages(2, QByteArray(), "/Annots [6 0 R]");
    objects[3] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Annots [6 0 R 8 0 R] >>";
    objects.push_back(H::javaScript("shared()"));
    objects.push_back("<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /A 7 0 R >>");
    objects.push_back(H::javaScript("head()", "/Next 5 0 R"));
    objects.push_back("<< /Type /Annot /Subtype /Link /Rect [20 0 30 10] /A 5 0 R >>");
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    const pdf::PDFJavaScriptScanner::Result result = pdf::PDFJavaScriptScanner(&document).scanDocument({ 0, 1 }, pdf::PDFJavaScriptScanner::ScanMask, nullptr);
    QCOMPARE(H::getScripts(result.entries), QStringList({ QStringLiteral("head()"), QStringLiteral("shared()") }));
    for (const pdf::PDFJavaScriptEntry& entry : result.entries)
    {
        QCOMPARE(entry.type, pdf::PDFJavaScriptEntry::Type::Annotation);
        QCOMPARE(entry.pageIndex, pdf::PDFInteger(0));
        QCOMPARE(entry.name, QStringLiteral("Link"));
        QCOMPARE(entry.trigger, QStringLiteral("A"));
    }

    // Only the second page: both of its links are read (the scan of a page selection)
    const pdf::PDFJavaScriptScanner::Result second = pdf::PDFJavaScriptScanner(&document).scanDocument({ 1 }, pdf::PDFJavaScriptScanner::ScanPage, nullptr);
    QCOMPARE(H::getScripts(second.entries), QStringList({ QStringLiteral("head()"), QStringLiteral("shared()") }));
    QCOMPARE(second.entries[0].pageIndex, pdf::PDFInteger(1));
}

void DocumentReportTest::javaScriptLongChain()
{
    using H = DocumentReportTestHelper;

    // R7-4: a chain of 100 000 actions by /Next: the iterative walk reaches its end without a stack overflow
    constexpr int CHAIN_LENGTH = 100000;
    std::vector<QByteArray> objects = H::createPages(1, "/OpenAction 4 0 R");
    for (int i = 0; i < CHAIN_LENGTH; ++i)
    {
        const int number = 4 + i;
        objects.push_back(i + 1 < CHAIN_LENGTH ? "<< /S /Named /N /NextPage /Next " + QByteArray::number(number + 1) + " 0 R >>" : H::javaScript("last()"));
    }
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    const pdf::PDFJavaScriptScanner::Result result = pdf::PDFJavaScriptScanner(&document).scanDocument({ }, pdf::PDFJavaScriptScanner::ScanDocument, nullptr);
    QCOMPARE(H::getScripts(result.entries), QStringList({ QStringLiteral("last()") }));
    QVERIFY(!result.isLimitReached);
}

void DocumentReportTest::javaScriptOutlineAndAdditionalActions()
{
    using H = DocumentReportTestHelper;

    // B203: the actions of the outline items (12.3.3, Table 151), the siblings in a cycle; /AA of the
    // catalog (Table 200), of a page (Table 198) and of a field (Table 199) with its fully qualified name
    std::vector<QByteArray> objects = H::createPages(1, "/Outlines 4 0 R /AA << /WC " + H::javaScript("close()") + " >> /AcroForm << /Fields [8 0 R] >>",
                                                     "/AA << /O " + H::javaScript("open()") + " >>");
    objects.push_back("<< /Type /Outlines /First 5 0 R >>");
    objects.push_back("<< /Title (Chapter) /A " + H::javaScript("outline()") + " /Next 6 0 R >>");
    objects.push_back("<< /Title (Second) /Next 5 0 R /First 7 0 R >>");
    objects.push_back("<< /Title (Child) /A " + H::javaScript("child()") + " >>");
    objects.push_back("<< /T (parent) /Kids [9 0 R] >>");
    objects.push_back("<< /T (child) /Parent 8 0 R /FT /Tx /AA << /K " + H::javaScript("keystroke()") + " >> >>");
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    const pdf::PDFJavaScriptScanner::Result result = pdf::PDFJavaScriptScanner(&document).scanDocument({ 0 }, pdf::PDFJavaScriptScanner::ScanMask, nullptr);
    QCOMPARE(H::getScripts(result.entries), QStringList({ QStringLiteral("close()"), QStringLiteral("keystroke()"), QStringLiteral("open()"), QStringLiteral("outline()"), QStringLiteral("child()") }));
    QCOMPARE(result.entries[0].type, pdf::PDFJavaScriptEntry::Type::Document);
    QCOMPARE(result.entries[0].trigger, QStringLiteral("WC"));
    QCOMPARE(result.entries[1].type, pdf::PDFJavaScriptEntry::Type::Form);
    QCOMPARE(result.entries[1].name, QStringLiteral("parent.child"));
    QCOMPARE(result.entries[1].trigger, QStringLiteral("K"));
    QCOMPARE(result.entries[2].type, pdf::PDFJavaScriptEntry::Type::Page);
    QCOMPARE(result.entries[2].trigger, QStringLiteral("O"));
    QCOMPARE(result.entries[3].type, pdf::PDFJavaScriptEntry::Type::Outline);
    QCOMPARE(result.entries[3].name, QStringLiteral("Chapter"));
    QCOMPARE(result.entries[4].name, QStringLiteral("Child"));
}

void DocumentReportTest::javaScriptXfa()
{
    using H = DocumentReportTestHelper;

    // R7-22 (4), SECURITY_TODO 61: the <script> of an XFA form is listed with its field and event
    const QByteArray xdp("<xdp:xdp xmlns:xdp=\"http://ns.adobe.com/xdp/\"><template xmlns=\"http://www.xfa.org/schema/xfa-template/3.3/\">"
                         "<subform name=\"form1\"><field name=\"f\"><event activity=\"click\"><script contentType=\"application/x-javascript\">app.alert(1)</script>"
                         "</event></field></subform></template></xdp:xdp>");
    std::vector<QByteArray> objects = H::createPages(1, "/AcroForm << /Fields [] /XFA 4 0 R >>");
    objects.push_back(H::stream(QByteArray(), xdp));
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    const pdf::PDFJavaScriptScanner::Result result = pdf::PDFJavaScriptScanner(&document).scanDocument({ }, pdf::PDFJavaScriptScanner::ScanXfa, nullptr);
    QVERIFY(result.hasXfa);
    QCOMPARE(result.entries.size(), size_t(1));
    QCOMPARE(result.entries[0].type, pdf::PDFJavaScriptEntry::Type::Xfa);
    QCOMPARE(result.entries[0].javaScript, QStringLiteral("app.alert(1)"));
    QCOMPARE(result.entries[0].name, QStringLiteral("f"));
    QCOMPARE(result.entries[0].trigger, QStringLiteral("click"));
}

void DocumentReportTest::namedDestinations()
{
    using H = DocumentReportTestHelper;

    // 12.3.2.2, Table 149; B189: a destination to an object, which is no page, is unresolved (never a page 0)
    std::vector<QByteArray> objects = H::createPages(2, "/Dests << /a [3 0 R /XYZ 10 20 2] /b [4 0 R /FitH 100] /lost [5 0 R /Fit] >>");
    objects.push_back("<< /Type /Annot /Subtype /Text /Rect [0 0 1 1] >>");
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    const std::vector<pdf::PDFDocumentReportDestination> all = pdf::PDFDocumentReport::getNamedDestinations(&document, { 0, 1 });
    QCOMPARE(all.size(), size_t(3));
    QCOMPARE(all[0].name, QStringLiteral("a"));
    QCOMPARE(all[0].pageIndex, pdf::PDFInteger(0));
    QCOMPARE(all[0].type, QStringLiteral("XYZ"));
    QCOMPARE(all[0].left.value_or(-1.0), 10.0);
    QCOMPARE(all[0].top.value_or(-1.0), 20.0);
    QCOMPARE(all[0].zoom.value_or(-1.0), 2.0);
    QVERIFY(!all[0].right.has_value());
    QCOMPARE(all[1].name, QStringLiteral("b"));
    QCOMPARE(all[1].pageIndex, pdf::PDFInteger(1));
    QCOMPARE(all[1].top.value_or(-1.0), 100.0);
    QVERIFY(!all[1].left.has_value());
    QCOMPARE(all[2].name, QStringLiteral("lost"));
    QCOMPARE(all[2].pageIndex, pdf::PDFInteger(-1));

    // A page selection lists the destinations of its pages; an unresolved one belongs to no page
    const std::vector<pdf::PDFDocumentReportDestination> second = pdf::PDFDocumentReport::getNamedDestinations(&document, { 1 });
    QCOMPARE(second.size(), size_t(1));
    QCOMPARE(second[0].name, QStringLiteral("b"));
}

void DocumentReportTest::pageBoxesUserUnit()
{
    using H = DocumentReportTestHelper;

    // 14.11.2, Table 31: the boxes in mm with /UserUnit 2 (a unit is 2/72 inch); the crop box defaults to the
    // media box, the bleed, trim and art boxes to the crop box; the pages with the same boxes are one group
    std::vector<QByteArray> objects = H::createPages(3, QByteArray(), QByteArray());
    objects[2] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 72 144] /CropBox [0 0 72 72] /UserUnit 2 >>";
    objects[3] = objects[2];
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    const std::vector<pdf::PDFDocumentReportPageBoxes> groups = pdf::PDFDocumentReport::getPageBoxes(&document, { 0, 1, 2 });
    QCOMPARE(groups.size(), size_t(2));
    QCOMPARE(groups[0].pageIndices, std::vector<pdf::PDFInteger>({ 0, 1 }));
    QCOMPARE(groups[0].userUnit, 2.0);
    QVERIFY(qAbs(groups[0].mediaBox.width() - 50.8) < 1e-6);
    QVERIFY(qAbs(groups[0].mediaBox.height() - 101.6) < 1e-6);
    QVERIFY(qAbs(groups[0].cropBox.height() - 50.8) < 1e-6);
    QVERIFY(qAbs(groups[0].trimBox.height() - 50.8) < 1e-6);
    QCOMPARE(groups[1].pageIndices, std::vector<pdf::PDFInteger>({ 2 }));
    QVERIFY(qAbs(groups[1].mediaBox.width() - 595.0 * 25.4 / 72.0) < 1e-6);
}

void DocumentReportTest::structureTree()
{
    using H = DocumentReportTestHelper;

    // 14.7.2: the elements of the tree with their level; /Pg of an ancestor holds for an element without
    // one (Table 355); a /K back to the parent (a cycle) ends; an integer of /K is a marked-content id (14.7.5)
    std::vector<QByteArray> objects = H::createPages(1, "/MarkInfo << /Marked true >> /StructTreeRoot 4 0 R");
    objects.push_back("<< /Type /StructTreeRoot /K 5 0 R >>");
    objects.push_back("<< /Type /StructElem /S /P /P 4 0 R /Pg 3 0 R /K [6 0 R 0] >>");
    objects.push_back("<< /Type /StructElem /S /Span /P 5 0 R /T (Name) /K [1 5 0 R] >>");
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    pdf::PDFDocumentReport::Options options;
    options.sections = { pdf::PDFDocumentReportSection::StructureTree };
    const QJsonObject data = pdf::PDFDocumentReport::createData(&document, options, nullptr, nullptr);
    const QJsonObject tree = data.value(QStringLiteral("structureTree")).toObject();
    QVERIFY(tree.value(QStringLiteral("present")).toBool());
    const QJsonArray elements = tree.value(QStringLiteral("elements")).toArray();
    QCOMPARE(elements.size(), 2);
    QCOMPARE(elements[0].toObject().value(QStringLiteral("type")).toString(), QStringLiteral("P"));
    QCOMPARE(elements[0].toObject().value(QStringLiteral("level")).toInt(), 1);
    QCOMPARE(elements[0].toObject().value(QStringLiteral("page")).toInt(), 1);
    QCOMPARE(elements[0].toObject().value(QStringLiteral("contentItems")).toInt(), 1);
    QCOMPARE(elements[1].toObject().value(QStringLiteral("type")).toString(), QStringLiteral("Span"));
    QCOMPARE(elements[1].toObject().value(QStringLiteral("level")).toInt(), 2);
    QCOMPARE(elements[1].toObject().value(QStringLiteral("page")).toInt(), 1);
    QCOMPARE(elements[1].toObject().value(QStringLiteral("title")).toString(), QStringLiteral("Name"));
    QCOMPARE(elements[1].toObject().value(QStringLiteral("contentItems")).toInt(), 1);
}

void DocumentReportTest::fontsSharedForm()
{
    using H = DocumentReportTestHelper;

    // K7.24: the fonts of a form drawn by two pages (a node shared by two paths), the form draws itself (a
    // cycle); K7.21: the name exactly as /BaseFont, the tag of the subset kept (9.9.2)
    std::vector<QByteArray> objects = H::createPages(2, QByteArray(), "/Resources << /XObject << /Fm 5 0 R >> >>");
    objects.push_back(H::stream("/Type /XObject /Subtype /Form /BBox [0 0 10 10] /Resources << /Font << /F1 6 0 R >> /XObject << /Self 5 0 R >> >>", "BT /F1 12 Tf ET"));
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /ABCDEF+Helvetica >>");
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    bool isLimitReached = true;
    const std::vector<pdf::PDFDocumentReportFont> fonts = pdf::PDFDocumentReport::getFonts(&document, { 0, 1 }, false, nullptr, &isLimitReached);
    QCOMPARE(fonts.size(), size_t(1));
    QCOMPARE(fonts[0].name, QStringLiteral("ABCDEF+Helvetica"));
    QVERIFY(fonts[0].isSubset);
    QVERIFY(!fonts[0].isEmbedded);
    QCOMPARE(fonts[0].fontType, pdf::FontType::Type1);
    QCOMPARE(fonts[0].reference, pdf::PDFObjectReference(6, 0));
    QCOMPARE(fonts[0].pageIndices, std::vector<pdf::PDFInteger>({ 0, 1 }));
    QVERIFY(!isLimitReached);

    // Only the second page
    const std::vector<pdf::PDFDocumentReportFont> second = pdf::PDFDocumentReport::getFonts(&document, { 1 }, false, nullptr);
    QCOMPARE(second.size(), size_t(1));
    QCOMPARE(second[0].pageIndices, std::vector<pdf::PDFInteger>({ 1 }));
}

void DocumentReportTest::metadataConformance()
{
    using H = DocumentReportTestHelper;

    // ISO 19005 (pdfaid:part 2, conformance B = PDF/A-2b) and ISO 14289 (pdfuaid:part 1) of the XMP
    const QByteArray xmp("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                         "<rdf:Description rdf:about=\"\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\" pdfaid:part=\"2\" pdfaid:conformance=\"B\"/>"
                         "<rdf:Description rdf:about=\"\" xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\"><pdfuaid:part>1</pdfuaid:part></rdf:Description>"
                         "</rdf:RDF></x:xmpmeta>");
    std::vector<QByteArray> objects = H::createPages(1, "/Metadata 4 0 R");
    objects.push_back(H::stream("/Type /Metadata /Subtype /XML", xmp));
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    bool hasMetadata = false;
    QString errorMessage;
    QCOMPARE(pdf::PDFDocumentReport::getMetadata(&document, &hasMetadata, &errorMessage), QString::fromUtf8(xmp));
    QVERIFY(hasMetadata);
    QVERIFY(errorMessage.isEmpty());
    QCOMPARE(pdf::PDFDocumentReport::getBasicInformation(&document).conformance, QStringList({ QStringLiteral("PDF/A-2b"), QStringLiteral("PDF/UA-1") }));

    // A document type definition is never read (no entities are expanded)
    QVERIFY(pdf::PDFDocumentReport::getConformance(QStringLiteral("<!DOCTYPE x [<!ENTITY a \"2\">]><x xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\" pdfaid:part=\"&a;\"/>")).isEmpty());
}

void DocumentReportTest::xmlExport()
{
    using H = DocumentReportTestHelper;

    // 484:18196: the four options; a control character of a string (U+0001, not allowed by XML 1.0) is
    // replaced, the file is always well-formed; the generator is the library (K7.21)
    std::vector<QByteArray> objects = H::createPages(1, QByteArray(), "/Contents 4 0 R");
    objects.push_back(H::stream(QByteArray(), "BT (Hello) Tj ET"));
    objects.push_back("<< /Title <FEFF004100010042> >>");
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects, "/Info 5 0 R"), &isRead);
    QVERIFY(isRead);

    auto write = [&document](const pdf::PDFDocumentXmlExport::Options& options, QByteArray* xml)
    {
        QBuffer buffer(xml);
        buffer.open(QIODevice::WriteOnly);
        QString errorMessage;
        return pdf::PDFDocumentXmlExport::write(&document, options, &buffer, nullptr, nullptr, &errorMessage);
    };

    QByteArray xml;
    pdf::PDFDocumentXmlExport::Options options;
    QVERIFY(write(options, &xml));
    QString errorMessage;
    QVERIFY2(H::isWellFormed(xml, &errorMessage), qPrintable(errorMessage));
    QVERIFY(xml.contains("Processed by PDF4QT"));
    QVERIFY(xml.contains("<trailer>"));
    QVERIFY(xml.contains("<pdfobject id=\"4\" gen=\"0\">"));
    QVERIFY(QString::fromUtf8(xml).contains(QStringLiteral("A�B")));
    QVERIFY(!xml.contains("<data>"));
    QVERIFY(!xml.contains("<text>"));
    QVERIFY(xml.contains("\n  "));

    // The data of the streams as hexadecimal, as a text; no indentation
    QByteArray streams;
    options.isStreamExported = true;
    options.isStreamWrittenAsText = true;
    options.isIndented = false;
    QVERIFY(write(options, &streams));
    QVERIFY2(H::isWellFormed(streams, &errorMessage), qPrintable(errorMessage));
    QVERIFY(streams.contains("<data>" + QByteArray("BT (Hello) Tj ET").toHex().toUpper() + "</data>"));
    QVERIFY(streams.contains("<text>BT (Hello) Tj ET</text>"));
    QVERIFY(!streams.contains("\n  "));

    // Every string as hexadecimal
    QByteArray binary;
    pdf::PDFDocumentXmlExport::Options binaryOptions;
    binaryOptions.isStringAlwaysBinary = true;
    QVERIFY(write(binaryOptions, &binary));
    QVERIFY(binary.contains("<string form=\"binary\">FEFF004100010042</string>"));

    QVERIFY(pdf::PDFDocumentXmlExport::estimateSize(&document, options) > pdf::PDFDocumentXmlExport::estimateSize(&document, binaryOptions));
}

void DocumentReportTest::reportTool()
{
    using H = DocumentReportTestHelper;

    // A script of the document is a text of the report, never markup (6.2: "</script>" escaped); FD-2: the
    // hashes of the bytes of the file; the run is an analysis with one file (484:18811)
    std::vector<QByteArray> objects = H::createPages(1, "/OpenAction " + H::javaScript("</script><script>alert(1)</script>"));
    const QByteArray data = H::createPdf(objects);
    bool isRead = false;
    const pdf::PDFDocument document = H::read(data, &isRead);
    QVERIFY(isRead);

    const pdf::PDFDocumentToolReport tool;
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), data,
                                                                                                                               QStringLiteral("Doc.pdf"), false));
    QVariantMap parameters;
    parameters.insert(QStringLiteral("fileHashes"), true);
    parameters.insert(QStringLiteral("format"), QStringLiteral("html"));
    const pdf::PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.outputs.size(), size_t(1));
    QCOMPARE(result.outputs[0].suggestedFileName, QStringLiteral("Doc - report.html"));
    QCOMPARE(result.data.value(QStringLiteral("kind")).toString(), QStringLiteral("report"));

    // The six sections of 484:15472 and the hashes asked for
    const QJsonObject sections = result.data.value(QStringLiteral("sections")).toObject();
    QCOMPARE(sections.keys(), QStringList({ QStringLiteral("basicInformation"), QStringLiteral("fileHashes"), QStringLiteral("javascript"), QStringLiteral("metadata"),
                                            QStringLiteral("namedDestinations"), QStringLiteral("pageBoxes"), QStringLiteral("structureTree") }));
    const QJsonArray hashes = sections.value(QStringLiteral("fileHashes")).toObject().value(QStringLiteral("hashes")).toArray();
    QCOMPARE(hashes.size(), 5);
    QCOMPARE(hashes[0].toObject().value(QStringLiteral("hash")).toString(), QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex().toUpper()));

    const QByteArray html = result.outputs[0].data;
    QVERIFY(!html.contains("</script><script>"));
    QVERIFY(html.contains("&lt;/script&gt;&lt;script&gt;alert(1)&lt;/script&gt;"));

    // The export of the data of the kept result (the GUI, never a new run): XML well-formed, text with the code
    QString errorMessage;
    QVariantMap xmlParameters = parameters;
    xmlParameters.insert(QStringLiteral("format"), QStringLiteral("xml"));
    const pdf::PDFDocumentToolOutputFile xml = pdf::PDFDocumentToolReport::writeOutput(result.data, xmlParameters, pdf::PDFDocumentToolTableWriter::Options(), QStringLiteral("Doc"), &errorMessage);
    QCOMPARE(xml.suggestedFileName, QStringLiteral("Doc.xml"));
    QVERIFY2(H::isWellFormed(xml.data, &errorMessage), qPrintable(errorMessage));
    QVariantMap textParameters = parameters;
    textParameters.insert(QStringLiteral("format"), QStringLiteral("text"));
    const pdf::PDFDocumentToolOutputFile text = pdf::PDFDocumentToolReport::writeOutput(result.data, textParameters, pdf::PDFDocumentToolTableWriter::Options(), QStringLiteral("Doc"), &errorMessage);
    QVERIFY(text.data.contains("</script><script>alert(1)</script>"));

    // Unsaved changes have no hashes (FD-2); no section is no report (484:20937)
    const pdf::PDFDocumentToolContext modified = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), data,
                                                                                                                                QStringLiteral("Doc.pdf"), true));
    const pdf::PDFDocumentToolResult modifiedResult = tool.run(modified, parameters, nullptr, nullptr);
    QVERIFY(modifiedResult.isSuccess());
    QVERIFY(!modifiedResult.data.value(QStringLiteral("sections")).toObject().value(QStringLiteral("fileHashes")).toObject().value(QStringLiteral("available")).toBool());

    QVariantMap nothing;
    for (const pdf::PDFDocumentReportSection section : pdf::PDFDocumentReport::getSections())
    {
        nothing.insert(pdf::PDFDocumentReport::getSectionId(section), false);
    }
    QCOMPARE(tool.run(context, nothing, nullptr, nullptr).status, pdf::PDFDocumentToolResultStatus::Failed);
}

void DocumentReportTest::structureXmlTool()
{
    using H = DocumentReportTestHelper;

    // Structure as XML: streamed into a temporary file (plan 4.4), the preview in the data
    std::vector<QByteArray> objects = H::createPages(2, QByteArray());
    const QByteArray data = H::createPdf(objects);
    bool isRead = false;
    const pdf::PDFDocument document = H::read(data, &isRead);
    QVERIFY(isRead);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), data,
                                                                                                                         QStringLiteral("Doc.pdf"), false));
    context.temporaryFolder = directory.path();
    QVariantMap parameters;
    parameters.insert(QStringLiteral("kind"), QStringLiteral("structure-xml"));
    const pdf::PDFDocumentToolResult result = pdf::PDFDocumentToolReport().run(context, parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.outputs.size(), size_t(1));
    QVERIFY(result.outputs[0].isTemporaryFile());
    QCOMPARE(result.outputs[0].suggestedFileName, QStringLiteral("Doc - structure.xml"));

    QFile file(result.outputs[0].getFilePath());
    QVERIFY(file.open(QFile::ReadOnly));
    const QByteArray xml = file.readAll();
    QString errorMessage;
    QVERIFY2(H::isWellFormed(xml, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(result.data.value(QStringLiteral("text")).toString(), QString::fromUtf8(xml));
    QCOMPARE(result.data.value(QStringLiteral("size")).toInteger(), qint64(xml.size()));

    // The XML is written by the run from the document, never from the data of a result
    QVERIFY(pdf::PDFDocumentToolReport::writeOutput(result.data, parameters, pdf::PDFDocumentToolTableWriter::Options(), QStringLiteral("Doc"), &errorMessage).suggestedFileName.isEmpty());
}

void DocumentReportTest::pdfToolInfo()
{
#ifndef PDF4QT_TEST_PDFTOOL
    QSKIP("PdfTool is not built.");
#else
    using H = DocumentReportTestHelper;

    // B176 / B204: Trapped under its own key, Tagged by /MarkInfo /Marked (14.7.1)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    std::vector<QByteArray> objects = H::createPages(1, "/StructTreeRoot 4 0 R");
    objects.push_back("<< /Type /StructTreeRoot >>");
    objects.push_back("<< /Trapped /True >>");
    const QString path = directory.filePath(QStringLiteral("Info.pdf"));
    QFile file(path);
    QVERIFY(file.open(QFile::WriteOnly));
    file.write(H::createPdf(objects, "/Info 5 0 R"));
    file.close();

    QByteArray output;
    QByteArray errorOutput;
    QCOMPARE(H::runPdfTool({ QStringLiteral("info"), path, QStringLiteral("--console-format"), QStringLiteral("xml") }, &output, &errorOutput), 0);
    QCOMPARE(H::getRowValue(output, QStringLiteral("trapped")), QStringLiteral("Yes"));
    QCOMPARE(H::getRowValue(output, QStringLiteral("tagged")), QStringLiteral("No"));
    QCOMPARE(H::countRows(output, QStringLiteral("version")), 1);
#endif
}

void DocumentReportTest::pdfToolPageRange()
{
#ifndef PDF4QT_TEST_PDFTOOL
    QSKIP("PdfTool is not built.");
#else
    using H = DocumentReportTestHelper;

    // B164: a page outside of the document and a huge range are errors of the arguments (they ended the
    // process or allocated billions of items); B180: an error goes to the standard error output
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("Pages.pdf"));
    QFile file(path);
    QVERIFY(file.open(QFile::WriteOnly));
    file.write(H::createPdf(H::createPages(2, QByteArray())));
    file.close();

    constexpr int ERROR_INVALID_ARGUMENTS = 7;
    for (const QString& selection : { QStringLiteral("999"), QStringLiteral("1-99999999999") })
    {
        QByteArray output;
        QByteArray errorOutput;
        QCOMPARE(H::runPdfTool({ QStringLiteral("info-page-boxes"), path, QStringLiteral("--page-select"), selection }, &output, &errorOutput), ERROR_INVALID_ARGUMENTS);
        QVERIFY(output.isEmpty());
        QVERIFY(!errorOutput.isEmpty());
    }

    QByteArray output;
    QByteArray errorOutput;
    QCOMPARE(H::runPdfTool({ QStringLiteral("info-page-boxes"), path, QStringLiteral("--page-select"), QStringLiteral("2") }, &output, &errorOutput), 0);
    QVERIFY(output.contains("Pages 2"));
    QVERIFY(!output.contains("Pages 1"));
#endif
}

void DocumentReportTest::pdfToolTextCodec()
{
#ifndef PDF4QT_TEST_PDFTOOL
    QSKIP("PdfTool is not built.");
#else
    using H = DocumentReportTestHelper;

    // B178: the default --text-codec UTF-8 writes UTF-8 into a redirected output (it was not recognized)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString title = QStringLiteral("Příliš žluťoučký kůň");
    QByteArray titleBytes("\xFE\xFF");
    for (const QChar character : title)
    {
        titleBytes.append(char(character.unicode() >> 8));
        titleBytes.append(char(character.unicode() & 0xFF));
    }
    std::vector<QByteArray> objects = H::createPages(1, QByteArray());
    objects.push_back("<< /Title <" + titleBytes.toHex() + "> >>");
    const QString path = directory.filePath(QStringLiteral("Codec.pdf"));
    QFile file(path);
    QVERIFY(file.open(QFile::WriteOnly));
    file.write(H::createPdf(objects, "/Info 4 0 R"));
    file.close();

    QByteArray output;
    QByteArray errorOutput;
    QCOMPARE(H::runPdfTool({ QStringLiteral("info"), path }, &output, &errorOutput), 0);
    QVERIFY(output.contains(title.toUtf8()));
#endif
}

void DocumentReportTest::pdfToolJavaScriptAndXml()
{
#ifndef PDF4QT_TEST_PDFTOOL
    QSKIP("PdfTool is not built.");
#else
    using H = DocumentReportTestHelper;

    // R7-5: info-javascript, info-dests and xml are the implementation of the core - the script of a chain
    // /Next (B202), an unresolved destination (B189), the streams decoded as text (Q7.18)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    std::vector<QByteArray> objects = H::createPages(1, "/OpenAction " + H::javaScript("first()", "/Next " + H::javaScript("second()")) + " /Dests << /lost [5 0 R /Fit] >>",
                                                     "/Contents 4 0 R");
    objects.push_back(H::stream(QByteArray(), "BT (Hello) Tj ET"));
    objects.push_back("<< /Type /Annot /Subtype /Text /Rect [0 0 1 1] >>");
    const QString path = directory.filePath(QStringLiteral("Scripts.pdf"));
    QFile file(path);
    QVERIFY(file.open(QFile::WriteOnly));
    file.write(H::createPdf(objects));
    file.close();

    QByteArray output;
    QByteArray errorOutput;
    QCOMPARE(H::runPdfTool({ QStringLiteral("info-javascript"), path }, &output, &errorOutput), 0);
    QVERIFY(output.contains("first()"));
    QVERIFY(output.contains("second()"));

    QCOMPARE(H::runPdfTool({ QStringLiteral("info-dests"), path }, &output, &errorOutput), 0);
    QVERIFY(output.contains("unresolved"));
    QVERIFY(output.contains("lost"));

    QCOMPARE(H::runPdfTool({ QStringLiteral("xml"), path, QStringLiteral("--xml-export-streams-as-text") }, &output, &errorOutput), 0);
    QString errorMessage;
    QVERIFY2(H::isWellFormed(output, &errorMessage), qPrintable(errorMessage));
    QVERIFY(output.contains("<text>BT (Hello) Tj ET</text>"));
    QVERIFY(output.contains("Processed by PDF4QT"));
#endif
}

void DocumentReportTest::javaScriptSharedLongStream()
{
    using H = DocumentReportTestHelper;

    // Review RS7CORE: 40 links run one script stream of 2 Mi characters (12.6.4.17, Table 221: /JS a text
    // stream). Every link is listed (a script is never hidden, S-62), a script keeps at most
    // MAXIMUM_SCRIPT_LENGTH characters and all texts together at most 16 such scripts: without the budget
    // 100 000 links of one stream were 200 GB. The text of a decoded stream is a prefix of the stream.
    constexpr int linkCount = 40;
    constexpr qint64 scriptLength = pdf::PDFJavaScriptScanner::MAXIMUM_SCRIPT_LENGTH;
    QByteArray annotations;
    for (int i = 0; i < linkCount; ++i)
    {
        annotations += QByteArray::number(4 + i) + " 0 R ";
    }
    std::vector<QByteArray> objects = H::createPages(1, QByteArray(), "/Annots [" + annotations + "]");
    for (int i = 0; i < linkCount; ++i)
    {
        objects.push_back("<< /Type /Annot /Subtype /Link /Rect [0 0 1 1] /A << /S /JavaScript /JS " + QByteArray::number(4 + linkCount) + " 0 R >> >>");
    }

    // qCompress writes the size and then a zlib stream (RFC 1950, the data of /FlateDecode, 7.4.4)
    const QByteArray script(2 * scriptLength, 'a');
    const QByteArray compressed = qCompress(script).mid(4);
    objects.push_back("<< /Filter /FlateDecode /Length " + QByteArray::number(compressed.size()) + " >>\nstream\n" + compressed + "\nendstream");
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    const pdf::PDFJavaScriptScanner::Result result = pdf::PDFJavaScriptScanner(&document).scanDocument({ 0 }, pdf::PDFJavaScriptScanner::ScanMask, nullptr);
    QCOMPARE(result.entries.size(), size_t(linkCount));

    qint64 totalLength = 0;
    for (const pdf::PDFJavaScriptEntry& entry : result.entries)
    {
        QVERIFY(entry.isTruncated);
        QVERIFY(entry.javaScript.size() <= scriptLength);
        QVERIFY(QStringView(entry.javaScript).toLatin1() == script.left(entry.javaScript.size()));
        totalLength += entry.javaScript.size();
    }
    QCOMPARE(qint64(result.entries.front().javaScript.size()), scriptLength);
    QVERIFY(totalLength <= 16 * scriptLength);
    QVERIFY(result.entries.back().javaScript.isEmpty());
}

void DocumentReportTest::fontsManyForms()
{
    using H = DocumentReportTestHelper;

    // Review RS7CORE: 20 000 forms of the pages 1 and 3 share one /Font dictionary; the page 2 has its own
    // font. The pages of a font are the pages, from which it is reached (7.8.3): Helvetica {1, 3}, Courier
    // {2}. The pages of each node were found by a walk backwards over the whole graph (20 000 walks of
    // 20 000 nodes), the pages were inserted once per owner and font (N * K * P numbers).
    constexpr int formCount = 20000;
    std::vector<QByteArray> objects = H::createPages(3, QByteArray());
    objects[2] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources 6 0 R >>";
    objects[3] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F 9 0 R >> >> >>";
    objects[4] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources 6 0 R >>";
    QByteArray xObjects;
    for (int i = 0; i < formCount; ++i)
    {
        xObjects += "/X" + QByteArray::number(i) + " " + QByteArray::number(10 + i) + " 0 R ";
    }
    objects.push_back("<< /XObject << " + xObjects + " >> >>");
    objects.push_back("<< /F1 8 0 R >>");
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>");
    for (int i = 0; i < formCount; ++i)
    {
        objects.push_back(H::stream("/Type /XObject /Subtype /Form /BBox [0 0 1 1] /Resources << /Font 7 0 R >>", "BT /F1 1 Tf ET"));
    }
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    QElapsedTimer timer;
    timer.start();
    bool isLimitReached = true;
    const std::vector<pdf::PDFDocumentReportFont> fonts = pdf::PDFDocumentReport::getFonts(&document, { 0, 1, 2 }, false, nullptr, &isLimitReached);
    QVERIFY(timer.elapsed() < 5000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR")));
    QVERIFY(!isLimitReached);
    QCOMPARE(fonts.size(), size_t(2));
    QCOMPARE(fonts[0].name, QStringLiteral("Courier"));
    QCOMPARE(fonts[0].pageIndices, std::vector<pdf::PDFInteger>({ 1 }));
    QCOMPARE(fonts[1].name, QStringLiteral("Helvetica"));
    QCOMPARE(fonts[1].pageIndices, std::vector<pdf::PDFInteger>({ 0, 2 }));
}

void DocumentReportTest::structureXmlPermission()
{
    // Q7.2: the data of the streams in Structure as XML are content extracted from the document (ISO
    // 32000-2, Table 22, bit 5) - as hexadecimal digits (exportStreams) and as their decoded text
    // (streamsAsText), each alone; the objects without the streams are read only
    using Permission = pdf::PDFSecurityHandler::Permission;
    using Permissions = std::vector<Permission>;
    const pdf::PDFDocumentToolReport report;
    const QString kind = QStringLiteral("kind");
    const QString structure = QStringLiteral("structure-xml");
    QCOMPARE(report.getRequiredPermissions(QVariantMap({ { kind, structure }, { QStringLiteral("streamsAsText"), true } })), Permissions({ Permission::CopyContent }));
    QCOMPARE(report.getRequiredPermissions(QVariantMap({ { kind, structure }, { QStringLiteral("exportStreams"), true }, { QStringLiteral("streamsAsText"), true } })),
             Permissions({ Permission::CopyContent }));
    QVERIFY(report.getRequiredPermissions(QVariantMap({ { kind, structure }, { QStringLiteral("exportStreams"), false }, { QStringLiteral("streamsAsText"), false } })).empty());
}

void DocumentReportTest::pdfToolContentPermission()
{
#ifndef PDF4QT_TEST_PDFTOOL
    QSKIP("PdfTool is not built.");
#else
    using H = DocumentReportTestHelper;

    // B166, Q7.2: a document, whose permissions do not allow the extraction of its content (ISO 32000-2,
    // Table 22, bit 5), opened by its user password gives no scripts, no XMP and no data of the streams
    // (exit code 9, ErrorPermissions, as fetch-text); its objects without the streams are written; the
    // owner password allows everything (7.6.4.2)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    std::vector<QByteArray> objects = H::createPages(1, "/OpenAction " + H::javaScript("secret()") + " /Metadata 5 0 R", "/Contents 4 0 R");
    objects.push_back(H::stream(QByteArray(), "BT (Hello) Tj ET"));
    objects.push_back(H::stream("/Type /Metadata /Subtype /XML", "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"/>"));
    bool isRead = false;
    const pdf::PDFDocument plain = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    pdf::PDFDocumentBuilder builder(&plain);
    pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
    settings.ownerPassword = QStringLiteral("owner-secret");
    settings.userPassword = QStringLiteral("user-secret");
    settings.permissions = 0xFFFFFFFCu & ~0x0010u;
    builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    pdf::PDFDocument encrypted = builder.build();
    const QString path = directory.filePath(QStringLiteral("Restricted.pdf"));
    QVERIFY(pdf::PDFDocumentWriter(nullptr).write(path, &encrypted, false));

    constexpr int errorPermissions = 9;
    const QStringList user = { QStringLiteral("--pswd"), QStringLiteral("user-secret") };
    QByteArray output;
    QByteArray errorOutput;
    QCOMPARE(H::runPdfTool(QStringList({ QStringLiteral("info-javascript"), path }) + user, &output, &errorOutput), errorPermissions);
    QVERIFY(!output.contains("secret()"));
    QCOMPARE(H::runPdfTool(QStringList({ QStringLiteral("info-metadata"), path }) + user, &output, &errorOutput), errorPermissions);
    QVERIFY(!output.contains("xmpmeta"));
    QCOMPARE(H::runPdfTool(QStringList({ QStringLiteral("xml"), path, QStringLiteral("--xml-export-streams-as-text") }) + user, &output, &errorOutput), errorPermissions);
    QVERIFY(!output.contains("Hello"));
    QCOMPARE(H::runPdfTool(QStringList({ QStringLiteral("xml"), path, QStringLiteral("--xml-export-streams") }) + user, &output, &errorOutput), errorPermissions);

    // The structure without the streams is no content
    QCOMPARE(H::runPdfTool(QStringList({ QStringLiteral("xml"), path }) + user, &output, &errorOutput), 0);
    QString errorMessage;
    QVERIFY2(H::isWellFormed(output, &errorMessage), qPrintable(errorMessage));

    // The owner password grants every permission (7.6.4.2)
    QCOMPARE(H::runPdfTool({ QStringLiteral("info-javascript"), path, QStringLiteral("--pswd"), QStringLiteral("owner-secret") }, &output, &errorOutput), 0);
    QVERIFY(output.contains("secret()"));
    QCOMPARE(H::runPdfTool({ QStringLiteral("xml"), path, QStringLiteral("--xml-export-streams-as-text"), QStringLiteral("--pswd"), QStringLiteral("owner-secret") }, &output, &errorOutput), 0);
    QVERIFY(output.contains("BT (Hello) Tj ET"));
#endif
}

void DocumentReportTest::pdfToolOwnerPasswordOfEmptyUserPassword()
{
#ifndef PDF4QT_TEST_PDFTOOL
    QSKIP("PdfTool is not built.");
#else
    using H = DocumentReportTestHelper;

    // RS7CORE: a file with an empty user password is opened without asking for a password (ISO 32000-2,
    // 7.6.4.4), so --pswd was never tried and its owner could not extract the content (Table 22, bit 5). --pswd
    // is verified as the owner password, which grants every permission (7.6.4.2); a wrong one does not
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    std::vector<QByteArray> objects = H::createPages(1, "/OpenAction " + H::javaScript("secret()") + " /Metadata 5 0 R", "/Contents 4 0 R");
    objects.push_back(H::stream(QByteArray(), "BT (Hello) Tj ET"));
    objects.push_back(H::stream("/Type /Metadata /Subtype /XML", "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"/>"));
    bool isRead = false;
    const pdf::PDFDocument plain = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    pdf::PDFDocumentBuilder builder(&plain);
    pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
    settings.ownerPassword = QStringLiteral("owner-secret");
    settings.userPassword = QString();
    settings.permissions = 0xFFFFFFFCu & ~0x0010u;
    builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    pdf::PDFDocument encrypted = builder.build();
    const QString path = directory.filePath(QStringLiteral("Restricted.pdf"));
    QVERIFY(pdf::PDFDocumentWriter(nullptr).write(path, &encrypted, false));

    constexpr int errorPermissions = 9;
    QByteArray output;
    QByteArray errorOutput;
    QCOMPARE(H::runPdfTool({ QStringLiteral("info-javascript"), path }, &output, &errorOutput), errorPermissions);
    QVERIFY(!output.contains("secret()"));
    QVERIFY(errorOutput.contains("--pswd"));
    QCOMPARE(H::runPdfTool({ QStringLiteral("info-javascript"), path, QStringLiteral("--pswd"), QStringLiteral("wrong") }, &output, &errorOutput), errorPermissions);
    QVERIFY(!output.contains("secret()"));

    const QStringList owner = { QStringLiteral("--pswd"), QStringLiteral("owner-secret") };
    QCOMPARE(H::runPdfTool(QStringList({ QStringLiteral("info-javascript"), path }) + owner, &output, &errorOutput), 0);
    QVERIFY(output.contains("secret()"));
    QCOMPARE(H::runPdfTool(QStringList({ QStringLiteral("info-metadata"), path }) + owner, &output, &errorOutput), 0);
    QVERIFY(output.contains("xmpmeta"));
    QCOMPARE(H::runPdfTool(QStringList({ QStringLiteral("xml"), path, QStringLiteral("--xml-export-streams-as-text") }) + owner, &output, &errorOutput), 0);
    QVERIFY(output.contains("BT (Hello) Tj ET"));
#endif
}

void DocumentReportTest::xmlRealsAndLargeStreams()
{
    using H = DocumentReportTestHelper;

    // RS7CORE: a real number of the XML reads back as the same value (ISO 32000-2, 7.3.3; six significant
    // digits lost it), and the hexadecimal digits of a stream of 2 MiB + 3 bytes, which are written by parts of
    // 64 KiB (not as one text of the whole stream, about ten times its size in the memory), are the digits of
    // every byte in their order - over the boundaries of the parts and the last short part
    std::vector<QByteArray> objects = H::createPages(1, QByteArray(), "/Contents 4 0 R");
    QByteArray content(2 * 1024 * 1024 + 3, Qt::Uninitialized);
    for (qsizetype i = 0; i < content.size(); ++i)
    {
        content[i] = char((i * 7 + 3) & 0xFF);
    }
    objects.push_back(H::stream(QByteArray(), content));
    objects.push_back("[0.123456789 3.14159265358979 100000.5 0.0000001 -2.718281828459045]");
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    QByteArray xml;
    QBuffer buffer(&xml);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    pdf::PDFDocumentXmlExport::Options options;
    options.isStreamExported = true;
    QString errorMessage;
    QVERIFY2(pdf::PDFDocumentXmlExport::write(&document, options, &buffer, nullptr, nullptr, &errorMessage), qPrintable(errorMessage));

    QStringList reals;
    QString data;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd())
    {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("real"))
        {
            reals << reader.readElementText();
        }
        else if (reader.isStartElement() && reader.name() == QLatin1String("data"))
        {
            data = reader.readElementText();
        }
    }
    QVERIFY2(!reader.hasError(), qPrintable(reader.errorString()));

    const pdf::PDFObject& array = document.getStorage().getObject(pdf::PDFObjectReference(5, 0));
    QVERIFY(array.isArray());
    QCOMPARE(reals.size(), qsizetype(array.getArray()->getCount()));
    for (qsizetype i = 0; i < reals.size(); ++i)
    {
        bool isNumber = false;
        QCOMPARE(QLocale::c().toDouble(reals[i], &isNumber), array.getArray()->getItem(size_t(i)).getReal());
        QVERIFY(isNumber);
    }

    QCOMPARE(data, QString::fromLatin1(content.toHex().toUpper()));
}

void DocumentReportTest::inksCanceled()
{
    using H = DocumentReportTestHelper;

    // RS7CORE: the inks of the report (PDFInkMapper) honor the cancellation - a /ColorSpace dictionary of 20 000
    // Separation spaces (8.6.6.4), each another spot ink, is not scanned after Stop
    constexpr int SPOT_COUNT = 20000;
    QByteArray colorSpaces;
    for (int i = 0; i < SPOT_COUNT; ++i)
    {
        const QByteArray number = QByteArray::number(i).rightJustified(5, '0');
        colorSpaces += "/C" + number + " [/Separation /S" + number + " /DeviceGray << /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>]\n";
    }
    const std::vector<QByteArray> objects = H::createPages(1, QByteArray(), "/Resources << /ColorSpace << " + colorSpaces + " >> >>");
    bool isRead = false;
    const pdf::PDFDocument document = H::read(H::createPdf(objects), &isRead);
    QVERIFY(isRead);

    // The process inks and the spot inks, which can be separations (476:11703: the first 26 of the walk)
    const std::vector<pdf::PDFDocumentReportInk> all = pdf::PDFDocumentReport::getInks(&document);
    QCOMPARE(all.size(), size_t(4 + pdf::PDFInkMapper::MAX_SPOT_COLOR_COMPONENTS));
    QCOMPARE(all.back().name, QByteArray("S00025"));

    // The scan asks the cancellation (at each color space of a dictionary): 20 000 polls at least
    DocumentReportTestCountingControl counting((std::numeric_limits<int>::max)());
    QCOMPARE(pdf::PDFDocumentReport::getInks(&document, &counting).size(), all.size());
    QVERIFY2(counting.getChecks() >= SPOT_COUNT, qPrintable(QString::number(counting.getChecks())));

    // Stopped before the scan: the process inks only (the inks found so far)
    DocumentReportTestCountingControl canceled(0);
    const std::vector<pdf::PDFDocumentReportInk> inks = pdf::PDFDocumentReport::getInks(&document, &canceled);
    QCOMPARE(inks.size(), size_t(4));
    QVERIFY(std::none_of(inks.cbegin(), inks.cend(), [](const pdf::PDFDocumentReportInk& ink) { return ink.isSpot; }));
}

void DocumentReportTest::factsInCLocale()
{
    using H = DocumentReportTestHelper;

    // The fix wave of step 7: the facts of the core are machine-readable (MCP, PdfTool) - the C locale, whatever
    // the default locale is (the GUI makes its own facts by PDFAppLocale)
    pdftest::TestLocaleGuard guard;
    QVERIFY(pdftest::TestLocaleGuard::isDefaultLocaleOtherThanC());

    const std::vector<QByteArray> objects = H::createPages(1, QByteArray());
    const QByteArray data = H::createPdf(objects);
    bool isRead = false;
    const pdf::PDFDocument document = H::read(data, &isRead);
    QVERIFY(isRead);

    const pdf::PDFDocumentToolReport tool;
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), data,
                                                                                                                               QStringLiteral("Doc.pdf"), false));
    QVariantMap parameters;
    parameters.insert(QStringLiteral("format"), QStringLiteral("html"));
    const pdf::PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.outputs.size(), size_t(1));

    const qint64 sectionCount = result.data.value(QStringLiteral("sections")).toObject().size();
    const qint64 size = result.outputs[0].data.size();
    QVERIFY(size > 1024);
    int checked = 0;
    for (const pdf::PDFDocumentToolFact& fact : result.facts)
    {
        if (fact.id == QLatin1String("sections"))
        {
            QCOMPARE(fact.value, QString::number(sectionCount));
            ++checked;
        }
        else if (fact.id == QLatin1String("size"))
        {
            QCOMPARE(fact.value, QLocale::c().formattedDataSize(size, 1, QLocale::DataSizeTraditionalFormat));
            QVERIFY(fact.value.contains(QLatin1Char('.')));
            ++checked;
        }
    }
    QCOMPARE(checked, 2);
}

QTEST_MAIN(DocumentReportTest)

#include "tst_documentreporttest.moc"
