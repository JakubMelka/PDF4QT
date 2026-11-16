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

#ifndef PDFSIGNATUREHANDLER_IMPL_H
#define PDFSIGNATUREHANDLER_IMPL_H

#include "pdfsignaturehandler.h"

#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pkcs7.h>
#include <openssl/cms.h>

namespace pdf
{

/// The checked layout of a signature in its file (B109, R6-22, B111, B123, B124), made by
/// PDFSignatureHandler::verifySignatures before a handler is created. A handler of a
/// signature with an invalid layout is never asked: the signature is an error.
struct PDFSignatureLayout
{
    bool isValid = false;
    QString error;                  ///< Translated reason, why the layout is invalid
    PDFInteger gapStart = 0;        ///< a of [0 a b c]: the offset of '<' of /Contents of this signature dictionary
    PDFInteger gapEnd = 0;          ///< b of [0 a b c]: the offset behind '>' of /Contents
    PDFInteger end = 0;             ///< b + c: the end of the signed revision
    int revision = -1;              ///< Index of the revision, which the signature ends
    QByteArray der;                 ///< /Contents without its zero padding: one object of its exact length (DER, or BER of the indefinite length)
};

/// Public key signature handler: the data covered by the signature, the trusted certificates,
/// the verification of the chain of the signer (R6-2, B116, B117, B119)
class PDFPublicKeySignatureHandler : public PDFSignatureHandler
{
protected:
    explicit PDFPublicKeySignatureHandler(const PDFFormFieldSignature* signatureField, const QByteArray& sourceData, const Parameters& parameters, const PDFSignatureLayout& layout) :
        m_signatureField(signatureField),
        m_sourceData(sourceData),
        m_parameters(parameters),
        m_layout(layout)
    {

    }

    void initializeResult(PDFSignatureVerificationResult& result) const;
    void addTrustedCertificates(X509_STORE* store) const;

    /// Returns the bytes covered by the signature: [0, a) and [b, b + c) of the checked layout
    QByteArray getSignedData() const;

    /// Returns the certificates of the document security store, at most MAXIMUM_CERTIFICATES of them
    /// and MAXIMUM_CERTIFICATES_SIZE bytes (the store is not a part of the signature). The caller owns them.
    STACK_OF(X509)* createDocumentSecurityStoreCertificates() const;

    /// Fills the certificates of the result by the signer and the chain built from it (R6-2) and, when the
    /// parameters ask for it, verifies the chain against the trusted certificates. Sets Certificate_OK, when
    /// the chain is verified without an error.
    /// \param result Result
    /// \param signer Certificate of the signer, identified by the SignerInfo and verified against the signature
    /// \param untrusted Certificates, which can build the chain (the bag of the signature, the DSS)
    /// \param isTimestamp The signer is a timestamp authority (X509_PURPOSE_TIMESTAMP_SIGN), otherwise a signer of documents (B116)
    void verifySignerCertificate(PDFSignatureVerificationResult& result, X509* signer, STACK_OF(X509)* untrusted, bool isTimestamp) const;

    /// Adds the digest algorithm to the result, with a warning, when it is weak (B114)
    static void addDigestAlgorithm(PDFSignatureVerificationResult& result, const ASN1_OBJECT* algorithm);

    /// Callback of the verification of a chain: the warnings of the result (CRL, qualified statements)
    static int verifyCallback(int ok, X509_STORE_CTX* context);

public:
    /// Return certificate info for given certificate
    static PDFCertificateInfo getCertificateInfo(X509* certificate);

    /// Returns name converted to QString
    static QString getStringFromX509Name(X509_NAME* name, int nid);

    /// Returns ASN1 string converted to QString
    static QString getStringFromASN1_STRING(ASN1_STRING* string);

    /// Converts ASN time to QDateTime. If conversion fails, then invalid
    /// datetime is returned.
    static QDateTime getDateTimeFromASN(const ASN1_TIME* time);

    /// Returns true, if the digest algorithm is weak (MD2, MD4, MD5, SHA-1; B114)
    static bool isWeakDigestAlgorithm(int nid);

    /// Returns true, if the certificate may sign documents (B116, plan 5.3): its key usage, when present, has
    /// digitalSignature or nonRepudiation, and its extended key usage, when present, has emailProtection,
    /// documentSigning (1.3.6.1.5.5.7.3.36), Microsoft Document Signing (1.3.6.1.4.1.311.10.3.12), the Adobe
    /// Authentic Documents Trust (1.2.840.113583.1.1.5) or anyExtendedKeyUsage
    static bool isDocumentSigningCertificate(X509* certificate);

protected:
    const PDFFormFieldSignature* m_signatureField;
    QByteArray m_sourceData;
    Parameters m_parameters;
    PDFSignatureLayout m_layout;
};

/// Handler of a signature of the Cryptographic Message Syntax (RFC 5652): adbe.pkcs7.detached,
/// adbe.pkcs7.sha1, ETSI.CAdES.detached, ETSI.RFC3161 (ISO 32000-2, 12.8.3.3, 12.8.3.4, 12.8.5).
/// The signer is the certificate of the only SignerInfo (issuer and serial number or the subject
/// key identifier, B115), verified against the value of the signature (R6-2).
class PDFCMSSignatureHandler : public PDFPublicKeySignatureHandler
{
public:
    /// What the signed data of the CMS object are
    enum class Content
    {
        Detached,       ///< The bytes of the byte range, not encapsulated (adbe.pkcs7.detached, ETSI.CAdES.detached)
        SHA1Digest,     ///< The SHA-1 digest of the bytes of the byte range, encapsulated (adbe.pkcs7.sha1)
        TimestampToken  ///< A timestamp token of RFC 3161 over the bytes of the byte range (ETSI.RFC3161)
    };

    explicit PDFCMSSignatureHandler(const PDFFormFieldSignature* signatureField, const QByteArray& sourceData, const Parameters& parameters, const PDFSignatureLayout& layout, Content content) :
        PDFPublicKeySignatureHandler(signatureField, sourceData, parameters, layout),
        m_content(content)
    {

    }

    virtual PDFSignatureVerificationResult verify() const override;

private:
    /// Verifies the content: the digest of the signed data and the whole CMS object with its signer.
    /// Returns true, if OpenSSL verifies the object with the signer.
    bool verifyContent(PDFSignatureVerificationResult& result, CMS_ContentInfo* cms, CMS_SignerInfo* signerInfo, bool hasSignedAttributes) const;

    /// Verifies the timestamp token of a document timestamp over the signed data
    void verifyTimestampToken(PDFSignatureVerificationResult& result) const;

    /// Return a list of certificates from PKCS7 object (owned by the object)
    static STACK_OF(X509)* getCertificates(PKCS7* pkcs7);

    /// Sets the signing time from the signed attribute signing-time of the signer; without it (PAdES,
    /// ETSI EN 319 142-1 forbids the attribute) from /M of the signature dictionary (ISO 32000-2,
    /// Table 255), marked as claimed by the signer - never of a document timestamp (its time is the token)
    void addSignatureDate(PDFSignatureVerificationResult& result, CMS_SignerInfo* signerInfo) const;

    /// Verifies the RFC 3161 timestamp of the signature, which is stored in the unsigned
    /// attributes of the signer info, and sets the date/time of the signing from it. The
    /// timestamp is not covered by the signature of the signer, so its time is used only when
    /// the token, the timestamped signature value and the certificate of the timestamp
    /// authority are verified - otherwise a warning is added.
    void verifySignatureTimestampAttribute(PDFSignatureVerificationResult& result, CMS_SignerInfo* signerInfo) const;

    Content m_content;
};

/// Handler of adbe.x509.rsa_sha1: a PKCS#1 signature, the signer is the first certificate of /Cert
/// (ISO 32000-2, 12.8.3.2, Table 255)
class PDFSignatureHandler_adbe_pkcs7_rsa_sha1 : public PDFPublicKeySignatureHandler
{
public:
    explicit PDFSignatureHandler_adbe_pkcs7_rsa_sha1(const PDFFormFieldSignature* signatureField, const QByteArray& sourceData, const Parameters& parameters, const PDFSignatureLayout& layout) :
        PDFPublicKeySignatureHandler(signatureField, sourceData, parameters, layout)
    {

    }

    virtual PDFSignatureVerificationResult verify() const override;

private:
    bool getMessageDigest(const QByteArray& message, ASN1_OCTET_STRING* encryptedString, RSA* rsa, int& algorithmNID, QByteArray& digest) const;
    bool getMessageDigestAlgorithm(ASN1_OCTET_STRING* encryptedString, RSA* rsa, int& algorithmNID) const;
};

}   // namespace pdf

#endif // PDFSIGNATUREHANDLER_IMPL_H
