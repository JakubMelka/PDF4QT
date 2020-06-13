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

#include <array>

namespace pdf
{

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

}   // namespace pdf
