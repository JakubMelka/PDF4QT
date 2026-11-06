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

#include "pdfdocumenttool_extracttext.h"

#include "pdfdocument.h"
#include "pdfoperationcontrol.h"

#include <QFileInfo>
#include <QLocale>

#include <cmath>
#include <limits>

namespace pdf
{

/// Helpers of the tool Extract Text
class PDFDocumentToolExtractTextHelper
{
public:
    PDFDocumentToolExtractTextHelper() = delete;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// Pages evenly spread over the given ones (all of them, when there are few)
    static std::vector<PDFInteger> createSample(const std::vector<PDFInteger>& pages, int maximumCount)
    {
        if (pages.size() <= size_t(maximumCount))
        {
            return pages;
        }

        std::vector<PDFInteger> sample;
        sample.reserve(size_t(maximumCount));
        for (int i = 0; i < maximumCount; ++i)
        {
            sample.push_back(pages[size_t(i) * pages.size() / size_t(maximumCount)]);
        }
        return sample;
    }

    /// A count scaled from the sample to all pages
    static qint64 scale(qint64 value, size_t sampleCount, size_t totalCount)
    {
        return sampleCount > 0 ? qint64(std::llround(double(value) * double(totalCount) / double(sampleCount))) : 0;
    }

    /// The document is tagged: its structure is the reading order of Auto
    static bool isTagged(const PDFDocument* document)
    {
        return PDFDocumentTextFlowFactory::getEffectiveAlgorithm(document, PDFDocumentTextFlowFactory::Algorithm::Auto) == PDFDocumentTextFlowFactory::Algorithm::Structure;
    }
};

std::vector<const char*> PDFDocumentToolExtractText::getIncludeParameters()
{
    return { PARAMETER_INCLUDE_PAGE_NUMBERS, PARAMETER_INCLUDE_TITLES, PARAMETER_INCLUDE_LANGUAGES, PARAMETER_INCLUDE_ALTERNATIVE_DESCRIPTIONS,
             PARAMETER_INCLUDE_EXPANDED_FORMS, PARAMETER_INCLUDE_ACTUAL_TEXT, PARAMETER_INCLUDE_PHONEMES };
}

PDFDocumentTextExporter::Options PDFDocumentToolExtractText::createOptions(const QVariantMap& parameters)
{
    using Helper = PDFDocumentToolExtractTextHelper;

    PDFDocumentTextExporter::Options options;
    options.showPageNumbers = parameters.value(Helper::name(PARAMETER_INCLUDE_PAGE_NUMBERS)).toBool();
    options.showStructureTitles = parameters.value(Helper::name(PARAMETER_INCLUDE_TITLES)).toBool();
    options.showStructureLanguage = parameters.value(Helper::name(PARAMETER_INCLUDE_LANGUAGES)).toBool();
    options.showStructureAlternativeDescription = parameters.value(Helper::name(PARAMETER_INCLUDE_ALTERNATIVE_DESCRIPTIONS)).toBool();
    options.showStructureExpandedForm = parameters.value(Helper::name(PARAMETER_INCLUDE_EXPANDED_FORMS)).toBool();
    options.showStructureActualText = parameters.value(Helper::name(PARAMETER_INCLUDE_ACTUAL_TEXT)).toBool();
    options.showStructurePhoneme = parameters.value(Helper::name(PARAMETER_INCLUDE_PHONEMES)).toBool();

    // A plain text has its paragraphs apart; a markup file keeps any character readable
    const PDFOutputFormatter::Style style = getStyle(parameters);
    options.separateItems = style == PDFOutputFormatter::Style::Text;
    options.sanitizeMarkup = style != PDFOutputFormatter::Style::Text;
    options.trimLineEnds = true;
    return options;
}

PDFDocumentTextFlowFactory::Algorithm PDFDocumentToolExtractText::getAlgorithm(const QVariantMap& parameters)
{
    const QString order = parameters.value(PDFDocumentToolExtractTextHelper::name(PARAMETER_ORDER)).toString();
    if (order == QLatin1String(ORDER_LAYOUT))
    {
        return PDFDocumentTextFlowFactory::Algorithm::Layout;
    }
    if (order == QLatin1String(ORDER_CONTENT_STREAM))
    {
        return PDFDocumentTextFlowFactory::Algorithm::Content;
    }
    if (order == QLatin1String(ORDER_STRUCTURE))
    {
        return PDFDocumentTextFlowFactory::Algorithm::Structure;
    }
    return PDFDocumentTextFlowFactory::Algorithm::Auto;
}

QString PDFDocumentToolExtractText::getOrderId(PDFDocumentTextFlowFactory::Algorithm algorithm)
{
    switch (algorithm)
    {
        case PDFDocumentTextFlowFactory::Algorithm::Content:
            return QString::fromLatin1(ORDER_CONTENT_STREAM);
        case PDFDocumentTextFlowFactory::Algorithm::Structure:
            return QString::fromLatin1(ORDER_STRUCTURE);
        case PDFDocumentTextFlowFactory::Algorithm::Auto:
            return QString::fromLatin1(ORDER_AUTO);
        case PDFDocumentTextFlowFactory::Algorithm::Layout:
            break;
    }
    return QString::fromLatin1(ORDER_LAYOUT);
}

PDFOutputFormatter::Style PDFDocumentToolExtractText::getStyle(const QVariantMap& parameters)
{
    const QString format = parameters.value(PDFDocumentToolExtractTextHelper::name(PARAMETER_FORMAT)).toString();
    if (format == QLatin1String(FORMAT_HTML))
    {
        return PDFOutputFormatter::Style::Html;
    }
    if (format == QLatin1String(FORMAT_XML))
    {
        return PDFOutputFormatter::Style::Xml;
    }
    return PDFOutputFormatter::Style::Text;
}

PDFDocumentTextExporter::Encoding PDFDocumentToolExtractText::getEncoding(const QVariantMap& parameters)
{
    const QString encoding = parameters.value(PDFDocumentToolExtractTextHelper::name(PARAMETER_ENCODING)).toString();
    if (encoding == QLatin1String(ENCODING_LATIN1))
    {
        return PDFDocumentTextExporter::Encoding::Latin1;
    }
    if (encoding == QLatin1String(ENCODING_SYSTEM))
    {
        return PDFDocumentTextExporter::Encoding::System;
    }
    return PDFDocumentTextExporter::Encoding::Utf8;
}

QString PDFDocumentToolExtractText::getExtension(const QVariantMap& parameters)
{
    switch (getStyle(parameters))
    {
        case PDFOutputFormatter::Style::Html:
            return QStringLiteral("html");
        case PDFOutputFormatter::Style::Xml:
            return QStringLiteral("xml");
        case PDFOutputFormatter::Style::Text:
            break;
    }
    return QStringLiteral("txt");
}

PDFDocumentTextFlow PDFDocumentToolExtractText::createFlow(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress)
{
    PDFDocumentTextFlowFactory factory;
    factory.setOperationControl(control);
    factory.setProgress(progress);
    factory.setStructureExport(true);
    return factory.create(document, pageIndices, getAlgorithm(parameters));
}

PDFDocumentTextExporter::EncodedText PDFDocumentToolExtractText::exportText(const PDFDocumentTextFlow& flow, const QVariantMap& parameters, const QString& documentTitle)
{
    PDFDocumentTextExporter::Options options = createOptions(parameters);
    const PDFOutputFormatter::Style style = getStyle(parameters);
    if (style == PDFOutputFormatter::Style::Html)
    {
        options.documentTitle = documentTitle;
    }

    PDFOutputFormatter formatter(style);
    formatter.setPlainText(true);
    PDFDocumentTextExporter::write(flow, options, formatter);
    return PDFDocumentTextExporter::encode(formatter.getString(), style, getEncoding(parameters));
}

std::vector<PDFInteger> PDFDocumentToolExtractText::getSelectedPages(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage)
{
    const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(parameters.value(PDFDocumentToolExtractTextHelper::name(PARAMETER_PAGES)), errorMessage);
    if (!selection)
    {
        return std::vector<PDFInteger>();
    }
    return selection->getPageIndices(document, errorMessage);
}

QString PDFDocumentToolExtractText::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolExtractText::getParameters() const
{
    return createParameters();
}

PDFDocumentToolParameterDefinitions PDFDocumentToolExtractText::createParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolExtractTextHelper;

    // The texts of the menus 339:8765
    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createPageSelection(tr, Helper::name(PARAMETER_PAGES), QT_TR_NOOP("Pages"), QT_TR_NOOP("Pages, whose text is extracted")).setStored(false));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_ORDER), QT_TR_NOOP("Reading order"), QT_TR_NOOP("In which order the text of a page is read"),
                                                 {
                                                     { QString::fromLatin1(ORDER_AUTO), QT_TR_NOOP("Auto"), QT_TR_NOOP("Structure when the document is tagged, otherwise layout"), QStringLiteral("zap") },
                                                     { QString::fromLatin1(ORDER_LAYOUT), QT_TR_NOOP("Layout"), QT_TR_NOOP("Blocks of text as they are placed on the page"), QStringLiteral("layout") },
                                                     { QString::fromLatin1(ORDER_CONTENT_STREAM), QT_TR_NOOP("Content stream"), QT_TR_NOOP("The order in which the text is written in the file"), QStringLiteral("stream") },
                                                     { QString::fromLatin1(ORDER_STRUCTURE), QT_TR_NOOP("Structure"), QT_TR_NOOP("Follows the tags; needs a tagged document"), QStringLiteral("tree") },
                                                 },
                                                 QString::fromLatin1(ORDER_AUTO)));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_INCLUDE_PAGE_NUMBERS), QT_TR_NOOP("Page numbers"), QT_TR_NOOP("A line “Page N” starts every page"), false));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_INCLUDE_TITLES), QT_TR_NOOP("Structure titles"), QT_TR_NOOP("Titles of tagged parts"), false));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_INCLUDE_LANGUAGES), QT_TR_NOOP("Languages"), QT_TR_NOOP("Language marks of tagged parts"), false));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_INCLUDE_ALTERNATIVE_DESCRIPTIONS), QT_TR_NOOP("Alternative descriptions"), QT_TR_NOOP("Text that describes pictures"), true));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_INCLUDE_EXPANDED_FORMS), QT_TR_NOOP("Expanded forms"), QT_TR_NOOP("Abbreviations written out"), false));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_INCLUDE_ACTUAL_TEXT), QT_TR_NOOP("Actual text"), QT_TR_NOOP("Replacement text of tagged content"), false));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_INCLUDE_PHONEMES), QT_TR_NOOP("Phonemes"), QT_TR_NOOP("Pronunciation hints"), false));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_FORMAT), QT_TR_NOOP("Format"), QT_TR_NOOP("Format of the file"),
                                                 {
                                                     { QString::fromLatin1(FORMAT_TEXT), QT_TR_NOOP("Plain text"), QT_TR_NOOP("A .txt file"), QStringLiteral("file-text") },
                                                     { QString::fromLatin1(FORMAT_HTML), QT_TR_NOOP("HTML"), QT_TR_NOOP("Headings and paragraphs for a browser"), QStringLiteral("code") },
                                                     { QString::fromLatin1(FORMAT_XML), QT_TR_NOOP("XML"), QT_TR_NOOP("Structured, for other programs"), QStringLiteral("code") },
                                                 },
                                                 QString::fromLatin1(FORMAT_TEXT)));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_ENCODING), QT_TR_NOOP("Encoding"), QT_TR_NOOP("Encoding of the characters of the file"),
                                                 {
                                                     { QString::fromLatin1(ENCODING_UTF8), QT_TR_NOOP("UTF-8"), QT_TR_NOOP("Recommended, every language"), QStringLiteral("encoding") },
                                                     { QString::fromLatin1(ENCODING_LATIN1), QT_TR_NOOP("Latin-1"), QT_TR_NOOP("Western European legacy programs"), QStringLiteral("encoding") },
                                                     { QString::fromLatin1(ENCODING_SYSTEM), QT_TR_NOOP("System"), QT_TR_NOOP("The encoding of this computer"), QStringLiteral("monitor") },
                                                 },
                                                 QString::fromLatin1(ENCODING_UTF8)));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolExtractText::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolExtractTextHelper;

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    const PDFDocument* document = context.getDocument();
    if (!errors.empty() || !document)
    {
        return errors;
    }

    QString errorMessage;
    if (getSelectedPages(document, parameters, &errorMessage).empty())
    {
        errors.push_back({ Helper::name(PARAMETER_PAGES), errorMessage });
    }

    // The order Structure reads the tags: an untagged document would give no text at all
    if (getAlgorithm(parameters) == PDFDocumentTextFlowFactory::Algorithm::Structure && !PDFDocumentTextFlowFactory::hasValidStructureTree(document))
    {
        errors.push_back({ Helper::name(PARAMETER_ORDER), tr("The document is not tagged, so it has no structure to follow. Use the order auto or layout.") });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolExtractText::computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolExtractTextHelper;

    const PDFDocument* document = context.getDocument();
    QString errorMessage;
    const std::vector<PDFInteger> pages = getSelectedPages(document, parameters, &errorMessage);

    PDFProgressPhases phases(progress);
    const int scanPhase = phases.addPhase(60, tr("Looking for text"), true);
    const int samplePhase = phases.addPhase(40, tr("Reading pages"), true);
    phases.start();

    // The pages with a text, then the text of a sample of them
    const std::vector<PDFInteger> pagesWithText = PDFDocumentTextExporter::findPagesWithText(document, pages, control, phases.getPhase(scanPhase));
    const std::vector<PDFInteger> sample = Helper::createSample(pagesWithText, PREVIEW_SAMPLE_PAGES);
    const PDFDocumentTextFlow flow = createFlow(document, sample, parameters, control, phases.getPhase(samplePhase));
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    const PDFDocumentTextExporter::TextCount count = PDFDocumentTextExporter::count(flow, createOptions(parameters));
    const PDFDocumentTextExporter::EncodedText text = exportText(flow, parameters, QString());
    phases.finish();

    PDFDocumentToolResult result;
    result.data.insert(QStringLiteral("tagged"), Helper::isTagged(document));
    result.data.insert(QStringLiteral("pageCount"), qint64(pages.size()));
    result.data.insert(QStringLiteral("pagesWithText"), qint64(pagesWithText.size()));
    result.data.insert(QStringLiteral("pagesWithoutText"), qint64(pages.size() - pagesWithText.size()));
    result.data.insert(QStringLiteral("words"), Helper::scale(count.words, sample.size(), pagesWithText.size()));
    result.data.insert(QStringLiteral("characters"), Helper::scale(count.characters, sample.size(), pagesWithText.size()));
    result.data.insert(QStringLiteral("size"), sample.empty() ? qint64(0) : Helper::scale(text.data.size(), sample.size(), pagesWithText.size()));
    result.data.insert(QStringLiteral("order"), getOrderId(PDFDocumentTextFlowFactory::getEffectiveAlgorithm(document, getAlgorithm(parameters))));
    result.data.insert(QStringLiteral("isEstimate"), sample.size() < pagesWithText.size());
    return result;
}

PDFDocumentToolResult PDFDocumentToolExtractText::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolExtractTextHelper;

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();

    QString errorMessage;
    const std::vector<PDFInteger> pages = getSelectedPages(document, parameters, &errorMessage);
    if (pages.empty())
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    // 356:13079: "Reading page 640 of 1,592"
    PDFProgressPhases phases(progress);
    const int readPhase = phases.addPhase(95, tr("Reading page"), true);
    const int writePhase = phases.addPhase(5, tr("Writing the text"), false);
    phases.start();

    const PDFDocumentTextFlow flow = createFlow(document, pages, parameters, control, phases.getPhase(readPhase));
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    phases.beginPhase(writePhase);
    const PDFDocumentTextExporter::Options options = createOptions(parameters);
    const PDFDocumentTextExporter::TextCount count = PDFDocumentTextExporter::count(flow, options);
    const qint64 pagesWithText = qint64(count.pagesWithText.size());
    const qint64 pagesWithoutText = qint64(pages.size()) - pagesWithText;
    const QLocale locale;
    const QString pagesValue = tr("%1 with text · %2 without").arg(locale.toString(pagesWithText), locale.toString(pagesWithoutText));

    // Never an empty file: a scan without text says so (tools.md: a run of nothing is a use)
    if (!count.hasText)
    {
        PDFDocumentToolResult result = PDFDocumentToolResult::createNothingToDo(tr("The selected pages have no text. They are most likely scanned images; nothing can be extracted from them."));
        result.facts.push_back({ QStringLiteral("pages"), tr("Pages"), pagesValue });
        result.data.insert(QStringLiteral("pageCount"), qint64(pages.size()));
        result.data.insert(QStringLiteral("pagesWithText"), pagesWithText);
        result.data.insert(QStringLiteral("pagesWithoutText"), pagesWithoutText);
        return result;
    }

    const QString baseName = QFileInfo(input.fileName).completeBaseName();
    const PDFDocumentTextExporter::EncodedText text = exportText(flow, parameters, input.fileName);
    if (text.data.size() > MAXIMUM_RESULT_SIZE)
    {
        return PDFDocumentToolResult::createFailed(tr("The text is too large to be written as one file. Choose fewer pages."));
    }
    phases.finish();

    PDFDocumentToolResult result;
    PDFDocumentToolOutputFile output;
    output.suggestedFileName = (baseName.isEmpty() ? QStringLiteral("document") : baseName) + QLatin1Char('.') + getExtension(parameters);
    output.mimeType = text.mimeType;
    output.data = text.data;
    result.outputs.push_back(std::move(output));

    // 339:9601
    result.facts.push_back({ QStringLiteral("pages"), tr("Pages"), pagesValue });
    result.facts.push_back({ QStringLiteral("words"), tr("Words"), locale.toString(count.words) });
    result.facts.push_back({ QStringLiteral("size"), tr("Size"), PDFDocumentToolFormat::formatSize(text.data.size()) });
    if (text.lostCharacters > 0)
    {
        const int lost = int(qMin<qint64>(text.lostCharacters, std::numeric_limits<int>::max()));
        result.facts.push_back({ QStringLiteral("encoding"), tr("Encoding"), lost == 1 ? tr("%Ln character replaced by ?", nullptr, lost) : tr("%Ln characters replaced by ?", nullptr, lost) });
    }

    result.data.insert(QStringLiteral("pageCount"), qint64(pages.size()));
    result.data.insert(QStringLiteral("pagesWithText"), pagesWithText);
    result.data.insert(QStringLiteral("pagesWithoutText"), pagesWithoutText);
    result.data.insert(QStringLiteral("words"), count.words);
    result.data.insert(QStringLiteral("characters"), count.characters);
    result.data.insert(QStringLiteral("size"), qint64(text.data.size()));
    result.data.insert(QStringLiteral("order"), getOrderId(PDFDocumentTextFlowFactory::getEffectiveAlgorithm(document, getAlgorithm(parameters))));
    result.data.insert(QStringLiteral("format"), parameters.value(Helper::name(PARAMETER_FORMAT)).toString());
    result.data.insert(QStringLiteral("encoding"), parameters.value(Helper::name(PARAMETER_ENCODING)).toString());
    result.data.insert(QStringLiteral("lostCharacters"), text.lostCharacters);
    return result;
}

PDFDocumentToolParameterDefinitions PDFDocumentToolExtractTextPage::getParameters() const
{
    PDFDocumentToolParameterDefinitions definitions = createParameters();
    definitions.push_back(PDFDocumentToolParameterDefinition::createInteger(tr, QString::fromLatin1(PARAMETER_PREVIEW_PAGE), QT_TR_NOOP("Page"), QT_TR_NOOP("Index of the page of the preview"),
                                                                            0, 0, PDFDocumentToolPageSelection::MAXIMUM_PAGE_NUMBER).setStored(false));
    return definitions;
}

PDFDocumentToolResult PDFDocumentToolExtractTextPage::computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    Q_UNUSED(progress);

    const PDFDocument* document = context.getDocument();
    const PDFInteger pageIndex = parameters.value(QString::fromLatin1(PARAMETER_PREVIEW_PAGE)).toLongLong();
    if (!document->getCatalog()->getPage(pageIndex))
    {
        return PDFDocumentToolResult::createFailed(tr("The page does not exist."));
    }

    const PDFDocumentTextFlow flow = createFlow(document, { pageIndex }, parameters, control, nullptr);
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    // The output of the format as it is in the file (a markup file shows its markup), bounded
    const PDFDocumentTextExporter::EncodedText text = exportText(flow, parameters, context.documents.front().fileName);
    QString output = PDFDocumentTextExporter::createDecoder(text.mimeType).decode(text.data);
    const bool isTruncated = output.size() > PREVIEW_TEXT_LIMIT;
    if (isTruncated)
    {
        output.truncate(PREVIEW_TEXT_LIMIT);
    }

    PDFDocumentToolResult result;
    result.data.insert(QStringLiteral("pageIndex"), qint64(pageIndex));
    result.data.insert(QStringLiteral("text"), output);
    result.data.insert(QStringLiteral("isTruncated"), isTruncated);
    result.data.insert(QStringLiteral("hasText"), PDFDocumentTextExporter::count(flow, createOptions(parameters)).hasText);
    result.data.insert(QStringLiteral("tagged"), PDFDocumentToolExtractTextHelper::isTagged(document));
    return result;
}

}   // namespace pdf
