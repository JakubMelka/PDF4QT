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

#ifndef PDFTRUSTEDCERTIFICATEREPOSITORY_H
#define PDFTRUSTEDCERTIFICATEREPOSITORY_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"

#include <QByteArray>
#include <QPointer>
#include <QString>

#include <vector>

namespace pdf
{

/// Certificate, which the user trusts (a row of the table trusted_certificate)
class PDF4QTLIBAPPSHARED_EXPORT PDFTrustedCertificate
{
public:
    qint64 id = 0;
    QByteArray data;        ///< DER encoded X.509 certificate
    qint64 added = 0;       ///< Milliseconds since the epoch
};

/// Certificates trusted by the user (table trusted_certificate, newapp_storage.md 4,
/// owner decision O15): the verification of the signatures trusts them together
/// with the bundled AATL list and the system store. The old file
/// TrustedCertStorage.bin of the old applications is never read. The rows are
/// untrusted input (another version, a damaged file): a row, which is not a
/// DER certificate of a sane size, is skipped. A value object: reads go to the
/// database, writes are short transactions, notified with the topic getTopic().
/// Usable in any thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFTrustedCertificateRepository
{
public:
    static QString getTopic() { return QStringLiteral("trusted_certificate"); }

    /// Maximal size of one certificate in bytes
    static constexpr qsizetype MAXIMUM_CERTIFICATE_SIZE = 64 * 1024;

    /// Maximal number of the certificates read
    static constexpr int MAXIMUM_CERTIFICATE_COUNT = 1000;

    explicit PDFTrustedCertificateRepository(PDFDatabase* database);

    /// Fixed time of the writes (tests); 0 = the current time
    void setCurrentTime(qint64 time) { m_currentTime = time; }

    /// Certificates in the order of adding
    std::vector<PDFTrustedCertificate> getCertificates() const;

    /// Maximal number of the rows listed by getRows
    static constexpr int MAXIMUM_LISTED_ROW_COUNT = 2 * MAXIMUM_CERTIFICATE_COUNT;

    /// Every row of the table in the order of adding (Settings › Security, owner
    /// decision Q4), also the damaged ones, which getCertificates skips - so the
    /// user can remove them: a value, which is not a blob of a sane size, has empty
    /// data; the data of a row are not checked to be a certificate
    std::vector<PDFTrustedCertificate> getRows() const;

    /// The certificate (DER) is trusted
    bool contains(const QByteArray& data) const;

    /// Adds the certificate (DER). A certificate, which is trusted already, is not
    /// added again; data, which are not a certificate, are refused, and so is a
    /// certificate over MAXIMUM_CERTIFICATE_COUNT rows (it would not be read).
    PDFDatabaseError addCertificate(const QByteArray& data);

    /// Removes the certificate of the row
    PDFDatabaseError removeCertificate(qint64 id);

    /// Removes the rows of getRows in one transaction. A row is removed only when
    /// it still has the data read by getRows: the id of a removed row is given to
    /// the next added certificate (INTEGER PRIMARY KEY), so another instance may
    /// have replaced the row, which the user has confirmed, by another certificate
    PDFDatabaseError removeRows(const std::vector<PDFTrustedCertificate>& rows);

    /// The data are a DER certificate of a sane size
    static bool isValidCertificate(const QByteArray& data);

private:
    qint64 getCurrentTime() const;
    PDFDatabaseConnection* getConnection() const;

    /// Number of the rows read by getCertificates (blobs of a sane size)
    int getCertificateRowCount() const;

    QPointer<PDFDatabase> m_database;
    qint64 m_currentTime = 0;
};

}   // namespace pdf

#endif // PDFTRUSTEDCERTIFICATEREPOSITORY_H
