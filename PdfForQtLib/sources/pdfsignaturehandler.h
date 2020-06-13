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

#include <optional>

namespace pdf
{
class PDFObjectStorage;

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

class PDFSignatureHandler
{
public:
    PDFSignatureHandler();
};

} // namespace pdf

#endif // PDFSIGNATUREHANDLER_H
