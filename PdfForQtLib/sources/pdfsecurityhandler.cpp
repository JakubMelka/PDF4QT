//    Copyright (C) 2019 Jakub Melka
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

#include <openssl/rc4.h>
#include <openssl/md5.h>

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

PDFSecurityHandlerPointer PDFSecurityHandler::createSecurityHandler(const PDFObject& encryptionDictionaryObject, const QByteArray& id)
{
    if (encryptionDictionaryObject.isNull())
    {
        return PDFSecurityHandlerPointer(new PDFNoneSecurityHandler());
    }

    if (!encryptionDictionaryObject.isDictionary())
    {
        throw PDFParserException(PDFTranslationContext::tr("Invalid encryption dictionary."));
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
                throw PDFParserException(PDFTranslationContext::tr("Invalid value for entry '%1' in encryption dictionary. Name expected.").arg(QString::fromLatin1(key)));
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
                throw PDFParserException(PDFTranslationContext::tr("Invalid value for entry '%1' in encryption dictionary. Integer expected.").arg(QString::fromLatin1(key)));
            }

            return defaultValue;
        }

        return intObject.getInteger();
    };

    QByteArray filterName = getName(dictionary, "Filter", true);
    if (filterName != "Standard")
    {
        throw PDFParserException(PDFTranslationContext::tr("Unknown security handler."));
    }

    const int V = getInt(dictionary, "V", true);

    // Check V
    if (V < 1 || V > 5)
    {
        throw PDFParserException(PDFTranslationContext::tr("Unsupported version of document encryption (V = %1).").arg(V));
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
    handler.m_cryptFilters["Identity"] = identityFilter;

    if (V == 4 || V == 5)
    {
        const PDFObject& cryptFilterObjects = dictionary->get("CF");
        if (cryptFilterObjects.isDictionary())
        {
            auto parseCryptFilter = [&getName](const PDFObject& object) -> CryptFilter
            {
                if (!object.isDictionary())
                {
                    throw PDFParserException(PDFTranslationContext::tr("Crypt filter is not a dictionary!"));
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
                    throw PDFParserException(PDFTranslationContext::tr("Unsupported encryption algorithm '%1'.").arg(QString::fromLatin1(CFMName)));
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
                    throw PDFParserException(PDFTranslationContext::tr("Unsupported authorization event '%1'.").arg(QString::fromLatin1(authEventName)));
                }

                return filter;
            };

            const PDFDictionary* cryptFilters = cryptFilterObjects.getDictionary();
            for (size_t i = 0, cryptFilterCount = cryptFilters->getCount(); i < cryptFilterCount; ++i)
            {
                handler.m_cryptFilters[cryptFilters->getKey(i)] = parseCryptFilter(cryptFilters->getValue(i));
            }
        }

        // Now, add standard filters
        auto resolveFilter = [&handler](const QByteArray& name)
        {
            auto it = handler.m_cryptFilters.find(name);

            if (it == handler.m_cryptFilters.cend())
            {
                throw PDFParserException(PDFTranslationContext::tr("Uknown crypt filter '%1'.").arg(QString::fromLatin1(name)));
            }

            return it->second;
        };

        handler.m_filterStreams = resolveFilter(getName(dictionary, "StmF", false, "Identity"));
        handler.m_filterStrings = resolveFilter(getName(dictionary, "StrF", false, "Identity"));

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
    if (R < 2 || R > 6 || R == 5)
    {
        throw PDFParserException(PDFTranslationContext::tr("Revision %1 of standard security handler is not supported.").arg(R));
    }
    handler.m_R = R;

    auto readByteArray = [dictionary](const char* key, int size)
    {
        QByteArray result;

        const PDFObject& object = dictionary->get(key);
        if (object.isString())
        {
            result = object.getString();

            if (result.size() != size)
            {
                throw PDFParserException(PDFTranslationContext::tr("Expected %1 characters long string in entry '%2'. Provided length is %3.").arg(size).arg(QString::fromLatin1(key)).arg(result.size()));
            }
        }
        else
        {
            throw PDFParserException(PDFTranslationContext::tr("Expected %1 characters long string in entry '%2'.").arg(size).arg(QString::fromLatin1(key)));
        }

        return result;
    };

    handler.m_O = readByteArray("O", (R != 6) ? 32 : 48);
    handler.m_U = readByteArray("U", (R != 6) ? 32 : 48);

    handler.m_permissions = static_cast<uint32_t>(static_cast<int>(getInt(dictionary, "P", true)));

    if (R == 6)
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
                        return AuthorizationResult::OwnerAuthorized;
                    }
                }

                // Try to authorize user password
                QByteArray fileEncryptionKey = createFileEncryptionKey(password);
                QByteArray U = createEntryValueU_r234(fileEncryptionKey);

                if (U == m_U)
                {
                    // We have authorized owner access
                    return AuthorizationResult::UserAuthorized;
                }

                break;
            }

            case 6:
            {
                // TODO: Implement revision 6 encryption
                return AuthorizationResult::Failed;
            }

            default:
                return AuthorizationResult::Failed;
        }


        // TODO: Handle passwords better - in some revisions, must be in PDFDocEncoding!
        password = getPasswordCallback(&passwordObtained).toUtf8();
    }

    return AuthorizationResult::Cancelled;
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
                throw PDFParserException(PDFTranslationContext::tr("Encryption key length (%1) exceeded maximal value of.").arg(keyByteLength).arg(MD5_DIGEST_LENGTH));
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

        case 6:
        {
            // TODO: Implement revision 6 key
            break;
        }

        default:
        {
            throw PDFParserException(PDFTranslationContext::tr("Revision %1 of standard security handler is not supported.").arg(m_R));
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
            RC4_set_key(&key, fileEncryptionKey.size(), reinterpret_cast<const unsigned char*>(fileEncryptionKey.data()));

            result.resize(static_cast<int>(PDFPasswordPadding.size()));
            RC4(&key, PDFPasswordPadding.size(), PDFPasswordPadding.data(), reinterpret_cast<unsigned char*>(result.data()));
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
            RC4_set_key(&key, fileEncryptionKey.size(), reinterpret_cast<const unsigned char*>(fileEncryptionKey.data()));

            std::array<uint8_t, MD5_DIGEST_LENGTH> encryptedHash;
            RC4(&key, hash.size(), hash.data(), reinterpret_cast<unsigned char*>(encryptedHash.data()));

            QByteArray transformedKey = fileEncryptionKey;
            for (int i = 1; i <= 19; ++i)
            {
                for (int j = 0, keySize = fileEncryptionKey.size(); j < keySize; ++j)
                {
                    transformedKey[j] = static_cast<uint8_t>(fileEncryptionKey[j]) ^ static_cast<uint8_t>(i);
                }

                RC4_set_key(&key, transformedKey.size(), reinterpret_cast<const unsigned char*>(transformedKey.data()));
                RC4(&key, encryptedHash.size(), encryptedHash.data(), reinterpret_cast<unsigned char*>(encryptedHash.data()));
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
            throw PDFParserException(PDFTranslationContext::tr("Revision %1 of standard security handler is not supported.").arg(m_R));
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
        throw PDFParserException(PDFTranslationContext::tr("Encryption key length (%1) exceeded maximal value of.").arg(keyByteLength).arg(MD5_DIGEST_LENGTH));
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
            RC4_set_key(&key, keyByteLength, reinterpret_cast<const unsigned char*>(hash.data()));
            result.resize(m_O.size());
            RC4(&key, m_O.size(), reinterpret_cast<const unsigned char*>(m_O.data()), reinterpret_cast<unsigned char*>(result.data()));
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
                RC4_set_key(&key, transformedKey.size(), reinterpret_cast<const unsigned char*>(transformedKey.data()));
                RC4(&key, buffer.size(), reinterpret_cast<const unsigned char*>(buffer.data()), reinterpret_cast<unsigned char*>(buffer.data()));
            }

            result = buffer;
            break;
        }

        default:
        {
            throw PDFParserException(PDFTranslationContext::tr("Revision %1 of standard security handler is not supported.").arg(m_R));
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

}   // namespace pdf
