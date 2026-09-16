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
#include "pdfobject.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QString>

#include <functional>

namespace pdf
{
class PDFDocument;
class PDFDocumentBuilder;
class PDFProgress;

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
        ExistingSignaturesNotPreserved  ///< Document contains signatures, which would be damaged by writing the document
    };

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
        QDateTime signingTime = QDateTime::currentDateTime();

        /// Progress reporting of the document writer, can be nullptr
        PDFProgress* progress = nullptr;
    };

    /// Signs the document. Returns Result::OK and fills \p signedDocument only
    /// when the newly created signature of the written document has been
    /// verified, otherwise \p signedDocument is left untouched.
    /// \param parameters Signing parameters
    /// \param signedDocument Bytes of the signed document
    static Result sign(const Parameters& parameters, QByteArray& signedDocument);

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

    /// Placeholder written into the byte range array, it is replaced by the real
    /// offsets when the document is written
    static constexpr const char* BYTE_RANGE_MARK_STRING = "123456789123";
    static constexpr PDFInteger BYTE_RANGE_MARK = 123456789123;

    /// Returns the data signed only to find out the size of the signature
    static QByteArray getTrialData();

    /// Signs the document once. When the signature does not fit into the
    /// reserved space, \p requiredSignatureSize is set to the size of the
    /// signature which did not fit.
    /// \param parameters Signing parameters
    /// \param reservedSignatureSize Size of the space reserved for the signature
    /// \param preserveExistingSignatures Document must be written as an incremental update
    /// \param signedDocument Bytes of the signed document
    /// \param requiredSignatureSize Size of the signature, which did not fit
    static Result signAttempt(const Parameters& parameters,
                              int reservedSignatureSize,
                              bool preserveExistingSignatures,
                              QByteArray& signedDocument,
                              int& requiredSignatureSize);

    /// Verifies the signature of the given field in the written document
    /// \param signedDocument Bytes of the signed document
    /// \param signatureField Field holding the signature being verified
    static bool verifySignedDocument(const QByteArray& signedDocument, PDFObjectReference signatureField);
};

}   // namespace pdf

#endif // PDFDOCUMENTSIGNER_H
