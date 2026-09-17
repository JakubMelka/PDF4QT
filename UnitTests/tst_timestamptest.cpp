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

#include "pdfcertificatemanager.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentsigner.h"
#include "pdfform.h"
#include "pdfsignaturehandler.h"

#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QTimeZone>

#include <openssl/asn1.h>
#include <openssl/cms.h>
#include <openssl/objects.h>
#include <openssl/ts.h>
#include <openssl/x509.h>

using namespace pdf;

/// Tests of the RFC 3161 timestamps - both the timestamp of a digital signature
/// and the document timestamp. The tests, which need a timestamp authority, are
/// skipped when no authority can be contacted, so the test can be run offline.
/// Authority to be used can be selected by the environment variable
/// PDF4QT_TIMESTAMP_TEST_URL.
class TimestampTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void emptyUrlIsRejected();
    void invalidUrlIsRejected_data();
    void invalidUrlIsRejected();
    void unreachableAuthorityIsReported();

    void timestampTokenMatchesTimestampedData();
    void signatureTimestampIsAttachedToSignature();
    void documentTimestampIsCreatedAndVerified();
    void signatureWithTimestampIsCreatedAndVerified();

private:
    static bool createTestCertificate(const QTemporaryDir& directory, PDFCertificateEntry& certificate, QString& password);
    static PDFSignatureFactory::TimestampSettings getTimestampSettings();
    static std::vector<PDFSignatureVerificationResult> verifySignedDocument(const QByteArray& signedDocument);

    /// Returns the timestamp token stored in the unsigned attributes of the
    /// first signer of the signature, or an empty array when there is none
    static QByteArray getSignatureTimestampToken(const QByteArray& signature);

    /// Returns the value signed by the first signer of the signature
    static QByteArray getSignatureValue(const QByteArray& signature);

    /// Skips the running test, when no timestamp authority can be contacted
    void skipWhenTimestampAuthorityIsNotAvailable();

    bool m_isTimestampAuthorityAvailable = false;
    QString m_timestampAuthorityError;
};

PDFSignatureFactory::TimestampSettings TimestampTest::getTimestampSettings()
{
    PDFSignatureFactory::TimestampSettings settings;
    settings.url = qEnvironmentVariable("PDF4QT_TIMESTAMP_TEST_URL", "http://timestamp.digicert.com");
    settings.timeoutMilliseconds = 30000;
    return settings;
}

bool TimestampTest::createTestCertificate(const QTemporaryDir& directory, PDFCertificateEntry& certificate, QString& password)
{
    PDFCertificateManager::NewCertificateInfo info;
    info.fileName = directory.filePath("timestamp-test.p12");
    info.privateKeyPasword = "test-password";
    info.certCommonName = "PDF4QT timestamp regression test";
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

std::vector<PDFSignatureVerificationResult> TimestampTest::verifySignedDocument(const QByteArray& signedDocument)
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

QByteArray TimestampTest::getSignatureTimestampToken(const QByteArray& signature)
{
    const unsigned char* buffer = reinterpret_cast<const unsigned char*>(signature.constData());
    CMS_ContentInfo* cms = d2i_CMS_ContentInfo(nullptr, &buffer, signature.size());
    if (!cms)
    {
        return QByteArray();
    }

    QByteArray result;
    STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
    if (signerInfos && sk_CMS_SignerInfo_num(signerInfos) > 0)
    {
        CMS_SignerInfo* signerInfo = sk_CMS_SignerInfo_value(signerInfos, 0);
        const int index = CMS_unsigned_get_attr_by_NID(signerInfo, NID_id_smime_aa_timeStampToken, -1);
        if (index >= 0)
        {
            X509_ATTRIBUTE* attribute = CMS_unsigned_get_attr(signerInfo, index);
            ASN1_TYPE* value = attribute ? X509_ATTRIBUTE_get0_type(attribute, 0) : nullptr;
            if (value && value->type == V_ASN1_SEQUENCE)
            {
                result = QByteArray(reinterpret_cast<const char*>(ASN1_STRING_get0_data(value->value.sequence)),
                                    ASN1_STRING_length(value->value.sequence));
            }
        }
    }

    CMS_ContentInfo_free(cms);
    return result;
}

QByteArray TimestampTest::getSignatureValue(const QByteArray& signature)
{
    const unsigned char* buffer = reinterpret_cast<const unsigned char*>(signature.constData());
    CMS_ContentInfo* cms = d2i_CMS_ContentInfo(nullptr, &buffer, signature.size());
    if (!cms)
    {
        return QByteArray();
    }

    QByteArray result;
    STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
    if (signerInfos && sk_CMS_SignerInfo_num(signerInfos) > 0)
    {
        CMS_SignerInfo* signerInfo = sk_CMS_SignerInfo_value(signerInfos, 0);
        if (ASN1_OCTET_STRING* signatureValue = CMS_SignerInfo_get0_signature(signerInfo))
        {
            result = QByteArray(reinterpret_cast<const char*>(ASN1_STRING_get0_data(signatureValue)),
                                ASN1_STRING_length(signatureValue));
        }
    }

    CMS_ContentInfo_free(cms);
    return result;
}

void TimestampTest::skipWhenTimestampAuthorityIsNotAvailable()
{
    if (!m_isTimestampAuthorityAvailable)
    {
        QSKIP(qPrintable(QString("Timestamp authority '%1' is not available. %2").arg(getTimestampSettings().url, m_timestampAuthorityError)));
    }
}

void TimestampTest::initTestCase()
{
    // The timestamp authority is contacted once - the tests, which need it, are
    // skipped when it does not answer, so the test suite can be run offline.
    QByteArray token;
    m_isTimestampAuthorityAvailable = PDFSignatureFactory::createTimestampToken("PDF4QT timestamp availability probe",
                                                                               getTimestampSettings(),
                                                                               token,
                                                                               m_timestampAuthorityError);
}

void TimestampTest::emptyUrlIsRejected()
{
    PDFSignatureFactory::TimestampSettings settings;
    settings.url = QString();

    QByteArray token;
    QString errorMessage;
    QVERIFY(!PDFSignatureFactory::createTimestampToken("data", settings, token, errorMessage));
    QVERIFY(token.isEmpty());
    QVERIFY(!errorMessage.isEmpty());
}

void TimestampTest::invalidUrlIsRejected_data()
{
    QTest::addColumn<QString>("url");

    QTest::newRow("no scheme") << QString("timestamp.digicert.com");
    QTest::newRow("only scheme") << QString("http://");
    QTest::newRow("spaces") << QString("   ");
}

void TimestampTest::invalidUrlIsRejected()
{
    QFETCH(QString, url);

    PDFSignatureFactory::TimestampSettings settings;
    settings.url = url;
    settings.timeoutMilliseconds = 5000;

    QByteArray token;
    QString errorMessage;
    QVERIFY(!PDFSignatureFactory::createTimestampToken("data", settings, token, errorMessage));
    QVERIFY(token.isEmpty());
    QVERIFY(!errorMessage.isEmpty());
}

void TimestampTest::unreachableAuthorityIsReported()
{
    PDFSignatureFactory::TimestampSettings settings;

    // Jakub Melka: the address is reserved for documentation (RFC 6761), so it
    // cannot be resolved to a running timestamp authority.
    settings.url = "http://timestamp.invalid/tsa";
    settings.timeoutMilliseconds = 10000;

    QByteArray token;
    QString errorMessage;
    QVERIFY(!PDFSignatureFactory::createTimestampToken("data", settings, token, errorMessage));
    QVERIFY(token.isEmpty());
    QVERIFY(!errorMessage.isEmpty());
}

void TimestampTest::timestampTokenMatchesTimestampedData()
{
    skipWhenTimestampAuthorityIsNotAvailable();

    const QByteArray data = "PDF4QT timestamped data";

    QByteArray token;
    QString errorMessage;
    QVERIFY2(PDFSignatureFactory::createTimestampToken(data, getTimestampSettings(), token, errorMessage), qPrintable(errorMessage));
    QVERIFY(!token.isEmpty());

    // The token must be a timestamp token of the given data and of nothing else.
    QVERIFY(PDFSignatureFactory::verifyTimestampToken(data, token));
    QVERIFY(!PDFSignatureFactory::verifyTimestampToken(data + " ", token));
    QVERIFY(!PDFSignatureFactory::verifyTimestampToken(QByteArray(), token));

    // The signature of the authority is verified, too - the last byte of the
    // token belongs to the signature value of its signer.
    QByteArray damagedToken = token;
    damagedToken[damagedToken.size() - 1] = char(damagedToken.back() ^ 0xFF);
    QVERIFY(!PDFSignatureFactory::verifyTimestampToken(data, damagedToken));

    // The token carries the time of the timestamp authority, which must be close
    // to the current time.
    const unsigned char* buffer = reinterpret_cast<const unsigned char*>(token.constData());
    PKCS7* tokenObject = d2i_PKCS7(nullptr, &buffer, token.size());
    QVERIFY(tokenObject);

    TS_TST_INFO* info = PKCS7_to_TS_TST_INFO(tokenObject);
    QVERIFY(info);

    const ASN1_GENERALIZEDTIME* time = TS_TST_INFO_get_time(info);
    QVERIFY(time);
    QDateTime timestampTime = QDateTime::fromString(QString::fromLatin1(reinterpret_cast<const char*>(ASN1_STRING_get0_data(time))).left(14), "yyyyMMddHHmmss");
    QVERIFY(timestampTime.isValid());
    timestampTime.setTimeZone(QTimeZone::UTC);
    QVERIFY(qAbs(timestampTime.secsTo(QDateTime::currentDateTimeUtc())) < 24 * 3600);

    TS_TST_INFO_free(info);
    PKCS7_free(tokenObject);
}

void TimestampTest::signatureTimestampIsAttachedToSignature()
{
    skipWhenTimestampAuthorityIsNotAvailable();

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    const QByteArray data = "PDF4QT data to be signed and timestamped";

    QByteArray signature;
    QVERIFY(PDFSignatureFactory::sign(certificate, password, data, signature));
    QVERIFY(getSignatureTimestampToken(signature).isEmpty());

    QByteArray timestampedSignature;
    QString errorMessage;
    QVERIFY2(PDFSignatureFactory::signWithTimestamp(certificate, password, data, getTimestampSettings(), timestampedSignature, errorMessage),
             qPrintable(errorMessage));

    // The timestamp is stored as an unsigned attribute of the signer and it must
    // be the timestamp of the value signed by that signer.
    const QByteArray token = getSignatureTimestampToken(timestampedSignature);
    QVERIFY(!token.isEmpty());

    const QByteArray signatureValue = getSignatureValue(timestampedSignature);
    QVERIFY(!signatureValue.isEmpty());
    QVERIFY(PDFSignatureFactory::verifyTimestampToken(signatureValue, token));
    QVERIFY(!PDFSignatureFactory::verifyTimestampToken(data, token));
}

void TimestampTest::documentTimestampIsCreatedAndVerified()
{
    skipWhenTimestampAuthorityIsNotAvailable();

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
    PDFDocument document = builder.build();

    QString errorMessage;
    PDFDocumentSigner::Parameters parameters;
    parameters.document = &document;
    parameters.subfilter = "ETSI.RFC3161";
    parameters.signatureDictionaryType = "DocTimeStamp";
    parameters.signFunction = [&](const QByteArray& data, QByteArray& timestamp)
    {
        return PDFSignatureFactory::createTimestampToken(data, getTimestampSettings(), timestamp, errorMessage);
    };
    parameters.createSignatureFieldFunction = [&](PDFDocumentBuilder& signedBuilder, PDFObjectReference signatureDictionary)
    {
        return signedBuilder.createSignatureField("Timestamp", signatureDictionary, page);
    };

    QByteArray signedDocument;
    QCOMPARE(PDFDocumentSigner::sign(parameters, signedDocument), PDFDocumentSigner::Result::OK);
    QVERIFY(!signedDocument.isEmpty());

    // The document timestamp must be readable as a document timestamp and the
    // token must attest exactly the bytes covered by the byte ranges.
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument readDocument = reader.readFromBuffer(signedDocument);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    const PDFForm form = PDFForm::parse(&readDocument, readDocument.getCatalog()->getFormObject());

    const PDFSignature* timestamp = nullptr;
    form.apply([&timestamp](const PDFFormField* field)
    {
        if (const PDFFormFieldSignature* signatureField = dynamic_cast<const PDFFormFieldSignature*>(field))
        {
            timestamp = &signatureField->getSignature();
        }
    });

    QVERIFY(timestamp);
    QCOMPARE(timestamp->getType(), PDFSignature::Type::DocTimeStamp);
    QCOMPARE(timestamp->getSubfilter(), QByteArray("ETSI.RFC3161"));

    // The time of a document timestamp is the one attested by the authority,
    // the local time of the signing is not written into its dictionary.
    QVERIFY(!timestamp->getSigningDateTime().isValid());

    QByteArray timestampedData;
    for (const PDFSignature::ByteRange& byteRange : timestamp->getByteRanges())
    {
        QVERIFY(byteRange.offset >= 0 && byteRange.size >= 0 && byteRange.offset + byteRange.size <= signedDocument.size());
        timestampedData.append(signedDocument.constData() + byteRange.offset, byteRange.size);
    }

    QVERIFY(PDFSignatureFactory::verifyTimestampToken(timestampedData, timestamp->getContents()));

    // A changed byte of the document must invalidate the timestamp.
    QByteArray changedData = timestampedData;
    changedData[10] = changedData[10] == 'X' ? 'Y' : 'X';
    QVERIFY(!PDFSignatureFactory::verifyTimestampToken(changedData, timestamp->getContents()));
}

void TimestampTest::signatureWithTimestampIsCreatedAndVerified()
{
    skipWhenTimestampAuthorityIsNotAvailable();

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
    PDFDocument document = builder.build();

    QString errorMessage;
    PDFDocumentSigner::Parameters parameters;
    parameters.document = &document;
    parameters.signFunction = [&](const QByteArray& data, QByteArray& signature)
    {
        return PDFSignatureFactory::signWithTimestamp(certificate, password, data, getTimestampSettings(), signature, errorMessage);
    };
    parameters.createSignatureFieldFunction = [&](PDFDocumentBuilder& signedBuilder, PDFObjectReference signatureDictionary)
    {
        return signedBuilder.createSignatureField("Signature", signatureDictionary, page);
    };

    QByteArray signedDocument;
    QCOMPARE(PDFDocumentSigner::sign(parameters, signedDocument), PDFDocumentSigner::Result::OK);
    QVERIFY(!signedDocument.isEmpty());

    // Adding the timestamp must not damage the signature itself.
    const auto results = verifySignedDocument(signedDocument);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));

    // The time of the timestamp must be read back from the signature.
    const QDateTime timestampDate = results.front().getTimestampDate();
    QVERIFY(timestampDate.isValid());
    QVERIFY(qAbs(timestampDate.secsTo(QDateTime::currentDateTimeUtc())) < 24 * 3600);

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument readDocument = reader.readFromBuffer(signedDocument);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    const PDFForm form = PDFForm::parse(&readDocument, readDocument.getCatalog()->getFormObject());

    const PDFSignature* signature = nullptr;
    form.apply([&signature](const PDFFormField* field)
    {
        if (const PDFFormFieldSignature* signatureField = dynamic_cast<const PDFFormFieldSignature*>(field))
        {
            signature = &signatureField->getSignature();
        }
    });

    QVERIFY(signature);
    QCOMPARE(signature->getType(), PDFSignature::Type::Sig);

    const QByteArray token = getSignatureTimestampToken(signature->getContents());
    QVERIFY(!token.isEmpty());
    QVERIFY(PDFSignatureFactory::verifyTimestampToken(getSignatureValue(signature->getContents()), token));
}

QTEST_MAIN(TimestampTest)
#include "tst_timestamptest.moc"
