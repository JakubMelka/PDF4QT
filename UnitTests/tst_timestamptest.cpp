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
#include "pdfcertificatestore.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentsigner.h"
#include "pdfform.h"
#include "pdfsecurityhandler.h"
#include "pdfsignaturehandler.h"

#include <QtTest>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimeZone>

#include <openssl/asn1.h>
#include <openssl/cms.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/ts.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <memory>
#include <utility>

using namespace pdf;

namespace
{

/// Minimal RFC 3161 timestamp authority listening on the loopback interface. The
/// tests use it instead of a public authority, so they do not depend on a network
/// service, and so the answers, which a correctly working authority never sends,
/// can be tested, too.
class TestTimestampAuthority
{
public:
    enum Behaviour
    {
        /// Correct timestamp response of the request
        Granted,

        /// The response created for the first request is sent again, so it does
        /// not belong to the request it answers
        Replay,

        /// The authority refuses to create the timestamp
        Rejected,

        /// Http error instead of the timestamp response
        HttpError,

        /// Data, which are not a timestamp response
        Garbage,

        /// The request is never answered
        NoAnswer
    };

    TestTimestampAuthority() = default;
    ~TestTimestampAuthority();

    TestTimestampAuthority(const TestTimestampAuthority&) = delete;
    TestTimestampAuthority& operator=(const TestTimestampAuthority&) = delete;

    /// Creates the certificate of the authority and starts listening
    bool start();

    void setBehaviour(Behaviour behaviour) { m_behaviour = behaviour; }

    QString getUrl() const;

    /// Certificate of the authority, DER encoded
    QByteArray getCertificate() const;

private:
    bool createCertificate();
    void onConnection();
    void onRequest(QTcpSocket* socket, const QByteArray& request);
    QByteArray createResponse(const QByteArray& request);
    QByteArray createRejection() const;

    QTcpServer m_server;
    Behaviour m_behaviour = Granted;
    EVP_PKEY* m_key = nullptr;
    X509* m_certificate = nullptr;
    TS_RESP_CTX* m_responseContext = nullptr;
    QByteArray m_firstResponse;
};

TestTimestampAuthority::~TestTimestampAuthority()
{
    m_server.close();

    if (m_responseContext)
    {
        TS_RESP_CTX_free(m_responseContext);
    }

    X509_free(m_certificate);
    EVP_PKEY_free(m_key);
}

bool TestTimestampAuthority::createCertificate()
{
    m_key = EVP_RSA_gen(2048);
    m_certificate = X509_new();

    if (!m_key || !m_certificate)
    {
        return false;
    }

    X509_set_version(m_certificate, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(m_certificate), 1);
    X509_gmtime_adj(X509_getm_notBefore(m_certificate), -3600);
    X509_gmtime_adj(X509_getm_notAfter(m_certificate), 365 * 24 * 3600);
    X509_set_pubkey(m_certificate, m_key);

    X509_NAME* name = X509_get_subject_name(m_certificate);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("PDF4QT test timestamp authority"), -1, -1, 0);
    X509_set_issuer_name(m_certificate, name);

    // The certificate of a timestamp authority must have exactly one extended key
    // usage, it must be the time stamping one and it must be critical (RFC 3161,
    // chapter 2.3).
    X509V3_CTX context;
    X509V3_set_ctx_nodb(&context);
    X509V3_set_ctx(&context, m_certificate, m_certificate, nullptr, nullptr, 0);

    const std::pair<int, const char*> extensions[] =
    {
        { NID_basic_constraints, "critical,CA:FALSE" },
        { NID_key_usage, "critical,digitalSignature,nonRepudiation" },
        { NID_ext_key_usage, "critical,timeStamping" }
    };

    for (const auto& extension : extensions)
    {
        X509_EXTENSION* object = X509V3_EXT_conf_nid(nullptr, &context, extension.first, extension.second);
        if (!object)
        {
            return false;
        }

        X509_add_ext(m_certificate, object, -1);
        X509_EXTENSION_free(object);
    }

    return X509_sign(m_certificate, m_key, EVP_sha256()) > 0;
}

bool TestTimestampAuthority::start()
{
    if (!createCertificate())
    {
        return false;
    }

    m_responseContext = TS_RESP_CTX_new();
    if (!m_responseContext)
    {
        return false;
    }

    ASN1_OBJECT* policy = OBJ_txt2obj("1.3.6.1.4.1.13762.3", 0);
    const bool isContextInitialized = TS_RESP_CTX_set_signer_cert(m_responseContext, m_certificate) == 1 &&
                                      TS_RESP_CTX_set_signer_key(m_responseContext, m_key) == 1 &&
                                      TS_RESP_CTX_set_signer_digest(m_responseContext, EVP_sha256()) == 1 &&
                                      policy && TS_RESP_CTX_set_def_policy(m_responseContext, policy) == 1 &&
                                      TS_RESP_CTX_add_md(m_responseContext, EVP_sha256()) == 1;
    ASN1_OBJECT_free(policy);

    if (!isContextInitialized)
    {
        return false;
    }

    QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() { onConnection(); });
    return m_server.listen(QHostAddress::LocalHost);
}

QString TestTimestampAuthority::getUrl() const
{
    return QString("http://127.0.0.1:%1/tsa").arg(m_server.serverPort());
}

QByteArray TestTimestampAuthority::getCertificate() const
{
    unsigned char* buffer = nullptr;
    const int size = i2d_X509(m_certificate, &buffer);
    if (size <= 0)
    {
        return QByteArray();
    }

    QByteArray result(reinterpret_cast<const char*>(buffer), size);
    OPENSSL_free(buffer);
    return result;
}

void TestTimestampAuthority::onConnection()
{
    while (QTcpSocket* socket = m_server.nextPendingConnection())
    {
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]()
        {
            buffer->append(socket->readAll());

            const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd == -1)
            {
                return;
            }

            qsizetype contentLength = 0;
            const QByteArray header = buffer->left(headerEnd).toLower();
            const qsizetype lengthIndex = header.indexOf("content-length:");
            if (lengthIndex != -1)
            {
                const qsizetype valueIndex = lengthIndex + 15;
                qsizetype endIndex = header.indexOf('\r', valueIndex);
                if (endIndex == -1)
                {
                    endIndex = header.size();
                }
                contentLength = header.mid(valueIndex, endIndex - valueIndex).trimmed().toLongLong();
            }

            if (buffer->size() < headerEnd + 4 + contentLength)
            {
                return;
            }

            onRequest(socket, buffer->mid(headerEnd + 4, contentLength));
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void TestTimestampAuthority::onRequest(QTcpSocket* socket, const QByteArray& request)
{
    if (m_behaviour == NoAnswer)
    {
        return;
    }

    QByteArray body;
    QByteArray status = "200 OK";
    QByteArray contentType = "application/timestamp-reply";

    switch (m_behaviour)
    {
        case Granted:
            body = createResponse(request);
            break;

        case Replay:
            if (m_firstResponse.isEmpty())
            {
                m_firstResponse = createResponse(request);
            }
            body = m_firstResponse;
            break;

        case Rejected:
            body = createRejection();
            break;

        case HttpError:
            status = "500 Internal Server Error";
            contentType = "text/plain";
            body = "timestamp authority is not available";
            break;

        case Garbage:
            body = "this is not a timestamp response";
            break;

        default:
            break;
    }

    QByteArray response = "HTTP/1.1 " + status + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

QByteArray TestTimestampAuthority::createResponse(const QByteArray& request)
{
    BIO* requestBuffer = BIO_new_mem_buf(request.constData(), int(request.size()));
    if (!requestBuffer)
    {
        return QByteArray();
    }

    TS_RESP* response = TS_RESP_create_response(m_responseContext, requestBuffer);
    BIO_free(requestBuffer);

    if (!response)
    {
        return QByteArray();
    }

    QByteArray result;
    if (BIO* outputBuffer = BIO_new(BIO_s_mem()))
    {
        if (i2d_TS_RESP_bio(outputBuffer, response) == 1)
        {
            BUF_MEM* memoryBuffer = nullptr;
            BIO_get_mem_ptr(outputBuffer, &memoryBuffer);
            result = QByteArray(memoryBuffer->data, int(memoryBuffer->length));
        }

        BIO_free_all(outputBuffer);
    }

    TS_RESP_free(response);
    return result;
}

QByteArray TestTimestampAuthority::createRejection() const
{
    TS_RESP* response = TS_RESP_new();
    TS_STATUS_INFO* statusInfo = TS_STATUS_INFO_new();

    QByteArray result;
    if (response && statusInfo && TS_STATUS_INFO_set_status(statusInfo, TS_STATUS_REJECTION) == 1 &&
        TS_RESP_set_status_info(response, statusInfo) == 1)
    {
        if (BIO* outputBuffer = BIO_new(BIO_s_mem()))
        {
            if (i2d_TS_RESP_bio(outputBuffer, response) == 1)
            {
                BUF_MEM* memoryBuffer = nullptr;
                BIO_get_mem_ptr(outputBuffer, &memoryBuffer);
                result = QByteArray(memoryBuffer->data, int(memoryBuffer->length));
            }

            BIO_free_all(outputBuffer);
        }
    }

    TS_STATUS_INFO_free(statusInfo);
    TS_RESP_free(response);
    return result;
}

}   // namespace

/// Tests of the RFC 3161 timestamps - the timestamp of a digital signature and
/// the document timestamp. A timestamp authority of the test, running on the
/// loopback interface, is used, so the tests need no network service and the
/// failures of an authority can be tested, too. The test against a public
/// authority is run only when the environment variable
/// PDF4QT_TIMESTAMP_TEST_URL is set to its address.
class TimestampTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void emptyUrlIsRejected();
    void invalidUrlIsRejected_data();
    void invalidUrlIsRejected();
    void unreachableAuthorityIsReported();
    void authorityFailureIsReported_data();
    void authorityFailureIsReported();
    void replayedTokenIsRejected();

    void timestampTokenMatchesTimestampedData();
    void signatureTimestampIsAttachedToSignature();
    void documentTimestampIsCreatedAndVerified();
    void documentTimestampOfEncryptedDocument();
    void signatureWithTimestampIsCreatedAndVerified();
    void timestampOfUntrustedAuthorityIsNotUsed();
    void timestampOfOtherDataIsNotUsed();

    void publicTimestampAuthority();

private:
    static bool createTestCertificate(const QTemporaryDir& directory, PDFCertificateEntry& certificate, QString& password);

    /// Settings of the timestamp authority of the test
    PDFSignatureFactory::TimestampSettings getTimestampSettings(int timeoutMilliseconds = 15000) const;

    static std::vector<PDFSignatureVerificationResult> verifySignedDocument(const QByteArray& signedDocument,
                                                                            const QByteArray& trustedCertificate = QByteArray());

    /// Returns the timestamp token stored in the unsigned attributes of the
    /// first signer of the signature, or an empty array when there is none
    static QByteArray getSignatureTimestampToken(const QByteArray& signature);

    /// Returns the value signed by the first signer of the signature
    static QByteArray getSignatureValue(const QByteArray& signature);

    /// Stores the token in the unsigned attributes of the first signer of the
    /// signature, whatever data the token timestamps
    static QByteArray setSignatureTimestampToken(const QByteArray& signature, const QByteArray& token);

    /// Signs the document by an invisible signature
    static PDFDocumentSigner::Result signDocument(const PDFDocument& document,
                                                  PDFObjectReference page,
                                                  const PDFDocumentSigner::SignFunction& signFunction,
                                                  bool isDocumentTimestamp,
                                                  QByteArray& signedDocument);

    /// Returns the signature of the only signature field of the document
    static PDFSignature getSignature(const PDFDocument& document);

    /// Returns the bytes of the document covered by the signature
    static QByteArray getSignedData(const PDFSignature& signature, const QByteArray& signedDocument);

    TestTimestampAuthority m_authority;
};

PDFSignatureFactory::TimestampSettings TimestampTest::getTimestampSettings(int timeoutMilliseconds) const
{
    PDFSignatureFactory::TimestampSettings settings;
    settings.url = m_authority.getUrl();
    settings.timeoutMilliseconds = timeoutMilliseconds;
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

std::vector<PDFSignatureVerificationResult> TimestampTest::verifySignedDocument(const QByteArray& signedDocument,
                                                                                const QByteArray& trustedCertificate)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(signedDocument);

    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        return { };
    }

    PDFCertificateStore store;
    if (!trustedCertificate.isEmpty())
    {
        store.add(PDFCertificateEntry::EntryType::User, trustedCertificate);
    }

    const PDFForm form = PDFForm::parse(&document, document.getCatalog()->getFormObject());
    PDFSignatureHandler::Parameters parameters;
    parameters.store = &store;
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

QByteArray TimestampTest::setSignatureTimestampToken(const QByteArray& signature, const QByteArray& token)
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
        if (CMS_unsigned_add1_attr_by_NID(signerInfo, NID_id_smime_aa_timeStampToken, V_ASN1_SEQUENCE, token.constData(), int(token.size())) == 1)
        {
            if (BIO* outputBuffer = BIO_new(BIO_s_mem()))
            {
                if (i2d_CMS_bio(outputBuffer, cms) == 1)
                {
                    BUF_MEM* memoryBuffer = nullptr;
                    BIO_get_mem_ptr(outputBuffer, &memoryBuffer);
                    result = QByteArray(memoryBuffer->data, int(memoryBuffer->length));
                }

                BIO_free_all(outputBuffer);
            }
        }
    }

    CMS_ContentInfo_free(cms);
    return result;
}

PDFDocumentSigner::Result TimestampTest::signDocument(const PDFDocument& document,
                                                      PDFObjectReference page,
                                                      const PDFDocumentSigner::SignFunction& signFunction,
                                                      bool isDocumentTimestamp,
                                                      QByteArray& signedDocument)
{
    PDFDocumentSigner::Parameters parameters;
    parameters.document = &document;
    parameters.signFunction = signFunction;
    parameters.createSignatureFieldFunction = [page](PDFDocumentBuilder& builder, PDFObjectReference signatureDictionary)
    {
        return builder.createSignatureField("Signature", signatureDictionary, page);
    };

    if (isDocumentTimestamp)
    {
        parameters.subfilter = "ETSI.RFC3161";
        parameters.signatureDictionaryType = "DocTimeStamp";
    }

    return PDFDocumentSigner::sign(parameters, signedDocument);
}

PDFSignature TimestampTest::getSignature(const PDFDocument& document)
{
    PDFSignature result;
    const PDFForm form = PDFForm::parse(&document, document.getCatalog()->getFormObject());
    form.apply([&result](const PDFFormField* field)
    {
        if (const PDFFormFieldSignature* signatureField = dynamic_cast<const PDFFormFieldSignature*>(field))
        {
            result = signatureField->getSignature();
        }
    });

    return result;
}

QByteArray TimestampTest::getSignedData(const PDFSignature& signature, const QByteArray& signedDocument)
{
    QByteArray result;
    for (const PDFSignature::ByteRange& byteRange : signature.getByteRanges())
    {
        if (byteRange.offset < 0 || byteRange.size < 0 || byteRange.offset + byteRange.size > signedDocument.size())
        {
            return QByteArray();
        }

        result.append(signedDocument.constData() + byteRange.offset, byteRange.size);
    }

    return result;
}

void TimestampTest::initTestCase()
{
    QVERIFY2(m_authority.start(), "Timestamp authority of the test cannot be started.");
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

    // Jakub Melka: nothing is listening on the port, so the connection is refused
    settings.url = "http://127.0.0.1:1/tsa";
    settings.timeoutMilliseconds = 5000;

    QByteArray token;
    QString errorMessage;
    QVERIFY(!PDFSignatureFactory::createTimestampToken("data", settings, token, errorMessage));
    QVERIFY(token.isEmpty());
    QVERIFY(!errorMessage.isEmpty());
}

void TimestampTest::authorityFailureIsReported_data()
{
    QTest::addColumn<int>("behaviour");

    QTest::newRow("rejected") << int(TestTimestampAuthority::Rejected);
    QTest::newRow("http error") << int(TestTimestampAuthority::HttpError);
    QTest::newRow("garbage") << int(TestTimestampAuthority::Garbage);
    QTest::newRow("no answer") << int(TestTimestampAuthority::NoAnswer);
}

void TimestampTest::authorityFailureIsReported()
{
    QFETCH(int, behaviour);

    m_authority.setBehaviour(static_cast<TestTimestampAuthority::Behaviour>(behaviour));

    QByteArray token;
    QString errorMessage;
    const bool isCreated = PDFSignatureFactory::createTimestampToken("data", getTimestampSettings(2000), token, errorMessage);
    m_authority.setBehaviour(TestTimestampAuthority::Granted);

    QVERIFY(!isCreated);
    QVERIFY(token.isEmpty());
    QVERIFY(!errorMessage.isEmpty());
}

void TimestampTest::replayedTokenIsRejected()
{
    m_authority.setBehaviour(TestTimestampAuthority::Replay);

    QByteArray token;
    QString errorMessage;
    const bool isFirstCreated = PDFSignatureFactory::createTimestampToken("first data", getTimestampSettings(), token, errorMessage);

    // The authority answers the second request with the token of the first one,
    // so the nonce of the token does not belong to the request and the token must
    // be refused (RFC 3161, chapter 2.4.2).
    QByteArray replayedToken;
    QString replayErrorMessage;
    const bool isSecondCreated = PDFSignatureFactory::createTimestampToken("second data", getTimestampSettings(), replayedToken, replayErrorMessage);
    m_authority.setBehaviour(TestTimestampAuthority::Granted);

    QVERIFY2(isFirstCreated, qPrintable(errorMessage));
    QVERIFY(!isSecondCreated);
    QVERIFY(replayedToken.isEmpty());
    QVERIFY(!replayErrorMessage.isEmpty());
}

void TimestampTest::timestampTokenMatchesTimestampedData()
{
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
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
    PDFDocument document = builder.build();

    QString errorMessage;
    auto signFunction = [&](const QByteArray& data, QByteArray& timestamp)
    {
        return PDFSignatureFactory::createTimestampToken(data, getTimestampSettings(), timestamp, errorMessage);
    };

    QByteArray signedDocument;
    QCOMPARE(signDocument(document, page, signFunction, true, signedDocument), PDFDocumentSigner::Result::OK);
    QVERIFY(!signedDocument.isEmpty());

    // The document timestamp must be readable as a document timestamp and the
    // token must attest exactly the bytes covered by the byte ranges.
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument readDocument = reader.readFromBuffer(signedDocument);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    const PDFSignature timestamp = getSignature(readDocument);
    QCOMPARE(timestamp.getType(), PDFSignature::Type::DocTimeStamp);
    QCOMPARE(timestamp.getSubfilter(), QByteArray("ETSI.RFC3161"));

    // The time of a document timestamp is the one attested by the authority, the
    // local time of the signing is not written into its dictionary.
    QVERIFY(!timestamp.getSigningDateTime().isValid());

    const QByteArray timestampedData = getSignedData(timestamp, signedDocument);
    QVERIFY(!timestampedData.isEmpty());
    QVERIFY(PDFSignatureFactory::verifyTimestampToken(timestampedData, timestamp.getContents()));

    // A changed byte of the document must invalidate the timestamp.
    QByteArray changedData = timestampedData;
    changedData[10] = changedData[10] == 'X' ? 'Y' : 'X';
    QVERIFY(!PDFSignatureFactory::verifyTimestampToken(changedData, timestamp.getContents()));

    // The timestamp must be valid for the verification of the whole document,
    // when the authority is trusted.
    const auto results = verifySignedDocument(signedDocument, m_authority.getCertificate());
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));
    QVERIFY(results.front().isCertificateValid());
    QVERIFY(results.front().getTimestampDate().isValid());
}

void TimestampTest::documentTimestampOfEncryptedDocument()
{
    // The contents of a signature dictionary must not be encrypted, which holds
    // for the dictionary of a document timestamp, too - otherwise the space
    // reserved for the timestamp cannot be found in the written document.
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));

    PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = PDFSecurityHandlerFactory::AES_256;
    settings.ownerPassword = "timestamp-test-owner";
    settings.permissions = 0xFFFFFFFCu;
    builder.setSecurityHandler(PDFSecurityHandlerFactory::createSecurityHandler(settings));

    PDFDocument document = builder.build();

    QString errorMessage;
    auto signFunction = [&](const QByteArray& data, QByteArray& timestamp)
    {
        return PDFSignatureFactory::createTimestampToken(data, getTimestampSettings(), timestamp, errorMessage);
    };

    QByteArray signedDocument;
    QCOMPARE(signDocument(document, page, signFunction, true, signedDocument), PDFDocumentSigner::Result::OK);
    QVERIFY(!signedDocument.isEmpty());

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument readDocument = reader.readFromBuffer(signedDocument);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    const PDFSignature timestamp = getSignature(readDocument);
    QCOMPARE(timestamp.getType(), PDFSignature::Type::DocTimeStamp);

    const QByteArray timestampedData = getSignedData(timestamp, signedDocument);
    QVERIFY(!timestampedData.isEmpty());
    QVERIFY(PDFSignatureFactory::verifyTimestampToken(timestampedData, timestamp.getContents()));
}

void TimestampTest::signatureWithTimestampIsCreatedAndVerified()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
    PDFDocument document = builder.build();

    QString errorMessage;
    auto signFunction = [&](const QByteArray& data, QByteArray& signature)
    {
        return PDFSignatureFactory::signWithTimestamp(certificate, password, data, getTimestampSettings(), signature, errorMessage);
    };

    QByteArray signedDocument;
    QCOMPARE(signDocument(document, page, signFunction, false, signedDocument), PDFDocumentSigner::Result::OK);
    QVERIFY(!signedDocument.isEmpty());

    // Adding the timestamp must not damage the signature itself.
    const auto results = verifySignedDocument(signedDocument, m_authority.getCertificate());
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));

    // The time of the timestamp is read back, because the timestamp has been
    // verified - the authority is trusted here.
    QVERIFY(!results.front().hasFlag(PDFSignatureVerificationResult::Warning_Signature_TimestampNotVerified));
    const QDateTime timestampDate = results.front().getTimestampDate();
    QVERIFY(timestampDate.isValid());
    QVERIFY(qAbs(timestampDate.secsTo(QDateTime::currentDateTimeUtc())) < 24 * 3600);

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument readDocument = reader.readFromBuffer(signedDocument);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    const PDFSignature signature = getSignature(readDocument);
    QCOMPARE(signature.getType(), PDFSignature::Type::Sig);

    const QByteArray token = getSignatureTimestampToken(signature.getContents());
    QVERIFY(!token.isEmpty());
    QVERIFY(PDFSignatureFactory::verifyTimestampToken(getSignatureValue(signature.getContents()), token));
}

void TimestampTest::timestampOfUntrustedAuthorityIsNotUsed()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
    PDFDocument document = builder.build();

    QString errorMessage;
    auto signFunction = [&](const QByteArray& data, QByteArray& signature)
    {
        return PDFSignatureFactory::signWithTimestamp(certificate, password, data, getTimestampSettings(), signature, errorMessage);
    };

    QByteArray signedDocument;
    QCOMPARE(signDocument(document, page, signFunction, false, signedDocument), PDFDocumentSigner::Result::OK);

    // The timestamp is an unsigned attribute, so anybody can replace it. Its time
    // must not be presented, when the authority, which issued it, is not trusted.
    const auto results = verifySignedDocument(signedDocument);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY(results.front().isSignatureValid());
    QVERIFY(!results.front().getTimestampDate().isValid());
    QVERIFY(results.front().hasFlag(PDFSignatureVerificationResult::Warning_Signature_TimestampNotVerified));
}

void TimestampTest::timestampOfOtherDataIsNotUsed()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    // Timestamp of some other data, obtained from the very same authority - it
    // must not be accepted as the timestamp of the signature.
    QString errorMessage;
    QByteArray foreignToken;
    QVERIFY2(PDFSignatureFactory::createTimestampToken("data of somebody else", getTimestampSettings(), foreignToken, errorMessage),
             qPrintable(errorMessage));

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
    PDFDocument document = builder.build();

    auto signFunction = [&](const QByteArray& data, QByteArray& signature)
    {
        QByteArray plainSignature;
        if (!PDFSignatureFactory::sign(certificate, password, data, plainSignature))
        {
            return false;
        }

        signature = setSignatureTimestampToken(plainSignature, foreignToken);
        return !signature.isEmpty();
    };

    QByteArray signedDocument;
    QCOMPARE(signDocument(document, page, signFunction, false, signedDocument), PDFDocumentSigner::Result::OK);

    // The signature itself is valid, but the timestamp does not belong to it, so
    // its time must not be shown, even though the authority is trusted.
    const auto results = verifySignedDocument(signedDocument, m_authority.getCertificate());
    QCOMPARE(results.size(), size_t(1));
    QVERIFY(results.front().isSignatureValid());
    QVERIFY(!results.front().getTimestampDate().isValid());
    QVERIFY(results.front().hasFlag(PDFSignatureVerificationResult::Warning_Signature_TimestampNotVerified));
}

void TimestampTest::publicTimestampAuthority()
{
    const QString url = qEnvironmentVariable("PDF4QT_TIMESTAMP_TEST_URL");
    if (url.isEmpty())
    {
        QSKIP("Set PDF4QT_TIMESTAMP_TEST_URL to the address of a timestamp authority to run this test.");
    }

    PDFSignatureFactory::TimestampSettings settings;
    settings.url = url;
    settings.timeoutMilliseconds = 30000;

    const QByteArray data = "PDF4QT data timestamped by a public authority";

    QByteArray token;
    QString errorMessage;
    QVERIFY2(PDFSignatureFactory::createTimestampToken(data, settings, token, errorMessage), qPrintable(errorMessage));
    QVERIFY(PDFSignatureFactory::verifyTimestampToken(data, token));
    QVERIFY(!PDFSignatureFactory::verifyTimestampToken(data + " ", token));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
    PDFDocument document = builder.build();

    auto signFunction = [&](const QByteArray& dataToBeSigned, QByteArray& signature)
    {
        return PDFSignatureFactory::signWithTimestamp(certificate, password, dataToBeSigned, settings, signature, errorMessage);
    };

    QByteArray signedDocument;
    QVERIFY2(signDocument(document, page, signFunction, false, signedDocument) == PDFDocumentSigner::Result::OK, qPrintable(errorMessage));

    auto timestampFunction = [&](const QByteArray& dataToBeSigned, QByteArray& timestamp)
    {
        return PDFSignatureFactory::createTimestampToken(dataToBeSigned, settings, timestamp, errorMessage);
    };

    QByteArray timestampedDocument;
    QVERIFY2(signDocument(document, page, timestampFunction, true, timestampedDocument) == PDFDocumentSigner::Result::OK, qPrintable(errorMessage));

    const PDFSignature timestamp = getSignature(PDFDocumentReader(nullptr, nullptr, false, false).readFromBuffer(timestampedDocument));
    QCOMPARE(timestamp.getType(), PDFSignature::Type::DocTimeStamp);
    QVERIFY(PDFSignatureFactory::verifyTimestampToken(getSignedData(timestamp, timestampedDocument), timestamp.getContents()));
}

QTEST_MAIN(TimestampTest)
#include "tst_timestamptest.moc"
