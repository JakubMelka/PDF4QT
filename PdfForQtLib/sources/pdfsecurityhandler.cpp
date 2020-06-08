//    Copyright (C) 2019-2020 Jakub Melka
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

#include "pdfsecurityhandler.h"
#include "pdfexception.h"
#include "pdfencoding.h"
#include "pdfvisitor.h"
#include "pdfutils.h"

#include <openssl/rc4.h>
#include <openssl/md5.h>
#include <openssl/aes.h>
#include <openssl/sha.h>

#include <array>

namespace pdf
{

// Padding password
static constexpr std::array<uint8_t, 32> PDFPasswordPadding = {
    0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41,
    0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
    0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80,
    0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A
};

class PDFDecryptObjectVisitor : public PDFAbstractVisitor
{
public:
    explicit PDFDecryptObjectVisitor(const PDFSecurityHandler* securityHandler, PDFObjectReference reference) :
        m_securityHandler(securityHandler),
        m_reference(reference)
    {
        m_objectStack.reserve(32);
    }

    virtual void visitNull() override;
    virtual void visitBool(bool value) override;
    virtual void visitInt(PDFInteger value) override;
    virtual void visitReal(PDFReal value) override;
    virtual void visitString(PDFStringRef string) override;
    virtual void visitName(PDFStringRef name) override;
    virtual void visitArray(const PDFArray* array) override;
    virtual void visitDictionary(const PDFDictionary* dictionary) override;
    virtual void visitStream(const PDFStream* stream) override;
    virtual void visitReference(const PDFObjectReference reference) override;

    PDFObject getDecryptedObject();

private:
    const PDFSecurityHandler* m_securityHandler;
    std::vector<PDFObject> m_objectStack;
    PDFObjectReference m_reference;
};

void PDFDecryptObjectVisitor::visitNull()
{
    m_objectStack.push_back(PDFObject::createNull());
}

void PDFDecryptObjectVisitor::visitBool(bool value)
{
    m_objectStack.push_back(PDFObject::createBool(value));
}

void PDFDecryptObjectVisitor::visitInt(PDFInteger value)
{
    m_objectStack.push_back(PDFObject::createInteger(value));
}

void PDFDecryptObjectVisitor::visitReal(PDFReal value)
{
    m_objectStack.push_back(PDFObject::createReal(value));
}

void PDFDecryptObjectVisitor::visitString(PDFStringRef string)
{
    m_objectStack.push_back(PDFObject::createString(m_securityHandler->decrypt(string.getString(), m_reference, PDFSecurityHandler::EncryptionScope::String)));
}

void PDFDecryptObjectVisitor::visitName(PDFStringRef name)
{
    m_objectStack.push_back(PDFObject::createName(name));
}

void PDFDecryptObjectVisitor::visitArray(const PDFArray* array)
{
    acceptArray(array);

    // We have all objects on the stack
    Q_ASSERT(array->getCount() <= m_objectStack.size());

    auto it = std::next(m_objectStack.cbegin(), m_objectStack.size() - array->getCount());
    std::vector<PDFObject> objects(it, m_objectStack.cend());
    PDFObject object = PDFObject::createArray(std::make_shared<PDFArray>(qMove(objects)));
    m_objectStack.erase(it, m_objectStack.cend());
    m_objectStack.push_back(object);
}

void PDFDecryptObjectVisitor::visitDictionary(const PDFDictionary* dictionary)
{
    Q_ASSERT(dictionary);

    // We must check, if it is or isn't a signature dictionary. If it is,
    // then don't decrypt the Content value. We also don't check, if signature
    // isn't indirectly referenced by reference. Hope it isn't...
    const PDFObject& typeObject = dictionary->get("Type");
    bool isSignatureObject = (typeObject.isName() && typeObject.getString() == "Sig");

    std::vector<PDFDictionary::DictionaryEntry> entries;
    entries.reserve(dictionary->getCount());

    for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
    {
        if (isSignatureObject && dictionary->getKey(i) == "Contents")
        {
            entries.emplace_back(dictionary->getKey(i), dictionary->getValue(i));
        }
        else
        {
            dictionary->getValue(i).accept(this);
            entries.emplace_back(dictionary->getKey(i), m_objectStack.back());
            m_objectStack.pop_back();
        }
    }

    m_objectStack.push_back(PDFObject::createDictionary(std::make_shared<PDFDictionary>(qMove(entries))));
}

void PDFDecryptObjectVisitor::visitStream(const PDFStream* stream)
{
    // Don't decrypt, if it is a Metadata stream and Metadata encryption is turned off
    const PDFDictionary* dictionary = stream->getDictionary();

    const PDFObject& typeObject = dictionary->get("Type");
    bool isMetadata = (typeObject.isName() && typeObject.getString() == "Metadata");

    if (isMetadata && !m_securityHandler->isMetadataEncrypted())
    {
        m_objectStack.push_back(PDFObject::createStream(std::make_shared<PDFStream>(PDFDictionary(*dictionary), QByteArray(*stream->getContent()))));
        return;
    }

    // Decrypt the dictionary
    visitDictionary(dictionary);
    PDFObject dictionaryObject = m_objectStack.back();
    m_objectStack.pop_back();

    // We must also handle situation, that stream has specified Crypt filter.
    // In this case, we must delegate decryption to the stream filters.
    PDFDictionary decryptedDictionary(*dictionaryObject.getDictionary());
    QByteArray decryptedData;
    if (!decryptedDictionary.hasKey("Crypt"))
    {
        decryptedData = m_securityHandler->decrypt(*stream->getContent(), m_reference, PDFSecurityHandler::EncryptionScope::Stream);
    }
    else
    {
        decryptedData = *stream->getContent();
        decryptedDictionary.addEntry(PDFInplaceOrMemoryString(PDFSecurityHandler::OBJECT_REFERENCE_DICTIONARY_NAME), PDFObject::createReference(m_reference));
    }

    m_objectStack.push_back(PDFObject::createStream(std::make_shared<PDFStream>(qMove(decryptedDictionary), qMove(decryptedData))));
}

void PDFDecryptObjectVisitor::visitReference(const PDFObjectReference reference)
{
    m_objectStack.push_back(PDFObject::createReference(reference));
}

PDFObject PDFDecryptObjectVisitor::getDecryptedObject()
{
    Q_ASSERT(m_objectStack.size() == 1);
    return qMove(m_objectStack.back());
}

PDFObject PDFSecurityHandler::decryptObject(const PDFObject& object, PDFObjectReference reference) const
{
    PDFDecryptObjectVisitor visitor(this, reference);
    object.accept(&visitor);
    return visitor.getDecryptedObject();
}

PDFSecurityHandlerPointer PDFSecurityHandler::createSecurityHandler(const PDFObject& encryptionDictionaryObject, const QByteArray& id)
{
    if (encryptionDictionaryObject.isNull())
    {
        return PDFSecurityHandlerPointer(new PDFNoneSecurityHandler());
    }

    if (!encryptionDictionaryObject.isDictionary())
    {
        throw PDFException(PDFTranslationContext::tr("Invalid encryption dictionary."));
    }

    const PDFDictionary* dictionary = encryptionDictionaryObject.getDictionary();

    auto getName = [](const PDFDictionary* dictionary, const char* key, bool required, const char* defaultValue = nullptr) -> QByteArray
    {
        const PDFObject& nameObject = dictionary->get(key);

        if (nameObject.isNull())
        {
            return defaultValue ? QByteArray(defaultValue) : QByteArray();
        }

        if (!nameObject.isName())
        {
            if (required)
            {
                throw PDFException(PDFTranslationContext::tr("Invalid value for entry '%1' in encryption dictionary. Name expected.").arg(QString::fromLatin1(key)));
            }

            return defaultValue ? QByteArray(defaultValue) : QByteArray();
        }

        return nameObject.getString();
    };

    auto getInt = [](const PDFDictionary* dictionary, const char* key, bool required, PDFInteger defaultValue = -1) -> PDFInteger
    {
        const PDFObject& intObject = dictionary->get(key);
        if (!intObject.isInt())
        {
            if (required)
            {
                throw PDFException(PDFTranslationContext::tr("Invalid value for entry '%1' in encryption dictionary. Integer expected.").arg(QString::fromLatin1(key)));
            }

            return defaultValue;
        }

        return intObject.getInteger();
    };

    QByteArray filterName = getName(dictionary, "Filter", true);
    if (filterName != "Standard")
    {
        throw PDFException(PDFTranslationContext::tr("Unknown security handler."));
    }

    const int V = getInt(dictionary, "V", true);

    // Check V
    if (V < 1 || V > 5)
    {
        throw PDFException(PDFTranslationContext::tr("Unsupported version of document encryption (V = %1).").arg(V));
    }

    // Only valid for V == 2 or V == 3, otherwise we set file encryption key length manually
    int Length = 40;

    switch (V)
    {
        case 1:
            Length = 40;
            break;

        case 2:
        case 3:
            Length = getInt(dictionary, "Length", false, 40);
            break;

        case 4:
            Length = 128;
            break;

        case 5:
            Length = 256;
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    // Create standard security handler
    PDFStandardSecurityHandler handler;
    handler.m_V = V;
    handler.m_keyLength = Length;

    // Add "Identity" filter to the filters
    CryptFilter identityFilter;
    identityFilter.type = CryptFilterType::Identity;
    handler.m_cryptFilters[IDENTITY_FILTER_NAME] = identityFilter;

    if (V == 4 || V == 5)
    {
        const PDFObject& cryptFilterObjects = dictionary->get("CF");
        if (cryptFilterObjects.isDictionary())
        {
            auto parseCryptFilter = [Length, &getName, &getInt](const PDFObject& object) -> CryptFilter
            {
                if (!object.isDictionary())
                {
                    throw PDFException(PDFTranslationContext::tr("Crypt filter is not a dictionary!"));
                }
                const PDFDictionary* cryptFilterDictionary = object.getDictionary();

                CryptFilter filter;

                QByteArray CFMName = getName(cryptFilterDictionary, "CFM", false, "None");
                if (CFMName == "None")
                {
                    filter.type = CryptFilterType::None;
                }
                else if (CFMName == "V2")
                {
                    filter.type = CryptFilterType::V2;
                }
                else if (CFMName == "AESV2")
                {
                    filter.type = CryptFilterType::AESV2;
                }
                else if (CFMName == "AESV3")
                {
                    filter.type = CryptFilterType::AESV3;
                }
                else
                {
                    throw PDFException(PDFTranslationContext::tr("Unsupported encryption algorithm '%1'.").arg(QString::fromLatin1(CFMName)));
                }

                QByteArray authEventName = getName(cryptFilterDictionary, "AuthEvent", false, "DocOpen");
                if (authEventName == "DocOpen")
                {
                    filter.authEvent = AuthEvent::DocOpen;
                }
                else if (authEventName == "EFOpen")
                {
                    filter.authEvent = AuthEvent::EFOpen;
                }
                else
                {
                    throw PDFException(PDFTranslationContext::tr("Unsupported authorization event '%1'.").arg(QString::fromLatin1(authEventName)));
                }

                filter.keyLength = getInt(cryptFilterDictionary, "Length", false, Length / 8);

                return filter;
            };

            const PDFDictionary* cryptFilters = cryptFilterObjects.getDictionary();
            for (size_t i = 0, cryptFilterCount = cryptFilters->getCount(); i < cryptFilterCount; ++i)
            {
                handler.m_cryptFilters[cryptFilters->getKey(i).getString()] = parseCryptFilter(cryptFilters->getValue(i));
            }
        }

        // Now, add standard filters
        auto resolveFilter = [&handler](const QByteArray& name)
        {
            auto it = handler.m_cryptFilters.find(name);

            if (it == handler.m_cryptFilters.cend())
            {
                throw PDFException(PDFTranslationContext::tr("Uknown crypt filter '%1'.").arg(QString::fromLatin1(name)));
            }

            return it->second;
        };

        handler.m_filterStreams = resolveFilter(getName(dictionary, "StmF", false, IDENTITY_FILTER_NAME));
        handler.m_filterStrings = resolveFilter(getName(dictionary, "StrF", false, IDENTITY_FILTER_NAME));

        if (dictionary->hasKey("EFF"))
        {
            handler.m_filterEmbeddedFiles = resolveFilter(getName(dictionary, "EFF", true));
        }
        else
        {
            // According to the PDF specification, if 'EFF' entry is omitted, then filter
            // for streams is used.
            handler.m_filterEmbeddedFiles = handler.m_filterStreams;
        }
    }

    int R = getInt(dictionary, "R", true);
    if (R < 2 || R > 6)
    {
        throw PDFException(PDFTranslationContext::tr("Revision %1 of standard security handler is not supported.").arg(R));
    }
    handler.m_R = R;

    handler.m_filterDefault.authEvent = AuthEvent::DocOpen;
    handler.m_filterDefault.keyLength = Length / 8;
    handler.m_filterDefault.type = (R > 4) ? CryptFilterType::AESV3 : CryptFilterType::V2;

    auto readByteArray = [dictionary](const char* key, int size)
    {
        QByteArray result;

        const PDFObject& object = dictionary->get(key);
        if (object.isString())
        {
            result = object.getString();

            if (result.size() != size)
            {
                throw PDFException(PDFTranslationContext::tr("Expected %1 characters long string in entry '%2'. Provided length is %3.").arg(size).arg(QString::fromLatin1(key)).arg(result.size()));
            }
        }
        else
        {
            throw PDFException(PDFTranslationContext::tr("Expected %1 characters long string in entry '%2'.").arg(size).arg(QString::fromLatin1(key)));
        }

        return result;
    };

    handler.m_O = readByteArray("O", (R != 6 && R != 5) ? 32 : 48);
    handler.m_U = readByteArray("U", (R != 6 && R != 5) ? 32 : 48);

    handler.m_permissions = static_cast<uint32_t>(static_cast<int>(getInt(dictionary, "P", true)));

    if (R == 6 || R == 5)
    {
        handler.m_OE = readByteArray("OE", 32);
        handler.m_UE = readByteArray("UE", 32);
        handler.m_Perms = readByteArray("Perms", 16);
    }

    const PDFObject& encryptMetadataObject = dictionary->get("EncryptMetadata");
    if (encryptMetadataObject.isBool())
    {
        handler.m_encryptMetadata = encryptMetadataObject.getBool();
    }

    handler.m_ID = id;

    return PDFSecurityHandlerPointer(new PDFStandardSecurityHandler(qMove(handler)));
}

PDFSecurityHandler::AuthorizationResult PDFStandardSecurityHandler::authenticate(const std::function<QString(bool*)>& getPasswordCallback)
{
    QByteArray password;
    bool passwordObtained = true;

    // Clear the authorization data
    m_authorizationData = AuthorizationData();

    while (passwordObtained)
    {
        switch (m_R)
        {
            case 2:
            case 3:
            case 4:
            {
                // Try to authorize by owner password
                {
                    QByteArray userPassword = createUserPasswordFromOwnerPassword(password);
                    QByteArray fileEncryptionKey = createFileEncryptionKey(userPassword);
                    QByteArray U = createEntryValueU_r234(fileEncryptionKey);

                    if (U == m_U)
                    {
                        // We have authorized owner access
                        m_authorizationData.authorizationResult = AuthorizationResult::OwnerAuthorized;
                        m_authorizationData.fileEncryptionKey = fileEncryptionKey;
                        return AuthorizationResult::OwnerAuthorized;
                    }
                }

                // Try to authorize user password
                QByteArray fileEncryptionKey = createFileEncryptionKey(password);
                QByteArray U = createEntryValueU_r234(fileEncryptionKey);

                if (U == m_U)
                {
                    // We have authorized owner access
                    m_authorizationData.authorizationResult = AuthorizationResult::UserAuthorized;
                    m_authorizationData.fileEncryptionKey = fileEncryptionKey;
                    return AuthorizationResult::UserAuthorized;
                }

                break;
            }

            case 5:
            case 6:
            {
                UserOwnerData_r6 userData = parseParts(m_U);
                UserOwnerData_r6 ownerData = parseParts(m_O);

                auto createHash_r5 = [](const QByteArray& inputData)
                {
                    QByteArray result(SHA256_DIGEST_LENGTH, char(0));
                    SHA256(convertByteArrayToUcharPtr(inputData), inputData.size(), convertByteArrayToUcharPtr(result));
                    return result;
                };

                auto createHash_r56 = [this, &createHash_r5](const QByteArray& input, const QByteArray& inputPassword, bool useUserKey)
                {
                    return (m_R == 5) ? createHash_r5(input) : createHash_r6(input, inputPassword, useUserKey);
                };

                // Try to authorize owner password
                {
                    QByteArray inputData = password + ownerData.validationSalt + m_U;
                    QByteArray hash = createHash_r56(inputData, password, true);

                    if (hash == ownerData.hash)
                    {
                        // We have authorized owner access. Now we must calculate the owner encryption key

                        QByteArray fileEncryptionKeyInputData = password + ownerData.keySalt + m_U;
                        QByteArray fileEncryptionDecryptionKey = createHash_r56(fileEncryptionKeyInputData, password, true);

                        Q_ASSERT(fileEncryptionDecryptionKey.size() == 32);
                        AES_KEY key = { };
                        AES_set_decrypt_key(convertByteArrayToUcharPtr(fileEncryptionDecryptionKey), fileEncryptionDecryptionKey.size() * 8, &key);
                        unsigned char aesInitializationVector[AES_BLOCK_SIZE] = { };
                        m_authorizationData.fileEncryptionKey.resize(m_OE.size());
                        AES_cbc_encrypt(convertByteArrayToUcharPtr(m_OE), convertByteArrayToUcharPtr(m_authorizationData.fileEncryptionKey), m_OE.size(), &key, aesInitializationVector, AES_DECRYPT);

                        m_authorizationData.authorizationResult = AuthorizationResult::OwnerAuthorized;
                    }
                }

                // Try to authorize user password
                if (!m_authorizationData.isAuthorized())
                {
                    QByteArray inputData = password + userData.validationSalt;
                    QByteArray hash = createHash_r56(inputData, password, false);

                    if (hash == userData.hash)
                    {
                        QByteArray fileEncryptionKeyInputData = password + userData.keySalt;
                        QByteArray fileEncryptionDecryptionKey = createHash_r56(fileEncryptionKeyInputData, password, false);

                        Q_ASSERT(fileEncryptionDecryptionKey.size() == 32);
                        AES_KEY key = { };
                        AES_set_decrypt_key(convertByteArrayToUcharPtr(fileEncryptionDecryptionKey), fileEncryptionDecryptionKey.size() * 8, &key);
                        unsigned char aesInitializationVector[AES_BLOCK_SIZE] = { };
                        m_authorizationData.fileEncryptionKey.resize(m_UE.size());
                        AES_cbc_encrypt(convertByteArrayToUcharPtr(m_UE), convertByteArrayToUcharPtr(m_authorizationData.fileEncryptionKey), m_UE.size(), &key, aesInitializationVector, AES_DECRYPT);

                        // We have authorized owner access
                        m_authorizationData.authorizationResult =  AuthorizationResult::UserAuthorized;
                    }
                }

                // Stop, if we authorized the document usage
                if (m_authorizationData.isAuthorized())
                {
                    // According the PDF specification, we must also check, if flags are not manipulated.
                    Q_ASSERT(m_Perms.size() == AES_BLOCK_SIZE);
                    AES_KEY key = { };
                    AES_set_decrypt_key(convertByteArrayToUcharPtr(m_authorizationData.fileEncryptionKey), m_authorizationData.fileEncryptionKey.size() * 8, &key);
                    QByteArray decodedPerms(m_Perms.size(), char(0));
                    AES_ecb_encrypt(convertByteArrayToUcharPtr(m_Perms), convertByteArrayToUcharPtr(decodedPerms), &key, AES_DECRYPT);

                    // 1) Checks, if bytes 9, 10, 11 are 'a', 'd', 'b'
                    if (decodedPerms[9] != 'a' || decodedPerms[10] != 'd' || decodedPerms[11] != 'b')
                    {
                        throw PDFException(PDFTranslationContext::tr("Permissions entry in the Encryption dictionary is invalid."));
                    }

                    // 2) Verify, that bytes 0-3 are valid permissions entry
                    const uint32_t permissions = qFromLittleEndian(*reinterpret_cast<const uint32_t*>(decodedPerms.data()));
                    if (permissions != m_permissions)
                    {
                        throw PDFException(PDFTranslationContext::tr("Security permissions are manipulated. Can't open the document."));
                    }

                    // 3) Verify, that byte 8 is 'T' or 'F' and is equal to EncryptMetadata entry
                    if (decodedPerms[8] != 'T' && decodedPerms[8] != 'F')
                    {
                        throw PDFException(PDFTranslationContext::tr("Security permissions are manipulated. Can't open the document."));
                    }
                    if ((decodedPerms[8] == 'T') != m_encryptMetadata)
                    {
                        throw PDFException(PDFTranslationContext::tr("Security permissions are manipulated. Can't open the document."));
                    }

                    return m_authorizationData.authorizationResult;
                }

                break;
            }

            default:
                return AuthorizationResult::Failed;
        }

        password = adjustPassword(getPasswordCallback(&passwordObtained));
    }

    return AuthorizationResult::Cancelled;
}

QByteArray PDFStandardSecurityHandler::decryptUsingFilter(const QByteArray& data, CryptFilter filter, PDFObjectReference reference) const
{
    QByteArray decryptedData;

    Q_ASSERT(m_authorizationData.isAuthorized());

    struct AES_data
    {
        QByteArray initializationVector;
        QByteArray paddedData;
    };

    auto prepareAES_data = [](const QByteArray& data)
    {
        AES_data result;

        result.initializationVector = data.left(AES_BLOCK_SIZE);

        // This is an error. But to handle it, we resize the vector
        // with arbitrary data.
        if (result.initializationVector.size() < AES_BLOCK_SIZE)
        {
            result.initializationVector.resize(AES_BLOCK_SIZE);
        }

        result.paddedData = data.mid(AES_BLOCK_SIZE);

        // Add padding remainder according to the specification
        int size = result.paddedData.size();
        int paddingRemainder = AES_BLOCK_SIZE - (size % AES_BLOCK_SIZE);

        for (int i = 0; i < paddingRemainder; ++i)
        {
            result.paddedData.push_back(paddingRemainder);
        }

        return result;
    };

    switch (filter.type)
    {
        case CryptFilterType::None:       // The application shall decrypt the data using the security handler
        {
            // This shouldn't occur, because in case the used filter has None value, then default filter
            // is used and default filter can't have this value.
            Q_ASSERT(false);
            break;
        }

        case CryptFilterType::V2:         // Use file encryption key for RC4 algorithm
        {
            std::vector<uint8_t> inputKeyData = convertByteArrayToVector(m_authorizationData.fileEncryptionKey);
            uint32_t objectNumber = qToLittleEndian(static_cast<uint32_t>(reference.objectNumber));
            uint32_t generation = qToLittleEndian(static_cast<uint32_t>(reference.generation));
            inputKeyData.insert(inputKeyData.cend(), { uint8_t(objectNumber & 0xFF), uint8_t((objectNumber >> 8) & 0xFF), uint8_t((objectNumber >> 16) & 0xFF), uint8_t(generation & 0xFF), uint8_t((generation >> 8) & 0xFF) });
            std::vector<uint8_t> objectEncryptionKey(MD5_DIGEST_LENGTH, uint8_t(0));
            MD5(inputKeyData.data(), inputKeyData.size(), objectEncryptionKey.data());

            // Use up to (n + 5) bytes, maximally 16, from the digest as object encryption key
            size_t objectEncryptionKeySize = qMin(filter.keyLength + 5, MD5_DIGEST_LENGTH);
            objectEncryptionKey.resize(objectEncryptionKeySize);

            decryptedData.resize(data.size());

            RC4_KEY key = { };
            RC4_set_key(&key, static_cast<int>(objectEncryptionKey.size()), objectEncryptionKey.data());
            RC4(&key, data.size(), convertByteArrayToUcharPtr(data), convertByteArrayToUcharPtr(decryptedData));

            break;
        }

        case CryptFilterType::AESV2:      // Use file encryption key for AES algorithm
        {
            std::vector<uint8_t> inputKeyData = convertByteArrayToVector(m_authorizationData.fileEncryptionKey);
            uint32_t objectNumber = qToLittleEndian(static_cast<uint32_t>(reference.objectNumber));
            uint32_t generation = qToLittleEndian(static_cast<uint32_t>(reference.generation));
            inputKeyData.insert(inputKeyData.cend(), { uint8_t(objectNumber & 0xFF), uint8_t((objectNumber >> 8) & 0xFF), uint8_t((objectNumber >> 16) & 0xFF), uint8_t(generation & 0xFF), uint8_t((generation >> 8) & 0xFF), 0x73, 0x41, 0x6C, 0x54 });
            std::vector<uint8_t> objectEncryptionKey(MD5_DIGEST_LENGTH, uint8_t(0));
            MD5(inputKeyData.data(), inputKeyData.size(), objectEncryptionKey.data());

            // For AES algorithm, always use 16 bytes key (128 bit encryption mode)

            AES_KEY key = { };
            AES_set_decrypt_key(objectEncryptionKey.data(), static_cast<int>(objectEncryptionKey.size()) * 8, &key);

            AES_data aes_data = prepareAES_data(data);
            if (!aes_data.paddedData.isEmpty())
            {
                decryptedData.resize(aes_data.paddedData.size());
                AES_cbc_encrypt(convertByteArrayToUcharPtr(aes_data.paddedData), convertByteArrayToUcharPtr(decryptedData), aes_data.paddedData.length(), &key, convertByteArrayToUcharPtr(aes_data.initializationVector), AES_DECRYPT);
                decryptedData = decryptedData.left(data.length() - AES_BLOCK_SIZE);
            }

            break;
        }

        case CryptFilterType::AESV3:      // Use file encryption key for AES 256 bit algorithm
        {
            Q_ASSERT(m_authorizationData.fileEncryptionKey.size() == 32);
            AES_KEY key = { };
            AES_set_decrypt_key(convertByteArrayToUcharPtr(m_authorizationData.fileEncryptionKey), static_cast<int>(m_authorizationData.fileEncryptionKey.size()) * 8, &key);

            AES_data aes_data = prepareAES_data(data);
            if (!aes_data.paddedData.isEmpty())
            {
                decryptedData.resize(aes_data.paddedData.size());
                AES_cbc_encrypt(convertByteArrayToUcharPtr(aes_data.paddedData), convertByteArrayToUcharPtr(decryptedData), aes_data.paddedData.length(), &key, convertByteArrayToUcharPtr(aes_data.initializationVector), AES_DECRYPT);
                decryptedData = decryptedData.left(data.length() - AES_BLOCK_SIZE);
            }

            break;
        }

        case CryptFilterType::Identity:   // Don't decrypt anything, use identity function
        {
            decryptedData = data;
            break;
        }
    }

    return decryptedData;
}

QByteArray PDFStandardSecurityHandler::decrypt(const QByteArray& data, PDFObjectReference reference, PDFSecurityHandler::EncryptionScope encryptionScope) const
{
    CryptFilter filter = m_filterDefault;

    switch (encryptionScope)
    {
        case EncryptionScope::String:
        {
            if (m_filterStrings.type != CryptFilterType::None)
            {
                filter = m_filterStrings;
            }

            break;
        }

        case EncryptionScope::Stream:
        {
            if (m_filterStreams.type != CryptFilterType::None)
            {
                filter = m_filterStreams;
            }

            break;
        }

        case EncryptionScope::EmbeddedFile:
        {
            if (m_filterEmbeddedFiles.type != CryptFilterType::None)
            {
                filter = m_filterEmbeddedFiles;
            }

            break;
        }
    }

    return decryptUsingFilter(data, filter, reference);
}

QByteArray PDFStandardSecurityHandler::decryptByFilter(const QByteArray& data, const QByteArray& filterName, PDFObjectReference reference) const
{
    auto it = m_cryptFilters.find(filterName);
    if (it == m_cryptFilters.cend())
    {
        throw PDFException(PDFTranslationContext::tr("Crypt filter '%1' not found.").arg(QString::fromLatin1(filterName)));
    }

    return decryptUsingFilter(data, it->second, reference);
}

QByteArray PDFStandardSecurityHandler::createFileEncryptionKey(const QByteArray& password) const
{
    QByteArray result;

    switch (m_R)
    {
        case 2:
        case 3:
        case 4:
        {
            std::array<uint8_t, 32> paddedPassword = createPaddedPassword32(password);
            uint32_t transformedPermissions = qToLittleEndian(m_permissions);

            MD5_CTX context = { };
            MD5_Init(&context);
            MD5_Update(&context, paddedPassword.data(), paddedPassword.size());
            MD5_Update(&context, m_O.constData(), m_O.size());
            MD5_Update(&context, &transformedPermissions, sizeof(transformedPermissions));
            MD5_Update(&context, m_ID.constData(), m_ID.size());

            if (!m_encryptMetadata)
            {
                constexpr uint32_t value = 0xFFFFFFFF;
                MD5_Update(&context, &value, sizeof(value));
            }

            std::array<uint8_t, MD5_DIGEST_LENGTH> fileEncryptionKey;
            MD5_Final(fileEncryptionKey.data(), &context);

            const int keyByteLength = m_keyLength / 8;
            if (keyByteLength > MD5_DIGEST_LENGTH)
            {
                throw PDFException(PDFTranslationContext::tr("Encryption key length (%1) exceeded maximal value of.").arg(keyByteLength).arg(MD5_DIGEST_LENGTH));
            }

            if (m_R >= 3)
            {
                for (int i = 0; i < 50; ++i)
                {

                    MD5_Init(&context);
                    MD5_Update(&context, fileEncryptionKey.data(), keyByteLength);
                    MD5_Final(fileEncryptionKey.data(), &context);
                }
            }

            result.resize(keyByteLength);
            std::copy_n(fileEncryptionKey.cbegin(), keyByteLength, result.begin());
            break;
        }

        case 5:
        case 6:
        {
            // This function must not be called with revision 5/6
            Q_ASSERT(false);
            break;
        }

        default:
        {
            throw PDFException(PDFTranslationContext::tr("Revision %1 of standard security handler is not supported.").arg(m_R));
        }
    }

    return result;
}

QByteArray PDFStandardSecurityHandler::createEntryValueU_r234(const QByteArray& fileEncryptionKey) const
{
    QByteArray result;

    switch (m_R)
    {
        case 2:
        {
            RC4_KEY key = { };
            RC4_set_key(&key, fileEncryptionKey.size(), convertByteArrayToUcharPtr(fileEncryptionKey));

            result.resize(static_cast<int>(PDFPasswordPadding.size()));
            RC4(&key, PDFPasswordPadding.size(), PDFPasswordPadding.data(), convertByteArrayToUcharPtr(result));
            break;
        }

        case 3:
        case 4:
        {
            std::array<uint8_t, MD5_DIGEST_LENGTH> hash;

            MD5_CTX context = { };
            MD5_Init(&context);
            MD5_Update(&context, PDFPasswordPadding.data(), PDFPasswordPadding.size());
            MD5_Update(&context, m_ID.data(), m_ID.size());
            MD5_Final(hash.data(), &context);

            RC4_KEY key = { };
            RC4_set_key(&key, fileEncryptionKey.size(), convertByteArrayToUcharPtr(fileEncryptionKey));

            std::array<uint8_t, MD5_DIGEST_LENGTH> encryptedHash;
            RC4(&key, hash.size(), hash.data(), encryptedHash.data());

            QByteArray transformedKey = fileEncryptionKey;
            for (int i = 1; i <= 19; ++i)
            {
                for (int j = 0, keySize = fileEncryptionKey.size(); j < keySize; ++j)
                {
                    transformedKey[j] = static_cast<uint8_t>(fileEncryptionKey[j]) ^ static_cast<uint8_t>(i);
                }

                RC4_set_key(&key, transformedKey.size(), convertByteArrayToUcharPtr(transformedKey));
                RC4(&key, encryptedHash.size(), encryptedHash.data(), encryptedHash.data());
            }

            // We do a hack here. In the PDF's specification, it is written, that arbitrary 16 bytes
            // are appended to the 16 bytes result. We use the last 16 bytes of the U entry, because we
            // want to compare byte arrays entirely (otherwise we must compare only 16 bytes to authenticate
            // user password).
            result = m_U;
            std::copy_n(encryptedHash.begin(), encryptedHash.size(), result.begin());
            break;
        }

        default:
        {
            throw PDFException(PDFTranslationContext::tr("Revision %1 of standard security handler is not supported.").arg(m_R));
        }
    }

    return result;
}

QByteArray PDFStandardSecurityHandler::createUserPasswordFromOwnerPassword(const QByteArray& password) const
{
    QByteArray result;

    std::array<uint8_t, 32> paddedPassword = createPaddedPassword32(password);
    std::array<uint8_t, MD5_DIGEST_LENGTH> hash;

    MD5_CTX context = { };
    MD5_Init(&context);
    MD5_Update(&context, paddedPassword.data(), paddedPassword.size());
    MD5_Final(hash.data(), &context);

    const int keyByteLength = m_keyLength / 8;
    if (keyByteLength > MD5_DIGEST_LENGTH)
    {
        throw PDFException(PDFTranslationContext::tr("Encryption key length (%1) exceeded maximal value of.").arg(keyByteLength).arg(MD5_DIGEST_LENGTH));
    }

    if (m_R >= 3)
    {
        for (int i = 0; i < 50; ++i)
        {

            MD5_Init(&context);
            MD5_Update(&context, hash.data(), keyByteLength);
            MD5_Final(hash.data(), &context);
        }
    }

    switch (m_R)
    {
        case 2:
        {
            RC4_KEY key = { };
            RC4_set_key(&key, keyByteLength, hash.data());
            result.resize(m_O.size());
            RC4(&key, m_O.size(), convertByteArrayToUcharPtr(m_O), convertByteArrayToUcharPtr(result));
            break;
        }

        case 3:
        case 4:
        {
            QByteArray buffer = m_O;
            QByteArray transformedKey;
            transformedKey.resize(keyByteLength);
            std::copy_n(hash.data(), keyByteLength, transformedKey.data());

            for (int i = 19; i >= 0; --i)
            {
                for (int j = 0, keySize = transformedKey.size(); j < keySize; ++j)
                {
                    transformedKey[j] = static_cast<uint8_t>(hash[j]) ^ static_cast<uint8_t>(i);
                }

                RC4_KEY key = { };
                RC4_set_key(&key, transformedKey.size(), convertByteArrayToUcharPtr(transformedKey));
                RC4(&key, buffer.size(), convertByteArrayToUcharPtr(buffer), convertByteArrayToUcharPtr(buffer));
            }

            result = buffer;
            break;
        }

        default:
        {
            throw PDFException(PDFTranslationContext::tr("Revision %1 of standard security handler is not supported.").arg(m_R));
        }
    }

    return result;
}

std::array<uint8_t, 32> PDFStandardSecurityHandler::createPaddedPassword32(const QByteArray& password) const
{
    std::array<uint8_t, 32> result = { };

    int copiedBytes = qMin<int>(static_cast<int>(result.size()), password.size());
    auto it = result.begin();

    for (int i = 0; i < copiedBytes; ++i)
    {
        *it++ = static_cast<uint8_t>(password[i]);
    }

    auto itPadding = PDFPasswordPadding.cbegin();
    for (; it != result.cend();)
    {
        Q_ASSERT(itPadding != PDFPasswordPadding.cend());
        *it++ = *itPadding++;
    }

    return result;
}

QByteArray PDFStandardSecurityHandler::createHash_r6(const QByteArray& input, const QByteArray& inputPassword, bool useUserKey) const
{
    QByteArray result;

    // First compute sha-256 digest of the input
    std::array<uint8_t, SHA256_DIGEST_LENGTH> inputDigest = { };
    SHA256(convertByteArrayToUcharPtr(input), input.size(), inputDigest.data());
    std::vector<uint8_t> K(inputDigest.cbegin(), inputDigest.cend());

    // Fill the user key, if we use it
    std::vector<uint8_t> userKey;
    if (useUserKey)
    {
        userKey.resize(m_U.size());
        std::copy_n(m_U.constData(), m_U.size(), userKey.begin());
    }
    const size_t userKeySize = userKey.size();

    // Fill the input password
    std::vector<uint8_t> password(inputPassword.constData(), inputPassword.constData() + inputPassword.size());
    const size_t passwordSize = password.size();

    std::vector<uint8_t> K1;
    std::vector<uint8_t> E;

    int round = 0;
    while (round < 64 || round < E.back() + 32)
    {
        const size_t blockCount = 64;
        const size_t KSize = K.size();
        const size_t sequenceSize = passwordSize + KSize + userKeySize;
        const size_t totalSize = blockCount * sequenceSize;

        // Resize the arrays
        K1.resize(totalSize);
        E.resize(totalSize);

        // a) fill the input array K1 with data
        auto it = K1.begin();
        for (size_t i = 0; i < blockCount; ++i)
        {
            std::copy_n(password.cbegin(), passwordSize, it);
            std::advance(it, passwordSize);

            std::copy_n(K.cbegin(), KSize, it);
            std::advance(it, KSize);

            std::copy_n(userKey.cbegin(), userKeySize, it);
            std::advance(it, userKeySize);
        }
        Q_ASSERT(it == K1.cend());
        Q_ASSERT(K.size() >= 32);

        // b) encrypt K1 with AES-128 in CBC mode, first 16 bytes of K is key,
        //    second 16 bytes in K is initialization vector for AES algorithm.
        AES_KEY key = { };
        AES_set_encrypt_key(K.data(), 128, &key);
        AES_cbc_encrypt(K1.data(), E.data(), K1.size(), &key, K.data() + 16, AES_ENCRYPT);

        // c) we take first 16 bytes from E as unsigned 128 bit big-endian integer and compute
        //    remainder modulo 3. Then we decide which SHA function we will use.

        // We can't directly modulo 128 bit unsigned number, because we do not have 128 bit arithmetic (yet).
        // We will use following trick from https://math.stackexchange.com/questions/2727954/bit-representation-and-divisibility-by-3
        //
        //      2^n mod 3 = 2 for n = 1, 3, 5, 7, 9, ...
        //      2^n mod 3 = 1 for n = 0, 2, 4, 6, 8, ...
        //
        // Also, it doesn't matter the endianity of the numbers, becase for example, when we change endianity of 16 bit
        // numbers, then bits 0-7 became 8-15, so even/odd bits become also even/odd.

        int remainderAccumulator = 0;
        for (size_t i = 0; i < 16; ++i)
        {
            uint8_t byte = E[i];

            int currentRemainder = 1;
            for (uint8_t i = 0; i < 8; ++i)
            {
                if ((byte >> i) & 1)
                {
                    remainderAccumulator += currentRemainder;
                }

                // We alternate the remainder 1, 2, 1, 2, 1, 2, ...
                currentRemainder = 3 - currentRemainder;
            }
        }
        remainderAccumulator = remainderAccumulator % 3;

        // d) according to the remainder, decide, which function we will use
        switch (remainderAccumulator)
        {
            case 0:
            {
                K.resize(SHA256_DIGEST_LENGTH);
                SHA256(E.data(), E.size(), K.data());
                break;
            }

            case 1:
            {
                K.resize(SHA384_DIGEST_LENGTH);
                SHA384(E.data(), E.size(), K.data());
                break;
            }

            case 2:
            {
                K.resize(SHA512_DIGEST_LENGTH);
                SHA512(E.data(), E.size(), K.data());
                break;
            }

            default:
            {
                // Invalid value, can't occur
                Q_ASSERT(false);
                break;
            }
        }

        ++round;
    }

    Q_ASSERT(K.size() >= 32);

    // Clamp result to 32 bytes
    result.resize(32);
    std::copy_n(K.data(), 32, result.data());
    return result;
}

PDFStandardSecurityHandler::UserOwnerData_r6 PDFStandardSecurityHandler::parseParts(const QByteArray& data) const
{
    UserOwnerData_r6 result;
    Q_ASSERT(data.size() == 48);

    result.hash = data.left(32);
    result.validationSalt = data.mid(32, 8);
    result.keySalt = data.mid(40, 8);

    return result;
}

QByteArray PDFStandardSecurityHandler::adjustPassword(const QString& password)
{
    QByteArray result;

    switch (m_R)
    {
        case 2:
        case 3:
        case 4:
        {
            // According to the PDF specification, convert string to PDFDocEncoding encoding
            result = PDFEncoding::convertToEncoding(password, PDFEncoding::Encoding::PDFDoc);
            break;
        }

        case 5:
        case 6:
        {
            // According to the PDF specification, use SASLprep profile for stringprep RFC 4013, please see these websites:
            //      - RFC 4013: https://tools.ietf.org/html/rfc4013 (SASLprep profile for stringprep algorithm)
            //      - RFC 3454: https://tools.ietf.org/html/rfc3454 (stringprep algorithm - preparation of internationalized strings)
            //
            // Note: we don't do checks according the RFC 4013, just use the mapping and normalize string in KC

            QString preparedPassword;
            preparedPassword.reserve(password.size());

            // RFC 4013 Section 2.1, use mapping

            for (const QChar character : password)
            {
                if (isUnicodeMappedToNothing(character.unicode()))
                {
                    // Mapped to nothing
                    continue;
                }

                if (isUnicodeNonAsciiSpaceCharacter(character.unicode()))
                {
                    // Map to space character
                    preparedPassword += QChar(QChar::Space);
                }
                else
                {
                    preparedPassword += character;
                }
            }

            // RFC 4013, Section 2.2, normalization to KC
            preparedPassword = preparedPassword.normalized(QString::NormalizationForm_KC);

            // We don't do other checks. We will transform password to the UTF-8 encoding
            // and according the PDF specification, we take only first 127 characters.
            result = preparedPassword.toUtf8().left(127);
        }

        default:
            break;
    }

    return result;
}

bool PDFStandardSecurityHandler::isUnicodeNonAsciiSpaceCharacter(ushort unicode)
{
    switch (unicode)
    {
        case 0x00A0:
        case 0x1680:
        case 0x2000:
        case 0x2001:
        case 0x2002:
        case 0x2003:
        case 0x2004:
        case 0x2005:
        case 0x2006:
        case 0x2007:
        case 0x2008:
        case 0x2009:
        case 0x200A:
        case 0x200B:
        case 0x202F:
        case 0x205F:
        case 0x3000:
            return true;

        default:
            return false;
    }
}

bool PDFStandardSecurityHandler::isUnicodeMappedToNothing(ushort unicode)
{
    switch (unicode)
    {
        case 0x00AD:
        case 0x034F:
        case 0x1806:
        case 0x180B:
        case 0x180C:
        case 0x180D:
        case 0x200B:
        case 0x200C:
        case 0x200D:
            return true;

        default:
            return false;
    }
}

}   // namespace pdf
