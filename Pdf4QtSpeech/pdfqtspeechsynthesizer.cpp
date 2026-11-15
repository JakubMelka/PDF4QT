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

#include "pdfqtspeechsynthesizer.h"
#include "pdfoperationcontrol.h"
#include "pdfthreadannotations.h"

#include <QAudioFormat>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QTextToSpeech>
#include <QThread>
#include <QVoice>
#include <QWaitCondition>

#include <deque>

namespace pdf
{

/// The state shared by the thread of a run (PDFQtSpeechSynthesizer) and the thread of the speech
/// (PDFQtSpeechWorker); every member is guarded by \p mutex. The worker pushes the blocks and the
/// end of a synthesis, the run takes them. A synthesis has its generation: a late block or end of
/// a stopped synthesis is never taken for the next one.
class PDFQtSpeechSession
{
public:
    struct Block
    {
        quint64 generation = 0;
        PDFAudioFormat format;
        QByteArray samples;
    };

    QMutex mutex;
    QWaitCondition condition;

    // The start of the engine (PDFQtSpeechWorker::initialize)
    bool isInitialized = false;
    bool isValid = false;
    QString initializationError;
    std::vector<PDFSpeechVoice> voices;
    QString defaultVoice;               ///< The voice of the engine at its start (the system setting)

    // The synthesis
    QString spokenVoice;                ///< The voice of the engine of the last synthesis
    std::deque<Block> blocks;
    quint64 finishedGeneration = 0;     ///< The last synthesis, which has ended (Ready or Error)
    quint64 failedGeneration = 0;       ///< The last synthesis, which has failed
    QString failure;                    ///< The reason of the failed synthesis
};

/// The worker of the thread of the speech: it owns QTextToSpeech (a child), created and deleted in
/// that thread. Its functions are called by queued invocations of the synthesizer.
class PDFQtSpeechWorker : public QObject
{
public:
    explicit PDFQtSpeechWorker(std::shared_ptr<PDFQtSpeechSession> session) :
        m_session(std::move(session))
    {

    }

    void initialize(const QString& engine);
    void start(const QString& text, const QString& voice, double rate, double volume, quint64 generation);
    void stop();

private:
    void onAudio(const QAudioFormat& format, const QByteArray& samples);
    void onStateChanged(QTextToSpeech::State state);
    void onErrorOccurred(QTextToSpeech::ErrorReason reason, const QString& errorString);
    void finish(bool isFailed, const QString& failure);

    std::shared_ptr<PDFQtSpeechSession> m_session;
    QTextToSpeech* m_speech = nullptr;
    QList<QVoice> m_voices;     ///< The voices of all languages of the engine (QTextToSpeech::findVoices)
    QVoice m_defaultVoice;      ///< The voice of the engine at its start (an empty name of a voice)
    quint64 m_generation = 0;
    bool m_isActive = false;
};

/// Helpers of the speech
class PDFQtSpeechHelper
{
public:
    PDFQtSpeechHelper() = delete;

    static PDFSpeechVoice createVoice(const QVoice& voice)
    {
        PDFSpeechVoice result;
        result.name = voice.name();

        // QLocale::bcp47Name leaves out the region of the default region of a language ("en"):
        // the name of the locale keeps it ("en_US" -> "en-US")
        result.languageCode = voice.locale().name().replace(QLatin1Char('_'), QLatin1Char('-'));
        switch (voice.gender())
        {
            case QVoice::Male:
                result.gender = PDFSpeechVoice::Gender::Male;
                break;
            case QVoice::Female:
                result.gender = PDFSpeechVoice::Gender::Female;
                break;
            default:
                break;
        }
        switch (voice.age())
        {
            case QVoice::Child:
                result.age = PDFSpeechVoice::Age::Child;
                break;
            case QVoice::Teenager:
                result.age = PDFSpeechVoice::Age::Teenager;
                break;
            case QVoice::Adult:
                result.age = PDFSpeechVoice::Age::Adult;
                break;
            case QVoice::Senior:
                result.age = PDFSpeechVoice::Age::Senior;
                break;
            default:
                break;
        }
        return result;
    }

    static PDFAudioFormat createFormat(const QAudioFormat& format)
    {
        PDFAudioFormat result;
        result.sampleRate = format.sampleRate();
        result.channelCount = format.channelCount();
        switch (format.sampleFormat())
        {
            case QAudioFormat::UInt8:
                result.sampleFormat = PDFAudioFormat::SampleFormat::UInt8;
                break;
            case QAudioFormat::Int16:
                result.sampleFormat = PDFAudioFormat::SampleFormat::Int16;
                break;
            case QAudioFormat::Int32:
                result.sampleFormat = PDFAudioFormat::SampleFormat::Int32;
                break;
            case QAudioFormat::Float:
                result.sampleFormat = PDFAudioFormat::SampleFormat::Float;
                break;
            default:
                break;
        }
        return result;
    }

    static QString getGenderText(PDFSpeechVoice::Gender gender)
    {
        switch (gender)
        {
            case PDFSpeechVoice::Gender::Male:
                return QStringLiteral("male");
            case PDFSpeechVoice::Gender::Female:
                return QStringLiteral("female");
            case PDFSpeechVoice::Gender::Unknown:
                break;
        }
        return QString();
    }

    static QString getAgeText(PDFSpeechVoice::Age age)
    {
        switch (age)
        {
            case PDFSpeechVoice::Age::Child:
                return QStringLiteral("child");
            case PDFSpeechVoice::Age::Teenager:
                return QStringLiteral("teenager");
            case PDFSpeechVoice::Age::Adult:
                return QStringLiteral("adult");
            case PDFSpeechVoice::Age::Senior:
                return QStringLiteral("senior");
            case PDFSpeechVoice::Age::Unknown:
                break;
        }
        return QString();
    }

    /// "en-US" / "en_us" -> "en-us"
    static QString normalizeLanguage(const QString& code)
    {
        return code.trimmed().toLower().replace(QLatin1Char('_'), QLatin1Char('-'));
    }
};

// --------------------------------------------------------- PDFQtSpeechWorker

void PDFQtSpeechWorker::initialize(const QString& engine)
{
    // The speech lives in this thread from its creation to its deletion (a child of the worker)
    m_speech = new QTextToSpeech(engine, this);
    connect(m_speech, &QTextToSpeech::stateChanged, this, &PDFQtSpeechWorker::onStateChanged);
    connect(m_speech, &QTextToSpeech::errorOccurred, this, &PDFQtSpeechWorker::onErrorOccurred);

    std::vector<PDFSpeechVoice> voices;
    QString error;
    const bool isValid = m_speech->state() != QTextToSpeech::Error && m_speech->engineCapabilities().testFlag(QTextToSpeech::Capability::Synthesize);
    if (isValid)
    {
        // The voices of every language (341:9657 groups them by the language, /Lang of an item
        // takes a voice of its language, PdfTool --voice-lang-code); availableVoices would give only
        // the voices of the current locale of the engine. findVoices goes through the locales and
        // gives the voice back; the default voice is kept for the empty name of a voice.
        m_defaultVoice = m_speech->voice();
        m_voices = m_speech->findVoices();
        if (m_speech->voice() != m_defaultVoice)
        {
            m_speech->setVoice(m_defaultVoice);
        }
        for (const QVoice& voice : m_voices)
        {
            voices.push_back(PDFQtSpeechHelper::createVoice(voice));
        }
    }
    else
    {
        error = m_speech->state() == QTextToSpeech::Error ? m_speech->errorString() : PDFQtSpeechSynthesizer::tr("The text-to-speech engine %1 cannot create audio files.").arg(engine);
    }

    QMutexLocker locker(&m_session->mutex);
    m_session->isInitialized = true;
    m_session->isValid = isValid;
    m_session->initializationError = error;
    m_session->voices = std::move(voices);
    m_session->defaultVoice = isValid ? m_defaultVoice.name() : QString();
    m_session->condition.wakeAll();
}

void PDFQtSpeechWorker::start(const QString& text, const QString& voice, double rate, double volume, quint64 generation)
{
    PDFThreadAnnotations::happensAfter(m_session.get());

    m_generation = generation;
    m_isActive = true;
    if (!m_speech)
    {
        finish(true, PDFQtSpeechSynthesizer::tr("The text-to-speech engine has not started."));
        return;
    }

    // The voice of its name of any language, the default voice for an empty name (also after a
    // voice of another language: PDFSpeechSynthesizer::setVoice)
    QVoice targetVoice = m_defaultVoice;
    for (const QVoice& item : m_voices)
    {
        if (!voice.isEmpty() && item.name() == voice)
        {
            targetVoice = item;
            break;
        }
    }
    if (m_speech->voice() != targetVoice)
    {
        m_speech->setVoice(targetVoice);
    }
    m_speech->setRate(rate);
    m_speech->setVolume(volume);

    {
        QMutexLocker locker(&m_session->mutex);
        m_session->spokenVoice = m_speech->voice().name();
    }

    // Plain text (B70: no SSML of SAPI, '<' and '&' are characters); the blocks come to onAudio
    // in this thread, the end by stateChanged
    m_speech->synthesize(text, this, &PDFQtSpeechWorker::onAudio);
}

void PDFQtSpeechWorker::stop()
{
    if (m_speech && m_isActive)
    {
        m_speech->stop(QTextToSpeech::BoundaryHint::Immediate);
    }
}

void PDFQtSpeechWorker::onAudio(const QAudioFormat& format, const QByteArray& samples)
{
    if (!m_isActive)
    {
        return;
    }

    QMutexLocker locker(&m_session->mutex);
    m_session->blocks.push_back({ m_generation, PDFQtSpeechHelper::createFormat(format), samples });
    m_session->condition.wakeAll();
}

void PDFQtSpeechWorker::onStateChanged(QTextToSpeech::State state)
{
    if (!m_isActive)
    {
        return;
    }

    if (state == QTextToSpeech::Ready)
    {
        finish(false, QString());
    }
    else if (state == QTextToSpeech::Error)
    {
        finish(true, m_speech ? m_speech->errorString() : QString());
    }
}

void PDFQtSpeechWorker::onErrorOccurred(QTextToSpeech::ErrorReason reason, const QString& errorString)
{
    Q_UNUSED(reason);
    if (m_isActive)
    {
        finish(true, errorString);
    }
}

void PDFQtSpeechWorker::finish(bool isFailed, const QString& failure)
{
    m_isActive = false;

    QMutexLocker locker(&m_session->mutex);
    m_session->finishedGeneration = m_generation;
    if (isFailed)
    {
        m_session->failedGeneration = m_generation;
        m_session->failure = failure.isEmpty() ? PDFQtSpeechSynthesizer::tr("The text-to-speech engine has failed.") : failure;
    }
    m_session->condition.wakeAll();
}

// --------------------------------------------------------- PDFSpeechVoiceSelector

std::optional<PDFSpeechVoice> PDFSpeechVoiceSelector::selectVoice(const std::vector<PDFSpeechVoice>& voices, const Criteria& criteria, QString* errorMessage)
{
    using Helper = PDFQtSpeechHelper;

    auto setError = [errorMessage](const QString& message)
    {
        if (errorMessage)
        {
            *errorMessage = message;
        }
    };

    const QString gender = criteria.gender.trimmed().toLower();
    if (!gender.isEmpty() && gender != QLatin1String("male") && gender != QLatin1String("female"))
    {
        setError(tr("Unknown gender of a voice '%1'. Valid values are male, female.").arg(criteria.gender));
        return std::nullopt;
    }

    const QString age = criteria.age.trimmed().toLower();
    if (!age.isEmpty() && age != QLatin1String("child") && age != QLatin1String("teenager") && age != QLatin1String("adult") && age != QLatin1String("senior"))
    {
        setError(tr("Unknown age of a voice '%1'. Valid values are child, teenager, adult, senior.").arg(criteria.age));
        return std::nullopt;
    }

    const QString language = Helper::normalizeLanguage(criteria.languageCode);
    const bool hasRegion = language.contains(QLatin1Char('-'));
    for (const PDFSpeechVoice& voice : voices)
    {
        if (!criteria.name.trimmed().isEmpty() && voice.name.compare(criteria.name.trimmed(), Qt::CaseInsensitive) != 0)
        {
            continue;
        }
        if (!gender.isEmpty() && Helper::getGenderText(voice.gender) != gender)
        {
            continue;
        }
        if (!age.isEmpty() && Helper::getAgeText(voice.age) != age)
        {
            continue;
        }
        if (!language.isEmpty())
        {
            const QString voiceLanguage = Helper::normalizeLanguage(voice.languageCode);
            const QString compared = hasRegion ? voiceLanguage : voiceLanguage.section(QLatin1Char('-'), 0, 0);
            if (compared != language)
            {
                continue;
            }
        }
        return voice;
    }

    setError(voices.empty() ? tr("No text-to-speech voice is available.")
                            : tr("No voice matches the given name, gender, age and language. The voices are: %1.").arg(getNames(voices)));
    return std::nullopt;
}

QString PDFSpeechVoiceSelector::getDescription(const PDFSpeechVoice& voice)
{
    QStringList attributes;
    if (!voice.languageCode.isEmpty())
    {
        attributes << voice.languageCode;
    }
    const QString gender = PDFQtSpeechHelper::getGenderText(voice.gender);
    if (!gender.isEmpty())
    {
        attributes << gender;
    }
    const QString age = PDFQtSpeechHelper::getAgeText(voice.age);
    if (!age.isEmpty())
    {
        attributes << age;
    }
    return attributes.isEmpty() ? voice.name : QStringLiteral("%1 (%2)").arg(voice.name, attributes.join(QStringLiteral(", ")));
}

QString PDFSpeechVoiceSelector::getNames(const std::vector<PDFSpeechVoice>& voices)
{
    QStringList names;
    for (const PDFSpeechVoice& voice : voices)
    {
        names << voice.name;
    }
    return names.join(QStringLiteral(", "));
}

// --------------------------------------------------------- PDFQtSpeechSynthesizer

PDFQtSpeechSynthesizer::PDFQtSpeechSynthesizer(QString engine) :
    m_engine(std::move(engine)),
    m_session(std::make_shared<PDFQtSpeechSession>()),
    m_thread(std::make_unique<QThread>())
{
    // Ownership: the worker has no parent and lives in m_thread; the destructor deletes it there
    // (deleteLater before quit, QThread deletes the deferred objects when it finishes)
    m_thread->setObjectName(QStringLiteral("PDF4QT speech"));
    m_worker = new PDFQtSpeechWorker(m_session);
    m_worker->moveToThread(m_thread.get());
    m_thread->start();

    {
        PDFThreadAnnotations::TransportScope scope;
        QMetaObject::invokeMethod(m_worker, &PDFQtSpeechWorker::initialize, Qt::QueuedConnection, m_engine);
    }

    // The engine starts in its thread: the voices and the capabilities are known then
    const QDeadlineTimer deadline(ENGINE_TIMEOUT);
    QMutexLocker locker(&m_session->mutex);
    while (!m_session->isInitialized && !deadline.hasExpired())
    {
        PDFThreadAnnotations::wait(m_session->condition, &m_session->mutex, deadline);
    }
    if (!m_session->isInitialized)
    {
        m_session->initializationError = tr("The text-to-speech engine %1 does not respond.").arg(m_engine);
    }
}

PDFQtSpeechSynthesizer::~PDFQtSpeechSynthesizer()
{
    if (m_worker)
    {
        // deleteLater itself (thread-safe), never a queued call of it: quit can end the event loop
        // of the thread before a queued call is delivered (the call is dropped with the thread, the
        // worker and its QTextToSpeech would never be deleted), but QThread deletes the deferred
        // objects, when it finishes - in the thread of the speech
        PDFThreadAnnotations::TransportScope scope;
        m_worker->deleteLater();
        m_worker = nullptr;
    }
    m_thread->quit();

    // The wait has no limit on purpose: the worker and its QTextToSpeech belong to the thread and
    // are deleted only when it finishes; a QThread destroyed while it runs ends the process
    // (qFatal), and leaving the thread alone would leave an engine, which may still call into the
    // freed session and worker. A synthesis is stopped before (stopAndWait, at most STOP_TIMEOUT)
    // and the event loop of the thread then ends at once; only an engine, which hangs inside of a
    // call of its own, can hold the destructor - the same engine would hang its GUI thread too.
    m_thread->wait();
}

bool PDFQtSpeechSynthesizer::isValid() const
{
    QMutexLocker locker(&m_session->mutex);
    return m_session->isInitialized && m_session->isValid;
}

QString PDFQtSpeechSynthesizer::getErrorMessage() const
{
    QMutexLocker locker(&m_session->mutex);
    return m_session->initializationError;
}

std::vector<PDFSpeechVoice> PDFQtSpeechSynthesizer::getVoices() const
{
    QMutexLocker locker(&m_session->mutex);
    return m_session->voices;
}

QString PDFQtSpeechSynthesizer::getSpokenVoice() const
{
    QMutexLocker locker(&m_session->mutex);
    return m_session->spokenVoice;
}

QString PDFQtSpeechSynthesizer::getDefaultVoice() const
{
    QMutexLocker locker(&m_session->mutex);
    return m_session->defaultVoice;
}

bool PDFQtSpeechSynthesizer::setVoice(const QString& voiceName, QString* errorMessage)
{
    if (voiceName.isEmpty())
    {
        m_voice.clear();
        return true;
    }

    const std::vector<PDFSpeechVoice> voices = getVoices();
    for (const PDFSpeechVoice& voice : voices)
    {
        if (voice.name == voiceName)
        {
            m_voice = voiceName;
            return true;
        }
    }

    if (errorMessage)
    {
        *errorMessage = voices.empty() ? tr("The voice '%1' does not exist: the text-to-speech engine has no voice.").arg(voiceName)
                                       : tr("The voice '%1' does not exist. The voices are: %2.").arg(voiceName, PDFSpeechVoiceSelector::getNames(voices));
    }
    return false;
}

void PDFQtSpeechSynthesizer::setRate(double rate)
{
    m_rate = qBound(-1.0, rate, 1.0);
}

void PDFQtSpeechSynthesizer::setVolume(double volume)
{
    m_volume = qBound(0.0, volume, 1.0);
}

void PDFQtSpeechSynthesizer::stopAndWait()
{
    {
        PDFThreadAnnotations::TransportScope scope;
        QMetaObject::invokeMethod(m_worker, &PDFQtSpeechWorker::stop, Qt::QueuedConnection);
    }

    // The stopped synthesis ends by Ready (or Error); its blocks are dropped
    const QDeadlineTimer deadline(STOP_TIMEOUT);
    QMutexLocker locker(&m_session->mutex);
    while (m_session->finishedGeneration < m_generationCounter && !deadline.hasExpired())
    {
        PDFThreadAnnotations::wait(m_session->condition, &m_session->mutex, deadline);
    }
    m_session->blocks.clear();

    // An engine, which has not ended its synthesis within STOP_TIMEOUT, is never used again: its
    // late blocks would be delivered to the functor of the next synthesis (QTextToSpeech keeps
    // one functor and queues a text given while it synthesizes), so they would carry the tag of
    // the next generation. The synthesizer refuses every further text.
    if (m_session->finishedGeneration < m_generationCounter)
    {
        m_isStopTimedOut = true;
    }
}

bool PDFQtSpeechSynthesizer::synthesize(const QString& text, PDFAudioSink* sink, const PDFOperationControl* operationControl, QString* errorMessage)
{
    if (errorMessage)
    {
        errorMessage->clear();
    }

    if (!isValid())
    {
        if (errorMessage)
        {
            *errorMessage = getErrorMessage();
        }
        return false;
    }

    if (m_isStopTimedOut)
    {
        if (errorMessage)
        {
            *errorMessage = tr("The text-to-speech engine %1 does not respond.").arg(m_engine);
        }
        return false;
    }

    if (text.trimmed().isEmpty())
    {
        return true;
    }

    // A new synthesis: the blocks of an earlier one are gone
    const quint64 generation = ++m_generationCounter;
    {
        QMutexLocker locker(&m_session->mutex);
        m_session->blocks.clear();
    }

    PDFThreadAnnotations::happensBefore(m_session.get());
    {
        PDFThreadAnnotations::TransportScope scope;
        QMetaObject::invokeMethod(m_worker, &PDFQtSpeechWorker::start, Qt::QueuedConnection, text, m_voice, m_rate, m_volume, generation);
    }

    QElapsedTimer idleTimer;
    idleTimer.start();
    while (true)
    {
        std::vector<PDFQtSpeechSession::Block> blocks;
        bool isFinished = false;
        bool isFailed = false;
        bool isCanceled = false;
        bool isTimedOut = false;
        QString failure;

        {
            QMutexLocker locker(&m_session->mutex);
            while (m_session->blocks.empty() && m_session->finishedGeneration < generation)
            {
                // The cancellation every 100 ms (R4-14 c)
                if (PDFOperationControl::isOperationCanceled(operationControl))
                {
                    isCanceled = true;
                    break;
                }
                if (idleTimer.elapsed() > ENGINE_TIMEOUT)
                {
                    isTimedOut = true;
                    break;
                }
                PDFThreadAnnotations::wait(m_session->condition, &m_session->mutex, QDeadlineTimer(100));
            }

            while (!m_session->blocks.empty())
            {
                if (m_session->blocks.front().generation == generation)
                {
                    blocks.push_back(std::move(m_session->blocks.front()));
                }
                m_session->blocks.pop_front();
            }
            isFinished = m_session->finishedGeneration >= generation;
            isFailed = m_session->failedGeneration == generation;
            failure = m_session->failure;
        }

        if (isCanceled || PDFOperationControl::isOperationCanceled(operationControl))
        {
            if (!isFinished)
            {
                stopAndWait();
            }
            return false;
        }

        for (const PDFQtSpeechSession::Block& block : blocks)
        {
            if (!sink->writeAudio(block.format, block.samples))
            {
                // The reason is the error of the sink
                if (!isFinished)
                {
                    stopAndWait();
                }
                return false;
            }
        }

        if (!blocks.empty())
        {
            idleTimer.restart();
        }

        if (isFailed)
        {
            if (errorMessage)
            {
                *errorMessage = failure;
            }
            return false;
        }

        if (isFinished)
        {
            return true;
        }

        if (isTimedOut)
        {
            stopAndWait();
            if (errorMessage)
            {
                *errorMessage = tr("The text-to-speech engine %1 does not respond.").arg(m_engine);
            }
            return false;
        }
    }
}

// --------------------------------------------------------- PDFQtSpeechSynthesizerFactory

PDFQtSpeechSynthesizerFactory::PDFQtSpeechSynthesizerFactory() :
    m_candidates(getPreferredEngines())
{

}

PDFQtSpeechSynthesizerFactory::PDFQtSpeechSynthesizerFactory(QString engine) :
    m_candidates({ std::move(engine) })
{

}

PDFQtSpeechSynthesizerFactory::~PDFQtSpeechSynthesizerFactory() = default;

std::shared_ptr<PDFQtSpeechSynthesizerFactory> PDFQtSpeechSynthesizerFactory::create()
{
    return std::make_shared<PDFQtSpeechSynthesizerFactory>();
}

QStringList PDFQtSpeechSynthesizerFactory::getPreferredEngines()
{
    // R4-14 c: winrt synthesizes in its own thread, sapi gives no audio by synthesize (Qt 6.11),
    // speechd cannot synthesize at all; mock is the engine of the tests of Qt
    return { QStringLiteral("winrt"), QStringLiteral("darwin"), QStringLiteral("flite") };
}

QStringList PDFQtSpeechSynthesizerFactory::getInstalledEngines()
{
    return QTextToSpeech::availableEngines();
}

bool PDFQtSpeechSynthesizerFactory::probeEngine(const QString& engine, std::vector<PDFSpeechVoice>* voices, QString* defaultVoice) const
{
    if (!QTextToSpeech::availableEngines().contains(engine))
    {
        return false;
    }

    PDFQtSpeechSynthesizer probe(engine);
    if (!probe.isValid())
    {
        return false;
    }
    *voices = probe.getVoices();
    *defaultVoice = probe.getDefaultVoice();
    return true;
}

void PDFQtSpeechSynthesizerFactory::ensureProbed() const
{
    // Called with m_mutex locked. A probe, which has found an engine with a voice, is final; a probe,
    // which has found none, is repeated by the next call (341:11665 "open this tool again": the
    // user adds a voice in the settings of the system and opens the tool again)
    if (m_isProbed)
    {
        return;
    }

    m_engine.clear();
    m_voices.clear();
    m_defaultVoice.clear();
    for (const QString& candidate : m_candidates)
    {
        std::vector<PDFSpeechVoice> voices;
        QString defaultVoice;
        if (probeEngine(candidate, &voices, &defaultVoice))
        {
            m_engine = candidate;
            m_voices = std::move(voices);
            m_defaultVoice = std::move(defaultVoice);
            m_isProbed = !m_voices.empty();
            return;
        }
    }
}

QString PDFQtSpeechSynthesizerFactory::getEngine() const
{
    QMutexLocker locker(&m_mutex);
    ensureProbed();
    return m_engine;
}

bool PDFQtSpeechSynthesizerFactory::isAvailable() const
{
    return !getEngine().isEmpty();
}

std::vector<PDFSpeechVoice> PDFQtSpeechSynthesizerFactory::getVoices() const
{
    QMutexLocker locker(&m_mutex);
    ensureProbed();
    return m_voices;
}

QString PDFQtSpeechSynthesizerFactory::getDefaultVoice() const
{
    QMutexLocker locker(&m_mutex);
    ensureProbed();
    return m_defaultVoice;
}

std::unique_ptr<PDFSpeechSynthesizer> PDFQtSpeechSynthesizerFactory::createSynthesizer(QString* errorMessage) const
{
    const QString engine = getEngine();
    if (engine.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = tr("No text-to-speech engine, which can create audio files, is available.");
        }
        return nullptr;
    }

    std::unique_ptr<PDFQtSpeechSynthesizer> synthesizer = std::make_unique<PDFQtSpeechSynthesizer>(engine);
    if (!synthesizer->isValid())
    {
        if (errorMessage)
        {
            *errorMessage = synthesizer->getErrorMessage();
        }
        return nullptr;
    }
    return synthesizer;
}

}   // namespace pdf
