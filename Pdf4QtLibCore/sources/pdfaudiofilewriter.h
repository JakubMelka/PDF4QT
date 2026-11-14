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

#ifndef PDFAUDIOFILEWRITER_H
#define PDFAUDIOFILEWRITER_H

#include "pdfspeechsynthesizer.h"

#include <QCoreApplication>
#include <QFile>

#include <memory>

namespace pdf
{

/// Encoder of an audio file (step 4 of phase 2, 4.7): it gets mono or stereo signed 16 bit samples
/// of one rate and writes them into a device. The audio books are written as WAV only (owner
/// 14. 11.: no MP3 encoder for now), PDFWavEncoder. Used by one thread.
class PDF4QTLIBCORESHARED_EXPORT PDFAudioEncoder
{
public:
    virtual ~PDFAudioEncoder();

    /// Starts the encoding of samples of the rate and the channels into the device (open for writing)
    virtual bool begin(QIODevice* device, int sampleRate, int channelCount, QString* errorMessage) = 0;

    /// Encodes interleaved signed 16 bit samples (frameCount frames)
    virtual bool write(const qint16* samples, qint64 frameCount, QString* errorMessage) = 0;

    /// Finishes the file (the sizes of the header of WAV)
    virtual bool finish(QString* errorMessage) = 0;

    /// Extension of the file without the dot ("wav") and its type ("audio/wav")
    virtual QString getFileExtension() const = 0;
    virtual QString getMimeType() const = 0;
};

/// WAV PCM 16 bit (RIFF, the canonical 44 byte header with the chunks "fmt " and "data"). A file
/// larger than 4 GiB is an error: the audio is longer than a WAV file can hold, the user chooses
/// fewer pages.
class PDF4QTLIBCORESHARED_EXPORT PDFWavEncoder : public PDFAudioEncoder
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFWavEncoder)

public:
    /// Largest WAV file (the sizes of RIFF are 32 bit)
    static constexpr qint64 MAXIMUM_FILE_SIZE = qint64(0xFFFFFFFFu);

    virtual ~PDFWavEncoder() override;

    /// Size of the header (RIFF, "fmt " of PCM, the head of "data")
    static constexpr qint64 HEADER_SIZE = 44;

    virtual bool begin(QIODevice* device, int sampleRate, int channelCount, QString* errorMessage) override;
    virtual bool write(const qint16* samples, qint64 frameCount, QString* errorMessage) override;
    virtual bool finish(QString* errorMessage) override;
    virtual QString getFileExtension() const override;
    virtual QString getMimeType() const override;

    /// The header of a WAV file of signed 16 bit PCM (little endian, Microsoft RIFF WAVE, WAVE_FORMAT_PCM)
    /// \param sampleRate Frames per second
    /// \param channelCount Channels
    /// \param dataSize Bytes of the samples
    static QByteArray createHeader(int sampleRate, int channelCount, quint32 dataSize);

private:
    QIODevice* m_device = nullptr;
    int m_sampleRate = 0;
    int m_channelCount = 0;
    qint64 m_dataSize = 0;
};

/// The audio file of a run of Create Audio Book (plan 4.7, R4-14 b): a sink of the PCM blocks of
/// the synthesizer, which converts them and hands them to its encoder. The format of the file is
/// the format of the first block, converted to signed 16 bit; a later block of another format
/// (a voice of another language, R4-13) is converted to it: the channels mixed or duplicated, the
/// samples to 16 bit, the rate linearly resampled. The PCM is never held: it goes to the encoder
/// and into the file at once. Silence between the items (ITEM_SILENCE) and the pages
/// (PAGE_SILENCE) is written by writeSilence. Used by the thread of the run only.
class PDF4QTLIBCORESHARED_EXPORT PDFAudioFileWriter : public PDFAudioSink
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAudioFileWriter)

public:
    /// Silence between two items and between two pages (ms)
    static constexpr int ITEM_SILENCE = 300;
    static constexpr int PAGE_SILENCE = 600;

    /// \param encoder The encoder of the file (never null)
    explicit PDFAudioFileWriter(std::unique_ptr<PDFAudioEncoder> encoder);
    virtual ~PDFAudioFileWriter() override;

    PDFAudioFileWriter(const PDFAudioFileWriter&) = delete;
    PDFAudioFileWriter& operator=(const PDFAudioFileWriter&) = delete;

    /// Opens the file (a temporary file of the result, PDFDocumentToolTemporaryFile)
    bool open(const QString& filePath, QString* errorMessage);

    /// A block of the synthesizer (see the class); false after an error (getErrorMessage)
    virtual bool writeAudio(const PDFAudioFormat& format, const QByteArray& samples) override;

    /// Silence of the length in the format of the file (nothing before the first block)
    bool writeSilence(int milliseconds);

    /// Finishes the encoder and closes the file
    bool close(QString* errorMessage);

    /// The format of the file (16 bit, the rate and the channels of the first block); invalid before it
    const PDFAudioFormat& getFileFormat() const { return m_fileFormat; }

    /// Frames written so far (the length of the audio book)
    qint64 getFrameCount() const { return m_frameCount; }

    /// The reason of the last failure
    const QString& getErrorMessage() const { return m_errorMessage; }

    /// Converts a block into the format of the file (R4-14 b): signed 16 bit samples, the channels of
    /// \p target (mixed or duplicated), the rate of \p target (linear interpolation). Empty for an
    /// invalid format.
    static QByteArray convert(const PDFAudioFormat& source, const QByteArray& samples, const PDFAudioFormat& target);

private:
    std::unique_ptr<PDFAudioEncoder> m_encoder;
    QFile m_file;
    PDFAudioFormat m_fileFormat;
    qint64 m_frameCount = 0;
    QString m_errorMessage;
};

}   // namespace pdf

#endif // PDFAUDIOFILEWRITER_H
