//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFSIGNATUREHANDLER_IMPL_H
#define PDFSIGNATUREHANDLER_IMPL_H

#include "pdfsignaturehandler.h"

#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pkcs7.h>

namespace pdf
{

/// PKCS7 public key signature handler
class PDFPublicKeySignatureHandler : public PDFSignatureHandler
{
protected:
    explicit PDFPublicKeySignatureHandler(const PDFFormFieldSignature* signatureField, const QByteArray& sourceData, const Parameters& parameters) :
        m_signatureField(signatureField),
        m_sourceData(sourceData),
        m_parameters(parameters)
    {

    }

    void initializeResult(PDFSignatureVerificationResult& result) const;
    void verifyCertificate(PDFSignatureVerificationResult& result) const;
    void verifySignature(PDFSignatureVerificationResult& result) const;
    void addTrustedCertificates(X509_STORE* store) const;

    BIO* getSignedDataBuffer(PDFSignatureVerificationResult& result, QByteArray& outputBuffer) const;

public:
    /// Return a list of certificates from PKCS7 object
    static STACK_OF(X509)* getCertificates(PKCS7* pkcs7);

    /// Return certificate info for given certificate
    static PDFCertificateInfo getCertificateInfo(X509* certificate);

    /// Returns name converted to QString
    static QString getStringFromX509Name(X509_NAME* name, int nid);

    /// Converts ASN time to QDateTime. If conversion fails, then invalid
    /// datetime is returned.
    static QDateTime getDateTimeFromASN(const ASN1_TIME* time);

protected:
    const PDFFormFieldSignature* m_signatureField;
    QByteArray m_sourceData;
    Parameters m_parameters;
};

class PDFSignatureHandler_adbe_pkcs7_detached : public PDFPublicKeySignatureHandler
{
public:
    explicit PDFSignatureHandler_adbe_pkcs7_detached(const PDFFormFieldSignature* signatureField, const QByteArray& sourceData, const Parameters& parameters) :
        PDFPublicKeySignatureHandler(signatureField, sourceData, parameters)
    {

    }

    virtual PDFSignatureVerificationResult verify() const override;
};

}   // namespace pdf

#endif // PDFSIGNATUREHANDLER_IMPL_H
