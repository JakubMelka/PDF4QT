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

#ifndef PDFDOCUMENTSIGNER_H
#define PDFDOCUMENTSIGNER_H

#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfobject.h"
#include "pdfprogress.h"
#include "pdfsecurityhandler.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

namespace pdf
{
class PDFDocument;
class PDFDocumentBuilder;
class PDFForm;

/// Creates a digitally signed document.
///
/// The size of a digital signature is not stable - signing the same data twice
/// can produce results which differ by a few bytes, because the length of the
/// DER encoded values varies. The space reserved for the signature in the
/// document therefore cannot be derived from a single trial signature. This
/// class reserves a padded area, checks that the final signature fits into it,
/// and repeats the whole cycle with a larger reservation when it does not.
///
/// When the data of the original document are available, the signed document
/// is written as an incremental update of them, so the bytes covered by the
/// signatures already present in the document are not touched and these
/// signatures stay valid. Only the newly created signature is verified before
/// the document is handed over to the caller - the signatures already present
/// can be invalid for reasons unrelated to the signing, but a document with a
/// damaged new signature is never produced.
///
/// The rules of step 6 of phase 2 (plan 4.4 with R6-1, R6-4, R6-12, B121, B122,
/// B134, B135):
///  - The signatures of the file are counted of the original data (R6-12), never
///    only of the document in the memory: an unsaved change, which removed a
///    field, never leads to a whole write over a signed file. A whole write is
///    made only of a document, whose file has no signature.
///  - A certified document with no changes allowed (DocMDP /P 1, ISO 32000-2,
///    12.8.2.2.1, Table 257) gets no signature; a document timestamp only in a
///    revision of nothing else (no unsaved change). A signed or certified file
///    with unsaved changes gets them in their own incremental revision before
///    the revision of the signature, so a verifier assigns each change to its
///    revision (R6-4). The permissions of Table 22 the caller names are checked
///    against the security handler of the document (B135).
///  - The space of /Contents is either computed by the caller (B134: no trial
///    signature, which would contact a timestamp authority or a smart card) or
///    measured by a trial signature (the old applications). /Contents is always
///    a hexadecimal string (B121), /ByteRange and /Contents are found inside the
///    written signature dictionary (B122).
///  - The signer knows no password: a file with an open password, which cannot
///    be read back without it, is refused (R6-1: never an empty password
///    callback of the reader).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentSigner
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentSigner)

public:
    PDFDocumentSigner() = delete;

    /// Creates the digital signature of the given data. Returns false, if the
    /// signature cannot be created.
    using SignFunction = std::function<bool(const QByteArray& dataToBeSigned, QByteArray& signature)>;

    /// Creates the signature form field referencing the given signature
    /// dictionary, sets the optional entries of the signature dictionary and
    /// returns the reference of the created field. It can be called repeatedly,
    /// because the document is rebuilt when the space reserved for the signature
    /// must be enlarged.
    using CreateSignatureFieldFunction = std::function<PDFObjectReference(PDFDocumentBuilder& builder, PDFObjectReference signatureDictionary)>;

    enum class Result
    {
        OK,                             ///< Document was signed
        NoDocument,                     ///< No document to be signed was given
        SigningFailed,                  ///< Digital signature could not be created
        WriteFailed,                    ///< Document could not be written
        PlaceholderNotFound,            ///< Space reserved for the signature was not found in the written document
        SignatureTooLarge,              ///< Signature did not fit into the reserved space
        VerificationFailed,             ///< Newly created signature of the written document could not be verified
        ExistingSignaturesNotPreserved, ///< Document contains signatures, which would be damaged by writing the document
        NotPermitted,                   ///< The security handler of the document does not grant a permission of Parameters::requiredPermissions (Table 22, B135)
        CertifiedNoChanges,             ///< The document is certified with no changes allowed (DocMDP /P 1): no signature; a document timestamp only without unsaved changes (R6-4)
        PasswordProtected               ///< The original file has an open password, which the document does not open: its encryption is not the encryption of the file (R6-1, PDFSecurityHandler::isSameEncryption)
    };

    /// What the signer has written (sign with \p details)
    struct Details
    {
        bool isIncrementalUpdate = false;       ///< The result is the exact bytes of the original data followed by the update(s)
        bool hasChanges = false;                ///< The document differs from the original data: the result includes the unsaved changes (Q6.17)
        bool isChangesRevisionWritten = false;  ///< The unsaved changes of the document went into their own revision before the revision of the signature (R6-4)
        PDFObjectReference signatureField;      ///< The field of the new signature (createSignatureFieldFunction)
        PDFObjectReference signatureDictionary; ///< The new signature dictionary
    };

    /// Largest DER of a signature, for which the space is reserved (plan 7.2: the verifier accepts
    /// /Contents of at most 1 MiB of hexadecimal digits)
    static constexpr qint64 MAXIMUM_SIGNATURE_SIZE = 512 * 1024;

    struct Parameters
    {
        /// Document to be signed
        const PDFDocument* document = nullptr;

        /// Data of the file the document was loaded from. When they are given,
        /// the signed document is written as an incremental update of them and
        /// the signatures already present in the document stay valid. Without
        /// them the document is written as a whole, which is possible only when
        /// it contains no signatures yet.
        QByteArray originalDocumentData;

        /// Creates the digital signature, must be set
        SignFunction signFunction;

        /// Creates the signature form field, must be set
        CreateSignatureFieldFunction createSignatureFieldFunction;

        QByteArray filter = "Adobe.PPKLite";
        QByteArray subfilter = "adbe.pkcs7.detached";

        /// Type of the created signature dictionary - "Sig" for a digital
        /// signature and "DocTimeStamp" for a document timestamp
        QByteArray signatureDictionaryType = "Sig";

        QDateTime signingTime = QDateTime::currentDateTime();

        /// Progress reporting of the document writer, can be nullptr
        PDFProgressSink* progress = nullptr;

        /// B134: the size of the DER of the signature, for which /Contents is reserved, computed
        /// by the caller (PDFSigningKey::estimateSignatureSize): no trial signature is made, and a
        /// signature, which does not fit, gets one more attempt with its measured size (R6-28: at
        /// most one more contact of a timestamp authority). 0 = a trial signature of the sign
        /// function measures it (the old applications).
        qint64 estimatedSignatureSize = 0;

        /// B135: the permissions of an encrypted document (ISO 32000-2, Table 22), which the
        /// signature needs - a new signature field bits 4 and 6 -, checked against the security
        /// handler of the document (PDFSecurityHandler::isAllowedByRevision); empty = no check (the
        /// dialog of an old application has checked them)
        std::vector<PDFSecurityHandler::Permission> requiredPermissions;

        /// Permissions of Table 22, of which any one is enough besides requiredPermissions (an existing
        /// empty field: bit 9 or bit 6, PDFDocumentTool::getAnyOfPermissions); empty = no such choice
        std::vector<PDFSecurityHandler::Permission> anyOfPermissions;
    };

    /// Signs the document. Returns Result::OK and fills \p signedDocument only
    /// when the newly created signature of the written document has been
    /// verified, otherwise \p signedDocument is left untouched.
    /// \param parameters Signing parameters
    /// \param signedDocument Bytes of the signed document
    static Result sign(const Parameters& parameters, QByteArray& signedDocument);

    /// Signs the document (as above) and tells what was written
    /// \param parameters Signing parameters
    /// \param signedDocument Bytes of the signed document
    /// \param details What was written, filled on Result::OK (may be null)
    static Result sign(const Parameters& parameters, QByteArray& signedDocument, Details* details);

    /// Reads data of a file for the signer (R6-1, R6-12): an encrypted file by a clone of the
    /// authenticated security handler of a document of the same file (PDFDocumentReader::
    /// setAuthenticatedSecurityHandler) - the signer knows no password, the password callback
    /// refuses; a damaged file is restored (permissive reading)
    /// \param data Bytes of the file
    /// \param securityHandler The authenticated handler of the document read from the file (or from
    ///        its older revision), may be null: a file with an open password is then not read
    /// \param isPasswordProtected The file has an open password, which \p securityHandler does not open; nothing was read (may be null)
    /// \param isRestored The objects were restored of a damaged file (may be null)
    /// \returns the document, std::nullopt when the data cannot be read
    static std::optional<PDFDocument> readDocument(const QByteArray& data, const PDFSecurityHandler* securityHandler, bool* isPasswordProtected, bool* isRestored);

    /// Reads data of a file for the signer without a security handler (readDocument above with null)
    static std::optional<PDFDocument> readDocument(const QByteArray& data, bool* isPasswordProtected, bool* isRestored);

    /// Returns true, if the file of the data contains at least one signed signature field (R6-12:
    /// the signatures of the file, not of a document in the memory); a file, which cannot be read
    /// (an open password, which \p securityHandler does not open, damaged data), counts as signed -
    /// nothing may be written over it whole
    /// \param data Bytes of the file (empty = no file: false)
    /// \param securityHandler The authenticated handler of the document of the file (readDocument), may be null
    static bool hasExistingSignatures(const QByteArray& data, const PDFSecurityHandler* securityHandler);

    /// Returns true, if the file of the data contains at least one signed signature field
    /// (hasExistingSignatures above without a security handler)
    static bool hasExistingSignatures(const QByteArray& data);

    /// Returns the localized message describing the result
    /// \param result Result
    static QString getResultMessage(Result result);

    /// Returns true, if the document contains at least one signed signature field
    /// \param document Document
    static bool hasExistingSignatures(const PDFDocument* document);

private:
    /// Space added to the size of the trial signature. It is only the initial
    /// estimate - when the final signature does not fit, the reserved space is
    /// enlarged and the document is signed again.
    static constexpr int INITIAL_RESERVE_EXTRA_BYTES = 1024;

    /// Maximal count of the signing attempts
    static constexpr int MAXIMAL_ATTEMPT_COUNT = 4;

    /// Value of the Type entry of the signature dictionary of a document timestamp
    static constexpr const char* DOCUMENT_TIMESTAMP_TYPE = "DocTimeStamp";

    /// Placeholder written into the byte range array, it is replaced by the real
    /// offsets when the document is written
    static constexpr const char* BYTE_RANGE_MARK_STRING = "123456789123";
    static constexpr PDFInteger BYTE_RANGE_MARK = 123456789123;

    /// Returns the data signed only to find out the size of the signature
    static QByteArray getTrialData();

    /// The password callback of the readings of the signer (R6-1): it refuses
    static QString refusePassword(bool* ok);

    /// The built document with the document information of \p document (R6-3, R6-4: the revision of
    /// a signature changes no /Info - PDFDocumentBuilder::build updates /ModDate and /Producer)
    /// \param document The document, which is signed
    /// \param builtDocument The document of the builder with the signature
    static PDFDocument keepDocumentInformation(const PDFDocument& document, PDFDocument builtDocument);

    /// True, if the document differs from the original: an object of the storage or an entry of the
    /// trailer, which an update writes (the rule of PDFDocumentWriter::writeIncrementalUpdate)
    static bool isDocumentChanged(const PDFDocument& original, const PDFDocument& document);

    /// The written signature dictionary of one attempt (B122): the positions in the written bytes
    struct WrittenDictionary
    {
        qsizetype contentsBegin = -1;               ///< Position of '<' of /Contents
        qsizetype contentsEnd = -1;                 ///< Position after '>' of /Contents
        std::vector<qsizetype> byteRangeMarks;      ///< Positions of the four marks of /ByteRange
    };

    /// Finds /Contents and /ByteRange inside the written signature dictionary (B122): the object
    /// of the dictionary in the part written by this attempt, its keys before its "endobj"
    /// \param data The written bytes
    /// \param updateStart Where the written part begins (the length of the original data)
    /// \param signatureDictionary The signature dictionary
    /// \param reservedHexLength Hexadecimal digits of the reserved /Contents
    static std::optional<WrittenDictionary> findWrittenDictionary(const QByteArray& data, qsizetype updateStart, PDFObjectReference signatureDictionary, qsizetype reservedHexLength);

    /// Signs the document once. When the signature does not fit into the
    /// reserved space, \p requiredSignatureSize is set to the size of the
    /// signature which did not fit.
    /// \param parameters Signing parameters
    /// \param document The document to be signed
    /// \param baseData The bytes, which the update follows (empty = a whole write)
    /// \param reservedSignatureSize Size of the space reserved for the signature
    /// \param preserveExistingSignatures Document must be written as an incremental update
    /// \param signedDocument Bytes of the signed document
    /// \param requiredSignatureSize Size of the signature, which did not fit
    /// \param details What was written
    static Result signAttempt(const Parameters& parameters,
                              const PDFDocument* document,
                              const QByteArray& baseData,
                              qint64 reservedSignatureSize,
                              bool preserveExistingSignatures,
                              QByteArray& signedDocument,
                              qint64& requiredSignatureSize,
                              Details& details);

    /// Verifies the signature of the given field in the written document
    /// \param signedDocument Bytes of the signed document
    /// \param signatureField Field holding the signature being verified
    /// \param isDocumentTimestamp Field holds a document timestamp
    /// \param securityHandler The authenticated handler of the signed document (an encrypted file, R6-1), may be null
    static bool verifySignedDocument(const QByteArray& signedDocument,
                                     PDFObjectReference signatureField,
                                     bool isDocumentTimestamp,
                                     const PDFSecurityHandler* securityHandler);

    /// Verifies, that the timestamp token of the given field belongs to the
    /// bytes of the written document covered by it. The certificate chain of
    /// the timestamp authority is not verified - it is unknown here and the
    /// trust in the authority is a decision of the user, not a property of
    /// the signing process.
    /// \param form Form of the written document
    /// \param signedDocument Bytes of the signed document
    /// \param signatureField Field holding the timestamp being verified
    static bool verifyDocumentTimestamp(const PDFForm& form,
                                        const QByteArray& signedDocument,
                                        PDFObjectReference signatureField);
};

}   // namespace pdf

#endif // PDFDOCUMENTSIGNER_H
