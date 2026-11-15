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

#ifndef PDFCERTIFICATECACHEREPOSITORY_H
#define PDFCERTIFICATECACHEREPOSITORY_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"

#include <QByteArray>
#include <QPointer>
#include <QString>

#include <optional>
#include <vector>

namespace pdf
{

class PDFDatabaseStatement;

/// The public certificate of a .pfx file of the user (a row of the table certificate_cache)
class PDF4QTLIBAPPSHARED_EXPORT PDFCachedCertificate
{
public:
    QString path;               ///< Path of the .pfx file (the key, as stored by PDFRepositoryHelper)
    QString fingerprint;        ///< SHA-256 of data, 64 lower case hexadecimal digits (PDFDocumentToolEncryption::getCertificateFingerprint - the value of the parameter certificate)
    QByteArray data;            ///< DER encoded X.509 certificate - public, never a private key
    qint64 fileSize = -1;       ///< Size of the .pfx file, when the row was written (-1 = unknown)
    qint64 fileModified = -1;   ///< Time of the last change of the .pfx file (ms since the epoch, UTC; -1 = unknown)
    qint64 added = 0;           ///< First write of the row (ms since the epoch, UTC)
    qint64 lastUsed = 0;        ///< Last write of the row (ms since the epoch, UTC)
};

/// The certificates of the .pfx files of the user (table certificate_cache of migration 3; plan of
/// step 5 of phase 2, owner answer Q5.11): a PKCS#12 file encrypts its certificate by its password,
/// so the list of Encrypt with Certificate (433:3049) could not show its name, organization, e-mail
/// and validity before the password. The public certificate is stored when the file is created,
/// imported or unlocked the first time (packages P1b / P3 call insert); a file, which was never
/// unlocked, shows its file name and "Details are shown after the password".
///
/// Only a public X.509 certificate is ever stored: insert refuses anything else (a PKCS#12, a key, a
/// password - PDFTrustedCertificateRepository::isValidCertificate, one rule for every stored
/// certificate). The rows are untrusted (another version, a damaged or a hostile file): a row, whose
/// data are no certificate of a sane size or whose fingerprint is not the SHA-256 of its data, is
/// skipped; at most MAXIMUM_ROWS are kept (the least recently used are pruned by a write). A value
/// object as the other repositories: reads go to the database, a write is one short transaction,
/// notified with the topic getTopic() (the key is the path). Usable in any thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFCertificateCacheRepository
{
public:
    static QString getTopic() { return QStringLiteral("certificate_cache"); }

    /// Most rows kept and read
    static constexpr int MAXIMUM_ROWS = 1000;

    explicit PDFCertificateCacheRepository(PDFDatabase* database);

    /// Fixed time of the writes (tests); 0 = the current time
    void setCurrentTime(qint64 time) { m_currentTime = time; }

    /// Stores the certificate of the .pfx file (a new row, or the row of the path replaced). Refused:
    /// data, which are not one DER certificate of a sane size, and a path, which is not a valid
    /// absolute path (PDFRepositoryHelper::isValidStoredPath).
    /// \param path Path of the .pfx file (normalized by PDFRepositoryHelper::normalizePath)
    /// \param data DER of the certificate (PDFCertificateInfo::getCertificateData)
    /// \param fileSize Size of the .pfx file (-1 = unknown)
    /// \param fileModified Time of the last change of the .pfx file (ms since the epoch, -1 = unknown)
    PDFDatabaseError insert(const QString& path, const QByteArray& data, qint64 fileSize, qint64 fileModified);

    /// The certificate of the .pfx file of the path, std::nullopt = none (or a damaged row)
    std::optional<PDFCachedCertificate> find(const QString& path) const;

    /// The most recently used row of the fingerprint, std::nullopt = none
    std::optional<PDFCachedCertificate> findByFingerprint(const QString& fingerprint) const;

    /// Every valid row, the most recently used first
    std::vector<PDFCachedCertificate> getCertificates() const;

    /// Removes the row of the path (a .pfx file, which is gone)
    PDFDatabaseError remove(const QString& path);

private:
    qint64 getCurrentTime() const;
    PDFDatabaseConnection* getConnection() const;

    /// The rows of a statement, which selects path, fingerprint, data, file_size, file_mtime, added,
    /// last_used; the invalid ones are skipped
    static std::vector<PDFCachedCertificate> readRows(PDFDatabaseStatement& statement, size_t maximumCount);

    QPointer<PDFDatabase> m_database;
    qint64 m_currentTime = 0;
};

}   // namespace pdf

#endif // PDFCERTIFICATECACHEREPOSITORY_H
