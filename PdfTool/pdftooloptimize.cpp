// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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
#include "pdfdocumentwriter.h"

namespace pdftool
{

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
    if (!options.optimizeFlags)
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

    pdf::PDFOptimizer optimizer(options.optimizeFlags, nullptr);
    QObject::connect(&optimizer, &pdf::PDFOptimizer::optimizationProgress, &optimizer, [&options](QString text) { PDFConsole::writeError(text, options.outputCodec); }, Qt::DirectConnection);
    optimizer.setDocument(&document);
    optimizer.optimize();
    document = optimizer.takeOptimizedDocument();

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
