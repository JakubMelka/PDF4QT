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

#include "pdfdocumenttool_objectinspector.h"
#include "pdfobjectinspector.h"
#include "pdfoperationcontrol.h"
#include "pdfstreamfilters.h"
#include "mergetestpdf.h"
#include "testlocaleguard.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QtTest>

#include <atomic>

using namespace pdf;

/// Helpers of the tests of Object Inspector and of the limited decoding (plan of step 7 of phase 2, 4.3, 8)
class ObjectInspectorTestHelper
{
public:
    ObjectInspectorTestHelper() = delete;

    using Pdf = mergetest::MergeTestPdf;
    using Status = PDFLimitedStreamDecoder::Status;

    /// A document with the stream 5 of \p entries and \p content
    static PDFDocument createStreamDocument(const QByteArray& entries, const QByteArray& content)
    {
        Pdf pdf;
        pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
        pdf.set(2, "<< /Type /Pages /Kids [] /Count 0 >>");
        pdf.set(5, Pdf::stream(content, entries));
        return pdf.read();
    }

    /// Decodes the stream 5 of a document of createStreamDocument
    static PDFLimitedStreamDecoder::Result decode(const QByteArray& entries, const QByteArray& content, qint64 limit, const PDFOperationControl* control = nullptr)
    {
        const PDFDocument document = createStreamDocument(entries, content);
        const PDFObject& object = document.getStorage().getObject(PDFObjectReference(5, 0));
        if (!object.isStream())
        {
            return PDFLimitedStreamDecoder::Result();
        }
        const PDFObjectStorage& storage = document.getStorage();
        return PDFLimitedStreamDecoder::decode(object.getStream(), [&storage](const PDFObject& value) -> const PDFObject& { return storage.getObject(value); }, nullptr, limit, control);
    }

    /// Decodes the first \p prefixSize bytes of the stream 5 of a document of createStreamDocument
    static PDFLimitedStreamDecoder::Result decodePrefix(const QByteArray& entries, const QByteArray& content, qint64 prefixSize, qint64 limit, const PDFOperationControl* control = nullptr)
    {
        const PDFDocument document = createStreamDocument(entries, content);
        const PDFObject& object = document.getStorage().getObject(PDFObjectReference(5, 0));
        if (!object.isStream())
        {
            return PDFLimitedStreamDecoder::Result();
        }
        const PDFObjectStorage& storage = document.getStorage();
        return PDFLimitedStreamDecoder::decodePrefix(object.getStream(), [&storage](const PDFObject& value) -> const PDFObject& { return storage.getObject(value); }, nullptr,
                                                     prefixSize, limit, control);
    }

    /// The inspector document: two pages sharing the resources 10 (the image 6 without /Type), a content
    /// stream 5, a chain of \p actions actions by /Next from /OpenAction
    static Pdf createInspectorPdf(int actions)
    {
        Pdf pdf;
        pdf.set(1, "<< /Type /Catalog /Pages 2 0 R /OpenAction 20 0 R >>");
        pdf.set(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>");
        pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 10 10] /Resources 10 0 R /Contents 5 0 R >>");
        pdf.set(4, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 10 10] /Resources 10 0 R >>");
        pdf.set(5, Pdf::stream("BT ET"));
        pdf.set(6, Pdf::stream(QByteArray("\x00\xff", 2), "/Subtype /Image /Width 2 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray"));
        pdf.set(10, "<< /XObject << /Im 6 0 R >> >>");
        for (int i = 0; i < actions; ++i)
        {
            pdf.set(20 + i, i + 1 < actions ? "<< /S /Named /N /NextPage /Next " + QByteArray::number(21 + i) + " 0 R >>" : QByteArray("<< /S /Named /N /NextPage >>"));
        }
        return pdf;
    }

    /// The child of \p key
    static PDFObjectInspectorNode child(const std::vector<PDFObjectInspectorNode>& children, const QByteArray& key)
    {
        for (const PDFObjectInspectorNode& node : children)
        {
            if (node.key == key)
            {
                return node;
            }
        }
        return PDFObjectInspectorNode();
    }

    static QString name(const char* text) { return QString::fromLatin1(text); }

    /// The value of the dictionary entry \p key of the JSON of an object
    static QJsonObject entry(const QJsonObject& object, const QString& key)
    {
        for (const QJsonValue& value : object.value(QStringLiteral("entries")).toArray())
        {
            if (value.toObject().value(QStringLiteral("key")).toString() == key)
            {
                return value.toObject().value(QStringLiteral("value")).toObject();
            }
        }
        return QJsonObject();
    }

    static PDFDocumentToolResult runTool(const PDFDocumentPointer& document, const QVariantMap& parameters)
    {
        const PDFDocumentToolObjectInspector tool;
        const PDFDocumentToolContext context = PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(document, QByteArray(), QString(), true));
        return tool.run(context, parameters, nullptr, nullptr);
    }

    static qint64 getTimeLimit()
    {
        bool ok = false;
        const int factor = qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR", &ok);
        return qint64(10000) * ((ok && factor > 0) ? factor : 1);
    }

    class Canceled : public PDFOperationControl
    {
    public:
        virtual bool isOperationCanceled() const override { return true; }
    };

    /// Counts the polls of the cancellation: the decoding loops poll after each MiB of their output
    /// (the contract of PDFLimitedStreamDecoder), so the polls say how much a decoder has written
    class CountingControl : public PDFOperationControl
    {
    public:
        virtual bool isOperationCanceled() const override { ++m_polls; return false; }
        int getPolls() const { return m_polls.load(); }

    private:
        mutable std::atomic<int> m_polls = 0;
    };
};

class ObjectInspectorTest : public QObject
{
    Q_OBJECT

private slots:
    void decoderFilters();
    void decoderLimits();
    void decoderBomb();
    void decoderStatus();
    void decoderPrefix();
    void inspectorViews();
    void inspectorTree();
    void inspectorDepthLimit();
    void inspectorObjectList();
    void inspectorTrailerOfCrossReferenceStream();
    void inspectorObjectStreams();
    void inspectorObjectStreamHeaderOnly();
    void inspectorTexts();
    void inspectorDecodeStream();
    void inspectorImage();
    void toolObject();
    void toolList();
    void toolTextsBounded();
    void toolFactsInCLocale();
};

void ObjectInspectorTest::decoderFilters()
{
    // The decoders of ISO 32000-2, 7.4 with the examples of the specification
    using H = ObjectInspectorTestHelper;
    constexpr qint64 LIMIT = 1024;

    // 7.4.2 ASCIIHexDecode: white space ignored, '>' the end
    PDFLimitedStreamDecoder::Result result = H::decode("/Filter /ASCIIHexDecode", "4142 43>", LIMIT);
    QCOMPARE(result.status, H::Status::Decoded);
    QCOMPARE(result.data, QByteArray("ABC"));

    // An odd number of digits: "as if a 0 followed the last digit" - also with white space between them
    result = H::decode("/Filter /ASCIIHexDecode", "41 42\n4>", LIMIT);
    QCOMPARE(result.data, QByteArray("AB@"));

    // 7.4.3 ASCII85Decode: "87cUR" is "Hell" (0x48656C6C in base 85), 'z' four zero bytes
    result = H::decode("/Filter /ASCII85Decode", "87cURz~>", LIMIT);
    QCOMPARE(result.status, H::Status::Decoded);
    QCOMPARE(result.data, QByteArray("Hell\0\0\0\0", 8));

    // 7.4.4.2, Example of LZWDecode: the codes 256 45 258 258 65 259 66 257 give "-----A---B"
    result = H::decode("/Filter /LZWDecode", QByteArray::fromHex("800B6050220C0C8501"), LIMIT);
    QCOMPARE(result.status, H::Status::Decoded);
    QCOMPARE(result.data, QByteArray("-----A---B"));

    // 7.4.5 RunLengthDecode: a length 2 copies 3 bytes, 254 repeats the next byte 257 - 254 = 3 times, 128 ends
    result = H::decode("/Filter /RunLengthDecode", QByteArray("\x02" "abc" "\xFE" "x" "\x80" "zz"), LIMIT);
    QCOMPARE(result.status, H::Status::Decoded);
    QCOMPARE(result.data, QByteArray("abcxxx"));

    // A literal run past the end of the data takes the data there are (no read behind them)
    result = H::decode("/Filter /RunLengthDecode", QByteArray("\x05" "ab"), LIMIT);
    QCOMPARE(result.status, H::Status::Decoded);
    QCOMPARE(result.data, QByteArray("ab"));

    // 7.4.4.4 FlateDecode with the PNG predictor Up (Table 8: /Predictor 12, a row of 2 bytes): the second row
    // adds the row above
    const QByteArray rows("\x02\x01\x02\x02\x01\x01", 6);
    result = H::decode("/Filter /FlateDecode /DecodeParms << /Predictor 12 /Columns 2 >>", PDFFlateDecodeFilter::compress(rows), LIMIT);
    QCOMPARE(result.status, H::Status::Decoded);
    QCOMPARE(result.data, QByteArray("\x01\x02\x02\x03", 4));

    // A chain of filters in their order (7.4.1): ASCIIHex of a Flate stream
    const QByteArray text("BT /F1 12 Tf (Hello) Tj ET");
    result = H::decode("/Filter [/ASCIIHexDecode /FlateDecode]", PDFFlateDecodeFilter::compress(text).toHex() + ">", LIMIT);
    QCOMPARE(result.status, H::Status::Decoded);
    QCOMPARE(result.data, text);

    // No filter: the data as they are
    result = H::decode(QByteArray(), "raw data", LIMIT);
    QCOMPARE(result.status, H::Status::Decoded);
    QCOMPARE(result.data, QByteArray("raw data"));
}

void ObjectInspectorTest::decoderLimits()
{
    // R7-3, Q7.18: every filter stops, when its output would exceed the limit - also an intermediate output
    using H = ObjectInspectorTestHelper;
    constexpr qint64 MIB = 1024 * 1024;

    // The output of FlateDecode (1.2 MiB of hex digits) exceeds 1 MiB, although the final output (600 KiB)
    // would not
    const QByteArray zeros(600 * 1024, '\0');
    const QByteArray flateOfHex = PDFFlateDecodeFilter::compress(zeros.toHex());
    PDFLimitedStreamDecoder::Result result = H::decode("/Filter [/FlateDecode /ASCIIHexDecode]", flateOfHex, MIB);
    QCOMPARE(result.status, H::Status::LimitExceeded);
    QCOMPARE(result.filterName, QByteArray("FlateDecode"));
    QVERIFY(result.data.isEmpty());
    QVERIFY(!result.errorMessage.isEmpty());
    result = H::decode("/Filter [/FlateDecode /ASCIIHexDecode]", flateOfHex, 2 * MIB);
    QCOMPARE(result.status, H::Status::Decoded);
    QCOMPARE(result.data, zeros);

    // Nested Flate: the second filter gives 4 MiB
    const QByteArray nested = PDFFlateDecodeFilter::compress(PDFFlateDecodeFilter::compress(QByteArray(4 * MIB, '\0')));
    result = H::decode("/Filter [/FlateDecode /FlateDecode]", nested, MIB);
    QCOMPARE(result.status, H::Status::LimitExceeded);
    QCOMPARE(result.filterName, QByteArray("FlateDecode"));

    // RunLength (a repeat run gives 128 bytes of 2), ASCII85 ('z' gives 4 bytes of 1), LZW
    QByteArray runs;
    for (int i = 0; i < 1000; ++i)
    {
        runs += QByteArray("\x81" "x");
    }
    QCOMPARE(H::decode("/Filter /RunLengthDecode", runs, 128 * 1000).status, H::Status::Decoded);
    result = H::decode("/Filter /RunLengthDecode", runs, 128 * 1000 - 1);
    QCOMPARE(result.status, H::Status::LimitExceeded);
    QCOMPARE(result.filterName, QByteArray("RunLengthDecode"));
    QCOMPARE(H::decode("/Filter /ASCII85Decode", QByteArray(1000, 'z'), 4000).status, H::Status::Decoded);
    QCOMPARE(H::decode("/Filter /ASCII85Decode", QByteArray(1000, 'z'), 3999).status, H::Status::LimitExceeded);
    QCOMPARE(H::decode("/Filter /LZWDecode", QByteArray::fromHex("800B6050220C0C8501"), 10).status, H::Status::Decoded);
    QCOMPARE(H::decode("/Filter /LZWDecode", QByteArray::fromHex("800B6050220C0C8501"), 9).status, H::Status::LimitExceeded);

    // The rows of a predictor (Table 8) are checked before it allocates them: rows of 137 GB of a hostile
    // /Columns (an error, nothing is allocated), rows of 100 MB over the limit
    const QByteArray flate = PDFFlateDecodeFilter::compress(QByteArray(10, '\0'));
    result = H::decode("/Filter /FlateDecode /DecodeParms << /Predictor 12 /Columns 2147483647 /Colors 32 /BitsPerComponent 16 >>", flate, MIB);
    QCOMPARE(result.status, H::Status::Error);
    QCOMPARE(result.filterName, QByteArray("FlateDecode"));
    result = H::decode("/Filter /FlateDecode /DecodeParms << /Predictor 12 /Columns 100000000 >>", flate, MIB);
    QCOMPARE(result.status, H::Status::LimitExceeded);
    result = H::decode("/Filter /FlateDecode /DecodeParms << /Predictor 2 /Columns 100000000 >>", flate, MIB);
    QCOMPARE(result.status, H::Status::LimitExceeded);
}

void ObjectInspectorTest::decoderBomb()
{
    // Plan 8: a stream of 300 MiB of zeros (a few hundred kilobytes in the file) against the limit of the
    // inspector, 256 MiB - an error, no 300 MiB are decoded
    using H = ObjectInspectorTestHelper;
    QByteArray bomb;
    {
        const QByteArray zeros(qsizetype(300) * 1024 * 1024, '\0');
        bomb = PDFFlateDecodeFilter::compress(zeros);
    }

    QElapsedTimer timer;
    timer.start();
    H::CountingControl control;
    const PDFLimitedStreamDecoder::Result result = H::decode("/Filter /FlateDecode", bomb, PDFObjectInspector::INSPECTOR_MAXIMUM_DECODED_SIZE, &control);
    QCOMPARE(result.status, H::Status::LimitExceeded);
    QVERIFY(result.data.isEmpty());
    QVERIFY(timer.elapsed() < H::getTimeLimit());

    // The decoder stops at the limit, it never writes the 300 MiB: one poll before the filter, one per MiB
    // of the output up to 256 MiB
    QVERIFY(control.getPolls() <= 1 + 256);

    // The other loops of the bombs: RunLength (a run of 2 bytes gives 128 bytes) and ASCII85 ('z' gives 4
    // bytes) of 8 MiB against the limit of 1 MiB
    constexpr qint64 MIB = 1024 * 1024;
    QByteArray runs;
    for (qint64 i = 0; i < 8 * MIB / 128; ++i)
    {
        runs += QByteArray("\x81" "x");
    }
    H::CountingControl runControl;
    QCOMPARE(H::decode("/Filter /RunLengthDecode", runs, MIB, &runControl).status, H::Status::LimitExceeded);
    QVERIFY(runControl.getPolls() <= 1 + 1);
    H::CountingControl asciiControl;
    QCOMPARE(H::decode("/Filter /ASCII85Decode", QByteArray(2 * MIB, 'z'), MIB, &asciiControl).status, H::Status::LimitExceeded);
    QVERIFY(asciiControl.getPolls() <= 1 + 1);
}

void ObjectInspectorTest::decoderStatus()
{
    using H = ObjectInspectorTestHelper;
    constexpr qint64 LIMIT = 1024;

    // An image filter (FD-3): the data before it, encoded by it
    PDFLimitedStreamDecoder::Result result = H::decode("/Filter /DCTDecode", "jpeg", LIMIT);
    QCOMPARE(result.status, H::Status::Encoded);
    QCOMPARE(result.filterName, QByteArray("DCTDecode"));
    QCOMPARE(result.data, QByteArray("jpeg"));
    result = H::decode("/Filter [/FlateDecode /JPXDecode]", PDFFlateDecodeFilter::compress("jpx"), LIMIT);
    QCOMPARE(result.status, H::Status::Encoded);
    QCOMPARE(result.filterName, QByteArray("JPXDecode"));
    QCOMPARE(result.data, QByteArray("jpx"));

    // An unknown filter by its name (FD-4)
    result = H::decode("/Filter /Unknown", "x", LIMIT);
    QCOMPARE(result.status, H::Status::Encoded);
    QCOMPARE(result.filterName, QByteArray("Unknown"));

    // A damaged Flate stream: an error with the name of the filter and the message of zlib (482:16403)
    result = H::decode("/Filter /FlateDecode", "not a zlib stream", LIMIT);
    QCOMPARE(result.status, H::Status::Error);
    QCOMPARE(result.filterName, QByteArray("FlateDecode"));
    QVERIFY(result.errorMessage.startsWith(QStringLiteral("Error in the FlateDecode filter: ")));
    QVERIFY(result.data.isEmpty());

    // An invalid code of LZW (an exception of the decoder, never to the caller)
    result = H::decode("/Filter /LZWDecode", QByteArray::fromHex("FFFFFF"), LIMIT);
    QCOMPARE(result.status, H::Status::Error);
    QCOMPARE(result.filterName, QByteArray("LZWDecode"));

    // An item of /Filter, which is no name
    QCOMPARE(H::decode("/Filter [/FlateDecode 1]", "x", LIMIT).status, H::Status::Error);

    // Canceled
    H::Canceled canceled;
    QCOMPARE(H::decode("/Filter /FlateDecode", PDFFlateDecodeFilter::compress("abc"), LIMIT, &canceled).status, H::Status::Canceled);

    // No stream
    QCOMPARE(PDFLimitedStreamDecoder::decode(nullptr, [](const PDFObject& object) -> const PDFObject& { return object; }, nullptr, LIMIT, nullptr).status, H::Status::Error);
}

void ObjectInspectorTest::decoderPrefix()
{
    // The filters decode their data in their order (ISO 32000-2, 7.4): the first n bytes of the decoding of a
    // prefix are the first n bytes of the whole decoding - for each filter of 7.4, a chain and a predictor
    using H = ObjectInspectorTestHelper;
    constexpr qint64 LIMIT = 1024;
    const QByteArray text("BT /F1 12 Tf (Hello) Tj ET");
    QByteArray runs;
    for (int i = 0; i < 20; ++i)
    {
        runs += QByteArray("\x02" "abc" "\xFE") + char('A' + i);
    }
    const std::vector<std::pair<QByteArray, QByteArray>> streams = {
        { "/Filter /FlateDecode", PDFFlateDecodeFilter::compress(text) },
        { "/Filter /ASCII85Decode", "87cURz87cUR~>" },
        { "/Filter /LZWDecode", QByteArray::fromHex("800B6050220C0C8501") },
        { "/Filter /RunLengthDecode", runs + "\x80" },
        { "/Filter /ASCIIHexDecode", text.toHex() + ">" },
        { "/Filter [/ASCIIHexDecode /FlateDecode]", PDFFlateDecodeFilter::compress(text).toHex() + ">" },
        { "/Filter /FlateDecode /DecodeParms << /Predictor 12 /Columns 2 >>", PDFFlateDecodeFilter::compress(QByteArray("\x02\x01\x02\x02\x01\x01\x02\x05\x05", 9)) }
    };
    for (const auto& [entries, content] : streams)
    {
        const PDFLimitedStreamDecoder::Result whole = H::decode(entries, content, LIMIT);
        QCOMPARE(whole.status, H::Status::Decoded);
        for (const qint64 prefixSize : { qint64(0), qint64(1), qint64(3), qint64(7), qint64(whole.data.size()), qint64(whole.data.size() + 10) })
        {
            const PDFLimitedStreamDecoder::Result prefix = H::decodePrefix(entries, content, prefixSize, LIMIT);
            QCOMPARE(prefix.status, H::Status::Decoded);
            QCOMPARE(prefix.data, whole.data.left(prefixSize));
        }
    }

    // The header of 10 bytes of 8 MiB of data against the limit of 1 MiB: the whole decoding exceeds the limit,
    // the prefix stops after its bytes - the loop polls the cancellation after each MiB of its output, so it
    // writes less than 1 MiB (one poll before the filter)
    constexpr qint64 MIB = 1024 * 1024;
    const QByteArray header("1 0 2 5 3 ");
    const QByteArray flate = PDFFlateDecodeFilter::compress(header + QByteArray(8 * MIB, ' '));
    QCOMPARE(H::decode("/Filter /FlateDecode", flate, MIB).status, H::Status::LimitExceeded);
    H::CountingControl control;
    const PDFLimitedStreamDecoder::Result prefix = H::decodePrefix("/Filter /FlateDecode", flate, header.size(), MIB, &control);
    QCOMPARE(prefix.status, H::Status::Decoded);
    QCOMPARE(prefix.data, header);
    QCOMPARE(control.getPolls(), 1);

    // An intermediate filter keeps its limit: Flate of 1.2 MiB of hex digits, of which the prefix needs 2 bytes
    const QByteArray flateOfHex = PDFFlateDecodeFilter::compress(QByteArray(600 * 1024, 'x').toHex());
    QCOMPARE(H::decodePrefix("/Filter [/FlateDecode /ASCIIHexDecode]", flateOfHex, 1, MIB).status, H::Status::LimitExceeded);

    // A prefix over the limit is the whole decoding with its limit
    QCOMPARE(H::decodePrefix("/Filter /FlateDecode", flate, 2 * MIB, MIB).status, H::Status::LimitExceeded);
}

void ObjectInspectorTest::inspectorViews()
{
    // The 13 views (482:15054) and their counts
    using H = ObjectInspectorTestHelper;
    const PDFDocumentPointer document = H::createInspectorPdf(5).readPointer();
    const PDFObjectInspector inspector(document);
    QVERIFY(inspector.prepare(nullptr));

    QCOMPARE(inspector.getRootCount(PDFObjectInspectorView::Document), qint64(1));
    QCOMPARE(inspector.getRootCount(PDFObjectInspectorView::Pages), qint64(2));
    QCOMPARE(inspector.getRootCount(PDFObjectInspectorView::ContentStreams), qint64(1));
    QCOMPARE(inspector.getRootCount(PDFObjectInspectorView::Images), qint64(1));
    QCOMPARE(inspector.getRootCount(PDFObjectInspectorView::Actions), qint64(5));
    QCOMPARE(inspector.getRootCount(PDFObjectInspectorView::Fonts), qint64(0));
    // The trailer and the 12 objects 1-6, 10, 20-24
    QCOMPARE(inspector.getRootCount(PDFObjectInspectorView::Objects), qint64(1 + 12));

    PDFObjectInspector::OpenedReferences opened;
    const std::vector<PDFObjectInspectorNode> pages = inspector.getRoots(PDFObjectInspectorView::Pages, &opened);
    QCOMPARE(pages.size(), size_t(2));
    QCOMPARE(pages[0].reference, PDFObjectReference(3, 0));
    QCOMPARE(pages[1].reference, PDFObjectReference(4, 0));
    QVERIFY(pages[0].hasChildren);
    QCOMPARE(opened, PDFObjectInspector::OpenedReferences({ PDFObjectReference(3, 0), PDFObjectReference(4, 0) }));

    const std::vector<PDFObjectInspectorNode> images = inspector.getRoots(PDFObjectInspectorView::Images, nullptr);
    QCOMPARE(images.size(), size_t(1));
    QCOMPARE(images.front().reference, PDFObjectReference(6, 0));

    // Each view by its identifier
    for (const PDFObjectInspectorView view : PDFObjectInspector::getViews())
    {
        QCOMPARE(PDFObjectInspector::findView(PDFObjectInspector::getViewId(view)), std::optional<PDFObjectInspectorView>(view));
    }
    QCOMPARE(PDFObjectInspector::getViews().size(), size_t(13));
}

void ObjectInspectorTest::inspectorTree()
{
    // The lazy tree (B158, B207): "A reference is opened only once" (482:14847) - one visited set of a view;
    // the cycle /Parent of the page tree and the resources 10 shared by both pages
    using H = ObjectInspectorTestHelper;
    const PDFDocumentPointer document = H::createInspectorPdf(5).readPointer();
    const PDFObjectInspector inspector(document);

    PDFObjectInspector::OpenedReferences opened;
    const std::vector<PDFObjectInspectorNode> roots = inspector.getRoots(PDFObjectInspectorView::Document, &opened);
    QCOMPARE(roots.size(), size_t(1));
    QVERIFY(roots.front().isTrailer);
    QVERIFY(roots.front().hasChildren);

    const PDFObjectInspectorNode catalog = H::child(inspector.getChildren(roots.front(), &opened), "Root");
    QCOMPARE(catalog.reference, PDFObjectReference(1, 0));
    QCOMPARE(catalog.depth, 1);
    QVERIFY(catalog.hasChildren);
    QVERIFY(!catalog.isRepeated);

    const std::vector<PDFObjectInspectorNode> catalogChildren = inspector.getChildren(catalog, &opened);
    QCOMPARE(catalogChildren.size(), size_t(3));
    QCOMPARE(catalogChildren[0].key, QByteArray("Type"));
    QVERIFY(!catalogChildren[0].hasChildren);
    QCOMPARE(catalogChildren[0].container, PDFObjectReference(1, 0));

    const PDFObjectInspectorNode pageTree = H::child(catalogChildren, "Pages");
    const PDFObjectInspectorNode kids = H::child(inspector.getChildren(pageTree, &opened), "Kids");
    QVERIFY(!kids.reference.isValid());
    QCOMPARE(kids.container, PDFObjectReference(2, 0));
    const std::vector<PDFObjectInspectorNode> pageNodes = inspector.getChildren(kids, &opened);
    QCOMPARE(pageNodes.size(), size_t(2));
    QCOMPARE(pageNodes[1].arrayIndex, PDFInteger(1));

    // The cycle: the parent of a page is the page tree opened above
    const std::vector<PDFObjectInspectorNode> firstPage = inspector.getChildren(pageNodes[0], &opened);
    QVERIFY(H::child(firstPage, "Parent").isRepeated);
    QVERIFY(!H::child(firstPage, "Parent").hasChildren);
    QVERIFY(!H::child(firstPage, "Resources").isRepeated);
    QVERIFY(H::child(firstPage, "Resources").hasChildren);

    // The shared resources: opened at the first page, a repeated node at the second
    const std::vector<PDFObjectInspectorNode> secondPage = inspector.getChildren(pageNodes[1], &opened);
    QVERIFY(H::child(secondPage, "Resources").isRepeated);
    QVERIFY(inspector.getChildren(H::child(secondPage, "Resources"), &opened).empty());

    // A leaf has no children
    QVERIFY(inspector.getChildren(catalogChildren[0], &opened).empty());

    // The Pages view: the first page opens the page tree, the second shows it repeated (482:15358)
    PDFObjectInspector::OpenedReferences pagesOpened;
    const std::vector<PDFObjectInspectorNode> pages = inspector.getRoots(PDFObjectInspectorView::Pages, &pagesOpened);
    const PDFObjectInspectorNode firstParent = H::child(inspector.getChildren(pages[0], &pagesOpened), "Parent");
    QVERIFY(!firstParent.isRepeated);
    QVERIFY(H::child(inspector.getChildren(pages[1], &pagesOpened), "Parent").isRepeated);
    // The pages below the page tree are roots of the view: repeated
    for (const PDFObjectInspectorNode& kid : inspector.getChildren(H::child(inspector.getChildren(firstParent, &pagesOpened), "Kids"), &pagesOpened))
    {
        QVERIFY(kid.isRepeated);
    }
}

void ObjectInspectorTest::inspectorDepthLimit()
{
    // Plan 8: a chain of 100 000 actions by /Next - the views list them all (no recursion), the path of the
    // tree ends at the depth MAXIMUM_PATH_DEPTH (B158 / B207, R7-22 (1))
    using H = ObjectInspectorTestHelper;
    constexpr int ACTIONS = 100000;
    const PDFDocumentPointer document = H::createInspectorPdf(ACTIONS).readPointer();
    const PDFObjectInspector inspector(document);

    QElapsedTimer timer;
    timer.start();
    QCOMPARE(inspector.getRootCount(PDFObjectInspectorView::Actions), qint64(ACTIONS));

    PDFObjectInspector::OpenedReferences opened;
    PDFObjectInspectorNode node = H::child(inspector.getChildren(inspector.getRoots(PDFObjectInspectorView::Document, &opened).front(), &opened), "Root");
    node = H::child(inspector.getChildren(node, &opened), "OpenAction");
    QCOMPARE(node.depth, 2);
    while (node.hasChildren)
    {
        node = H::child(inspector.getChildren(node, &opened), "Next");
    }
    QCOMPARE(node.depth, PDFObjectInspector::MAXIMUM_PATH_DEPTH);
    QCOMPARE(node.reference, PDFObjectReference(20 + PDFObjectInspector::MAXIMUM_PATH_DEPTH - 2, 0));
    QVERIFY(!node.isRepeated);
    QVERIFY(inspector.getChildren(node, &opened).empty());
    QVERIFY(timer.elapsed() < H::getTimeLimit());
}

void ObjectInspectorTest::inspectorObjectList()
{
    // "References are not followed in the object list - every object is listed once by its number" (482:16200)
    using H = ObjectInspectorTestHelper;
    const PDFDocumentPointer document = H::createInspectorPdf(2).readPointer();
    const PDFObjectInspector inspector(document);

    PDFObjectInspector::OpenedReferences opened;
    const std::vector<PDFObjectInspectorNode> first = inspector.getRoots(PDFObjectInspectorView::Objects, &opened, 0, 3);
    QCOMPARE(first.size(), size_t(3));
    QVERIFY(first[0].isTrailer);
    QCOMPARE(first[1].reference, PDFObjectReference(1, 0));
    QCOMPARE(first[2].reference, PDFObjectReference(2, 0));

    // A window by the order of the list (the trailer is its item 0), the free numbers left out
    const std::vector<PDFObjectInspectorNode> window = inspector.getRoots(PDFObjectInspectorView::Objects, &opened, 6, 3);
    QCOMPARE(window.size(), size_t(3));
    QCOMPARE(window[0].reference, PDFObjectReference(6, 0));
    QCOMPARE(window[1].reference, PDFObjectReference(10, 0));
    QCOMPARE(window[2].reference, PDFObjectReference(20, 0));
    QVERIFY(inspector.getRoots(PDFObjectInspectorView::Objects, &opened, 100, 3).empty());

    // A reference is a repeated node (the object is a root of the list), a reference to nothing is not
    const std::vector<PDFObjectInspectorNode> catalog = inspector.getChildren(first[1], &opened);
    QVERIFY(H::child(catalog, "Pages").isRepeated);
    QVERIFY(!H::child(catalog, "Pages").hasChildren);
    QVERIFY(opened.empty());

    // The children in parts (a large array is fetched by windows)
    QCOMPARE(PDFObjectInspector::getChildCount(first[1]), qint64(3));
    QCOMPARE(PDFObjectInspector::getChildCount(H::child(catalog, "Pages")), qint64(0));
    const std::vector<PDFObjectInspectorNode> part = inspector.getChildren(first[1], &opened, 1, 5);
    QCOMPARE(part.size(), size_t(2));
    QCOMPARE(part[0].key, QByteArray("Pages"));
    QCOMPARE(part[1].key, QByteArray("OpenAction"));
    QVERIFY(inspector.getChildren(first[1], &opened, 3, 5).empty());
}

void ObjectInspectorTest::inspectorTrailerOfCrossReferenceStream()
{
    // FD-6: the trailer of a file with a cross-reference stream (ISO 32000-2, 7.5.8.2) is the dictionary of
    // the stream; the stream itself is the file structure
    using H = ObjectInspectorTestHelper;
    const QByteArray bytes = H::createInspectorPdf(1).writeWithCrossReferenceStream();
    const PDFDocumentPointer document(new PDFDocument(mergetest::MergeTestPdf::readBytes(bytes)));
    const PDFObjectInspector inspector(document);

    const PDFObjectInspectorNode trailer = inspector.getRoots(PDFObjectInspectorView::Document, nullptr).front();
    QVERIFY(trailer.isTrailer);
    QVERIFY(trailer.object.isDictionary());
    QVERIFY(trailer.object.getDictionary()->hasKey("Root"));
    QVERIFY(trailer.object.getDictionary()->hasKey("W"));
    QVERIFY(H::child(inspector.getChildren(trailer, nullptr), "Root").hasChildren);
}

void ObjectInspectorTest::inspectorObjectStreams()
{
    // FD-6 by the header of an object stream (ISO 32000-2, 7.5.7: /N pairs of the object number and the
    // offset in the first /First bytes): "in object stream 40 0 R"
    using Pdf = mergetest::MergeTestPdf;
    Pdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [] /Count 0 >>");
    pdf.set(40, Pdf::stream(PDFFlateDecodeFilter::compress("50 0 51 3 52 6 <<>><<>><<>>"), "/Type /ObjStm /N 2 /First 15 /Filter /FlateDecode"));
    pdf.set(50, "<< >>");
    pdf.set(51, "<< >>");
    pdf.set(52, "<< >>");
    const PDFDocumentPointer document = pdf.readPointer();

    const PDFObjectInspector::ObjectStreams objectStreams = PDFObjectInspector::findObjectStreams(document.data(), nullptr);
    QCOMPARE(objectStreams, PDFObjectInspector::ObjectStreams({ { 50, PDFObjectReference(40, 0) }, { 51, PDFObjectReference(40, 0) } }));

    PDFObjectInspector inspector(document);
    inspector.setObjectStreams(objectStreams);
    const std::vector<PDFObjectInspectorNode> roots = inspector.getRoots(PDFObjectInspectorView::Objects, nullptr);
    for (const PDFObjectInspectorNode& node : roots)
    {
        const bool isInStream = node.reference == PDFObjectReference(50, 0) || node.reference == PDFObjectReference(51, 0);
        QCOMPARE(node.objectStream, isInStream ? PDFObjectReference(40, 0) : PDFObjectReference());
        QCOMPARE(node.objectStreamNote, isInStream ? QStringLiteral("in object stream 40 0 R") : QString());
    }
}

void ObjectInspectorTest::inspectorObjectStreamHeaderOnly()
{
    // RS7CORE: the numbers of the objects of an object stream are its first /First bytes (ISO 32000-2, 7.5.7) -
    // only they are decoded. The objects of the stream are 32 MiB (white space after them): the decoder polls
    // the cancellation after each MiB of its output, so the polls say that the rest is not decoded.
    using Pdf = mergetest::MergeTestPdf;
    using H = ObjectInspectorTestHelper;
    constexpr qint64 MIB = 1024 * 1024;
    const QByteArray header("50 0 51 3 ");
    Pdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [] /Count 0 >>");
    pdf.set(40, Pdf::stream(PDFFlateDecodeFilter::compress(header + "<<>><<>>" + QByteArray(32 * MIB, ' ')),
                            "/Type /ObjStm /N 2 /First " + QByteArray::number(header.size()) + " /Filter /FlateDecode"));
    pdf.set(50, "<< >>");
    pdf.set(51, "<< >>");
    const PDFDocumentPointer document = pdf.readPointer();

    H::CountingControl control;
    const PDFObjectInspector::ObjectStreams objectStreams = PDFObjectInspector::findObjectStreams(document.data(), &control);
    QCOMPARE(objectStreams, PDFObjectInspector::ObjectStreams({ { 50, PDFObjectReference(40, 0) }, { 51, PDFObjectReference(40, 0) } }));

    // One poll for each object of the storage and one before the filter; the whole decoding would add 32
    const int objectCount = int(document->getStorage().getObjects().size());
    QVERIFY2(control.getPolls() <= objectCount + 1, qPrintable(QString::number(control.getPolls())));
}

void ObjectInspectorTest::inspectorTexts()
{
    // The description of the right panel (482:14847 "Dictionary [6 items]", 482:15358 "Stream [2 items, 4,806
    // data bytes]"), the PDF syntax of an object, the hex dump of Q7.12
    using H = ObjectInspectorTestHelper;
    const PDFDocumentPointer document = H::createInspectorPdf(1).readPointer();
    const PDFObjectInspector inspector(document);
    const std::vector<PDFObjectInspectorNode> objects = inspector.getRoots(PDFObjectInspectorView::Objects, nullptr);

    auto find = [&objects](int number)
    {
        for (const PDFObjectInspectorNode& node : objects)
        {
            if (node.reference == PDFObjectReference(number, 0))
            {
                return node;
            }
        }
        return PDFObjectInspectorNode();
    };

    QCOMPARE(inspector.describe(find(1)), QStringLiteral("Dictionary [3 items]"));
    QCOMPARE(inspector.describe(find(5)), QStringLiteral("Stream [1 items, 5 data bytes]"));
    // B185: an image without /Type, its size from its dictionary
    QCOMPARE(inspector.describe(find(6)), QStringLiteral("Stream [6 items, 2 data bytes, 2 x 1 pixels, 8 bits per component]"));

    PDFObjectInspectorNode value;
    value.object = PDFObject::createName("Catalog");
    QCOMPARE(inspector.describe(value), QStringLiteral("/Catalog"));
    value.object = PDFObject::createReference(PDFObjectReference(12, 0));
    QCOMPARE(inspector.describe(value), QStringLiteral("12 0 R"));
    value.object = PDFObject::createInteger(12406);
    QCOMPARE(inspector.describe(value), QStringLiteral("12406"));

    bool isTruncated = true;
    QCOMPARE(inspector.getSyntax(find(1), &isTruncated), QStringLiteral("<<\n  /Type /Catalog\n  /Pages 2 0 R\n  /OpenAction 20 0 R\n>>"));
    QVERIFY(!isTruncated);
    QCOMPARE(inspector.getSyntax(find(3), nullptr), QStringLiteral("<<\n  /Type /Page\n  /Parent 2 0 R\n  /MediaBox [ 0 0 10 10 ]\n  /Resources 10 0 R\n  /Contents 5 0 R\n>>"));
    value.object = PDFObject::createString(QByteArray("a(b)", 4));
    QCOMPARE(inspector.getSyntax(value, nullptr), QStringLiteral("(a\\(b\\))"));
    value.object = PDFObject::createString(QByteArray("\x00\xff", 2));
    QCOMPARE(inspector.getSyntax(value, nullptr), QStringLiteral("<00FF>"));
    value.object = PDFObject::createName("A B#");
    QCOMPARE(inspector.getSyntax(value, nullptr), QStringLiteral("/A#20B#23"));

    QCOMPARE(PDFObjectInspector::createHexDump(QByteArray("%PDF-1.7\n\0", 10)),
             QStringLiteral("00000000  25 50 44 46 2D 31 2E 37 0A 00                    |%PDF-1.7..|\n"));
    QCOMPARE(PDFObjectInspector::createHexDump(QByteArray(17, 'A')).count(QLatin1Char('\n')), 2);
    QVERIFY(PDFObjectInspector::isBinary(QByteArray("a\0b", 3)));
    QVERIFY(!PDFObjectInspector::isBinary(QByteArray("BT\r\n\t(x) Tj\fET")));
    QCOMPARE(PDFObjectInspector::toText(QByteArray("\xc4\x8d")), QString(QChar(0x010D)));
    QCOMPARE(PDFObjectInspector::toText(QByteArray("\xe9")), QString(QChar(0x00E9)));
}

void ObjectInspectorTest::inspectorDecodeStream()
{
    // The decoding of the inspector (B165 core, FD-3 - FD-5): the shown part at most 1 MiB, binary data marked
    using Pdf = mergetest::MergeTestPdf;
    constexpr qint64 MIB = 1024 * 1024;
    Pdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [] /Count 0 >>");
    pdf.set(5, Pdf::stream(PDFFlateDecodeFilter::compress("BT ET"), "/Filter /FlateDecode"));
    pdf.set(6, Pdf::stream(QByteArray("\x00\x01\x02", 3)));
    pdf.set(7, Pdf::stream(PDFFlateDecodeFilter::compress(QByteArray(2 * MIB, 'a')), "/Filter /FlateDecode"));
    pdf.set(8, Pdf::stream("jpeg", "/Filter /DCTDecode"));
    const PDFObjectInspector inspector(pdf.readPointer());

    PDFObjectInspector::DecodedStream decoded = inspector.decodeStream(PDFObjectReference(5, 0), PDFObjectInspector::INSPECTOR_MAXIMUM_DECODED_SIZE, nullptr);
    QCOMPARE(decoded.status, PDFLimitedStreamDecoder::Status::Decoded);
    QCOMPARE(decoded.data, QByteArray("BT ET"));
    QCOMPARE(decoded.decodedSize, qint64(5));
    QVERIFY(!decoded.isBinary);
    QVERIFY(!decoded.isTruncated);

    decoded = inspector.decodeStream(PDFObjectReference(6, 0), PDFObjectInspector::INSPECTOR_MAXIMUM_DECODED_SIZE, nullptr);
    QVERIFY(decoded.isBinary);

    decoded = inspector.decodeStream(PDFObjectReference(7, 0), PDFObjectInspector::INSPECTOR_MAXIMUM_DECODED_SIZE, nullptr);
    QCOMPARE(decoded.status, PDFLimitedStreamDecoder::Status::Decoded);
    QVERIFY(decoded.isTruncated);
    QCOMPARE(decoded.decodedSize, 2 * MIB);
    QCOMPARE(qint64(decoded.data.size()), PDFObjectInspector::MAXIMUM_SHOWN_BYTES);

    decoded = inspector.decodeStream(PDFObjectReference(7, 0), MIB, nullptr);
    QCOMPARE(decoded.status, PDFLimitedStreamDecoder::Status::LimitExceeded);
    QVERIFY(decoded.data.isEmpty());

    decoded = inspector.decodeStream(PDFObjectReference(8, 0), MIB, nullptr);
    QCOMPARE(decoded.status, PDFLimitedStreamDecoder::Status::Encoded);
    QCOMPARE(decoded.filterName, QByteArray("DCTDecode"));
    QVERIFY(decoded.errorMessage.contains(QStringLiteral("DCTDecode")));

    decoded = inspector.decodeStream(PDFObjectReference(1, 0), MIB, nullptr);
    QCOMPARE(decoded.status, PDFLimitedStreamDecoder::Status::Error);
    QCOMPARE(decoded.errorMessage, QStringLiteral("The object 1 0 R is not a stream."));
}

void ObjectInspectorTest::inspectorImage()
{
    // B185: an image XObject without /Type (Table 87: /Type is optional); its pixels by DeviceGray (8.6.4.2)
    using Pdf = mergetest::MergeTestPdf;
    Pdf pdf = ObjectInspectorTestHelper::createInspectorPdf(1);
    pdf.set(7, Pdf::stream(QByteArray(2, '\0'), "/Subtype /Image /Width 100000 /Height 100000 /BitsPerComponent 8 /ColorSpace /DeviceGray"));
    const PDFObjectInspector inspector(pdf.readPointer());

    QString errorMessage;
    const QImage image = inspector.getImage(PDFObjectReference(6, 0), nullptr, &errorMessage);
    QVERIFY(errorMessage.isEmpty());
    QCOMPARE(image.size(), QSize(2, 1));
    QCOMPARE(qGray(image.pixel(0, 0)), 0);
    QCOMPARE(qGray(image.pixel(1, 0)), 255);

    // A declared size over the limit is never decoded (SECURITY_TODO.md 2)
    QVERIFY(inspector.getImage(PDFObjectReference(7, 0), nullptr, &errorMessage).isNull());
    QCOMPARE(errorMessage, QStringLiteral("The image of 100000 x 100000 pixels is too large to be shown."));

    QVERIFY(inspector.getImage(PDFObjectReference(5, 0), nullptr, &errorMessage).isNull());
    QCOMPARE(errorMessage, QStringLiteral("The object 5 0 R is not an image."));
}

void ObjectInspectorTest::toolObject()
{
    // The tool for MCP (plan 7.1): an object as JSON, its references followed to "depth" levels with one
    // visited set; the content of a stream on request (bit 5)
    using H = ObjectInspectorTestHelper;
    const PDFDocumentPointer document = H::createInspectorPdf(2).readPointer();

    QVariantMap parameters;
    parameters.insert(QStringLiteral("object"), QStringLiteral("catalog"));
    PDFDocumentToolResult result = H::runTool(document, parameters);
    QVERIFY(result.isSuccess());
    QJsonObject object = result.data.value(QStringLiteral("object")).toObject();
    QCOMPARE(object.value(QStringLiteral("object")).toString(), QStringLiteral("1 0 R"));
    QCOMPARE(object.value(QStringLiteral("type")).toString(), QStringLiteral("dictionary"));
    QCOMPARE(object.value(QStringLiteral("entries")).toArray().size(), 3);
    QCOMPARE(object.value(QStringLiteral("entries")).toArray().at(0).toObject().value(QStringLiteral("key")).toString(), QStringLiteral("Type"));
    QCOMPARE(H::entry(object, QStringLiteral("Pages")).value(QStringLiteral("reference")).toString(), QStringLiteral("2 0 R"));
    QVERIFY(!H::entry(object, QStringLiteral("Pages")).contains(QStringLiteral("object")));
    QCOMPARE(result.data.value(QStringLiteral("truncated")).toBool(), false);

    // Depth 4 from the page 3: its parent 2, the kids of 2 - the page 3 itself is visited (no object), the
    // page 4 is followed
    parameters.insert(QStringLiteral("object"), QStringLiteral("3 0 R"));
    parameters.insert(QStringLiteral("depth"), 4);
    result = H::runTool(document, parameters);
    QVERIFY(result.isSuccess());
    object = result.data.value(QStringLiteral("object")).toObject();
    const QJsonObject parent = H::entry(object, QStringLiteral("Parent")).value(QStringLiteral("object")).toObject();
    const QJsonArray kids = H::entry(parent, QStringLiteral("Kids")).value(QStringLiteral("items")).toArray();
    QCOMPARE(kids.size(), 2);
    QVERIFY(!kids.at(0).toObject().contains(QStringLiteral("object")));
    QVERIFY(kids.at(1).toObject().contains(QStringLiteral("object")));

    // The trailer; a wrong text and a missing object fail
    parameters.insert(QStringLiteral("object"), QStringLiteral("trailer"));
    parameters.insert(QStringLiteral("depth"), 1);
    result = H::runTool(document, parameters);
    QCOMPARE(result.data.value(QStringLiteral("object")).toObject().value(QStringLiteral("object")).toString(), QStringLiteral("trailer"));
    parameters.insert(QStringLiteral("object"), QStringLiteral("root"));
    QCOMPARE(H::runTool(document, parameters).status, PDFDocumentToolResultStatus::Failed);
    parameters.insert(QStringLiteral("object"), QStringLiteral("999 0 R"));
    QCOMPARE(H::runTool(document, parameters).status, PDFDocumentToolResultStatus::Failed);

    // The content of a stream: a text, the binary data of the image as hexadecimal digits
    parameters.insert(QStringLiteral("object"), QStringLiteral("5 0 R"));
    parameters.insert(QStringLiteral("decodeStream"), true);
    object = H::runTool(document, parameters).data.value(QStringLiteral("object")).toObject();
    QCOMPARE(object.value(QStringLiteral("decodeStatus")).toString(), QStringLiteral("decoded"));
    QCOMPARE(object.value(QStringLiteral("content")).toString(), QStringLiteral("BT ET"));
    QCOMPARE(object.value(QStringLiteral("isBinary")).toBool(), false);
    QCOMPARE(object.value(QStringLiteral("streamLength")).toInteger(), qint64(5));
    parameters.insert(QStringLiteral("object"), QStringLiteral("6 0 R"));
    object = H::runTool(document, parameters).data.value(QStringLiteral("object")).toObject();
    QCOMPARE(object.value(QStringLiteral("content")).toString(), QStringLiteral("00ff"));
    QCOMPARE(object.value(QStringLiteral("isBinary")).toBool(), true);

    // maxBytes cuts the content
    parameters.insert(QStringLiteral("object"), QStringLiteral("5 0 R"));
    parameters.insert(QStringLiteral("maxBytes"), 2);
    result = H::runTool(document, parameters);
    QCOMPARE(result.data.value(QStringLiteral("object")).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("BT"));
    QCOMPARE(result.data.value(QStringLiteral("truncated")).toBool(), true);

    // Bit 5 for the content (Q7.2)
    const PDFDocumentToolObjectInspector tool;
    const std::vector<PDFSecurityHandler::Permission> permissions = tool.getRequiredPermissions(parameters);
    QVERIFY(std::find(permissions.cbegin(), permissions.cend(), PDFSecurityHandler::Permission::CopyContent) != permissions.cend());
}

void ObjectInspectorTest::toolList()
{
    // The roots of a view, a window by offset / limit
    using H = ObjectInspectorTestHelper;
    const PDFDocumentPointer document = H::createInspectorPdf(2).readPointer();

    QVariantMap parameters;
    parameters.insert(QStringLiteral("object"), QString());
    parameters.insert(QStringLiteral("view"), QStringLiteral("pages"));
    parameters.insert(QStringLiteral("limit"), 1);
    PDFDocumentToolResult result = H::runTool(document, parameters);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.data.value(QStringLiteral("total")).toInteger(), qint64(2));
    QCOMPARE(result.data.value(QStringLiteral("truncated")).toBool(), true);
    QJsonArray roots = result.data.value(QStringLiteral("roots")).toArray();
    QCOMPARE(roots.size(), 1);
    QCOMPARE(roots.at(0).toObject().value(QStringLiteral("object")).toString(), QStringLiteral("3 0 R"));
    QCOMPARE(roots.at(0).toObject().value(QStringLiteral("description")).toString(), QStringLiteral("Dictionary [5 items]"));

    parameters.insert(QStringLiteral("offset"), 1);
    result = H::runTool(document, parameters);
    roots = result.data.value(QStringLiteral("roots")).toArray();
    QCOMPARE(roots.size(), 1);
    QCOMPARE(roots.at(0).toObject().value(QStringLiteral("object")).toString(), QStringLiteral("4 0 R"));
    QCOMPARE(result.data.value(QStringLiteral("truncated")).toBool(), false);

    parameters.insert(QStringLiteral("view"), QStringLiteral("objects"));
    parameters.insert(QStringLiteral("offset"), 0);
    roots = H::runTool(document, parameters).data.value(QStringLiteral("roots")).toArray();
    QCOMPARE(roots.at(0).toObject().value(QStringLiteral("object")).toString(), QStringLiteral("trailer"));
}

void ObjectInspectorTest::toolTextsBounded()
{
    // RS7CORE: a string of a hostile file (7.3.4) is cut before its text is made, not after the whole JSON of
    // MCP: 64 KiB of one string (its length and the mark textTruncated), 4 MiB of all strings of the object
    // (the JSON ends there, "truncated"); the description of a string has at most MAXIMUM_DESCRIBED_BYTES of it
    using Pdf = mergetest::MergeTestPdf;
    using H = ObjectInspectorTestHelper;
    constexpr int LONG_STRING = 200 * 1024;
    constexpr int STRING_COUNT = 100;
    constexpr int SHORTER_STRING = 60 * 1024;
    Pdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [] /Count 0 >>");
    pdf.set(7, "(" + QByteArray(LONG_STRING, 'a') + ")");
    QByteArray references;
    for (int i = 0; i < STRING_COUNT; ++i)
    {
        pdf.set(100 + i, "(" + QByteArray(SHORTER_STRING, char('a' + i % 26)) + ")");
        references += QByteArray::number(100 + i) + " 0 R ";
    }
    pdf.set(8, "[" + references + "]");
    const PDFDocumentPointer document = pdf.readPointer();

    QVariantMap parameters;
    parameters.insert(QStringLiteral("object"), QStringLiteral("7 0 R"));
    PDFDocumentToolResult result = H::runTool(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QJsonObject object = result.data.value(QStringLiteral("object")).toObject();
    QCOMPARE(object.value(QStringLiteral("value")).toString().size(), qsizetype(64 * 1024));
    QCOMPARE(object.value(QStringLiteral("length")).toInteger(), qint64(LONG_STRING));
    QCOMPARE(object.value(QStringLiteral("textTruncated")).toBool(), true);
    QCOMPARE(result.data.value(QStringLiteral("truncated")).toBool(), true);
    QCOMPARE(object.value(QStringLiteral("description")).toString(), QStringLiteral("\"%1\"…").arg(QString(int(PDFObjectInspector::MAXIMUM_DESCRIBED_BYTES), QLatin1Char('a'))));

    // 100 strings of 60 KiB by references (6000 KiB): whole strings up to 4 MiB, then the JSON ends
    parameters.insert(QStringLiteral("object"), QStringLiteral("8 0 R"));
    parameters.insert(QStringLiteral("depth"), 2);
    result = H::runTool(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.data.value(QStringLiteral("truncated")).toBool(), true);
    const QJsonArray items = result.data.value(QStringLiteral("object")).toObject().value(QStringLiteral("items")).toArray();
    qint64 textBytes = 0;
    int wholeStrings = 0;
    for (const QJsonValue& item : items)
    {
        const QJsonObject string = item.toObject().value(QStringLiteral("object")).toObject();
        textBytes += string.value(QStringLiteral("value")).toString().size();
        wholeStrings += string.value(QStringLiteral("value")).toString().size() == SHORTER_STRING ? 1 : 0;
    }
    QVERIFY2(textBytes <= 4 * 1024 * 1024, qPrintable(QString::number(textBytes)));
    QCOMPARE(wholeStrings, (4 * 1024 * 1024) / SHORTER_STRING);
    QVERIFY(items.size() < STRING_COUNT);

    // The inspector of the GUI: the same limit of a description
    const PDFObjectInspector inspector(document);
    PDFObjectInspectorNode node;
    node.object = document->getStorage().getObject(PDFObjectReference(7, 0));
    QCOMPARE(inspector.describe(node).size(), qsizetype(PDFObjectInspector::MAXIMUM_DESCRIBED_BYTES + 3));
    node.object = PDFObject::createName(QByteArray(LONG_STRING, 'N'));
    QCOMPARE(inspector.describe(node), QStringLiteral("/%1…").arg(QString(int(PDFObjectInspector::MAXIMUM_DESCRIBED_BYTES), QLatin1Char('N'))));
}

void ObjectInspectorTest::toolFactsInCLocale()
{
    // The fix wave of step 7: the facts and the descriptions of the data of MCP are machine-readable - the C
    // locale, whatever the default locale is: the 13 objects of the list (inspectorViews: the trailer and
    // 12 objects) and "Dictionary [3 items]" of the catalog (482:14847)
    using H = ObjectInspectorTestHelper;
    pdftest::TestLocaleGuard guard;
    QVERIFY(pdftest::TestLocaleGuard::isDefaultLocaleOtherThanC());

    const PDFDocumentPointer document = H::createInspectorPdf(5).readPointer();
    QVariantMap parameters;
    parameters.insert(QStringLiteral("object"), QString());
    parameters.insert(QStringLiteral("view"), QStringLiteral("objects"));
    const PDFDocumentToolResult result = H::runTool(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    QCOMPARE(result.facts.size(), size_t(1));
    QCOMPARE(result.facts.front().id, QStringLiteral("objects"));
    QCOMPARE(result.facts.front().value, QStringLiteral("13"));
    const QJsonArray roots = result.data.value(QStringLiteral("roots")).toArray();
    QCOMPARE(roots.at(1).toObject().value(QStringLiteral("object")).toString(), QStringLiteral("1 0 R"));
    QCOMPARE(roots.at(1).toObject().value(QStringLiteral("description")).toString(), QStringLiteral("Dictionary [3 items]"));
}

QTEST_MAIN(ObjectInspectorTest)

#include "tst_objectinspectortest.moc"
