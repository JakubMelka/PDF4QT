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

#include "pdfcertificatestore.h"
#include "pdfsecurityhandler.h"
#include "pdfutils.h"

#if defined(PDF4QT_COMPILER_MINGW) || defined(PDF4QT_COMPILER_GCC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#if defined(PDF4QT_COMPILER_MSVC)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/rsaerr.h>
#include <openssl/ts.h>
#include <openssl/tserr.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QDataStream>
#include <QStandardPaths>
#include <QDomDocument>

#include "pdfdbgheap.h"

#include <array>
#ifdef Q_OS_UNIX
#include <time.h>
#endif

namespace pdf
{

QRecursiveMutex PDFOpenSSLGlobalLock::s_globalOpenSSLMutex;

PDFOpenSSLGlobalLock::PDFOpenSSLGlobalLock() :
    m_mutexLocker(&s_globalOpenSSLMutex)
{

}

void PDFCertificateEntry::serialize(QDataStream& stream) const
{
    stream << persist_version;
    stream << type;
    stream << info;
}

void PDFCertificateEntry::deserialize(QDataStream& stream)
{
    int persistVersionDeserialized = 0;
    stream >> persistVersionDeserialized;
    if (persistVersionDeserialized != persist_version)
    {
        // B131: another version is not read as this one
        stream.setStatus(QDataStream::ReadCorruptData);
        return;
    }

    stream >> type;
    stream >> info;
}


void PDFCertificateInfo::serialize(QDataStream& stream) const
{
    stream << persist_version;
    stream << m_version;
    stream << m_keySize;
    stream << m_publicKey;
    stream << m_nameEntries;
    stream << m_notValidBefore;
    stream << m_notValidAfter;
    stream << m_keyUsage;
    stream << m_certificateData;
}

void PDFCertificateInfo::deserialize(QDataStream& stream)
{
    int persistVersionDeserialized = 0;
    stream >> persistVersionDeserialized;
    if (persistVersionDeserialized != persist_version)
    {
        // B131: another version is not read as this one
        stream.setStatus(QDataStream::ReadCorruptData);
        return;
    }

    stream >> m_version;
    stream >> m_keySize;
    stream >> m_publicKey;

    // B131: the count of the names is read by its own, a hostile count is never iterated (a file of an
    // older or newer build may have a few names less or more: the missing ones are empty, the extra ones
    // are skipped - as the generic reader of std::array did)
    constexpr std::array<QString, NameEnd>::size_type MAXIMUM_NAME_COUNT = 64;
    std::array<QString, NameEnd>::size_type nameCount = 0;
    stream >> nameCount;
    if (stream.status() != QDataStream::Ok || nameCount > MAXIMUM_NAME_COUNT)
    {
        stream.setStatus(QDataStream::ReadCorruptData);
        return;
    }

    for (std::array<QString, NameEnd>::size_type i = 0; i < nameCount; ++i)
    {
        QString name;
        stream >> name;
        if (i < m_nameEntries.size())
        {
            m_nameEntries[i] = std::move(name);
        }
    }

    for (std::array<QString, NameEnd>::size_type i = nameCount; i < m_nameEntries.size(); ++i)
    {
        m_nameEntries[i].clear();
    }

    stream >> m_notValidBefore;
    stream >> m_notValidAfter;
    stream >> m_keyUsage;
    stream >> m_certificateData;
}

QDateTime PDFCertificateInfo::getNotValidBefore() const
{
    return m_notValidBefore;
}

void PDFCertificateInfo::setNotValidBefore(const QDateTime& notValidBefore)
{
    m_notValidBefore = notValidBefore;
}

QDateTime PDFCertificateInfo::getNotValidAfter() const
{
    return m_notValidAfter;
}

void PDFCertificateInfo::setNotValidAfter(const QDateTime& notValidAfter)
{
    m_notValidAfter = notValidAfter;
}

int32_t PDFCertificateInfo::getVersion() const
{
    return m_version;
}

void PDFCertificateInfo::setVersion(int32_t version)
{
    m_version = version;
}

PDFCertificateInfo::PublicKey PDFCertificateInfo::getPublicKey() const
{
    return m_publicKey;
}

void PDFCertificateInfo::setPublicKey(const PublicKey& publicKey)
{
    m_publicKey = publicKey;
}

int PDFCertificateInfo::getKeySize() const
{
    return m_keySize;
}

void PDFCertificateInfo::setKeySize(int keySize)
{
    m_keySize = keySize;
}

PDFCertificateInfo::KeyUsageFlags PDFCertificateInfo::getKeyUsage() const
{
    return m_keyUsage;
}

void PDFCertificateInfo::setKeyUsage(KeyUsageFlags keyUsage)
{
    m_keyUsage = keyUsage;
}

bool PDFCertificateInfo::isUsableForDigitalSignature() const
{
    return m_keyUsage.testAnyFlags(KeyUsageFlags(KeyUsageDigitalSignature | KeyUsageNonRepudiation));
}

std::optional<PDFCertificateInfo> PDFCertificateInfo::getCertificateInfo(const QByteArray& certificateData)
{
    std::optional<PDFCertificateInfo> result;

    PDFOpenSSLGlobalLock lock;
    const unsigned char* data = convertByteArrayToUcharPtr(certificateData);
    if (X509* certificate = d2i_X509(nullptr, &data, certificateData.length()))
    {
        result = getCertificateInfo(certificate);
        X509_free(certificate);
    }

    return result;
}

QString PDFCertificateInfo::getIssuerName(const QByteArray& certificateData)
{
    QString result;

    PDFOpenSSLGlobalLock lock;
    const unsigned char* data = convertByteArrayToUcharPtr(certificateData);
    X509* certificate = d2i_X509(nullptr, &data, certificateData.length());
    if (!certificate)
    {
        return result;
    }

    if (X509_NAME* issuerName = X509_get_issuer_name(certificate))
    {
        for (const int nid : { NID_commonName, NID_organizationName })
        {
            const int location = X509_NAME_get_index_by_NID(issuerName, nid, -1);
            ASN1_STRING* string = location >= 0 ? X509_NAME_ENTRY_get_data(X509_NAME_get_entry(issuerName, location)) : nullptr;
            unsigned char* utf8Buffer = nullptr;
            const int length = string ? ASN1_STRING_to_UTF8(&utf8Buffer, string) : -1;
            if (length > 0)
            {
                result = QString::fromUtf8(reinterpret_cast<const char*>(utf8Buffer), length);
            }
            OPENSSL_free(utf8Buffer);
            if (!result.isEmpty())
            {
                break;
            }
        }

        if (result.isEmpty())
        {
            if (char* line = X509_NAME_oneline(issuerName, nullptr, 0))
            {
                result = QString::fromUtf8(line);
                OPENSSL_free(line);
            }
        }
    }

    X509_free(certificate);
    return result;
}

PDFCertificateInfo PDFCertificateInfo::getCertificateInfo(x509_st* certificate)
{
    PDFCertificateInfo info;

    auto getStringFromASN1_STRING = [](ASN1_STRING* string) -> QString
    {
        QString result;

        if (string)
        {
            // Jakub Melka: we must convert entry to UTF8 encoding using function ASN1_STRING_to_UTF8
            unsigned char* utf8Buffer = nullptr;
            int errorCodeOrLength = ASN1_STRING_to_UTF8(&utf8Buffer, string);
            if (errorCodeOrLength > 0)
            {
                result = QString::fromUtf8(reinterpret_cast<const char*>(utf8Buffer), errorCodeOrLength);
            }
            OPENSSL_free(utf8Buffer);
        }

        return result;
    };

    auto getStringFromX509Name = [&getStringFromASN1_STRING](X509_NAME* name, int nid) -> QString
    {
        QString result;

        const int stringLocation = X509_NAME_get_index_by_NID(name, nid, -1);
        X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, stringLocation);
        return getStringFromASN1_STRING(X509_NAME_ENTRY_get_data(entry));
    };

    auto getDateTimeFromASN = [](const ASN1_TIME* time) -> QDateTime
    {
        QDateTime result;

        if (time)
        {
            tm internalTime = { };
            if (ASN1_TIME_to_tm(time, &internalTime) > 0)
            {
#if defined(Q_OS_WIN)
                time_t localTime = _mkgmtime(&internalTime);
#elif defined(Q_OS_UNIX)
                time_t localTime = timegm(&internalTime);
#else
                static_assert(false, "Implement this for another OS!");
#endif
                result = QDateTime::fromSecsSinceEpoch(localTime, Qt::UTC);
            }
        }

        return result;
    };

    if (X509_NAME* subjectName = X509_get_subject_name(certificate))
    {
        // List of these properties are in RFC 5280, section 4.1.2.4, these attributes
        // are standard and all implementations must be prepared to process them.
        QString countryName = getStringFromX509Name(subjectName, NID_countryName);
        QString organizationName = getStringFromX509Name(subjectName, NID_organizationName);
        QString organizationalUnitName = getStringFromX509Name(subjectName, NID_organizationalUnitName);
        QString distinguishedName = getStringFromX509Name(subjectName, NID_distinguishedName);
        QString stateOrProvinceName = getStringFromX509Name(subjectName, NID_stateOrProvinceName);
        QString commonName = getStringFromX509Name(subjectName, NID_commonName);
        QString serialNumber = getStringFromX509Name(subjectName, NID_serialNumber);

        // These attributes are defined also in section 4.1.2.4, they are not mandatory,
        // but application should be able to process them.
        QString localityName = getStringFromX509Name(subjectName, NID_localityName);
        QString title = getStringFromX509Name(subjectName, NID_title);
        QString surname = getStringFromX509Name(subjectName, NID_surname);
        QString givenName = getStringFromX509Name(subjectName, NID_givenName);
        QString initials = getStringFromX509Name(subjectName, NID_initials);
        QString pseudonym = getStringFromX509Name(subjectName, NID_pseudonym);
        QString generationQualifier = getStringFromX509Name(subjectName, NID_generationQualifier);

        // This entry is not defined in section 4.1.2.4, but is commonly used; RFC 5280, 4.2.1.6
        // puts the e-mail into the rfc822Name of the subject alternative name (certificates of
        // the certification authorities carry it there) - the first one, when the subject has none
        QString email = getStringFromX509Name(subjectName, NID_pkcs9_emailAddress);
        if (email.isEmpty())
        {
            if (GENERAL_NAMES* alternativeNames = static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(certificate, NID_subject_alt_name, nullptr, nullptr)))
            {
                for (int i = 0; i < sk_GENERAL_NAME_num(alternativeNames) && email.isEmpty(); ++i)
                {
                    const GENERAL_NAME* alternativeName = sk_GENERAL_NAME_value(alternativeNames, i);
                    if (alternativeName && alternativeName->type == GEN_EMAIL)
                    {
                        email = getStringFromASN1_STRING(alternativeName->d.rfc822Name);
                    }
                }
                GENERAL_NAMES_free(alternativeNames);
            }
        }

        info.setName(PDFCertificateInfo::CountryName, qMove(countryName));
        info.setName(PDFCertificateInfo::OrganizationName, qMove(organizationName));
        info.setName(PDFCertificateInfo::OrganizationalUnitName, qMove(organizationalUnitName));
        info.setName(PDFCertificateInfo::DistinguishedName, qMove(distinguishedName));
        info.setName(PDFCertificateInfo::StateOrProvinceName, qMove(stateOrProvinceName));
        info.setName(PDFCertificateInfo::CommonName, qMove(commonName));
        info.setName(PDFCertificateInfo::SerialNumber, qMove(serialNumber));

        info.setName(PDFCertificateInfo::LocalityName, qMove(localityName));
        info.setName(PDFCertificateInfo::Title, qMove(title));
        info.setName(PDFCertificateInfo::Surname, qMove(surname));
        info.setName(PDFCertificateInfo::GivenName, qMove(givenName));
        info.setName(PDFCertificateInfo::Initials, qMove(initials));
        info.setName(PDFCertificateInfo::Pseudonym, qMove(pseudonym));
        info.setName(PDFCertificateInfo::GenerationalQualifier, qMove(generationQualifier));

        info.setName(PDFCertificateInfo::Email, qMove(email));

        const long version = X509_get_version(certificate);
        info.setVersion(version);

        const ASN1_TIME* notBeforeTime = X509_get0_notBefore(certificate);
        const ASN1_TIME* notAfterTime = X509_get0_notAfter(certificate);

        info.setNotValidBefore(getDateTimeFromASN(notBeforeTime));
        info.setNotValidAfter(getDateTimeFromASN(notAfterTime));

        // X509_PUBKEY_get0 does not take a reference (X509_PUBKEY_get does and its key would leak)
        X509_PUBKEY* publicKey = X509_get_X509_PUBKEY(certificate);
        EVP_PKEY* evpKey = publicKey ? X509_PUBKEY_get0(publicKey) : nullptr;
        const int keyType = evpKey ? EVP_PKEY_type(EVP_PKEY_base_id(evpKey)) : NID_undef;

        PDFCertificateInfo::PublicKey key = PDFCertificateInfo::KeyUnknown;
        switch (keyType)
        {
        case EVP_PKEY_RSA:
            key = PDFCertificateInfo::KeyRSA;
            break;

        case EVP_PKEY_DSA:
            key = PDFCertificateInfo::KeyDSA;
            break;

        case EVP_PKEY_DH:
            key = PDFCertificateInfo::KeyDH;
            break;

        case EVP_PKEY_EC:
            key = PDFCertificateInfo::KeyEC;
            break;

        default:
            break;
        }
        info.setPublicKey(key);

        const int bits = evpKey ? EVP_PKEY_bits(evpKey) : 0;
        info.setKeySize(bits);

        uint32_t keyUsage = X509_get_key_usage(certificate);
        if (keyUsage != UINT32_MAX)
        {
            static_assert(PDFCertificateInfo::KeyUsageDigitalSignature    == KU_DIGITAL_SIGNATURE, "Fix this code!");
            static_assert(PDFCertificateInfo::KeyUsageNonRepudiation      == KU_NON_REPUDIATION, "Fix this code!");
            static_assert(PDFCertificateInfo::KeyUsageKeyEncipherment     == KU_KEY_ENCIPHERMENT, "Fix this code!");
            static_assert(PDFCertificateInfo::KeyUsageDataEncipherment    == KU_DATA_ENCIPHERMENT, "Fix this code!");
            static_assert(PDFCertificateInfo::KeyUsageAgreement           == KU_KEY_AGREEMENT, "Fix this code!");
            static_assert(PDFCertificateInfo::KeyUsageCertSign            == KU_KEY_CERT_SIGN, "Fix this code!");
            static_assert(PDFCertificateInfo::KeyUsageCrlSign             == KU_CRL_SIGN, "Fix this code!");
            static_assert(PDFCertificateInfo::KeyUsageEncipherOnly        == KU_ENCIPHER_ONLY, "Fix this code!");
            static_assert(PDFCertificateInfo::KeyUsageDecipherOnly        == KU_DECIPHER_ONLY, "Fix this code!");

            if (X509_get_extension_flags(certificate) & EXFLAG_XKUSAGE)
            {
                const uint32_t extendedKeyUsage = X509_get_extended_key_usage(certificate);
                Q_ASSERT(extendedKeyUsage != UINT32_MAX);

                static_assert(PDFCertificateInfo::KeyUsageExtended_SSL_SERVER  >> 16 == XKU_SSL_SERVER, "Fix this code!");
                static_assert(PDFCertificateInfo::KeyUsageExtended_SSL_CLIENT  >> 16 == XKU_SSL_CLIENT, "Fix this code!");
                static_assert(PDFCertificateInfo::KeyUsageExtended_SMIME       >> 16 == XKU_SMIME, "Fix this code!");
                static_assert(PDFCertificateInfo::KeyUsageExtended_CODE_SIGN   >> 16 == XKU_CODE_SIGN, "Fix this code!");
                static_assert(PDFCertificateInfo::KeyUsageExtended_SGC         >> 16 == XKU_SGC, "Fix this code!");
                static_assert(PDFCertificateInfo::KeyUsageExtended_OCSP_SIGN   >> 16 == XKU_OCSP_SIGN, "Fix this code!");
                static_assert(PDFCertificateInfo::KeyUsageExtended_TIMESTAMP   >> 16 == XKU_TIMESTAMP, "Fix this code!");
                static_assert(PDFCertificateInfo::KeyUsageExtended_DVCS        >> 16 == XKU_DVCS, "Fix this code!");
                static_assert(PDFCertificateInfo::KeyUsageExtended_ANYEKU      >> 16 == XKU_ANYEKU, "Fix this code!");

                keyUsage = keyUsage | (extendedKeyUsage << 16);
            }

            info.setKeyUsage(static_cast<PDFCertificateInfo::KeyUsageFlags>(keyUsage));
        }

        unsigned char* buffer = nullptr;
        int length = i2d_X509(certificate, &buffer);
        if (length >= 0)
        {
            Q_ASSERT(buffer);
            info.setCertificateData(QByteArray(reinterpret_cast<const char*>(buffer), length));
            OPENSSL_free(buffer);
        }
    }

    return info;
}

QByteArray PDFCertificateInfo::getCertificateData() const
{
    return m_certificateData;
}

void PDFCertificateInfo::setCertificateData(const QByteArray& certificateData)
{
    m_certificateData = certificateData;
}


void PDFCertificateStore::serialize(QDataStream& stream) const
{
    stream << persist_version;
    stream << m_certificates;
}

void PDFCertificateStore::deserialize(QDataStream& stream)
{
    m_certificates.clear();

    // B131 (SECURITY_TODO 53): the version of the format and the count of the entries are checked
    // before anything is allocated by them
    int persistVersionDeserialized = 0;
    stream >> persistVersionDeserialized;
    if (stream.status() != QDataStream::Ok || persistVersionDeserialized != persist_version)
    {
        stream.setStatus(QDataStream::ReadCorruptData);
        return;
    }

    PDFCertificateEntries::size_type count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > MAXIMUM_STORE_ENTRIES)
    {
        stream.setStatus(QDataStream::ReadCorruptData);
        return;
    }

    PDFCertificateEntries certificates;
    certificates.reserve(count);
    for (PDFCertificateEntries::size_type i = 0; i < count; ++i)
    {
        PDFCertificateEntry entry;
        stream >> entry;
        if (stream.status() != QDataStream::Ok)
        {
            stream.setStatus(QDataStream::ReadCorruptData);
            return;
        }

        certificates.emplace_back(std::move(entry));
    }

    m_certificates = std::move(certificates);
}

bool PDFCertificateStore::loadFromFile(const QString& fileName)
{
    m_certificates.clear();

    QFile file(fileName);
    if (file.size() > MAXIMUM_STORE_FILE_SIZE || !file.open(QFile::ReadOnly))
    {
        return false;
    }

    // Never more than the limit is read, also when the file grows in the meantime
    const QByteArray data = file.read(MAXIMUM_STORE_FILE_SIZE + 1);
    file.close();
    if (data.size() > MAXIMUM_STORE_FILE_SIZE)
    {
        return false;
    }

    QDataStream stream(data);
    deserialize(stream);
    if (stream.status() != QDataStream::Ok)
    {
        m_certificates.clear();
        return false;
    }

    return true;
}

bool PDFCertificateStore::add(PDFCertificateEntry::EntryType type, const QByteArray& certificate)
{
    if (auto certificateInfo = PDFCertificateInfo::getCertificateInfo(certificate))
    {
        return add(type, qMove(*certificateInfo));
    }

    return false;
}

bool PDFCertificateStore::add(PDFCertificateEntry::EntryType type, PDFCertificateInfo info)
{
    auto it = std::find_if(m_certificates.cbegin(), m_certificates.cend(), [&info](const auto& entry) { return entry.info == info; });
    if (it == m_certificates.cend())
    {
        m_certificates.push_back({ type, qMove(info), QByteArray(), QString() });
    }

    return true;
}

bool PDFCertificateStore::contains(const PDFCertificateInfo& info)
{
    return std::find_if(m_certificates.cbegin(), m_certificates.cend(), [&info](const auto& entry) { return entry.info == info; }) != m_certificates.cend();
}

QString PDFCertificateStore::getDefaultCertificateStoreFileName() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/TrustedCertStorage.bin";
}

void PDFCertificateStore::loadDefaultUserCertificates()
{
    createDirectoryForDefaultUserCertificatesStore();
    QString trustedCertificateStoreFileName = getDefaultCertificateStoreFileName();
    QString trustedCertificateStoreLockFileName = trustedCertificateStoreFileName + ".lock";

    QLockFile lockFile(trustedCertificateStoreLockFileName);
    if (lockFile.lock())
    {
        loadFromFile(trustedCertificateStoreFileName);
        lockFile.unlock();
    }
}

void PDFCertificateStore::saveDefaultUserCertificates()
{
    createDirectoryForDefaultUserCertificatesStore();
    QString trustedCertificateStoreFileName = getDefaultCertificateStoreFileName();
    QString trustedCertificateStoreLockFileName = trustedCertificateStoreFileName + ".lock";

    QFileInfo fileInfo(trustedCertificateStoreFileName);
    QDir dir = fileInfo.dir();
    dir.mkpath(dir.path());

    QLockFile lockFile(trustedCertificateStoreLockFileName);
    if (lockFile.lock())
    {
        QFile trustedCertificateStoreFile(trustedCertificateStoreFileName);
        if (trustedCertificateStoreFile.open(QFile::WriteOnly | QFile::Truncate))
        {
            QDataStream stream(&trustedCertificateStoreFile);
            serialize(stream);
            trustedCertificateStoreFile.close();
        }
        lockFile.unlock();
    }
}

void PDFCertificateStore::createDirectoryForDefaultUserCertificatesStore()
{
    QFileInfo fileInfo(getDefaultCertificateStoreFileName());
    QString path = fileInfo.path();
    QDir().mkpath(path);
}

PDFCertificateEntries PDFCertificateStore::getAATLCertificates()
{
    PDFCertificateEntries result;

    QFile aatlFile(":/aatl/SecuritySettings.xml");
    if (aatlFile.open(QFile::ReadOnly))
    {
        QString errorMessage;
        QDomDocument aatlDocument;
        if (aatlDocument.setContent(&aatlFile, &errorMessage))
        {
            // Najdeme kořenový element
            QDomElement root = aatlDocument.documentElement();

            // Seek path "SecuritySettings/TrustedIdentities/Identity/Certificate"
            QDomNodeList identities = root.firstChildElement("TrustedIdentities").elementsByTagName("Identity");

            for (int i = 0; i < identities.count(); ++i)
            {
                QDomNode identityNode = identities.at(i);
                QDomElement certificateElement = identityNode.firstChildElement("Certificate");

                if (!certificateElement.isNull())
                {
                    QString text = certificateElement.text();
                    QString pemFormattedText = QString("-----BEGIN CERTIFICATE-----\n%1\n-----END CERTIFICATE-----").arg(text);
                    QByteArray certificateData = pemFormattedText.toLatin1();

                    // Read PEM certificate to the OpenSSL X509
                    BIO* bio = BIO_new_mem_buf(certificateData.constData(), certificateData.size());
                    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
                    BIO_free(bio);

                    if (!cert)
                    {
                        continue;
                    }

                    // Převést certifikát na DER
                    int len = i2d_X509(cert, nullptr);
                    QByteArray derData(len, 0);
                    unsigned char *derPtr = reinterpret_cast<unsigned char*>(derData.data());
                    i2d_X509(cert, &derPtr);

                    X509_free(cert);

                    std::optional<PDFCertificateInfo> info = PDFCertificateInfo::getCertificateInfo(derData);
                    if (info)
                    {
                        PDFCertificateEntry entry;
                        entry.type = PDFCertificateEntry::EntryType::AATL;
                        entry.info = qMove(*info);
                        result.emplace_back(qMove(entry));
                    }
                }
            }
        }

        aatlFile.close();
    }

    return result;
}

/// The store of the system (PDFPersonalCertificateStore::createSystemStore)
class PDFSystemPersonalCertificateStore : public PDFPersonalCertificateStore
{
public:
    virtual void* openStore() const override;
};

/// A store in the memory of the process (PDFPersonalCertificateStore::createMemoryStore); it owns the
/// handle of the store, the copies of openStore are duplicates of it
class PDFMemoryPersonalCertificateStore : public PDFPersonalCertificateStore
{
public:
    explicit PDFMemoryPersonalCertificateStore(void* store) : m_store(store) { }
    virtual ~PDFMemoryPersonalCertificateStore() override;

    PDFMemoryPersonalCertificateStore(const PDFMemoryPersonalCertificateStore&) = delete;
    PDFMemoryPersonalCertificateStore& operator=(const PDFMemoryPersonalCertificateStore&) = delete;

    virtual void* openStore() const override;

private:
    void* m_store = nullptr;
};

/// The injected store of PDFPersonalCertificateStore::setStore
class PDFPersonalCertificateStoreHelper
{
public:
    static QMutex s_mutex;
    static std::shared_ptr<const PDFPersonalCertificateStore> s_store;
};

QMutex PDFPersonalCertificateStoreHelper::s_mutex;
std::shared_ptr<const PDFPersonalCertificateStore> PDFPersonalCertificateStoreHelper::s_store;

PDFPersonalCertificateStore::~PDFPersonalCertificateStore() = default;

std::shared_ptr<const PDFPersonalCertificateStore> PDFPersonalCertificateStore::createSystemStore()
{
    return std::make_shared<const PDFSystemPersonalCertificateStore>();
}

std::shared_ptr<const PDFPersonalCertificateStore> PDFPersonalCertificateStore::getStore()
{
    {
        QMutexLocker locker(&PDFPersonalCertificateStoreHelper::s_mutex);
        if (PDFPersonalCertificateStoreHelper::s_store)
        {
            return PDFPersonalCertificateStoreHelper::s_store;
        }
    }

    return createSystemStore();
}

void PDFPersonalCertificateStore::setStore(std::shared_ptr<const PDFPersonalCertificateStore> store)
{
    QMutexLocker locker(&PDFPersonalCertificateStoreHelper::s_mutex);
    PDFPersonalCertificateStoreHelper::s_store = std::move(store);
}

}   // namespace pdf

#ifdef Q_OS_WIN
#include <Windows.h>
#include <wincrypt.h>
#if defined(PDF4QT_USE_PRAGMA_LIB)
#pragma comment(lib, "crypt32.lib")
#endif
#endif

void* pdf::PDFSystemPersonalCertificateStore::openStore() const
{
#ifdef Q_OS_WIN
    // Read only: nothing of the store of the user is ever changed
    return CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, NULL, CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG | CERT_STORE_OPEN_EXISTING_FLAG, L"MY");
#else
    return nullptr;
#endif
}

pdf::PDFMemoryPersonalCertificateStore::~PDFMemoryPersonalCertificateStore()
{
#ifdef Q_OS_WIN
    if (m_store)
    {
        CertCloseStore(static_cast<HCERTSTORE>(m_store), 0);
    }
#endif
}

void* pdf::PDFMemoryPersonalCertificateStore::openStore() const
{
#ifdef Q_OS_WIN
    return m_store ? CertDuplicateStore(static_cast<HCERTSTORE>(m_store)) : nullptr;
#else
    return nullptr;
#endif
}

std::shared_ptr<const pdf::PDFPersonalCertificateStore> pdf::PDFPersonalCertificateStore::createMemoryStore(const QByteArray& pkcs12, const QByteArray& password)
{
#ifdef Q_OS_WIN
    if (pkcs12.isEmpty() || pkcs12.size() > 1024 * 1024)
    {
        return nullptr;
    }

    QString passwordText = QString::fromUtf8(password);
    std::wstring passwordWide = passwordText.toStdWString();
    PDFSecureMemory::clear(passwordText);

    CRYPT_DATA_BLOB blob = { };
    blob.cbData = DWORD(pkcs12.size());
    blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(pkcs12.constData()));

    // PKCS12_NO_PERSIST_KEY: the keys live only in the memory of this process (no key container, the
    // store of the machine is never touched)
    HCERTSTORE store = PFXImportCertStore(&blob, passwordWide.c_str(), PKCS12_NO_PERSIST_KEY | PKCS12_ALWAYS_CNG_KSP);
    PDFSecureMemory::clear(passwordWide.data(), passwordWide.size() * sizeof(wchar_t));

    if (!store)
    {
        return nullptr;
    }

    return std::make_shared<const PDFMemoryPersonalCertificateStore>(store);
#else
    Q_UNUSED(pkcs12);
    Q_UNUSED(password);
    return nullptr;
#endif
}

std::optional<QByteArray> pdf::PDFPersonalCertificateStore::decryptEnvelope(const QByteArray& certificateData, const QByteArray& envelope)
{
#ifdef Q_OS_WIN
    std::shared_ptr<const PDFPersonalCertificateStore> personalStore = getStore();
    HCERTSTORE store = personalStore ? static_cast<HCERTSTORE>(personalStore->openStore()) : nullptr;
    if (!store || certificateData.isEmpty() || envelope.isEmpty())
    {
        if (store)
        {
            CertCloseStore(store, 0);
        }
        return std::nullopt;
    }

    std::optional<QByteArray> result;
    PCCERT_CONTEXT wantedCertificate = CertCreateCertificateContext(X509_ASN_ENCODING, reinterpret_cast<const BYTE*>(certificateData.constData()), DWORD(certificateData.size()));
    PCCERT_CONTEXT certificate = wantedCertificate ? CertFindCertificateInStore(store, X509_ASN_ENCODING, 0, CERT_FIND_EXISTING, wantedCertificate, nullptr) : nullptr;

    // Only the recipient info of this certificate is decrypted: a store of one link to it (the link
    // keeps the properties of the certificate, also its key)
    HCERTSTORE linkStore = certificate ? CertOpenStore(CERT_STORE_PROV_MEMORY, 0, NULL, 0, nullptr) : nullptr;
    if (linkStore && CertAddCertificateLinkToStore(linkStore, certificate, CERT_STORE_ADD_ALWAYS, nullptr))
    {
        CRYPT_DECRYPT_MESSAGE_PARA parameters = { };
        parameters.cbSize = sizeof(parameters);
        parameters.dwMsgAndCertEncodingType = PKCS_7_ASN_ENCODING | X509_ASN_ENCODING;
        parameters.cCertStore = 1;
        parameters.rghCertStore = &linkStore;

        const BYTE* envelopeData = reinterpret_cast<const BYTE*>(envelope.constData());
        DWORD size = 0;
        if (CryptDecryptMessage(&parameters, envelopeData, DWORD(envelope.size()), nullptr, &size, nullptr) && size > 0)
        {
            QByteArray decrypted(qsizetype(size), char(0));
            if (CryptDecryptMessage(&parameters, envelopeData, DWORD(envelope.size()), reinterpret_cast<BYTE*>(decrypted.data()), &size, nullptr))
            {
                decrypted.resize(qsizetype(size));
                result = std::move(decrypted);
            }
            else
            {
                PDFSecureMemory::clear(decrypted);
            }
        }
    }

    if (linkStore)
    {
        CertCloseStore(linkStore, 0);
    }
    if (certificate)
    {
        CertFreeCertificateContext(certificate);
    }
    if (wantedCertificate)
    {
        CertFreeCertificateContext(wantedCertificate);
    }
    CertCloseStore(store, 0);
    return result;
#else
    Q_UNUSED(certificateData);
    Q_UNUSED(envelope);
    return std::nullopt;
#endif
}

pdf::PDFCertificateEntries pdf::PDFCertificateStore::getSystemCertificates()
{
    PDFCertificateEntries result;

#ifdef Q_OS_WIN
    HCERTSTORE certStore = CertOpenSystemStore(0, L"ROOT");
    PCCERT_CONTEXT context = nullptr;
    if (certStore)
    {
        while (context = CertEnumCertificatesInStore(certStore, context))
        {
            const unsigned char* pointer = context->pbCertEncoded;
            QByteArray data(reinterpret_cast<const char*>(pointer), context->cbCertEncoded);
            std::optional<PDFCertificateInfo> info = PDFCertificateInfo::getCertificateInfo(data);
            if (info)
            {
                PDFCertificateEntry entry;
                entry.type = PDFCertificateEntry::EntryType::System;
                entry.info = qMove(*info);
                result.emplace_back(qMove(entry));
            }
        }

        CertCloseStore(certStore, CERT_CLOSE_STORE_FORCE_FLAG);
    }
#endif

    return result;
}

pdf::PDFCertificateEntries pdf::PDFCertificateStore::getPersonalCertificates(PDFCertificateUsageFilter filter)
{
    PDFCertificateEntries result;

#ifdef Q_OS_WIN
    // Jakub Melka: the personal certificate storage contains also certificates,
    // which are automatically created by the operating system and by various
    // applications. Reading the friendly name, under which they are displayed
    // in the system, makes them recognizable for the user.
    auto getFriendlyName = [](PCCERT_CONTEXT certificateContext) -> QString
    {
        DWORD size = 0;
        if (!CertGetCertificateContextProperty(certificateContext, CERT_FRIENDLY_NAME_PROP_ID, nullptr, &size) || size == 0)
        {
            return QString();
        }

        QByteArray buffer(int(size), char(0));
        if (!CertGetCertificateContextProperty(certificateContext, CERT_FRIENDLY_NAME_PROP_ID, buffer.data(), &size))
        {
            return QString();
        }

        return QString::fromWCharArray(reinterpret_cast<const wchar_t*>(buffer.constData()));
    };

    // The store of the system, or the store of a test (Q6.2: never the store of the machine in a test)
    std::shared_ptr<const PDFPersonalCertificateStore> personalStore = PDFPersonalCertificateStore::getStore();
    HCERTSTORE certStore = personalStore ? static_cast<HCERTSTORE>(personalStore->openStore()) : nullptr;
    PCCERT_CONTEXT context = nullptr;
    if (certStore)
    {
        while ((context = CertEnumCertificatesInStore(certStore, context)) != nullptr)
        {
            const unsigned char* pointer = context->pbCertEncoded;
            QByteArray data(reinterpret_cast<const char*>(pointer), context->cbCertEncoded);
            std::optional<PDFCertificateInfo> info = PDFCertificateInfo::getCertificateInfo(data);
            if (info)
            {
                if (filter == PDFCertificateUsageFilter::DigitalSignature && !info->isUsableForDigitalSignature())
                {
                    continue;
                }

                PDFCertificateEntry entry;
                entry.type = PDFCertificateEntry::EntryType::System;
                entry.info = qMove(*info);
                entry.friendlyName = getFriendlyName(context);
                result.emplace_back(qMove(entry));
            }
        }

        // Not forced: the handle may be a duplicate of an injected store, which stays open
        CertCloseStore(certStore, 0);
    }
#else
    Q_UNUSED(filter);
#endif

    return result;
}

#if defined(PDF4QT_COMPILER_MINGW) || defined(PDF4QT_COMPILER_GCC)
#pragma GCC diagnostic pop
#endif

#if defined(PDF4QT_COMPILER_MSVC)
#pragma warning(pop)
#endif
