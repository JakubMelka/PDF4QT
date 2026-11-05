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

#include "pdfannotation.h"
#include "pdfcatalog.h"
#include "pdfdocumenthiddendatascan.h"
#include "pdfdocumentlinkoperations.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentsanitizer.h"
#include "pdfdocumenttool_protect.h"
#include "pdfdocumentwriter.h"
#include "pdfform.h"
#include "pdfoperationcontrol.h"
#include "pdfstreamfilters.h"

#include <QBuffer>
#include <QtTest>

#include <functional>
#include <map>

// The tools Remove Hidden Data and Remove External Links of the core (package 2B of the first
// batch of phase 2, plan chapters 1.3, 2.7, 4 and 7). The documents are written by hand (every
// object with its number, the cross-reference table computed), so hostile structures - direct
// annotations, shared arrays, a /Popup of the catalog - are as easy as the ordinary ones. Every
// kind of hidden data has its own marker text; a removal is checked in the written file: its
// bytes, every string and every decoded stream (a compressed stream hides a marker from the bytes).

/// A PDF file written by hand
class RawPdf
{
public:
    void add(int number, const QByteArray& body)
    {
        m_objects[number] = body;
    }

    /// A stream; \p compress encodes it by FlateDecode
    void addStream(int number, const QByteArray& dictionaryEntries, const QByteArray& data, bool compress)
    {
        const QByteArray content = compress ? pdf::PDFFlateDecodeFilter::compress(data) : data;
        QByteArray body = "<< " + dictionaryEntries + " /Length " + QByteArray::number(content.size());
        if (compress)
        {
            body += " /Filter /FlateDecode";
        }
        body += " >>\nstream\n" + content + "\nendstream";
        m_objects[number] = body;
    }

    QByteArray build(int root, int info = 0) const
    {
        QByteArray data = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
        std::map<int, qint64> offsets;
        int maximum = 0;
        for (const auto& [number, body] : m_objects)
        {
            offsets[number] = data.size();
            data += QByteArray::number(number) + " 0 obj\n" + body + "\nendobj\n";
            maximum = qMax(maximum, number);
        }

        const qint64 xref = data.size();
        data += "xref\n0 " + QByteArray::number(maximum + 1) + "\n0000000000 65535 f \n";
        for (int number = 1; number <= maximum; ++number)
        {
            const auto it = offsets.find(number);
            if (it != offsets.end())
            {
                data += QByteArray::number(it->second).rightJustified(10, '0') + " 00000 n \n";
            }
            else
            {
                data += "0000000000 00000 f \n";
            }
        }
        data += "trailer\n<< /Size " + QByteArray::number(maximum + 1) + " /Root " + QByteArray::number(root) + " 0 R";
        if (info)
        {
            data += " /Info " + QByteArray::number(info) + " 0 R";
        }
        data += " >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
        return data;
    }

private:
    std::map<int, QByteArray> m_objects;
};

/// A canceled operation
class ProtectCanceledControl : public pdf::PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return true; }
};

/// The kinds of hidden data with their markers
struct HiddenMarkers
{
    const char* kind;
    QStringList markers;
};

class ProtectTestHelper
{
public:
    ProtectTestHelper() = delete;

    static QByteArray write(const pdf::PDFDocument& document)
    {
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        pdf::PDFDocumentWriter writer(nullptr);
        const pdf::PDFOperationResult result = writer.write(&buffer, &document);
        return result ? data : QByteArray();
    }

    static pdf::PDFDocument read(const QByteArray& data)
    {
        const auto getPassword = [](bool* ok)
        {
            *ok = false;
            return QString();
        };
        pdf::PDFDocumentReader reader(nullptr, getPassword, false, false);
        pdf::PDFDocument document = reader.readFromBuffer(data);
        return reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK ? document : pdf::PDFDocument();
    }

    static pdf::PDFDocumentToolContext createContext(const pdf::PDFDocument& document, const QByteArray& data)
    {
        return pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), data, QStringLiteral("Project_Report.pdf"), false));
    }

    /// The marker is somewhere in the file: in its bytes, in a string of an object or in a decoded stream
    static bool contains(const QByteArray& data, const QString& marker)
    {
        const QByteArray markerBytes = marker.toLatin1();
        if (data.contains(markerBytes))
        {
            return true;
        }

        const pdf::PDFDocument document = read(data);
        bool isFound = false;
        std::function<void(const pdf::PDFObject&)> visit = [&](const pdf::PDFObject& object)
        {
            if (isFound)
            {
                return;
            }
            switch (object.getType())
            {
                case pdf::PDFObject::Type::String:
                    isFound = object.getString().contains(markerBytes);
                    break;
                case pdf::PDFObject::Type::Array:
                    for (size_t i = 0; i < object.getArray()->getCount(); ++i)
                    {
                        visit(object.getArray()->getItem(i));
                    }
                    break;
                case pdf::PDFObject::Type::Dictionary:
                    for (size_t i = 0; i < object.getDictionary()->getCount(); ++i)
                    {
                        visit(object.getDictionary()->getValue(i));
                    }
                    break;
                case pdf::PDFObject::Type::Stream:
                {
                    const pdf::PDFDictionary* dictionary = object.getStream()->getDictionary();
                    for (size_t i = 0; i < dictionary->getCount(); ++i)
                    {
                        visit(dictionary->getValue(i));
                    }
                    isFound = isFound || document.getDecodedStream(object.getStream()).contains(markerBytes);
                    break;
                }
                default:
                    break;
            }
        };

        for (const pdf::PDFObjectStorage::Entry& entry : document.getStorage().getObjects())
        {
            visit(entry.object);
        }
        visit(document.getStorage().getTrailerDictionary());
        return isFound;
    }

    /// Every kind of hidden data of 438:12588 with a marker; the page has a visible text too
    static QByteArray createHiddenDataDocument()
    {
        RawPdf pdf;
        pdf.add(1, "<< /Type /Catalog /Pages 2 0 R /Metadata 50 0 R /Names << /EmbeddedFiles << /Names [(a.txt) 20 0 R] >> >> /AF [20 0 R] "
                   "/Outlines 60 0 R /PageLabels << /Nums [0 << /S /r >> 1 << /S /D /P (MarkerLabel) >>] >> "
                   "/PieceInfo << /SearchIndex << /LastModified (MarkerIndex) >> >> /AcroForm 90 0 R /Perms << /DocMDP 92 0 R >> "
                   "/StructTreeRoot << /Type /StructTreeRoot /K << /Type /OBJR /Obj 30 0 R >> >> >>");
        pdf.add(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>");
        pdf.add(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 5 0 R /Resources << /Font << /F1 6 0 R >> >> "
                   "/Annots [30 0 R 31 0 R 33 0 R 91 0 R] /Metadata 51 0 R /Thumb 80 0 R >>");
        pdf.add(4, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>");
        pdf.addStream(5, "", "BT /F1 12 Tf 72 700 Td (VisibleText) Tj ET\nBT /F1 12 Tf 3 Tr 72 650 Td (MarkerInvisible) Tj ET", true);
        pdf.add(6, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
        pdf.add(10, "<< /Title (MarkerInfoTitle) /Author (MarkerAuthor) >>");
        pdf.add(20, "<< /Type /Filespec /F (a.txt) /EF << /F 21 0 R >> >>");
        pdf.addStream(21, "/Type /EmbeddedFile", "MarkerAttachment payload", true);
        pdf.add(22, "<< /Type /Filespec /F (b.txt) /EF << /F 23 0 R >> >>");
        pdf.addStream(23, "/Type /EmbeddedFile", "MarkerAnnotationFile payload", true);
        pdf.add(30, "<< /Type /Annot /Subtype /Text /Rect [10 10 30 30] /Contents (MarkerComment) /Popup 31 0 R >>");
        pdf.add(31, "<< /Type /Annot /Subtype /Popup /Rect [40 40 200 100] /Parent 30 0 R /Contents (MarkerPopup) >>");
        pdf.add(33, "<< /Type /Annot /Subtype /FileAttachment /Rect [50 50 70 70] /FS 22 0 R >>");
        pdf.addStream(50, "/Type /Metadata /Subtype /XML", "<x:xmpmeta>MarkerXmp</x:xmpmeta>", true);
        pdf.addStream(51, "/Type /Metadata /Subtype /XML", "<x:xmpmeta>MarkerPageXmp</x:xmpmeta>", false);
        pdf.add(60, "<< /Type /Outlines /First 61 0 R /Last 61 0 R /Count 1 >>");
        pdf.add(61, "<< /Title (MarkerOutline) /Parent 60 0 R /Dest [3 0 R /Fit] >>");
        pdf.addStream(80, "/Width 2 /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 8", "MarkerThumb", true);
        pdf.add(90, "<< /Fields [91 0 R] /SigFlags 3 >>");
        pdf.add(91, "<< /FT /Sig /T (Signature1) /V 92 0 R /Type /Annot /Subtype /Widget /Rect [0 0 0 0] /P 3 0 R >>");
        pdf.add(92, "<< /Type /Sig /Name (MarkerSigner) /Reason (MarkerReason) /Contents <00> >>");
        return pdf.build(1, 10);
    }

    static std::vector<HiddenMarkers> getMarkers()
    {
        return {
            { "documentInfo", { QStringLiteral("MarkerInfoTitle"), QStringLiteral("MarkerAuthor") } },
            { "metadata", { QStringLiteral("MarkerXmp"), QStringLiteral("MarkerPageXmp") } },
            { "comments", { QStringLiteral("MarkerComment"), QStringLiteral("MarkerPopup") } },
            { "attachments", { QStringLiteral("MarkerAttachment") } },
            { "outline", { QStringLiteral("MarkerOutline") } },
            { "pageLabels", { QStringLiteral("MarkerLabel") } },
            { "searchIndex", { QStringLiteral("MarkerIndex") } },
            { "thumbnails", { QStringLiteral("MarkerThumb") } },
            { "invisibleText", { QStringLiteral("MarkerInvisible") } },
            { "signatureFields", { QStringLiteral("MarkerSigner"), QStringLiteral("MarkerReason") } },
        };
    }

    static QVariantMap only(const QString& kind)
    {
        QVariantMap values = pdf::PDFDocumentToolSanitize::getPresetValues(pdf::PDFDocumentToolSanitize::Preset::Custom);
        if (!kind.isEmpty())
        {
            values.insert(kind, true);
        }
        return values;
    }

    /// A page of links: an indirect web link, a direct mailto link, an ftp link, an internal
    /// link, a web link with its own contents and a comment
    static QByteArray createLinkDocument()
    {
        RawPdf pdf;
        pdf.add(1, "<< /Type /Catalog /Pages 2 0 R >>");
        pdf.add(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>");
        pdf.add(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                   "/Annots [10 0 R << /Type /Annot /Subtype /Link /Rect [10 100 200 115] /A << /S /URI /URI (mailto:jane.novak@example.com?subject=MarkerSubject) >> >> 11 0 R 12 0 R 14 0 R] >>");
        pdf.add(4, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [13 0 R] >>");
        pdf.add(10, "<< /Type /Annot /Subtype /Link /Rect [54 117 234 132] /A << /S /URI /URI (https://www.acme-corp.example/) >> >>");
        pdf.add(11, "<< /Type /Annot /Subtype /Link /Rect [10 200 50 215] /A << /S /URI /URI (ftp://files.example.org/MarkerFtp) >> >>");
        pdf.add(12, "<< /Type /Annot /Subtype /Link /Rect [10 300 50 315] /Dest [4 0 R /Fit] >>");
        pdf.add(13, "<< /Type /Annot /Subtype /Link /Rect [10 10 90 25] /Contents (Market study 2026) /A << /S /URI /URI (https://research.example.org/MarkerStudy) >> >>");
        pdf.add(14, "<< /Type /Annot /Subtype /Text /Rect [300 300 320 320] /Contents (KeptComment) >>");
        return pdf.build(1);
    }
};

class DocumentToolProtectTest : public QObject
{
    Q_OBJECT

private slots:
    void listExternalLinks();
    void parseIds();
    void removeLinksKeepsExcluded();
    void removeLinksKeepsObjectNumbers();
    void removeLinksRefusals();
    void removeLinksOldCommandUnchanged();
    void removeLinksHostile();
    void removeLinksListBounded();
    void scanFindsEveryKind();
    void scanCleanDocument();
    void sanitizeRemovesKind_data();
    void sanitizeRemovesKind();
    void sanitizeStepsAndFacts();
    void sanitizeNothingToDo();
    void sanitizeWholeFileWithoutOldRevision();
    void invisibleTextOverStreams();
    void ownOcrDataRemoved();
    void signatureFieldsNotInAll();
    void sanitizeHostile();
    void scanBounded();
    void presets();
};

void DocumentToolProtectTest::listExternalLinks()
{
    using Links = pdf::PDFDocumentLinkOperations;
    const pdf::PDFDocument document = ProtectTestHelper::read(ProtectTestHelper::createLinkDocument());
    QVERIFY(document.getCatalog()->getPageCount() == 2);

    const Links::LinkList list = Links::listExternalLinks(&document);

    // An internal link (GoTo) is no external link; a comment is no link (438:15032)
    QCOMPARE(list.internalLinkCount, 1);
    QVERIFY(!list.isTruncated);
    QCOMPARE(int(list.links.size()), 4);

    // The order of the pages and of /Annots; the id of an indirect annotation is the id of
    // document.get_annotations ("obj:gen"), the direct one has the page and its index
    QCOMPARE(list.links[0].id, QStringLiteral("10:0"));
    QCOMPARE(list.links[0].kind, Links::LinkKind::Web);
    QCOMPARE(list.links[0].title, QStringLiteral("acme-corp.example"));
    QCOMPARE(list.links[0].address, QStringLiteral("https://www.acme-corp.example/"));
    QCOMPARE(list.links[0].rectangle, QRectF(54, 117, 180, 15));

    QCOMPARE(list.links[1].id, QStringLiteral("p0:1"));
    QVERIFY(!list.links[1].annotationReference.isValid());
    QCOMPARE(list.links[1].kind, Links::LinkKind::EMail);
    QCOMPARE(list.links[1].title, QStringLiteral("jane.novak@example.com"));

    // K17: another scheme is an external link only of All links
    QCOMPARE(list.links[2].kind, Links::LinkKind::Other);

    // The contents of a link are its title (438:14890 "Market study 2026")
    QCOMPARE(list.links[3].pageIndex, 1);
    QCOMPARE(list.links[3].title, QStringLiteral("Market study 2026"));

    QCOMPARE(Links::getLinkKind("HTTPS://X"), Links::LinkKind::Web);
    QCOMPARE(Links::getLinkKind("MailTo:a@b"), Links::LinkKind::EMail);
    QCOMPARE(Links::getLinkKind("file:///etc"), Links::LinkKind::Other);

    // The preview of the tool carries the same links
    const std::vector<Links::ExternalLink> back = Links::fromJson(Links::toJson(list.links));
    QCOMPARE(int(back.size()), 4);
    QCOMPARE(back[1].id, QStringLiteral("p0:1"));
    QCOMPARE(back[3].pageIndex, 1);
    QCOMPARE(back[0].rectangle, QRectF(54, 117, 180, 15));
}

void DocumentToolProtectTest::parseIds()
{
    std::set<QString> ids;
    QString invalid;
    QVERIFY(pdf::PDFDocumentLinkOperations::parseIds(QStringLiteral(" 12:0, 15:0 ,p3:1,,"), &ids, &invalid));
    QCOMPARE(ids, (std::set<QString>{ QStringLiteral("12:0"), QStringLiteral("15:0"), QStringLiteral("p3:1") }));
    QVERIFY(pdf::PDFDocumentLinkOperations::parseIds(QString(), &ids, &invalid));

    QVERIFY(!pdf::PDFDocumentLinkOperations::parseIds(QStringLiteral("12:0, javascript:alert(1)"), nullptr, &invalid));
    QCOMPARE(invalid, QStringLiteral("javascript:alert(1)"));
    QVERIFY(!pdf::PDFDocumentLinkOperations::parseIds(QStringLiteral("12"), nullptr, &invalid));
    QVERIFY(!pdf::PDFDocumentLinkOperations::parseIds(QStringLiteral("-1:0"), nullptr, &invalid));
}

void DocumentToolProtectTest::removeLinksKeepsExcluded()
{
    const QByteArray data = ProtectTestHelper::createLinkDocument();
    const pdf::PDFDocument document = ProtectTestHelper::read(data);
    pdf::PDFDocumentToolRemoveLinks tool;

    QVariantMap parameters;
    parameters.insert(QStringLiteral("exclude"), QStringLiteral("11:0"));
    const pdf::PDFDocumentToolResult result = tool.run(ProtectTestHelper::createContext(document, data), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(int(result.documents.size()), 1);
    QCOMPARE(result.documents.front().suggestedFileName, QStringLiteral("Project_Report_no_links.pdf"));
    QVERIFY(result.documents.front().flags.testFlag(pdf::PDFModifiedDocument::Annotation));

    // 438:16073: Removed "… on … pages", Kept, Internal links "· not touched"
    QCOMPARE(result.facts.size(), size_t(3));
    QCOMPARE(result.facts[0].value, QStringLiteral("3 external links on 2 pages"));
    QCOMPARE(result.facts[1].value, QStringLiteral("1 external link"));
    QCOMPARE(result.facts[2].value, QStringLiteral("1 · not touched"));

    // The addresses of the removed links are not in the file written whole; the kept link,
    // the internal link and the comment stay
    const QByteArray written = ProtectTestHelper::write(*result.documents.front().document);
    QVERIFY(!written.isEmpty());
    QVERIFY(!ProtectTestHelper::contains(written, QStringLiteral("acme-corp")));
    QVERIFY(!ProtectTestHelper::contains(written, QStringLiteral("MarkerSubject")));
    QVERIFY(!ProtectTestHelper::contains(written, QStringLiteral("MarkerStudy")));
    QVERIFY(ProtectTestHelper::contains(written, QStringLiteral("MarkerFtp")));
    QVERIFY(ProtectTestHelper::contains(written, QStringLiteral("KeptComment")));

    const pdf::PDFDocument output = ProtectTestHelper::read(written);
    const pdf::PDFDocumentLinkOperations::LinkList after = pdf::PDFDocumentLinkOperations::listExternalLinks(&output);
    QCOMPARE(int(after.links.size()), 1);
    QCOMPARE(after.links.front().id, QStringLiteral("11:0"));
    QCOMPARE(after.internalLinkCount, 1);
}

void DocumentToolProtectTest::removeLinksKeepsObjectNumbers()
{
    // A signed document is saved incrementally (IncrementalSave): the page tree is not flattened,
    // no object is renumbered, the update is written and read back
    const QByteArray data = ProtectTestHelper::createLinkDocument();
    const pdf::PDFDocument document = ProtectTestHelper::read(data);
    pdf::PDFDocumentToolRemoveLinks tool;
    const pdf::PDFDocumentToolResult result = tool.run(ProtectTestHelper::createContext(document, data), QVariantMap(), nullptr, nullptr);
    QVERIFY(result.isSuccess());

    const pdf::PDFDocument* output = result.documents.front().document.data();
    QCOMPARE(output->getCatalog()->getPage(0)->getPageReference(), document.getCatalog()->getPage(0)->getPageReference());
    QCOMPARE(output->getCatalog()->getPage(1)->getPageReference(), document.getCatalog()->getPage(1)->getPageReference());
    QVERIFY(output->getStorage().getObjectByReference(pdf::PDFObjectReference(2, 0)).isDictionary());

    QByteArray updated;
    QBuffer buffer(&updated);
    buffer.open(QIODevice::WriteOnly);
    pdf::PDFDocumentWriter writer(nullptr);
    QVERIFY(writer.writeIncrementalUpdate(&buffer, data, output));
    QVERIFY(updated.startsWith(data));
    const pdf::PDFDocument reread = ProtectTestHelper::read(updated);
    QCOMPARE(pdf::PDFDocumentLinkOperations::listExternalLinks(&reread).links.size(), size_t(0));
}

void DocumentToolProtectTest::removeLinksRefusals()
{
    const QByteArray data = ProtectTestHelper::createLinkDocument();
    const pdf::PDFDocument document = ProtectTestHelper::read(data);
    pdf::PDFDocumentToolRemoveLinks tool;

    // An id, which is no external link, is no link the caller keeps
    QVariantMap parameters;
    parameters.insert(QStringLiteral("exclude"), QStringLiteral("12:0"));
    pdf::PDFDocumentToolResult result = tool.run(ProtectTestHelper::createContext(document, data), parameters, nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.message.contains(QStringLiteral("12:0")));
    QVERIFY(result.documents.empty());

    // Not an id
    parameters.insert(QStringLiteral("exclude"), QStringLiteral("all"));
    result = tool.run(ProtectTestHelper::createContext(document, data), parameters, nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.message.contains(QStringLiteral("all")));

    // Every link kept: nothing to do
    parameters.insert(QStringLiteral("exclude"), QStringLiteral("10:0,p0:1,11:0,13:0"));
    result = tool.run(ProtectTestHelper::createContext(document, data), parameters, nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::NothingToDo);
    QVERIFY(result.documents.empty());

    // No external link at all
    RawPdf pdf;
    pdf.add(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.add(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    pdf.add(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>");
    const QByteArray emptyData = pdf.build(1);
    result = tool.run(ProtectTestHelper::createContext(ProtectTestHelper::read(emptyData), emptyData), QVariantMap(), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::NothingToDo);

    // The value is never stored: the kept links of one document mean nothing for another one
    QVERIFY(!pdf::PDFDocumentToolParameters::find(tool.getParameters(), QStringLiteral("exclude"))->isStored());
}

void DocumentToolProtectTest::removeLinksOldCommandUnchanged()
{
    // PdfTool remove-external-links and the Editor: the annotations of the arrays of references
    // only (a direct annotation stays, as before the tools of phase 2), the page tree flattened
    const pdf::PDFDocument document = ProtectTestHelper::read(ProtectTestHelper::createLinkDocument());
    const pdf::PDFDocumentLinkOperations::RemovalResult result = pdf::PDFDocumentLinkOperations::removeExternalLinkAnnotations(&document);
    QVERIFY(result.document);
    // Page 1 has a direct item: its array is no array of references and is not read; page 2 has 13 0 R
    QCOMPARE(result.removed, 1);
    QVERIFY(result.flags.testFlag(pdf::PDFModifiedDocument::Annotation));

    const pdf::PDFDocumentLinkOperations::LinkList after = pdf::PDFDocumentLinkOperations::listExternalLinks(result.document.data());
    QCOMPARE(int(after.links.size()), 3);
}

void DocumentToolProtectTest::removeLinksHostile()
{
    // A link, whose /Popup is the catalog, a page listed as its own link annotation, an array
    // of annotations shared by two pages: the document stays whole
    RawPdf pdf;
    pdf.add(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.add(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>");
    pdf.add(3, "<< /Type /Page /Subtype /Link /A << /S /URI /URI (http://page.example) >> /Parent 2 0 R /MediaBox [0 0 612 792] /Annots 9 0 R >>");
    pdf.add(4, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots 9 0 R >>");
    pdf.add(9, "[10 0 R 3 0 R]");
    pdf.add(10, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Popup 1 0 R /A << /S /URI /URI (http://hostile.example) >> >>");
    const QByteArray data = pdf.build(1);
    const pdf::PDFDocument document = ProtectTestHelper::read(data);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(2));

    pdf::PDFDocumentToolRemoveLinks tool;
    const pdf::PDFDocumentToolResult result = tool.run(ProtectTestHelper::createContext(document, data), QVariantMap(), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const QByteArray written = ProtectTestHelper::write(*result.documents.front().document);
    const pdf::PDFDocument output = ProtectTestHelper::read(written);
    QCOMPARE(output.getCatalog()->getPageCount(), size_t(2));
    QVERIFY(!ProtectTestHelper::contains(written, QStringLiteral("hostile.example")));
}

void DocumentToolProtectTest::removeLinksListBounded()
{
    // 100 001 direct links: the list stops at MAXIMUM_LINKS and says it
    QByteArray annotations;
    const QByteArray link = "<</Subtype/Link/Rect[0 0 1 1]/A<</S/URI/URI(http://a)>>>>";
    annotations.reserve(link.size() * (pdf::PDFDocumentLinkOperations::MAXIMUM_LINKS + 2));
    for (int i = 0; i <= pdf::PDFDocumentLinkOperations::MAXIMUM_LINKS; ++i)
    {
        annotations += link;
    }

    RawPdf pdf;
    pdf.add(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.add(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    pdf.add(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [" + annotations + "] >>");
    const pdf::PDFDocument document = ProtectTestHelper::read(pdf.build(1));
    const pdf::PDFDocumentLinkOperations::LinkList list = pdf::PDFDocumentLinkOperations::listExternalLinks(&document);
    QCOMPARE(int(list.links.size()), pdf::PDFDocumentLinkOperations::MAXIMUM_LINKS);
    QVERIFY(list.isTruncated);

    // A canceled listing stops at once
    ProtectCanceledControl control;
    QVERIFY(pdf::PDFDocumentLinkOperations::listExternalLinks(&document, &control).links.empty());
}

void DocumentToolProtectTest::scanFindsEveryKind()
{
    using Scan = pdf::PDFDocumentHiddenDataScan;
    using Kind = Scan::Kind;

    const pdf::PDFDocument document = ProtectTestHelper::read(ProtectTestHelper::createHiddenDataDocument());
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(2));
    const Scan::Result result = Scan::scan(&document, true, nullptr, nullptr);

    QCOMPARE(result.getFoundCount(), 10);
    QCOMPARE(result.get(Kind::DocumentInfo).count, 2);
    QCOMPARE(result.get(Kind::Metadata).count, 2);

    // The text annotation and the file attachment are markup annotations, the popup goes with them
    QCOMPARE(result.get(Kind::Comments).count, 2);

    // Two embedded files: of the name tree (also of /AF) and of the annotation; the size of their streams
    QCOMPARE(result.get(Kind::Attachments).count, 2);
    QVERIFY(result.get(Kind::Attachments).bytes > 0);

    QCOMPARE(result.get(Kind::Outline).count, 1);
    QCOMPARE(result.pageLabelsText, QStringLiteral("i, MarkerLabel1"));
    QVERIFY(result.get(Kind::SearchIndex).isPresent);
    QCOMPARE(result.get(Kind::Thumbnails).count, 1);
    QCOMPARE(result.get(Kind::InvisibleText).count, 1);
    QCOMPARE(result.get(Kind::SignatureFields).count, 1);
    for (const Scan::KindResult& kind : result.kinds)
    {
        QVERIFY(kind.isComplete);
    }

    // Without the invisible text the content is not decoded
    const Scan::Result withoutText = Scan::scan(&document, false, nullptr, nullptr);
    QVERIFY(!withoutText.get(Kind::InvisibleText).isPresent);

    // The data of the preview
    const Scan::Result back = Scan::Result::fromJson(result.toJson());
    QCOMPARE(back.getFoundCount(), 10);
    QCOMPARE(back.get(Kind::Attachments).bytes, result.get(Kind::Attachments).bytes);
    QCOMPARE(back.pageLabelsText, result.pageLabelsText);
}

void DocumentToolProtectTest::scanCleanDocument()
{
    // 438:13839: nothing found
    RawPdf pdf;
    pdf.add(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.add(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    pdf.add(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R >>");
    pdf.addStream(4, "", "BT /F1 12 Tf 0 Tr (Visible) Tj ET", false);
    const pdf::PDFDocument document = ProtectTestHelper::read(pdf.build(1));
    QCOMPARE(pdf::PDFDocumentHiddenDataScan::scan(&document, true, nullptr, nullptr).getFoundCount(), 0);
}

void DocumentToolProtectTest::sanitizeRemovesKind_data()
{
    QTest::addColumn<QString>("kind");
    for (const HiddenMarkers& markers : ProtectTestHelper::getMarkers())
    {
        QTest::newRow(markers.kind) << QString::fromLatin1(markers.kind);
    }
    QTest::newRow("everything") << QString();
}

void DocumentToolProtectTest::sanitizeRemovesKind()
{
    QFETCH(QString, kind);

    const QByteArray data = ProtectTestHelper::createHiddenDataDocument();
    const pdf::PDFDocument document = ProtectTestHelper::read(data);
    const QVariantMap parameters = kind.isEmpty() ? pdf::PDFDocumentToolSanitize::getPresetValues(pdf::PDFDocumentToolSanitize::Preset::Everything) : ProtectTestHelper::only(kind);

    pdf::PDFDocumentToolSanitize tool;
    const pdf::PDFDocumentToolResult result = tool.run(ProtectTestHelper::createContext(document, data), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(int(result.documents.size()), 1);
    QCOMPARE(result.documents.front().suggestedFileName, QStringLiteral("Project_Report_clean.pdf"));

    const QByteArray written = ProtectTestHelper::write(*result.documents.front().document);
    QVERIFY(!written.isEmpty());
    const pdf::PDFDocument output = ProtectTestHelper::read(written);
    QCOMPARE(output.getCatalog()->getPageCount(), size_t(2));

    // The page content is not changed (callout of 438:12588)
    QVERIFY(ProtectTestHelper::contains(written, QStringLiteral("VisibleText")));

    for (const HiddenMarkers& markers : ProtectTestHelper::getMarkers())
    {
        // A comment of a file attachment is a markup annotation: its file goes with the comments
        const bool isRemoved = kind.isEmpty() || kind == QLatin1String(markers.kind);
        for (const QString& marker : markers.markers)
        {
            QVERIFY2(ProtectTestHelper::contains(written, marker) != isRemoved, qPrintable(QStringLiteral("%1 of %2").arg(marker, QString::fromLatin1(markers.kind))));
        }
    }

    const bool isAnnotationFileRemoved = kind.isEmpty() || kind == QLatin1String("attachments") || kind == QLatin1String("comments");
    QCOMPARE(ProtectTestHelper::contains(written, QStringLiteral("MarkerAnnotationFile")), !isAnnotationFileRemoved);

    if (kind == QLatin1String("documentInfo"))
    {
        // No information dictionary at all: the builders of the steps write their producer into one
        QVERIFY(!output.getStorage().getDictionaryFromObject(output.getStorage().getTrailerDictionary())->hasKey("Info"));
    }
}

void DocumentToolProtectTest::sanitizeStepsAndFacts()
{
    const QByteArray data = ProtectTestHelper::createHiddenDataDocument();
    const pdf::PDFDocument document = ProtectTestHelper::read(data);
    pdf::PDFDocumentToolSanitize tool;

    // Standard: everything except the invisible text (and the signature fields, owner answer Q10)
    const pdf::PDFDocumentToolResult result = tool.run(ProtectTestHelper::createContext(document, data), QVariantMap(), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    // 438:14342: a step per removed kind in the order of the form
    QStringList steps;
    for (const pdf::PDFDocumentToolStep& step : result.steps)
    {
        steps << step.text;
        QCOMPARE(step.state, pdf::PDFDocumentToolStep::State::Done);
    }
    QCOMPARE(steps, QStringList({ QStringLiteral("Document info was removed"), QStringLiteral("2 metadata streams removed"),
                                  QStringLiteral("2 markup annotations removed"), QStringLiteral("2 file attachments removed"),
                                  QStringLiteral("Outline was removed"), QStringLiteral("Page labels were removed"),
                                  QStringLiteral("Search index was removed"), QStringLiteral("1 page thumbnail removed") }));
    QCOMPARE(result.steps.front().iconName, QStringLiteral("info"));

    QCOMPARE(result.facts.size(), size_t(3));
    QCOMPARE(result.facts[0].id, QStringLiteral("removed"));
    QCOMPARE(result.facts[0].value, QStringLiteral("8 kinds of hidden data"));
    QCOMPARE(result.facts[1].id, QStringLiteral("size"));
    QCOMPARE(result.facts[2].value, QStringLiteral("Invisible text (OCR layer), Signature fields"));
    QCOMPARE(result.data.value(QStringLiteral("keptKinds")).toArray().size(), 2);
    QVERIFY(result.data.value(QStringLiteral("sizeAfter")).toInteger() > 0);
}

void DocumentToolProtectTest::sanitizeNothingToDo()
{
    const QByteArray data = ProtectTestHelper::createLinkDocument();
    pdf::PDFDocumentToolSanitize tool;

    // The document has a comment only; the outline is chosen
    const pdf::PDFDocumentToolResult result = tool.run(ProtectTestHelper::createContext(ProtectTestHelper::read(data), data), ProtectTestHelper::only(QStringLiteral("outline")), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::NothingToDo);
    QVERIFY(result.documents.empty());
    QVERIFY(result.isUse());
}

void DocumentToolProtectTest::sanitizeWholeFileWithoutOldRevision()
{
    // An incremental update replaced the title: the old one is still in the bytes of the input
    const QByteArray base = ProtectTestHelper::createHiddenDataDocument();
    const qsizetype startXref = base.lastIndexOf("startxref");
    const QByteArray previous = base.mid(startXref + 10).split('\n').front();
    QByteArray update = base + "10 0 obj\n<< /Title (NewTitle) /Keywords (MarkerOldRevisionKept) >>\nendobj\n";
    const qint64 objectOffset = base.size();
    const qint64 xref = update.size();
    update += "xref\n10 1\n" + QByteArray::number(objectOffset).rightJustified(10, '0') + " 00000 n \n";
    update += "trailer\n<< /Size 93 /Root 1 0 R /Info 10 0 R /Prev " + previous + " >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";

    const pdf::PDFDocument document = ProtectTestHelper::read(update);
    QCOMPARE(document.getInfo()->title, QStringLiteral("NewTitle"));
    pdf::PDFDocumentToolSanitize tool;
    const pdf::PDFDocumentToolResult result = tool.run(ProtectTestHelper::createContext(document, update), ProtectTestHelper::only(QStringLiteral("documentInfo")), nullptr, nullptr);
    QVERIFY(result.isSuccess());

    // Written whole: one revision, neither the old nor the new title
    const QByteArray written = ProtectTestHelper::write(*result.documents.front().document);
    QCOMPARE(written.count("%%EOF"), 1);
    QVERIFY(!written.contains("/Prev"));
    QVERIFY(!ProtectTestHelper::contains(written, QStringLiteral("MarkerInfoTitle")));
    QVERIFY(!ProtectTestHelper::contains(written, QStringLiteral("MarkerOldRevisionKept")));

    // The version of the input stays (createWholeFileDocument)
    QCOMPARE(result.documents.front().document->getInfo()->version, document.getInfo()->version);
}

void DocumentToolProtectTest::invisibleTextOverStreams()
{
    // The streams of /Contents are one content stream (ISO 32000-2, 7.8.2): Tr 3 of the first
    // stream hides the text of the second one; q / Q restore the mode
    RawPdf pdf;
    pdf.add(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.add(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    pdf.add(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents [4 0 R 5 0 R] /Resources << /Font << /F1 6 0 R >> >> >>");
    pdf.addStream(4, "", "q BT /F1 12 Tf 3 Tr 72 700 Td", true);
    pdf.addStream(5, "", "(MarkerSplit) Tj ET Q BT /F1 12 Tf 72 600 Td (VisibleAfterQ) Tj ET", true);
    pdf.add(6, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    const QByteArray data = pdf.build(1);
    const pdf::PDFDocument document = ProtectTestHelper::read(data);

    QCOMPARE(pdf::PDFDocumentHiddenDataScan::scan(&document, true, nullptr, nullptr).get(pdf::PDFDocumentHiddenDataScan::Kind::InvisibleText).count, 1);

    pdf::PDFDocumentToolSanitize tool;
    const pdf::PDFDocumentToolResult result = tool.run(ProtectTestHelper::createContext(document, data), ProtectTestHelper::only(QStringLiteral("invisibleText")), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const QByteArray written = ProtectTestHelper::write(*result.documents.front().document);
    QVERIFY(!ProtectTestHelper::contains(written, QStringLiteral("MarkerSplit")));
    QVERIFY(ProtectTestHelper::contains(written, QStringLiteral("VisibleAfterQ")));
}

void DocumentToolProtectTest::ownOcrDataRemoved()
{
    // The private data of an own OCR layer hold the words of the layer
    RawPdf pdf;
    pdf.add(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.add(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    pdf.add(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /PieceInfo << /PDF4QT_OCR << /Private << /Data 4 0 R >> >> /Other << /Private (KeptPieceInfo) >> >> >>");
    pdf.addStream(4, "", "{\"words\": [\"MarkerOcrWord\"]}", true);
    const QByteArray data = pdf.build(1);
    const pdf::PDFDocument document = ProtectTestHelper::read(data);
    QCOMPARE(pdf::PDFDocumentHiddenDataScan::scan(&document, true, nullptr, nullptr).get(pdf::PDFDocumentHiddenDataScan::Kind::InvisibleText).count, 1);

    pdf::PDFDocumentToolSanitize tool;
    const pdf::PDFDocumentToolResult result = tool.run(ProtectTestHelper::createContext(document, data), ProtectTestHelper::only(QStringLiteral("invisibleText")), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const QByteArray written = ProtectTestHelper::write(*result.documents.front().document);
    QVERIFY(!ProtectTestHelper::contains(written, QStringLiteral("MarkerOcrWord")));
    QVERIFY(ProtectTestHelper::contains(written, QStringLiteral("KeptPieceInfo")));
}

void DocumentToolProtectTest::signatureFieldsNotInAll()
{
    // Owner answer Q10: All of the old applications never removes the signature fields
    QVERIFY(!(pdf::PDFDocumentSanitizer::All & pdf::PDFDocumentSanitizer::SignatureFields));

    const pdf::PDFDocument document = ProtectTestHelper::read(ProtectTestHelper::createHiddenDataDocument());
    QCOMPARE(pdf::PDFForm::getSignatureCount(&document), 1);

    pdf::PDFDocumentSanitizer sanitizer(pdf::PDFDocumentSanitizer::All, nullptr);
    sanitizer.setDocument(&document);
    sanitizer.sanitize();
    const pdf::PDFDocument all(sanitizer.takeStorage(), document.getInfo()->version, QByteArray());
    const QByteArray written = ProtectTestHelper::write(all);
    QVERIFY(ProtectTestHelper::contains(written, QStringLiteral("MarkerSigner")));
    const pdf::PDFDocument allReread = ProtectTestHelper::read(written);
    QCOMPARE(pdf::PDFForm::getSignatureCount(&allReread), 1);

    pdf::PDFDocumentSanitizer signatures(pdf::PDFDocumentSanitizer::SignatureFields, nullptr);
    signatures.setDocument(&document);
    signatures.sanitize();
    const pdf::PDFDocument withoutSignatures(signatures.takeStorage(), document.getInfo()->version, QByteArray());
    const pdf::PDFDocument reread = ProtectTestHelper::read(ProtectTestHelper::write(withoutSignatures));
    QCOMPARE(pdf::PDFForm::getSignatureCount(&reread), 0);
    QVERIFY(!pdf::PDFDocumentHiddenDataScan::scan(&reread, false, nullptr, nullptr).get(pdf::PDFDocumentHiddenDataScan::Kind::SignatureFields).isPresent);
}

void DocumentToolProtectTest::sanitizeHostile()
{
    // A comment, whose /Popup is the catalog; a page in its own /Annots; a mixed array of a
    // direct comment and references; /Names and /PieceInfo of wrong types; a cycle of /Parent of fields
    RawPdf pdf;
    pdf.add(1, "<< /Type /Catalog /Pages 2 0 R /Names 7 /PieceInfo (x) /AcroForm << /Fields [20 0 R] >> >>");
    pdf.add(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    pdf.add(3, "<< /Type /Page /Subtype /Text /Contents (PageAsComment) /Parent 2 0 R /MediaBox [0 0 612 792] "
               "/Annots [10 0 R 3 0 R << /Type /Annot /Subtype /Square /Rect [0 0 5 5] /Contents (MarkerDirectComment) >> 11 0 R] >>");
    pdf.add(10, "<< /Type /Annot /Subtype /Text /Rect [0 0 10 10] /Contents (MarkerHostileComment) /Popup 1 0 R >>");
    pdf.add(11, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest [3 0 R /Fit] >>");
    pdf.add(20, "<< /T (a) /Parent 21 0 R /Kids [21 0 R] >>");
    pdf.add(21, "<< /T (b) /Parent 20 0 R /Kids [20 0 R] >>");
    const QByteArray data = pdf.build(1);
    const pdf::PDFDocument document = ProtectTestHelper::read(data);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(1));

    pdf::PDFDocumentToolSanitize tool;
    const pdf::PDFDocumentToolResult result = tool.run(ProtectTestHelper::createContext(document, data), pdf::PDFDocumentToolSanitize::getPresetValues(pdf::PDFDocumentToolSanitize::Preset::Everything), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const QByteArray written = ProtectTestHelper::write(*result.documents.front().document);
    const pdf::PDFDocument output = ProtectTestHelper::read(written);

    // The catalog and the page stay; both comments are gone; the internal link stays
    QCOMPARE(output.getCatalog()->getPageCount(), size_t(1));
    QVERIFY(!ProtectTestHelper::contains(written, QStringLiteral("MarkerHostileComment")));
    QVERIFY(!ProtectTestHelper::contains(written, QStringLiteral("MarkerDirectComment")));
    QCOMPARE(pdf::PDFDocumentLinkOperations::listExternalLinks(&output).internalLinkCount, 1);
}

void DocumentToolProtectTest::scanBounded()
{
    // A form drawing itself, a form of forms and a hostile content: the scan ends, nothing is changed
    RawPdf pdf;
    pdf.add(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.add(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    pdf.add(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /XObject << /Fm 5 0 R >> >> >>");
    pdf.addStream(4, "", "/Fm Do", false);
    pdf.addStream(5, "/Type /XObject /Subtype /Form /BBox [0 0 10 10] /Resources << /XObject << /Fm 5 0 R /Fn 6 0 R >> >>", "/Fm Do /Fn Do", false);
    pdf.addStream(6, "/Type /XObject /Subtype /Form /BBox [0 0 10 10]", "BT 3 Tr (MarkerFormText) Tj ET", true);
    const QByteArray data = pdf.build(1);
    const pdf::PDFDocument document = ProtectTestHelper::read(data);

    const pdf::PDFDocumentHiddenDataScan::Result result = pdf::PDFDocumentHiddenDataScan::scan(&document, true, nullptr, nullptr);
    QCOMPARE(result.get(pdf::PDFDocumentHiddenDataScan::Kind::InvisibleText).count, 1);

    // A canceled scan says so
    ProtectCanceledControl control;
    QVERIFY(pdf::PDFDocumentHiddenDataScan::scan(&document, true, &control, nullptr).isCanceled);
    pdf::PDFDocumentToolSanitize tool;
    QCOMPARE(tool.computePreview(ProtectTestHelper::createContext(document, data), QVariantMap(), &control, nullptr).status, pdf::PDFDocumentToolResultStatus::Stopped);
}

void DocumentToolProtectTest::presets()
{
    using Sanitize = pdf::PDFDocumentToolSanitize;
    using Kind = Sanitize::Kind;

    // 438:12831: Standard = everything except the invisible text
    const QVariantMap standard = Sanitize::getPresetValues(Sanitize::Preset::Standard);
    QCOMPARE(Sanitize::getChosenKinds(standard), (std::vector<Kind>{ Kind::DocumentInfo, Kind::Metadata, Kind::Comments, Kind::Attachments, Kind::Outline, Kind::PageLabels, Kind::SearchIndex, Kind::Thumbnails }));

    // Before sending: info, metadata, comments, index and thumbnails (438:13119)
    QCOMPARE(Sanitize::getChosenKinds(Sanitize::getPresetValues(Sanitize::Preset::BeforeSending)), (std::vector<Kind>{ Kind::DocumentInfo, Kind::Metadata, Kind::Comments, Kind::SearchIndex, Kind::Thumbnails }));

    // Before publishing: also attachments, the outline and the page labels stay
    QCOMPARE(Sanitize::getChosenKinds(Sanitize::getPresetValues(Sanitize::Preset::BeforePublishing)), (std::vector<Kind>{ Kind::DocumentInfo, Kind::Metadata, Kind::Comments, Kind::Attachments, Kind::SearchIndex, Kind::Thumbnails }));

    // Everything: every kind including the OCR text layer (438:13356)
    QCOMPARE(Sanitize::getChosenKinds(Sanitize::getPresetValues(Sanitize::Preset::Everything)).size(), size_t(10));

    // Clear all (438:13607): nothing, the label says Custom (K18)
    QVERIFY(Sanitize::getChosenKinds(Sanitize::getPresetValues(Sanitize::Preset::Custom)).empty());
    const std::vector<Kind> allKinds = pdf::PDFDocumentHiddenDataScan::getKinds();
    QCOMPARE(Sanitize::findPreset(Sanitize::getPresetValues(Sanitize::Preset::Custom), allKinds), Sanitize::Preset::Custom);
    QCOMPARE(Sanitize::findPreset(standard, allKinds), Sanitize::Preset::Standard);

    // A kind, which the document does not have, is not compared: 438:13119 has no search index
    QVariantMap sending = Sanitize::getPresetValues(Sanitize::Preset::BeforeSending);
    sending.insert(QStringLiteral("searchIndex"), false);
    QCOMPARE(Sanitize::findPreset(sending, allKinds), Sanitize::Preset::Custom);
    std::vector<Kind> withoutIndex = allKinds;
    withoutIndex.erase(std::find(withoutIndex.begin(), withoutIndex.end(), Kind::SearchIndex));
    QCOMPARE(Sanitize::findPreset(sending, withoutIndex), Sanitize::Preset::BeforeSending);

    // The defaults of the parameters are Standard, the signature fields are never a default
    pdf::PDFDocumentToolSanitize tool;
    for (const pdf::PDFDocumentToolParameterDefinition& definition : tool.getParameters())
    {
        QCOMPARE(definition.getDefaultValue().toBool(), standard.value(definition.getName()).toBool());
    }
    QCOMPARE(Sanitize::formatKindCount(1, false), QStringLiteral("1 kind"));
    QCOMPARE(Sanitize::formatKindCount(7, true), QStringLiteral("7 kinds of hidden data"));
}

QTEST_MAIN(DocumentToolProtectTest)

#include "tst_documenttoolprotecttest.moc"
