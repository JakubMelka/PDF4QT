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

#ifndef PDFQTSPEECHSYNTHESIZER_H
#define PDFQTSPEECHSYNTHESIZER_H

#include "pdf4qtspeech_export.h"
#include "pdfspeechsynthesizer.h"

#include <QCoreApplication>
#include <QMutex>
#include <QStringList>

#include <memory>
#include <optional>
#include <vector>

class QThread;

namespace pdf
{

class PDFQtSpeechSession;
class PDFQtSpeechWorker;

/// The one selection of a voice by its attributes (R4-15): PdfTool audio-book --voice-name,
/// --voice-gender, --voice-age, --voice-lang-code; the tool itself takes a voice by its name.
/// No copy of it elsewhere.
class PDF4QTSPEECHSHARED_EXPORT PDFSpeechVoiceSelector
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFSpeechVoiceSelector)

public:
    PDFSpeechVoiceSelector() = delete;

    /// What the user asked for; an empty text = any
    class Criteria
    {
    public:
        QString name;           ///< The name of the voice (case insensitive)
        QString gender;         ///< "male", "female"
        QString age;            ///< "child", "teenager", "adult", "senior"
        QString languageCode;   ///< BCP 47 code or its language ("en-US", "en")
    };

    /// The first voice of \p voices, which matches every given criterion; std::nullopt and the
    /// reason (the names of the voices) when none matches or a criterion has an unknown value
    static std::optional<PDFSpeechVoice> selectVoice(const std::vector<PDFSpeechVoice>& voices, const Criteria& criteria, QString* errorMessage);

    /// "Microsoft Zira (en-US, female, adult)": a voice in a list of PdfTool and of an error
    static QString getDescription(const PDFSpeechVoice& voice);

    /// The names of the voices, separated by commas (the reason of an unknown voice, for MCP)
    static QString getNames(const std::vector<PDFSpeechVoice>& voices);
};

/// The speech synthesizer of Qt TextToSpeech for one run (owner answer Q4.1 a, plan 4.7, R4-14 c):
/// its own QThread with an event loop holds QTextToSpeech and a worker object (no lambdas, member
/// functions of the worker); synthesize hands the text over a queued call
/// (PDFThreadAnnotations::TransportScope), the PCM blocks come back through a queue with a mutex
/// (PDFQtSpeechSession), the thread of the run waits for them by PDFThreadAnnotations::wait and
/// writes them to the sink. The cancellation is polled every 100 ms: stop is queued, the end of
/// the synthesis is waited for (at most STOP_TIMEOUT; an engine, which does not stop within it,
/// refuses every further text, so its late blocks never reach another synthesis). An engine,
/// which gives no audio and no change of its state for ENGINE_TIMEOUT, has failed. Owned by the
/// run (std::unique_ptr); its QThread ends by quit + an unlimited wait in the destructor (the
/// reason is there), the worker and QTextToSpeech are deleted in it.
///
/// R4-14 c, checked on Windows 11 with Qt 6.11.2 (14. 11. 2026): the engine winrt synthesizes in
/// a QThread of its own (and is stopped there); the engine sapi reports Capability::Synthesize,
/// but its synthesize gives no audio and no change of its state - neither in a QThread nor in the
/// GUI thread. The factory therefore never takes sapi (nor speechd, which cannot synthesize).
class PDF4QTSPEECHSHARED_EXPORT PDFQtSpeechSynthesizer : public PDFSpeechSynthesizer
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFQtSpeechSynthesizer)

public:
    /// Longest silence of an engine during a synthesis (ms)
    static constexpr int ENGINE_TIMEOUT = 60000;

    /// Longest wait for the end of a stopped synthesis (ms)
    static constexpr int STOP_TIMEOUT = 5000;

    /// Starts the thread and the engine (the constructor waits, until the engine is ready)
    /// \param engine Name of the engine of Qt TextToSpeech ("winrt", "darwin", "flite", "mock")
    explicit PDFQtSpeechSynthesizer(QString engine);
    virtual ~PDFQtSpeechSynthesizer() override;

    PDFQtSpeechSynthesizer(const PDFQtSpeechSynthesizer&) = delete;
    PDFQtSpeechSynthesizer& operator=(const PDFQtSpeechSynthesizer&) = delete;

    /// The engine has started and can synthesize (Capability::Synthesize)
    bool isValid() const;

    /// The reason, why the engine is not valid
    QString getErrorMessage() const;

    /// The voices of the engine, of all of its languages
    std::vector<PDFSpeechVoice> getVoices() const;

    /// The name of the voice of the engine, which has spoken the last text (empty before the first
    /// synthesis): the voice of setVoice, or the default voice of the engine for an empty name
    QString getSpokenVoice() const;

    /// The name of the voice of the engine at its start (the default voice of the system, the voice
    /// of an empty name), empty when the engine is not valid
    QString getDefaultVoice() const;

    virtual bool setVoice(const QString& voiceName, QString* errorMessage) override;
    virtual void setRate(double rate) override;
    virtual void setVolume(double volume) override;
    virtual bool synthesize(const QString& text, PDFAudioSink* sink, const PDFOperationControl* operationControl, QString* errorMessage) override;

private:
    /// Stops the synthesis of the engine and waits for its end (the blocks are dropped); an engine,
    /// which does not end within STOP_TIMEOUT, is not used any more
    void stopAndWait();

    QString m_engine;
    std::shared_ptr<PDFQtSpeechSession> m_session;
    std::unique_ptr<QThread> m_thread;
    PDFQtSpeechWorker* m_worker = nullptr;    ///< Lives in m_thread, deleted there (deleteLater before quit)
    quint64 m_generationCounter = 0;          ///< The generation of the last synthesis (PDFQtSpeechSession)
    bool m_isStopTimedOut = false;            ///< A stopped synthesis has not ended: every further synthesis fails
    QString m_voice;
    double m_rate = 0.0;
    double m_volume = 1.0;
};

/// The factory of Pdf4QtSpeech (R4-15): the engine of Qt TextToSpeech, which can synthesize
/// (Capability::Synthesize; the order winrt, darwin, flite - never sapi, speechd or mock, see
/// PDFQtSpeechSynthesizer), its voices and the synthesizers of the runs. A plain shared object
/// (PDFAppContext::getSpeechSynthesizerFactory, PdfTool); its functions are const and thread-safe:
/// the engine and its voices are found by a probe synthesizer under a mutex - once, or again by the
/// next call, when the probe has found no voice.
class PDF4QTSPEECHSHARED_EXPORT PDFQtSpeechSynthesizerFactory : public PDFSpeechSynthesizerFactory
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFQtSpeechSynthesizerFactory)

public:
    /// The factory of the best engine of the system
    PDFQtSpeechSynthesizerFactory();

    /// The factory of one engine (the tests: "mock" of the installation of Qt)
    explicit PDFQtSpeechSynthesizerFactory(QString engine);

    virtual ~PDFQtSpeechSynthesizerFactory() override;

    /// The factory of the application and of PdfTool
    static std::shared_ptr<PDFQtSpeechSynthesizerFactory> create();

    /// The engines, which the factory tries, in their order
    static QStringList getPreferredEngines();

    /// The engines of Qt TextToSpeech installed with the application (QTextToSpeech::availableEngines)
    static QStringList getInstalledEngines();

    /// The name of the engine ("winrt"), empty = none can synthesize
    QString getEngine() const;

    virtual bool isAvailable() const override;
    virtual std::vector<PDFSpeechVoice> getVoices() const override;
    virtual QString getDefaultVoice() const override;
    virtual std::unique_ptr<PDFSpeechSynthesizer> createSynthesizer(QString* errorMessage) const override;

protected:
    /// Starts the engine once (a probe synthesizer): true, when it can synthesize, with its voices
    /// and its default voice. Called under the mutex of the factory.
    virtual bool probeEngine(const QString& engine, std::vector<PDFSpeechVoice>* voices, QString* defaultVoice) const;

private:
    /// Finds the engine and its voices: once, when a probe has found a voice; a probe, which has
    /// found none, is repeated by the next call (the user may add a voice meanwhile, 341:11665)
    void ensureProbed() const;

    QStringList m_candidates;
    mutable QMutex m_mutex;
    mutable bool m_isProbed = false;
    mutable QString m_engine;
    mutable std::vector<PDFSpeechVoice> m_voices;
    mutable QString m_defaultVoice;
};

}   // namespace pdf

#endif // PDFQTSPEECHSYNTHESIZER_H
