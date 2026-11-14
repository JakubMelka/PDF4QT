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

#ifndef PDFSPEECHSYNTHESIZER_H
#define PDFSPEECHSYNTHESIZER_H

#include "pdfglobal.h"

#include <QByteArray>
#include <QString>

#include <memory>
#include <vector>

namespace pdf
{

class PDFOperationControl;

/// Format of a block of PCM samples (step 4 of phase 2, 4.7, R4-14 b): the engine and the voice
/// decide it, every block says its own. Samples are interleaved by the channels. A value.
class PDF4QTLIBCORESHARED_EXPORT PDFAudioFormat
{
public:
    /// Format of one sample (QAudioFormat::SampleFormat without the dependency on Qt Multimedia)
    enum class SampleFormat
    {
        Unknown,
        UInt8,
        Int16,
        Int32,
        Float
    };

    int sampleRate = 0;                             ///< Frames per second (16 000, 22 050, ...)
    int channelCount = 0;                           ///< Channels (1 = mono)
    SampleFormat sampleFormat = SampleFormat::Unknown;

    /// The format can be read: a positive rate and channel count, a known sample format
    bool isValid() const;

    /// Bytes of one sample (0 for Unknown)
    int getBytesPerSample() const;

    /// Bytes of one frame (all channels)
    int getBytesPerFrame() const { return getBytesPerSample() * channelCount; }

    bool operator==(const PDFAudioFormat&) const = default;
};

/// Receiver of the PCM blocks of a synthesis (the audio file of the run, a test). Called by the
/// thread, which runs PDFSpeechSynthesizer::synthesize, one block at a time.
class PDF4QTLIBCORESHARED_EXPORT PDFAudioSink
{
public:
    virtual ~PDFAudioSink();

    /// One block of samples of the format
    /// \returns false, when the sink cannot take it (a write error): the synthesis stops and fails
    virtual bool writeAudio(const PDFAudioFormat& format, const QByteArray& samples) = 0;
};

/// A voice of an engine (QVoice without the dependency on Qt TextToSpeech): the Voice menu of
/// Audio Book groups them by the language (341:9657); PdfTool selects one by its attributes
/// (--voice-name, --voice-gender, --voice-age, --voice-lang-code, R4-15)
class PDF4QTLIBCORESHARED_EXPORT PDFSpeechVoice
{
public:
    enum class Gender
    {
        Unknown,
        Male,
        Female
    };

    enum class Age
    {
        Unknown,
        Child,
        Teenager,
        Adult,
        Senior
    };

    QString name;           ///< Name of the voice as the engine says it ("Microsoft Zira"); the value of the parameter voice
    QString languageCode;   ///< BCP 47 code of its language ("en-US")
    Gender gender = Gender::Unknown;
    Age age = Age::Unknown;
};

/// One synthesizer of a run (plan 4.7, R4-14 c, R4-15): owned by the run (a std::unique_ptr of the
/// job), used by the thread of the run only. The implementation of Pdf4QtSpeech
/// (PDFQtSpeechSynthesizer) runs QTextToSpeech in its own QThread and hands the PCM blocks over a
/// queue with a mutex; the core knows only this interface. A test gives a fake synthesizer.
class PDF4QTLIBCORESHARED_EXPORT PDFSpeechSynthesizer
{
public:
    virtual ~PDFSpeechSynthesizer();

    /// Selects the voice of its name (PDFSpeechVoice::name); an empty name = the default voice
    /// \returns false and the reason (the names of the voices of the engine, for MCP), when there
    ///          is no such voice
    virtual bool setVoice(const QString& voiceName, QString* errorMessage) = 0;

    /// Speed, -1 (slowest) to 1 (fastest), 0 = normal (QTextToSpeech::rate)
    virtual void setRate(double rate) = 0;

    /// Volume, 0 to 1 (QTextToSpeech::volume)
    virtual void setVolume(double volume) = 0;

    /// Synthesizes a plain text (never SSML: '<' and '&' are characters, B70) and hands its PCM to
    /// the sink in the calling thread; blocks until the text is spoken. The cancellation is checked
    /// at least every 100 ms (the engine is stopped then).
    /// \returns false and the reason: an error of the engine, the sink refused a block, canceled
    ///          (the reason is then empty)
    virtual bool synthesize(const QString& text, PDFAudioSink* sink, const PDFOperationControl* operationControl, QString* errorMessage) = 0;
};

/// Who creates the synthesizers of a run (R4-15): PDFDocumentToolContext::
/// speechSynthesizerFactory. A plain object without a QObject; its functions are const and may be
/// called by any thread (the GUI asks for the voices, the run creates a synthesizer). Pdf4QtSpeech
/// implements it (PDFQtSpeechSynthesizerFactory), a test gives a fake one. Without a factory the
/// tool fails with "No text-to-speech engine".
class PDF4QTLIBCORESHARED_EXPORT PDFSpeechSynthesizerFactory
{
public:
    virtual ~PDFSpeechSynthesizerFactory();

    /// An engine, which can synthesize into PCM, exists (Capability::Synthesize; speechd cannot)
    virtual bool isAvailable() const = 0;

    /// The voices of the engine (empty, when no engine is available)
    virtual std::vector<PDFSpeechVoice> getVoices() const = 0;

    /// The name of the default voice of the engine (the voice of an empty name of a voice, the
    /// system setting); empty = not known. An item of a language, which the default voice speaks,
    /// keeps it (PDFDocumentToolAudioBook::getVoiceForLanguage).
    virtual QString getDefaultVoice() const;

    /// A new synthesizer for one run, nullptr and the reason, when none can be created
    virtual std::unique_ptr<PDFSpeechSynthesizer> createSynthesizer(QString* errorMessage) const = 0;
};

}   // namespace pdf

#endif // PDFSPEECHSYNTHESIZER_H
