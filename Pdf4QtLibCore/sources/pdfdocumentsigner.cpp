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
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfform.h"
#include "pdfsignaturehandler.h"

#include <QBuffer>

#include <algorithm>

#include "pdfdbgheap.h"

namespace pdf
{

QByteArray PDFDocumentSigner::getTrialData()
{
    return QByteArray("PDF4QT trial data to be signed");
}

bool PDFDocumentSigner::hasExistingSignatures(const PDFDocument* document)
{
    bool result = false;

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

PDFDocumentSigner::Result PDFDocumentSigner::sign(const Parameters& parameters, QByteArray& signedDocument)
{
    if (!parameters.document || !parameters.signFunction || !parameters.createSignatureFieldFunction)
    {
        return Result::NoDocument;
    }

    // Signatures already present in the document stay valid only when the bytes
    // they cover are not touched, so the document must be written as an
    // incremental update of the original data, which requires them.
    const bool preserveExistingSignatures = hasExistingSignatures(parameters.document);
    if (preserveExistingSignatures && parameters.originalDocumentData.isEmpty())
    {
        return Result::ExistingSignaturesNotPreserved;
    }

    // The size of the signature is not stable, so we cannot reserve exactly the
    // size of a trial signature. We start with the trial signature enlarged by
    // the initial reserve and enlarge it further, if the final signature does
    // not fit into the reserved space.
    QByteArray trialSignature;
    if (!parameters.signFunction(getTrialData(), trialSignature) || trialSignature.isEmpty())
    {
        return Result::SigningFailed;
    }

    int reservedSignatureSize = trialSignature.size() + INITIAL_RESERVE_EXTRA_BYTES;

    for (int attempt = 0; attempt < MAXIMAL_ATTEMPT_COUNT; ++attempt)
    {
        QByteArray attemptResult;
        int requiredSignatureSize = 0;
        const Result result = signAttempt(parameters, trialSignature, reservedSignatureSize, preserveExistingSignatures, attemptResult, requiredSignatureSize);

        if (result == Result::OK)
        {
            signedDocument = qMove(attemptResult);
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

PDFDocumentSigner::Result PDFDocumentSigner::signAttempt(const Parameters& parameters,
                                                         const QByteArray& trialSignature,
                                                         int reservedSignatureSize,
                                                         bool preserveExistingSignatures,
                                                         QByteArray& signedDocument,
                                                         int& requiredSignatureSize)
{
    // The placeholder keeps the trial signature at its beginning, so that it can be
    // reliably found in the written document, and is padded to the reserved size.
    // All of it is overwritten by the final signature. The trial signature is reused
    // here, because creating it can be expensive - a timestamp of the signature is
    // requested from a timestamp authority over the network.
    QByteArray placeholder = trialSignature;
    if (placeholder.isEmpty())
    {
        return Result::SigningFailed;
    }

    if (placeholder.size() > reservedSignatureSize)
    {
        requiredSignatureSize = int(placeholder.size());
        return Result::SignatureTooLarge;
    }

    placeholder.resize(reservedSignatureSize, char(0));

    const bool isDocumentTimestamp = parameters.signatureDictionaryType == DOCUMENT_TIMESTAMP_TYPE;

    PDFDocumentBuilder builder(parameters.document);
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
            PDFDictionary timestampDictionary(*dictionary);
            timestampDictionary.removeEntry("M");
            builder.setObject(signatureDictionary, PDFObject::createDictionary(std::make_shared<PDFDictionary>(qMove(timestampDictionary))));
        }
    }
    const PDFObjectReference signatureField = parameters.createSignatureFieldFunction(builder, signatureDictionary);

    PDFDocument documentToBeSigned = builder.build();

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
    if (!parameters.originalDocumentData.isEmpty())
    {
        isWritten = bool(writer.writeIncrementalUpdate(&buffer, parameters.originalDocumentData, &documentToBeSigned));
        if (!isWritten && preserveExistingSignatures)
        {
            return Result::ExistingSignaturesNotPreserved;
        }
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

    const QByteArray placeholderHex = placeholder.toHex();
    const qsizetype indexOfSignature = buffer.data().indexOf(placeholderHex);

    // The placeholder must be present exactly once - otherwise we do not know,
    // which occurence belongs to the signature we are creating.
    if (indexOfSignature == -1 || buffer.data().lastIndexOf(placeholderHex) != indexOfSignature)
    {
        return Result::PlaceholderNotFound;
    }

    // Byte ranges of the signed data - everything except the hexadecimal string
    // holding the signature, including its angle brackets
    const PDFInteger i1 = 0;
    const PDFInteger i2 = indexOfSignature - 1;
    const PDFInteger i3 = i2 + PDFInteger(placeholderHex.size()) + 2;
    const PDFInteger i4 = buffer.data().size() - i3;

    const QByteArray byteRangeMark(BYTE_RANGE_MARK_STRING);
    auto writeInt = [&](PDFInteger offset) -> bool
    {
        QString offsetString = QString::number(offset);
        offsetString = offsetString.leftJustified(int(byteRangeMark.size()), QChar::Space, true);
        const qsizetype index = buffer.data().lastIndexOf(byteRangeMark, indexOfSignature);
        if (index == -1)
        {
            return false;
        }

        buffer.seek(index);
        return buffer.write(offsetString.toLatin1()) == byteRangeMark.size();
    };

    if (!writeInt(i4) || !writeInt(i3) || !writeInt(i2) || !writeInt(i1))
    {
        return Result::WriteFailed;
    }

    QByteArray dataToBeSigned;
    buffer.seek(i1);
    dataToBeSigned.append(buffer.read(i2));
    buffer.seek(i3);
    dataToBeSigned.append(buffer.read(i4));

    QByteArray signature;
    if (!parameters.signFunction(dataToBeSigned, signature) || signature.isEmpty())
    {
        return Result::SigningFailed;
    }

    if (signature.size() > reservedSignatureSize)
    {
        requiredSignatureSize = int(signature.size());
        return Result::SignatureTooLarge;
    }

    // The signature is usually smaller than the reserved space, so the rest of
    // the hexadecimal string is filled with zeros. They are not a part of the
    // DER encoded signature, which carries its own length, and they keep the
    // byte ranges valid whatever the size of the signature is.
    QByteArray signatureHex = signature.toHex();
    signatureHex.resize(placeholderHex.size(), '0');

    buffer.seek(i2 + 1);
    if (buffer.write(signatureHex) != signatureHex.size())
    {
        return Result::WriteFailed;
    }

    QByteArray writtenDocument = buffer.data();
    buffer.close();

    if (!verifySignedDocument(writtenDocument, signatureField, isDocumentTimestamp))
    {
        return Result::VerificationFailed;
    }

    signedDocument = qMove(writtenDocument);
    return Result::OK;
}

bool PDFDocumentSigner::verifySignedDocument(const QByteArray& signedDocument,
                                             PDFObjectReference signatureField,
                                             bool isDocumentTimestamp)
{
    PDFDocumentReader reader(nullptr, nullptr, true, false);
    PDFDocument document = reader.readFromBuffer(signedDocument);

    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        return false;
    }

    const PDFForm form = PDFForm::parse(&document, document.getCatalog()->getFormObject());

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
        if (byteRange.offset < 0 || byteRange.size < 0 || byteRange.offset + byteRange.size > signedDocument.size())
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

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

}   // namespace pdf
