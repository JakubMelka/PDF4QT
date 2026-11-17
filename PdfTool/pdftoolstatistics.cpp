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

#include "pdftoolstatistics.h"
#include "pdfdocumenttool_statistics.h"

#include <QJsonObject>

namespace pdftool
{

static PDFToolStatisticsApplication s_statisticsApplication;

QString PDFToolStatisticsApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "statistics";

        case Name:
            return PDFToolTranslationContext::tr("Statistics");

        case Description:
            return PDFToolTranslationContext::tr("Compute statistics of internal objects used in a document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolStatisticsApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    bool isRestored = false;
    if (!readDocument(options, document, &sourceData, false, &isRestored))
    {
        return ErrorDocumentReading;
    }

    // One implementation (plan of step 7 of phase 2, 4.2, 4.10): the data and the export of Document
    // Statistics, both tables, the C locale (Q7.14, R7-14); a damaged file restored by the reader is
    // "Repaired when opened" (plan 4.2)
    using Tool = pdf::PDFDocumentToolStatistics;

    bool isCanceled = false;
    const QJsonObject data = Tool::createData(&document, sourceData.isEmpty() ? qint64(-1) : qint64(sourceData.size()), isRestored, nullptr, &isCanceled);

    pdf::PDFDocumentToolTableFormat format = pdf::PDFDocumentToolTableFormat::Text;
    switch (options.outputStyle)
    {
        case PDFOutputFormatter::Style::Text:
            format = pdf::PDFDocumentToolTableFormat::Text;
            break;
        case PDFOutputFormatter::Style::Xml:
            format = pdf::PDFDocumentToolTableFormat::Xml;
            break;
        case PDFOutputFormatter::Style::Html:
            format = pdf::PDFDocumentToolTableFormat::Html;
            break;
    }

    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_FORMAT), pdf::PDFDocumentToolTableWriter::getFormatId(format));
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_VIEW), QString::fromLatin1(Tool::VIEW_FUNCTION));

    QString errorMessage;
    const pdf::PDFDocumentToolOutputFile output = Tool::writeOutput(data, parameters, pdf::PDFDocumentToolTableWriter::Options(), QStringLiteral("statistics"), &errorMessage);
    if (!errorMessage.isEmpty())
    {
        PDFConsole::writeError(errorMessage, options.outputCodec);
        return ErrorUnknown;
    }

    PDFConsole::writeText(QString::fromUtf8(output.data), options.outputCodec);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolStatisticsApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument;
}

}   // namespace pdftool
