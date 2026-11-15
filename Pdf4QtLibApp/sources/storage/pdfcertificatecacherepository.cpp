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

#include "pdfcertificatecacherepository.h"
#include "pdfdatabasestatement.h"
#include "pdfrepositoryhelper.h"
#include "pdftrustedcertificaterepository.h"

#include "pdfdocumenttool_encryption.h"

#include <QCoreApplication>
#include <QDateTime>

#include <limits>

namespace pdf
{

PDFCertificateCacheRepository::PDFCertificateCacheRepository(PDFDatabase* database) :
    m_database(database)
{

}

qint64 PDFCertificateCacheRepository::getCurrentTime() const
{
    return m_currentTime > 0 ? m_currentTime : QDateTime::currentMSecsSinceEpoch();
}

PDFDatabaseConnection* PDFCertificateCacheRepository::getConnection() const
{
    return m_database ? m_database->getConnection() : nullptr;
}

std::vector<PDFCachedCertificate> PDFCertificateCacheRepository::readRows(PDFDatabaseStatement& statement, size_t maximumCount)
{
    // A row is used only when it is what insert writes: a valid stored path, one certificate of a
    // sane size and the SHA-256 of it as its fingerprint (a hostile row cannot give a certificate
    // the name or the fingerprint of another one)
    std::vector<PDFCachedCertificate> rows;
    while (rows.size() < maximumCount && statement.next())
    {
        PDFCachedCertificate row;
        row.path = PDFRepositoryHelper::readText(statement, 0, PDFRepositoryHelper::MAXIMUM_PATH_LENGTH);
        row.fingerprint = PDFRepositoryHelper::readText(statement, 1, 64);
        row.data = statement.isNull(2) ? QByteArray() : statement.getBlob(2);
        row.fileSize = PDFRepositoryHelper::readInteger(statement, 3, -1, 0, std::numeric_limits<qint64>::max());
        row.fileModified = PDFRepositoryHelper::readInteger(statement, 4, -1, 0, std::numeric_limits<qint64>::max());
        row.added = PDFRepositoryHelper::readInteger(statement, 5, 0, 0, std::numeric_limits<qint64>::max());
        row.lastUsed = PDFRepositoryHelper::readInteger(statement, 6, 0, 0, std::numeric_limits<qint64>::max());

        if (!PDFRepositoryHelper::isValidStoredPath(row.path) ||
            !PDFTrustedCertificateRepository::isValidCertificate(row.data) ||
            row.fingerprint != PDFDocumentToolEncryption::getCertificateFingerprint(row.data))
        {
            continue;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

PDFDatabaseError PDFCertificateCacheRepository::insert(const QString& path, const QByteArray& data, qint64 fileSize, qint64 fileModified)
{
    if (!PDFRepositoryHelper::isValidStoredPath(path))
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFCertificateCacheRepository", "The path of the certificate file is not valid."));
    }

    // Only a public certificate is ever stored - never a PKCS#12, a key or a password
    if (!PDFTrustedCertificateRepository::isValidCertificate(data))
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFCertificateCacheRepository", "The data are not a certificate."));
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    const qint64 now = getCurrentTime();
    PDFDatabaseStatement statement(connection, "INSERT INTO certificate_cache(path, fingerprint, data, file_size, file_mtime, added, last_used) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?6) "
                                               "ON CONFLICT(path) DO UPDATE SET fingerprint = excluded.fingerprint, data = excluded.data, file_size = excluded.file_size, "
                                               "file_mtime = excluded.file_mtime, last_used = excluded.last_used, "
                                               "added = CASE WHEN certificate_cache.fingerprint IS excluded.fingerprint THEN certificate_cache.added ELSE excluded.added END");
    statement.bindText(1, path);
    statement.bindText(2, PDFDocumentToolEncryption::getCertificateFingerprint(data));
    statement.bindBlob(3, data);
    if (fileSize >= 0)
    {
        statement.bindInt64(4, fileSize);
    }
    else
    {
        statement.bindNull(4);
    }
    if (fileModified >= 0)
    {
        statement.bindInt64(5, fileModified);
    }
    else
    {
        statement.bindNull(5);
    }
    statement.bindInt64(6, now);
    if (!statement.execute())
    {
        return statement.getError();
    }

    // The least recently used rows over the limit go (the row just written is the newest one)
    PDFDatabaseStatement prune(connection, "DELETE FROM certificate_cache WHERE path NOT IN "
                                           "(SELECT path FROM certificate_cache ORDER BY last_used DESC, path LIMIT ?1)");
    prune.bindInt64(1, MAXIMUM_ROWS);
    if (!prune.execute())
    {
        return prune.getError();
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

std::optional<PDFCachedCertificate> PDFCertificateCacheRepository::find(const QString& path) const
{
    if (!PDFRepositoryHelper::isValidStoredPath(path))
    {
        return std::nullopt;
    }

    PDFDatabaseStatement statement(getConnection(), "SELECT path, fingerprint, data, file_size, file_mtime, added, last_used FROM certificate_cache "
                                                    "WHERE path = ?1 AND typeof(data) = 'blob' AND length(data) BETWEEN 1 AND 65536");
    statement.bindText(1, path);
    std::vector<PDFCachedCertificate> rows = readRows(statement, 1);
    if (rows.empty())
    {
        return std::nullopt;
    }
    return std::move(rows.front());
}

std::optional<PDFCachedCertificate> PDFCertificateCacheRepository::findByFingerprint(const QString& fingerprint) const
{
    // The rows of the fingerprint are bounded by the limit of the table; a damaged one is skipped
    PDFDatabaseStatement statement(getConnection(), "SELECT path, fingerprint, data, file_size, file_mtime, added, last_used FROM certificate_cache "
                                                    "WHERE fingerprint = ?1 AND typeof(data) = 'blob' AND length(data) BETWEEN 1 AND 65536 "
                                                    "ORDER BY last_used DESC, path LIMIT 1000");
    statement.bindText(1, fingerprint);
    std::vector<PDFCachedCertificate> rows = readRows(statement, 1);
    if (rows.empty())
    {
        return std::nullopt;
    }
    return std::move(rows.front());
}

std::vector<PDFCachedCertificate> PDFCertificateCacheRepository::getCertificates() const
{
    PDFDatabaseStatement statement(getConnection(), "SELECT path, fingerprint, data, file_size, file_mtime, added, last_used FROM certificate_cache "
                                                    "WHERE typeof(data) = 'blob' AND length(data) BETWEEN 1 AND 65536 ORDER BY last_used DESC, path LIMIT 1000");
    return readRows(statement, size_t(MAXIMUM_ROWS));
}

PDFDatabaseError PDFCertificateCacheRepository::remove(const QString& path)
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM certificate_cache WHERE path = ?1");
    statement.bindText(1, path);
    if (!statement.execute())
    {
        return statement.getError();
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

}   // namespace pdf
