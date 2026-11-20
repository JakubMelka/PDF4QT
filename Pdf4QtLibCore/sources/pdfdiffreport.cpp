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

#include "pdfdiffreport.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumenttool.h"
#include "pdfoperationcontrol.h"

#include <QFileInfo>
#include <QPolygonF>

#include "pdfdbgheap.h"

namespace pdf
{

/// The parts of the documents of Compare
class PDFDiffReportBuilderHelper
{
public:
    PDFDiffReportBuilderHelper() = delete;

    /// The pages of a view: the pairs of the page sequence of the result in its order (an empty
    /// sequence - no comparison yet - pairs the pages by their positions). A page of the sequence
    /// outside of its document is an error (a result of other documents).
    static bool createPages(PDFDiffReportView view,
                            const PDFDocument* original,
                            const PDFDocument* revised,
                            const PDFDiffResult& result,
                            std::vector<PDFDiffCombinedPage>* pages,
                            QString* errorMessage)
    {
        const PDFInteger originalPageCount = PDFInteger(original->getCatalog()->getPageCount());
        const PDFInteger revisedPageCount = PDFInteger(revised->getCatalog()->getPageCount());

        PDFDiffResult::PageSequence sequence = result.getPageSequence();
        if (sequence.empty())
        {
            const PDFInteger count = qMax(originalPageCount, revisedPageCount);
            for (PDFInteger i = 0; i < count; ++i)
            {
                PDFDiffResult::PageSequenceItem item;
                item.leftPage = i < originalPageCount ? i : -1;
                item.rightPage = i < revisedPageCount ? i : -1;
                sequence.push_back(item);
            }
        }

        const bool isOriginalShown = view != PDFDiffReportView::Revised;
        const bool isRevisedShown = view != PDFDiffReportView::Original;

        for (const PDFDiffResult::PageSequenceItem& item : sequence)
        {
            if (item.leftPage < -1 || item.leftPage >= originalPageCount || item.rightPage < -1 || item.rightPage >= revisedPageCount)
            {
                *errorMessage = PDFDiffReportBuilder::tr("The result of the comparison does not belong to these documents.");
                return false;
            }

            if (isOriginalShown && item.leftPage != -1)
            {
                pages->push_back({ item.leftPage, -1 });
            }
            if (isRevisedShown && item.rightPage != -1)
            {
                pages->push_back({ -1, item.rightPage });
            }
        }

        if (pages->empty())
        {
            *errorMessage = PDFDiffReportBuilder::tr("There is no page to show.");
            return false;
        }

        if (qint64(pages->size()) > PDFDocumentMerger::MAXIMUM_PAGE_COUNT)
        {
            *errorMessage = PDFDiffReportBuilder::tr("The documents have too many pages to be shown together.");
            return false;
        }

        return true;
    }

    /// The pages of the view merged by the merger of Merge (one implementation, PDFDocumentMerger)
    static std::optional<PDFDocument> merge(const PDFDocumentPointer& original,
                                            const PDFDocumentPointer& revised,
                                            const QString& originalName,
                                            const QString& revisedName,
                                            bool isOriginalOwnerPasswordVerified,
                                            bool isRevisedOwnerPasswordVerified,
                                            bool checkPermissions,
                                            const std::vector<PDFDiffCombinedPage>& pages,
                                            const PDFOperationControl* control,
                                            QString* errorMessage)
    {
        std::vector<PDFDocumentMerger::Source> sources;
        sources.push_back(PDFDocumentMerger::Source::createDocument(original, originalName));
        sources.back().isOwnerPasswordVerified = isOriginalOwnerPasswordVerified;
        sources.push_back(PDFDocumentMerger::Source::createDocument(revised, revisedName));
        sources.back().isOwnerPasswordVerified = isRevisedOwnerPasswordVerified;

        std::vector<PDFDocumentMerger::Item> items;
        items.reserve(pages.size());
        for (const PDFDiffCombinedPage& page : pages)
        {
            PDFDocumentMerger::Item item;
            item.source = page.originalPage != -1 ? 0 : 1;
            item.pageIndex = page.originalPage != -1 ? page.originalPage : page.revisedPage;
            items.push_back(item);
        }

        PDFDocumentMerger::Settings settings;
        settings.outline = PDFDocumentMerger::OutlineMode::NoOutline;
        settings.checkPermissions = checkPermissions;

        PDFDocumentMerger::Result merged = PDFDocumentMerger::merge(sources, items, settings, control, nullptr);
        if (merged.isCanceled || PDFOperationControl::isOperationCanceled(control))
        {
            *errorMessage = PDFDiffReportBuilder::tr("The operation has been canceled.");
            return std::nullopt;
        }

        // B162: a failed merge, or an input left out of it, is an error - never an empty document
        // with the page sequence of the result
        if (!merged.isSuccess)
        {
            *errorMessage = merged.errorMessage.isEmpty() ? PDFDiffReportBuilder::tr("The pages of the documents cannot be joined.") : merged.errorMessage;
            return std::nullopt;
        }
        if (!merged.leftOut.empty())
        {
            *errorMessage = PDFDiffReportBuilder::tr("The pages of the documents cannot be joined: %1").arg(merged.leftOut.front().reason);
            return std::nullopt;
        }
        if (merged.document.getCatalog()->getPageCount() != pages.size())
        {
            *errorMessage = PDFDiffReportBuilder::tr("The pages of the documents cannot be joined.");
            return std::nullopt;
        }

        return std::move(merged.document);
    }

    /// The color of a difference (the priority of the old Diff: replaced, removed, added, moved)
    static QColor getColor(const PDFDiffResult& result, size_t index, const PDFDiffReportColors& colors)
    {
        if (result.isReplaceDifference(index))
        {
            return colors.replaced;
        }
        if (result.isRemoveDifference(index))
        {
            return colors.removed;
        }
        if (result.isAddDifference(index))
        {
            return colors.added;
        }
        return colors.moved;
    }

    /// The display name of an input ("Report_v1.pdf"; "Original" / "Revised" without a file)
    static QString getName(const PDFDocumentToolInputDocument& input, bool isOriginal)
    {
        const QString name = QFileInfo(input.fileName).fileName();
        if (!name.isEmpty())
        {
            return name;
        }
        return isOriginal ? PDFDiffReportBuilder::tr("Original") : PDFDiffReportBuilder::tr("Revised");
    }
};

PDFDiffCombinedDocument PDFDiffReportBuilder::createCombinedDocument(PDFDiffReportView view,
                                                                     const PDFDocumentPointer& original,
                                                                     const PDFDocumentPointer& revised,
                                                                     const PDFDiffResult& result,
                                                                     const PDFOperationControl* control)
{
    using Helper = PDFDiffReportBuilderHelper;

    PDFDiffCombinedDocument combined;
    if (!original || !revised)
    {
        combined.errorMessage = tr("A document of the comparison is missing.");
        return combined;
    }

    std::vector<PDFDiffCombinedPage> pages;
    if (!Helper::createPages(view, original.data(), revised.data(), result, &pages, &combined.errorMessage))
    {
        return combined;
    }

    // The document is shown in the tab only, it is never given to the user as a file: the
    // permissions of the inputs are not checked (the report is, createReport)
    std::optional<PDFDocument> document = Helper::merge(original, revised, tr("Original"), tr("Revised"), false, false, false, pages, control, &combined.errorMessage);
    if (!document)
    {
        return combined;
    }

    combined.document = PDFDocumentPointer(new PDFDocument(std::move(*document)));
    combined.pages = std::move(pages);
    combined.errorMessage.clear();
    return combined;
}

std::optional<PDFDocument> PDFDiffReportBuilder::createReport(const PDFDocumentToolInputDocument& original,
                                                              const PDFDocumentToolInputDocument& revised,
                                                              const PDFDiffResult& result,
                                                              const PDFDiffReportSettings& settings,
                                                              const PDFOperationControl* control,
                                                              QString* errorMessage)
{
    using Helper = PDFDiffReportBuilderHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (!original.document || !revised.document)
    {
        error = tr("A document of the comparison is missing.");
        return std::nullopt;
    }

    // B161, B210: the report is a new document of the pages of the inputs, which is not encrypted -
    // the rule of Merge (owner answer Q2 of step 2, Q7.2 of step 7): a restricted input needs its
    // owner password, for every caller
    const std::array<const PDFDocumentToolInputDocument*, 2> inputs = { &original, &revised };
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        const PDFDocumentToolInputDocument* input = inputs[i];
        const PDFDocumentMerger::Permission permission = PDFDocumentMerger::getPermission(input->document.data(), input->isOwnerPasswordVerified);
        if (permission != PDFDocumentMerger::Permission::Permitted)
        {
            error = PDFDocumentMerger::getPermissionText(permission, Helper::getName(*input, i == 0));
            return std::nullopt;
        }
    }

    std::vector<PDFDiffCombinedPage> pages;
    if (!Helper::createPages(settings.view, original.document.data(), revised.document.data(), result, &pages, &error))
    {
        return std::nullopt;
    }

    // The pages of the documents in the report (each page is there once at most)
    std::vector<PDFInteger> originalToReport(original.document->getCatalog()->getPageCount(), -1);
    std::vector<PDFInteger> revisedToReport(revised.document->getCatalog()->getPageCount(), -1);
    for (size_t i = 0; i < pages.size(); ++i)
    {
        if (pages[i].originalPage != -1)
        {
            originalToReport[size_t(pages[i].originalPage)] = PDFInteger(i);
        }
        if (pages[i].revisedPage != -1)
        {
            revisedToReport[size_t(pages[i].revisedPage)] = PDFInteger(i);
        }
    }

    // The annotations: a polygon per rectangle of a difference on a page of the report
    struct Mark
    {
        PDFInteger reportPage = -1;
        QRectF rectangle;
        size_t difference = 0;
    };
    std::vector<Mark> marks;

    auto addMarks = [&marks](size_t difference, const std::pair<PDFDiffResult::RectInfosIt, PDFDiffResult::RectInfosIt>& rectangles, const std::vector<PDFInteger>& pageMap)
    {
        for (auto it = rectangles.first; it != rectangles.second; ++it)
        {
            const PDFInteger page = it->first;
            if (page >= 0 && size_t(page) < pageMap.size() && pageMap[size_t(page)] != -1 && it->second.isValid())
            {
                marks.push_back({ pageMap[size_t(page)], it->second, difference });
            }
        }
    };

    for (size_t i = 0; i < result.getDifferencesCount(); ++i)
    {
        addMarks(i, result.getLeftRectangles(i), originalToReport);
        addMarks(i, result.getRightRectangles(i), revisedToReport);
        if (qint64(marks.size()) > MAXIMUM_ANNOTATION_COUNT)
        {
            error = tr("The report would have more than %1 marked differences. Show fewer differences by the filter.").arg(MAXIMUM_ANNOTATION_COUNT);
            return std::nullopt;
        }
    }

    std::optional<PDFDocument> combined = Helper::merge(original.document, revised.document, Helper::getName(original, true), Helper::getName(revised, false),
                                                        original.isOwnerPasswordVerified, revised.isOwnerPasswordVerified, true, pages, control, &error);
    if (!combined)
    {
        return std::nullopt;
    }

    PDFDocumentBuilder builder(&*combined);
    const PDFCatalog* catalog = combined->getCatalog();

    // A page, whose /Annots is a reference, gets the direct array first (the polygons are appended
    // to the array of the page; an indirect array would be replaced, the annotations of the input lost)
    std::vector<char> isPageMarked(pages.size(), 0);
    for (const Mark& mark : marks)
    {
        isPageMarked[size_t(mark.reportPage)] = 1;
    }
    for (size_t i = 0; i < pages.size(); ++i)
    {
        if (!isPageMarked[i])
        {
            continue;
        }

        const PDFObjectReference pageReference = catalog->getPage(i)->getPageReference();
        const PDFDictionary* pageDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(pageReference));
        if (pageDictionary && pageDictionary->get("Annots").isReference())
        {
            const PDFObject& annotations = builder.getObject(pageDictionary->get("Annots"));
            PDFObjectFactory factory;
            factory.beginDictionary();
            factory.beginDictionaryItem("Annots");
            if (annotations.isArray())
            {
                factory << annotations;
            }
            else
            {
                factory << WrapEmptyArray();
            }
            factory.endDictionaryItem();
            factory.endDictionary();
            builder.mergeTo(pageReference, factory.takeObject());
        }
    }

    const QString subject = tr("Difference");
    const PDFReal opacity = qBound(0.0, settings.opacity, 1.0);
    for (size_t i = 0; i < marks.size(); ++i)
    {
        // The cancellation per page of the marks (they are in the order of the differences)
        if ((i % 256) == 0 && PDFOperationControl::isOperationCanceled(control))
        {
            error = tr("The operation has been canceled.");
            return std::nullopt;
        }

        const Mark& mark = marks[i];
        const QColor color = Helper::getColor(result, mark.difference, settings.colors);
        const PDFObjectReference pageReference = catalog->getPage(size_t(mark.reportPage))->getPageReference();

        QPolygonF polygon;
        polygon << mark.rectangle.topLeft() << mark.rectangle.topRight() << mark.rectangle.bottomRight() << mark.rectangle.bottomLeft();

        // /Contents: the description of the difference (PDF/UA, the alternate description of the
        // annotation); the rule of Merge holds, so its text may be in the report
        const PDFObjectReference annotation = builder.createAnnotationPolygon(pageReference, polygon, 1.0, color, color, settings.author, subject, result.getMessage(mark.difference));
        builder.setAnnotationOpacity(annotation, opacity);
        builder.updateAnnotationAppearanceStreams(annotation);
    }

    return builder.build();
}

}   // namespace pdf
