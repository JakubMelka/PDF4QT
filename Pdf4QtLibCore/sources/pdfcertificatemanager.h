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

#include <QString>
#include <QCoreApplication>
#include <QFileInfoList>
#include <QStringList>

#include <optional>

namespace pdf
{

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

        int rsaKeyLength = 1024;
        int validityInSeconds = 2 * 365 * 24 * 3600;
        long serialNumber = 1;
    };

    /// Shortest and longest RSA key of a created certificate in bits
    static constexpr int MINIMUM_RSA_KEY_LENGTH = 1024;
    static constexpr int MAXIMUM_RSA_KEY_LENGTH = 16384;

    /// Largest .pfx file, which is read from a certificate folder (a file of a hostile size in
    /// the folder is skipped, never read whole; plan of step 5 of phase 2, 7.2: 1 MiB as the import)
    static constexpr qint64 MAXIMUM_CERTIFICATE_FILE_SIZE = 1024 * 1024;

    /// Creates a self-signed certificate with its private key into the PKCS#12 file of
    /// \p info.fileName, protected by \p info.privateKeyPasword (UTF-8). The key usage is
    /// digitalSignature, keyEncipherment (a recipient of the encryption by a certificate, ISO
    /// 32000-2, 7.6.5; B99 - a reader refuses a certificate without it) and keyAgreement. An
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

private:
    static bool signImpl_Win(const PDFCertificateEntry& certificateEntry,
                             QString password,
                             QByteArray data,
                             QByteArray& result);

    /// Adds the timestamp of the signature value to the unsigned attributes of
    /// the signer info of the signature
    static bool addTimestampToSignature(const QByteArray& signature,
                                        const TimestampSettings& timestampSettings,
                                        QByteArray& result,
                                        QString& errorMessage);
};

} // namespace pdf

#endif // PDFCERTIFICATEMANAGER_H
