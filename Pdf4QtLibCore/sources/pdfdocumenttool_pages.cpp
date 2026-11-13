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

#include "pdfdocumenttool_pages.h"

#include "pdfcatalog.h"
#include "pdfdocmdppermissions.h"
#include "pdfdocumentwriter.h"
#include "pdfexception.h"
#include "pdfform.h"
#include "pdfoperationcontrol.h"

#include <QFileInfo>
#include <QJsonArray>

#include <set>

namespace pdf
{

/// Helpers of the tools of the pages
class PDFDocumentToolPagesHelper
{
public:
    PDFDocumentToolPagesHelper() = delete;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// The visible sizes of the pages (the orientation filters)
    static std::vector<QSizeF> getPageSizes(const PDFDocument* document)
    {
        std::vector<QSizeF> sizes;
        const PDFCatalog* catalog = document->getCatalog();
        sizes.reserve(catalog->getPageCount());
        for (size_t i = 0; i < catalog->getPageCount(); ++i)
        {
            sizes.push_back(catalog->getPage(i)->getRotatedCropBox().size());
        }
        return sizes;
    }

    /// Numbers of the pages (from 1) for MCP
    static QJsonArray getPageNumbers(const std::vector<PDFInteger>& pages)
    {
        QJsonArray numbers;
        for (const PDFInteger page : pages)
        {
            numbers.append(qint64(page + 1));
        }
        return numbers;
    }

    /// The changed document of the result with its suggested name
    static void addDocument(PDFDocumentToolResult* result, const PDFDocumentToolInputDocument& input, PDFDocument document, const QString& suffix)
    {
        PDFDocumentToolOutputDocument output;
        output.kind = PDFDocumentToolOutputDocument::Kind::Changed;
        output.inputIndex = 0;
        output.document = PDFDocumentPointer(new PDFDocument(std::move(document)));
        output.flags = PDFModifiedDocument::Reset;
        const QString baseName = QFileInfo(input.fileName).completeBaseName();
        output.suggestedFileName = (baseName.isEmpty() ? QStringLiteral("document") : baseName) + suffix + QStringLiteral(".pdf");
        result->documents.push_back(std::move(output));

        // A signed document is saved by an incremental update (chapter 4 of the plan), as the
        // tools, which rewrite the file, say in their data
        result->data.insert(QStringLiteral("signatureCount"), PDFForm::getSignatureCount(input.document.data()));
        result->data.insert(QStringLiteral("signatureImpact"), QStringLiteral("incremental-save"));
    }

    static constexpr const char* ROTATION_MIXED = "mixed";

    /// The page selection of the parameter follows the page objects (PDFDocumentToolPageSelection::mapPages)
    static void mapSelection(QVariantMap* parameters, const char* parameter, const PDFDocument* from, const std::vector<PDFInteger>& pageMap)
    {
        const QString key = name(parameter);
        if (!parameters->contains(key))
        {
            return;
        }

        QString error;
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(parameters->value(key), &error);
        if (selection)
        {
            parameters->insert(key, selection->mapPages(from, pageMap).toVariant());
        }
    }
};

// ------------------------------------------------------- PDFDocumentToolRotatePages

std::optional<PDFDocumentToolRotatePages::Turn> PDFDocumentToolRotatePages::getTurn(const QString& rotation)
{
    if (rotation == QLatin1String(ROTATION_LEFT))
    {
        return Turn{ PageRotation::Rotate270, false };
    }
    if (rotation == QLatin1String(ROTATION_RIGHT))
    {
        return Turn{ PageRotation::Rotate90, false };
    }
    if (rotation == QLatin1String(ROTATION_HALF))
    {
        return Turn{ PageRotation::Rotate180, false };
    }
    if (rotation == QLatin1String(ROTATION_RESET))
    {
        return Turn{ PageRotation::None, true };
    }
    return std::nullopt;
}

QString PDFDocumentToolRotatePages::getTurnName(PageRotation turn)
{
    switch (turn)
    {
        case PageRotation::Rotate90:
            return QString::fromLatin1(ROTATION_RIGHT);
        case PageRotation::Rotate180:
            return QString::fromLatin1(ROTATION_HALF);
        case PageRotation::Rotate270:
            return QString::fromLatin1(ROTATION_LEFT);
        default:
            return QString();
    }
}

QString PDFDocumentToolRotatePages::getTurnText(PageRotation turn)
{
    switch (turn)
    {
        case PageRotation::Rotate90:
            return tr("90° right");
        case PageRotation::Rotate180:
            return tr("180°");
        case PageRotation::Rotate270:
            return tr("90° left");
        default:
            return QString();
    }
}

std::optional<std::map<PDFInteger, PDFDocumentToolRotatePages::Turn>> PDFDocumentToolRotatePages::parsePlan(const QString& plan, const PDFDocument* document, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (!document)
    {
        error = tr("There is no document.");
        return std::nullopt;
    }
    if (plan.size() > MAXIMUM_PLAN_LENGTH)
    {
        error = tr("The plan of the rotations is longer than %1 characters.").arg(MAXIMUM_PLAN_LENGTH);
        return std::nullopt;
    }

    const std::vector<QSizeF> sizes = PDFDocumentToolPagesHelper::getPageSizes(document);
    std::map<PDFInteger, Turn> turns;
    for (const QString& group : plan.split(QLatin1Char(';')))
    {
        const QString trimmed = group.trimmed();
        if (trimmed.isEmpty())
        {
            continue;
        }

        const qsizetype separator = trimmed.lastIndexOf(QLatin1Char(':'));
        const QString rotationText = separator >= 0 ? trimmed.mid(separator + 1).trimmed() : QString();
        const std::optional<Turn> turn = getTurn(rotationText);
        if (separator <= 0 || !turn)
        {
            error = tr("“%1” is not a part of a plan of the rotations. Write the pages and the rotation, such as 9,10,15:right; 20:half (left, right, half, reset).")
                        .arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(trimmed));
            return std::nullopt;
        }

        PDFDocumentToolPageSelection selection;
        selection.pages = trimmed.left(separator).trimmed();
        if (selection.pages.isEmpty())
        {
            error = tr("A part of the plan of the rotations has no pages.");
            return std::nullopt;
        }

        QString pagesError;
        const std::vector<PDFInteger> pages = selection.getPageIndices(sizes, &pagesError);
        if (pages.empty())
        {
            error = pagesError;
            return std::nullopt;
        }

        for (const PDFInteger page : pages)
        {
            if (!turns.emplace(page, *turn).second)
            {
                error = tr("The page %1 is in the plan of the rotations more than once.").arg(page + 1);
                return std::nullopt;
            }
        }
    }

    if (turns.empty())
    {
        error = tr("The plan of the rotations has no pages.");
        return std::nullopt;
    }
    return turns;
}

QString PDFDocumentToolRotatePages::createPlan(const std::map<PDFInteger, PageRotation>& turns)
{
    std::map<PDFInteger, Turn> planTurns;
    for (const auto& [page, turn] : turns)
    {
        planTurns.emplace(page, Turn{ turn, false });
    }
    return createPlan(planTurns);
}

QString PDFDocumentToolRotatePages::createPlan(const std::map<PDFInteger, Turn>& turns)
{
    // One group per rotation (the order right, left, half, reset), a long group is split
    QStringList groups;
    for (const Turn groupTurn : { Turn{ PageRotation::Rotate90, false }, Turn{ PageRotation::Rotate270, false }, Turn{ PageRotation::Rotate180, false }, Turn{ PageRotation::None, true } })
    {
        std::vector<PDFInteger> pages;
        for (const auto& [page, pageTurn] : turns)
        {
            const bool isGroup = groupTurn.isReset ? pageTurn.isReset : (!pageTurn.isReset && pageTurn.turn == groupTurn.turn);
            if (isGroup && page >= 0)
            {
                pages.push_back(page);
            }
        }

        QStringList parts;
        int length = 0;
        const QString suffix = QLatin1Char(':') + (groupTurn.isReset ? QString::fromLatin1(ROTATION_RESET) : getTurnName(groupTurn.turn));
        for (size_t i = 0; i < pages.size();)
        {
            size_t last = i;
            while (last + 1 < pages.size() && pages[last + 1] == pages[last] + 1)
            {
                ++last;
            }
            const QString part = last == i ? QString::number(pages[i] + 1) : QStringLiteral("%1-%2").arg(pages[i] + 1).arg(pages[last] + 1);
            if (!parts.isEmpty() && length + part.size() + 1 > PDFDocumentToolPageSelection::MAXIMUM_PAGES_LENGTH)
            {
                groups << parts.join(QLatin1Char(',')) + suffix;
                parts.clear();
                length = 0;
            }
            parts << part;
            length += int(part.size()) + 1;
            i = last + 1;
        }
        if (!parts.isEmpty())
        {
            groups << parts.join(QLatin1Char(',')) + suffix;
        }
    }
    return groups.join(QStringLiteral("; "));
}

std::optional<std::vector<PDFDocumentPageOperations::PageRotationChange>> PDFDocumentToolRotatePages::createChanges(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage)
{
    using Helper = PDFDocumentToolPagesHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();
    if (!document)
    {
        error = tr("There is no document.");
        return std::nullopt;
    }

    std::map<PDFInteger, Turn> turns;
    const QString plan = parameters.value(Helper::name(PARAMETER_PLAN)).toString().trimmed();
    if (!plan.isEmpty())
    {
        std::optional<std::map<PDFInteger, Turn>> planTurns = parsePlan(plan, document, &error);
        if (!planTurns)
        {
            return std::nullopt;
        }
        turns = std::move(*planTurns);
    }
    else
    {
        const std::optional<Turn> turn = getTurn(parameters.value(Helper::name(PARAMETER_ROTATION)).toString());
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(parameters.value(Helper::name(PARAMETER_PAGES)), &error);
        if (!turn)
        {
            error = tr("Say how the pages turn: rotation left, right, half or reset.");
            return std::nullopt;
        }
        if (!selection)
        {
            return std::nullopt;
        }
        for (const PDFInteger page : selection->getPageIndices(Helper::getPageSizes(document), &error))
        {
            turns.emplace(page, *turn);
        }
        if (turns.empty())
        {
            return std::nullopt;
        }
    }

    const PDFCatalog* catalog = document->getCatalog();
    std::vector<PDFDocumentPageOperations::PageRotationChange> changes;
    for (const auto& [page, turn] : turns)
    {
        const PageRotation current = catalog->getPage(size_t(page))->getPageRotation();
        const PageRotation rotation = turn.isReset ? PageRotation::None : getPageRotationCombined(turn.turn, current);
        changes.push_back({ page, rotation });
    }
    return changes;
}

QString PDFDocumentToolRotatePages::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolRotatePages::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolPagesHelper;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createPageSelection(tr, Helper::name(PARAMETER_PAGES), QT_TR_NOOP("Pages to rotate"), QT_TR_NOOP("The pages, which turn by the rotation (not with plan)")).setStored(false));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_ROTATION), QT_TR_NOOP("Rotation"), QT_TR_NOOP("How the pages turn (not with plan); reset gives them the rotation 0"),
                                                 {
                                                     { QString::fromLatin1(ROTATION_LEFT), QT_TR_NOOP("Rotate Left"), QT_TR_NOOP("90° counterclockwise") },
                                                     { QString::fromLatin1(ROTATION_RIGHT), QT_TR_NOOP("Rotate Right"), QT_TR_NOOP("90° clockwise") },
                                                     { QString::fromLatin1(ROTATION_HALF), QT_TR_NOOP("Rotate 180°"), QT_TR_NOOP("Upside down") },
                                                     { QString::fromLatin1(ROTATION_RESET), QT_TR_NOOP("Reset Rotation"), QT_TR_NOOP("The rotation of the page becomes 0") },
                                                 },
                                                 QString::fromLatin1(ROTATION_RIGHT)).setStored(false));
    definitions.push_back(Definition::createString(tr, Helper::name(PARAMETER_PLAN), QT_TR_NOOP("Plan"),
                                                   QT_TR_NOOP("Pages turned differently in one step, instead of pages and rotation: groups of pages and a rotation separated by semicolons, such as \"9,10,15:right; 20:half\""),
                                                   QString(), MAXIMUM_PLAN_LENGTH).setStored(false));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolRotatePages::checkGivenParameters(const QVariantMap& given) const
{
    using Helper = PDFDocumentToolPagesHelper;

    // pages and rotation would be ignored next to a plan, a call without both asks for nothing
    std::vector<PDFDocumentToolParameterError> errors;
    const bool hasPlan = !given.value(Helper::name(PARAMETER_PLAN)).toString().trimmed().isEmpty();
    if (hasPlan)
    {
        for (const char* parameter : { PARAMETER_PAGES, PARAMETER_ROTATION })
        {
            if (given.contains(Helper::name(parameter)))
            {
                errors.push_back({ Helper::name(parameter), tr("%1 is not used with plan: the plan names its pages and rotations. Leave %1 out, or leave plan out.").arg(Helper::name(parameter)) });
            }
        }
    }
    else if (!given.contains(Helper::name(PARAMETER_ROTATION)))
    {
        errors.push_back({ Helper::name(PARAMETER_ROTATION), tr("Set rotation (left, right, half or reset) with pages, or a plan.") });
    }
    return errors;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolRotatePages::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (!errors.empty())
    {
        return errors;
    }

    QString error;
    if (!createChanges(context.getDocument(), parameters, &error))
    {
        const bool hasPlan = !parameters.value(PDFDocumentToolPagesHelper::name(PARAMETER_PLAN)).toString().trimmed().isEmpty();
        errors.push_back({ PDFDocumentToolPagesHelper::name(hasPlan ? PARAMETER_PLAN : PARAMETER_PAGES), error });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolRotatePages::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    Q_UNUSED(progress);
    using Helper = PDFDocumentToolPagesHelper;

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();

    QString error;
    const std::optional<std::vector<PDFDocumentPageOperations::PageRotationChange>> changes = createChanges(document, parameters, &error);
    if (!changes)
    {
        return PDFDocumentToolResult::createFailed(error);
    }

    // Only a page, whose rotation changes, is turned
    const PDFCatalog* catalog = document->getCatalog();
    std::vector<PDFDocumentPageOperations::PageRotationChange> effective;
    std::vector<PDFInteger> pages;
    std::set<PageRotation> turns;
    for (const PDFDocumentPageOperations::PageRotationChange& change : *changes)
    {
        const PageRotation current = catalog->getPage(size_t(change.pageIndex))->getPageRotation();
        if (current == change.rotation)
        {
            continue;
        }
        effective.push_back(change);
        pages.push_back(change.pageIndex);
        turns.insert(getPageRotationCombined(getPageRotationInversed(current), change.rotation));
    }

    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    if (effective.empty())
    {
        return PDFDocumentToolResult::createNothingToDo(tr("The pages have this rotation already."));
    }

    PDFDocument output = PDFDocumentPageOperations::rotatePages(document, effective);

    // Q12, R10: no level of a certification allows to rotate pages - the result is a copy (the
    // rule of the callout of the panel, owner 13. 11.)
    PDFInteger certificationSignaturesRemoved = 0;
    const bool isCertified = PDFDocMDPPermissions::isCertificationRemovedBy(document, getOutputTraits(parameters));
    if (isCertified)
    {
        output = PDFDocMDPPermissions::removeCertification(output, &certificationSignaturesRemoved);
    }

    const QString rotationText = turns.size() == 1 ? getTurnText(*turns.begin()) : tr("Mixed");
    PDFDocumentToolResult result;
    result.facts.push_back({ QStringLiteral("rotated"), tr("Rotated"), tr("%Ln pages · %1", nullptr, int(pages.size())).arg(PDFDocumentPageOperations::formatPageNumbers(pages)) });
    result.facts.push_back({ QStringLiteral("rotation"), tr("Rotation"), rotationText });
    result.data.insert(QStringLiteral("rotatedCount"), qint64(pages.size()));
    result.data.insert(QStringLiteral("rotatedPages"), Helper::getPageNumbers(pages));
    result.data.insert(QStringLiteral("rotation"), turns.size() == 1 ? getTurnName(*turns.begin()) : QString::fromLatin1(Helper::ROTATION_MIXED));
    Helper::addDocument(&result, input, std::move(output), QStringLiteral("_rotated"));
    if (isCertified)
    {
        PDFDocMDPPermissions::addCertificationRemoved(&result, certificationSignaturesRemoved);
    }
    return result;
}

QVariantMap PDFDocumentToolRotatePages::mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const
{
    using Helper = PDFDocumentToolPagesHelper;

    if (!from || !to || from == to)
    {
        return parameters;
    }

    const std::vector<PDFInteger> pageMap = PDFDocumentToolPageSelection::mapPageIndices(from, to);
    QVariantMap mapped = parameters;
    Helper::mapSelection(&mapped, PARAMETER_PAGES, from, pageMap);

    const QString plan = parameters.value(Helper::name(PARAMETER_PLAN)).toString();
    QString error;
    const std::optional<std::map<PDFInteger, Turn>> turns = plan.trimmed().isEmpty() ? std::nullopt : parsePlan(plan, from, &error);
    if (turns)
    {
        std::map<PDFInteger, Turn> mappedTurns;
        for (const auto& [page, turn] : *turns)
        {
            if (size_t(page) < pageMap.size() && pageMap[size_t(page)] >= 0)
            {
                mappedTurns.emplace(pageMap[size_t(page)], turn);
            }
        }
        mapped.insert(Helper::name(PARAMETER_PLAN), createPlan(mappedTurns));

        // Without the plan the pages and the rotation would turn: all pages by default
        QString selectionError;
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(mapped.value(Helper::name(PARAMETER_PAGES)), &selectionError);
        if (mappedTurns.empty() && (!selection || (!selection->isNone && selection->pages.trimmed().isEmpty())))
        {
            PDFDocumentToolPageSelection none;
            none.isNone = true;
            mapped.insert(Helper::name(PARAMETER_PAGES), none.toVariant());
        }
    }
    return mapped;
}

// ------------------------------------------------------- PDFDocumentToolRemovePages

std::vector<PDFInteger> PDFDocumentToolRemovePages::getPages(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();
    if (!document)
    {
        error = tr("There is no document.");
        return {};
    }

    const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(parameters.value(PDFDocumentToolPagesHelper::name(PARAMETER_PAGES)), &error);
    if (!selection)
    {
        return {};
    }

    std::vector<PDFInteger> pages = selection->getPageIndices(document, &error);
    if (!pages.empty() && pages.size() >= document->getCatalog()->getPageCount())
    {
        error = tr("All pages are selected. A document must keep at least one page.");
        return {};
    }
    return pages;
}

QString PDFDocumentToolRemovePages::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolRemovePages::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createPageSelection(tr, PDFDocumentToolPagesHelper::name(PARAMETER_PAGES), QT_TR_NOOP("Pages to remove"),
                                                          QT_TR_NOOP("The pages, which are removed; at least one page of the document stays")).setRequired(true).setStored(false));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolRemovePages::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (!errors.empty())
    {
        return errors;
    }

    QString error;
    if (getPages(context.getDocument(), parameters, &error).empty())
    {
        errors.push_back({ PDFDocumentToolPagesHelper::name(PARAMETER_PAGES), error });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolRemovePages::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    Q_UNUSED(progress);
    using Helper = PDFDocumentToolPagesHelper;

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();

    QString error;
    const std::vector<PDFInteger> pages = getPages(document, parameters, &error);
    if (pages.empty())
    {
        return PDFDocumentToolResult::createFailed(error);
    }

    PDFDocumentPageOperations::RemovalStatistics statistics;
    PDFDocument output = PDFDocumentPageOperations::removePages(document, pages, control, &statistics);
    if (statistics.isCanceled || PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    // Without its tags the document is not checked against PDF/UA or PDF/A of the level a
    ConformanceRemoval conformance;
    if (statistics.structureTreeRemoved)
    {
        PDFObjectStorage storage = output.getStorage();
        conformance = removeConformanceDeclaration(&storage, &output);
        if (!conformance.kept.isEmpty())
        {
            return createConformanceFailure(conformance.kept);
        }
        if (!conformance.removed.isEmpty())
        {
            output = PDFDocument(std::move(storage), document->getInfo()->version, document->getSourceDataHash());
        }
    }

    // Q12, R10: no level of a certification allows to remove pages - the result is a copy (the
    // rule of the callout of the panel, owner 13. 11.)
    PDFInteger certificationSignaturesRemoved = 0;
    const bool isCertified = PDFDocMDPPermissions::isCertificationRemovedBy(document, getOutputTraits(parameters));
    if (isCertified)
    {
        output = PDFDocMDPPermissions::removeCertification(output, &certificationSignaturesRemoved);
    }

    const size_t pageCountBefore = document->getCatalog()->getPageCount();
    const size_t pageCount = output.getCatalog()->getPageCount();

    PDFDocumentToolResult result;
    result.facts.push_back({ QStringLiteral("removed"), tr("Removed"), tr("%Ln pages · %1", nullptr, int(pages.size())).arg(PDFDocumentPageOperations::formatPageNumbers(pages)) });
    result.facts.push_back({ QStringLiteral("document"), tr("Document"), tr("%Ln pages (was %1)", nullptr, int(pageCount)).arg(pageCountBefore) });
    if (statistics.structureTreeRemoved)
    {
        result.facts.push_back({ QStringLiteral("tags"), tr("Tags"), tr("Removed: the structure of the whole document goes with the pages") });
    }
    if (statistics.signaturesRemoved > 0)
    {
        result.facts.push_back({ QStringLiteral("signatures"), tr("Signatures"), tr("%Ln removed with their pages", nullptr, int(statistics.signaturesRemoved)) });
    }
    if (statistics.documentPartsRemoved)
    {
        // 14.12: the parts keep the pages, which stay; only a damaged hierarchy is removed
        result.facts.push_back({ QStringLiteral("documentParts"), tr("Document parts"), tr("Document parts removed") });
    }
    addConformanceFact(&result, conformance.removed);

    result.data.insert(QStringLiteral("removedCount"), qint64(pages.size()));
    result.data.insert(QStringLiteral("removedPages"), Helper::getPageNumbers(pages));
    result.data.insert(QStringLiteral("pageCount"), qint64(pageCount));
    result.data.insert(QStringLiteral("pageCountBefore"), qint64(pageCountBefore));
    result.data.insert(QStringLiteral("outlineItemsRemoved"), statistics.outlineItemsRemoved);
    result.data.insert(QStringLiteral("namedDestinationsRemoved"), statistics.namedDestinationsRemoved);
    result.data.insert(QStringLiteral("linksRemoved"), statistics.linksRemoved);
    result.data.insert(QStringLiteral("fieldsRemoved"), statistics.fieldsRemoved);
    result.data.insert(QStringLiteral("signaturesRemoved"), statistics.signaturesRemoved);
    result.data.insert(QStringLiteral("layersRemoved"), statistics.layersRemoved);
    result.data.insert(QStringLiteral("structureTreeRemoved"), statistics.structureTreeRemoved);
    result.data.insert(QStringLiteral("threadsRemoved"), statistics.threadsRemoved);
    Helper::addDocument(&result, input, std::move(output), QStringLiteral("_removed"));
    if (isCertified)
    {
        PDFDocMDPPermissions::addCertificationRemoved(&result, certificationSignaturesRemoved);
    }
    return result;
}

QVariantMap PDFDocumentToolRemovePages::mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const
{
    if (!from || !to || from == to)
    {
        return parameters;
    }

    QVariantMap mapped = parameters;
    PDFDocumentToolPagesHelper::mapSelection(&mapped, PARAMETER_PAGES, from, PDFDocumentToolPageSelection::mapPageIndices(from, to));
    return mapped;
}

}   // namespace pdf
