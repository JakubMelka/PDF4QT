// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "audiobookcreator.h"

#include <QTextStream>

#ifdef Q_OS_WIN
#include <windows.h>
#include <sapi.h>
#if defined(PDF4QT_USE_PRAGMA_LIB)
#pragma comment(lib, "ole32")
#endif
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
            textStream << trimmedText << Qt::endl;
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
