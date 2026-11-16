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
#include "pdfobjectutils.h"
#include "restoredsignedfile.h"
#include "pdfcatalog.h"
#include "pdfdocmdppermissions.h"
#include "pdfsecurityhandler.h"

#include "mergetestpdf.h"

#include <QtTest>
#include <QBuffer>
#include <QTemporaryDir>

using namespace pdf;

namespace signaturebuildertest
{
PDFObject dictionary(std::initializer_list<std::pair<const char*, PDFObject>> entries)
{
    PDFDictionaryBuilder result;
    for (const auto& entry : entries)
    {
        result.setEntry(PDFInplaceOrMemoryString(entry.first), PDFObject(entry.second));
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
    PDFDictionaryBuilder streamDictionary(*object.getDictionary());
    QByteArray data("1 0 0 rg 20 30 80 40 re f\n");
    streamDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(data.size()));
    return builder.addObject(PDFObject::createStream(PDFStream(std::move(streamDictionary), std::move(data))));
}
}   // namespace signaturebuildertest

using namespace signaturebuildertest;

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
    void signedObjectStreamsUpdatedIncrementally();
    void signedFileWithRawCarriageReturn();
    void signatureOfRestoredFile();
    void signingOverInvalidSignature();
    void existingSignaturesNotPreserved();
    void preservesAcroForm_data();
    void preservesAcroForm();
    void widgetStructure_data();
    void widgetStructure();

    // Step 6 of phase 2, package P2 (plan 4.4, 9 with R6-1, R6-4, R6-12, B121, B122, B134, B135)
    void signEncryptedDocument_data();
    void signEncryptedDocument();
    void contentsAlwaysHexadecimal();
    void byteRangeOfWrittenSignature();
    void estimatedSizeWithoutTrialSignature();
    void certifiedWithoutChanges();
    void permissionsOfEncryptedDocument();
    void signaturesCountedOfFile();
    void unsavedChangesOfSignedFile();
    void documentInformationUnchanged();

    // Step 6 of phase 2, fix wave P2S6FIXCORE
    void signatureWidgetWithoutAcroForm();
    void signatureWidgetsWalkWithoutAcroForm();
    void unreadableFileNeverRewritten();

private:
    /// The bytes of a whole write of the document
    static QByteArray writeDocument(const PDFDocument& document);

    /// The signature of a document of one page by a function of the test
    static PDFDocumentSigner::Parameters createParameters(const PDFDocument& document, const QByteArray& originalData, const PDFDocumentSigner::SignFunction& signFunction);

    static bool createTestCertificate(const QTemporaryDir& directory, PDFCertificateEntry& certificate, QString& password);
    static std::vector<PDFSignatureVerificationResult> verifySignedDocument(const QByteArray& signedDocument);
    static PDFDocument readDocument(const QByteArray& data);

    /// A file of the structures of PDF 1.5: the document in an object stream, a cross-reference stream
    static QByteArray createObjectStreamDocument();

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

QByteArray SignatureBuilderTest::writeDocument(const PDFDocument& document)
{
    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly) || !PDFDocumentWriter(nullptr).write(&buffer, &document))
    {
        return QByteArray();
    }
    return data;
}

PDFDocumentSigner::Parameters SignatureBuilderTest::createParameters(const PDFDocument& document, const QByteArray& originalData, const PDFDocumentSigner::SignFunction& signFunction)
{
    PDFDocumentSigner::Parameters parameters;
    parameters.document = &document;
    parameters.originalDocumentData = originalData;
    parameters.signFunction = signFunction;
    parameters.createSignatureFieldFunction = [&document](PDFDocumentBuilder& builder, PDFObjectReference signatureDictionary)
    {
        return builder.createSignatureField("Signature", signatureDictionary, document.getCatalog()->getPage(0)->getPageReference());
    };
    return parameters;
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

    // The last signature covers the whole document, the earlier ones only their part of it: the later
    // revisions carry only signatures, which is no change of the signed document (R6-3 of the plan of
    // step 6 of phase 2, ISO 32000-2 12.8.1) - an information, not a warning
    QCOMPARE(results.front().getLaterRevisions(), PDFSignatureVerificationResult::LaterRevisions::Allowed);
    QVERIFY(!results.front().hasSignatureWarning());
    QCOMPARE(results.back().getLaterRevisions(), PDFSignatureVerificationResult::LaterRevisions::None);
    QVERIFY(!results.back().hasSignatureWarning());
    QCOMPARE(results.back().getSignatureFieldQualifiedName(), QString("Third"));
}

QByteArray SignatureBuilderTest::createObjectStreamDocument()
{
    QByteArray data("%PDF-1.5\r\n");

    // PDF Reference 1.7, 3.4.6, Table 3.14: the catalog and the page tree in an object stream
    QByteArray pairs;
    QByteArray body;
    int number = 0;
    for (const QByteArray& object : { QByteArray("<< /Type /Catalog /Pages 2 0 R >>"),
                                      QByteArray("<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>"),
                                      QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 300 400 ] >>") })
    {
        pairs += QByteArray::number(++number) + " " + QByteArray::number(body.size()) + " ";
        body += object + " ";
    }

    const qint64 objectStreamOffset = data.size();
    data += "4 0 obj\r\n<< /Type /ObjStm /N 3 /First " + QByteArray::number(pairs.size()) + " /Length " + QByteArray::number(pairs.size() + body.size()) + " >>\r\nstream\r\n" + pairs + body + "\r\nendstream\r\nendobj\r\n";

    // 3.4.7, Table 3.16: objects 1 ... 3 in the object stream 4, the cross-reference stream itself
    const qint64 crossReferenceOffset = data.size();
    QByteArray entries;
    const auto addEntry = [&entries](int type, qint64 second, int third)
    {
        entries.append(char(type));
        for (int shift = 24; shift >= 0; shift -= 8)
        {
            entries.append(char((second >> shift) & 0xFF));
        }
        entries.append(char((third >> 8) & 0xFF));
        entries.append(char(third & 0xFF));
    };
    addEntry(0, 0, 65535);
    addEntry(2, 4, 0);
    addEntry(2, 4, 1);
    addEntry(2, 4, 2);
    addEntry(1, objectStreamOffset, 0);
    addEntry(1, crossReferenceOffset, 0);
    data += "5 0 obj\r\n<< /Type /XRef /Size 6 /W [ 1 4 2 ] /Root 1 0 R /Length " + QByteArray::number(entries.size()) + " >>\r\nstream\r\n" + entries + "\r\nendstream\r\nendobj\r\n";
    data += "startxref\r\n" + QByteArray::number(crossReferenceOffset) + "\r\n%%EOF\r\n";
    return data;
}

void SignatureBuilderTest::signedFileWithRawCarriageReturn()
{
    // A signature covers the bytes of the file (PDF Reference 1.7, 8.7), not the values read from them: a raw CR of a
    // literal string in the signed part is read as a line feed (3.2.3), the file itself is not changed by the reading.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    QByteArray original("%PDF-1.7\n");
    std::vector<qsizetype> offsets;
    for (const QByteArray& object : { QByteArray("<< /Type /Catalog /Pages 2 0 R >>"),
                                      QByteArray("<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>"),
                                      QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 300 400 ] /Note 4 0 R >>"),
                                      QByteArray("(first\rsecond\r\nthird)") })
    {
        offsets.push_back(original.size());
        original += QByteArray::number(qulonglong(offsets.size())) + " 0 obj\n" + object + "\nendobj\n";
    }
    const qsizetype tableOffset = original.size();
    original += "xref\n0 5\n0000000000 65535 f\r\n";
    for (qsizetype offset : offsets)
    {
        original += QByteArray::number(qlonglong(offset)).rightJustified(10, '0') + " 00000 n\r\n";
    }
    original += "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n" + QByteArray::number(qlonglong(tableOffset)) + "\n%%EOF\n";

    const PDFDocument document = readDocument(original);
    QCOMPARE(document.getObjectByReference(PDFObjectReference(4, 0)).getString(), QByteArray("first\nsecond\nthird"));

    QByteArray signedData;
    QCOMPARE(signDocument(document, original, certificate, password, "Signature", signedData), PDFDocumentSigner::Result::OK);
    QVERIFY(signedData.startsWith(original));

    const std::vector<PDFSignatureVerificationResult> results = verifySignedDocument(signedData);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));

    // The value of the signed document reads the same, the bytes of the signed part have the raw CR
    QCOMPARE(readDocument(signedData).getObjectByReference(PDFObjectReference(4, 0)).getString(), QByteArray("first\nsecond\nthird"));

    // The same value by another byte of the file is another signed file
    QByteArray changed = signedData;
    const qsizetype position = changed.indexOf("first\rsecond");
    QVERIFY(position != -1);
    changed[position + 5] = '\n';
    QCOMPARE(readDocument(changed).getObjectByReference(PDFObjectReference(4, 0)).getString(), QByteArray("first\nsecond\nthird"));
    const std::vector<PDFSignatureVerificationResult> changedResults = verifySignedDocument(changed);
    QCOMPARE(changedResults.size(), size_t(1));
    QVERIFY(!changedResults.front().isSignatureValid());
}

void SignatureBuilderTest::signedObjectStreamsUpdatedIncrementally()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    const QByteArray original = createObjectStreamDocument();
    const PDFDocument document = readDocument(original);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(1));

    // The signature is an update of a file with object streams
    QByteArray signedData;
    QCOMPARE(signDocument(document, original, certificate, password, "First", signedData), PDFDocumentSigner::Result::OK);
    QVERIFY(signedData.startsWith(original));
    const PDFDocument signedDocument = readDocument(signedData);

    // An edit of the user is appended: the signed bytes stay, the signature is valid
    PDFDocumentBuilder builder(&signedDocument);
    builder.addObject(PDFObject::createInteger(1234567));
    const PDFDocument edited = builder.build();

    QByteArray updated;
    {
        QBuffer buffer(&updated);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        PDFDocumentWriter writer(nullptr);
        QVERIFY(bool(writer.writeIncrementalUpdate(&buffer, signedData, &edited)));
    }
    QVERIFY(updated.startsWith(signedData));
    QVERIFY(updated.mid(signedData.size()).contains("1234567"));

    std::vector<PDFSignatureVerificationResult> results = verifySignedDocument(updated);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));

    // The containers dropped from the document (a whole write of a tool) drop nothing of an
    // update: no object is written over the number of the object stream, whose objects the
    // older sections still lead to, and the signature stays valid
    PDFObjectStorage storage = signedDocument.getStorage();
    QVERIFY(PDFObjectUtils::removeFileStructureObjects(storage) >= 2);
    QVERIFY(storage.getObject(PDFObjectReference(4, 0)).isNull());
    const PDFDocument withoutContainers(std::move(storage), signedDocument.getInfo()->version, QByteArray());

    QByteArray withoutContainersUpdated;
    {
        QBuffer buffer(&withoutContainersUpdated);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        PDFDocumentWriter writer(nullptr);
        QVERIFY(bool(writer.writeIncrementalUpdate(&buffer, signedData, &withoutContainers)));
    }
    QVERIFY(withoutContainersUpdated.startsWith(signedData));
    QVERIFY(!withoutContainersUpdated.mid(signedData.size()).contains("\n4 0 obj"));

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    const PDFDocument reopened = reader.readFromBuffer(withoutContainersUpdated);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
    QCOMPARE(reopened.getCatalog()->getPageCount(), size_t(1));
    QVERIFY(PDFObjectUtils::isObjectStream(reopened.getStorage().getObject(PDFObjectReference(4, 0))));

    results = verifySignedDocument(withoutContainersUpdated);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));

    // The old signer: another object under the number of the object stream cannot be an update.
    // A document without signatures is written whole instead of a broken update, a document
    // with signatures is refused
    const auto replaceObjectStream = [](const PDFDocument& input)
    {
        PDFObjectStorage replaced = input.getStorage();
        replaced.setObject(PDFObjectReference(4, 0), PDFObject::createInteger(1));
        return PDFDocument(std::move(replaced), input.getInfo()->version, QByteArray());
    };

    const PDFDocument unsignedReplaced = replaceObjectStream(document);
    QByteArray unsignedResult;
    QCOMPARE(signDocument(unsignedReplaced, original, certificate, password, "First", unsignedResult), PDFDocumentSigner::Result::OK);
    QVERIFY(!unsignedResult.startsWith(original));
    QCOMPARE(verifySignedDocument(unsignedResult).size(), size_t(1));
    QVERIFY(verifySignedDocument(unsignedResult).front().isSignatureValid());

    const PDFDocument signedReplaced = replaceObjectStream(signedDocument);
    QByteArray signedResult;
    QCOMPARE(signDocument(signedReplaced, signedData, certificate, password, "Second", signedResult), PDFDocumentSigner::Result::ExistingSignaturesNotPreserved);
    QVERIFY(signedResult.isEmpty());
}

void SignatureBuilderTest::signatureOfRestoredFile()
{
    // A signature is about bytes, a table of a file (ISO 32000-2, 7.5.4) says, which objects are the document. Without
    // a table the reader can read, the objects come from the bytes, so a signature over each of the bytes proves nothing
    // about the shown page: the attacker has put a page into the file, which no reader by the table shows to the signer.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    using Sample = pdftest::RestoredSignedFile;

    // The content of the page: the streams of /Contents, a missing one gives nothing
    const auto readContent = [](const PDFDocument& document)
    {
        QByteArray content;
        const PDFObject contents = document.getObject(document.getCatalog()->getPage(0)->getContents());
        if (contents.isArray())
        {
            for (size_t i = 0; i < contents.getArray()->getCount(); ++i)
            {
                const PDFObject item = document.getObject(contents.getArray()->getItem(i));
                content += item.isStream() ? document.getDecodedStream(item.getStream()) : QByteArray();
            }
        }
        return content;
    };
    const auto verifyTrustingSigner = [](const PDFDocument& document, const QByteArray& data)
    {
        // The certificate of the signer is trusted, as a real certificate of a real signer is
        const PDFForm form = PDFForm::parse(&document, document.getCatalog()->getFormObject());
        PDFSignatureHandler::Parameters parameters;
        parameters.useSystemCertificateStore = false;
        std::vector<PDFSignatureVerificationResult> results = PDFSignatureHandler::verifySignatures(form, data, parameters);
        if (results.size() != 1 || results.front().getCertificateInfos().empty())
        {
            return results;
        }

        PDFCertificateStore store;
        for (const PDFCertificateInfo& info : results.front().getCertificateInfos())
        {
            store.add(PDFCertificateEntry::EntryType::User, info.getCertificateData());
        }
        parameters.store = &store;
        return PDFSignatureHandler::verifySignatures(form, data, parameters);
    };

    // The control: the same file valid for the strict reader, the page of the table is shown and the signature is valid
    const QByteArray validData = Sample::create(certificate, password, Sample::Damage::None);
    QVERIFY(!validData.isEmpty());
    {
        PDFDocumentReader reader(nullptr, nullptr, false, false);
        const PDFDocument document = reader.readFromBuffer(validData);
        QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
        QVERIFY(!reader.isDocumentRestored());
        QVERIFY(readContent(document).contains(Sample::SIGNED_TEXT));
        QVERIFY(readContent(document).contains(Sample::LOST_TEXT));
        QVERIFY(!readContent(document).contains(Sample::HIDDEN_TEXT));

        const std::vector<PDFSignatureVerificationResult> results = verifyTrustingSigner(document, validData);
        QCOMPARE(results.size(), size_t(1));
        QVERIFY2(results.front().isValid(), qPrintable(results.front().getErrors().join('\n')));
        QVERIFY(!results.front().hasFlag(PDFSignatureVerificationResult::Warning_Signature_NotCoveredBytes));
        QVERIFY(!results.front().hasFlag(PDFSignatureVerificationResult::Warning_Signature_FileRestored));
        QVERIFY(!results.front().hasWarning());
    }

    // A wrong count of the first table: the reader refuses the table, the shown page is not the signed one
    const QByteArray data = Sample::create(certificate, password, Sample::Damage::TableCount);
    QVERIFY(!data.isEmpty());
    {
        PDFDocumentReader strictReader(nullptr, nullptr, false, false);
        strictReader.readFromBuffer(data);
        QCOMPARE(strictReader.getReadingResult(), PDFDocumentReader::Result::Failed);
    }

    PDFDocumentReader reader(nullptr, nullptr, true, false);
    const PDFDocument document = reader.readFromBuffer(data);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
    QVERIFY(reader.isDocumentRestored());
    QVERIFY(readContent(document).contains(Sample::HIDDEN_TEXT));

    // The signature itself is intact and covers the whole file - and the result says, that it does not vouch for the page
    const std::vector<PDFSignatureVerificationResult> results = verifyTrustingSigner(document, data);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));
    QVERIFY(!results.front().hasFlag(PDFSignatureVerificationResult::Warning_Signature_NotCoveredBytes));
    QVERIFY(results.front().hasFlag(PDFSignatureVerificationResult::Warning_Signature_FileRestored));
    QVERIFY(results.front().hasSignatureWarning());
    QVERIFY(results.front().hasWarning());
    QCOMPARE(results.front().getWarnings().size(), 1);

    // A wrong length of a stream of the page: the reader goes by the table and leaves the stream out, a part of
    // the page the signer has read is not shown. Nothing was changed in the bytes, only the reader sees it.
    const QByteArray streamData = Sample::create(certificate, password, Sample::Damage::StreamLength);
    QVERIFY(!streamData.isEmpty());
    PDFDocumentReader streamReader(nullptr, nullptr, true, false);
    const PDFDocument streamDocument = streamReader.readFromBuffer(streamData);
    QCOMPARE(streamReader.getReadingResult(), PDFDocumentReader::Result::OK);
    QVERIFY(streamReader.isDocumentRestored());
    QVERIFY(readContent(streamDocument).contains(Sample::SIGNED_TEXT));
    QVERIFY(!readContent(streamDocument).contains(Sample::LOST_TEXT));

    const std::vector<PDFSignatureVerificationResult> streamResults = verifyTrustingSigner(streamDocument, streamData);
    QCOMPARE(streamResults.size(), size_t(1));
    QVERIFY2(streamResults.front().isSignatureValid(), qPrintable(streamResults.front().getErrors().join('\n')));
    QVERIFY(!streamResults.front().hasFlag(PDFSignatureVerificationResult::Warning_Signature_NotCoveredBytes));
    QVERIFY(streamResults.front().hasFlag(PDFSignatureVerificationResult::Warning_Signature_FileRestored));
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
    PDFObject fields = PDFObject::createArray(PDFArrayBuilder(PDFDocumentBuilder::createObjectsFromReferences({oldField})));
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
    builder.mergeTo(page, dictionary({{"Annots", PDFObject::createArray(PDFArrayBuilder(PDFDocumentBuilder::createObjectsFromReferences({annotation})))}}));
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

void SignatureBuilderTest::signEncryptedDocument_data()
{
    QTest::addColumn<int>("algorithm");
    QTest::addColumn<QString>("userPassword");

    QTest::newRow("rc4-open-password") << int(PDFSecurityHandlerFactory::RC4) << QStringLiteral("user");
    QTest::newRow("aes256-open-password") << int(PDFSecurityHandlerFactory::AES_256) << QStringLiteral("user");
    QTest::newRow("aes256-owner-only") << int(PDFSecurityHandlerFactory::AES_256) << QString();
}

void SignatureBuilderTest::signEncryptedDocument()
{
    // R6-1 (plan of step 6 of phase 2): the read back of the original and of the signed file used
    // an empty password callback - the security handler of a file with an open password calls it
    // after the empty password fails (std::bad_function_call). The writer and the signer know no
    // password: they read the file by a clone of the authenticated handler of the document
    // (PDFDocumentReader::setAuthenticatedSecurityHandler); a document of another encryption than the
    // file is refused with the reason and nothing is written.
    QFETCH(int, algorithm);
    QFETCH(QString, userPassword);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = PDFSecurityHandlerFactory::Algorithm(algorithm);
    settings.userPassword = userPassword;
    settings.ownerPassword = QStringLiteral("owner");
    settings.permissions = 0xFFFFFFFF;
    settings.id = builder.ensureDocumentId();
    builder.setSecurityHandler(PDFSecurityHandlerFactory::createSecurityHandler(settings));
    const QByteArray original = writeDocument(builder.build());
    QVERIFY(!original.isEmpty());

    // The tab has opened the file by its open password
    const std::function<QString(bool*)> giveUserPassword = [userPassword](bool* ok)
    {
        *ok = true;
        return userPassword;
    };
    PDFDocumentReader reader(nullptr, giveUserPassword, false, false);
    const PDFDocument document = reader.readFromBuffer(original);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
    QVERIFY(document.getStorage().getSecurityHandler()->getMode() != EncryptionMode::None);

    // An edit of the user saved incrementally (the Keep of Sign by Hand of a signed file)
    PDFDocumentBuilder edit(&document);
    edit.addObject(PDFObject::createInteger(7654321));
    const PDFDocument edited = edit.build();
    QByteArray update;
    PDFOperationResult updateResult(true);
    {
        QBuffer buffer(&update);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        updateResult = PDFDocumentWriter(nullptr).writeIncrementalUpdate(&buffer, original, &edited);
    }

    QByteArray signedData;
    PDFDocumentSigner::Details details;
    const PDFDocumentSigner::Result result = PDFDocumentSigner::sign(createParameters(document, original, [&](const QByteArray& data, QByteArray& signature)
    {
        return PDFSignatureFactory::sign(certificate, password, data, signature);
    }), signedData, &details);

    // The update and the signature are made over the exact bytes of the file - with an open password too
    QVERIFY2(updateResult, qPrintable(updateResult.getErrorMessage()));
    QVERIFY(update.startsWith(original));
    QCOMPARE(result, PDFDocumentSigner::Result::OK);
    QVERIFY(details.isIncrementalUpdate);
    QVERIFY(signedData.startsWith(original));
    const PDFSecurityHandler* handler = document.getStorage().getSecurityHandler();
    QVERIFY(!PDFDocumentSigner::hasExistingSignatures(original, handler));
    QVERIFY(PDFDocumentSigner::hasExistingSignatures(signedData, handler));

    // ISO 32000-2, 7.6.2, 7.5.6: the update keeps the encryption of the file - the update and the signed
    // file open only by the password; the edit is in the update
    for (const QByteArray* bytes : { &update, &signedData })
    {
        PDFDocumentReader refusing(nullptr, [](bool* ok) { *ok = false; return QString(); }, false, false);
        refusing.readFromBuffer(*bytes);
        QCOMPARE(refusing.getReadingResult(), userPassword.isEmpty() ? PDFDocumentReader::Result::OK : PDFDocumentReader::Result::Canceled);
    }
    PDFDocumentReader updateReader(nullptr, giveUserPassword, false, false);
    const PDFDocument updated = updateReader.readFromBuffer(update);
    QCOMPARE(updateReader.getReadingResult(), PDFDocumentReader::Result::OK);
    bool hasEditedObject = false;
    for (const PDFObjectStorage::Entry& entry : updated.getStorage().getObjects())
    {
        hasEditedObject = hasEditedObject || (entry.object.isInt() && entry.object.getInteger() == 7654321);
    }
    QVERIFY(hasEditedObject);

    // The strings of the new objects are encrypted (the name of the field is not in the update in the
    // clear), /Contents of the signature is not - the signature verifies
    const QByteArray updatePart = signedData.mid(original.size());
    QVERIFY(!updatePart.contains("(Signature)"));
    QVERIFY(updatePart.contains("/Contents <30"));
    PDFDocumentReader signedReader(nullptr, giveUserPassword, false, false);
    const PDFDocument signedDocument = signedReader.readFromBuffer(signedData);
    QCOMPARE(signedReader.getReadingResult(), PDFDocumentReader::Result::OK);
    const PDFForm form = PDFForm::parse(&signedDocument, signedDocument.getCatalog()->getFormObject());
    PDFSignatureHandler::Parameters parameters;
    parameters.useSystemCertificateStore = false;
    parameters.securityHandler = signedDocument.getStorage().getSecurityHandler();
    const std::vector<PDFSignatureVerificationResult> results = PDFSignatureHandler::verifySignatures(form, signedData, parameters);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));
    QCOMPARE(results.front().getSignatureFieldQualifiedName(), QStringLiteral("Signature"));

    // A document of another encryption than its file (the tab encrypted it again, not saved yet): its
    // handler does not open the file - the signer refuses it with the reason, nothing is written
    if (!userPassword.isEmpty())
    {
        PDFDocumentBuilder reencrypt(&document);
        PDFSecurityHandlerFactory::SecuritySettings otherSettings = settings;
        otherSettings.userPassword = QStringLiteral("another");
        reencrypt.setSecurityHandler(PDFSecurityHandlerFactory::createSecurityHandler(otherSettings));
        const PDFDocument reencrypted = reencrypt.build();
        QByteArray refused;
        QCOMPARE(PDFDocumentSigner::sign(createParameters(reencrypted, original, [&](const QByteArray& data, QByteArray& signature)
        {
            return PDFSignatureFactory::sign(certificate, password, data, signature);
        }), refused), PDFDocumentSigner::Result::PasswordProtected);
        QVERIFY(refused.isEmpty());
    }
}

void SignatureBuilderTest::contentsAlwaysHexadecimal()
{
    // B121, ISO 32000-2 Table 255: /Contents of a signature dictionary is a hexadecimal string; a
    // value without a special byte was written as a literal string, which the signer did not find.
    // Another /Contents (the text of an annotation) stays a literal string.
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
    const PDFObjectReference signature = builder.createSignatureDictionary("Adobe.PPKLite", "adbe.pkcs7.detached", "short", QDateTime::currentDateTimeUtc(), 0);
    builder.createSignatureField("Signature", signature, page);
    builder.addObject(dictionary({ { "Type", PDFObject::createName("Annot") }, { "Subtype", PDFObject::createName("Text") },
                                   { "Contents", PDFObject::createString("note") } }));
    const QByteArray data = writeDocument(builder.build());
    QVERIFY(!data.isEmpty());

    QVERIFY(data.contains("/Contents <73686f7274>"));
    QVERIFY(!data.contains("(short)"));
    QVERIFY(data.contains("/Contents (note)"));

    // The value read back is the same
    const PDFDocument document = readDocument(data);
    QCOMPARE(document.getObjectByReference(signature).getDictionary()->get("Contents").getString(), QByteArray("short"));
}

void SignatureBuilderTest::byteRangeOfWrittenSignature()
{
    // ISO 32000-2, 12.8.1: /ByteRange covers the whole file except the hexadecimal string of
    // /Contents with its angle brackets - [0 a b c], a = the position of '<', b = after '>',
    // b + c = the end of the file. B122: the signer writes the marks of this very dictionary - a
    // reason with the text of the marks and a field name of the same digits change nothing.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const QByteArray original = writeDocument(builder.build());
    const PDFDocument document = readDocument(original);

    PDFDocumentSigner::Parameters parameters = createParameters(document, original, [&](const QByteArray& data, QByteArray& signature)
    {
        return PDFSignatureFactory::sign(certificate, password, data, signature);
    });
    parameters.createSignatureFieldFunction = [&document](PDFDocumentBuilder& signedBuilder, PDFObjectReference signatureDictionary)
    {
        signedBuilder.setSignatureReason(signatureDictionary, QStringLiteral("123456789123 123456789123"));
        return signedBuilder.createSignatureField("123456789123", signatureDictionary, document.getCatalog()->getPage(0)->getPageReference());
    };

    QByteArray signedData;
    QCOMPARE(PDFDocumentSigner::sign(parameters, signedData), PDFDocumentSigner::Result::OK);

    // The values of the file computed by the test of its text
    const qsizetype byteRangeKey = signedData.lastIndexOf("/ByteRange [");
    QVERIFY(byteRangeKey > original.size());
    const qsizetype byteRangeEnd = signedData.indexOf(']', byteRangeKey);
    const QList<QByteArray> numbers = signedData.mid(byteRangeKey + 12, byteRangeEnd - byteRangeKey - 12).simplified().split(' ');
    QCOMPARE(numbers.size(), 4);

    const qsizetype contentsKey = signedData.indexOf("/Contents <", byteRangeKey);
    QVERIFY(contentsKey > byteRangeKey);
    const qsizetype lessThan = contentsKey + 10;
    const qsizetype greaterThan = signedData.indexOf('>', lessThan);
    QCOMPARE(signedData.at(lessThan), '<');
    QCOMPARE(numbers[0].toLongLong(), 0);
    QCOMPARE(numbers[1].toLongLong(), qint64(lessThan));
    QCOMPARE(numbers[2].toLongLong(), qint64(greaterThan + 1));
    QCOMPARE(numbers[2].toLongLong() + numbers[3].toLongLong(), qint64(signedData.size()));

    // The hexadecimal string holds only hexadecimal digits
    const QByteArray hex = signedData.mid(lessThan + 1, greaterThan - lessThan - 1);
    QVERIFY(!hex.isEmpty());
    QCOMPARE(QByteArray::fromHex(hex).toHex(), hex.toLower());

    // The marks of the text are untouched
    QVERIFY(signedData.mid(original.size()).contains("123456789123 123456789123"));

    const std::vector<PDFSignatureVerificationResult> results = verifySignedDocument(signedData);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));
    QVERIFY(!results.front().hasSignatureWarning());
}

void SignatureBuilderTest::estimatedSizeWithoutTrialSignature()
{
    // B134: with a size computed by the caller no trial signature is made (it would contact the
    // timestamp authority or the smart card); a signature, which does not fit, gets one more
    // attempt at most (R6-28: at most one more contact of the authority)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const QByteArray original = writeDocument(builder.build());
    const PDFDocument document = readDocument(original);

    for (const qint64 estimate : { qint64(16384), qint64(64) })
    {
        std::vector<QByteArray> signedInputs;
        PDFDocumentSigner::Parameters parameters = createParameters(document, original, [&](const QByteArray& data, QByteArray& signature)
        {
            signedInputs.push_back(data);
            return PDFSignatureFactory::sign(certificate, password, data, signature);
        });
        parameters.estimatedSignatureSize = estimate;

        QByteArray signedData;
        QCOMPARE(PDFDocumentSigner::sign(parameters, signedData), PDFDocumentSigner::Result::OK);

        // Only the bytes of the file are signed: one signature, or two, when the estimate is small
        QCOMPARE(signedInputs.size(), size_t(estimate > 1024 ? 1 : 2));
        for (const QByteArray& input : signedInputs)
        {
            QVERIFY(input.startsWith(original));
            QVERIFY(input != QByteArrayLiteral("PDF4QT trial data to be signed"));
        }

        // The reserved space of the estimate: 2 hexadecimal digits a byte
        if (estimate > 1024)
        {
            const qsizetype contents = signedData.lastIndexOf("/Contents <") + 11;
            QCOMPARE(signedData.indexOf('>', contents) - contents, qsizetype(2 * estimate));
        }
        QVERIFY(verifySignedDocument(signedData).front().isSignatureValid());
    }

    // A signature, which never fits: two attempts, then the failure
    int calls = 0;
    PDFDocumentSigner::Parameters parameters = createParameters(document, original, [&](const QByteArray& data, QByteArray& signature)
    {
        ++calls;
        PDFSignatureFactory::sign(certificate, password, data, signature);
        signature.append(QByteArray(100000 * calls, char(0)));
        return true;
    });
    parameters.estimatedSignatureSize = 4096;
    QByteArray signedData;
    QCOMPARE(PDFDocumentSigner::sign(parameters, signedData), PDFDocumentSigner::Result::SignatureTooLarge);
    QCOMPARE(calls, 2);
    QVERIFY(signedData.isEmpty());
}

void SignatureBuilderTest::certifiedWithoutChanges()
{
    // B135, R6-4 (ISO 32000-2, 12.8.2.2.1 and Table 257 /P 1: "any changes shall invalidate the
    // signature with the exception of subsequent DSS ... and/or document timestamp ... incremental
    // updates"): no signature of a certified document with no changes allowed; a document
    // timestamp only in a revision of nothing else
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const PDFObject transformParameters = dictionary({ { "Type", PDFObject::createName("TransformParams") }, { "P", PDFObject::createInteger(1) }, { "V", PDFObject::createName("1.2") } });
    const PDFObject reference = dictionary({ { "Type", PDFObject::createName("SigRef") }, { "TransformMethod", PDFObject::createName("DocMDP") }, { "TransformParams", transformParameters } });
    const PDFObjectReference certification = builder.addObject(dictionary({ { "Type", PDFObject::createName("Sig") }, { "Filter", PDFObject::createName("Adobe.PPKLite") },
                                                                            { "Reference", PDFObject::createArray(PDFArrayBuilder(std::vector<PDFObject>{ reference })) } }));
    builder.mergeTo(builder.getCatalogReference(), dictionary({ { "Perms", dictionary({ { "DocMDP", PDFObject::createReference(certification) } }) } }));
    const QByteArray original = writeDocument(builder.build());
    const PDFDocument document = readDocument(original);
    QCOMPARE(PDFDocMDPPermissions::getLevel(&document), PDFDocMDPPermissions::Level::NoChanges);

    // The signer stops before anything is signed; a timestamp of the unchanged file goes on (the
    // function of the test refuses to sign, so it ends there)
    int calls = 0;
    const PDFDocumentSigner::SignFunction refuse = [&calls](const QByteArray&, QByteArray&) { ++calls; return false; };
    QByteArray signedData;
    QCOMPARE(PDFDocumentSigner::sign(createParameters(document, original, refuse), signedData), PDFDocumentSigner::Result::CertifiedNoChanges);
    QCOMPARE(calls, 0);

    PDFDocumentSigner::Parameters timestamp = createParameters(document, original, refuse);
    timestamp.signatureDictionaryType = "DocTimeStamp";
    timestamp.subfilter = "ETSI.RFC3161";
    timestamp.estimatedSignatureSize = 4096;
    QCOMPARE(PDFDocumentSigner::sign(timestamp, signedData), PDFDocumentSigner::Result::SigningFailed);
    QCOMPARE(calls, 1);

    // An unsaved change of the tab: the timestamp is refused too
    PDFDocumentBuilder edit(&document);
    edit.addObject(PDFObject::createInteger(7654321));
    const PDFDocument edited = edit.build();
    PDFDocumentSigner::Parameters editedTimestamp = createParameters(edited, original, refuse);
    editedTimestamp.signatureDictionaryType = "DocTimeStamp";
    editedTimestamp.estimatedSignatureSize = 4096;
    QCOMPARE(PDFDocumentSigner::sign(editedTimestamp, signedData), PDFDocumentSigner::Result::CertifiedNoChanges);
    QCOMPARE(calls, 1);
    QVERIFY(signedData.isEmpty());
}

void SignatureBuilderTest::permissionsOfEncryptedDocument()
{
    // B135, ISO 32000-2 Table 22: a new signature field needs bit 4 (Modify) and bit 6 (annotations
    // and form fields); a document of an owner password without bit 6 is refused
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    for (const bool isAllowed : { false, true })
    {
        PDFDocumentBuilder builder;
        builder.appendPage(QRectF(0, 0, 300, 400));
        PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = PDFSecurityHandlerFactory::AES_256;
        settings.ownerPassword = QStringLiteral("owner");
        settings.permissions = isAllowed ? 0xFFFFFFFF : uint32_t(PDFSecurityHandler::Permission::Modify) | uint32_t(PDFSecurityHandler::Permission::PrintHighResolution);
        settings.id = builder.ensureDocumentId();
        builder.setSecurityHandler(PDFSecurityHandlerFactory::createSecurityHandler(settings));
        const QByteArray original = writeDocument(builder.build());
        const PDFDocument document = readDocument(original);
        QCOMPARE(document.getStorage().getSecurityHandler()->getAuthorizationResult(), PDFSecurityHandler::AuthorizationResult::UserAuthorized);

        PDFDocumentSigner::Parameters parameters = createParameters(document, original, [&](const QByteArray& data, QByteArray& signature)
        {
            return PDFSignatureFactory::sign(certificate, password, data, signature);
        });
        parameters.requiredPermissions = { PDFSecurityHandler::Permission::Modify, PDFSecurityHandler::Permission::ModifyInteractiveItems };

        QByteArray signedData;
        QCOMPARE(PDFDocumentSigner::sign(parameters, signedData), isAllowed ? PDFDocumentSigner::Result::OK : PDFDocumentSigner::Result::NotPermitted);
        QCOMPARE(signedData.isEmpty(), !isAllowed);
    }
}

void SignatureBuilderTest::signaturesCountedOfFile()
{
    // R6-12: the signatures are counted of the file, not of the document of the tab. An unsaved
    // change, which removed the field, and an update, which cannot be made (another object under
    // the number of an object stream), never lead to a whole write, which destroys the signature.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    const QByteArray original = createObjectStreamDocument();
    QByteArray signedData;
    QCOMPARE(signDocument(readDocument(original), original, certificate, password, "First", signedData), PDFDocumentSigner::Result::OK);
    const PDFDocument signedDocument = readDocument(signedData);
    QVERIFY(PDFDocumentSigner::hasExistingSignatures(&signedDocument));

    PDFObjectStorage storage = signedDocument.getStorage();
    const PDFObjectReference catalogReference = signedDocument.getTrailerDictionary()->get("Root").getReference();
    PDFDictionaryBuilder catalog(*storage.getObject(catalogReference).getDictionary());
    catalog.removeEntry("AcroForm");
    storage.setObject(catalogReference, PDFObject::createDictionary(std::move(catalog)));
    storage.setObject(PDFObjectReference(4, 0), PDFObject::createInteger(1));

    // The widget of the field goes too: a signature widget in /Annots of a page is a signature also
    // without /AcroForm (PDFForm::parse, ASan E2 of step 6)
    const PDFObjectReference pageReference = signedDocument.getCatalog()->getPage(0)->getPageReference();
    PDFDictionaryBuilder page(*storage.getObject(pageReference).getDictionary());
    page.removeEntry("Annots");
    storage.setObject(pageReference, PDFObject::createDictionary(std::move(page)));
    const PDFDocument withoutField(std::move(storage), signedDocument.getInfo()->version, QByteArray());
    QVERIFY(!PDFDocumentSigner::hasExistingSignatures(&withoutField));
    QVERIFY(PDFDocumentSigner::hasExistingSignatures(signedData));

    QByteArray resigned;
    QCOMPARE(signDocument(withoutField, signedData, certificate, password, "Second", resigned), PDFDocumentSigner::Result::ExistingSignaturesNotPreserved);
    QVERIFY(resigned.isEmpty());

    // A file, which cannot be read, may be signed: nothing is written over it whole
    QVERIFY(PDFDocumentSigner::hasExistingSignatures(QByteArray("not a document")));
    QVERIFY(!PDFDocumentSigner::hasExistingSignatures(original));
    QVERIFY(!PDFDocumentSigner::hasExistingSignatures(QByteArray()));
}

void SignatureBuilderTest::unsavedChangesOfSignedFile()
{
    // R6-4: the unsaved changes of a signed file go into their own revision before the revision of
    // the new signature (a verifier assigns every change to its revision, 12.8.2.2.1); an unsigned
    // file gets the changes and the signature in one revision (Q6.17 a)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const QByteArray unsignedData = writeDocument(builder.build());
    QByteArray signedData;
    QCOMPARE(signDocument(readDocument(unsignedData), unsignedData, certificate, password, "First", signedData), PDFDocumentSigner::Result::OK);

    for (const bool isSigned : { false, true })
    {
        const QByteArray& file = isSigned ? signedData : unsignedData;
        const PDFDocument document = readDocument(file);
        PDFDocumentBuilder edit(&document);
        edit.addObject(PDFObject::createInteger(7654321));
        const PDFDocument edited = edit.build();

        QByteArray result;
        PDFDocumentSigner::Details details;
        PDFDocumentSigner::Parameters parameters = createParameters(edited, file, [&](const QByteArray& data, QByteArray& signature)
        {
            return PDFSignatureFactory::sign(certificate, password, data, signature);
        });
        QCOMPARE(PDFDocumentSigner::sign(parameters, result, &details), PDFDocumentSigner::Result::OK);
        QVERIFY(details.isIncrementalUpdate);
        QCOMPARE(details.isChangesRevisionWritten, isSigned);
        QVERIFY(result.startsWith(file));

        // The revisions after the file: each ends by %%EOF
        const QByteArray update = result.mid(file.size());
        QCOMPARE(update.count("%%EOF"), isSigned ? 2 : 1);
        const qsizetype firstEnd = update.indexOf("%%EOF");
        QVERIFY(update.indexOf("7654321") < firstEnd);
        QCOMPARE(update.indexOf("/ByteRange") > firstEnd, isSigned);

        const std::vector<PDFSignatureVerificationResult> results = verifySignedDocument(result);
        QCOMPARE(results.size(), size_t(isSigned ? 2 : 1));
        for (const PDFSignatureVerificationResult& verification : results)
        {
            QVERIFY2(verification.isSignatureValid(), qPrintable(verification.getErrors().join('\n')));
        }
    }
}

void SignatureBuilderTest::documentInformationUnchanged()
{
    // R6-3, R6-4: the revision of a signature carries the signature, its field and widget, /AcroForm
    // and /Annots - no change of the document information (/ModDate, /Producer), which a verifier of a
    // certification with /P 2 counts as a change of the signed document (12.8.2.2.1, Table 257)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const PDFObjectReference information = builder.addObject(dictionary({ { "Title", PDFObject::createString("Contract") }, { "Producer", PDFObject::createString("Original producer") } }));
    builder.setDocumentInfo(information);
    PDFDocument document = builder.build();
    PDFObjectStorage storage = document.getStorage();
    storage.setObject(information, dictionary({ { "Title", PDFObject::createString("Contract") }, { "Producer", PDFObject::createString("Original producer") } }));
    const QByteArray original = writeDocument(PDFDocument(std::move(storage), document.getInfo()->version, QByteArray()));
    const PDFDocument originalDocument = readDocument(original);
    const PDFObject originalInformation = originalDocument.getTrailerDictionary()->get("Info");

    QByteArray signedData;
    QCOMPARE(signDocument(originalDocument, original, certificate, password, "Signature", signedData), PDFDocumentSigner::Result::OK);

    const PDFDocument signedDocument = readDocument(signedData);
    QVERIFY(signedDocument.getTrailerDictionary()->get("Info") == originalInformation);
    const PDFDictionary* signedInformation = signedDocument.getDictionaryFromObject(signedDocument.getTrailerDictionary()->get("Info"));
    QVERIFY(signedInformation);
    QCOMPARE(signedInformation->get("Producer").getString(), QByteArray("Original producer"));
    QVERIFY(!signedInformation->hasKey("ModDate"));

    // The update writes no object of the information
    const QByteArray header = QByteArray::number(originalInformation.getReference().objectNumber) + " 0 obj";
    QVERIFY(!signedData.mid(original.size()).contains("\n" + header));
}

void SignatureBuilderTest::signatureWidgetWithoutAcroForm()
{
    // ASan E2 of step 6 (Signature/multiplesignatures.pdf: three signatures of widgets in /Annots, no
    // /AcroForm): a document without /AcroForm has no fields (ISO 32000-2, 12.7.3), but a signature
    // widget of a page still signs the bytes of the file - it is counted, verified and kept by the
    // signer (an incremental update), never overlooked
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    QByteArray firstSigned;
    QCOMPARE(signDocument(builder.build(), QByteArray(), certificate, password, "First", firstSigned), PDFDocumentSigner::Result::OK);

    // A revision, which removes /AcroForm of the catalog
    const PDFDocument signedDocument = readDocument(firstSigned);
    PDFObjectStorage storage = signedDocument.getStorage();
    const PDFObjectReference catalogReference = signedDocument.getTrailerDictionary()->get("Root").getReference();
    PDFDictionaryBuilder catalog(*storage.getObject(catalogReference).getDictionary());
    catalog.removeEntry("AcroForm");
    storage.setObject(catalogReference, PDFObject::createDictionary(std::move(catalog)));
    const PDFDocument withoutAcroForm(std::move(storage), signedDocument.getInfo()->version, QByteArray());
    QByteArray fileData;
    {
        QBuffer buffer(&fileData);
        QVERIFY(buffer.open(QBuffer::WriteOnly));
        PDFDocumentWriter writer(nullptr);
        QVERIFY(bool(writer.writeIncrementalUpdate(&buffer, firstSigned, &withoutAcroForm)));
    }

    const PDFDocument document = readDocument(fileData);
    QVERIFY(document.getCatalog()->getFormObject().isNull());
    const PDFForm form = PDFForm::parse(&document, document.getCatalog()->getFormObject());
    QCOMPARE(form.getFormType(), PDFForm::FormType::None);
    QCOMPARE(PDFForm::getSignatureCount(&document), 1);
    QVERIFY(PDFDocumentSigner::hasExistingSignatures(&document));

    const std::vector<PDFSignatureVerificationResult> results = verifySignedDocument(fileData);
    QCOMPARE(results.size(), size_t(1));
    QCOMPARE(results.front().getSignatureFieldQualifiedName(), QStringLiteral("First"));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));

    // A second signature is an update of the file, the first one is kept
    QByteArray secondSigned;
    QCOMPARE(signDocument(document, fileData, certificate, password, "Second", secondSigned), PDFDocumentSigner::Result::OK);
    QVERIFY(secondSigned.startsWith(fileData));
    QCOMPARE(verifySignedDocument(secondSigned).size(), size_t(2));
}

void SignatureBuilderTest::signatureWidgetsWalkWithoutAcroForm()
{
    // The walk from a widget to its root field (ISO 32000-2, 12.7.4.1 /Parent; /FT inheritable, Table 226)
    // of a document without /AcroForm: a root shared by two widgets is one field, a cycle of /Parent and
    // a field of another type are no signature field
    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 200 200] >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /Resources << >> /Annots [10 0 R 11 0 R 12 0 R 13 0 R] >>");
    pdf.set(10, "<< /Type /Annot /Subtype /Widget /Rect [0 0 10 10] /P 3 0 R /Parent 20 0 R >>");
    pdf.set(11, "<< /Type /Annot /Subtype /Widget /Rect [20 0 30 10] /P 3 0 R /Parent 20 0 R >>");
    pdf.set(12, "<< /Type /Annot /Subtype /Widget /Rect [40 0 50 10] /P 3 0 R /Parent 21 0 R >>");
    pdf.set(13, "<< /Type /Annot /Subtype /Widget /Rect [60 0 70 10] /P 3 0 R /FT /Tx /T (Text) /V (x) >>");
    pdf.set(20, "<< /FT /Sig /T (Shared) /V 30 0 R /Kids [10 0 R 11 0 R] >>");
    pdf.set(21, "<< /FT /Sig /T (Cycle) /V 30 0 R /Parent 22 0 R /Kids [12 0 R] >>");
    pdf.set(22, "<< /T (Loop) /Parent 21 0 R /Kids [21 0 R] >>");
    pdf.set(30, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 0 0 0] /Contents <00> >>");
    const PDFDocument document = pdf.read();

    const PDFForm form = PDFForm::parse(&document, document.getCatalog()->getFormObject());
    QCOMPARE(form.getFormType(), PDFForm::FormType::None);
    QCOMPARE(form.getFormFields().size(), size_t(1));
    QCOMPARE(form.getFormFields().front()->getName(PDFFormField::NameType::FullyQualified), QStringLiteral("Shared"));
    QVERIFY(PDFDocumentSigner::hasExistingSignatures(&document));
}

void SignatureBuilderTest::unreadableFileNeverRewritten()
{
    // RS6SEC Low of step 6: a file, which cannot be read, counts as signed (hasExistingSignatures) -
    // the document in the memory (here without any signature) never decides about a whole write over
    // it; without the file data the document is written whole
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const PDFDocument document = builder.build();
    QVERIFY(!PDFDocumentSigner::hasExistingSignatures(&document));

    QByteArray signedData;
    QCOMPARE(signDocument(document, QByteArray("not a document"), certificate, password, "First", signedData), PDFDocumentSigner::Result::ExistingSignaturesNotPreserved);
    QVERIFY(signedData.isEmpty());
    QCOMPARE(signDocument(document, QByteArray(), certificate, password, "First", signedData), PDFDocumentSigner::Result::OK);
    QCOMPARE(verifySignedDocument(signedData).size(), size_t(1));
}

QTEST_MAIN(SignatureBuilderTest)
#include "tst_signaturebuildertest.moc"
