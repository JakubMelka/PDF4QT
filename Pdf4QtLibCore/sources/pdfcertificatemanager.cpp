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
#include "pdfexception.h"
#include "pdfsecurityhandler.h"

#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

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
#include <openssl/bn.h>
#include <openssl/cms.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>

namespace pdf
{

PDFCertificateManager::PDFCertificateManager()
{

}

template<typename T>
using openssl_ptr = std::unique_ptr<T, void(*)(T*)>;

/// Helpers of PDFCertificateManager
class PDFCertificateManagerHelper
{
public:
    /// Frees a stack of certificates with the certificates (PKCS12_parse gives them owned)
    static void freeCertificateStack(STACK_OF(X509)* certificates)
    {
        sk_X509_pop_free(certificates, X509_free);
    }

    /// Frees a stack of PKCS#7 objects with the objects
    static void freePkcs7Stack(STACK_OF(PKCS7)* objects)
    {
        sk_PKCS7_pop_free(objects, PKCS7_free);
    }

    /// Frees a stack of safe bags with the bags
    static void freeBagStack(STACK_OF(PKCS12_SAFEBAG)* bags)
    {
        sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
    }

    /// The PKCS#12 of the data (exactly their length), null for anything else
    static PKCS12* readPkcs12(const QByteArray& data)
    {
        if (data.isEmpty() || data.size() > PDFCertificateManager::MAXIMUM_CERTIFICATE_FILE_SIZE)
        {
            return nullptr;
        }

        const unsigned char* pointer = reinterpret_cast<const unsigned char*>(data.constData());
        return d2i_PKCS12(nullptr, &pointer, long(data.size()));
    }

    /// An iteration count of at most MAXIMUM_PKCS12_ITERATIONS (absent = 1, RFC 7292)
    static bool isIterationCountAcceptable(const ASN1_INTEGER* iterations)
    {
        if (!iterations)
        {
            return true;
        }

        int64_t value = 0;
        return ASN1_INTEGER_get_int64(&value, iterations) == 1 && value >= 1 && value <= PDFCertificateManager::MAXIMUM_PKCS12_ITERATIONS;
    }

    /// A key derivation of PBES2 or PBMAC1: PBKDF2 by its iterations, scrypt by the product of its cost,
    /// block size and parallelization; another one is refused
    static bool isKeyDerivationAcceptable(const X509_ALGOR* algorithm)
    {
        if (!algorithm || !algorithm->parameter)
        {
            return false;
        }

        const int nid = OBJ_obj2nid(algorithm->algorithm);
        if (nid == NID_id_pbkdf2)
        {
            PBKDF2PARAM* parameters = static_cast<PBKDF2PARAM*>(ASN1_TYPE_unpack_sequence(ASN1_ITEM_rptr(PBKDF2PARAM), algorithm->parameter));
            const bool isAcceptable = parameters && isIterationCountAcceptable(parameters->iter);
            PBKDF2PARAM_free(parameters);
            return isAcceptable;
        }

#ifndef OPENSSL_NO_SCRYPT
        if (nid == NID_id_scrypt)
        {
            SCRYPT_PARAMS* parameters = static_cast<SCRYPT_PARAMS*>(ASN1_TYPE_unpack_sequence(ASN1_ITEM_rptr(SCRYPT_PARAMS), algorithm->parameter));
            uint64_t cost = 0;
            uint64_t blockSize = 0;
            uint64_t parallelization = 0;
            bool isAcceptable = parameters &&
                                ASN1_INTEGER_get_uint64(&cost, parameters->costParameter) == 1 &&
                                ASN1_INTEGER_get_uint64(&blockSize, parameters->blockSize) == 1 &&
                                ASN1_INTEGER_get_uint64(&parallelization, parameters->parallelizationParameter) == 1;
            const uint64_t limit = uint64_t(PDFCertificateManager::MAXIMUM_PKCS12_ITERATIONS);
            isAcceptable = isAcceptable && cost > 0 && blockSize > 0 && parallelization > 0 &&
                           cost <= limit && blockSize <= limit / cost && parallelization <= limit / (cost * blockSize);
            SCRYPT_PARAMS_free(parameters);
            return isAcceptable;
        }
#endif

        return false;
    }

    /// A password based encryption (PBES2, or PBES1 / the PBE of PKCS#12 with PBEPARAM)
    static bool isEncryptionAcceptable(const X509_ALGOR* algorithm)
    {
        if (!algorithm || !algorithm->parameter)
        {
            return false;
        }

        if (OBJ_obj2nid(algorithm->algorithm) == NID_pbes2)
        {
            PBE2PARAM* parameters = static_cast<PBE2PARAM*>(ASN1_TYPE_unpack_sequence(ASN1_ITEM_rptr(PBE2PARAM), algorithm->parameter));
            const bool isAcceptable = parameters && isKeyDerivationAcceptable(parameters->keyfunc);
            PBE2PARAM_free(parameters);
            return isAcceptable;
        }

        PBEPARAM* parameters = static_cast<PBEPARAM*>(ASN1_TYPE_unpack_sequence(ASN1_ITEM_rptr(PBEPARAM), algorithm->parameter));
        const bool isAcceptable = parameters && isIterationCountAcceptable(parameters->iter);
        PBEPARAM_free(parameters);
        return isAcceptable;
    }

    /// The shrouded key bags of the bags and of their nested safe contents (depth limited)
    static bool areBagsAcceptable(const STACK_OF(PKCS12_SAFEBAG)* bags, int depth)
    {
        constexpr int MAXIMUM_DEPTH = 4;
        if (!bags || depth > MAXIMUM_DEPTH)
        {
            return false;
        }

        for (int i = 0; i < sk_PKCS12_SAFEBAG_num(bags); ++i)
        {
            const PKCS12_SAFEBAG* bag = sk_PKCS12_SAFEBAG_value(bags, i);
            switch (PKCS12_SAFEBAG_get_nid(bag))
            {
                case NID_pkcs8ShroudedKeyBag:
                {
                    const X509_ALGOR* algorithm = nullptr;
                    const X509_SIG* shroudedKey = PKCS12_SAFEBAG_get0_pkcs8(bag);
                    if (!shroudedKey)
                    {
                        return false;
                    }

                    X509_SIG_get0(shroudedKey, &algorithm, nullptr);
                    if (!isEncryptionAcceptable(algorithm))
                    {
                        return false;
                    }
                    break;
                }

                case NID_safeContentsBag:
                    if (!areBagsAcceptable(PKCS12_SAFEBAG_get0_safes(bag), depth + 1))
                    {
                        return false;
                    }
                    break;

                default:
                    break;
            }
        }

        return true;
    }

    /// The callback of the generation of a key (EVP_PKEY_CTX_set_cb): 0 stops the generation, when
    /// the flag of the cancellation (the application data of the context) is set
    static int keyGenerationCallback(EVP_PKEY_CTX* context)
    {
        const std::atomic<bool>* cancelFlag = static_cast<const std::atomic<bool>*>(EVP_PKEY_CTX_get_app_data(context));
        return (cancelFlag && cancelFlag->load()) ? 0 : 1;
    }

    /// The bytes of a memory BIO
    static QByteArray getData(BIO* buffer)
    {
        BUF_MEM* memory = nullptr;
        BIO_get_mem_ptr(buffer, &memory);
        return (memory && memory->length > 0) ? QByteArray(memory->data, qsizetype(memory->length)) : QByteArray();
    }

    /// Creates the PKCS#12 of a new self-signed certificate (createCertificate of both classes; B130):
    /// the key length was checked by the caller
    static QByteArray createPkcs12(const PDFCertificateManager::NewCertificateInfo& info, const std::atomic<bool>* cancelFlag, QString& error);

    /// The folders of setCertificateDirectories (empty = the folders of the user)
    static QMutex s_directoriesMutex;
    static QString s_directory;
    static QStringList s_oldDirectories;
};

QMutex PDFCertificateManagerHelper::s_directoriesMutex;
QString PDFCertificateManagerHelper::s_directory;
QStringList PDFCertificateManagerHelper::s_oldDirectories;

QByteArray PDFCertificateManagerHelper::createPkcs12(const PDFCertificateManager::NewCertificateInfo& info, const std::atomic<bool>* cancelFlag, QString& error)
{
    using Manager = PDFCertificateManager;

    auto isCanceled = [cancelFlag]() { return cancelFlag && cancelFlag->load(); };
    const QString canceledText = Manager::tr("The creation of the certificate was canceled.");
    const QString failureText = Manager::tr("The certificate cannot be created.");

    ERR_clear_error();
    if (isCanceled())
    {
        error = canceledText;
        return QByteArray();
    }

    // The RSA key by the EVP API (B130); the generation of a long key takes seconds, the callback
    // stops it on the cancellation (6.4)
    openssl_ptr<EVP_PKEY_CTX> keyContext(EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr), &EVP_PKEY_CTX_free);
    if (!keyContext ||
        EVP_PKEY_keygen_init(keyContext.get()) != 1 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(keyContext.get(), info.rsaKeyLength) != 1)
    {
        ERR_clear_error();
        error = failureText;
        return QByteArray();
    }

    EVP_PKEY_CTX_set_app_data(keyContext.get(), const_cast<std::atomic<bool>*>(cancelFlag));
    EVP_PKEY_CTX_set_cb(keyContext.get(), &PDFCertificateManagerHelper::keyGenerationCallback);

    EVP_PKEY* generatedKey = nullptr;
    const bool isGenerated = EVP_PKEY_generate(keyContext.get(), &generatedKey) == 1;
    openssl_ptr<EVP_PKEY> privateKey(generatedKey, &EVP_PKEY_free);
    if (!isGenerated || !privateKey)
    {
        ERR_clear_error();
        error = isCanceled() ? canceledText : failureText;
        return QByteArray();
    }

    openssl_ptr<X509> certificate(X509_new(), &X509_free);
    if (!certificate || X509_set_version(certificate.get(), X509_VERSION_3) != 1)
    {
        ERR_clear_error();
        error = failureText;
        return QByteArray();
    }

    // A random positive serial number of 127 bits (RFC 5280, 4.1.2.2: unique for the issuer; two
    // self-signed certificates of the same name must differ in the issuer and serial number, by which a
    // signature names its signer)
    ASN1_INTEGER* serialNumber = X509_get_serialNumber(certificate.get());
    if (info.serialNumber > 1)
    {
        ASN1_INTEGER_set(serialNumber, info.serialNumber);
    }
    else
    {
        QByteArray randomBytes;
        try
        {
            randomBytes = PDFSecurityHandlerFactory::generateRandomBytes(16);
        }
        catch (const PDFException&)
        {
            error = failureText;
            return QByteArray();
        }

        randomBytes[0] = char(randomBytes[0] & 0x7F);
        openssl_ptr<BIGNUM> number(BN_bin2bn(reinterpret_cast<const unsigned char*>(randomBytes.constData()), int(randomBytes.size()), nullptr), &BN_free);
        if (!number || BN_is_zero(number.get()) || !BN_to_ASN1_INTEGER(number.get(), serialNumber))
        {
            ERR_clear_error();
            error = failureText;
            return QByteArray();
        }
    }

    // Validity of the certificate
    X509_gmtime_adj(X509_getm_notBefore(certificate.get()), 0);
    X509_gmtime_adj(X509_getm_notAfter(certificate.get()), long(info.validityInSeconds));

    // Name of the subject, by the identifiers of the attributes (RFC 5280, 4.1.2.4; the e-mail is
    // emailAddress of PKCS #9 - OpenSSL has no short name "E"); an attribute, which cannot be added (a
    // country, which is no code of two letters, ISO 3166), fails the creation
    X509_NAME* name = X509_get_subject_name(certificate.get());
    auto addString = [name](int nid, const QString& string)
    {
        if (string.isEmpty())
        {
            return true;
        }

        const QByteArray stringUtf8 = string.toUtf8();
        return X509_NAME_add_entry_by_NID(name, nid, MBSTRING_UTF8, reinterpret_cast<const unsigned char*>(stringUtf8.constData()), int(stringUtf8.length()), -1, 0) == 1;
    };

    if (!addString(NID_countryName, info.certCountryCode))
    {
        ERR_clear_error();
        error = Manager::tr("The country '%1' cannot be used in a certificate, it must be a code of two letters.").arg(info.certCountryCode);
        return QByteArray();
    }
    if (!addString(NID_organizationName, info.certOrganization) ||
        !addString(NID_organizationalUnitName, info.certOrganizationUnit) ||
        !addString(NID_commonName, info.certCommonName))
    {
        ERR_clear_error();
        error = Manager::tr("The name of the certificate cannot be written.");
        return QByteArray();
    }
    if (!addString(NID_pkcs9_emailAddress, info.certEmail))
    {
        ERR_clear_error();
        error = Manager::tr("The e-mail '%1' cannot be used in a certificate.").arg(info.certEmail);
        return QByteArray();
    }

    if (X509_set_issuer_name(certificate.get(), name) != 1 || X509_set_pubkey(certificate.get(), privateKey.get()) != 1)
    {
        ERR_clear_error();
        error = failureText;
        return QByteArray();
    }

    // Extensions of version 3 (5.3, B130): an end entity (not a CA), the key usage of the signing of
    // documents and of the recipient of the encryption by a certificate (ISO 32000-2, 7.6.5, B99), the
    // key identifiers (the AKI of a self-signed certificate is its own SKI, so the SKI goes first)
    X509V3_CTX context = { };
    X509V3_set_ctx_nodb(&context);
    X509V3_set_ctx(&context, certificate.get(), certificate.get(), nullptr, nullptr, 0);

    const std::array<std::pair<int, const char*>, 4> extensions =
    {{
        { NID_basic_constraints, "critical,CA:FALSE" },
        { NID_key_usage, "critical,digitalSignature,nonRepudiation,keyEncipherment" },
        { NID_subject_key_identifier, "hash" },
        { NID_authority_key_identifier, "keyid:always" }
    }};

    for (const auto& extensionDefinition : extensions)
    {
        openssl_ptr<X509_EXTENSION> extension(X509V3_EXT_conf_nid(nullptr, &context, extensionDefinition.first, extensionDefinition.second), &X509_EXTENSION_free);
        if (!extension || X509_add_ext(certificate.get(), extension.get(), -1) != 1)
        {
            ERR_clear_error();
            error = failureText;
            return QByteArray();
        }
    }

    // B130: the result of the signing is checked
    if (X509_sign(certificate.get(), privateKey.get(), EVP_sha256()) <= 0)
    {
        ERR_clear_error();
        error = failureText;
        return QByteArray();
    }

    QByteArray privateKeyPasswordUtf8 = info.privateKeyPasword.toUtf8();
    openssl_ptr<PKCS12> pkcs12(PKCS12_create(privateKeyPasswordUtf8.constData(),
                                             nullptr,
                                             privateKey.get(),
                                             certificate.get(),
                                             nullptr,
                                             0,
                                             0,
                                             PKCS12_DEFAULT_ITER,
                                             PKCS12_DEFAULT_ITER,
                                             0), &PKCS12_free);
    PDFSecureMemory::clear(privateKeyPasswordUtf8);

    openssl_ptr<BIO> buffer(BIO_new(BIO_s_mem()), &BIO_free_all);
    if (!pkcs12 || !buffer || i2d_PKCS12_bio(buffer.get(), pkcs12.get()) != 1)
    {
        ERR_clear_error();
        error = failureText;
        return QByteArray();
    }

    ERR_clear_error();
    return getData(buffer.get());
}

bool PDFCertificateManager::createCertificate(const NewCertificateInfo& info, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (info.rsaKeyLength < MINIMUM_RSA_KEY_LENGTH || info.rsaKeyLength > MAXIMUM_RSA_KEY_LENGTH)
    {
        error = tr("Invalid length of the key: %1 bits.").arg(info.rsaKeyLength);
        return false;
    }

    if (info.fileName.isEmpty())
    {
        error = tr("The certificate file has no name.");
        return false;
    }

    const QByteArray pkcs12 = PDFCertificateManagerHelper::createPkcs12(info, nullptr, error);
    if (pkcs12.isEmpty())
    {
        if (error.isEmpty())
        {
            error = tr("The certificate cannot be created.");
        }
        return false;
    }

    QFile file(info.fileName);
    bool isWritten = false;
    if (file.open(QFile::WriteOnly | QFile::Truncate))
    {
        isWritten = file.write(pkcs12) == pkcs12.size();
        file.close();
        if (!isWritten)
        {
            file.remove();
        }
    }

    if (!isWritten)
    {
        error = tr("The certificate file '%1' cannot be written. %2").arg(QDir::toNativeSeparators(info.fileName), file.errorString());
    }
    return isWritten;
}

PDFCertificateEntries PDFCertificateManager::getCertificates(PDFCertificateUsageFilter filter)
{
    PDFCertificateEntries entries = PDFCertificateStore::getPersonalCertificates(filter);

    // The folder of the application first, then the folders of the old applications, read
    // only (owner answer Q5.5: a document encrypted by a certificate of the old Editor opens)
    addCertificateFiles(getCertificateDirectory(), false, entries);
    for (const QString& directory : getOldCertificateDirectories())
    {
        addCertificateFiles(directory, true, entries);
    }

    return entries;
}

void PDFCertificateManager::addCertificateFiles(const QString& directoryPath, bool isReadOnlyLocation, PDFCertificateEntries& entries)
{
    QDir directory(directoryPath);
    if (!directory.exists())
    {
        return;
    }

    const QFileInfoList pfxFiles = directory.entryInfoList(QStringList() << "*.pfx", QDir::Files | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
    for (const QFileInfo& fileInfo : pfxFiles)
    {
        if (fileInfo.size() > MAXIMUM_CERTIFICATE_FILE_SIZE)
        {
            continue;
        }

        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QFile::ReadOnly))
        {
            continue;
        }

        const QByteArray data = file.read(MAXIMUM_CERTIFICATE_FILE_SIZE + 1);
        file.close();
        if (!isPkcs12(data))
        {
            continue;
        }

        // R5-18, B107: a file of the user, never an entry of the system store. B103: a file
        // protected by its password is listed too - its certificate is read without a password
        // only, when the file allows it (else the info stays empty until the password is typed;
        // readCertificate limits the iterations, R6-28)
        PDFCertificateEntry entry;
        entry.type = PDFCertificateEntry::EntryType::User;
        if (std::optional<PDFCertificateInfo> info = readCertificate(data, QString()))
        {
            entry.info = std::move(*info);
        }

        entry.pkcs12 = data;
        entry.pkcs12fileName = fileInfo.fileName();
        entry.pkcs12FilePath = fileInfo.absoluteFilePath();
        entry.isReadOnlyLocation = isReadOnlyLocation;
        entries.emplace_back(std::move(entry));
    }
}

QString PDFCertificateManager::getCertificateDirectory()
{
    {
        QMutexLocker locker(&PDFCertificateManagerHelper::s_directoriesMutex);
        if (!PDFCertificateManagerHelper::s_directory.isEmpty())
        {
            return PDFCertificateManagerHelper::s_directory;
        }
    }

    QString standardDataLocation = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).front();
    QDir directory(standardDataLocation + "/certificates/");
    return directory.absolutePath();
}

void PDFCertificateManager::setCertificateDirectories(const QString& directory, const QStringList& oldDirectories)
{
    QMutexLocker locker(&PDFCertificateManagerHelper::s_directoriesMutex);
    PDFCertificateManagerHelper::s_directory = directory.isEmpty() ? QString() : QDir(directory).absolutePath();
    PDFCertificateManagerHelper::s_oldDirectories.clear();
    if (!directory.isEmpty())
    {
        for (const QString& oldDirectory : oldDirectories)
        {
            PDFCertificateManagerHelper::s_oldDirectories << QDir(oldDirectory).absolutePath();
        }
    }
}

QStringList PDFCertificateManager::getOldCertificateDirectories()
{
    {
        QMutexLocker locker(&PDFCertificateManagerHelper::s_directoriesMutex);
        if (!PDFCertificateManagerHelper::s_directory.isEmpty())
        {
            return PDFCertificateManagerHelper::s_oldDirectories;
        }
    }

    // The data folder of an application is <data>/<organization>/<application> (QStandardPaths,
    // AppDataLocation): the old applications of the same organization are its siblings
    // (QDir::cdUp would not move to a folder, which does not exist yet)
    const QString ownDirectory = getCertificateDirectory();
    const QDir organizationDirectory(QFileInfo(QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).front()).absolutePath());

    QStringList directories;
    for (const char* applicationName : { "PDF4QT Editor", "PDF4QT Viewer" })
    {
        const QString directory = QDir(organizationDirectory.absoluteFilePath(QString::fromLatin1(applicationName)) + "/certificates/").absolutePath();
        if (QDir::cleanPath(directory).compare(QDir::cleanPath(ownDirectory), Qt::CaseInsensitive) != 0 && !directories.contains(directory))
        {
            directories << directory;
        }
    }
    return directories;
}

bool PDFCertificateManager::isPkcs12(const QByteArray& data)
{
    openssl_ptr<PKCS12> pkcs12(PDFCertificateManagerHelper::readPkcs12(data), &PKCS12_free);
    ERR_clear_error();
    return pkcs12 != nullptr;
}

bool PDFCertificateManager::isPkcs12IterationCountAcceptable(const QByteArray& pkcs12Data, const QByteArray& password)
{
    using Helper = PDFCertificateManagerHelper;

    ERR_clear_error();
    openssl_ptr<PKCS12> pkcs12(Helper::readPkcs12(pkcs12Data), &PKCS12_free);
    if (!pkcs12)
    {
        ERR_clear_error();
        return false;
    }

    // The MAC (RFC 7292, 5.1; PBMAC1 of RFC 9579 by its PBKDF2)
    if (PKCS12_mac_present(pkcs12.get()))
    {
        const ASN1_OCTET_STRING* mac = nullptr;
        const X509_ALGOR* macAlgorithm = nullptr;
        const ASN1_OCTET_STRING* salt = nullptr;
        const ASN1_INTEGER* iterations = nullptr;
        PKCS12_get0_mac(&mac, &macAlgorithm, &salt, &iterations, pkcs12.get());

#if OPENSSL_VERSION_NUMBER >= 0x30400000L
        if (macAlgorithm && OBJ_obj2nid(macAlgorithm->algorithm) == NID_pbmac1)
        {
            PBKDF2PARAM* parameters = PBMAC1_get1_pbkdf2_param(macAlgorithm);
            const bool isAcceptable = parameters && Helper::isIterationCountAcceptable(parameters->iter);
            PBKDF2PARAM_free(parameters);
            if (!isAcceptable)
            {
                ERR_clear_error();
                return false;
            }
        }
        else
#endif
        if (!Helper::isIterationCountAcceptable(iterations))
        {
            return false;
        }
    }

    openssl_ptr<STACK_OF(PKCS7)> authenticatedSafes(PKCS12_unpack_authsafes(pkcs12.get()), &Helper::freePkcs7Stack);
    if (!authenticatedSafes)
    {
        ERR_clear_error();
        return false;
    }

    for (int i = 0; i < sk_PKCS7_num(authenticatedSafes.get()); ++i)
    {
        PKCS7* safe = sk_PKCS7_value(authenticatedSafes.get(), i);
        switch (OBJ_obj2nid(safe->type))
        {
            case NID_pkcs7_data:
            {
                openssl_ptr<STACK_OF(PKCS12_SAFEBAG)> bags(PKCS12_unpack_p7data(safe), &Helper::freeBagStack);
                if (!Helper::areBagsAcceptable(bags.get(), 0))
                {
                    ERR_clear_error();
                    return false;
                }
                break;
            }

            case NID_pkcs7_encrypted:
            {
                // The encryption of the safe itself, then (decrypted by its checked iterations) the
                // shrouded keys inside it; a wrong password cannot decrypt it - PKCS12_parse then fails
                // by the MAC before it decrypts anything
                const X509_ALGOR* algorithm = (safe->d.encrypted && safe->d.encrypted->enc_data) ? safe->d.encrypted->enc_data->algorithm : nullptr;
                if (!Helper::isEncryptionAcceptable(algorithm))
                {
                    ERR_clear_error();
                    return false;
                }

                STACK_OF(PKCS12_SAFEBAG)* decryptedBags = PKCS12_unpack_p7encdata(safe, password.isEmpty() ? "" : password.constData(), int(password.size()));
                if (!decryptedBags && password.isEmpty())
                {
                    decryptedBags = PKCS12_unpack_p7encdata(safe, nullptr, 0);
                }

                openssl_ptr<STACK_OF(PKCS12_SAFEBAG)> bags(decryptedBags, &Helper::freeBagStack);
                if (bags && !Helper::areBagsAcceptable(bags.get(), 0))
                {
                    ERR_clear_error();
                    return false;
                }
                break;
            }

            default:
                // PKCS12_parse reads no other safe
                ERR_clear_error();
                return false;
        }
    }

    ERR_clear_error();
    return true;
}

std::optional<PDFCertificateInfo> PDFCertificateManager::readCertificate(const QByteArray& pkcs12Data, const QString& password)
{
    openssl_ptr<PKCS12> pkcs12(PDFCertificateManagerHelper::readPkcs12(pkcs12Data), &PKCS12_free);
    if (!pkcs12)
    {
        ERR_clear_error();
        return std::nullopt;
    }

    // B84: the password of a PKCS#12 is UTF-8 everywhere
    QByteArray passwordUtf8 = password.toUtf8();
    const char* passwordPointer = passwordUtf8.isEmpty() ? nullptr : passwordUtf8.constData();

    // R6-28, 6.3: also the listing with the empty password limits the iterations
    if (!isPkcs12IterationCountAcceptable(pkcs12Data, passwordUtf8))
    {
        PDFSecureMemory::clear(passwordUtf8);
        return std::nullopt;
    }

    EVP_PKEY* keyPtr = nullptr;
    X509* certificatePtr = nullptr;
    STACK_OF(X509)* certificatesPtr = nullptr;
    const bool isParsed = PKCS12_parse(pkcs12.get(), passwordPointer, &keyPtr, &certificatePtr, &certificatesPtr) == 1;
    PDFSecureMemory::clear(passwordUtf8);

    openssl_ptr<EVP_PKEY> key(keyPtr, &EVP_PKEY_free);
    openssl_ptr<X509> certificate(certificatePtr, &X509_free);
    openssl_ptr<STACK_OF(X509)> certificates(certificatesPtr, &PDFCertificateManagerHelper::freeCertificateStack);

    if (!isParsed || !key || !certificate)
    {
        ERR_clear_error();
        return std::nullopt;
    }

    // The private key must belong to the certificate (a damaged file could pair them wrongly)
    if (X509_check_private_key(certificate.get(), key.get()) != 1)
    {
        ERR_clear_error();
        return std::nullopt;
    }

    PDFCertificateInfo info = PDFCertificateInfo::getCertificateInfo(certificate.get());
    if (info.getCertificateData().isEmpty())
    {
        return std::nullopt;
    }
    return info;
}

bool PDFCertificateManager::isUsableForEncryption(const QByteArray& certificateData)
{
    const unsigned char* data = reinterpret_cast<const unsigned char*>(certificateData.constData());
    openssl_ptr<X509> certificate(d2i_X509(nullptr, &data, certificateData.size()), &X509_free);
    if (!certificate)
    {
        ERR_clear_error();
        return false;
    }

    // RFC 5280, 4.2.1.3: without the extension the key may be used for any purpose
    const uint32_t keyUsage = X509_get_key_usage(certificate.get());
    if (keyUsage != UINT32_MAX && !(keyUsage & KU_KEY_ENCIPHERMENT))
    {
        return false;
    }

    // PKCS#7 enveloped data of the encryption by a certificate transport the key by RSA
    EVP_PKEY* publicKey = X509_get0_pubkey(certificate.get());
    return publicKey && EVP_PKEY_base_id(publicKey) == EVP_PKEY_RSA;
}

bool PDFCertificateManager::copyCertificateToUserDirectory(const PDFCertificateEntry& entry, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    const QString fileName = QFileInfo(entry.pkcs12fileName).fileName();
    if (entry.pkcs12.isEmpty() || fileName.isEmpty())
    {
        error = tr("The certificate has no file to copy.");
        return false;
    }

    const QString directoryPath = getCertificateDirectory();
    if (!QDir().mkpath(directoryPath))
    {
        error = tr("The certificate folder '%1' cannot be created.").arg(QDir::toNativeSeparators(directoryPath));
        return false;
    }

    const QString targetPath = QDir(directoryPath).absoluteFilePath(fileName);
    QFile target(targetPath);
    if (target.exists())
    {
        // The same file was copied before; another file of the same name is never replaced
        if (target.size() == entry.pkcs12.size() && target.open(QFile::ReadOnly))
        {
            const bool isSame = target.readAll() == entry.pkcs12;
            target.close();
            if (isSame)
            {
                return true;
            }
        }

        error = tr("A different certificate file '%1' already exists in the certificate folder.").arg(fileName);
        return false;
    }

    // NewOnly: a file created by another process in the meantime is not replaced either
    if (!target.open(QFile::WriteOnly | QFile::NewOnly))
    {
        error = tr("The certificate file '%1' cannot be written. %2").arg(QDir::toNativeSeparators(targetPath), target.errorString());
        return false;
    }

    const bool isWritten = target.write(entry.pkcs12) == entry.pkcs12.size();
    target.close();
    if (!isWritten)
    {
        target.remove();
        error = tr("The certificate file '%1' cannot be written. %2").arg(QDir::toNativeSeparators(targetPath), target.errorString());
        return false;
    }
    return true;
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
    if (certificateEntry.pkcs12.isEmpty())
    {
        // A certificate of the system store: the system asks for its PIN itself
        return true;
    }

    openssl_ptr<PKCS12> pkcs12(PDFCertificateManagerHelper::readPkcs12(certificateEntry.pkcs12), &PKCS12_free);
    if (!pkcs12)
    {
        ERR_clear_error();
        return false;
    }

    QByteArray passwordByteArray = password.isEmpty() ? QByteArray() : password.toUtf8();
    PDFSecureMemory::clear(password);

    // 6.3: no password is tried on a file of a hostile count of iterations
    bool isValid = isPkcs12IterationCountAcceptable(certificateEntry.pkcs12, passwordByteArray);
    if (isValid)
    {
        const char* passwordPointer = passwordByteArray.isEmpty() ? nullptr : passwordByteArray.constData();
        isValid = PKCS12_parse(pkcs12.get(), passwordPointer, nullptr, nullptr, nullptr) == 1;
    }

    // The UTF-8 copy of the password is no longer needed (plan of step 5 of phase 2, 6.1, S5.1;
    // review RS5SEC L8)
    PDFSecureMemory::clear(passwordByteArray);
    ERR_clear_error();
    return isValid;
}

/// The data of an unlocked key (PDFSigningKey): the EVP_PKEY of a .pfx with its certificate and chain,
/// or the handles of the system store (the key stays in the system)
struct PDFSigningKey::Impl
{
    Impl() = default;
    ~Impl();

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    Source source = Source::None;
    openssl_ptr<X509> certificate = openssl_ptr<X509>(nullptr, &X509_free);
    openssl_ptr<EVP_PKEY> privateKey = openssl_ptr<EVP_PKEY>(nullptr, &EVP_PKEY_free);
    std::vector<QByteArray> chain;

    // The system store (Windows): PCCERT_CONTEXT, HCRYPTPROV_OR_NCRYPT_KEY_HANDLE, the key spec
    void* systemCertificate = nullptr;
    quintptr systemKey = 0;
    unsigned long systemKeySpec = 0;
    bool isSystemKeyOwned = false;
};

/// Helpers of PDFSigningKey and of PDFSignatureFactory::unlock
class PDFSigningKeyHelper
{
public:
    /// The bytes of a DER object by its i2d function
    template<typename T>
    static QByteArray encode(const T* object, int (*i2d)(const T*, unsigned char**))
    {
        unsigned char* buffer = nullptr;
        const int size = object ? i2d(object, &buffer) : -1;
        QByteArray result;
        if (size > 0 && buffer)
        {
            result = QByteArray(reinterpret_cast<const char*>(buffer), size);
        }
        OPENSSL_free(buffer);
        return result;
    }

    /// SHA-256 of the data
    static QByteArray sha256(const char* data, size_t size)
    {
        std::array<unsigned char, EVP_MAX_MD_SIZE> digest = { };
        unsigned int digestSize = 0;
        if (EVP_Digest(data, size, digest.data(), &digestSize, EVP_sha256(), nullptr) != 1)
        {
            ERR_clear_error();
            return QByteArray();
        }
        return QByteArray(reinterpret_cast<const char*>(digest.data()), int(digestSize));
    }

    /// The certificate may sign PDF documents now (plan 5.3, B116): valid at this time, the key usage
    /// (when present) digitalSignature or nonRepudiation, the extended key usage (when present) of the
    /// signing of documents or of e-mails, an RSA or an ECDSA key
    static bool checkCertificate(X509* certificate, QString* errorMessage);

    /// The certificate may sign PDF documents whatever the time is (PDFCertificateManager::
    /// isUsableForSigning): the key usage, the extended key usage and the type of the key
    static bool checkSigningUsage(X509* certificate, QString* errorMessage);

    /// The DER of the signed attributes as a SET OF, which is signed (RFC 5652, 5.4: the DER encoding of
    /// the SET OF Attribute, sorted as OpenSSL writes the SignerInfo)
    static QByteArray encodeSignedAttributes(CMS_SignerInfo* signerInfo);

    /// An ECDSA signature r || s of the system as the DER ECDSA-Sig-Value of CMS
    static QByteArray convertEcdsaSignature(const QByteArray& signature);

    /// Signs the DER of the signed attributes by the key (SHA-256)
    static QByteArray signAttributes(const PDFSigningKey::Impl& impl, const QByteArray& attributes, QString* errorMessage);

    /// Opens the certificate of the system store with its private key and its chain (Windows; the end
    /// of this file)
    static bool openSystemKey(const QByteArray& certificateData, quintptr parentWindow, PDFSigningKey::Impl& impl, QString* errorMessage);

    /// Signs the SHA-256 hash by the key of the system store: PKCS#1 v1.5 for RSA, r || s for ECDSA
    static QByteArray signHashBySystemKey(const PDFSigningKey::Impl& impl, const QByteArray& hash, bool isEllipticCurve, QString* errorMessage);

    /// Frees the handles of the system store
    static void releaseSystemKey(PDFSigningKey::Impl& impl);
};

PDFSigningKey::Impl::~Impl()
{
    PDFSigningKeyHelper::releaseSystemKey(*this);
}

bool PDFSigningKeyHelper::checkCertificate(X509* certificate, QString* errorMessage)
{
    auto setError = [errorMessage](const QString& text)
    {
        if (errorMessage)
        {
            *errorMessage = text;
        }
        return false;
    };

    if (!certificate)
    {
        return setError(PDFSigningKey::tr("The certificate cannot be read."));
    }

    if (X509_cmp_current_time(X509_get0_notBefore(certificate)) >= 0)
    {
        return setError(PDFSigningKey::tr("The certificate is not valid yet."));
    }

    if (X509_cmp_current_time(X509_get0_notAfter(certificate)) <= 0)
    {
        return setError(PDFSigningKey::tr("The certificate has expired."));
    }

    return checkSigningUsage(certificate, errorMessage);
}

bool PDFSigningKeyHelper::checkSigningUsage(X509* certificate, QString* errorMessage)
{
    auto setError = [errorMessage](const QString& text)
    {
        if (errorMessage)
        {
            *errorMessage = text;
        }
        return false;
    };

    if (!certificate)
    {
        return setError(PDFSigningKey::tr("The certificate cannot be read."));
    }

    // RFC 5280, 4.2.1.3: without the extension the key may be used for any purpose
    const uint32_t keyUsage = X509_get_key_usage(certificate);
    if (keyUsage != UINT32_MAX && !(keyUsage & (KU_DIGITAL_SIGNATURE | KU_NON_REPUDIATION)))
    {
        return setError(PDFSigningKey::tr("The certificate is not meant for signatures (its key usage has neither digitalSignature nor nonRepudiation)."));
    }

    // B116: an own check of the purpose (SMIME_SIGN of OpenSSL refuses documentSigning): emailProtection,
    // documentSigning (RFC 9336), Adobe Authentic Documents Trust, Microsoft Document Signing, or any
    int criticality = -1;
    EXTENDED_KEY_USAGE* extendedKeyUsage = static_cast<EXTENDED_KEY_USAGE*>(X509_get_ext_d2i(certificate, NID_ext_key_usage, &criticality, nullptr));
    if (extendedKeyUsage)
    {
        bool isSigningAllowed = false;
        for (int i = 0; i < sk_ASN1_OBJECT_num(extendedKeyUsage) && !isSigningAllowed; ++i)
        {
            const ASN1_OBJECT* purpose = sk_ASN1_OBJECT_value(extendedKeyUsage, i);
            const int nid = OBJ_obj2nid(purpose);
            if (nid == NID_email_protect || nid == NID_anyExtendedKeyUsage)
            {
                isSigningAllowed = true;
                continue;
            }

            std::array<char, 128> text = { };
            if (OBJ_obj2txt(text.data(), int(text.size()), purpose, 1) > 0)
            {
                const QByteArray oid(text.data());
                isSigningAllowed = oid == "1.3.6.1.5.5.7.3.36" || oid == "1.2.840.113583.1.1.5" || oid == "1.3.6.1.4.1.311.10.3.12";
            }
        }
        EXTENDED_KEY_USAGE_free(extendedKeyUsage);

        if (!isSigningAllowed)
        {
            return setError(PDFSigningKey::tr("The certificate is not meant for the signing of documents (its extended key usage)."));
        }
    }
    else if (criticality != -1)
    {
        // The extension is present, but damaged
        ERR_clear_error();
        return setError(PDFSigningKey::tr("The certificate cannot be read."));
    }

    const EVP_PKEY* publicKey = X509_get0_pubkey(certificate);
    const int keyType = publicKey ? EVP_PKEY_get_base_id(publicKey) : NID_undef;
    if (keyType != EVP_PKEY_RSA && keyType != EVP_PKEY_EC)
    {
        ERR_clear_error();
        return setError(PDFSigningKey::tr("The key of the certificate cannot sign PDF documents (only RSA and ECDSA keys are supported)."));
    }

    return true;
}

bool PDFCertificateManager::isUsableForSigning(const QByteArray& certificateData, QString* errorMessage)
{
    // The rule of unlock (PDFSigningKeyHelper::checkCertificate) without the time
    const unsigned char* data = reinterpret_cast<const unsigned char*>(certificateData.constData());
    openssl_ptr<X509> certificate(certificateData.isEmpty() ? nullptr : d2i_X509(nullptr, &data, certificateData.size()), &X509_free);
    if (!certificate)
    {
        ERR_clear_error();
    }
    return PDFSigningKeyHelper::checkSigningUsage(certificate.get(), errorMessage);
}

QByteArray PDFSigningKeyHelper::encodeSignedAttributes(CMS_SignerInfo* signerInfo)
{
    const int count = CMS_signed_get_attr_count(signerInfo);
    if (count <= 0)
    {
        return QByteArray();
    }

    std::vector<QByteArray> encodings;
    encodings.reserve(size_t(count));
    for (int i = 0; i < count; ++i)
    {
        QByteArray encoding = encode<X509_ATTRIBUTE>(CMS_signed_get_attr(signerInfo, i), &i2d_X509_ATTRIBUTE);
        if (encoding.isEmpty())
        {
            return QByteArray();
        }
        encodings.emplace_back(std::move(encoding));
    }

    // X.690, 11.6: the components of a SET OF in the ascending order of their encodings (OpenSSL: the
    // common prefix by bytes, then the shorter one first)
    std::sort(encodings.begin(), encodings.end(), [](const QByteArray& left, const QByteArray& right)
    {
        const qsizetype commonSize = std::min(left.size(), right.size());
        const int comparison = std::memcmp(left.constData(), right.constData(), size_t(commonSize));
        return comparison != 0 ? comparison < 0 : left.size() < right.size();
    });

    QByteArray content;
    for (const QByteArray& encoding : encodings)
    {
        content.append(encoding);
    }

    // The tag SET and the DER length
    QByteArray result;
    result.append(char(0x31));
    const qsizetype length = content.size();
    if (length < 0x80)
    {
        result.append(char(length));
    }
    else
    {
        QByteArray lengthBytes;
        for (qsizetype value = length; value > 0; value >>= 8)
        {
            lengthBytes.prepend(char(value & 0xFF));
        }
        result.append(char(0x80 | lengthBytes.size()));
        result.append(lengthBytes);
    }
    result.append(content);
    return result;
}

QByteArray PDFSigningKeyHelper::convertEcdsaSignature(const QByteArray& signature)
{
    if (signature.isEmpty() || signature.size() % 2 != 0)
    {
        return QByteArray();
    }

    const int half = int(signature.size() / 2);
    const unsigned char* data = reinterpret_cast<const unsigned char*>(signature.constData());
    BIGNUM* r = BN_bin2bn(data, half, nullptr);
    BIGNUM* s = BN_bin2bn(data + half, half, nullptr);
    openssl_ptr<ECDSA_SIG> value(ECDSA_SIG_new(), &ECDSA_SIG_free);
    if (!r || !s || !value || ECDSA_SIG_set0(value.get(), r, s) != 1)
    {
        BN_free(r);
        BN_free(s);
        ERR_clear_error();
        return QByteArray();
    }

    return encode<ECDSA_SIG>(value.get(), &i2d_ECDSA_SIG);
}

QByteArray PDFSigningKeyHelper::signAttributes(const PDFSigningKey::Impl& impl, const QByteArray& attributes, QString* errorMessage)
{
    const QString failureText = PDFSigningKey::tr("The signature cannot be created.");

    if (impl.source == PDFSigningKey::Source::Pkcs12)
    {
        // SHA-256 always (5.3: never MD5 or SHA-1); RSA PKCS#1 v1.5, ECDSA as DER (RFC 5652, RFC 5753)
        openssl_ptr<EVP_MD_CTX> context(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
        size_t size = 0;
        if (!context ||
            EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr, impl.privateKey.get()) != 1 ||
            EVP_DigestSign(context.get(), nullptr, &size, reinterpret_cast<const unsigned char*>(attributes.constData()), size_t(attributes.size())) != 1 ||
            size == 0)
        {
            ERR_clear_error();
            *errorMessage = failureText;
            return QByteArray();
        }

        QByteArray signature(qsizetype(size), char(0));
        if (EVP_DigestSign(context.get(), reinterpret_cast<unsigned char*>(signature.data()), &size, reinterpret_cast<const unsigned char*>(attributes.constData()), size_t(attributes.size())) != 1)
        {
            ERR_clear_error();
            *errorMessage = failureText;
            return QByteArray();
        }

        signature.resize(qsizetype(size));
        return signature;
    }

    if (impl.source == PDFSigningKey::Source::SystemStore)
    {
        const QByteArray hash = sha256(attributes.constData(), size_t(attributes.size()));
        const EVP_PKEY* publicKey = X509_get0_pubkey(impl.certificate.get());
        const bool isEllipticCurve = publicKey && EVP_PKEY_get_base_id(publicKey) == EVP_PKEY_EC;
        QByteArray signature = signHashBySystemKey(impl, hash, isEllipticCurve, errorMessage);
        if (isEllipticCurve && !signature.isEmpty())
        {
            signature = convertEcdsaSignature(signature);
            if (signature.isEmpty())
            {
                *errorMessage = failureText;
            }
        }
        return signature;
    }

    *errorMessage = failureText;
    return QByteArray();
}

PDFSigningKey::PDFSigningKey() = default;

PDFSigningKey::~PDFSigningKey() = default;

PDFSigningKey::PDFSigningKey(PDFSigningKey&& other) noexcept = default;

PDFSigningKey& PDFSigningKey::operator=(PDFSigningKey&& other) noexcept = default;

bool PDFSigningKey::isValid() const
{
    return m_impl && m_impl->source != Source::None && m_impl->certificate;
}

PDFSigningKey::Source PDFSigningKey::getSource() const
{
    return isValid() ? m_impl->source : Source::None;
}

QByteArray PDFSigningKey::getCertificateData() const
{
    return isValid() ? PDFSigningKeyHelper::encode<X509>(m_impl->certificate.get(), &i2d_X509) : QByteArray();
}

std::vector<QByteArray> PDFSigningKey::getChainData() const
{
    return isValid() ? m_impl->chain : std::vector<QByteArray>();
}

qint64 PDFSigningKey::estimateSignatureSize(PDFSignatureFormat format, bool isTimestamped) const
{
    // B134: the DER of the certificates, the signature value, 2048 bytes of the structure and of the
    // signed attributes (PAdES adds ESS signing-certificate-v2, PKCS#7 signing-time: both far below),
    // 16 KiB of a timestamp token
    Q_UNUSED(format);
    if (!isValid())
    {
        return 0;
    }

    qint64 size = getCertificateData().size();
    for (const QByteArray& certificate : m_impl->chain)
    {
        size += certificate.size();
    }

    const EVP_PKEY* publicKey = X509_get0_pubkey(m_impl->certificate.get());
    const int bits = publicKey ? EVP_PKEY_get_bits(publicKey) : 0;
    const qint64 keyBytes = (qint64(bits) + 7) / 8;
    const bool isEllipticCurve = publicKey && EVP_PKEY_get_base_id(publicKey) == EVP_PKEY_EC;
    size += isEllipticCurve ? 2 * keyBytes + 16 : keyBytes;

    size += 2048;
    if (isTimestamped)
    {
        size += 16 * 1024;
    }
    return size;
}

PDFSigningKey::Signature PDFSigningKey::sign(const QByteArray& data, PDFSignatureFormat format, const Timestamp* timestamp, const PDFOperationControl* control) const
{
    using Helper = PDFSigningKeyHelper;

    Signature signature;
    const QString failureText = tr("The signature cannot be created.");

    if (!isValid())
    {
        signature.errorMessage = tr("The certificate is not unlocked.");
        return signature;
    }

    if (data.size() > (std::numeric_limits<int>::max)())
    {
        signature.errorMessage = tr("The document is too large to be signed.");
        return signature;
    }

    ERR_clear_error();
    X509* certificate = m_impl->certificate.get();

    // An empty detached SignedData, then the signer without signing: OpenSSL would add signing-time at
    // its signing (R6-28), so the signed attributes are built and signed here, the same way for the key
    // of a .pfx and for the key of the system store (Q6.1, Q6.2: one CMS API)
    openssl_ptr<CMS_ContentInfo> cms(CMS_sign(nullptr, nullptr, nullptr, nullptr, CMS_PARTIAL | CMS_DETACHED | CMS_BINARY), &CMS_ContentInfo_free);
    if (!cms || CMS_set_detached(cms.get(), 1) != 1)
    {
        ERR_clear_error();
        signature.errorMessage = failureText;
        return signature;
    }

    // The key of the system store is not in the process: CMS takes the public key (it only sets the
    // algorithms with it, the signature value is set below)
    EVP_PKEY* signerKey = m_impl->privateKey ? m_impl->privateKey.get() : X509_get0_pubkey(certificate);
    unsigned int signerFlags = CMS_PARTIAL | CMS_BINARY | CMS_NOSMIMECAP;
    if (format == PDFSignatureFormat::PAdES)
    {
        // ETSI EN 319 122-1, 5.2.2.3 / EN 319 142-1, 5.2.2: ESS signing-certificate-v2 with SHA-256
        signerFlags |= CMS_CADES;
    }

    CMS_SignerInfo* signerInfo = signerKey ? CMS_add1_signer(cms.get(), certificate, signerKey, EVP_sha256(), signerFlags) : nullptr;
    if (!signerInfo)
    {
        ERR_clear_error();
        signature.errorMessage = failureText;
        return signature;
    }

    for (const QByteArray& chainCertificateData : m_impl->chain)
    {
        const unsigned char* pointer = reinterpret_cast<const unsigned char*>(chainCertificateData.constData());
        openssl_ptr<X509> chainCertificate(d2i_X509(nullptr, &pointer, long(chainCertificateData.size())), &X509_free);
        if (!chainCertificate || CMS_add1_cert(cms.get(), chainCertificate.get()) != 1)
        {
            ERR_clear_error();
            signature.errorMessage = failureText;
            return signature;
        }
    }

    // RFC 5652, 5.3, 11.1, 11.2: content-type id-data and message-digest SHA-256 of the covered bytes;
    // signing-time only by the compatible format (PAdES: the time is /M of the dictionary, ETSI EN 319
    // 142-1, 5.2.1)
    const QByteArray messageDigest = Helper::sha256(data.constData(), size_t(data.size()));
    bool isAttributeAdded = !messageDigest.isEmpty() &&
                            CMS_signed_add1_attr_by_NID(signerInfo, NID_pkcs9_contentType, V_ASN1_OBJECT, OBJ_nid2obj(NID_pkcs7_data), -1) == 1 &&
                            CMS_signed_add1_attr_by_NID(signerInfo, NID_pkcs9_messageDigest, V_ASN1_OCTET_STRING, messageDigest.constData(), int(messageDigest.size())) == 1;
    if (isAttributeAdded && format == PDFSignatureFormat::Pkcs7)
    {
        openssl_ptr<ASN1_TIME> signingTime(X509_gmtime_adj(nullptr, 0), &ASN1_TIME_free);
        isAttributeAdded = signingTime && CMS_signed_add1_attr_by_NID(signerInfo, NID_pkcs9_signingTime, signingTime->type, signingTime.get(), -1) == 1;
    }

    const QByteArray signedAttributes = isAttributeAdded ? Helper::encodeSignedAttributes(signerInfo) : QByteArray();
    if (signedAttributes.isEmpty())
    {
        ERR_clear_error();
        signature.errorMessage = failureText;
        return signature;
    }

    QString signingError;
    const QByteArray signatureValue = Helper::signAttributes(*m_impl, signedAttributes, &signingError);
    ASN1_OCTET_STRING* signatureValueString = CMS_SignerInfo_get0_signature(signerInfo);
    if (signatureValue.isEmpty() || !signatureValueString ||
        ASN1_STRING_set(signatureValueString, signatureValue.constData(), int(signatureValue.size())) != 1)
    {
        ERR_clear_error();
        signature.errorMessage = signingError.isEmpty() ? failureText : signingError;
        return signature;
    }

    // RFC 3161, Appendix A: the timestamp of the signature value as the unsigned attribute
    // id-aa-timeStampToken (PAdES B-T); one request of the authority
    if (timestamp)
    {
        if (!timestamp->client)
        {
            signature.timestamp.status = PDFTimestampClient::Status::NoTransport;
            signature.timestamp.errorMessage = PDFTimestampClient::tr("The timestamp authority cannot be contacted: the application has no network access for it.");
            signature.errorMessage = signature.timestamp.errorMessage;
            return signature;
        }

        const QByteArray signatureValueDigest = Helper::sha256(signatureValue.constData(), size_t(signatureValue.size()));
        signature.timestamp = timestamp->client->request(timestamp->url, signatureValueDigest, control);
        if (!signature.timestamp.isGranted())
        {
            if (signature.timestamp.errorMessage.isEmpty())
            {
                signature.timestamp.errorMessage = PDFTimestampClient::tr("The timestamp authority did not give the timestamp.");
            }
            signature.errorMessage = signature.timestamp.errorMessage;
            return signature;
        }

        const QByteArray& token = signature.timestamp.token;
        if (CMS_unsigned_add1_attr_by_NID(signerInfo, NID_id_smime_aa_timeStampToken, V_ASN1_SEQUENCE, token.constData(), int(token.size())) != 1)
        {
            ERR_clear_error();
            signature.errorMessage = tr("Timestamp cannot be added to the signature.");
            return signature;
        }
    }

    const QByteArray encodedSignature = Helper::encode<CMS_ContentInfo>(cms.get(), &i2d_CMS_ContentInfo);
    if (encodedSignature.isEmpty())
    {
        ERR_clear_error();
        signature.errorMessage = failureText;
        return signature;
    }

    // B126: the result is checked - the signature read back from its DER verifies over the data (the
    // signature value over the signed attributes, the message digest); the trust is not decided here
    // (CMS_CADES of CMS_verify would build the chain of the certificate)
    const unsigned char* encodedPointer = reinterpret_cast<const unsigned char*>(encodedSignature.constData());
    openssl_ptr<CMS_ContentInfo> readBack(d2i_CMS_ContentInfo(nullptr, &encodedPointer, long(encodedSignature.size())), &CMS_ContentInfo_free);
    openssl_ptr<BIO> content(BIO_new_mem_buf(data.constData(), int(data.size())), &BIO_free_all);
    if (!readBack || !content || CMS_verify(readBack.get(), nullptr, nullptr, content.get(), nullptr, CMS_BINARY | CMS_NO_SIGNER_CERT_VERIFY) != 1)
    {
        ERR_clear_error();
        signature.errorMessage = failureText;
        return signature;
    }

    ERR_clear_error();
    signature.data = encodedSignature;
    return signature;
}

bool PDFSignatureFactory::sign(const PDFCertificateEntry& certificateEntry,
                               QString password,
                               QByteArray data,
                               QByteArray& result)
{
    // B112, B113: one implementation (unlock + PDFSigningKey); the copies of the password are cleared
    QByteArray passwordUtf8 = password.toUtf8();
    PDFSecureMemory::clear(password);
    PDFSigningKey key = unlock(certificateEntry, passwordUtf8, 0, nullptr);
    PDFSecureMemory::clear(passwordUtf8);

    if (!key.isValid())
    {
        return false;
    }

    PDFSigningKey::Signature signature = key.sign(data, PDFSignatureFormat::Pkcs7, nullptr, nullptr);
    if (!signature.isSigned())
    {
        return false;
    }

    result = signature.data;
    return true;
}

bool PDFSignatureFactory::signWithTimestamp(const PDFCertificateEntry& certificateEntry,
                                            QString password,
                                            QByteArray data,
                                            const TimestampSettings& timestampSettings,
                                            QByteArray& result,
                                            QString& errorMessage)
{
    errorMessage.clear();

    QByteArray passwordUtf8 = password.toUtf8();
    PDFSecureMemory::clear(password);
    PDFSigningKey key = unlock(certificateEntry, passwordUtf8, 0, nullptr);
    PDFSecureMemory::clear(passwordUtf8);

    if (!key.isValid())
    {
        // The failure of the signing itself is reported by the caller (it is not caused by the timestamp)
        return false;
    }

    // The client of the new application (SECURITY_TODO 52: the old applications call it from their
    // thread of the user interface)
    const PDFTimestampClient client(PDFTimestampClient::createNetworkTransport(), timestampSettings.timeoutMilliseconds);
    PDFSigningKey::Timestamp timestamp;
    timestamp.client = &client;
    timestamp.url = timestampSettings.url;

    PDFSigningKey::Signature signature = key.sign(data, PDFSignatureFormat::Pkcs7, &timestamp, nullptr);
    if (!signature.isSigned())
    {
        errorMessage = signature.timestamp.errorMessage;
        return false;
    }

    result = signature.data;
    return true;
}

bool PDFSignatureFactory::createTimestampToken(QByteArray data,
                                               const TimestampSettings& timestampSettings,
                                               QByteArray& result,
                                               QString& errorMessage)
{
    result.clear();
    errorMessage.clear();

    // The client of the new application (moved there, plan 4.1)
    const QByteArray digest = PDFSigningKeyHelper::sha256(data.constData(), size_t(data.size()));
    const PDFTimestampClient client(PDFTimestampClient::createNetworkTransport(), timestampSettings.timeoutMilliseconds);
    const PDFTimestampClient::Result timestamp = client.request(timestampSettings.url, digest, nullptr);
    if (!timestamp.isGranted())
    {
        errorMessage = timestamp.errorMessage.isEmpty() ? tr("Timestamp cannot be created.") : timestamp.errorMessage;
        return false;
    }

    result = timestamp.token;
    return true;
}

bool PDFSignatureFactory::verifyTimestampToken(const QByteArray& data, const QByteArray& token)
{
    return PDFTimestampClient::verifyToken(data, token);
}

PDFSigningKey PDFSignatureFactory::unlock(const PDFCertificateEntry& entry, const QByteArray& password, quintptr parentWindow, QString* errorMessage)
{
    using Helper = PDFCertificateManagerHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    PDFSigningKey key;
    std::unique_ptr<PDFSigningKey::Impl> impl = std::make_unique<PDFSigningKey::Impl>();
    ERR_clear_error();

    if (!entry.pkcs12.isEmpty())
    {
        // 6.3: a file of a hostile count of iterations is refused before its password is tried
        openssl_ptr<PKCS12> pkcs12(Helper::readPkcs12(entry.pkcs12), &PKCS12_free);
        if (!pkcs12 || !PDFCertificateManager::isPkcs12IterationCountAcceptable(entry.pkcs12, password))
        {
            ERR_clear_error();
            error = tr("The certificate file cannot be used.");
            return key;
        }

        EVP_PKEY* keyPtr = nullptr;
        X509* certificatePtr = nullptr;
        STACK_OF(X509)* certificatesPtr = nullptr;
        const bool isParsed = PKCS12_parse(pkcs12.get(), password.isEmpty() ? nullptr : password.constData(), &keyPtr, &certificatePtr, &certificatesPtr) == 1;

        openssl_ptr<EVP_PKEY> privateKey(keyPtr, &EVP_PKEY_free);
        openssl_ptr<X509> certificate(certificatePtr, &X509_free);
        openssl_ptr<STACK_OF(X509)> certificates(certificatesPtr, &Helper::freeCertificateStack);
        if (!isParsed)
        {
            // Figma 434:7143
            ERR_clear_error();
            error = tr("Password to open certificate is invalid.");
            return key;
        }

        if (!privateKey || !certificate || X509_check_private_key(certificate.get(), privateKey.get()) != 1)
        {
            ERR_clear_error();
            error = tr("The certificate file has no private key of its certificate.");
            return key;
        }

        // 5.3, 7.2: the chain of the PKCS#12 goes into the signature, at most 16 certificates and 256 KiB
        const int chainCount = certificates ? sk_X509_num(certificates.get()) : 0;
        qint64 chainSize = 0;
        if (chainCount > PDFCertificateManager::MAXIMUM_CHAIN_CERTIFICATES)
        {
            error = tr("The certificate file has too many certificates.");
            return key;
        }

        for (int i = 0; i < chainCount; ++i)
        {
            QByteArray chainCertificate = PDFSigningKeyHelper::encode<X509>(sk_X509_value(certificates.get(), i), &i2d_X509);
            chainSize += chainCertificate.size();
            if (chainCertificate.isEmpty() || chainSize > PDFCertificateManager::MAXIMUM_CHAIN_SIZE)
            {
                ERR_clear_error();
                error = tr("The certificate file has too many certificates.");
                return key;
            }
            impl->chain.emplace_back(std::move(chainCertificate));
        }

        impl->certificate = std::move(certificate);
        impl->privateKey = std::move(privateKey);
        impl->source = PDFSigningKey::Source::Pkcs12;

        if (!PDFSigningKeyHelper::checkCertificate(impl->certificate.get(), &error))
        {
            return key;
        }
    }
    else if (entry.type == PDFCertificateEntry::EntryType::System)
    {
        // Q6.2: the certificate is checked before its key is opened (no PIN for an unusable one)
        const QByteArray certificateData = entry.info.getCertificateData();
        const unsigned char* pointer = reinterpret_cast<const unsigned char*>(certificateData.constData());
        openssl_ptr<X509> certificate(d2i_X509(nullptr, &pointer, long(certificateData.size())), &X509_free);
        if (!PDFSigningKeyHelper::checkCertificate(certificate.get(), &error))
        {
            ERR_clear_error();
            return key;
        }

        impl->certificate = std::move(certificate);
        if (!PDFSigningKeyHelper::openSystemKey(certificateData, parentWindow, *impl, &error))
        {
            return key;
        }
        impl->source = PDFSigningKey::Source::SystemStore;
    }
    else
    {
        error = tr("The certificate has no private key.");
        return key;
    }

    ERR_clear_error();
    key.m_impl = std::move(impl);
    return key;
}

bool PDFSignatureFactory::isPasswordValid(const QByteArray& pkcs12Data, const QByteArray& password)
{
    using Helper = PDFCertificateManagerHelper;

    openssl_ptr<PKCS12> pkcs12(Helper::readPkcs12(pkcs12Data), &PKCS12_free);
    if (!pkcs12 || !PDFCertificateManager::isPkcs12IterationCountAcceptable(pkcs12Data, password))
    {
        ERR_clear_error();
        return false;
    }

    EVP_PKEY* keyPtr = nullptr;
    X509* certificatePtr = nullptr;
    STACK_OF(X509)* certificatesPtr = nullptr;
    const bool isParsed = PKCS12_parse(pkcs12.get(), password.isEmpty() ? nullptr : password.constData(), &keyPtr, &certificatePtr, &certificatesPtr) == 1;

    // The key is dropped at once (6.3)
    openssl_ptr<EVP_PKEY> privateKey(keyPtr, &EVP_PKEY_free);
    openssl_ptr<X509> certificate(certificatePtr, &X509_free);
    openssl_ptr<STACK_OF(X509)> certificates(certificatesPtr, &Helper::freeCertificateStack);

    const bool isValid = isParsed && privateKey && certificate && X509_check_private_key(certificate.get(), privateKey.get()) == 1;
    ERR_clear_error();
    return isValid;
}

QByteArray PDFSignatureFactory::getSubFilter(PDFSignatureFormat format)
{
    switch (format)
    {
        case PDFSignatureFormat::PAdES:
            return QByteArrayLiteral("ETSI.CAdES.detached");
        case PDFSignatureFormat::Pkcs7:
            return QByteArrayLiteral("adbe.pkcs7.detached");
    }
    return QByteArray();
}

QByteArray PDFSignatureFactory::createCertificate(const PDFCertificateManager::NewCertificateInfo& info, const std::atomic<bool>* cancelFlag, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    // Q6.14: 2048 (the default), 3072 or 4096 bits
    const auto& lengths = PDFCertificateManager::RSA_KEY_LENGTHS;
    if (std::find(std::begin(lengths), std::end(lengths), info.rsaKeyLength) == std::end(lengths))
    {
        error = PDFCertificateManager::tr("Invalid length of the key: %1 bits.").arg(info.rsaKeyLength);
        return QByteArray();
    }

    QByteArray pkcs12 = PDFCertificateManagerHelper::createPkcs12(info, cancelFlag, error);
    if (pkcs12.isEmpty() && error.isEmpty())
    {
        error = PDFCertificateManager::tr("The certificate cannot be created.");
    }
    return pkcs12;
}

}   // namespace pdf

#ifdef Q_OS_WIN
#include <Windows.h>
#include <wincrypt.h>
#include <ncrypt.h>
#if defined(PDF4QT_USE_PRAGMA_LIB)
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ncrypt.lib")
#endif
#endif

bool pdf::PDFSigningKeyHelper::openSystemKey(const QByteArray& certificateData, quintptr parentWindow, PDFSigningKey::Impl& impl, QString* errorMessage)
{
#ifdef Q_OS_WIN
    std::shared_ptr<const PDFPersonalCertificateStore> personalStore = PDFPersonalCertificateStore::getStore();
    HCERTSTORE store = personalStore ? static_cast<HCERTSTORE>(personalStore->openStore()) : nullptr;
    if (!store)
    {
        *errorMessage = PDFSigningKey::tr("The certificate store of the system cannot be opened.");
        return false;
    }

    PCCERT_CONTEXT wantedCertificate = CertCreateCertificateContext(X509_ASN_ENCODING, reinterpret_cast<const BYTE*>(certificateData.constData()), DWORD(certificateData.size()));
    PCCERT_CONTEXT certificate = wantedCertificate ? CertFindCertificateInStore(store, X509_ASN_ENCODING, 0, CERT_FIND_EXISTING, wantedCertificate, nullptr) : nullptr;
    if (wantedCertificate)
    {
        CertFreeCertificateContext(wantedCertificate);
    }

    if (!certificate)
    {
        CertCloseStore(store, 0);
        *errorMessage = PDFSigningKey::tr("The certificate is not in the certificate store of the system.");
        return false;
    }

    // The PIN dialog of the system (shown at the signing) belongs to the window of the application
    HWND window = reinterpret_cast<HWND>(parentWindow);
    DWORD flags = CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_COMPARE_KEY_FLAG;
    if (window)
    {
        flags |= CRYPT_ACQUIRE_WINDOW_HANDLE_FLAG;
    }

    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE keyHandle = 0;
    DWORD keySpec = 0;
    BOOL mustFree = FALSE;
    bool isAcquired = CryptAcquireCertificatePrivateKey(certificate, flags, window ? &window : nullptr, &keyHandle, &keySpec, &mustFree) == TRUE;
    if (!isAcquired && GetLastError() == DWORD(CRYPT_E_NO_KEY_PROPERTY))
    {
        // A key, which is not persisted (a store in the memory, PKCS12_NO_PERSIST_KEY), has only its
        // handle in CERT_KEY_CONTEXT_PROP_ID: it is used through the cache of the certificate context,
        // which this key holds until its end (the handle belongs to the context, mustFree is false)
        isAcquired = CryptAcquireCertificatePrivateKey(certificate, flags | CRYPT_ACQUIRE_CACHE_FLAG, window ? &window : nullptr, &keyHandle, &keySpec, &mustFree) == TRUE;
    }

    if (!isAcquired)
    {
        CertFreeCertificateContext(certificate);
        CertCloseStore(store, 0);
        *errorMessage = PDFSigningKey::tr("The private key of the certificate cannot be used.");
        return false;
    }

    impl.systemCertificate = const_cast<CERT_CONTEXT*>(certificate);
    impl.systemKey = quintptr(keyHandle);
    impl.systemKeySpec = keySpec;
    impl.isSystemKeyOwned = mustFree == TRUE;

    // The chain from the store and the cache of the system, never from the network (S6.6: the network
    // only for the authority); the certificate of the signer itself is not repeated
    CERT_CHAIN_PARA chainParameters = { };
    chainParameters.cbSize = sizeof(chainParameters);
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    if (CertGetCertificateChain(nullptr, certificate, nullptr, store, &chainParameters, CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL, nullptr, &chain) && chain)
    {
        if (chain->cChain > 0 && chain->rgpChain[0])
        {
            const CERT_SIMPLE_CHAIN* simpleChain = chain->rgpChain[0];
            qint64 chainSize = 0;
            for (DWORD i = 1; i < simpleChain->cElement; ++i)
            {
                const CERT_CONTEXT* element = simpleChain->rgpElement[i]->pCertContext;
                chainSize += element->cbCertEncoded;
                if (int(impl.chain.size()) >= PDFCertificateManager::MAXIMUM_CHAIN_CERTIFICATES || chainSize > PDFCertificateManager::MAXIMUM_CHAIN_SIZE)
                {
                    break;
                }
                impl.chain.emplace_back(reinterpret_cast<const char*>(element->pbCertEncoded), qsizetype(element->cbCertEncoded));
            }
        }
        CertFreeCertificateChain(chain);
    }

    CertCloseStore(store, 0);
    return true;
#else
    Q_UNUSED(certificateData);
    Q_UNUSED(parentWindow);
    Q_UNUSED(impl);
    *errorMessage = PDFSigningKey::tr("This system has no certificate store for signing.");
    return false;
#endif
}

QByteArray pdf::PDFSigningKeyHelper::signHashBySystemKey(const PDFSigningKey::Impl& impl, const QByteArray& hash, bool isEllipticCurve, QString* errorMessage)
{
    const QString failureText = PDFSigningKey::tr("The key of the certificate store of the system cannot sign.");

#ifdef Q_OS_WIN
    if (hash.size() != 32 || !impl.systemKey)
    {
        *errorMessage = failureText;
        return QByteArray();
    }

    if (impl.systemKeySpec == CERT_NCRYPT_KEY_SPEC)
    {
        // CNG: one signature (B126: the result of every call is checked; no trial signature, B134)
        NCRYPT_KEY_HANDLE key = NCRYPT_KEY_HANDLE(impl.systemKey);
        BCRYPT_PKCS1_PADDING_INFO padding = { };
        padding.pszAlgId = BCRYPT_SHA256_ALGORITHM;
        void* paddingInfo = isEllipticCurve ? nullptr : &padding;
        const DWORD flags = isEllipticCurve ? 0 : BCRYPT_PAD_PKCS1;

        PBYTE hashData = reinterpret_cast<PBYTE>(const_cast<char*>(hash.constData()));
        DWORD size = 0;
        if (NCryptSignHash(key, paddingInfo, hashData, DWORD(hash.size()), nullptr, 0, &size, flags) != ERROR_SUCCESS || size == 0)
        {
            *errorMessage = failureText;
            return QByteArray();
        }

        QByteArray signature(qsizetype(size), char(0));
        if (NCryptSignHash(key, paddingInfo, hashData, DWORD(hash.size()), reinterpret_cast<PBYTE>(signature.data()), size, &size, flags) != ERROR_SUCCESS)
        {
            *errorMessage = failureText;
            return QByteArray();
        }

        signature.resize(qsizetype(size));
        return signature;
    }

    // A legacy CSP (RSA only): SHA-256 needs a provider of the type PROV_RSA_AES
    if (isEllipticCurve)
    {
        *errorMessage = failureText;
        return QByteArray();
    }

    HCRYPTHASH hashHandle = 0;
    QByteArray signature;
    DWORD size = 0;
    if (CryptCreateHash(HCRYPTPROV(impl.systemKey), CALG_SHA_256, 0, 0, &hashHandle) &&
        CryptSetHashParam(hashHandle, HP_HASHVAL, reinterpret_cast<const BYTE*>(hash.constData()), 0) &&
        CryptSignHashW(hashHandle, impl.systemKeySpec, nullptr, 0, nullptr, &size) && size > 0)
    {
        signature.resize(qsizetype(size));
        if (CryptSignHashW(hashHandle, impl.systemKeySpec, nullptr, 0, reinterpret_cast<BYTE*>(signature.data()), &size))
        {
            // CryptoAPI gives the signature little endian
            signature.resize(qsizetype(size));
            std::reverse(signature.begin(), signature.end());
        }
        else
        {
            signature.clear();
        }
    }

    if (hashHandle)
    {
        CryptDestroyHash(hashHandle);
    }

    if (signature.isEmpty())
    {
        *errorMessage = failureText;
    }
    return signature;
#else
    Q_UNUSED(impl);
    Q_UNUSED(hash);
    Q_UNUSED(isEllipticCurve);
    *errorMessage = failureText;
    return QByteArray();
#endif
}

void pdf::PDFSigningKeyHelper::releaseSystemKey(PDFSigningKey::Impl& impl)
{
#ifdef Q_OS_WIN
    if (impl.systemKey && impl.isSystemKeyOwned)
    {
        if (impl.systemKeySpec == CERT_NCRYPT_KEY_SPEC)
        {
            NCryptFreeObject(NCRYPT_KEY_HANDLE(impl.systemKey));
        }
        else
        {
            CryptReleaseContext(HCRYPTPROV(impl.systemKey), 0);
        }
    }

    if (impl.systemCertificate)
    {
        CertFreeCertificateContext(static_cast<PCCERT_CONTEXT>(impl.systemCertificate));
    }
#endif

    impl.systemKey = 0;
    impl.systemCertificate = nullptr;
    impl.isSystemKeyOwned = false;
}

#if defined(PDF4QT_COMPILER_MINGW) || defined(PDF4QT_COMPILER_GCC)
#pragma GCC diagnostic pop
#endif

#if defined(PDF4QT_COMPILER_MSVC)
#pragma warning(pop)
#endif
