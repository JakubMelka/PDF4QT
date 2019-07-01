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


#include "pdfoptionalcontent.h"
#include "pdfdocument.h"
#include "pdfexception.h"

namespace pdf
{

PDFOptionalContentProperties PDFOptionalContentProperties::create(const PDFDocument* document, const PDFObject& object)
{
    PDFOptionalContentProperties properties;

    const PDFObject& dereferencedObject = document->getObject(object);
    if (dereferencedObject.isDictionary())
    {
        const PDFDictionary* dictionary = dereferencedObject.getDictionary();
        PDFDocumentDataLoaderDecorator loader(document);
        properties.m_allOptionalContentGroups = loader.readReferenceArrayFromDictionary(dictionary, "OCGs");

        for (const PDFObjectReference& reference : properties.m_allOptionalContentGroups)
        {
            const PDFObject& object = document->getStorage().getObject(reference);
            if (!object.isNull())
            {
                properties.m_optionalContentGroups[reference] = PDFOptionalContentGroup::create(document, object);
            }
        }

        if (dictionary->hasKey("D"))
        {
            properties.m_defaultConfiguration = PDFOptionalContentConfiguration::create(document, dictionary->get("D"));
        }

        if (dictionary->hasKey("Configs"))
        {
            const PDFObject& configsObject = document->getObject(dictionary->get("Configs"));
            if (configsObject.isArray())
            {
                const PDFArray* configsArray = configsObject.getArray();
                properties.m_configurations.reserve(configsArray->getCount());

                for (size_t i = 0, count = configsArray->getCount(); i < count; ++i)
                {
                    properties.m_configurations.emplace_back(PDFOptionalContentConfiguration::create(document, configsArray->getItem(i)));
                }
            }
            else if (!configsObject.isNull())
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid optional content properties."));
            }
        }
    }
    else if (!dereferencedObject.isNull())
    {
        throw PDFParserException(PDFTranslationContext::tr("Invalid optional content properties."));
    }

    return properties;
}

PDFOptionalContentConfiguration PDFOptionalContentConfiguration::create(const PDFDocument* document, const PDFObject& object)
{
    PDFOptionalContentConfiguration configuration;

    const PDFObject& dereferencedObject = document->getObject(object);
    if (dereferencedObject.isDictionary())
    {
        const PDFDictionary* dictionary = dereferencedObject.getDictionary();
        PDFDocumentDataLoaderDecorator loader(document);
        configuration.m_name = loader.readTextStringFromDictionary(dictionary, "Name", QString());
        configuration.m_creator = loader.readTextStringFromDictionary(dictionary, "Creator", QString());

        constexpr const std::array<std::pair<const char*, BaseState>, 3> baseStateEnumValues = {
            std::pair<const char*, BaseState>{ "ON", BaseState::ON },
            std::pair<const char*, BaseState>{ "OFF", BaseState::OFF },
            std::pair<const char*, BaseState>{ "Unchanged", BaseState::Unchanged }
        };
        configuration.m_baseState = loader.readEnumByName(dictionary->get("BaseState"), baseStateEnumValues.cbegin(), baseStateEnumValues.cend(), BaseState::ON);
        configuration.m_OnArray = loader.readReferenceArrayFromDictionary(dictionary, "ON");
        configuration.m_OffArray = loader.readReferenceArrayFromDictionary(dictionary, "OFF");

        if (dictionary->hasKey("Intent"))
        {
            const PDFObject& nameOrNames = document->getObject(dictionary->get("Intent"));

            if (nameOrNames.isName())
            {
                configuration.m_intents = { loader.readName(nameOrNames) };
            }
            else if (nameOrNames.isArray())
            {
                configuration.m_intents = loader.readNameArray(nameOrNames);
            }
            else if (!nameOrNames.isNull())
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid optional content configuration."));
            }
        }

        if (dictionary->hasKey("AS"))
        {
            const PDFObject& asArrayObject = document->getObject(dictionary->get("AS"));
            if (asArrayObject.isArray())
            {
                const PDFArray* asArray = asArrayObject.getArray();
                configuration.m_usageApplications.reserve(asArray->getCount());

                for (size_t i = 0, count = asArray->getCount(); i < count; ++i)
                {
                    configuration.m_usageApplications.emplace_back(createUsageApplication(document, asArray->getItem(i)));
                }
            }
            else if (!asArrayObject.isNull())
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid optional content configuration."));
            }
        }

        configuration.m_order = document->getObject(dictionary->get("Order"));
        if (!configuration.m_order.isArray() && !configuration.m_order.isNull())
        {
            throw PDFParserException(PDFTranslationContext::tr("Invalid optional content configuration."));
        }

        constexpr const std::array<std::pair<const char*, ListMode>, 3> listModeEnumValues = {
            std::pair<const char*, ListMode>{ "AllPages", ListMode::AllPages },
            std::pair<const char*, ListMode>{ "VisiblePages", ListMode::VisiblePages }
        };
        configuration.m_listMode = loader.readEnumByName(dictionary->get("ListMode"), listModeEnumValues.cbegin(), listModeEnumValues.cend(), ListMode::AllPages);

        if (dictionary->hasKey("RBGroups"))
        {
            const PDFObject& rbGroupsObject = document->getObject(dictionary->get("RBGroups"));
            if (rbGroupsObject.isArray())
            {
                const PDFArray* rbGroupsArray = rbGroupsObject.getArray();
                configuration.m_radioButtonGroups.reserve(rbGroupsArray->getCount());

                for (size_t i = 0, count = rbGroupsArray->getCount(); i < count; ++i)
                {
                    configuration.m_radioButtonGroups.emplace_back(loader.readReferenceArray(rbGroupsArray->getItem(i)));
                }
            }
            else if (!rbGroupsObject.isNull())
            {
                throw PDFParserException(PDFTranslationContext::tr("Invalid optional content configuration."));
            }
        }

        configuration.m_locked = loader.readReferenceArrayFromDictionary(dictionary, "Locked");
    }

    return configuration;
}

PDFOptionalContentConfiguration::UsageApplication PDFOptionalContentConfiguration::createUsageApplication(const PDFDocument* document, const PDFObject& object)
{
    UsageApplication result;

    const PDFObject& dereferencedObject = document->getObject(object);
    if (dereferencedObject.isDictionary())
    {
        PDFDocumentDataLoaderDecorator loader(document);
        const PDFDictionary* dictionary = dereferencedObject.getDictionary();
        result.event = loader.readNameFromDictionary(dictionary, "Event");
        result.optionalContengGroups = loader.readReferenceArrayFromDictionary(dictionary, "OCGs");
        result.categories = loader.readNameArrayFromDictionary(dictionary, "Category");
    }

    return result;
}

PDFOptionalContentGroup::PDFOptionalContentGroup() :
    m_usageZoomMin(0),
    m_usageZoomMax(std::numeric_limits<PDFReal>::infinity()),
    m_usagePrintState(OCState::Unknown),
    m_usageViewState(OCState::Unknown),
    m_usageExportState(OCState::Unknown)
{

}

PDFOptionalContentGroup PDFOptionalContentGroup::create(const PDFDocument* document, const PDFObject& object)
{
    PDFOptionalContentGroup result;

    const PDFObject& dereferencedObject = document->getObject(object);
    if (!dereferencedObject.isDictionary())
    {
        throw PDFParserException(PDFTranslationContext::tr("Invalid optional content group."));
    }

    PDFDocumentDataLoaderDecorator loader(document);

    const PDFDictionary* dictionary = dereferencedObject.getDictionary();
    result.m_name = loader.readTextStringFromDictionary(dictionary, "Name", QString());

    if (dictionary->hasKey("Intent"))
    {
        const PDFObject& nameOrNames = document->getObject(dictionary->get("Intent"));

        if (nameOrNames.isName())
        {
            result.m_intents = { loader.readName(nameOrNames) };
        }
        else if (nameOrNames.isArray())
        {
            result.m_intents = loader.readNameArray(nameOrNames);
        }
        else if (!nameOrNames.isNull())
        {
            throw PDFParserException(PDFTranslationContext::tr("Invalid optional content group."));
        }
    }

    const PDFObject& usageDictionaryObject = dictionary->get("Usage");
    if (usageDictionaryObject.isDictionary())
    {
        const PDFDictionary* usageDictionary = usageDictionaryObject.getDictionary();

        result.m_creatorInfo = document->getObject(usageDictionary->get("CreatorInfo"));
        result.m_language = document->getObject(usageDictionary->get("Language"));

        const PDFObject& zoomDictionary = document->getObject(usageDictionary->get("Zoom"));
        if (zoomDictionary.isDictionary())
        {
            result.m_usageZoomMin = loader.readNumberFromDictionary(usageDictionary, "min", result.m_usageZoomMin);
            result.m_usageZoomMax = loader.readNumberFromDictionary(usageDictionary, "max", result.m_usageZoomMax);
        }

        auto readState = [document, usageDictionary, &loader](const char* dictionaryKey, const char* key) -> OCState
        {
            const PDFObject& stateDictionaryObject = document->getObject(usageDictionary->get(dictionaryKey));
            if (stateDictionaryObject.isDictionary())
            {
                const PDFDictionary* stateDictionary = stateDictionaryObject.getDictionary();
                QByteArray stateName = loader.readNameFromDictionary(stateDictionary, key);

                if (stateName == "ON")
                {
                    return OCState::ON;
                }
                if (stateName == "OFF")
                {
                    return OCState::OFF;
                }
            }

            return OCState::Unknown;
        };

        result.m_usageViewState = readState("View", "ViewState");
        result.m_usagePrintState = readState("Print", "PrintState");
        result.m_usageExportState = readState("Export", "ExportState");
    }

    return result;
}

}   // namespace pdf
