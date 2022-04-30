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

#include <openssl/bio.h>
#include <openssl/rsa.h>
#include <openssl/rsaerr.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

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
    openssl_ptr<BIO> certificateBuffer(BIO_new(BIO_s_mem()), &BIO_free_all);
    openssl_ptr<BIO> privateKeyBuffer(BIO_new(BIO_s_mem()), &BIO_free_all);

    if (certificateBuffer && privateKeyBuffer)
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
            X509_gmtime_adj(X509_getm_notBefore(certificate.get()), info.validityInSeconds);

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
            X509V3_CTX context;
            X509V3_set_ctx_nodb(&context);
            X509V3_set_ctx(&context, certificate.get(), certificate.get(), nullptr, nullptr, 0);
            extension = X509V3_EXT_conf_nid (NULL, &context, NID_key_usage, "digitalSignature, keyAgreement");

            X509_set_issuer_name(certificate.get(), name);

            // Set public key
            X509_set_pubkey(certificate.get(), privateKey.get());
            X509_sign(certificate.get(), privateKey.get(), EVP_sha512());

            // Private key password
            QByteArray privateKeyPaswordUtf8 = info.privateKeyPasword.toUtf8();

            // Write the data
            const int retWritePrivateKey = PEM_write_bio_PKCS8PrivateKey(privateKeyBuffer.get(), privateKey.get(), EVP_aes_256_cbc(), privateKeyPaswordUtf8.data(), privateKeyPaswordUtf8.size(), nullptr, nullptr);
            const int retWriteCertificate = PEM_write_bio_X509(certificateBuffer.get(), certificate.get());

            if (retWritePrivateKey == 1 && retWriteCertificate == 1)
            {

            }
        }
    }
}

}   // namespace pdfplugin
