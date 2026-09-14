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
#include "pdfcertificatemanager.h"
#include "pdfsignaturehandler.h"
#include "pdfform.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"

#include <QtTest>
#include <QBuffer>
#include <QTemporaryDir>

using namespace pdf;

namespace
{
PDFObject dictionary(std::initializer_list<std::pair<const char*, PDFObject>> entries)
{
    auto result = std::make_shared<PDFDictionary>();
    for (const auto& entry : entries)
    {
        result->setEntry(PDFInplaceOrMemoryString(entry.first), PDFObject(entry.second));
    }
    return PDFObject::createDictionary(std::move(result));
}

PDFObjectReference appearance(PDFDocumentBuilder& builder, QRectF bbox)
{
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("XObject");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subtype");
    factory << WrapName("Form");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("BBox");
    factory << bbox;
    factory.endDictionaryItem();
    factory.endDictionary();
    const PDFObject object = factory.takeObject();
    PDFDictionary streamDictionary(*object.getDictionary());
    QByteArray data("1 0 0 rg 20 30 80 40 re f\n");
    streamDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(data.size()));
    return builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(streamDictionary), std::move(data))));
}
}

class SignatureBuilderTest : public QObject
{
    Q_OBJECT
private slots:
    void signedDocumentRoundTrip();
    void preservesAcroForm_data();
    void preservesAcroForm();
    void widgetStructure_data();
    void widgetStructure();
};

void SignatureBuilderTest::signedDocumentRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateManager::NewCertificateInfo info;
    info.fileName = directory.filePath("signature-test.p12");
    info.privateKeyPasword = "test-password";
    info.certCommonName = "PDF4QT signature regression test";
    info.rsaKeyLength = 2048;
    PDFCertificateManager().createCertificate(info);
    QFile file(info.fileName);
    QVERIFY(file.open(QIODevice::ReadOnly));
    PDFCertificateEntry certificate;
    certificate.pkcs12 = file.readAll();
    QVERIFY(!certificate.pkcs12.isEmpty());
    file.close();

    for (bool visible : {false, true})
    {
        QByteArray signature;
        QVERIFY(PDFSignatureFactory::sign(certificate, info.privateKeyPasword, "sample", signature));
        PDFDocumentBuilder builder;
        const auto page = builder.appendPage(QRectF(100, 200, 300, 400));
        const QByteArray marker("123456789123");
        const auto signatureValue = builder.createSignatureDictionary("Adobe.PPKLite", "adbe.pkcs7.detached", signature, QDateTime::currentDateTime(), marker.toLongLong());
        const auto stream = visible ? appearance(builder, QRectF(20, 30, 80, 40)) : PDFObjectReference();
        builder.createSignatureField("Signature", signatureValue, page, stream, QRectF(120, 230, 80, 40));
        PDFDocument document = builder.build();
        QBuffer buffer;
        QVERIFY(buffer.open(QIODevice::ReadWrite));
        QVERIFY(PDFDocumentWriter(nullptr).write(&buffer, &document));
        QByteArray bytes = buffer.data();
        const qsizetype contentsStart = bytes.indexOf(signature.toHex());
        QVERIFY(contentsStart > 0);
        const qsizetype contentsEnd = contentsStart + signature.toHex().size() + 1;
        const std::array<qsizetype, 4> ranges{0, contentsStart - 1, contentsEnd, bytes.size() - contentsEnd};
        for (auto it = ranges.crbegin(); it != ranges.crend(); ++it)
        {
            const qsizetype position = bytes.lastIndexOf(marker, contentsStart);
            QVERIFY(position >= 0);
            bytes.replace(position, marker.size(), QByteArray::number(*it).leftJustified(marker.size(), ' '));
        }
        const QByteArray signedData = bytes.left(ranges[1]) + bytes.mid(ranges[2], ranges[3]);
        QByteArray finalSignature;
        QVERIFY(PDFSignatureFactory::sign(certificate, info.privateKeyPasword, signedData, finalSignature));
        QCOMPARE(finalSignature.size(), signature.size());
        bytes.replace(contentsStart, signature.toHex().size(), finalSignature.toHex());
        PDFDocumentReader reader(nullptr, nullptr, false, false);
        document = reader.readFromBuffer(bytes);
        QVERIFY2(reader.getReadingResult() == PDFDocumentReader::Result::OK, qPrintable(reader.getErrorMessage()));
        const auto acroForm = document.getObjectByReference(builder.getCatalogReference()).getDictionary()->get("AcroForm");
        const PDFForm form = PDFForm::parse(&document, acroForm);
        PDFSignatureHandler::Parameters parameters;
        parameters.useSystemCertificateStore = false;
        const auto results = PDFSignatureHandler::verifySignatures(form, bytes, parameters);
        QCOMPARE(results.size(), size_t(1));
        QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));
        QVERIFY(!results.front().hasSignatureWarning());
        // A changed signed byte must be detected independently of certificate trust.
        QByteArray changedBytes = bytes;
        changedBytes[10] = changedBytes[10] == 'X' ? 'Y' : 'X';
        const auto changedResults = PDFSignatureHandler::verifySignatures(form, changedBytes, parameters);
        QCOMPARE(changedResults.size(), size_t(1));
        QVERIFY(!changedResults.front().isSignatureValid());
    }
}

void SignatureBuilderTest::preservesAcroForm_data()
{
    QTest::addColumn<bool>("indirectForm");
    QTest::addColumn<bool>("indirectFields");
    QTest::newRow("direct-form-direct-fields") << false << false;
    QTest::newRow("direct-form-indirect-fields") << false << true;
    QTest::newRow("indirect-form-direct-fields") << true << false;
    QTest::newRow("indirect-form-indirect-fields") << true << true;
}

void SignatureBuilderTest::preservesAcroForm()
{
    QFETCH(bool, indirectForm);
    QFETCH(bool, indirectFields);
    PDFDocumentBuilder original;
    const auto page = original.appendPage(QRectF(0, 0, 300, 400));
    const auto oldField = original.addObject(dictionary({{"FT", PDFObject::createName("Tx")},
                                                       {"T", PDFObjectFactory::createTextString("Existing")},
                                                       {"V", PDFObjectFactory::createTextString("Keep this value")}}));
    PDFObject fields = PDFObject::createArray(std::make_shared<PDFArray>(PDFDocumentBuilder::createObjectsFromReferences({oldField})));
    if (indirectFields)
    {
        fields = PDFObject::createReference(original.addObject(fields));
    }
    const auto resources = original.addObject(dictionary({}));
    const auto flags = original.addObject(PDFObject::createInteger(8));
    const PDFObject form = dictionary({{"Fields", fields}, {"SigFlags", PDFObject::createReference(flags)},
                                      {"NeedAppearances", PDFObject::createBool(true)},
                                      {"DR", PDFObject::createReference(resources)},
                                      {"DA", PDFObjectFactory::createTextString("/Helv 12 Tf 0 g")},
                                      {"Q", PDFObject::createInteger(2)},
                                      {"XFA", PDFObjectFactory::createTextString("Preserve XFA")}});
    const PDFObjectReference formReference = indirectForm ? original.addObject(form) : PDFObjectReference();
    original.mergeTo(original.getCatalogReference(), dictionary({{"AcroForm", indirectForm ? PDFObject::createReference(formReference) : form}}));
    const PDFDocument source = original.build();
    PDFDocumentBuilder builder(&source);
    const auto signature = builder.createSignatureDictionary("Adobe.PPKLite", "adbe.pkcs7.detached", "test", QDateTime::currentDateTime(), 0);
    const auto field = builder.createSignatureField("Signature", signature, page);
    const PDFDocument document = builder.build();
    const PDFObject acroForm = document.getObjectByReference(builder.getCatalogReference()).getDictionary()->get("AcroForm");
    if (indirectForm)
    {
        QCOMPARE(acroForm.getReference(), formReference);
    }
    const PDFDictionary* result = document.getDictionaryFromObject(acroForm);
    QVERIFY(result);
    const PDFObject resultFields = document.getObject(result->get("Fields"));
    QVERIFY(resultFields.isArray());
    QCOMPARE(resultFields.getArray()->getCount(), size_t(2));
    QCOMPARE(resultFields.getArray()->getItem(0).getReference(), oldField);
    QCOMPARE(resultFields.getArray()->getItem(1).getReference(), field);
    QCOMPARE(document.getObject(result->get("SigFlags")).getInteger(), PDFInteger(11));
    for (const char* key : {"NeedAppearances", "DR", "DA", "Q", "XFA"})
    {
        QVERIFY(result->get(key) == form.getDictionary()->get(key));
    }
    QVERIFY(document.getObjectByReference(oldField) == source.getObjectByReference(oldField));
    const auto* sourceForm = source.getDictionaryFromObject(source.getObjectByReference(builder.getCatalogReference()).getDictionary()->get("AcroForm"));
    QCOMPARE(source.getObject(sourceForm->get("Fields")).getArray()->getCount(), size_t(1));
    QCOMPARE(source.getObject(sourceForm->get("SigFlags")).getInteger(), PDFInteger(8));
}

void SignatureBuilderTest::widgetStructure_data()
{
    QTest::addColumn<bool>("visible");
    QTest::addColumn<QPointF>("origin");
    QTest::addColumn<int>("rotation");
    for (bool visible : {false, true})
    {
        for (QPointF origin : {QPointF(), QPointF(100, 200), QPointF(-100, -200)})
        {
            for (int rotation : {0, 90, 180, 270})
            {
                const QByteArray name = QByteArray::number(visible) + "-" + QByteArray::number(origin.x()) + "-" + QByteArray::number(rotation);
                QTest::newRow(name.constData()) << visible << origin << rotation;
            }
        }
    }
}

void SignatureBuilderTest::widgetStructure()
{
    QFETCH(bool, visible);
    QFETCH(QPointF, origin);
    QFETCH(int, rotation);
    PDFDocumentBuilder builder;
    const auto firstPage = builder.appendPage(QRectF(0, 0, 100, 100));
    const auto page = builder.appendPage(QRectF(origin, QSizeF(300, 400)));
    builder.setPageRotation(page, static_cast<PageRotation>(rotation / 90));
    const auto annotation = builder.addObject(dictionary({{"Type", PDFObject::createName("Annot")},
                                                        {"Subtype", PDFObject::createName("Text")}}));
    builder.mergeTo(page, dictionary({{"Annots", PDFObject::createArray(std::make_shared<PDFArray>(PDFDocumentBuilder::createObjectsFromReferences({annotation})))}}));
    // Existing /Annots may itself be indirect.
    const auto annots = builder.getStorage()->getObjectByReference(page).getDictionary()->get("Annots");
    const auto annotsReference = builder.addObject(annots);
    builder.mergeTo(page, dictionary({{"Annots", PDFObject::createReference(annotsReference)}}));
    const QRectF bbox(20, 30, 80, 40);
    const QRectF rect = bbox.translated(origin);
    const auto stream = visible ? appearance(builder, bbox) : PDFObjectReference();
    const auto signature = builder.createSignatureDictionary("Adobe.PPKLite", "adbe.pkcs7.detached", "test", QDateTime::currentDateTime(), 0);
    const auto field = builder.createSignatureField("Signature", signature, page, stream, visible ? rect : QRectF());
    PDFDocument document = builder.build();

    // Verify the serialized PDF too, including stream references and page annotations.
    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    PDFDocumentWriter writer(nullptr);
    QVERIFY(writer.write(&buffer, &document));
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    document = reader.readFromBuffer(buffer.data());
    QVERIFY2(reader.getReadingResult() == PDFDocumentReader::Result::OK, qPrintable(reader.getErrorMessage()));
    const PDFDictionary* widget = document.getObjectByReference(field).getDictionary();
    QVERIFY(widget);
    PDFDocumentDataLoaderDecorator loader(&document);
    QCOMPARE(loader.readRectangle(widget->get("Rect"), QRectF()), visible ? rect : QRectF());
    QCOMPARE(widget->get("F").getInteger(), PDFInteger(PDFAnnotation::Print));
    QCOMPARE(widget->get("P").getReference(), page);
    QCOMPARE(widget->get("V").getReference(), signature);
    QCOMPARE(widget->get("Subtype").getString(), QByteArray("Widget"));
    const auto* form = document.getDictionaryFromObject(document.getObjectByReference(builder.getCatalogReference()).getDictionary()->get("AcroForm"));
    QVERIFY(form);
    QCOMPARE(form->get("SigFlags").getInteger(), PDFInteger(3));
    QCOMPARE(document.getObject(form->get("Fields")).getArray()->getItem(0).getReference(), field);
    const PDFObject pageAnnots = document.getObject(document.getObjectByReference(page).getDictionary()->get("Annots"));
    QCOMPARE(pageAnnots.getArray()->getCount(), size_t(2));
    QCOMPARE(pageAnnots.getArray()->getItem(0).getReference(), annotation);
    QCOMPARE(pageAnnots.getArray()->getItem(1).getReference(), field);
    QVERIFY(document.getObjectByReference(firstPage).getDictionary()->get("Annots").isNull());
    if (visible)
    {
        const auto* ap = document.getDictionaryFromObject(widget->get("AP"));
        QVERIFY(ap);
        const PDFObject normal = document.getObject(ap->get("N"));
        QVERIFY(normal.isStream());
        QCOMPARE(loader.readRectangle(normal.getStream()->getDictionary()->get("BBox"), QRectF()), bbox);
    }
    else
    {
        QVERIFY(widget->get("AP").isNull());
    }
}

QTEST_MAIN(SignatureBuilderTest)
#include "tst_signaturebuildertest.moc"
