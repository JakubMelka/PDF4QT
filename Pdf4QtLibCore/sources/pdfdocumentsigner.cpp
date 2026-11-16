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

#include "pdfdocumentsigner.h"

#include "pdfcertificatemanager.h"
#include "pdfdocmdppermissions.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfform.h"
#include "pdfsignaturehandler.h"

#include <QBuffer>

#include <algorithm>
#include <cstring>

#include "pdfdbgheap.h"

namespace pdf
{

QByteArray PDFDocumentSigner::getTrialData()
{
    return QByteArray("PDF4QT trial data to be signed");
}

QString PDFDocumentSigner::refusePassword(bool* ok)
{
    *ok = false;
    return QString();
}

std::optional<PDFDocument> PDFDocumentSigner::readDocument(const QByteArray& data, bool* isPasswordProtected, bool* isRestored)
{
    return readDocument(data, nullptr, isPasswordProtected, isRestored);
}

std::optional<PDFDocument> PDFDocumentSigner::readDocument(const QByteArray& data, const PDFSecurityHandler* securityHandler, bool* isPasswordProtected, bool* isRestored)
{
    // R6-1: the signer knows no password - an encrypted file is opened by a clone of the handler of
    // the document, never by a password; an empty callback would be called by the security handler
    // of a file, which the handler does not open (std::bad_function_call)
    PDFDocumentReader reader(nullptr, &PDFDocumentSigner::refusePassword, true, false);
    reader.setAuthenticatedSecurityHandler(securityHandler);
    PDFDocument document = reader.readFromBuffer(data);

    const bool isRead = reader.getReadingResult() == PDFDocumentReader::Result::OK;
    if (isPasswordProtected)
    {
        *isPasswordProtected = reader.getReadingResult() == PDFDocumentReader::Result::Canceled;
    }
    if (isRestored)
    {
        *isRestored = isRead && reader.isDocumentRestored();
    }

    if (!isRead)
    {
        return std::nullopt;
    }
    return document;
}

bool PDFDocumentSigner::hasExistingSignatures(const PDFDocument* document)
{
    bool result = false;

    if (!document || !document->getCatalog())
    {
        return false;
    }

    const PDFForm form = PDFForm::parse(document, document->getCatalog()->getFormObject());
    form.apply([&result](const PDFFormField* field)
    {
        if (field->getFieldType() == PDFFormField::FieldType::Signature)
        {
            const PDFFormFieldSignature* signatureField = dynamic_cast<const PDFFormFieldSignature*>(field);
            if (signatureField && !signatureField->getSignature().getContents().isEmpty())
            {
                result = true;
            }
        }
    });

    return result;
}

bool PDFDocumentSigner::hasExistingSignatures(const QByteArray& data)
{
    return hasExistingSignatures(data, nullptr);
}

bool PDFDocumentSigner::hasExistingSignatures(const QByteArray& data, const PDFSecurityHandler* securityHandler)
{
    if (data.isEmpty())
    {
        return false;
    }

    // A file, which cannot be read, may be signed: nothing is written over it whole
    const std::optional<PDFDocument> document = readDocument(data, securityHandler, nullptr, nullptr);
    return !document || hasExistingSignatures(&*document);
}

bool PDFDocumentSigner::isDocumentChanged(const PDFDocument& original, const PDFDocument& document)
{
    // The rule of the incremental update (PDFDocumentWriter::writeIncrementalUpdate): an object,
    // which differs from the original, a new object, which is not null
    const PDFObjectStorage::PDFObjects& objects = document.getStorage().getObjects();
    const PDFObjectStorage::PDFObjects& originalObjects = original.getStorage().getObjects();
    for (size_t i = 1; i < objects.size(); ++i)
    {
        const bool isNewObject = i >= originalObjects.size();
        if (isNewObject ? !objects[i].object.isNull() : objects[i] != originalObjects[i])
        {
            return true;
        }
    }

    // The entries of the trailer, which an update writes
    const PDFDictionary* trailer = document.getTrailerDictionary();
    const PDFDictionary* originalTrailer = original.getTrailerDictionary();
    if (!trailer || !originalTrailer)
    {
        return trailer != originalTrailer;
    }

    for (const char* key : { "Root", "Encrypt", "Info", "ID" })
    {
        if (!(trailer->get(key) == originalTrailer->get(key)))
        {
            return true;
        }
    }
    return false;
}

PDFDocument PDFDocumentSigner::keepDocumentInformation(const PDFDocument& document, PDFDocument builtDocument)
{
    PDFObjectStorage storage = builtDocument.getStorage();
    const PDFDictionary* trailer = document.getTrailerDictionary();
    const PDFDictionary* builtTrailer = builtDocument.getTrailerDictionary();
    const PDFObject information = trailer ? trailer->get("Info") : PDFObject();
    const PDFObject builtInformation = builtTrailer ? builtTrailer->get("Info") : PDFObject();

    if (builtInformation.isReference())
    {
        // The object of the information of the document as it was; a new one is dropped (null, the
        // writer writes no new null object)
        const PDFObjectReference reference = builtInformation.getReference();
        storage.setObject(reference, information.isReference() && information.getReference() == reference ? document.getObjectByReference(reference) : PDFObject());
    }

    PDFDictionaryBuilder trailerUpdate;
    trailerUpdate.addEntry(PDFInplaceOrMemoryString("Info"), information);
    storage.updateTrailerDictionary(PDFObject::createDictionary(std::move(trailerUpdate)));
    return PDFDocument(std::move(storage), builtDocument.getInfo()->version, QByteArray());
}

PDFDocumentSigner::Result PDFDocumentSigner::sign(const Parameters& parameters, QByteArray& signedDocument)
{
    return sign(parameters, signedDocument, nullptr);
}

PDFDocumentSigner::Result PDFDocumentSigner::sign(const Parameters& parameters, QByteArray& signedDocument, Details* details)
{
    if (!parameters.document || !parameters.signFunction || !parameters.createSignatureFieldFunction)
    {
        return Result::NoDocument;
    }

    const bool isDocumentTimestamp = parameters.signatureDictionaryType == DOCUMENT_TIMESTAMP_TYPE;

    // B135: the permissions of the encrypted document (Table 22), which the caller names
    const PDFSecurityHandler* securityHandler = parameters.document->getStorage().getSecurityHandler();
    for (const PDFSecurityHandler::Permission permission : parameters.requiredPermissions)
    {
        if (securityHandler && !PDFSecurityHandler::isAllowedByRevision(securityHandler, permission))
        {
            return Result::NotPermitted;
        }
    }
    if (securityHandler && !parameters.anyOfPermissions.empty() &&
        std::none_of(parameters.anyOfPermissions.cbegin(), parameters.anyOfPermissions.cend(),
                     [securityHandler](PDFSecurityHandler::Permission permission) { return PDFSecurityHandler::isAllowedByRevision(securityHandler, permission); }))
    {
        return Result::NotPermitted;
    }

    // R6-12: the signatures of the file are counted of its bytes, never only of the document in
    // the memory (an unsaved change, which removed a field, would otherwise lead to a whole write
    // over a signed file). R6-1: a file with an open password is read back by the authenticated
    // handler of the document; only a document of another encryption cannot read it.
    std::optional<PDFDocument> original;
    bool isOriginalRestored = false;
    if (!parameters.originalDocumentData.isEmpty())
    {
        bool isPasswordProtected = false;
        original = readDocument(parameters.originalDocumentData, securityHandler, &isPasswordProtected, &isOriginalRestored);
        if (isPasswordProtected)
        {
            return Result::PasswordProtected;
        }

        // A file, which cannot be read, counts as signed (hasExistingSignatures): no update of it can
        // be written and the document in the memory never decides about a whole write over it
        if (!original)
        {
            return Result::ExistingSignaturesNotPreserved;
        }
    }

    const PDFDocument* signedFile = original ? &*original : nullptr;
    const bool preserveExistingSignatures = hasExistingSignatures(signedFile ? signedFile : parameters.document);
    if (preserveExistingSignatures && parameters.originalDocumentData.isEmpty())
    {
        return Result::ExistingSignaturesNotPreserved;
    }

    // B135, R6-4 (ISO 32000-2, 12.8.2.2.1, Table 257 /P 1): a certified document with no changes
    // allowed gets no signature; a document timestamp is no change of the document, but only in a
    // revision, which carries nothing else - never the unsaved changes of the document
    const bool hasChanges = signedFile && !isOriginalRestored ? isDocumentChanged(*signedFile, *parameters.document) : true;
    const PDFDocMDPPermissions::Level level = PDFDocMDPPermissions::getLevel(signedFile ? signedFile : parameters.document);
    if (level == PDFDocMDPPermissions::Level::NoChanges && (!isDocumentTimestamp || hasChanges))
    {
        return Result::CertifiedNoChanges;
    }

    // R6-4: the unsaved changes of a signed (or certified) file go into their own revision before
    // the revision of the signature, so a verifier assigns every change to its revision
    Details written;
    written.hasChanges = signedFile && hasChanges;
    const PDFDocument* documentToSign = parameters.document;
    QByteArray baseData = parameters.originalDocumentData;
    std::optional<PDFDocument> changesDocument;
    if (preserveExistingSignatures && signedFile && !isOriginalRestored && hasChanges)
    {
        QByteArray changesData;
        {
            QBuffer buffer(&changesData);
            if (!buffer.open(QBuffer::WriteOnly))
            {
                return Result::WriteFailed;
            }

            PDFDocumentWriter writer(parameters.progress);
            if (!writer.writeIncrementalUpdate(&buffer, parameters.originalDocumentData, parameters.document))
            {
                return Result::ExistingSignaturesNotPreserved;
            }
        }

        bool isChangesRestored = false;
        changesDocument = readDocument(changesData, securityHandler, nullptr, &isChangesRestored);
        if (!changesDocument || isChangesRestored)
        {
            return Result::WriteFailed;
        }

        documentToSign = &*changesDocument;
        baseData = std::move(changesData);
        written.isChangesRevisionWritten = true;
    }

    // The space of the signature: computed by the caller (B134, no trial signature, one more
    // attempt at most - R6-28), or measured by a trial signature (the old applications). The size
    // of a signature is not stable, so a trial signature is enlarged by a reserve.
    qint64 reservedSignatureSize = 0;
    int attemptCount = MAXIMAL_ATTEMPT_COUNT;
    if (parameters.estimatedSignatureSize > 0)
    {
        reservedSignatureSize = parameters.estimatedSignatureSize;
        attemptCount = 2;
    }
    else
    {
        QByteArray trialSignature;
        if (!parameters.signFunction(getTrialData(), trialSignature) || trialSignature.isEmpty())
        {
            return Result::SigningFailed;
        }
        reservedSignatureSize = trialSignature.size() + INITIAL_RESERVE_EXTRA_BYTES;
    }

    for (int attempt = 0; attempt < attemptCount; ++attempt)
    {
        if (reservedSignatureSize > MAXIMUM_SIGNATURE_SIZE)
        {
            return Result::SignatureTooLarge;
        }

        QByteArray attemptResult;
        qint64 requiredSignatureSize = 0;
        const Result result = signAttempt(parameters, documentToSign, baseData, reservedSignatureSize, preserveExistingSignatures, attemptResult, requiredSignatureSize, written);

        if (result == Result::OK)
        {
            signedDocument = std::move(attemptResult);
            if (details)
            {
                *details = written;
            }
            return Result::OK;
        }

        if (result != Result::SignatureTooLarge)
        {
            return result;
        }

        // The signature did not fit. We know how large it was, so we reserve
        // that much plus the reserve for the next size change, and sign again -
        // the whole document must be rebuilt, because the byte offsets change.
        reservedSignatureSize = qMax(requiredSignatureSize + INITIAL_RESERVE_EXTRA_BYTES, reservedSignatureSize * 2);
    }

    return Result::SignatureTooLarge;
}

std::optional<PDFDocumentSigner::WrittenDictionary> PDFDocumentSigner::findWrittenDictionary(const QByteArray& data,
                                                                                             qsizetype updateStart,
                                                                                             PDFObjectReference signatureDictionary,
                                                                                             qsizetype reservedHexLength)
{
    // The reserved /Contents: a hexadecimal string of zeros of the reserved length (B121: always
    // hexadecimal), exactly once in the part written now - otherwise we do not know, which
    // occurrence belongs to the signature we are creating
    const QByteArray contents = QByteArray("/Contents <") + QByteArray(reservedHexLength, '0') + QByteArray(">");
    const qsizetype contentsKey = data.indexOf(contents, updateStart);
    if (contentsKey == -1 || data.indexOf(contents, contentsKey + 1) != -1)
    {
        return std::nullopt;
    }

    // B122: the string is the value of /Contents of the signature dictionary itself - the object of
    // the dictionary begins on its own line before it and does not end before it
    const QByteArray header = QByteArray::number(signatureDictionary.objectNumber) + ' ' + QByteArray::number(signatureDictionary.generation) + QByteArray(" obj");
    qsizetype objectStart = data.lastIndexOf(header, contentsKey);
    while (objectStart > updateStart && data[objectStart - 1] != '\n' && data[objectStart - 1] != '\r')
    {
        objectStart = data.lastIndexOf(header, objectStart - 1);
    }

    if (objectStart < updateStart || (objectStart > 0 && data[objectStart - 1] != '\n' && data[objectStart - 1] != '\r'))
    {
        return std::nullopt;
    }

    const qsizetype objectEnd = data.indexOf("endobj", objectStart);
    if (objectEnd == -1 || objectEnd < contentsKey)
    {
        return std::nullopt;
    }

    // /ByteRange of the same dictionary: its four marks between its brackets
    const qsizetype byteRangeKey = data.indexOf("/ByteRange [", objectStart);
    const qsizetype byteRangeEnd = byteRangeKey != -1 ? data.indexOf(']', byteRangeKey) : -1;
    if (byteRangeKey == -1 || byteRangeEnd == -1 || byteRangeEnd > objectEnd)
    {
        return std::nullopt;
    }

    WrittenDictionary result;
    const QByteArray byteRangeMark(BYTE_RANGE_MARK_STRING);
    for (qsizetype position = data.indexOf(byteRangeMark, byteRangeKey); position != -1 && position < byteRangeEnd; position = data.indexOf(byteRangeMark, position + byteRangeMark.size()))
    {
        result.byteRangeMarks.push_back(position);
    }
    if (result.byteRangeMarks.size() != 4)
    {
        return std::nullopt;
    }

    result.contentsBegin = contentsKey + qsizetype(std::strlen("/Contents "));
    result.contentsEnd = contentsKey + contents.size();
    return result;
}

PDFDocumentSigner::Result PDFDocumentSigner::signAttempt(const Parameters& parameters,
                                                         const PDFDocument* document,
                                                         const QByteArray& baseData,
                                                         qint64 reservedSignatureSize,
                                                         bool preserveExistingSignatures,
                                                         QByteArray& signedDocument,
                                                         qint64& requiredSignatureSize,
                                                         Details& details)
{
    // The placeholder is a string of zeros of the reserved size: the writer writes it as a
    // hexadecimal string (B121) and all of it is overwritten by the final signature
    const QByteArray placeholder(reservedSignatureSize, char(0));
    const bool isDocumentTimestamp = parameters.signatureDictionaryType == DOCUMENT_TIMESTAMP_TYPE;

    PDFDocumentBuilder builder(document);
    const PDFObjectReference signatureDictionary = builder.createSignatureDictionary(parameters.filter,
                                                                                    parameters.subfilter,
                                                                                    placeholder,
                                                                                    parameters.signingTime,
                                                                                    BYTE_RANGE_MARK,
                                                                                    parameters.signatureDictionaryType);

    // The time of a document timestamp is the time attested by the timestamp
    // authority and stored in the timestamp token. The time of the computer,
    // which created it, says nothing about the document, so it is removed from
    // the dictionary of the timestamp.
    if (isDocumentTimestamp)
    {
        if (const PDFDictionary* dictionary = builder.getDictionaryFromObject(builder.getObjectByReference(signatureDictionary)))
        {
            PDFDictionaryBuilder timestampDictionary(*dictionary);
            timestampDictionary.removeEntry("M");
            builder.setObject(signatureDictionary, PDFObject::createDictionary(qMove(timestampDictionary)));
        }
    }
    const PDFObjectReference signatureField = parameters.createSignatureFieldFunction(builder, signatureDictionary);
    if (!signatureField.isValid())
    {
        return Result::WriteFailed;
    }

    // R6-3, R6-4: the revision of a signature carries the signature, its field and widget, /AcroForm
    // and /Annots - nothing else; build() updates /ModDate and /Producer of the document information,
    // which is a change of the signed document (a certification with /P 2 forbids it)
    PDFDocument documentToBeSigned = keepDocumentInformation(*document, builder.build());

    QBuffer buffer;
    if (!buffer.open(QBuffer::ReadWrite))
    {
        return Result::WriteFailed;
    }

    // The incremental update keeps the original bytes intact, so it is used
    // whenever the original data are available. When they cannot be updated,
    // the document is written as a whole - unless it would damage signatures
    // already present in it.
    PDFDocumentWriter writer(parameters.progress);
    bool isWritten = false;
    bool isIncrementalUpdate = false;
    if (!baseData.isEmpty())
    {
        isWritten = bool(writer.writeIncrementalUpdate(&buffer, baseData, &documentToBeSigned));
        if (!isWritten && preserveExistingSignatures)
        {
            return Result::ExistingSignaturesNotPreserved;
        }
        isIncrementalUpdate = isWritten;
    }

    if (!isWritten)
    {
        buffer.buffer().clear();
        buffer.seek(0);
        isWritten = bool(writer.write(&buffer, &documentToBeSigned));
    }

    if (!isWritten)
    {
        return Result::WriteFailed;
    }

    QByteArray data = buffer.data();
    buffer.close();

    const qsizetype reservedHexLength = qsizetype(reservedSignatureSize) * 2;
    const std::optional<WrittenDictionary> written = findWrittenDictionary(data, isIncrementalUpdate ? baseData.size() : 0, signatureDictionary, reservedHexLength);
    if (!written)
    {
        return Result::PlaceholderNotFound;
    }

    // Byte ranges of the signed data - everything except the hexadecimal string
    // holding the signature, including its angle brackets (ISO 32000-2, 12.8.1)
    const qint64 byteRange[4] = { 0, written->contentsBegin, written->contentsEnd, qint64(data.size()) - written->contentsEnd };
    const qsizetype markLength = qsizetype(std::strlen(BYTE_RANGE_MARK_STRING));
    for (size_t i = 0; i < 4; ++i)
    {
        const QByteArray number = QByteArray::number(byteRange[i]);
        if (number.size() > markLength)
        {
            return Result::WriteFailed;
        }
        data.replace(written->byteRangeMarks[i], markLength, number.leftJustified(markLength, ' '));
    }

    QByteArray dataToBeSigned;
    dataToBeSigned.reserve(qsizetype(byteRange[1] + byteRange[3]));
    dataToBeSigned.append(data.constData(), qsizetype(byteRange[1]));
    dataToBeSigned.append(data.constData() + byteRange[2], qsizetype(byteRange[3]));

    QByteArray signature;
    if (!parameters.signFunction(dataToBeSigned, signature) || signature.isEmpty())
    {
        return Result::SigningFailed;
    }

    if (signature.size() > reservedSignatureSize)
    {
        requiredSignatureSize = signature.size();
        return Result::SignatureTooLarge;
    }

    // The signature is usually smaller than the reserved space, so the rest of
    // the hexadecimal string is filled with zeros. They are not a part of the
    // DER encoded signature, which carries its own length, and they keep the
    // byte ranges valid whatever the size of the signature is.
    QByteArray signatureHex = signature.toHex();
    signatureHex.resize(reservedHexLength, '0');
    data.replace(written->contentsBegin + 1, reservedHexLength, signatureHex);

    if (!verifySignedDocument(data, signatureField, isDocumentTimestamp, document->getStorage().getSecurityHandler()))
    {
        return Result::VerificationFailed;
    }

    details.isIncrementalUpdate = isIncrementalUpdate;
    details.signatureField = signatureField;
    details.signatureDictionary = signatureDictionary;
    signedDocument = std::move(data);
    return Result::OK;
}

bool PDFDocumentSigner::verifySignedDocument(const QByteArray& signedDocument,
                                             PDFObjectReference signatureField,
                                             bool isDocumentTimestamp,
                                             const PDFSecurityHandler* securityHandler)
{
    const std::optional<PDFDocument> document = readDocument(signedDocument, securityHandler, nullptr, nullptr);
    if (!document)
    {
        return false;
    }

    const PDFForm form = PDFForm::parse(&*document, document->getCatalog()->getFormObject());

    if (isDocumentTimestamp)
    {
        return verifyDocumentTimestamp(form, signedDocument, signatureField);
    }

    // We are verifying the signature we have just created, so we are interested
    // in this signature only - other signatures of the document can be invalid
    // for reasons unrelated to the signing, and the certificate can be untrusted
    // or self-signed, which is a decision of the user, not an error of the
    // signing process.
    PDFCertificateStore store;
    PDFSignatureHandler::Parameters parameters;
    parameters.store = &store;
    parameters.enableVerification = true;
    parameters.useSystemCertificateStore = false;
    parameters.securityHandler = document->getStorage().getSecurityHandler();

    const std::vector<PDFSignatureVerificationResult> results = PDFSignatureHandler::verifySignatures(form, signedDocument, parameters);

    auto isCreatedSignature = [signatureField](const PDFSignatureVerificationResult& result) { return result.getSignatureFieldReference() == signatureField; };
    auto it = std::find_if(results.cbegin(), results.cend(), isCreatedSignature);
    return it != results.cend() && it->isSignatureValid();
}

bool PDFDocumentSigner::verifyDocumentTimestamp(const PDFForm& form,
                                                const QByteArray& signedDocument,
                                                PDFObjectReference signatureField)
{
    const PDFSignature* timestamp = nullptr;
    form.apply([&timestamp, signatureField](const PDFFormField* field)
    {
        if (field->getSelfReference() != signatureField)
        {
            return;
        }

        if (const PDFFormFieldSignature* signatureFormField = dynamic_cast<const PDFFormFieldSignature*>(field))
        {
            timestamp = &signatureFormField->getSignature();
        }
    });

    if (!timestamp || timestamp->getType() != PDFSignature::Type::DocTimeStamp)
    {
        return false;
    }

    // The bytes attested by the timestamp are reconstructed from the byte ranges
    // of the written document, exactly as a verifier of the document would do it.
    QByteArray timestampedData;
    for (const PDFSignature::ByteRange& byteRange : timestamp->getByteRanges())
    {
        if (byteRange.offset < 0 || byteRange.size < 0 || byteRange.offset > signedDocument.size() || byteRange.size > signedDocument.size() - byteRange.offset)
        {
            return false;
        }

        timestampedData.append(signedDocument.constData() + byteRange.offset, byteRange.size);
    }

    return PDFSignatureFactory::verifyTimestampToken(timestampedData, timestamp->getContents());
}

QString PDFDocumentSigner::getResultMessage(Result result)
{
    switch (result)
    {
        case Result::OK:
            return QString();

        case Result::NoDocument:
            return tr("No document to be signed.");

        case Result::SigningFailed:
            return tr("Failed to create digital signature.");

        case Result::WriteFailed:
            return tr("Failed to write the signed document.");

        case Result::PlaceholderNotFound:
            return tr("Failed to find the space reserved for the digital signature in the written document.");

        case Result::SignatureTooLarge:
            return tr("Digital signature does not fit into the space reserved for it.");

        case Result::VerificationFailed:
            return tr("Digital signature of the created document could not be verified, the document was not saved.");

        case Result::ExistingSignaturesNotPreserved:
            return tr("The document already contains digital signatures, which cannot be preserved, so the document was not signed.");

        case Result::NotPermitted:
            return tr("The permissions of the document do not allow to sign it.");

        case Result::CertifiedNoChanges:
            return tr("This document is certified with no changes allowed. It cannot be signed, and a timestamp can be added only without unsaved changes.");

        case Result::PasswordProtected:
            return tr("The document is protected by an open password, so its signed copy cannot be verified and it was not signed.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

}   // namespace pdf
