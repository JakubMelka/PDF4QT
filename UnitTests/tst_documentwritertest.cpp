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

#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"

#include <QtTest>
#include <QBuffer>

using namespace pdf;

class DocumentWriterTest : public QObject
{
    Q_OBJECT

private slots:
    void objectStreamValidation();
    void incrementalUpdateOfClassicTable();
    void incrementalUpdateOfCrossReferenceStream();
    void incrementalUpdateRejectsUnreadableOriginal();

private:
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

QTEST_APPLESS_MAIN(DocumentWriterTest)

#include "tst_documentwritertest.moc"
