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

#include "pdfdocumenttool_manage.h"

#include "pdfcatalog.h"
#include "pdfdocmdppermissions.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumentpageoperations.h"
#include "pdfdocumenttool_merge.h"
#include "pdfexception.h"
#include "pdffilenametemplate.h"
#include "pdfform.h"
#include "pdfimagepagefactory.h"
#include "pdfocrpagepreparer.h"
#include "pdfoperationcontrol.h"
#include "pdfpagerangelist.h"
#include "pdfsecurityhandler.h"

#include <QFileInfo>
#include <QJsonArray>

#include <set>

namespace pdf
{

/// Helpers of Manage Pages
class PDFDocumentToolManageHelper
{
public:
    PDFDocumentToolManageHelper() = delete;

    using Assembly = PDFDocumentAssembly;
    using Tool = PDFDocumentToolManage;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// The pages of the assembly as the merger takes them: the sources used by the plan (the
    /// target first for This document), a blank source for the blank pages, the items and the parts
    struct Run
    {
        std::vector<PDFDocumentMerger::Source> sources;
        std::vector<PDFDocumentMerger::Item> items;
        Assembly::Plan plan;
        std::vector<int> inputOfSource;     ///< The source of the assembly of each merger source, -1 = the blank pages
        int usedSourceCount = 0;            ///< Sources of the assembly with a page in the result (Pages "17 from 3 sources")
    };

    /// The input document of the context of the source \p input of the assembly: the target is the
    /// first document; else the document of its id (the GUI); a document without an id (the MCP
    /// bridge gives the open documents in the order of the PDF sources) is the k-th PDF source
    static const PDFDocumentToolInputDocument* findDocument(const PDFDocumentToolContext& context, const Assembly& assembly, size_t input)
    {
        const Assembly::Source& source = assembly.getSources()[input];
        if (source.isTarget)
        {
            return context.documents.empty() ? nullptr : &context.documents.front();
        }
        for (const PDFDocumentToolInputDocument& document : context.documents)
        {
            if (!document.inputId.isEmpty() && document.inputId == source.id)
            {
                return &document;
            }
        }

        const auto& sources = assembly.getSources();
        const size_t pdfIndex = size_t(std::count_if(sources.cbegin(), sources.cbegin() + qsizetype(input), [](const Assembly::Source& item) { return item.kind == Assembly::SourceKind::Pdf; }));
        if (pdfIndex < context.documents.size() && context.documents[pdfIndex].inputId.isEmpty())
        {
            return &context.documents[pdfIndex];
        }
        return nullptr;
    }

    /// The run of the assembly; std::nullopt and the reason for a source, which is not in the
    /// context, a page, which its source does not have, and an image, which cannot be read
    static std::optional<Run> createRun(const PDFDocumentToolContext& context, const Assembly& assembly, bool isTargetFirst, QString* errorMessage)
    {
        Run run;
        run.plan = Assembly::toPlan(assembly);
        if (run.plan.items.empty())
        {
            *errorMessage = Tool::tr("The assembly has no pages.");
            return std::nullopt;
        }

        const std::vector<Assembly::Source>& sources = assembly.getSources();
        std::vector<PDFInteger> maximumPage(sources.size(), -1);
        bool hasBlankPage = false;
        for (const PDFDocumentPagePlan::Item& item : run.plan.items)
        {
            if (item.input < 0)
            {
                hasBlankPage = true;
            }
            else
            {
                maximumPage[size_t(item.input)] = qMax(maximumPage[size_t(item.input)], item.pageIndex);
            }
        }

        std::vector<int> mergerSource(sources.size(), -1);
        if (isTargetFirst)
        {
            if (sources.empty() || !sources.front().isTarget || context.documents.empty() || !context.documents.front().document)
            {
                *errorMessage = Tool::tr("This document is not a source of the assembly.");
                return std::nullopt;
            }
            mergerSource[0] = 0;
            PDFDocumentMerger::Source target = PDFDocumentMerger::Source::createDocument(context.documents.front().document, sources.front().name);
            target.isOwnerPasswordVerified = context.documents.front().isOwnerPasswordVerified;
            run.sources.push_back(std::move(target));
            run.inputOfSource.push_back(0);
        }

        for (size_t input = 0; input < sources.size(); ++input)
        {
            if (maximumPage[input] < 0)
            {
                continue;
            }
            ++run.usedSourceCount;
            if (mergerSource[input] >= 0)
            {
                // The target of This document
                if (maximumPage[input] >= PDFInteger(context.documents.front().document->getCatalog()->getPageCount()))
                {
                    *errorMessage = Tool::tr("The document has changed.");
                    return std::nullopt;
                }
                continue;
            }

            const Assembly::Source& source = sources[input];
            if (source.kind == Assembly::SourceKind::Image)
            {
                const PDFImagePageFactory::ImageInfo info = PDFImagePageFactory::readInfo(source.imageData, source.imageFormat);
                if (info.frameCount <= maximumPage[input])
                {
                    *errorMessage = Tool::tr("%1 cannot be used: %2").arg(source.name, info.frameCount <= 0 ? info.error : Tool::tr("it has %Ln pages", nullptr, info.frameCount));
                    return std::nullopt;
                }
                run.sources.push_back(PDFDocumentMerger::Source::createImage(source.imageData, source.imageFormat, 0.0, source.name));
            }
            else
            {
                const PDFDocumentToolInputDocument* document = findDocument(context, assembly, input);
                if (!document || !document->document)
                {
                    *errorMessage = Tool::tr("The file %1 of the assembly is not open.").arg(source.name);
                    return std::nullopt;
                }
                const PDFInteger pageCount = PDFInteger(document->document->getCatalog()->getPageCount());
                if (maximumPage[input] >= pageCount)
                {
                    *errorMessage = Tool::tr("Page %1 does not exist – %2 has %Ln pages.", nullptr, int(pageCount)).arg(maximumPage[input] + 1).arg(source.name);
                    return std::nullopt;
                }
                PDFDocumentMerger::Source mergerInput = PDFDocumentMerger::Source::createDocument(document->document, source.name);
                mergerInput.isOwnerPasswordVerified = document->isOwnerPasswordVerified;
                run.sources.push_back(std::move(mergerInput));
            }
            mergerSource[input] = int(run.sources.size() - 1);
            run.inputOfSource.push_back(int(input));
        }

        int blankSource = -1;
        if (hasBlankPage)
        {
            run.sources.push_back(PDFDocumentMerger::Source::createBlank());
            run.inputOfSource.push_back(-1);
            blankSource = int(run.sources.size() - 1);
        }

        run.items = PDFDocumentPagePlan::createMergerItems(run.plan.items);
        for (size_t i = 0; i < run.items.size(); ++i)
        {
            PDFDocumentMerger::Item& item = run.items[i];
            const int input = run.plan.items[i].input;
            item.source = size_t(input < 0 ? blankSource : mergerSource[size_t(input)]);

            // The crop of the assembly is in points of the page as it is shown (Source::pageSizes:
            // with /UserUnit), the merger cuts it from the crop box in the units of the page
            // (Table 31: /UserUnit is the size of a unit in points)
            const PDFDocumentPointer& document = run.sources[item.source].document;
            if (!item.cropMargins.isNull() && document)
            {
                const PDFReal unit = document->getCatalog()->getPage(size_t(item.pageIndex))->getEffectiveUserUnit();
                item.cropMargins = QMarginsF(item.cropMargins.left() / unit, item.cropMargins.top() / unit, item.cropMargins.right() / unit, item.cropMargins.bottom() / unit);
            }
        }
        return run;
    }

    /// The pages of one PDF source only, without a group: the document keeps its catalog and its
    /// outline (R3 of step 2) - the assembly has no structure of its own to make parts of
    static bool isSingleDocumentWithoutGroups(const Assembly& assembly, const Run& run)
    {
        const bool hasGroup = std::any_of(assembly.getItems().cbegin(), assembly.getItems().cend(), [](const Assembly::Item& item) { return item.isGroup(); });
        return !hasGroup && run.sources.size() == 1 && run.sources.front().kind == PDFDocumentMerger::Source::Kind::Document;
    }

    /// The settings of the merger of the outline (QB3: the parts of the assembly; none for one PDF
    /// source without groups, R3)
    static PDFDocumentMerger::Settings createSettings(const QVariantMap& parameters, const Assembly& assembly, const Run& run)
    {
        PDFDocumentMerger::Settings settings;
        settings.checkPermissions = true;
        const QString outline = parameters.value(name(Tool::PARAMETER_OUTLINE)).toString();
        if (outline == QLatin1String(Tool::OUTLINE_NONE))
        {
            settings.outline = PDFDocumentMerger::OutlineMode::NoOutline;
            return settings;
        }
        settings.outline = outline == QLatin1String(Tool::OUTLINE_JOIN) ? PDFDocumentMerger::OutlineMode::Join : PDFDocumentMerger::OutlineMode::DocumentParts;
        if (!isSingleDocumentWithoutGroups(assembly, run))
        {
            settings.parts = run.plan.parts;
        }
        return settings;
    }

    /// The images of the result: the presets of Merge (K24, one definition with Compress)
    static std::optional<PDFImageOptimizer::Settings> createImageSettings(const QVariantMap& parameters)
    {
        const QString images = parameters.value(name(Tool::PARAMETER_OPTIMIZE_IMAGES)).toString();
        QVariantMap mergeParameters;
        if (images == QLatin1String(Tool::IMAGES_QUALITY))
        {
            mergeParameters.insert(name(PDFDocumentToolMerge::PARAMETER_OPTIMIZE_IMAGES), QString::fromLatin1(PDFDocumentToolMerge::OPTIMIZE_PREFER_QUALITY));
        }
        else if (images == QLatin1String(Tool::IMAGES_SIZE))
        {
            mergeParameters.insert(name(PDFDocumentToolMerge::PARAMETER_OPTIMIZE_IMAGES), QString::fromLatin1(PDFDocumentToolMerge::OPTIMIZE_MINIMUM_SIZE));
        }
        return PDFDocumentToolMerge::createImageSettings(mergeParameters);
    }

    /// The pages of the document, which have a widget of a signed signature field (RB5 5)
    static std::set<PDFInteger> getSignedPages(const PDFDocument* document)
    {
        std::set<PDFInteger> signedPages;
        try
        {
            const PDFCatalog* catalog = document->getCatalog();
            const PDFForm form = PDFForm::parse(document, catalog->getFormObject());
            if (!form.isAcroForm())
            {
                return signedPages;
            }
            for (size_t i = 0; i < catalog->getPageCount(); ++i)
            {
                for (const PDFObjectReference& annotation : catalog->getPage(i)->getAnnotations())
                {
                    const PDFFormField* field = form.getFormFieldForWidget(annotation);
                    if (field && field->getFieldType() == PDFFormField::FieldType::Signature && !document->getObject(field->getValue()).isNull())
                    {
                        signedPages.insert(PDFInteger(i));
                    }
                }
            }
        }
        catch (const PDFException&)
        {
            // A damaged form has no signature, which could be kept
        }
        return signedPages;
    }

    /// The facts of This document, which only the arrangement knows: the document parts (14.12:
    /// removed by another order; with the order kept the removed pages leave their parts and the
    /// new pages join them - a damaged hierarchy is removed by removePages) and the widgets, which
    /// a clone leaves out (RB1: of signature fields; L1 of RS3BCORE: of the fields locked by a
    /// signed signature field, its /Lock, 12.7.5.5, Table 236, or the FieldMDP transform
    /// parameters of its signature, 12.8.2.4, Table 259)
    static void addArrangementFacts(const PDFDocument* target, const PDFDocument* arranged, const PDFDocumentMerger::Statistics& statistics, PDFDocumentToolResult* result)
    {
        const PDFDictionary* targetCatalog = getCatalogDictionary(target);
        const PDFDictionary* arrangedCatalog = getCatalogDictionary(arranged);
        if (targetCatalog && targetCatalog->hasKey("DPartRoot") && !(arrangedCatalog && arrangedCatalog->hasKey("DPartRoot")))
        {
            result->facts.push_back({ QStringLiteral("documentParts"), Tool::tr("Document parts"), Tool::tr("Document parts removed") });
        }
        if (statistics.signatureWidgetsNotCloned > 0)
        {
            result->facts.push_back({ QStringLiteral("signatureFields"), Tool::tr("Signature fields"), Tool::tr("Signature fields are not cloned") });
        }
        if (statistics.lockedWidgetsNotCloned > 0)
        {
            result->facts.push_back({ QStringLiteral("lockedFields"), Tool::tr("Locked fields"), Tool::tr("Fields locked by a signature are not cloned") });
        }
    }

    /// The catalog of a document
    static const PDFDictionary* getCatalogDictionary(const PDFDocument* document)
    {
        const PDFDictionary* trailer = document->getTrailerDictionary();
        return trailer ? document->getDictionaryFromObject(trailer->get("Root")) : nullptr;
    }

    /// The facts of the pages of This document; NothingToDo, when the pages stay as they are
    static PDFDocumentToolResult createDocumentFacts(const PDFDocument* target, const Run& run, bool* isNothingToDo)
    {
        const PDFInteger pageCount = PDFInteger(target->getCatalog()->getPageCount());
        std::vector<bool> isUsed(size_t(pageCount), false);
        PDFInteger previous = -1;
        bool isOrderChanged = false;
        qint64 rotated = 0;
        qint64 cropped = 0;
        qint64 inserted = 0;
        std::vector<PDFInteger> clonedPages;
        for (const PDFDocumentMerger::Item& item : run.items)
        {
            if (item.source != 0)
            {
                ++inserted;
                continue;
            }
            isUsed[size_t(item.pageIndex)] = true;
            rotated += item.turn != PageRotation::None ? 1 : 0;
            cropped += !item.cropMargins.isNull() ? 1 : 0;
            if (item.isClone)
            {
                clonedPages.push_back(item.pageIndex);
                continue;
            }
            isOrderChanged = isOrderChanged || item.pageIndex < previous;
            previous = item.pageIndex;
        }
        std::vector<PDFInteger> removed;
        for (PDFInteger i = 0; i < pageCount; ++i)
        {
            if (!isUsed[size_t(i)])
            {
                removed.push_back(i);
            }
        }

        PDFDocumentToolResult result;
        *isNothingToDo = !isOrderChanged && removed.empty() && rotated == 0 && cropped == 0 && clonedPages.empty() && inserted == 0;
        if (*isNothingToDo)
        {
            return result;
        }

        const qint64 resultCount = qint64(run.items.size());
        result.facts.push_back({ QStringLiteral("pages"), Tool::tr("Pages"), Tool::tr("%Ln pages (was %1)", nullptr, int(resultCount)).arg(pageCount) });
        if (isOrderChanged)
        {
            result.facts.push_back({ QStringLiteral("order"), Tool::tr("Order"), Tool::tr("Pages moved") });
        }
        if (rotated > 0)
        {
            result.facts.push_back({ QStringLiteral("rotated"), Tool::tr("Rotated"), Tool::tr("%Ln pages", nullptr, int(rotated)) });
        }
        if (cropped > 0)
        {
            result.facts.push_back({ QStringLiteral("cropped"), Tool::tr("Cropped"), Tool::tr("%Ln pages", nullptr, int(cropped)) });
        }
        if (!clonedPages.empty())
        {
            result.facts.push_back({ QStringLiteral("cloned"), Tool::tr("Cloned"), Tool::tr("%Ln pages", nullptr, int(clonedPages.size())) });
        }
        if (inserted > 0)
        {
            result.facts.push_back({ QStringLiteral("inserted"), Tool::tr("Inserted"), Tool::tr("%Ln pages", nullptr, int(inserted)) });
        }
        if (!removed.empty())
        {
            result.facts.push_back({ QStringLiteral("removed"), Tool::tr("Removed"), Tool::tr("%Ln pages · %1", nullptr, int(removed.size())).arg(PDFDocumentPageOperations::formatPageNumbers(removed)) });
        }

        // QB6 a, RB13: the structure tree stays (its reading order too), removed pages take it
        // away as Remove Pages does (Q5 b of step 3 is not done yet)
        const bool isTagged = PDFOCRPagePreparer::isTaggedDocument(target);
        if (isTagged && !removed.empty())
        {
            result.facts.push_back({ QStringLiteral("tags"), Tool::tr("Tags"), Tool::tr("Removed: the structure of the whole document goes with the removed pages") });
        }
        else if (isTagged && isOrderChanged)
        {
            result.facts.push_back({ QStringLiteral("tags"), Tool::tr("Tags"), Tool::tr("The reading order of the tags is not changed") });
        }
        if (isTagged && !clonedPages.empty())
        {
            result.facts.push_back({ QStringLiteral("clonedTags"), Tool::tr("Cloned pages"), Tool::tr("Cloned pages are not tagged") });
        }

        // RB5: the signed revisions (the fields, which a clone leaves out, are facts of the
        // arrangement: addArrangementFacts)
        const PDFDictionary* catalog = getCatalogDictionary(target);
        const std::set<PDFInteger> signedPages = getSignedPages(target);
        if (std::any_of(removed.cbegin(), removed.cend(), [&signedPages](PDFInteger page) { return signedPages.count(page) > 0; }))
        {
            result.facts.push_back({ QStringLiteral("removedSignature"), Tool::tr("Signatures"), Tool::tr("The signature on a removed page is gone") });
        }
        else if (PDFForm::getSignatureCount(target) > 0 && PDFDocMDPPermissions::isPageChangeAllowed(target))
        {
            result.facts.push_back({ QStringLiteral("signedRevisions"), Tool::tr("Signatures"), Tool::tr("Signed revisions stay unchanged; viewers list the page changes made after signing") });
        }
        const PDFDictionary* permissions = catalog ? target->getDictionaryFromObject(catalog->get("Perms")) : nullptr;
        if (permissions && permissions->hasKey("UR3"))
        {
            result.facts.push_back({ QStringLiteral("usageRights"), Tool::tr("Usage rights"), Tool::tr("Usage rights of the document stop being valid") });
        }

        // Q18: the declarations stay (PDF/A, PDF/UA and PDF/X), the new pages were not checked
        const QStringList declarations = PDFDocumentTool::getConformanceDeclarations(target);
        if (!declarations.isEmpty() && (inserted > 0 || !clonedPages.empty()))
        {
            result.facts.push_back({ QStringLiteral("conformance"), Tool::tr("Conformance"), Tool::tr("Pages were not checked for %1").arg(declarations.join(QStringLiteral(", "))) });
        }

        result.data.insert(QStringLiteral("pageCount"), resultCount);
        result.data.insert(QStringLiteral("pageCountBefore"), qint64(pageCount));
        result.data.insert(QStringLiteral("orderChanged"), isOrderChanged);
        result.data.insert(QStringLiteral("rotatedCount"), rotated);
        result.data.insert(QStringLiteral("croppedCount"), cropped);
        result.data.insert(QStringLiteral("clonedCount"), qint64(clonedPages.size()));
        result.data.insert(QStringLiteral("insertedCount"), inserted);
        QJsonArray removedNumbers;
        for (const PDFInteger page : removed)
        {
            removedNumbers.append(qint64(page + 1));
        }
        result.data.insert(QStringLiteral("removedPages"), removedNumbers);
        return result;
    }

    /// The suggested name of a new document: "<the first source>_assembled.pdf"
    static QString getSuggestedFileName(const Assembly& assembly)
    {
        for (const Assembly::Source& source : assembly.getSources())
        {
            const QString baseName = QFileInfo(source.name).completeBaseName();
            if (!baseName.isEmpty() && !source.isParked)
            {
                return baseName + QStringLiteral("_assembled.pdf");
            }
        }
        return QStringLiteral("assembled.pdf");
    }
};

QString PDFDocumentToolManage::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolManage::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolManageHelper;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createString(tr, Helper::name(PARAMETER_INPUTS), QT_TR_NOOP("Files"),
                                                   QT_TR_NOOP("Ids of the files in the order of the panel, separated by commas"),
                                                   QString(), PDFDocumentToolMerge::MAXIMUM_INPUTS_LENGTH).setStored(false).setPanelOnly(true));
    definitions.push_back(Definition::createValue(tr, Helper::name(PARAMETER_ASSEMBLY), QT_TR_NOOP("Assembly"),
                                                  QT_TR_NOOP("The sources, the pages and the groups of the assembly"),
                                                  QVariant::fromValue(PDFDocumentAssembly())));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_OUTPUT), QT_TR_NOOP("Output"), QT_TR_NOOP("What the tool makes of the assembly"),
                                                 {
                                                     { QString::fromLatin1(OUTPUT_DOCUMENT), QT_TR_NOOP("This document"), QT_TR_NOOP("Changes the open document") },
                                                     { QString::fromLatin1(OUTPUT_ONE_PDF), QT_TR_NOOP("One PDF"), QT_TR_NOOP("All pages in one document") },
                                                     { QString::fromLatin1(OUTPUT_PER_GROUP), QT_TR_NOOP("One PDF per group or item"), QT_TR_NOOP("Files named after the groups") },
                                                     { QString::fromLatin1(OUTPUT_PER_PAGE), QT_TR_NOOP("One PDF per page"), QT_TR_NOOP("A file of every page") },
                                                 },
                                                 QString::fromLatin1(OUTPUT_DOCUMENT)).setStored(false));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_OUTLINE), QT_TR_NOOP("Outline"), QT_TR_NOOP("Bookmarks of the result"),
                                                 {
                                                     { QString::fromLatin1(OUTLINE_DOCUMENT_PARTS), QT_TR_NOOP("Document parts"), QT_TR_NOOP("One bookmark per group or run of a file") },
                                                     { QString::fromLatin1(OUTLINE_JOIN), QT_TR_NOOP("Join outlines"), QT_TR_NOOP("Bookmarks of the files nested under each part") },
                                                     { QString::fromLatin1(OUTLINE_NONE), QT_TR_NOOP("No outline"), QT_TR_NOOP("The result has no bookmarks") },
                                                 },
                                                 QString::fromLatin1(OUTLINE_DOCUMENT_PARTS)));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_OPTIMIZE_IMAGES), QT_TR_NOOP("Images"), QT_TR_NOOP("Images of the result"),
                                                 {
                                                     { QString::fromLatin1(IMAGES_OFF), QT_TR_NOOP("Off"), QT_TR_NOOP("Images are stored exactly as they are") },
                                                     { QString::fromLatin1(IMAGES_QUALITY), QT_TR_NOOP("Prefer quality"), QT_TR_NOOP("Downsamples large images, keeps detail") },
                                                     { QString::fromLatin1(IMAGES_SIZE), QT_TR_NOOP("Minimum size"), QT_TR_NOOP("Smallest files, stronger compression") },
                                                 },
                                                 QString::fromLatin1(IMAGES_OFF)));
    PDFDocumentToolFileOutput::addDefinitions(&definitions, tr, QStringLiteral("##_{group_name}"));
    definitions.push_back(Definition::createFilePath(tr, Helper::name(PARAMETER_ASSEMBLY_PATH), QT_TR_NOOP("Assembly file"),
                                                     QT_TR_NOOP("An assembly file (.pdf4qtassembly) saved by PDF4QT, whose PDF files are open documents; with output one-pdf, per-group or per-page")).setStored(false));
    definitions.push_back(Definition::createString(tr, Helper::name(PARAMETER_PAGES), QT_TR_NOOP("Pages"),
                                                   QT_TR_NOOP("The pages of the document in their new order, such as \"3, 1-2, 5-\": every page at least once, a repeated page is a clone; with output document"),
                                                   QString(), MAXIMUM_PAGES_LENGTH).setStored(false));
    return definitions;
}

QString PDFDocumentToolManage::getOutput(const QVariantMap& parameters)
{
    const QString output = parameters.value(QString::fromLatin1(PARAMETER_OUTPUT)).toString();
    return output.isEmpty() ? QString::fromLatin1(OUTPUT_DOCUMENT) : output;
}

PDFDocumentAssembly PDFDocumentToolManage::getAssembly(const QVariantMap& parameters)
{
    const QVariant value = parameters.value(QString::fromLatin1(PARAMETER_ASSEMBLY));
    return value.canConvert<PDFDocumentAssembly>() && value.metaType() == QMetaType::fromType<PDFDocumentAssembly>() ? value.value<PDFDocumentAssembly>() : PDFDocumentAssembly();
}

std::optional<PDFDocumentAssembly> PDFDocumentToolManage::createAssemblyOfPages(const PDFDocument* document, const QString& name, const QString& pages, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (!document)
    {
        error = tr("There is no document.");
        return std::nullopt;
    }
    if (pages.size() > MAXIMUM_PAGES_LENGTH)
    {
        error = tr("The pages are longer than %1 characters.").arg(MAXIMUM_PAGES_LENGTH);
        return std::nullopt;
    }

    // Owner 2026-11-12: a document over the pages of an assembly is refused before its pages are read
    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    if (!PDFDocumentAssembly::checkDocumentPageCount(pageCount, &error))
    {
        return std::nullopt;
    }
    const std::optional<PDFPageRangeList> range = PDFPageRangeList::parse(pages, pageCount, &error);
    if (!range)
    {
        return std::nullopt;
    }

    // RB9: every page is named, a removal is pages.remove
    std::vector<bool> isNamed(size_t(pageCount), false);
    for (const PDFInteger page : range->getPageIndices())
    {
        isNamed[size_t(page)] = true;
    }
    std::vector<PDFInteger> missing;
    for (PDFInteger i = 0; i < pageCount; ++i)
    {
        if (!isNamed[size_t(i)])
        {
            missing.push_back(i);
        }
    }
    if (!missing.empty())
    {
        error = tr("pages must name every page of the document, missing: %1. A repeated page is a clone; remove pages by pages.remove.").arg(PDFDocumentPageOperations::formatPageNumbers(missing, 64));
        return std::nullopt;
    }

    // The assembly of the document in its order, then its items in the order of the pages; a
    // repeated page is a clone (the first occurrence is the original, RB4)
    const PDFDocumentAssembly original = PDFDocumentAssembly::createForDocument(document, 0, name);
    std::vector<PDFDocumentAssembly::Item> items;
    items.reserve(range->getPageIndices().size());
    std::vector<bool> isUsed(size_t(pageCount), false);
    for (const PDFInteger page : range->getPageIndices())
    {
        PDFDocumentAssembly::Item item = original.getItems()[size_t(page)];
        item.pages.front().plan.isClone = isUsed[size_t(page)];
        if (isUsed[size_t(page)])
        {
            item.id = QStringLiteral("c%1").arg(items.size());
        }
        isUsed[size_t(page)] = true;
        items.push_back(std::move(item));
    }

    PDFDocumentAssembly assembly = PDFDocumentAssembly::create(original.getSources(), std::move(items), { }, &error);
    if (!error.isEmpty())
    {
        return std::nullopt;
    }
    return assembly;
}

bool PDFDocumentToolManage::createFiles(const PDFDocumentAssembly& assembly, const QVariantMap& parameters, const QDate& date, std::vector<PDFDocumentToolFileOutput::File>* files, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();
    files->clear();

    // A file per item (per group) or per page; the values of the names of each file
    struct FileValues
    {
        int itemIndex = 0;
        int input = -1;
    };
    std::vector<FileValues> values;
    const bool isPerPage = getOutput(parameters) == QLatin1String(OUTPUT_PER_PAGE);
    const std::vector<PDFDocumentAssembly::Item>& items = assembly.getItems();
    PDFInteger position = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const PDFDocumentAssembly::Item& item = items[i];
        auto firstSource = std::find_if(item.pages.cbegin(), item.pages.cend(), [](const PDFDocumentAssembly::Page& page) { return page.plan.input >= 0; });
        if (!isPerPage)
        {
            PDFDocumentToolFileOutput::File file;
            file.title = PDFDocumentAssembly::getItemName(assembly, item);
            for (size_t j = 0; j < item.pages.size(); ++j)
            {
                file.pages.push_back(position++);
            }
            files->push_back(std::move(file));
            values.push_back({ int(i), firstSource != item.pages.cend() ? firstSource->plan.input : -1 });
            continue;
        }
        for (const PDFDocumentAssembly::Page& page : item.pages)
        {
            PDFDocumentToolFileOutput::File file;
            file.title = PDFDocumentAssembly::getItemName(assembly, item);
            file.pages.push_back(position++);
            files->push_back(std::move(file));
            values.push_back({ int(i), page.plan.input });
        }
        if (files->size() > size_t(PDFFileNameTemplate::MAXIMUM_OUTPUT_COUNT))
        {
            break;
        }
    }

    if (files->empty())
    {
        error = tr("The assembly has no pages.");
        return false;
    }
    if (files->size() > size_t(PDFFileNameTemplate::MAXIMUM_OUTPUT_COUNT))
    {
        error = tr("The pages would be split into more than %Ln files.", nullptr, PDFFileNameTemplate::MAXIMUM_OUTPUT_COUNT);
        return false;
    }

    const PDFFileNameTemplate nameTemplate(parameters.value(QString::fromLatin1(PDFDocumentToolFileOutput::PARAMETER_FILE_NAME)).toString(), QStringLiteral(".pdf"));
    if (!nameTemplate.isValid(&error))
    {
        return false;
    }

    QStringList names;
    for (size_t i = 0; i < files->size(); ++i)
    {
        PDFDocumentToolFileOutput::File& file = (*files)[i];
        PDFFileNameTemplate::Values templateValues;
        templateValues.outputIndex = int(i + 1);
        templateValues.outputCount = int(files->size());
        templateValues.pageNumber = file.pages.front() + 1;
        templateValues.sourceIndex = values[i].input + 1;
        templateValues.groupIndex = values[i].itemIndex + 1;
        templateValues.groupName = file.title;
        templateValues.sourceFileName = values[i].input >= 0 ? assembly.getSources()[size_t(values[i].input)].name : QStringLiteral("blank.pdf");
        templateValues.date = date.isValid() ? date : QDate::currentDate();
        file.fileName = nameTemplate.createFileName(templateValues);
        names << file.fileName;
    }

    const std::vector<int> duplicates = PDFFileNameTemplate::findDuplicates(names);
    if (!duplicates.empty())
    {
        error = int(duplicates.size()) + 1 == int(names.size()) ? tr("Every file would be named %1").arg(names.front())
                                                                : tr("%Ln files would have the same name as another file", nullptr, int(duplicates.size()));
        return false;
    }
    return true;
}

PDFDocumentToolResultKind PDFDocumentToolManage::getResultKind(const QVariantMap& parameters) const
{
    const QString output = getOutput(parameters);
    if (output == QLatin1String(OUTPUT_ONE_PDF))
    {
        return PDFDocumentToolResultKind::NewDocument;
    }
    if (output == QLatin1String(OUTPUT_PER_GROUP) || output == QLatin1String(OUTPUT_PER_PAGE))
    {
        return PDFDocumentToolResultKind::Files;
    }
    return PDFDocumentToolResultKind::ChangeDocument;
}

PDFDocumentToolPreselectedResult PDFDocumentToolManage::getPreselectedResult(const QVariantMap& parameters) const
{
    // tools.md 9.3 point 11 by the kind of the output: Keep, Open in a new tab (387:17762), the
    // written files (396:27563)
    return PDFDocumentToolExtendedData::getDefaultPreselectedResult(getResultKind(parameters));
}

PDFDocumentToolSignatureImpact PDFDocumentToolManage::getSignatureImpact(const QVariantMap& parameters) const
{
    // This document keeps the object numbers: a signed file gets an incremental update (4.5); a
    // new document has no signature of the inputs
    return PDFDocumentToolExtendedData::getDefaultSignatureImpact(getResultKind(parameters));
}

bool PDFDocumentToolManage::isNewDocumentOfInputs(const QVariantMap& parameters) const
{
    return getResultKind(parameters) != PDFDocumentToolResultKind::ChangeDocument;
}

bool PDFDocumentToolManage::isPageChange(const QVariantMap& parameters) const
{
    return getResultKind(parameters) == PDFDocumentToolResultKind::ChangeDocument;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolManage::checkGivenParameters(const QVariantMap& given) const
{
    using Helper = PDFDocumentToolManageHelper;

    // The assembly is never given by an assistant (it is not in the schema): the MCP bridge hands
    // over the assembly it has read of assemblyPath (Q9 b), so only then it is accepted
    const bool hasPages = !given.value(Helper::name(PARAMETER_PAGES)).toString().trimmed().isEmpty();
    const bool hasAssemblyPath = !given.value(Helper::name(PARAMETER_ASSEMBLY_PATH)).toString().trimmed().isEmpty();
    std::vector<PDFDocumentToolParameterError> errors;
    for (const char* parameter : { PARAMETER_INPUTS, PARAMETER_ASSEMBLY })
    {
        const bool isReadAssembly = hasAssemblyPath && qstrcmp(parameter, PARAMETER_ASSEMBLY) == 0;
        if (given.contains(Helper::name(parameter)) && !isReadAssembly)
        {
            errors.push_back({ Helper::name(parameter), tr("%1 is not a parameter of an assistant: give pages with documentId, or assemblyPath.").arg(Helper::name(parameter)) });
        }
    }

    const QString output = given.value(Helper::name(PARAMETER_OUTPUT)).toString();
    if (hasPages && hasAssemblyPath)
    {
        errors.push_back({ Helper::name(PARAMETER_PAGES), tr("Give either pages (the open document changes) or assemblyPath (new documents of an assembly file), not both.") });
    }
    else if (hasPages && !output.isEmpty() && output != QLatin1String(OUTPUT_DOCUMENT))
    {
        errors.push_back({ Helper::name(PARAMETER_OUTPUT), tr("pages changes the open document: leave output out or set it to document. New documents come from assemblyPath.") });
    }
    else if (hasAssemblyPath && (output.isEmpty() || output == QLatin1String(OUTPUT_DOCUMENT)))
    {
        errors.push_back({ Helper::name(PARAMETER_OUTPUT), tr("An assembly file makes new documents: set output to one-pdf, per-group or per-page.") });
    }
    else if (!hasPages && !hasAssemblyPath)
    {
        errors.push_back({ Helper::name(PARAMETER_PAGES), tr("Give pages (the new order of the pages of the open document) or assemblyPath.") });
    }
    return errors;
}

QVariantMap PDFDocumentToolManage::completeGivenParameters(const PDFDocumentToolContext& context, const QVariantMap& given) const
{
    using Helper = PDFDocumentToolManageHelper;

    const QString pages = given.value(Helper::name(PARAMETER_PAGES)).toString();
    if (pages.trimmed().isEmpty() || given.contains(Helper::name(PARAMETER_ASSEMBLY)) || context.documents.empty())
    {
        return given;
    }

    const PDFDocumentToolInputDocument& input = context.documents.front();
    QString error;
    const std::optional<PDFDocumentAssembly> assembly = createAssemblyOfPages(input.document.data(), QFileInfo(input.fileName).fileName(), pages, &error);
    if (!assembly)
    {
        return given;
    }

    QVariantMap completed = given;
    completed.insert(Helper::name(PARAMETER_ASSEMBLY), QVariant::fromValue(*assembly));
    if (!completed.contains(Helper::name(PARAMETER_OUTPUT)))
    {
        completed.insert(Helper::name(PARAMETER_OUTPUT), QString::fromLatin1(OUTPUT_DOCUMENT));
    }
    return completed;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolManage::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolManageHelper;

    // Not the default validation: an assembly of images and blank pages has no document
    std::vector<PDFDocumentToolParameterError> errors;
    for (const PDFDocumentToolInputDocument& document : context.documents)
    {
        if (!document.document)
        {
            errors.push_back({ QString(), tr("An input document is missing.") });
            return errors;
        }
    }

    const QString output = getOutput(parameters);
    const bool isDocument = output == QLatin1String(OUTPUT_DOCUMENT);
    const QString pages = parameters.value(Helper::name(PARAMETER_PAGES)).toString();
    if (!pages.trimmed().isEmpty())
    {
        // QB4, RB9: the pages of the open document
        QString error;
        if (!isDocument)
        {
            errors.push_back({ Helper::name(PARAMETER_OUTPUT), tr("pages changes the open document: the output must be document.") });
            return errors;
        }
        if (!createAssemblyOfPages(context.getDocument(), QString(), pages, &error))
        {
            errors.push_back({ Helper::name(PARAMETER_PAGES), error });
            return errors;
        }
    }

    const PDFDocumentAssembly assembly = getAssembly(parameters);
    if (assembly.isEmpty())
    {
        const bool hasAssemblyPath = !parameters.value(Helper::name(PARAMETER_ASSEMBLY_PATH)).toString().trimmed().isEmpty();
        errors.push_back({ Helper::name(hasAssemblyPath ? PARAMETER_ASSEMBLY_PATH : PARAMETER_ASSEMBLY), hasAssemblyPath ? tr("The assembly file has not been read.") : tr("The assembly has no pages.") });
        return errors;
    }

    // Owner 2026-11-12: an assembly over its limits (the tab of a document, which has grown by
    // another tool) is refused, never run
    QString error;
    if (!PDFDocumentAssembly::checkLimits(assembly, &error))
    {
        errors.push_back({ Helper::name(PARAMETER_ASSEMBLY), error });
        return errors;
    }

    const std::optional<Helper::Run> run = Helper::createRun(context, assembly, isDocument, &error);
    if (!run)
    {
        errors.push_back({ Helper::name(PARAMETER_ASSEMBLY), error });
        return errors;
    }

    if (isDocument)
    {
        // The target needs Assemble (its catalog row, Table 22 bit 11) and Modify for a crop (bit
        // 4, Q4); the other sources the rule Q2 of step 2 (their pages lose their restrictions)
        const PDFDocument* target = context.getDocument();
        const PDFDocumentToolCatalogEntry* info = getInfo();
        const bool isCropped = std::any_of(run->items.cbegin(), run->items.cend(), [](const PDFDocumentMerger::Item& item) { return item.source == 0 && !item.cropMargins.isNull(); });
        if ((info && !info->isPermitted(target)) ||
            (isCropped && !PDFSecurityHandler::isAllowedByRevision(target->getStorage().getSecurityHandler(), PDFSecurityHandler::Permission::Modify)))
        {
            errors.push_back({ QString(), tr("The permissions of the document do not allow this tool.") });
            return errors;
        }

        // RB2: the assembly of another version of the document is refused, never applied
        const std::vector<PDFObjectReference> targetPages = PDFDocumentAssembly::getTargetPages(assembly);
        std::vector<PDFObjectReference> pagesOfDocument;
        for (size_t i = 0; i < target->getCatalog()->getPageCount(); ++i)
        {
            pagesOfDocument.push_back(target->getCatalog()->getPage(i)->getPageReference());
        }
        if (targetPages != pagesOfDocument)
        {
            errors.push_back({ Helper::name(PARAMETER_ASSEMBLY), tr("The document has changed.") });
            return errors;
        }
    }

    // A new document is not encrypted: every PDF source permits it; This document: the other sources
    for (size_t i = isDocument ? 1 : 0; i < run->sources.size(); ++i)
    {
        const PDFDocumentMerger::Source& source = run->sources[i];
        if (source.kind != PDFDocumentMerger::Source::Kind::Document)
        {
            continue;
        }
        const PDFDocumentMerger::Permission permission = PDFDocumentMerger::getPermission(source.document.data(), source.isOwnerPasswordVerified);
        if (permission != PDFDocumentMerger::Permission::Permitted)
        {
            errors.push_back({ QString(), PDFDocumentMerger::getPermissionText(permission, source.name) });
            return errors;
        }
    }

    if (output == QLatin1String(OUTPUT_PER_GROUP) || output == QLatin1String(OUTPUT_PER_PAGE))
    {
        std::vector<PDFDocumentToolFileOutput::File> files;
        if (!createFiles(assembly, parameters, QDate(), &files, &error))
        {
            errors.push_back({ Helper::name(PDFDocumentToolFileOutput::PARAMETER_FILE_NAME), error });
        }
    }
    else if (!isDocument && output != QLatin1String(OUTPUT_ONE_PDF))
    {
        errors.push_back({ Helper::name(PARAMETER_OUTPUT), tr("Unknown output.") });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolManage::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolManageHelper;

    const PDFDocumentAssembly assembly = getAssembly(parameters);
    const QString output = getOutput(parameters);
    const bool isDocument = output == QLatin1String(OUTPUT_DOCUMENT);

    QString error;
    std::optional<Helper::Run> run = Helper::createRun(context, assembly, isDocument, &error);
    if (!run)
    {
        return PDFDocumentToolResult::createFailed(error);
    }

    // ------------------------------------------------------------ This document (4.5, arrange)
    if (isDocument)
    {
        const PDFDocumentToolInputDocument& input = context.documents.front();
        const PDFDocument* target = input.document.data();

        bool isNothingToDo = false;
        PDFDocumentToolResult result = Helper::createDocumentFacts(target, *run, &isNothingToDo);
        if (isNothingToDo)
        {
            return PDFDocumentToolResult::createNothingToDo(tr("The pages are in this order already."));
        }

        PDFDocumentMerger::Settings settings;
        settings.outline = PDFDocumentMerger::OutlineMode::NoOutline;
        settings.targetPages = PDFDocumentAssembly::getTargetPages(assembly);
        PDFDocumentMerger::Result arranged = PDFDocumentMerger::arrange(*target, run->sources, run->items, settings, control, progress);
        if (arranged.isCanceled || PDFOperationControl::isOperationCancelled(control))
        {
            return PDFDocumentToolResult::createStopped();
        }
        if (!arranged.isSuccess)
        {
            return PDFDocumentToolResult::createFailed(arranged.errorMessage);
        }

        PDFDocument document = std::move(arranged.document);

        // Q12, R10, RB5 3: no level of a certification or of a signed field lock allows a change of
        // the pages - the result is a copy (the fact is the shared one of addCertificationRemoved;
        // the rule of the callout of the panel, owner 13. 11.)
        PDFInteger certificationSignaturesRemoved = 0;
        const bool isCertified = PDFDocMDPPermissions::isCertificationRemovedBy(target, getOutputTraits(parameters));
        if (isCertified)
        {
            document = PDFDocMDPPermissions::removeCertification(document, &certificationSignaturesRemoved);
        }

        const PDFDocumentMerger::Statistics& statistics = arranged.statistics;
        Helper::addArrangementFacts(target, &document, statistics, &result);
        if (statistics.fieldsRenamed > 0)
        {
            result.facts.push_back({ QStringLiteral("fieldsRenamed"), tr("Form fields"), tr("%Ln renamed (the names were used in the document)", nullptr, int(statistics.fieldsRenamed)) });
        }
        if (statistics.signaturesRemoved > 0)
        {
            result.facts.push_back({ QStringLiteral("signatures"), tr("Signatures"), tr("%Ln of the inserted pages removed (a signature of another file is not valid here)", nullptr, int(statistics.signaturesRemoved)) });
        }
        result.data.insert(QStringLiteral("fieldsRenamed"), statistics.fieldsRenamed);
        result.data.insert(QStringLiteral("signaturesRemoved"), statistics.signaturesRemoved);
        result.data.insert(QStringLiteral("pageTreeFlattened"), statistics.isPageTreeFlattened);

        PDFDocumentToolOutputDocument changed;
        changed.kind = PDFDocumentToolOutputDocument::Kind::Changed;
        changed.inputIndex = 0;
        changed.document = PDFDocumentPointer(new PDFDocument(std::move(document)));
        changed.flags = PDFModifiedDocument::Reset;
        const QString baseName = QFileInfo(input.fileName).completeBaseName();
        changed.suggestedFileName = (baseName.isEmpty() ? QStringLiteral("document") : baseName) + QStringLiteral("_arranged.pdf");
        result.documents.push_back(std::move(changed));

        // A signed document is saved by an incremental update (the object numbers stay); a
        // certified one only as a copy (Q12)
        result.data.insert(QStringLiteral("signatureCount"), PDFForm::getSignatureCount(target));
        result.data.insert(QStringLiteral("signatureImpact"), QStringLiteral("incremental-save"));
        if (isCertified)
        {
            PDFDocMDPPermissions::addCertificationRemoved(&result, certificationSignaturesRemoved);
        }
        return result;
    }

    // ------------------------------------------------------------ the new documents (4.4)
    const std::optional<PDFImageOptimizer::Settings> imageSettings = Helper::createImageSettings(parameters);
    const PDFDocumentMerger::Settings settings = Helper::createSettings(parameters, assembly, *run);
    const bool isOnePdf = output == QLatin1String(OUTPUT_ONE_PDF);

    std::vector<PDFDocumentToolFileOutput::File> files;
    if (!isOnePdf && !createFiles(assembly, parameters, QDate::currentDate(), &files, &error))
    {
        return PDFDocumentToolResult::createFailed(error);
    }
    if (!isOnePdf && !context.fileSink)
    {
        return PDFDocumentToolResult::createFailed(PDFDocumentToolFileOutput::tr("The files have no place to be written."));
    }

    PDFProgressPhases phases(progress);
    const int mergePhase = phases.addPhase(isOnePdf ? 70 : 30, tr("Joining the pages"), true);
    const int imagesPhase = isOnePdf && imageSettings ? phases.addPhase(20, tr("Optimizing the images"), true) : -1;
    const int measurePhase = isOnePdf ? phases.addPhase(10, tr("Measuring the result"), false) : phases.addPhase(70, tr("Writing the files"), false);
    phases.start();

    phases.beginPhase(mergePhase);
    PDFDocumentMerger::Result merged = PDFDocumentMerger::merge(run->sources, run->items, settings, control, phases.getPhase(mergePhase));
    if (merged.isCanceled || PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }
    if (!merged.isSuccess)
    {
        return PDFDocumentToolResult::createFailed(merged.errorMessage);
    }

    // A source, whose copy failed (owner answer Q5 of step 2), is left out: the partial result
    std::vector<PDFDocumentToolLeftOutInput> leftOutInputs;
    std::set<size_t> leftOutSources;
    for (const PDFDocumentMerger::LeftOut& leftOut : merged.leftOut)
    {
        leftOutSources.insert(leftOut.source);
        const int input = run->inputOfSource[leftOut.source];
        PDFDocumentToolLeftOutInput leftOutInput;
        leftOutInput.inputIndex = input;
        leftOutInput.inputId = input >= 0 ? assembly.getSources()[size_t(input)].id : QString();
        leftOutInput.name = run->sources[leftOut.source].name;
        leftOutInput.pageCount = leftOut.pageCount;
        leftOutInput.reason = leftOut.reason;
        leftOutInputs.push_back(std::move(leftOutInput));
    }

    PDFDocumentToolResult result;
    const PDFDocumentMerger::Statistics& statistics = merged.statistics;
    if (isOnePdf)
    {
        PDFDocument document = std::move(merged.document);
        qint64 imagesRecompressed = 0;
        if (imageSettings)
        {
            phases.beginPhase(imagesPhase);
            std::vector<PDFImageOptimizer::ImageResult> imageResults;
            PDFImageOptimizer imageOptimizer;
            document = imageOptimizer.optimize(&document, *imageSettings, PDFImageOptimizer::ImageOverrides(), phases.getPhase(imagesPhase), nullptr, &imageResults, control, nullptr);
            if (PDFOperationControl::isOperationCancelled(control))
            {
                return PDFDocumentToolResult::createStopped();
            }
            for (const PDFImageOptimizer::ImageResult& imageResult : imageResults)
            {
                imagesRecompressed += imageResult.keptOriginal ? 0 : 1;
            }
        }

        phases.beginPhase(measurePhase);
        QByteArray writtenData = PDFDocumentToolNewDocument::write(&document, &error);
        if (writtenData.isEmpty())
        {
            return PDFDocumentToolResult::createFailed(error);
        }
        phases.finish();

        // 387:17762: Pages "17 from 3 sources", Outline "5 document parts", Size
        const int sourceCount = run->usedSourceCount - int(leftOutInputs.size());
        result.facts.push_back({ QStringLiteral("pages"), tr("Pages"), tr("%1 from %Ln sources", nullptr, sourceCount).arg(statistics.pageCount) });
        QString outlineText;
        switch (settings.outline)
        {
            case PDFDocumentMerger::OutlineMode::DocumentParts:
                outlineText = tr("%Ln document parts", nullptr, int(settings.parts.size()));
                break;
            case PDFDocumentMerger::OutlineMode::Join:
                outlineText = tr("%Ln document parts with their bookmarks", nullptr, int(settings.parts.size()));
                break;
            case PDFDocumentMerger::OutlineMode::NoOutline:
                outlineText = tr("None");
                break;
        }
        if (settings.outline != PDFDocumentMerger::OutlineMode::NoOutline && settings.parts.empty())
        {
            // One PDF source without groups keeps its outline (R3 of step 2)
            outlineText = tr("Kept from the document");
        }
        result.facts.push_back({ QStringLiteral("outline"), tr("Outline"), outlineText });
        result.facts.push_back({ QStringLiteral("size"), tr("Size"), PDFDocumentToolFormat::formatSize(writtenData.size()) });
        if (imageSettings)
        {
            result.facts.push_back({ QStringLiteral("images"), tr("Images"), tr("%Ln recompressed", nullptr, int(imagesRecompressed)) });
        }
        result.data.insert(QStringLiteral("sizeAfter"), qint64(writtenData.size()));
        result.data.insert(QStringLiteral("imagesRecompressed"), imagesRecompressed);
        PDFDocumentToolNewDocument::addToResult(&result, std::move(document), std::move(writtenData), Helper::getSuggestedFileName(assembly));
    }
    else
    {
        // The positions of the plan in the merged document (a left out source has none)
        std::vector<PDFInteger> mergedPage(run->items.size(), -1);
        PDFInteger next = 0;
        for (size_t i = 0; i < run->items.size(); ++i)
        {
            if (!leftOutSources.count(run->items[i].source))
            {
                mergedPage[i] = next++;
            }
        }
        std::vector<PDFDocumentToolFileOutput::File> mergedFiles;
        for (PDFDocumentToolFileOutput::File& file : files)
        {
            std::vector<PDFInteger> pages;
            for (const PDFInteger page : file.pages)
            {
                if (mergedPage[size_t(page)] >= 0)
                {
                    pages.push_back(mergedPage[size_t(page)]);
                }
            }
            if (!pages.empty())
            {
                file.pages = std::move(pages);
                mergedFiles.push_back(std::move(file));
            }
        }

        // Each file is a part of the merged document (its outline cut to its pages)
        PDFDocumentToolContext filesContext;
        PDFDocumentToolInputDocument mergedInput;
        mergedInput.document = PDFDocumentPointer(new PDFDocument(std::move(merged.document)));
        mergedInput.fileName = Helper::getSuggestedFileName(assembly);
        filesContext.documents.push_back(std::move(mergedInput));
        filesContext.fileSink = context.fileSink;
        phases.beginPhase(measurePhase);
        result = PDFDocumentToolFileOutput::writeFiles(filesContext, mergedFiles, imageSettings, control, progress);
        result.data.insert(QStringLiteral("output"), output);
    }

    result.leftOutInputs = std::move(leftOutInputs);
    if (!result.leftOutInputs.empty())
    {
        QStringList names;
        for (const PDFDocumentToolLeftOutInput& leftOut : result.leftOutInputs)
        {
            names << tr("%1 · %Ln pages", nullptr, int(leftOut.pageCount)).arg(leftOut.name);
        }
        result.facts.push_back({ QStringLiteral("leftOut"), tr("Left out"), names.join(QStringLiteral(", ")) });
    }
    if (statistics.fieldsRenamed > 0)
    {
        result.facts.push_back({ QStringLiteral("fieldsRenamed"), tr("Form fields"), tr("%Ln form fields renamed", nullptr, int(statistics.fieldsRenamed)) });
    }
    if (statistics.signaturesRemoved > 0)
    {
        result.facts.push_back({ QStringLiteral("signatures"), tr("Signatures"), tr("%Ln signatures removed", nullptr, int(statistics.signaturesRemoved)) });
    }
    if (statistics.structureTreeRemoved)
    {
        result.facts.push_back({ QStringLiteral("tags"), tr("Tags"), tr("Removed: not every file is tagged and taken whole") });
    }
    addConformanceFact(&result, statistics.conformanceRemoved);

    result.data.insert(QStringLiteral("pageCount"), statistics.pageCount);
    result.data.insert(QStringLiteral("sourceCount"), qint64(run->usedSourceCount));
    result.data.insert(QStringLiteral("partCount"), qint64(settings.parts.size()));
    result.data.insert(QStringLiteral("outline"), parameters.value(Helper::name(PARAMETER_OUTLINE)).toString());
    result.data.insert(QStringLiteral("optimizeImages"), parameters.value(Helper::name(PARAMETER_OPTIMIZE_IMAGES)).toString());
    result.data.insert(QStringLiteral("fieldsRenamed"), statistics.fieldsRenamed);
    result.data.insert(QStringLiteral("signaturesRemoved"), statistics.signaturesRemoved);
    result.data.insert(QStringLiteral("leftOutCount"), qint64(result.leftOutInputs.size()));
    result.data.insert(QStringLiteral("signatureCount"), 0);
    result.data.insert(QStringLiteral("signatureImpact"), QStringLiteral("none"));
    return result;
}

}   // namespace pdf
