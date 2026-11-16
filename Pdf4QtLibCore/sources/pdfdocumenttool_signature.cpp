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

#include "pdfdocumenttool_signature.h"
#include "pdfdocmdppermissions.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentsigner.h"
#include "pdfdocumenttool_encryption.h"
#include "pdfencoding.h"
#include "pdfform.h"
#include "pdfobjectutils.h"
#include "pdfoperationcontrol.h"
#include "pdfpagedrawing.h"
#include "pdfsignaturehandler.h"
#include "pdftimestampclient.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>

#include <set>

namespace pdf
{

/// Helpers of the run of Sign with Certificate and Add Timestamp
class PDFDocumentToolSignatureHelper
{
public:
    PDFDocumentToolSignatureHelper() = delete;

    using Tool = PDFDocumentToolSignature;
    using Permission = PDFSecurityHandler::Permission;

    /// The flags of /SV /Ff (ISO 32000-2, Table 234): a constraint is required, when its bit is set
    enum SeedValueFlag : PDFInteger
    {
        SeedFilter = 1 << 0,
        SeedSubFilter = 1 << 1,
        SeedVersion = 1 << 2,
        SeedReasons = 1 << 3,
        SeedLegalAttestation = 1 << 4,
        SeedAddRevInfo = 1 << 5,
        SeedDigestMethod = 1 << 6,
        SeedLockDocument = 1 << 7,
        SeedAppearanceFilter = 1 << 8
    };

    /// The newest seed value dictionary the tool reads (Table 234 /V: 3 = PDF 2.0)
    static constexpr PDFReal MAXIMUM_SEED_VALUE_VERSION = 3.0;

    /// The filter of every signature of the tool (Table 255)
    static constexpr const char* SIGNATURE_FILTER = "Adobe.PPKLite";

    /// Releases the state of OpenSSL of the thread of the run, whatever way the run ends (B137)
    class ThreadResourcesGuard
    {
    public:
        ThreadResourcesGuard() = default;
        ~ThreadResourcesGuard() { PDFSignatureHandler::releaseThreadResources(); }

        ThreadResourcesGuard(const ThreadResourcesGuard&) = delete;
        ThreadResourcesGuard& operator=(const ThreadResourcesGuard&) = delete;
    };

    /// Clears the password buffers of the run, whatever way it ends (S6.1, B113)
    class SecretGuard
    {
    public:
        SecretGuard(QString* text, QByteArray* data) : m_text(text), m_data(data) { }
        ~SecretGuard()
        {
            PDFSecureMemory::clear(*m_text);
            PDFSecureMemory::clear(*m_data);
        }

        SecretGuard(const SecretGuard&) = delete;
        SecretGuard& operator=(const SecretGuard&) = delete;

    private:
        QString* m_text;
        QByteArray* m_data;
    };

    /// A copy of the secret of the parameters in a buffer of its own (PDFSecureMemory::clear never
    /// writes a buffer shared with the copy of the caller)
    static QString copySecret(const QVariant& value)
    {
        const QString text = value.toString();
        return QString(text.constData(), text.size());
    }

    static QString getString(const QVariantMap& parameters, const char* name)
    {
        return parameters.value(QString::fromLatin1(name)).toString();
    }

    static bool isSignMode(const QVariantMap& parameters, Tool::Mode mode)
    {
        Q_UNUSED(parameters);
        return mode == Tool::Mode::Sign;
    }

    static bool isTimestamped(const QVariantMap& parameters, Tool::Mode mode)
    {
        return mode == Tool::Mode::Timestamp || getString(parameters, Tool::PARAMETER_TYPE) == QLatin1String(Tool::TYPE_SIGNATURE_TIMESTAMP);
    }

    static bool isVisible(const QVariantMap& parameters, Tool::Mode mode)
    {
        return mode == Tool::Mode::Sign && getString(parameters, Tool::PARAMETER_APPEARANCE) == QLatin1String(Tool::APPEARANCE_VISIBLE);
    }

    static PDFPageDrawing getDrawing(const QVariantMap& parameters)
    {
        const QVariant value = parameters.value(QString::fromLatin1(Tool::PARAMETER_DRAWING));
        return value.canConvert<PDFPageDrawing>() ? value.value<PDFPageDrawing>() : PDFPageDrawing();
    }

    /// The names of a list of names or text strings of a dictionary of the seed value
    static QStringList readNames(const PDFDocument* document, const PDFObject& object)
    {
        QStringList names;
        const PDFObject& value = document->getObject(object);
        if (value.isArray())
        {
            for (const PDFObject& item : *value.getArray())
            {
                const PDFObject& itemValue = document->getObject(item);
                if (itemValue.isName())
                {
                    names << QString::fromLatin1(itemValue.getString());
                }
                else if (itemValue.isString())
                {
                    names << PDFEncoding::convertTextString(itemValue.getString());
                }
            }
        }
        return names;
    }

    /// Reads /Lock and /SV of an empty field (ISO 32000-2, 12.7.5.5, Table 233, Table 234, R6-25)
    static void readSeedValues(const PDFDocument* document, const PDFDictionary* fieldDictionary, Tool::EmptySignatureField* field)
    {
        if (fieldDictionary->hasKey("Lock") && !document->getObject(fieldDictionary->get("Lock")).isNull())
        {
            field->unusableReason = Tool::tr("The field locks other fields when it is signed (FieldMDP), which this tool does not write.");
            return;
        }

        const PDFDictionary* seedValue = document->getDictionaryFromObject(fieldDictionary->get("SV"));
        if (!seedValue)
        {
            return;
        }

        PDFDocumentDataLoaderDecorator loader(document);
        const PDFInteger flags = loader.readIntegerFromDictionary(seedValue, "Ff", 0);

        // A certification signature (Table 234 /MDP /P 1 - 3), which the tool does not write (Q6.4)
        if (const PDFDictionary* mdp = document->getDictionaryFromObject(seedValue->get("MDP")))
        {
            if (loader.readIntegerFromDictionary(mdp, "P", 0) > 0)
            {
                field->unusableReason = Tool::tr("The field requires a certification signature, which this tool does not create.");
                return;
            }
        }

        const QStringList filters = readNames(document, seedValue->get("Filter"));
        const PDFObject& filterObject = document->getObject(seedValue->get("Filter"));
        if ((flags & SeedFilter) && filterObject.isName() && filterObject.getString() != SIGNATURE_FILTER)
        {
            field->unusableReason = Tool::tr("The field requires another signature handler.");
            return;
        }

        if (flags & SeedSubFilter)
        {
            const QStringList subFilters = readNames(document, seedValue->get("SubFilter"));
            for (const PDFSignatureFormat format : { PDFSignatureFormat::PAdES, PDFSignatureFormat::Pkcs7 })
            {
                const QString subFilter = QString::fromLatin1(PDFSignatureFactory::getSubFilter(format));
                if (subFilters.contains(subFilter))
                {
                    field->subFilters << subFilter;
                }
            }
            if (field->subFilters.isEmpty())
            {
                field->unusableReason = Tool::tr("The field requires a format of the signature, which this tool does not create.");
                return;
            }
        }

        if ((flags & SeedVersion) && loader.readNumberFromDictionary(seedValue, "V", 1.0) > MAXIMUM_SEED_VALUE_VERSION)
        {
            field->unusableReason = Tool::tr("The field requires a newer version of the signing.");
            return;
        }

        if (flags & SeedReasons)
        {
            field->reasons = readNames(document, seedValue->get("Reasons"));
        }

        if ((flags & SeedAddRevInfo) && loader.readBooleanFromDictionary(seedValue, "AddRevInfo", false))
        {
            field->unusableReason = Tool::tr("The field requires the revocation information of the certificate, which this tool does not add.");
            return;
        }

        if (flags & SeedDigestMethod)
        {
            const QStringList digestMethods = readNames(document, seedValue->get("DigestMethod"));
            if (!digestMethods.isEmpty() && !digestMethods.contains(QStringLiteral("SHA256")))
            {
                field->unusableReason = Tool::tr("The field requires a digest algorithm other than SHA-256.");
                return;
            }
        }

        const PDFObject& lockDocument = document->getObject(seedValue->get("LockDocument"));
        if ((flags & SeedLockDocument) && lockDocument.isName() && lockDocument.getString() == "true")
        {
            field->unusableReason = Tool::tr("The field requires to lock the document when it is signed, which this tool does not do.");
            return;
        }

        // The constraints of the certificate (Table 235) are not evaluated: a required one cannot be met
        if (const PDFDictionary* certificate = document->getDictionaryFromObject(seedValue->get("Cert")))
        {
            if (loader.readIntegerFromDictionary(certificate, "Ff", 0) != 0)
            {
                field->unusableReason = Tool::tr("The field restricts the certificate of the signer, which this tool does not check.");
                return;
            }
        }

        if (const PDFDictionary* timestamp = document->getDictionaryFromObject(seedValue->get("TimeStamp")))
        {
            field->isTimestampRequired = loader.readIntegerFromDictionary(timestamp, "Ff", 0) & 1;
        }
    }

    /// The certificate of the run: the entry of the context of the fingerprint; \p info gets its
    /// certificate (a .pfx entry, whose certificate is not known yet, is read by the password)
    static const PDFCertificateEntry* findCertificate(const PDFDocumentToolContext& context, const QString& fingerprint, const QString& password, PDFCertificateInfo* info)
    {
        for (const PDFCertificateEntry& entry : context.userCertificates)
        {
            std::optional<PDFCertificateInfo> certificate;
            if (!entry.info.getCertificateData().isEmpty())
            {
                certificate = entry.info;
            }
            else if (!entry.pkcs12.isEmpty())
            {
                certificate = PDFCertificateManager::readCertificate(entry.pkcs12, password);
            }

            if (certificate && PDFDocumentToolEncryption::getCertificateFingerprint(certificate->getCertificateData()) == fingerprint)
            {
                *info = *certificate;
                return &entry;
            }
        }
        return nullptr;
    }

    /// The common name of a certificate (DER), "Unknown signer" without one
    static QString getCommonName(const QByteArray& certificateData)
    {
        const std::optional<PDFCertificateInfo> info = PDFCertificateInfo::getCertificateInfo(certificateData);
        const QString name = info ? info->getName(PDFCertificateInfo::CommonName) : QString();
        return name.isEmpty() ? Tool::tr("Unknown signer") : name;
    }

    /// A name of a new field, which the form does not have ("Signature1", "Timestamp2", ...)
    static QString createFieldName(const PDFDocument* document, const QString& prefix)
    {
        std::set<QString> names;
        const PDFForm form = PDFForm::parse(document, document->getCatalog()->getFormObject());
        form.apply([&names](const PDFFormField* field) { names.insert(field->getName(PDFFormField::NameType::FullyQualified)); });
        for (int index = 1; ; ++index)
        {
            const QString name = prefix + QString::number(index);
            if (!names.count(name))
            {
                return name;
            }
        }
    }

    /// Sets the bits SignaturesExist and AppendOnly of /SigFlags (Table 225) - the field of the
    /// signature exists already (owner answer Q6.3)
    static void setSignatureFlags(PDFDocumentBuilder& builder)
    {
        const PDFDictionary* catalog = builder.getDictionaryFromObject(builder.getObjectByReference(builder.getCatalogReference()));
        const PDFObject acroFormObject = catalog ? catalog->get("AcroForm") : PDFObject();
        const PDFDictionary* form = builder.getDictionaryFromObject(acroFormObject);
        if (!form)
        {
            return;
        }

        PDFDictionaryBuilder updatedForm(*form);
        const PDFObject flags = builder.getObject(form->get("SigFlags"));
        updatedForm.setEntry(PDFInplaceOrMemoryString("SigFlags"), PDFObject::createInteger((flags.isInt() ? flags.getInteger() : 0) | 3));
        if (acroFormObject.isReference())
        {
            builder.setObject(acroFormObject.getReference(), PDFObject::createDictionary(std::move(updatedForm)));
        }
        else
        {
            builder.setCatalogAcroForm(builder.addObject(PDFObject::createDictionary(std::move(updatedForm))));
        }
    }

    /// ISO 14289-1, 7.18.1 and ISO 32000-2, 14.7.4.4 (owner answer Q6.18): the widget of a visible
    /// signature of a tagged document is the content of a structure element /Form (a reference of
    /// the object /OBJR), its /StructParent is a new key of /ParentTree
    static void addFormStructure(PDFDocumentBuilder& builder, PDFObjectReference widget, PDFObjectReference page)
    {
        const PDFObjectStorage* storage = builder.getStorage();
        const PDFDictionary* catalog = builder.getDictionaryFromObject(builder.getObjectByReference(builder.getCatalogReference()));
        const PDFObject rootObject = catalog ? catalog->get("StructTreeRoot") : PDFObject();
        const PDFDictionary* root = builder.getDictionaryFromObject(rootObject);
        if (!root || !rootObject.isReference())
        {
            return;
        }
        const PDFObjectReference rootReference = rootObject.getReference();

        // The new key: after every key of the tree and /ParentTreeNextKey (14.7.5.4)
        PDFInteger key = 0;
        const PDFObject nextKey = builder.getObject(root->get("ParentTreeNextKey"));
        if (nextKey.isInt())
        {
            key = qMax<PDFInteger>(0, nextKey.getInteger());
        }
        for (const auto& [entryKey, value] : PDFObjectUtils::readNumberTree(storage, root->get("ParentTree")))
        {
            Q_UNUSED(value);
            key = qMax(key, entryKey + 1);
        }

        // The parent: the only top element of the tree (/Document), else the root itself
        PDFObjectReference parentReference = rootReference;
        const PDFObject& kids = root->get("K");
        if (kids.isReference())
        {
            const PDFDictionary* kid = builder.getDictionaryFromObject(builder.getObject(kids));
            const PDFObject& type = kid ? kid->get("Type") : PDFObject();
            if (kid && (type.isNull() || (type.isName() && type.getString() == "StructElem")) && kid->get("S").isName())
            {
                parentReference = kids.getReference();
            }
        }

        PDFObjectFactory objectReference;
        objectReference.beginDictionary();
        objectReference.beginDictionaryItem("Type");
        objectReference << WrapName("OBJR");
        objectReference.endDictionaryItem();
        objectReference.beginDictionaryItem("Obj");
        objectReference << widget;
        objectReference.endDictionaryItem();
        objectReference.beginDictionaryItem("Pg");
        objectReference << page;
        objectReference.endDictionaryItem();
        objectReference.endDictionary();

        PDFDictionaryBuilder element;
        element.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("StructElem"));
        element.addEntry(PDFInplaceOrMemoryString("S"), PDFObject::createName("Form"));
        element.addEntry(PDFInplaceOrMemoryString("P"), PDFObject::createReference(parentReference));
        element.addEntry(PDFInplaceOrMemoryString("Pg"), PDFObject::createReference(page));
        element.addEntry(PDFInplaceOrMemoryString("K"), objectReference.takeObject());
        const PDFObjectReference elementReference = builder.addObject(PDFObject::createDictionary(std::move(element)));

        // The element is the last kid of its parent
        const PDFDictionary* parent = builder.getDictionaryFromObject(builder.getObjectByReference(parentReference));
        PDFDictionaryBuilder updatedParent(*parent);
        const PDFObject parentKids = builder.getObject(parent->get("K"));
        PDFArrayBuilder newKids;
        if (parentKids.isArray())
        {
            newKids = PDFArrayBuilder(*parentKids.getArray());
        }
        else if (!parent->get("K").isNull())
        {
            newKids.appendItem(parent->get("K"));
        }
        newKids.appendItem(PDFObject::createReference(elementReference));
        updatedParent.setEntry(PDFInplaceOrMemoryString("K"), PDFObject::createArray(std::move(newKids)));
        builder.setObject(parentReference, PDFObject::createDictionary(std::move(updatedParent)));

        // /ParentTree: the key goes after every key - into the leaf of the root, or a new last kid
        const PDFDictionary* updatedRoot = builder.getDictionaryFromObject(builder.getObjectByReference(rootReference));
        PDFDictionaryBuilder rootBuilder(*updatedRoot);
        const PDFObject parentTreeObject = updatedRoot->get("ParentTree");
        const PDFDictionary* parentTree = builder.getDictionaryFromObject(parentTreeObject);
        const PDFObject newEntry[2] = { PDFObject::createInteger(key), PDFObject::createReference(elementReference) };

        PDFDictionaryBuilder tree = parentTree ? PDFDictionaryBuilder(*parentTree) : PDFDictionaryBuilder();
        const PDFObject treeKids = parentTree ? builder.getObject(parentTree->get("Kids")) : PDFObject();
        if (treeKids.isArray())
        {
            PDFDictionaryBuilder leaf;
            leaf.addEntry(PDFInplaceOrMemoryString("Limits"), PDFObject::createArray(PDFArrayBuilder(std::vector<PDFObject>{ PDFObject::createInteger(key), PDFObject::createInteger(key) })));
            leaf.addEntry(PDFInplaceOrMemoryString("Nums"), PDFObject::createArray(PDFArrayBuilder(std::vector<PDFObject>{ newEntry[0], newEntry[1] })));
            PDFArrayBuilder kidsBuilder(*treeKids.getArray());
            kidsBuilder.appendItem(PDFObject::createReference(builder.addObject(PDFObject::createDictionary(std::move(leaf)))));
            tree.setEntry(PDFInplaceOrMemoryString("Kids"), PDFObject::createArray(std::move(kidsBuilder)));
        }
        else
        {
            const PDFObject nums = parentTree ? builder.getObject(parentTree->get("Nums")) : PDFObject();
            PDFArrayBuilder numsBuilder = nums.isArray() ? PDFArrayBuilder(*nums.getArray()) : PDFArrayBuilder();
            numsBuilder.appendItem(newEntry[0]);
            numsBuilder.appendItem(newEntry[1]);
            tree.setEntry(PDFInplaceOrMemoryString("Nums"), PDFObject::createArray(std::move(numsBuilder)));
        }

        if (parentTreeObject.isReference())
        {
            builder.setObject(parentTreeObject.getReference(), PDFObject::createDictionary(std::move(tree)));
        }
        else
        {
            rootBuilder.setEntry(PDFInplaceOrMemoryString("ParentTree"), PDFObject::createReference(builder.addObject(PDFObject::createDictionary(std::move(tree)))));
        }
        rootBuilder.setEntry(PDFInplaceOrMemoryString("ParentTreeNextKey"), PDFObject::createInteger(key + 1));
        builder.setObject(rootReference, PDFObject::createDictionary(std::move(rootBuilder)));

        PDFObjectFactory widgetUpdate;
        widgetUpdate.beginDictionary();
        widgetUpdate.beginDictionaryItem("StructParent");
        widgetUpdate << key;
        widgetUpdate.endDictionaryItem();
        widgetUpdate.endDictionary();
        builder.mergeTo(widget, widgetUpdate.takeObject());
    }

    /// The id of a status of a failed request of the authority (DATA_TIMESTAMP_ERROR)
    static QString getTimestampErrorId(PDFTimestampClient::Status status)
    {
        switch (status)
        {
            case PDFTimestampClient::Status::InvalidUrl:
                return QStringLiteral("invalid-url");
            case PDFTimestampClient::Status::NoTransport:
                return QStringLiteral("no-transport");
            case PDFTimestampClient::Status::NoNetwork:
                return QStringLiteral("no-network");
            case PDFTimestampClient::Status::Timeout:
                return QStringLiteral("timeout");
            case PDFTimestampClient::Status::HttpError:
                return QStringLiteral("http-error");
            case PDFTimestampClient::Status::Rejected:
                return QStringLiteral("rejected");
            case PDFTimestampClient::Status::InvalidDigest:
            case PDFTimestampClient::Status::InvalidResponse:
                return QStringLiteral("invalid-response");
            case PDFTimestampClient::Status::Granted:
            case PDFTimestampClient::Status::Canceled:
                break;
        }
        return QString();
    }

    /// "Sep 30, 2026 · 14:32:08 UTC" (434:9276) independent of any locale: the text of MCP and PdfTool.
    /// The time is also the data DATA_SIGNING_TIME / DATA_TIMESTAMP_TIME (ISO 8601), by which the new
    /// application formats the fact in the language of its user interface (O17, PDFAppLocale)
    static QString formatTime(const QDateTime& time)
    {
        const QDateTime utc = time.toUTC();
        const QLocale locale = QLocale::c();
        return Tool::tr("%1 · %2 UTC").arg(locale.toString(utc.date(), QStringLiteral("MMM d, yyyy")), locale.toString(utc.time(), QStringLiteral("HH:mm:ss")));
    }

    /// The state of a signature of a file for the fact Earlier signatures (R6-4: by the verifier
    /// with the classification of the later revisions of R6-3, over the written bytes)
    enum class SignatureState
    {
        Valid,      ///< Intact, nothing but signatures, timestamps or validation data after it
        Changed,    ///< Intact, the document was changed after it (the unsaved changes of the tab)
        Invalid     ///< Not intact
    };

    /// The states of the signatures of a file by their fields
    /// \param data Bytes of the file
    /// \param securityHandler The authenticated handler of the document of the file (an encrypted file, R6-1), may be null
    static std::vector<std::pair<PDFObjectReference, SignatureState>> verifySignatures(const QByteArray& data, const PDFSecurityHandler* securityHandler)
    {
        std::vector<std::pair<PDFObjectReference, SignatureState>> states;
        const std::optional<PDFDocument> document = PDFDocumentSigner::readDocument(data, securityHandler, nullptr, nullptr);
        if (!document)
        {
            return states;
        }

        const PDFForm form = PDFForm::parse(&*document, document->getCatalog()->getFormObject());
        PDFSignatureHandler::Parameters parameters;
        parameters.useSystemCertificateStore = false;
        parameters.verifyCertificates = false;
        parameters.securityHandler = document->getStorage().getSecurityHandler();
        for (const PDFSignatureVerificationResult& result : PDFSignatureHandler::verifySignatures(form, data, parameters))
        {
            SignatureState state = SignatureState::Invalid;
            if (result.isSignatureValid() && !result.hasSignatureError())
            {
                state = result.getLaterRevisions() == PDFSignatureVerificationResult::LaterRevisions::Changed ? SignatureState::Changed : SignatureState::Valid;
            }
            states.emplace_back(result.getSignatureFieldReference(), state);
        }
        return states;
    }

    /// The timestamp authority of the new signature of the signed file is trusted (plan 5.2, RS6SEC M3):
    /// the chain of the certificate of the token ends at a certificate of the trusted store of the
    /// context (the trusted certificates of the user) or of the bundled AATL certificates, as the
    /// signature panel decides it (PDFSignatureHandler::verifySignatures, AATLCertificates::Always).
    /// A document timestamp (\p isDocumentTimestamp) is the signature itself: its certificate is valid; a
    /// timestamp of a signature is an unsigned attribute: it is verified (no Warning_Signature_TimestampNotVerified)
    /// \param signedData The bytes of the signed file
    /// \param signatureField The field of the new signature (PDFDocumentSigner::Details)
    /// \param securityHandler The authenticated handler of the document of the file (R6-1), may be null
    /// \param store The trusted certificates of the context, may be null (then only the AATL certificates)
    static bool isAuthorityTrusted(const QByteArray& signedData, PDFObjectReference signatureField, const PDFSecurityHandler* securityHandler,
                                   const PDFCertificateStore* store, bool isDocumentTimestamp)
    {
        const std::optional<PDFDocument> document = PDFDocumentSigner::readDocument(signedData, securityHandler, nullptr, nullptr);
        if (!document)
        {
            return false;
        }

        const PDFForm form = PDFForm::parse(&*document, document->getCatalog()->getFormObject());
        PDFSignatureHandler::Parameters parameters;
        parameters.store = store;
        parameters.useSystemCertificateStore = false;
        parameters.aatlCertificates = PDFSignatureHandler::AATLCertificates::Always;
        parameters.securityHandler = document->getStorage().getSecurityHandler();
        for (const PDFSignatureVerificationResult& result : PDFSignatureHandler::verifySignatures(form, signedData, parameters))
        {
            if (result.getSignatureFieldReference() == signatureField)
            {
                return isDocumentTimestamp ? result.isCertificateValid() && !result.hasCertificateError()
                                           : !result.hasFlag(PDFSignatureVerificationResult::Warning_Signature_TimestampNotVerified);
            }
        }
        return false;
    }
};

PDFDocumentToolSignature::PDFDocumentToolSignature(Mode mode) :
    m_mode(mode)
{

}

QString PDFDocumentToolSignature::getToolId(Mode mode)
{
    switch (mode)
    {
        case Mode::Sign:
            return QString::fromLatin1(TOOL_ID_SIGN);
        case Mode::Timestamp:
            return QString::fromLatin1(TOOL_ID_TIMESTAMP);
    }
    return QString();
}

std::optional<PDFDocumentToolSignature::Mode> PDFDocumentToolSignature::findMode(QStringView toolId)
{
    for (const Mode mode : { Mode::Sign, Mode::Timestamp })
    {
        if (toolId == getToolId(mode))
        {
            return mode;
        }
    }
    return std::nullopt;
}

PDFSignatureFormat PDFDocumentToolSignature::getFormat(const QString& formatId)
{
    return formatId == QLatin1String(FORMAT_PKCS7) ? PDFSignatureFormat::Pkcs7 : PDFSignatureFormat::PAdES;
}

QString PDFDocumentToolSignature::getId() const
{
    return getToolId(m_mode);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolSignature::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Option = PDFDocumentToolParameterOption;

    PDFDocumentToolParameterDefinitions definitions;
    if (m_mode == Mode::Sign)
    {
        definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_CERTIFICATE), QT_TR_NOOP("Certificate"),
                                                       QT_TR_NOOP("SHA-256 fingerprint of the certificate of the signer (64 hexadecimal digits); the user confirms the certificate in PDF4QT"),
                                                       QString(), 64));
        definitions.push_back(Definition::createSecret(tr, QString::fromLatin1(PARAMETER_CERTIFICATE_PASSWORD), QT_TR_NOOP("Password"), QT_TR_NOOP("Password of the certificate file")));
        definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_FORMAT), QT_TR_NOOP("Format"), QT_TR_NOOP("Format of the signature"),
                                                     { Option(QString::fromLatin1(FORMAT_PADES), QT_TR_NOOP("PAdES"), QT_TR_NOOP("The current standard for signatures in PDF, recommended"), QStringLiteral("sig-format-pades")),
                                                       Option(QString::fromLatin1(FORMAT_PKCS7), QT_TR_NOOP("Compatible (PKCS#7)"), QT_TR_NOOP("For older software that cannot read PAdES"), QStringLiteral("sig-format-pkcs7")) },
                                                     QString::fromLatin1(FORMAT_PADES)));
        definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_TYPE), QT_TR_NOOP("Signature"), QT_TR_NOOP("Kind of the signature"),
                                                     { Option(QString::fromLatin1(TYPE_SIGNATURE), QT_TR_NOOP("Signature"), QT_TR_NOOP("Signed with your certificate, time taken from this computer"), QStringLiteral("sign-certificate")),
                                                       Option(QString::fromLatin1(TYPE_SIGNATURE_TIMESTAMP), QT_TR_NOOP("Signature with timestamp"),
                                                              QT_TR_NOOP("A timestamp authority also confirms the signing time"), QStringLiteral("timestamp")) },
                                                     QString::fromLatin1(TYPE_SIGNATURE)));
        definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_APPEARANCE), QT_TR_NOOP("Appearance"), QT_TR_NOOP("Whether the signature is visible on a page"),
                                                     { Option(QString::fromLatin1(APPEARANCE_VISIBLE), QT_TR_NOOP("Visible"), QT_TR_NOOP("The drawn appearance is placed on the page"), QStringLiteral("eye")),
                                                       Option(QString::fromLatin1(APPEARANCE_INVISIBLE), QT_TR_NOOP("Invisible"), QT_TR_NOOP("Recorded in the document only, nothing on the page"), QStringLiteral("eye-off")) },
                                                     QString::fromLatin1(APPEARANCE_VISIBLE)));
        definitions.push_back(Definition::createValue(tr, QString::fromLatin1(PARAMETER_DRAWING), QT_TR_NOOP("Drawing"), QT_TR_NOOP("The visible appearance of the signature"),
                                                      QVariant::fromValue(PDFPageDrawing())).setSensitive(true));
        definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_FIELD), QT_TR_NOOP("Field"),
                                                       QT_TR_NOOP("Full name of an empty signature field of the document; empty = a new field"),
                                                       QString(), MAXIMUM_FIELD_NAME_LENGTH).setStored(false));
        definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_REASON), QT_TR_NOOP("Reason"), QT_TR_NOOP("Reason of the signing"), QString(), MAXIMUM_TEXT_LENGTH));
        definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_CONTACT_INFO), QT_TR_NOOP("Contact info"), QT_TR_NOOP("How to contact the signer"), QString(), MAXIMUM_TEXT_LENGTH));
    }
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_TSA_URL), QT_TR_NOOP("Timestamp authority"),
                                                   QT_TR_NOOP("Address of the RFC 3161 timestamp authority (http or https)"),
                                                   PDFTimestampClient::getPresetUrls().value(0), PDFTimestampClient::MAXIMUM_URL_LENGTH));
    return definitions;
}

std::vector<PDFDocumentToolSignature::EmptySignatureField> PDFDocumentToolSignature::getEmptySignatureFields(const PDFDocument* document)
{
    std::vector<EmptySignatureField> fields;
    if (!document || !document->getCatalog())
    {
        return fields;
    }

    // PDFForm::parse walks the field tree once (a visited set, a depth limit)
    const PDFForm form = PDFForm::parse(document, document->getCatalog()->getFormObject());
    if (!form.isAcroForm())
    {
        return fields;
    }

    // A field shared by two parents of a damaged tree is one field: listed once, by its first path
    std::set<PDFObjectReference> visitedFields;
    form.apply([document, &fields, &visitedFields](const PDFFormField* field)
    {
        const PDFFormFieldSignature* signatureField = dynamic_cast<const PDFFormFieldSignature*>(field);
        if (!signatureField || !signatureField->getSignature().getContents().isEmpty() || !visitedFields.insert(field->getSelfReference()).second)
        {
            return;
        }

        const PDFDictionary* fieldDictionary = document->getDictionaryFromObject(document->getObjectByReference(field->getSelfReference()));
        if (!fieldDictionary || !document->getObject(fieldDictionary->get("V")).isNull())
        {
            return;
        }

        EmptySignatureField emptyField;
        emptyField.name = field->getName(PDFFormField::NameType::FullyQualified);
        emptyField.field = field->getSelfReference();
        if (!field->getWidgets().empty())
        {
            const PDFFormWidget& widget = field->getWidgets().front();
            emptyField.widget = widget.getWidget();
            const size_t pageIndex = document->getCatalog()->getPageIndexFromPageReference(widget.getPage());
            emptyField.pageIndex = pageIndex != PDFCatalog::INVALID_PAGE_INDEX ? PDFInteger(pageIndex) : -1;
            const PDFDictionary* widgetDictionary = document->getDictionaryFromObject(document->getObjectByReference(widget.getWidget()));
            if (widgetDictionary)
            {
                PDFDocumentDataLoaderDecorator loader(document);
                emptyField.rectangle = loader.readRectangle(widgetDictionary->get("Rect"), QRectF()).normalized();
            }
        }

        PDFDocumentToolSignatureHelper::readSeedValues(document, fieldDictionary, &emptyField);
        fields.push_back(std::move(emptyField));
    });
    return fields;
}

std::vector<PDFSecurityHandler::Permission> PDFDocumentToolSignature::getRequiredPermissions(const QVariantMap& parameters) const
{
    // ISO 32000-2, Table 22 (R6-8): a signature of an existing empty field needs bit 9 or bit 6
    // (getAnyOfPermissions) and nothing else; a new field bits 4 and 6 - the row of the catalog
    if (m_mode == Mode::Sign && !PDFDocumentToolSignatureHelper::getString(parameters, PARAMETER_FIELD).isEmpty())
    {
        return { };
    }
    return PDFDocumentTool::getRequiredPermissions(parameters);
}

std::vector<PDFSecurityHandler::Permission> PDFDocumentToolSignature::getAnyOfPermissions(const QVariantMap& parameters) const
{
    // ISO 32000-2, Table 22 (RS6APP M10): bit 9 "fill in existing interactive form fields (including
    // signature fields), even if bit 6 is clear" - and bit 6 "fill in interactive form fields": either
    // one allows the signature of an existing empty field
    if (m_mode == Mode::Sign && !PDFDocumentToolSignatureHelper::getString(parameters, PARAMETER_FIELD).isEmpty())
    {
        return { PDFSecurityHandler::Permission::ModifyFormFields, PDFSecurityHandler::Permission::ModifyInteractiveItems };
    }
    return PDFDocumentTool::getAnyOfPermissions(parameters);
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolSignature::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolSignatureHelper;

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (!errors.empty())
    {
        return errors;
    }

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();
    const PDFDocMDPPermissions::Level level = PDFDocMDPPermissions::getLevel(document);

    if (Helper::isTimestamped(parameters, m_mode))
    {
        QString urlError;
        if (!PDFTimestampClient::validateUrl(Helper::getString(parameters, PARAMETER_TSA_URL), &urlError))
        {
            errors.push_back({ QString::fromLatin1(PARAMETER_TSA_URL), urlError });
        }
    }

    if (m_mode == Mode::Timestamp)
    {
        // R6-4 (12.8.2.2.1): a timestamp of a document certified with no changes allowed only in a
        // revision of nothing else
        if (level == PDFDocMDPPermissions::Level::NoChanges && input.isModified && !input.originalData.isEmpty())
        {
            errors.push_back({ QString(), tr("This document is certified with no changes allowed. Undo the unsaved changes to add a timestamp.") });
        }
        return errors;
    }

    // B135 (Table 257 /P 1): no signature of a document certified with no changes allowed
    if (level == PDFDocMDPPermissions::Level::NoChanges)
    {
        errors.push_back({ QString(), tr("This document is certified with no changes allowed. It cannot be signed.") });
        return errors;
    }

    // The certificate and its password (434:7143)
    const QString certificateParameter = QString::fromLatin1(PARAMETER_CERTIFICATE);
    const QString passwordParameter = QString::fromLatin1(PARAMETER_CERTIFICATE_PASSWORD);
    const QString fingerprint = Helper::getString(parameters, PARAMETER_CERTIFICATE).toLower();
    QString password = Helper::copySecret(parameters.value(passwordParameter));
    QByteArray passwordData = password.toUtf8();
    const Helper::SecretGuard secretGuard(&password, &passwordData);
    if (fingerprint.isEmpty())
    {
        errors.push_back({ certificateParameter, tr("Choose a certificate.") });
    }
    else
    {
        PDFCertificateInfo info;
        const PDFCertificateEntry* entry = Helper::findCertificate(context, fingerprint, password, &info);
        if (!entry)
        {
            errors.push_back({ certificateParameter, tr("The chosen certificate is not available.") });
        }
        else if (!entry->pkcs12.isEmpty() && !PDFSignatureFactory::isPasswordValid(entry->pkcs12, passwordData))
        {
            errors.push_back({ passwordParameter, tr("Password to open certificate is invalid.") });
        }
        else
        {
            const QDateTime now = QDateTime::currentDateTimeUtc();
            const QDateTime notBefore = info.getNotValidBefore();
            const QDateTime notAfter = info.getNotValidAfter();
            if ((notAfter.isValid() && notAfter < now) || (notBefore.isValid() && notBefore > now))
            {
                errors.push_back({ certificateParameter, tr("The certificate is not valid now, it cannot be used.") });
            }
            else if (info.getKeyUsage().toInt() != 0 && !info.isUsableForDigitalSignature())
            {
                errors.push_back({ certificateParameter, tr("The certificate cannot be used for signing (its key usage has no digital signature).") });
            }
        }
    }

    // The appearance (434:4688 "No appearance drawn")
    const PDFPageDrawing drawing = Helper::getDrawing(parameters);
    if (Helper::isVisible(parameters, m_mode))
    {
        QString drawingError;
        if (drawing.isEmpty())
        {
            errors.push_back({ QString::fromLatin1(PARAMETER_DRAWING), tr("Draw the appearance of the signature, or choose Invisible.") });
        }
        else if (!drawing.isValid(document, &drawingError))
        {
            errors.push_back({ QString::fromLatin1(PARAMETER_DRAWING), drawingError });
        }
    }

    // The field of Q6.3 and its seed values (R6-25, Table 234)
    const QString fieldName = Helper::getString(parameters, PARAMETER_FIELD);
    if (!fieldName.isEmpty())
    {
        const QString fieldParameter = QString::fromLatin1(PARAMETER_FIELD);
        const std::vector<EmptySignatureField> fields = getEmptySignatureFields(document);
        auto it = std::find_if(fields.cbegin(), fields.cend(), [&fieldName](const EmptySignatureField& field) { return field.name == fieldName; });
        if (it == fields.cend())
        {
            errors.push_back({ fieldParameter, tr("The document has no empty signature field of this name.") });
        }
        else if (!it->unusableReason.isEmpty())
        {
            errors.push_back({ fieldParameter, it->unusableReason });
        }
        else
        {
            const QString subFilter = QString::fromLatin1(PDFSignatureFactory::getSubFilter(getFormat(Helper::getString(parameters, PARAMETER_FORMAT))));
            if (!it->subFilters.isEmpty() && !it->subFilters.contains(subFilter))
            {
                errors.push_back({ QString::fromLatin1(PARAMETER_FORMAT), tr("The field requires another format of the signature.") });
            }
            if (it->isTimestampRequired && !Helper::isTimestamped(parameters, m_mode))
            {
                errors.push_back({ QString::fromLatin1(PARAMETER_TYPE), tr("The field requires a signature with a timestamp.") });
            }

            // Table 234 /Reasons: a single empty text = no reason may be given
            const QString reason = Helper::getString(parameters, PARAMETER_REASON);
            const bool isNoReason = it->reasons.size() == 1 && it->reasons.front().isEmpty();
            if (!it->reasons.isEmpty() && (isNoReason ? !reason.isEmpty() : !it->reasons.contains(reason)))
            {
                errors.push_back({ QString::fromLatin1(PARAMETER_REASON), isNoReason ? tr("The field allows no reason.") : tr("The field allows only these reasons: %1").arg(it->reasons.join(QStringLiteral(", "))) });
            }
        }
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolSignature::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolSignatureHelper;
    const Helper::ThreadResourcesGuard threadResourcesGuard;

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();
    const bool isTimestamped = Helper::isTimestamped(parameters, m_mode);
    const bool isVisible = Helper::isVisible(parameters, m_mode);
    const QString tsaUrl = Helper::getString(parameters, PARAMETER_TSA_URL);

    // 4.4, R6-12: a file rewritten whole in the tab or restored of a damaged file is no base of an
    // update; it is written whole only when the file has no signature (434:8913). R6-1: a file with
    // an open password is read by the authenticated security handler of the document of the tab.
    const PDFSecurityHandler* securityHandler = document->getStorage().getSecurityHandler();
    QByteArray originalData = input.originalData;
    if (input.isWholeFileRewritten || input.isRestored)
    {
        if (PDFDocumentSigner::hasExistingSignatures(originalData, securityHandler))
        {
            return PDFDocumentToolResult::createFailed(PDFDocumentSigner::getResultMessage(PDFDocumentSigner::Result::ExistingSignaturesNotPreserved));
        }
        originalData.clear();
    }

    // S6.6: the authority only by the client of the context (never in a test: a local one)
    if (isTimestamped && !context.timestampClient)
    {
        return PDFDocumentToolResult::createFailed(tr("No timestamp authority can be contacted."));
    }

    if (PDFOperationControl::isOperationCanceled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    PDFDocumentSigner::Parameters signerParameters;
    signerParameters.document = document;
    signerParameters.originalDocumentData = originalData;
    signerParameters.filter = Helper::SIGNATURE_FILTER;
    signerParameters.signingTime = QDateTime::currentDateTimeUtc();
    signerParameters.progress = progress;
    signerParameters.requiredPermissions = getRequiredPermissions(parameters);
    signerParameters.anyOfPermissions = getAnyOfPermissions(parameters);

    // The key: one unlock per run, RAII (S6.1, B113); the password buffers are cleared at once
    PDFSigningKey key;
    PDFSignatureFormat format = getFormat(Helper::getString(parameters, PARAMETER_FORMAT));
    QString signerName;
    PDFTimestampClient::Result timestampResult;
    QString signingError;
    PDFSigningKey::Timestamp timestamp;
    timestamp.client = context.timestampClient.get();
    timestamp.url = tsaUrl;

    if (m_mode == Mode::Sign)
    {
        QString password = Helper::copySecret(parameters.value(QString::fromLatin1(PARAMETER_CERTIFICATE_PASSWORD)));
        QByteArray passwordData = password.toUtf8();
        const Helper::SecretGuard secretGuard(&password, &passwordData);

        PDFCertificateInfo info;
        const PDFCertificateEntry* entry = Helper::findCertificate(context, Helper::getString(parameters, PARAMETER_CERTIFICATE).toLower(), password, &info);
        if (!entry)
        {
            return PDFDocumentToolResult::createFailed(tr("The chosen certificate is not available."));
        }

        QString unlockError;
        key = PDFSignatureFactory::unlock(*entry, passwordData, context.parentWindow, &unlockError);
        if (!key.isValid())
        {
            return PDFDocumentToolResult::createFailed(unlockError.isEmpty() ? tr("The certificate cannot be used for signing.") : unlockError);
        }

        signerName = Helper::getCommonName(key.getCertificateData());
        signerParameters.subfilter = PDFSignatureFactory::getSubFilter(format);
        signerParameters.signatureDictionaryType = "Sig";
        signerParameters.estimatedSignatureSize = key.estimateSignatureSize(format, isTimestamped);
        signerParameters.signFunction = [&](const QByteArray& data, QByteArray& signature)
        {
            const PDFSigningKey::Signature result = key.sign(data, format, isTimestamped ? &timestamp : nullptr, control);
            timestampResult = result.timestamp;
            signingError = result.errorMessage;
            signature = result.data;
            return result.isSigned();
        };
    }
    else
    {
        signerParameters.subfilter = "ETSI.RFC3161";
        signerParameters.signatureDictionaryType = "DocTimeStamp";
        signerParameters.estimatedSignatureSize = TIMESTAMP_TOKEN_RESERVE;
        signerParameters.signFunction = [&](const QByteArray& data, QByteArray& token)
        {
            timestampResult = context.timestampClient->request(tsaUrl, QCryptographicHash::hash(data, QCryptographicHash::Sha256), control);
            signingError = timestampResult.errorMessage;
            token = timestampResult.token;
            return timestampResult.isGranted() && !token.isEmpty();
        };
    }

    // The field of the signature: a new one (visible on the first page of the drawing, else
    // invisible on the first page), or the empty field of the parameter field (Q6.3)
    const PDFPageDrawing drawing = Helper::getDrawing(parameters);
    const QString fieldName = m_mode == Mode::Sign ? Helper::getString(parameters, PARAMETER_FIELD) : QString();
    std::optional<EmptySignatureField> emptyField;
    if (!fieldName.isEmpty())
    {
        for (const EmptySignatureField& field : getEmptySignatureFields(document))
        {
            if (field.name == fieldName && field.unusableReason.isEmpty())
            {
                emptyField = field;
            }
        }
        if (!emptyField)
        {
            return PDFDocumentToolResult::createFailed(tr("The document has no empty signature field of this name."));
        }
    }

    const PDFInteger drawingPage = drawing.getFirstPageIndex();
    const bool isOpaqueRequired = getConformanceDeclarations(document).contains(QStringLiteral("PDF/A"));
    const QString reason = m_mode == Mode::Sign ? Helper::getString(parameters, PARAMETER_REASON) : QString();
    const QString contactInfo = m_mode == Mode::Sign ? Helper::getString(parameters, PARAMETER_CONTACT_INFO) : QString();
    const QString newFieldName = Helper::createFieldName(document, m_mode == Mode::Sign ? QStringLiteral("Signature") : QStringLiteral("Timestamp"));
    PDFInteger visiblePage = -1;

    signerParameters.createSignatureFieldFunction = [&](PDFDocumentBuilder& builder, PDFObjectReference signatureDictionary) -> PDFObjectReference
    {
        if (!reason.isEmpty())
        {
            builder.setSignatureReason(signatureDictionary, reason);
        }
        if (!contactInfo.isEmpty())
        {
            builder.setSignatureContactInfo(signatureDictionary, contactInfo);
        }

        // The page objects (the leaves of the page tree, ISO 32000-2, 7.7.3.2 - never an intermediate
        // /Pages node, nor a flattened tree: the revision of a signature changes no page tree); the
        // signed document has the object numbers of the document of the tab (an incremental update)
        std::vector<PDFObjectReference> pages;
        for (size_t pageIndex = 0; pageIndex < document->getCatalog()->getPageCount(); ++pageIndex)
        {
            pages.push_back(document->getCatalog()->getPage(pageIndex)->getPageReference());
        }
        PDFObjectReference field;
        PDFObjectReference widget;
        PDFObjectReference page;
        visiblePage = -1;
        if (emptyField)
        {
            // Q6.3: the value of the field, the appearance fitted to /Rect of its widget
            field = emptyField->field;
            widget = emptyField->widget;
            PDFObjectFactory value;
            value.beginDictionary();
            value.beginDictionaryItem("V");
            value << signatureDictionary;
            value.endDictionaryItem();
            value.endDictionary();
            builder.mergeTo(field, value.takeObject());

            if (isVisible && widget.isValid() && !emptyField->rectangle.isEmpty())
            {
                const PDFObjectReference appearance = drawing.createFormXObject(&builder, drawingPage, isOpaqueRequired);
                if (!appearance.isValid())
                {
                    return PDFObjectReference();
                }

                PDFObjectFactory appearanceFactory;
                appearanceFactory.beginDictionary();
                appearanceFactory.beginDictionaryItem("AP");
                appearanceFactory.beginDictionary();
                appearanceFactory.beginDictionaryItem("N");
                appearanceFactory << appearance;
                appearanceFactory.endDictionaryItem();
                appearanceFactory.endDictionary();
                appearanceFactory.endDictionaryItem();
                appearanceFactory.endDictionary();
                builder.mergeTo(widget, appearanceFactory.takeObject());
                visiblePage = emptyField->pageIndex;
            }
            Helper::setSignatureFlags(builder);
        }
        else
        {
            PDFObjectReference appearance;
            QRectF rectangle;
            page = pages.empty() ? PDFObjectReference() : pages.front();
            if (isVisible)
            {
                appearance = drawing.createFormXObject(&builder, drawingPage, isOpaqueRequired);
                if (!appearance.isValid())
                {
                    return PDFObjectReference();
                }
                rectangle = drawing.getBoundingBox(drawingPage);
                if (drawingPage < 0 || size_t(drawingPage) >= pages.size())
                {
                    return PDFObjectReference();
                }
                page = pages[size_t(drawingPage)];
                visiblePage = drawingPage;
            }
            field = builder.createSignatureField(newFieldName, signatureDictionary, page, appearance, rectangle);
            widget = field;
        }

        // K6.22, ISO 32000-2 Table 226, ISO 14289-1 7.18.1: the description for the assistive
        // technology - never in the revision of a document timestamp (R6-4)
        const PDFDictionary* fieldDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(field));
        if (m_mode == Mode::Sign && fieldDictionary && !fieldDictionary->hasKey("TU"))
        {
            PDFObjectFactory description;
            description.beginDictionary();
            description.beginDictionaryItem("TU");
            description << tr("Digital signature of %1").arg(signerName);
            description.endDictionaryItem();
            description.endDictionary();
            builder.mergeTo(field, description.takeObject());
        }

        // Q6.18: the widget of a new visible signature of a tagged document is a /Form element
        if (!emptyField && visiblePage >= 0)
        {
            Helper::addFormStructure(builder, widget, page);
        }
        return field;
    };

    QByteArray signedData;
    PDFDocumentSigner::Details details;
    const PDFDocumentSigner::Result signerResult = PDFDocumentSigner::sign(signerParameters, signedData, &details);
    if (signerResult != PDFDocumentSigner::Result::OK)
    {
        if (PDFOperationControl::isOperationCanceled(control) || timestampResult.status == PDFTimestampClient::Status::Canceled)
        {
            return PDFDocumentToolResult::createStopped();
        }

        PDFDocumentToolResult result = PDFDocumentToolResult::createFailed(signingError.isEmpty() ? PDFDocumentSigner::getResultMessage(signerResult) : signingError);
        const QString timestampError = isTimestamped && signerResult == PDFDocumentSigner::Result::SigningFailed ? Helper::getTimestampErrorId(timestampResult.status) : QString();
        if (!timestampError.isEmpty())
        {
            // 434:8518, 435:8117: the authority failed - the panel offers Try Again (Sign Without Timestamp)
            result.data.insert(QString::fromLatin1(DATA_TIMESTAMP_ERROR), timestampError);
            if (timestampResult.httpStatus > 0)
            {
                result.data.insert(QString::fromLatin1(DATA_HTTP_STATUS), timestampResult.httpStatus);
            }
        }
        return result;
    }

    // The earlier signatures stay as they were (4.1 step 5): a signature, which was intact in the
    // file and is not in the result, would be damaged by the update - never handed over; a damaged
    // one is never reported better than it was
    using State = Helper::SignatureState;
    const std::vector<std::pair<PDFObjectReference, State>> before = Helper::verifySignatures(originalData, securityHandler);
    const std::vector<std::pair<PDFObjectReference, State>> after = Helper::verifySignatures(signedData, securityHandler);
    int staysValid = 0;
    int showsChanges = 0;
    int wasInvalid = 0;
    QJsonArray earlierStates;
    for (const auto& [field, stateBefore] : before)
    {
        auto it = std::find_if(after.cbegin(), after.cend(), [field](const auto& state) { return state.first == field; });
        State state = it != after.cend() ? it->second : State::Invalid;
        if (stateBefore != State::Invalid && state == State::Invalid)
        {
            return PDFDocumentToolResult::createFailed(tr("An earlier signature of the document would become invalid, so the document was not signed."));
        }

        // Never better than before (6.7): a signature changed before stays changed
        if (stateBefore == State::Changed || stateBefore == State::Invalid)
        {
            state = stateBefore;
        }

        switch (state)
        {
            case State::Valid:
                ++staysValid;
                earlierStates.append(QStringLiteral("valid"));
                break;
            case State::Changed:
                ++showsChanges;
                earlierStates.append(QStringLiteral("changed"));
                break;
            case State::Invalid:
                ++wasInvalid;
                earlierStates.append(QStringLiteral("invalid"));
                break;
        }
    }

    const std::optional<PDFDocument> signedDocument = PDFDocumentSigner::readDocument(signedData, securityHandler, nullptr, nullptr);
    if (!signedDocument)
    {
        return PDFDocumentToolResult::createFailed(PDFDocumentSigner::getResultMessage(PDFDocumentSigner::Result::VerificationFailed));
    }

    // The result is only the verified file (R6-27): the exact bytes of the input and the update
    PDFDocumentToolResult result;
    PDFDocumentToolOutputDocument output;
    output.kind = PDFDocumentToolOutputDocument::Kind::New;
    output.document = PDFDocumentPointer(new PDFDocument(*signedDocument));
    output.writtenData = signedData;
    const QString baseName = QFileInfo(input.fileName).completeBaseName();
    output.suggestedFileName = (baseName.isEmpty() ? QStringLiteral("document") : baseName) + (m_mode == Mode::Sign ? QStringLiteral("_SIGNED.pdf") : QStringLiteral("_TIMESTAMPED.pdf"));
    result.documents.push_back(std::move(output));

    // The facts of 434:9276 and 435:8356, the data of MCP (never a password, a key or a path)
    const QByteArray subFilter = signerParameters.subfilter;
    result.data.insert(QString::fromLatin1(DATA_SUB_FILTER), QString::fromLatin1(subFilter));
    if (m_mode == Mode::Sign)
    {
        result.facts.push_back({ QString::fromLatin1(FACT_SIGNER), tr("Signer"), signerName });

        QString type = isTimestamped ? tr("Signature with timestamp") : tr("Signature");
        if (emptyField)
        {
            type = tr("%1 · in field %2").arg(type, emptyField->name);
        }
        else if (visiblePage >= 0)
        {
            type = tr("%1 · visible on page %2").arg(type).arg(visiblePage + 1);
        }
        else
        {
            type = tr("%1 · invisible").arg(type);
        }
        result.facts.push_back({ QString::fromLatin1(FACT_TYPE), tr("Type"), type });
        result.facts.push_back({ QString::fromLatin1(FACT_TIME), tr("Time"), Helper::formatTime(signerParameters.signingTime) });
        result.data.insert(QString::fromLatin1(DATA_SIGNING_TIME), signerParameters.signingTime.toUTC().toString(Qt::ISODate));
    }
    else
    {
        result.facts.push_back({ QString::fromLatin1(FACT_TYPE), tr("Type"), tr("Document timestamp") });
        result.facts.push_back({ QString::fromLatin1(FACT_TIME), tr("Time"), Helper::formatTime(timestampResult.time) });
    }

    if (isTimestamped && timestampResult.isGranted())
    {
        // Plan 5.2 (RS6SEC M3): the trust in the authority is no condition of the run, it is said beside
        // its name - the trusted certificates of the context (the user's and the AATL ones)
        const bool isTrusted = Helper::isAuthorityTrusted(signedData, details.signatureField, securityHandler, context.certificateStore.get(), m_mode == Mode::Timestamp);
        const QString authority = isTrusted ? timestampResult.authorityName : tr("%1 · Authority not trusted").arg(timestampResult.authorityName);
        result.facts.push_back({ QString::fromLatin1(FACT_AUTHORITY), tr("Authority"), authority });
        result.data.insert(QString::fromLatin1(DATA_TSA_TRUSTED), isTrusted);
        result.data.insert(QString::fromLatin1(DATA_TIMESTAMP_TIME), timestampResult.time.toUTC().toString(Qt::ISODate));
        result.data.insert(QString::fromLatin1(DATA_TSA_NAME), timestampResult.authorityName);
    }

    if (!before.empty())
    {
        // 434:9276 "Stay valid", "1 stays valid", "2 were invalid already"; a signature, after which
        // the unsaved changes of the tab were written, reports them (R6-3, R6-4)
        QString earlier;
        if (showsChanges == 0 && wasInvalid == 0)
        {
            earlier = staysValid == 1 ? tr("Stays valid") : tr("Stay valid");
        }
        else
        {
            QStringList parts;
            if (staysValid > 0)
            {
                parts << tr("%Ln stays valid", nullptr, staysValid);
            }
            if (showsChanges > 0)
            {
                parts << tr("%Ln reports the unsaved changes", nullptr, showsChanges);
            }
            if (wasInvalid > 0)
            {
                parts << tr("%Ln were invalid already", nullptr, wasInvalid);
            }
            earlier = parts.join(QStringLiteral(" · "));
        }
        result.facts.push_back({ QString::fromLatin1(FACT_EARLIER_SIGNATURES), tr("Earlier signatures"), earlier });
    }
    result.data.insert(QString::fromLatin1(DATA_EARLIER_SIGNATURES), earlierStates);

    // Q6.17: the copy includes the unsaved changes of the tab
    result.data.insert(QString::fromLatin1(DATA_UNSAVED_CHANGES), details.hasChanges);
    if (details.hasChanges)
    {
        result.facts.push_back({ QString::fromLatin1(FACT_UNSAVED_CHANGES), tr("Unsaved changes"), tr("Includes the unsaved changes") });
    }

    result.message = m_mode == Mode::Sign ? tr("The document was signed.") : tr("The timestamp was added.");
    return result;
}

}   // namespace pdf
