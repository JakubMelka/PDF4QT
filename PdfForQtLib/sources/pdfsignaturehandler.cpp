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

#include "pdfsignaturehandler.h"
#include "pdfdocument.h"
#include "pdfencoding.h"
#include "pdfform.h"
#include "pdfsignaturehandler_impl.h"

#include <QMutex>
#include <QMutexLocker>

#include <array>

namespace pdf
{

static QMutex s_globalOpenSSLMutex(QMutex::Recursive);

/// OpenSSL is not thread safe.
class PDFOpenSSLGlobalLock
{
public:
    explicit inline PDFOpenSSLGlobalLock() : m_mutexLocker(&s_globalOpenSSLMutex) { }
    inline ~PDFOpenSSLGlobalLock() = default;

private:
    QMutexLocker m_mutexLocker;
};

PDFSignatureReference PDFSignatureReference::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFSignatureReference result;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);

        constexpr const std::array<std::pair<const char*, PDFSignatureReference::TransformMethod>, 3> types = {
            std::pair<const char*, PDFSignatureReference::TransformMethod>{ "DocMDP", PDFSignatureReference::TransformMethod::DocMDP },
            std::pair<const char*, PDFSignatureReference::TransformMethod>{ "UR", PDFSignatureReference::TransformMethod::UR },
            std::pair<const char*, PDFSignatureReference::TransformMethod>{ "FieldMDP", PDFSignatureReference::TransformMethod::FieldMDP }
        };

        // Jakub Melka: parse the signature reference dictionary
        result.m_transformMethod = loader.readEnumByName(dictionary->get("TransformMethod"), types.cbegin(), types.cend(), PDFSignatureReference::TransformMethod::Invalid);
        result.m_transformParams = dictionary->get("TransformParams");
        result.m_data = dictionary->get("Data");
        result.m_digestMethod = loader.readNameFromDictionary(dictionary, "DigestMethod");
    }

    return result;
}

PDFSignature PDFSignature::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFSignature result;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);

        constexpr const std::array<std::pair<const char*, Type>, 2> types = {
            std::pair<const char*, Type>{ "Sig", Type::Sig },
            std::pair<const char*, Type>{ "DocTimeStamp", Type::DocTimeStamp }
        };

        // Jakub Melka: parse the signature dictionary
        result.m_type = loader.readEnumByName(dictionary->get("Type"), types.cbegin(), types.cend(), Type::Sig);
        result.m_filter = loader.readNameFromDictionary(dictionary, "Filter");
        result.m_subfilter = loader.readNameFromDictionary(dictionary, "SubFilter");
        result.m_contents = loader.readStringFromDictionary(dictionary, "Contents");

        if (dictionary->hasKey("Cert"))
        {
            PDFObject certificates = storage->getObject(dictionary->get("Cert"));
            if (certificates.isString())
            {
                result.m_certificates = { loader.readString(certificates) };
            }
            else if (certificates.isArray())
            {
                result.m_certificates = loader.readStringArray(certificates);
            }
        }

        std::vector<PDFInteger> byteRangesArray = loader.readIntegerArrayFromDictionary(dictionary, "ByteRange");
        const size_t byteRangeCount = byteRangesArray.size() / 2;
        result.m_byteRanges.reserve(byteRangeCount);
        for (size_t i = 0; i < byteRangeCount; ++i)
        {
            ByteRange byteRange = { byteRangesArray[i], byteRangesArray[i + 1] };
            result.m_byteRanges.push_back(byteRange);
        }

        result.m_references = loader.readObjectList<PDFSignatureReference>(dictionary->get("References"));
        std::vector<PDFInteger> changes = loader.readIntegerArrayFromDictionary(dictionary, "Changes");

        if (changes.size() == 3)
        {
            result.m_changes = { changes[0], changes[1], changes[2] };
        }

        result.m_name = loader.readTextStringFromDictionary(dictionary, "Name", QString());
        result.m_signingDateTime = PDFEncoding::convertToDateTime(loader.readStringFromDictionary(dictionary, "M"));
        result.m_location = loader.readTextStringFromDictionary(dictionary, "Location", QString());
        result.m_reason = loader.readTextStringFromDictionary(dictionary, "Reason", QString());
        result.m_contactInfo = loader.readTextStringFromDictionary(dictionary, "ContactInfo", QString());
        result.m_R = loader.readIntegerFromDictionary(dictionary, "R", 0);
        result.m_V = loader.readIntegerFromDictionary(dictionary, "V", 0);
        result.m_propBuild = dictionary->get("Prop_Build");
        result.m_propTime = loader.readIntegerFromDictionary(dictionary, "Prop_AuthTime", 0);

        constexpr const std::array<std::pair<const char*, AuthentificationType>, 3> authentificationTypes = {
            std::pair<const char*, AuthentificationType>{ "PIN", AuthentificationType::PIN },
            std::pair<const char*, AuthentificationType>{ "Password", AuthentificationType::Password },
            std::pair<const char*, AuthentificationType>{ "Fingerprint", AuthentificationType::Fingerprint }
        };
        result.m_propType = loader.readEnumByName(dictionary->get("Prop_AuthType"), authentificationTypes.cbegin(), authentificationTypes.cend(), AuthentificationType::Invalid);
    }

    return result;
}

PDFSignatureHandler* PDFSignatureHandler::createHandler(const PDFFormFieldSignature* signatureField, const QByteArray& sourceData)
{
    Q_ASSERT(signatureField);

    const QByteArray& subfilter = signatureField->getSignature().getSubfilter();
    if (subfilter == "adbe.pkcs7.detached")
    {
        return new PDFSignatureHandler_adbe_pkcs7_detached(signatureField, sourceData);
    }

    return nullptr;
}

std::vector<PDFSignatureVerificationResult> PDFSignatureHandler::verifySignatures(const PDFForm& form, const QByteArray& sourceData)
{
    std::vector<PDFSignatureVerificationResult> result;

    if (form.isAcroForm() || form.isXFAForm())
    {
        std::vector<const PDFFormFieldSignature*> signatureFields;
        auto getSignatureFields = [&signatureFields](const PDFFormField* field)
        {
            if (field->getFieldType() == PDFFormField::FieldType::Signature)
            {
                const PDFFormFieldSignature* signatureField = dynamic_cast<const PDFFormFieldSignature*>(field);
                Q_ASSERT(signatureField);
                signatureFields.push_back(signatureField);
            }
        };
        form.apply(getSignatureFields);
        result.reserve(signatureFields.size());

        for (const PDFFormFieldSignature* signatureField : signatureFields)
        {
            if (const PDFSignatureHandler* signatureHandler = createHandler(signatureField, sourceData))
            {
                result.emplace_back(signatureHandler->verify());
                delete signatureHandler;
            }
            else
            {
                PDFObjectReference signatureFieldReference = signatureField->getSelfReference();
                QString qualifiedName = signatureField->getName(PDFFormField::NameType::FullyQualified);
                PDFSignatureVerificationResult verificationResult(signatureFieldReference, qMove(qualifiedName));
                verificationResult.addNoHandlerError(signatureField->getSignature().getSubfilter());
                result.emplace_back(qMove(verificationResult));
            }
        }
    }

    return result;
}

void PDFSignatureVerificationResult::addNoHandlerError(const QByteArray& format)
{
    m_flags.setFlag(Error_NoHandler);
    m_errors << PDFTranslationContext::tr("No signature handler for signature format '%1'.").arg(QString::fromLatin1(format));
}

void PDFSignatureVerificationResult::addInvalidCertificateError()
{
    m_flags.setFlag(Error_Certificate_Invalid);
    m_errors << PDFTranslationContext::tr("Certificate format is invalid.");
}

void PDFSignatureVerificationResult::addNoSignaturesError()
{
    m_flags.setFlag(Error_Certificate_NoSignatures);
    m_errors << PDFTranslationContext::tr("No signatures in certificate data.");
}

void PDFSignatureVerificationResult::addCertificateMissingError()
{
    m_flags.setFlag(Error_Certificate_Missing);
    m_errors << PDFTranslationContext::tr("Certificate is missing.");
}

void PDFSignatureVerificationResult::addCertificateGenericError()
{
    m_flags.setFlag(Error_Certificate_Generic);
    m_errors << PDFTranslationContext::tr("Generic error occured during certificate validation.");
}

void PDFSignatureVerificationResult::addCertificateExpiredError()
{
    m_flags.setFlag(Error_Certificate_Expired);
    m_errors << PDFTranslationContext::tr("Certificate has expired.");
}

void PDFSignatureVerificationResult::addCertificateSelfSignedError()
{
    m_flags.setFlag(Error_Certificate_SelfSigned);
    m_errors << PDFTranslationContext::tr("Certificate is self-signed.");
}

void PDFSignatureVerificationResult::addCertificateSelfSignedInChainError()
{
    m_flags.setFlag(Error_Certificate_SelfSignedChain);
    m_errors << PDFTranslationContext::tr("Self-signed certificate in chain.");
}

void PDFSignatureVerificationResult::addCertificateTrustedNotFoundError()
{
    m_flags.setFlag(Error_Certificate_TrustedNotFound);
    m_errors << PDFTranslationContext::tr("Trusted certificate not found.");
}

void PDFSignatureVerificationResult::addCertificateRevokedError()
{
    m_flags.setFlag(Error_Certificate_Revoked);
    m_errors << PDFTranslationContext::tr("Certificate has been revoked.");
}

void PDFSignatureVerificationResult::addCertificateOtherError(int error)
{
    m_flags.setFlag(Error_Certificate_Other);
    m_errors << PDFTranslationContext::tr("Certificate validation failed with code %1.").arg(error);
}

void PDFSignatureVerificationResult::setSignatureFieldQualifiedName(const QString& signatureFieldQualifiedName)
{
    m_signatureFieldQualifiedName = signatureFieldQualifiedName;
}

void PDFSignatureVerificationResult::setSignatureFieldReference(PDFObjectReference signatureFieldReference)
{
    m_signatureFieldReference = signatureFieldReference;
}

void PDFPublicKeySignatureHandler::initializeResult(PDFSignatureVerificationResult& result) const
{
    PDFObjectReference signatureFieldReference = m_signatureField->getSelfReference();
    QString signatureFieldQualifiedName = m_signatureField->getName(PDFFormField::NameType::FullyQualified);
    result.setSignatureFieldReference(signatureFieldReference);
    result.setSignatureFieldQualifiedName(signatureFieldQualifiedName);
}

STACK_OF(X509)* PDFPublicKeySignatureHandler::getCertificates(PKCS7* pkcs7)
{
    if (!pkcs7)
    {
        return nullptr;
    }

    if (PKCS7_type_is_signed(pkcs7))
    {
        return pkcs7->d.sign->cert;
    }

    if (PKCS7_type_is_signedAndEnveloped(pkcs7))
    {
        return pkcs7->d.signed_and_enveloped->cert;
    }

    return nullptr;
}

void PDFPublicKeySignatureHandler::verifyCertificate(PDFSignatureVerificationResult& result) const
{
    PDFOpenSSLGlobalLock lock;

    OpenSSL_add_all_algorithms();

    const PDFSignature& signature = m_signatureField->getSignature();
    const QByteArray& content = signature.getContents();

    // Jakub Melka: we will try to get pkcs7 from signature, then
    // verify signer certificates.
    const unsigned char* data = reinterpret_cast<const unsigned char*>(content.data());
    if (PKCS7* pkcs7 = d2i_PKCS7(nullptr, &data, content.size()))
    {
        X509_STORE* store = X509_STORE_new();
        X509_STORE_CTX* context = X509_STORE_CTX_new();

        // Above functions can fail only if not enough memory. But in this
        // case, this library will crash anyway.
        Q_ASSERT(store);
        Q_ASSERT(context);

        STACK_OF(PKCS7_SIGNER_INFO)* signerInfo = PKCS7_get_signer_info(pkcs7);
        const int signerInfoCount = sk_PKCS7_SIGNER_INFO_num(signerInfo);
        STACK_OF(X509)* certificates = getCertificates(pkcs7);
        if (signerInfo && signerInfoCount > 0 && certificates)
        {
            for (int i = 0; i < signerInfoCount; ++i)
            {
                PKCS7_SIGNER_INFO* signerInfoValue = sk_PKCS7_SIGNER_INFO_value(signerInfo, i);
                PKCS7_ISSUER_AND_SERIAL* issuerAndSerial = signerInfoValue->issuer_and_serial;
                X509* signer = X509_find_by_issuer_and_serial(certificates, issuerAndSerial->issuer, issuerAndSerial->serial);

                if (!signer)
                {
                    result.addCertificateMissingError();
                    break;
                }

                if (!X509_STORE_CTX_init(context, store, signer, nullptr))
                {
                    result.addCertificateGenericError();
                    break;
                }

                if (!X509_STORE_CTX_set_purpose(context, X509_PURPOSE_SMIME_SIGN))
                {
                    result.addCertificateGenericError();
                    break;
                }

                int verificationResult = X509_verify_cert(context);
                if (verificationResult <= 0)
                {
                    int error = X509_STORE_CTX_get_error(context);
                    switch (error)
                    {
                        case X509_V_OK:
                            // Strange, this should not occur... when X509_verify_cert fails
                            break;

                        case X509_V_ERR_CERT_HAS_EXPIRED:
                            result.addCertificateExpiredError();
                            break;

                        case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
                            result.addCertificateSelfSignedError();
                            break;

                        case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
                            result.addCertificateSelfSignedInChainError();
                            break;

                        case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
                        case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
                            result.addCertificateTrustedNotFoundError();
                            break;

                        case X509_V_ERR_CERT_REVOKED:
                            result.addCertificateRevokedError();
                            break;

                        default:
                            result.addCertificateOtherError(error);
                            break;
                    }
                }
                X509_STORE_CTX_cleanup(context);
            }
        }
        else
        {
            result.addNoSignaturesError();
        }

        X509_STORE_CTX_free(context);
        X509_STORE_free(store);

        PKCS7_free(pkcs7);
    }
    else
    {
        result.addInvalidCertificateError();
    }
}

void PDFPublicKeySignatureHandler::verifySignature(PDFSignatureVerificationResult& result) const
{
    PDFOpenSSLGlobalLock lock;
}

PDFSignatureVerificationResult PDFSignatureHandler_adbe_pkcs7_detached::verify() const
{
    PDFSignatureVerificationResult result;
    initializeResult(result);
    verifyCertificate(result);
    verifySignature(result);
    return result;
}

}   // namespace pdf
