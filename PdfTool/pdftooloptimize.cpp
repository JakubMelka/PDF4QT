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

#include "pdftooloptimize.h"
#include "pdfdocumenttool_compress.h"
#include "pdfdocumentwriter.h"

#include <QMutex>
#include <QMutexLocker>

namespace pdftool
{

/// The texts of the structure optimizer: kept by the computation, printed by the command
/// after it (the progress never runs foreign code, PDFProgressSink)
class PDFToolOptimizeMessages : public pdf::PDFProgressSink
{
public:
    virtual void start(size_t stepCount, pdf::ProgressStartupInfo startupInfo) override
    {
        Q_UNUSED(stepCount);
        Q_UNUSED(startupInfo);
    }

    virtual void step() override { }
    virtual void finish() override { }

    virtual void addMessage(const QString& message) override
    {
        QMutexLocker locker(&m_mutex);
        m_messages << message;
    }

    /// The texts in their order (after the computation)
    QStringList takeMessages()
    {
        QMutexLocker locker(&m_mutex);
        return std::move(m_messages);
    }

private:
    QMutex m_mutex;
    QStringList m_messages;
};

static PDFToolOptimize s_optimizeApplication;

QString PDFToolOptimize::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "optimize";

        case Name:
            return PDFToolTranslationContext::tr("Optimize");

        case Description:
            return PDFToolTranslationContext::tr("Optimize document size using various algorithms.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolOptimize::execute(const PDFToolOptions& options)
{
    if (!options.optimizeFlags && !options.imageOptimizationSettings.enabled)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("No optimization option has been set."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    // One implementation with the tool Compress of the new application: the written file keeps the
    // version of the input and it has no old revisions and no declaration of PDF/A, which is not checked
    pdf::PDFDocumentToolCompress::Settings settings;
    settings.images = options.imageOptimizationSettings;
    settings.structure = options.optimizeFlags;

    PDFToolOptimizeMessages messages;
    pdf::PDFDocumentToolCompress::Progress progress;
    progress.structure = &messages;

    pdf::PDFDocumentToolCompress::Statistics statistics;
    document = pdf::PDFDocumentToolCompress::compress(&document, settings, nullptr, progress, &statistics);
    for (const QString& message : messages.takeMessages())
    {
        PDFConsole::writeError(message, options.outputCodec);
    }

    if (!statistics.conformance.removed.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("The declaration of %1 was removed: the result was not checked against the standard.").arg(statistics.conformance.removed.join(QStringLiteral(", "))), options.outputCodec);
    }
    if (!statistics.conformance.kept.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("The document still declares %1, its metadata cannot be changed: the result was not checked against the standard.").arg(statistics.conformance.kept.join(QStringLiteral(", "))), options.outputCodec);
    }

    pdf::PDFDocumentWriter writer(nullptr);
    pdf::PDFOperationResult result = writer.write(options.document, &document, true);
    if (!result)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to write optimize document. %1").arg(result.getErrorMessage()), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolOptimize::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | Optimize;
}

}   // namespace pdftool
