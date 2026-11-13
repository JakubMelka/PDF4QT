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

#include "pdfdocumenttool_insert.h"

#include "pdfcatalog.h"
#include "pdfdocmdppermissions.h"
#include "pdfdocumentpageoperations.h"
#include "pdfdocumenttool_geometry.h"
#include "pdfdocumenttool_merge.h"
#include "pdfform.h"
#include "pdfoperationcontrol.h"
#include "pdfpagerangelist.h"
#include "pdfpagesizepresets.h"

#include <QFileInfo>
#include <QJsonArray>

namespace pdf
{

/// Helpers of Insert Pages and Add Image as Page
class PDFDocumentToolInsertHelper
{
public:
    PDFDocumentToolInsertHelper() = delete;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// Numbers of pages (from 1) for MCP
    static QJsonArray getPageNumbers(PDFInteger first, qint64 count)
    {
        QJsonArray numbers;
        for (qint64 i = 0; i < count; ++i)
        {
            numbers.append(qint64(first + i + 1));
        }
        return numbers;
    }
};

// ------------------------------------------------------- PDFDocumentToolInsertBase

std::optional<PDFDocumentToolInsertBase::Placement> PDFDocumentToolInsertBase::resolvePlacement(const PDFDocument* target, const QVariantMap& parameters, QString* errorMessage) const
{
    using Helper = PDFDocumentToolInsertHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();
    if (!target)
    {
        error = tr("There is no document.");
        return std::nullopt;
    }

    const PDFInteger pageCount = PDFInteger(target->getCatalog()->getPageCount());
    Placement placement;
    placement.positionId = parameters.value(Helper::name(PARAMETER_POSITION)).toString();
    placement.pageNumber = parameters.value(Helper::name(PARAMETER_PAGE)).toLongLong();

    const bool isBeforeOrAfter = placement.positionId == QLatin1String(POSITION_BEFORE) || placement.positionId == QLatin1String(POSITION_AFTER);
    if (isBeforeOrAfter && (placement.pageNumber < 1 || placement.pageNumber > pageCount))
    {
        error = tr("Page %1 does not exist – the document has %Ln pages.", nullptr, int(pageCount)).arg(placement.pageNumber);
        return std::nullopt;
    }

    if (placement.positionId == QLatin1String(POSITION_BEFORE))
    {
        placement.position = placement.pageNumber - 1;
        placement.neighborPage = placement.pageNumber - 1;
    }
    else if (placement.positionId == QLatin1String(POSITION_AFTER))
    {
        placement.position = placement.pageNumber;
        placement.neighborPage = placement.pageNumber - 1;
    }
    else if (placement.positionId == QLatin1String(POSITION_START))
    {
        placement.position = 0;
        placement.neighborPage = 0;
    }
    else if (placement.positionId == QLatin1String(POSITION_END))
    {
        placement.position = pageCount;
        placement.neighborPage = qMax<PDFInteger>(0, pageCount - 1);
    }
    else if (placement.positionId == QLatin1String(POSITION_REPLACE))
    {
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(parameters.value(Helper::name(PDFDocumentToolInsertPages::PARAMETER_REPLACE_PAGES)), &error);
        if (!selection)
        {
            return std::nullopt;
        }
        placement.replacedPages = selection->isNone ? std::vector<PDFInteger>() : selection->getPageIndices(target, &error);
        if (!error.isEmpty())
        {
            return std::nullopt;
        }
        if (placement.replacedPages.empty())
        {
            error = tr("Select the pages to replace in Thumbnails.");
            return std::nullopt;
        }
        if (PDFInteger(placement.replacedPages.size()) >= pageCount)
        {
            error = tr("Every page of the document would be replaced. Insert the pages at the beginning and remove the old pages with Remove Pages.");
            return std::nullopt;
        }
        std::sort(placement.replacedPages.begin(), placement.replacedPages.end());
        placement.position = placement.replacedPages.front();
        placement.neighborPage = placement.replacedPages.front();
    }
    else
    {
        error = tr("Unknown place of the insertion.");
        return std::nullopt;
    }
    return placement;
}

std::optional<std::vector<PDFDocumentToolInsertBase::SourceInput>> PDFDocumentToolInsertBase::resolveSources(const PDFDocumentToolContext& context, const QVariantMap& parameters, int* blankIndex, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();
    *blankIndex = -1;

    const size_t sourceCount = (context.documents.empty() ? 0 : context.documents.size() - 1) + context.images.size();
    if (sourceCount > size_t(MAXIMUM_SOURCE_COUNT))
    {
        error = tr("At most %Ln files can be inserted at once.", nullptr, MAXIMUM_SOURCE_COUNT);
        return std::nullopt;
    }

    // The order of Merge over the context without the target; "blank" is the place of the blank pages
    QStringList order;
    int sourceIdCount = 0;
    for (const QString& part : parameters.value(PDFDocumentToolInsertHelper::name(PARAMETER_INPUTS)).toString().split(QLatin1Char(',')))
    {
        const QString id = part.trimmed();
        if (id.isEmpty())
        {
            continue;
        }
        if (id == QLatin1String(BLANK_INPUT_ID))
        {
            if (*blankIndex >= 0)
            {
                error = tr("The input “%1” is in the order of the inputs more than once.").arg(id);
                return std::nullopt;
            }
            *blankIndex = sourceIdCount;
            continue;
        }
        order << id;
        ++sourceIdCount;
    }

    PDFDocumentToolContext sourceContext = context;
    if (!sourceContext.documents.empty())
    {
        sourceContext.documents.erase(sourceContext.documents.begin());
    }
    QVariantMap sourceParameters;
    sourceParameters.insert(QString::fromLatin1(PDFDocumentToolMerge::PARAMETER_INPUTS), order.join(QLatin1Char(',')));
    const std::optional<std::vector<PDFDocumentToolMerge::Input>> inputs = PDFDocumentToolMerge::resolveInputs(sourceContext, sourceParameters, &error);
    if (!inputs)
    {
        return std::nullopt;
    }

    std::vector<SourceInput> sources;
    sources.reserve(inputs->size());
    for (const PDFDocumentToolMerge::Input& input : *inputs)
    {
        SourceInput source;
        source.isImage = input.isImage;
        source.index = input.isImage ? input.index : input.index + 1;
        source.inputId = input.inputId;
        source.name = input.name;
        source.pageCount = input.pageCount;
        sources.push_back(std::move(source));
    }
    if (*blankIndex > int(sources.size()))
    {
        *blankIndex = int(sources.size());
    }
    return sources;
}

PDFImagePageLayout PDFDocumentToolInsertBase::createImageLayout(QSizeF pageSize, double marginsMillimeters, PDFImagePageLayout::Scaling scaling, Qt::Alignment anchor, PageRotation turn)
{
    const double margins = PDFPageSizePresets::getPoints(marginsMillimeters);
    PDFImagePageLayout layout;
    layout.pageSize = pageSize;
    layout.orientation = PDFImagePageLayout::Orientation::Auto;
    layout.margins = QMarginsF(margins, margins, margins, margins);
    layout.scaling = scaling;
    layout.anchor = anchor;
    layout.turn = turn;
    return layout;
}

QSizeF PDFDocumentToolInsertBase::getShownPageSize(const PDFDocument* document, PDFInteger pageIndex)
{
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    if (!catalog || pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        return QSizeF();
    }
    return catalog->getPage(size_t(pageIndex))->getRotatedMediaBox().size();
}

QString PDFDocumentToolInsertBase::getPlacementText(const Placement& placement)
{
    if (placement.positionId == QLatin1String(POSITION_BEFORE))
    {
        return tr("before page %1").arg(placement.pageNumber);
    }
    if (placement.positionId == QLatin1String(POSITION_AFTER))
    {
        return tr("after page %1").arg(placement.pageNumber);
    }
    if (placement.positionId == QLatin1String(POSITION_START))
    {
        return tr("at the beginning");
    }
    if (placement.positionId == QLatin1String(POSITION_END))
    {
        return tr("at the end");
    }
    return tr("in place of pages %1").arg(PDFDocumentPageOperations::formatPageNumbers(placement.replacedPages));
}

PDFDocumentToolInputCountRange PDFDocumentToolInsertBase::getInputCountRange() const
{
    // The target and its sources (Insert of blank pages only has the target alone)
    return PDFDocumentToolInputCountRange{ 1, MAXIMUM_SOURCE_COUNT + 1 };
}

void PDFDocumentToolInsertBase::addPlacementParameters(PDFDocumentToolParameterDefinitions* definitions, bool hasReplace)
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolInsertHelper;

    std::vector<PDFDocumentToolParameterOption> options = {
        { QString::fromLatin1(POSITION_BEFORE), QT_TR_NOOP("Before page"), QT_TR_NOOP("The pages go before the page of “page”") },
        { QString::fromLatin1(POSITION_AFTER), QT_TR_NOOP("After page"), QT_TR_NOOP("The pages go after the page of “page”") },
        { QString::fromLatin1(POSITION_START), QT_TR_NOOP("At the beginning"), QT_TR_NOOP("The pages go before the first page") },
        { QString::fromLatin1(POSITION_END), QT_TR_NOOP("At the end"), QT_TR_NOOP("The pages go after the last page") },
    };
    if (hasReplace)
    {
        options.push_back({ QString::fromLatin1(POSITION_REPLACE), QT_TR_NOOP("Replace selected pages"), QT_TR_NOOP("The pages of “replacePages” are removed, the new pages take their place") });
    }
    definitions->push_back(Definition::createEnum(tr, Helper::name(PARAMETER_POSITION), QT_TR_NOOP("Position"), QT_TR_NOOP("Where the new pages go in the document"),
                                                  std::move(options), QString::fromLatin1(POSITION_END)).setStored(false));
    definitions->push_back(Definition::createInteger(tr, Helper::name(PARAMETER_PAGE), QT_TR_NOOP("Page"), QT_TR_NOOP("The page (from 1) of the positions before and after"),
                                                     1, 1, PDFDocumentToolPageSelection::MAXIMUM_PAGE_NUMBER).setStored(false));
}

void PDFDocumentToolInsertBase::addInputsParameter(PDFDocumentToolParameterDefinitions* definitions)
{
    using Definition = PDFDocumentToolParameterDefinition;
    definitions->push_back(Definition::createString(tr, PDFDocumentToolInsertHelper::name(PARAMETER_INPUTS), QT_TR_NOOP("Order of the sources"),
                                                    QT_TR_NOOP("Input ids of the sources separated by commas; empty = the documents, then the images"),
                                                    QString(), PDFDocumentToolMerge::MAXIMUM_INPUTS_LENGTH).setPanelOnly(true).setStored(false));
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolInsertBase::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolInsertHelper;

    // The target needs Assemble (its catalog entry, Q4), each source the rule Q2 of step 2: its
    // pages lose its restrictions in the document of the tab, which has its own encryption
    std::vector<PDFDocumentToolParameterError> errors;
    if (context.documents.empty())
    {
        errors.push_back({ QString(), tr("The tool needs the document of the tab.") });
        return errors;
    }
    for (const PDFDocumentToolInputDocument& document : context.documents)
    {
        if (!document.document)
        {
            errors.push_back({ QString(), tr("An input document is missing.") });
            return errors;
        }
    }
    const PDFDocumentToolCatalogEntry* info = getInfo();
    if (info && !info->isPermitted(context.documents.front().document.data()))
    {
        errors.push_back({ QString(), tr("The permissions of the document do not allow this tool.") });
        return errors;
    }
    for (size_t i = 1; i < context.documents.size(); ++i)
    {
        const PDFDocumentToolInputDocument& source = context.documents[i];
        const PDFDocumentMerger::Permission permission = PDFDocumentMerger::getPermission(source.document.data(), source.isOwnerPasswordVerified);
        if (permission != PDFDocumentMerger::Permission::Permitted)
        {
            errors.push_back({ Helper::name(PARAMETER_INPUTS), PDFDocumentMerger::getPermissionText(permission, QFileInfo(source.fileName).fileName()) });
        }
    }
    if (!errors.empty())
    {
        return errors;
    }

    QString error;
    const std::optional<Placement> placement = resolvePlacement(context.getDocument(), parameters, &error);
    if (!placement)
    {
        const bool isReplace = parameters.value(Helper::name(PARAMETER_POSITION)).toString() == QLatin1String(POSITION_REPLACE);
        errors.push_back({ Helper::name(isReplace ? PDFDocumentToolInsertPages::PARAMETER_REPLACE_PAGES : PARAMETER_PAGE), error });
        return errors;
    }
    if (!createInsertionPlan(context, parameters, *placement, &error))
    {
        errors.push_back({ Helper::name(PARAMETER_INPUTS), error });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolInsertBase::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolInsertHelper;

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* target = input.document.data();

    QString error;
    const std::optional<Placement> placement = resolvePlacement(target, parameters, &error);
    if (!placement)
    {
        return PDFDocumentToolResult::createFailed(error);
    }
    const std::optional<InsertionPlan> plan = createInsertionPlan(context, parameters, *placement, &error);
    if (!plan)
    {
        return PDFDocumentToolResult::createFailed(error);
    }

    PDFDocumentMerger::Settings settings;
    settings.outline = PDFDocumentMerger::OutlineMode::NoOutline;
    PDFDocumentMerger::Result inserted = PDFDocumentMerger::insert(*target, placement->position, placement->replacedPages, plan->sources, plan->items, settings, control, progress);
    if (inserted.isCanceled || PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }
    if (!inserted.isSuccess)
    {
        return PDFDocumentToolResult::createFailed(inserted.errorMessage);
    }

    PDFDocument output = std::move(inserted.document);
    const PDFDocumentMerger::Statistics& statistics = inserted.statistics;

    // Q12, R10: a certified document allows no change of its pages - the result is a copy (the
    // rule of the callout of the panel, owner 13. 11.)
    PDFInteger certificationSignaturesRemoved = 0;
    const bool isCertified = PDFDocMDPPermissions::isCertificationRemovedBy(target, getOutputTraits(parameters));
    if (isCertified)
    {
        output = PDFDocMDPPermissions::removeCertification(output, &certificationSignaturesRemoved);
    }

    const qint64 pageCountBefore = qint64(target->getCatalog()->getPageCount());
    const qint64 pageCount = qint64(output.getCatalog()->getPageCount());
    const PDFInteger firstInsertedPage = placement->position - PDFInteger(std::count_if(placement->replacedPages.cbegin(), placement->replacedPages.cend(),
                                                                                         [&placement](PDFInteger page) { return page < placement->position; }));

    PDFDocumentToolResult result;
    result.facts.push_back(createInsertedFact(*plan, statistics.insertedPageCount, *placement));
    result.facts.push_back({ QStringLiteral("document"), tr("Document"), tr("%Ln pages (was %1)", nullptr, int(pageCount)).arg(pageCountBefore) });
    if (statistics.replacedPageCount > 0)
    {
        result.facts.push_back({ QStringLiteral("replaced"), tr("Replaced"), tr("%Ln pages · %1", nullptr, int(statistics.replacedPageCount)).arg(PDFDocumentPageOperations::formatPageNumbers(placement->replacedPages)) });
    }
    if (statistics.fieldsRenamed > 0)
    {
        result.facts.push_back({ QStringLiteral("fieldsRenamed"), tr("Form fields"), tr("%Ln renamed (the names were used in the document)", nullptr, int(statistics.fieldsRenamed)) });
    }
    if (statistics.signaturesRemoved > 0)
    {
        result.facts.push_back({ QStringLiteral("signatures"), tr("Signatures"), tr("%Ln of the inserted pages removed (a signature of another file is not valid here)", nullptr, int(statistics.signaturesRemoved)) });
    }
    if (statistics.insertedPagesUntagged)
    {
        result.facts.push_back({ QStringLiteral("tags"), tr("Tags"), tr("Inserted pages are not tagged") });
    }
    if (statistics.replacement.structureTreeRemoved)
    {
        result.facts.push_back({ QStringLiteral("tagsRemoved"), tr("Tags"), tr("Removed: the structure of the whole document goes with the replaced pages") });
    }
    // Q18: the declarations stay, the inserted pages are not checked
    const QStringList declarations = getConformanceDeclarations(target);
    if (!declarations.isEmpty())
    {
        result.facts.push_back({ QStringLiteral("conformance"), tr("Conformance"), tr("Inserted pages were not checked for %1").arg(declarations.join(QStringLiteral(", "))) });
    }
    if (isCertified)
    {
        result.facts.push_back({ QStringLiteral("certification"), tr("Certification"), tr("Removed: a certified document allows no change of its pages, save the result as a copy") });
    }

    for (const PDFDocumentMerger::LeftOut& leftOut : inserted.leftOut)
    {
        const int inputIndex = leftOut.source < plan->inputOfSource.size() ? plan->inputOfSource[leftOut.source] : -1;
        PDFDocumentToolLeftOutInput leftOutInput;
        leftOutInput.inputIndex = inputIndex;
        if (inputIndex >= 0)
        {
            leftOutInput.inputId = plan->inputs[size_t(inputIndex)].inputId;
            leftOutInput.name = plan->inputs[size_t(inputIndex)].name;
        }
        leftOutInput.pageCount = leftOut.pageCount;
        leftOutInput.reason = leftOut.reason;
        result.leftOutInputs.push_back(std::move(leftOutInput));
    }
    if (!result.leftOutInputs.empty())
    {
        result.facts.push_back({ QStringLiteral("leftOut"), tr("Not inserted"), tr("%Ln files could not be read", nullptr, int(result.leftOutInputs.size())) });
    }

    result.data.insert(QStringLiteral("insertedCount"), statistics.insertedPageCount);
    result.data.insert(QStringLiteral("insertedPages"), Helper::getPageNumbers(firstInsertedPage, statistics.insertedPageCount));
    result.data.insert(QStringLiteral("pageCount"), pageCount);
    result.data.insert(QStringLiteral("pageCountBefore"), pageCountBefore);
    QJsonArray replaced;
    for (const PDFInteger page : placement->replacedPages)
    {
        replaced.append(qint64(page + 1));
    }
    result.data.insert(QStringLiteral("replacedPages"), replaced);
    result.data.insert(QStringLiteral("imageCount"), plan->imageCount);
    result.data.insert(QStringLiteral("fieldsRenamed"), statistics.fieldsRenamed);
    result.data.insert(QStringLiteral("destinationsRenamed"), statistics.destinationsRenamed);
    result.data.insert(QStringLiteral("signaturesRemoved"), statistics.signaturesRemoved);
    result.data.insert(QStringLiteral("insertedPagesUntagged"), statistics.insertedPagesUntagged);
    result.data.insert(QStringLiteral("structureTreeJoined"), statistics.structureTreeMerged);
    result.data.insert(QStringLiteral("pageTreeFlattened"), statistics.isPageTreeFlattened);
    result.data.insert(QStringLiteral("declarationsKept"), QJsonArray::fromStringList(declarations));

    PDFDocumentToolOutputDocument document;
    document.kind = PDFDocumentToolOutputDocument::Kind::Changed;
    document.inputIndex = 0;
    document.document = PDFDocumentPointer(new PDFDocument(std::move(output)));
    document.flags = PDFModifiedDocument::Reset;
    const QString baseName = QFileInfo(input.fileName).completeBaseName();
    document.suggestedFileName = (baseName.isEmpty() ? QStringLiteral("document") : baseName) + getFileNameSuffix() + QStringLiteral(".pdf");
    result.documents.push_back(std::move(document));

    // A signed document is saved by an incremental update (the object numbers stay, Q3 a); a
    // certified one only as a copy (Q12)
    result.data.insert(QStringLiteral("signatureCount"), PDFForm::getSignatureCount(target));
    result.data.insert(QStringLiteral("signatureImpact"), QStringLiteral("incremental-save"));
    if (isCertified)
    {
        PDFDocMDPPermissions::addCertificationRemoved(&result, certificationSignaturesRemoved);
    }
    return result;
}

// ------------------------------------------------------- PDFDocumentToolInsertPages

std::optional<std::vector<std::vector<PDFInteger>>> PDFDocumentToolInsertPages::parseSourcePages(const QString& text, const std::vector<SourceInput>& inputs, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();
    if (text.size() > MAXIMUM_SOURCE_PAGES_LENGTH)
    {
        error = tr("The pages of the sources are too long.");
        return std::nullopt;
    }

    const QStringList parts = text.split(QLatin1Char(';'));
    if (!text.trimmed().isEmpty() && parts.size() > qsizetype(inputs.size()))
    {
        error = tr("The pages are given for %Ln sources, the insertion has %1.", nullptr, int(parts.size())).arg(inputs.size());
        return std::nullopt;
    }

    std::vector<std::vector<PDFInteger>> ranges;
    ranges.reserve(inputs.size());
    PDFInteger total = 0;
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        const QString part = i < size_t(parts.size()) ? parts[qsizetype(i)].trimmed() : QString();
        std::vector<PDFInteger> pages;
        if (part.isEmpty() || part == QLatin1String("all"))
        {
            for (PDFInteger page = 0; page < inputs[i].pageCount; ++page)
            {
                pages.push_back(page);
            }
        }
        else
        {
            QString rangeError;
            const std::optional<PDFPageRangeList> list = PDFPageRangeList::parse(part, inputs[i].pageCount, &rangeError);
            if (!list)
            {
                error = tr("%1: %2").arg(inputs[i].name, rangeError);
                return std::nullopt;
            }
            pages = list->getPageIndices();
        }
        total += PDFInteger(pages.size());
        if (total > PDFDocumentMerger::MAXIMUM_PAGE_COUNT)
        {
            error = tr("At most %Ln pages can be inserted at once.", nullptr, int(PDFDocumentMerger::MAXIMUM_PAGE_COUNT));
            return std::nullopt;
        }
        ranges.push_back(std::move(pages));
    }
    return ranges;
}

QSizeF PDFDocumentToolInsertPages::getOrientedSize(QSizeF shownSize, const QVariantMap& parameters)
{
    using Helper = PDFDocumentToolInsertHelper;

    const bool isLandscape = parameters.value(Helper::name(PARAMETER_BLANK_ORIENTATION)).toString() == QLatin1String(ORIENTATION_LANDSCAPE);
    const bool isShownLandscape = shownSize.width() > shownSize.height();
    if (shownSize.width() != shownSize.height() && isShownLandscape != isLandscape)
    {
        shownSize.transpose();
    }
    return shownSize;
}

QString PDFDocumentToolInsertPages::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolInsertPages::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolInsertHelper;

    PDFDocumentToolParameterDefinitions definitions;
    addPlacementParameters(&definitions, true);
    PDFDocumentToolPageSelection none;
    none.isNone = true;
    definitions.push_back(Definition::createPageSelection(tr, Helper::name(PARAMETER_REPLACE_PAGES), QT_TR_NOOP("Replaced pages"),
                                                          QT_TR_NOOP("The pages, which the new pages replace (position replace)"), none).setStored(false));
    addInputsParameter(&definitions);
    definitions.push_back(Definition::createString(tr, Helper::name(PARAMETER_SOURCE_PAGES), QT_TR_NOOP("Pages of the sources"),
                                                   QT_TR_NOOP("The pages of each source in its order, the sources separated by semicolons: \"1-5, 7; all; odd\" (empty or all = every page; a range in either direction, pages may repeat)"),
                                                   QString(), MAXIMUM_SOURCE_PAGES_LENGTH).setStored(false));
    definitions.push_back(Definition::createInteger(tr, Helper::name(PARAMETER_BLANK_PAGES), QT_TR_NOOP("Blank pages"), QT_TR_NOOP("Number of blank pages inserted after the sources"),
                                                    0, 0, MAXIMUM_BLANK_PAGES).setStored(false));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_BLANK_SIZE), QT_TR_NOOP("Size"), QT_TR_NOOP("Size of the blank pages"),
                                                 {
                                                     { QString::fromLatin1(SIZE_SAME_AS_PAGE), QT_TR_NOOP("As the neighboring page"), QT_TR_NOOP("The size and the rotation of the page next to the place") },
                                                     { QString::fromLatin1(PDFPageSizePresets::A4), QT_TR_NOOP("A4"), QT_TR_NOOP("210 × 297 mm") },
                                                     { QString::fromLatin1(PDFPageSizePresets::A3), QT_TR_NOOP("A3"), QT_TR_NOOP("297 × 420 mm") },
                                                     { QString::fromLatin1(PDFPageSizePresets::A5), QT_TR_NOOP("A5"), QT_TR_NOOP("148 × 210 mm") },
                                                     { QString::fromLatin1(PDFPageSizePresets::LETTER), QT_TR_NOOP("Letter"), QT_TR_NOOP("216 × 279 mm") },
                                                     { QString::fromLatin1(PDFPageSizePresets::LEGAL), QT_TR_NOOP("Legal"), QT_TR_NOOP("216 × 356 mm") },
                                                 },
                                                 QString::fromLatin1(SIZE_SAME_AS_PAGE)));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_BLANK_ORIENTATION), QT_TR_NOOP("Orientation"), QT_TR_NOOP("Orientation of the blank pages, also of the size of the neighboring page (not given: that size keeps the orientation of the page)"),
                                                 {
                                                     { QString::fromLatin1(ORIENTATION_PORTRAIT), QT_TR_NOOP("Portrait"), QT_TR_NOOP("Higher than wide") },
                                                     { QString::fromLatin1(ORIENTATION_LANDSCAPE), QT_TR_NOOP("Landscape"), QT_TR_NOOP("Wider than high") },
                                                 },
                                                 QString::fromLatin1(ORIENTATION_PORTRAIT)));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolInsertPages::checkGivenParameters(const QVariantMap& given) const
{
    using Helper = PDFDocumentToolInsertHelper;

    // A value, which would be silently ignored, is an error, which says what to set
    std::vector<PDFDocumentToolParameterError> errors;
    const QString position = given.value(Helper::name(PARAMETER_POSITION), QString::fromLatin1(POSITION_END)).toString();
    const bool isBeforeOrAfter = position == QLatin1String(POSITION_BEFORE) || position == QLatin1String(POSITION_AFTER);
    if (isBeforeOrAfter && !given.contains(Helper::name(PARAMETER_PAGE)))
    {
        errors.push_back({ Helper::name(PARAMETER_PAGE), tr("Set page (from 1) with position %1.").arg(position) });
    }
    if (!isBeforeOrAfter && given.contains(Helper::name(PARAMETER_PAGE)))
    {
        errors.push_back({ Helper::name(PARAMETER_PAGE), tr("page is used only with position before or after.") });
    }
    const bool isReplace = position == QLatin1String(POSITION_REPLACE);
    if (isReplace != given.contains(Helper::name(PARAMETER_REPLACE_PAGES)))
    {
        errors.push_back({ Helper::name(PARAMETER_REPLACE_PAGES), isReplace ? tr("Set replacePages with position replace.") : tr("replacePages is used only with position replace.") });
    }
    const bool hasBlankPages = given.value(Helper::name(PARAMETER_BLANK_PAGES)).toLongLong() > 0;
    for (const char* parameter : { PARAMETER_BLANK_SIZE, PARAMETER_BLANK_ORIENTATION })
    {
        if (!hasBlankPages && given.contains(Helper::name(parameter)))
        {
            errors.push_back({ Helper::name(parameter), tr("%1 is used only with blankPages.").arg(Helper::name(parameter)) });
        }
    }
    return errors;
}

QVariantMap PDFDocumentToolInsertPages::completeGivenParameters(const PDFDocumentToolContext& context, const QVariantMap& given) const
{
    using Helper = PDFDocumentToolInsertHelper;

    QVariantMap completed = PDFDocumentToolInsertBase::completeGivenParameters(context, given);
    if (completed.contains(Helper::name(PARAMETER_BLANK_ORIENTATION)))
    {
        return completed;
    }

    // The values as the run will see them (the defaults of the missing ones): blank pages of the
    // size of the neighboring page
    QVariantMap normalized;
    if (!PDFDocumentToolParameters::normalize(getParameters(), completed, &normalized).empty() ||
        normalized.value(Helper::name(PARAMETER_BLANK_PAGES)).toLongLong() <= 0 ||
        normalized.value(Helper::name(PARAMETER_BLANK_SIZE)).toString() != QLatin1String(SIZE_SAME_AS_PAGE))
    {
        return completed;
    }

    const PDFDocument* target = context.getDocument();
    const std::optional<Placement> placement = resolvePlacement(target, normalized, nullptr);
    if (!placement)
    {
        return completed;
    }

    // The page as it is shown (its rotation applied); a square page or a page of no size keeps
    // the default, which leaves it as it is
    const QSizeF shownSize = getShownPageSize(target, placement->neighborPage);
    const bool isLandscape = shownSize.width() > shownSize.height();
    completed.insert(Helper::name(PARAMETER_BLANK_ORIENTATION), QString::fromLatin1(isLandscape ? ORIENTATION_LANDSCAPE : ORIENTATION_PORTRAIT));
    return completed;
}

QVariantMap PDFDocumentToolInsertPages::mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const
{
    using Helper = PDFDocumentToolInsertHelper;

    if (!from || !to || from == to)
    {
        return parameters;
    }

    // The page of the place and the replaced pages follow their page objects
    const std::vector<PDFInteger> pageMap = PDFDocumentToolPageSelection::mapPageIndices(from, to);
    QVariantMap mapped = parameters;
    const PDFInteger page = parameters.value(Helper::name(PARAMETER_PAGE)).toLongLong() - 1;
    if (page >= 0 && size_t(page) < pageMap.size() && pageMap[size_t(page)] >= 0)
    {
        mapped.insert(Helper::name(PARAMETER_PAGE), qint64(pageMap[size_t(page)] + 1));
    }

    const QString key = Helper::name(PARAMETER_REPLACE_PAGES);
    QString error;
    const std::optional<PDFDocumentToolPageSelection> selection = parameters.contains(key) ? PDFDocumentToolPageSelection::fromVariant(parameters.value(key), &error) : std::nullopt;
    if (selection)
    {
        mapped.insert(key, selection->mapPages(from, pageMap).toVariant());
    }
    return mapped;
}

std::optional<PDFDocumentToolInsertBase::InsertionPlan> PDFDocumentToolInsertPages::createInsertionPlan(const PDFDocumentToolContext& context, const QVariantMap& parameters, const Placement& placement, QString* errorMessage) const
{
    using Helper = PDFDocumentToolInsertHelper;

    int blankIndex = -1;
    const std::optional<std::vector<SourceInput>> inputs = resolveSources(context, parameters, &blankIndex, errorMessage);
    if (!inputs)
    {
        return std::nullopt;
    }
    const std::optional<std::vector<std::vector<PDFInteger>>> ranges = parseSourcePages(parameters.value(Helper::name(PARAMETER_SOURCE_PAGES)).toString(), *inputs, errorMessage);
    if (!ranges)
    {
        return std::nullopt;
    }

    const PDFDocument* target = context.getDocument();
    const qint64 blankCount = parameters.value(Helper::name(PARAMETER_BLANK_PAGES)).toLongLong();
    if (blankIndex < 0)
    {
        blankIndex = int(inputs->size());
    }

    // K35: an image source has the defaults of Add Image - the size of the neighboring page, the
    // whole image fit in 10 mm margins in the middle, the orientation of the image
    const PDFImagePageLayout imageLayout = createImageLayout(getShownPageSize(target, placement.neighborPage), DEFAULT_MARGINS_MILLIMETERS,
                                                             PDFImagePageLayout::Scaling::Fit, Qt::AlignCenter, PageRotation::None);

    InsertionPlan plan;
    plan.inputs = *inputs;
    for (int i = 0; i <= int(inputs->size()); ++i)
    {
        if (i == blankIndex && blankCount > 0)
        {
            // 388:16811 "As page 12": the media box and the rotation of the neighboring page, the
            // orientation turns its size as it is shown (owner 2026-11-09: A4 portrait + Landscape
            // = A4 landscape; the rotation stays, the media box is transposed)
            PDFDocumentMerger::Item item;
            item.source = plan.sources.size();
            const QString sizeId = parameters.value(Helper::name(PARAMETER_BLANK_SIZE)).toString();
            if (sizeId == QLatin1String(SIZE_SAME_AS_PAGE))
            {
                const PDFPage* neighbor = target->getCatalog()->getPage(size_t(placement.neighborPage));
                item.pageSize = neighbor ? neighbor->getMediaBox().size() : QSizeF();
                item.turn = neighbor ? neighbor->getPageRotation() : PageRotation::None;
                const QSizeF shownSize = PDFPage::getRotatedSize(item.pageSize, item.turn);
                if (getOrientedSize(shownSize, parameters) != shownSize)
                {
                    item.pageSize.transpose();
                }
            }
            else
            {
                item.pageSize = getOrientedSize(PDFPageSizePresets::getSize(sizeId).value_or(QSizeF()), parameters);
            }
            if (!PDFPageSizePresets::isValidSide(item.pageSize.width()) || !PDFPageSizePresets::isValidSide(item.pageSize.height()))
            {
                // A neighboring page of a size outside of the limits of a page gives A4
                item.pageSize = PDFPageSizePresets::getSize(QStringView(u"a4")).value_or(QSizeF(595.276, 841.89));
            }
            plan.sources.push_back(PDFDocumentMerger::Source::createBlank());
            plan.inputOfSource.push_back(-1);
            for (qint64 j = 0; j < blankCount; ++j)
            {
                plan.items.push_back(item);
            }
        }
        if (i == int(inputs->size()))
        {
            break;
        }

        const SourceInput& input = (*inputs)[size_t(i)];
        const std::vector<PDFInteger>& pages = (*ranges)[size_t(i)];
        if (pages.empty())
        {
            continue;
        }

        if (input.isImage)
        {
            const PDFDocumentToolInputImage& image = context.images[input.index];
            if (input.pageCount <= 0)
            {
                const QString reason = PDFImagePageFactory::readInfo(image.data, image.format).error;
                *errorMessage = tr("%1 cannot be read: %2").arg(input.name, reason);
                return std::nullopt;
            }
            plan.sources.push_back(PDFDocumentMerger::Source::createImage(image.data, image.format, image.dotsPerInch, input.name));
            ++plan.imageCount;
        }
        else
        {
            PDFDocumentMerger::Source source = PDFDocumentMerger::Source::createDocument(context.documents[input.index].document, input.name);
            source.isOwnerPasswordVerified = context.documents[input.index].isOwnerPasswordVerified;
            plan.sources.push_back(std::move(source));
        }
        plan.inputOfSource.push_back(i);

        for (const PDFInteger page : pages)
        {
            PDFDocumentMerger::Item item;
            item.source = plan.sources.size() - 1;
            item.pageIndex = page;
            if (input.isImage)
            {
                item.imageLayout = imageLayout;
            }
            plan.items.push_back(std::move(item));
        }
    }

    if (plan.items.empty())
    {
        *errorMessage = tr("There is nothing to insert. Add PDF files, images or blank pages.");
        return std::nullopt;
    }
    if (qint64(plan.items.size()) + qint64(target->getCatalog()->getPageCount()) > PDFDocumentMerger::MAXIMUM_PAGE_COUNT)
    {
        *errorMessage = tr("The document would have more than %Ln pages.", nullptr, int(PDFDocumentMerger::MAXIMUM_PAGE_COUNT));
        return std::nullopt;
    }
    return plan;
}

PDFDocumentToolFact PDFDocumentToolInsertPages::createInsertedFact(const InsertionPlan& plan, qint64 insertedCount, const Placement& placement) const
{
    Q_UNUSED(plan);
    return { QStringLiteral("inserted"), tr("Inserted"), tr("%Ln pages %1", nullptr, int(insertedCount)).arg(getPlacementText(placement)) };
}

QString PDFDocumentToolInsertPages::getFileNameSuffix() const
{
    return QStringLiteral("_inserted");
}

// ------------------------------------------------------- PDFDocumentToolAddImage

std::optional<PDFImagePageLayout::Scaling> PDFDocumentToolAddImage::getScaling(const QString& id)
{
    if (id == QLatin1String(SCALING_FIT))
    {
        return PDFImagePageLayout::Scaling::Fit;
    }
    if (id == QLatin1String(SCALING_FILL))
    {
        return PDFImagePageLayout::Scaling::Fill;
    }
    if (id == QLatin1String(SCALING_STRETCH))
    {
        return PDFImagePageLayout::Scaling::Stretch;
    }
    if (id == QLatin1String(SCALING_ORIGINAL_SIZE))
    {
        return PDFImagePageLayout::Scaling::OriginalSize;
    }
    return std::nullopt;
}

QStringList PDFDocumentToolAddImage::getAnchorIds()
{
    return { QStringLiteral("top-left"), QStringLiteral("top-center"), QStringLiteral("top-right"),
             QStringLiteral("middle-left"), QStringLiteral("middle-center"), QStringLiteral("middle-right"),
             QStringLiteral("bottom-left"), QStringLiteral("bottom-center"), QStringLiteral("bottom-right") };
}

std::optional<Qt::Alignment> PDFDocumentToolAddImage::getAnchor(const QString& id)
{
    // The anchor belongs to the page: never mirrored for right-to-left (Qt::AlignLeft, not
    // AlignLeading); the names of the anchors of Resize Pages (one table, the anchor field of the
    // GUI writes them)
    if (!getAnchorIds().contains(id))
    {
        return std::nullopt;
    }
    return PDFDocumentToolGeometry::getAnchor(id);
}

std::optional<std::vector<PageRotation>> PDFDocumentToolAddImage::parseTurns(const QString& text, size_t count, QString* errorMessage)
{
    std::vector<PageRotation> turns(count, PageRotation::None);
    const QStringList parts = text.trimmed().isEmpty() ? QStringList() : text.split(QLatin1Char(','));
    if (size_t(parts.size()) > count)
    {
        *errorMessage = tr("The turns are given for %Ln images, there are %1.", nullptr, int(parts.size())).arg(count);
        return std::nullopt;
    }
    for (qsizetype i = 0; i < parts.size(); ++i)
    {
        const QString value = parts[i].trimmed();
        if (value.isEmpty() || value == QLatin1String("0"))
        {
            continue;
        }
        if (value == QLatin1String("90"))
        {
            turns[size_t(i)] = PageRotation::Rotate90;
        }
        else if (value == QLatin1String("180"))
        {
            turns[size_t(i)] = PageRotation::Rotate180;
        }
        else if (value == QLatin1String("270"))
        {
            turns[size_t(i)] = PageRotation::Rotate270;
        }
        else
        {
            *errorMessage = tr("A turn of an image is 0, 90, 180 or 270, not “%1”.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(value));
            return std::nullopt;
        }
    }
    return turns;
}

QString PDFDocumentToolAddImage::createTurns(const std::vector<PageRotation>& turns)
{
    QStringList parts;
    bool hasTurn = false;
    for (const PageRotation turn : turns)
    {
        switch (turn)
        {
            case PageRotation::Rotate90:
                parts << QStringLiteral("90");
                hasTurn = true;
                break;
            case PageRotation::Rotate180:
                parts << QStringLiteral("180");
                hasTurn = true;
                break;
            case PageRotation::Rotate270:
                parts << QStringLiteral("270");
                hasTurn = true;
                break;
            default:
                parts << QStringLiteral("0");
                break;
        }
    }
    return hasTurn ? parts.join(QLatin1Char(',')) : QString();
}

QString PDFDocumentToolAddImage::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolAddImage::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolInsertHelper;

    PDFDocumentToolParameterDefinitions definitions;
    addPlacementParameters(&definitions, false);
    addInputsParameter(&definitions);
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_PAGE_SIZE), QT_TR_NOOP("Page"), QT_TR_NOOP("Size of the new pages; the orientation follows each image"),
                                                 {
                                                     { QString::fromLatin1(SIZE_SAME_AS_PAGE), QT_TR_NOOP("Same as the neighboring page"), QT_TR_NOOP("The size of the page next to the place") },
                                                     { QString::fromLatin1(SIZE_FIT_TO_IMAGE), QT_TR_NOOP("Fit to image"), QT_TR_NOOP("The size of the image by its resolution and the margins") },
                                                     { QString::fromLatin1(PDFPageSizePresets::A4), QT_TR_NOOP("A4"), QT_TR_NOOP("210 × 297 mm") },
                                                     { QString::fromLatin1(PDFPageSizePresets::A3), QT_TR_NOOP("A3"), QT_TR_NOOP("297 × 420 mm") },
                                                     { QString::fromLatin1(PDFPageSizePresets::LETTER), QT_TR_NOOP("Letter"), QT_TR_NOOP("216 × 279 mm") },
                                                     { QString::fromLatin1(SIZE_CUSTOM), QT_TR_NOOP("Custom"), QT_TR_NOOP("Width and height in millimeters") },
                                                 },
                                                 QString::fromLatin1(SIZE_SAME_AS_PAGE)));
    definitions.push_back(Definition::createReal(tr, Helper::name(PARAMETER_WIDTH), QT_TR_NOOP("Width"), QT_TR_NOOP("Width of a page of the custom size, in millimeters"),
                                                 210.0, PDFPageSizePresets::MINIMUM_SIDE_MILLIMETERS, PDFPageSizePresets::MAXIMUM_SIDE_MILLIMETERS));
    definitions.push_back(Definition::createReal(tr, Helper::name(PARAMETER_HEIGHT), QT_TR_NOOP("Height"), QT_TR_NOOP("Height of a page of the custom size, in millimeters"),
                                                 297.0, PDFPageSizePresets::MINIMUM_SIDE_MILLIMETERS, PDFPageSizePresets::MAXIMUM_SIDE_MILLIMETERS));
    definitions.push_back(Definition::createReal(tr, Helper::name(PARAMETER_MARGINS), QT_TR_NOOP("Margins"), QT_TR_NOOP("Margins around the image, in millimeters"),
                                                 DEFAULT_MARGINS_MILLIMETERS, 0.0, PDFPageSizePresets::MAXIMUM_SIDE_MILLIMETERS / 2.0));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_IMAGE_SCALING), QT_TR_NOOP("Image"), QT_TR_NOOP("How the image fills the page inside of the margins"),
                                                 {
                                                     { QString::fromLatin1(SCALING_FIT), QT_TR_NOOP("Fit, keep ratio"), QT_TR_NOOP("The whole image, the ratio kept") },
                                                     { QString::fromLatin1(SCALING_FILL), QT_TR_NOOP("Fill, keep ratio"), QT_TR_NOOP("The whole area, the ratio kept, the rest clipped") },
                                                     { QString::fromLatin1(SCALING_STRETCH), QT_TR_NOOP("Stretch"), QT_TR_NOOP("The whole area, the ratio not kept") },
                                                     { QString::fromLatin1(SCALING_ORIGINAL_SIZE), QT_TR_NOOP("Original size"), QT_TR_NOOP("The size by the resolution of the image") },
                                                 },
                                                 QString::fromLatin1(SCALING_FIT)));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_ANCHOR), QT_TR_NOOP("Anchor"), QT_TR_NOOP("Where the image lies inside of the margins"),
                                                 {
                                                     { QStringLiteral("top-left"), QT_TR_NOOP("Top left") },
                                                     { QStringLiteral("top-center"), QT_TR_NOOP("Top center") },
                                                     { QStringLiteral("top-right"), QT_TR_NOOP("Top right") },
                                                     { QStringLiteral("middle-left"), QT_TR_NOOP("Middle left") },
                                                     { QStringLiteral("middle-center"), QT_TR_NOOP("Middle center") },
                                                     { QStringLiteral("middle-right"), QT_TR_NOOP("Middle right") },
                                                     { QStringLiteral("bottom-left"), QT_TR_NOOP("Bottom left") },
                                                     { QStringLiteral("bottom-center"), QT_TR_NOOP("Bottom center") },
                                                     { QStringLiteral("bottom-right"), QT_TR_NOOP("Bottom right") },
                                                 },
                                                 QString::fromLatin1(ANCHOR_MIDDLE_CENTER)));
    definitions.push_back(Definition::createString(tr, Helper::name(PARAMETER_TURNS), QT_TR_NOOP("Turns"),
                                                   QT_TR_NOOP("Turns of the images clockwise in their order, separated by commas: 0, 90, 180 or 270 (empty = none)"),
                                                   QString(), 8192).setStored(false));
    return definitions;
}

PDFDocumentToolInputCountRange PDFDocumentToolAddImage::getInputCountRange() const
{
    // The target and at least one image
    return PDFDocumentToolInputCountRange{ 2, MAXIMUM_SOURCE_COUNT + 1 };
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolAddImage::checkGivenParameters(const QVariantMap& given) const
{
    using Helper = PDFDocumentToolInsertHelper;

    std::vector<PDFDocumentToolParameterError> errors;
    const QString position = given.value(Helper::name(PARAMETER_POSITION), QString::fromLatin1(POSITION_END)).toString();
    const bool isBeforeOrAfter = position == QLatin1String(POSITION_BEFORE) || position == QLatin1String(POSITION_AFTER);
    if (isBeforeOrAfter != given.contains(Helper::name(PARAMETER_PAGE)))
    {
        errors.push_back({ Helper::name(PARAMETER_PAGE), isBeforeOrAfter ? tr("Set page (from 1) with position %1.").arg(position) : tr("page is used only with position before or after.") });
    }
    const bool isCustom = given.value(Helper::name(PARAMETER_PAGE_SIZE)).toString() == QLatin1String(SIZE_CUSTOM);
    for (const char* parameter : { PARAMETER_WIDTH, PARAMETER_HEIGHT })
    {
        if (isCustom != given.contains(Helper::name(parameter)))
        {
            errors.push_back({ Helper::name(parameter), isCustom ? tr("Set width and height with pageSize custom.") : tr("%1 is used only with pageSize custom.").arg(Helper::name(parameter)) });
        }
    }
    return errors;
}

std::optional<PDFDocumentToolInsertBase::InsertionPlan> PDFDocumentToolAddImage::createInsertionPlan(const PDFDocumentToolContext& context, const QVariantMap& parameters, const Placement& placement, QString* errorMessage) const
{
    using Helper = PDFDocumentToolInsertHelper;

    if (context.documents.size() > 1)
    {
        *errorMessage = tr("Add Image as Page takes images only. Insert PDF files with Insert Pages.");
        return std::nullopt;
    }
    int blankIndex = -1;
    const std::optional<std::vector<SourceInput>> inputs = resolveSources(context, parameters, &blankIndex, errorMessage);
    if (!inputs)
    {
        return std::nullopt;
    }
    if (inputs->empty())
    {
        *errorMessage = tr("Add the images, which become pages.");
        return std::nullopt;
    }
    const std::optional<std::vector<PageRotation>> turns = parseTurns(parameters.value(Helper::name(PARAMETER_TURNS)).toString(), inputs->size(), errorMessage);
    if (!turns)
    {
        return std::nullopt;
    }

    // The page of the layout (5.8)
    const PDFDocument* target = context.getDocument();
    const QString sizeId = parameters.value(Helper::name(PARAMETER_PAGE_SIZE)).toString();
    QSizeF pageSize;
    if (sizeId == QLatin1String(SIZE_SAME_AS_PAGE))
    {
        pageSize = getShownPageSize(target, placement.neighborPage);
    }
    else if (sizeId == QLatin1String(SIZE_CUSTOM))
    {
        pageSize = QSizeF(PDFPageSizePresets::getPoints(parameters.value(Helper::name(PARAMETER_WIDTH)).toDouble()),
                          PDFPageSizePresets::getPoints(parameters.value(Helper::name(PARAMETER_HEIGHT)).toDouble()));
    }
    else if (sizeId != QLatin1String(SIZE_FIT_TO_IMAGE))
    {
        pageSize = PDFPageSizePresets::getSize(sizeId).value_or(QSizeF());
    }
    if (sizeId != QLatin1String(SIZE_FIT_TO_IMAGE) && (!PDFPageSizePresets::isValidSide(pageSize.width()) || !PDFPageSizePresets::isValidSide(pageSize.height())))
    {
        *errorMessage = tr("Each side of a page must be between 3 and 14 400 points.");
        return std::nullopt;
    }

    const double margins = parameters.value(Helper::name(PARAMETER_MARGINS)).toDouble();
    if (!pageSize.isEmpty() && 2.0 * PDFPageSizePresets::getPoints(margins) >= qMin(pageSize.width(), pageSize.height()))
    {
        *errorMessage = tr("The margins leave no room for the image: they can be %1 mm at most.")
                            .arg(PDFPageSizePresets::getMillimeters(qMin(pageSize.width(), pageSize.height()) / 2.0), 0, 'f', 0);
        return std::nullopt;
    }
    const PDFImagePageLayout::Scaling scaling = getScaling(parameters.value(Helper::name(PARAMETER_IMAGE_SCALING)).toString()).value_or(PDFImagePageLayout::Scaling::Fit);
    const Qt::Alignment anchor = getAnchor(parameters.value(Helper::name(PARAMETER_ANCHOR)).toString()).value_or(Qt::AlignCenter);

    InsertionPlan plan;
    plan.inputs = *inputs;
    for (size_t i = 0; i < inputs->size(); ++i)
    {
        const SourceInput& input = (*inputs)[i];
        const PDFDocumentToolInputImage& image = context.images[input.index];
        if (input.pageCount <= 0)
        {
            *errorMessage = tr("%1 cannot be read: %2").arg(input.name, PDFImagePageFactory::readInfo(image.data, image.format).error);
            return std::nullopt;
        }

        plan.sources.push_back(PDFDocumentMerger::Source::createImage(image.data, image.format, image.dotsPerInch, input.name));
        plan.inputOfSource.push_back(int(i));
        ++plan.imageCount;
        for (PDFInteger frame = 0; frame < input.pageCount; ++frame)
        {
            PDFDocumentMerger::Item item;
            item.source = plan.sources.size() - 1;
            item.pageIndex = frame;
            item.imageLayout = createImageLayout(pageSize, margins, scaling, anchor, (*turns)[i]);
            plan.items.push_back(std::move(item));
        }
    }
    if (qint64(plan.items.size()) + qint64(target->getCatalog()->getPageCount()) > PDFDocumentMerger::MAXIMUM_PAGE_COUNT)
    {
        *errorMessage = tr("The document would have more than %Ln pages.", nullptr, int(PDFDocumentMerger::MAXIMUM_PAGE_COUNT));
        return std::nullopt;
    }
    return plan;
}

PDFDocumentToolFact PDFDocumentToolAddImage::createInsertedFact(const InsertionPlan& plan, qint64 insertedCount, const Placement& placement) const
{
    // 388:20121 "2 images after page 12"; a multi-page image gives more pages than images
    // Both numbers of a multi-page image are counts, each one has its own %Ln
    const QString images = tr("%Ln images", nullptr, int(plan.imageCount));
    const QString text = plan.imageCount == insertedCount ? tr("%Ln images %1", nullptr, int(plan.imageCount)).arg(getPlacementText(placement))
                                                          : tr("%Ln pages of %1 %2", nullptr, int(insertedCount)).arg(images, getPlacementText(placement));
    return { QStringLiteral("added"), tr("Added"), text };
}

QString PDFDocumentToolAddImage::getFileNameSuffix() const
{
    return QStringLiteral("_images_added");
}

}   // namespace pdf
