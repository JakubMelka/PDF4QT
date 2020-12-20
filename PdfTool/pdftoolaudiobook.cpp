//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftoolaudiobook.h"

#ifdef Q_OS_WIN

#include <QFileInfo>

#include <sapi.h>

#pragma comment(lib, "ole32")

namespace pdftool
{

static PDFToolAudioBook s_audioBookApplication;
static PDFToolAudioBookVoices s_audioBookVoicesApplication;

PDFVoiceInfo::PDFVoiceInfo(std::map<QString, QString> properties, ISpObjectToken* voiceToken) :
    m_properties(qMove(properties)),
    m_voiceToken(voiceToken)
{
    if (m_voiceToken)
    {
        m_voiceToken->AddRef();
    }
}

PDFVoiceInfo::PDFVoiceInfo(PDFVoiceInfo&& other)
{
    std::swap(m_properties, other.m_properties);
    std::swap(m_voiceToken, other.m_voiceToken);
}

PDFVoiceInfo& PDFVoiceInfo::operator=(PDFVoiceInfo&& other)
{
    std::swap(m_properties, other.m_properties);
    std::swap(m_voiceToken, other.m_voiceToken);
    return *this;
}

PDFVoiceInfo::~PDFVoiceInfo()
{
    if (m_voiceToken)
    {
        m_voiceToken->Release();
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

int PDFToolAudioBookBase::fillVoices(const PDFToolOptions& options, PDFVoiceInfoList& list, bool fillVoiceTokenPointers)
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

                if (fillVoiceTokenPointers)
                {
                    list.emplace_back(qMove(properties), token);
                }
                else
                {
                    list.emplace_back(qMove(properties), nullptr);
                }

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


int PDFToolAudioBook::getDocumentTextFlow(const PDFToolOptions& options, pdf::PDFDocumentTextFlow& flow)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData))
    {
        return ErrorDocumentReading;
    }

    QString parseError;
    std::vector<pdf::PDFInteger> pages = options.getPageRange(document.getCatalog()->getPageCount(), parseError, true);

    if (!parseError.isEmpty())
    {
        PDFConsole::writeError(parseError, options.outputCodec);
        return ErrorInvalidArguments;
    }

    pdf::PDFDocumentTextFlowFactory factory;
    flow = factory.create(&document, pages, options.textAnalysisAlgorithm);

    return ExitSuccess;
}

int PDFToolAudioBook::createAudioBook(const PDFToolOptions& options, pdf::PDFDocumentTextFlow& flow)
{
    QString audioString;
    QTextStream textStream(&audioString);

    for (const pdf::PDFDocumentTextFlow::Item& item : flow.getItems())
    {
        if (item.flags.testFlag(pdf::PDFDocumentTextFlow::PageStart) && options.textSpeechMarkPageNumbers)
        {
            textStream << QString("<bookmark mark=\"%1\"/>").arg(item.text) << endl;
        }

        if (!item.text.isEmpty())
        {
            bool showText = (item.flags.testFlag(pdf::PDFDocumentTextFlow::Text)) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::PageStart) && options.textSpeechSayPageNumbers) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::PageEnd) && options.textSpeechSayPageNumbers) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureTitle) && options.textSpeechSayStructTitles) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureAlternativeDescription) && options.textSpeechSayStructAlternativeDescription) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureExpandedForm) && options.textSpeechSayStructExpandedForm) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureActualText) && options.textSpeechSayStructActualText);

            if (showText)
            {
                textStream << item.text << endl;
            }
        }
    }

    PDFVoiceInfoList voices;
    fillVoices(options, voices, true);

    // Do we have any voice?
    if (voices.empty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("No suitable voice found."), options.outputCodec);
        return ErrorSAPI;
    }

    if (!voices.front().getVoiceToken())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid voice."), options.outputCodec);
        return ErrorSAPI;
    }

    QFileInfo info(options.document);
    QString outputFile = QString("%1/%2.%3").arg(info.path(), info.completeBaseName(), options.textSpeechAudioFormat);
    BSTR outputFileName = (BSTR)outputFile.utf16();

    ISpeechFileStream* stream = nullptr;
    if (!SUCCEEDED(::CoCreateInstance(CLSID_SpFileStream, NULL, CLSCTX_ALL, __uuidof(ISpeechFileStream), (LPVOID*)&stream)))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot create output stream '%1'.").arg(outputFile), options.outputCodec);
        return ErrorSAPI;
    }

    ISpVoice* voice = nullptr;
    if (!SUCCEEDED(::CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, __uuidof(ISpVoice), (LPVOID*)&voice)))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot create voice."), options.outputCodec);
        stream->Release();
        return ErrorSAPI;
    }

    if (!SUCCEEDED(stream->Open(outputFileName, SSFMCreateForWrite)))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot create output stream '%1'.").arg(outputFile), options.outputCodec);
        voice->Release();
        stream->Release();
        return ErrorSAPI;
    }

    ISpObjectToken* voiceToken = voices.front().getVoiceToken();
    if (!SUCCEEDED(voice->SetVoice(voiceToken)))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to set requested voice. Default voice will be used."), options.outputCodec);
    }
    voices.clear();

    LPCWSTR stringToSpeak = (LPCWSTR)audioString.utf16();

    voice->SetOutput(stream, FALSE);
    voice->Speak(stringToSpeak, SPF_PURGEBEFORESPEAK | SPF_PARSE_SAPI, NULL);

    voice->Release();
    stream->Release();

    return ExitSuccess;
}

int PDFToolAudioBook::execute(const PDFToolOptions& options)
{
    pdf::PDFDocumentTextFlow textFlow;
    int result = getDocumentTextFlow(options, textFlow);
    if (result != ExitSuccess)
    {
        return result;
    }

    if (textFlow.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("No text extracted to be converted to audio book."), options.outputCodec);
        return ErrorNoText;
    }

    if (!SUCCEEDED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_SPEED_OVER_MEMORY)))
    {
        return ErrorCOM;
    }

    result = createAudioBook(options, textFlow);

    ::CoUninitialize();

    return result;
}

PDFToolAbstractApplication::Options PDFToolAudioBook::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | VoiceSelector | TextAnalysis | TextSpeech;
}

}   // namespace pdftool

#endif
