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

// The library Pdf4QtSpeech (step 4 of phase 2, package P4; plan 8 row Pdf4QtSpeech, owner answers
// Q4.1 - Q4.3, R4-14, R4-15): the synthesis of the engine mock of Qt (it synthesizes, nothing is
// played) in a thread of a run, the cancellation, a sink, which refuses, the engines of the
// factory, the one selection of a voice by its attributes.

#include "pdfqtspeechsynthesizer.h"
#include "pdfoperationcontrol.h"

#include <QElapsedTimer>
#include <QtTest>

#include <atomic>
#include <thread>

// The hooks of QtCore (private API, qhooks_p.h; UnitTestsFluentLifetime uses them too): every
// QObject created and destroyed, in any thread
QT_BEGIN_NAMESPACE
extern quintptr Q_CORE_EXPORT qtHookData[];
QT_END_NAMESPACE

class SpeechTest : public QObject
{
    Q_OBJECT

private slots:
    void preferredEngines();
    void unknownEngine();
    void synthesisOfTheMockEngine();
    void synthesisInAThreadOfARun();
    void cancellation();
    void sinkRefuses();
    void unknownVoiceNamesTheVoices();
    void selectVoice();
    void voicesOfAllLanguages();
    void emptyVoiceIsTheDefaultVoice();
    void synthesizerLeavesNoObject();
    void probeAgainAfterNoVoice();
    void defaultVoiceOfTheFactory();
};

/// A factory, whose engine has no voice at first and gets one later (341:11665: the user adds a
/// voice in the settings of the system and opens the tool again); counts its probes
class SpeechTestLateVoiceFactory : public pdf::PDFQtSpeechSynthesizerFactory
{
public:
    SpeechTestLateVoiceFactory() : pdf::PDFQtSpeechSynthesizerFactory(QStringLiteral("late")) { }

    mutable std::atomic<int> probes = 0;
    std::atomic<bool> hasVoice = false;

protected:
    virtual bool probeEngine(const QString& engine, std::vector<pdf::PDFSpeechVoice>* voices, QString* defaultVoice) const override
    {
        ++probes;
        if (engine != QStringLiteral("late") || !hasVoice.load())
        {
            return false;
        }
        *voices = { { QStringLiteral("Microsoft Zira"), QStringLiteral("en-US") } };
        *defaultVoice = QStringLiteral("Microsoft Zira");
        return true;
    }
};

/// Counts the objects created and destroyed while it is installed (the hooks AddQObject and
/// RemoveQObject of QtCore; any thread)
class SpeechTestObjectCounter
{
public:
    /// The indices of qtHookData (QHooks::HookIndex of qhooks_p.h)
    static constexpr int HOOK_DATA_SIZE = 1;
    static constexpr int ADD_QOBJECT = 3;
    static constexpr int REMOVE_QOBJECT = 4;

    static bool isSupported() { return QT_PREPEND_NAMESPACE(qtHookData)[HOOK_DATA_SIZE] > quintptr(REMOVE_QOBJECT); }

    static void install()
    {
        added.store(0);
        removed.store(0);
        QT_PREPEND_NAMESPACE(qtHookData)[ADD_QOBJECT] = reinterpret_cast<quintptr>(&SpeechTestObjectCounter::onAdd);
        QT_PREPEND_NAMESPACE(qtHookData)[REMOVE_QOBJECT] = reinterpret_cast<quintptr>(&SpeechTestObjectCounter::onRemove);
    }

    static void uninstall()
    {
        QT_PREPEND_NAMESPACE(qtHookData)[ADD_QOBJECT] = 0;
        QT_PREPEND_NAMESPACE(qtHookData)[REMOVE_QOBJECT] = 0;
    }

    static inline std::atomic<int> added = 0;
    static inline std::atomic<int> removed = 0;

private:
    static void onAdd(QObject*) { ++added; }
    static void onRemove(QObject*) { ++removed; }
};

/// A sink of the test: the blocks and the formats; it may refuse the first block or cancel the run
class SpeechTestSink : public pdf::PDFAudioSink
{
public:
    class Control : public pdf::PDFOperationControl
    {
    public:
        virtual bool isOperationCanceled() const override { return m_isCanceled.load(); }
        void cancel() { m_isCanceled.store(true); }

    private:
        std::atomic<bool> m_isCanceled = false;
    };

    virtual bool writeAudio(const pdf::PDFAudioFormat& format, const QByteArray& samples) override
    {
        formats.push_back(format);
        bytes += samples.size();
        if (cancelAtFirstBlock)
        {
            cancelAtFirstBlock->cancel();
        }
        return !isRefusing;
    }

    std::vector<pdf::PDFAudioFormat> formats;
    qint64 bytes = 0;
    bool isRefusing = false;
    Control* cancelAtFirstBlock = nullptr;
};

/// The engine mock of the installation of Qt (plugins/texttospeech)
static std::shared_ptr<pdf::PDFQtSpeechSynthesizerFactory> createMockFactory()
{
    return std::make_shared<pdf::PDFQtSpeechSynthesizerFactory>(QStringLiteral("mock"));
}

void SpeechTest::preferredEngines()
{
    // R4-14 c: sapi gives no audio by synthesize, speechd cannot synthesize, mock is for tests
    const QStringList engines = pdf::PDFQtSpeechSynthesizerFactory::getPreferredEngines();
    QVERIFY(engines.contains(QStringLiteral("winrt")));
    QVERIFY(!engines.contains(QStringLiteral("sapi")));
    QVERIFY(!engines.contains(QStringLiteral("speechd")));
    QVERIFY(!engines.contains(QStringLiteral("mock")));
}

void SpeechTest::unknownEngine()
{
    pdf::PDFQtSpeechSynthesizerFactory factory(QStringLiteral("no-such-engine"));
    QVERIFY(!factory.isAvailable());
    QVERIFY(factory.getVoices().empty());
    QString error;
    QVERIFY(!factory.createSynthesizer(&error));
    QVERIFY(!error.isEmpty());
}

void SpeechTest::synthesisOfTheMockEngine()
{
    if (!pdf::PDFQtSpeechSynthesizerFactory::getInstalledEngines().contains(QStringLiteral("mock")))
    {
        QSKIP("The engine mock of Qt TextToSpeech is not installed.");
    }

    std::shared_ptr<pdf::PDFQtSpeechSynthesizerFactory> factory = createMockFactory();
    QVERIFY(factory->isAvailable());
    QCOMPARE(factory->getEngine(), QStringLiteral("mock"));
    QVERIFY(!factory->getVoices().empty());

    QString error;
    std::unique_ptr<pdf::PDFSpeechSynthesizer> synthesizer = factory->createSynthesizer(&error);
    QVERIFY2(synthesizer, qPrintable(error));
    QVERIFY(synthesizer->setVoice(factory->getVoices().front().name, &error));

    // The PCM of a plain text ('<' and '&' are characters, B70); two texts one after another
    for (const QString& text : { QStringLiteral("Hello <world> & you."), QStringLiteral("Second text.") })
    {
        SpeechTestSink sink;
        QVERIFY2(synthesizer->synthesize(text, &sink, nullptr, &error), qPrintable(error));
        QVERIFY(!sink.formats.empty());
        QVERIFY(sink.bytes > 0);
        for (const pdf::PDFAudioFormat& format : sink.formats)
        {
            QVERIFY(format.isValid());
        }
        QCOMPARE(sink.bytes % sink.formats.front().getBytesPerFrame(), qint64(0));
    }

    // An empty text is nothing to say
    SpeechTestSink empty;
    QVERIFY(synthesizer->synthesize(QStringLiteral("  "), &empty, nullptr, &error));
    QVERIFY(empty.formats.empty());
}

void SpeechTest::synthesisInAThreadOfARun()
{
    if (!pdf::PDFQtSpeechSynthesizerFactory::getInstalledEngines().contains(QStringLiteral("mock")))
    {
        QSKIP("The engine mock of Qt TextToSpeech is not installed.");
    }

    // The run creates and uses its synthesizer in a thread of the pool; the speech has its own
    // thread (R4-14 c), the thread of the run only waits
    std::shared_ptr<pdf::PDFQtSpeechSynthesizerFactory> factory = createMockFactory();
    bool isOk = false;
    qint64 bytes = 0;
    QString error;
    std::thread run([&]()
    {
        std::unique_ptr<pdf::PDFSpeechSynthesizer> synthesizer = factory->createSynthesizer(&error);
        if (synthesizer)
        {
            SpeechTestSink sink;
            isOk = synthesizer->synthesize(QStringLiteral("A text of a thread."), &sink, nullptr, &error);
            bytes = sink.bytes;
        }
    });
    run.join();
    QVERIFY2(isOk, qPrintable(error));
    QVERIFY(bytes > 0);
}

void SpeechTest::cancellation()
{
    if (!pdf::PDFQtSpeechSynthesizerFactory::getInstalledEngines().contains(QStringLiteral("mock")))
    {
        QSKIP("The engine mock of Qt TextToSpeech is not installed.");
    }

    // Stop after the first block of a long text: synthesize returns false without a reason within
    // the stop of the engine (100 ms polling, at most STOP_TIMEOUT)
    std::shared_ptr<pdf::PDFQtSpeechSynthesizerFactory> factory = createMockFactory();
    QString error;
    std::unique_ptr<pdf::PDFSpeechSynthesizer> synthesizer = factory->createSynthesizer(&error);
    QVERIFY(synthesizer);

    SpeechTestSink::Control control;
    SpeechTestSink sink;
    sink.cancelAtFirstBlock = &control;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!synthesizer->synthesize(QStringLiteral("This is a long text, which is never finished. ").repeated(40), &sink, &control, &error));
    QVERIFY(error.isEmpty());
    const double factor = qMax(1.0, qEnvironmentVariable("PDF4QT_TEST_TIMEOUT_FACTOR").toDouble());
    QVERIFY2(timer.elapsed() < qint64((pdf::PDFQtSpeechSynthesizer::STOP_TIMEOUT + 2000) * factor), qPrintable(QString::number(timer.elapsed())));

    // The synthesizer works again after a stop: the blocks of the stopped text are not taken
    SpeechTestSink next;
    QVERIFY2(synthesizer->synthesize(QStringLiteral("Next."), &next, nullptr, &error), qPrintable(error));
    QVERIFY(next.bytes > 0);
}

void SpeechTest::sinkRefuses()
{
    if (!pdf::PDFQtSpeechSynthesizerFactory::getInstalledEngines().contains(QStringLiteral("mock")))
    {
        QSKIP("The engine mock of Qt TextToSpeech is not installed.");
    }

    // A sink, which cannot take a block (a write error), stops the synthesis
    std::shared_ptr<pdf::PDFQtSpeechSynthesizerFactory> factory = createMockFactory();
    QString error;
    std::unique_ptr<pdf::PDFSpeechSynthesizer> synthesizer = factory->createSynthesizer(&error);
    QVERIFY(synthesizer);
    SpeechTestSink sink;
    sink.isRefusing = true;
    QVERIFY(!synthesizer->synthesize(QStringLiteral("Refused text of the sink."), &sink, nullptr, &error));
    QCOMPARE(int(sink.formats.size()), 1);
}

void SpeechTest::unknownVoiceNamesTheVoices()
{
    if (!pdf::PDFQtSpeechSynthesizerFactory::getInstalledEngines().contains(QStringLiteral("mock")))
    {
        QSKIP("The engine mock of Qt TextToSpeech is not installed.");
    }

    // MCP: an unknown voice is an error with the names of the voices (plan 7)
    std::shared_ptr<pdf::PDFQtSpeechSynthesizerFactory> factory = createMockFactory();
    QString error;
    std::unique_ptr<pdf::PDFSpeechSynthesizer> synthesizer = factory->createSynthesizer(&error);
    QVERIFY(synthesizer);
    QVERIFY(!synthesizer->setVoice(QStringLiteral("Nobody"), &error));
    for (const pdf::PDFSpeechVoice& voice : factory->getVoices())
    {
        QVERIFY2(error.contains(voice.name), qPrintable(error));
    }
    QVERIFY(synthesizer->setVoice(QString(), &error));
}

void SpeechTest::selectVoice()
{
    using Selector = pdf::PDFSpeechVoiceSelector;

    // R4-15: the attributes of PdfTool --voice-name, --voice-gender, --voice-age, --voice-lang-code
    const std::vector<pdf::PDFSpeechVoice> voices = {
        { QStringLiteral("Microsoft Zira"), QStringLiteral("en-US"), pdf::PDFSpeechVoice::Gender::Female, pdf::PDFSpeechVoice::Age::Adult },
        { QStringLiteral("Microsoft David"), QStringLiteral("en-US"), pdf::PDFSpeechVoice::Gender::Male, pdf::PDFSpeechVoice::Age::Adult },
        { QStringLiteral("Microsoft Hazel"), QStringLiteral("en-GB"), pdf::PDFSpeechVoice::Gender::Female, pdf::PDFSpeechVoice::Age::Adult },
        { QStringLiteral("Microsoft Jakub"), QStringLiteral("cs-CZ"), pdf::PDFSpeechVoice::Gender::Male, pdf::PDFSpeechVoice::Age::Senior },
    };

    QString error;
    Selector::Criteria criteria;
    criteria.gender = QStringLiteral("male");
    QCOMPARE(Selector::selectVoice(voices, criteria, &error)->name, QStringLiteral("Microsoft David"));

    criteria.languageCode = QStringLiteral("cs");
    QCOMPARE(Selector::selectVoice(voices, criteria, &error)->name, QStringLiteral("Microsoft Jakub"));

    criteria = Selector::Criteria();
    criteria.languageCode = QStringLiteral("en_GB");
    QCOMPARE(Selector::selectVoice(voices, criteria, &error)->name, QStringLiteral("Microsoft Hazel"));

    criteria = Selector::Criteria();
    criteria.name = QStringLiteral("microsoft zira");
    QCOMPARE(Selector::selectVoice(voices, criteria, &error)->name, QStringLiteral("Microsoft Zira"));

    criteria = Selector::Criteria();
    criteria.age = QStringLiteral("senior");
    QCOMPARE(Selector::selectVoice(voices, criteria, &error)->name, QStringLiteral("Microsoft Jakub"));

    // No match: the reason names the voices; an unknown gender is an error of its own
    criteria = Selector::Criteria();
    criteria.languageCode = QStringLiteral("de");
    QVERIFY(!Selector::selectVoice(voices, criteria, &error));
    QVERIFY2(error.contains(QStringLiteral("Microsoft Hazel")), qPrintable(error));

    criteria = Selector::Criteria();
    criteria.gender = QStringLiteral("robot");
    QVERIFY(!Selector::selectVoice(voices, criteria, &error));
    QVERIFY2(error.contains(QStringLiteral("robot")), qPrintable(error));

    QVERIFY(!Selector::selectVoice({}, Selector::Criteria(), &error));
}

void SpeechTest::voicesOfAllLanguages()
{
    if (!pdf::PDFQtSpeechSynthesizerFactory::getInstalledEngines().contains(QStringLiteral("mock")))
    {
        QSKIP("The engine mock of Qt TextToSpeech is not installed.");
    }

    // 341:9657 groups the voices by their language, /Lang of an item takes a voice of its language
    // (14.9.2), PdfTool --voice-lang-code: the voices of every language of the engine, not only of
    // its current locale. The engine mock of Qt has the locales en-GB (Bob, Anne), en-US (Charly,
    // Mary), nb-NO (Eivind, Kjersti), nn-NO (Anders, Ingvild) and fi-FI (Kari, Anneli).
    std::shared_ptr<pdf::PDFQtSpeechSynthesizerFactory> factory = createMockFactory();
    QStringList names;
    QSet<QString> languages;
    for (const pdf::PDFSpeechVoice& voice : factory->getVoices())
    {
        names << voice.name;
        languages.insert(voice.languageCode.section(QLatin1Char('-'), 0, 0));
    }
    for (const char* name : { "Bob", "Anne", "Charly", "Mary", "Eivind", "Kjersti", "Anders", "Ingvild", "Kari", "Anneli" })
    {
        QVERIFY2(names.contains(QString::fromLatin1(name)), qPrintable(names.join(QStringLiteral(", "))));
    }
    QVERIFY(languages.contains(QStringLiteral("en")));
    QVERIFY(languages.contains(QStringLiteral("fi")));

    // A voice of another language than the current one speaks
    QString error;
    std::unique_ptr<pdf::PDFSpeechSynthesizer> synthesizer = factory->createSynthesizer(&error);
    QVERIFY2(synthesizer, qPrintable(error));
    QVERIFY2(synthesizer->setVoice(QStringLiteral("Kari"), &error), qPrintable(error));
    SpeechTestSink sink;
    QVERIFY2(synthesizer->synthesize(QStringLiteral("Hyvää päivää."), &sink, nullptr, &error), qPrintable(error));
    QVERIFY(sink.bytes > 0);
    QCOMPARE(static_cast<pdf::PDFQtSpeechSynthesizer*>(synthesizer.get())->getSpokenVoice(), QStringLiteral("Kari"));
}

void SpeechTest::emptyVoiceIsTheDefaultVoice()
{
    if (!pdf::PDFQtSpeechSynthesizerFactory::getInstalledEngines().contains(QStringLiteral("mock")))
    {
        QSKIP("The engine mock of Qt TextToSpeech is not installed.");
    }

    // PDFSpeechSynthesizer::setVoice: an empty name is the default voice - also after a voice of
    // another language of an item (/Lang, 14.9.2), so the items after it are read by the default
    // voice again (MCP and PdfTool run with the default voice)
    std::shared_ptr<pdf::PDFQtSpeechSynthesizerFactory> factory = createMockFactory();
    QString error;
    std::unique_ptr<pdf::PDFSpeechSynthesizer> synthesizer = factory->createSynthesizer(&error);
    QVERIFY2(synthesizer, qPrintable(error));
    const pdf::PDFQtSpeechSynthesizer* qtSynthesizer = static_cast<const pdf::PDFQtSpeechSynthesizer*>(synthesizer.get());

    SpeechTestSink first;
    QVERIFY2(synthesizer->synthesize(QStringLiteral("First."), &first, nullptr, &error), qPrintable(error));
    const QString defaultVoice = qtSynthesizer->getSpokenVoice();
    QVERIFY(!defaultVoice.isEmpty());

    // A voice of another language than the default one
    QString otherVoice;
    for (const pdf::PDFSpeechVoice& voice : factory->getVoices())
    {
        if (voice.name != defaultVoice && voice.languageCode.startsWith(QStringLiteral("fi")))
        {
            otherVoice = voice.name;
        }
    }
    QVERIFY(!otherVoice.isEmpty());
    QVERIFY(synthesizer->setVoice(otherVoice, &error));
    SpeechTestSink second;
    QVERIFY2(synthesizer->synthesize(QStringLiteral("Second."), &second, nullptr, &error), qPrintable(error));
    QCOMPARE(qtSynthesizer->getSpokenVoice(), otherVoice);

    QVERIFY(synthesizer->setVoice(QString(), &error));
    SpeechTestSink third;
    QVERIFY2(synthesizer->synthesize(QStringLiteral("Third."), &third, nullptr, &error), qPrintable(error));
    QCOMPARE(qtSynthesizer->getSpokenVoice(), defaultVoice);
}

void SpeechTest::synthesizerLeavesNoObject()
{
    if (!pdf::PDFQtSpeechSynthesizerFactory::getInstalledEngines().contains(QStringLiteral("mock")))
    {
        QSKIP("The engine mock of Qt TextToSpeech is not installed.");
    }
    if (!SpeechTestObjectCounter::isSupported())
    {
        QSKIP("The hooks of QtCore are not available.");
    }

    // The worker and its QTextToSpeech live in the thread of the speech and are deleted there, when
    // the synthesizer is deleted: every object created by a synthesizer is destroyed with it
    // (a queued call of deleteLater before QThread::quit is dropped, when the event loop of the
    // thread ends first - the worker and the engine would stay forever, a run after a run)
    std::shared_ptr<pdf::PDFQtSpeechSynthesizerFactory> factory = createMockFactory();
    QString error;
    {
        // The plugin of the engine is loaded once, by the first synthesizer
        std::unique_ptr<pdf::PDFSpeechSynthesizer> warmUp = factory->createSynthesizer(&error);
        QVERIFY2(warmUp, qPrintable(error));
        SpeechTestSink sink;
        QVERIFY2(warmUp->synthesize(QStringLiteral("Warm up."), &sink, nullptr, &error), qPrintable(error));
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    for (int run = 0; run < 5; ++run)
    {
        SpeechTestObjectCounter::install();
        {
            std::unique_ptr<pdf::PDFSpeechSynthesizer> synthesizer = factory->createSynthesizer(&error);
            if (synthesizer)
            {
                SpeechTestSink sink;
                synthesizer->synthesize(QStringLiteral("A text of a run."), &sink, nullptr, &error);
            }
        }
        SpeechTestObjectCounter::uninstall();
        QVERIFY(SpeechTestObjectCounter::added.load() > 0);
        QCOMPARE(SpeechTestObjectCounter::removed.load(), SpeechTestObjectCounter::added.load());
    }
}

void SpeechTest::probeAgainAfterNoVoice()
{
    // RS4AUDIO Low, 341:11665 "open this tool again": a probe, which has found no voice, is not
    // final - the next question probes again; a probe, which has found a voice, is final
    SpeechTestLateVoiceFactory factory;
    QVERIFY(!factory.isAvailable());
    QVERIFY(factory.getVoices().empty());
    QCOMPARE(factory.probes.load(), 2);

    factory.hasVoice.store(true);
    QVERIFY(factory.isAvailable());
    QCOMPARE(factory.getVoices().size(), size_t(1));
    QCOMPARE(factory.getVoices().front().name, QStringLiteral("Microsoft Zira"));
    QCOMPARE(factory.getDefaultVoice(), QStringLiteral("Microsoft Zira"));
    QCOMPARE(factory.getEngine(), QStringLiteral("late"));
    QCOMPARE(factory.probes.load(), 3);
}

void SpeechTest::defaultVoiceOfTheFactory()
{
    // The default voice of the engine (the voice of an empty name, the setting of the system):
    // the engine mock of Qt starts in en-GB with its voice Bob (qtexttospeech_mock.cpp)
    if (!pdf::PDFQtSpeechSynthesizerFactory::getInstalledEngines().contains(QStringLiteral("mock")))
    {
        QSKIP("The engine mock of Qt TextToSpeech is not installed.");
    }

    std::shared_ptr<pdf::PDFQtSpeechSynthesizerFactory> factory = createMockFactory();
    QCOMPARE(factory->getDefaultVoice(), QStringLiteral("Bob"));

    // A synthesis without a chosen voice speaks by it
    QString error;
    std::unique_ptr<pdf::PDFSpeechSynthesizer> synthesizer = factory->createSynthesizer(&error);
    QVERIFY2(synthesizer, qPrintable(error));
    pdf::PDFQtSpeechSynthesizer* qtSynthesizer = dynamic_cast<pdf::PDFQtSpeechSynthesizer*>(synthesizer.get());
    QVERIFY(qtSynthesizer);
    QCOMPARE(qtSynthesizer->getDefaultVoice(), QStringLiteral("Bob"));
    SpeechTestSink sink;
    QVERIFY2(synthesizer->synthesize(QStringLiteral("Default."), &sink, nullptr, &error), qPrintable(error));
    QCOMPARE(qtSynthesizer->getSpokenVoice(), factory->getDefaultVoice());
}

QTEST_GUILESS_MAIN(SpeechTest)

#include "tst_speechtest.moc"
