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

#ifndef PDFSIGNATUREHANDLER_H
#define PDFSIGNATUREHANDLER_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdfutils.h"
#include "pdfcertificatestore.h"

#include <QString>
#include <QDateTime>

#include <optional>

class QDataStream;

namespace pdf
{
class PDFForm;
class PDFObjectStorage;
class PDFCertificateStore;
class PDFFormFieldSignature;
class PDFDocumentSecurityStore;
class PDFSecurityHandler;
struct PDFSignatureLayout;

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

    /// Reference of the signature dictionary, an invalid reference for a direct dictionary
    PDFObjectReference getSelfReference() const { return m_selfReference; }

    /// Permission P of the DocMDP transform of the signature (ISO 32000-2, 12.8.2.2, Table 257):
    /// 1, 2 or 3 for a certification signature, 0 for a signature without a DocMDP reference
    PDFInteger getDocMDPPermission() const { return m_docMDPPermission; }

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
    PDFObjectReference m_selfReference;
    PDFInteger m_docMDPPermission = 0;
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

class PDF4QTLIBCORESHARED_EXPORT PDFSignatureVerificationResult
{
public:
    explicit PDFSignatureVerificationResult() = default;
    explicit PDFSignatureVerificationResult(PDFSignature::Type type, PDFObjectReference signatureFieldReference, QString qualifiedName) :
        m_type(type),
        m_signatureFieldReference(signatureFieldReference),
        m_signatureFieldQualifiedName(qualifiedName)
    {

    }

    enum class Status
    {
        OK,
        Warning,
        Error
    };

    enum VerificationFlag
    {
        None                                            = 0x00000000,  ///< Used only for initialization
        OK                                              = 0x00000001,  ///< Both certificate and signature is OK
        Certificate_OK                                  = 0x00000002,  ///< Certificate is OK
        Signature_OK                                    = 0x00000004,  ///< Signature is OK
        Error_NoHandler                                 = 0x00000008,  ///< No signature handler for given signature
        Error_Generic                                   = 0x00000010,  ///< Generic error (uknown general error)

        Error_Certificate_Invalid                       = 0x00000020,  ///< Certificate is invalid
        Error_Certificate_NoSignatures                  = 0x00000040,  ///< No signature found in certificate data
        Error_Certificate_Missing                       = 0x00000080,  ///< Certificate is missing
        Error_Certificate_Generic                       = 0x00000100,  ///< Generic error during certificate verification
        Error_Certificate_Expired                       = 0x00000200,  ///< Certificate has expired
        Error_Certificate_SelfSigned                    = 0x00000400,  ///< Self signed certificate
        Error_Certificate_SelfSignedChain               = 0x00000800,  ///< Self signed certificate in chain
        Error_Certificate_TrustedNotFound               = 0x00001000,  ///< No trusted certificate was found
        Error_Certificate_Revoked                       = 0x00002000,  ///< Certificate has been revoked
        Error_Certificate_Other                         = 0x00004000,  ///< Other certificate error. See OpenSSL code for details.

        Error_Signature_Invalid                         = 0x00008000,  ///< Signature is invalid for some reason
        Error_Signature_SourceCertificateMissing        = 0x00010000,  ///< Source certificate of signature is missing
        Error_Signature_NoSignaturesFound               = 0x00020000,  ///< No signatures found
        Error_Signature_DigestFailure                   = 0x00040000,  ///< Digest failure
        Error_Signature_DataOther                       = 0x00080000,  ///< Signed data were not verified
        Error_Signature_DataCoveredBySignatureMissing   = 0x00100000,  ///< Data covered by signature are not present

        /// The document was changed after it was signed: a later revision holds a change outside of the allowed list
        /// (ISO 32000-2, 12.8.1; plan R6-3), it cannot be classified, or the bytes after the signature are not a revision
        Warning_Signature_NotCoveredBytes               = 0x00200000,
        Warning_Certificate_CRLValidityTimeExpired      = 0x00400000,  ///< Certificate revocation list was not checked, because it's validity expired
        Warning_Certificate_QualifiedStatement          = 0x00800000,  ///< Qualified certificate statement not verified
        Warning_Certificate_UnableToGetCRL              = 0x01000000,  ///< Unable to get CRL
        Warning_Signature_TimestampNotVerified          = 0x02000000,  ///< Timestamp of the signature could not be verified
        Warning_Signature_FileRestored                  = 0x04000000,  ///< The document was restored from the bytes of a damaged file, it may not be the signed one

        /// The layout of the signature in the file is invalid (B109, R6-22): /ByteRange is not [0 a b c], its gap is not the
        /// /Contents of this signature dictionary, it does not end at the end of a revision, /Contents is not one exact encoded
        /// object followed only by zeros, or a limit of the verification (plan 7.2) is exceeded
        Error_Signature_ByteRange                       = 0x08000000,
        /// A certification signature (DocMDP, ISO 32000-2, 12.8.2.2, Table 257): a later revision holds changes, which its
        /// permission P does not allow
        Error_Signature_ChangesNotAllowed               = 0x10000000,
        /// A weak digest algorithm (MD5, SHA-1) protects the signature (B114); the strict mode of a viewer treats it as an error
        Warning_Signature_WeakAlgorithm                 = 0x20000000,

        Error_Certificates_Mask = Error_Certificate_Invalid | Error_Certificate_NoSignatures | Error_Certificate_Missing | Error_Certificate_Generic |
                                  Error_Certificate_Expired | Error_Certificate_SelfSigned | Error_Certificate_SelfSignedChain | Error_Certificate_TrustedNotFound |
                                  Error_Certificate_Revoked | Error_Certificate_Other,

        Error_Signatures_Mask = Error_Signature_Invalid | Error_Signature_SourceCertificateMissing | Error_Signature_NoSignaturesFound |
                                Error_Signature_DigestFailure | Error_Signature_DataOther | Error_Signature_DataCoveredBySignatureMissing |
                                Error_Signature_ByteRange | Error_Signature_ChangesNotAllowed,

        Warning_Certificates_Mask = Warning_Certificate_CRLValidityTimeExpired | Warning_Certificate_QualifiedStatement | Warning_Certificate_UnableToGetCRL,
        Warning_Signatures_Mask = Warning_Signature_NotCoveredBytes | Warning_Signature_TimestampNotVerified | Warning_Signature_FileRestored |
                                  Warning_Signature_WeakAlgorithm,

        Warnings_Mask = Warning_Certificates_Mask | Warning_Signatures_Mask
    };
    Q_DECLARE_FLAGS(VerificationFlags, VerificationFlag)

    PDFSignature::Type getType() const;
    void setType(const PDFSignature::Type& type);

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
    void addInvalidSignatureError();
    void addSignatureNoSignaturesFoundError();
    void addSignatureCertificateMissingError();
    void addSignatureDigestFailureError();
    void addSignatureDataOtherError();
    void addSignatureDataCoveredBySignatureMissingError();

    void addSignatureNotCoveredBytesWarning(PDFInteger count);
    void addSignatureTimestampNotVerifiedWarning();
    void addSignatureFileRestoredWarning();
    void addCertificateCRLValidityTimeExpiredWarning();
    void addCertificateQualifiedStatementNotVerifiedWarning();
    void addCertificateUnableToGetCRLWarning();

    /// The layout of the signature in the file is invalid (Error_Signature_ByteRange)
    /// \param reason Translated reason, a sentence
    void addByteRangeError(const QString& reason);

    /// A later revision holds changes, which the certification does not allow (Error_Signature_ChangesNotAllowed).
    /// The signature stops being valid (the classification runs after the signature was verified).
    /// \param permission Permission P of the DocMDP transform (1, 2)
    void addChangesNotAllowedError(PDFInteger permission);

    /// The certificate of the signer may not sign documents (B116): its key usage has neither digitalSignature
    /// nor nonRepudiation, or its extended key usage has no purpose of documents (Error_Certificate_Other)
    void addCertificatePurposeError();

    /// A weak digest algorithm protects the signature (Warning_Signature_WeakAlgorithm)
    /// \param algorithm Name of the algorithm (SHA-1, MD5)
    void addWeakAlgorithmWarning(const QString& algorithm);

    /// The signature was not verified, a limit of the verification was exceeded (Error_Generic, Error_Signature_ByteRange)
    /// \param reason Translated reason, a sentence
    void addNotVerifiedError(const QString& reason);

    /// Adds an information, which is neither a warning nor an error (later signatures were added, ...)
    void addInfo(const QString& info);

    /// The revisions of the file after the revision, which the signature ends (plan R6-3, ISO 32000-2, 12.8.1)
    enum class LaterRevisions
    {
        None,       ///< The signature ends the last revision of the file (or its layout is invalid)
        Allowed,    ///< Only signatures, document timestamps or validation data (DSS) were added later (getInfos says what);
                    ///< for a certification with P = 2 also the filling in of form fields
        Changed     ///< The document was changed after it was signed (Warning_Signature_NotCoveredBytes)
    };

    LaterRevisions getLaterRevisions() const { return m_laterRevisions; }
    void setLaterRevisions(LaterRevisions laterRevisions) { m_laterRevisions = laterRevisions; }

    /// Permission P of the certification (DocMDP, Table 257): 1, 2, 3; 0 = an approval signature or a timestamp
    PDFInteger getCertificationPermission() const { return m_certificationPermission; }
    void setCertificationPermission(PDFInteger certificationPermission) { m_certificationPermission = certificationPermission; }

    bool isValid() const { return hasFlag(OK); }
    bool isCertificateValid() const { return hasFlag(Certificate_OK); }
    bool isSignatureValid() const { return hasFlag(Signature_OK); }
    bool hasError() const { return !isValid(); }
    bool hasWarning() const { return m_flags & Warnings_Mask; }
    bool hasCertificateError() const { return m_flags & Error_Certificates_Mask; }
    bool hasSignatureError() const { return m_flags & Error_Signatures_Mask; }
    bool hasCertificateWarning() const { return m_flags & Warning_Certificates_Mask; }
    bool hasSignatureWarning() const { return m_flags & Warning_Signatures_Mask; }
    bool hasFlag(VerificationFlag flag) const { return m_flags.testFlag(flag); }
    void setFlag(VerificationFlag flag, bool value) { m_flags.setFlag(flag, value); }

    PDFObjectReference getSignatureFieldReference() const { return m_signatureFieldReference; }
    const QString& getSignatureFieldQualifiedName() const { return m_signatureFieldQualifiedName; }
    const QStringList& getErrors() const { return m_errors; }
    const QStringList& getWarnings() const { return m_warnings; }
    const QStringList& getInfos() const { return m_infos; }
    const QStringList& getHashAlgorithms() const { return m_hashAlgorithms; }

    /// Certificates of the signature (R6-2): the first one is the certificate of the signer, identified by
    /// the SignerInfo of the signature (issuer and serial number or the subject key identifier) and verified
    /// against the value of the signature (getSignerCertificate), followed by the chain built from it (the
    /// last one is its root, when the chain reaches one). Never a certificate of the bag of the signature,
    /// which is not in the chain of the signer. Empty, when the signer was not identified (an error).
    const PDFCertificateInfos& getCertificateInfos() const { return m_certificateInfos; }

    /// Returns the certificate of the signer (R6-2) - the name of the signer, the certificate to trust -
    /// or nullptr, when the signer was not identified (the signature has an error then)
    const PDFCertificateInfo* getSignerCertificate() const { return m_isSignerIdentified && !m_certificateInfos.empty() ? &m_certificateInfos.front() : nullptr; }

    void setSignatureFieldQualifiedName(const QString& signatureFieldQualifiedName);
    void setSignatureFieldReference(PDFObjectReference signatureFieldReference);

    /// Sets the certificates: the signer and the chain built from it (see getCertificateInfos)
    void setCertificateInfos(PDFCertificateInfos infos, bool isSignerIdentified) { m_certificateInfos = std::move(infos); m_isSignerIdentified = isSignerIdentified; }
    void addCertificateInfo(PDFCertificateInfo info) { m_certificateInfos.emplace_back(qMove(info)); }
    void addHashAlgorithm(const QString& algorithm);

    /// Adds OK flag, if both certificate and signature are valid
    void validate();

    QDateTime getSignatureDate() const;
    void setSignatureDate(const QDateTime& signatureDate);

    /// Where the date of getSignatureDate comes from
    enum class SignatureDateSource
    {
        None,                   ///< No date
        SigningTimeAttribute,   ///< The signed attribute signing-time of the CMS (covered by the signature)
        SignatureDictionary     ///< /M of the signature dictionary (ISO 32000-2, Table 255): the time claimed by the
                                ///< signer (PAdES has no signing-time attribute), not attested by a timestamp
    };

    SignatureDateSource getSignatureDateSource() const { return m_signatureDateSource; }
    void setSignatureDateSource(SignatureDateSource source) { m_signatureDateSource = source; }

    /// The date of getSignatureDate is only claimed by the signer (/M), not attested by a timestamp
    bool isSignatureDateClaimed() const { return m_signatureDateSource == SignatureDateSource::SignatureDictionary; }

    QDateTime getTimestampDate() const;
    void setTimestampDate(const QDateTime& timestampDate);

    QByteArray getSignatureHandler() const;
    void setSignatureHandler(const QByteArray& signatureFilter);

    Status getCertificateStatus() const;
    Status getSignatureStatus() const;

    QString getCertificateStatusText() const { return getStatusText(getCertificateStatus()); }
    QString getSignatureStatusText() const { return getStatusText(getSignatureStatus()); }

    static QString getStatusText(Status status);

    const PDFClosedIntervalSet& getBytesCoveredBySignature() const;
    void setBytesCoveredBySignature(const PDFClosedIntervalSet& bytesCoveredBySignature);

private:
    PDFSignature::Type m_type = PDFSignature::Type::Invalid;
    VerificationFlags m_flags = None;
    PDFObjectReference m_signatureFieldReference;
    QString m_signatureFieldQualifiedName;
    QDateTime m_signatureDate;
    SignatureDateSource m_signatureDateSource = SignatureDateSource::None;
    QDateTime m_timestampDate;
    QStringList m_errors;
    QStringList m_warnings;
    QStringList m_infos;
    QStringList m_hashAlgorithms;
    QByteArray m_signatureHandler;
    PDFCertificateInfos m_certificateInfos;
    bool m_isSignerIdentified = false;
    LaterRevisions m_laterRevisions = LaterRevisions::None;
    PDFInteger m_certificationPermission = 0;
    PDFClosedIntervalSet m_bytesCoveredBySignature;
};

/// Signature handler. Can verify both certificate and signature validity.
class PDF4QTLIBCORESHARED_EXPORT PDFSignatureHandler
{
public:
    explicit PDFSignatureHandler() = default;
    virtual ~PDFSignatureHandler() = default;

    virtual PDFSignatureVerificationResult verify() const = 0;

    /// When the bundled AATL certificates (PDFCertificateStore::getAATLCertificates)
    /// are trusted
    enum class AATLCertificates
    {
        WithSystemStore,    ///< Only together with the system store (useSystemCertificateStore) - the old applications
        Always,             ///< Always, also without the system store (the new application, O15)
        Never               ///< Never
    };

    struct Parameters
    {
        const PDFCertificateStore* store = nullptr;
        const PDFDocumentSecurityStore* dss = nullptr;
        bool enableVerification = true;
        bool ignoreExpirationDate = false;
        bool useSystemCertificateStore = true;
        AATLCertificates aatlCertificates = AATLCertificates::WithSystemStore;

        /// False: the certificate of the signer is identified (R6-2), but its chain and its trust are not
        /// verified (PdfTool --ver-no-cert-check, B132): no certificate error, no Certificate_OK, so no OK
        bool verifyCertificates = true;

        /// The authenticated security handler of the document of the form (an encrypted file, R6-1): the
        /// revisions of the file are read by its clone (PDFDocumentReader::setAuthenticatedSecurityHandler),
        /// when the file has its encryption. Null: an encrypted file with an open password has revisions,
        /// which cannot be read, so its later revisions count as changes. Only used during the call.
        const PDFSecurityHandler* securityHandler = nullptr;
    };

    /// Limits of the verification (plan 7.2, B123)
    static constexpr PDFInteger MAXIMUM_SIGNATURE_SIZE = 512 * 1024;            ///< Bytes of /Contents (1 MiB of hexadecimal digits)
    static constexpr int MAXIMUM_CERTIFICATES = 64;                             ///< Certificates of a signature (the bag of CMS, /Cert), and of the DSS used
    static constexpr PDFInteger MAXIMUM_CERTIFICATES_SIZE = 1024 * 1024;        ///< Bytes of the certificates of /Cert, and of the DSS used
    static constexpr int MAXIMUM_CHAIN_DEPTH = 16;                              ///< Certificates of a chain between the signer and its root
    static constexpr int MAXIMUM_VERIFIED_SIGNATURES = 256;                     ///< Signatures verified in one document, the others are "not verified"
    static constexpr int MAXIMUM_CLASSIFIED_REVISIONS = 256;                    ///< Later revisions classified (each one is read), more = changed

    /// Tries to verify all signatures in the form. If form is invalid, then empty vector is returned.
    /// A signature field without a signature gives a result of the type PDFSignature::Type::Invalid.
    ///
    /// Each signature is checked against the file (step 6 of phase 2, plan 6.7, 7.2, R6-2, R6-3, R6-22):
    /// /ByteRange is exactly [0 a b c], its gap is the hexadecimal /Contents of this signature dictionary at
    /// the position of the reader, b + c is the end of a revision, /Contents is one object of its exact
    /// length (DER; BER of the indefinite length of real signers is measured by its parsing) followed only
    /// by zeros (otherwise Error_Signature_ByteRange); the signer is the certificate of
    /// the SignerInfo (R6-2). The revisions after the signed one are compared object by object, each with the
    /// previous one: only new signature fields signed by a valid signature, their widgets, /AcroForm /Fields
    /// and /SigFlags, /DSS and document timestamps are allowed (LaterRevisions::Allowed and an information),
    /// anything else is a change (LaterRevisions::Changed, a warning); a certification (DocMDP) with P = 1
    /// or P = 2 is evaluated over them (Error_Signature_ChangesNotAllowed).
    /// \param form Form
    /// \param sourceData Source data
    /// \param parameters Verification settings
    static std::vector<PDFSignatureVerificationResult> verifySignatures(const PDFForm& form, const QByteArray& sourceData, const Parameters& parameters);

    /// Releases the state of OpenSSL kept for the calling thread (the error queue,
    /// ...). A worker of a thread pool, which has verified signatures or parsed
    /// certificates, calls it at its end: a pool thread can be alive at the exit
    /// of the application, and the cleanup of OpenSSL at the exit does not release
    /// the state of a thread, which still runs.
    static void releaseThreadResources();

private:

    /// Creates signature handler using format specified by signature in signature field.
    /// If signature format is unknown, then nullptr is returned.
    /// \param signatureField Signature field
    /// \param sourceData
    /// \param parameters Verification settings
    /// \param layout Checked layout of the signature in the file
    static PDFSignatureHandler* createHandler(const PDFFormFieldSignature* signatureField, const QByteArray& sourceData, const Parameters& parameters, const PDFSignatureLayout& layout);
};

} // namespace pdf

#endif // PDFSIGNATUREHANDLER_H
