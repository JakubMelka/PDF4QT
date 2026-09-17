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

namespace pdf
{

class PDF4QTLIBCORESHARED_EXPORT PDFCertificateManager
{
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

    void createCertificate(const NewCertificateInfo& info);

    /// Returns certificates, which can be used by the application - personal
    /// certificates from the system storage and certificates stored in the
    /// certificate directory of the application. Certificates from the
    /// certificate directory are never filtered out, because they are
    /// usually password protected and their key usage cannot be read.
    /// \param filter Which certificates from the system storage are returned
    static PDFCertificateEntries getCertificates(PDFCertificateUsageFilter filter = PDFCertificateUsageFilter::Any);
    static QString getCertificateDirectory();
    static QString generateCertificateFileName();
    static bool isCertificateValid(const PDFCertificateEntry& certificateEntry, QString password);
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
