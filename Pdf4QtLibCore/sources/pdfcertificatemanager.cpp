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

#include <QDir>
#include <QFile>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include "pdfdbgheap.h"

#if defined(PDF4QT_COMPILER_MINGW) || defined(PDF4QT_COMPILER_GCC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#if defined(PDF4QT_COMPILER_MSVC)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

#include <openssl/bio.h>
#include <openssl/rsa.h>
#include <openssl/rsaerr.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <openssl/pkcs12.h>
#include <openssl/cms.h>
#include <openssl/objects.h>
#include <openssl/ts.h>

#include <array>
#include <memory>

namespace pdf
{

PDFCertificateManager::PDFCertificateManager()
{

}

template<typename T>
using openssl_ptr = std::unique_ptr<T, void(*)(T*)>;

void PDFCertificateManager::createCertificate(const NewCertificateInfo& info)
{
    openssl_ptr<BIO> pksBuffer(BIO_new(BIO_s_mem()), &BIO_free_all);

    if (pksBuffer)
    {
        openssl_ptr<BIGNUM> bignumber(BN_new(), &BN_free);
        openssl_ptr<RSA> rsaKey(RSA_new(), &RSA_free);

        BN_set_word(bignumber.get(), RSA_F4);
        const int rsaResult = RSA_generate_key_ex(rsaKey.get(), info.rsaKeyLength, bignumber.get(), nullptr);
        if (rsaResult)
        {
            openssl_ptr<X509> certificate(X509_new(), &X509_free);
            openssl_ptr<EVP_PKEY> privateKey(EVP_PKEY_new(), &EVP_PKEY_free);

            EVP_PKEY_set1_RSA(privateKey.get(), rsaKey.get());
            ASN1_INTEGER* serialNumber = X509_get_serialNumber(certificate.get());
            ASN1_INTEGER_set(serialNumber, info.serialNumber);

            // Set validity of the certificate
            X509_gmtime_adj(X509_getm_notBefore(certificate.get()), 0);
            X509_gmtime_adj(X509_getm_notAfter(certificate.get()), info.validityInSeconds);

            // Set name
            X509_NAME* name = X509_get_subject_name(certificate.get());

            auto addString = [name](const char* identifier, QString string)
            {
                if (string.isEmpty())
                {
                    return;
                }

                QByteArray stringUtf8 = string.toUtf8();
                X509_NAME_add_entry_by_txt(name, identifier, MBSTRING_UTF8, reinterpret_cast<const unsigned char*>(stringUtf8.constData()), stringUtf8.length(), -1, 0);
            };
            addString("C", info.certCountryCode);
            addString("O", info.certOrganization);
            addString("OU", info.certOrganizationUnit);
            addString("CN", info.certCommonName);
            addString("E", info.certEmail);

            X509_EXTENSION* extension = nullptr;
            X509V3_CTX context = { };
            X509V3_set_ctx_nodb(&context);
            X509V3_set_ctx(&context, certificate.get(), certificate.get(), nullptr, nullptr, 0);
            extension = X509V3_EXT_conf_nid (NULL, &context, NID_key_usage, "digitalSignature, keyAgreement");
            X509_add_ext(certificate.get(), extension, -1);
            X509_EXTENSION_free(extension);

            X509_set_issuer_name(certificate.get(), name);

            // Set public key
            X509_set_pubkey(certificate.get(), privateKey.get());
            X509_sign(certificate.get(), privateKey.get(), EVP_sha512());

            // Private key password
            QByteArray privateKeyPaswordUtf8 = info.privateKeyPasword.toUtf8();

            // Write the data
            openssl_ptr<PKCS12> pkcs12(PKCS12_create(privateKeyPaswordUtf8.constData(),
                                                     nullptr,
                                                     privateKey.get(),
                                                     certificate.get(),
                                                     nullptr,
                                                     0,
                                                     0,
                                                     PKCS12_DEFAULT_ITER,
                                                     PKCS12_DEFAULT_ITER,
                                                     0), &PKCS12_free);
            i2d_PKCS12_bio(pksBuffer.get(), pkcs12.get());

            BUF_MEM* pksMemoryBuffer = nullptr;
            BIO_get_mem_ptr(pksBuffer.get(), &pksMemoryBuffer);

            if (!info.fileName.isEmpty())
            {
                QFile file(info.fileName);
                if (file.open(QFile::WriteOnly | QFile::Truncate))
                {
                    file.write(pksMemoryBuffer->data, pksMemoryBuffer->length);
                    file.close();
                }
            }
        }
    }
}

PDFCertificateEntries PDFCertificateManager::getCertificates(PDFCertificateUsageFilter filter)
{
    PDFCertificateEntries entries = PDFCertificateStore::getPersonalCertificates(filter);

    QDir directory(getCertificateDirectory());
    QFileInfoList pfxFiles = directory.entryInfoList(QStringList() << "*.pfx", QDir::Files | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);

    for (const QFileInfo& fileInfo : pfxFiles)
    {
        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QFile::ReadOnly))
        {
            QByteArray data = file.readAll();

            openssl_ptr<BIO> pksBuffer(BIO_new(BIO_s_mem()), &BIO_free_all);
            BIO_write(pksBuffer.get(), data.constData(), data.length());

            openssl_ptr<PKCS12> pkcs12(d2i_PKCS12_bio(pksBuffer.get(), nullptr), &PKCS12_free);
            if (pkcs12)
            {
                X509* certificatePtr = nullptr;

                PDFCertificateEntry entry;

                // Parse PKCS12 with password
                bool isParsed = PKCS12_parse(pkcs12.get(), nullptr, nullptr, &certificatePtr, nullptr) == 1;
                if (isParsed)
                {
                    std::optional<PDFCertificateInfo> info = PDFCertificateInfo::getCertificateInfo(certificatePtr);
                    if (info)
                    {
                        entry.type = PDFCertificateEntry::EntryType::System;
                        entry.info = qMove(*info);
                    }
                }

                if (certificatePtr)
                {
                    X509_free(certificatePtr);
                }

                entry.pkcs12 = data;
                entry.pkcs12fileName = fileInfo.fileName();
                entries.emplace_back(qMove(entry));
            }

            file.close();
        }
    }

    return entries;
}

QString PDFCertificateManager::getCertificateDirectory()
{
    QString standardDataLocation = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).front();
    QDir directory(standardDataLocation + "/certificates/");
    return directory.absolutePath();
}

QString PDFCertificateManager::generateCertificateFileName()
{
    QString directoryString = getCertificateDirectory();
    QDir directory(directoryString);

    int certificateIndex = 1;
    while (true)
    {
        QString fileName = directory.absoluteFilePath(QString("cert_%1.pfx").arg(certificateIndex++));
        if (!QFile::exists(fileName))
        {
            return fileName;
        }
    }

    return QString();
}

bool PDFCertificateManager::isCertificateValid(const PDFCertificateEntry& certificateEntry, QString password)
{
    QByteArray pkcs12data = certificateEntry.pkcs12;

    openssl_ptr<BIO> pksBuffer(BIO_new(BIO_s_mem()), &BIO_free_all);
    BIO_write(pksBuffer.get(), pkcs12data.constData(), pkcs12data.length());

    openssl_ptr<PKCS12> pkcs12(d2i_PKCS12_bio(pksBuffer.get(), nullptr), &PKCS12_free);
    if (pkcs12)
    {
        const char* passwordPointer = nullptr;
        QByteArray passwordByteArray = password.isEmpty() ? QByteArray() : password.toUtf8();
        if (!passwordByteArray.isEmpty())
        {
            passwordPointer = passwordByteArray.constData();
        }

        return PKCS12_parse(pkcs12.get(), passwordPointer, nullptr, nullptr, nullptr) == 1;
    }

    return pkcs12data.isEmpty();
}

bool PDFSignatureFactory::sign(const PDFCertificateEntry& certificateEntry,
                               QString password,
                               QByteArray data,
                               QByteArray& result)
{
    QByteArray pkcs12Data = certificateEntry.pkcs12;

    if (!pkcs12Data.isEmpty())
    {
        openssl_ptr<BIO> pkcs12Buffer(BIO_new(BIO_s_mem()), &BIO_free_all);
        BIO_write(pkcs12Buffer.get(), pkcs12Data.constData(), pkcs12Data.length());

        openssl_ptr<PKCS12> pkcs12(d2i_PKCS12_bio(pkcs12Buffer.get(), nullptr), &PKCS12_free);
        if (pkcs12)
        {
            const char* passwordPointer = nullptr;
            QByteArray passwordByteArray = password.isEmpty() ? QByteArray() : password.toUtf8();
            if (!passwordByteArray.isEmpty())
            {
                passwordPointer = passwordByteArray.constData();
            }

            EVP_PKEY* key = nullptr;
            X509* certificate = nullptr;
            STACK_OF(X509)* certificates = nullptr;
            if (PKCS12_parse(pkcs12.get(), passwordPointer, &key, &certificate, &certificates) == 1)
            {
                openssl_ptr<BIO> signedDataBuffer(BIO_new(BIO_s_mem()), &BIO_free_all);
                BIO_write(signedDataBuffer.get(), data.constData(), data.length());

                PKCS7* signature = PKCS7_sign(certificate, key, certificates, signedDataBuffer.get(), PKCS7_DETACHED | PKCS7_BINARY);
                if (signature)
                {
                    openssl_ptr<BIO> outputBuffer(BIO_new(BIO_s_mem()), &BIO_free_all);
                    i2d_PKCS7_bio(outputBuffer.get(), signature);

                    BUF_MEM* pksMemoryBuffer = nullptr;
                    BIO_get_mem_ptr(outputBuffer.get(), &pksMemoryBuffer);

                    result = QByteArray(pksMemoryBuffer->data, int(pksMemoryBuffer->length));

                    PKCS7_free(signature);
                    EVP_PKEY_free(key);
                    X509_free(certificate);
                    sk_X509_free(certificates);
                    return true;
                }

                EVP_PKEY_free(key);
                X509_free(certificate);
                sk_X509_free(certificates);
                return false;
            }
        }
    }
#ifdef Q_OS_WIN
    else
    {
        return signImpl_Win(certificateEntry, password, data, result);
    }
#endif

    return false;
}

namespace
{

/// Sends the RFC 3161 timestamp request to the timestamp authority and reads
/// its response. Returns false, when the authority cannot be contacted or it
/// does not answer with a timestamp response.
bool postTimestampRequest(const QByteArray& requestData,
                          const QString& url,
                          int timeoutMilliseconds,
                          QByteArray& response,
                          QString& errorMessage)
{
    const QUrl timestampUrl(url);
    if (!timestampUrl.isValid() || timestampUrl.scheme().isEmpty())
    {
        errorMessage = PDFSignatureFactory::tr("Url '%1' of the timestamp authority is not valid.").arg(url);
        return false;
    }

    const int timeout = (timeoutMilliseconds > 0) ? timeoutMilliseconds : 15000;

    QNetworkAccessManager networkAccessManager;
    QNetworkRequest networkRequest(timestampUrl);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/timestamp-query"));
    networkRequest.setRawHeader("Accept", "application/timestamp-reply");
    networkRequest.setTransferTimeout(timeout);

    QNetworkReply* reply = networkAccessManager.post(networkRequest, requestData);

    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);

    // Jakub Melka: the transfer timeout limits the time between the transferred
    // chunks only, so we use a timer to limit the duration of the whole request.
    QTimer deadlineTimer;
    deadlineTimer.setSingleShot(true);
    QObject::connect(&deadlineTimer, &QTimer::timeout, reply, &QNetworkReply::abort);
    deadlineTimer.start(timeout);

    // User input is excluded, because the document is being signed and it must
    // not be modified until the signing is finished.
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);

    const QNetworkReply::NetworkError error = reply->error();
    const QString errorString = reply->errorString();
    const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    response = reply->readAll();
    reply->deleteLater();

    if (error != QNetworkReply::NoError)
    {
        errorMessage = PDFSignatureFactory::tr("Timestamp authority '%1' cannot be contacted. %2").arg(url, errorString);
        return false;
    }

    if (statusCode.isValid() && statusCode.toInt() != 200)
    {
        errorMessage = PDFSignatureFactory::tr("Timestamp authority '%1' answered with the http status code %2.").arg(url).arg(statusCode.toInt());
        return false;
    }

    if (response.isEmpty())
    {
        errorMessage = PDFSignatureFactory::tr("Timestamp authority '%1' answered with an empty response.").arg(url);
        return false;
    }

    return true;
}

}   // namespace

bool PDFSignatureFactory::signWithTimestamp(const PDFCertificateEntry& certificateEntry,
                                            QString password,
                                            QByteArray data,
                                            const TimestampSettings& timestampSettings,
                                            QByteArray& result,
                                            QString& errorMessage)
{
    errorMessage.clear();

    QByteArray signature;
    if (!sign(certificateEntry, password, data, signature) || signature.isEmpty())
    {
        // Jakub Melka: the failure of the signing itself is reported by the caller,
        // because it is not caused by the timestamp.
        return false;
    }

    return addTimestampToSignature(signature, timestampSettings, result, errorMessage);
}

bool PDFSignatureFactory::addTimestampToSignature(const QByteArray& signature,
                                                  const TimestampSettings& timestampSettings,
                                                  QByteArray& result,
                                                  QString& errorMessage)
{
    // Jakub Melka: the signature is a CMS/PKCS#7 signed data object. We decode it,
    // let the timestamp authority timestamp the signature value of its signer and
    // store the returned token as an unsigned attribute of the signer, which is
    // the way the signature timestamps are carried (RFC 3161, appendix A).
    const unsigned char* signatureBuffer = reinterpret_cast<const unsigned char*>(signature.constData());
    openssl_ptr<CMS_ContentInfo> cms(d2i_CMS_ContentInfo(nullptr, &signatureBuffer, signature.size()), &CMS_ContentInfo_free);
    if (!cms)
    {
        errorMessage = tr("Signature to be timestamped cannot be decoded.");
        return false;
    }

    STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms.get());
    CMS_SignerInfo* signerInfo = (signerInfos && sk_CMS_SignerInfo_num(signerInfos) > 0) ? sk_CMS_SignerInfo_value(signerInfos, 0) : nullptr;
    ASN1_OCTET_STRING* signerSignature = signerInfo ? CMS_SignerInfo_get0_signature(signerInfo) : nullptr;
    if (!signerSignature)
    {
        errorMessage = tr("Signature to be timestamped has no signer.");
        return false;
    }

    const QByteArray signatureValue(reinterpret_cast<const char*>(ASN1_STRING_get0_data(signerSignature)),
                                    ASN1_STRING_length(signerSignature));

    QByteArray token;
    if (!createTimestampToken(signatureValue, timestampSettings, token, errorMessage))
    {
        return false;
    }

    if (CMS_unsigned_add1_attr_by_NID(signerInfo, NID_id_smime_aa_timeStampToken, V_ASN1_SEQUENCE, token.constData(), int(token.size())) != 1)
    {
        errorMessage = tr("Timestamp cannot be added to the signature.");
        return false;
    }

    openssl_ptr<BIO> outputBuffer(BIO_new(BIO_s_mem()), &BIO_free_all);
    if (!outputBuffer || i2d_CMS_bio(outputBuffer.get(), cms.get()) != 1)
    {
        errorMessage = tr("Timestamped signature cannot be encoded.");
        return false;
    }

    BUF_MEM* memoryBuffer = nullptr;
    BIO_get_mem_ptr(outputBuffer.get(), &memoryBuffer);
    result = QByteArray(memoryBuffer->data, int(memoryBuffer->length));

    if (result.isEmpty())
    {
        errorMessage = tr("Timestamped signature cannot be encoded.");
        return false;
    }

    return true;
}

bool PDFSignatureFactory::createTimestampToken(QByteArray data,
                                               const TimestampSettings& timestampSettings,
                                               QByteArray& result,
                                               QString& errorMessage)
{
    result.clear();
    errorMessage.clear();

    if (timestampSettings.url.isEmpty())
    {
        errorMessage = tr("Url of the timestamp authority is not set.");
        return false;
    }

    const EVP_MD* digestAlgorithm = EVP_sha256();
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest = { };
    unsigned int digestSize = 0;
    if (!digestAlgorithm || EVP_Digest(data.constData(), size_t(data.size()), digest.data(), &digestSize, digestAlgorithm, nullptr) != 1)
    {
        errorMessage = tr("Digest of the timestamped data cannot be calculated.");
        return false;
    }

    openssl_ptr<TS_REQ> request(TS_REQ_new(), &TS_REQ_free);
    openssl_ptr<TS_MSG_IMPRINT> imprint(TS_MSG_IMPRINT_new(), &TS_MSG_IMPRINT_free);
    openssl_ptr<X509_ALGOR> algorithm(X509_ALGOR_new(), &X509_ALGOR_free);
    openssl_ptr<ASN1_INTEGER> nonce(ASN1_INTEGER_new(), &ASN1_INTEGER_free);
    openssl_ptr<BIO> requestBuffer(BIO_new(BIO_s_mem()), &BIO_free_all);

    if (!request || !imprint || !algorithm || !nonce || !requestBuffer)
    {
        errorMessage = tr("Timestamp request cannot be created.");
        return false;
    }

    // Jakub Melka: functions TS_MSG_IMPRINT_set_algo, TS_REQ_set_msg_imprint and
    // TS_REQ_set_nonce copy their arguments, so the objects created here stay
    // owned by the smart pointers and are released by them.
    X509_ALGOR_set0(algorithm.get(), OBJ_nid2obj(EVP_MD_get_type(digestAlgorithm)), V_ASN1_NULL, nullptr);
    ASN1_INTEGER_set_uint64(nonce.get(), QRandomGenerator::global()->generate64());

    if (TS_REQ_set_version(request.get(), 1) != 1 ||
        TS_MSG_IMPRINT_set_algo(imprint.get(), algorithm.get()) != 1 ||
        TS_MSG_IMPRINT_set_msg(imprint.get(), digest.data(), int(digestSize)) != 1 ||
        TS_REQ_set_msg_imprint(request.get(), imprint.get()) != 1 ||
        TS_REQ_set_nonce(request.get(), nonce.get()) != 1 ||
        TS_REQ_set_cert_req(request.get(), 1) != 1)
    {
        errorMessage = tr("Timestamp request cannot be created.");
        return false;
    }

    if (i2d_TS_REQ_bio(requestBuffer.get(), request.get()) != 1)
    {
        errorMessage = tr("Timestamp request cannot be encoded.");
        return false;
    }

    BUF_MEM* requestMemoryBuffer = nullptr;
    BIO_get_mem_ptr(requestBuffer.get(), &requestMemoryBuffer);
    const QByteArray requestData(requestMemoryBuffer->data, int(requestMemoryBuffer->length));

    QByteArray responseData;
    if (!postTimestampRequest(requestData, timestampSettings.url, timestampSettings.timeoutMilliseconds, responseData, errorMessage))
    {
        return false;
    }

    const unsigned char* responseBuffer = reinterpret_cast<const unsigned char*>(responseData.constData());
    openssl_ptr<TS_RESP> response(d2i_TS_RESP(nullptr, &responseBuffer, responseData.size()), &TS_RESP_free);
    if (!response)
    {
        errorMessage = tr("Answer of the timestamp authority '%1' is not a timestamp response.").arg(timestampSettings.url);
        return false;
    }

    TS_STATUS_INFO* statusInfo = TS_RESP_get_status_info(response.get());
    const ASN1_INTEGER* status = statusInfo ? TS_STATUS_INFO_get0_status(statusInfo) : nullptr;
    const long statusValue = status ? ASN1_INTEGER_get(status) : -1;
    if (statusValue != TS_STATUS_GRANTED && statusValue != TS_STATUS_GRANTED_WITH_MODS)
    {
        errorMessage = tr("Timestamp authority '%1' rejected the timestamp request, status is %2.").arg(timestampSettings.url).arg(statusValue);
        return false;
    }

    // Jakub Melka: the token is owned by the response, it must not be released here.
    PKCS7* token = TS_RESP_get_token(response.get());
    if (!token)
    {
        errorMessage = tr("Timestamp authority '%1' did not answer with a timestamp token.").arg(timestampSettings.url);
        return false;
    }

    // The token must answer our request - the nonce must be repeated in it, so
    // a token of some other data cannot be replayed to us, and the authority
    // must have used the hash algorithm we have asked for (RFC 3161, chapter
    // 2.4.2).
    bool isAnswerOfRequest = false;
    if (TS_TST_INFO* info = PKCS7_to_TS_TST_INFO(token))
    {
        const ASN1_INTEGER* responseNonce = TS_TST_INFO_get_nonce(info);
        const bool isNonceValid = responseNonce && ASN1_INTEGER_cmp(responseNonce, nonce.get()) == 0;

        TS_MSG_IMPRINT* responseImprint = TS_TST_INFO_get_msg_imprint(info);
        X509_ALGOR* responseAlgorithm = responseImprint ? TS_MSG_IMPRINT_get_algo(responseImprint) : nullptr;
        const ASN1_OBJECT* responseAlgorithmObject = nullptr;
        if (responseAlgorithm)
        {
            X509_ALGOR_get0(&responseAlgorithmObject, nullptr, nullptr, responseAlgorithm);
        }
        const bool isAlgorithmValid = responseAlgorithmObject && OBJ_obj2nid(responseAlgorithmObject) == EVP_MD_get_type(digestAlgorithm);

        isAnswerOfRequest = isNonceValid && isAlgorithmValid;
        TS_TST_INFO_free(info);
    }

    if (!isAnswerOfRequest)
    {
        errorMessage = tr("Timestamp token of the authority '%1' does not belong to the timestamp request.").arg(timestampSettings.url);
        return false;
    }

    openssl_ptr<BIO> tokenBuffer(BIO_new(BIO_s_mem()), &BIO_free_all);
    if (!tokenBuffer || i2d_PKCS7_bio(tokenBuffer.get(), token) != 1)
    {
        errorMessage = tr("Timestamp token of the authority '%1' cannot be encoded.").arg(timestampSettings.url);
        return false;
    }

    BUF_MEM* tokenMemoryBuffer = nullptr;
    BIO_get_mem_ptr(tokenBuffer.get(), &tokenMemoryBuffer);
    QByteArray tokenData(tokenMemoryBuffer->data, int(tokenMemoryBuffer->length));

    if (tokenData.isEmpty() || !verifyTimestampToken(data, tokenData))
    {
        errorMessage = tr("Timestamp token of the authority '%1' does not match the timestamped data.").arg(timestampSettings.url);
        return false;
    }

    result = qMove(tokenData);
    return true;
}

bool PDFSignatureFactory::verifyTimestampToken(const QByteArray& data, const QByteArray& token)
{
    const unsigned char* tokenBuffer = reinterpret_cast<const unsigned char*>(token.constData());
    openssl_ptr<PKCS7> tokenObject(d2i_PKCS7(nullptr, &tokenBuffer, token.size()), &PKCS7_free);
    if (!tokenObject || !PKCS7_type_is_signed(tokenObject.get()) || !tokenObject->d.sign)
    {
        return false;
    }

    TS_VERIFY_CTX* verifyContext = TS_VERIFY_CTX_new();
    if (!verifyContext)
    {
        return false;
    }

    // Jakub Melka: the timestamp authority is not trusted here - the trust in it
    // is decided when the signatures of the document are verified, where the
    // trusted certificates are known. We check only, that the token really is a
    // correctly signed timestamp of the data (RFC 3161, chapter 2.4.2), so the
    // certificate, which signed the token, is used as the only trusted one. The
    // signature of the token, the certificate identifier stored in it and the
    // extended key usage of the certificate of the authority are verified this
    // way, its issuers are not - they are usually not all present in the token
    // and verifying them here would mean deciding the trust.
    STACK_OF(X509)* tokenCertificates = tokenObject->d.sign->cert;
    STACK_OF(X509)* signers = PKCS7_get0_signers(tokenObject.get(), tokenCertificates, 0);
    X509* signerCertificate = (signers && sk_X509_num(signers) > 0) ? sk_X509_value(signers, 0) : nullptr;
    sk_X509_free(signers);

    X509_STORE* store = X509_STORE_new();
    STACK_OF(X509)* certificates = sk_X509_new_null();
    bool isContextInitialized = store && certificates && signerCertificate;

    if (isContextInitialized)
    {
        // The certificate of the authority ends the chain, its issuers are not verified.
        X509_STORE_set_flags(store, X509_V_FLAG_PARTIAL_CHAIN);
        X509_STORE_add_cert(store, signerCertificate);

        const int certificateCount = sk_X509_num(tokenCertificates);
        for (int i = 0; i < certificateCount; ++i)
        {
            X509* certificate = sk_X509_value(tokenCertificates, i);
            X509_up_ref(certificate);
            sk_X509_push(certificates, certificate);
        }
    }

    BIO* dataBuffer = BIO_new_mem_buf(data.constData(), int(data.size()));
    isContextInitialized = isContextInitialized && dataBuffer;

    // All objects given to the context are owned by it and are released by it
    TS_VERIFY_CTX_set0_store(verifyContext, store);
    TS_VERIFY_CTX_set0_certs(verifyContext, certificates);
    TS_VERIFY_CTX_set0_data(verifyContext, dataBuffer);
    TS_VERIFY_CTX_set_flags(verifyContext, TS_VFY_SIGNATURE | TS_VFY_VERSION | TS_VFY_DATA);

    const int verifyResult = isContextInitialized ? TS_RESP_verify_token(verifyContext, tokenObject.get()) : 0;
    TS_VERIFY_CTX_free(verifyContext);

    return verifyResult == 1;
}

}   // namespace pdf

#ifdef Q_OS_WIN
#include <Windows.h>
#include <wincrypt.h>
#include <ncrypt.h>
#if defined(PDF4QT_USE_PRAGMA_LIB)
#pragma comment(lib, "crypt32.lib")
#endif
#endif

bool pdf::PDFSignatureFactory::signImpl_Win(const pdf::PDFCertificateEntry& certificateEntry, QString password, QByteArray data, QByteArray& result)
{
    bool success = false;

#ifdef Q_OS_WIN
    Q_UNUSED(password);

    HCERTSTORE certStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, NULL, CERT_SYSTEM_STORE_CURRENT_USER, L"MY");
    if (certStore)
    {
        PCCERT_CONTEXT pCertContext = nullptr;

        while (pCertContext = CertEnumCertificatesInStore(certStore, pCertContext))
        {
            const unsigned char* pointer = pCertContext->pbCertEncoded;
            QByteArray testData(reinterpret_cast<const char*>(pointer), pCertContext->cbCertEncoded);

            if (testData == certificateEntry.info.getCertificateData())
            {
                break;
            }
        }

        if (pCertContext)
        {
            CRYPT_SIGN_MESSAGE_PARA SignParams{};
            BYTE* pbSignedBlob = nullptr;
            DWORD cbSignedBlob = 0;
            PCCERT_CONTEXT pCertContextArray[1] = { pCertContext };

            const BYTE* pbDataToBeSigned = (const BYTE*)data.constData();
            DWORD cbDataToBeSigned = (DWORD)data.size();

            // Nastavení parametrů pro podpis
            SignParams.cbSize = sizeof(CRYPT_SIGN_MESSAGE_PARA);
            SignParams.dwMsgEncodingType = PKCS_7_ASN_ENCODING | X509_ASN_ENCODING;
            SignParams.pSigningCert = pCertContext;
            SignParams.HashAlgorithm.pszObjId = (LPSTR)szOID_RSA_SHA256RSA;
            SignParams.HashAlgorithm.Parameters.cbData = 0;
            SignParams.HashAlgorithm.Parameters.pbData = NULL;
            SignParams.cMsgCert = 1;
            SignParams.rgpMsgCert = pCertContextArray;
            pCertContextArray[0] = pCertContext;

            const BYTE* rgpbToBeSigned[1] = {pbDataToBeSigned};
            DWORD rgcbToBeSigned[1] = {cbDataToBeSigned};

            // Retrieve signed message size
            CryptSignMessage(
                &SignParams,
                TRUE,
                1,
                rgpbToBeSigned,
                rgcbToBeSigned,
                NULL,
                &cbSignedBlob
                );

            pbSignedBlob = new BYTE[cbSignedBlob];

            // Create digital signature
            if (CryptSignMessage(
                    &SignParams,
                    TRUE,
                    1,
                    rgpbToBeSigned,
                    rgcbToBeSigned,
                    pbSignedBlob,
                    &cbSignedBlob
                    ))
            {
                result = QByteArray((const char*)pbSignedBlob, cbSignedBlob);
                success = true;
            }

            delete[] pbSignedBlob;

            CertFreeCertificateContext(pCertContext);
        }

        CertCloseStore(certStore, CERT_CLOSE_STORE_FORCE_FLAG);
    }
#else
    Q_UNUSED(certificateEntry);
    Q_UNUSED(password);
    Q_UNUSED(data);
    Q_UNUSED(result);
#endif

    return success;
}

#if defined(PDF4QT_COMPILER_MINGW) || defined(PDF4QT_COMPILER_GCC)
#pragma GCC diagnostic pop
#endif

#if defined(PDF4QT_COMPILER_MSVC)
#pragma warning(pop)
#endif
