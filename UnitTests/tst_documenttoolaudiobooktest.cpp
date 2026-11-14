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

// The tool Create Audio Book (step 4 of phase 2, package P4; plan 8 row Audio Book jádro, R4-14,
// R4-15, R4-21): the parameters of plan 7 and R4-15, the run without an engine, and the behavior
// with a fake synthesizer: the WAV file of the RIFF specification (Microsoft RIFF WAVE, PCM 16 bit),
// the silence of 300 ms between the items and 600 ms between the pages, the skipped items, the
// edited stream, the voice of a language (ISO 32000-2 14.9.2), Stop and an engine error without a
// file, a voice of another rate (R4-14 b) and 20 minutes of PCM without holding it (R4-21).

#include "pdfaudiofilewriter.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool_audiobook.h"
#include "pdfdocumentwriter.h"
#include "pdfoperationcontrol.h"

#include <QBuffer>
#include <QFontDatabase>
#include <QPainter>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include <atomic>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <Psapi.h>
#endif

class DocumentToolAudioBookTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void parameters();
    void noEngine();
    void wavOfTheItems();
    void skippedItemsAreNotRead();
    void specialItemsByReadSpecial();
    void editedStream();
    void voiceOfTheLanguage();
    void stopDeletesTheFile();
    void engineErrorWithoutFile();
    void unknownVoice();
    void invalidParameters();
    void noTextToRead();
    void otherRateIsConverted();
    void conversionOfFormats();
    void wavLimit();
    void twentyMinutes();
    void streamWithoutCharacterRectangles();
    void defaultVoiceOfTheRun();
    void replaceKeepsTheOldFileOnError();
};

/// A cancellation of the test
class AudioBookTestControl : public pdf::PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return m_isCanceled.load(); }
    void cancel() { m_isCanceled.store(true); }

private:
    std::atomic<bool> m_isCanceled = false;
};

/// The settings of the fake engine (shared by the factory and its synthesizers)
class FakeEngineState
{
public:
    std::vector<QString> texts;                 ///< The spoken texts in their order
    std::vector<QString> voices;                ///< The voice of each text
    int framesPerCharacter = 160;               ///< 10 ms per character at 16 kHz
    qint64 fixedFrames = -1;                    ///< Frames of each text (the 20 minutes), -1 = by the characters
    bool isAlternatingRate = false;             ///< 16 000 Hz and 22 050 Hz by turns (R4-14 b)
    AudioBookTestControl* cancelAtSecondText = nullptr;
    int synthesisCount = 0;
    QString defaultVoice;                       ///< The default voice of the engine (empty = not known)
};

/// A synthesizer, whose audio is a constant value 1000 of 16 bit mono, as many frames as the text
/// has characters times framesPerCharacter; "FAIL" in a text is an error of the engine
class FakeSynthesizer : public pdf::PDFSpeechSynthesizer
{
public:
    explicit FakeSynthesizer(std::shared_ptr<FakeEngineState> state) : m_state(std::move(state)) { }

    virtual bool setVoice(const QString& voiceName, QString* errorMessage) override
    {
        if (!voiceName.isEmpty() && voiceName != QLatin1String("Zira") && voiceName != QLatin1String("Jakub"))
        {
            *errorMessage = QStringLiteral("The voice '%1' does not exist. The voices are: Zira, Jakub.").arg(voiceName);
            return false;
        }
        m_voice = voiceName;
        return true;
    }

    virtual void setRate(double) override { }
    virtual void setVolume(double) override { }

    virtual bool synthesize(const QString& text, pdf::PDFAudioSink* sink, const pdf::PDFOperationControl* operationControl, QString* errorMessage) override
    {
        m_state->texts.push_back(text);
        m_state->voices.push_back(m_voice);
        const int index = m_state->synthesisCount++;
        if (index == 1 && m_state->cancelAtSecondText)
        {
            m_state->cancelAtSecondText->cancel();
        }
        if (text.contains(QLatin1String("FAIL")))
        {
            *errorMessage = QStringLiteral("The engine has failed.");
            return false;
        }

        pdf::PDFAudioFormat format;
        format.sampleRate = m_state->isAlternatingRate && index % 2 == 1 ? 22050 : 16000;
        format.channelCount = 1;
        format.sampleFormat = pdf::PDFAudioFormat::SampleFormat::Int16;

        qint64 frames = m_state->fixedFrames >= 0 ? m_state->fixedFrames : qint64(text.size()) * m_state->framesPerCharacter;
        while (frames > 0)
        {
            if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
            {
                return false;
            }
            const qint64 blockFrames = qMin<qint64>(frames, 16000);
            QByteArray block(int(blockFrames * 2), Qt::Uninitialized);
            for (qint64 i = 0; i < blockFrames; ++i)
            {
                qToLittleEndian<qint16>(1000, block.data() + 2 * i);
            }
            if (!sink->writeAudio(format, block))
            {
                return false;
            }
            frames -= blockFrames;
        }
        return true;
    }

private:
    std::shared_ptr<FakeEngineState> m_state;
    QString m_voice;
};

class FakeSynthesizerFactory : public pdf::PDFSpeechSynthesizerFactory
{
public:
    explicit FakeSynthesizerFactory(std::shared_ptr<FakeEngineState> state) : m_state(std::move(state)) { }

    virtual bool isAvailable() const override { return true; }
    virtual std::vector<pdf::PDFSpeechVoice> getVoices() const override
    {
        return { { QStringLiteral("Zira"), QStringLiteral("en-US"), pdf::PDFSpeechVoice::Gender::Female }, { QStringLiteral("Jakub"), QStringLiteral("cs-CZ"), pdf::PDFSpeechVoice::Gender::Male } };
    }
    virtual QString getDefaultVoice() const override { return m_state->defaultVoice; }
    virtual std::unique_ptr<pdf::PDFSpeechSynthesizer> createSynthesizer(QString*) const override
    {
        return std::make_unique<FakeSynthesizer>(m_state);
    }

private:
    std::shared_ptr<FakeEngineState> m_state;
};

/// Documents, contexts and the reading of the WAV file
class AudioBookTestHelper
{
public:
    /// Pages, whose texts are drawn far apart (each one a block of the layout)
    static pdf::PDFDocument createDocument(const std::vector<QStringList>& pages)
    {
        pdf::PDFDocumentBuilder builder;
        for (const QStringList& texts : pages)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 600, 800));
            if (texts.isEmpty())
            {
                continue;
            }
            pdf::PDFPageContentStreamBuilder content(&builder);
            QPainter* painter = content.begin(page);
            QFont font = painter->font();
            font.setPixelSize(12);
            painter->setFont(font);
            for (int i = 0; i < texts.size(); ++i)
            {
                painter->drawText(QPointF(40, 100 + 300 * i), texts[i]);
            }
            content.end(painter);
        }

        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        pdf::PDFDocument built = builder.build();
        pdf::PDFDocumentWriter(nullptr).write(&buffer, &built);
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        return reader.readFromBuffer(buffer.data());
    }

    static pdf::PDFDocumentToolContext createContext(const pdf::PDFDocument& document, std::shared_ptr<FakeEngineState> state, const QString& folder)
    {
        pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), QByteArray(), QStringLiteral("C:/Books/Project_Report.pdf"), false));
        context.speechSynthesizerFactory = std::make_shared<FakeSynthesizerFactory>(std::move(state));
        context.temporaryFolder = folder;
        return context;
    }

    static QVariantMap layoutParameters()
    {
        QVariantMap parameters;
        parameters.insert(QStringLiteral("order"), QStringLiteral("layout"));
        return parameters;
    }

    /// The bytes of the only output (its temporary file)
    static QByteArray readOutput(const pdf::PDFDocumentToolResult& result)
    {
        if (result.outputs.size() != 1)
        {
            return QByteArray();
        }
        bool isTruncated = false;
        QString errorMessage;
        return result.outputs.front().readContent(qint64(1) << 32, &isTruncated, &errorMessage);
    }

    static quint32 readUInt32(const QByteArray& data, int offset) { return qFromLittleEndian<quint32>(data.constData() + offset); }
    static quint16 readUInt16(const QByteArray& data, int offset) { return qFromLittleEndian<quint16>(data.constData() + offset); }
    static qint16 sample(const QByteArray& data, qint64 frame) { return qFromLittleEndian<qint16>(data.constData() + 44 + 2 * frame); }

    /// The runs of the samples: (value is nonzero, frames)
    static std::vector<std::pair<bool, qint64>> getRuns(const QByteArray& data)
    {
        std::vector<std::pair<bool, qint64>> runs;
        const qint64 frames = (data.size() - 44) / 2;
        for (qint64 i = 0; i < frames; ++i)
        {
            const bool isSound = sample(data, i) != 0;
            if (runs.empty() || runs.back().first != isSound)
            {
                runs.push_back({ isSound, 0 });
            }
            ++runs.back().second;
        }
        return runs;
    }

    static QString factValue(const pdf::PDFDocumentToolResult& result, const QString& id)
    {
        for (const pdf::PDFDocumentToolFact& fact : result.facts)
        {
            if (fact.id == id)
            {
                return fact.value;
            }
        }
        return QString();
    }

    static int countFiles(const QString& folder)
    {
        return int(QDir(folder).entryList(QDir::Files).size());
    }
};

void DocumentToolAudioBookTest::initTestCase()
{
    // The offscreen platform has no fonts: the bundled Inter font of Pdf4QtLibFluent
    const QDir fonts(QStringLiteral(PDF4QT_TEST_FONT_DIRECTORY));
    for (const QString& file : fonts.entryList({ QStringLiteral("*.ttf") }, QDir::Files))
    {
        QVERIFY(QFontDatabase::addApplicationFont(fonts.absoluteFilePath(file)) >= 0);
    }
    QGuiApplication::setFont(QFont(QStringLiteral("Inter")));
}

void DocumentToolAudioBookTest::parameters()
{
    // Plan 7 with R4-15: readSpecial instead of skipSpecial; the edited stream only of the panel;
    // WAV only (owner 14. 11.)
    pdf::PDFDocumentToolAudioBook tool;
    QCOMPARE(tool.getId(), QStringLiteral("convert.audiobook"));
    QVERIFY(tool.getInfo());

    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QString error;
    QVERIFY2(pdf::PDFDocumentToolParameters::isValid(definitions, &error), qPrintable(error));
    const QJsonObject properties = pdf::PDFDocumentToolParameters::toJsonSchema(definitions).value(QStringLiteral("properties")).toObject();
    for (const char* name : { "pages", "order", "voice", "rate", "volume", "format", "skip", "readSpecial" })
    {
        QVERIFY2(properties.contains(QString::fromLatin1(name)), name);
    }
    QVERIFY(!properties.contains(QStringLiteral("editedStream")));
    QVERIFY(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("editedStream")));

    const pdf::PDFDocumentToolParameterDefinition* rate = pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("rate"));
    QCOMPARE(rate->getRealMinimum(), -1.0);
    QCOMPARE(rate->getRealMaximum(), 1.0);
    QCOMPARE(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("skip"))->getMaximumLength(), 1000);

    const QJsonArray formats = properties.value(QStringLiteral("format")).toObject().value(QStringLiteral("enum")).toArray();
    QCOMPARE(formats.size(), 1);
    QCOMPARE(formats.at(0).toString(), QStringLiteral("wav"));
}

void DocumentToolAudioBookTest::noEngine()
{
    // R4-15: a context without the factory of the synthesizers has no engine - the run fails so
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    pdf::PDFDocumentPointer document(new pdf::PDFDocument(builder.build()));
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(document, QByteArray(), QStringLiteral("book.pdf"), true));
    QVERIFY(!context.speechSynthesizerFactory);

    pdf::PDFDocumentToolAudioBook tool;
    const pdf::PDFDocumentToolResult result = tool.run(context, QVariantMap(), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QCOMPARE(result.message, QStringLiteral("No text-to-speech engine."));
    QVERIFY(result.outputs.empty());
}

void DocumentToolAudioBookTest::wavOfTheItems()
{
    using H = AudioBookTestHelper;

    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    auto state = std::make_shared<FakeEngineState>();
    const pdf::PDFDocument document = H::createDocument({ { QStringLiteral("Alpha one"), QStringLiteral("Beta two") }, { QStringLiteral("Gamma three") } });
    pdf::PDFDocumentToolAudioBook tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(document, state, folder.path()), H::layoutParameters(), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    // The texts in their order; "Page 1" (a page number) is not read by default
    QCOMPARE(int(state->texts.size()), 3);
    QCOMPARE(state->texts[0].simplified(), QStringLiteral("Alpha one"));
    QCOMPARE(state->texts[1].simplified(), QStringLiteral("Beta two"));
    QCOMPARE(state->texts[2].simplified(), QStringLiteral("Gamma three"));

    // One output: a temporary file of the folder of the run, "Project_Report.wav", audio/wav
    QCOMPARE(result.outputs.size(), size_t(1));
    const pdf::PDFDocumentToolOutputFile& output = result.outputs.front();
    QVERIFY(output.isTemporaryFile());
    QVERIFY(output.data.isEmpty());
    QCOMPARE(output.suggestedFileName, QStringLiteral("Project_Report.wav"));
    QCOMPARE(output.mimeType, QStringLiteral("audio/wav"));
    QCOMPARE(QFileInfo(output.getFilePath()).absolutePath(), QFileInfo(folder.path()).absoluteFilePath());

    // RIFF WAVE: "RIFF", the size of the rest, "WAVE", "fmt " of 16 bytes with PCM (1), mono,
    // 16 000 Hz, 32 000 bytes a second, blocks of 2 bytes, 16 bits, "data" and its size
    const QByteArray data = H::readOutput(result);
    QVERIFY(data.size() > 44);
    QCOMPARE(data.left(4), QByteArray("RIFF"));
    QCOMPARE(H::readUInt32(data, 4), quint32(data.size() - 8));
    QCOMPARE(data.mid(8, 4), QByteArray("WAVE"));
    QCOMPARE(data.mid(12, 4), QByteArray("fmt "));
    QCOMPARE(H::readUInt32(data, 16), quint32(16));
    QCOMPARE(H::readUInt16(data, 20), quint16(1));
    QCOMPARE(H::readUInt16(data, 22), quint16(1));
    QCOMPARE(H::readUInt32(data, 24), quint32(16000));
    QCOMPARE(H::readUInt32(data, 28), quint32(32000));
    QCOMPARE(H::readUInt16(data, 32), quint16(2));
    QCOMPARE(H::readUInt16(data, 34), quint16(16));
    QCOMPARE(data.mid(36, 4), QByteArray("data"));
    QCOMPARE(H::readUInt32(data, 40), quint32(data.size() - 44));
    QCOMPARE(output.getSize(), qint64(data.size()));

    // The speech of each item, 300 ms (4 800 frames) between the items of a page, 600 ms (9 600
    // frames) between the pages; nothing before the first and after the last item
    const std::vector<std::pair<bool, qint64>> runs = H::getRuns(data);
    QCOMPARE(int(runs.size()), 5);
    QCOMPARE(runs[0], std::make_pair(true, qint64(state->texts[0].size()) * 160));
    QCOMPARE(runs[1], std::make_pair(false, qint64(4800)));
    QCOMPARE(runs[2], std::make_pair(true, qint64(state->texts[1].size()) * 160));
    QCOMPARE(runs[3], std::make_pair(false, qint64(9600)));
    QCOMPARE(runs[4], std::make_pair(true, qint64(state->texts[2].size()) * 160));

    // 341:11159: Items read (of the five items with a text: two page numbers and three texts),
    // Length (the frames of the file), Size
    QCOMPARE(H::factValue(result, QStringLiteral("items")), QStringLiteral("3 of 5"));
    QCOMPARE(result.data.value(QStringLiteral("lengthMs")).toInteger(), qint64(data.size() - 44) / 2 * 1000 / 16000);
    QCOMPARE(result.data.value(QStringLiteral("size")).toInteger(), qint64(data.size()));
    QCOMPARE(result.data.value(QStringLiteral("sampleRate")).toInteger(), qint64(16000));
}

void DocumentToolAudioBookTest::skippedItemsAreNotRead()
{
    using H = AudioBookTestHelper;

    // skip: a regular expression of the items, which are not read (MCP)
    QTemporaryDir folder;
    auto state = std::make_shared<FakeEngineState>();
    const pdf::PDFDocument document = H::createDocument({ { QStringLiteral("Alpha one"), QStringLiteral("Beta two") } });
    QVariantMap parameters = H::layoutParameters();
    parameters.insert(QStringLiteral("skip"), QStringLiteral("^Beta"));
    pdf::PDFDocumentToolAudioBook tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(document, state, folder.path()), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(int(state->texts.size()), 1);
    QCOMPARE(state->texts[0].simplified(), QStringLiteral("Alpha one"));
    QCOMPARE(H::factValue(result, QStringLiteral("items")), QStringLiteral("1 of 3"));
}

void DocumentToolAudioBookTest::specialItemsByReadSpecial()
{
    using H = AudioBookTestHelper;

    // readSpecial page-numbers (PdfTool --say-page-numbers): "Page 1" is read before the text
    QTemporaryDir folder;
    auto state = std::make_shared<FakeEngineState>();
    const pdf::PDFDocument document = H::createDocument({ { QStringLiteral("Alpha one") } });
    QVariantMap parameters = H::layoutParameters();
    parameters.insert(QStringLiteral("readSpecial"), QStringLiteral("page-numbers"));
    pdf::PDFDocumentToolAudioBook tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(document, state, folder.path()), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(int(state->texts.size()), 2);
    QCOMPARE(state->texts[0], QStringLiteral("Page 1"));
    QCOMPARE(state->texts[1].simplified(), QStringLiteral("Alpha one"));

    // ISO 32000-2 14.9.4, 14.9.5: /ActualText (/E) is read instead of the text it replaces, never
    // both; without the kind in readSpecial the text of the page is read and the replacement not
    using Flag = pdf::PDFDocumentTextFlow::Flag;
    const pdf::PDFDocumentTextFlow::Item actualText{ QRectF(), 0, QStringLiteral("fifty"), Flag::StructureActualText, {} };
    const pdf::PDFDocumentTextFlow::Item replacedByActualText{ QRectF(), 0, QStringLiteral("50"), pdf::PDFDocumentTextFlow::Flags(Flag::Text) | Flag::ReplacedText | Flag::StructureActualText, {} };
    const pdf::PDFDocumentTextFlow::Item expandedForm{ QRectF(), 0, QStringLiteral("for example"), Flag::StructureExpandedForm, {} };
    const pdf::PDFDocumentTextFlow::Item replacedByExpandedForm{ QRectF(), 0, QStringLiteral("e.g."), pdf::PDFDocumentTextFlow::Flags(Flag::Text) | Flag::ReplacedText | Flag::StructureExpandedForm, {} };
    const QStringList defaultReadSpecial = { QStringLiteral("actual-text"), QStringLiteral("expanded") };
    QVERIFY(pdf::PDFDocumentToolAudioBook::isReadByDefault(actualText, defaultReadSpecial));
    QVERIFY(!pdf::PDFDocumentToolAudioBook::isReadByDefault(replacedByActualText, defaultReadSpecial));
    QVERIFY(pdf::PDFDocumentToolAudioBook::isReadByDefault(expandedForm, defaultReadSpecial));
    QVERIFY(!pdf::PDFDocumentToolAudioBook::isReadByDefault(replacedByExpandedForm, defaultReadSpecial));
    QVERIFY(!pdf::PDFDocumentToolAudioBook::isReadByDefault(actualText, QStringList()));
    QVERIFY(pdf::PDFDocumentToolAudioBook::isReadByDefault(replacedByActualText, QStringList()));
    QVERIFY(pdf::PDFDocumentToolAudioBook::isReadByDefault(replacedByActualText, { QStringLiteral("expanded") }));
    QVERIFY(!pdf::PDFDocumentToolAudioBook::isReadByDefault(replacedByExpandedForm, { QStringLiteral("expanded") }));
}

void DocumentToolAudioBookTest::editedStream()
{
    using H = AudioBookTestHelper;

    // The panel: the edited stream is read as it is - its order, its texts, without the skipped
    // items; the count of the panel is in the fact (341:11159 "1 of 3")
    pdf::PDFDocumentTextFlow::Items items;
    items.push_back({ QRectF(), 0, QStringLiteral("First"), pdf::PDFDocumentTextFlow::Text, {} });
    items.push_back({ QRectF(), 0, QStringLiteral("Second"), pdf::PDFDocumentTextFlow::Text, {} });
    items.push_back({ QRectF(), 1, QStringLiteral("Third"), pdf::PDFDocumentTextFlow::Text, {} });
    pdf::PDFDocumentTextFlowEditor editor;
    editor.setTextFlow(pdf::PDFDocumentTextFlow(std::move(items)));
    editor.removeItem(0);
    editor.setText(QStringLiteral("Third, edited"), 2);
    editor.select(2, true);
    editor.moveSelectionUp();
    editor.deselect();

    QTemporaryDir folder;
    auto state = std::make_shared<FakeEngineState>();
    const pdf::PDFDocument document = H::createDocument({ { QStringLiteral("Not read") } });
    QVariantMap parameters;
    parameters.insert(QStringLiteral("editedStream"), QVariant::fromValue(pdf::PDFDocumentToolAudioBookStream(editor)));
    pdf::PDFDocumentToolAudioBook tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(document, state, folder.path()), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(state->texts, std::vector<QString>({ QStringLiteral("Third, edited"), QStringLiteral("Second") }));
    QCOMPARE(H::factValue(result, QStringLiteral("items")), QStringLiteral("2 of 3"));

    // The two items are on two pages: 600 ms between them
    const std::vector<std::pair<bool, qint64>> runs = H::getRuns(H::readOutput(result));
    QCOMPARE(int(runs.size()), 3);
    QCOMPARE(runs[1], std::make_pair(false, qint64(9600)));
}

void DocumentToolAudioBookTest::voiceOfTheLanguage()
{
    using H = AudioBookTestHelper;

    // 14.9.2: an item of a language, which a voice speaks, is read by that voice; the others by
    // the chosen one
    pdf::PDFDocumentTextFlow::Item english{ QRectF(), 0, QStringLiteral("Hello"), pdf::PDFDocumentTextFlow::Text, {} };
    english.language = QStringLiteral("en-US");
    pdf::PDFDocumentTextFlow::Item czech{ QRectF(), 0, QStringLiteral("Ahoj"), pdf::PDFDocumentTextFlow::Text, {} };
    czech.language = QStringLiteral("cs");
    pdf::PDFDocumentTextFlow::Item german{ QRectF(), 0, QStringLiteral("Hallo"), pdf::PDFDocumentTextFlow::Text, {} };
    german.language = QStringLiteral("de-DE");

    QTemporaryDir folder;
    auto state = std::make_shared<FakeEngineState>();
    const pdf::PDFDocument document = H::createDocument({ { QStringLiteral("Not read") } });
    QVariantMap parameters;
    parameters.insert(QStringLiteral("voice"), QStringLiteral("Zira"));
    parameters.insert(QStringLiteral("editedStream"), QVariant::fromValue(pdf::PDFDocumentToolAudioBookStream(pdf::PDFDocumentTextFlow({ english, czech, german }))));
    pdf::PDFDocumentToolAudioBook tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(document, state, folder.path()), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(state->voices, std::vector<QString>({ QStringLiteral("Zira"), QStringLiteral("Jakub"), QStringLiteral("Zira") }));
}

void DocumentToolAudioBookTest::stopDeletesTheFile()
{
    using H = AudioBookTestHelper;

    // 341:10706 "Stop ends the run, nothing is kept": Stopped, the temporary file is gone
    QTemporaryDir folder;
    AudioBookTestControl control;
    auto state = std::make_shared<FakeEngineState>();
    state->cancelAtSecondText = &control;
    const pdf::PDFDocument document = H::createDocument({ { QStringLiteral("Alpha one"), QStringLiteral("Beta two") }, { QStringLiteral("Gamma three") } });
    pdf::PDFDocumentToolAudioBook tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(document, state, folder.path()), H::layoutParameters(), &control, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Stopped);
    QVERIFY(result.outputs.empty());
    QCOMPARE(int(state->texts.size()), 2);
    QCOMPARE(H::countFiles(folder.path()), 0);
}

void DocumentToolAudioBookTest::engineErrorWithoutFile()
{
    using H = AudioBookTestHelper;

    // An error of the engine (a voice gone during the run, plan 6): Failed with its reason, no file
    QTemporaryDir folder;
    auto state = std::make_shared<FakeEngineState>();
    const pdf::PDFDocument document = H::createDocument({ { QStringLiteral("Alpha one"), QStringLiteral("FAIL here") } });
    pdf::PDFDocumentToolAudioBook tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(document, state, folder.path()), H::layoutParameters(), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QCOMPARE(result.message, QStringLiteral("The engine has failed."));
    QVERIFY(result.outputs.empty());
    QCOMPARE(H::countFiles(folder.path()), 0);
}

void DocumentToolAudioBookTest::unknownVoice()
{
    using H = AudioBookTestHelper;

    // MCP: an unknown voice is an error with the names of the voices (plan 7)
    QTemporaryDir folder;
    auto state = std::make_shared<FakeEngineState>();
    const pdf::PDFDocument document = H::createDocument({ { QStringLiteral("Alpha one") } });
    QVariantMap parameters = H::layoutParameters();
    parameters.insert(QStringLiteral("voice"), QStringLiteral("Nobody"));
    pdf::PDFDocumentToolAudioBook tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(document, state, folder.path()), parameters, nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.contains(QStringLiteral("Zira")), qPrintable(result.message));
    QVERIFY(state->texts.empty());
    QCOMPARE(H::countFiles(folder.path()), 0);
}

void DocumentToolAudioBookTest::invalidParameters()
{
    using H = AudioBookTestHelper;

    // An unknown kind of readSpecial and a wrong regular expression are refused before the run
    QTemporaryDir folder;
    auto state = std::make_shared<FakeEngineState>();
    const pdf::PDFDocument document = H::createDocument({ { QStringLiteral("Alpha one") } });
    pdf::PDFDocumentToolAudioBook tool;

    QVariantMap special = H::layoutParameters();
    special.insert(QStringLiteral("readSpecial"), QStringLiteral("titles,footnotes"));
    const pdf::PDFDocumentToolResult specialResult = tool.run(H::createContext(document, state, folder.path()), special, nullptr, nullptr);
    QCOMPARE(specialResult.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY2(specialResult.message.contains(QStringLiteral("footnotes")), qPrintable(specialResult.message));

    QVariantMap skip = H::layoutParameters();
    skip.insert(QStringLiteral("skip"), QStringLiteral("(Page"));
    const pdf::PDFDocumentToolResult skipResult = tool.run(H::createContext(document, state, folder.path()), skip, nullptr, nullptr);
    QCOMPARE(skipResult.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY2(skipResult.message.contains(QStringLiteral("regular expression")), qPrintable(skipResult.message));
    QVERIFY(state->texts.empty());
}

void DocumentToolAudioBookTest::noTextToRead()
{
    using H = AudioBookTestHelper;

    // 2.7: a scan without OCR has no text to read
    QTemporaryDir folder;
    auto state = std::make_shared<FakeEngineState>();
    const pdf::PDFDocument document = H::createDocument({ {} });
    pdf::PDFDocumentToolAudioBook tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(document, state, folder.path()), H::layoutParameters(), nullptr, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QCOMPARE(result.message, QStringLiteral("This document has no text to read."));
    QCOMPARE(H::countFiles(folder.path()), 0);
}

void DocumentToolAudioBookTest::otherRateIsConverted()
{
    using H = AudioBookTestHelper;

    // R4-14 b: the first block decides the rate of the file (16 000 Hz); the audio of a voice of
    // 22 050 Hz is resampled to it - its length in time stays
    QTemporaryDir folder;
    auto state = std::make_shared<FakeEngineState>();
    state->isAlternatingRate = true;
    state->fixedFrames = 22050;
    pdf::PDFDocumentTextFlow::Items items;
    items.push_back({ QRectF(), 0, QStringLiteral("First"), pdf::PDFDocumentTextFlow::Text, {} });
    items.push_back({ QRectF(), 0, QStringLiteral("Second"), pdf::PDFDocumentTextFlow::Text, {} });
    const pdf::PDFDocument document = H::createDocument({ { QStringLiteral("Not read") } });
    QVariantMap parameters;
    parameters.insert(QStringLiteral("editedStream"), QVariant::fromValue(pdf::PDFDocumentToolAudioBookStream(pdf::PDFDocumentTextFlow(std::move(items)))));
    pdf::PDFDocumentToolAudioBook tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(document, state, folder.path()), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const QByteArray data = H::readOutput(result);
    QCOMPARE(H::readUInt32(data, 24), quint32(16000));
    const std::vector<std::pair<bool, qint64>> runs = H::getRuns(data);
    QCOMPARE(int(runs.size()), 3);
    QCOMPARE(runs[0].second, qint64(22050));                   // 1.378 s of 16 kHz (the first voice)
    QCOMPARE(runs[1].second, qint64(4800));
    QCOMPARE(runs[2].second, qint64(16000));                   // 1 s of 22.05 kHz is 1 s of 16 kHz
    QCOMPARE(H::sample(data, 22050 + 4800 + 100), qint16(1000)); // the value stays
}

void DocumentToolAudioBookTest::conversionOfFormats()
{
    using Format = pdf::PDFAudioFormat;

    // 16 bit mono stays as it is
    const Format mono16{ 16000, 1, Format::SampleFormat::Int16 };
    QByteArray samples(6, Qt::Uninitialized);
    qToLittleEndian<qint16>(-32768, samples.data());
    qToLittleEndian<qint16>(12345, samples.data() + 2);
    qToLittleEndian<qint16>(32767, samples.data() + 4);
    const QByteArray same = pdf::PDFAudioFileWriter::convert(mono16, samples, mono16);
    QCOMPARE(same.size(), 6);
    const qint16* sameSamples = reinterpret_cast<const qint16*>(same.constData());
    QCOMPARE(sameSamples[0], qint16(-32768));
    QCOMPARE(sameSamples[1], qint16(12345));
    QCOMPARE(sameSamples[2], qint16(32767));

    // Float stereo to 16 bit mono: the mean of the channels (0.5 and -0.5 is silence, 0.5 and 0.5 is 16384)
    const Format stereoFloat{ 16000, 2, Format::SampleFormat::Float };
    const float values[] = { 0.5f, -0.5f, 0.5f, 0.5f };
    QByteArray floatSamples(reinterpret_cast<const char*>(values), int(sizeof(values)));
    const QByteArray mixed = pdf::PDFAudioFileWriter::convert(stereoFloat, floatSamples, mono16);
    QCOMPARE(mixed.size(), 4);
    QCOMPARE(reinterpret_cast<const qint16*>(mixed.constData())[0], qint16(0));
    QCOMPARE(reinterpret_cast<const qint16*>(mixed.constData())[1], qint16(16384));

    // 8 bit unsigned mono to 16 bit stereo: 128 is silence, the channel is duplicated
    const Format mono8{ 16000, 1, Format::SampleFormat::UInt8 };
    const Format stereo16{ 16000, 2, Format::SampleFormat::Int16 };
    QByteArray byteSamples;
    byteSamples.append('\x80');
    byteSamples.append('\xC0');
    const QByteArray duplicated = pdf::PDFAudioFileWriter::convert(mono8, byteSamples, stereo16);
    QCOMPARE(duplicated.size(), 8);
    const qint16* stereo = reinterpret_cast<const qint16*>(duplicated.constData());
    QCOMPARE(stereo[0], qint16(0));
    QCOMPARE(stereo[1], qint16(0));
    QCOMPARE(stereo[2], qint16(16384));
    QCOMPARE(stereo[3], qint16(16384));

    // The rate: 8 000 frames of 8 kHz are 16 000 frames of 16 kHz; a ramp stays linear
    const Format mono8k{ 8000, 1, Format::SampleFormat::Int16 };
    QByteArray ramp(4, Qt::Uninitialized);
    qToLittleEndian<qint16>(0, ramp.data());
    qToLittleEndian<qint16>(1000, ramp.data() + 2);
    const QByteArray resampled = pdf::PDFAudioFileWriter::convert(mono8k, ramp, mono16);
    QCOMPARE(resampled.size(), 8);
    const qint16* resampledSamples = reinterpret_cast<const qint16*>(resampled.constData());
    QCOMPARE(resampledSamples[0], qint16(0));
    QCOMPARE(resampledSamples[1], qint16(500));
    QCOMPARE(resampledSamples[2], qint16(1000));

    // An unknown format gives nothing
    QVERIFY(pdf::PDFAudioFileWriter::convert(Format(), samples, mono16).isEmpty());
}

void DocumentToolAudioBookTest::wavLimit()
{
    // The sizes of RIFF are 32 bit: a WAV file larger than 4 GB is an error (checked before the
    // samples are copied: the pointer is never read)
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    pdf::PDFWavEncoder encoder;
    QString error;
    QVERIFY(encoder.begin(&buffer, 16000, 1, &error));
    const qint16 sample = 0;
    QVERIFY(!encoder.write(&sample, qint64(1) << 31, &error));
    QVERIFY2(error.contains(QStringLiteral("longer than a WAV file can hold")), qPrintable(error));
    QCOMPARE(buffer.size(), qint64(44));
}

void DocumentToolAudioBookTest::twentyMinutes()
{
    using H = AudioBookTestHelper;

    // R4-21: 20 minutes of fake PCM (120 items of 10 s, 38.4 MB): the PCM goes into the file at
    // once - the peak of the memory of the process grows by much less than the file
    pdf::PDFDocumentTextFlow::Items items;
    for (int i = 0; i < 120; ++i)
    {
        items.push_back({ QRectF(), 0, QStringLiteral("Item %1").arg(i), pdf::PDFDocumentTextFlow::Text, {} });
    }

    QTemporaryDir folder;
    auto state = std::make_shared<FakeEngineState>();
    state->fixedFrames = 160000;
    const pdf::PDFDocument document = H::createDocument({ { QStringLiteral("Not read") } });
    QVariantMap parameters;
    parameters.insert(QStringLiteral("editedStream"), QVariant::fromValue(pdf::PDFDocumentToolAudioBookStream(pdf::PDFDocumentTextFlow(std::move(items)))));

#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS before = { };
    QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(), &before, sizeof(before)));
#endif

    QElapsedTimer timer;
    timer.start();
    pdf::PDFDocumentToolAudioBook tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(document, state, folder.path()), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const qint64 frames = 120 * qint64(160000) + 119 * qint64(4800);
    const qint64 fileSize = 44 + 2 * frames;
    QCOMPARE(result.outputs.front().getSize(), fileSize);
    QCOMPARE(result.data.value(QStringLiteral("lengthMs")).toInteger(), frames * 1000 / 16000);
    // 1,200 s of speech and 119 pauses of 0.3 s: 1,235.7 s is 21 minutes, rounded
    QCOMPARE(pdf::PDFDocumentToolAudioBook::formatLength(result.data.value(QStringLiteral("lengthMs")).toInteger()), QStringLiteral("21 min"));

    const double factor = qMax(1.0, qEnvironmentVariable("PDF4QT_TEST_TIMEOUT_FACTOR").toDouble());
    QVERIFY2(timer.elapsed() < qint64(60000 * factor), qPrintable(QString::number(timer.elapsed())));

#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS after = { };
    QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(), &after, sizeof(after)));
    const qint64 growth = qint64(after.PeakWorkingSetSize) - qint64(before.WorkingSetSize);
    QVERIFY2(growth < fileSize / 2, qPrintable(QStringLiteral("peak growth %1 of a file of %2").arg(growth).arg(fileSize)));
#endif
}

void DocumentToolAudioBookTest::streamWithoutCharacterRectangles()
{
    using H = AudioBookTestHelper;

    // The reading stream of the panel and of a run (createStream): every item has its rectangle
    // (the overlay, Select by rectangle), no item the rectangles of its characters - the panel
    // keeps a copy of the stream in every step of Undo (32 bytes per character of the document)
    const pdf::PDFDocument document = H::createDocument({ { QStringLiteral("Alpha one"), QStringLiteral("Beta two") }, { QStringLiteral("Gamma three") } });
    for (const char* order : { "layout", "content" })
    {
        QVariantMap parameters = pdf::PDFDocumentToolParameters::getDefaultValues(pdf::PDFDocumentToolAudioBook().getParameters());
        parameters.insert(QStringLiteral("order"), QString::fromLatin1(order));
        QString errorMessage;
        const pdf::PDFDocumentTextFlow flow = pdf::PDFDocumentToolAudioBook::createStream(&document, parameters, nullptr, nullptr, &errorMessage);
        int textCount = 0;
        for (const pdf::PDFDocumentTextFlow::Item& item : flow.getItems())
        {
            QVERIFY2(item.characterBoundingRects.empty(), qPrintable(item.text));
            if (item.isText() && !item.text.trimmed().isEmpty())
            {
                ++textCount;
                QVERIFY2(!item.boundingRect.isEmpty(), qPrintable(item.text));
            }
        }

        // The layout: a block of each text; the order of the content: the text of the pages
        QVERIFY2(qstrcmp(order, "layout") == 0 ? textCount == 3 : textCount >= 1, qPrintable(QStringLiteral("%1: %2 texts").arg(QString::fromLatin1(order)).arg(textCount)));
    }
}

void DocumentToolAudioBookTest::defaultVoiceOfTheRun()
{
    using H = AudioBookTestHelper;

    // Coordinator 14. 11.: no voice chosen and the default voice of the engine (Zira, en-US)
    // speaks English - the English item of en-GB keeps it (no voice is set), the Czech item gets
    // Jakub, the German item (no voice speaks it) the default voice again
    pdf::PDFDocumentTextFlow::Item english{ QRectF(), 0, QStringLiteral("Hello"), pdf::PDFDocumentTextFlow::Text, {} };
    english.language = QStringLiteral("en-GB");
    pdf::PDFDocumentTextFlow::Item czech{ QRectF(), 0, QStringLiteral("Ahoj"), pdf::PDFDocumentTextFlow::Text, {} };
    czech.language = QStringLiteral("cs-CZ");
    pdf::PDFDocumentTextFlow::Item german{ QRectF(), 0, QStringLiteral("Hallo"), pdf::PDFDocumentTextFlow::Text, {} };
    german.language = QStringLiteral("de-DE");

    QTemporaryDir folder;
    auto state = std::make_shared<FakeEngineState>();
    state->defaultVoice = QStringLiteral("Zira");
    const pdf::PDFDocument document = H::createDocument({ { QStringLiteral("Not read") } });
    QVariantMap parameters;
    parameters.insert(QStringLiteral("editedStream"), QVariant::fromValue(pdf::PDFDocumentToolAudioBookStream(pdf::PDFDocumentTextFlow({ english, czech, german }))));
    pdf::PDFDocumentToolAudioBook tool;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(document, state, folder.path()), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(state->voices, std::vector<QString>({ QString(), QStringLiteral("Jakub"), QString() }));
}

void DocumentToolAudioBookTest::replaceKeepsTheOldFileOnError()
{
    // RS4AUDIO Low, PdfTool --overwrite (B77): the new file replaces the old one only when it is
    // complete; when the new one cannot be written, the old file stays as it was
    QTemporaryDir folder;
    const QString target = folder.filePath(QStringLiteral("Book.wav"));
    auto writeFile = [](const QString& path, const QByteArray& data)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    auto readFile = [](const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    QVERIFY(writeFile(target, QByteArray("OLD AUDIO BOOK")));

    QString error;
    std::shared_ptr<pdf::PDFDocumentToolTemporaryFile> temporaryFile = pdf::PDFDocumentToolTemporaryFile::create(folder.path(), QStringLiteral(".wav"), &error);
    QVERIFY2(temporaryFile, qPrintable(error));
    const QByteArray newData(5 * 1024 * 1024 + 17, 'N');
    QVERIFY(writeFile(temporaryFile->getFilePath(), newData));
    QVERIFY(temporaryFile->copyTo(target, &error));
    QCOMPARE(readFile(target), newData);
    QVERIFY(QFile::exists(temporaryFile->getFilePath()));

    // The source is gone: the old target stays
    QVERIFY(writeFile(target, QByteArray("OLD AUDIO BOOK")));
    QVERIFY(QFile::remove(temporaryFile->getFilePath()));
    QVERIFY(!temporaryFile->copyTo(target, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(readFile(target), QByteArray("OLD AUDIO BOOK"));

    // A moved file is no source any more
    std::shared_ptr<pdf::PDFDocumentToolTemporaryFile> movedFile = pdf::PDFDocumentToolTemporaryFile::create(folder.path(), QStringLiteral(".wav"), &error);
    QVERIFY(movedFile);
    QVERIFY(movedFile->moveTo(folder.filePath(QStringLiteral("Moved.wav")), &error));
    QVERIFY(!movedFile->copyTo(target, &error));
    QCOMPARE(readFile(target), QByteArray("OLD AUDIO BOOK"));
}

QTEST_MAIN(DocumentToolAudioBookTest)

#include "tst_documenttoolaudiobooktest.moc"
