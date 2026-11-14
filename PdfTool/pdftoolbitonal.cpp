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

#include "pdftoolbitonal.h"

#include "pdfdocumenttool_bitonal.h"
#include "pdfdocumentwriter.h"

#include <QJsonArray>

#include <vector>

namespace pdftool
{

static PDFToolBitonal s_bitonalApplication;

/// The tool Convert to Black & White of the core with the color management of the options of
/// the command (--cms-*): the one implementation of the conversion (step 4 of phase 2, B67), its
/// run normalizes and validates the parameters as for the application and MCP
class PDFToolBitonalDocumentTool : public pdf::PDFDocumentToolBitonal
{
public:
    explicit PDFToolBitonalDocumentTool(const pdf::PDFCMSSettings& cmsSettings) :
        m_cmsSettings(cmsSettings)
    {

    }

    virtual pdf::PDFDocumentToolResult execute(const pdf::PDFDocumentToolContext& context, const QVariantMap& parameters, const pdf::PDFOperationControl* control, pdf::PDFProgressSink* progress) const override
    {
        return convert(context, parameters, m_cmsSettings, control, progress);
    }

    /// The value of the parameter "method" of the method of the options
    static QString getMethodId(pdf::PDFImageConversion::ConversionMethod method)
    {
        switch (method)
        {
            case pdf::PDFImageConversion::ConversionMethod::Manual:
                return QString::fromLatin1(METHOD_THRESHOLD);
            case pdf::PDFImageConversion::ConversionMethod::Adaptive:
                return QString::fromLatin1(METHOD_ADAPTIVE);
            case pdf::PDFImageConversion::ConversionMethod::Dither:
                return QString::fromLatin1(METHOD_DITHER);
            default:
                break;
        }
        return QString::fromLatin1(METHOD_AUTOMATIC);
    }

    /// The value of the parameter "compression" of the compression of the options
    static QString getCompressionId(pdf::PDFBitonalDocumentCreator::Compression compression)
    {
        switch (compression)
        {
            case pdf::PDFBitonalDocumentCreator::Compression::Flate:
                return QString::fromLatin1(COMPRESSION_FLATE);
            case pdf::PDFBitonalDocumentCreator::Compression::RunLength:
                return QString::fromLatin1(COMPRESSION_RUN_LENGTH);
            case pdf::PDFBitonalDocumentCreator::Compression::CCITTGroup4:
                return QString::fromLatin1(COMPRESSION_CCITT_G4);
            case pdf::PDFBitonalDocumentCreator::Compression::JBIG2:
                return QString::fromLatin1(COMPRESSION_JBIG2);
            default:
                break;
        }
        return QString::fromLatin1(COMPRESSION_AUTO);
    }

    /// The value of a fact of the result, empty = none
    static QString getFact(const pdf::PDFDocumentToolResult& result, const QString& id)
    {
        for (const pdf::PDFDocumentToolFact& fact : result.facts)
        {
            if (fact.id == id)
            {
                return fact.value;
            }
        }
        return QString();
    }

private:
    pdf::PDFCMSSettings m_cmsSettings;
};

QString PDFToolBitonal::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "bitonal";

        case Name:
            return PDFToolTranslationContext::tr("Bitonal Document");

        case Description:
            return PDFToolTranslationContext::tr("Convert a document into a bitonal (monochromatic) one.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolBitonal::execute(const PDFToolOptions& options)
{
    using Tool = PDFToolBitonalDocumentTool;
    using ItemMode = pdf::PDFBitonalDocumentCreator::ItemMode;

    if (!options.bitonalInvalidArgument.isEmpty())
    {
        PDFConsole::writeError(options.bitonalInvalidArgument, options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (options.bitonalDocument.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("No output document specified."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (options.bitonalThreshold < 0 || options.bitonalThreshold > 255)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Threshold must be in range 0 to 255."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    const bool isPageSource = options.bitonalSource == pdf::PDFBitonalDocumentCreator::ConversionSource::Pages;

    if (!isPageSource && options.isPageRangeSet())
    {
        // Images are not bound to a single page - the same image can be used on many
        // pages and images inside form XObjects are not reachable from the page
        // resources at all, so a page range cannot be honored reliably here.
        PDFConsole::writeError(PDFToolTranslationContext::tr("Page range can be used only with '--bitonal-source pages'."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (!isPageSource && options.bitonalDetectBlankPages)
    {
        // A blank page is a property of the page, a single image of a page carries no
        // information about it
        PDFConsole::writeError(PDFToolTranslationContext::tr("The option '--bitonal-detect-blank' can be used only with '--bitonal-source pages'."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (options.bitonalDpiResolution != 0 &&
        (options.bitonalDpiResolution < pdf::PDFBitonalDocumentCreator::MINIMUM_DPI_RESOLUTION ||
         options.bitonalDpiResolution > pdf::PDFBitonalDocumentCreator::MAXIMUM_DPI_RESOLUTION))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Resolution must be zero, or in range %1 to %2 DPI.").arg(pdf::PDFBitonalDocumentCreator::MINIMUM_DPI_RESOLUTION)
                                                                                                                 .arg(pdf::PDFBitonalDocumentCreator::MAXIMUM_DPI_RESOLUTION), options.outputCodec);
        return ErrorInvalidArguments;
    }

    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    // The modes of the items: the mode of the options for every converted item; with a page
    // range the pages outside of it stay unchanged
    std::vector<Tool::ItemEntry> entries;
    if (isPageSource)
    {
        QString parseError;
        const pdf::PDFInteger pageCount = pdf::PDFInteger(document.getCatalog()->getPageCount());
        const std::vector<pdf::PDFInteger> pageIndices = options.getPageRange(pageCount, parseError, true);

        if (!parseError.isEmpty())
        {
            PDFConsole::writeError(parseError, options.outputCodec);
            return ErrorInvalidArguments;
        }

        if (pageIndices.empty())
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("No page has been selected for the conversion."), options.outputCodec);
            return ErrorInvalidArguments;
        }

        std::vector<char> isSelected(size_t(pageCount), 0);
        for (const pdf::PDFInteger pageIndex : pageIndices)
        {
            if (pageIndex >= 0 && pageIndex < pageCount)
            {
                isSelected[size_t(pageIndex)] = 1;
            }
        }

        for (pdf::PDFInteger pageIndex = 0; pageIndex < pageCount; ++pageIndex)
        {
            const ItemMode mode = isSelected[size_t(pageIndex)] ? options.bitonalItemMode : ItemMode::Original;
            if (mode != ItemMode::Algorithm)
            {
                Tool::ItemEntry entry;
                entry.pageIndex = pageIndex;
                entry.mode = mode;
                entries.push_back(entry);
            }
        }
    }
    else if (options.bitonalItemMode != ItemMode::Algorithm)
    {
        const pdf::PDFBitonalDocumentCreator creator(&document, nullptr, nullptr);
        for (const pdf::PDFObjectReference reference : creator.getConvertibleImages())
        {
            Tool::ItemEntry entry;
            entry.image = pdf::PDFDocumentImageList::Key::createReference(reference);
            entry.mode = options.bitonalItemMode;
            entries.push_back(entry);
        }
    }

    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_SOURCE), QString::fromLatin1(isPageSource ? Tool::SOURCE_PAGES : Tool::SOURCE_IMAGES));
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_METHOD), Tool::getMethodId(options.bitonalMethod));
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_THRESHOLD), qint64(options.bitonalThreshold));
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_COMPRESSION), Tool::getCompressionId(options.bitonalCompression));
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_ITEMS), Tool::createItemsText(entries));
    if (isPageSource)
    {
        parameters.insert(QString::fromLatin1(Tool::PARAMETER_DPI), qint64(options.bitonalDpiResolution));
        parameters.insert(QString::fromLatin1(Tool::PARAMETER_REPLACE_BLANK_PAGES), options.bitonalDetectBlankPages);
    }

    pdf::PDFDocumentToolInputDocument input = pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document))), sourceData, options.document, false);
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(std::move(input));

    const Tool tool(options.cmsSettings);
    const pdf::PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);

    if (result.status == pdf::PDFDocumentToolResultStatus::NothingToDo)
    {
        PDFConsole::writeError(isPageSource ? PDFToolTranslationContext::tr("No page has been selected for the conversion.")
                                            : PDFToolTranslationContext::tr("Document does not contain any convertible image."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (!result.isSuccess() || result.documents.empty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to convert the document into the bitonal format. %1").arg(result.message), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    if (options.bitonalDetectBlankPages)
    {
        const qsizetype blankPageCount = result.data.value(QStringLiteral("blankPages")).toArray().size();
        PDFConsole::writeError(PDFToolTranslationContext::tr("Note: %1 pages have been detected as blank and they are replaced by a white fill.").arg(blankPageCount), options.outputCodec);
    }

    const QString notConverted = Tool::getFact(result, QStringLiteral("notConverted"));
    if (!notConverted.isEmpty())
    {
        // The document has been created, but a part of it could not be converted and it
        // is left in the original, colored form. The user has to know about it.
        PDFConsole::writeError(PDFToolTranslationContext::tr("Warning: some items could not be converted and they are left unchanged: %1.").arg(notConverted), options.outputCodec);
    }

    const QString renderingIssues = Tool::getFact(result, QStringLiteral("renderingIssues"));
    if (!renderingIssues.isEmpty())
    {
        // The pages are converted (owner 14. 11. 2026), the renderer skipped a part of them
        PDFConsole::writeError(PDFToolTranslationContext::tr("Warning: rendering issues: %1.").arg(renderingIssues), options.outputCodec);
    }

    const QString conformance = Tool::getFact(result, QStringLiteral("conformance"));
    if (!conformance.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Note: %1").arg(conformance), options.outputCodec);
    }

    pdf::PDFDocumentWriter writer(nullptr);
    pdf::PDFOperationResult writeResult = writer.write(options.bitonalDocument, result.documents.front().document.data(), true);
    if (!writeResult)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to write bitonal document. %1").arg(writeResult.getErrorMessage()), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolBitonal::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | ColorManagementSystem | Bitonal;
}

}   // namespace pdftool
