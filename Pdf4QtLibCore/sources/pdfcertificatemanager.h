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

#ifndef PDFCERTIFICATEMANAGER_H
#define PDFCERTIFICATEMANAGER_H

#include "pdfglobal.h"
#include "pdfcertificatestore.h"

#include "pdftimestampclient.h"

#include <QString>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfoList>
#include <QStringList>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace pdf
{

class PDFOperationControl;

/// The format of a new digital signature (plan of step 6 of phase 2, owner answer Q6.1: the user
/// chooses; the parameter format of Sign with Certificate, of MCP and of PdfTool). Both are made by
/// one CMS API (CMS_sign, R6-28); a document timestamp is always ETSI.RFC3161.
enum class PDFSignatureFormat
{
    PAdES,  ///< /SubFilter /ETSI.CAdES.detached (ISO 32000-2, 12.8.3.4; ETSI EN 319 142-1 PAdES B-B, with a timestamp B-T): the signed attribute ESS signing-certificate-v2, no signing-time (the time is only /M of the signature dictionary) - the default
    Pkcs7   ///< /SubFilter /adbe.pkcs7.detached (12.8.3.3), "Compatible (PKCS#7)": signing-time allowed
};

/// The unlocked private key of one certificate of the user for the time of one run (plan of step 6 of
/// phase 2, 4.1, 5.3, 6.1, 10.3; package P1b implements it): a .pfx file (an EVP_PKEY with the chain of
/// the PKCS#12, at most 16 certificates and 256 KiB) or a certificate of the system store of Windows
/// (a handle of the certificate; the key never enters the process, the system asks for a PIN in its own
/// dialog, the parent of which is \p parentWindow of unlock).
///
/// Contract:
///  - RAII, move-only: the destructor frees the key (EVP_PKEY_free, which clears the private parts) and
///    the handle; nothing of it is copied, logged or written. One unlock per run (no trial signature,
///    B134: the size of the signature comes from estimateSignatureSize).
///  - Threads: created and used by the thread of the run (sign never runs in the GUI thread, B120); the
///    run calls PDFSignatureHandler::releaseThreadResources at its end (B137).
///  - Algorithms: SHA-256 (RSA, ECDSA); a key, which would sign by MD5 or SHA-1, is refused (5.3).
///  - The certificate must be valid at the time of the signing and have digitalSignature or
///    nonRepudiation; an extended key usage, when present, must allow the signing of documents (5.3,
///    B116) - checked by unlock.
class PDF4QTLIBCORESHARED_EXPORT PDFSigningKey
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFSigningKey)

public:
    /// Where the key is
    enum class Source
    {
        None,           ///< No key (a failed unlock)
        Pkcs12,         ///< A .pfx file
        SystemStore     ///< The certificate store of Windows (Q6.2)
    };

    /// A timestamp of the signature value, which a signature with a timestamp gets as the unsigned
    /// attribute id-aa-timeStampToken of its SignerInfo (RFC 3161, Appendix A; PAdES B-T)
    struct Timestamp
    {
        const PDFTimestampClient* client = nullptr;     ///< The client of the authority (the context of the run)
        QString url;                                    ///< The address of the authority
    };

    /// The result of sign
    struct Signature
    {
        QByteArray data;                        ///< DER of the CMS ContentInfo (SignedData, detached), empty on a failure
        PDFTimestampClient::Result timestamp;   ///< The answer of the authority of a signature with a timestamp
        QString errorMessage;                   ///< Translated reason of a failure (never a password, never a key)

        bool isSigned() const { return !data.isEmpty(); }
    };

    PDFSigningKey();
    ~PDFSigningKey();

    PDFSigningKey(PDFSigningKey&& other) noexcept;
    PDFSigningKey& operator=(PDFSigningKey&& other) noexcept;
    PDFSigningKey(const PDFSigningKey&) = delete;
    PDFSigningKey& operator=(const PDFSigningKey&) = delete;

    /// The key can sign
    bool isValid() const;

    Source getSource() const;

    /// The certificate of the signer (DER of the X.509 certificate); empty for an invalid key
    QByteArray getCertificateData() const;

    /// The certificates of the chain, which go into the signature besides the certificate of the signer
    /// (DER; the PKCS#12, or the chain of the system store - CertGetCertificateChain)
    std::vector<QByteArray> getChainData() const;

    /// The size of the DER of a signature of this key in bytes, at least (B134: the reservation of
    /// /Contents is computed, no trial signature contacts the authority or the smart card): the DER of the
    /// chain, the length of the key, 2048 bytes and 16 KiB with a timestamp
    /// \param format The format of the signature
    /// \param isTimestamped The signature gets a timestamp
    qint64 estimateSignatureSize(PDFSignatureFormat format, bool isTimestamped) const;

    /// Signs the bytes covered by /ByteRange: a detached CMS SignedData by the format (PAdES without
    /// signing-time and with ESS signing-certificate-v2; R6-28 - CMS_sign adds signing-time itself, it is
    /// removed by CMS_PARTIAL), SHA-256, with the timestamp of the signature value when \p timestamp is
    /// given (one request of the authority). A system store key may show the PIN dialog of the system.
    /// \param data The bytes covered by the signature
    /// \param format The format (Q6.1)
    /// \param timestamp The authority of a signature with a timestamp, nullptr = none
    /// \param control Cancellation of the run (aborts the request of the authority, not a PIN dialog of the system)
    Signature sign(const QByteArray& data, PDFSignatureFormat format, const Timestamp* timestamp, const PDFOperationControl* control) const;

private:
    friend class PDFSignatureFactory;
    friend class PDFSigningKeyHelper;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

class PDF4QTLIBCORESHARED_EXPORT PDFCertificateManager
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFCertificateManager)

public:
    PDFCertificateManager();

    struct NewCertificateInfo
    {
        QString fileName;
        QString privateKeyPasword;

        QString certCountryCode;
        QString certOrganization;
        QString certOrganizationUnit;
        QString certCommonName;
        QString certEmail;

        int rsaKeyLength = 2048;                        ///< B130: 2048 bits by default
        int validityInSeconds = 2 * 365 * 24 * 3600;
        long serialNumber = 1;                          ///< 1 (the default) = a random serial number of 127 bits (RFC 5280, 4.1.2.2)
    };

    /// Shortest and longest RSA key of a certificate created by createCertificate (the old applications
    /// and the tests) in bits; the new application offers only RSA_KEY_LENGTHS (owner answer Q6.14)
    static constexpr int MINIMUM_RSA_KEY_LENGTH = 1024;
    static constexpr int MAXIMUM_RSA_KEY_LENGTH = 16384;

    /// The lengths of the RSA key of PDFSignatureFactory::createCertificate (owner answer Q6.14: no 1024)
    static constexpr int RSA_KEY_LENGTHS[] = { 2048, 3072, 4096 };

    /// The default length of the RSA key of a new certificate (B130, Q6.14)
    static constexpr int DEFAULT_RSA_KEY_LENGTH = 2048;

    /// Largest .pfx file, which is read from a certificate folder (a file of a hostile size in
    /// the folder is skipped, never read whole; plan of step 5 of phase 2, 7.2: 1 MiB as the import)
    static constexpr qint64 MAXIMUM_CERTIFICATE_FILE_SIZE = 1024 * 1024;

    /// Most iterations of the MAC or of a password based encryption of a PKCS#12 (plan of step 6 of
    /// phase 2, 6.3, R6-28): a file with more is refused before its password is tried - "The certificate
    /// file cannot be used" (a hostile .pfx would block the thread for minutes). For scrypt the product
    /// of its cost, block size and parallelization.
    static constexpr qint64 MAXIMUM_PKCS12_ITERATIONS = 10000000;

    /// Most certificates of the chain of a signing key besides the certificate of the signer, and
    /// their largest size together (plan 5.3, 7.2)
    static constexpr int MAXIMUM_CHAIN_CERTIFICATES = 16;
    static constexpr qint64 MAXIMUM_CHAIN_SIZE = 256 * 1024;

    /// True, if the PKCS#12 does not need more than MAXIMUM_PKCS12_ITERATIONS iterations of its MAC and
    /// of each password based encryption, which can be read (6.3): the MAC, the encrypted safes and the
    /// shrouded key bags; the bags of an encrypted safe are inspected after its decryption by
    /// \p password. Data, which are no PKCS#12, or an algorithm, whose iterations cannot be read, give
    /// false. Called before every PKCS12_parse of this class (also the listing with the empty password).
    /// \param pkcs12 The PKCS#12 data
    /// \param password The password in UTF-8 (empty = none)
    static bool isPkcs12IterationCountAcceptable(const QByteArray& pkcs12, const QByteArray& password);

    /// Creates a self-signed certificate with its private key into the PKCS#12 file of
    /// \p info.fileName, protected by \p info.privateKeyPasword (UTF-8): the certificate of
    /// PDFSignatureFactory::createCertificate (X.509 version 3, keyUsage digitalSignature,
    /// nonRepudiation, keyEncipherment - a recipient of the encryption by a certificate, ISO 32000-2,
    /// 7.6.5, B99 -, basicConstraints CA:FALSE, SKI, AKI; B130), a key of MINIMUM_RSA_KEY_LENGTH to
    /// MAXIMUM_RSA_KEY_LENGTH bits (the old applications). The generation runs in the calling thread. An
    /// existing file is replaced (the caller asks before). The password buffers are cleared.
    /// \param info The certificate
    /// \param errorMessage Translated reason of a failure (may be null); never contains the password
    /// \returns true, when the file was written
    bool createCertificate(const NewCertificateInfo& info, QString* errorMessage = nullptr);

    /// Returns certificates, which can be used by the application - personal
    /// certificates from the system storage and certificates stored in the
    /// certificate directory of the application. Certificates from the
    /// certificate directory are never filtered out, because they are
    /// usually password protected and their key usage cannot be read.
    ///
    /// The .pfx files (B103, B107, R5-18, owner answers Q5.5, Q5.11): every PKCS#12 file of the
    /// folder of the application, then, read only (isReadOnlyLocation), of the folders of the
    /// old applications (getOldCertificateDirectories) - also a file protected by its password,
    /// whose certificate cannot be read without it: its info is then empty (the application
    /// shows its file name, "Details are shown after the password"). An entry of a file has the
    /// type User, its PKCS#12, its file name and its absolute path. A file larger than
    /// MAXIMUM_CERTIFICATE_FILE_SIZE or no PKCS#12 is skipped.
    /// \param filter Which certificates from the system storage are returned
    static PDFCertificateEntries getCertificates(PDFCertificateUsageFilter filter = PDFCertificateUsageFilter::Any);

    /// The folder of the .pfx files of this application (the AppDataLocation of the application
    /// name), where new and imported certificates go; the folder of setCertificateDirectories,
    /// when it is set
    static QString getCertificateDirectory();

    /// The certificate folders of the old applications PDF4QT Editor and PDF4QT Viewer (B97,
    /// owner answer Q5.5): the folder of the same organization with their application names,
    /// read only; the folder of this application (getCertificateDirectory) is never among them.
    /// A folder, which does not exist, is listed too (nothing is read of it). The folders of
    /// setCertificateDirectories, when they are set.
    static QStringList getOldCertificateDirectories();

    /// Places the certificate folders elsewhere than the data folders of the user: the tests (the
    /// owner rule: nothing of a test on the system disk - a folder under their temporary folder),
    /// later the portable mode of the application. Thread-safe; a reader of another thread sees
    /// either the old or the new folders.
    /// \param directory The folder of getCertificateDirectory; empty = the default folders again
    ///        (\p oldDirectories is then ignored)
    /// \param oldDirectories The folders of getOldCertificateDirectories (may be empty: none)
    static void setCertificateDirectories(const QString& directory, const QStringList& oldDirectories);

    static QString generateCertificateFileName();
    static bool isCertificateValid(const PDFCertificateEntry& certificateEntry, QString password);

    /// Reads the certificate of a PKCS#12 by its password (B103, owner answer Q5.11): the
    /// password is UTF-8 (B84); the PKCS#12 must hold the certificate and its private key. The
    /// returned info has getCertificateData() = the DER of the X.509 certificate. The password
    /// buffers are cleared (PDFSecureMemory).
    /// \param pkcs12 The PKCS#12 data (at most MAXIMUM_CERTIFICATE_FILE_SIZE bytes are accepted)
    /// \param password The password (empty = a file without a password)
    /// \returns the certificate, std::nullopt for a wrong password, damaged data or no private key
    static std::optional<PDFCertificateInfo> readCertificate(const QByteArray& pkcs12, const QString& password);

    /// True, if the data are a PKCS#12 (d2i_PKCS12 parses them; no password is needed) of at most
    /// MAXIMUM_CERTIFICATE_FILE_SIZE bytes - the one check of a .pfx file of the folders
    /// (getCertificates) and of the import (plan of step 5 of phase 2, 7.2)
    /// \param data The content of the file
    static bool isPkcs12(const QByteArray& data);

    /// True, if the certificate can be a recipient of the encryption by a certificate (ISO
    /// 32000-2, 7.6.5; plan of step 5 of phase 2, B99, Q5.4): its key is RSA (the key transport of
    /// PKCS#7 enveloped data) and its key usage extension, when present, has keyEncipherment
    /// (RFC 5280, 4.2.1.3: a certificate without the extension may be used for any purpose)
    /// \param certificateData DER of the X.509 certificate (PDFCertificateInfo::getCertificateData)
    static bool isUsableForEncryption(const QByteArray& certificateData);

    /// True, if the certificate may sign PDF documents (plan of step 6 of phase 2, 5.3, B116) - the one
    /// rule of PDFSignatureFactory::unlock and of the lists of certificates of the application: an RSA
    /// or ECDSA key; its key usage extension, when present, has digitalSignature or nonRepudiation (RFC
    /// 5280, 4.2.1.3); its extended key usage extension, when present, has emailProtection,
    /// documentSigning (RFC 9336), Adobe Authentic Documents Trust (1.2.840.113583.1.1.5), Microsoft
    /// Document Signing (1.3.6.1.4.1.311.10.3.12) or anyExtendedKeyUsage (RFC 5280, 4.2.1.12; owner
    /// 2026-11-15). The time of validity is not checked here (unlock checks it at the signing).
    /// \param certificateData DER of the X.509 certificate (PDFCertificateInfo::getCertificateData)
    /// \param errorMessage Translated reason, when it may not sign (may be null)
    static bool isUsableForSigning(const QByteArray& certificateData, QString* errorMessage);

    /// Copies a .pfx file of an old folder (PDFCertificateEntry::isReadOnlyLocation) into the
    /// folder of the application under its file name (owner answer Q5.5), the folder is created
    /// when needed. An existing file of the same name is never replaced: a file with the same
    /// bytes counts as copied, another one is a conflict, which the error text names.
    /// \param entry The entry (its pkcs12 is written, its pkcs12fileName names the copy)
    /// \param errorMessage Translated reason of a failure (may be null)
    /// \returns true, when the folder of the application has the file afterwards
    static bool copyCertificateToUserDirectory(const PDFCertificateEntry& entry, QString* errorMessage);

private:
    /// The .pfx files of one folder (getCertificates)
    static void addCertificateFiles(const QString& directoryPath, bool isReadOnlyLocation, PDFCertificateEntries& entries);
};

class PDF4QTLIBCORESHARED_EXPORT PDFSignatureFactory
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFSignatureFactory)

public:
    /// Settings of the RFC 3161 timestamp authority
    struct TimestampSettings
    {
        /// Url of the timestamp authority
        QString url;

        /// Timeout of the request to the timestamp authority in milliseconds
        int timeoutMilliseconds = 15000;
    };

    /// Creates the signature adbe.pkcs7.detached of the data (the old applications and the tests; the new
    /// application signs by unlock and PDFSigningKey::sign, which this function calls: one unlock, the
    /// format Pkcs7, no timestamp). Fails for a certificate, which unlock refuses.
    /// \param certificateEntry Certificate used for the signing
    /// \param password Password of the certificate (its copies are cleared)
    /// \param data Data to be signed
    /// \param result DER of the CMS signature
    static bool sign(const PDFCertificateEntry& certificateEntry,
                     QString password,
                     QByteArray data,
                     QByteArray& result);

    /// Creates the digital signature of the data and adds a RFC 3161 timestamp
    /// of the signature value as an unsigned attribute of the signer info, so
    /// the time of the signing is attested by a timestamp authority and does
    /// not depend on the clock of the computer, which created the signature.
    /// \param certificateEntry Certificate used for the signing
    /// \param password Password of the certificate
    /// \param data Data to be signed
    /// \param timestampSettings Timestamp authority to be used
    /// \param result Signature
    /// \param errorMessage Localized description of the failure
    static bool signWithTimestamp(const PDFCertificateEntry& certificateEntry,
                                  QString password,
                                  QByteArray data,
                                  const TimestampSettings& timestampSettings,
                                  QByteArray& result,
                                  QString& errorMessage);

    /// Creates the RFC 3161 timestamp token of the data. The token is used as
    /// the contents of a document timestamp (signatures with the ETSI.RFC3161
    /// subfilter), which attests, that the document existed at the time of the
    /// timestamp, without signing it.
    /// \param data Data to be timestamped
    /// \param timestampSettings Timestamp authority to be used
    /// \param result Timestamp token
    /// \param errorMessage Localized description of the failure
    static bool createTimestampToken(QByteArray data,
                                     const TimestampSettings& timestampSettings,
                                     QByteArray& result,
                                     QString& errorMessage);

    /// Returns true, if the token is a correctly signed RFC 3161 timestamp of
    /// the data - its signature, the identifier of the certificate of the
    /// authority and the message imprint of the data are verified. The
    /// certificate of the timestamp authority is deliberately not verified
    /// against the trusted certificates - the trust in the authority is
    /// decided when the signatures of the document are verified.
    /// \param data Timestamped data
    /// \param token Timestamp token
    static bool verifyTimestampToken(const QByteArray& data, const QByteArray& token);

    /// Unlocks the private key of a certificate of the user for one run (plan of step 6 of phase 2,
    /// 6.1, 10.3; package P1b): the PKCS#12 of a .pfx entry by its password (UTF-8; PKCS#12 with more
    /// than 10,000,000 iterations of its MAC or PBE is refused before PKCS12_parse - "The certificate
    /// file cannot be used", 6.3), or the certificate of the system store by its handle (the password
    /// is not used). The password is used once and never kept; the caller clears its buffer
    /// (PDFSecureMemory::clear, B112, B113).
    /// \param entry The certificate (PDFCertificateEntry::pkcs12 of a .pfx, the system store otherwise)
    /// \param password The password of the .pfx file in UTF-8 (empty = a file without a password)
    /// \param parentWindow The native handle of the window, to which the PIN dialog of the system belongs (CRYPT_ACQUIRE_WINDOW_HANDLE_FLAG; 0 = none)
    /// \param errorMessage Translated reason of a failure: "Password to open certificate is invalid." (434:7143), an expired certificate, no usage for signing
    /// \returns the key, an invalid key on a failure
    static PDFSigningKey unlock(const PDFCertificateEntry& entry, const QByteArray& password, quintptr parentWindow, QString* errorMessage);

    /// The password opens the PKCS#12 (the live check of the password of Sign with Certificate,
    /// 434:7143, 6.3: one job, the key is dropped at once; the limit of the iterations of unlock)
    static bool isPasswordValid(const QByteArray& pkcs12, const QByteArray& password);

    /// The /SubFilter of a format ("ETSI.CAdES.detached", "adbe.pkcs7.detached")
    static QByteArray getSubFilter(PDFSignatureFormat format);

    /// Creates a self-signed certificate with its private key into the bytes of a PKCS#12 protected by
    /// \p info.privateKeyPasword (plan of step 6 of phase 2, 5.3, 6.4, B130; package P1b): X.509 version 3,
    /// keyUsage digitalSignature, nonRepudiation, keyEncipherment; basicConstraints CA:FALSE; SKI and
    /// AKI; RSA by the EVP API (2048 default, 3072, 4096 - owner answer Q6.14); the result of X509_sign
    /// checked. The generation of the key can be canceled: the callback of EVP_PKEY_CTX_set_cb reads
    /// \p cancelFlag. Nothing is written to a file (the GUI thread writes the bytes by QSaveFile, 6.4);
    /// the password buffers are cleared. Reentrant: one job of the dialog (QtConcurrent::run).
    /// \param info The certificate (fileName is not used)
    /// \param cancelFlag Set by the dialog to cancel (may be null)
    /// \param errorMessage Translated reason of a failure or of the cancellation (may be null)
    /// \returns the PKCS#12, empty on a failure or a cancellation
    static QByteArray createCertificate(const PDFCertificateManager::NewCertificateInfo& info, const std::atomic<bool>* cancelFlag, QString* errorMessage);
};

} // namespace pdf

#endif // PDFCERTIFICATEMANAGER_H
