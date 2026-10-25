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

#include "pdftrustedcertificaterepository.h"
#include "pdfdatabasestatement.h"
#include "pdfrepositoryhelper.h"

#include "pdfcertificatestore.h"

#include <QCoreApplication>
#include <QDateTime>

#include <limits>

namespace pdf
{

PDFTrustedCertificateRepository::PDFTrustedCertificateRepository(PDFDatabase* database) :
    m_database(database)
{

}

qint64 PDFTrustedCertificateRepository::getCurrentTime() const
{
    return m_currentTime > 0 ? m_currentTime : QDateTime::currentMSecsSinceEpoch();
}

PDFDatabaseConnection* PDFTrustedCertificateRepository::getConnection() const
{
    return m_database ? m_database->getConnection() : nullptr;
}

bool PDFTrustedCertificateRepository::isValidCertificate(const QByteArray& data)
{
    if (data.isEmpty() || data.size() > MAXIMUM_CERTIFICATE_SIZE)
    {
        return false;
    }
    return PDFCertificateInfo::getCertificateInfo(data).has_value();
}

std::vector<PDFTrustedCertificate> PDFTrustedCertificateRepository::getCertificates() const
{
    // Rows, which cannot be a certificate (another type, an empty or a huge value),
    // do not take a place of the limit; addCertificate keeps the rows below it
    std::vector<PDFTrustedCertificate> certificates;
    PDFDatabaseStatement statement(getConnection(), "SELECT id, data, added FROM trusted_certificate "
                                                    "WHERE typeof(data) = 'blob' AND length(data) BETWEEN 1 AND 65536 ORDER BY id LIMIT 1000");
    while (statement.next())
    {
        PDFTrustedCertificate certificate;
        certificate.id = PDFRepositoryHelper::readInteger(statement, 0, 0, 1, std::numeric_limits<qint64>::max());
        certificate.data = statement.isNull(1) ? QByteArray() : statement.getBlob(1);
        certificate.added = PDFRepositoryHelper::readInteger(statement, 2, 0, 0, std::numeric_limits<qint64>::max());
        if (certificate.id <= 0 || !isValidCertificate(certificate.data))
        {
            continue;
        }

        certificates.push_back(std::move(certificate));
        if (int(certificates.size()) >= MAXIMUM_CERTIFICATE_COUNT)
        {
            break;
        }
    }
    return certificates;
}

std::vector<PDFTrustedCertificate> PDFTrustedCertificateRepository::getRows() const
{
    std::vector<PDFTrustedCertificate> rows;
    PDFDatabaseStatement statement(getConnection(), "SELECT id, CASE WHEN typeof(data) = 'blob' AND length(data) BETWEEN 1 AND 65536 THEN data ELSE NULL END, added "
                                                    "FROM trusted_certificate ORDER BY id LIMIT 2000");
    while (statement.next() && int(rows.size()) < MAXIMUM_LISTED_ROW_COUNT)
    {
        PDFTrustedCertificate row;
        row.id = PDFRepositoryHelper::readInteger(statement, 0, 0, 1, std::numeric_limits<qint64>::max());
        row.data = statement.isNull(1) ? QByteArray() : statement.getBlob(1);
        row.added = PDFRepositoryHelper::readInteger(statement, 2, 0, 0, std::numeric_limits<qint64>::max());
        if (row.id > 0)
        {
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

bool PDFTrustedCertificateRepository::contains(const QByteArray& data) const
{
    // A row of the table, also one behind the limit of getCertificates (no
    // duplicate rows are added)
    if (!isValidCertificate(data))
    {
        return false;
    }

    PDFDatabaseStatement statement(getConnection(), "SELECT 1 FROM trusted_certificate WHERE typeof(data) = 'blob' AND data = ?1 LIMIT 1");
    statement.bindBlob(1, data);
    return statement.next();
}

int PDFTrustedCertificateRepository::getCertificateRowCount() const
{
    PDFDatabaseStatement statement(getConnection(), "SELECT count(*) FROM trusted_certificate WHERE typeof(data) = 'blob' AND length(data) BETWEEN 1 AND 65536");
    return statement.next() ? int(PDFRepositoryHelper::readInteger(statement, 0, 0, 0, std::numeric_limits<int>::max())) : 0;
}

PDFDatabaseError PDFTrustedCertificateRepository::addCertificate(const QByteArray& data)
{
    if (!isValidCertificate(data))
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFTrustedCertificateRepository", "The data are not a certificate."));
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    if (contains(data))
    {
        return PDFDatabaseError();
    }

    // A certificate behind the limit would never be read by the verification
    if (getCertificateRowCount() >= MAXIMUM_CERTIFICATE_COUNT)
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFTrustedCertificateRepository", "Too many certificates are trusted already (at most %1).").arg(MAXIMUM_CERTIFICATE_COUNT));
    }

    PDFDatabaseStatement statement(connection, "INSERT INTO trusted_certificate(data, added) VALUES (?1, ?2)");
    statement.bindBlob(1, data);
    statement.bindInt64(2, getCurrentTime());
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

PDFDatabaseError PDFTrustedCertificateRepository::removeCertificate(qint64 id)
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM trusted_certificate WHERE id = ?1");
    statement.bindInt64(1, id);
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

PDFDatabaseError PDFTrustedCertificateRepository::removeRows(const std::vector<PDFTrustedCertificate>& rows)
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    for (const PDFTrustedCertificate& row : rows)
    {
        // The data compared as getRows reads them (a damaged value is null there)
        PDFDatabaseStatement statement(connection, "DELETE FROM trusted_certificate WHERE id = ?1 AND "
                                                   "(CASE WHEN typeof(data) = 'blob' AND length(data) BETWEEN 1 AND 65536 THEN data ELSE NULL END) IS ?2");
        statement.bindInt64(1, row.id);
        if (row.data.isEmpty())
        {
            statement.bindNull(2);
        }
        else
        {
            statement.bindBlob(2, row.data);
        }
        if (!statement.execute())
        {
            return statement.getError();
        }
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

}   // namespace pdf
