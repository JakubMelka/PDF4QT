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

#include "pdftooldiff.h"

#include "pdfdocumenttool_compare.h"
#include "pdfdocumentreader.h"

#include <QFile>
#include <QJsonArray>

namespace pdftool
{

static PDFToolDiff s_toolDiffApplication;

/// The tool Compare, whose run keeps its whole result (as the run tool of the tab Compare): the data of a result
/// hold at most PDFDocumentToolCompare::MAXIMUM_DIFFERENCES differences
class PDFToolDiffCompareTool : public pdf::PDFDocumentToolCompare
{
public:
    explicit PDFToolDiffCompareTool(pdf::PDFDiffResultCollector* collector) :
        m_collector(collector)
    {

    }

    virtual pdf::PDFDocumentToolResult execute(const pdf::PDFDocumentToolContext& context,
                                               const QVariantMap& parameters,
                                               const pdf::PDFOperationControl* control,
                                               pdf::PDFProgressSink* progress) const override
    {
        return compare(context, parameters, control, progress, m_collector);
    }

private:
    pdf::PDFDiffResultCollector* m_collector;
};

QString PDFToolDiff::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "diff";

        case Name:
            return PDFToolTranslationContext::tr("Compare documents");

        case Description:
            return PDFToolTranslationContext::tr("Compare contents of two documents.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolDiff::execute(const PDFToolOptions& options)
{
    if (options.diffFiles.size() != 2)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Exactly two documents must be specified."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, options.permissiveReading, false);

    // The bytes of each file are kept: the inputs of the tool are snapshots of the files (as the
    // tabs of the application give them)
    std::array<pdf::PDFDocumentToolInputDocument, 2> inputs;
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        const QString& fileName = options.diffFiles[qsizetype(i)];
        QFile file(fileName);
        QByteArray data;
        if (file.open(QFile::ReadOnly))
        {
            data = file.readAll();
            file.close();
        }

        pdf::PDFDocument document = reader.readFromBuffer(data);
        if (data.isEmpty() || reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot open document '%1'.").arg(fileName), options.outputCodec);
            return ErrorDocumentReading;
        }

        inputs[i] = pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document))), std::move(data), fileName, false);
    }

    // One implementation with Compare of the new application (plan of step 7 of phase 2, 4.9, 4.10):
    // the tool checks the documents without pages (B192), keeps the text comparison within its budget
    // (B191, R7-6) and gives the XML of the differences only with bit 5 of both documents (Q7.2); the
    // numbers in the C locale (R7-14)
    using Tool = pdf::PDFDocumentToolCompare;

    const bool isXml = options.outputStyle == PDFOutputFormatter::Style::Xml;
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_OUTPUT), QString::fromLatin1(isXml ? Tool::OUTPUT_XML : Tool::OUTPUT_SUMMARY));

    pdf::PDFDocumentToolContext context;
    context.documents.assign(inputs.cbegin(), inputs.cend());

    // The data of the tool hold at most Tool::MAXIMUM_DIFFERENCES differences (MCP); the command lists every one
    // as before step 7c of phase 2 (review RS7CCORE), so the run keeps its whole result
    pdf::PDFDiffResultCollector collector;
    const PDFToolDiffCompareTool tool(&collector);
    const pdf::PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
    const std::optional<pdf::PDFDiffResult> diffResult = collector.getResult();
    if (!result.isSuccess() || !diffResult)
    {
        PDFConsole::writeError(result.message, options.outputCodec);
        return ErrorUnknown;
    }

    if (isXml)
    {
        const QByteArray xml = result.outputs.empty() ? QByteArray() : result.outputs.front().data;
        PDFConsole::writeText(QString::fromUtf8(xml), options.outputCodec);
        return ExitSuccess;
    }

    const QLocale locale = QLocale::c();
    const bool isTextOmitted = result.data.value(QString::fromLatin1(Tool::DATA_IS_TEXT_OMITTED)).toBool();

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("diff", PDFToolTranslationContext::tr("Difference Report"));
    formatter.endl();

    formatter.beginTable("differences", PDFToolTranslationContext::tr("Differences"));

    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("no", PDFToolTranslationContext::tr("No."), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("type", PDFToolTranslationContext::tr("Type"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("left-page-number", PDFToolTranslationContext::tr("Left Page"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("right-page-number", PDFToolTranslationContext::tr("Right Page"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("description", PDFToolTranslationContext::tr("Description"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("removed-text", PDFToolTranslationContext::tr("Removed Text"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("added-text", PDFToolTranslationContext::tr("Added Text"), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    // Every difference of the whole result (PdfTool diff has no filter); the texts only with bit 5 of both
    // documents (Q7.2, isTextOmitted of the tool)
    const size_t differenceCount = diffResult->getDifferencesCount();
    for (size_t i = 0; i < differenceCount; ++i)
    {
        const pdf::PDFInteger leftPage = diffResult->getLeftPage(i);
        const pdf::PDFInteger rightPage = diffResult->getRightPage(i);

        formatter.beginTableRow("difference", int(i));
        formatter.writeTableColumn("no", locale.toString(qulonglong(i + 1)), Qt::AlignRight);
        formatter.writeTableColumn("type", diffResult->getTypeDescription(i), Qt::AlignLeft);
        formatter.writeTableColumn("left-page-number", leftPage < 0 ? QString() : locale.toString(qint64(leftPage + 1)), Qt::AlignRight);
        formatter.writeTableColumn("right-page-number", rightPage < 0 ? QString() : locale.toString(qint64(rightPage + 1)), Qt::AlignRight);
        formatter.writeTableColumn("description", diffResult->getDescription(i), Qt::AlignLeft);
        formatter.writeTableColumn("removed-text", isTextOmitted ? QString() : diffResult->getRemovedText(i), Qt::AlignLeft);
        formatter.writeTableColumn("added-text", isTextOmitted ? QString() : diffResult->getAddedText(i), Qt::AlignLeft);
        formatter.endTableRow();
    }

    formatter.endTable();

    if (isTextOmitted)
    {
        formatter.endl();
        formatter.writeText("text-omitted", PDFToolTranslationContext::tr("The texts of the differences are not listed: a document does not allow copying of its content."));
    }
    for (const pdf::PDFDocumentToolFact& fact : result.facts)
    {
        if (fact.id == QLatin1String("textTooLarge"))
        {
            formatter.endl();
            formatter.writeText("text-too-large", fact.value);
        }
    }

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);
    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolDiff::getOptionsFlags() const
{
    return ConsoleFormat | Diff;
}

}   // namespace pdftool
