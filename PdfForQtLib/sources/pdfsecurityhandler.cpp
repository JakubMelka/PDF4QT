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

namespace pdf
{

PDFSecurityHandlerPointer PDFSecurityHandler::createSecurityHandler(const PDFObject& encryptionDictionaryObject)
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

    return PDFSecurityHandlerPointer(new PDFStandardSecurityHandler(qMove(handler)));
}

}   // namespace pdf
