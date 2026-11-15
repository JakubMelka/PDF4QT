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

#include "pdfdocumenttool_audiobook.h"
#include "pdfaudiofilewriter.h"
#include "pdfdocument.h"
#include "pdfoperationcontrol.h"
#include "pdfprogress.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>

#include <cmath>

#include "pdfdbgheap.h"

namespace pdf
{

/// Helpers of the tool Create Audio Book
class PDFDocumentToolAudioBookHelper
{
public:
    PDFDocumentToolAudioBookHelper() = delete;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// The kinds of readSpecial in the order of the documentation
    static QStringList getKinds()
    {
        using Tool = PDFDocumentToolAudioBook;
        return { name(Tool::SPECIAL_PAGE_NUMBERS), name(Tool::SPECIAL_TITLES), name(Tool::SPECIAL_ALT),
                 name(Tool::SPECIAL_EXPANDED), name(Tool::SPECIAL_ACTUAL_TEXT), name(Tool::SPECIAL_ARTIFACTS) };
    }

    /// The regular expression of skip; invalid (with \p errorMessage) for a wrong pattern, an empty
    /// pattern gives an invalid expression without an error (nothing is skipped)
    static QRegularExpression getSkipExpression(const QVariantMap& parameters, QString* errorMessage)
    {
        const QString pattern = parameters.value(name(PDFDocumentToolAudioBook::PARAMETER_SKIP)).toString();
        if (pattern.isEmpty())
        {
            return QRegularExpression();
        }

        const QRegularExpression expression = PDFDocumentToolAudioBook::createExpression(pattern);
        if (!expression.isValid() && errorMessage)
        {
            *errorMessage = PDFDocumentToolAudioBook::tr("The regular expression of skip is not valid: %1").arg(expression.errorString());
        }
        return expression;
    }

    /// The item is skipped by the expression of skip
    static bool isSkipped(const QRegularExpression& expression, const QString& text)
    {
        return expression.isValid() && !expression.pattern().isEmpty() && expression.match(text).hasMatch();
    }

    /// One item of the run
    struct ReadItem
    {
        QString text;
        PDFInteger pageIndex = -1;
        QString language;
    };

    /// "Microsoft Zira · normal speed" (341:11159)
    static QString getVoiceText(const QString& voice, double rate)
    {
        QString speed = PDFDocumentToolAudioBook::tr("normal speed");
        if (rate < -0.005)
        {
            speed = PDFDocumentToolAudioBook::tr("slower speed");
        }
        else if (rate > 0.005)
        {
            speed = PDFDocumentToolAudioBook::tr("faster speed");
        }
        return PDFDocumentToolAudioBook::tr("%1 · %2").arg(voice.isEmpty() ? PDFDocumentToolAudioBook::tr("Default voice") : voice, speed);
    }
};

PDFDocumentToolAudioBookStream::PDFDocumentToolAudioBookStream(PDFDocumentTextFlow flow) :
    m_flow(std::make_shared<const PDFDocumentTextFlow>(std::move(flow)))
{

}

PDFDocumentToolAudioBookStream::PDFDocumentToolAudioBookStream(PDFDocumentTextFlow flow, qint64 itemCount) :
    m_flow(std::make_shared<const PDFDocumentTextFlow>(std::move(flow))),
    m_itemCount(itemCount)
{

}

PDFDocumentToolAudioBookStream::PDFDocumentToolAudioBookStream(const PDFDocumentTextFlowEditor& editor) :
    m_editor(std::make_shared<const PDFDocumentTextFlowEditor>(editor)),
    m_itemCount(qint64(editor.getItemCount()))
{

}

PDFDocumentTextFlow PDFDocumentToolAudioBookStream::createFlow() const
{
    if (m_editor)
    {
        return m_editor->createEditedTextFlow();
    }
    return m_flow ? *m_flow : PDFDocumentTextFlow();
}

QString PDFDocumentToolAudioBook::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolAudioBook::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createPageSelection(tr, QString::fromLatin1(PARAMETER_PAGES), QT_TR_NOOP("Pages"), QT_TR_NOOP("Pages, whose text is read")).setStored(false));

    // 341:9657 Order
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_ORDER), QT_TR_NOOP("Order"), QT_TR_NOOP("Reading order of the text"),
                                                 {
                                                     { QString::fromLatin1(ORDER_AUTO), QT_TR_NOOP("Auto"), QT_TR_NOOP("Structure when the document is tagged, otherwise layout"), QStringLiteral("zap") },
                                                     { QString::fromLatin1(ORDER_LAYOUT), QT_TR_NOOP("Layout"), QT_TR_NOOP("Blocks of text as they are placed on the page"), QStringLiteral("layout") },
                                                     { QString::fromLatin1(ORDER_CONTENT), QT_TR_NOOP("Content stream"), QT_TR_NOOP("The order in which the text is written in the file"), QStringLiteral("align-left") },
                                                     { QString::fromLatin1(ORDER_STRUCTURE), QT_TR_NOOP("Structure"), QT_TR_NOOP("Follows the tags; needs a tagged document"), QStringLiteral("list-tree") },
                                                 },
                                                 QString::fromLatin1(ORDER_AUTO)));
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_VOICE), QT_TR_NOOP("Voice"),
                                                   QT_TR_NOOP("Name of the voice of the text-to-speech engine; empty = the default voice"), QString(), 256));
    definitions.push_back(Definition::createReal(tr, QString::fromLatin1(PARAMETER_RATE), QT_TR_NOOP("Speed"),
                                                 QT_TR_NOOP("Speed of the speech, -1 (slowest) to 1 (fastest); 0 = normal"), 0.0, -1.0, 1.0));
    definitions.push_back(Definition::createReal(tr, QString::fromLatin1(PARAMETER_VOLUME), QT_TR_NOOP("Volume"),
                                                 QT_TR_NOOP("Volume of the speech, 0 to 1"), 1.0, 0.0, 1.0));
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_FORMAT), QT_TR_NOOP("File format"), QT_TR_NOOP("Format of the audio file"),
                                                 {
                                                     { QString::fromLatin1(FORMAT_WAV), QT_TR_NOOP("WAV"), QT_TR_NOOP("Uncompressed audio, 16 bit PCM") },
                                                 },
                                                 QString::fromLatin1(FORMAT_WAV)));
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_SKIP), QT_TR_NOOP("Skip"),
                                                   QT_TR_NOOP("Regular expression of the items, which are not read; empty = none"), QString(), MAXIMUM_SKIP_LENGTH).setStored(false));
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_READ_SPECIAL), QT_TR_NOOP("Read special items"),
                                                   QT_TR_NOOP("Special items, which are read, separated by commas: page-numbers, titles, alt, expanded, actual-text, artifacts; "
                                                              "actual-text and expanded read the replacements of the tags instead of the text they replace"),
                                                   QStringLiteral("actual-text,expanded"), 256));

    Definition editedStream = Definition::createValue(tr, QString::fromLatin1(PARAMETER_EDITED_STREAM), QT_TR_NOOP("Edited text stream"),
                                                      QT_TR_NOOP("The text stream edited in the panel"), QVariant::fromValue(PDFDocumentToolAudioBookStream()));
    definitions.push_back(std::move(editedStream));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolAudioBook::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolAudioBookHelper;

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);

    QString errorMessage;
    getReadSpecial(parameters, &errorMessage);
    if (!errorMessage.isEmpty())
    {
        errors.push_back({ Helper::name(PARAMETER_READ_SPECIAL), errorMessage });
    }

    errorMessage.clear();
    Helper::getSkipExpression(parameters, &errorMessage);
    if (!errorMessage.isEmpty())
    {
        errors.push_back({ Helper::name(PARAMETER_SKIP), errorMessage });
    }
    return errors;
}

PDFDocumentTextFlowFactory::Algorithm PDFDocumentToolAudioBook::getAlgorithm(const QVariantMap& parameters)
{
    const QString order = parameters.value(QString::fromLatin1(PARAMETER_ORDER)).toString();
    if (order == QLatin1String(ORDER_LAYOUT))
    {
        return PDFDocumentTextFlowFactory::Algorithm::Layout;
    }
    if (order == QLatin1String(ORDER_CONTENT))
    {
        return PDFDocumentTextFlowFactory::Algorithm::Content;
    }
    if (order == QLatin1String(ORDER_STRUCTURE))
    {
        return PDFDocumentTextFlowFactory::Algorithm::Structure;
    }
    return PDFDocumentTextFlowFactory::Algorithm::Auto;
}

QString PDFDocumentToolAudioBook::getOrderId(PDFDocumentTextFlowFactory::Algorithm algorithm)
{
    switch (algorithm)
    {
        case PDFDocumentTextFlowFactory::Algorithm::Auto:
            return QString::fromLatin1(ORDER_AUTO);
        case PDFDocumentTextFlowFactory::Algorithm::Layout:
            return QString::fromLatin1(ORDER_LAYOUT);
        case PDFDocumentTextFlowFactory::Algorithm::Content:
            return QString::fromLatin1(ORDER_CONTENT);
        case PDFDocumentTextFlowFactory::Algorithm::Structure:
            return QString::fromLatin1(ORDER_STRUCTURE);
    }
    return QString::fromLatin1(ORDER_AUTO);
}

QStringList PDFDocumentToolAudioBook::getReadSpecial(const QVariantMap& parameters, QString* errorMessage)
{
    const QStringList kinds = PDFDocumentToolAudioBookHelper::getKinds();
    QStringList result;
    for (const QString& part : parameters.value(QString::fromLatin1(PARAMETER_READ_SPECIAL)).toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        const QString kind = part.trimmed().toLower();
        if (kind.isEmpty())
        {
            continue;
        }
        if (!kinds.contains(kind))
        {
            if (errorMessage && errorMessage->isEmpty())
            {
                *errorMessage = tr("Unknown kind of special items '%1'. Valid kinds are %2.").arg(kind, kinds.join(QStringLiteral(", ")));
            }
            continue;
        }
        if (!result.contains(kind))
        {
            result << kind;
        }
    }
    return result;
}

QString PDFDocumentToolAudioBook::getSpecialKind(const PDFDocumentTextFlow::Item& item)
{
    using Flag = PDFDocumentTextFlow::Flag;

    if (item.isText())
    {
        return QString();
    }
    if (item.flags.testFlag(Flag::PageStart))
    {
        return QString::fromLatin1(SPECIAL_PAGE_NUMBERS);
    }
    if (item.flags.testFlag(Flag::StructureTitle))
    {
        return QString::fromLatin1(SPECIAL_TITLES);
    }
    if (item.flags.testFlag(Flag::StructureAlternativeDescription))
    {
        return QString::fromLatin1(SPECIAL_ALT);
    }
    if (item.flags.testFlag(Flag::StructureActualText))
    {
        return QString::fromLatin1(SPECIAL_ACTUAL_TEXT);
    }
    if (item.flags.testFlag(Flag::StructureExpandedForm))
    {
        return QString::fromLatin1(SPECIAL_EXPANDED);
    }
    if (item.flags.testFlag(Flag::Artifact))
    {
        return QString::fromLatin1(SPECIAL_ARTIFACTS);
    }

    // The ends of the pages and of the structure items, the languages and the phonemes are never read
    return QString();
}

bool PDFDocumentToolAudioBook::isReadByDefault(const PDFDocumentTextFlow::Item& item, const QStringList& readSpecial)
{
    using Flag = PDFDocumentTextFlow::Flag;

    if (item.text.trimmed().isEmpty())
    {
        return false;
    }

    if (item.isText())
    {
        // The text replaced by /ActualText or /E is read only, when its replacement is not
        if (item.flags.testFlag(Flag::ReplacedText))
        {
            const QString kind = item.flags.testFlag(Flag::StructureActualText) ? QString::fromLatin1(SPECIAL_ACTUAL_TEXT) : QString::fromLatin1(SPECIAL_EXPANDED);
            return !readSpecial.contains(kind);
        }
        return true;
    }

    const QString kind = getSpecialKind(item);
    return !kind.isEmpty() && readSpecial.contains(kind);
}

PDFDocumentTextFlow PDFDocumentToolAudioBook::createStream(const PDFDocument* document, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress, QString* errorMessage)
{
    if (!document)
    {
        return PDFDocumentTextFlow();
    }

    const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(parameters.value(QString::fromLatin1(PARAMETER_PAGES)), errorMessage);
    const std::vector<PDFInteger> pages = selection ? selection->getPageIndices(document, errorMessage) : std::vector<PDFInteger>();
    if (pages.empty())
    {
        return PDFDocumentTextFlow();
    }

    // R4-13: the reading stream (replacements, artifacts, languages) with the rectangles of the
    // items (the overlay of the panel, Select by rectangle)
    PDFDocumentTextFlowFactory factory;
    factory.setCalculateBoundingBoxes(true);
    factory.setReadingStream(true);
    factory.setOperationControl(control);
    factory.setProgress(progress);
    PDFDocumentTextFlow flow = factory.create(document, pages, getAlgorithm(parameters));
    if (PDFOperationControl::isOperationCanceled(control))
    {
        return PDFDocumentTextFlow();
    }

    // The rectangles of the characters are not needed (an item is read, selected and drawn by its
    // rectangle); the stream is copied into every step of Undo of the panel, 32 bytes per character
    PDFDocumentTextFlow::Items items = flow.getItems();
    for (PDFDocumentTextFlow::Item& item : items)
    {
        std::vector<QRectF>().swap(item.characterBoundingRects);
    }
    return PDFDocumentTextFlow(std::move(items));
}

QRegularExpression PDFDocumentToolAudioBook::createExpression(const QString& pattern)
{
    // (*LIMIT_MATCH=n) at the start of a pattern can only lower the limit of PCRE2; the verbs of
    // the start of the pattern of the user may follow it
    return QRegularExpression(QStringLiteral("(*LIMIT_MATCH=%1)").arg(MAXIMUM_MATCH_STEPS) + pattern, QRegularExpression::UseUnicodePropertiesOption);
}

qint64 PDFDocumentToolAudioBook::estimateDuration(qint64 characters, double rate)
{
    // QTextToSpeech: rate -1 is about half and 1 about double the normal speed
    const double speed = std::pow(2.0, qBound(-1.0, rate, 1.0));
    return qint64(std::llround(double(qMax<qint64>(0, characters)) * 1000.0 / (CHARACTERS_PER_SECOND * speed)));
}

QString PDFDocumentToolAudioBook::formatLength(qint64 milliseconds)
{
    const qint64 seconds = qMax<qint64>(0, (milliseconds + 500) / 1000);
    if (seconds < 60)
    {
        return tr("%1 s").arg(seconds);
    }

    const qint64 minutes = (seconds + 30) / 60;
    if (minutes < 60)
    {
        return tr("%1 min").arg(minutes);
    }
    return tr("%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
}

QString PDFDocumentToolAudioBook::getVoiceForLanguage(const std::vector<PDFSpeechVoice>& voices, const QString& language, const QString& fallback, const QString& defaultVoice)
{
    if (language.isEmpty())
    {
        return fallback;
    }

    // BCP 47: "en-US", "cs"; the primary subtag is the language
    const QString normalized = language.trimmed().replace(QLatin1Char('_'), QLatin1Char('-')).toLower();
    const QString primary = normalized.section(QLatin1Char('-'), 0, 0);
    auto getPrimary = [](const PDFSpeechVoice& voice) { return voice.languageCode.toLower().replace(QLatin1Char('_'), QLatin1Char('-')).section(QLatin1Char('-'), 0, 0); };

    // The chosen voice speaks the language: it stays. No chosen voice is the default voice of the
    // engine (the setting of the system): it stays too, when it speaks the language (coordinator
    // 14. 11.) - another voice only for a language, which it does not speak.
    const QString spokenVoice = fallback.isEmpty() ? defaultVoice : fallback;
    for (const PDFSpeechVoice& voice : voices)
    {
        if (!spokenVoice.isEmpty() && voice.name == spokenVoice && getPrimary(voice) == primary)
        {
            return fallback;
        }
    }

    for (const PDFSpeechVoice& voice : voices)
    {
        if (voice.languageCode.toLower().replace(QLatin1Char('_'), QLatin1Char('-')) == normalized)
        {
            return voice.name;
        }
    }
    for (const PDFSpeechVoice& voice : voices)
    {
        if (getPrimary(voice) == primary)
        {
            return voice.name;
        }
    }
    return fallback;
}

PDFDocumentToolResult PDFDocumentToolAudioBook::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolAudioBookHelper;

    // R4-15: without the factory of the synthesizers there is no engine
    if (!context.speechSynthesizerFactory)
    {
        return PDFDocumentToolResult::createFailed(tr("No text-to-speech engine."));
    }

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();

    QString errorMessage;
    const QStringList readSpecial = getReadSpecial(parameters, &errorMessage);
    const QRegularExpression skipExpression = Helper::getSkipExpression(parameters, &errorMessage);
    if (!errorMessage.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    PDFProgressPhases phases(progress);
    const PDFDocumentToolAudioBookStream editedStream = parameters.value(Helper::name(PARAMETER_EDITED_STREAM)).value<PDFDocumentToolAudioBookStream>();
    const int readPhase = editedStream.isEmpty() ? phases.addPhase(10, tr("Reading pages"), true) : -1;
    const int speechPhase = phases.addPhase(90, tr("Creating audio"), true);
    phases.start();

    // The items, which are read: the edited stream of the panel as it is, else the reading stream
    // of the parameters by the rule of the defaults
    std::vector<Helper::ReadItem> items;
    qint64 itemCount = 0;
    qint64 characterCount = 0;
    if (!editedStream.isEmpty())
    {
        const PDFDocumentTextFlow editedFlow = editedStream.createFlow();
        for (const PDFDocumentTextFlow::Item& item : editedFlow.getItems())
        {
            const QString text = item.text.trimmed();
            if (!text.isEmpty() && !Helper::isSkipped(skipExpression, text))
            {
                items.push_back({ text, item.pageIndex, item.language });
            }
        }
        itemCount = editedStream.getItemCount() >= 0 ? editedStream.getItemCount() : qint64(items.size());
    }
    else
    {
        const PDFDocumentTextFlow flow = createStream(document, parameters, control, phases.getPhase(readPhase), &errorMessage);
        if (PDFOperationControl::isOperationCanceled(control))
        {
            return PDFDocumentToolResult::createStopped();
        }

        for (const PDFDocumentTextFlow::Item& item : flow.getItems())
        {
            const QString text = item.text.trimmed();
            if (text.isEmpty())
            {
                continue;
            }
            ++itemCount;
            if (isReadByDefault(item, readSpecial) && !Helper::isSkipped(skipExpression, text))
            {
                items.push_back({ text, item.pageIndex, item.language });
            }
        }
    }

    if (items.empty())
    {
        return PDFDocumentToolResult::createFailed(errorMessage.isEmpty() ? tr("This document has no text to read.") : errorMessage);
    }
    for (const Helper::ReadItem& item : items)
    {
        characterCount += item.text.size();
    }

    // The synthesizer of the run and its voice (an unknown name is an error with the names)
    std::unique_ptr<PDFSpeechSynthesizer> synthesizer = context.speechSynthesizerFactory->createSynthesizer(&errorMessage);
    if (!synthesizer)
    {
        return PDFDocumentToolResult::createFailed(errorMessage.isEmpty() ? tr("No text-to-speech engine.") : errorMessage);
    }

    const QString voice = parameters.value(Helper::name(PARAMETER_VOICE)).toString();
    if (!synthesizer->setVoice(voice, &errorMessage))
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }
    const double rate = qBound(-1.0, parameters.value(Helper::name(PARAMETER_RATE)).toDouble(), 1.0);
    const double volume = qBound(0.0, parameters.value(Helper::name(PARAMETER_VOLUME)).toDouble(), 1.0);
    synthesizer->setRate(rate);
    synthesizer->setVolume(volume);
    const std::vector<PDFSpeechVoice> voices = context.speechSynthesizerFactory->getVoices();
    const QString defaultVoice = context.speechSynthesizerFactory->getDefaultVoice();

    // The audio goes into a temporary file of the result at once (R4-1): the file is deleted with
    // the last copy of the result, here by every return before the result owns it. The writer is
    // declared after the file: it closes the file before the file is deleted.
    std::shared_ptr<PDFDocumentToolTemporaryFile> temporaryFile = PDFDocumentToolTemporaryFile::create(context.temporaryFolder, QStringLiteral(".wav"), &errorMessage);
    if (!temporaryFile)
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    PDFAudioFileWriter writer(std::make_unique<PDFWavEncoder>());
    if (!writer.open(temporaryFile->getFilePath(), &errorMessage))
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    PDFProgressSink* speechProgress = phases.getPhase(speechPhase);
    if (speechProgress)
    {
        speechProgress->start(items.size(), ProgressStartupInfo());
    }

    QString currentVoice = voice;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (PDFOperationControl::isOperationCanceled(control))
        {
            return PDFDocumentToolResult::createStopped();
        }

        const Helper::ReadItem& item = items[i];
        if (i > 0 && !writer.writeSilence(items[i - 1].pageIndex != item.pageIndex ? PDFAudioFileWriter::PAGE_SILENCE : PDFAudioFileWriter::ITEM_SILENCE))
        {
            return PDFDocumentToolResult::createFailed(writer.getErrorMessage());
        }

        // 14.9.2: a voice of the language of the item, when the engine has one
        const QString itemVoice = getVoiceForLanguage(voices, item.language, voice, defaultVoice);
        if (itemVoice != currentVoice)
        {
            QString voiceError;
            if (synthesizer->setVoice(itemVoice, &voiceError))
            {
                currentVoice = itemVoice;
            }
        }

        if (!synthesizer->synthesize(item.text, &writer, control, &errorMessage))
        {
            if (PDFOperationControl::isOperationCanceled(control))
            {
                return PDFDocumentToolResult::createStopped();
            }
            if (!writer.getErrorMessage().isEmpty())
            {
                errorMessage = writer.getErrorMessage();
            }
            return PDFDocumentToolResult::createFailed(errorMessage.isEmpty() ? tr("The text-to-speech engine has failed.") : errorMessage);
        }

        if (speechProgress)
        {
            speechProgress->step();
        }
    }

    if (!writer.close(&errorMessage))
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }
    if (speechProgress)
    {
        speechProgress->finish();
    }
    phases.finish();

    const PDFAudioFormat format = writer.getFileFormat();
    const qint64 lengthMs = format.sampleRate > 0 ? writer.getFrameCount() * 1000 / format.sampleRate : 0;
    const qint64 size = temporaryFile->getSize();
    const QString baseName = QFileInfo(input.fileName).completeBaseName();

    PDFDocumentToolResult result;
    result.outputs.push_back(PDFDocumentToolOutputFile::createTemporary((baseName.isEmpty() ? QStringLiteral("document") : baseName) + QStringLiteral(".wav"), QStringLiteral("audio/wav"), temporaryFile));

    // 341:11159: Length, Size, Voice, Items read
    const QLocale locale;
    result.facts.push_back({ QStringLiteral("length"), tr("Length"), formatLength(lengthMs) });
    result.facts.push_back({ QStringLiteral("size"), tr("Size"), PDFDocumentToolFormat::formatSize(size) });
    result.facts.push_back({ QStringLiteral("voice"), tr("Voice"), Helper::getVoiceText(voice, rate) });
    result.facts.push_back({ QStringLiteral("items"), tr("Items read"), tr("%1 of %2").arg(locale.toString(qint64(items.size())), locale.toString(itemCount)) });

    result.data.insert(QStringLiteral("itemsRead"), qint64(items.size()));
    result.data.insert(QStringLiteral("itemCount"), itemCount);
    result.data.insert(QStringLiteral("characters"), characterCount);
    result.data.insert(QStringLiteral("lengthMs"), lengthMs);
    result.data.insert(QStringLiteral("size"), size);
    result.data.insert(QStringLiteral("sampleRate"), qint64(format.sampleRate));
    result.data.insert(QStringLiteral("channels"), qint64(format.channelCount));
    result.data.insert(QStringLiteral("format"), QString::fromLatin1(FORMAT_WAV));
    result.data.insert(QStringLiteral("voice"), voice);
    if (editedStream.isEmpty())
    {
        result.data.insert(QStringLiteral("order"), getOrderId(PDFDocumentTextFlowFactory::getEffectiveAlgorithm(document, getAlgorithm(parameters))));
    }
    return result;
}

}   // namespace pdf
