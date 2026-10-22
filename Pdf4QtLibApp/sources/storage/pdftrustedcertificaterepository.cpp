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
    std::vector<PDFTrustedCertificate> certificates;
    PDFDatabaseStatement statement(getConnection(), "SELECT id, CASE WHEN typeof(data) = 'blob' AND length(data) <= 65536 THEN data END, added FROM trusted_certificate "
                                                    "ORDER BY id LIMIT 1000");
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

bool PDFTrustedCertificateRepository::contains(const QByteArray& data) const
{
    for (const PDFTrustedCertificate& certificate : getCertificates())
    {
        if (certificate.data == data)
        {
            return true;
        }
    }
    return false;
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

}   // namespace pdf
