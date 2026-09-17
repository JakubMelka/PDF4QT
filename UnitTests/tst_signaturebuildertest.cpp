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
#include "pdfdocumentsigner.h"
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
    void certificateSignatureUsage();
    void signedDocumentRoundTrip();
    void signatureSizeChanges_data();
    void signatureSizeChanges();
    void signingFailureIsReported_data();
    void signingFailureIsReported();
    void multipleSignatures();
    void signingOverInvalidSignature();
    void existingSignaturesNotPreserved();
    void preservesAcroForm_data();
    void preservesAcroForm();
    void widgetStructure_data();
    void widgetStructure();

private:
    static bool createTestCertificate(const QTemporaryDir& directory, PDFCertificateEntry& certificate, QString& password);
    static std::vector<PDFSignatureVerificationResult> verifySignedDocument(const QByteArray& signedDocument);
    static PDFDocument readDocument(const QByteArray& data);

    /// Signs the first page of the document by an invisible signature
    static PDFDocumentSigner::Result signDocument(const PDFDocument& document,
                                                  const QByteArray& originalData,
                                                  const PDFCertificateEntry& certificate,
                                                  const QString& password,
                                                  const QString& fieldName,
                                                  QByteArray& signedDocument);
};

PDFDocument SignatureBuilderTest::readDocument(const QByteArray& data)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    return reader.readFromBuffer(data);
}

PDFDocumentSigner::Result SignatureBuilderTest::signDocument(const PDFDocument& document,
                                                             const QByteArray& originalData,
                                                             const PDFCertificateEntry& certificate,
                                                             const QString& password,
                                                             const QString& fieldName,
                                                             QByteArray& signedDocument)
{
    PDFDocumentSigner::Parameters parameters;
    parameters.document = &document;
    parameters.originalDocumentData = originalData;
    parameters.signFunction = [&](const QByteArray& data, QByteArray& signature)
    {
        return PDFSignatureFactory::sign(certificate, password, data, signature);
    };
    parameters.createSignatureFieldFunction = [&](PDFDocumentBuilder& builder, PDFObjectReference signatureDictionary)
    {
        return builder.createSignatureField(fieldName, signatureDictionary, document.getCatalog()->getPage(0)->getPageReference());
    };
    return PDFDocumentSigner::sign(parameters, signedDocument);
}

bool SignatureBuilderTest::createTestCertificate(const QTemporaryDir& directory, PDFCertificateEntry& certificate, QString& password)
{
    PDFCertificateManager::NewCertificateInfo info;
    info.fileName = directory.filePath("signature-test.p12");
    info.privateKeyPasword = "test-password";
    info.certCommonName = "PDF4QT signature regression test";
    info.rsaKeyLength = 2048;
    PDFCertificateManager().createCertificate(info);

    QFile file(info.fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }

    certificate = PDFCertificateEntry();
    certificate.pkcs12 = file.readAll();
    file.close();
    password = info.privateKeyPasword;

    return !certificate.pkcs12.isEmpty();
}

std::vector<PDFSignatureVerificationResult> SignatureBuilderTest::verifySignedDocument(const QByteArray& signedDocument)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(signedDocument);

    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        return { };
    }

    const PDFForm form = PDFForm::parse(&document, document.getCatalog()->getFormObject());
    PDFSignatureHandler::Parameters parameters;
    parameters.useSystemCertificateStore = false;
    return PDFSignatureHandler::verifySignatures(form, signedDocument, parameters);
}

void SignatureBuilderTest::certificateSignatureUsage()
{
    // Only certificates, which declare the digital signature or the non
    // repudiation key usage, can be offered to the user for signing. The
    // personal certificate storage of the operating system contains also
    // certificates without the key usage extension, which are generated
    // by the system for its internal purposes.
    PDFCertificateInfo info;
    QVERIFY(!info.isUsableForDigitalSignature());

    info.setKeyUsage(PDFCertificateInfo::KeyUsageKeyEncipherment);
    QVERIFY(!info.isUsableForDigitalSignature());

    info.setKeyUsage(PDFCertificateInfo::KeyUsageDigitalSignature);
    QVERIFY(info.isUsableForDigitalSignature());

    info.setKeyUsage(PDFCertificateInfo::KeyUsageNonRepudiation);
    QVERIFY(info.isUsableForDigitalSignature());

    info.setKeyUsage(PDFCertificateInfo::KeyUsageFlags(PDFCertificateInfo::KeyUsageDigitalSignature | PDFCertificateInfo::KeyUsageAgreement));
    QVERIFY(info.isUsableForDigitalSignature());
}

void SignatureBuilderTest::signedDocumentRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    for (bool visible : {false, true})
    {
        PDFDocumentBuilder builder;
        const auto page = builder.appendPage(QRectF(100, 200, 300, 400));
        PDFDocument document = builder.build();

        QByteArray signedDocument;
        PDFDocumentSigner::Parameters parameters;
        parameters.document = &document;
        parameters.signFunction = [&](const QByteArray& data, QByteArray& signature)
        {
            return PDFSignatureFactory::sign(certificate, password, data, signature);
        };
        parameters.createSignatureFieldFunction = [&](PDFDocumentBuilder& signedBuilder, PDFObjectReference signatureDictionary)
        {
            const auto stream = visible ? appearance(signedBuilder, QRectF(20, 30, 80, 40)) : PDFObjectReference();
            return signedBuilder.createSignatureField("Signature", signatureDictionary, page, stream, QRectF(120, 230, 80, 40));
        };

        QCOMPARE(PDFDocumentSigner::sign(parameters, signedDocument), PDFDocumentSigner::Result::OK);
        QVERIFY(!signedDocument.isEmpty());

        const auto results = verifySignedDocument(signedDocument);
        QCOMPARE(results.size(), size_t(1));
        QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));
        QVERIFY(!results.front().hasSignatureWarning());

        // A changed signedData byte must be detected independently of certificate trust.
        QByteArray changedBytes = signedDocument;
        changedBytes[10] = changedBytes[10] == 'X' ? 'Y' : 'X';
        const auto changedResults = verifySignedDocument(changedBytes);
        QCOMPARE(changedResults.size(), size_t(1));
        QVERIFY(!changedResults.front().isSignatureValid());
    }
}

void SignatureBuilderTest::signatureSizeChanges_data()
{
    QTest::addColumn<int>("extraBytes");

    // The size of a signature is not stable - the same data signedData twice can
    // produce results differing by a few bytes. The document must be signedData
    // correctly whatever the size of the final signature is, including the case
    // when it does not fit into the initially reserved space.
    QTest::newRow("same-size") << 0;
    QTest::newRow("longer-by-one") << 1;
    QTest::newRow("longer-by-eight") << 8;
    QTest::newRow("longer-than-reserved-space") << 2000;
    QTest::newRow("much-longer-than-reserved-space") << 40000;
}

void SignatureBuilderTest::signatureSizeChanges()
{
    QFETCH(int, extraBytes);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    const auto page = builder.appendPage(QRectF(0, 0, 300, 400));
    PDFDocument document = builder.build();

    // The trial signature keeps its size, the signature of the document is
    // enlarged - the padding is ignored by the DER decoder, so the signature
    // stays valid.
    QByteArray signedDocument;
    PDFDocumentSigner::Parameters parameters;
    parameters.document = &document;
    parameters.signFunction = [&](const QByteArray& data, QByteArray& signature)
    {
        if (!PDFSignatureFactory::sign(certificate, password, data, signature))
        {
            return false;
        }

        if (data.size() > 1000)
        {
            signature.append(QByteArray(extraBytes, char(0)));
        }

        return true;
    };
    parameters.createSignatureFieldFunction = [&](PDFDocumentBuilder& signedBuilder, PDFObjectReference signatureDictionary)
    {
        return signedBuilder.createSignatureField("Signature", signatureDictionary, page);
    };

    QCOMPARE(PDFDocumentSigner::sign(parameters, signedDocument), PDFDocumentSigner::Result::OK);

    const auto results = verifySignedDocument(signedDocument);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));

    // Nothing outside of the signature string may be left out of the signature
    QVERIFY(!results.front().hasSignatureWarning());
}

void SignatureBuilderTest::signingFailureIsReported_data()
{
    QTest::addColumn<int>("failureMode");
    QTest::addColumn<int>("expectedResult");

    QTest::newRow("signing-failed") << 0 << int(PDFDocumentSigner::Result::SigningFailed);
    QTest::newRow("signature-never-fits") << 1 << int(PDFDocumentSigner::Result::SignatureTooLarge);
    QTest::newRow("damaged-signature") << 2 << int(PDFDocumentSigner::Result::VerificationFailed);
}

void SignatureBuilderTest::signingFailureIsReported()
{
    QFETCH(int, failureMode);
    QFETCH(int, expectedResult);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    const auto page = builder.appendPage(QRectF(0, 0, 300, 400));
    PDFDocument document = builder.build();

    QByteArray signedDocument;
    int signatureGrowth = 0;
    PDFDocumentSigner::Parameters parameters;
    parameters.document = &document;
    parameters.signFunction = [&](const QByteArray& data, QByteArray& signature)
    {
        const bool isDocumentSigned = data.size() > 1000;

        if (failureMode == 0 && isDocumentSigned)
        {
            return false;
        }

        if (!PDFSignatureFactory::sign(certificate, password, data, signature))
        {
            return false;
        }

        if (failureMode == 1 && isDocumentSigned)
        {
            // The signature quadruples on each attempt, which is faster than the
            // signer can enlarge the reserved space, so it never fits - the
            // signer must give up instead of writing a damaged document.
            signature.append(QByteArray((1 << 16) << (2 * signatureGrowth++), char(0)));
        }

        if (failureMode == 2 && isDocumentSigned)
        {
            // The signature does not belong to the signedData data
            PDFSignatureFactory::sign(certificate, password, QByteArray("something else"), signature);
        }

        return true;
    };
    parameters.createSignatureFieldFunction = [&](PDFDocumentBuilder& signedBuilder, PDFObjectReference signatureDictionary)
    {
        return signedBuilder.createSignatureField("Signature", signatureDictionary, page);
    };

    QCOMPARE(int(PDFDocumentSigner::sign(parameters, signedDocument)), expectedResult);

    // Nothing may be handed over to the caller, when the document was not signedData
    QVERIFY(signedDocument.isEmpty());
}

void SignatureBuilderTest::multipleSignatures()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const PDFDocument document = builder.build();

    QByteArray firstSigned;
    QCOMPARE(signDocument(document, QByteArray(), certificate, password, "First", firstSigned), PDFDocumentSigner::Result::OK);

    // The second signature is added as an incremental update, so the bytes
    // covered by the first signature are not touched and it stays valid.
    QByteArray secondSigned;
    QCOMPARE(signDocument(readDocument(firstSigned), firstSigned, certificate, password, "Second", secondSigned), PDFDocumentSigner::Result::OK);
    QVERIFY(secondSigned.startsWith(firstSigned));

    QByteArray thirdSigned;
    QCOMPARE(signDocument(readDocument(secondSigned), secondSigned, certificate, password, "Third", thirdSigned), PDFDocumentSigner::Result::OK);
    QVERIFY(thirdSigned.startsWith(secondSigned));

    const auto results = verifySignedDocument(thirdSigned);
    QCOMPARE(results.size(), size_t(3));
    for (const PDFSignatureVerificationResult& result : results)
    {
        QVERIFY2(result.isSignatureValid(), qPrintable(result.getSignatureFieldQualifiedName() + ": " + result.getErrors().join('\n')));
    }

    // The last signature covers the whole document, the earlier ones only their part of it
    QVERIFY(results.front().hasSignatureWarning());
    QVERIFY(!results.back().hasSignatureWarning());
    QCOMPARE(results.back().getSignatureFieldQualifiedName(), QString("Third"));
}

void SignatureBuilderTest::signingOverInvalidSignature()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const PDFDocument document = builder.build();

    QByteArray signedData;
    QCOMPARE(signDocument(document, QByteArray(), certificate, password, "First", signedData), PDFDocumentSigner::Result::OK);

    // Damage the first signature by changing the first byte of its value - the
    // document stays readable, but the signature cannot be decoded anymore
    const qsizetype contents = signedData.indexOf("/Contents <");
    QVERIFY(contents > 0);
    const qsizetype firstDigit = contents + qsizetype(std::strlen("/Contents <"));
    signedData[firstDigit] = signedData[firstDigit] == '3' ? '4' : '3';
    const auto damagedResults = verifySignedDocument(signedData);
    QCOMPARE(damagedResults.size(), size_t(1));
    QVERIFY(!damagedResults.front().isSignatureValid());

    // A document with an invalid signature can still be signedData, only the new
    // signature is verified
    QByteArray resigned;
    QCOMPARE(signDocument(readDocument(signedData), signedData, certificate, password, "Second", resigned), PDFDocumentSigner::Result::OK);

    const auto results = verifySignedDocument(resigned);
    QCOMPARE(results.size(), size_t(2));
    QCOMPARE(results.front().getSignatureFieldQualifiedName(), QString("First"));
    QVERIFY(!results.front().isSignatureValid());
    QCOMPARE(results.back().getSignatureFieldQualifiedName(), QString("Second"));
    QVERIFY2(results.back().isSignatureValid(), qPrintable(results.back().getErrors().join('\n')));
    QVERIFY(!results.back().hasSignatureWarning());
}

void SignatureBuilderTest::existingSignaturesNotPreserved()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const PDFDocument document = builder.build();

    QByteArray signedData;
    QCOMPARE(signDocument(document, QByteArray(), certificate, password, "First", signedData), PDFDocumentSigner::Result::OK);
    const PDFDocument signedDocument = readDocument(signedData);

    // Without the original data, or with data which cannot be updated, the
    // existing signature would be damaged by writing the document, so the
    // signer must refuse instead
    QByteArray resigned;
    QCOMPARE(signDocument(signedDocument, QByteArray(), certificate, password, "Second", resigned), PDFDocumentSigner::Result::ExistingSignaturesNotPreserved);
    QVERIFY(resigned.isEmpty());
    QCOMPARE(signDocument(signedDocument, QByteArray("not a document"), certificate, password, "Second", resigned), PDFDocumentSigner::Result::ExistingSignaturesNotPreserved);
    QVERIFY(resigned.isEmpty());
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
    const auto signature = builder.createSignatureDictionary("Adobe.PPKLite", "adbe.pkcs7.detached", "test", QDateTime::currentDateTime(), 0, "Sig");
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
    const auto signature = builder.createSignatureDictionary("Adobe.PPKLite", "adbe.pkcs7.detached", "test", QDateTime::currentDateTime(), 0, "Sig");
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
