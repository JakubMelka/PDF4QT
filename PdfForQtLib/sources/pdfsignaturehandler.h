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

#ifndef PDFSIGNATUREHANDLER_H
#define PDFSIGNATUREHANDLER_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QString>
#include <QDateTime>

#include <optional>

namespace pdf
{
class PDFForm;
class PDFObjectStorage;
class PDFFormFieldSignature;

/// Signature reference dictionary.
class PDFSignatureReference
{
public:

    enum class TransformMethod
    {
        Invalid,
        DocMDP,
        UR,
        FieldMDP
    };

    TransformMethod getTransformMethod() const { return m_transformMethod; }
    const PDFObject& getTransformParams() const { return m_transformParams; }
    const PDFObject& getData() const { return m_data; }
    const QByteArray& getDigestMethod() const { return m_digestMethod; }

    /// Tries to parse the signature reference. No exception is thrown, in case of error,
    /// invalid signature reference object is returned.
    /// \param storage Object storage
    /// \param object Object containing the signature
    static PDFSignatureReference parse(const PDFObjectStorage* storage, PDFObject object);

private:
    TransformMethod m_transformMethod = TransformMethod::Invalid;
    PDFObject m_transformParams;
    PDFObject m_data;
    QByteArray m_digestMethod;
};

/// Signature dictionary. Contains digital signature. This signature can be validated by signature validator.
/// This object contains certificates, digital signatures, and other information about signature.
class PDFSignature
{
public:

    enum class Type
    {
        Invalid,
        Sig,
        DocTimeStamp
    };

    struct ByteRange
    {
        PDFInteger offset = 0;
        PDFInteger size = 0;
    };

    using ByteRanges = std::vector<ByteRange>;

    struct Changes
    {
        PDFInteger pagesAltered = 0;
        PDFInteger fieldsAltered = 0;
        PDFInteger fieldsFilled = 0;
    };

    enum class AuthentificationType
    {
        Invalid,
        PIN,
        Password,
        Fingerprint
    };

    /// Tries to parse the signature. No exception is thrown, in case of error,
    /// invalid signature object is returned.
    /// \param storage Object storage
    /// \param object Object containing the signature
    static PDFSignature parse(const PDFObjectStorage* storage, PDFObject object);

    Type getType() const { return m_type; }
    const QByteArray& getFilter() const { return m_filter; }
    const QByteArray& getSubfilter() const { return m_subfilter; }
    const QByteArray& getContents() const { return m_contents; }
    const std::vector<QByteArray>* getCertificates() const { return m_certificates.has_value() ? &m_certificates.value() : nullptr; }
    const ByteRanges& getByteRanges() const { return m_byteRanges; }
    const std::vector<PDFSignatureReference>& getReferences() const { return m_references; }
    const Changes* getChanges() const { return m_changes.has_value() ? &m_changes.value() : nullptr; }

    const QString& getName() const { return m_name; }
    const QDateTime& getSigningDateTime() const { return m_signingDateTime; }
    const QString& getLocation() const { return m_location; }
    const QString& getReason() const { return m_reason; }
    const QString& getContactInfo() const { return m_contactInfo; }
    PDFInteger getR() const { return m_R; }
    PDFInteger getV() const { return m_V; }
    const PDFObject& getPropBuild() const { return m_propBuild; }
    PDFInteger getPropTime() const { return m_propTime; }
    AuthentificationType getAuthentificationType() const { return m_propType; }

private:
    Type m_type = Type::Invalid;
    QByteArray m_filter;    ///< Preferred signature handler name
    QByteArray m_subfilter; ///< Describes encoding of signature
    QByteArray m_contents;
    std::optional<std::vector<QByteArray>> m_certificates; ///< Certificate chain (only for adbe.x509.rsa_sha1)
    ByteRanges m_byteRanges;
    std::vector<PDFSignatureReference> m_references;
    std::optional<Changes> m_changes;

    QString m_name; ///< Name of signer. Should rather be extracted from signature.
    QDateTime m_signingDateTime; ///< Signing date and time. Should be extracted from signature, if possible.
    QString m_location; ///< CPU hostname or physical location of signing
    QString m_reason; ///< Reason for signing
    QString m_contactInfo; ///< Contact info for verifying the signature
    PDFInteger m_R; ///< Version of signature handler. Obsolete.
    PDFInteger m_V; ///< Version of signature dictionary format. 1 if References should be used.
    PDFObject m_propBuild;
    PDFInteger m_propTime = 0;
    AuthentificationType m_propType = AuthentificationType::Invalid;
};

/// Info about certificate, various details etc.
class PDFFORQTLIBSHARED_EXPORT PDFCertificateInfo
{
public:
    explicit inline PDFCertificateInfo() = default;

    /// These entries are taken from RFC 5280, section 4.1.2.4,
    /// they are supported and loaded from the certificate, with
    /// exception of Email entry.
    enum NameEntry
    {
        CountryName,
        OrganizationName,
        OrganizationalUnitName,
        DistinguishedName,
        StateOrProvinceName,
        CommonName,
        SerialNumber,
        LocalityName,
        Title,
        Surname,
        GivenName,
        Initials,
        Pseudonym,
        GenerationalQualifier,
        Email,
        NameEnd
    };

    enum PublicKey
    {
        KeyRSA,
        KeyDSA,
        KeyEC,
        KeyDH,
        KeyUnknown
    };

    const QString& getName(NameEntry name) const { return m_nameEntries[name]; }
    void setName(NameEntry name, QString string) { m_nameEntries[name] = qMove(string); }

    QDateTime getNotValidBefore() const;
    void setNotValidBefore(const QDateTime& notValidBefore);

    QDateTime getNotValidAfter() const;
    void setNotValidAfter(const QDateTime& notValidAfter);

    long getVersion() const;
    void setVersion(long version);

    PublicKey getPublicKey() const;
    void setPublicKey(const PublicKey& publicKey);

private:
    long m_version = 0;
    PublicKey m_publicKey = KeyUnknown;
    std::array<QString, NameEnd> m_nameEntries;
    QDateTime m_notValidBefore;
    QDateTime m_notValidAfter;
};

using PDFCertificateInfos = std::vector<PDFCertificateInfo>;

class PDFFORQTLIBSHARED_EXPORT PDFSignatureVerificationResult
{
public:
    explicit PDFSignatureVerificationResult() = default;
    explicit PDFSignatureVerificationResult(PDFObjectReference signatureFieldReference, QString qualifiedName) :
        m_signatureFieldReference(signatureFieldReference),
        m_signatureFieldQualifiedName(qualifiedName)
    {

    }

    enum VerificationFlag
    {
        None                                = 0x00000,  ///< Used only for initialization
        OK                                  = 0x00001,  ///< Both certificate and signature is OK
        Certificate_OK                      = 0x00002,  ///< Certificate is OK
        Signature_OK                        = 0x00004,  ///< Signature is OK
        Error_NoHandler                     = 0x00008,  ///< No signature handler for given signature
        Error_Generic                       = 0x00010,  ///< Generic error (uknown general error)

        Error_Certificate_Invalid           = 0x00020,  ///< Certificate is invalid
        Error_Certificate_NoSignatures      = 0x00040,  ///< No signature found in certificate data
        Error_Certificate_Missing           = 0x00080,  ///< Certificate is missing
        Error_Certificate_Generic           = 0x00100,  ///< Generic error during certificate verification
        Error_Certificate_Expired           = 0x00200,  ///< Certificate has expired
        Error_Certificate_SelfSigned        = 0x00400,  ///< Self signed certificate
        Error_Certificate_SelfSignedChain   = 0x00800,  ///< Self signed certificate in chain
        Error_Certificate_TrustedNotFound   = 0x01000,  ///< No trusted certificate was found
        Error_Certificate_Revoked           = 0x02000,  ///< Certificate has been revoked
        Error_Certificate_Other             = 0x04000,  ///< Other certificate error. See OpenSSL code for details.

        Error_Certificates_Mask = Error_Certificate_Invalid | Error_Certificate_NoSignatures | Error_Certificate_Missing | Error_Certificate_Generic |
                                  Error_Certificate_Expired | Error_Certificate_SelfSigned | Error_Certificate_SelfSignedChain | Error_Certificate_TrustedNotFound |
                                  Error_Certificate_Revoked | Error_Certificate_Other
    };
    Q_DECLARE_FLAGS(VerificationFlags, VerificationFlag)

    /// Adds no handler error for given signature format
    /// \param format Signature format
    void addNoHandlerError(const QByteArray& format);

    void addInvalidCertificateError();
    void addNoSignaturesError();
    void addCertificateMissingError();
    void addCertificateGenericError();
    void addCertificateExpiredError();
    void addCertificateSelfSignedError();
    void addCertificateSelfSignedInChainError();
    void addCertificateTrustedNotFoundError();
    void addCertificateRevokedError();
    void addCertificateOtherError(int error);

    bool isValid() const { return hasFlag(OK); }
    bool isCertificateValid() const { return hasFlag(Certificate_OK); }
    bool isSignatureValid() const { return hasFlag(Signature_OK); }
    bool hasError() const { return !isValid(); }
    bool hasCertificateError() const { return m_flags & Error_Certificates_Mask; }
    bool hasFlag(VerificationFlag flag) const { return m_flags.testFlag(flag); }
    void setFlag(VerificationFlag flag, bool value) { m_flags.setFlag(flag, value); }

    PDFObjectReference getSignatureFieldReference() const { return m_signatureFieldReference; }
    const QString& getSignatureFieldQualifiedName() const { return m_signatureFieldQualifiedName; }
    const QStringList& getErrors() const { return m_errors; }

    void setSignatureFieldQualifiedName(const QString& signatureFieldQualifiedName);
    void setSignatureFieldReference(PDFObjectReference signatureFieldReference);

    void addCertificateInfo(PDFCertificateInfo info) { m_certificateInfos.emplace_back(qMove(info)); }

private:
    VerificationFlags m_flags = None;
    PDFObjectReference m_signatureFieldReference;
    QString m_signatureFieldQualifiedName;
    QStringList m_errors;
    PDFCertificateInfos m_certificateInfos;
};

/// Signature handler. Can verify both certificate and signature validity.
class PDFFORQTLIBSHARED_EXPORT PDFSignatureHandler
{
public:
    explicit PDFSignatureHandler() = default;
    virtual ~PDFSignatureHandler() = default;

    virtual PDFSignatureVerificationResult verify() const = 0;

    /// Tries to verify all signatures in the form. If form is invalid, then
    /// empty vector is returned.
    /// \param form Form
    /// \param sourceData Source data
    static std::vector<PDFSignatureVerificationResult> verifySignatures(const PDFForm& form, const QByteArray& sourceData);

private:

    /// Creates signature handler using format specified by signature in signature field.
    /// If signature format is unknown, then nullptr is returned.
    /// \param signatureField Signature field
    /// \param sourceData
    static PDFSignatureHandler* createHandler(const PDFFormFieldSignature* signatureField, const QByteArray& sourceData);
};

} // namespace pdf

#endif // PDFSIGNATUREHANDLER_H
