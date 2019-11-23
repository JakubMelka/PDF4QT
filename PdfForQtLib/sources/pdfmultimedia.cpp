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

#include "pdfmultimedia.h"
#include "pdfdocument.h"

namespace pdf
{

PDFSound PDFSound::parse(const PDFDocument* document, PDFObject object)
{
    PDFSound result;

    object = document->getObject(object);
    if (object.isStream())
    {
        const PDFStream* stream = object.getStream();
        const PDFDictionary* dictionary = stream->getDictionary();

        constexpr const std::array<std::pair<const char*, Format>, 4> formats = {
            std::pair<const char*, Format>{ "Raw", Format::Raw },
            std::pair<const char*, Format>{ "Signed", Format::Signed },
            std::pair<const char*, Format>{ "muLaw", Format::muLaw },
            std::pair<const char*, Format>{ "ALaw", Format::ALaw }
        };

        // Jakub Melka: parse the sound without exceptions
        PDFDocumentDataLoaderDecorator loader(document);
        result.m_fileSpecification = PDFFileSpecification::parse(document, dictionary->get("F"));
        result.m_samplingRate = loader.readNumberFromDictionary(dictionary, "R", 0.0);
        result.m_channels = loader.readIntegerFromDictionary(dictionary, "C", 1);
        result.m_bitsPerSample = loader.readIntegerFromDictionary(dictionary, "B", 8);
        result.m_format = loader.readEnumByName(dictionary->get("E"), formats.cbegin(), formats.cend(), Format::Raw);
        result.m_soundCompression = loader.readNameFromDictionary(dictionary, "CO");
        result.m_soundCompressionParameters = document->getObject(dictionary->get("CP"));
        result.m_streamObject = object;
    }

    return result;
}

PDFRendition PDFRendition::parse(const PDFDocument* document, PDFObject object)
{
    PDFRendition result;
    object = document->getObject(object);
    const PDFDictionary* renditionDictionary = nullptr;
    if (object.isDictionary())
    {
        renditionDictionary = object.getDictionary();
    }
    else if (object.isStream())
    {
        renditionDictionary = object.getStream()->getDictionary();
    }

    if (renditionDictionary)
    {
        constexpr const std::array<std::pair<const char*, Type>, 2> types = {
            std::pair<const char*, Type>{ "MR", Type::Media },
            std::pair<const char*, Type>{ "SR", Type::Selector }
        };

        PDFDocumentDataLoaderDecorator loader(document);
        result.m_type = loader.readEnumByName(renditionDictionary->get("S"), types.cbegin(), types.cend(), Type::Invalid);
        result.m_name = loader.readTextStringFromDictionary(renditionDictionary, "N", QString());

        auto readMediaCriteria = [renditionDictionary, document](const char* entry)
        {
            PDFObject dictionaryObject = document->getObject(renditionDictionary->get(entry));
            if (dictionaryObject.isDictionary())
            {
                const PDFDictionary* dictionary = dictionaryObject.getDictionary();
                return PDFMediaCriteria::parse(document, dictionary->get("C"));
            }

            return PDFMediaCriteria();
        };

        result.m_mustHonored = readMediaCriteria("MH");
        result.m_bestEffort = readMediaCriteria("BE");

        switch (result.m_type)
        {
            case Type::Media:
            {
                MediaRenditionData data;
                data.clip = PDFMediaClip::parse(document, renditionDictionary->get("C"));
                data.playParameters = PDFMediaPlayParameters::parse(document, renditionDictionary->get("P"));
                data.screenParameters = PDFMediaScreenParameters::parse(document, renditionDictionary->get("SP"));

                result.m_data = qMove(data);
                break;
            }

            case Type::Selector:
            {
                result.m_data = SelectorRenditionData{ renditionDictionary->get("R") };
                break;
            }

            default:
                break;
        }
    }

    return result;
}

PDFMediaMinimumBitDepth PDFMediaMinimumBitDepth::parse(const PDFDocument* document, PDFObject object)
{
    if (const PDFDictionary* dictionary = document->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(document);
        return PDFMediaMinimumBitDepth(loader.readIntegerFromDictionary(dictionary, "V", -1), loader.readIntegerFromDictionary(dictionary, "M", 0));
    }

    return PDFMediaMinimumBitDepth(-1, -1);
}

PDFMediaMinimumScreenSize PDFMediaMinimumScreenSize::parse(const PDFDocument* document, PDFObject object)
{
    if (const PDFDictionary* dictionary = document->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(document);
        std::vector<PDFInteger> values = loader.readIntegerArrayFromDictionary(dictionary, "V");
        if (values.size() == 2)
        {
            return PDFMediaMinimumScreenSize(values[0], values[1], loader.readIntegerFromDictionary(dictionary, "M", 0));
        }
    }

    return  PDFMediaMinimumScreenSize(-1, -1, -1);
}

PDFMediaSoftwareIdentifier PDFMediaSoftwareIdentifier::parse(const PDFDocument* document, PDFObject object)
{
    if (const PDFDictionary* dictionary = document->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(document);
        return PDFMediaSoftwareIdentifier(loader.readTextStringFromDictionary(dictionary, "U", QString()).toLatin1(),
                                          loader.readIntegerArrayFromDictionary(dictionary, "L"),
                                          loader.readIntegerArrayFromDictionary(dictionary, "H"),
                                          loader.readBooleanFromDictionary(dictionary, "LI", true),
                                          loader.readBooleanFromDictionary(dictionary, "HI", true),
                                          loader.readStringArrayFromDictionary(dictionary, "OS"));
    }

    return PDFMediaSoftwareIdentifier(QByteArray(), { }, { }, true, true, { });
}

PDFMediaCriteria PDFMediaCriteria::parse(const PDFDocument* document, PDFObject object)
{
    PDFMediaCriteria criteria;

    if (const PDFDictionary* dictionary = document->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(document);
        auto readBoolean = [&loader, dictionary](const char* name, std::optional<bool>& value)
        {
            if (dictionary->hasKey(name))
            {
                value = loader.readBooleanFromDictionary(dictionary, name, false);
            }
        };

        readBoolean("A", criteria.m_audioDescriptions);
        readBoolean("C", criteria.m_textCaptions);
        readBoolean("O", criteria.m_audioOverdubs);
        readBoolean("S", criteria.m_subtitles);
        if (dictionary->hasKey("R"))
        {
            criteria.m_bitrate = loader.readIntegerFromDictionary(dictionary, "R", 0);
        }
        if (dictionary->hasKey("D"))
        {
            criteria.m_minimumBitDepth = PDFMediaMinimumBitDepth::parse(document, dictionary->get("D"));
        }
        if (dictionary->hasKey("Z"))
        {
            criteria.m_minimumScreenSize = PDFMediaMinimumScreenSize::parse(document, dictionary->get("Z"));
        }
        if (dictionary->hasKey("V"))
        {
            const PDFObject& viewerObject = document->getObject(dictionary->get("V"));
            if (viewerObject.isArray())
            {
                std::vector<PDFMediaSoftwareIdentifier> viewers;
                const PDFArray* viewersArray = viewerObject.getArray();
                viewers.reserve(viewersArray->getCount());
                for (size_t i = 0; i < viewersArray->getCount(); ++i)
                {
                    viewers.emplace_back(PDFMediaSoftwareIdentifier::parse(document, viewersArray->getItem(i)));
                }
                criteria.m_viewers = qMove(viewers);
            }
        }
        std::vector<QByteArray> pdfVersions = loader.readNameArrayFromDictionary(dictionary, "P");
        if (pdfVersions.size() > 0)
        {
            criteria.m_minimumPdfVersion = qMove(pdfVersions[0]);
            if (pdfVersions.size() > 1)
            {
                criteria.m_maximumPdfVersion = qMove(pdfVersions[1]);
            }
        }
        if (dictionary->hasKey("L"))
        {
            criteria.m_languages = loader.readStringArrayFromDictionary(dictionary, "L");
        }
    }

    return criteria;
}

PDFMediaPermissions PDFMediaPermissions::parse(const PDFDocument* document, PDFObject object)
{
    if (const PDFDictionary* dictionary = document->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(document);
        constexpr const std::array<std::pair<const char*, Permission>, 4> types = {
            std::pair<const char*, Permission>{ "TEMPNEVER", Permission::Never },
            std::pair<const char*, Permission>{ "TEMPEXTRACT", Permission::Extract },
            std::pair<const char*, Permission>{ "TEMPACCESS", Permission::Access },
            std::pair<const char*, Permission>{ "TEMPALWAYS", Permission::Always }
        };

        return PDFMediaPermissions(loader.readEnumByName(dictionary->get("TF"), types.cbegin(), types.cend(), Permission::Never));
    }

    return PDFMediaPermissions(Permission::Never);
}

PDFMediaPlayers PDFMediaPlayers::parse(const PDFDocument* document, PDFObject object)
{
    if (const PDFDictionary* dictionary = document->getDictionaryFromObject(object))
    {
        auto readPlayers = [document, dictionary](const char* key)
        {
            std::vector<PDFMediaPlayer> result;

            const PDFObject& playersArrayObject = document->getObject(dictionary->get(key));
            if (playersArrayObject.isArray())
            {
                const PDFArray* playersArray = playersArrayObject.getArray();
                result.reserve(playersArray->getCount());

                for (size_t i = 0; i < playersArray->getCount(); ++i)
                {
                    result.emplace_back(PDFMediaPlayer::parse(document, playersArray->getItem(i)));
                }
            }

            return result;
        };

        return PDFMediaPlayers(readPlayers("MU"), readPlayers("A"), readPlayers("NU"));
    }

    return PDFMediaPlayers({ }, { }, { });
}

PDFMediaPlayer PDFMediaPlayer::parse(const PDFDocument* document, PDFObject object)
{
    if (const PDFDictionary* dictionary = document->getDictionaryFromObject(object))
    {
        return PDFMediaPlayer(PDFMediaSoftwareIdentifier::parse(document, dictionary->get("PID")));
    }
    return PDFMediaPlayer(PDFMediaSoftwareIdentifier(QByteArray(), { }, { }, true, true, { }));
}

PDFMediaOffset PDFMediaOffset::parse(const PDFDocument* document, PDFObject object)
{
    if (const PDFDictionary* dictionary = document->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(document);
        QByteArray S = loader.readNameFromDictionary(dictionary, "S");
        if (S == "T")
        {
            if (const PDFDictionary* timespanDictionary = document->getDictionaryFromObject(dictionary->get("T")))
            {
                return PDFMediaOffset(Type::Time, TimeData{ loader.readIntegerFromDictionary(timespanDictionary, "V", 0) });
            }
        }
        else if (S == "F")
        {
            return PDFMediaOffset(Type::Frame, FrameData{ loader.readIntegerFromDictionary(dictionary, "F", 0) });
        }
        else if (S == "M")
        {
            return PDFMediaOffset(Type::Marker, MarkerData{ loader.readTextStringFromDictionary(dictionary, "M", QString()) });
        }
    }

    return PDFMediaOffset(Type::Invalid, std::monostate());
}

PDFMediaClip PDFMediaClip::parse(const PDFDocument* document, PDFObject object)
{
    MediaClipData clipData;
    std::vector<MediaSectionData> sections;

    if (const PDFDictionary* dictionary = document->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(document);
        std::set<PDFObjectReference> usedReferences;
        while (dictionary)
        {
            QByteArray type = loader.readNameFromDictionary(dictionary, "S");
            if (type == "MCD")
            {
                clipData.name = loader.readTextStringFromDictionary(dictionary, "N", QString());

                PDFObject fileSpecificationOrStreamObject = document->getObject(dictionary->get("D"));
                if (fileSpecificationOrStreamObject.isStream())
                {
                    clipData.dataStream = fileSpecificationOrStreamObject;
                }
                else
                {
                    clipData.fileSpecification = PDFFileSpecification::parse(document, fileSpecificationOrStreamObject);
                }
                clipData.contentType = loader.readStringFromDictionary(dictionary, "CT");
                clipData.permissions = PDFMediaPermissions::parse(document, dictionary->get("P"));
                clipData.alternateTextDescriptions = PDFMediaMultiLanguageTexts::parse(document, dictionary->get("Alt"));
                clipData.players = PDFMediaPlayers::parse(document, dictionary->get("PL"));

                auto getBaseUrl = [&loader, document, dictionary](const char* name)
                {
                    if (const PDFDictionary* subdictionary = document->getDictionaryFromObject(dictionary->get(name)))
                    {
                        return loader.readStringFromDictionary(subdictionary, "BU");
                    }

                    return QByteArray();
                };
                clipData.m_baseUrlMustHonored = getBaseUrl("MH");
                clipData.m_baseUrlBestEffort = getBaseUrl("BE");
                break;
            }
            else if (type == "MCS")
            {
                MediaSectionData sectionData;
                sectionData.name = loader.readTextStringFromDictionary(dictionary, "N", QString());
                sectionData.alternateTextDescriptions = PDFMediaMultiLanguageTexts::parse(document, dictionary->get("Alt"));

                auto readMediaSectionBeginEnd = [document, dictionary](const char* name)
                {
                    MediaSectionBeginEnd result;

                    if (const PDFDictionary* subdictionary = document->getDictionaryFromObject(dictionary->get(name)))
                    {
                        result.offsetBegin = PDFMediaOffset::parse(document, subdictionary->get("B"));
                        result.offsetEnd = PDFMediaOffset::parse(document, subdictionary->get("E"));
                    }

                    return result;
                };
                sectionData.m_mustHonored = readMediaSectionBeginEnd("MH");
                sectionData.m_bestEffort = readMediaSectionBeginEnd("BE");

                // Jakub Melka: parse next item in linked list
                PDFObject next = dictionary->get("D");
                if (next.isReference())
                {
                    if (usedReferences.count(next.getReference()))
                    {
                        break;
                    }
                    usedReferences.insert(next.getReference());
                }
                dictionary = document->getDictionaryFromObject(next);
                continue;
            }

            dictionary = nullptr;
        }
    }

    return PDFMediaClip(qMove(clipData), qMove(sections));
}

PDFMediaMultiLanguageTexts PDFMediaMultiLanguageTexts::parse(const PDFDocument* document, PDFObject object)
{
    PDFMediaMultiLanguageTexts texts;

    object = document->getObject(object);
    if (object.isArray())
    {
        const PDFArray* array = object.getArray();
        if (array->getCount() % 2 == 0)
        {
            PDFDocumentDataLoaderDecorator loader(document);
            const size_t pairs = array->getCount() / 2;
            for (size_t i = 0; i < pairs; ++i)
            {
                const PDFObject& languageName = document->getObject(array->getItem(2 * i));
                const PDFObject& text = array->getItem(2 * i + 1);

                if (languageName.isString())
                {
                    texts.texts[languageName.getString()] = loader.readTextString(text, QString());
                }
            }
        }
    }

    return texts;
}

PDFMediaPlayParameters PDFMediaPlayParameters::parse(const PDFDocument* document, PDFObject object)
{
    PDFMediaPlayParameters result;

    if (const PDFDictionary* dictionary = document->getDictionaryFromObject(object))
    {
        result.m_players = PDFMediaPlayers::parse(document, dictionary->get("PL"));

        auto getPlayParameters = [document, dictionary](const char* name)
        {
            PlayParameters parameters;

            if (const PDFDictionary* subdictionary = document->getDictionaryFromObject(dictionary->get(name)))
            {
                PDFDocumentDataLoaderDecorator loader(document);
                parameters.volume = loader.readIntegerFromDictionary(subdictionary, "V", 100);
                parameters.controllerUserInterface = loader.readBooleanFromDictionary(subdictionary, "C", false);
                parameters.fitMode = static_cast<FitMode>(loader.readIntegerFromDictionary(subdictionary, "F", 5));
                parameters.playAutomatically = loader.readBooleanFromDictionary(subdictionary, "A", true);
                parameters.repeat = loader.readNumberFromDictionary(dictionary, "RC", 1.0);

                if (const PDFDictionary* durationDictionary = document->getDictionaryFromObject(subdictionary->get("D")))
                {
                    constexpr const std::array<std::pair<const char*, Duration>, 3> durations = {
                        std::pair<const char*, Duration>{ "I", Duration::Intrinsic },
                        std::pair<const char*, Duration>{ "F", Duration::Infinity },
                        std::pair<const char*, Duration>{ "T", Duration::Seconds }
                    };
                    parameters.duration = loader.readEnumByName(durationDictionary->get("S"), durations.cbegin(), durations.cend(), Duration::Intrinsic);

                    if (const PDFDictionary* timeDictionary = document->getDictionaryFromObject(durationDictionary->get("T")))
                    {
                        parameters.durationSeconds = loader.readNumberFromDictionary(timeDictionary, "V", 0.0);
                    }
                }
            }

            return parameters;
        };
        result.m_mustHonored = getPlayParameters("MH");
        result.m_bestEffort = getPlayParameters("BE");
    }

    return result;
}

PDFMediaScreenParameters PDFMediaScreenParameters::parse(const PDFDocument* document, PDFObject object)
{
    if (const PDFDictionary* dictionary = document->getDictionaryFromObject(object))
    {
        auto getScreenParameters = [document, dictionary](const char* name)
        {
            ScreenParameters result;
            if (const PDFDictionary* screenDictionary = document->getDictionaryFromObject(dictionary->get(name)))
            {
                PDFDocumentDataLoaderDecorator loader(document);
                result.windowType = static_cast<WindowType>(loader.readIntegerFromDictionary(screenDictionary, "W", 3));
                result.opacity = loader.readNumberFromDictionary(screenDictionary, "O", 1.0);
                result.monitorSpecification = loader.readIntegerFromDictionary(screenDictionary, "M", 0);
                std::vector<PDFReal> rgb = loader.readNumberArrayFromDictionary(screenDictionary, "B", { 1.0, 1.0, 1.0 });
                rgb.resize(3, 1.0);
                result.backgroundColor.setRgbF(rgb[0], rgb[1], rgb[2]);

                if (const PDFDictionary* floatWindowDictionary = document->getDictionaryFromObject(screenDictionary->get("F")))
                {
                    std::vector<PDFInteger> sizeArray = loader.readIntegerArrayFromDictionary(floatWindowDictionary, "D");
                    sizeArray.resize(2, 0);
                    result.floatingWindowSize = QSize(sizeArray[0], sizeArray[1]);
                    result.floatingWindowReference = static_cast<WindowRelativeTo>(loader.readIntegerFromDictionary(floatWindowDictionary, "RT", 0));
                    result.floatingWindowOffscreenMode = static_cast<OffscreenMode>(loader.readIntegerFromDictionary(floatWindowDictionary, "O", 1));
                    result.floatingWindowHasTitleBar = loader.readBooleanFromDictionary(floatWindowDictionary, "T", true);
                    result.floatingWindowCloseable = loader.readBooleanFromDictionary(floatWindowDictionary, "UC", true);
                    result.floatingWindowResizeMode = static_cast<ResizeMode>(loader.readIntegerFromDictionary(floatWindowDictionary, "R", 0));
                    result.floatingWindowTitle = PDFMediaMultiLanguageTexts::parse(document, floatWindowDictionary->get("TT"));
                    switch (loader.readIntegerFromDictionary(floatWindowDictionary, "P", 4))
                    {
                        case 0:
                            result.floatingWindowAlignment = Qt::AlignTop | Qt::AlignLeft;
                            break;
                        case 1:
                            result.floatingWindowAlignment = Qt::AlignTop | Qt::AlignHCenter;
                            break;
                        case 2:
                            result.floatingWindowAlignment = Qt::AlignTop | Qt::AlignRight;
                            break;
                        case 3:
                            result.floatingWindowAlignment = Qt::AlignVCenter | Qt::AlignLeft;
                            break;
                        case 4:
                        default:
                            result.floatingWindowAlignment = Qt::AlignCenter;
                            break;
                        case 5:
                            result.floatingWindowAlignment =  Qt::AlignVCenter | Qt::AlignRight;
                            break;
                        case 6:
                            result.floatingWindowAlignment = Qt::AlignBottom | Qt::AlignLeft;
                            break;
                        case 7:
                            result.floatingWindowAlignment = Qt::AlignBottom | Qt::AlignHCenter;
                            break;
                        case 8:
                            result.floatingWindowAlignment = Qt::AlignBottom | Qt::AlignRight;
                            break;
                    }
                }
            }

            return result;
        };
        return PDFMediaScreenParameters(getScreenParameters("MH"), getScreenParameters("BE"));
    }

    return PDFMediaScreenParameters();
}

}   // namespace pdf
