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

#include "pdftoolaudiobook.h"

#ifdef Q_OS_WIN

#include <sapi.h>
//#include <sphelper.h>

#pragma comment(lib, "ole32")

namespace pdftool
{

static PDFToolAudioBook s_audioBookApplication;
static PDFToolAudioBookVoices s_audioBookVoicesApplication;

PDFVoiceInfo::PDFVoiceInfo(std::map<QString, QString> properties, ISpVoice* voice) :
    m_properties(qMove(properties)),
    m_voice(voice)
{

}

PDFVoiceInfo::~PDFVoiceInfo()
{
    if (m_voice)
    {
        m_voice->Release();
    }
}

QLocale PDFVoiceInfo::getLocale() const
{
    bool ok = false;
    LCID locale = getLanguage().toInt(&ok, 16);

    if (ok)
    {
        // Language name
        int count = GetLocaleInfoW(locale, LOCALE_SISO639LANGNAME, NULL, 0);
        std::vector<wchar_t> buffer(count, wchar_t());
        GetLocaleInfoW(locale, LOCALE_SISO639LANGNAME, buffer.data(), int(buffer.size()));
        QString languageCode = QString::fromWCharArray(buffer.data());

        // Country name
        count = GetLocaleInfoW(locale, LOCALE_SISO3166CTRYNAME, NULL, 0);
        buffer.resize(count, wchar_t());
        GetLocaleInfoW(locale, LOCALE_SISO3166CTRYNAME, buffer.data(), int(buffer.size()));
        QString countryCode = QString::fromWCharArray(buffer.data());

        return QLocale(QString("%1_%2").arg(languageCode, countryCode));
    }

    return QLocale();
}

QString PDFVoiceInfo::getStringValue(QString key) const
{
    auto it = m_properties.find(key);
    if (it != m_properties.cend())
    {
        return it->second;
    }

    return QString();
}

int PDFToolAudioBookBase::fillVoices(const PDFToolOptions& options, PDFVoiceInfoList& list, bool fillVoicePointers)
{
    int result = ExitSuccess;

    QStringList voiceSelector;
    if (!options.textVoiceName.isEmpty())
    {
        voiceSelector << QString("Name=%1").arg(options.textVoiceName);
    }
    if (!options.textVoiceGender.isEmpty())
    {
        voiceSelector << QString("Gender=%1").arg(options.textVoiceGender);
    }
    if (!options.textVoiceAge.isEmpty())
    {
        voiceSelector << QString("Age=%1").arg(options.textVoiceAge);
    }
    if (!options.textVoiceLangCode.isEmpty())
    {
        voiceSelector << QString("Language=%1").arg(options.textVoiceLangCode);
    }
    QString voiceSelectorString = voiceSelector.join(";");
    LPCWSTR requiredAttributes = !voiceSelectorString.isEmpty() ? (LPCWSTR)voiceSelectorString.utf16() : nullptr;

    ISpObjectTokenCategory* category = nullptr;
    if (!SUCCEEDED(::CoCreateInstance(CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL, __uuidof(ISpObjectTokenCategory), (LPVOID*)&category)))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("SAPI Error: Cannot enumerate SAPI voices."), options.outputCodec);
        return ErrorSAPI;
    }

    if (!SUCCEEDED(category->SetId(SPCAT_VOICES, FALSE)))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("SAPI Error: Cannot enumerate SAPI voices."), options.outputCodec);
        return ErrorSAPI;
    }

    IEnumSpObjectTokens* enumTokensObject = nullptr;
    if (SUCCEEDED(category->EnumTokens(requiredAttributes, NULL, &enumTokensObject)))
    {
        ISpObjectToken* token = nullptr;
        while (SUCCEEDED(enumTokensObject->Next(1, &token, NULL)))
        {
            if (token)
            {
                /* Attributes can be for example:
                 *    Version,
                 *    Language,
                 *    Gender,
                 *    Age,
                 *    Name
                 *    Vendor */

                std::map<QString, QString> properties;

                ISpDataKey* attributes = nullptr;
                if (SUCCEEDED(token->OpenKey(L"Attributes", &attributes)))
                {
                    for (ULONG i = 0; ; ++i)
                    {
                        LPWSTR valueName = NULL;
                        if (SUCCEEDED(attributes->EnumValues(i, &valueName)))
                        {
                            LPWSTR data = NULL;
                            if (SUCCEEDED(attributes->GetStringValue(valueName, &data)))
                            {
                                QString propertyName = QString::fromWCharArray(valueName);
                                QString propertyValue = QString::fromWCharArray(data);
                                if (!propertyValue.isEmpty())
                                {
                                    properties[propertyName] = propertyValue;
                                }
                                ::CoTaskMemFree(data);
                            }

                            ::CoTaskMemFree(valueName);
                        }
                        else
                        {
                            break;
                        }
                    }
                    attributes->Release();
                }

                ISpVoice* voice = nullptr;
                if (fillVoicePointers)
                {
                    token->QueryInterface(__uuidof(ISpVoice), (void**)&voice);
                }

                list.emplace_back(qMove(properties), voice);

                token->Release();
            }
            else
            {
                break;
            }
        }
    }
    else
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("SAPI Error: Cannot enumerate SAPI voices."), options.outputCodec);
        result = ErrorSAPI;
    }

    if (enumTokensObject)
    {
        enumTokensObject->Release();
    }

    if (category)
    {
        category->Release();
    }

    return result;
}

int PDFToolAudioBookBase::showVoiceList(const PDFToolOptions& options)
{
    PDFVoiceInfoList voices;
    int result = fillVoices(options, voices, false);

    PDFOutputFormatter formatter(options.outputStyle, options.outputCodec);
    formatter.beginDocument("voices", PDFToolTranslationContext::tr("Available voices for given settings:"));
    formatter.endl();

    formatter.beginTable("voices", PDFToolTranslationContext::tr("Voice list"));

    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("name", PDFToolTranslationContext::tr("Name"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("gender", PDFToolTranslationContext::tr("Gender"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("age", PDFToolTranslationContext::tr("Age"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("language-code", PDFToolTranslationContext::tr("Lang. Code"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("locale", PDFToolTranslationContext::tr("Locale"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("language", PDFToolTranslationContext::tr("Language"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("country", PDFToolTranslationContext::tr("Country"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("vendor", PDFToolTranslationContext::tr("Vendor"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("version", PDFToolTranslationContext::tr("Version"), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    for (const PDFVoiceInfo& voice : voices)
    {
        QLocale locale = voice.getLocale();
        formatter.beginTableRow("voice");
        formatter.writeTableColumn("name", voice.getName(), Qt::AlignLeft);
        formatter.writeTableColumn("gender", voice.getGender(), Qt::AlignLeft);
        formatter.writeTableColumn("age", voice.getAge(), Qt::AlignLeft);
        formatter.writeTableColumn("language", voice.getLanguage(), Qt::AlignLeft);
        formatter.writeTableColumn("locale", locale.name(), Qt::AlignLeft);
        formatter.writeTableColumn("language", locale.nativeLanguageName(), Qt::AlignLeft);
        formatter.writeTableColumn("country", locale.nativeCountryName(), Qt::AlignLeft);
        formatter.writeTableColumn("vendor", voice.getVendor(), Qt::AlignLeft);
        formatter.writeTableColumn("version", voice.getVersion(), Qt::AlignLeft);
        formatter.endTableRow();
    }

    formatter.endTable();

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    return result;
}

QString PDFToolAudioBookVoices::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "audio-book-voices";

        case Name:
            return PDFToolTranslationContext::tr("Audio book voices");

        case Description:
            return PDFToolTranslationContext::tr("List of available voices for audio book conversion.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolAudioBookVoices::execute(const PDFToolOptions& options)
{
    if (!SUCCEEDED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_SPEED_OVER_MEMORY)))
    {
        return ErrorCOM;
    }

    int returnCode = showVoiceList(options);

    ::CoUninitialize();

    return returnCode;
}

PDFToolAbstractApplication::Options PDFToolAudioBookVoices::getOptionsFlags() const
{
    return ConsoleFormat | VoiceSelector;
}

QString PDFToolAudioBook::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "audio-book";

        case Name:
            return PDFToolTranslationContext::tr("Audio book convertor");

        case Description:
            return PDFToolTranslationContext::tr("Convert your document to a simple audio book.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolAudioBook::execute(const PDFToolOptions& options)
{
    if (!SUCCEEDED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_SPEED_OVER_MEMORY)))
    {
        return ErrorCOM;
    }

    ::CoUninitialize();

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolAudioBook::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | VoiceSelector | TextAnalysis;
}

}   // namespace pdftool

#endif
