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

#include "pdfcms.h"
#include "pdfcolorspaces.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfexecutionpolicy.h"
#include "pdffont.h"
#include "pdfocrpagepreparer.h"
#include "pdfoptionalcontent.h"
#include "pdfparser.h"
#include "pdfrenderer.h"
#include "pdfsecurityhandler.h"

#include <QtTest>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLocale>
#include <QPainter>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <map>

using namespace pdf;

class DocumentWriterTest : public QObject
{
    Q_OBJECT

private slots:
    void objectStreamValidation();
    void incrementalUpdateOfClassicTable();
    void incrementalUpdateOfCrossReferenceStream();
    void incrementalUpdateRejectsUnreadableOriginal();
    void namesWithNumberSignRoundTrip();
    void stringsWithCarriageReturn();
    void endOfLinesOfStringsRoundTrip();
    void literalStringsOfAContentStream_data();
    void literalStringsOfAContentStream();
    void encryptedStringsRoundTrip_data();
    void encryptedStringsRoundTrip();
    void oldFileWithRawCarriageReturn_data();
    void oldFileWithRawCarriageReturn();
    void damagedFileIsStillRestored();
    void damagedRevisionsLastOneWins_data();
    void damagedRevisionsLastOneWins();
    void damagedFileObjects_data();
    void damagedFileObjects();
    void damagedFileOldestContentWarning();
    void incrementalUpdateOfDamagedFiles();
    void damagedFileIsToldRestored();
    void damagedFileTrailers_data();
    void damagedFileTrailers();
    void damagedFileTrailerOnlyInObject();
    void damagedFileFallbackUsesFirstTrailer();
    void damagedFileNoDocumentInAnyRevision();
    void damagedFileAttackedByIncrementalUpdate_data();
    void damagedFileAttackedByIncrementalUpdate();
    void damagedFileHostileSizes_data();
    void damagedFileHostileSizes();
    void oldFileWithRawCarriageReturnInIdentifier();
    void oldEncryptedStringWithRawCarriageReturn_data();
    void oldEncryptedStringWithRawCarriageReturn();
    void trailerSizeOfTheWrittenTable();
    void damagedEncryptedFileIsDecrypted_data();
    void damagedEncryptedFileIsDecrypted();
    void damagedEncryptedRevisions_data();
    void damagedEncryptedRevisions();
    void damagedFileVersion_data();
    void damagedFileVersion();
    void invalidVersionNeverWritten();
    void realNumberText_data();
    void realNumberText();
    void realNumbersRoundTrip_data();
    void realNumbersRoundTrip();
    void realNumbersRandomRoundTrip();
    void realNumbersInStructures();
    void realNumbersNotFinite();
    void realNumberTextIndependentOfLocale();
    void longNumberOfFileKeepsItsValue();
    void fontMatrixSurvivesWholeWrite_data();
    void fontMatrixSurvivesWholeWrite();
    void incrementalUpdateKeepsNumbersOfOriginal();
    void ocrFingerprintOfExactNumbers();
    void writtenSizeBenchmark();

private:
    /// Document with a page of 1000 x 100 points, whose only content is a Type 3 glyph:
    /// a square of 1000 x 1000 units, scaled by the font matrix and by the font size
    static QByteArray createFontMatrixDocument(const QByteArray& fontMatrixValue, const QByteArray& fontSize);

    /// Document with a page of 1000 x 100 points showing the given operand of the operator Tj by
    /// a Type 3 font: the code 10 is a square of 100 x 100 points, the code 13 draws nothing
    static QByteArray createLineEndFontDocument(const QByteArray& operand);

    /// Writes a document protected by the passwords "user" and the given owner password
    static QByteArray writeEncrypted(PDFDocumentBuilder& builder, PDFSecurityHandlerFactory::Algorithm algorithm, const QString& ownerPassword, const QByteArray& id = QByteArray());

    /// Result of reading of a file by the given password
    static PDFDocument readWithPassword(const QByteArray& data, const QString& password, PDFDocumentReader::Result* result, bool permissive = false);

    /// File with one page, followed by the given objects, without a cross-reference table
    /// and with a wrong startxref: only the permissive reading opens it
    static QByteArray createDamagedFile(const QByteArray& objects);

    /// Text of an object: a string (text), a number, stream:data, null, a dictionary {key=value ...}
    static QByteArray describeObject(const PDFObject& object);

    /// The old writer wrote a string without parentheses and backslashes as a literal one, whatever bytes
    /// it has. Replaces the hexadecimal string at the position by such a literal string and spaces, so the
    /// offsets of the file stay. Returns false, when the old writer would have written it as hexadecimal.
    static bool replaceByOldLiteralString(QByteArray& data, qsizetype position);

    /// Returns the value of the hexadecimal string at the position
    static QByteArray getHexStringValue(const QByteArray& data, qsizetype position);

    /// Builds a document of the bodies of its objects: object numbers are their positions + 1,
    /// the first one is the catalog, a classic cross-reference table
    static QByteArray createDocument(const std::vector<QByteArray>& objects);

    /// Renders the page by 4 pixels per point
    static QImage renderPage(const PDFDocument& document);

    /// True, if the values are the same double, including the sign of a zero
    static bool isSameDouble(PDFReal first, PDFReal second);

    /// True, if the text is a real number of PDF Reference 1.7, chapter 3.2.2: no exponent, no other radix
    static bool isValidRealText(const QByteArray& text);

    static QByteArray write(const PDFDocument& document);
    static QByteArray writeIncrementalUpdate(const QByteArray& original, const PDFDocument& document);
    static PDFDocument read(const QByteArray& data);

    /// Builds a small document whose only cross-reference section is a stream,
    /// which the writer of the library never produces itself
    static QByteArray createCrossReferenceStreamDocument();
};

QByteArray DocumentWriterTest::write(const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    PDFDocumentWriter(nullptr).write(&buffer, &document);
    return buffer.data();
}

QByteArray DocumentWriterTest::writeIncrementalUpdate(const QByteArray& original, const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    if (!PDFDocumentWriter(nullptr).writeIncrementalUpdate(&buffer, original, &document))
    {
        return QByteArray();
    }
    return buffer.data();
}

PDFDocument DocumentWriterTest::read(const QByteArray& data)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    return reader.readFromBuffer(data);
}

QByteArray DocumentWriterTest::createCrossReferenceStreamDocument()
{
    QByteArray data("%PDF-1.5\n");
    std::vector<qsizetype> offsets = { 0 };

    auto addObject = [&](const QByteArray& object)
    {
        offsets.push_back(data.size());
        data.append(object);
    };

    addObject("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    addObject("2 0 obj\n<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>\nendobj\n");
    addObject("3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 300 400 ] >>\nendobj\n");

    // The cross-reference stream refers to itself, so its offset is the current size
    const qsizetype xrefOffset = data.size();
    offsets.push_back(xrefOffset);

    QByteArray entries;
    for (size_t i = 0; i < offsets.size(); ++i)
    {
        const quint32 offset = quint32(offsets[i]);
        entries.append(char(i == 0 ? 0 : 1));
        entries.append(char((offset >> 24) & 0xFF));
        entries.append(char((offset >> 16) & 0xFF));
        entries.append(char((offset >> 8) & 0xFF));
        entries.append(char(offset & 0xFF));
        entries.append(char(i == 0 ? 0xFF : 0));
        entries.append(char(i == 0 ? 0xFF : 0));
    }

    data.append("4 0 obj\n<< /Type /XRef /Size 5 /W [ 1 4 2 ] /Root 1 0 R /Length " + QByteArray::number(entries.size()) + " >>\nstream\n");
    data.append(entries);
    data.append("\nendstream\nendobj\n");
    data.append("startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n");
    return data;
}


void DocumentWriterTest::objectStreamValidation()
{
    auto makeDocument = [](PDFInteger count, PDFInteger first, PDFInteger relativeOffset)
    {
        QByteArray data("%PDF-1.5\n");
        std::array<qsizetype, 7> offsets{};
        auto addObject = [&](int number, const QByteArray& body)
        {
            offsets[number] = data.size();
            data += QByteArray::number(number) + " 0 obj\n" + body + "\nendobj\n";
        };
        addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
        addObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
        addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] >>");
        const QByteArray content = "4 " + QByteArray::number(relativeOffset) + " /VeryLongCompressedObjectName";
        addObject(5, "<< /Type /ObjStm /N " + QByteArray::number(count) + " /First " + QByteArray::number(first) +
                     " /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "\nendstream");
        offsets[6] = data.size();
        QByteArray entries;
        for (int i = 0; i < int(offsets.size()); ++i)
        {
            const quint32 offset = i == 4 ? 5 : quint32(offsets[i]);
            entries.append(char(i == 0 ? 0 : i == 4 ? 2 : 1));
            for (int shift : { 24, 16, 8, 0 })
            {
                entries.append(char((offset >> shift) & 0xFF));
            }
            entries.append(char(i == 0 ? 0xFF : 0));
            entries.append(char(i == 0 ? 0xFF : 0));
        }
        addObject(6, "<< /Type /XRef /Size 7 /W [1 4 2] /Root 1 0 R /Length " + QByteArray::number(entries.size()) +
                     " >>\nstream\n" + entries + "\nendstream");
        data += "startxref\n" + QByteArray::number(offsets[6]) + "\n%%EOF\n";
        return data;
    };

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    const PDFDocument valid = reader.readFromBuffer(makeDocument(1, 4, 0));
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
    QCOMPARE(valid.getObjectByReference(PDFObjectReference(4, 0)).getString(), QByteArray("VeryLongCompressedObjectName"));

    const std::array<std::array<PDFInteger, 3>, 7> invalidHeaders = {{
        { -1, 4, 0 }, { PDF_INTEGER_MAX, 4, 0 }, { 1, -1, 0 },
        { 1, PDF_INTEGER_MAX, 0 }, { 1, 4, -1 }, { 1, 4, PDF_INTEGER_MAX }, { 2, 4, 0 }
    }};
    for (const auto& header : invalidHeaders)
    {
        PDFDocumentReader invalidReader(nullptr, nullptr, false, false);
        invalidReader.readFromBuffer(makeDocument(header[0], header[1], header[2]));
        QCOMPARE(invalidReader.getReadingResult(), PDFDocumentReader::Result::Failed);
        QVERIFY(!invalidReader.getErrorMessage().isEmpty());
    }
}

void DocumentWriterTest::incrementalUpdateOfClassicTable()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
    const PDFObjectReference removed = builder.addObject(PDFObject::createInteger(42));
    const PDFObjectReference kept = builder.addObject(PDFObject::createInteger(43));
    const QByteArray original = write(builder.build());
    QVERIFY(original.contains("xref"));

    const PDFDocument originalDocument = read(original);
    PDFDocumentBuilder update(&originalDocument);
    update.setObject(removed, PDFObject());
    const PDFObjectReference added = update.addObject(PDFObject::createInteger(44));
    update.setPageRotation(page, PageRotation::Rotate90);
    const PDFDocument updatedDocument = update.build();

    const QByteArray updated = writeIncrementalUpdate(original, updatedDocument);
    QVERIFY(!updated.isEmpty());

    // The original bytes are untouched and the update is a classic table with a link to the previous section
    QVERIFY(updated.startsWith(original));
    const QByteArray appended = updated.mid(original.size());
    QVERIFY(appended.startsWith("\r\n") || original.endsWith('\n'));
    QVERIFY(appended.contains("xref"));
    QVERIFY(appended.contains("/Prev"));
    QVERIFY(!appended.contains("/Type /XRef"));

    // Only the changed objects are written: the removed one as null, the kept one not at all
    QVERIFY(appended.contains(QByteArray::number(removed.objectNumber) + " 0 obj\r\nnull"));
    QVERIFY(!appended.contains(QByteArray::number(kept.objectNumber) + " 0 obj"));
    QVERIFY(appended.contains(QByteArray::number(added.objectNumber) + " 0 obj"));

    const PDFDocument document = read(updated);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(1));
    QCOMPARE(document.getCatalog()->getPage(0)->getPageRotation(), PageRotation::Rotate90);
    QVERIFY(document.getObjectByReference(removed).isNull());
    QCOMPARE(document.getObjectByReference(kept).getInteger(), PDFInteger(43));
    QCOMPARE(document.getObjectByReference(added).getInteger(), PDFInteger(44));
    QCOMPARE(document.getStorage().getObjects().size(), updatedDocument.getStorage().getObjects().size());
}

void DocumentWriterTest::incrementalUpdateOfCrossReferenceStream()
{
    const QByteArray original = createCrossReferenceStreamDocument();
    const PDFDocument originalDocument = read(original);
    QCOMPARE(originalDocument.getCatalog()->getPageCount(), size_t(1));

    PDFDocumentBuilder update(&originalDocument);
    update.appendPage(QRectF(0, 0, 300, 400));
    const QByteArray updated = writeIncrementalUpdate(original, update.build());
    QVERIFY(!updated.isEmpty());

    // A document using cross-reference streams must be updated by a cross-reference stream
    QVERIFY(updated.startsWith(original));
    const QByteArray appended = updated.mid(original.size());
    QVERIFY(!appended.contains("\r\nxref"));
    QVERIFY(!appended.contains("trailer"));
    QVERIFY(appended.contains("/Type /XRef"));
    QVERIFY(appended.contains("/Prev"));

    const PDFDocument document = read(updated);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(2));
}

void DocumentWriterTest::incrementalUpdateRejectsUnreadableOriginal()
{
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const PDFDocument document = builder.build();

    QVERIFY(writeIncrementalUpdate(QByteArray("this is not a document"), document).isEmpty());
    QVERIFY(writeIncrementalUpdate(QByteArray(), document).isEmpty());
}

QTEST_MAIN(DocumentWriterTest)

void DocumentWriterTest::stringsWithCarriageReturn()
{
    // PDF Reference 1.7, 3.2.3: an end-of-line marker in a literal string is read as a line feed,
    // whether it was CR, LF or both, so a string with a CR never stays a literal one: a conforming
    // reader would read other bytes (/O, /U, /ID, an encrypted string have any bytes).
    const QByteArray withCarriageReturn("a\rb");
    const QByteArray withBothMarkers("c\r\nd");
    const QByteArray withLineFeed("e\nf");

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    const PDFObjectReference first = builder.addObject(PDFObject::createString(withCarriageReturn));
    const PDFObjectReference second = builder.addObject(PDFObject::createString(withBothMarkers));
    const PDFObjectReference third = builder.addObject(PDFObject::createString(withLineFeed));

    const QByteArray data = write(builder.build());
    QVERIFY(!data.contains("(a\rb)"));
    QVERIFY(!data.contains("(c\r\nd)"));
    QVERIFY(data.contains("<610d62>"));
    QVERIFY(data.contains("<630d0a64>"));
    QVERIFY2(data.contains("(e\nf)"), "A line feed is the same byte for every reader");

    const PDFDocument document = read(data);
    QCOMPARE(document.getObjectByReference(first).getString(), withCarriageReturn);
    QCOMPARE(document.getObjectByReference(second).getString(), withBothMarkers);
    QCOMPARE(document.getObjectByReference(third).getString(), withLineFeed);
}

void DocumentWriterTest::endOfLinesOfStringsRoundTrip()
{
    // PDF Reference 1.7, 3.2.3: any 8-bit value may appear in a string, the bytes must come back whatever the
    // end-of-line markers are, with a text around them and alone, also with a parenthesis or a backslash
    const std::vector<QByteArray> values = {
        QByteArray("\r"), QByteArray("\n"), QByteArray("\r\n"), QByteArray("\n\r"), QByteArray("\r\r"), QByteArray("\n\n"),
        QByteArray("a\rb"), QByteArray("a\nb"), QByteArray("a\r\nb"), QByteArray("a\n\rb"), QByteArray("a\r"), QByteArray("\ra"), QByteArray("a\r\n"),
        QByteArray("(a\rb)"), QByteArray("a\\\rb"), QByteArray("\xFE\xFF\x01\r\x01\n")
    };

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));

    std::vector<PDFObjectReference> references;
    for (const QByteArray& value : values)
    {
        references.push_back(builder.addObject(PDFObject::createString(value)));
    }

    const PDFDocument document = read(write(builder.build()));
    for (size_t i = 0; i < values.size(); ++i)
    {
        QCOMPARE(document.getObjectByReference(references[i]).getString(), values[i]);
    }

    // The second generation of the file has the same bytes
    const PDFDocument again = read(write(document));
    for (size_t i = 0; i < values.size(); ++i)
    {
        QCOMPARE(again.getObjectByReference(references[i]).getString(), values[i]);
    }
}

void DocumentWriterTest::literalStringsOfAContentStream_data()
{
    QTest::addColumn<QByteArray>("operand");
    QTest::addColumn<bool>("firstSquare");
    QTest::addColumn<bool>("secondSquare");

    // PDF Reference 1.7, 3.2.3: a raw marker of a literal string is the code 10, an escape keeps the code 13
    QTest::newRow("raw cr") << QByteArray("(\r)") << true << false;
    QTest::newRow("raw lf") << QByteArray("(\n)") << true << false;
    QTest::newRow("raw crlf is one glyph") << QByteArray("(\r\n)") << true << false;
    QTest::newRow("raw lf cr is two glyphs") << QByteArray("(\n\r)") << true << true;
    QTest::newRow("raw cr cr is two glyphs") << QByteArray("(\r\r)") << true << true;
    QTest::newRow("escape r") << QByteArray("(\\r)") << false << false;
    QTest::newRow("escape n") << QByteArray("(\\n)") << true << false;
    QTest::newRow("octal 15") << QByteArray("(\\015)") << false << false;
    QTest::newRow("hex 0d") << QByteArray("<0d>") << false << false;
    QTest::newRow("hex 0a") << QByteArray("<0a>") << true << false;
    QTest::newRow("hex 0d 0a") << QByteArray("<0d0a>") << false << true;
}

void DocumentWriterTest::literalStringsOfAContentStream()
{
    QFETCH(QByteArray, operand);
    QFETCH(bool, firstSquare);
    QFETCH(bool, secondSquare);

    const PDFDocument document = read(createLineEndFontDocument(operand));
    const QImage image = renderPage(document);

    // 4 pixels per point: the first glyph covers 0 - 100 points, the second one 100 - 200 points
    QCOMPARE(qGray(image.pixel(200, 200)) < 128, firstSquare);
    QCOMPARE(qGray(image.pixel(600, 200)) < 128, secondSquare);
    QCOMPARE(qGray(image.pixel(1000, 200)) < 128, false);
}

void DocumentWriterTest::encryptedStringsRoundTrip_data()
{
    QTest::addColumn<int>("algorithm");

    QTest::newRow("RC4") << int(PDFSecurityHandlerFactory::RC4);
    QTest::newRow("AES 128") << int(PDFSecurityHandlerFactory::AES_128);
    QTest::newRow("AES 256") << int(PDFSecurityHandlerFactory::AES_256);
}

void DocumentWriterTest::encryptedStringsRoundTrip()
{
    // PDF Reference 1.7, 3.2.3: "when a document is encrypted, all of its strings are encrypted and often contain
    // arbitrary 8-bit values". Each of the many ciphertexts has its random bytes: some of them have a CR or a CRLF.
    QFETCH(int, algorithm);

    const std::vector<QByteArray> values = { QByteArray("a\rb"), QByteArray("c\r\nd"), QByteArray("e\nf"), QByteArray("\r") };

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));

    std::vector<PDFObjectReference> references;
    for (int i = 0; i < 400; ++i)
    {
        references.push_back(builder.addObject(PDFObject::createString(values[i % values.size()] + QByteArray(i % 37, 'x'))));
    }

    const QByteArray data = writeEncrypted(builder, PDFSecurityHandlerFactory::Algorithm(algorithm), QStringLiteral("owner"));

    // The file has strings with such bytes, otherwise the test would say nothing
    int ciphertextsWithCarriageReturn = 0;
    for (const PDFObjectReference& reference : references)
    {
        const QByteArray header = "\n" + QByteArray::number(reference.objectNumber) + " 0 obj\r\n<";
        const qsizetype position = data.indexOf(header);
        if (position != -1 && getHexStringValue(data, position + header.size() - 1).contains('\r'))
        {
            ++ciphertextsWithCarriageReturn;
        }
    }
    QVERIFY(ciphertextsWithCarriageReturn > 0);

    PDFDocumentReader::Result result = PDFDocumentReader::Result::Failed;
    const PDFDocument document = readWithPassword(data, QStringLiteral("user"), &result);
    QCOMPARE(result, PDFDocumentReader::Result::OK);

    for (int i = 0; i < 400; ++i)
    {
        QCOMPARE(document.getObjectByReference(references[i]).getString(), values[i % values.size()] + QByteArray(i % 37, 'x'));
    }
}

void DocumentWriterTest::oldFileWithRawCarriageReturn_data()
{
    using Result = PDFDocumentReader::Result;

    QTest::addColumn<int>("algorithm");
    QTest::addColumn<QByteArray>("key");
    QTest::addColumn<QByteArray>("marker");
    QTest::addColumn<int>("first");
    QTest::addColumn<int>("last");
    QTest::addColumn<int>("userResult");
    QTest::addColumn<int>("ownerResult");

    // A file of the old writer read by the rules of 3.2.3: the raw CR is a line feed, as in a conforming reader. Cancelled
    // means a refused password. Algorithm 2 of 3.5.2: the key of RC4 / AES-128 depends on /O and /ID, and /U is compared.
    const int canceled = int(Result::Cancelled);
    const int failed = int(Result::Failed);
    const int ok = int(Result::OK);
    const int rc4 = int(PDFSecurityHandlerFactory::RC4);
    const int aes128 = int(PDFSecurityHandlerFactory::AES_128);
    const int aes256 = int(PDFSecurityHandlerFactory::AES_256);

    QTest::newRow("RC4 O cr") << rc4 << QByteArray("/O ") << QByteArray("\r") << 0 << 32 << canceled << canceled;
    QTest::newRow("RC4 O crlf") << rc4 << QByteArray("/O ") << QByteArray("\r\n") << 0 << 32 << failed << failed;
    QTest::newRow("RC4 U cr") << rc4 << QByteArray("/U ") << QByteArray("\r") << 0 << 16 << canceled << canceled;
    QTest::newRow("RC4 U crlf") << rc4 << QByteArray("/U ") << QByteArray("\r\n") << 0 << 32 << failed << failed;
    QTest::newRow("AES 128 O cr") << aes128 << QByteArray("/O ") << QByteArray("\r") << 0 << 32 << canceled << canceled;
    QTest::newRow("AES 128 U cr") << aes128 << QByteArray("/U ") << QByteArray("\r") << 0 << 16 << canceled << canceled;

    // Algorithm 2.A of ISO 32000-2: the user password needs /U and /UE only, the owner password /O, /OE and /U, both /Perms
    QTest::newRow("AES 256 O cr") << aes256 << QByteArray("/O ") << QByteArray("\r") << 0 << 32 << ok << canceled;
    QTest::newRow("AES 256 O crlf") << aes256 << QByteArray("/O ") << QByteArray("\r\n") << 0 << 48 << failed << failed;
    QTest::newRow("AES 256 U hash cr") << aes256 << QByteArray("/U ") << QByteArray("\r") << 0 << 32 << canceled << canceled;
    QTest::newRow("AES 256 U key salt cr") << aes256 << QByteArray("/U ") << QByteArray("\r") << 40 << 48 << failed << canceled;
    QTest::newRow("AES 256 U crlf") << aes256 << QByteArray("/U ") << QByteArray("\r\n") << 0 << 48 << failed << failed;
    QTest::newRow("AES 256 UE cr") << aes256 << QByteArray("/UE ") << QByteArray("\r") << 0 << 32 << failed << ok;
    QTest::newRow("AES 256 OE cr") << aes256 << QByteArray("/OE ") << QByteArray("\r") << 0 << 32 << ok << failed;
    QTest::newRow("AES 256 Perms cr") << aes256 << QByteArray("/Perms ") << QByteArray("\r") << 0 << 16 << failed << failed;
}

void DocumentWriterTest::oldFileWithRawCarriageReturn()
{
    QFETCH(int, algorithm);
    QFETCH(QByteArray, key);
    QFETCH(QByteArray, marker);
    QFETCH(int, first);
    QFETCH(int, last);
    QFETCH(int, userResult);
    QFETCH(int, ownerResult);

    // The values of the encryption are random, a file with the marker at its place is found by trying
    for (int attempt = 0; attempt < 1000; ++attempt)
    {
        const QString ownerPassword = QStringLiteral("owner-%1").arg(attempt);

        PDFDocumentBuilder builder;
        builder.appendPage(QRectF(0, 0, 100, 100));
        QByteArray data = writeEncrypted(builder, PDFSecurityHandlerFactory::Algorithm(algorithm), ownerPassword);

        const qsizetype keyPosition = data.indexOf(key + "<");
        if (keyPosition == -1)
        {
            continue;
        }

        const qsizetype position = keyPosition + key.size();

        // A CRLF gives a shorter value, which is refused whatever its bytes are (Table 3.19 gives the length of the
        // value). It is rare among random bytes, so it is written into any value, which is hexadecimal
        if (marker.size() == 2)
        {
            QByteArray shortened = getHexStringValue(data, position);
            shortened.replace('(', 'x').replace(')', 'x').replace('\\', 'x').replace('\r', 'x');
            shortened.replace(first, 2, marker);
            const qsizetype end = data.indexOf('>', position);
            data.replace(position + 1, end - position - 1, shortened.toHex());
        }

        // Only one marker of the given kind at a position of the row
        const QByteArray value = getHexStringValue(data, position);
        const qsizetype markerPosition = value.indexOf(marker);
        const bool isOnlyMarker = value.count('\r') == 1 && (marker.size() == 2 || !value.contains("\r\n"));
        if (markerPosition < first || markerPosition >= last || !isOnlyMarker || !replaceByOldLiteralString(data, position))
        {
            continue;
        }

        PDFDocumentReader::Result result = PDFDocumentReader::Result::Failed;
        readWithPassword(data, QStringLiteral("user"), &result);
        QCOMPARE(int(result), userResult);

        readWithPassword(data, ownerPassword, &result);
        QCOMPARE(int(result), ownerResult);

        // The applications read permissively: the result is the same, a failure of the encryption is not a damaged file,
        // which would ask for the password the second time - the one password of a caller would then be called wrong
        readWithPassword(data, QStringLiteral("user"), &result, true);
        QCOMPARE(int(result), userResult);

        readWithPassword(data, ownerPassword, &result, true);
        QCOMPARE(int(result), ownerResult);
        return;
    }

    QFAIL("A file with the marker was not found");
}

void DocumentWriterTest::damagedFileIsStillRestored()
{
    // A failure of the structure of a file without encryption is restored from the bytes by the permissive reading,
    // only a failure of the encryption is not: a wrong offset of the cross-reference table
    QByteArray data = createDocument({ "<< /Type /Catalog /Pages 2 0 R >>",
                                       "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>",
                                       "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 100 100 ] /Note (a\rb) >>" });
    const qsizetype position = data.lastIndexOf("startxref");
    QVERIFY(position != -1);
    data.truncate(position);
    data += "trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n99999999\n%%EOF\n";

    PDFDocumentReader::Result result = PDFDocumentReader::Result::OK;
    readWithPassword(data, QString(), &result);
    QCOMPARE(result, PDFDocumentReader::Result::Failed);

    const PDFDocument restored = readWithPassword(data, QString(), &result, true);
    QCOMPARE(result, PDFDocumentReader::Result::OK);
    QCOMPARE(restored.getCatalog()->getPageCount(), size_t(1));
    const PDFDictionary* page = restored.getDictionaryFromObject(PDFObject::createReference(PDFObjectReference(3, 0)));
    QVERIFY(page);
    QCOMPARE(page->get("Note").getString(), QByteArray("a\nb"));
}

QByteArray DocumentWriterTest::createDamagedFile(const QByteArray& objects)
{
    return "%PDF-1.7\n"
           "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
           "2 0 obj\n<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>\nendobj\n"
           "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 100 100 ] >>\nendobj\n" +
           objects +
           "\ntrailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n99999999\n%%EOF\n";
}

QByteArray DocumentWriterTest::describeObject(const PDFObject& object)
{
    if (object.isString())
    {
        return "(" + object.getString() + ")";
    }
    if (object.isInt())
    {
        return QByteArray::number(object.getInteger());
    }
    if (object.isName())
    {
        return "/" + object.getString();
    }
    if (object.isStream())
    {
        return "stream:" + *object.getStream()->getContent();
    }
    if (object.isDictionary())
    {
        QByteArray text = "{";
        const PDFDictionary* dictionary = object.getDictionary();
        for (size_t i = 0; i < dictionary->getCount(); ++i)
        {
            text += (i > 0 ? " " : "") + dictionary->getKey(i).getString() + "=" + describeObject(dictionary->getValue(i));
        }
        return text + "}";
    }
    return object.isNull() ? QByteArray("null") : QByteArray("other");
}

void DocumentWriterTest::damagedRevisionsLastOneWins_data()
{
    QTest::addColumn<int>("strategy");
    QTest::newRow("single thread") << int(PDFExecutionPolicy::Strategy::SingleThreaded);
    QTest::newRow("default") << int(PDFExecutionPolicy::Strategy::PageMultithreaded);
    QTest::newRow("threads forced") << int(PDFExecutionPolicy::Strategy::AlwaysMultithreaded);
}

void DocumentWriterTest::damagedRevisionsLastOneWins()
{
    // ISO 32000-2, 7.5.6: a later revision supersedes the older one. Two revisions of 200 strings: the threads
    // of the reader never show the old revision or a mix of both, whatever the order they come in.
    QFETCH(int, strategy);
    PDFExecutionPolicy::setStrategy(PDFExecutionPolicy::Strategy(strategy));
    const auto restore = qScopeGuard([]() { PDFExecutionPolicy::setStrategy(PDFExecutionPolicy::Strategy::PageMultithreaded); });

    constexpr int FIRST_NUMBER = 4;
    constexpr int COUNT = 200;
    QByteArray objects;
    for (const char* const revision : { "old", "new" })
    {
        for (int i = 0; i < COUNT; ++i)
        {
            objects += QByteArray::number(FIRST_NUMBER + i) + " 0 obj\n(" + revision + " " + QByteArray::number(i) + ")\nendobj\n";
        }
        objects += "trailer\n<< /Root 1 0 R >>\nstartxref\n0\n%%EOF\n";
    }
    const QByteArray data = createDamagedFile(objects);

    PDFDocumentReader::Result result = PDFDocumentReader::Result::OK;
    readWithPassword(data, QString(), &result);
    QCOMPARE(result, PDFDocumentReader::Result::Failed);

    for (int read = 0; read < 100; ++read)
    {
        const PDFDocument document = readWithPassword(data, QString(), &result, true);
        QCOMPARE(result, PDFDocumentReader::Result::OK);
        for (int i = 0; i < COUNT; ++i)
        {
            const PDFObject& object = document.getStorage().getObject(PDFObjectReference(FIRST_NUMBER + i, 0));
            QVERIFY(object.isString());
            QCOMPARE(object.getString(), "new " + QByteArray::number(i));
        }
    }
}

void DocumentWriterTest::damagedFileObjects_data()
{
    QTest::addColumn<QByteArray>("objects");
    QTest::addColumn<QStringList>("expected");

    // Bytes of a stream, which look as the end of it and as other objects
    const QByteArray fake = "12 0 obj (fake) endobj 13 0 obj (fake)";
    const QByteArray fakeEnd = "a endstream endobj " + fake + " endobj";
    const QByteArray fakeLength = QByteArray::number(fakeEnd.size());
    const QString fakeResult = QString::fromLatin1("stream:" + fakeEnd);
    const QStringList noFakes = { "12 0=null", "13 0=null" };

    // An object without its endobj ends at the next object
    QTest::newRow("no endobj, string") << QByteArray("4 0 obj (a)\n5 0 obj (b)\nendobj\n") << QStringList{ "4 0=(a)", "5 0=(b)" };
    QTest::newRow("no endobj, integer") << QByteArray("4 0 obj 7\n5 0 obj (b)\nendobj\n") << QStringList{ "4 0=7", "5 0=(b)" };
    QTest::newRow("no endobj, no space") << QByteArray("4 0 obj <</A 1>>5 0 obj (b)\nendobj\n") << QStringList{ "4 0={A=1}", "5 0=(b)" };
    QTest::newRow("no endobj, stream") << QByteArray("4 0 obj << /Length 3 >> stream\nabc\nendstream\n5 0 obj (b)\nendobj\n") << QStringList{ "4 0=stream:abc", "5 0=(b)" };
    QTest::newRow("no endobj, three") << QByteArray("4 0 obj (a)\n5 0 obj (b)\n6 0 obj (c)\n") << QStringList{ "4 0=(a)", "5 0=(b)", "6 0=(c)" };
    QTest::newRow("no body") << QByteArray("4 0 obj\n5 0 obj (b)\nendobj\n") << QStringList{ "4 0=null", "5 0=(b)" };
    QTest::newRow("no endobj, end of revision") << QByteArray("4 0 obj (a)\nxref\n0 1\n0000000000 65535 f \ntrailer\n<< /Size 4 >>\nstartxref\n0\n%%EOF\n5 0 obj (b)\nendobj\n") << QStringList{ "4 0=(a)", "5 0=(b)" };
    QTest::newRow("no endobj, parenthesis of a trailer") << QByteArray("4 0 obj (a)\ntrailer\n<< /ID [ (x ] >>\n5 0 obj (b)\n6 0 obj 1 )\nendobj\n") << QStringList{ "4 0=(a)", "5 0=(b)" };

    // Bytes inside of a stream are no objects
    QTest::newRow("stream, header") << QByteArray("4 0 obj << /Length 8 >> stream\n12 0 obj\nendstream\nendobj\n") << QStringList{ "4 0=stream:12 0 obj", "12 0=null" };
    QTest::newRow("stream, without endobj behind") << QByteArray("4 0 obj << /Length 8 >> stream\n12 0 obj\nendstream\n5 0 obj (b)\nendobj\n") << QStringList{ "4 0=stream:12 0 obj", "5 0=(b)", "12 0=null" };
    QTest::newRow("stream, carriage return and line feed") << QByteArray("4 0 obj << /Length 8 >> stream\r\n12 0 obj\r\nendstream\r\nendobj\r\n") << QStringList{ "4 0=stream:12 0 obj", "12 0=null" };
    QTest::newRow("stream, whole objects") << QByteArray("4 0 obj << /Length " + fakeLength + " >> stream\n" + fakeEnd + "\nendstream\nendobj\n5 0 obj (b)\nendobj\n") << QStringList{ "4 0=" + fakeResult, "5 0=(b)" } + noFakes;
    QTest::newRow("stream, length behind") << QByteArray("4 0 obj << /Length 5 0 R >> stream\n" + fakeEnd + "\nendstream\nendobj\n5 0 obj " + fakeLength + "\nendobj\n") << QStringList{ "4 0=" + fakeResult, "5 0=" + QString::fromLatin1(fakeLength) } + noFakes;
    QTest::newRow("stream, length in front") << QByteArray("5 0 obj " + fakeLength + "\nendobj\n4 0 obj << /Length 5 0 R >> stream\n" + fakeEnd + "\nendstream\nendobj\n") << QStringList{ "4 0=" + fakeResult, "5 0=" + QString::fromLatin1(fakeLength) } + noFakes;
    QTest::newRow("stream, wrong length") << QByteArray("4 0 obj << /Length 1 >> stream\n12 0 obj (fake) 13 0 obj (fake)\nendstream\nendobj\n5 0 obj (b)\nendobj\n") << QStringList{ "4 0=null", "5 0=(b)" } + noFakes;
    QTest::newRow("stream, no length") << QByteArray("4 0 obj << >> stream\n12 0 obj\nendstream\n5 0 obj (b)\nendobj\n") << QStringList{ "4 0=null", "5 0=(b)", "12 0=null" };
    QTest::newRow("stream without endstream") << QByteArray("4 0 obj << /Length 1000 >> stream\nabc\nendobj\n5 0 obj (b)\nendobj\n6 0 obj << /Length 1 >> stream\nx\nendstream\nendobj\n") << QStringList{ "4 0=null", "5 0=(b)", "6 0=stream:x" };
    QTest::newRow("name stream") << QByteArray("4 0 obj << /A /stream /Length 3 >>\n5 0 obj (b)\nendobj\n") << QStringList{ "4 0={A=/stream Length=3}", "5 0=(b)" };

    // Bytes inside of a string or a comment are no objects
    QTest::newRow("string, header") << QByteArray("4 0 obj (x 12 0 obj (fake) y)\nendobj\n") << QStringList{ "4 0=(x 12 0 obj (fake) y)", "12 0=null" };
    QTest::newRow("string, in a dictionary") << QByteArray("4 0 obj << /A (12 0 obj) /B 1 >>\nendobj\n") << QStringList{ "4 0={A=(12 0 obj) B=1}", "12 0=null" };
    QTest::newRow("string, whole objects") << QByteArray("4 0 obj (" + fake + " endobj)\nendobj\n") << QStringList{ "4 0=(" + QString::fromLatin1(fake) + " endobj)" } + noFakes;
    QTest::newRow("string, endobj twice") << QByteArray("4 0 obj << /A (endobj) /B (endobj) >>\nendobj\n5 0 obj (b)\nendobj\n") << QStringList{ "4 0={A=(endobj) B=(endobj)}", "5 0=(b)" };
    QTest::newRow("string, escaped parentheses") << QByteArray("4 0 obj (a\\) 12 0 obj \\( b)\nendobj\n") << QStringList{ "4 0=(a) 12 0 obj ( b)", "12 0=null" };
    QTest::newRow("string, stream behind") << QByteArray("4 0 obj << /A (12 0 obj) /Length 8 >> stream\n13 0 obj\nendstream\nendobj\n") << QStringList{ "4 0=stream:13 0 obj" } + noFakes;
    QTest::newRow("comment") << QByteArray("4 0 obj << % 12 0 obj (\n /A 1 >>\nendobj\n5 0 obj (b)\nendobj\n") << QStringList{ "4 0={A=1}", "5 0=(b)", "12 0=null" };
    QTest::newRow("comment between objects") << QByteArray("% 12 0 obj (fake) endobj\n5 0 obj (b)\nendobj\n") << QStringList{ "5 0=(b)", "12 0=null" };
    QTest::newRow("comment of carriage returns") << QByteArray("% 12 0 obj (fake)\r5 0 obj (b)\rendobj\r") << QStringList{ "5 0=(b)", "12 0=null" };
    QTest::newRow("comment behind an object") << QByteArray("4 0 obj (a) endobj % 12 0 obj (fake)\n5 0 obj (b)\nendobj\n") << QStringList{ "4 0=(a)", "5 0=(b)", "12 0=null" };
    QTest::newRow("glued behind the end of file mark") << QByteArray("%%EOF5 0 obj (b)\nendobj\n%%EOF 6 0 obj (c)\nendobj\n") << QStringList{ "5 0=(b)", "6 0=(c)" };

    // A parenthesis without its pair: the damaged object does not swallow the file behind it
    QTest::newRow("string not closed") << QByteArray("4 0 obj << /A (x >>\nendobj\n5 0 obj (b)\nendobj\n") << QStringList{ "4 0=null", "5 0=(b)" };
    QTest::newRow("string not closed, no endobj") << QByteArray("4 0 obj << /A (x >>\n5 0 obj (b)\n") << QStringList{ "4 0=null", "5 0=(b)" };
    QTest::newRow("string closed by another object") << QByteArray("4 0 obj << /A (x >>\nendobj\n5 0 obj (b)\nendobj\n6 0 obj 1 )\nendobj\n7 0 obj (c)\nendobj\n8 0 obj (12 0 obj y)\nendobj\n") << QStringList{ "4 0=null", "5 0=(b)", "7 0=(c)", "8 0=(12 0 obj y)", "12 0=null" };

    // Revisions: the last object of a reference in the file, which can be read
    QTest::newRow("revisions") << QByteArray("4 0 obj (a)\nendobj\n4 0 obj (b)\nendobj\n4 0 obj (c)\nendobj\n") << QStringList{ "4 0=(c)" };
    QTest::newRow("revisions, the last one damaged") << QByteArray("4 0 obj (old)\nendobj\n4 0 obj << /A\nendobj\n") << QStringList{ "4 0=(old)" };
    QTest::newRow("revisions, the last one cut") << QByteArray("4 0 obj (old)\nendobj\n4 0 obj << /A (x") << QStringList{ "4 0=(old)" };
    QTest::newRow("revisions, the last one null") << QByteArray("4 0 obj (old)\nendobj\n4 0 obj null\nendobj\n") << QStringList{ "4 0=null" };
    QTest::newRow("revisions, null revived") << QByteArray("4 0 obj (old)\nendobj\n4 0 obj null\nendobj\n4 0 obj (new)\nendobj\n") << QStringList{ "4 0=(new)" };

    // One number: the highest generation, 7.5.4 - a generation only grows, what references the other one reads null
    QTest::newRow("generation, later") << QByteArray("4 0 obj (g0)\nendobj\n4 1 obj (g1)\nendobj\n") << QStringList{ "4 1=(g1)", "4 0=null" };
    QTest::newRow("generation, in front") << QByteArray("4 1 obj (g1)\nendobj\n4 0 obj (g0)\nendobj\n") << QStringList{ "4 1=(g1)", "4 0=null" };
    QTest::newRow("generation, revisions") << QByteArray("4 1 obj (a)\nendobj\n4 0 obj (g0)\nendobj\n4 1 obj (b)\nendobj\n") << QStringList{ "4 1=(b)", "4 0=null" };

    // /Length as a reference: only the winning object
    QTest::newRow("length, two revisions") << QByteArray("4 0 obj << /Length 5 0 R >> stream\nabc\nendstream\nendobj\n5 0 obj 3\nendobj\n"
                                                         "4 0 obj << /Length 5 0 R >> stream\nabcdefgh\nendstream\nendobj\n5 0 obj 8\nendobj\n") << QStringList{ "4 0=stream:abcdefgh", "5 0=8" };
    QTest::newRow("length, new one in front of its stream") << QByteArray("4 0 obj << /Length 5 0 R >> stream\nabc\nendstream\nendobj\n5 0 obj 3\nendobj\n"
                                                                          "5 0 obj 8\nendobj\n4 0 obj << /Length 5 0 R >> stream\nabcdefgh\nendstream\nendobj\n") << QStringList{ "4 0=stream:abcdefgh", "5 0=8" };
    QTest::newRow("length, new one damaged") << QByteArray("5 0 obj 3\nendobj\n4 0 obj << /Length 5 0 R >> stream\nabc\nendstream\nendobj\n5 0 obj <<\nendobj\n") << QStringList{ "4 0=stream:abc", "5 0=3" };
    QTest::newRow("length, generation") << QByteArray("5 0 obj 8\nendobj\n5 1 obj 3\nendobj\n4 0 obj << /Length 5 0 R >> stream\nabcdefgh\nendstream\nendobj\n") << QStringList{ "4 0=stream:abcdefgh", "5 1=3" };
    QTest::newRow("length, old stream never by the old length") << QByteArray("4 0 obj << /Length 5 0 R >> stream\nabc\nendstream\nendobj\n5 0 obj 3\nendobj\n5 0 obj 8\nendobj\n") << QStringList{ "4 0=null", "5 0=8" };
    QTest::newRow("length, the new stream has no length") << QByteArray("4 0 obj << /Length 3 >> stream\nabc\nendstream\nendobj\n4 0 obj << /Length 5 0 R >> stream\nabcdefgh\nendstream\nendobj\n") << QStringList{ "4 0=stream:abc" };
    QTest::newRow("length of an unknown object") << QByteArray("4 0 obj << /Length 5 0 R >> stream\n12 0 obj\nendstream\nendobj\n6 0 obj (b)\nendobj\n") << QStringList{ "4 0=null", "6 0=(b)", "12 0=null" };

    // The last objects are not a document, the first ones still are
    const QByteArray oldRevision = "4 0 obj (old)\nendobj\n5 0 obj 3\nendobj\n6 0 obj << /Length 5 0 R >> stream\nabc\nendstream\nendobj\n4 0 obj (new)\nendobj\n5 0 obj 8\nendobj\n6 0 obj << /Length 5 0 R >> stream\nabcdefgh\nendstream\nendobj\n";
    const QStringList oldObjects = { "4 0=(old)", "5 0=3", "6 0=stream:abc" };
    QTest::newRow("page twice in the page tree") << QByteArray(oldRevision + "2 0 obj << /Type /Pages /Kids [ 3 0 R 3 0 R ] /Count 2 >>\nendobj\n") << oldObjects;
    QTest::newRow("cycle of the page tree") << QByteArray(oldRevision + "2 0 obj << /Type /Pages /Kids [ 2 0 R ] /Count 1 >>\nendobj\n") << oldObjects;
    QTest::newRow("missing page") << QByteArray(oldRevision + "2 0 obj << /Type /Pages /Kids [ 33 0 R ] /Count 1 >>\nendobj\n") << oldObjects;
    QTest::newRow("page tree without a dictionary") << QByteArray(oldRevision + "2 0 obj 7\nendobj\n") << oldObjects;
    QTest::newRow("catalog without a dictionary") << QByteArray(oldRevision + "1 0 obj 7\nendobj\n") << oldObjects;
    QTest::newRow("page tree without a page") << QByteArray(oldRevision + "2 0 obj << /Type /Pages /Kids [ ] /Count 0 >>\nendobj\n") << oldObjects;
}

void DocumentWriterTest::damagedFileObjects()
{
    QFETCH(QByteArray, objects);
    QFETCH(QStringList, expected);

    const auto restore = qScopeGuard([]() { PDFExecutionPolicy::setStrategy(PDFExecutionPolicy::Strategy::PageMultithreaded); });
    const QByteArray data = createDamagedFile(objects);

    for (const PDFExecutionPolicy::Strategy strategy : { PDFExecutionPolicy::Strategy::SingleThreaded, PDFExecutionPolicy::Strategy::AlwaysMultithreaded })
    {
        PDFExecutionPolicy::setStrategy(strategy);
        for (int read = 0; read < 5; ++read)
        {
            PDFDocumentReader::Result result = PDFDocumentReader::Result::Failed;
            const PDFDocument document = readWithPassword(data, QString(), &result, true);
            QCOMPARE(result, PDFDocumentReader::Result::OK);
            QCOMPARE(document.getCatalog()->getPageCount(), size_t(1));

            for (const QString& item : expected)
            {
                const qsizetype equals = item.indexOf(QLatin1Char('='));
                const QStringList reference = item.left(equals).split(QLatin1Char(' '));
                const PDFObject& object = document.getStorage().getObject(PDFObjectReference(reference[0].toInt(), reference[1].toInt()));
                QCOMPARE(reference[0] + QLatin1Char(' ') + reference[1] + QLatin1Char('=') + QString::fromLatin1(describeObject(object)), item);
            }
        }
    }
}

void DocumentWriterTest::damagedFileOldestContentWarning()
{
    // Only when the last objects are not a document: a warning and the typed state of the reader, which the
    // application tells the user by its info bar (UnitTestsAppDocumentFile restoredFileBar)
    QStringList warningsOfLastRevision;
    {
        PDFDocumentReader reader(nullptr, nullptr, true, false);
        const PDFDocument document = reader.readFromBuffer(createDamagedFile("4 0 obj (old)\nendobj\n4 0 obj (new)\nendobj\n"));
        QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
        QCOMPARE(document.getStorage().getObject(PDFObjectReference(4, 0)).getString(), QByteArray("new"));
        QVERIFY(reader.isDocumentRestored());
        QVERIFY(!reader.isOldestContentShown());
        warningsOfLastRevision = reader.getWarnings();
    }

    PDFDocumentReader reader(nullptr, nullptr, true, false);
    const PDFDocument document = reader.readFromBuffer(createDamagedFile("4 0 obj (old)\nendobj\n4 0 obj (new)\nendobj\n2 0 obj << /Type /Pages /Kids [ 3 0 R 3 0 R ] /Count 2 >>\nendobj\n"));
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
    QCOMPARE(document.getStorage().getObject(PDFObjectReference(4, 0)).getString(), QByteArray("old"));
    QCOMPARE(reader.getWarnings().size(), warningsOfLastRevision.size() + 1);
    QVERIFY(reader.isDocumentRestored());
    QVERIFY(reader.isOldestContentShown());

    // The same reader on a valid file forgets it
    reader.readFromBuffer(createDocument({ "<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>", "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 100 100 ] >>" }));
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
    QVERIFY(!reader.isOldestContentShown());
    QVERIFY(!reader.isDocumentRestored());
}

void DocumentWriterTest::incrementalUpdateOfDamagedFiles()
{
    // An incremental update goes by the table of the file (ISO 32000-2, 7.5.6): a file without the end of file mark
    // (7.5.5), which the permissive reading opens by its table, is updated and its bytes stay; a file restored from
    // its bytes has no table to go by, it is refused
    const QByteArray valid = createDocument({ "<< /Type /Catalog /Pages 2 0 R >>",
                                              "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>",
                                              "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 100 100 ] >>" });
    QByteArray noMark = valid;
    noMark.truncate(noMark.lastIndexOf("%%EOF"));

    for (const QByteArray& original : { valid, noMark })
    {
        PDFDocumentReader reader(nullptr, nullptr, true, false);
        const PDFDocument document = reader.readFromBuffer(original);
        QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
        QVERIFY(!reader.isDocumentRestored());

        PDFDocumentBuilder builder(&document);
        builder.setDocumentTitle(QStringLiteral("Updated"));
        const PDFDocument updated = builder.build();

        QByteArray data;
        QBuffer buffer(&data);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        PDFDocumentWriter writer(nullptr);
        const PDFOperationResult result = writer.writeIncrementalUpdate(&buffer, original, &updated);
        QVERIFY2(result, qPrintable(result.getErrorMessage()));
        QVERIFY(data.startsWith(original));
        QVERIFY(data.size() > original.size());

        PDFDocumentReader strictReader(nullptr, nullptr, false, false);
        const PDFDocument reopened = strictReader.readFromBuffer(data);
        QCOMPARE(strictReader.getReadingResult(), PDFDocumentReader::Result::OK);
        QCOMPARE(reopened.getInfo()->title, QStringLiteral("Updated"));
    }

    const QByteArray damaged = createDamagedFile(QByteArray());
    PDFDocumentReader reader(nullptr, nullptr, true, false);
    const PDFDocument document = reader.readFromBuffer(damaged);
    QVERIFY(reader.isDocumentRestored());
    QByteArray data;
    QBuffer buffer(&data);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    PDFDocumentWriter writer(nullptr);
    const PDFOperationResult result = writer.writeIncrementalUpdate(&buffer, damaged, &document);
    QVERIFY(!result);
    QCOMPARE(result.getErrorMessage(), QStringLiteral("Original document cannot be read, so it cannot be updated incrementally."));
    QVERIFY(data.isEmpty());
}

void DocumentWriterTest::damagedFileIsToldRestored()
{
    // Only when the objects really come from the bytes, not from a table: the signatures of a file depend on it
    const QByteArray valid = createDocument({ "<< /Type /Catalog /Pages 2 0 R >>",
                                              "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>",
                                              "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 100 100 ] >>" });
    {
        PDFDocumentReader reader(nullptr, nullptr, true, false);
        reader.readFromBuffer(valid);
        QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
        QVERIFY(!reader.isDocumentRestored());
    }

    // The end of file mark of 7.5.5 is missing: the permissive reading still goes by the table
    {
        QByteArray noMark = valid;
        const qsizetype position = noMark.lastIndexOf("%%EOF");
        QVERIFY(position != -1);
        noMark.truncate(position);
        PDFDocumentReader strictReader(nullptr, nullptr, false, false);
        strictReader.readFromBuffer(noMark);
        QCOMPARE(strictReader.getReadingResult(), PDFDocumentReader::Result::Failed);

        PDFDocumentReader reader(nullptr, nullptr, true, false);
        reader.readFromBuffer(noMark);
        QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
        QVERIFY(!reader.isDocumentRestored());
    }

    PDFDocumentReader reader(nullptr, nullptr, true, false);
    reader.readFromBuffer(createDamagedFile(QByteArray()));
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
    QVERIFY(reader.isDocumentRestored());

    // The same reader on another file forgets it
    reader.readFromBuffer(valid);
    QVERIFY(!reader.isDocumentRestored());
}

void DocumentWriterTest::damagedFileTrailers_data()
{
    QTest::addColumn<QByteArray>("objects");
    QTest::addColumn<QByteArray>("trailers");
    QTest::addColumn<int>("expectedRoot");
    QTest::addColumn<int>("expectedInfo");

    const QByteArray secondCatalog = "5 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
    const QByteArray fakeTrailer = "trailer << /Root 9 0 R /Encrypt << /Filter /Standard /V 1 /R 2 /O (x) /U (y) /P 0 >> >> ";
    const QByteArray inStream = "4 0 obj\n<< /Length " + QByteArray::number(fakeTrailer.size()) + " >>\nstream\n" + fakeTrailer + "\nendstream\nendobj\n";

    // Bytes of a trailer in a stream or a string are data (an attached PDF, a text): no other root and no password
    QTest::newRow("in a stream") << inStream << QByteArray() << 1 << 0;
    QTest::newRow("in a string") << QByteArray("4 0 obj (" + fakeTrailer + ")\nendobj\n") << QByteArray() << 1 << 0;
    QTest::newRow("behind the real one") << QByteArray() << inStream << 1 << 0;

    // ISO 32000-2, 7.5.6: of more trailers the last one wins, an entry only in an older one stays, null removes it
    QTest::newRow("the last one wins") << secondCatalog << QByteArray("trailer\n<< /Root 5 0 R >>\n") << 5 << 0;
    QTest::newRow("older entry stays") << QByteArray("trailer\n<< /Info 7 0 R >>\n") << QByteArray() << 1 << 7;
    QTest::newRow("removed by null") << QByteArray("trailer\n<< /Info 7 0 R >>\n") << QByteArray("trailer\n<< /Info null >>\n") << 1 << 0;

    // A damaged trailer does not take the others away
    QTest::newRow("not closed string") << QByteArray("trailer\n<< /Info (1 >>\n") << QByteArray() << 1 << 0;
    QTest::newRow("not a dictionary") << QByteArray("trailer\n[ 1 ]\n") << QByteArray() << 1 << 0;
}

void DocumentWriterTest::damagedFileTrailers()
{
    QFETCH(QByteArray, objects);
    QFETCH(QByteArray, trailers);
    QFETCH(int, expectedRoot);
    QFETCH(int, expectedInfo);

    int passwordRequests = 0;
    PDFDocumentReader reader(nullptr, [&passwordRequests](bool* ok) { ++passwordRequests; *ok = false; return QString(); }, true, false);
    const PDFDocument document = reader.readFromBuffer(createDamagedFile(objects) + trailers);

    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
    QCOMPARE(passwordRequests, 0);
    QCOMPARE(document.getStorage().getSecurityHandler()->getMode(), EncryptionMode::None);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(1));

    const PDFDictionary* trailer = document.getTrailerDictionary();
    QVERIFY(!trailer->hasKey("Encrypt"));
    QCOMPARE(trailer->get("Root").getReference(), PDFObjectReference(expectedRoot, 0));
    QCOMPARE(trailer->get("Size").getInteger(), PDFInteger(4));
    QCOMPARE(trailer->hasKey("Info"), expectedInfo != 0);
    if (expectedInfo != 0)
    {
        QCOMPARE(trailer->get("Info").getReference(), PDFObjectReference(expectedInfo, 0));
    }
}

void DocumentWriterTest::damagedFileTrailerOnlyInObject()
{
    // No other trailer has the catalog: the file was opened before, it stays readable by the bytes in its stream
    const QByteArray trailer = "trailer << /Root 1 0 R >> ";
    const QByteArray data = "%PDF-1.7\n"
                            "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
                            "2 0 obj\n<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>\nendobj\n"
                            "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 100 100 ] >>\nendobj\n"
                            "4 0 obj\n<< /Length " + QByteArray::number(trailer.size()) + " >>\nstream\n" + trailer + "\nendstream\nendobj\n"
                            "startxref\n99999999\n%%EOF\n";

    PDFDocumentReader::Result result = PDFDocumentReader::Result::Failed;
    const PDFDocument document = readWithPassword(data, QString(), &result, true);
    QCOMPARE(result, PDFDocumentReader::Result::OK);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(1));
}

void DocumentWriterTest::damagedFileFallbackUsesFirstTrailer()
{
    // The oldest content is the first objects with the first trailer: the root of the last trailer, which leads to
    // the broken page tree of the last revision, is no part of it (a page twice in the page tree refuses the document)
    const QByteArray lastRevision = "6 0 obj\n<< /Type /Catalog /Pages 7 0 R >>\nendobj\n"
                                    "7 0 obj\n<< /Type /Pages /Kids [ 33 0 R 33 0 R ] /Count 2 >>\nendobj\n"
                                    "33 0 obj\n<< /Type /Page /Parent 7 0 R /MediaBox [ 0 0 200 200 ] >>\nendobj\n"
                                    "trailer\n<< /Root 6 0 R >>\n";

    PDFDocumentReader reader(nullptr, nullptr, true, false);
    const PDFDocument document = reader.readFromBuffer(createDamagedFile(QByteArray()) + lastRevision);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(1));
    QCOMPARE(document.getCatalog()->getPage(0)->getPageReference(), PDFObjectReference(3, 0));
    QCOMPARE(document.getTrailerDictionary()->get("Root").getReference(), PDFObjectReference(1, 0));
    QVERIFY(reader.getWarnings().contains(PDFDocumentReader::tr("The last changes of the damaged file can't be read, its oldest content is shown.")));
}

void DocumentWriterTest::damagedFileNoDocumentInAnyRevision()
{
    // No revision is a document, the file stays refused - a choice of a revision does not make one
    PDFDocumentReader::Result result = PDFDocumentReader::Result::OK;
    const QByteArray data = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n2 0 obj\n<< /Type /Pages /Kids [ 2 0 R ] /Count 1 >>\nendobj\n1 0 obj\n7\nendobj\n"
                            "trailer\n<< /Root 1 0 R >>\nstartxref\n99999999\n%%EOF\n";
    readWithPassword(data, QString(), &result, true);
    QCOMPARE(result, PDFDocumentReader::Result::Failed);
}

void DocumentWriterTest::damagedFileAttackedByIncrementalUpdate_data()
{
    QTest::addColumn<int>("strategy");
    QTest::newRow("single thread") << int(PDFExecutionPolicy::Strategy::SingleThreaded);
    QTest::newRow("default") << int(PDFExecutionPolicy::Strategy::PageMultithreaded);
    QTest::newRow("threads forced") << int(PDFExecutionPolicy::Strategy::AlwaysMultithreaded);
}

void DocumentWriterTest::damagedFileAttackedByIncrementalUpdate()
{
    // The structure of testpdf/Signature/invalid/isa.pdf, an attack on a signature: the signed revision, a revision of the
    // signature, a last update without an endobj of the page tree, with an empty table and no end of file. Some reads
    // refused it, some showed the signed page: the last objects always win, so it is the page of the attacker.
    QFETCH(int, strategy);
    PDFExecutionPolicy::setStrategy(PDFExecutionPolicy::Strategy(strategy));
    const auto restore = qScopeGuard([]() { PDFExecutionPolicy::setStrategy(PDFExecutionPolicy::Strategy::PageMultithreaded); });

    const QByteArray content = "BT\n/F1 12 Tf\n(Manipulated page) Tj\nET\n";
    const QByteArray data =
        "%PDF-1.4\n%\xf6\xe4\xfc\xdf\n"
        "1 0 obj\n<<\n/Type /Catalog\n/Pages 2 0 R\n>>\nendobj\n"
        "2 0 obj\n<<\n/Type /Pages\n/Kids [3 0 R]\n/Count 1\n>>\nendobj\n"
        "3 0 obj\n<<\n/Type /Page\n/MediaBox [0.0 0.0 612.0 792.0]\n/Parent 2 0 R\n/Contents 4 0 R\n>>\nendobj\n"
        "4 0 obj\n<<\n/Length 19\n>>\nstream\nBT (Signed page) ET\nendstream\nendobj\n"
        "xref\n0 5\n0000000000 65535 f \n0000000015 00000 n \ntrailer\n<<\n/Root 1 0 R\n/Size 5\n>>\nstartxref\n300\n%%EOF\n\n"
        "1 0 obj\n<<\n/Type /Catalog\n/Pages 2 0 R\n/AcroForm <<\n/Fields [8 0 R]\n/SigFlags 3\n>>\n>>\nendobj\n"
        "8 0 obj\n<<\n/FT /Sig\n/T (Signature1)\n/Contents <00112233>\n>>\nendobj\n"
        "3 0 obj\n<<\n/Type /Page\n/MediaBox [0.0 0.0 612.0 792.0]\n/Parent 2 0 R\n/Contents 4 0 R\n/Annots [8 0 R]\n>>\nendobj\n"
        "xref\n0 2\n0000000000 65535 f \n0000000300 00000 n \ntrailer\n<<\n/Root 1 0 R\n/Size 9\n/Prev 300\n>>\nstartxref\n700\n%%EOF\n\n"
        "2 0 obj\n<<\n/Type /Pages\n/Kids [33 0 R]\n/Count 1\n>>\n"
        "33 0 obj\n<<\n/Type /Page\n/MediaBox [0.0 0.0 612.0 792.0]\n/Parent 2 0 R\n/Contents 15 0 R\n/Annots [8 0 R]\n>>\nendobj\n"
        "15 0 obj\n<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "\nendstream\nendobj\n"
        "xref\ntrailer\n<<\n/Root 1 0 R\n>>\nstartxref\n23291 %Must be the correct byte offset of \"xref\"\n";

    PDFDocumentReader::Result result = PDFDocumentReader::Result::OK;
    readWithPassword(data, QString(), &result);
    QCOMPARE(result, PDFDocumentReader::Result::Failed);

    for (int read = 0; read < 300; ++read)
    {
        const PDFDocument document = readWithPassword(data, QString(), &result, true);
        QCOMPARE(result, PDFDocumentReader::Result::OK);

        QCOMPARE(document.getCatalog()->getPageCount(), size_t(1));
        QCOMPARE(document.getCatalog()->getPage(0)->getPageReference(), PDFObjectReference(33, 0));
        QCOMPARE(describeObject(document.getStorage().getObject(PDFObjectReference(15, 0))), "stream:" + content);
        QVERIFY(document.getStorage().getObject(PDFObjectReference(1, 0)).getDictionary()->hasKey("AcroForm"));
        QVERIFY(document.getStorage().getObject(PDFObjectReference(3, 0)).getDictionary()->hasKey("Annots"));
    }
}

void DocumentWriterTest::damagedFileHostileSizes_data()
{
    QTest::addColumn<QByteArray>("head");
    QTest::addColumn<QByteArray>("pattern");
    QTest::addColumn<QByteArray>("tail");
    QTest::addColumn<bool>("isLastObjectFound");

    constexpr qsizetype SIZE = 4 * 1024 * 1024;

    // Each of them is quadratic, when a search for something, which is not there, runs again to the end of the file
    QTest::newRow("strings not closed") << QByteArray() << QByteArray("1 0 obj (") << QByteArray() << true;
    QTest::newRow("strings not closed with endobj") << QByteArray() << QByteArray("1 0 obj (a endobj ") << QByteArray() << true;
    QTest::newRow("parentheses in front of endobj") << QByteArray() << QByteArray("(") << QByteArray("endobj\n") << true;
    QTest::newRow("strings closed behind a far endobj") << QByteArray() << QByteArray("(a)") << QByteArray("endobj\n") << true;
    QTest::newRow("endobj in strings of one object") << QByteArray("[ ") << QByteArray("(endobj)") << QByteArray(" ] endobj\n") << true;
    QTest::newRow("streams without endstream") << QByteArray() << QByteArray("1 0 obj << /Length 7 0 R >> stream\nendobj\n") << QByteArray() << true;
    QTest::newRow("streams without endobj") << QByteArray() << QByteArray("1 0 obj << /Length 7 0 R >> stream\nendstream\n") << QByteArray() << true;
    QTest::newRow("streams without anything") << QByteArray() << QByteArray("1 0 obj << /Length 7 0 R >> stream\n") << QByteArray() << false;
    QTest::newRow("candidates of one length") << QByteArray() << QByteArray("7 0 obj 1\nendobj\n8 0 obj << /Length 7 0 R >> stream\nendobj\n") << QByteArray() << true;
    QTest::newRow("headers") << QByteArray() << QByteArray("1 0 obj\n") << QByteArray() << true;
    QTest::newRow("marks") << QByteArray() << QByteArray("obj ") << QByteArray() << true;
    QTest::newRow("digits") << QByteArray() << QByteArray("1234567890 ") << QByteArray("obj\n") << true;

    // A parenthesis or a comment in front of each mark, read to the end of the file again for each of them
    QTest::newRow("trailers with comments") << QByteArray() << QByteArray("trailer%") << QByteArray("\n") << true;
    QTest::newRow("trailers with strings") << QByteArray() << QByteArray("trailer<</a(") << QByteArray() << true;

    // Dictionaries of many keys: each of them searched in all of them
    QByteArray trailerKeys;
    QByteArray oneTrailer = "trailer<<";
    for (int i = 0; trailerKeys.size() < SIZE / 8; ++i)
    {
        trailerKeys += "trailer<</a" + QByteArray::number(i) + " 1>>\n";
        oneTrailer += "/a" + QByteArray::number(i) + " 1\n";
    }
    QTest::newRow("trailers of other keys") << trailerKeys << QByteArray(" ") << QByteArray() << true;
    QTest::newRow("one trailer of many keys") << QByteArray(oneTrailer + ">>\n") << QByteArray(" ") << QByteArray() << true;

    // Each of the objects is read by a level of the stack of the thread
    QTest::newRow("arrays inside of each other") << QByteArray() << QByteArray("[") << QByteArray() << true;
    QTest::newRow("dictionaries inside of each other") << QByteArray() << QByteArray("<</a") << QByteArray() << true;

    // Each /Length of many streams leads to one run of white space without endstream behind it
    const QByteArray lengthStart = " 0 obj<</Length ";
    const QByteArray streamStart = ">>stream\n";
    const QByteArray objectEnd = "endobj\n";
    constexpr int NUMBER_SIZE = 8;
    constexpr int LENGTH_SIZE = 10;
    const qsizetype objectSize = NUMBER_SIZE + lengthStart.size() + LENGTH_SIZE + streamStart.size() + objectEnd.size();
    const qsizetype objectCount = SIZE / objectSize;
    QByteArray streams;
    for (qsizetype i = 0; i < objectCount; ++i)
    {
        const qsizetype dataStart = i * objectSize + NUMBER_SIZE + lengthStart.size() + LENGTH_SIZE + streamStart.size();
        streams += QByteArray::number(10 + i).rightJustified(NUMBER_SIZE, '0') + lengthStart + QByteArray::number(objectCount * objectSize - dataStart).rightJustified(LENGTH_SIZE, '0') + streamStart + objectEnd;
    }
    QTest::newRow("lengths leading to one white space") << streams << QByteArray(" ") << QByteArray("X") << true;
}

void DocumentWriterTest::damagedFileHostileSizes()
{
    QFETCH(QByteArray, head);
    QFETCH(QByteArray, pattern);
    QFETCH(QByteArray, tail);
    QFETCH(bool, isLastObjectFound);

    constexpr qsizetype SIZE = 4 * 1024 * 1024;
    QByteArray objects = "4 0 obj " + head;
    objects += pattern.repeated(SIZE / pattern.size());
    objects += tail;
    objects += "\n99 0 obj (last)\nendobj\n";
    const QByteArray data = createDamagedFile(objects);

    QElapsedTimer timer;
    timer.start();
    PDFDocumentReader::Result result = PDFDocumentReader::Result::Failed;
    const PDFDocument document = readWithPassword(data, QString(), &result, true);
    const qint64 limit = 20000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    QVERIFY2(timer.elapsed() < limit, qPrintable(QString::number(timer.elapsed())));
    qInfo("%lld ms", timer.elapsed());

    QCOMPARE(result, PDFDocumentReader::Result::OK);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(1));
    QCOMPARE(describeObject(document.getStorage().getObject(PDFObjectReference(99, 0))), isLastObjectFound ? QByteArray("(last)") : QByteArray("null"));
}

void DocumentWriterTest::oldFileWithRawCarriageReturnInIdentifier()
{
    // Algorithm 2 of 3.5.2: the first string of /ID is an input of the key of RC4 / AES-128. The trailer
    // follows the cross-reference table, the entry written into it moves no object.
    const QByteArray id("0123456\r89abcdef", 16);

    const auto createFile = [&id](const QByteArray& entry)
    {
        PDFDocumentBuilder builder;
        builder.appendPage(QRectF(0, 0, 100, 100));
        QByteArray data = writeEncrypted(builder, PDFSecurityHandlerFactory::RC4, QStringLiteral("owner"), id);
        const qsizetype trailer = data.lastIndexOf("trailer");
        Q_ASSERT(trailer != -1);
        data.insert(data.indexOf("<<", trailer) + 2, " /ID [ " + entry + " " + entry + " ] ");
        return data;
    };

    PDFDocumentReader::Result result = PDFDocumentReader::Result::Failed;

    // The written form of today, the control of the test
    readWithPassword(createFile("<" + id.toHex() + ">"), QStringLiteral("user"), &result);
    QCOMPARE(result, PDFDocumentReader::Result::OK);

    // The old writer: both passwords are refused
    const QByteArray oldFile = createFile("(" + id + ")");
    readWithPassword(oldFile, QStringLiteral("user"), &result);
    QCOMPARE(result, PDFDocumentReader::Result::Cancelled);
    readWithPassword(oldFile, QStringLiteral("owner"), &result);
    QCOMPARE(result, PDFDocumentReader::Result::Cancelled);
}

void DocumentWriterTest::oldEncryptedStringWithRawCarriageReturn_data()
{
    QTest::addColumn<int>("algorithm");
    QTest::addColumn<QByteArray>("marker");

    QTest::newRow("RC4 cr") << int(PDFSecurityHandlerFactory::RC4) << QByteArray("\r");
    QTest::newRow("RC4 crlf") << int(PDFSecurityHandlerFactory::RC4) << QByteArray("\r\n");
    QTest::newRow("AES 128 cr") << int(PDFSecurityHandlerFactory::AES_128) << QByteArray("\r");
    QTest::newRow("AES 128 crlf") << int(PDFSecurityHandlerFactory::AES_128) << QByteArray("\r\n");
    QTest::newRow("AES 256 cr") << int(PDFSecurityHandlerFactory::AES_256) << QByteArray("\r");
    QTest::newRow("AES 256 crlf") << int(PDFSecurityHandlerFactory::AES_256) << QByteArray("\r\n");
}

void DocumentWriterTest::oldEncryptedStringWithRawCarriageReturn()
{
    // A string of an encrypted file of the old writer, whose ciphertext has a raw CR. The file opens, only that string reads other bytes.
    QFETCH(int, algorithm);
    QFETCH(QByteArray, marker);

    const bool isRC4 = algorithm == int(PDFSecurityHandlerFactory::RC4);
    const QByteArray plain = "This is the text of the string, which is long enough for 4 blocks of AES.";
    QCOMPARE(plain.size(), 73);

    // The ciphertexts are random: a CRLF inside of the second block of AES, without another CR and without
    // a parenthesis or a backslash in the 96 bytes, is 5.6e-5 per string, so 2.7 % per file. 200 attempts did
    // not find it in 0.39 % of the runs; a typical run needs about 36 attempts, a miss here is 7e-25.
    for (int attempt = 0; attempt < 2000; ++attempt)
    {
        PDFDocumentBuilder builder;
        builder.appendPage(QRectF(0, 0, 100, 100));
        std::vector<PDFObjectReference> references;
        for (int i = 0; i < 500; ++i)
        {
            references.push_back(builder.addObject(PDFObject::createString(plain)));
        }

        // RC4 has no random input: another owner password gives other ciphertexts
        QByteArray data = writeEncrypted(builder, PDFSecurityHandlerFactory::Algorithm(algorithm), QStringLiteral("owner-%1").arg(attempt));

        // The first string with one marker: for AES the initialization vector has 16 bytes, the blocks follow it
        for (const PDFObjectReference& reference : references)
        {
            const QByteArray header = "\n" + QByteArray::number(reference.objectNumber) + " 0 obj\r\n";
            const qsizetype headerPosition = data.indexOf(header);
            const qsizetype position = headerPosition + header.size();
            if (headerPosition == -1 || data[position] != '<')
            {
                continue;
            }

            const QByteArray value = getHexStringValue(data, position);
            const qsizetype markerPosition = value.indexOf(marker);
            const bool isOnlyMarker = value.count('\r') == 1 && (marker.size() == 2 || !value.contains("\r\n"));
            const bool isInsideOfSecondBlock = markerPosition >= 32 && markerPosition < 48;
            if (!isOnlyMarker || markerPosition == -1 || (!isRC4 && !isInsideOfSecondBlock) || !replaceByOldLiteralString(data, position))
            {
                continue;
            }

            PDFDocumentReader::Result result = PDFDocumentReader::Result::Failed;
            const PDFDocument document = readWithPassword(data, QStringLiteral("user"), &result);
            QCOMPARE(result, PDFDocumentReader::Result::OK);

            // Every other string is the plain text
            for (const PDFObjectReference& other : references)
            {
                if (other != reference)
                {
                    QCOMPARE(document.getObjectByReference(other).getString(), plain);
                }
            }

            const QByteArray string = document.getObjectByReference(reference).getString();
            QVERIFY(string != plain);

            // CR -> LF changes the bits 0x07 of the byte of the ciphertext
            const char changedBit = char(0x0D ^ 0x0A);
            if (isRC4)
            {
                // A stream cipher: the byte itself. A CRLF shortens the ciphertext: everything behind it reads another key stream.
                QCOMPARE(string.left(markerPosition), plain.left(markerPosition));
                QCOMPARE(string[markerPosition], char(plain[markerPosition] ^ changedBit));
                if (marker.size() == 1)
                {
                    QCOMPARE(string.mid(markerPosition + 1), plain.mid(markerPosition + 1));
                }
                else
                {
                    QCOMPARE(string.size(), plain.size() - 1);
                    QVERIFY(string.mid(markerPosition + 1) != plain.mid(markerPosition + 2));
                }
            }
            else
            {
                // CBC: the block of the byte is lost and the block behind it has the byte changed. The first block is whole.
                const qsizetype blockPosition = markerPosition - 32;
                QCOMPARE(string.left(16), plain.left(16));
                QVERIFY(string.mid(16, 16) != plain.mid(16, 16));
                if (marker.size() == 1)
                {
                    QByteArray expectedBlock = plain.mid(32, 16);
                    expectedBlock[blockPosition] = char(expectedBlock[blockPosition] ^ changedBit);
                    QCOMPARE(string.mid(32, 16), expectedBlock);
                    QCOMPARE(string.mid(48), plain.mid(48));
                }
                else
                {
                    // Blocks behind a shortened ciphertext are shifted, the incomplete last block is dropped
                    QVERIFY(string.size() < plain.size());
                }
            }
            return;
        }
    }

    QFAIL("A string with the marker was not found");
}

void DocumentWriterTest::damagedEncryptedFileIsDecrypted_data()
{
    QTest::addColumn<int>("algorithm");

    QTest::newRow("RC4") << int(PDFSecurityHandlerFactory::RC4);
    QTest::newRow("AES 128") << int(PDFSecurityHandlerFactory::AES_128);
    QTest::newRow("AES 256") << int(PDFSecurityHandlerFactory::AES_256);
}

void DocumentWriterTest::damagedEncryptedFileIsDecrypted()
{
    // A wrong offset of the table: the permissive reading restores the objects from the bytes, where
    // they are encrypted. Left so, a page could not be read and a written copy would be encrypted twice:
    // the file of the user, replaced by Overwrite, would be garbage also with the right password.
    QFETCH(int, algorithm);

    const QByteArray secretText("the content of the secret page");
    const QByteArray secretValue("a secret value");

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    const PDFObjectReference stringReference = builder.addObject(PDFObject::createString(secretValue));
    PDFDictionaryBuilder streamDictionary;
    streamDictionary.addEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(secretText.size()));
    const PDFObjectReference streamReference = builder.addObject(PDFObject::createStream(PDFStream(std::move(streamDictionary), QByteArray(secretText))));

    QByteArray data = writeEncrypted(builder, PDFSecurityHandlerFactory::Algorithm(algorithm), QStringLiteral("owner"));
    QVERIFY(!data.contains(secretText));

    const qsizetype position = data.lastIndexOf("startxref");
    QVERIFY(position != -1);
    data.truncate(position);
    data += "startxref\n99999999\n%%EOF\n";

    PDFDocumentReader::Result result = PDFDocumentReader::Result::OK;
    readWithPassword(data, QStringLiteral("user"), &result);
    QCOMPARE(result, PDFDocumentReader::Result::Failed);

    // A wrong password is no document: the bytes are never handed out undecrypted
    const PDFDocument wrongPassword = readWithPassword(data, QStringLiteral("wrong"), &result, true);
    QCOMPARE(result, PDFDocumentReader::Result::Cancelled);
    QCOMPARE(wrongPassword.getCatalog()->getPageCount(), size_t(0));

    const PDFDocument restored = readWithPassword(data, QStringLiteral("user"), &result, true);
    QCOMPARE(result, PDFDocumentReader::Result::OK);
    QCOMPARE(restored.getObjectByReference(stringReference).getString(), secretValue);
    QCOMPARE(restored.getDecodedStream(restored.getObjectByReference(streamReference).getStream()), secretText);

    // Written again and read by the strict reader: one encryption, the same content
    const QByteArray written = write(restored);
    QVERIFY(!written.contains(secretText));
    const PDFDocument reopened = readWithPassword(written, QStringLiteral("user"), &result);
    QCOMPARE(result, PDFDocumentReader::Result::OK);
    QCOMPARE(reopened.getObjectByReference(stringReference).getString(), secretValue);
    QCOMPARE(reopened.getDecodedStream(reopened.getObjectByReference(streamReference).getStream()), secretText);
}

void DocumentWriterTest::damagedEncryptedRevisions_data()
{
    QTest::addColumn<int>("algorithm");

    QTest::newRow("RC4") << int(PDFSecurityHandlerFactory::RC4);
    QTest::newRow("AES 128") << int(PDFSecurityHandlerFactory::AES_128);
    QTest::newRow("AES 256") << int(PDFSecurityHandlerFactory::AES_256);
}

void DocumentWriterTest::damagedEncryptedRevisions()
{
    // ISO 32000-2, 7.6.3.3: the key of RC4 and AES 128 depends on the object number and the generation. Only
    // the winning object is decrypted, once, by the reference it has in the file, whichever revision wins.
    QFETCH(int, algorithm);

    const QByteArray firstValue("the value of the first revision");
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    const PDFObjectReference stringReference = builder.addObject(PDFObject::createString(firstValue));
    const PDFObjectReference generationReference = builder.addObject(PDFObject::createString(QByteArray("the value of generation 0")));
    QByteArray data = writeEncrypted(builder, PDFSecurityHandlerFactory::Algorithm(algorithm), QStringLiteral("owner"));

    PDFDocumentReader::Result result = PDFDocumentReader::Result::Failed;
    const PDFDocument valid = readWithPassword(data, QStringLiteral("user"), &result);
    QCOMPARE(result, PDFDocumentReader::Result::OK);
    const PDFSecurityHandler* handler = valid.getStorage().getSecurityHandler();
    const PDFObjectReference pageReference = valid.getCatalog()->getPage(0)->getPageReference();
    const PDFObjectReference pagesReference = valid.getObject(valid.getTrailerDictionary()->get("Root")).getDictionary()->get("Pages").getReference();

    const auto encrypted = [handler](PDFObjectReference reference, const QByteArray& value)
    {
        const QByteArray bytes = handler->encryptObject(PDFObject::createString(QByteArray(value)), reference).getString();
        return QByteArray::number(reference.objectNumber) + " " + QByteArray::number(reference.generation) + " obj\n<" + bytes.toHex() + ">\nendobj\n";
    };

    // The table is damaged, the second revision has another value and a higher generation of the other object
    data.truncate(data.lastIndexOf("startxref"));
    const QByteArray end = "startxref\n99999999\n%%EOF\n";
    const PDFObjectReference secondGeneration(generationReference.objectNumber, 1);
    const QByteArray secondRevision = encrypted(stringReference, QByteArray("the value of the second revision")) + encrypted(secondGeneration, QByteArray("the value of generation 1"));

    const PDFDocument restored = readWithPassword(data + secondRevision + end, QStringLiteral("user"), &result, true);
    QCOMPARE(result, PDFDocumentReader::Result::OK);
    QCOMPARE(restored.getStorage().getObject(stringReference).getString(), QByteArray("the value of the second revision"));
    QCOMPARE(restored.getStorage().getObject(secondGeneration).getString(), QByteArray("the value of generation 1"));
    QVERIFY(restored.getStorage().getObject(generationReference).isNull());

    // A wrong password is no document, whichever revision would win
    const PDFDocument wrongPassword = readWithPassword(data + secondRevision + end, QStringLiteral("wrong"), &result, true);
    QCOMPARE(result, PDFDocumentReader::Result::Cancelled);
    QCOMPARE(wrongPassword.getCatalog()->getPageCount(), size_t(0));

    // The last revision is not a document: the oldest content is decrypted the same way
    const QByteArray brokenPages = QByteArray::number(pagesReference.objectNumber) + " 0 obj\n<< /Type /Pages /Kids [ " + QByteArray::number(pageReference.objectNumber) + " 0 R " + QByteArray::number(pageReference.objectNumber) + " 0 R ] /Count 2 >>\nendobj\n";
    const PDFDocument oldest = readWithPassword(data + secondRevision + brokenPages + end, QStringLiteral("user"), &result, true);
    QCOMPARE(result, PDFDocumentReader::Result::OK);
    QCOMPARE(oldest.getCatalog()->getPageCount(), size_t(1));
    QCOMPARE(oldest.getStorage().getObject(stringReference).getString(), firstValue);
}

void DocumentWriterTest::damagedFileVersion_data()
{
    QTest::addColumn<QByteArray>("prefix");
    QTest::addColumn<QByteArray>("header");
    QTest::addColumn<QByteArray>("suffix");
    QTest::addColumn<int>("major");
    QTest::addColumn<int>("minor");

    const QByteArray padding(2000, '\0');
    QTest::newRow("padded at the end") << QByteArray() << QByteArray("%PDF-1.4") << padding << 1 << 4;
    QTest::newRow("padded at the start") << padding << QByteArray("%PDF-1.5") << QByteArray() << 1 << 5;
    QTest::newRow("the other form of the header") << padding << QByteArray("%!PS-Adobe-3.0 PDF-1.3") << QByteArray() << 1 << 3;
    QTest::newRow("no header") << QByteArray() << QByteArray("%nothing") << padding << 1 << 7;
    QTest::newRow("a header of no version") << QByteArray() << QByteArray("%PDF-0.0") << padding << 1 << 7;
    QTest::newRow("a mark with no version, then a header") << QByteArray("PDF- is not a header\n") << QByteArray("%PDF-2.0") << padding << 2 << 0;
}

void DocumentWriterTest::damagedFileVersion()
{
    // More than 1 kB behind %%EOF or before the header: the structure fails before the header is read, the
    // restored file had the version 0.0 and everything written from it the header %PDF-0.0 the strict reader refuses
    QFETCH(QByteArray, prefix);
    QFETCH(QByteArray, header);
    QFETCH(QByteArray, suffix);
    QFETCH(int, major);
    QFETCH(int, minor);

    QByteArray data = createDocument({ "<< /Type /Catalog /Pages 2 0 R >>",
                                       "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>",
                                       "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 100 100 ] >>" });
    const qsizetype lineEnd = data.indexOf('\n');
    QVERIFY(data.startsWith("%PDF-") && lineEnd != -1);
    data = prefix + header + data.mid(lineEnd) + suffix;

    PDFDocumentReader::Result result = PDFDocumentReader::Result::OK;
    readWithPassword(data, QString(), &result);
    QCOMPARE(result, PDFDocumentReader::Result::Failed);

    const PDFDocument restored = readWithPassword(data, QString(), &result, true);
    QCOMPARE(result, PDFDocumentReader::Result::OK);
    QCOMPARE(restored.getCatalog()->getPageCount(), size_t(1));
    QCOMPARE(int(restored.getInfo()->version.major), major);
    QCOMPARE(int(restored.getInfo()->version.minor), minor);

    const QByteArray written = write(restored);
    QVERIFY2(written.startsWith(QStringLiteral("%PDF-%1.%2").arg(major).arg(minor).toLatin1()), written.left(16).constData());
    const PDFDocument reopened = readWithPassword(written, QString(), &result);
    QCOMPARE(result, PDFDocumentReader::Result::OK);
    QCOMPARE(int(reopened.getInfo()->version.major), major);
}

void DocumentWriterTest::invalidVersionNeverWritten()
{
    // A written file always has a header a strict reader takes, whatever document it was given
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    const PDFDocument built = builder.build();
    const PDFDocument document(PDFObjectStorage(built.getStorage()), PDFVersion(), QByteArray());
    QVERIFY(!document.getInfo()->version.isValid());

    const QByteArray written = write(document);
    QVERIFY2(written.startsWith("%PDF-1.7"), written.left(16).constData());
    PDFDocumentReader::Result result = PDFDocumentReader::Result::Failed;
    readWithPassword(written, QString(), &result);
    QCOMPARE(result, PDFDocumentReader::Result::OK);
}

void DocumentWriterTest::trailerSizeOfTheWrittenTable()
{
    // PDF Reference 1.7, Table 3.13: Size is the number of the entries of the cross-reference
    // table, objects beyond it are missing for a reader. The image optimizer adds a new soft mask
    // to a storage, whose trailer dictionary is older.
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    const PDFDocument document = builder.build();
    const PDFInteger originalSize = PDFInteger(document.getStorage().getObjects().size());
    QCOMPARE(document.getTrailerDictionary()->get("Size").getInteger(), originalSize);

    PDFObjectStorage storage = document.getStorage();
    const PDFObjectReference added = storage.addObject(PDFObject::createInteger(42));
    const PDFDocument extended(std::move(storage), document.getInfo()->version, QByteArray());

    const QByteArray data = write(extended);
    const QRegularExpressionMatch table = QRegularExpression("xref\\s+0 (\\d+)\\s").match(QString::fromLatin1(data));
    const QRegularExpressionMatch size = QRegularExpression("/Size (\\d+)").match(QString::fromLatin1(data));
    QVERIFY(table.hasMatch());
    QVERIFY(size.hasMatch());
    QCOMPARE(table.captured(1).toLongLong(), originalSize + 1);
    QCOMPARE(size.captured(1).toLongLong(), originalSize + 1);
    QCOMPARE(read(data).getObjectByReference(added).getInteger(), PDFInteger(42));
}

void DocumentWriterTest::namesWithNumberSignRoundTrip()
{
    // ISO 32000-2, 7.3.5: "#" in a name introduces two hexadecimal digits, so the number sign
    // itself is written as #23. A name "A#GB" written as it is makes the file unreadable (found
    // by ECMA-363 4th Edition.pdf of UnitTestsDocumentToolCorpus); a space is escaped as before.
    const QList<QByteArray> names = { "A#GB", "#", "A##", "A#20B", "A B" };

    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    std::vector<pdf::PDFObjectReference> references;
    for (const QByteArray& name : names)
    {
        references.push_back(builder.addObject(pdf::PDFObject::createName(name)));
    }
    const pdf::PDFDocument document = builder.build();

    QByteArray data;
    QBuffer buffer(&data);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    pdf::PDFDocumentWriter writer(nullptr);
    QVERIFY(bool(writer.write(&buffer, &document)));
    QVERIFY2(data.contains("/A#23GB"), "The number sign of a name is written as #23");

    const auto refusePassword = [](bool* ok)
    {
        *ok = false;
        return QString();
    };
    pdf::PDFDocumentReader reader(nullptr, refusePassword, false, false);
    const pdf::PDFDocument readDocument = reader.readFromBuffer(data);
    QVERIFY2(reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK, qPrintable(reader.getErrorMessage()));

    for (int i = 0; i < names.size(); ++i)
    {
        const pdf::PDFObject& object = readDocument.getObjectByReference(references[size_t(i)]);
        QVERIFY(object.isName());
        QCOMPARE(object.getString(), names[i]);
    }
}

bool DocumentWriterTest::isSameDouble(PDFReal first, PDFReal second)
{
    return std::bit_cast<quint64>(first) == std::bit_cast<quint64>(second);
}

bool DocumentWriterTest::isValidRealText(const QByteArray& text)
{
    static const QRegularExpression pattern(QStringLiteral("^-?[0-9]+\\.[0-9]+$"));
    return pattern.match(QString::fromLatin1(text)).hasMatch();
}

QByteArray DocumentWriterTest::createDocument(const std::vector<QByteArray>& objects)
{
    QByteArray data("%PDF-1.7\n");
    std::vector<qsizetype> offsets;

    for (size_t i = 0; i < objects.size(); ++i)
    {
        offsets.push_back(data.size());
        data += QByteArray::number(qulonglong(i + 1)) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }

    const qsizetype xrefOffset = data.size();
    data += "xref\n0 " + QByteArray::number(qulonglong(objects.size() + 1)) + "\n0000000000 65535 f\r\n";
    for (qsizetype offset : offsets)
    {
        data += QByteArray::number(qlonglong(offset)).rightJustified(10, '0') + " 00000 n\r\n";
    }
    data += "trailer\n<< /Size " + QByteArray::number(qulonglong(objects.size() + 1)) + " /Root 1 0 R >>\n";
    data += "startxref\n" + QByteArray::number(qlonglong(xrefOffset)) + "\n%%EOF\n";
    return data;
}

QByteArray DocumentWriterTest::createFontMatrixDocument(const QByteArray& fontMatrixValue, const QByteArray& fontSize)
{
    const QByteArray content = "BT /F1 " + fontSize + " Tf (a) Tj ET";
    const QByteArray glyph = "1000 0 d0 0 0 1000 1000 re f";

    return createDocument({
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 1000 100 ] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "\nendstream",
        "<< /Type /Font /Subtype /Type3 /FontBBox [ 0 0 1000 1000 ] /FontMatrix [ " + fontMatrixValue + " 0 0 " + fontMatrixValue +
            " 0 0 ] /CharProcs << /a 6 0 R >> /Encoding << /Type /Encoding /Differences [ 97 /a ] >> /FirstChar 97 /LastChar 97 /Widths [ 1000 ] >>",
        "<< /Length " + QByteArray::number(glyph.size()) + " >>\nstream\n" + glyph + "\nendstream"
    });
}

QByteArray DocumentWriterTest::createLineEndFontDocument(const QByteArray& operand)
{
    const QByteArray content = "BT /F1 100 Tf " + operand + " Tj ET";
    const QByteArray square = "1000 0 d0 0 0 1000 1000 re f";
    const QByteArray nothing = "1000 0 d0";

    return createDocument({
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 1000 100 ] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "\nendstream",
        "<< /Type /Font /Subtype /Type3 /FontBBox [ 0 0 1000 1000 ] /FontMatrix [ 0.001 0 0 0.001 0 0 ] /CharProcs << /lineFeed 6 0 R /carriageReturn 7 0 R >> "
            "/Encoding << /Type /Encoding /Differences [ 10 /lineFeed 13 /carriageReturn ] >> /FirstChar 10 /LastChar 13 /Widths [ 1000 0 0 1000 ] >>",
        "<< /Length " + QByteArray::number(square.size()) + " >>\nstream\n" + square + "\nendstream",
        "<< /Length " + QByteArray::number(nothing.size()) + " >>\nstream\n" + nothing + "\nendstream"
    });
}

QByteArray DocumentWriterTest::writeEncrypted(PDFDocumentBuilder& builder, PDFSecurityHandlerFactory::Algorithm algorithm, const QString& ownerPassword, const QByteArray& id)
{
    PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = algorithm;
    settings.userPassword = QStringLiteral("user");
    settings.ownerPassword = ownerPassword;
    settings.permissions = 0xFFFFFFFCu;
    settings.id = id;
    builder.setSecurityHandler(PDFSecurityHandlerFactory::createSecurityHandler(settings));
    return write(builder.build());
}

PDFDocument DocumentWriterTest::readWithPassword(const QByteArray& data, const QString& password, PDFDocumentReader::Result* result, bool permissive)
{
    bool wasAsked = false;
    const auto getPassword = [&wasAsked, &password](bool* ok)
    {
        *ok = !wasAsked;
        wasAsked = true;
        return password;
    };

    PDFDocumentReader reader(nullptr, getPassword, permissive, false);
    PDFDocument document = reader.readFromBuffer(data);
    *result = reader.getReadingResult();
    return document;
}

QByteArray DocumentWriterTest::getHexStringValue(const QByteArray& data, qsizetype position)
{
    const qsizetype end = data.indexOf('>', position);
    return QByteArray::fromHex(data.mid(position + 1, end - position - 1));
}

bool DocumentWriterTest::replaceByOldLiteralString(QByteArray& data, qsizetype position)
{
    const qsizetype end = data.indexOf('>', position);
    const QByteArray value = getHexStringValue(data, position);
    if (value.contains('(') || value.contains(')') || value.contains('\\'))
    {
        return false;
    }

    const qsizetype length = end - position + 1;
    QByteArray literal = "(" + value + ")";
    literal.append(QByteArray(length - literal.size(), ' '));
    data.replace(position, length, literal);
    return true;
}

QImage DocumentWriterTest::renderPage(const PDFDocument& document)
{
    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    iccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));

    PDFRenderer renderer(&document, &fontCache, &iccProfileCache, &cms, &activity, PDFRenderer::Features(), PDFMeshQualitySettings());

    QImage image(4000, 400, QImage::Format_RGB888);
    image.fill(Qt::white);

    QPainter painter(&image);
    renderer.render(&painter, QRectF(0, 0, image.width(), image.height()), 0);
    painter.end();
    return image;
}

void DocumentWriterTest::realNumberText_data()
{
    QTest::addColumn<double>("value");
    QTest::addColumn<QByteArray>("text");

    // The shortest fixed notation, always with a period: the type stays a real (PDF Reference 1.7, 3.2.2)
    QTest::newRow("integral") << 5.0 << QByteArray("5.0");
    QTest::newRow("negative integral") << -40.0 << QByteArray("-40.0");
    QTest::newRow("zero") << 0.0 << QByteArray("0.0");
    QTest::newRow("negative zero") << -0.0 << QByteArray("-0.0");
    QTest::newRow("tenth") << 0.1 << QByteArray("0.1");
    QTest::newRow("font matrix of the bug") << 0.00999451 << QByteArray("0.00999451");
    QTest::newRow("below five places") << 0.000004 << QByteArray("0.000004");
    QTest::newRow("sixteen digits") << 1.0 / 3.0 << QByteArray("0.3333333333333333");
    QTest::newRow("seventeen digits") << 0.1 + 0.2 << QByteArray("0.30000000000000004");
    QTest::newRow("largest of table C.1") << 3.403e38 << QByteArray("3403") + QByteArray(35, '0') + ".0";
    QTest::newRow("closest to zero of table C.1") << 1.175e-38 << QByteArray("0.") + QByteArray(37, '0') + "1175";
    QTest::newRow("no exponent for a large value") << 1e300 << QByteArray("1") + QByteArray(300, '0') + ".0";
    QTest::newRow("no exponent for a denormal") << 5e-324 << QByteArray("0.") + QByteArray(323, '0') + "5";
}

void DocumentWriterTest::realNumberText()
{
    QFETCH(double, value);
    QFETCH(QByteArray, text);

    QCOMPARE(PDFDocumentWriter::getRealNumberText(value), text);
}

void DocumentWriterTest::realNumbersRoundTrip_data()
{
    QTest::addColumn<double>("value");

    QTest::newRow("integral") << 5.0;
    QTest::newRow("zero") << 0.0;
    QTest::newRow("negative zero") << -0.0;
    QTest::newRow("0.3") << 0.3;
    QTest::newRow("0.7") << 0.7;
    QTest::newRow("sum of tenths") << 0.1 + 0.2;
    QTest::newRow("third") << 1.0 / 3.0;
    QTest::newRow("font matrix of the bug") << 0.00999451;
    QTest::newRow("below five places") << 0.000004;
    QTest::newRow("largest of table C.1") << 3.403e38;
    QTest::newRow("negative largest of table C.1") << -3.403e38;
    QTest::newRow("closest to zero of table C.1") << 1.175e-38;
    QTest::newRow("smallest single") << double(std::numeric_limits<float>::min());
    QTest::newRow("largest single") << double(std::numeric_limits<float>::max());
    QTest::newRow("float noise") << double(0.2f);
    QTest::newRow("smallest normal") << std::numeric_limits<double>::min();
    QTest::newRow("denormal") << 2.225e-308;
    QTest::newRow("smallest denormal") << std::numeric_limits<double>::denorm_min();
    QTest::newRow("1e22") << 1e22;
    QTest::newRow("1e23") << 1e23;
    QTest::newRow("above the integers") << 9007199254740993.0;
    QTest::newRow("1e300") << 1e300;
    QTest::newRow("-1e300") << -1e300;
    QTest::newRow("largest") << std::numeric_limits<double>::max();
    QTest::newRow("just below one") << std::nextafter(1.0, 0.0);
}

void DocumentWriterTest::realNumbersRoundTrip()
{
    QFETCH(double, value);

    const QByteArray text = PDFDocumentWriter::getRealNumberText(value);
    QVERIFY2(isValidRealText(text), text.constData());

    // The lexical analyzer alone
    PDFLexicalAnalyzer analyzer(text.constData(), text.constData() + text.size());
    const PDFLexicalAnalyzer::Token token = analyzer.fetch();
    QCOMPARE(token.type, PDFLexicalAnalyzer::TokenType::Real);
    QVERIFY2(isSameDouble(token.data.toDouble(), value), text.constData());

    // A whole document written and read
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    const PDFObjectReference reference = builder.addObject(PDFObject::createReal(value));
    const QByteArray written = write(builder.build());
    QVERIFY(written.contains(" obj\r\n" + text + " "));

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    const PDFDocument document = reader.readFromBuffer(written);
    QVERIFY2(reader.getReadingResult() == PDFDocumentReader::Result::OK, qPrintable(reader.getErrorMessage()));

    const PDFObject& object = document.getObjectByReference(reference);
    QVERIFY(object.isReal());
    QVERIFY2(isSameDouble(object.getReal(), value), text.constData());
}

void DocumentWriterTest::realNumbersRandomRoundTrip()
{
    // Every finite double, whatever its exponent or digits are, gives the same double back
    QRandomGenerator generator(20261102);
    for (int i = 0; i < 100000; ++i)
    {
        const double value = std::bit_cast<double>(generator.generate64());
        if (!std::isfinite(value))
        {
            continue;
        }

        const QByteArray text = PDFDocumentWriter::getRealNumberText(value);
        PDFLexicalAnalyzer analyzer(text.constData(), text.constData() + text.size());
        const PDFLexicalAnalyzer::Token token = analyzer.fetch();
        QCOMPARE(token.type, PDFLexicalAnalyzer::TokenType::Real);
        QVERIFY2(isSameDouble(token.data.toDouble(), value), text.constData());
        QVERIFY2(isValidRealText(text), text.constData());
    }

    // Values of the usual size: coordinates and scales computed by a program
    for (int i = 0; i < 100000; ++i)
    {
        const double value = generator.generateDouble() * std::pow(10.0, generator.bounded(-7, 6));
        const QByteArray text = PDFDocumentWriter::getRealNumberText(value);
        PDFLexicalAnalyzer analyzer(text.constData(), text.constData() + text.size());
        const PDFLexicalAnalyzer::Token token = analyzer.fetch();
        QVERIFY2(isSameDouble(token.data.toDouble(), value), text.constData());
    }
}

void DocumentWriterTest::realNumbersInStructures()
{
    // Numbers in arrays and dictionaries, and the same in a stream dictionary; an integer stays an integer
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));

    PDFArrayBuilder array;
    array.appendItem(PDFObject::createReal(0.00999451));
    array.appendItem(PDFObject::createReal(5.0));
    array.appendItem(PDFObject::createInteger(5));
    const PDFObjectReference arrayReference = builder.addObject(PDFObject::createArray(std::move(array)));

    PDFDictionaryBuilder dictionary;
    dictionary.addEntry(PDFInplaceOrMemoryString("Small"), PDFObject::createReal(0.000004));
    dictionary.addEntry(PDFInplaceOrMemoryString("Integral"), PDFObject::createReal(-2.0));
    dictionary.addEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(31));
    PDFStream stream(std::move(dictionary), QByteArray("0.123456789 is data of a stream"));
    const PDFObjectReference streamReference = builder.addObject(PDFObject::createStream(std::move(stream)));

    const QByteArray written = write(builder.build());
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    const PDFDocument document = reader.readFromBuffer(written);
    QVERIFY2(reader.getReadingResult() == PDFDocumentReader::Result::OK, qPrintable(reader.getErrorMessage()));

    const PDFArray* readArray = document.getObjectByReference(arrayReference).getArray();
    QCOMPARE(readArray->getCount(), size_t(3));
    QVERIFY(readArray->getItem(0).isReal());
    QVERIFY(isSameDouble(readArray->getItem(0).getReal(), 0.00999451));
    QVERIFY(readArray->getItem(1).isReal());
    QVERIFY(isSameDouble(readArray->getItem(1).getReal(), 5.0));
    QVERIFY(readArray->getItem(2).isInt());
    QCOMPARE(readArray->getItem(2).getInteger(), PDFInteger(5));

    const PDFStream* readStream = document.getObjectByReference(streamReference).getStream();
    QVERIFY(isSameDouble(readStream->getDictionary()->get("Small").getReal(), 0.000004));
    QVERIFY(readStream->getDictionary()->get("Integral").isReal());
    QVERIFY(isSameDouble(readStream->getDictionary()->get("Integral").getReal(), -2.0));
    QCOMPARE(*readStream->getContent(), QByteArray("0.123456789 is data of a stream"));

    static const QRegularExpression exponent(QStringLiteral("[0-9][eE][-+]?[0-9]"));
    QVERIFY(!exponent.match(QString::fromLatin1(written)).hasMatch());
}

void DocumentWriterTest::realNumbersNotFinite()
{
    // PDF has no token for these values. A computed value must not make the file unreadable: the old text "nan" is
    // not a number of PDF Reference 1.7, chapter 3.2.2, and an infinity is the largest real of Table C.1, which
    // readers of single precision read too; it keeps its sign.
    const double infinity = std::numeric_limits<double>::infinity();
    const double largest = double(std::numeric_limits<float>::max());

    QCOMPARE(PDFDocumentWriter::getRealNumberText(std::numeric_limits<double>::quiet_NaN()), QByteArray("0.0"));
    QVERIFY(isValidRealText(PDFDocumentWriter::getRealNumberText(infinity)));
    QVERIFY(isValidRealText(PDFDocumentWriter::getRealNumberText(-infinity)));

    // The same rule for the numbers of the content streams computed by the library
    QCOMPARE(PDFDocumentBuilder::formatPDFReal(std::numeric_limits<double>::quiet_NaN()), QByteArray("0"));
    QCOMPARE(PDFDocumentBuilder::formatPDFReal(infinity).toDouble(), largest);
    QCOMPARE(PDFDocumentBuilder::formatPDFReal(-infinity).toDouble(), -largest);

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    const PDFObjectReference nan = builder.addObject(PDFObject::createReal(std::numeric_limits<double>::quiet_NaN()));
    const PDFObjectReference positive = builder.addObject(PDFObject::createReal(infinity));
    const PDFObjectReference negative = builder.addObject(PDFObject::createReal(-infinity));

    PDFArrayBuilder array;
    array.appendItem(PDFObject::createReal(std::numeric_limits<double>::signaling_NaN()));
    array.appendItem(PDFObject::createInteger(7));
    const PDFObjectReference arrayReference = builder.addObject(PDFObject::createArray(std::move(array)));

    const QByteArray written = write(builder.build());
    QVERIFY(!written.contains("nan"));
    QVERIFY(!written.contains("inf"));

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    const PDFDocument document = reader.readFromBuffer(written);
    QVERIFY2(reader.getReadingResult() == PDFDocumentReader::Result::OK, qPrintable(reader.getErrorMessage()));

    QVERIFY(document.getObjectByReference(nan).isReal());
    QVERIFY(isSameDouble(document.getObjectByReference(nan).getReal(), 0.0));
    QVERIFY(isSameDouble(document.getObjectByReference(positive).getReal(), largest));
    QVERIFY(isSameDouble(document.getObjectByReference(negative).getReal(), -largest));
    QCOMPARE(document.getObjectByReference(arrayReference).getArray()->getItem(1).getInteger(), PDFInteger(7));
}

void DocumentWriterTest::realNumberTextIndependentOfLocale()
{
    const QLocale defaultLocale;
    QLocale::setDefault(QLocale(QLocale::Czech, QLocale::CzechRepublic));

    const QByteArray text = PDFDocumentWriter::getRealNumberText(1234567.5);
    const QByteArray serialized = PDFDocumentWriter::getSerializedObject(PDFObject::createReal(-0.25));

    QLocale::setDefault(defaultLocale);

    QCOMPARE(text, QByteArray("1234567.5"));
    QCOMPARE(serialized, QByteArray("-0.25 "));

    for (const char character : PDFDocumentWriter::getRealNumberText(0.00999451))
    {
        QVERIFY(uchar(character) < 128);
    }
}

void DocumentWriterTest::longNumberOfFileKeepsItsValue()
{
    // A number of a file with more digits than a double has is read as the closest double, and that double is
    // written; a number of 400 digits in the fraction is valid and needs no error
    const QByteArray longFraction = "0." + QByteArray(400, '3');
    const QByteArray longZeros = "0." + QByteArray(400, '0') + "1";

    const QByteArray data = createDocument({
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 100 100 ] >>",
        longFraction,
        longZeros,
        "3.14159265358979323846264338327950288"
    });

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    const PDFDocument document = reader.readFromBuffer(data);
    QVERIFY2(reader.getReadingResult() == PDFDocumentReader::Result::OK, qPrintable(reader.getErrorMessage()));

    const QByteArray written = write(document);
    const PDFDocument writtenDocument = read(written);

    QVERIFY(isSameDouble(writtenDocument.getObjectByReference(PDFObjectReference(4, 0)).getReal(), 1.0 / 3.0));
    QVERIFY(writtenDocument.getObjectByReference(PDFObjectReference(5, 0)).isReal());
    QVERIFY(isSameDouble(writtenDocument.getObjectByReference(PDFObjectReference(5, 0)).getReal(), 0.0));
    QVERIFY(isSameDouble(writtenDocument.getObjectByReference(PDFObjectReference(6, 0)).getReal(), 3.14159265358979323846264338327950288));
    QVERIFY(written.contains("3.141592653589793 "));
}

void DocumentWriterTest::fontMatrixSurvivesWholeWrite_data()
{
    QTest::addColumn<QByteArray>("matrixText");
    QTest::addColumn<double>("matrix");
    QTest::addColumn<QByteArray>("fontSize");
    QTest::addColumn<int>("lastInkX");
    QTest::addColumn<int>("firstPaperX");

    // The glyph of 1000 units has the width 1000 * matrix * font size in points, 4 pixels per point.
    // 0.00999451 * 100: 999.451 points = 3997.8 pixels; five decimal places would give 999 points = 3996 pixels.
    QTest::newRow("font matrix of the bug") << QByteArray("0.00999451") << 0.00999451 << QByteArray("100") << 3996 << 3998;

    // 0.000004 * 25000: 100 points = 400 pixels; five decimal places would give no glyph at all
    QTest::newRow("below five places") << QByteArray("0.000004") << 0.000004 << QByteArray("25000") << 398 << 401;
}

void DocumentWriterTest::fontMatrixSurvivesWholeWrite()
{
    QFETCH(QByteArray, matrixText);
    QFETCH(double, matrix);
    QFETCH(QByteArray, fontSize);
    QFETCH(int, lastInkX);
    QFETCH(int, firstPaperX);

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    const PDFDocument original = reader.readFromBuffer(createFontMatrixDocument(matrixText, fontSize));
    QVERIFY2(reader.getReadingResult() == PDFDocumentReader::Result::OK, qPrintable(reader.getErrorMessage()));

    const PDFDocument written = read(write(original));
    const PDFDictionary* font = written.getObjectByReference(PDFObjectReference(5, 0)).getDictionary();
    QVERIFY(font);
    const PDFArray* fontMatrix = font->get("FontMatrix").getArray();
    QVERIFY(fontMatrix);
    QVERIFY(isSameDouble(fontMatrix->getItem(0).getReal(), matrix));
    QVERIFY(isSameDouble(fontMatrix->getItem(3).getReal(), matrix));

    const QImage image = renderPage(written);
    QCOMPARE(image.pixelColor(lastInkX, 200), QColor(Qt::black));
    QCOMPARE(image.pixelColor(firstPaperX, 200), QColor(Qt::white));
    QCOMPARE(image, renderPage(original));
}

void DocumentWriterTest::incrementalUpdateKeepsNumbersOfOriginal()
{
    // The unchanged objects stay as the bytes of the original with all their digits, also those with a text the
    // writer would not give them; a new object has the exact text too
    const QByteArray original = createDocument({
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 100 100 ] >>",
        "+000.1234567890000"
    });

    const PDFDocument originalDocument = read(original);
    PDFDocumentBuilder update(&originalDocument);
    const PDFObjectReference added = update.addObject(PDFObject::createReal(0.00999451));
    const QByteArray updated = writeIncrementalUpdate(original, update.build());
    QVERIFY(!updated.isEmpty());
    QVERIFY(updated.startsWith(original));
    QVERIFY(updated.contains("+000.1234567890000"));

    const QByteArray appended = updated.mid(original.size());
    QVERIFY(!appended.contains("0.123456789"));
    QVERIFY(appended.contains("0.00999451 "));

    const PDFDocument document = read(updated);
    QVERIFY(isSameDouble(document.getObjectByReference(PDFObjectReference(4, 0)).getReal(), 0.123456789));
    QVERIFY(isSameDouble(document.getObjectByReference(added).getReal(), 0.00999451));
}

void DocumentWriterTest::ocrFingerprintOfExactNumbers()
{
    // The fingerprint binds an OCR layer to its page (GUI overhaul: owner 2026-11-02): the digest has the exact text of the
    // writer, so a change of a number is a change of the page, also below five decimal places, and a value read back stays.
    const PDFDocument exact = read(createFontMatrixDocument("0.00999451", "100"));
    const PDFDocument exactAgain = read(createFontMatrixDocument("0.00999451", "100"));
    const PDFDocument rounded = read(createFontMatrixDocument("0.00999", "100"));
    const PDFDocument near = read(createFontMatrixDocument("0.00999452", "100"));
    const PDFDocument other = read(createFontMatrixDocument("0.0200", "100"));

    const QByteArray fingerprint = PDFOCRPagePreparer::computePageFingerprint(&exact, 0);
    QVERIFY(!fingerprint.isEmpty());
    QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(&exactAgain, 0), fingerprint);
    QVERIFY(PDFOCRPagePreparer::computePageFingerprint(&rounded, 0) != fingerprint);
    QVERIFY(PDFOCRPagePreparer::computePageFingerprint(&near, 0) != fingerprint);
    QVERIFY(PDFOCRPagePreparer::computePageFingerprint(&other, 0) != fingerprint);

    // A document written whole by PDF4QT and read back has the same values, also the second generation
    const PDFDocument written = read(write(exact));
    QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(&written, 0), fingerprint);
    const PDFDocument writtenAgain = read(write(written));
    QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(&writtenAgain, 0), fingerprint);

    // Six decimal places ending by 5: the case of the old digest of five decimals, which depended on the rounding of the lexer
    const PDFDocument tie = read(createFontMatrixDocument("0.123455", "100"));
    const PDFDocument tieNear = read(createFontMatrixDocument("0.123456", "100"));
    QVERIFY(PDFOCRPagePreparer::computePageFingerprint(&tie, 0) != PDFOCRPagePreparer::computePageFingerprint(&tieNear, 0));
    const PDFDocument tieWritten = read(write(tie));
    QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(&tieWritten, 0), PDFOCRPagePreparer::computePageFingerprint(&tie, 0));

    // One format of the numbers
    QCOMPARE(PDFDocumentWriter::getSerializedObject(PDFObject::createReal(0.00999451)), QByteArray("0.00999451 "));
    QCOMPARE(PDFDocumentWriter::getSerializedObject(PDFObject::createReal(5.0)), QByteArray("5.0 "));

    // A string of the resources by its value: a raw CR is a line feed (3.2.3), the written document keeps it
    const auto createStringDocument = [](const QByteArray& value)
    {
        return createDocument({
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 100 100 ] /Resources << /Value " + value + " >> >>"
        });
    };

    const PDFDocument rawCarriageReturn = read(createStringDocument("(a\rb)"));
    const QByteArray stringFingerprint = PDFOCRPagePreparer::computePageFingerprint(&rawCarriageReturn, 0);
    const PDFDocument lineFeed = read(createStringDocument("<610a62>"));
    const PDFDocument carriageReturn = read(createStringDocument("<610d62>"));
    QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(&lineFeed, 0), stringFingerprint);
    QVERIFY(PDFOCRPagePreparer::computePageFingerprint(&carriageReturn, 0) != stringFingerprint);
    const PDFDocument rawWritten = read(write(rawCarriageReturn));
    QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(&rawWritten, 0), stringFingerprint);
    const PDFDocument carriageReturnWritten = read(write(carriageReturn));
    QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(&carriageReturnWritten, 0), PDFOCRPagePreparer::computePageFingerprint(&carriageReturn, 0));

    // The same rule below the limit of the depth of the digest, where an object is digested in its serialized form
    const auto createDeepDocument = [](const QByteArray& value)
    {
        const int depth = 70;
        return createDocument({
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 100 100 ] /Resources << /Deep " + QByteArray(depth, '[') + value + QByteArray(depth, ']') + " >> >>"
        });
    };

    const PDFDocument deepExact = read(createDeepDocument("0.00999451"));
    const PDFDocument deepExactAgain = read(createDeepDocument("0.00999451"));
    const PDFDocument deepRounded = read(createDeepDocument("0.00999"));
    const PDFDocument deepOther = read(createDeepDocument("0.02"));
    QCOMPARE(PDFOCRPagePreparer::computePageFingerprint(&deepExactAgain, 0), PDFOCRPagePreparer::computePageFingerprint(&deepExact, 0));
    QVERIFY(PDFOCRPagePreparer::computePageFingerprint(&deepRounded, 0) != PDFOCRPagePreparer::computePageFingerprint(&deepExact, 0));
    QVERIFY(PDFOCRPagePreparer::computePageFingerprint(&deepOther, 0) != PDFOCRPagePreparer::computePageFingerprint(&deepExact, 0));
}

void DocumentWriterTest::writtenSizeBenchmark()
{
    // A measurement, not a test of the default run: PDF4QT_WRITER_SIZE_FILES is a text file with one PDF per line.
    // Prints the size of a whole write against the numbers of five decimal places and the keys of the dictionaries
    // responsible for the growth, and checks that every real number of every file survives the write.
    const QString listFile = qEnvironmentVariable("PDF4QT_WRITER_SIZE_FILES");
    if (listFile.isEmpty())
    {
        QSKIP("PDF4QT_WRITER_SIZE_FILES is not set");
    }

    QFile list(listFile);
    QVERIFY(list.open(QFile::ReadOnly | QFile::Text));

    struct Statistics
    {
        qint64 realCount = 0;
        qint64 growth = 0;
        std::map<QByteArray, qint64> growthOfKeys;
        std::vector<PDFReal> values;
    };

    std::function<void(const PDFObject&, const QByteArray&, Statistics&)> walk = [&walk](const PDFObject& object, const QByteArray& key, Statistics& statistics)
    {
        if (object.isReal())
        {
            const qint64 growth = PDFDocumentWriter::getSerializedObject(object).size() - (QByteArray::number(object.getReal(), 'f', 5).size() + 1);
            statistics.realCount += 1;
            statistics.growth += growth;
            statistics.growthOfKeys[key] += growth;
            statistics.values.push_back(object.getReal());
        }
        else if (object.isArray())
        {
            const PDFArray* array = object.getArray();
            for (size_t i = 0; i < array->getCount(); ++i)
            {
                walk(array->getItem(i), key, statistics);
            }
        }
        else if (object.isDictionary() || object.isStream())
        {
            const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : object.getDictionary();
            for (size_t i = 0; i < dictionary->getCount(); ++i)
            {
                walk(dictionary->getValue(i), dictionary->getKey(i).getString(), statistics);
            }
        }
    };

    const auto collect = [&walk](const PDFDocument& document)
    {
        Statistics statistics;
        for (const PDFObjectStorage::Entry& entry : document.getStorage().getObjects())
        {
            walk(entry.object, QByteArray(), statistics);
        }
        return statistics;
    };

    const auto refusePassword = [](bool* ok)
    {
        *ok = false;
        return QString();
    };

    while (!list.atEnd())
    {
        const QString fileName = QString::fromUtf8(list.readLine()).trimmed();
        if (fileName.isEmpty())
        {
            continue;
        }

        PDFDocumentReader reader(nullptr, refusePassword, false, false);
        const PDFDocument document = reader.readFromFile(fileName);
        if (reader.getReadingResult() != PDFDocumentReader::Result::OK || !document.getStorage().getSecurityHandler()->isEncryptionAllowed())
        {
            qInfo().noquote() << "SKIP" << fileName;
            continue;
        }

        const Statistics statistics = collect(document);
        const QByteArray written = write(document);
        const qint64 oldSize = written.size() - statistics.growth;

        std::vector<std::pair<qint64, QByteArray>> keys;
        for (const auto& [key, growth] : statistics.growthOfKeys)
        {
            keys.emplace_back(growth, key);
        }
        std::sort(keys.begin(), keys.end(), std::greater<>());

        QStringList topKeys;
        for (size_t i = 0; i < qMin(keys.size(), size_t(4)); ++i)
        {
            topKeys << QString("%1:%2").arg(QString::fromLatin1(keys[i].second)).arg(keys[i].first);
        }

        // Every real number comes back as the same double
        PDFDocumentReader readerBack(nullptr, refusePassword, false, false);
        const PDFDocument writtenDocument = readerBack.readFromBuffer(written);
        QVERIFY2(readerBack.getReadingResult() == PDFDocumentReader::Result::OK, qPrintable(fileName + ": " + readerBack.getErrorMessage()));

        const Statistics writtenStatistics = collect(writtenDocument);
        QCOMPARE(writtenStatistics.values.size(), statistics.values.size());
        for (size_t i = 0; i < statistics.values.size(); ++i)
        {
            QVERIFY2(isSameDouble(writtenStatistics.values[i], statistics.values[i]), qPrintable(fileName));
        }

        qInfo().noquote() << QString("SIZE|%1|%2|%3|%4|%5|%6|%7").arg(QFileInfo(fileName).fileName()).arg(QFileInfo(fileName).size()).arg(oldSize).arg(written.size()).arg(100.0 * statistics.growth / oldSize, 0, 'f', 3).arg(statistics.realCount).arg(topKeys.join(' '));
    }
}

#include "tst_documentwritertest.moc"
