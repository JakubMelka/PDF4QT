//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "audiobookcreator.h"

#ifdef Q_OS_WIN
#include <sapi.h>
#pragma comment(lib, "ole32")
#endif

namespace pdfplugin
{

AudioBookCreator::AudioBookCreator() :
    m_initialized(false)
{
#ifdef Q_OS_WIN
    auto comResult = ::CoInitialize(nullptr);
    m_initialized = SUCCEEDED(comResult);
#endif
}

AudioBookCreator::~AudioBookCreator()
{
#ifdef Q_OS_WIN
    if (m_initialized)
    {
        ::CoUninitialize();
    }
#endif
}

pdf::PDFOperationResult AudioBookCreator::createAudioBook(const Settings& settings, pdf::PDFDocumentTextFlow& flow)
{
#ifdef Q_OS_WIN
    QString audioString;
    QTextStream textStream(&audioString);

    for (const pdf::PDFDocumentTextFlow::Item& item : flow.getItems())
    {
        QString trimmedText = item.text.trimmed();
        if (!trimmedText.isEmpty())
        {
            textStream << trimmedText << endl;
        }
    }

    auto getVoiceToken = [](const Settings& settings)
    {
        ISpObjectToken* token = nullptr;

        QStringList voiceSelector;
        if (!settings.voiceName.isEmpty())
        {
            voiceSelector << QString("Name=%1").arg(settings.voiceName);
        }
        if (!settings.voiceGender.isEmpty())
        {
            voiceSelector << QString("Gender=%1").arg(settings.voiceGender);
        }
        if (!settings.voiceAge.isEmpty())
        {
            voiceSelector << QString("Age=%1").arg(settings.voiceAge);
        }
        if (!settings.voiceLangCode.isEmpty())
        {
            voiceSelector << QString("Language=%1").arg(settings.voiceLangCode);
        }
        QString voiceSelectorString = voiceSelector.join(";");
        LPCWSTR requiredAttributes = !voiceSelectorString.isEmpty() ? (LPCWSTR)voiceSelectorString.utf16() : nullptr;

        ISpObjectTokenCategory* category = nullptr;
        if (!SUCCEEDED(::CoCreateInstance(CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL, __uuidof(ISpObjectTokenCategory), (LPVOID*)&category)))
        {
            return token;
        }

        if (!SUCCEEDED(category->SetId(SPCAT_VOICES, FALSE)))
        {
            category->Release();
            return token;
        }

        IEnumSpObjectTokens* enumTokensObject = nullptr;
        if (SUCCEEDED(category->EnumTokens(requiredAttributes, NULL, &enumTokensObject)))
        {
            enumTokensObject->Next(1, &token, NULL);
        }

        if (enumTokensObject)
        {
            enumTokensObject->Release();
        }

        if (category)
        {
            category->Release();
        }

        return token;
    };

    ISpObjectToken* voiceToken = getVoiceToken(settings);

    // Do we have any voice?
    if (!voiceToken)
    {
        return tr("No suitable voice found.");
    }

    QString outputFile = settings.audioFileName;
    BSTR outputFileName = (BSTR)outputFile.utf16();

    ISpeechFileStream* stream = nullptr;
    if (!SUCCEEDED(::CoCreateInstance(CLSID_SpFileStream, NULL, CLSCTX_ALL, __uuidof(ISpeechFileStream), (LPVOID*)&stream)))
    {
        voiceToken->Release();
        return tr("Cannot create output stream '%1'.").arg(outputFile);
    }

    ISpVoice* voice = nullptr;
    if (!SUCCEEDED(::CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, __uuidof(ISpVoice), (LPVOID*)&voice)))
    {
        voiceToken->Release();
        stream->Release();
        return tr("Cannot create voice.");
    }

    if (!SUCCEEDED(stream->Open(outputFileName, SSFMCreateForWrite)))
    {
        voiceToken->Release();
        voice->Release();
        stream->Release();
        return tr("Cannot create output stream '%1'.").arg(outputFile);
    }

    if (!SUCCEEDED(voice->SetVoice(voiceToken)))
    {
        voiceToken->Release();
        voice->Release();
        stream->Release();
        return tr("Failed to set requested voice.");
    }

    LPCWSTR stringToSpeak = (LPCWSTR)audioString.utf16();

    voice->SetOutput(stream, FALSE);
    voice->SetRate(settings.rate * 10.0);
    voice->SetVolume(settings.volume * 100.0);
    voice->Speak(stringToSpeak, SPF_PURGEBEFORESPEAK | SPF_PARSE_SAPI, NULL);

    voice->Release();
    stream->Release();
    voiceToken->Release();

    return true;
#else
    return tr("Audio book plugin is unsupported on your system.");
#endif
}

}   // namespace pdfplugin
