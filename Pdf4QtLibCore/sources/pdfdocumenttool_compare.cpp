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

#include "pdfdocumenttool_compare.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumenttool_merge.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfoperationcontrol.h"

#include <QFileInfo>
#include <QJsonArray>

#include "pdfdbgheap.h"

namespace pdf
{

/// The parts of the parameters of Compare
class PDFDocumentToolCompareHelper
{
public:
    PDFDocumentToolCompareHelper() = delete;

    /// The output of the normalized parameters (summary without one)
    static QString getOutput(const QVariantMap& parameters)
    {
        const QString output = parameters.value(QLatin1String(PDFDocumentToolCompare::PARAMETER_OUTPUT)).toString();
        return output.isEmpty() ? QString::fromLatin1(PDFDocumentToolCompare::OUTPUT_SUMMARY) : output;
    }

    /// A Bool of the normalized parameters (true without one: the filters show everything)
    static bool isIncluded(const QVariantMap& parameters, const char* name)
    {
        return parameters.value(QLatin1String(name), true).toBool();
    }

    /// A difference of the type is shown by the filters of the parameters (PDFDiffResult::filter)
    static bool isShown(PDFDiffResult::Type type, const QVariantMap& parameters)
    {
        const QString category = PDFDocumentToolCompare::getCategoryId(type);
        if (category == QLatin1String(PDFDocumentToolCompare::CATEGORY_TEXT))
        {
            return isIncluded(parameters, PDFDocumentToolCompare::PARAMETER_INCLUDE_TEXT);
        }
        if (category == QLatin1String(PDFDocumentToolCompare::CATEGORY_VECTOR_GRAPHICS))
        {
            return isIncluded(parameters, PDFDocumentToolCompare::PARAMETER_INCLUDE_VECTOR_GRAPHICS);
        }
        if (category == QLatin1String(PDFDocumentToolCompare::CATEGORY_IMAGES))
        {
            return isIncluded(parameters, PDFDocumentToolCompare::PARAMETER_INCLUDE_IMAGES);
        }
        if (category == QLatin1String(PDFDocumentToolCompare::CATEGORY_SHADING))
        {
            return isIncluded(parameters, PDFDocumentToolCompare::PARAMETER_INCLUDE_SHADING);
        }
        if (category == QLatin1String(PDFDocumentToolCompare::CATEGORY_PAGE_MOVEMENT))
        {
            return isIncluded(parameters, PDFDocumentToolCompare::PARAMETER_INCLUDE_PAGE_MOVEMENT);
        }
        return false;
    }

    /// The pages of a document of the parameter \p name (indices from 0 as a set): the range is
    /// parsed and checked against the pages of the document before anything is unfolded (B174,
    /// B192: the parser of Page selection, at most PDFDocumentToolPageSelection::MAXIMUM_PAGES_LENGTH
    /// characters; a document without pages is an error)
    static std::optional<PDFClosedIntervalSet> getPages(const QVariantMap& parameters, const char* name, const PDFDocument* document, QString* errorMessage)
    {
        // A value, which is missing (a caller of compare without the normalized parameters), is all pages
        const QVariant value = parameters.value(QLatin1String(name));
        const std::optional<PDFDocumentToolPageSelection> selection = value.isValid() ? PDFDocumentToolPageSelection::fromVariant(value, errorMessage) : PDFDocumentToolPageSelection();
        if (!selection)
        {
            return std::nullopt;
        }

        const std::vector<PDFInteger> indices = selection->getPageIndices(document, errorMessage);
        if (indices.empty())
        {
            return std::nullopt;
        }

        // The runs of consecutive pages (the indices are sorted and unique)
        PDFClosedIntervalSet pages;
        size_t runStart = 0;
        for (size_t i = 1; i <= indices.size(); ++i)
        {
            if (i == indices.size() || indices[i] != indices[i - 1] + 1)
            {
                pages.addInterval(indices[runStart], indices[i - 1]);
                runStart = i;
            }
        }
        return pages;
    }

    /// The input permits the extraction of its content (Table 22, bit 5; the verified owner password)
    static bool isTextPermitted(const PDFDocumentToolInputDocument& input)
    {
        return input.isOwnerPasswordVerified || !input.document ||
               PDFSecurityHandler::isAllowedByRevision(input.document->getStorage().getSecurityHandler(), PDFSecurityHandler::Permission::CopyContent);
    }

    /// The base name of an input for the names of the outputs ("Report_v1")
    static QString getBaseName(const PDFDocumentToolInputDocument& input, bool isOriginal)
    {
        const QString baseName = QFileInfo(input.fileName).completeBaseName();
        if (!baseName.isEmpty())
        {
            return baseName;
        }
        return isOriginal ? PDFDocumentToolCompare::tr("Original") : PDFDocumentToolCompare::tr("Revised");
    }

    /// The rectangles of a side of a difference as [[x, y, width, height]] and their pages (from 1)
    static void addRectangles(const std::pair<PDFDiffResult::RectInfosIt, PDFDiffResult::RectInfosIt>& rectangles, QJsonArray* rectangleArray, QJsonArray* pageArray)
    {
        for (auto it = rectangles.first; it != rectangles.second; ++it)
        {
            const QRectF& rect = it->second;
            rectangleArray->append(QJsonArray({ rect.x(), rect.y(), rect.width(), rect.height() }));
            pageArray->append(qint64(it->first + 1));
        }
    }

    /// A page from 1 or null
    static QJsonValue getPageValue(PDFInteger pageIndex)
    {
        return pageIndex >= 0 ? QJsonValue(qint64(pageIndex + 1)) : QJsonValue(QJsonValue::Null);
    }
};

void PDFDiffResultCollector::setResult(PDFDiffResult result)
{
    QMutexLocker locker(&m_mutex);
    m_result = std::move(result);
}

std::optional<PDFDiffResult> PDFDiffResultCollector::getResult() const
{
    QMutexLocker locker(&m_mutex);
    return m_result;
}

QString PDFDocumentToolCompare::getKindId(PDFDiffResult::Type type)
{
    using Type = PDFDiffResult::Type;
    switch (type)
    {
        case Type::Invalid:
            break;
        case Type::PageMoved:
            return QStringLiteral("page-moved");
        case Type::PageAdded:
            return QStringLiteral("page-added");
        case Type::PageRemoved:
            return QStringLiteral("page-removed");
        case Type::RemovedTextCharContent:
            return QStringLiteral("text-characters-removed");
        case Type::RemovedVectorGraphicContent:
            return QStringLiteral("vector-graphics-removed");
        case Type::RemovedImageContent:
            return QStringLiteral("image-removed");
        case Type::RemovedShadingContent:
            return QStringLiteral("shading-removed");
        case Type::AddedTextCharContent:
            return QStringLiteral("text-characters-added");
        case Type::AddedVectorGraphicContent:
            return QStringLiteral("vector-graphics-added");
        case Type::AddedImageContent:
            return QStringLiteral("image-added");
        case Type::AddedShadingContent:
            return QStringLiteral("shading-added");
        case Type::TextReplaced:
            return QStringLiteral("text-replaced");
        case Type::TextAdded:
            return QStringLiteral("text-added");
        case Type::TextRemoved:
            return QStringLiteral("text-removed");
    }
    return QString();
}

QString PDFDocumentToolCompare::getCategoryId(PDFDiffResult::Type type)
{
    using Type = PDFDiffResult::Type;
    switch (type)
    {
        case Type::Invalid:
            break;
        case Type::PageMoved:
        case Type::PageAdded:
        case Type::PageRemoved:
            return QString::fromLatin1(CATEGORY_PAGE_MOVEMENT);
        case Type::RemovedTextCharContent:
        case Type::AddedTextCharContent:
        case Type::TextReplaced:
        case Type::TextAdded:
        case Type::TextRemoved:
            return QString::fromLatin1(CATEGORY_TEXT);
        case Type::RemovedVectorGraphicContent:
        case Type::AddedVectorGraphicContent:
            return QString::fromLatin1(CATEGORY_VECTOR_GRAPHICS);
        case Type::RemovedImageContent:
        case Type::AddedImageContent:
            return QString::fromLatin1(CATEGORY_IMAGES);
        case Type::RemovedShadingContent:
        case Type::AddedShadingContent:
            return QString::fromLatin1(CATEGORY_SHADING);
    }
    return QString();
}

QString PDFDocumentToolCompare::getChangeId(PDFDiffResult::Type type)
{
    using Type = PDFDiffResult::Type;
    switch (type)
    {
        case Type::Invalid:
            break;
        case Type::PageMoved:
            return QString::fromLatin1(CHANGE_MOVED);
        case Type::PageAdded:
        case Type::AddedTextCharContent:
        case Type::AddedVectorGraphicContent:
        case Type::AddedImageContent:
        case Type::AddedShadingContent:
        case Type::TextAdded:
            return QString::fromLatin1(CHANGE_ADDED);
        case Type::PageRemoved:
        case Type::RemovedTextCharContent:
        case Type::RemovedVectorGraphicContent:
        case Type::RemovedImageContent:
        case Type::RemovedShadingContent:
        case Type::TextRemoved:
            return QString::fromLatin1(CHANGE_REMOVED);
        case Type::TextReplaced:
            return QString::fromLatin1(CHANGE_REPLACED);
    }
    return QString();
}

QString PDFDocumentToolCompare::getKindDescription(const QString& kindId)
{
    using Type = PDFDiffResult::Type;
    constexpr std::array types = { Type::PageMoved, Type::PageAdded, Type::PageRemoved, Type::RemovedTextCharContent, Type::RemovedVectorGraphicContent,
                                   Type::RemovedImageContent, Type::RemovedShadingContent, Type::AddedTextCharContent, Type::AddedVectorGraphicContent,
                                   Type::AddedImageContent, Type::AddedShadingContent, Type::TextReplaced, Type::TextAdded, Type::TextRemoved };
    for (const Type type : types)
    {
        if (getKindId(type) == kindId)
        {
            return PDFDiffResult::getTypeDescription(type);
        }
    }
    return QString();
}

PDFDiffReportView PDFDocumentToolCompare::getView(const QString& id)
{
    if (id == QLatin1String(VIEW_ORIGINAL))
    {
        return PDFDiffReportView::Original;
    }
    if (id == QLatin1String(VIEW_REVISED))
    {
        return PDFDiffReportView::Revised;
    }
    if (id == QLatin1String(VIEW_OVERLAY))
    {
        return PDFDiffReportView::Overlay;
    }
    return PDFDiffReportView::SideBySide;
}

PDFDiff::Options PDFDocumentToolCompare::getOptions(const QVariantMap& parameters)
{
    // The pages are matched by every kind of their content (the old Diff); the two options of the menu Options
    PDFDiff::Options options = { PDFDiff::PC_Text, PDFDiff::PC_VectorGraphics, PDFDiff::PC_Images, PDFDiff::PC_Mesh };
    options.setFlag(PDFDiff::CompareTextsAsVector, parameters.value(QLatin1String(PARAMETER_COMPARE_TEXT_AS_VECTOR)).toBool());
    options.setFlag(PDFDiff::CompareWords, !parameters.value(QLatin1String(PARAMETER_COMPARE_CHARACTERS)).toBool());
    return options;
}

PDFDiffResult PDFDocumentToolCompare::filter(const PDFDiffResult& result, const QVariantMap& parameters)
{
    using Helper = PDFDocumentToolCompareHelper;
    PDFDiffResult copy = result;
    return copy.filter(Helper::isIncluded(parameters, PARAMETER_INCLUDE_PAGE_MOVEMENT), Helper::isIncluded(parameters, PARAMETER_INCLUDE_TEXT),
                       Helper::isIncluded(parameters, PARAMETER_INCLUDE_VECTOR_GRAPHICS), Helper::isIncluded(parameters, PARAMETER_INCLUDE_IMAGES),
                       Helper::isIncluded(parameters, PARAMETER_INCLUDE_SHADING));
}

QString PDFDocumentToolCompare::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolCompare::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createPageSelection(tr, QString::fromLatin1(PARAMETER_ORIGINAL_PAGES), QT_TR_NOOP("Original pages"), QT_TR_NOOP("Pages of the original document, which are compared")).setStored(false));
    definitions.push_back(Definition::createPageSelection(tr, QString::fromLatin1(PARAMETER_REVISED_PAGES), QT_TR_NOOP("Revised pages"), QT_TR_NOOP("Pages of the revised document, which are compared")).setStored(false));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_COMPARE_TEXT_AS_VECTOR), QT_TR_NOOP("Compare texts as vector graphics"), QT_TR_NOOP("The texts are compared as shapes, not as characters"), false));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_COMPARE_CHARACTERS), QT_TR_NOOP("Compare characters instead of words"), QT_TR_NOOP("The texts are compared character by character"), false));

    // The five filters of the menu Filter (492:20366)
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_INCLUDE_TEXT), QT_TR_NOOP("Text"), QT_TR_NOOP("The differences of the text are shown"), true));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_INCLUDE_VECTOR_GRAPHICS), QT_TR_NOOP("Vector graphics"), QT_TR_NOOP("The differences of the vector graphics are shown"), true));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_INCLUDE_IMAGES), QT_TR_NOOP("Images"), QT_TR_NOOP("The differences of the images are shown"), true));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_INCLUDE_SHADING), QT_TR_NOOP("Shading"), QT_TR_NOOP("The differences of the shadings are shown"), true));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_INCLUDE_PAGE_MOVEMENT), QT_TR_NOOP("Page movement"), QT_TR_NOOP("The moved, added and removed pages are shown"), true));

    std::vector<PDFDocumentToolParameterOption> outputs;
    outputs.push_back({ QString::fromLatin1(OUTPUT_SUMMARY), QT_TR_NOOP("Summary"), QT_TR_NOOP("The differences as data") });
    outputs.push_back({ QString::fromLatin1(OUTPUT_XML), QT_TR_NOOP("Differences as XML"), QT_TR_NOOP("The differences as an XML file") });
    outputs.push_back({ QString::fromLatin1(OUTPUT_REPORT), QT_TR_NOOP("Report as PDF"), QT_TR_NOOP("A PDF with the pages and the differences marked") });
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_OUTPUT), QT_TR_NOOP("Output"), QT_TR_NOOP("What the comparison gives"), std::move(outputs), QString::fromLatin1(OUTPUT_SUMMARY)));

    std::vector<PDFDocumentToolParameterOption> views;
    views.push_back({ QString::fromLatin1(VIEW_SIDE_BY_SIDE), QT_TR_NOOP("Side by Side"), QT_TR_NOOP("The original and the revised page next to each other") });
    views.push_back({ QString::fromLatin1(VIEW_ORIGINAL), QT_TR_NOOP("Original Only"), QT_TR_NOOP("The pages of the original document") });
    views.push_back({ QString::fromLatin1(VIEW_REVISED), QT_TR_NOOP("Revised Only"), QT_TR_NOOP("The pages of the revised document") });
    views.push_back({ QString::fromLatin1(VIEW_OVERLAY), QT_TR_NOOP("Overlay"), QT_TR_NOOP("The revised page over the original page") });
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_VIEW), QT_TR_NOOP("View"), QT_TR_NOOP("Pages of the report"), std::move(views), QString::fromLatin1(VIEW_SIDE_BY_SIDE)));
    return definitions;
}

PDFDocumentToolResultKind PDFDocumentToolCompare::getResultKind(const QVariantMap& parameters) const
{
    // Plan 4.9: the report PDF is a new document (the other kind of the row of the catalog); the summary and
    // the XML are analyses
    return PDFDocumentToolCompareHelper::getOutput(parameters) == QLatin1String(OUTPUT_REPORT) ? PDFDocumentToolResultKind::NewDocument : PDFDocumentToolResultKind::Analysis;
}

PDFDocumentToolPreselectedResult PDFDocumentToolCompare::getPreselectedResult(const QVariantMap& parameters) const
{
    // The report is a file (FileOnly, 492:107143: its name and folder, Open the report in a new tab)
    return getResultKind(parameters) == PDFDocumentToolResultKind::NewDocument ? PDFDocumentToolPreselectedResult::File : PDFDocumentToolPreselectedResult::None;
}

PDFDocumentToolSignatureImpact PDFDocumentToolCompare::getSignatureImpact(const QVariantMap& parameters) const
{
    // The open documents never change
    Q_UNUSED(parameters);
    return PDFDocumentToolSignatureImpact::None;
}

bool PDFDocumentToolCompare::isNewDocumentOfInputs(const QVariantMap& parameters) const
{
    // Q7.2: the report is a new document of the pages of the inputs and it is not encrypted - the rule of Merge
    return getResultKind(parameters) == PDFDocumentToolResultKind::NewDocument;
}

std::vector<PDFSecurityHandler::Permission> PDFDocumentToolCompare::getRequiredPermissions(const QVariantMap& parameters) const
{
    // Q7.2: the XML of the differences holds the texts of the documents - an extraction (bit 5); viewing the
    // differences is reading the documents; the report is checked by the rule of Merge (validate)
    std::vector<PDFSecurityHandler::Permission> permissions = PDFDocumentTool::getRequiredPermissions(parameters);
    if (PDFDocumentToolCompareHelper::getOutput(parameters) == QLatin1String(OUTPUT_XML))
    {
        permissions.push_back(PDFSecurityHandler::Permission::CopyContent);
    }
    return permissions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolCompare::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (!errors.empty())
    {
        return errors;
    }

    // Exactly the original and the revised document (the catalog: inputs {2, 2})
    if (!getInputCountRange().contains(int(context.documents.size())))
    {
        errors.push_back({ QString(), tr("Compare needs two documents: the original and the revised one.") });
        return errors;
    }

    // B161, B210: the report is a new document of the inputs without encryption - the rule of Merge (a
    // restricted input needs its owner password), checked in the core for every caller (PdfTool, MCP)
    if (isNewDocumentOfInputs(parameters))
    {
        for (const PDFDocumentToolInputDocument& input : context.documents)
        {
            const PDFDocumentMerger::Permission permission = PDFDocumentMerger::getPermission(input.document.data(), input.isOwnerPasswordVerified);
            if (permission != PDFDocumentMerger::Permission::Permitted)
            {
                errors.push_back({ QString(), PDFDocumentMerger::getPermissionText(permission, QFileInfo(input.fileName).fileName()) });
                return errors;
            }
        }
    }

    // B174, B192: the ranges of the pages of both documents (a document without pages is an error)
    const std::array<const char*, 2> pageParameters = { PARAMETER_ORIGINAL_PAGES, PARAMETER_REVISED_PAGES };
    for (size_t i = 0; i < pageParameters.size(); ++i)
    {
        QString errorMessage;
        if (!PDFDocumentToolCompareHelper::getPages(parameters, pageParameters[i], context.documents[i].document.data(), &errorMessage))
        {
            errors.push_back({ QString::fromLatin1(pageParameters[i]), errorMessage });
        }
    }

    return errors;
}

PDFDocumentToolResult PDFDocumentToolCompare::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    return compare(context, parameters, control, progress, nullptr);
}

PDFDocumentToolResult PDFDocumentToolCompare::compare(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress, PDFDiffResultCollector* collector)
{
    using Helper = PDFDocumentToolCompareHelper;

    if (context.documents.size() != 2 || !context.documents[0].document || !context.documents[1].document)
    {
        return PDFDocumentToolResult::createFailed(tr("Compare needs two documents: the original and the revised one."));
    }

    const PDFDocumentToolInputDocument& original = context.documents[0];
    const PDFDocumentToolInputDocument& revised = context.documents[1];

    QString errorMessage;
    const std::optional<PDFClosedIntervalSet> originalPages = Helper::getPages(parameters, PARAMETER_ORIGINAL_PAGES, original.document.data(), &errorMessage);
    if (!originalPages)
    {
        return PDFDocumentToolResult::createFailed(tr("Original document: %1").arg(errorMessage));
    }
    const std::optional<PDFClosedIntervalSet> revisedPages = Helper::getPages(parameters, PARAMETER_REVISED_PAGES, revised.document.data(), &errorMessage);
    if (!revisedPages)
    {
        return PDFDocumentToolResult::createFailed(tr("Revised document: %1").arg(errorMessage));
    }

    // R7-15: in this thread, no nested job; B173: a canceled comparison is Stopped, never "no differences"
    PDFDiffResult diffResult = PDFDiff::compare(original.document.data(), revised.document.data(), *originalPages, *revisedPages, getOptions(parameters), control, progress);
    if (PDFOperationControl::isOperationCanceled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }
    if (!diffResult.getResult())
    {
        return PDFDocumentToolResult::createFailed(diffResult.getResult().getErrorMessage());
    }

    // Q7.2: the texts of the differences are an extraction of the content (bit 5 of every input)
    const bool isTextIncluded = Helper::isTextPermitted(original) && Helper::isTextPermitted(revised);

    PDFDocumentToolResult result;
    result.data = createData(diffResult, parameters, MAXIMUM_DIFFERENCES, isTextIncluded);

    const PDFDiffResult shownResult = filter(diffResult, parameters);
    const int total = int(diffResult.getDifferencesCount());
    const int shown = int(shownResult.getDifferencesCount());
    const int pagePairCount = int(diffResult.getPageSequence().size());

    // The facts (machine-readable, MCP and PdfTool: %n in the C locale; the GUI makes its own facts by PDFAppLocale)
    const QString output = Helper::getOutput(parameters);
    const QString names = tr("%1 vs %2").arg(Helper::getBaseName(original, true), Helper::getBaseName(revised, false));
    if (output == QLatin1String(OUTPUT_REPORT))
    {
        PDFDiffReportSettings settings;
        settings.view = getView(parameters.value(QLatin1String(PARAMETER_VIEW)).toString());
        settings.author = context.authorName;

        std::optional<PDFDocument> report = PDFDiffReportBuilder::createReport(original, revised, shownResult, settings, control, &errorMessage);
        if (PDFOperationControl::isOperationCanceled(control))
        {
            return PDFDocumentToolResult::createStopped();
        }
        if (!report)
        {
            return PDFDocumentToolResult::createFailed(errorMessage);
        }

        QByteArray writtenData = PDFDocumentToolNewDocument::write(&*report, &errorMessage);
        if (writtenData.isEmpty())
        {
            return PDFDocumentToolResult::createFailed(errorMessage);
        }

        QString pagesText;
        const int reportPageCount = int(report->getCatalog()->getPageCount());
        switch (settings.view)
        {
            case PDFDiffReportView::SideBySide:
                pagesText = tr("%n page pair(s), side by side", nullptr, pagePairCount);
                break;
            case PDFDiffReportView::Overlay:
                pagesText = tr("%n page pair(s), overlaid", nullptr, pagePairCount);
                break;
            case PDFDiffReportView::Original:
                pagesText = tr("%n page(s) of the original document", nullptr, reportPageCount);
                break;
            case PDFDiffReportView::Revised:
                pagesText = tr("%n page(s) of the revised document", nullptr, reportPageCount);
                break;
        }

        result.facts.push_back({ QStringLiteral("pages"), tr("Pages"), pagesText });
        result.facts.push_back({ QStringLiteral("differences"), tr("Differences"), tr("%n marked as annotations", nullptr, shown) });
        result.facts.push_back({ QStringLiteral("annotation"), tr("Annotation"), tr("Colored polygon with the description") });
        PDFDocumentToolNewDocument::addToResult(&result, std::move(*report), std::move(writtenData), tr("%1 - comparison").arg(names) + QStringLiteral(".pdf"));
    }
    else if (output == QLatin1String(OUTPUT_XML))
    {
        PDFDocumentToolOutputFile file;
        file.suggestedFileName = tr("%1 - differences").arg(names) + QStringLiteral(".xml");
        file.mimeType = QStringLiteral("application/xml");
        shownResult.saveToXML(&file.data);
        result.outputs.push_back(std::move(file));

        result.facts.push_back({ QStringLiteral("differences"), tr("Differences"), tr("%n shown difference(s)", nullptr, shown) });
        result.facts.push_back({ QStringLiteral("content"), tr("Content"), tr("Type, pages and changed text") });
        result.facts.push_back({ QStringLiteral("pageSequence"), tr("Page sequence"), tr("%n page pair(s)", nullptr, pagePairCount) });
    }
    else
    {
        result.facts.push_back({ QStringLiteral("differences"), tr("Differences"), (total > shown) ? tr("%1 shown · %2 hidden").arg(shown).arg(total - shown) : tr("%n difference(s)", nullptr, total) });
        result.facts.push_back({ QStringLiteral("pageSequence"), tr("Page sequence"), tr("%n page pair(s)", nullptr, pagePairCount) });
    }

    // B191, R7-6: the pages, whose text was not compared word by word
    const std::vector<PDFInteger> textTooLargePages = diffResult.getTextTooLargeLeftPageIndices();
    if (!textTooLargePages.empty())
    {
        const QString pages = PDFDocumentToolPageSelection::createRangeText(textTooLargePages);
        const QString text = (textTooLargePages.size() == 1) ? tr("Text of page %1 differs (too large to compare word by word)").arg(pages)
                                                              : tr("Text of pages %1 differs (too large to compare word by word)").arg(pages);
        result.facts.push_back({ QStringLiteral("textTooLarge"), tr("Text"), text });
    }

    result.message = diffResult.isSame() ? tr("No differences found.") : tr("%n difference(s) found.", nullptr, total);

    if (collector)
    {
        collector->setResult(std::move(diffResult));
    }

    return result;
}

QJsonObject PDFDocumentToolCompare::createData(const PDFDiffResult& result, const QVariantMap& parameters, int maximumDifferences, bool isTextIncluded)
{
    using Helper = PDFDocumentToolCompareHelper;

    // The counts of the whole comparison ("12 shown · 2 hidden", the counts of the menu Filter)
    const std::array<const char*, 5> categories = { CATEGORY_TEXT, CATEGORY_VECTOR_GRAPHICS, CATEGORY_IMAGES, CATEGORY_SHADING, CATEGORY_PAGE_MOVEMENT };
    const std::array<const char*, 4> changes = { CHANGE_REMOVED, CHANGE_ADDED, CHANGE_REPLACED, CHANGE_MOVED };
    std::map<QString, qint64> counts;
    for (const char* category : categories)
    {
        counts[QString::fromLatin1(category)] = 0;
    }
    for (const char* change : changes)
    {
        counts[QString::fromLatin1(change)] = 0;
    }

    const size_t total = result.getDifferencesCount();
    qint64 shown = 0;
    QJsonArray differences;
    for (size_t i = 0; i < total; ++i)
    {
        const PDFDiffResult::Type type = result.getType(i);
        ++counts[getCategoryId(type)];
        ++counts[getChangeId(type)];

        if (!Helper::isShown(type, parameters))
        {
            continue;
        }

        ++shown;
        if (differences.size() >= maximumDifferences)
        {
            continue;
        }

        QJsonObject difference;
        difference.insert(QStringLiteral("index"), qint64(i));
        difference.insert(QStringLiteral("kind"), getKindId(type));
        difference.insert(QStringLiteral("category"), getCategoryId(type));
        difference.insert(QStringLiteral("change"), getChangeId(type));
        difference.insert(QStringLiteral("originalPage"), Helper::getPageValue(result.getLeftPage(i)));
        difference.insert(QStringLiteral("revisedPage"), Helper::getPageValue(result.getRightPage(i)));
        difference.insert(QStringLiteral("description"), result.getDescription(i));

        if (result.isTextTooLarge(i))
        {
            difference.insert(QStringLiteral("isTextTooLarge"), true);
        }

        if (isTextIncluded)
        {
            const QString removedText = result.getRemovedText(i);
            const QString addedText = result.getAddedText(i);
            if (!removedText.isEmpty())
            {
                difference.insert(QStringLiteral("originalText"), removedText);
            }
            if (!addedText.isEmpty())
            {
                difference.insert(QStringLiteral("revisedText"), addedText);
            }
        }

        QJsonArray originalRects;
        QJsonArray originalRectPages;
        QJsonArray revisedRects;
        QJsonArray revisedRectPages;
        Helper::addRectangles(result.getLeftRectangles(i), &originalRects, &originalRectPages);
        Helper::addRectangles(result.getRightRectangles(i), &revisedRects, &revisedRectPages);
        difference.insert(QStringLiteral("originalRects"), originalRects);
        difference.insert(QStringLiteral("originalRectPages"), originalRectPages);
        difference.insert(QStringLiteral("revisedRects"), revisedRects);
        difference.insert(QStringLiteral("revisedRectPages"), revisedRectPages);

        differences.append(difference);
    }

    QJsonObject countsData;
    countsData.insert(QStringLiteral("total"), qint64(total));
    countsData.insert(QStringLiteral("shown"), shown);
    countsData.insert(QStringLiteral("hidden"), qint64(total) - shown);
    for (const auto& count : counts)
    {
        if (!count.first.isEmpty())
        {
            countsData.insert(count.first, count.second);
        }
    }

    QJsonArray pageSequence;
    qint64 originalPageCount = 0;
    qint64 revisedPageCount = 0;
    for (const PDFDiffResult::PageSequenceItem& item : result.getPageSequence())
    {
        QJsonObject pair;
        pair.insert(QStringLiteral("originalPage"), Helper::getPageValue(item.leftPage));
        pair.insert(QStringLiteral("revisedPage"), Helper::getPageValue(item.rightPage));
        pageSequence.append(pair);

        originalPageCount += (item.leftPage >= 0) ? 1 : 0;
        revisedPageCount += (item.rightPage >= 0) ? 1 : 0;
    }

    QJsonObject data;
    data.insert(QString::fromLatin1(DATA_IS_SAME), result.isSame());
    data.insert(QString::fromLatin1(DATA_COUNTS), countsData);
    data.insert(QString::fromLatin1(DATA_DIFFERENCES), differences);
    data.insert(QString::fromLatin1(DATA_TRUNCATED), shown > qint64(maximumDifferences));
    data.insert(QString::fromLatin1(DATA_PAGE_SEQUENCE), pageSequence);
    data.insert(QString::fromLatin1(DATA_ORIGINAL_PAGE_COUNT), originalPageCount);
    data.insert(QString::fromLatin1(DATA_REVISED_PAGE_COUNT), revisedPageCount);
    data.insert(QString::fromLatin1(DATA_IS_TEXT_OMITTED), !isTextIncluded);
    return data;
}

}   // namespace pdf
