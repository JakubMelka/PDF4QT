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

#include "pdfaudiofilewriter.h"

#include <QtEndian>

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "pdfdbgheap.h"

namespace pdf
{

/// Helpers of the audio file
class PDFAudioFileWriterHelper
{
public:
    PDFAudioFileWriterHelper() = delete;

    /// Appends a little endian value
    static void appendUInt32(QByteArray* data, quint32 value)
    {
        char bytes[4];
        qToLittleEndian(value, bytes);
        data->append(bytes, 4);
    }

    static void appendUInt16(QByteArray* data, quint16 value)
    {
        char bytes[2];
        qToLittleEndian(value, bytes);
        data->append(bytes, 2);
    }

    /// A sample of the format as a value from -1 to 1
    static double readSample(const char* data, PDFAudioFormat::SampleFormat format)
    {
        switch (format)
        {
            case PDFAudioFormat::SampleFormat::UInt8:
                return (double(quint8(*data)) - 128.0) / 128.0;

            case PDFAudioFormat::SampleFormat::Int16:
                return double(qFromLittleEndian<qint16>(data)) / 32768.0;

            case PDFAudioFormat::SampleFormat::Int32:
                return double(qFromLittleEndian<qint32>(data)) / 2147483648.0;

            case PDFAudioFormat::SampleFormat::Float:
            {
                const quint32 bits = qFromLittleEndian<quint32>(data);
                float value = 0.0f;
                std::memcpy(&value, &bits, sizeof(value));
                return std::isfinite(value) ? double(value) : 0.0;
            }

            case PDFAudioFormat::SampleFormat::Unknown:
                break;
        }
        return 0.0;
    }

    /// A value from -1 to 1 as a signed 16 bit sample (clipped; the inverse of readSample of
    /// Int16, so a 16 bit sample stays the same)
    static qint16 toInt16(double value)
    {
        const double scaled = std::round(qBound(-1.0, value, 1.0) * 32768.0);
        return qint16(qBound(-32768.0, scaled, 32767.0));
    }

    /// The error of a write of the device
    static QString getWriteError(const QIODevice* device)
    {
        return PDFAudioFileWriter::tr("The audio file cannot be written: %1").arg(device ? device->errorString() : QString());
    }
};

PDFAudioEncoder::~PDFAudioEncoder() = default;

PDFWavEncoder::~PDFWavEncoder() = default;

QByteArray PDFWavEncoder::createHeader(int sampleRate, int channelCount, quint32 dataSize)
{
    using Helper = PDFAudioFileWriterHelper;

    // RIFF chunk: "RIFF", size of the rest (36 + data), "WAVE"; "fmt " chunk of 16 bytes:
    // WAVE_FORMAT_PCM (1), channels, rate, bytes per second, block align, 16 bits; "data" chunk
    const quint16 blockAlign = quint16(channelCount * 2);
    QByteArray header;
    header.reserve(int(HEADER_SIZE));
    header.append("RIFF", 4);
    Helper::appendUInt32(&header, quint32(36) + dataSize);
    header.append("WAVE", 4);
    header.append("fmt ", 4);
    Helper::appendUInt32(&header, 16);
    Helper::appendUInt16(&header, 1);
    Helper::appendUInt16(&header, quint16(channelCount));
    Helper::appendUInt32(&header, quint32(sampleRate));
    Helper::appendUInt32(&header, quint32(sampleRate) * blockAlign);
    Helper::appendUInt16(&header, blockAlign);
    Helper::appendUInt16(&header, 16);
    header.append("data", 4);
    Helper::appendUInt32(&header, dataSize);
    return header;
}

bool PDFWavEncoder::begin(QIODevice* device, int sampleRate, int channelCount, QString* errorMessage)
{
    if (!device || sampleRate <= 0 || channelCount <= 0 || channelCount > 2)
    {
        if (errorMessage)
        {
            *errorMessage = PDFAudioFileWriter::tr("The audio has a format, which cannot be written.");
        }
        return false;
    }

    m_device = device;
    m_sampleRate = sampleRate;
    m_channelCount = channelCount;
    m_dataSize = 0;

    // The sizes are written by finish
    const QByteArray header = createHeader(sampleRate, channelCount, 0);
    if (m_device->write(header) != header.size())
    {
        if (errorMessage)
        {
            *errorMessage = PDFAudioFileWriterHelper::getWriteError(m_device);
        }
        return false;
    }
    return true;
}

bool PDFWavEncoder::write(const qint16* samples, qint64 frameCount, QString* errorMessage)
{
    if (!m_device)
    {
        return false;
    }

    const qint64 bytes = frameCount * m_channelCount * 2;
    if (HEADER_SIZE + m_dataSize + bytes > MAXIMUM_FILE_SIZE)
    {
        if (errorMessage)
        {
            *errorMessage = tr("The audio is longer than a WAV file can hold (4 GB). Choose fewer pages.");
        }
        return false;
    }

    // The samples are little endian in the file
    QByteArray data;
    data.resize(bytes);
    for (qint64 i = 0, count = frameCount * m_channelCount; i < count; ++i)
    {
        qToLittleEndian(samples[i], data.data() + 2 * i);
    }

    if (m_device->write(data) != data.size())
    {
        if (errorMessage)
        {
            *errorMessage = PDFAudioFileWriterHelper::getWriteError(m_device);
        }
        return false;
    }
    m_dataSize += bytes;
    return true;
}

bool PDFWavEncoder::finish(QString* errorMessage)
{
    if (!m_device)
    {
        return false;
    }

    // The sizes of the chunks RIFF and data (write checks, that they fit into 32 bits)
    const QByteArray header = createHeader(m_sampleRate, m_channelCount, quint32(m_dataSize));
    const qint64 end = m_device->pos();
    if (!m_device->seek(0) || m_device->write(header) != header.size() || !m_device->seek(end))
    {
        if (errorMessage)
        {
            *errorMessage = PDFAudioFileWriterHelper::getWriteError(m_device);
        }
        return false;
    }
    m_device = nullptr;
    return true;
}

QString PDFWavEncoder::getFileExtension() const
{
    return QStringLiteral("wav");
}

QString PDFWavEncoder::getMimeType() const
{
    return QStringLiteral("audio/wav");
}

PDFAudioFileWriter::PDFAudioFileWriter(std::unique_ptr<PDFAudioEncoder> encoder) :
    m_encoder(std::move(encoder))
{

}

PDFAudioFileWriter::~PDFAudioFileWriter() = default;

bool PDFAudioFileWriter::open(const QString& filePath, QString* errorMessage)
{
    m_file.setFileName(filePath);
    if (!m_encoder || !m_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        m_errorMessage = tr("The audio file cannot be written: %1").arg(m_file.errorString());
        if (errorMessage)
        {
            *errorMessage = m_errorMessage;
        }
        return false;
    }

    m_fileFormat = PDFAudioFormat();
    m_frameCount = 0;
    m_errorMessage.clear();
    return true;
}

bool PDFAudioFileWriter::writeAudio(const PDFAudioFormat& format, const QByteArray& samples)
{
    if (!m_file.isOpen() || !m_errorMessage.isEmpty())
    {
        return false;
    }

    if (!format.isValid())
    {
        m_errorMessage = tr("The text-to-speech engine gave audio of an unknown format.");
        return false;
    }

    // The first block decides the format of the file: its rate and channels, 16 bit (R4-14 b)
    if (!m_fileFormat.isValid())
    {
        PDFAudioFormat fileFormat;
        fileFormat.sampleRate = format.sampleRate;
        fileFormat.channelCount = qMin(format.channelCount, 2);
        fileFormat.sampleFormat = PDFAudioFormat::SampleFormat::Int16;
        if (!m_encoder->begin(&m_file, fileFormat.sampleRate, fileFormat.channelCount, &m_errorMessage))
        {
            return false;
        }
        m_fileFormat = fileFormat;
    }

    const QByteArray converted = convert(format, samples, m_fileFormat);
    const qint64 frameCount = converted.size() / m_fileFormat.getBytesPerFrame();
    if (frameCount == 0)
    {
        return true;
    }

    // convert gives the samples in the order of the host (an array of qint16)
    if (!m_encoder->write(reinterpret_cast<const qint16*>(converted.constData()), frameCount, &m_errorMessage))
    {
        return false;
    }
    m_frameCount += frameCount;
    return true;
}

bool PDFAudioFileWriter::writeSilence(int milliseconds)
{
    if (!m_file.isOpen() || !m_errorMessage.isEmpty())
    {
        return false;
    }

    // Nothing before the first block: the file has no format yet, the audio book starts by speech
    if (!m_fileFormat.isValid() || milliseconds <= 0)
    {
        return true;
    }

    const qint64 frameCount = qint64(m_fileFormat.sampleRate) * milliseconds / 1000;
    const std::vector<qint16> silence(size_t(frameCount * m_fileFormat.channelCount), 0);
    if (!m_encoder->write(silence.data(), frameCount, &m_errorMessage))
    {
        return false;
    }
    m_frameCount += frameCount;
    return true;
}

bool PDFAudioFileWriter::close(QString* errorMessage)
{
    bool isOk = m_errorMessage.isEmpty() && m_file.isOpen();
    if (isOk && !m_fileFormat.isValid())
    {
        m_errorMessage = tr("The text-to-speech engine gave no audio.");
        isOk = false;
    }
    if (isOk)
    {
        isOk = m_encoder->finish(&m_errorMessage);
    }
    if (m_file.isOpen())
    {
        m_file.close();
        if (isOk && m_file.error() != QFileDevice::NoError)
        {
            m_errorMessage = tr("The audio file cannot be written: %1").arg(m_file.errorString());
            isOk = false;
        }
    }

    if (!isOk && errorMessage)
    {
        *errorMessage = m_errorMessage;
    }
    return isOk;
}

QByteArray PDFAudioFileWriter::convert(const PDFAudioFormat& source, const QByteArray& samples, const PDFAudioFormat& target)
{
    using Helper = PDFAudioFileWriterHelper;

    if (!source.isValid() || !target.isValid() || target.sampleFormat != PDFAudioFormat::SampleFormat::Int16 || target.channelCount > 2)
    {
        return QByteArray();
    }

    const int sourceBytesPerSample = source.getBytesPerSample();
    const int sourceFrameSize = source.getBytesPerFrame();
    const qint64 sourceFrames = samples.size() / sourceFrameSize;
    if (sourceFrames == 0)
    {
        return QByteArray();
    }

    // The channels of the target: mono = the mean of all channels, stereo = the first two (a mono
    // source is duplicated)
    std::vector<double> frames(size_t(sourceFrames * target.channelCount));
    const char* data = samples.constData();
    for (qint64 frame = 0; frame < sourceFrames; ++frame)
    {
        const char* frameData = data + frame * sourceFrameSize;
        if (target.channelCount == 1)
        {
            double sum = 0.0;
            for (int channel = 0; channel < source.channelCount; ++channel)
            {
                sum += Helper::readSample(frameData + channel * sourceBytesPerSample, source.sampleFormat);
            }
            frames[size_t(frame)] = sum / source.channelCount;
        }
        else
        {
            for (int channel = 0; channel < 2; ++channel)
            {
                const int sourceChannel = qMin(channel, source.channelCount - 1);
                frames[size_t(frame * 2 + channel)] = Helper::readSample(frameData + sourceChannel * sourceBytesPerSample, source.sampleFormat);
            }
        }
    }

    // The rate of the target by linear interpolation
    qint64 targetFrames = sourceFrames;
    if (source.sampleRate != target.sampleRate)
    {
        targetFrames = qint64(std::llround(double(sourceFrames) * target.sampleRate / source.sampleRate));
    }

    QByteArray result;
    result.resize(targetFrames * target.channelCount * qint64(sizeof(qint16)));
    qint16* output = reinterpret_cast<qint16*>(result.data());
    const double step = double(source.sampleRate) / double(target.sampleRate);
    for (qint64 frame = 0; frame < targetFrames; ++frame)
    {
        const double position = source.sampleRate == target.sampleRate ? double(frame) : frame * step;
        const qint64 index = qMin<qint64>(qint64(position), sourceFrames - 1);
        const qint64 nextIndex = qMin<qint64>(index + 1, sourceFrames - 1);
        const double fraction = qBound(0.0, position - double(index), 1.0);
        for (int channel = 0; channel < target.channelCount; ++channel)
        {
            const double a = frames[size_t(index * target.channelCount + channel)];
            const double b = frames[size_t(nextIndex * target.channelCount + channel)];
            output[frame * target.channelCount + channel] = Helper::toInt16(a + (b - a) * fraction);
        }
    }
    return result;
}

}   // namespace pdf
