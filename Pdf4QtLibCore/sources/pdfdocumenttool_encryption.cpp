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

#include "pdfdocumenttool_encryption.h"
#include "pdfcatalog.h"
#include "pdfcertificatemanager.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfexception.h"
#include "pdfobjectutils.h"
#include "pdfoperationcontrol.h"
#include "pdfprogress.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

/// Helpers of PDFDocumentToolEncryption (file local)
class PDFDocumentToolEncryptionHelper
{
public:
    PDFDocumentToolEncryptionHelper() = delete;

    using Tool = PDFDocumentToolEncryption;
    using Factory = PDFSecurityHandlerFactory;
    using Permission = PDFSecurityHandler::Permission;

    /// The standards, whose declaration the encryption removes (R5-6, owner answer Q5.7): PDF/A
    /// forbids the encryption (ISO 19005-1 6.1.3, -2 and -3 6.1.3, -4 6.1.4); PDF/UA stays (bit 10
    /// is always set, ISO 14289-1, 7.16)
    static QStringList getRemovedStandards() { return { QStringLiteral("PDF/A") }; }

    /// A copy of a secret of the parameters in a buffer of its own: PDFSecureMemory::clear never
    /// writes a buffer shared with the caller's copy, so the run clears exactly this one (S5.1)
    static QString copySecret(const QVariant& value)
    {
        const QString text = value.toString();
        return QString(text.constData(), text.size());
    }

    /// Clears the passwords of the settings, whatever way the function leaves (S5.1)
    class SecretGuard
    {
    public:
        explicit SecretGuard(Factory::SecuritySettings* settings) : m_settings(settings) { }
        ~SecretGuard()
        {
            PDFSecureMemory::clear(m_settings->userPassword);
            PDFSecureMemory::clear(m_settings->ownerPassword);
        }

        SecretGuard(const SecretGuard&) = delete;
        SecretGuard& operator=(const SecretGuard&) = delete;

    private:
        Factory::SecuritySettings* m_settings;
    };

    static Factory::Algorithm getAlgorithm(const QString& id)
    {
        if (id == QLatin1String(Tool::ALGORITHM_AES_128))
        {
            return Factory::AES_128;
        }
        if (id == QLatin1String(Tool::ALGORITHM_RC4))
        {
            return Factory::RC4;
        }
        return Factory::AES_256;
    }

    static Factory::EncryptContents getContents(const QString& id)
    {
        if (id == QLatin1String(Tool::SCOPE_EXCEPT_METADATA))
        {
            return Factory::AllExceptMetadata;
        }
        if (id == QLatin1String(Tool::SCOPE_ATTACHMENTS))
        {
            return Factory::EmbeddedFiles;
        }
        return Factory::All;
    }

    static QString getAlgorithmId(Factory::Algorithm algorithm)
    {
        switch (algorithm)
        {
            case Factory::RC4:
                return QString::fromLatin1(Tool::ALGORITHM_RC4);
            case Factory::AES_128:
                return QString::fromLatin1(Tool::ALGORITHM_AES_128);
            case Factory::AES_256:
                return QString::fromLatin1(Tool::ALGORITHM_AES_256);
            case Factory::Certificate:
                return QStringLiteral("certificate");
            case Factory::None:
                break;
        }
        return QStringLiteral("none");
    }

    static QString getScopeId(Factory::EncryptContents contents)
    {
        switch (contents)
        {
            case Factory::AllExceptMetadata:
                return QString::fromLatin1(Tool::SCOPE_EXCEPT_METADATA);
            case Factory::EmbeddedFiles:
                return QString::fromLatin1(Tool::SCOPE_ATTACHMENTS);
            case Factory::All:
                break;
        }
        return QString::fromLatin1(Tool::SCOPE_EVERYTHING);
    }

    /// "AES 256-bit" of the menu Algorithm (430:1497)
    static QString getAlgorithmTitle(Factory::Algorithm algorithm)
    {
        switch (algorithm)
        {
            case Factory::RC4:
                return Tool::tr("RC4 128-bit");
            case Factory::AES_128:
                return Tool::tr("AES 128-bit");
            case Factory::AES_256:
                return Tool::tr("AES 256-bit");
            case Factory::Certificate:
                return Tool::tr("Certificate · AES 256-bit");
            case Factory::None:
                break;
        }
        return Tool::getNoEncryptionText();
    }

    /// The fact Encrypted of 430:3226 / 433:4721
    static QString getScopeText(Factory::EncryptContents contents)
    {
        switch (contents)
        {
            case Factory::AllExceptMetadata:
                return Tool::tr("Everything except metadata");
            case Factory::EmbeddedFiles:
                return Tool::tr("File attachments only");
            case Factory::All:
                break;
        }
        return Tool::tr("Everything, including metadata");
    }

    /// The value of /P of the handler (its dictionary: the authenticated recipient of a public-key
    /// handler gives the 4 bytes of its envelope, 7.6.5.3)
    static uint32_t getPermissionValue(const PDFSecurityHandler* handler)
    {
        const PDFObject dictionaryObject = handler->createEncryptionDictionaryObject();
        const PDFObject value = dictionaryObject.isDictionary() ? dictionaryObject.getDictionary()->get("P") : PDFObject();
        return value.isInt() ? uint32_t(int32_t(value.getInteger())) : 0;
    }

    /// The permission parameters set to their bits; K5.8 (Table 22): bit 12 means nothing without
    /// bit 3 (high resolution includes the low one), bit 6 includes the filling of forms (bit 9)
    static uint32_t getPermissionsOfParameters(const QVariantMap& parameters)
    {
        uint32_t permissions = uint32_t(Permission::Accessibility);
        for (const auto& item : Tool::getPermissionParameters())
        {
            if (parameters.value(QString::fromLatin1(item.first)).toBool())
            {
                permissions |= uint32_t(item.second);
            }
        }
        if (permissions & uint32_t(Permission::PrintHighResolution))
        {
            permissions |= uint32_t(Permission::PrintLowResolution);
        }
        if (permissions & uint32_t(Permission::ModifyInteractiveItems))
        {
            permissions |= uint32_t(Permission::ModifyFormFields);
        }
        return permissions;
    }

    /// The allowed and the refused groups of the facts of 432:4388 ("Viewing, printing, screen
    /// readers" / "Editing, forms, copying"); a group partly allowed is "printing (partly)"
    static std::pair<QString, QString> getPermissionTexts(uint32_t permissions)
    {
        struct Group
        {
            QString text;
            uint32_t bits;
        };

        const std::vector<Group> groups =
        {
            { Tool::tr("printing"), uint32_t(Permission::PrintLowResolution) | uint32_t(Permission::PrintHighResolution) },
            { Tool::tr("editing"), uint32_t(Permission::Modify) | uint32_t(Permission::Assemble) | uint32_t(Permission::ModifyInteractiveItems) },
            { Tool::tr("forms"), uint32_t(Permission::ModifyFormFields) },
            { Tool::tr("copying"), uint32_t(Permission::CopyContent) },
        };

        QStringList allowed = { Tool::tr("viewing") };
        QStringList refused;
        for (const Group& group : groups)
        {
            const uint32_t present = permissions & group.bits;
            if (present == group.bits)
            {
                allowed << group.text;
            }
            else if (present != 0)
            {
                allowed << Tool::tr("%1 (partly)").arg(group.text);
            }
            else
            {
                refused << group.text;
            }
        }
        allowed << Tool::tr("screen readers");

        auto join = [](QStringList list)
        {
            QString text = list.join(QStringLiteral(", "));
            if (!text.isEmpty())
            {
                text[0] = text[0].toUpper();
            }
            return text;
        };
        return { join(allowed), refused.isEmpty() ? Tool::tr("Nothing") : join(refused) };
    }

    static QJsonArray getPermissionIds(uint32_t permissions)
    {
        QJsonArray ids;
        for (const auto& item : Tool::getPermissionIds())
        {
            if (permissions & uint32_t(item.second))
            {
                ids.append(QString::fromLatin1(item.first));
            }
        }
        return ids;
    }

    /// The certificate of the run: the entry of the context, whose certificate has the fingerprint
    /// of the parameter (a locked entry is read by the password); its certificate in \p info.
    /// \p isLockedEntry: an entry, whose certificate the password could not read, was skipped
    static const PDFCertificateEntry* findCertificate(const PDFDocumentToolContext& context, const QString& fingerprint, const QString& password, PDFCertificateInfo* info, bool* isLockedEntry)
    {
        *isLockedEntry = false;
        for (const PDFCertificateEntry& entry : context.userCertificates)
        {
            std::optional<PDFCertificateInfo> certificate;
            if (!entry.info.getCertificateData().isEmpty())
            {
                certificate = entry.info;
            }
            else
            {
                certificate = PDFCertificateManager::readCertificate(entry.pkcs12, password);
                *isLockedEntry = *isLockedEntry || !certificate;
            }

            if (certificate && Tool::getCertificateFingerprint(certificate->getCertificateData()) == fingerprint)
            {
                *info = *certificate;
                return &entry;
            }
        }
        return nullptr;
    }

    /// R5-10 (ISO 32000-2, 7.4.10): no stream keeps a /Crypt step - the new /Encrypt does not know
    /// the old crypt filters (or there is none). The reader decrypts a stream of its own crypt filter
    /// and removes the step when it opens an encrypted file; a step, which stays, is either the
    /// crypt filter Identity (no key, the data are the same - the step is only dropped, also of a
    /// file without encryption) or one the reader could not decrypt (an unknown name, a locked
    /// attachment): such a stream would be written as garbage, so the run fails with the reason.
    /// \returns the number of the changed streams; throws PDFException for a step, which is not Identity
    static int removeCryptFilters(PDFObjectStorage* storage)
    {
        int count = 0;
        const size_t objectCount = storage->getObjects().size();
        for (size_t i = 0; i < objectCount; ++i)
        {
            const PDFObjectStorage::Entry& entry = storage->getObjects()[i];
            if (!entry.object.isStream())
            {
                continue;
            }

            const PDFStream* stream = entry.object.getStream();
            const PDFDictionary* dictionary = stream->getDictionary();

            std::vector<QByteArray> filters;
            const PDFObject& filterObject = storage->getObject(dictionary->get("Filter"));
            if (filterObject.isName())
            {
                filters.push_back(filterObject.getString());
            }
            else if (filterObject.isArray())
            {
                for (size_t j = 0; j < filterObject.getArray()->getCount(); ++j)
                {
                    const PDFObject& item = storage->getObject(filterObject.getArray()->getItem(j));
                    filters.push_back(item.isName() ? item.getString() : QByteArray());
                }
            }

            if (std::find(filters.cbegin(), filters.cend(), QByteArray("Crypt")) == filters.cend())
            {
                continue;
            }

            std::vector<PDFObject> parameters;
            const PDFObject& parametersObject = storage->getObject(dictionary->get("DecodeParms"));
            if (parametersObject.isArray())
            {
                for (size_t j = 0; j < parametersObject.getArray()->getCount(); ++j)
                {
                    parameters.push_back(storage->getObject(parametersObject.getArray()->getItem(j)));
                }
            }
            else
            {
                parameters.push_back(parametersObject);
            }
            parameters.resize(filters.size());

            const PDFObjectReference reference(PDFInteger(i), entry.generation);
            std::vector<QByteArray> remainingNames;
            std::vector<PDFObject> remaining;
            for (size_t j = 0; j < filters.size(); ++j)
            {
                if (filters[j] != "Crypt")
                {
                    remainingNames.push_back(filters[j]);
                    remaining.push_back(parameters[j]);
                    continue;
                }

                // 7.4.10, Table 14: the name of the crypt filter, Identity by default
                QByteArray name = PDFSecurityHandler::IDENTITY_FILTER_NAME;
                if (const PDFDictionary* cryptParameters = storage->getDictionaryFromObject(parameters[j]))
                {
                    const PDFObject& nameObject = storage->getObject(cryptParameters->get("Name"));
                    if (nameObject.isName())
                    {
                        name = nameObject.getString();
                    }
                }
                if (name != PDFSecurityHandler::IDENTITY_FILTER_NAME)
                {
                    throw PDFException(Tool::tr("The stream %1 is encrypted by its own crypt filter '%2', which could not be decrypted when the document was opened. It cannot be written with another encryption, so nothing was made.")
                                       .arg(reference.objectNumber).arg(QString::fromLatin1(name)));
                }
            }

            PDFDictionaryBuilder newDictionary(*dictionary);
            newDictionary.removeEntry("Filter");
            newDictionary.removeEntry("DecodeParms");
            newDictionary.removeEntry(PDFSecurityHandler::OBJECT_REFERENCE_DICTIONARY_NAME);

            const size_t remainingCount = remainingNames.size();
            if (remainingCount > 0)
            {
                PDFArrayBuilder remainingFilters;
                PDFArrayBuilder remainingParameters;
                bool hasParameters = false;
                for (size_t j = 0; j < remainingCount; ++j)
                {
                    remainingFilters.appendItem(PDFObject::createName(remainingNames[j]));
                    remainingParameters.appendItem(remaining[j]);
                    hasParameters = hasParameters || !remaining[j].isNull();
                }

                if (remainingCount == 1)
                {
                    newDictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName(remainingNames.back()));
                    if (hasParameters)
                    {
                        newDictionary.setEntry(PDFInplaceOrMemoryString("DecodeParms"), remaining.back());
                    }
                }
                else
                {
                    newDictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createArray(std::move(remainingFilters)));
                    if (hasParameters)
                    {
                        newDictionary.setEntry(PDFInplaceOrMemoryString("DecodeParms"), PDFObject::createArray(std::move(remainingParameters)));
                    }
                }
            }

            // The data are the same: the crypt filter Identity does not change them
            QByteArray data = *stream->getContent();
            storage->setObject(reference, PDFObject::createStream(PDFStream(std::move(newDictionary), std::move(data))));
            ++count;
        }

        return count;
    }

    /// R5-3: the streams of the embedded files of every file specification (/EF and /RF, Table
    /// 43; 7.6.6: the related files have the crypt filter of the embedded files) - wherever the
    /// specification is (the name tree EmbeddedFiles, a FileAttachment annotation, /AF, a
    /// collection, ...), so every indirect object of the storage is read once (one walk: a cycle
    /// or a node shared by more paths is read once) and its direct objects up to the depth limit
    /// \param walk The walk (its limit tells, whether everything was read)
    static std::set<PDFObjectReference> findEmbeddedFileStreams(const PDFObjectStorage* storage, PDFObjectWalk* walk)
    {
        std::set<PDFObjectReference> streams;
        const PDFObjectStorage::PDFObjects& objects = storage->getObjects();
        for (size_t i = 0; i < objects.size(); ++i)
        {
            const PDFObjectReference reference(PDFInteger(i), objects[i].generation);
            if (!objects[i].object.isNull() && walk->enterReference(reference, 0))
            {
                findInObject(storage, objects[i].object, 1, walk, streams);
            }
        }
        return streams;
    }

    /// Marks the embedded file streams by /Type /EmbeddedFile (Table 44: optional, "shall be
    /// EmbeddedFile if present")
    /// \returns the number of the changed streams
    static int setEmbeddedFileTypes(PDFObjectStorage* storage, const std::set<PDFObjectReference>& streams)
    {
        int count = 0;
        for (const PDFObjectReference& reference : streams)
        {
            const PDFObject& object = storage->getObject(reference);
            const PDFStream* stream = object.getStream();
            if (PDFSecurityHandler::isEmbeddedFileStream(stream->getDictionary()))
            {
                // The one rule of the reader, the writer and this walk
                continue;
            }

            PDFDictionaryBuilder dictionary(*stream->getDictionary());
            dictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName(QByteArray("EmbeddedFile")));
            QByteArray content = *stream->getContent();
            storage->setObject(reference, PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content))));
            ++count;
        }
        return count;
    }

    /// File attachments only (owner 2026-11-15): the strings are not encrypted (/StrF Identity,
    /// 7.6.6), so the MD5 of an attachment in /Params /CheckSum (Table 45) would stay readable and
    /// tell, which file is attached. It is removed from the parameters of every embedded file
    /// stream; parameters in an object of their own are changed in that object (no copy of the
    /// checksum stays in the file).
    /// \returns the number of the changed objects
    static int removeEmbeddedFileCheckSums(PDFObjectStorage* storage, const std::set<PDFObjectReference>& streams)
    {
        int count = 0;
        for (const PDFObjectReference& reference : streams)
        {
            const PDFObject streamObject = storage->getObject(reference);
            const PDFStream* stream = streamObject.getStream();
            // Table 44: /Params is a dictionary (another object, also a stream of a hostile file, stays)
            const PDFObject& parametersObject = stream->getDictionary()->get("Params");
            const PDFObject& parametersValue = storage->getObject(parametersObject);
            const PDFDictionary* parameters = parametersValue.isDictionary() ? parametersValue.getDictionary() : nullptr;
            if (!parameters || !parameters->hasKey("CheckSum"))
            {
                continue;
            }

            PDFDictionaryBuilder newParameters(*parameters);
            newParameters.removeEntry("CheckSum");
            if (parametersObject.isReference())
            {
                storage->setObject(parametersObject.getReference(), PDFObject::createDictionary(std::move(newParameters)));
            }
            else
            {
                PDFDictionaryBuilder dictionary(*stream->getDictionary());
                dictionary.setEntry(PDFInplaceOrMemoryString("Params"), PDFObject::createDictionary(std::move(newParameters)));
                QByteArray content = *stream->getContent();
                storage->setObject(reference, PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content))));
            }
            ++count;
        }
        return count;
    }

private:
    /// A stream of /EF or of a related files array of /RF
    static void addStream(const PDFObjectStorage* storage, const PDFObject& object, std::set<PDFObjectReference>& streams)
    {
        if (object.isReference() && storage->getObject(object).isStream())
        {
            streams.insert(object.getReference());
        }
    }

    static void findInObject(const PDFObjectStorage* storage, const PDFObject& object, int depth, PDFObjectWalk* walk, std::set<PDFObjectReference>& streams)
    {
        // References are not followed: every indirect object is read by findEmbeddedFileStreams
        if (object.isReference() || !walk->enter(object, depth))
        {
            return;
        }

        if (object.isArray())
        {
            for (size_t i = 0; i < object.getArray()->getCount(); ++i)
            {
                findInObject(storage, object.getArray()->getItem(i), depth + 1, walk, streams);
            }
            return;
        }

        const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : (object.isDictionary() ? object.getDictionary() : nullptr);
        if (!dictionary)
        {
            return;
        }

        // A file specification: /EF { /F, /UF, /DOS, /Mac, /Unix: stream }, /RF { key: [ name stream ... ] }
        if (const PDFDictionary* embeddedFiles = storage->getDictionaryFromObject(dictionary->get("EF")))
        {
            for (size_t i = 0; i < embeddedFiles->getCount(); ++i)
            {
                addStream(storage, embeddedFiles->getValue(i), streams);
            }
        }
        if (const PDFDictionary* relatedFiles = storage->getDictionaryFromObject(dictionary->get("RF")))
        {
            for (size_t i = 0; i < relatedFiles->getCount(); ++i)
            {
                const PDFObject& array = storage->getObject(relatedFiles->getValue(i));
                for (size_t j = 1; array.isArray() && j < array.getArray()->getCount(); j += 2)
                {
                    addStream(storage, array.getArray()->getItem(j), streams);
                }
            }
        }

        for (size_t i = 0; i < dictionary->getCount(); ++i)
        {
            findInObject(storage, dictionary->getValue(i), depth + 1, walk, streams);
        }
    }
};

PDFDocumentToolEncryption::PDFDocumentToolEncryption(Mode mode) :
    m_mode(mode)
{

}

QString PDFDocumentToolEncryption::getToolId(Mode mode)
{
    switch (mode)
    {
        case Mode::Password:
            return QString::fromLatin1(TOOL_ID_PASSWORD);
        case Mode::Permissions:
            return QString::fromLatin1(TOOL_ID_PERMISSIONS);
        case Mode::Certificate:
            return QString::fromLatin1(TOOL_ID_CERTIFICATE);
        case Mode::Unlock:
            return QString::fromLatin1(TOOL_ID_UNLOCK);
    }

    Q_ASSERT(false);
    return QString();
}

std::optional<PDFDocumentToolEncryption::Mode> PDFDocumentToolEncryption::findMode(QStringView toolId)
{
    for (const Mode mode : { Mode::Password, Mode::Permissions, Mode::Certificate, Mode::Unlock })
    {
        if (toolId == getToolId(mode))
        {
            return mode;
        }
    }
    return std::nullopt;
}

std::vector<std::pair<const char*, PDFSecurityHandler::Permission>> PDFDocumentToolEncryption::getPermissionParameters()
{
    using Permission = PDFSecurityHandler::Permission;
    return { { PARAMETER_PRINT_LOW, Permission::PrintLowResolution },
             { PARAMETER_PRINT_HIGH, Permission::PrintHighResolution },
             { PARAMETER_MODIFY, Permission::Modify },
             { PARAMETER_ASSEMBLE, Permission::Assemble },
             { PARAMETER_ANNOTATE, Permission::ModifyInteractiveItems },
             { PARAMETER_FILL_FORMS, Permission::ModifyFormFields },
             { PARAMETER_COPY, Permission::CopyContent } };
}

std::vector<std::pair<const char*, PDFSecurityHandler::Permission>> PDFDocumentToolEncryption::getPermissionIds()
{
    using Permission = PDFSecurityHandler::Permission;
    return { { "print-low", Permission::PrintLowResolution },
             { "print-high", Permission::PrintHighResolution },
             { "modify", Permission::Modify },
             { "assemble", Permission::Assemble },
             { "annotate", Permission::ModifyInteractiveItems },
             { "fill-forms", Permission::ModifyFormFields },
             { "copy", Permission::CopyContent },
             { "accessibility", Permission::Accessibility } };
}

QString PDFDocumentToolEncryption::getCertificateFingerprint(const QByteArray& certificateData)
{
    return QString::fromLatin1(QCryptographicHash::hash(certificateData, QCryptographicHash::Sha256).toHex());
}

QVariantMap PDFDocumentToolEncryption::getPresetValues(Preset preset)
{
    // The check marks of the presets of 432:2698 (View and print 432:2105, Print and fill forms 432:3072,
    // Allow everything 432:4069); the accessibility is no parameter (always allowed, K5.7)
    QVariantMap values;
    for (const auto& item : getPermissionParameters())
    {
        values.insert(QString::fromLatin1(item.first), false);
    }

    const QString printLow = QString::fromLatin1(PARAMETER_PRINT_LOW);
    const QString printHigh = QString::fromLatin1(PARAMETER_PRINT_HIGH);
    switch (preset)
    {
        case Preset::ViewOnly:
        case Preset::Custom:
            break;

        case Preset::ViewAndPrint:
            values.insert(printLow, true);
            values.insert(printHigh, true);
            break;

        case Preset::PrintAndFillForms:
            values.insert(printLow, true);
            values.insert(printHigh, true);
            values.insert(QString::fromLatin1(PARAMETER_FILL_FORMS), true);
            break;

        case Preset::AllowEverything:
            for (auto it = values.begin(); it != values.end(); ++it)
            {
                it.value() = true;
            }
            break;
    }
    return values;
}

PDFDocumentToolEncryption::Preset PDFDocumentToolEncryption::findPreset(const QVariantMap& values)
{
    for (const Preset preset : { Preset::ViewOnly, Preset::ViewAndPrint, Preset::PrintAndFillForms, Preset::AllowEverything })
    {
        const QVariantMap presetValues = getPresetValues(preset);
        bool isMatch = true;
        for (auto it = presetValues.cbegin(); it != presetValues.cend(); ++it)
        {
            isMatch = isMatch && values.value(it.key()).toBool() == it.value().toBool();
        }
        if (isMatch)
        {
            return preset;
        }
    }
    return Preset::Custom;
}

QString PDFDocumentToolEncryption::getId() const
{
    return getToolId(m_mode);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolEncryption::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    // The texts of the options are the rows of the menus Algorithm and Encrypt of 430:1497 and
    // 433:3480, the titles of the permissions the rows of 432:2105 (package P1b checks them
    // against the frames; K5.15: American English)
    PDFDocumentToolParameterDefinitions definitions;
    const bool isPassword = m_mode == Mode::Password;
    const bool isPermissions = m_mode == Mode::Permissions;
    const bool isCertificate = m_mode == Mode::Certificate;

    if (isPassword || isPermissions)
    {
        definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_ALGORITHM), QT_TR_NOOP("Algorithm"), QT_TR_NOOP("Encryption algorithm"),
                                                     {
                                                         { QString::fromLatin1(ALGORITHM_AES_256), QT_TR_NOOP("AES 256-bit"), QT_TR_NOOP("Very strong · recommended"), QStringLiteral("key") },
                                                         { QString::fromLatin1(ALGORITHM_AES_128), QT_TR_NOOP("AES 128-bit"), QT_TR_NOOP("Strong · for readers older than 2010"), QStringLiteral("key") },
                                                         { QString::fromLatin1(ALGORITHM_RC4), QT_TR_NOOP("RC4 128-bit"), QT_TR_NOOP("Weak · only for very old readers"), QStringLiteral("key") },
                                                     },
                                                     QString::fromLatin1(ALGORITHM_AES_256)));
    }

    if (isPassword || isPermissions || isCertificate)
    {
        // 430:1497 and 433:3480: the attachments need the password, or the certificate; the icons
        // of the rows (icon/file-text, icon/metadata, icon/paperclip; icon/key of the algorithm)
        definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_SCOPE), QT_TR_NOOP("Encrypt"), QT_TR_NOOP("What is encrypted"),
                                                     {
                                                         { QString::fromLatin1(SCOPE_EVERYTHING), QT_TR_NOOP("Everything"), QT_TR_NOOP("All contents including document metadata"), QStringLiteral("file-text") },
                                                         { QString::fromLatin1(SCOPE_EXCEPT_METADATA), QT_TR_NOOP("Everything except metadata"), QT_TR_NOOP("Only the XMP metadata stays readable for search engines"), QStringLiteral("metadata") },
                                                         isCertificate ? PDFDocumentToolParameterOption(QString::fromLatin1(SCOPE_ATTACHMENTS), QT_TR_NOOP("File attachments only"), QT_TR_NOOP("The document opens freely, attachments need the certificate"), QStringLiteral("paperclip"))
                                                                       : PDFDocumentToolParameterOption(QString::fromLatin1(SCOPE_ATTACHMENTS), QT_TR_NOOP("File attachments only"), QT_TR_NOOP("The document opens freely, attachments need the password"), QStringLiteral("paperclip")),
                                                     },
                                                     QString::fromLatin1(SCOPE_EVERYTHING)));
    }

    if (isPassword)
    {
        definitions.push_back(Definition::createSecret(tr, QString::fromLatin1(PARAMETER_OPEN_PASSWORD), QT_TR_NOOP("Open password"), QT_TR_NOOP("Password needed to open the document")));
        definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_OWNER_SAME_AS_OPEN), QT_TR_NOOP("Use the open password for owner access too"),
                                                     QT_TR_NOOP("The open password also allows changing the security; not possible, when restrictions are kept"), true));
        definitions.push_back(Definition::createSecret(tr, QString::fromLatin1(PARAMETER_OWNER_PASSWORD), QT_TR_NOOP("Owner password"), QT_TR_NOOP("Password needed to change the security")));
        definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_KEEP_RESTRICTIONS), QT_TR_NOOP("Keep the restrictions"),
                                                     QT_TR_NOOP("The permissions of an encrypted document stay as they are"), true));
    }

    if (isPermissions)
    {
        definitions.push_back(Definition::createSecret(tr, QString::fromLatin1(PARAMETER_OWNER_PASSWORD), QT_TR_NOOP("Owner password"), QT_TR_NOOP("Password needed to change the restrictions")));
        definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_REQUIRE_OPEN_PASSWORD), QT_TR_NOOP("Also require a password to open"), nullptr, false));
        definitions.push_back(Definition::createSecret(tr, QString::fromLatin1(PARAMETER_OPEN_PASSWORD), QT_TR_NOOP("Open password"), QT_TR_NOOP("Password needed to open the document")));
    }

    if (isPermissions || isCertificate)
    {
        // Restrict Permissions starts with View and print (432:2105), a certificate allows everything
        const QVariantMap defaults = getPresetValues(isPermissions ? Preset::ViewAndPrint : Preset::AllowEverything);
        const auto value = [&defaults](const char* name) { return defaults.value(QString::fromLatin1(name)).toBool(); };
        // The descriptions say what each bit of ISO 32000-2 Table 22 (Table 24 for a certificate)
        // allows - the schema of MCP shows them; the panel has the texts of its frames
        definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_PRINT_LOW), QT_TR_NOOP("Print in low resolution"),
                                                     QT_TR_NOOP("Print the document (bit 3); without the high resolution only a low-quality, possibly degraded representation"), value(PARAMETER_PRINT_LOW)));
        definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_PRINT_HIGH), QT_TR_NOOP("Print in high resolution"),
                                                     QT_TR_NOOP("Print in full quality (bit 12); needs the low resolution printing, it is turned on with it"), value(PARAMETER_PRINT_HIGH)));
        definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_MODIFY), QT_TR_NOOP("Modify document contents"),
                                                     QT_TR_NOOP("Change the contents of the document by other operations than assembling, comments and filling of forms (bit 4)"), value(PARAMETER_MODIFY)));
        definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_ASSEMBLE), QT_TR_NOOP("Assemble the document"),
                                                     QT_TR_NOOP("Insert, rotate and delete pages, create bookmarks and thumbnails (bit 11), also without modifying the contents"), value(PARAMETER_ASSEMBLE)));
        definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_ANNOTATE), QT_TR_NOOP("Edit comments and form fields"),
                                                     QT_TR_NOOP("Add and change comments and fill in form fields; with modifying the contents also create form fields (bit 6); it includes filling in forms"), value(PARAMETER_ANNOTATE)));
        definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_FILL_FORMS), QT_TR_NOOP("Fill in forms"),
                                                     QT_TR_NOOP("Fill in existing form fields, signature fields included (bit 9), also without editing comments and form fields"), value(PARAMETER_FILL_FORMS)));
        definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_COPY), QT_TR_NOOP("Copy and extract content"),
                                                     QT_TR_NOOP("Copy or otherwise extract text and graphics (bit 5); extraction for assistive technology is always allowed (bit 10)"), value(PARAMETER_COPY)));
    }

    if (isCertificate)
    {
        // The fingerprint never goes through MCP: the confirmation of PDF4QT chooses the certificate
        // (owner answer Q5.6); 64 hexadecimal digits of SHA-256
        definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_CERTIFICATE), QT_TR_NOOP("Certificate"),
                                                       QT_TR_NOOP("SHA-256 fingerprint of the certificate"), QString(), 64).setMcpExcluded(true));
        definitions.push_back(Definition::createSecret(tr, QString::fromLatin1(PARAMETER_CERTIFICATE_PASSWORD), QT_TR_NOOP("Certificate password"), QT_TR_NOOP("Password of the .pfx file; it is not stored")));
    }

    return definitions;
}

uint32_t PDFDocumentToolEncryption::getAllPermissions()
{
    uint32_t permissions = 0;
    for (const auto& item : getPermissionIds())
    {
        permissions |= uint32_t(item.second);
    }
    return permissions;
}

bool PDFDocumentToolEncryption::isSecurityChangeAllowed(const PDFDocument* document)
{
    // The one rule of the security handler (ISO 32000-2, 7.6.4.1 the owner; Table 24 bit 2 of the
    // envelope of the recipient, never the unprotected /P; Q5.9 c a document opened without its
    // attachments)
    const PDFSecurityHandler* handler = document ? document->getStorage().getSecurityHandler() : nullptr;
    return document && (!handler || handler->isEncryptionChangeAllowed());
}

uint32_t PDFDocumentToolEncryption::getDocumentPermissions(const PDFDocument* document)
{
    const PDFSecurityHandler* handler = document ? document->getStorage().getSecurityHandler() : nullptr;
    if (!handler || handler->getMode() == EncryptionMode::None)
    {
        return getAllPermissions();
    }

    if (handler->getMode() == EncryptionMode::PublicKey)
    {
        // Table 24: the permissions of the envelope of the authorized recipient (bit 2 gives every
        // permission), never the unprotected /P of the dictionary (R5-24)
        uint32_t permissions = uint32_t(PDFSecurityHandler::Permission::Accessibility);
        for (const auto& item : getPermissionIds())
        {
            if (handler->isEncryptionChangeAllowed() || handler->isAllowed(item.second))
            {
                permissions |= uint32_t(item.second);
            }
        }
        return permissions;
    }

    // The standard handler: its /P (protected by the key of revisions 2 - 4 and by /Perms of
    // revision 6); isAllowed would give the owner everything
    const uint32_t value = PDFDocumentToolEncryptionHelper::getPermissionValue(handler);
    const PDFStandardSecurityHandler* standardHandler = dynamic_cast<const PDFStandardSecurityHandler*>(handler);
    const bool isRevision2 = standardHandler && standardHandler->getRevision() == 2;

    // Table 22: the accessibility is always allowed (PDF 2.0, R5-14)
    uint32_t permissions = uint32_t(PDFSecurityHandler::Permission::Accessibility);
    for (const auto& item : getPermissionIds())
    {
        const PDFSecurityHandler::Permission bit = isRevision2 ? PDFSecurityHandler::getRevision2Permission(item.second) : item.second;
        if (value & uint32_t(bit))
        {
            permissions |= uint32_t(item.second);
        }
    }
    return permissions;
}

QString PDFDocumentToolEncryption::getEncryptionName(const PDFDocument* document)
{
    const PDFSecurityHandler* handler = document ? document->getStorage().getSecurityHandler() : nullptr;
    if (!handler || handler->getMode() == EncryptionMode::None)
    {
        return getNoEncryptionText();
    }

    // The method of the crypt filter of the streams (/StmF; /EFF, when the streams are not encrypted)
    const PDFObjectStorage& storage = document->getStorage();
    const PDFDictionary* trailer = document->getTrailerDictionary();
    const PDFDictionary* encrypt = trailer ? storage.getDictionaryFromObject(trailer->get("Encrypt")) : nullptr;
    QByteArray method;
    PDFInteger length = 0;
    if (encrypt)
    {
        const PDFObject& lengthObject = storage.getObject(encrypt->get("Length"));
        length = lengthObject.isInt() ? lengthObject.getInteger() : 0;

        const PDFDictionary* cryptFilters = storage.getDictionaryFromObject(encrypt->get("CF"));
        for (const char* key : { "StmF", "StrF", "EFF" })
        {
            const PDFObject& name = storage.getObject(encrypt->get(key));
            const PDFDictionary* filter = (cryptFilters && name.isName()) ? storage.getDictionaryFromObject(cryptFilters->get(name.getString())) : nullptr;
            const PDFObject& filterMethod = filter ? storage.getObject(filter->get("CFM")) : PDFObject();
            if (filterMethod.isName() && filterMethod.getString() != "None")
            {
                method = filterMethod.getString();
                break;
            }
        }
    }

    QString cipher;
    if (handler->getVersion() >= 5 || method == "AESV3")
    {
        cipher = tr("AES 256-bit");
    }
    else if (method == "AESV2")
    {
        cipher = tr("AES 128-bit");
    }
    else
    {
        // RC4: V 4 has 128 bits, V 1 40 bits, V 2 / 3 their /Length (40 by default)
        int bits = 40;
        if (handler->getVersion() == 4)
        {
            bits = 128;
        }
        else if (handler->getVersion() > 1 && length >= 40 && length <= 128)
        {
            bits = int(length);
        }
        cipher = tr("RC4 %1-bit").arg(bits);
    }

    return handler->getMode() == EncryptionMode::PublicKey ? tr("Certificate · %1").arg(cipher) : cipher;
}

QString PDFDocumentToolEncryption::getOpenPasswordText(bool isPasswordSet, bool isAttachmentsOnly)
{
    if (!isPasswordSet)
    {
        return tr("Not required");
    }
    return isAttachmentsOnly ? tr("Not required · attachments need the password") : tr("Required");
}

QString PDFDocumentToolEncryption::getAllAllowedText()
{
    return tr("All allowed");
}

QString PDFDocumentToolEncryption::getNoEncryptionText()
{
    return tr("None");
}

PDFSecurityHandlerFactory::SecuritySettings PDFDocumentToolEncryption::createSettings(const PDFDocumentToolContext& context, const QVariantMap& parameters, std::vector<PDFDocumentToolParameterError>* errors) const
{
    using Helper = PDFDocumentToolEncryptionHelper;
    using Factory = PDFSecurityHandlerFactory;

    std::vector<PDFDocumentToolParameterError> dummy;
    std::vector<PDFDocumentToolParameterError>& errorList = errors ? *errors : dummy;
    const PDFDocument* document = context.getDocument();

    Factory::SecuritySettings settings;
    settings.algorithm = Factory::None;
    if (m_mode == Mode::Unlock)
    {
        return settings;
    }

    settings.encryptContents = Helper::getContents(parameters.value(QString::fromLatin1(PARAMETER_SCOPE)).toString());
    const QString openParameter = QString::fromLatin1(PARAMETER_OPEN_PASSWORD);
    const QString ownerParameter = QString::fromLatin1(PARAMETER_OWNER_PASSWORD);

    switch (m_mode)
    {
        case Mode::Password:
        {
            settings.algorithm = Helper::getAlgorithm(parameters.value(QString::fromLatin1(PARAMETER_ALGORITHM)).toString());
            settings.userPassword = Helper::copySecret(parameters.value(openParameter));

            const bool isOwnerSameAsOpen = parameters.value(QString::fromLatin1(PARAMETER_OWNER_SAME_AS_OPEN)).toBool();
            if (!isOwnerSameAsOpen)
            {
                settings.ownerPassword = Helper::copySecret(parameters.value(ownerParameter));
            }

            // K5.19: only the bits of /P are kept, never the old owner password
            const bool isKeepRestrictions = parameters.value(QString::fromLatin1(PARAMETER_KEEP_RESTRICTIONS)).toBool();
            settings.permissions = isKeepRestrictions ? getDocumentPermissions(document) : getAllPermissions();
            const bool hasRestrictions = settings.permissions != getAllPermissions();

            if (settings.userPassword.isEmpty())
            {
                errorList.push_back({ openParameter, tr("Type the open password.") });
            }
            if (!isOwnerSameAsOpen && settings.ownerPassword.isEmpty())
            {
                errorList.push_back({ ownerParameter, tr("Type the owner password or use the open password for owner access.") });
            }

            // Owner answer Q5.1: kept restrictions need an owner password of their own, otherwise
            // every reader is the owner (Algorithm 7 succeeds with the open password)
            if (hasRestrictions && !settings.userPassword.isEmpty() && (isOwnerSameAsOpen || settings.ownerPassword == settings.userPassword))
            {
                errorList.push_back({ isOwnerSameAsOpen ? QString::fromLatin1(PARAMETER_OWNER_SAME_AS_OPEN) : ownerParameter,
                                      tr("The restrictions of the document are kept, so it needs an owner password different from the open password – otherwise every reader gets owner access.") });
            }
            break;
        }

        case Mode::Permissions:
        {
            settings.algorithm = Helper::getAlgorithm(parameters.value(QString::fromLatin1(PARAMETER_ALGORITHM)).toString());
            settings.ownerPassword = Helper::copySecret(parameters.value(ownerParameter));
            const bool isOpenPasswordRequired = parameters.value(QString::fromLatin1(PARAMETER_REQUIRE_OPEN_PASSWORD)).toBool();
            if (isOpenPasswordRequired)
            {
                settings.userPassword = Helper::copySecret(parameters.value(openParameter));
            }
            settings.permissions = Helper::getPermissionsOfParameters(parameters);

            if (settings.ownerPassword.isEmpty())
            {
                errorList.push_back({ ownerParameter, tr("Type the owner password.") });
            }
            if (isOpenPasswordRequired && settings.userPassword.isEmpty())
            {
                errorList.push_back({ openParameter, tr("Type the open password.") });
            }

            // 432:3721
            if (isOpenPasswordRequired && !settings.userPassword.isEmpty() && settings.userPassword == settings.ownerPassword)
            {
                const QString text = tr("Use a password different from the open password – otherwise every reader gets owner access.");
                errorList.push_back({ ownerParameter, text });
                errorList.push_back({ openParameter, text });
            }
            break;
        }

        case Mode::Certificate:
        {
            settings.algorithm = Factory::Certificate;
            settings.permissions = Helper::getPermissionsOfParameters(parameters);
            settings.userPassword = Helper::copySecret(parameters.value(QString::fromLatin1(PARAMETER_CERTIFICATE_PASSWORD)));

            const QString certificateParameter = QString::fromLatin1(PARAMETER_CERTIFICATE);
            const QString passwordParameter = QString::fromLatin1(PARAMETER_CERTIFICATE_PASSWORD);
            const QString fingerprint = parameters.value(certificateParameter).toString().toLower();
            if (fingerprint.isEmpty())
            {
                errorList.push_back({ certificateParameter, tr("Choose a certificate.") });
                break;
            }

            PDFCertificateInfo info;
            bool isLockedEntry = false;
            const PDFCertificateEntry* entry = Helper::findCertificate(context, fingerprint, settings.userPassword, &info, &isLockedEntry);
            if (!entry)
            {
                // A file, which the password does not open, may be the chosen one (433:3972)
                errorList.push_back(isLockedEntry ? PDFDocumentToolParameterError{ passwordParameter, tr("Invalid certificate or password.") }
                                                  : PDFDocumentToolParameterError{ certificateParameter, tr("The chosen certificate is not available.") });
                break;
            }
            settings.certificate = *entry;

            // 433:3972: the password must open the private key of this very certificate
            const std::optional<PDFCertificateInfo> unlocked = PDFCertificateManager::readCertificate(entry->pkcs12, settings.userPassword);
            if (!unlocked || getCertificateFingerprint(unlocked->getCertificateData()) != fingerprint)
            {
                errorList.push_back({ passwordParameter, tr("Invalid certificate or password.") });
                break;
            }
            settings.certificate.info = *unlocked;

            const QDateTime now = QDateTime::currentDateTimeUtc();
            const QDateTime notBefore = unlocked->getNotValidBefore();
            const QDateTime notAfter = unlocked->getNotValidAfter();
            if ((notAfter.isValid() && notAfter < now) || (notBefore.isValid() && notBefore > now))
            {
                errorList.push_back({ certificateParameter, tr("The certificate is not valid now, it cannot be used.") });
            }
            else if (!PDFCertificateManager::isUsableForEncryption(unlocked->getCertificateData()))
            {
                errorList.push_back({ certificateParameter, tr("The certificate cannot be used for encryption (its key usage has no key encipherment).") });
            }
            break;
        }

        case Mode::Unlock:
            break;
    }

    return settings;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolEncryption::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (!errors.empty())
    {
        return errors;
    }

    // Plan 4.6, R5-4: only the owner changes the security
    const PDFDocument* document = context.getDocument();
    if (!isSecurityChangeAllowed(document))
    {
        const bool isCertificate = document && document->getStorage().getSecurityHandler()->getMode() == EncryptionMode::PublicKey;
        errors.push_back({ QString(), isCertificate ? tr("The certificate of this document does not allow changing its security.")
                                                    : tr("Only the owner of the document can change its security. Unlock it by the owner password first.") });
        return errors;
    }
    if (document->getStorage().getSecurityHandler()->isEmbeddedFilesLocked())
    {
        errors.push_back({ QString(), tr("The attachments of the document are locked. Open the document with its password first.") });
        return errors;
    }

    PDFSecurityHandlerFactory::SecuritySettings settings = createSettings(context, parameters, &errors);
    PDFDocumentToolEncryptionHelper::SecretGuard guard(&settings);
    if (errors.empty() && settings.algorithm != PDFSecurityHandlerFactory::None && settings.algorithm != PDFSecurityHandlerFactory::Certificate)
    {
        // The rules of the factory (PDFDocEncoding of revision 4, 430:1999; SASLprep of revision 6)
        QString errorMessage;
        if (!PDFSecurityHandlerFactory::validate(settings, &errorMessage))
        {
            errors.push_back({ QString(), errorMessage });
        }
    }
    return errors;
}

PDFDocumentToolEncryption::Change PDFDocumentToolEncryption::changeEncryption(const PDFDocument* input, PDFSecurityHandlerFactory::SecuritySettings settings, const PDFOperationControl* control, PDFProgressSink* progress)
{
    using Helper = PDFDocumentToolEncryptionHelper;
    Helper::SecretGuard guard(&settings);

    Change change;
    if (!input)
    {
        change.error = tr("An input document is missing.");
        return change;
    }
    if (!isSecurityChangeAllowed(input))
    {
        change.error = tr("Only the owner of the document can change its security.");
        return change;
    }

    const PDFSecurityHandler* inputHandler = input->getStorage().getSecurityHandler();
    if (inputHandler && inputHandler->isEmbeddedFilesLocked())
    {
        change.error = tr("The attachments of the document are locked. Open the document with its password first.");
        return change;
    }

    const bool isEncryption = settings.algorithm != PDFSecurityHandlerFactory::None;

    PDFProgressPhases phases(progress);
    const int preparePhase = phases.addPhase(20, tr("Preparing the document"), false);
    const int encryptPhase = phases.addPhase(30, isEncryption ? tr("Encrypting the document") : tr("Removing the protection"), false);
    const int checkPhase = phases.addPhase(50, tr("Checking the result"), false);
    phases.start();
    phases.beginPhase(preparePhase);

    try
    {
        PDFDocumentBuilder builder(input);

        // R5-5: /ID before the handler - the key of revisions 2 - 4 is computed of /ID[0]
        if (isEncryption)
        {
            settings.id = builder.ensureDocumentId();
        }
        PDFObjectStorage storage = *builder.getStorage();

        // R5-10: no stream keeps a crypt filter of the old encryption
        change.removedCryptFilters = Helper::removeCryptFilters(&storage);

        if (isEncryption)
        {
            // R5-3: every embedded file stream is encrypted as an embedded file
            PDFObjectWalk walk;
            const std::set<PDFObjectReference> embeddedFiles = Helper::findEmbeddedFileStreams(&storage, &walk);
            if (walk.isLimitReached())
            {
                change.error = tr("The document is nested too deeply, its attachments cannot all be found. Nothing was encrypted.");
                return change;
            }
            change.typedEmbeddedFiles = Helper::setEmbeddedFileTypes(&storage, embeddedFiles);
            change.embeddedFiles = int(embeddedFiles.size());

            // File attachments only: the strings stay in the clear, the checksums of the attachments go
            if (settings.encryptContents == PDFSecurityHandlerFactory::EmbeddedFiles)
            {
                change.removedCheckSums = Helper::removeEmbeddedFileCheckSums(&storage, embeddedFiles);
            }
        }

        if (PDFOperationControl::isOperationCanceled(control))
        {
            change.isStopped = true;
            return change;
        }

        if (isEncryption)
        {
            // R5-6, Q5.7: PDF/A forbids the encryption - its declaration goes before the metadata
            // are encrypted, PDF/UA stays
            const PDFDocument prepared(PDFObjectStorage(storage), input->getInfo()->version, input->getSourceDataHash());
            const ConformanceRemoval conformance = removeConformanceDeclaration(&storage, &prepared, Helper::getRemovedStandards());
            if (!conformance.kept.isEmpty())
            {
                change.error = createConformanceFailure(conformance.kept).message;
                return change;
            }
            change.conformanceRemoved = conformance.removed;
        }

        // The whole file is written: the containers of the file structure go, the version stays
        PDFDocument wholeFile = createWholeFileDocument(std::move(storage), input);

        phases.beginPhase(encryptPhase);
        PDFSecurityHandlerPointer handler;
        if (isEncryption)
        {
            // B101: never a document without the encryption the user asked for
            QString errorMessage;
            if (!PDFSecurityHandlerFactory::validate(settings, &errorMessage))
            {
                change.error = errorMessage;
                return change;
            }

            handler = PDFSecurityHandlerFactory::createSecurityHandler(settings, &errorMessage);
            if (!handler || handler->getMode() == EncryptionMode::None)
            {
                change.error = !errorMessage.isEmpty() ? errorMessage : tr("The encryption cannot be created.");
                return change;
            }
        }

        PDFDocumentBuilder secured(&wholeFile);
        secured.setSecurityHandler(handler);
        change.document = PDFDocument(PDFObjectStorage(*secured.getStorage()), input->getInfo()->version, input->getSourceDataHash());

        if (PDFOperationControl::isOperationCanceled(control))
        {
            change.isStopped = true;
            change.document = PDFDocument();
            return change;
        }

        // 4.1 step 4: the result is written and read back as its user will open it
        phases.beginPhase(checkPhase);
        QString errorMessage;
        change.writtenData = writeDocumentData(&change.document, &errorMessage);
        if (change.writtenData.isEmpty())
        {
            change.error = errorMessage;
            change.document = PDFDocument();
            return change;
        }

        const bool isStandard = settings.algorithm == PDFSecurityHandlerFactory::RC4 || settings.algorithm == PDFSecurityHandlerFactory::AES_128 || settings.algorithm == PDFSecurityHandlerFactory::AES_256;
        const QString ownerPassword = settings.ownerPassword.isEmpty() ? settings.userPassword : settings.ownerPassword;
        const size_t pageCount = change.document.getCatalog()->getPageCount();

        struct Check
        {
            QString password;
            bool isOwnerOnly = false;
        };

        std::vector<Check> checks;
        if (!isEncryption)
        {
            checks.push_back({ QString(), false });
        }
        else if (isStandard)
        {
            checks.push_back({ ownerPassword, true });
            checks.push_back({ settings.userPassword, false });
        }
        else
        {
            checks.push_back({ settings.userPassword, false });
        }

        for (const Check& check : checks)
        {
            bool isFirstAttempt = true;
            auto passwordCallback = [&check, &isFirstAttempt](bool* ok) -> QString
            {
                *ok = isFirstAttempt;
                isFirstAttempt = false;
                return check.password;
            };

            PDFDocumentReader reader(nullptr, passwordCallback, false, check.isOwnerOnly);
            if (settings.algorithm == PDFSecurityHandlerFactory::Certificate)
            {
                // R5-1: by the chosen certificate, never by the certificate folders of the user
                reader.setCertificates({ settings.certificate });
            }
            const PDFDocument readBack = reader.readFromBuffer(change.writtenData);
            const PDFSecurityHandler* readHandler = readBack.getStorage().getSecurityHandler();
            const PDFSecurityHandler::AuthorizationResult result = readHandler ? readHandler->getAuthorizationResult() : PDFSecurityHandler::AuthorizationResult::Failed;

            bool isOpened = reader.getReadingResult() == PDFDocumentReader::Result::OK && readBack.getCatalog() && readBack.getCatalog()->getPageCount() == pageCount;
            if (isOpened)
            {
                if (!isEncryption)
                {
                    isOpened = readHandler && readHandler->getMode() == EncryptionMode::None;
                }
                else if (check.isOwnerOnly)
                {
                    isOpened = result == PDFSecurityHandler::AuthorizationResult::OwnerAuthorized;
                }
                else
                {
                    isOpened = result == PDFSecurityHandler::AuthorizationResult::UserAuthorized || result == PDFSecurityHandler::AuthorizationResult::OwnerAuthorized;
                }
            }

            if (!isOpened)
            {
                change.error = isEncryption ? tr("The encrypted document could not be opened again by its password or certificate, so nothing was made.")
                                            : tr("The document without the protection could not be opened again, so nothing was made.");
                change.document = PDFDocument();
                change.writtenData.clear();
                return change;
            }
        }

        phases.finish();
    }
    catch (const PDFException& exception)
    {
        change.error = exception.getMessage();
        change.document = PDFDocument();
        change.writtenData.clear();
    }

    return change;
}

PDFDocumentToolResult PDFDocumentToolEncryption::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolEncryptionHelper;
    using Factory = PDFSecurityHandlerFactory;

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();
    const PDFSecurityHandler* inputHandler = document->getStorage().getSecurityHandler();
    const bool isInputEncrypted = inputHandler && inputHandler->getMode() != EncryptionMode::None;

    // 433:5671: nothing to remove
    if (m_mode == Mode::Unlock && !isInputEncrypted)
    {
        return PDFDocumentToolResult::createNothingToDo(tr("The document is not encrypted: there is no protection to remove."));
    }

    const qint64 sizeBefore = getWholeFileInputSize(input);
    if (sizeBefore < 0)
    {
        return PDFDocumentToolResult::createFailed(tr("The size of the document cannot be measured."));
    }

    const QString inputEncryptionName = getEncryptionName(document);
    Factory::SecuritySettings settings = createSettings(context, parameters, nullptr);

    // The values of the facts, before the passwords go (S5.1: no password is kept beyond the run)
    const Factory::Algorithm algorithm = settings.algorithm;
    const Factory::EncryptContents contents = settings.encryptContents;
    const uint32_t permissions = algorithm == Factory::None ? getAllPermissions() : settings.permissions;

    // File attachments only (7.6.6, /StmF and /StrF Identity): the document opens without the
    // password, only its attachments need it (review RS5SEC L5)
    const bool isAttachmentsOnly = algorithm != Factory::None && contents == Factory::EmbeddedFiles;
    const bool isPasswordGiven = algorithm == Factory::Certificate || !settings.userPassword.isEmpty();
    const bool isOpenPasswordRequired = isPasswordGiven && !isAttachmentsOnly;
    const bool isOwnerPasswordSeparate = algorithm != Factory::Certificate && !settings.ownerPassword.isEmpty() && settings.ownerPassword != settings.userPassword;
    QString holder = settings.certificate.info.getName(PDFCertificateInfo::CommonName);
    if (holder.isEmpty())
    {
        holder = QFileInfo(settings.certificate.pkcs12fileName).completeBaseName();
    }

    Change change = changeEncryption(document, std::move(settings), control, progress);
    if (change.isStopped || PDFOperationControl::isOperationCanceled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }
    if (!change.error.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(change.error);
    }

    PDFDocumentToolResult result;
    const auto [allowedText, notAllowedText] = Helper::getPermissionTexts(permissions);
    const bool isRestricted = permissions != getAllPermissions();
    QString suffix;

    switch (m_mode)
    {
        case Mode::Password:
            // 430:3226
            suffix = QStringLiteral("_protected");
            result.facts.push_back({ QString::fromLatin1(FACT_ENCRYPTION), tr("Encryption"), Helper::getAlgorithmTitle(algorithm) });
            result.facts.push_back({ QString::fromLatin1(FACT_OPEN_PASSWORD), tr("Open password"), getOpenPasswordText(true, isAttachmentsOnly) });
            result.facts.push_back({ QString::fromLatin1(FACT_ENCRYPTED), tr("Encrypted"), Helper::getScopeText(contents) });
            if (isRestricted)
            {
                result.facts.push_back({ QString::fromLatin1(FACT_ALLOWED), tr("Allowed"), allowedText });
                result.facts.push_back({ QString::fromLatin1(FACT_NOT_ALLOWED), tr("Not allowed"), notAllowedText });
            }
            break;

        case Mode::Permissions:
            // 432:4388
            suffix = QStringLiteral("_restricted");
            result.facts.push_back({ QString::fromLatin1(FACT_ALLOWED), tr("Allowed"), allowedText });
            result.facts.push_back({ QString::fromLatin1(FACT_NOT_ALLOWED), tr("Not allowed"), notAllowedText });
            result.facts.push_back({ QString::fromLatin1(FACT_ENCRYPTION), tr("Encryption"), tr("%1 · owner password set").arg(Helper::getAlgorithmTitle(algorithm)) });
            if (isPasswordGiven)
            {
                result.facts.push_back({ QString::fromLatin1(FACT_OPEN_PASSWORD), tr("Open password"), getOpenPasswordText(true, isAttachmentsOnly) });
            }
            if (contents != Factory::All)
            {
                result.facts.push_back({ QString::fromLatin1(FACT_ENCRYPTED), tr("Encrypted"), Helper::getScopeText(contents) });
            }
            break;

        case Mode::Certificate:
            // 433:4721: the name of the holder, never a path
            suffix = QStringLiteral("_encrypted");
            result.facts.push_back({ QString::fromLatin1(FACT_OPENS_FOR), tr("Opens for"), isAttachmentsOnly ? tr("Everyone · attachments: %1 (private key needed)").arg(holder)
                                                                                                                : tr("%1 (private key needed)").arg(holder) });
            result.facts.push_back({ QString::fromLatin1(FACT_ENCRYPTION), tr("Encryption"), Helper::getAlgorithmTitle(algorithm) });
            result.facts.push_back({ QString::fromLatin1(FACT_ENCRYPTED), tr("Encrypted"), Helper::getScopeText(contents) });
            if (isRestricted)
            {
                result.facts.push_back({ QString::fromLatin1(FACT_ALLOWED), tr("Allowed"), allowedText });
                result.facts.push_back({ QString::fromLatin1(FACT_NOT_ALLOWED), tr("Not allowed"), notAllowedText });
            }
            break;

        case Mode::Unlock:
            // 433:5999
            suffix = QStringLiteral("_unprotected");
            result.facts.push_back({ QString::fromLatin1(FACT_ENCRYPTION), tr("Encryption"), tr("None (was %1)").arg(inputEncryptionName) });
            result.facts.push_back({ QString::fromLatin1(FACT_OPEN_PASSWORD), tr("Open password"), getOpenPasswordText(false, false) });
            result.facts.push_back({ QString::fromLatin1(FACT_PERMISSIONS), tr("Permissions"), getAllAllowedText() });
            break;
    }

    // File attachments only of a document without attachments: the run protects nothing (review
    // RS5SEC L5; plan 2.5 and 4.1 name no refusal, so the result says it)
    if (isAttachmentsOnly && change.embeddedFiles == 0)
    {
        result.facts.push_back({ QString::fromLatin1(FACT_NO_ATTACHMENTS), tr("Attachments"), tr("None – the document has no attachments, so nothing is protected") });
    }
    addConformanceFact(&result, change.conformanceRemoved);

    // The data of MCP (plan 4.1): never a password, a key or a path
    const PDFStandardSecurityHandler* newHandler = dynamic_cast<const PDFStandardSecurityHandler*>(change.document.getStorage().getSecurityHandler());
    result.data.insert(QString::fromLatin1(DATA_ALGORITHM), Helper::getAlgorithmId(algorithm));
    result.data.insert(QString::fromLatin1(DATA_REVISION), newHandler ? newHandler->getRevision() : 0);
    result.data.insert(QString::fromLatin1(DATA_SCOPE), algorithm == Factory::None ? QString() : Helper::getScopeId(contents));
    result.data.insert(QString::fromLatin1(DATA_PERMISSIONS), Helper::getPermissionIds(permissions));
    result.data.insert(QString::fromLatin1(DATA_OPEN_PASSWORD_REQUIRED), isOpenPasswordRequired);
    result.data.insert(QString::fromLatin1(DATA_OWNER_PASSWORD_SEPARATE), isOwnerPasswordSeparate);

    finishWholeFileResult(&result, input, std::move(change.document), sizeBefore, qint64(change.writtenData.size()), suffix, WholeFileDocument::Always);

    // The verified file is the file, which is saved: another write of an encrypted document has
    // other random vectors and was never read back (the copy and the overwrite of the result
    // dialog and of MCP save these bytes, PDFAppToolResultWriter::createCopyRequest)
    if (!result.documents.empty())
    {
        result.documents.back().writtenData = std::move(change.writtenData);
    }
    return result;
}

}   // namespace pdf
