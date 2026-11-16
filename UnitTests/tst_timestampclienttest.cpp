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

// Step 6 of phase 2, package P1b (plan 5.1 - 5.3, 6.3, 6.6, 7.2, 9): the client of an RFC 3161
// timestamp authority, the signing key (PAdES / PKCS#7 over one CMS API, Q6.1), the creation of a
// certificate with its cancellation (Q6.14, B130), the limits of a PKCS#12 (6.3), the store of the
// system through an injected store in the memory of the process (Q6.2) and the limits of the old
// trusted store (B131).
//
// Never the internet and never the store of the machine: the authority of the test is in the process
// (it answers by TS_RESP_CTX of OpenSSL, independently of the client) or behind a QTcpServer on the
// loopback; the store of the system is replaced by a store in the memory (PKCS12_NO_PERSIST_KEY). All
// certificates and keys are made at run time. The expected values come from RFC 3161, RFC 5652,
// RFC 5035, RFC 5280, ETSI EN 319 142-1 and the plan, never from a run of the code.

#include "pdfcertificatemanager.h"
#include "pdfcertificatestore.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool_encryption.h"
#include "pdfdocumenttool_signature.h"
#include "pdfdocumentwriter.h"
#include "pdfoperationcontrol.h"
#include "pdftimestampclient.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/cms.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pkcs12.h>
#include <openssl/pkcs7.h>
#include <openssl/rsa.h>
#include <openssl/ts.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace timestampclienttest
{

template<typename T>
using openssl_ptr = std::unique_ptr<T, void(*)(T*)>;

/// Multiplier of the time limits of the test (PDF4QT_TEST_TIMEOUT_FACTOR)
int getTimeoutFactor()
{
    return qBound(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"), 1000);
}

QByteArray sha256(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

template<typename T>
QByteArray encode(const T* object, int (*i2d)(const T*, unsigned char**))
{
    unsigned char* buffer = nullptr;
    const int size = object ? i2d(object, &buffer) : -1;
    QByteArray result = (size > 0) ? QByteArray(reinterpret_cast<const char*>(buffer), size) : QByteArray();
    OPENSSL_free(buffer);
    return result;
}

QByteArray getData(BIO* buffer)
{
    BUF_MEM* memory = nullptr;
    BIO_get_mem_ptr(buffer, &memory);
    return (memory && memory->length > 0) ? QByteArray(memory->data, qsizetype(memory->length)) : QByteArray();
}

/// A cancellation of a run
class TestOperationControl : public pdf::PDFOperationControl
{
public:
    virtual bool isOperationCanceled() const override { return m_isCanceled.load(); }
    void cancel() { m_isCanceled.store(true); }

private:
    std::atomic<bool> m_isCanceled{ false };
};

/// One certificate of the test: what its extensions and validity are
struct CertificateDefinition
{
    QByteArray commonName = "PDF4QT test";
    long notBeforeOffset = -3600;
    long notAfterOffset = 365 * 24 * 3600;
    std::vector<std::pair<int, QByteArray>> extensions;
};

/// Creates a self-signed certificate of the key
X509* createTestCertificate(EVP_PKEY* key, const CertificateDefinition& definition, long serialNumber)
{
    X509* certificate = X509_new();
    X509_set_version(certificate, X509_VERSION_3);
    ASN1_INTEGER_set(X509_get_serialNumber(certificate), serialNumber);
    X509_gmtime_adj(X509_getm_notBefore(certificate), definition.notBeforeOffset);
    X509_gmtime_adj(X509_getm_notAfter(certificate), definition.notAfterOffset);
    X509_set_pubkey(certificate, key);

    X509_NAME* name = X509_get_subject_name(certificate);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8, reinterpret_cast<const unsigned char*>(definition.commonName.constData()), -1, -1, 0);
    X509_set_issuer_name(certificate, name);

    X509V3_CTX context = { };
    X509V3_set_ctx_nodb(&context);
    X509V3_set_ctx(&context, certificate, certificate, nullptr, nullptr, 0);
    for (const auto& extensionDefinition : definition.extensions)
    {
        X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, &context, extensionDefinition.first, extensionDefinition.second.constData());
        if (extension)
        {
            X509_add_ext(certificate, extension, -1);
            X509_EXTENSION_free(extension);
        }
    }

    X509_sign(certificate, key, EVP_sha256());
    return certificate;
}

/// A PKCS#12 of the key and its certificate (the algorithms of OpenSSL 3: PBES2, AES-256, SHA-256 MAC)
QByteArray createPkcs12(EVP_PKEY* key, X509* certificate, STACK_OF(X509)* chain, const QByteArray& password)
{
    openssl_ptr<PKCS12> pkcs12(PKCS12_create(password.constData(), nullptr, key, certificate, chain, 0, 0, PKCS12_DEFAULT_ITER, PKCS12_DEFAULT_ITER, 0), &PKCS12_free);
    return encode<PKCS12>(pkcs12.get(), &i2d_PKCS12);
}

/// The certificate of a PKCS#12 (DER)
QByteArray getPkcs12Certificate(const QByteArray& pkcs12Data, const QByteArray& password, int* keyBits = nullptr)
{
    const unsigned char* pointer = reinterpret_cast<const unsigned char*>(pkcs12Data.constData());
    openssl_ptr<PKCS12> pkcs12(d2i_PKCS12(nullptr, &pointer, long(pkcs12Data.size())), &PKCS12_free);
    EVP_PKEY* key = nullptr;
    X509* certificate = nullptr;
    STACK_OF(X509)* chain = nullptr;
    QByteArray result;
    if (pkcs12 && PKCS12_parse(pkcs12.get(), password.constData(), &key, &certificate, &chain) == 1)
    {
        result = encode<X509>(certificate, &i2d_X509);
        if (keyBits)
        {
            *keyBits = EVP_PKEY_get_bits(key);
        }
    }
    EVP_PKEY_free(key);
    X509_free(certificate);
    sk_X509_pop_free(chain, X509_free);
    ERR_clear_error();
    return result;
}

X509* decodeCertificate(const QByteArray& data)
{
    const unsigned char* pointer = reinterpret_cast<const unsigned char*>(data.constData());
    return d2i_X509(nullptr, &pointer, long(data.size()));
}

/// Replaces the iteration count of the MAC of a PKCS#12 (the MAC itself is not computed again: the
/// client must refuse the file before it verifies the MAC)
QByteArray setMacIterations(const QByteArray& pkcs12Data, int64_t iterations)
{
    const unsigned char* pointer = reinterpret_cast<const unsigned char*>(pkcs12Data.constData());
    openssl_ptr<PKCS12> pkcs12(d2i_PKCS12(nullptr, &pointer, long(pkcs12Data.size())), &PKCS12_free);
    const ASN1_OCTET_STRING* mac = nullptr;
    const X509_ALGOR* algorithm = nullptr;
    const ASN1_OCTET_STRING* salt = nullptr;
    const ASN1_INTEGER* iterationCount = nullptr;
    PKCS12_get0_mac(&mac, &algorithm, &salt, &iterationCount, pkcs12.get());
    if (!iterationCount)
    {
        return QByteArray();
    }

    ASN1_INTEGER_set_int64(const_cast<ASN1_INTEGER*>(iterationCount), iterations);
    return encode<PKCS12>(pkcs12.get(), &i2d_PKCS12);
}

/// Replaces the PBKDF2 iteration count of the shrouded key bag of a PKCS#12 (made by PKCS12_create: the
/// key bag is in the unencrypted safe) and computes the MAC again (one iteration), so that only the
/// count of the key encryption is hostile
QByteArray setKeyBagIterations(const QByteArray& pkcs12Data, const QByteArray& password, int64_t iterations)
{
    const unsigned char* pointer = reinterpret_cast<const unsigned char*>(pkcs12Data.constData());
    openssl_ptr<PKCS12> pkcs12(d2i_PKCS12(nullptr, &pointer, long(pkcs12Data.size())), &PKCS12_free);
    STACK_OF(PKCS7)* safes = pkcs12 ? PKCS12_unpack_authsafes(pkcs12.get()) : nullptr;
    STACK_OF(PKCS7)* newSafes = sk_PKCS7_new_null();
    bool isChanged = false;

    for (int i = 0; safes && i < sk_PKCS7_num(safes); ++i)
    {
        PKCS7* safe = sk_PKCS7_value(safes, i);
        if (OBJ_obj2nid(safe->type) != NID_pkcs7_data)
        {
            sk_PKCS7_push(newSafes, PKCS7_dup(safe));
            continue;
        }

        STACK_OF(PKCS12_SAFEBAG)* bags = PKCS12_unpack_p7data(safe);
        for (int j = 0; bags && j < sk_PKCS12_SAFEBAG_num(bags); ++j)
        {
            PKCS12_SAFEBAG* bag = sk_PKCS12_SAFEBAG_value(bags, j);
            if (PKCS12_SAFEBAG_get_nid(bag) != NID_pkcs8ShroudedKeyBag)
            {
                continue;
            }

            const X509_ALGOR* algorithm = nullptr;
            X509_SIG_get0(PKCS12_SAFEBAG_get0_pkcs8(bag), &algorithm, nullptr);
            PBE2PARAM* pbe2 = static_cast<PBE2PARAM*>(ASN1_TYPE_unpack_sequence(ASN1_ITEM_rptr(PBE2PARAM), algorithm->parameter));
            PBKDF2PARAM* pbkdf2 = pbe2 ? static_cast<PBKDF2PARAM*>(ASN1_TYPE_unpack_sequence(ASN1_ITEM_rptr(PBKDF2PARAM), pbe2->keyfunc->parameter)) : nullptr;
            if (pbkdf2)
            {
                ASN1_INTEGER_set_int64(pbkdf2->iter, iterations);
                ASN1_TYPE_pack_sequence(ASN1_ITEM_rptr(PBKDF2PARAM), pbkdf2, &pbe2->keyfunc->parameter);
                ASN1_TYPE_pack_sequence(ASN1_ITEM_rptr(PBE2PARAM), pbe2, &const_cast<X509_ALGOR*>(algorithm)->parameter);
                isChanged = true;
            }
            PBKDF2PARAM_free(pbkdf2);
            PBE2PARAM_free(pbe2);
        }

        sk_PKCS7_push(newSafes, PKCS12_pack_p7data(bags));
        sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
    }

    openssl_ptr<PKCS12> newPkcs12(PKCS12_init(NID_pkcs7_data), &PKCS12_free);
    QByteArray result;
    if (isChanged && newPkcs12 && PKCS12_pack_authsafes(newPkcs12.get(), newSafes) == 1 &&
        PKCS12_set_mac(newPkcs12.get(), password.constData(), -1, nullptr, 0, 1, EVP_sha256()) == 1)
    {
        result = encode<PKCS12>(newPkcs12.get(), &i2d_PKCS12);
    }

    sk_PKCS7_pop_free(newSafes, PKCS7_free);
    sk_PKCS7_pop_free(safes, PKCS7_free);
    ERR_clear_error();
    return result;
}

/// The local timestamp authority of the test, in the process: OpenSSL makes its answers (TS_RESP_CTX),
/// independently of the client; behaviors of a wrong authority are made by changing the request it
/// answers, the signer of the token or the bytes of the answer
class TestAuthority
{
public:
    enum Behavior
    {
        Granted,
        WrongNonce,
        NoNonce,
        WrongImprint,
        OtherAlgorithm,
        Rejected,
        Garbage,
        TrailingByte,
        DamagedSignature,
        NonCriticalUsage,
        NoUsage,
        ExpiredCertificate,
        FarFuture,
        NearFuture
    };

    TestAuthority()
    {
        m_key = EVP_EC_gen("P-256");

        CertificateDefinition definition;
        definition.commonName = "PDF4QT Test Timestamp Authority";
        definition.extensions = { { NID_basic_constraints, "critical,CA:FALSE" }, { NID_key_usage, "critical,digitalSignature" }, { NID_ext_key_usage, "critical,timeStamping" } };
        m_certificate = createTestCertificate(m_key, definition, 1);

        definition.extensions = { { NID_basic_constraints, "critical,CA:FALSE" }, { NID_key_usage, "critical,digitalSignature" }, { NID_ext_key_usage, "timeStamping" } };
        m_nonCriticalCertificate = createTestCertificate(m_key, definition, 2);

        definition.extensions = { { NID_basic_constraints, "critical,CA:FALSE" }, { NID_key_usage, "critical,digitalSignature" } };
        m_noUsageCertificate = createTestCertificate(m_key, definition, 3);

        definition.extensions = { { NID_basic_constraints, "critical,CA:FALSE" }, { NID_key_usage, "critical,digitalSignature" }, { NID_ext_key_usage, "critical,timeStamping" } };
        definition.notBeforeOffset = -2 * 365 * 24 * 3600;
        definition.notAfterOffset = -24 * 3600;
        m_expiredCertificate = createTestCertificate(m_key, definition, 4);
    }

    ~TestAuthority()
    {
        X509_free(m_expiredCertificate);
        X509_free(m_noUsageCertificate);
        X509_free(m_nonCriticalCertificate);
        X509_free(m_certificate);
        EVP_PKEY_free(m_key);
    }

    TestAuthority(const TestAuthority&) = delete;
    TestAuthority& operator=(const TestAuthority&) = delete;

    void setBehavior(Behavior behavior) { QMutexLocker locker(&m_mutex); m_behavior = behavior; }

    /// The answer (DER of a TimeStampResp) of the request (DER of a TimeStampReq)
    QByteArray respond(const QByteArray& request) const
    {
        QMutexLocker locker(&m_mutex);
        ERR_clear_error();

        if (m_behavior == Garbage)
        {
            return QByteArrayLiteral("this is not a timestamp response");
        }

        if (m_behavior == Rejected)
        {
            return createRejection();
        }

        QByteArray answeredRequest = request;
        if (m_behavior == WrongNonce || m_behavior == NoNonce || m_behavior == WrongImprint || m_behavior == OtherAlgorithm)
        {
            answeredRequest = modifyRequest(request);
        }

        QByteArray response = createResponse(answeredRequest);
        switch (m_behavior)
        {
            case TrailingByte:
                response.append('\0');
                break;

            case DamagedSignature:
                // The last bytes of the answer are the signature value of the token
                response[response.size() - 1] = char(response.back() ^ 0x5A);
                break;

            case NonCriticalUsage:
                response = resignToken(response, m_nonCriticalCertificate);
                break;

            case NoUsage:
                response = resignToken(response, m_noUsageCertificate);
                break;

            case ExpiredCertificate:
                response = resignToken(response, m_expiredCertificate);
                break;

            default:
                break;
        }

        ERR_clear_error();
        return response;
    }

    QByteArray getCertificate() const { return encode<X509>(m_certificate, &i2d_X509); }

private:
    static int timeCallback(TS_RESP_CTX*, void* data, long* seconds, long* microseconds)
    {
        const long offset = *static_cast<const long*>(data);
        *seconds = long(QDateTime::currentSecsSinceEpoch()) + offset;
        *microseconds = 0;
        return 1;
    }

    QByteArray createResponse(const QByteArray& request) const
    {
        openssl_ptr<TS_RESP_CTX> context(TS_RESP_CTX_new(), &TS_RESP_CTX_free);
        openssl_ptr<ASN1_OBJECT> policy(OBJ_txt2obj("1.3.6.1.4.1.13762.3", 1), &ASN1_OBJECT_free);
        TS_RESP_CTX_set_signer_cert(context.get(), m_certificate);
        TS_RESP_CTX_set_signer_key(context.get(), m_key);
        TS_RESP_CTX_set_signer_digest(context.get(), EVP_sha256());
        TS_RESP_CTX_set_def_policy(context.get(), policy.get());
        TS_RESP_CTX_add_md(context.get(), EVP_sha256());
        TS_RESP_CTX_add_md(context.get(), EVP_sha512());

        static long farFuture = 11 * 60;
        static long nearFuture = 5 * 60;
        if (m_behavior == FarFuture)
        {
            TS_RESP_CTX_set_time_cb(context.get(), &TestAuthority::timeCallback, &farFuture);
        }
        else if (m_behavior == NearFuture)
        {
            TS_RESP_CTX_set_time_cb(context.get(), &TestAuthority::timeCallback, &nearFuture);
        }

        openssl_ptr<BIO> requestBuffer(BIO_new_mem_buf(request.constData(), int(request.size())), &BIO_free_all);
        openssl_ptr<TS_RESP> response(TS_RESP_create_response(context.get(), requestBuffer.get()), &TS_RESP_free);
        return encode<TS_RESP>(response.get(), &i2d_TS_RESP);
    }

    QByteArray createRejection() const
    {
        openssl_ptr<TS_RESP> response(TS_RESP_new(), &TS_RESP_free);
        openssl_ptr<TS_STATUS_INFO> statusInfo(TS_STATUS_INFO_new(), &TS_STATUS_INFO_free);
        TS_STATUS_INFO_set_status(statusInfo.get(), TS_STATUS_REJECTION);
        TS_RESP_set_status_info(response.get(), statusInfo.get());
        return encode<TS_RESP>(response.get(), &i2d_TS_RESP);
    }

    QByteArray modifyRequest(const QByteArray& request) const
    {
        const unsigned char* pointer = reinterpret_cast<const unsigned char*>(request.constData());
        openssl_ptr<TS_REQ> original(d2i_TS_REQ(nullptr, &pointer, long(request.size())), &TS_REQ_free);
        if (!original)
        {
            return request;
        }

        openssl_ptr<TS_REQ> modified(TS_REQ_dup(original.get()), &TS_REQ_free);
        switch (m_behavior)
        {
            case WrongNonce:
            {
                openssl_ptr<ASN1_INTEGER> nonce(ASN1_INTEGER_new(), &ASN1_INTEGER_free);
                ASN1_INTEGER_set_uint64(nonce.get(), 0x0123456789ABCDEFull);
                TS_REQ_set_nonce(modified.get(), nonce.get());
                break;
            }

            case NoNonce:
            {
                openssl_ptr<TS_REQ> withoutNonce(TS_REQ_new(), &TS_REQ_free);
                TS_REQ_set_version(withoutNonce.get(), 1);
                TS_REQ_set_msg_imprint(withoutNonce.get(), TS_REQ_get_msg_imprint(original.get()));
                TS_REQ_set_cert_req(withoutNonce.get(), 1);
                return encode<TS_REQ>(withoutNonce.get(), &i2d_TS_REQ);
            }

            case WrongImprint:
            {
                TS_MSG_IMPRINT* imprint = TS_REQ_get_msg_imprint(modified.get());
                ASN1_OCTET_STRING* digest = TS_MSG_IMPRINT_get_msg(imprint);
                QByteArray changed(reinterpret_cast<const char*>(ASN1_STRING_get0_data(digest)), ASN1_STRING_length(digest));
                changed[0] = char(changed[0] ^ 0x01);
                TS_MSG_IMPRINT_set_msg(imprint, reinterpret_cast<unsigned char*>(changed.data()), int(changed.size()));
                break;
            }

            case OtherAlgorithm:
            {
                // The same hash bytes would not fit SHA-512: the authority answers SHA-512 of 64 bytes
                openssl_ptr<TS_MSG_IMPRINT> imprint(TS_MSG_IMPRINT_new(), &TS_MSG_IMPRINT_free);
                openssl_ptr<X509_ALGOR> algorithm(X509_ALGOR_new(), &X509_ALGOR_free);
                X509_ALGOR_set0(algorithm.get(), OBJ_nid2obj(NID_sha512), V_ASN1_NULL, nullptr);
                QByteArray digest(64, char(0x11));
                TS_MSG_IMPRINT_set_algo(imprint.get(), algorithm.get());
                TS_MSG_IMPRINT_set_msg(imprint.get(), reinterpret_cast<unsigned char*>(digest.data()), int(digest.size()));
                TS_REQ_set_msg_imprint(modified.get(), imprint.get());
                break;
            }

            default:
                break;
        }

        return encode<TS_REQ>(modified.get(), &i2d_TS_REQ);
    }

    /// The same TSTInfo signed again by another certificate of the key of the authority (the token of
    /// a certificate, which OpenSSL would not take as the signer of its responder): CMS with the
    /// content type id-ct-TSTInfo and ESS signing-certificate-v2 of that certificate
    QByteArray resignToken(const QByteArray& responseData, X509* certificate) const
    {
        const unsigned char* pointer = reinterpret_cast<const unsigned char*>(responseData.constData());
        openssl_ptr<TS_RESP> response(d2i_TS_RESP(nullptr, &pointer, long(responseData.size())), &TS_RESP_free);
        const QByteArray tokenInfo = response ? encode<TS_TST_INFO>(TS_RESP_get_tst_info(response.get()), &i2d_TS_TST_INFO) : QByteArray();
        if (tokenInfo.isEmpty())
        {
            return QByteArray();
        }

        openssl_ptr<BIO> content(BIO_new_mem_buf(tokenInfo.constData(), int(tokenInfo.size())), &BIO_free_all);
        openssl_ptr<CMS_ContentInfo> cms(CMS_sign(certificate, m_key, nullptr, nullptr, CMS_BINARY | CMS_PARTIAL | CMS_CADES | CMS_NOSMIMECAP), &CMS_ContentInfo_free);
        if (!cms || CMS_set1_eContentType(cms.get(), OBJ_nid2obj(NID_id_smime_ct_TSTInfo)) != 1 || CMS_final(cms.get(), content.get(), nullptr, CMS_BINARY) != 1)
        {
            return QByteArray();
        }

        const QByteArray tokenData = encode<CMS_ContentInfo>(cms.get(), &i2d_CMS_ContentInfo);
        const unsigned char* tokenPointer = reinterpret_cast<const unsigned char*>(tokenData.constData());
        PKCS7* token = d2i_PKCS7(nullptr, &tokenPointer, long(tokenData.size()));
        const unsigned char* infoPointer = reinterpret_cast<const unsigned char*>(tokenInfo.constData());
        TS_TST_INFO* info = d2i_TS_TST_INFO(nullptr, &infoPointer, long(tokenInfo.size()));

        openssl_ptr<TS_RESP> newResponse(TS_RESP_new(), &TS_RESP_free);
        openssl_ptr<TS_STATUS_INFO> statusInfo(TS_STATUS_INFO_new(), &TS_STATUS_INFO_free);
        TS_STATUS_INFO_set_status(statusInfo.get(), TS_STATUS_GRANTED);
        TS_RESP_set_status_info(newResponse.get(), statusInfo.get());
        TS_RESP_set_tst_info(newResponse.get(), token, info);   // takes the token and the info
        return encode<TS_RESP>(newResponse.get(), &i2d_TS_RESP);
    }

    mutable QMutex m_mutex;
    Behavior m_behavior = Granted;
    EVP_PKEY* m_key = nullptr;
    X509* m_certificate = nullptr;
    X509* m_nonCriticalCertificate = nullptr;
    X509* m_noUsageCertificate = nullptr;
    X509* m_expiredCertificate = nullptr;
};

/// A transport in the process: it hands the request to the local authority and records the requests
class LocalTransport : public pdf::PDFTimestampTransport
{
public:
    explicit LocalTransport(const TestAuthority* authority) : m_authority(authority) { }

    struct Settings
    {
        Error error = Error::None;
        int httpStatus = 200;
        QByteArray contentType = "application/timestamp-reply";
    };

    virtual Response post(const Request& request, const pdf::PDFOperationControl* control) const override
    {
        Q_UNUSED(control);
        QMutexLocker locker(&m_mutex);
        m_requests.push_back(request);

        Response response;
        response.error = m_settings.error;
        response.httpStatus = (m_settings.error == Error::Redirected) ? 302 : m_settings.httpStatus;
        response.contentType = m_settings.contentType;
        if (response.error == Error::None && response.httpStatus == 200)
        {
            response.body = m_authority->respond(request.body);
        }
        if (response.error != Error::None)
        {
            response.errorMessage = QStringLiteral("transport error");
        }
        return response;
    }

    void setSettings(const Settings& settings) { QMutexLocker locker(&m_mutex); m_settings = settings; }
    std::vector<Request> getRequests() const { QMutexLocker locker(&m_mutex); return m_requests; }

private:
    const TestAuthority* m_authority;
    mutable QMutex m_mutex;
    Settings m_settings;
    mutable std::vector<Request> m_requests;
};

/// An HTTP server on the loopback (the thread of the test) for the network transport
class TestHttpServer
{
public:
    enum Behavior
    {
        Authority,          ///< The answer of the authority
        Status503,          ///< 503 Service Unavailable
        Redirect,           ///< 302 to another path of the server
        LyingLength,        ///< Content-Length above the limit, then nothing more
        NoLength,           ///< A body of 1 MiB + 1 byte without Content-Length
        ExactLimit,         ///< A body of exactly 1 MiB
        Silent              ///< Never answers
    };

    explicit TestHttpServer(const TestAuthority* authority) : m_authority(authority)
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() { onConnection(); });
        m_server.listen(QHostAddress::LocalHost);
    }

    QString getUrl(const QString& path = QStringLiteral("/tsa")) const { return QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path); }
    void setBehavior(Behavior behavior) { m_behavior = behavior; m_requests.clear(); }

    /// The received requests: the request line and the headers (lower case), the body
    struct Received
    {
        QByteArray head;
        QByteArray body;
    };

    const std::vector<Received>& getRequests() const { return m_requests; }

private:
    void onConnection()
    {
        while (QTcpSocket* socket = m_server.nextPendingConnection())
        {
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]()
            {
                buffer->append(socket->readAll());
                const qsizetype headEnd = buffer->indexOf("\r\n\r\n");
                if (headEnd < 0)
                {
                    return;
                }

                const QByteArray head = buffer->left(headEnd).toLower();
                qsizetype contentLength = 0;
                const qsizetype lengthIndex = head.indexOf("content-length:");
                if (lengthIndex >= 0)
                {
                    const qsizetype lineEnd = head.indexOf('\r', lengthIndex);
                    contentLength = head.mid(lengthIndex + 15, (lineEnd < 0 ? head.size() : lineEnd) - lengthIndex - 15).trimmed().toLongLong();
                }

                if (buffer->size() < headEnd + 4 + contentLength)
                {
                    return;
                }

                m_requests.push_back({ head, buffer->mid(headEnd + 4, contentLength) });
                answer(socket, m_requests.back().body);
                buffer->clear();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    void answer(QTcpSocket* socket, const QByteArray& body)
    {
        QByteArray response;
        switch (m_behavior)
        {
            case Authority:
            {
                const QByteArray answerBody = m_authority->respond(body);
                response = "HTTP/1.1 200 OK\r\nContent-Type: application/timestamp-reply\r\nContent-Length: " + QByteArray::number(answerBody.size()) + "\r\nConnection: close\r\n\r\n" + answerBody;
                break;
            }

            case Status503:
                response = "HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\nContent-Length: 4\r\nConnection: close\r\n\r\nbusy";
                break;

            case Redirect:
                response = "HTTP/1.1 302 Found\r\nLocation: " + getUrl(QStringLiteral("/redirected")).toLatin1() + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                break;

            case LyingLength:
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/timestamp-reply\r\nContent-Length: 1048577\r\n\r\n0123456789");
                socket->flush();
                return;

            case NoLength:
                response = "HTTP/1.1 200 OK\r\nContent-Type: application/timestamp-reply\r\nConnection: close\r\n\r\n" + QByteArray(1024 * 1024 + 1, 'x');
                break;

            case ExactLimit:
                response = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: 1048576\r\nConnection: close\r\n\r\n" + QByteArray(1024 * 1024, 'y');
                break;

            case Silent:
                return;
        }

        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }

    const TestAuthority* m_authority;
    QTcpServer m_server;
    Behavior m_behavior = Authority;
    std::vector<Received> m_requests;
};

/// The parts of a TimeStampReq of the client
struct RequestParts
{
    bool isValid = false;
    long version = 0;
    int algorithmNid = NID_undef;
    QByteArray digest;
    QByteArray nonce;
    bool isCertificateRequested = false;
};

RequestParts parseRequest(const QByteArray& data)
{
    RequestParts parts;
    const unsigned char* pointer = reinterpret_cast<const unsigned char*>(data.constData());
    openssl_ptr<TS_REQ> request(d2i_TS_REQ(nullptr, &pointer, long(data.size())), &TS_REQ_free);
    if (!request)
    {
        return parts;
    }

    TS_MSG_IMPRINT* imprint = TS_REQ_get_msg_imprint(request.get());
    const ASN1_OBJECT* algorithm = nullptr;
    X509_ALGOR_get0(&algorithm, nullptr, nullptr, TS_MSG_IMPRINT_get_algo(imprint));
    ASN1_OCTET_STRING* digest = TS_MSG_IMPRINT_get_msg(imprint);
    const ASN1_INTEGER* nonce = TS_REQ_get_nonce(request.get());

    parts.isValid = true;
    parts.version = TS_REQ_get_version(request.get());
    parts.algorithmNid = OBJ_obj2nid(algorithm);
    parts.digest = QByteArray(reinterpret_cast<const char*>(ASN1_STRING_get0_data(digest)), ASN1_STRING_length(digest));
    parts.nonce = nonce ? encode<ASN1_INTEGER>(nonce, &i2d_ASN1_INTEGER) : QByteArray();
    parts.isCertificateRequested = TS_REQ_get_cert_req(request.get()) != 0;
    return parts;
}

/// The imprint and the nonce of a token
struct TokenParts
{
    QByteArray digest;
    QByteArray nonce;
};

TokenParts parseToken(const QByteArray& tokenData)
{
    TokenParts parts;
    const unsigned char* pointer = reinterpret_cast<const unsigned char*>(tokenData.constData());
    openssl_ptr<PKCS7> token(d2i_PKCS7(nullptr, &pointer, long(tokenData.size())), &PKCS7_free);
    openssl_ptr<TS_TST_INFO> info(token ? PKCS7_to_TS_TST_INFO(token.get()) : nullptr, &TS_TST_INFO_free);
    if (info)
    {
        ASN1_OCTET_STRING* digest = TS_MSG_IMPRINT_get_msg(TS_TST_INFO_get_msg_imprint(info.get()));
        parts.digest = QByteArray(reinterpret_cast<const char*>(ASN1_STRING_get0_data(digest)), ASN1_STRING_length(digest));
        const ASN1_INTEGER* nonce = TS_TST_INFO_get_nonce(info.get());
        parts.nonce = nonce ? encode<ASN1_INTEGER>(nonce, &i2d_ASN1_INTEGER) : QByteArray();
    }
    return parts;
}

/// The value of a signed or unsigned attribute of the first signer (DER of its single value)
QByteArray getAttributeValue(CMS_SignerInfo* signerInfo, int nid, bool isSigned)
{
    const int index = isSigned ? CMS_signed_get_attr_by_NID(signerInfo, nid, -1) : CMS_unsigned_get_attr_by_NID(signerInfo, nid, -1);
    if (index < 0)
    {
        return QByteArray();
    }

    X509_ATTRIBUTE* attribute = isSigned ? CMS_signed_get_attr(signerInfo, index) : CMS_unsigned_get_attr(signerInfo, index);
    ASN1_TYPE* value = X509_ATTRIBUTE_get0_type(attribute, 0);
    return value ? encode<ASN1_TYPE>(value, &i2d_ASN1_TYPE) : QByteArray();
}

/// A signature read back
struct SignatureParts
{
    openssl_ptr<CMS_ContentInfo> cms = openssl_ptr<CMS_ContentInfo>(nullptr, &CMS_ContentInfo_free);
    CMS_SignerInfo* signerInfo = nullptr;
    int signerCount = 0;
};

SignatureParts parseSignature(const QByteArray& data)
{
    SignatureParts parts;
    const unsigned char* pointer = reinterpret_cast<const unsigned char*>(data.constData());
    parts.cms.reset(d2i_CMS_ContentInfo(nullptr, &pointer, long(data.size())));
    STACK_OF(CMS_SignerInfo)* signerInfos = parts.cms ? CMS_get0_SignerInfos(parts.cms.get()) : nullptr;
    parts.signerCount = signerInfos ? sk_CMS_SignerInfo_num(signerInfos) : 0;
    parts.signerInfo = parts.signerCount > 0 ? sk_CMS_SignerInfo_value(signerInfos, 0) : nullptr;
    return parts;
}

/// The signature verifies over the data (the signature value and the message digest; no trust)
bool verifySignature(const SignatureParts& parts, const QByteArray& data)
{
    openssl_ptr<BIO> content(BIO_new_mem_buf(data.constData(), int(data.size())), &BIO_free_all);
    const bool isValid = parts.cms && CMS_verify(parts.cms.get(), nullptr, nullptr, content.get(), nullptr, CMS_BINARY | CMS_NO_SIGNER_CERT_VERIFY) == 1;
    ERR_clear_error();
    return isValid;
}

/// DER of an OCTET STRING of the bytes (the value of message-digest, RFC 5652, 11.2)
QByteArray encodeOctetString(const QByteArray& bytes)
{
    QByteArray result;
    result.append(char(0x04));
    result.append(char(bytes.size()));
    result.append(bytes);
    return result;
}

}   // namespace timestampclienttest

using namespace timestampclienttest;

class TimestampClientTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void interface();
    void validateUrl_data();
    void validateUrl();
    void requestGranted();
    void nonceIsRandom();
    void invalidInputSendsNothing();
    void invalidAnswers_data();
    void invalidAnswers();
    void contentTypes_data();
    void contentTypes();
    void transportErrors_data();
    void transportErrors();
    void verifyToken();

    void networkGranted();
    void networkErrors_data();
    void networkErrors();
    void networkExactLimit();
    void networkTimeout();
    void networkCancel();

    void createCertificate();
    void createCertificateKeyLengths();
    void createCertificateCancel();
    void createCertificateFile();
    void unlockPassword();
    void signPades();
    void signPkcs7();
    void signWithTimestamp();
    void signTimestampFailure();
    void estimateSignatureSize();
    void unlockChecksCertificate_data();
    void unlockChecksCertificate();
    void moveOnlyKey();
    void pkcs12IterationLimit();
    void systemStore();
    void trustedStoreLimits();

    // Step 6 of phase 2, fix wave P2S6FIXCORE
    void authorityTrustFact_data();
    void authorityTrustFact();

private:
    /// Runs the network transport in a worker thread (the thread of a run) while the server answers in
    /// this one
    void postInThread(const pdf::PDFTimestampTransport::Request& request, const pdf::PDFOperationControl* control, int waitMilliseconds, pdf::PDFTimestampTransport::Response& response);

    pdf::PDFTimestampTransport::Request createRequest(const QString& url, int timeoutMilliseconds) const;

    std::unique_ptr<TestAuthority> m_authority;
    std::shared_ptr<LocalTransport> m_transport;
    std::unique_ptr<TestHttpServer> m_server;

    /// A certificate of PDFSignatureFactory::createCertificate (RSA 2048) and its password
    QByteArray m_pkcs12;
    QByteArray m_password = "test-password";
};

void TimestampClientTest::initTestCase()
{
    m_authority = std::make_unique<TestAuthority>();
    m_transport = std::make_shared<LocalTransport>(m_authority.get());
    m_server = std::make_unique<TestHttpServer>(m_authority.get());

    pdf::PDFCertificateManager::NewCertificateInfo info;
    info.privateKeyPasword = QString::fromUtf8(m_password);
    info.certCommonName = QStringLiteral("Jane Novak");
    info.certOrganization = QStringLiteral("PDF4QT Test");
    info.certEmail = QStringLiteral("jane@example.com");
    QString error;
    m_pkcs12 = pdf::PDFSignatureFactory::createCertificate(info, nullptr, &error);
    QVERIFY2(!m_pkcs12.isEmpty(), qPrintable(error));
}

void TimestampClientTest::cleanupTestCase()
{
    pdf::PDFPersonalCertificateStore::setStore(nullptr);
    m_server.reset();
}

void TimestampClientTest::interface()
{
    // Figma 434:6241 and the old signing dialog: digicert (the default), sectigo, freetsa
    const QStringList presets = pdf::PDFTimestampClient::getPresetUrls();
    QCOMPARE(presets, QStringList({ QStringLiteral("http://timestamp.digicert.com"), QStringLiteral("http://timestamp.sectigo.com"), QStringLiteral("https://freetsa.org/tsr") }));

    // R6-28: a text of the address never shows its user info
    const QString shown = pdf::PDFTimestampClient::getDisplayUrl(QUrl(QStringLiteral("https://user:secret@tsa.example.com:8443/tsr")));
    QVERIFY(!shown.contains(QStringLiteral("secret")));
    QVERIFY(!shown.contains(QStringLiteral("user")));
    QVERIFY(shown.contains(QStringLiteral("tsa.example.com")));

    // RS6SEC Low: the query and the fragment (an API key of the authority) never reach a text, the path does
    QCOMPARE(pdf::PDFTimestampClient::getDisplayUrl(QUrl(QStringLiteral("https://tsa.example.com:8443/v1/tsr?apikey=K3Y-s3cret&x=1#frag"))),
             QStringLiteral("https://tsa.example.com:8443/v1/tsr"));

    // The texts of the errors of a request name the authority by that form
    std::shared_ptr<LocalTransport> failingTransport = std::make_shared<LocalTransport>(m_authority.get());
    LocalTransport::Settings failing;
    failing.httpStatus = 500;
    failingTransport->setSettings(failing);
    const pdf::PDFTimestampClient failingClient(failingTransport);
    const pdf::PDFTimestampClient::Result failed = failingClient.request(QStringLiteral("https://tsa.example.com/tsr?apikey=K3Y-s3cret"), sha256(QByteArrayLiteral("data")), nullptr);
    QVERIFY(!failed.isGranted());
    QVERIFY2(failed.errorMessage.contains(QStringLiteral("https://tsa.example.com/tsr")), qPrintable(failed.errorMessage));
    QVERIFY2(!failed.errorMessage.contains(QStringLiteral("K3Y-s3cret")) && !failed.errorMessage.contains(QStringLiteral("apikey")), qPrintable(failed.errorMessage));

    // A client without a transport sends nothing and says so
    const pdf::PDFTimestampClient client(nullptr);
    const QByteArray digest = sha256(QByteArrayLiteral("data"));
    const pdf::PDFTimestampClient::Result result = client.request(presets.front(), digest, nullptr);
    QCOMPARE(result.status, pdf::PDFTimestampClient::Status::NoTransport);
    QVERIFY(result.token.isEmpty());
    QVERIFY(!result.errorMessage.isEmpty());
}

void TimestampClientTest::validateUrl_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<bool>("isValid");

    // S6.6: http and https only, a host, no user info, the port optional, at most 2048 characters
    QTest::newRow("digicert") << QStringLiteral("http://timestamp.digicert.com") << true;
    QTest::newRow("sectigo") << QStringLiteral("http://timestamp.sectigo.com") << true;
    QTest::newRow("freetsa") << QStringLiteral("https://freetsa.org/tsr") << true;
    QTest::newRow("port and query") << QStringLiteral("https://tsa.example.com:8443/tsr?policy=1") << true;
    QTest::newRow("upper case scheme") << QStringLiteral("HTTPS://tsa.example.com/") << true;
    QTest::newRow("loopback") << QStringLiteral("http://127.0.0.1:8080/tsa") << true;
    QTest::newRow("longest") << (QStringLiteral("https://tsa.example.com/") + QString(2048 - 24, QLatin1Char('a'))) << true;

    QTest::newRow("empty") << QString() << false;
    QTest::newRow("spaces") << QStringLiteral("   ") << false;
    QTest::newRow("leading space") << QStringLiteral(" http://tsa.example.com") << false;
    QTest::newRow("no scheme") << QStringLiteral("timestamp.digicert.com") << false;
    QTest::newRow("scheme only") << QStringLiteral("http://") << false;
    QTest::newRow("ftp") << QStringLiteral("ftp://tsa.example.com/tsr") << false;
    QTest::newRow("file") << QStringLiteral("file:///C:/tsa.der") << false;
    QTest::newRow("windows path") << QStringLiteral("C:/tsa.der") << false;
    QTest::newRow("unc path") << QStringLiteral("\\\\server\\share\\tsa") << false;
    QTest::newRow("javascript") << QStringLiteral("javascript:alert(1)") << false;
    QTest::newRow("user and password") << QStringLiteral("https://jane:secret@tsa.example.com/tsr") << false;
    QTest::newRow("user") << QStringLiteral("https://jane@tsa.example.com/tsr") << false;
    QTest::newRow("empty user info") << QStringLiteral("https://@tsa.example.com/tsr") << false;
    QTest::newRow("fragment") << QStringLiteral("https://tsa.example.com/tsr#part") << false;
    QTest::newRow("too long") << (QStringLiteral("https://tsa.example.com/") + QString(2048 - 23, QLatin1Char('a'))) << false;
}

void TimestampClientTest::validateUrl()
{
    QFETCH(QString, url);
    QFETCH(bool, isValid);

    QString errorMessage = QStringLiteral("stale");
    QCOMPARE(pdf::PDFTimestampClient::validateUrl(url, &errorMessage), isValid);
    QCOMPARE(errorMessage.isEmpty(), isValid);

    // R6-28: the text of a refusal never repeats a password or a user name of the address
    QVERIFY(!errorMessage.contains(QStringLiteral("secret")));
    QVERIFY(!errorMessage.contains(QStringLiteral("jane")));
}

void TimestampClientTest::requestGranted()
{
    m_authority->setBehavior(TestAuthority::Granted);
    m_transport->setSettings(LocalTransport::Settings());
    const size_t requestCount = m_transport->getRequests().size();

    const pdf::PDFTimestampClient client(m_transport);
    const QByteArray digest = sha256(QByteArrayLiteral("PDF4QT timestamped data"));
    const QDateTime before = QDateTime::currentDateTimeUtc();
    const pdf::PDFTimestampClient::Result result = client.request(QStringLiteral("https://tsa.example.com/tsr"), digest, nullptr);
    QVERIFY2(result.isGranted(), qPrintable(result.errorMessage));

    // One POST (no retry), the limits of 6.6 and 7.2
    const std::vector<pdf::PDFTimestampTransport::Request> requests = m_transport->getRequests();
    QCOMPARE(requests.size(), requestCount + 1);
    const pdf::PDFTimestampTransport::Request& request = requests.back();
    QCOMPARE(request.url, QUrl(QStringLiteral("https://tsa.example.com/tsr")));
    QCOMPARE(request.contentType, QByteArrayLiteral("application/timestamp-query"));
    QCOMPARE(request.maximumResponseSize, qint64(1024 * 1024));
    QCOMPARE(request.timeoutMilliseconds, 15000);

    // RFC 3161, 2.4.1: version 1, the SHA-256 imprint of the hash, a nonce, certReq
    const RequestParts parts = parseRequest(request.body);
    QVERIFY(parts.isValid);
    QCOMPARE(parts.version, 1L);
    QCOMPARE(parts.algorithmNid, NID_sha256);
    QCOMPARE(parts.digest, digest);
    QVERIFY(parts.isCertificateRequested);
    QVERIFY(!parts.nonce.isEmpty());

    // The token answers this request: its imprint and nonce
    const TokenParts tokenParts = parseToken(result.token);
    QCOMPARE(tokenParts.digest, digest);
    QCOMPARE(tokenParts.nonce, parts.nonce);

    // The facts of the result (434:9276): the name of the authority, its time in UTC
    QCOMPARE(result.authorityName, QStringLiteral("PDF4QT Test Timestamp Authority"));
    QCOMPARE(result.time.timeSpec(), Qt::UTC);
    QVERIFY(result.time >= before.addSecs(-2));
    QVERIFY(result.time <= QDateTime::currentDateTimeUtc().addSecs(2));
    QCOMPARE(result.httpStatus, 200);
    QVERIFY(result.errorMessage.isEmpty());
}

void TimestampClientTest::nonceIsRandom()
{
    // B133: the nonce is new for each request (RAND_bytes) - a replayed answer cannot match it
    m_authority->setBehavior(TestAuthority::Granted);
    m_transport->setSettings(LocalTransport::Settings());
    const pdf::PDFTimestampClient client(m_transport);
    const QByteArray digest = sha256(QByteArrayLiteral("data"));

    QSet<QByteArray> nonces;
    for (int i = 0; i < 8; ++i)
    {
        QVERIFY(client.request(QStringLiteral("http://tsa.example.com"), digest, nullptr).isGranted());
        nonces.insert(parseRequest(m_transport->getRequests().back().body).nonce);
    }
    QCOMPARE(nonces.size(), 8);
}

void TimestampClientTest::invalidInputSendsNothing()
{
    m_transport->setSettings(LocalTransport::Settings());
    const size_t requestCount = m_transport->getRequests().size();
    const pdf::PDFTimestampClient client(m_transport);
    const QByteArray digest = sha256(QByteArrayLiteral("data"));

    // S6.6: an address, which validateUrl refuses (also a stored one), is never contacted
    pdf::PDFTimestampClient::Result result = client.request(QStringLiteral("https://user:secret@tsa.example.com/"), digest, nullptr);
    QCOMPARE(result.status, pdf::PDFTimestampClient::Status::InvalidUrl);
    QVERIFY(!result.errorMessage.contains(QStringLiteral("secret")));

    // Only a SHA-256 hash is an imprint
    result = client.request(QStringLiteral("https://tsa.example.com/"), digest.left(31), nullptr);
    QCOMPARE(result.status, pdf::PDFTimestampClient::Status::InvalidDigest);

    // A stopped run sends nothing
    TestOperationControl control;
    control.cancel();
    result = client.request(QStringLiteral("https://tsa.example.com/"), digest, &control);
    QCOMPARE(result.status, pdf::PDFTimestampClient::Status::Canceled);

    QCOMPARE(m_transport->getRequests().size(), requestCount);
}

void TimestampClientTest::invalidAnswers_data()
{
    QTest::addColumn<int>("behavior");
    QTest::addColumn<int>("status");

    using Status = pdf::PDFTimestampClient::Status;

    // RFC 3161, 2.4.2: the answer belongs to the request (nonce, imprint, algorithm), its signature and
    // its signer certificate (2.3: the only and critical extended key usage timeStamping, valid) are
    // verified; the DER has exactly the length of the body; genTime at most 10 minutes ahead (5.2)
    QTest::newRow("granted") << int(TestAuthority::Granted) << int(Status::Granted);
    QTest::newRow("wrong nonce") << int(TestAuthority::WrongNonce) << int(Status::InvalidResponse);
    QTest::newRow("no nonce") << int(TestAuthority::NoNonce) << int(Status::InvalidResponse);
    QTest::newRow("wrong imprint") << int(TestAuthority::WrongImprint) << int(Status::InvalidResponse);
    QTest::newRow("other algorithm") << int(TestAuthority::OtherAlgorithm) << int(Status::InvalidResponse);
    QTest::newRow("rejected") << int(TestAuthority::Rejected) << int(Status::Rejected);
    QTest::newRow("garbage") << int(TestAuthority::Garbage) << int(Status::InvalidResponse);
    QTest::newRow("trailing byte") << int(TestAuthority::TrailingByte) << int(Status::InvalidResponse);
    QTest::newRow("damaged signature") << int(TestAuthority::DamagedSignature) << int(Status::InvalidResponse);
    QTest::newRow("usage not critical") << int(TestAuthority::NonCriticalUsage) << int(Status::InvalidResponse);
    QTest::newRow("no usage") << int(TestAuthority::NoUsage) << int(Status::InvalidResponse);
    QTest::newRow("expired authority") << int(TestAuthority::ExpiredCertificate) << int(Status::InvalidResponse);
    QTest::newRow("eleven minutes ahead") << int(TestAuthority::FarFuture) << int(Status::InvalidResponse);
    QTest::newRow("five minutes ahead") << int(TestAuthority::NearFuture) << int(Status::Granted);
}

void TimestampClientTest::invalidAnswers()
{
    QFETCH(int, behavior);
    QFETCH(int, status);

    m_authority->setBehavior(static_cast<TestAuthority::Behavior>(behavior));
    m_transport->setSettings(LocalTransport::Settings());
    const pdf::PDFTimestampClient client(m_transport);
    const pdf::PDFTimestampClient::Result result = client.request(QStringLiteral("http://tsa.example.com/tsr"), sha256(QByteArrayLiteral("data")), nullptr);
    m_authority->setBehavior(TestAuthority::Granted);

    QCOMPARE(int(result.status), status);
    QCOMPARE(result.token.isEmpty(), status != int(pdf::PDFTimestampClient::Status::Granted));
    QCOMPARE(result.errorMessage.isEmpty(), status == int(pdf::PDFTimestampClient::Status::Granted));
}

void TimestampClientTest::contentTypes_data()
{
    QTest::addColumn<QByteArray>("contentType");
    QTest::addColumn<bool>("isGranted");

    // RFC 3161, 3.4 application/timestamp-reply; the older application/timestamp-response tolerated (6.6)
    QTest::newRow("reply") << QByteArrayLiteral("application/timestamp-reply") << true;
    QTest::newRow("response") << QByteArrayLiteral("application/timestamp-response") << true;
    QTest::newRow("parameter") << QByteArrayLiteral("Application/Timestamp-Reply; charset=binary") << true;
    QTest::newRow("text") << QByteArrayLiteral("text/html") << false;
    QTest::newRow("octets") << QByteArrayLiteral("application/octet-stream") << false;
    QTest::newRow("none") << QByteArray() << false;
}

void TimestampClientTest::contentTypes()
{
    QFETCH(QByteArray, contentType);
    QFETCH(bool, isGranted);

    m_authority->setBehavior(TestAuthority::Granted);
    LocalTransport::Settings settings;
    settings.contentType = contentType;
    m_transport->setSettings(settings);

    const pdf::PDFTimestampClient client(m_transport);
    const pdf::PDFTimestampClient::Result result = client.request(QStringLiteral("http://tsa.example.com/tsr"), sha256(QByteArrayLiteral("data")), nullptr);
    m_transport->setSettings(LocalTransport::Settings());

    QCOMPARE(result.isGranted(), isGranted);
    if (!isGranted)
    {
        QCOMPARE(result.status, pdf::PDFTimestampClient::Status::InvalidResponse);
    }
}

void TimestampClientTest::transportErrors_data()
{
    QTest::addColumn<int>("error");
    QTest::addColumn<int>("httpStatus");
    QTest::addColumn<int>("status");
    QTest::addColumn<QString>("text");

    using Error = pdf::PDFTimestampTransport::Error;
    using Status = pdf::PDFTimestampClient::Status;

    // 434:8518 (15 s), 435:8117 (HTTP 503), the limits of 6.6
    QTest::newRow("503") << int(Error::None) << 503 << int(Status::HttpError) << QStringLiteral("answered with the http status code 503");
    QTest::newRow("404") << int(Error::None) << 404 << int(Status::HttpError) << QStringLiteral("404");
    QTest::newRow("no network") << int(Error::NoNetwork) << 0 << int(Status::NoNetwork) << QStringLiteral("cannot be contacted");
    QTest::newRow("tls") << int(Error::Tls) << 0 << int(Status::NoNetwork) << QStringLiteral("cannot be contacted");
    QTest::newRow("timeout") << int(Error::Timeout) << 0 << int(Status::Timeout) << QStringLiteral("did not answer within 15");
    QTest::newRow("too large") << int(Error::TooLarge) << 0 << int(Status::InvalidResponse) << QStringLiteral("larger than 1048576 bytes");
    QTest::newRow("redirected") << int(Error::Redirected) << 0 << int(Status::HttpError) << QStringLiteral("not followed");
    QTest::newRow("canceled") << int(Error::Canceled) << 0 << int(Status::Canceled) << QStringLiteral("canceled");
}

void TimestampClientTest::transportErrors()
{
    QFETCH(int, error);
    QFETCH(int, httpStatus);
    QFETCH(int, status);
    QFETCH(QString, text);

    m_authority->setBehavior(TestAuthority::Granted);
    LocalTransport::Settings settings;
    settings.error = static_cast<pdf::PDFTimestampTransport::Error>(error);
    settings.httpStatus = httpStatus;
    m_transport->setSettings(settings);
    const size_t requestCount = m_transport->getRequests().size();

    const pdf::PDFTimestampClient client(m_transport);
    const pdf::PDFTimestampClient::Result result = client.request(QStringLiteral("http://timestamp.digicert.com"), sha256(QByteArrayLiteral("data")), nullptr);
    m_transport->setSettings(LocalTransport::Settings());

    QCOMPARE(int(result.status), status);
    QVERIFY(result.token.isEmpty());
    QVERIFY2(result.errorMessage.contains(text), qPrintable(result.errorMessage));

    // At most one request (no retry, R6-28)
    QCOMPARE(m_transport->getRequests().size(), requestCount + 1);
}

void TimestampClientTest::verifyToken()
{
    // The verification of a token over data (moved from PDFSignatureFactory::verifyTimestampToken)
    m_authority->setBehavior(TestAuthority::Granted);
    m_transport->setSettings(LocalTransport::Settings());
    const pdf::PDFTimestampClient client(m_transport);
    const QByteArray data = QByteArrayLiteral("PDF4QT timestamped data");
    const pdf::PDFTimestampClient::Result result = client.request(QStringLiteral("http://tsa.example.com"), sha256(data), nullptr);
    QVERIFY(result.isGranted());

    QVERIFY(pdf::PDFTimestampClient::verifyToken(data, result.token));
    QVERIFY(pdf::PDFSignatureFactory::verifyTimestampToken(data, result.token));
    QVERIFY(!pdf::PDFTimestampClient::verifyToken(data + " ", result.token));
    QByteArray damaged = result.token;
    damaged[damaged.size() - 1] = char(damaged.back() ^ 0xFF);
    QVERIFY(!pdf::PDFTimestampClient::verifyToken(data, damaged));
}

pdf::PDFTimestampTransport::Request TimestampClientTest::createRequest(const QString& url, int timeoutMilliseconds) const
{
    pdf::PDFTimestampTransport::Request request;
    request.url = QUrl(url);
    request.body = QByteArrayLiteral("request");
    request.maximumResponseSize = pdf::PDFTimestampClient::MAXIMUM_RESPONSE_SIZE;
    request.timeoutMilliseconds = timeoutMilliseconds;
    return request;
}

void TimestampClientTest::postInThread(const pdf::PDFTimestampTransport::Request& request, const pdf::PDFOperationControl* control, int waitMilliseconds, pdf::PDFTimestampTransport::Response& response)
{
    std::shared_ptr<const pdf::PDFTimestampTransport> transport = pdf::PDFTimestampClient::createNetworkTransport();
    std::atomic<bool> isFinished{ false };
    std::unique_ptr<QThread> thread(QThread::create([&]()
    {
        response = transport->post(request, control);
        isFinished.store(true);
    }));
    thread->start();

    // The server of this thread answers while the worker waits; the thread is always joined
    const bool isInTime = QTest::qWaitFor([&isFinished]() { return isFinished.load(); }, waitMilliseconds);
    thread->wait();
    QVERIFY2(isInTime, "The network transport did not return in time.");
}

void TimestampClientTest::networkGranted()
{
    // One real exchange over the loopback (plan 9), the client in the thread of a run
    m_authority->setBehavior(TestAuthority::Granted);
    m_server->setBehavior(TestHttpServer::Authority);

    const std::shared_ptr<const pdf::PDFTimestampClient> client = pdf::PDFTimestampClient::createNetworkClient();
    const QByteArray digest = sha256(QByteArrayLiteral("data over the network"));
    pdf::PDFTimestampClient::Result result;
    std::atomic<bool> isFinished{ false };
    std::unique_ptr<QThread> thread(QThread::create([&]()
    {
        result = client->request(m_server->getUrl(), digest, nullptr);
        isFinished.store(true);
    }));
    thread->start();
    const bool isInTime = QTest::qWaitFor([&isFinished]() { return isFinished.load(); }, 20000 * getTimeoutFactor());
    thread->wait();

    QVERIFY(isInTime);
    QVERIFY2(result.isGranted(), qPrintable(result.errorMessage));
    QCOMPARE(parseToken(result.token).digest, digest);

    // S6.6: one POST of the TimeStampReq; User-Agent "PDF4QT" only; no cookie, no authorization
    QCOMPARE(m_server->getRequests().size(), size_t(1));
    const TestHttpServer::Received& received = m_server->getRequests().front();
    QVERIFY(received.head.startsWith("post /tsa http/1.1"));
    QVERIFY(received.head.contains("content-type: application/timestamp-query"));
    QVERIFY(received.head.contains("user-agent: pdf4qt\r\n") || received.head.endsWith("user-agent: pdf4qt"));
    QVERIFY(!received.head.contains("cookie:"));
    QVERIFY(!received.head.contains("authorization:"));
    QCOMPARE(parseRequest(received.body).digest, digest);
}

void TimestampClientTest::networkErrors_data()
{
    QTest::addColumn<int>("behavior");
    QTest::addColumn<int>("error");
    QTest::addColumn<int>("httpStatus");
    QTest::addColumn<int>("requestCount");

    using Error = pdf::PDFTimestampTransport::Error;

    // 6.6: only HTTP 200 is an answer; no redirection is followed (one request); the body at most 1 MiB,
    // aborted by Content-Length (the body never comes) or by the received bytes
    QTest::newRow("503") << int(TestHttpServer::Status503) << int(Error::None) << 503 << 1;
    QTest::newRow("redirect") << int(TestHttpServer::Redirect) << int(Error::Redirected) << 302 << 1;
    QTest::newRow("lying length") << int(TestHttpServer::LyingLength) << int(Error::TooLarge) << 200 << 1;
    QTest::newRow("no length") << int(TestHttpServer::NoLength) << int(Error::TooLarge) << 200 << 1;
}

void TimestampClientTest::networkErrors()
{
    QFETCH(int, behavior);
    QFETCH(int, error);
    QFETCH(int, httpStatus);
    QFETCH(int, requestCount);

    m_server->setBehavior(static_cast<TestHttpServer::Behavior>(behavior));

    // The time limit is far above the time of the case: an abort by the limit of the size must not wait
    const int timeout = 60000 * getTimeoutFactor();
    QElapsedTimer timer;
    timer.start();
    pdf::PDFTimestampTransport::Response response;
    postInThread(createRequest(m_server->getUrl(), timeout), nullptr, timeout, response);
    if (QTest::currentTestFailed())
    {
        return;
    }

    QCOMPARE(int(response.error), error);
    QCOMPARE(response.httpStatus, httpStatus);
    QVERIFY(response.body.size() <= pdf::PDFTimestampClient::MAXIMUM_RESPONSE_SIZE);
    QVERIFY(timer.elapsed() < 20000 * getTimeoutFactor());

    // A redirection is not followed: the server saw only the first request
    QTRY_COMPARE_WITH_TIMEOUT(int(m_server->getRequests().size()), requestCount, 1000);
    QTest::qWait(200);
    QCOMPARE(int(m_server->getRequests().size()), requestCount);
}

void TimestampClientTest::networkExactLimit()
{
    // 7.2: exactly 1 MiB is still an answer (its content type is checked by the client, not here)
    m_server->setBehavior(TestHttpServer::ExactLimit);
    const int timeout = 60000 * getTimeoutFactor();
    pdf::PDFTimestampTransport::Response response;
    postInThread(createRequest(m_server->getUrl(), timeout), nullptr, timeout, response);
    if (QTest::currentTestFailed())
    {
        return;
    }

    QCOMPARE(response.error, pdf::PDFTimestampTransport::Error::None);
    QCOMPARE(response.httpStatus, 200);
    QCOMPARE(response.body.size(), qsizetype(1024 * 1024));
    QCOMPARE(response.contentType, QByteArrayLiteral("application/octet-stream"));
}

void TimestampClientTest::networkTimeout()
{
    // The whole exchange at most the limit (434:8518: 15 s in the application, shorter here)
    m_server->setBehavior(TestHttpServer::Silent);
    const int timeout = 1000 * getTimeoutFactor();
    QElapsedTimer timer;
    timer.start();
    pdf::PDFTimestampTransport::Response response;
    postInThread(createRequest(m_server->getUrl(), timeout), nullptr, 30000 * getTimeoutFactor(), response);
    if (QTest::currentTestFailed())
    {
        return;
    }

    QCOMPARE(response.error, pdf::PDFTimestampTransport::Error::Timeout);
    QVERIFY(timer.elapsed() >= timeout - 100);
    QVERIFY(timer.elapsed() < timeout + 10000 * getTimeoutFactor());
}

void TimestampClientTest::networkCancel()
{
    // Stop of the run: the request is aborted at once, long before its time limit
    m_server->setBehavior(TestHttpServer::Silent);
    TestOperationControl control;
    const int timeout = 120000 * getTimeoutFactor();
    QTimer cancelTimer;
    cancelTimer.setSingleShot(true);
    QObject::connect(&cancelTimer, &QTimer::timeout, &cancelTimer, [&control]() { control.cancel(); });
    cancelTimer.start(300);

    QElapsedTimer timer;
    timer.start();
    pdf::PDFTimestampTransport::Response response;
    postInThread(createRequest(m_server->getUrl(), timeout), &control, 30000 * getTimeoutFactor(), response);
    if (QTest::currentTestFailed())
    {
        return;
    }

    QCOMPARE(response.error, pdf::PDFTimestampTransport::Error::Canceled);
    QVERIFY(timer.elapsed() < 20000 * getTimeoutFactor());
}

void TimestampClientTest::createCertificate()
{
    // 5.3, B130: X.509 version 3, keyUsage digitalSignature + nonRepudiation + keyEncipherment
    // (critical, RFC 5280, 4.2.1.3), basicConstraints CA:FALSE, SKI and AKI = SKI (self-signed),
    // signed by SHA-256, 2048 bits by default (Q6.14)
    int bits = 0;
    const QByteArray certificateData = getPkcs12Certificate(m_pkcs12, m_password, &bits);
    QVERIFY(!certificateData.isEmpty());
    QCOMPARE(bits, 2048);

    openssl_ptr<X509> certificate(decodeCertificate(certificateData), &X509_free);
    QVERIFY(certificate);
    QCOMPARE(X509_get_version(certificate.get()), long(X509_VERSION_3));
    QCOMPARE(X509_get_key_usage(certificate.get()), uint32_t(KU_DIGITAL_SIGNATURE | KU_NON_REPUDIATION | KU_KEY_ENCIPHERMENT));
    QCOMPARE(X509_EXTENSION_get_critical(X509_get_ext(certificate.get(), X509_get_ext_by_NID(certificate.get(), NID_key_usage, -1))), 1);

    int criticality = -1;
    BASIC_CONSTRAINTS* constraints = static_cast<BASIC_CONSTRAINTS*>(X509_get_ext_d2i(certificate.get(), NID_basic_constraints, &criticality, nullptr));
    QVERIFY(constraints);
    QCOMPARE(constraints->ca, 0);
    QCOMPARE(criticality, 1);
    BASIC_CONSTRAINTS_free(constraints);

    const ASN1_OCTET_STRING* subjectKeyId = X509_get0_subject_key_id(certificate.get());
    const ASN1_OCTET_STRING* authorityKeyId = X509_get0_authority_key_id(certificate.get());
    QVERIFY(subjectKeyId);
    QVERIFY(authorityKeyId);
    QCOMPARE(ASN1_OCTET_STRING_cmp(subjectKeyId, authorityKeyId), 0);

    QCOMPARE(X509_get_signature_nid(certificate.get()), NID_sha256WithRSAEncryption);
    QCOMPARE(X509_verify(certificate.get(), X509_get0_pubkey(certificate.get())), 1);

    // The name of the subject (RFC 5280, 4.1.2.4)
    const std::optional<pdf::PDFCertificateInfo> info = pdf::PDFCertificateInfo::getCertificateInfo(certificateData);
    QVERIFY(info);
    QCOMPARE(info->getName(pdf::PDFCertificateInfo::CommonName), QStringLiteral("Jane Novak"));
    QCOMPARE(info->getName(pdf::PDFCertificateInfo::Email), QStringLiteral("jane@example.com"));

    // RFC 5280, 4.1.2.2: a positive serial number, unique - two certificates of the same name differ
    pdf::PDFCertificateManager::NewCertificateInfo secondInfo;
    secondInfo.privateKeyPasword = QString::fromUtf8(m_password);
    secondInfo.certCommonName = QStringLiteral("Jane Novak");
    const QByteArray secondPkcs12 = pdf::PDFSignatureFactory::createCertificate(secondInfo, nullptr, nullptr);
    openssl_ptr<X509> secondCertificate(decodeCertificate(getPkcs12Certificate(secondPkcs12, m_password)), &X509_free);
    QVERIFY(secondCertificate);
    QVERIFY(ASN1_INTEGER_cmp(X509_get0_serialNumber(certificate.get()), X509_get0_serialNumber(secondCertificate.get())) != 0);
    openssl_ptr<BIGNUM> serialNumber(ASN1_INTEGER_to_BN(X509_get0_serialNumber(certificate.get()), nullptr), &BN_free);
    QVERIFY(serialNumber);
    QVERIFY(!BN_is_negative(serialNumber.get()));
    QVERIFY(BN_num_bits(serialNumber.get()) > 64);
}

void TimestampClientTest::createCertificateKeyLengths()
{
    // Q6.14: 2048 (default), 3072, 4096 - no 1024 and no other length
    pdf::PDFCertificateManager::NewCertificateInfo info;
    info.privateKeyPasword = QString::fromUtf8(m_password);
    info.certCommonName = QStringLiteral("Key length");
    QCOMPARE(info.rsaKeyLength, 2048);

    for (const int refusedLength : { 1024, 2047, 8192 })
    {
        info.rsaKeyLength = refusedLength;
        QString error;
        QVERIFY(pdf::PDFSignatureFactory::createCertificate(info, nullptr, &error).isEmpty());
        QVERIFY(error.contains(QString::number(refusedLength)));
    }

    info.rsaKeyLength = 3072;
    QString error;
    const QByteArray pkcs12 = pdf::PDFSignatureFactory::createCertificate(info, nullptr, &error);
    QVERIFY2(!pkcs12.isEmpty(), qPrintable(error));
    int bits = 0;
    QVERIFY(!getPkcs12Certificate(pkcs12, m_password, &bits).isEmpty());
    QCOMPARE(bits, 3072);
}

void TimestampClientTest::createCertificateCancel()
{
    // 6.4: the generation is stopped by the flag through the callback of EVP_PKEY_CTX_set_cb, also in
    // the middle of the generation of a 4096 bit key
    pdf::PDFCertificateManager::NewCertificateInfo info;
    info.privateKeyPasword = QString::fromUtf8(m_password);
    info.certCommonName = QStringLiteral("Canceled");
    info.rsaKeyLength = 4096;

    std::atomic<bool> cancelFlag{ true };
    QString error;
    QVERIFY(pdf::PDFSignatureFactory::createCertificate(info, &cancelFlag, &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("canceled")));

    // The flag is set 200 ms after the start; a generation, which ended before (a lucky prime search),
    // is not a cancellation and is tried again - one canceled generation is required
    int canceledCount = 0;
    for (int attempt = 0; attempt < 5 && canceledCount == 0; ++attempt)
    {
        cancelFlag.store(false);
        QByteArray result;
        QElapsedTimer timer;
        std::atomic<qint64> finishedAt{ -1 };
        timer.start();
        std::unique_ptr<QThread> thread(QThread::create([&]()
        {
            result = pdf::PDFSignatureFactory::createCertificate(info, &cancelFlag, &error);
            finishedAt.store(timer.elapsed());
        }));
        thread->start();
        QThread::msleep(200);
        cancelFlag.store(true);
        const qint64 cancelAt = timer.elapsed();
        thread->wait();

        if (!result.isEmpty())
        {
            // The key was ready before the flag (the certificate and the PKCS#12 take a few ms after it)
            QVERIFY2(finishedAt.load() < cancelAt + 50, qPrintable(QStringLiteral("finished %1 ms after the cancellation").arg(finishedAt.load() - cancelAt)));
            continue;
        }

        // Stopped by the callback in the middle of the generation
        QVERIFY(result.isEmpty());
        QVERIFY(error.contains(QStringLiteral("canceled")));
        ++canceledCount;
    }
    QCOMPARE(canceledCount, 1);
}

void TimestampClientTest::createCertificateFile()
{
    // The old applications and the tests: the file of PDFCertificateManager::createCertificate has the
    // same certificate of version 3 (B130), 1024 bits are still accepted there
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    pdf::PDFCertificateManager::NewCertificateInfo info;
    info.fileName = directory.filePath(QStringLiteral("old.pfx"));
    info.privateKeyPasword = QString::fromUtf8(m_password);
    info.certCommonName = QStringLiteral("Old application");
    info.rsaKeyLength = 1024;
    QString error;
    QVERIFY2(pdf::PDFCertificateManager().createCertificate(info, &error), qPrintable(error));

    QFile file(info.fileName);
    QVERIFY(file.open(QFile::ReadOnly));
    const QByteArray pkcs12 = file.readAll();
    int bits = 0;
    openssl_ptr<X509> certificate(decodeCertificate(getPkcs12Certificate(pkcs12, m_password, &bits)), &X509_free);
    QVERIFY(certificate);
    QCOMPARE(bits, 1024);
    QCOMPARE(X509_get_version(certificate.get()), long(X509_VERSION_3));
    QCOMPARE(X509_get_key_usage(certificate.get()), uint32_t(KU_DIGITAL_SIGNATURE | KU_NON_REPUDIATION | KU_KEY_ENCIPHERMENT));
    QVERIFY(pdf::PDFCertificateManager::isUsableForEncryption(pdf::PDFCertificateManager::readCertificate(pkcs12, QString::fromUtf8(m_password))->getCertificateData()));

    // An invalid country fails the creation with its text
    info.certCountryCode = QStringLiteral("Czechia");
    QVERIFY(!pdf::PDFCertificateManager().createCertificate(info, &error));
    QVERIFY(error.contains(QStringLiteral("Czechia")));
}

void TimestampClientTest::unlockPassword()
{
    pdf::PDFCertificateEntry entry;
    entry.pkcs12 = m_pkcs12;

    // Figma 434:7143: the text of a wrong password; the live check gives only a bool (6.3)
    QString error;
    pdf::PDFSigningKey key = pdf::PDFSignatureFactory::unlock(entry, QByteArrayLiteral("wrong"), 0, &error);
    QVERIFY(!key.isValid());
    QCOMPARE(key.getSource(), pdf::PDFSigningKey::Source::None);
    QCOMPARE(error, QStringLiteral("Password to open certificate is invalid."));
    QVERIFY(!pdf::PDFSignatureFactory::isPasswordValid(m_pkcs12, QByteArrayLiteral("wrong")));
    QVERIFY(pdf::PDFSignatureFactory::isPasswordValid(m_pkcs12, m_password));

    key = pdf::PDFSignatureFactory::unlock(entry, m_password, 0, &error);
    QVERIFY2(key.isValid(), qPrintable(error));
    QCOMPARE(key.getSource(), pdf::PDFSigningKey::Source::Pkcs12);
    QCOMPARE(key.getCertificateData(), getPkcs12Certificate(m_pkcs12, m_password));
    QVERIFY(key.getChainData().empty());
    QVERIFY(error.isEmpty());

    // An entry without a key file and not of the system store has nothing to unlock
    pdf::PDFCertificateEntry userEntry;
    QVERIFY(!pdf::PDFSignatureFactory::unlock(userEntry, m_password, 0, &error).isValid());
    QVERIFY(!error.isEmpty());
}

void TimestampClientTest::signPades()
{
    pdf::PDFCertificateEntry entry;
    entry.pkcs12 = m_pkcs12;
    QString error;
    const pdf::PDFSigningKey key = pdf::PDFSignatureFactory::unlock(entry, m_password, 0, &error);
    QVERIFY2(key.isValid(), qPrintable(error));

    const QByteArray data = QByteArrayLiteral("%PDF-1.7 the bytes covered by /ByteRange");
    const pdf::PDFSigningKey::Signature signature = key.sign(data, pdf::PDFSignatureFormat::PAdES, nullptr, nullptr);
    QVERIFY2(signature.isSigned(), qPrintable(signature.errorMessage));

    // RFC 5652: a detached SignedData of one signer, SHA-256, verifying over exactly the covered bytes
    const SignatureParts parts = parseSignature(signature.data);
    QVERIFY(parts.signerInfo);
    QCOMPARE(parts.signerCount, 1);
    QCOMPARE(CMS_get0_content(parts.cms.get()) ? *CMS_get0_content(parts.cms.get()) : nullptr, static_cast<ASN1_OCTET_STRING*>(nullptr));
    QVERIFY(verifySignature(parts, data));
    QVERIFY(!verifySignature(parts, data + " "));

    X509_ALGOR* digestAlgorithm = nullptr;
    CMS_SignerInfo_get0_algs(parts.signerInfo, nullptr, nullptr, &digestAlgorithm, nullptr);
    const ASN1_OBJECT* digestObject = nullptr;
    X509_ALGOR_get0(&digestObject, nullptr, nullptr, digestAlgorithm);
    QCOMPARE(OBJ_obj2nid(digestObject), NID_sha256);

    // Signed attributes (RFC 5652, 11.1, 11.2; ETSI EN 319 142-1, 5.2: no signing-time - the time is /M)
    QCOMPARE(getAttributeValue(parts.signerInfo, NID_pkcs9_contentType, true), encode<ASN1_OBJECT>(OBJ_nid2obj(NID_pkcs7_data), &i2d_ASN1_OBJECT));
    QCOMPARE(getAttributeValue(parts.signerInfo, NID_pkcs9_messageDigest, true), encodeOctetString(sha256(data)));
    QVERIFY(getAttributeValue(parts.signerInfo, NID_pkcs9_signingTime, true).isEmpty());

    // ESS signing-certificate-v2 (RFC 5035; EN 319 122-1, 5.2.2.3): ESSCertIDv2 with the SHA-256 hash of
    // the certificate of the signer (the default algorithm, so no AlgorithmIdentifier is written)
    const QByteArray signingCertificate = getAttributeValue(parts.signerInfo, NID_id_smime_aa_signingCertificateV2, true);
    QVERIFY(!signingCertificate.isEmpty());
    QVERIFY(signingCertificate.contains(sha256(key.getCertificateData())));
    QVERIFY(getAttributeValue(parts.signerInfo, NID_id_smime_aa_signingCertificate, true).isEmpty());

    // The certificate of the signer is in the signature
    STACK_OF(X509)* certificates = CMS_get1_certs(parts.cms.get());
    QCOMPARE(sk_X509_num(certificates), 1);
    QCOMPARE(encode<X509>(sk_X509_value(certificates, 0), &i2d_X509), key.getCertificateData());
    sk_X509_pop_free(certificates, X509_free);
}

void TimestampClientTest::signPkcs7()
{
    pdf::PDFCertificateEntry entry;
    entry.pkcs12 = m_pkcs12;
    const pdf::PDFSigningKey key = pdf::PDFSignatureFactory::unlock(entry, m_password, 0, nullptr);
    QVERIFY(key.isValid());

    const QByteArray data = QByteArrayLiteral("%PDF-1.4 the bytes covered by /ByteRange");
    const pdf::PDFSigningKey::Signature signature = key.sign(data, pdf::PDFSignatureFormat::Pkcs7, nullptr, nullptr);
    QVERIFY2(signature.isSigned(), qPrintable(signature.errorMessage));

    // adbe.pkcs7.detached (12.8.3.3): signing-time allowed (the compatible format writes it), no ESS
    const SignatureParts parts = parseSignature(signature.data);
    QVERIFY(verifySignature(parts, data));
    QVERIFY(!getAttributeValue(parts.signerInfo, NID_pkcs9_signingTime, true).isEmpty());
    QVERIFY(getAttributeValue(parts.signerInfo, NID_id_smime_aa_signingCertificateV2, true).isEmpty());
    QCOMPARE(getAttributeValue(parts.signerInfo, NID_pkcs9_messageDigest, true), encodeOctetString(sha256(data)));

    // The readers of PKCS#7 (the verifier of the old applications) verify it too
    const unsigned char* pointer = reinterpret_cast<const unsigned char*>(signature.data.constData());
    openssl_ptr<PKCS7> pkcs7(d2i_PKCS7(nullptr, &pointer, long(signature.data.size())), &PKCS7_free);
    QVERIFY(pkcs7);
    openssl_ptr<BIO> content(BIO_new_mem_buf(data.constData(), int(data.size())), &BIO_free_all);
    QCOMPARE(PKCS7_verify(pkcs7.get(), nullptr, nullptr, content.get(), nullptr, PKCS7_BINARY | PKCS7_NOVERIFY), 1);

    // The old interface signs the same way (B112, B113: one implementation)
    QByteArray oldSignature;
    QVERIFY(pdf::PDFSignatureFactory::sign(entry, QString::fromUtf8(m_password), data, oldSignature));
    QVERIFY(verifySignature(parseSignature(oldSignature), data));
    QVERIFY(!pdf::PDFSignatureFactory::sign(entry, QStringLiteral("wrong"), data, oldSignature));
}

void TimestampClientTest::signWithTimestamp()
{
    m_authority->setBehavior(TestAuthority::Granted);
    m_transport->setSettings(LocalTransport::Settings());
    const size_t requestCount = m_transport->getRequests().size();

    pdf::PDFCertificateEntry entry;
    entry.pkcs12 = m_pkcs12;
    const pdf::PDFSigningKey key = pdf::PDFSignatureFactory::unlock(entry, m_password, 0, nullptr);
    QVERIFY(key.isValid());

    const pdf::PDFTimestampClient client(m_transport);
    pdf::PDFSigningKey::Timestamp timestamp;
    timestamp.client = &client;
    timestamp.url = QStringLiteral("http://timestamp.digicert.com");

    const QByteArray data = QByteArrayLiteral("covered bytes");
    const pdf::PDFSigningKey::Signature signature = key.sign(data, pdf::PDFSignatureFormat::PAdES, &timestamp, nullptr);
    QVERIFY2(signature.isSigned(), qPrintable(signature.errorMessage));
    QVERIFY(signature.timestamp.isGranted());
    QCOMPARE(signature.timestamp.authorityName, QStringLiteral("PDF4QT Test Timestamp Authority"));

    // One request of the authority (B134, R6-28)
    QCOMPARE(m_transport->getRequests().size(), requestCount + 1);

    // RFC 3161, Appendix A (PAdES B-T): the unsigned attribute id-aa-timeStampToken, whose imprint is
    // SHA-256 of the signature value of the signer
    const SignatureParts parts = parseSignature(signature.data);
    QVERIFY(verifySignature(parts, data));
    const QByteArray tokenAttribute = getAttributeValue(parts.signerInfo, NID_id_smime_aa_timeStampToken, false);
    QVERIFY(!tokenAttribute.isEmpty());
    QCOMPARE(tokenAttribute, signature.timestamp.token);

    const ASN1_OCTET_STRING* signatureValue = CMS_SignerInfo_get0_signature(parts.signerInfo);
    const QByteArray signatureValueBytes(reinterpret_cast<const char*>(ASN1_STRING_get0_data(signatureValue)), ASN1_STRING_length(signatureValue));
    QCOMPARE(parseToken(signature.timestamp.token).digest, sha256(signatureValueBytes));

    // The estimated size covers the real one (B134)
    QVERIFY(key.estimateSignatureSize(pdf::PDFSignatureFormat::PAdES, true) >= signature.data.size());
}

void TimestampClientTest::signTimestampFailure()
{
    // 434:8518: no signature without the timestamp, the reason of the authority is given
    m_authority->setBehavior(TestAuthority::Granted);
    LocalTransport::Settings settings;
    settings.httpStatus = 503;
    m_transport->setSettings(settings);
    const size_t requestCount = m_transport->getRequests().size();

    pdf::PDFCertificateEntry entry;
    entry.pkcs12 = m_pkcs12;
    const pdf::PDFSigningKey key = pdf::PDFSignatureFactory::unlock(entry, m_password, 0, nullptr);
    const pdf::PDFTimestampClient client(m_transport);
    pdf::PDFSigningKey::Timestamp timestamp;
    timestamp.client = &client;
    timestamp.url = QStringLiteral("http://timestamp.digicert.com");

    const pdf::PDFSigningKey::Signature signature = key.sign(QByteArrayLiteral("data"), pdf::PDFSignatureFormat::PAdES, &timestamp, nullptr);
    m_transport->setSettings(LocalTransport::Settings());

    QVERIFY(!signature.isSigned());
    QCOMPARE(signature.timestamp.status, pdf::PDFTimestampClient::Status::HttpError);
    QCOMPARE(signature.timestamp.httpStatus, 503);
    QVERIFY(signature.errorMessage.contains(QStringLiteral("503")));
    QCOMPARE(m_transport->getRequests().size(), requestCount + 1);

    // A timestamp without a client never reaches the network
    pdf::PDFSigningKey::Timestamp noClient;
    noClient.url = timestamp.url;
    const pdf::PDFSigningKey::Signature withoutClient = key.sign(QByteArrayLiteral("data"), pdf::PDFSignatureFormat::PAdES, &noClient, nullptr);
    QVERIFY(!withoutClient.isSigned());
    QCOMPARE(withoutClient.timestamp.status, pdf::PDFTimestampClient::Status::NoTransport);
}

void TimestampClientTest::estimateSignatureSize()
{
    // B134: the reservation of /Contents without a trial signature - at least the real size
    pdf::PDFCertificateEntry entry;
    entry.pkcs12 = m_pkcs12;
    const pdf::PDFSigningKey key = pdf::PDFSignatureFactory::unlock(entry, m_password, 0, nullptr);
    QVERIFY(key.isValid());

    for (const pdf::PDFSignatureFormat format : { pdf::PDFSignatureFormat::PAdES, pdf::PDFSignatureFormat::Pkcs7 })
    {
        const pdf::PDFSigningKey::Signature signature = key.sign(QByteArrayLiteral("data"), format, nullptr, nullptr);
        QVERIFY(signature.isSigned());
        const qint64 estimate = key.estimateSignatureSize(format, false);
        QVERIFY(estimate >= signature.data.size());

        // The parts of 4.1: the certificate, the key length, 2048 bytes; a timestamp adds 16 KiB
        QCOMPARE(estimate, qint64(key.getCertificateData().size()) + 2048 / 8 + 2048);
        QCOMPARE(key.estimateSignatureSize(format, true), estimate + 16 * 1024);
    }

    QCOMPARE(pdf::PDFSigningKey().estimateSignatureSize(pdf::PDFSignatureFormat::PAdES, true), qint64(0));
}

void TimestampClientTest::unlockChecksCertificate_data()
{
    QTest::addColumn<int>("variant");
    QTest::addColumn<bool>("isAccepted");
    QTest::addColumn<QString>("text");

    // 5.3, B116: valid now, keyUsage digitalSignature or nonRepudiation (when present), extended key usage
    // (when present) emailProtection, documentSigning, Adobe or Microsoft document signing, any; RSA or EC
    QTest::newRow("no extensions") << 0 << true << QString();
    QTest::newRow("non repudiation") << 1 << true << QString();
    QTest::newRow("key encipherment only") << 2 << false << QStringLiteral("key usage");
    QTest::newRow("server only") << 3 << false << QStringLiteral("extended key usage");
    QTest::newRow("document signing") << 4 << true << QString();
    QTest::newRow("e-mail protection") << 5 << true << QString();
    QTest::newRow("adobe") << 6 << true << QString();
    QTest::newRow("expired") << 7 << false << QStringLiteral("expired");
    QTest::newRow("not yet valid") << 8 << false << QStringLiteral("not valid yet");
    QTest::newRow("chain of 17") << 9 << false << QStringLiteral("too many certificates");
    QTest::newRow("chain of 16") << 10 << true << QString();

    // Owner 2026-11-15: anyExtendedKeyUsage (RFC 5280, 4.2.1.12) and Microsoft Document Signing
    QTest::newRow("microsoft document signing") << 11 << true << QString();
    QTest::newRow("any extended key usage") << 12 << true << QString();
    QTest::newRow("server and code signing") << 13 << false << QStringLiteral("extended key usage");
}

void TimestampClientTest::unlockChecksCertificate()
{
    QFETCH(int, variant);
    QFETCH(bool, isAccepted);
    QFETCH(QString, text);

    openssl_ptr<EVP_PKEY> key(EVP_EC_gen("P-256"), &EVP_PKEY_free);
    CertificateDefinition definition;
    switch (variant)
    {
        case 1: definition.extensions = { { NID_key_usage, "critical,nonRepudiation" } }; break;
        case 2: definition.extensions = { { NID_key_usage, "critical,keyEncipherment" } }; break;
        case 3: definition.extensions = { { NID_ext_key_usage, "serverAuth" } }; break;
        case 4: definition.extensions = { { NID_ext_key_usage, "1.3.6.1.5.5.7.3.36" } }; break;
        case 5: definition.extensions = { { NID_ext_key_usage, "emailProtection" } }; break;
        case 6: definition.extensions = { { NID_ext_key_usage, "1.2.840.113583.1.1.5" } }; break;
        case 7: definition.notBeforeOffset = -3 * 24 * 3600; definition.notAfterOffset = -24 * 3600; break;
        case 8: definition.notBeforeOffset = 24 * 3600; definition.notAfterOffset = 3 * 24 * 3600; break;
        case 11: definition.extensions = { { NID_ext_key_usage, "1.3.6.1.4.1.311.10.3.12" } }; break;
        case 12: definition.extensions = { { NID_ext_key_usage, "anyExtendedKeyUsage" } }; break;
        case 13: definition.extensions = { { NID_ext_key_usage, "serverAuth,codeSigning" } }; break;
        default: break;
    }

    openssl_ptr<X509> certificate(createTestCertificate(key.get(), definition, 7), &X509_free);
    openssl_ptr<STACK_OF(X509)> chain(sk_X509_new_null(), [](STACK_OF(X509)* stack) { sk_X509_pop_free(stack, X509_free); });
    const int chainLength = (variant == 9) ? 17 : (variant == 10 ? 16 : 0);
    for (int i = 0; i < chainLength; ++i)
    {
        CertificateDefinition chainDefinition;
        chainDefinition.commonName = "Chain " + QByteArray::number(i);
        sk_X509_push(chain.get(), createTestCertificate(key.get(), chainDefinition, 100 + i));
    }

    pdf::PDFCertificateEntry entry;
    entry.pkcs12 = createPkcs12(key.get(), certificate.get(), chain.get(), m_password);
    QVERIFY(!entry.pkcs12.isEmpty());

    QString error;
    const pdf::PDFSigningKey signingKey = pdf::PDFSignatureFactory::unlock(entry, m_password, 0, &error);
    QCOMPARE(signingKey.isValid(), isAccepted);
    QVERIFY2(error.contains(text), qPrintable(error));

    // The one rule of the usage for unlock and the lists of the application (PDFCertificateManager::
    // isUsableForSigning): the key usage, the extended key usage and the key - not the time, not the chain
    const bool isUsageRow = variant == 2 || variant == 3 || variant == 13;
    QString usageError;
    QCOMPARE(pdf::PDFCertificateManager::isUsableForSigning(encode<X509>(certificate.get(), &i2d_X509), &usageError), !isUsageRow);
    QCOMPARE(usageError.isEmpty(), !isUsageRow);
    if (isUsageRow)
    {
        QVERIFY2(usageError.contains(text), qPrintable(usageError));
    }

    if (isAccepted)
    {
        // An ECDSA key signs and its signature verifies (RFC 5753)
        QCOMPARE(int(signingKey.getChainData().size()), chainLength);
        const QByteArray data = QByteArrayLiteral("data");
        const pdf::PDFSigningKey::Signature signature = signingKey.sign(data, pdf::PDFSignatureFormat::PAdES, nullptr, nullptr);
        QVERIFY2(signature.isSigned(), qPrintable(signature.errorMessage));
        QVERIFY(verifySignature(parseSignature(signature.data), data));
        QVERIFY(signingKey.estimateSignatureSize(pdf::PDFSignatureFormat::PAdES, false) >= signature.data.size());
    }
}

void TimestampClientTest::moveOnlyKey()
{
    // RAII, move-only (10.3): the moved-from key is empty and cannot sign
    pdf::PDFCertificateEntry entry;
    entry.pkcs12 = m_pkcs12;
    pdf::PDFSigningKey first = pdf::PDFSignatureFactory::unlock(entry, m_password, 0, nullptr);
    QVERIFY(first.isValid());

    pdf::PDFSigningKey second(std::move(first));
    QVERIFY(second.isValid());
    QVERIFY(!first.isValid());
    QVERIFY(first.getCertificateData().isEmpty());
    QVERIFY(!first.sign(QByteArrayLiteral("data"), pdf::PDFSignatureFormat::PAdES, nullptr, nullptr).isSigned());

    pdf::PDFSigningKey third;
    third = std::move(second);
    QVERIFY(third.isValid());
    QVERIFY(!second.isValid());
    QVERIFY(third.sign(QByteArrayLiteral("data"), pdf::PDFSignatureFormat::PAdES, nullptr, nullptr).isSigned());
}

void TimestampClientTest::pkcs12IterationLimit()
{
    // 6.3, R6-28: more than 10,000,000 iterations of the MAC or of the key encryption = "The certificate
    // file cannot be used", refused before the password is tried - quickly, also by the listing (an
    // empty password) and by the live check of the password
    const qint64 limit = pdf::PDFCertificateManager::MAXIMUM_PKCS12_ITERATIONS;
    QCOMPARE(limit, qint64(10000000));
    QVERIFY(pdf::PDFCertificateManager::isPkcs12IterationCountAcceptable(m_pkcs12, m_password));

    const QByteArray atLimitMac = setMacIterations(m_pkcs12, limit);
    const QByteArray hostileMac = setMacIterations(m_pkcs12, limit + 1);
    const QByteArray atLimitKey = setKeyBagIterations(m_pkcs12, m_password, limit);
    const QByteArray hostileKey = setKeyBagIterations(m_pkcs12, m_password, limit + 1);
    QVERIFY(!hostileMac.isEmpty());
    QVERIFY(!hostileKey.isEmpty());

    QVERIFY(pdf::PDFCertificateManager::isPkcs12IterationCountAcceptable(atLimitMac, m_password));
    QVERIFY(pdf::PDFCertificateManager::isPkcs12IterationCountAcceptable(atLimitKey, m_password));
    QVERIFY(!pdf::PDFCertificateManager::isPkcs12IterationCountAcceptable(hostileMac, m_password));
    QVERIFY(!pdf::PDFCertificateManager::isPkcs12IterationCountAcceptable(hostileKey, m_password));
    QVERIFY(!pdf::PDFCertificateManager::isPkcs12IterationCountAcceptable(QByteArrayLiteral("no PKCS#12"), m_password));

    for (const QByteArray& hostile : { hostileMac, hostileKey })
    {
        QElapsedTimer timer;
        timer.start();

        pdf::PDFCertificateEntry entry;
        entry.pkcs12 = hostile;
        QString error;
        QVERIFY(!pdf::PDFSignatureFactory::unlock(entry, m_password, 0, &error).isValid());
        QCOMPARE(error, QStringLiteral("The certificate file cannot be used."));
        QVERIFY(!pdf::PDFSignatureFactory::isPasswordValid(hostile, m_password));
        QVERIFY(!pdf::PDFCertificateManager::readCertificate(hostile, QString()).has_value());
        QVERIFY(!pdf::PDFCertificateManager::readCertificate(hostile, QString::fromUtf8(m_password)).has_value());
        QVERIFY(!pdf::PDFCertificateManager::isCertificateValid(entry, QString::fromUtf8(m_password)));

        // Ten million iterations of PBKDF2 would take seconds; the refusal does not iterate at all
        QVERIFY2(timer.elapsed() < 1500 * getTimeoutFactor(), qPrintable(QString::number(timer.elapsed())));
    }
}

void TimestampClientTest::systemStore()
{
#ifdef Q_OS_WIN
    // Q6.2: the store of the system is replaced by a store in the memory of the process (the owner rule:
    // never the store of the machine); the key stays in CNG, the signature and the decryption are made
    // by the system
    std::shared_ptr<const pdf::PDFPersonalCertificateStore> store = pdf::PDFPersonalCertificateStore::createMemoryStore(m_pkcs12, m_password);
    QVERIFY(store);
    QVERIFY(!pdf::PDFPersonalCertificateStore::createMemoryStore(m_pkcs12, QByteArrayLiteral("wrong")));
    pdf::PDFPersonalCertificateStore::setStore(store);

    const QByteArray certificateData = getPkcs12Certificate(m_pkcs12, m_password);
    const pdf::PDFCertificateEntries entries = pdf::PDFCertificateStore::getPersonalCertificates(pdf::PDFCertificateUsageFilter::DigitalSignature);
    auto it = std::find_if(entries.cbegin(), entries.cend(), [&certificateData](const pdf::PDFCertificateEntry& entry) { return entry.info.getCertificateData() == certificateData; });
    QVERIFY(it != entries.cend());
    QCOMPARE(it->type, pdf::PDFCertificateEntry::EntryType::System);
    QVERIFY(it->pkcs12.isEmpty());
    QCOMPARE(int(entries.size()), 1);

    QString error;
    const pdf::PDFSigningKey key = pdf::PDFSignatureFactory::unlock(*it, QByteArray(), 0, &error);
    QVERIFY2(key.isValid(), qPrintable(error));
    QCOMPARE(key.getSource(), pdf::PDFSigningKey::Source::SystemStore);
    QCOMPARE(key.getCertificateData(), certificateData);

    const QByteArray data = QByteArrayLiteral("covered bytes signed by the system");
    for (const pdf::PDFSignatureFormat format : { pdf::PDFSignatureFormat::PAdES, pdf::PDFSignatureFormat::Pkcs7 })
    {
        const pdf::PDFSigningKey::Signature signature = key.sign(data, format, nullptr, nullptr);
        QVERIFY2(signature.isSigned(), qPrintable(signature.errorMessage));
        const SignatureParts parts = parseSignature(signature.data);
        QVERIFY(verifySignature(parts, data));
        QCOMPARE(getAttributeValue(parts.signerInfo, NID_pkcs9_signingTime, true).isEmpty(), format == pdf::PDFSignatureFormat::PAdES);
        QCOMPARE(getAttributeValue(parts.signerInfo, NID_id_smime_aa_signingCertificateV2, true).isEmpty(), format == pdf::PDFSignatureFormat::Pkcs7);
    }

    // The opening of a document encrypted for the certificate (ISO 32000-2, 7.6.5.3: the recipient is
    // PKCS#7 enveloped data of the seed and the permissions)
    const QByteArray seed = QByteArrayLiteral("01234567890123456789\xFC\xFF\xFF\xFF");
    openssl_ptr<X509> certificate(decodeCertificate(certificateData), &X509_free);
    openssl_ptr<STACK_OF(X509)> recipients(sk_X509_new_null(), [](STACK_OF(X509)* stack) { sk_X509_free(stack); });
    sk_X509_push(recipients.get(), certificate.get());
    openssl_ptr<BIO> seedBuffer(BIO_new_mem_buf(seed.constData(), int(seed.size())), &BIO_free_all);
    openssl_ptr<PKCS7> envelope(PKCS7_encrypt(recipients.get(), seedBuffer.get(), EVP_aes_256_cbc(), PKCS7_BINARY), &PKCS7_free);
    const QByteArray envelopeData = encode<PKCS7>(envelope.get(), &i2d_PKCS7);
    QVERIFY(!envelopeData.isEmpty());

    const std::optional<QByteArray> decrypted = pdf::PDFPersonalCertificateStore::decryptEnvelope(certificateData, envelopeData);
    QVERIFY(decrypted.has_value());
    QCOMPARE(*decrypted, seed);

    // Another certificate (not in the store) and a damaged envelope decrypt nothing
    openssl_ptr<EVP_PKEY> otherKey(EVP_RSA_gen(2048), &EVP_PKEY_free);
    openssl_ptr<X509> otherCertificate(createTestCertificate(otherKey.get(), CertificateDefinition(), 9), &X509_free);
    QVERIFY(!pdf::PDFPersonalCertificateStore::decryptEnvelope(encode<X509>(otherCertificate.get(), &i2d_X509), envelopeData).has_value());
    QVERIFY(!pdf::PDFPersonalCertificateStore::decryptEnvelope(certificateData, envelopeData.left(envelopeData.size() / 2)).has_value());

    // Without the injected store, the certificate is not offered (the test never reads the store of the
    // machine for it: an entry of the memory store is unknown there)
    pdf::PDFPersonalCertificateStore::setStore(nullptr);
    QVERIFY(pdf::PDFPersonalCertificateStore::getStore());
#else
    QSKIP("The certificate store of the system exists only on Windows (Q6.2).");
#endif
}

void TimestampClientTest::trustedStoreLimits()
{
    // B131 (SECURITY_TODO 53): the version of the format, at most 10,000 entries and 4 MiB of the file
    pdf::PDFCertificateStore store;
    QVERIFY(store.add(pdf::PDFCertificateEntry::EntryType::User, getPkcs12Certificate(m_pkcs12, m_password)));

    QByteArray valid;
    {
        QDataStream stream(&valid, QIODevice::WriteOnly);
        store.serialize(stream);
    }

    pdf::PDFCertificateStore readStore;
    {
        QDataStream stream(valid);
        readStore.deserialize(stream);
        QCOMPARE(stream.status(), QDataStream::Ok);
        QCOMPARE(readStore.getCertificates().size(), size_t(1));
        QCOMPARE(readStore.getCertificates().front().info, store.getCertificates().front().info);
    }

    auto createStream = [](int version, quint64 count, const std::function<void(QDataStream&)>& writeEntries)
    {
        QByteArray data;
        QDataStream stream(&data, QIODevice::WriteOnly);
        stream << version;
        stream << count;
        if (writeEntries)
        {
            writeEntries(stream);
        }
        return data;
    };

    auto readCount = [](const QByteArray& data, QDataStream::Status* readStatus)
    {
        pdf::PDFCertificateStore deserializedStore;
        QDataStream stream(data);
        deserializedStore.deserialize(stream);
        *readStatus = stream.status();
        return deserializedStore.getCertificates().size();
    };

    // Another version of the format is not read as this one
    QDataStream::Status status = QDataStream::Ok;
    QCOMPARE(readCount(createStream(2, 0, nullptr), &status), size_t(0));
    QCOMPARE(status, QDataStream::ReadCorruptData);

    // A hostile count is refused before anything is allocated by it
    QCOMPARE(readCount(createStream(1, quint64(1) << 40, nullptr), &status), size_t(0));
    QCOMPARE(status, QDataStream::ReadCorruptData);

    // 10,000 entries are read, 10,001 are not (each a valid entry, written by serialize)
    auto createStore = [&store](size_t count)
    {
        pdf::PDFCertificateStore largeStore;
        largeStore.setCertificates(pdf::PDFCertificateEntries(count, store.getCertificates().front()));
        QByteArray data;
        QDataStream stream(&data, QIODevice::WriteOnly);
        largeStore.serialize(stream);
        return data;
    };

    QCOMPARE(readCount(createStore(10000), &status), size_t(10000));
    QCOMPARE(status, QDataStream::Ok);
    QCOMPARE(readCount(createStore(10001), &status), size_t(0));
    QCOMPARE(status, QDataStream::ReadCorruptData);

    // A hostile count of the names of a certificate is not iterated
    const QByteArray hostileNames = createStream(1, 1, [](QDataStream& stream)
    {
        stream << int(1) << int(pdf::PDFCertificateEntry::EntryType::User);
        stream << int(1) << int32_t(2) << int(2048) << int(pdf::PDFCertificateInfo::KeyRSA);
        stream << (quint64(1) << 40);
    });
    QCOMPARE(readCount(hostileNames, &status), size_t(0));
    QCOMPARE(status, QDataStream::ReadCorruptData);

    // The file: 4 MiB at most, then the data are checked
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString validFile = directory.filePath(QStringLiteral("valid.bin"));
    const QString largeFile = directory.filePath(QStringLiteral("large.bin"));
    {
        QFile file(validFile);
        QVERIFY(file.open(QFile::WriteOnly));
        file.write(valid);
    }
    {
        QFile file(largeFile);
        QVERIFY(file.open(QFile::WriteOnly));
        file.write(valid);
        file.write(QByteArray(pdf::PDFCertificateStore::MAXIMUM_STORE_FILE_SIZE + 1 - valid.size(), char(0)));
    }

    pdf::PDFCertificateStore fileStore;
    QVERIFY(fileStore.loadFromFile(validFile));
    QCOMPARE(fileStore.getCertificates().size(), size_t(1));
    QVERIFY(!fileStore.loadFromFile(largeFile));
    QVERIFY(fileStore.getCertificates().empty());
    QVERIFY(!fileStore.loadFromFile(directory.filePath(QStringLiteral("missing.bin"))));
}

void TimestampClientTest::authorityTrustFact_data()
{
    QTest::addColumn<bool>("isDocumentTimestamp");
    QTest::addColumn<bool>("isTrusted");

    QTest::newRow("timestamp-trusted") << true << true;
    QTest::newRow("timestamp-untrusted") << true << false;
    QTest::newRow("signature-trusted") << false << true;
    QTest::newRow("signature-untrusted") << false << false;
}

void TimestampClientTest::authorityTrustFact()
{
    // RS6SEC M3, plan 5.2 (pdftimestampclient.h): the trust in the authority is no condition of the run,
    // the fact Authority says "Authority not trusted" beside its name, when the certificate of the token
    // does not lead to a trusted certificate of the context (the local test authority: trusted only when
    // the store of the context has its certificate; never the certificates of the system)
    QFETCH(bool, isDocumentTimestamp);
    QFETCH(bool, isTrusted);
    using Tool = pdf::PDFDocumentToolSignature;

    m_authority->setBehavior(TestAuthority::Granted);
    m_transport->setSettings(LocalTransport::Settings());

    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const pdf::PDFDocument plain = builder.build();
    QByteArray data;
    {
        QBuffer buffer(&data);
        QVERIFY(buffer.open(QBuffer::WriteOnly));
        pdf::PDFDocumentWriter writer(nullptr);
        QVERIFY(bool(writer.write(&buffer, &plain)));
    }
    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    const pdf::PDFDocumentPointer document(new pdf::PDFDocument(reader.readFromBuffer(data)));
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);

    pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(document, data, QStringLiteral("contract.pdf"), false));
    context.timestampClient = std::make_shared<pdf::PDFTimestampClient>(m_transport);
    std::shared_ptr<pdf::PDFCertificateStore> store = std::make_shared<pdf::PDFCertificateStore>();
    if (isTrusted)
    {
        QVERIFY(store->add(pdf::PDFCertificateEntry::EntryType::User, m_authority->getCertificate()));
    }
    context.certificateStore = store;

    QVariantMap parameters = { { QString::fromLatin1(Tool::PARAMETER_TSA_URL), QStringLiteral("http://timestamp.digicert.com") } };
    if (!isDocumentTimestamp)
    {
        pdf::PDFCertificateEntry entry;
        entry.pkcs12 = m_pkcs12;
        const std::optional<pdf::PDFCertificateInfo> certificate = pdf::PDFCertificateManager::readCertificate(m_pkcs12, QString::fromUtf8(m_password));
        QVERIFY(certificate);
        entry.info = *certificate;
        context.userCertificates = { entry };
        parameters.insert(QString::fromLatin1(Tool::PARAMETER_CERTIFICATE), pdf::PDFDocumentToolEncryption::getCertificateFingerprint(entry.info.getCertificateData()));
        parameters.insert(QString::fromLatin1(Tool::PARAMETER_CERTIFICATE_PASSWORD), QString::fromUtf8(m_password));
        parameters.insert(QString::fromLatin1(Tool::PARAMETER_TYPE), QString::fromLatin1(Tool::TYPE_SIGNATURE_TIMESTAMP));
        parameters.insert(QString::fromLatin1(Tool::PARAMETER_APPEARANCE), QString::fromLatin1(Tool::APPEARANCE_INVISIBLE));
    }

    const Tool tool(isDocumentTimestamp ? Tool::Mode::Timestamp : Tool::Mode::Sign);
    const pdf::PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const auto fact = std::find_if(result.facts.cbegin(), result.facts.cend(), [](const pdf::PDFDocumentToolFact& item) { return item.id == QLatin1String(Tool::FACT_AUTHORITY); });
    QVERIFY(fact != result.facts.cend());
    const QString name = QStringLiteral("PDF4QT Test Timestamp Authority");
    QCOMPARE(fact->value, isTrusted ? name : name + QStringLiteral(" · Authority not trusted"));
    QCOMPARE(result.data.value(QString::fromLatin1(Tool::DATA_TSA_TRUSTED)).toBool(), isTrusted);
    QCOMPARE(result.data.value(QString::fromLatin1(Tool::DATA_TSA_NAME)).toString(), name);
}

QTEST_GUILESS_MAIN(TimestampClientTest)

#include "tst_timestampclienttest.moc"
