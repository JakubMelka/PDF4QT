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
    void oldFileWithRawCarriageReturnInIdentifier();
    void oldEncryptedStringWithRawCarriageReturn_data();
    void oldEncryptedStringWithRawCarriageReturn();
    void trailerSizeOfTheWrittenTable();
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

    for (int attempt = 0; attempt < 200; ++attempt)
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
