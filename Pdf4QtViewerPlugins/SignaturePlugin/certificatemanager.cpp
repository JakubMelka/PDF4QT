//    Copyright (C) 2022 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "certificatemanager.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <openssl/bio.h>
#include <openssl/rsa.h>
#include <openssl/rsaerr.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <openssl/pkcs12.h>

#include <memory>

namespace pdfplugin
{

CertificateManager::CertificateManager()
{

}

template<typename T>
using openssl_ptr = std::unique_ptr<T, void(*)(T*)>;

void CertificateManager::createCertificate(const NewCertificateInfo& info)
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

QFileInfoList CertificateManager::getCertificates()
{
    QDir directory(getCertificateDirectory());
    return directory.entryInfoList(QStringList() << "*.pfx", QDir::Files | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
}

QString CertificateManager::getCertificateDirectory()
{
    QDir directory(QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).front() + "/certificates/");
    return directory.absolutePath();
}

QString CertificateManager::generateCertificateFileName()
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

bool CertificateManager::isCertificateValid(QString fileName, QString password)
{
    QFile file(fileName);
    if (file.open(QFile::ReadOnly))
    {
        QByteArray data = file.readAll();
        file.close();

        openssl_ptr<BIO> pksBuffer(BIO_new(BIO_s_mem()), &BIO_free_all);
        BIO_write(pksBuffer.get(), data.constData(), data.length());

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
    }

    return false;
}

}   // namespace pdfplugin
