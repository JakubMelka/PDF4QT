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

#include "pdfmultimedia.h"
#include "pdfdocument.h"

#include <QtEndian>

namespace pdf
{

PDFSound PDFSound::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFSound result;

    object = storage->getObject(object);
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
        PDFDocumentDataLoaderDecorator loader(storage);
        result.m_fileSpecification = PDFFileSpecification::parse(storage, dictionary->get("F"));
        result.m_samplingRate = loader.readNumberFromDictionary(dictionary, "R", 0.0);
        result.m_channels = loader.readIntegerFromDictionary(dictionary, "C", 1);
        result.m_bitsPerSample = loader.readIntegerFromDictionary(dictionary, "B", 8);
        result.m_format = loader.readEnumByName(dictionary->get("E"), formats.cbegin(), formats.cend(), Format::Raw);
        result.m_soundCompression = loader.readNameFromDictionary(dictionary, "CO");
        result.m_soundCompressionParameters = storage->getObject(dictionary->get("CP"));
        result.m_streamObject = object;
    }

    return result;
}

PDFRendition PDFRendition::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFRendition result;
    object = storage->getObject(object);
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

        PDFDocumentDataLoaderDecorator loader(storage);
        result.m_type = loader.readEnumByName(renditionDictionary->get("S"), types.cbegin(), types.cend(), Type::Invalid);
        result.m_name = loader.readTextStringFromDictionary(renditionDictionary, "N", QString());

        auto readMediaCriteria = [renditionDictionary, storage](const char* entry)
        {
            PDFObject dictionaryObject = storage->getObject(renditionDictionary->get(entry));
            if (dictionaryObject.isDictionary())
            {
                const PDFDictionary* dictionary = dictionaryObject.getDictionary();
                return PDFMediaCriteria::parse(storage, dictionary->get("C"));
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
                data.clip = PDFMediaClip::parse(storage, renditionDictionary->get("C"));
                data.playParameters = PDFMediaPlayParameters::parse(storage, renditionDictionary->get("P"));
                data.screenParameters = PDFMediaScreenParameters::parse(storage, renditionDictionary->get("SP"));

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

PDFMediaMinimumBitDepth PDFMediaMinimumBitDepth::parse(const PDFObjectStorage* storage, PDFObject object)
{
    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        return PDFMediaMinimumBitDepth(loader.readIntegerFromDictionary(dictionary, "V", -1), loader.readIntegerFromDictionary(dictionary, "M", 0));
    }

    return PDFMediaMinimumBitDepth(-1, -1);
}

PDFMediaMinimumScreenSize PDFMediaMinimumScreenSize::parse(const PDFObjectStorage* storage, PDFObject object)
{
    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        std::vector<PDFInteger> values = loader.readIntegerArrayFromDictionary(dictionary, "V");
        if (values.size() == 2)
        {
            return PDFMediaMinimumScreenSize(values[0], values[1], loader.readIntegerFromDictionary(dictionary, "M", 0));
        }
    }

    return  PDFMediaMinimumScreenSize(-1, -1, -1);
}

PDFMediaSoftwareIdentifier PDFMediaSoftwareIdentifier::parse(const PDFObjectStorage* storage, PDFObject object)
{
    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        return PDFMediaSoftwareIdentifier(loader.readTextStringFromDictionary(dictionary, "U", QString()).toLatin1(),
                                          loader.readIntegerArrayFromDictionary(dictionary, "L"),
                                          loader.readIntegerArrayFromDictionary(dictionary, "H"),
                                          loader.readBooleanFromDictionary(dictionary, "LI", true),
                                          loader.readBooleanFromDictionary(dictionary, "HI", true),
                                          loader.readStringArrayFromDictionary(dictionary, "OS"));
    }

    return PDFMediaSoftwareIdentifier(QByteArray(), { }, { }, true, true, { });
}

PDFMediaCriteria PDFMediaCriteria::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFMediaCriteria criteria;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
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
            criteria.m_minimumBitDepth = PDFMediaMinimumBitDepth::parse(storage, dictionary->get("D"));
        }
        if (dictionary->hasKey("Z"))
        {
            criteria.m_minimumScreenSize = PDFMediaMinimumScreenSize::parse(storage, dictionary->get("Z"));
        }
        if (dictionary->hasKey("V"))
        {
            const PDFObject& viewerObject = storage->getObject(dictionary->get("V"));
            if (viewerObject.isArray())
            {
                std::vector<PDFMediaSoftwareIdentifier> viewers;
                const PDFArray* viewersArray = viewerObject.getArray();
                viewers.reserve(viewersArray->getCount());
                for (size_t i = 0; i < viewersArray->getCount(); ++i)
                {
                    viewers.emplace_back(PDFMediaSoftwareIdentifier::parse(storage, viewersArray->getItem(i)));
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

PDFMediaPermissions PDFMediaPermissions::parse(const PDFObjectStorage* storage, PDFObject object)
{
    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
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

PDFMediaPlayers PDFMediaPlayers::parse(const PDFObjectStorage* storage, PDFObject object)
{
    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        auto readPlayers = [storage, dictionary](const char* key)
        {
            std::vector<PDFMediaPlayer> result;

            const PDFObject& playersArrayObject = storage->getObject(dictionary->get(key));
            if (playersArrayObject.isArray())
            {
                const PDFArray* playersArray = playersArrayObject.getArray();
                result.reserve(playersArray->getCount());

                for (size_t i = 0; i < playersArray->getCount(); ++i)
                {
                    result.emplace_back(PDFMediaPlayer::parse(storage, playersArray->getItem(i)));
                }
            }

            return result;
        };

        return PDFMediaPlayers(readPlayers("MU"), readPlayers("A"), readPlayers("NU"));
    }

    return PDFMediaPlayers({ }, { }, { });
}

PDFMediaPlayer PDFMediaPlayer::parse(const PDFObjectStorage* storage, PDFObject object)
{
    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        return PDFMediaPlayer(PDFMediaSoftwareIdentifier::parse(storage, dictionary->get("PID")));
    }
    return PDFMediaPlayer(PDFMediaSoftwareIdentifier(QByteArray(), { }, { }, true, true, { }));
}

PDFMediaOffset PDFMediaOffset::parse(const PDFObjectStorage* storage, PDFObject object)
{
    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        QByteArray S = loader.readNameFromDictionary(dictionary, "S");
        if (S == "T")
        {
            if (const PDFDictionary* timespanDictionary = storage->getDictionaryFromObject(dictionary->get("T")))
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

PDFMediaClip PDFMediaClip::parse(const PDFObjectStorage* storage, PDFObject object)
{
    MediaClipData clipData;
    std::vector<MediaSectionData> sections;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        std::set<PDFObjectReference> usedReferences;
        while (dictionary)
        {
            QByteArray type = loader.readNameFromDictionary(dictionary, "S");
            if (type == "MCD")
            {
                clipData.name = loader.readTextStringFromDictionary(dictionary, "N", QString());

                PDFObject fileSpecificationOrStreamObject = storage->getObject(dictionary->get("D"));
                if (fileSpecificationOrStreamObject.isStream())
                {
                    clipData.dataStream = fileSpecificationOrStreamObject;
                }
                else
                {
                    clipData.fileSpecification = PDFFileSpecification::parse(storage, fileSpecificationOrStreamObject);
                }
                clipData.contentType = loader.readStringFromDictionary(dictionary, "CT");
                clipData.permissions = PDFMediaPermissions::parse(storage, dictionary->get("P"));
                clipData.alternateTextDescriptions = PDFMediaMultiLanguageTexts::parse(storage, dictionary->get("Alt"));
                clipData.players = PDFMediaPlayers::parse(storage, dictionary->get("PL"));

                auto getBaseUrl = [&loader, storage, dictionary](const char* name)
                {
                    if (const PDFDictionary* subdictionary = storage->getDictionaryFromObject(dictionary->get(name)))
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
                sectionData.alternateTextDescriptions = PDFMediaMultiLanguageTexts::parse(storage, dictionary->get("Alt"));

                auto readMediaSectionBeginEnd = [storage, dictionary](const char* name)
                {
                    MediaSectionBeginEnd result;

                    if (const PDFDictionary* subdictionary = storage->getDictionaryFromObject(dictionary->get(name)))
                    {
                        result.offsetBegin = PDFMediaOffset::parse(storage, subdictionary->get("B"));
                        result.offsetEnd = PDFMediaOffset::parse(storage, subdictionary->get("E"));
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
                dictionary = storage->getDictionaryFromObject(next);
                continue;
            }

            dictionary = nullptr;
        }
    }

    return PDFMediaClip(qMove(clipData), qMove(sections));
}

PDFMediaMultiLanguageTexts PDFMediaMultiLanguageTexts::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFMediaMultiLanguageTexts texts;

    object = storage->getObject(object);
    if (object.isArray())
    {
        const PDFArray* array = object.getArray();
        if (array->getCount() % 2 == 0)
        {
            PDFDocumentDataLoaderDecorator loader(storage);
            const size_t pairs = array->getCount() / 2;
            for (size_t i = 0; i < pairs; ++i)
            {
                const PDFObject& languageName = storage->getObject(array->getItem(2 * i));
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

PDFMediaPlayParameters PDFMediaPlayParameters::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFMediaPlayParameters result;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        result.m_players = PDFMediaPlayers::parse(storage, dictionary->get("PL"));

        auto getPlayParameters = [storage, dictionary](const char* name)
        {
            PlayParameters parameters;

            if (const PDFDictionary* subdictionary = storage->getDictionaryFromObject(dictionary->get(name)))
            {
                PDFDocumentDataLoaderDecorator loader(storage);
                parameters.volume = loader.readIntegerFromDictionary(subdictionary, "V", 100);
                parameters.controllerUserInterface = loader.readBooleanFromDictionary(subdictionary, "C", false);
                parameters.fitMode = static_cast<FitMode>(loader.readIntegerFromDictionary(subdictionary, "F", 5));
                parameters.playAutomatically = loader.readBooleanFromDictionary(subdictionary, "A", true);
                parameters.repeat = loader.readNumberFromDictionary(dictionary, "RC", 1.0);

                if (const PDFDictionary* durationDictionary = storage->getDictionaryFromObject(subdictionary->get("D")))
                {
                    constexpr const std::array<std::pair<const char*, Duration>, 3> durations = {
                        std::pair<const char*, Duration>{ "I", Duration::Intrinsic },
                        std::pair<const char*, Duration>{ "F", Duration::Infinity },
                        std::pair<const char*, Duration>{ "T", Duration::Seconds }
                    };
                    parameters.duration = loader.readEnumByName(durationDictionary->get("S"), durations.cbegin(), durations.cend(), Duration::Intrinsic);

                    if (const PDFDictionary* timeDictionary = storage->getDictionaryFromObject(durationDictionary->get("T")))
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

PDFMediaScreenParameters PDFMediaScreenParameters::parse(const PDFObjectStorage* storage, PDFObject object)
{
    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        auto getScreenParameters = [storage, dictionary](const char* name)
        {
            ScreenParameters result;
            if (const PDFDictionary* screenDictionary = storage->getDictionaryFromObject(dictionary->get(name)))
            {
                PDFDocumentDataLoaderDecorator loader(storage);
                result.windowType = static_cast<WindowType>(loader.readIntegerFromDictionary(screenDictionary, "W", 3));
                result.opacity = loader.readNumberFromDictionary(screenDictionary, "O", 1.0);
                result.monitorSpecification = loader.readIntegerFromDictionary(screenDictionary, "M", 0);
                std::vector<PDFReal> rgb = loader.readNumberArrayFromDictionary(screenDictionary, "B", { 1.0, 1.0, 1.0 });
                rgb.resize(3, 1.0);
                result.backgroundColor.setRgbF(rgb[0], rgb[1], rgb[2]);

                if (const PDFDictionary* floatWindowDictionary = storage->getDictionaryFromObject(screenDictionary->get("F")))
                {
                    std::vector<PDFInteger> sizeArray = loader.readIntegerArrayFromDictionary(floatWindowDictionary, "D");
                    sizeArray.resize(2, 0);
                    result.floatingWindowSize = QSize(sizeArray[0], sizeArray[1]);
                    result.floatingWindowReference = static_cast<WindowRelativeTo>(loader.readIntegerFromDictionary(floatWindowDictionary, "RT", 0));
                    result.floatingWindowOffscreenMode = static_cast<OffscreenMode>(loader.readIntegerFromDictionary(floatWindowDictionary, "O", 1));
                    result.floatingWindowHasTitleBar = loader.readBooleanFromDictionary(floatWindowDictionary, "T", true);
                    result.floatingWindowCloseable = loader.readBooleanFromDictionary(floatWindowDictionary, "UC", true);
                    result.floatingWindowResizeMode = static_cast<ResizeMode>(loader.readIntegerFromDictionary(floatWindowDictionary, "R", 0));
                    result.floatingWindowTitle = PDFMediaMultiLanguageTexts::parse(storage, floatWindowDictionary->get("TT"));
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

PDFMovie PDFMovie::parse(const PDFObjectStorage* storage, PDFObject object)
{
    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFMovie result;

        PDFDocumentDataLoaderDecorator loader(storage);
        result.m_movieFile = PDFFileSpecification::parse(storage, dictionary->get("F"));
        std::vector<PDFInteger> windowSizeArray = loader.readIntegerArrayFromDictionary(dictionary, "Aspect");
        if (windowSizeArray.size() == 2)
        {
            result.m_windowSize = QSize(windowSizeArray[0], windowSizeArray[1]);
        }
        result.m_rotationAngle = loader.readIntegerFromDictionary(dictionary, "Rotate", 0);

        PDFObject posterObject = storage->getObject(dictionary->get("Poster"));
        if (posterObject.isBool())
        {
            result.m_showPoster = posterObject.getBool();
        }
        else if (posterObject.isStream())
        {
            result.m_showPoster = true;
            result.m_poster = posterObject;
        }

        return result;
    }

    return PDFMovie();
}

PDFMovieActivation PDFMovieActivation::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFMovieActivation result;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);

        constexpr const std::array<std::pair<const char*, Mode>, 4> modes = {
            std::pair<const char*, Mode>{ "Once", Mode::Once },
            std::pair<const char*, Mode>{ "Open", Mode::Open },
            std::pair<const char*, Mode>{ "Repeat", Mode::Repeat },
            std::pair<const char*, Mode>{ "Palindrome", Mode::Palindrome }
        };

        std::vector<PDFInteger> scale = loader.readIntegerArrayFromDictionary(dictionary, "FWScale");
        if (scale.size() != 2)
        {
            scale.resize(2, 0);
        }
        std::vector<PDFReal> relativePosition = loader.readNumberArrayFromDictionary(dictionary, "FWPosition");
        if (relativePosition.size() != 2)
        {
            relativePosition.resize(2, 0.5);
        }

        result.m_start = parseMovieTime(storage, dictionary->get("Start"));
        result.m_duration = parseMovieTime(storage, dictionary->get("Duration"));
        result.m_rate = loader.readNumberFromDictionary(dictionary, "Rate", 1.0);
        result.m_volume = loader.readNumberFromDictionary(dictionary, "Volume", 1.0);
        result.m_showControls = loader.readBooleanFromDictionary(dictionary, "ShowControls", false);
        result.m_synchronous = loader.readBooleanFromDictionary(dictionary, "Synchronous", false);
        result.m_mode = loader.readEnumByName(dictionary->get("Mode"), modes.cbegin(), modes.cend(), Mode::Once);
        result.m_scaleNumerator = scale[0];
        result.m_scaleDenominator = scale[1];
        result.m_relativeWindowPosition = QPointF(relativePosition[0], relativePosition[1]);
    }

    return result;
}

PDFMovieActivation::MovieTime PDFMovieActivation::parseMovieTime(const PDFObjectStorage* storage, PDFObject object)
{
    MovieTime result;

    object = storage->getObject(object);
    if (object.isInt())
    {
        result.value = object.getInteger();
    }
    else if (object.isString())
    {
        result.value = parseMovieTimeFromString(object.getString());
    }
    else if (object.isArray())
    {
        const PDFArray* objectArray = object.getArray();
        if (objectArray->getCount() == 2)
        {
            PDFDocumentDataLoaderDecorator loader(storage);
            result.unitsPerSecond = loader.readInteger(objectArray->getItem(1), 0);

            object = storage->getObject(objectArray->getItem(0));
            if (object.isInt())
            {
                result.value = object.getInteger();
            }
            else if (object.isString())
            {
                result.value = parseMovieTimeFromString(object.getString());
            }
        }
    }

    return result;
}

PDFInteger PDFMovieActivation::parseMovieTimeFromString(const QByteArray& string)
{
    // According to the specification, the string contains 64-bit signed integer,
    // in big-endian format.
    if (string.size() == sizeof(quint64))
    {
        quint64 result = reinterpret_cast<quint64>(string.data());
        qFromBigEndian<decltype(result)>(&result, qsizetype(sizeof(decltype(result))), &result);
        return static_cast<PDFInteger>(result);
    }

    return 0;
}

}   // namespace pdf
