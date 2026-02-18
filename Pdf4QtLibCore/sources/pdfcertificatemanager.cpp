// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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
#include <openssl/ts.h>
#include <openssl/obj_mac.h>

#include <array>
#include <memory>

namespace pdf
{

PDFCertificateManager::PDFCertificateManager()
{

}

template<typename T>
using openssl_ptr = std::unique_ptr<T, void(*)(T*)>;

namespace
{

bool postTimestampRequest(const QByteArray& request, const QString& url, int timeoutMs, QByteArray& response)
{
    QNetworkAccessManager networkAccessManager;
    QNetworkRequest networkRequest{ QUrl(url) };
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/timestamp-query");
    networkRequest.setRawHeader("Accept", "application/timestamp-reply");

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    QNetworkReply* reply = networkAccessManager.post(networkRequest, request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timeoutTimer.start(timeoutMs > 0 ? timeoutMs : 15000);
    loop.exec();

    const bool timedOut = !timeoutTimer.isActive();
    timeoutTimer.stop();

    if (timedOut)
    {
        reply->abort();
        reply->deleteLater();
        return false;
    }

    if (reply->error() != QNetworkReply::NoError)
    {
        reply->deleteLater();
        return false;
    }

    response = reply->readAll();
    reply->deleteLater();
    return !response.isEmpty();
}

bool parsePkcs12(const PDFCertificateEntry& certificateEntry,
                 const QString& password,
                 EVP_PKEY** key,
                 X509** certificate,
                 STACK_OF(X509)** certificates)
{
    QByteArray pkcs12Data = certificateEntry.pkcs12;
    if (pkcs12Data.isEmpty())
    {
        return false;
    }

    openssl_ptr<BIO> pkcs12Buffer(BIO_new(BIO_s_mem()), &BIO_free_all);
    BIO_write(pkcs12Buffer.get(), pkcs12Data.constData(), pkcs12Data.length());

    openssl_ptr<PKCS12> pkcs12(d2i_PKCS12_bio(pkcs12Buffer.get(), nullptr), &PKCS12_free);
    if (!pkcs12)
    {
        return false;
    }

    const char* passwordPointer = nullptr;
    QByteArray passwordByteArray = password.isEmpty() ? QByteArray() : password.toUtf8();
    if (!passwordByteArray.isEmpty())
    {
        passwordPointer = passwordByteArray.constData();
    }

    return PKCS12_parse(pkcs12.get(), passwordPointer, key, certificate, certificates) == 1;
}

} // namespace

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

PDFCertificateEntries PDFCertificateManager::getCertificates()
{
    PDFCertificateEntries entries = PDFCertificateStore::getPersonalCertificates();

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
    EVP_PKEY* key = nullptr;
    X509* certificate = nullptr;
    STACK_OF(X509)* certificates = nullptr;
    if (parsePkcs12(certificateEntry, password, &key, &certificate, &certificates))
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
#ifdef Q_OS_WIN
    else
    {
        return signImpl_Win(certificateEntry, password, data, result);
    }
#endif

    return false;
}

bool PDFSignatureFactory::sign(const PDFCertificateEntry& certificateEntry,
                               QString password,
                               QByteArray data,
                               QByteArray& result,
                               const TimestampSettings& timestampSettings)
{
    EVP_PKEY* key = nullptr;
    X509* certificate = nullptr;
    STACK_OF(X509)* certificates = nullptr;
    if (parsePkcs12(certificateEntry, password, &key, &certificate, &certificates))
    {
        openssl_ptr<BIO> signedDataBuffer(BIO_new(BIO_s_mem()), &BIO_free_all);
        BIO_write(signedDataBuffer.get(), data.constData(), data.length());

        CMS_ContentInfo* cms = CMS_sign(certificate, key, certificates, signedDataBuffer.get(), CMS_DETACHED | CMS_BINARY);
        if (cms)
        {
            STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
            CMS_SignerInfo* signerInfo = signerInfos ? sk_CMS_SignerInfo_value(signerInfos, 0) : nullptr;
            ASN1_OCTET_STRING* signerSignature = signerInfo ? CMS_SignerInfo_get0_signature(signerInfo) : nullptr;

            if (signerSignature)
            {
                QByteArray signatureValue(reinterpret_cast<const char*>(ASN1_STRING_get0_data(signerSignature)),
                                          ASN1_STRING_length(signerSignature));
                QByteArray token;
                if (createTimestampToken(signatureValue, token, timestampSettings))
                {
                    if (CMS_unsigned_add1_attr_by_NID(signerInfo,
                                                      NID_id_smime_aa_timeStampToken,
                                                      V_ASN1_SEQUENCE,
                                                      token.constData(),
                                                      token.size()) == 1)
                    {
                        openssl_ptr<BIO> outputBuffer(BIO_new(BIO_s_mem()), &BIO_free_all);
                        i2d_CMS_bio(outputBuffer.get(), cms);
                        BUF_MEM* cmsMemoryBuffer = nullptr;
                        BIO_get_mem_ptr(outputBuffer.get(), &cmsMemoryBuffer);
                        result = QByteArray(cmsMemoryBuffer->data, int(cmsMemoryBuffer->length));

                        CMS_ContentInfo_free(cms);
                        EVP_PKEY_free(key);
                        X509_free(certificate);
                        sk_X509_free(certificates);
                        return true;
                    }
                }
            }

            CMS_ContentInfo_free(cms);
        }

        EVP_PKEY_free(key);
        X509_free(certificate);
        sk_X509_free(certificates);
        return false;
    }

    return false;
}

bool PDFSignatureFactory::createTimestampToken(QByteArray data,
                                               QByteArray& result,
                                               const TimestampSettings& timestampSettings)
{
    if (timestampSettings.url.isEmpty())
    {
        return false;
    }

    const EVP_MD* digestMethod = EVP_sha256();
    if (!digestMethod)
    {
        return false;
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest = { };
    unsigned int digestSize = 0;
    if (EVP_Digest(data.constData(), data.size(), digest.data(), &digestSize, digestMethod, nullptr) != 1)
    {
        return false;
    }

    openssl_ptr<TS_REQ> request(TS_REQ_new(), &TS_REQ_free);
    openssl_ptr<TS_MSG_IMPRINT> imprint(TS_MSG_IMPRINT_new(), &TS_MSG_IMPRINT_free);
    openssl_ptr<X509_ALGOR> algorithm(X509_ALGOR_new(), &X509_ALGOR_free);
    if (!request || !imprint || !algorithm)
    {
        return false;
    }

    TS_REQ_set_version(request.get(), 1);

    ASN1_OBJECT* algorithmObject = OBJ_nid2obj(EVP_MD_get_type(digestMethod));
    X509_ALGOR_set0(algorithm.get(), algorithmObject, V_ASN1_NULL, nullptr);
    TS_MSG_IMPRINT_set_algo(imprint.get(), algorithm.get());
    algorithm.release();

    TS_MSG_IMPRINT_set_msg(imprint.get(), digest.data(), digestSize);
    TS_REQ_set_msg_imprint(request.get(), imprint.get());
    imprint.release();

    openssl_ptr<ASN1_INTEGER> nonce(ASN1_INTEGER_new(), &ASN1_INTEGER_free);
    if (!nonce)
    {
        return false;
    }
    ASN1_INTEGER_set_uint64(nonce.get(), QRandomGenerator::global()->generate64());
    TS_REQ_set_nonce(request.get(), nonce.get());
    TS_REQ_set_cert_req(request.get(), 1);

    openssl_ptr<BIO> requestBio(BIO_new(BIO_s_mem()), &BIO_free_all);
    if (!requestBio)
    {
        return false;
    }

    if (i2d_TS_REQ_bio(requestBio.get(), request.get()) != 1)
    {
        return false;
    }

    BUF_MEM* requestMemory = nullptr;
    BIO_get_mem_ptr(requestBio.get(), &requestMemory);
    QByteArray requestBytes(requestMemory->data, int(requestMemory->length));

    QByteArray responseBytes;
    if (!postTimestampRequest(requestBytes, timestampSettings.url, timestampSettings.timeoutMs, responseBytes))
    {
        return false;
    }

    const unsigned char* responseBuffer = reinterpret_cast<const unsigned char*>(responseBytes.constData());
    openssl_ptr<TS_RESP> response(d2i_TS_RESP(nullptr, &responseBuffer, responseBytes.size()), &TS_RESP_free);
    if (!response)
    {
        return false;
    }

    TS_STATUS_INFO* statusInfo = TS_RESP_get_status_info(response.get());
    if (!statusInfo)
    {
        return false;
    }

    const ASN1_INTEGER* status = TS_STATUS_INFO_get0_status(statusInfo);
    if (!status)
    {
        return false;
    }

    const int statusValue = ASN1_INTEGER_get(status);
    if (statusValue != TS_STATUS_GRANTED && statusValue != TS_STATUS_GRANTED_WITH_MODS)
    {
        return false;
    }

    PKCS7* token = TS_RESP_get_token(response.get());
    if (!token)
    {
        return false;
    }

    // Verify token against the same input data (message imprint check).
    TS_VERIFY_CTX* verifyContext = TS_VERIFY_CTX_new();
    if (!verifyContext)
    {
        return false;
    }
    TS_VERIFY_CTX_init(verifyContext);
    BIO* verifyDataBio = BIO_new_mem_buf(data.constData(), data.size());
    if (!verifyDataBio)
    {
        TS_VERIFY_CTX_cleanup(verifyContext);
        TS_VERIFY_CTX_free(verifyContext);
        return false;
    }
    TS_VERIFY_CTX_set0_data(verifyContext, verifyDataBio);
    TS_VERIFY_CTX_set_flags(verifyContext, TS_VFY_DATA);
    const int verifyResult = TS_RESP_verify_token(verifyContext, token);
    TS_VERIFY_CTX_cleanup(verifyContext);
    TS_VERIFY_CTX_free(verifyContext);
    if (verifyResult != 1)
    {
        return false;
    }

    openssl_ptr<BIO> tokenOutput(BIO_new(BIO_s_mem()), &BIO_free_all);
    if (!tokenOutput)
    {
        return false;
    }

    if (i2d_PKCS7_bio(tokenOutput.get(), token) != 1)
    {
        return false;
    }

    BUF_MEM* tokenMemory = nullptr;
    BIO_get_mem_ptr(tokenOutput.get(), &tokenMemory);
    result = QByteArray(tokenMemory->data, int(tokenMemory->length));
    return !result.isEmpty();
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
