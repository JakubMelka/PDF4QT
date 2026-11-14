// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "pdftoolaudiobook.h"
#include "pdfdocumenttool_audiobook.h"
#include "pdfqtspeechsynthesizer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace pdftool
{

static PDFToolAudioBook s_audioBookApplication;
static PDFToolAudioBookVoices s_audioBookVoicesApplication;

bool PDFToolAudioBookBase::hasVoiceCriteria(const PDFToolOptions& options)
{
    return !options.textVoiceName.isEmpty() || !options.textVoiceGender.isEmpty() || !options.textVoiceAge.isEmpty() || !options.textVoiceLangCode.isEmpty();
}

std::vector<pdf::PDFSpeechVoice> PDFToolAudioBookBase::filterVoices(const PDFToolOptions& options, const std::vector<pdf::PDFSpeechVoice>& voices, QString* errorMessage)
{
    pdf::PDFSpeechVoiceSelector::Criteria criteria;
    criteria.name = options.textVoiceName;
    criteria.gender = options.textVoiceGender;
    criteria.age = options.textVoiceAge;
    criteria.languageCode = options.textVoiceLangCode;

    // The one selection of Pdf4QtSpeech, voice after voice (an unknown gender or age is an error
    // for every voice: it is reported once)
    std::vector<pdf::PDFSpeechVoice> result;
    for (const pdf::PDFSpeechVoice& voice : voices)
    {
        QString error;
        if (pdf::PDFSpeechVoiceSelector::selectVoice({ voice }, criteria, &error))
        {
            result.push_back(voice);
        }
        else if (errorMessage && errorMessage->isEmpty() && (error.startsWith(QStringLiteral("Unknown"))))
        {
            *errorMessage = error;
        }
    }
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
    std::shared_ptr<pdf::PDFQtSpeechSynthesizerFactory> factory = pdf::PDFQtSpeechSynthesizerFactory::create();
    if (!factory->isAvailable())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("No text-to-speech engine, which can create audio files, is available."), options.outputCodec);
        return ErrorNoText;
    }

    QString errorMessage;
    const std::vector<pdf::PDFSpeechVoice> voices = filterVoices(options, factory->getVoices(), &errorMessage);
    if (!errorMessage.isEmpty())
    {
        PDFConsole::writeError(errorMessage, options.outputCodec);
        return ErrorInvalidArguments;
    }

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("voices", PDFToolTranslationContext::tr("Available voices for given settings:"));
    formatter.endl();

    formatter.beginTable("voices", PDFToolTranslationContext::tr("Voice list"));

    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("name", PDFToolTranslationContext::tr("Name"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("gender", PDFToolTranslationContext::tr("Gender"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("age", PDFToolTranslationContext::tr("Age"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("language-code", PDFToolTranslationContext::tr("Lang. Code"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("language", PDFToolTranslationContext::tr("Language"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("country", PDFToolTranslationContext::tr("Country"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("engine", PDFToolTranslationContext::tr("Engine"), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    const QString engine = factory->getEngine();
    for (const pdf::PDFSpeechVoice& voice : voices)
    {
        const QLocale locale(voice.languageCode);
        QString gender;
        switch (voice.gender)
        {
            case pdf::PDFSpeechVoice::Gender::Male:
                gender = QStringLiteral("male");
                break;
            case pdf::PDFSpeechVoice::Gender::Female:
                gender = QStringLiteral("female");
                break;
            case pdf::PDFSpeechVoice::Gender::Unknown:
                break;
        }
        QString age;
        switch (voice.age)
        {
            case pdf::PDFSpeechVoice::Age::Child:
                age = QStringLiteral("child");
                break;
            case pdf::PDFSpeechVoice::Age::Teenager:
                age = QStringLiteral("teenager");
                break;
            case pdf::PDFSpeechVoice::Age::Adult:
                age = QStringLiteral("adult");
                break;
            case pdf::PDFSpeechVoice::Age::Senior:
                age = QStringLiteral("senior");
                break;
            case pdf::PDFSpeechVoice::Age::Unknown:
                break;
        }

        formatter.beginTableRow("voice");
        formatter.writeTableColumn("name", voice.name, Qt::AlignLeft);
        formatter.writeTableColumn("gender", gender, Qt::AlignLeft);
        formatter.writeTableColumn("age", age, Qt::AlignLeft);
        formatter.writeTableColumn("language-code", voice.languageCode, Qt::AlignLeft);
        formatter.writeTableColumn("language", locale.nativeLanguageName(), Qt::AlignLeft);
        formatter.writeTableColumn("country", locale.nativeTerritoryName(), Qt::AlignLeft);
        formatter.writeTableColumn("engine", engine, Qt::AlignLeft);
        formatter.endTableRow();
    }

    formatter.endTable();

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    return ExitSuccess;
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
    using Tool = pdf::PDFDocumentToolAudioBook;

    if (!options.textSpeechInvalidArgument.isEmpty())
    {
        PDFConsole::writeError(options.textSpeechInvalidArgument, options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (options.textSpeechMarkPageNumbers)
    {
        // The bookmarks of SAPI are not a part of the speech of Qt (owner answer Q4.1)
        PDFConsole::writeError(PDFToolTranslationContext::tr("The option --mark-page-numbers is no longer supported; the page numbers are not marked."), options.outputCodec);
    }

    // B77: the audio file is --output, else next to the document; never replaced without --overwrite
    const QFileInfo documentInfo(options.document);
    const QString outputFile = !options.textSpeechOutputFile.isEmpty() ? QFileInfo(options.textSpeechOutputFile).absoluteFilePath()
                                                                       : QDir(documentInfo.absolutePath()).filePath(documentInfo.completeBaseName() + QStringLiteral(".wav"));
    if (QFileInfo::exists(outputFile) && !options.textSpeechOverwrite)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("The file '%1' already exists. Use --overwrite to replace it.").arg(outputFile), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
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
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());

    std::shared_ptr<pdf::PDFQtSpeechSynthesizerFactory> factory = pdf::PDFQtSpeechSynthesizerFactory::create();
    if (!factory->isAvailable())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("No text-to-speech engine, which can create audio files, is available."), options.outputCodec);
        return ErrorNoText;
    }

    // The voice by its attributes (R4-15), the first matching voice
    QString voiceName;
    if (hasVoiceCriteria(options))
    {
        QString errorMessage;
        const std::vector<pdf::PDFSpeechVoice> voices = filterVoices(options, factory->getVoices(), &errorMessage);
        if (!errorMessage.isEmpty() || voices.empty())
        {
            PDFConsole::writeError(errorMessage.isEmpty() ? PDFToolTranslationContext::tr("No suitable voice found.") : errorMessage, options.outputCodec);
            return ErrorInvalidArguments;
        }
        voiceName = voices.front().name;
    }

    // --say-* are the special items, which are read (R4-15); without --say-struct-act-text and
    // --say-struct-exp-form the text of the page is read, as before
    QStringList readSpecial;
    if (options.textSpeechSayPageNumbers)
    {
        readSpecial << QString::fromLatin1(Tool::SPECIAL_PAGE_NUMBERS);
    }
    if (options.textSpeechSayStructTitles)
    {
        readSpecial << QString::fromLatin1(Tool::SPECIAL_TITLES);
    }
    if (options.textSpeechSayStructAlternativeDescription)
    {
        readSpecial << QString::fromLatin1(Tool::SPECIAL_ALT);
    }
    if (options.textSpeechSayStructExpandedForm)
    {
        readSpecial << QString::fromLatin1(Tool::SPECIAL_EXPANDED);
    }
    if (options.textSpeechSayStructActualText)
    {
        readSpecial << QString::fromLatin1(Tool::SPECIAL_ACTUAL_TEXT);
    }

    pdf::PDFDocumentToolPageSelection selection;
    selection.pages = pdf::PDFDocumentToolPageSelection::createRangeText(pages);

    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_PAGES), selection.toVariant());
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_ORDER), Tool::getOrderId(options.textAnalysisAlgorithm));
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_VOICE), voiceName);
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_RATE), options.textSpeechRate);
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_VOLUME), options.textSpeechVolume);
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_FORMAT), QString::fromLatin1(Tool::FORMAT_WAV));
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_READ_SPECIAL), readSpecial.join(QLatin1Char(',')));

    // The temporary file is in the folder of the target: the result is renamed into it
    pdf::PDFDocumentPointer documentPointer(new pdf::PDFDocument(std::move(document)));
    pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(documentPointer, sourceData, options.document, false));
    context.speechSynthesizerFactory = factory;
    context.temporaryFolder = QFileInfo(outputFile).absolutePath();

    const Tool tool;
    const pdf::PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
    if (!result.isSuccess() || result.outputs.empty() || !result.outputs.front().isTemporaryFile())
    {
        PDFConsole::writeError(result.message.isEmpty() ? PDFToolTranslationContext::tr("The audio book cannot be created.") : result.message, options.outputCodec);
        return result.message == Tool::tr("This document has no text to read.") ? ErrorNoText : ErrorFailedWriteToFile;
    }

    // A new file is renamed (the temporary file is in its folder); --overwrite replaces the existing
    // file by a complete copy (QSaveFile) - the old file stays, when the writing fails
    const pdf::PDFDocumentToolOutputFile& output = result.outputs.front();
    QString errorMessage;
    const bool isWritten = !QFileInfo::exists(outputFile) && output.temporaryFile->moveTo(outputFile, &errorMessage);
    if (!isWritten && !output.temporaryFile->copyTo(outputFile, &errorMessage))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot write the audio file '%1'. %2").arg(outputFile, errorMessage), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolAudioBook::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | VoiceSelector | TextAnalysis | TextSpeech;
}

}   // namespace pdftool
