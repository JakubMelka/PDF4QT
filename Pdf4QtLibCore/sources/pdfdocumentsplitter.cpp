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

#include "pdfdocumentsplitter.h"

#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool.h"
#include "pdfexception.h"
#include "pdfexecutionpolicy.h"
#include "pdfnewdocumentfinisher.h"
#include "pdfoperationcontrol.h"
#include "pdfprogress.h"
#include "pdfutils.h"

namespace pdf
{

/// Helpers of the splitter
class PDFDocumentSplitterHelper
{
public:
    PDFDocumentSplitterHelper() = delete;

    /// What happened with a part of a window
    struct Slot
    {
        std::optional<PDFDocumentSplitter::PartResult> result;
        QString errorMessage;
        bool isCanceled = false;
    };

    /// The attachments of the document (/Names /EmbeddedFiles, /AF of the catalog, 7.11.4,
    /// 14.13) are removed (owner answer Q6: they stay only in the first part); true, when
    /// the document had them
    static bool removeDocumentAttachments(PDFDocument* document)
    {
        PDFDocumentBuilder builder(document);
        const PDFObjectReference catalogReference = builder.getCatalogReference();
        const PDFDictionary* catalog = builder.getDictionaryFromObject(builder.getObjectByReference(catalogReference));
        if (!catalog)
        {
            return false;
        }

        bool isRemoved = false;
        PDFDictionaryBuilder newCatalog(*catalog);
        if (catalog->hasKey("AF"))
        {
            newCatalog.removeEntry("AF");
            isRemoved = true;
        }

        const PDFObject namesObject = catalog->get("Names");
        if (const PDFDictionary* names = builder.getDictionaryFromObject(namesObject); names && names->hasKey("EmbeddedFiles"))
        {
            PDFDictionaryBuilder newNames(*names);
            newNames.removeEntry("EmbeddedFiles");
            if (namesObject.isReference())
            {
                builder.setObject(namesObject.getReference(), PDFObject::createDictionary(std::move(newNames)));
            }
            else
            {
                newCatalog.setEntry(PDFInplaceOrMemoryString("Names"), PDFObject::createDictionary(std::move(newNames)));
            }
            isRemoved = true;
        }

        if (!isRemoved)
        {
            return false;
        }

        builder.setObject(catalogReference, PDFObject::createDictionary(std::move(newCatalog)));
        PDFDocument built = builder.build();
        *document = PDFDocument(PDFObjectStorage(built.getStorage()), document->getInfo()->version, document->getSourceDataHash());
        return true;
    }

    /// Makes one part (keepPages, the attachments, the finishing, the images, the bytes)
    static void makePart(const PDFDocument* document, const PDFDocumentPageOperations::PageIndex& index, size_t partIndex,
                         const PDFDocumentSplitter::Part& part, const PDFDocumentSplitter::Settings& settings,
                         const PDFOperationControl* control, Slot* slot)
    {
        try
        {
            PDFDocumentSplitter::PartResult result;
            result.partIndex = partIndex;

            PDFDocument partDocument = PDFDocumentPageOperations::keepPages(document, index, part.pages, control, &result.statistics);
            if (result.statistics.isCanceled || PDFOperationControl::isOperationCancelled(control))
            {
                slot->isCanceled = true;
                return;
            }

            if (partIndex > 0 && settings.attachmentsInFirstPartOnly)
            {
                result.attachmentsRemoved = removeDocumentAttachments(&partDocument);
            }

            PDFNewDocumentFinisher::Settings finisherSettings;
            finisherSettings.version = document->getInfo()->version;
            finisherSettings.creationTime = settings.creationTime;
            finisherSettings.isWholeSingleDocument = true;
            finisherSettings.isNewFile = true;
            finisherSettings.removeConformance = true;
            PDFNewDocumentFinisher::Result finished = PDFNewDocumentFinisher::finish(std::move(partDocument), finisherSettings, control);
            if (finished.isCanceled)
            {
                slot->isCanceled = true;
                return;
            }
            if (!finished.isSuccess)
            {
                slot->errorMessage = finished.errorMessage;
                return;
            }
            result.conformanceRemoved = finished.conformanceRemoved;
            result.statistics.signaturesRemoved += finished.signaturesRemoved;
            PDFDocument output = std::move(finished.document);

            if (settings.imageSettings)
            {
                std::vector<PDFImageOptimizer::ImageResult> imageResults;
                PDFImageOptimizer imageOptimizer;
                output = imageOptimizer.optimize(&output, *settings.imageSettings, PDFImageOptimizer::ImageOverrides(), nullptr, nullptr, &imageResults, control, nullptr);
                if (PDFOperationControl::isOperationCancelled(control))
                {
                    slot->isCanceled = true;
                    return;
                }
                for (const PDFImageOptimizer::ImageResult& imageResult : imageResults)
                {
                    result.imagesRecompressed += imageResult.keptOriginal ? 0 : 1;
                }
            }

            result.pageCount = PDFInteger(output.getCatalog()->getPageCount());
            result.data = PDFDocumentTool::writeDocumentData(&output, &slot->errorMessage);
            if (result.data.isEmpty())
            {
                return;
            }
            slot->result = std::move(result);
        }
        catch (const PDFException& exception)
        {
            slot->errorMessage = exception.getMessage();
        }
    }
};

PDFDocumentSplitter::Result PDFDocumentSplitter::split(const PDFDocument* document, const std::vector<Part>& parts, const Settings& settings,
                                                       const PDFOperationControl* control, PDFProgressSink* progress, const Receiver& receiver)
{
    using Helper = PDFDocumentSplitterHelper;

    Result result;
    if (!document || !receiver)
    {
        result.errorMessage = tr("There is no document.");
        return result;
    }
    if (parts.empty())
    {
        result.errorMessage = tr("There is nothing to split.");
        return result;
    }
    if (parts.size() > MAXIMUM_PART_COUNT)
    {
        result.errorMessage = tr("The document would be split into more than %Ln files.", nullptr, int(MAXIMUM_PART_COUNT));
        return result;
    }

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    for (const Part& part : parts)
    {
        if (part.pages.empty())
        {
            result.errorMessage = tr("A file would have no page.");
            return result;
        }
        for (const PDFInteger page : part.pages)
        {
            if (page < 0 || page >= pageCount)
            {
                result.errorMessage = tr("Page %1 does not exist.").arg(page + 1);
                return result;
            }
        }
    }

    std::shared_ptr<const PDFDocumentPageOperations::PageIndex> index;
    try
    {
        index = PDFDocumentPageOperations::createPageIndex(document, control);
    }
    catch (const PDFException& exception)
    {
        result.errorMessage = exception.getMessage();
        return result;
    }
    if (!index || PDFOperationControl::isOperationCancelled(control))
    {
        result.isCanceled = true;
        return result;
    }

    // The window: the threads less one, so the parallel loops of the finishing of each part
    // always have a free thread (a loop waits for its tasks in the same pool); the image
    // optimization nests its loops deeper, so its parts are made one by one
    size_t windowSize = 1;
    if (!settings.imageSettings && PDFExecutionPolicy::isParallelizing(PDFExecutionPolicy::Scope::Page))
    {
        windowSize = size_t(qMax(1, PDFExecutionPolicy::getMaxThreadCount(PDFExecutionPolicy::Scope::Page) - 1));
    }
    const bool isParallel = windowSize > 1 && PDFExecutionPolicy::getMaxThreadCount(PDFExecutionPolicy::Scope::Page) > 1;

    if (progress)
    {
        progress->start(parts.size(), ProgressStartupInfo());
    }

    for (size_t start = 0; start < parts.size(); start += windowSize)
    {
        if (PDFOperationControl::isOperationCancelled(control))
        {
            result.isCanceled = true;
            break;
        }

        const size_t end = qMin(parts.size(), start + windowSize);
        std::vector<Helper::Slot> windowSlots(end - start);
        if (isParallel && end - start > 1)
        {
            PDFIntegerRange<size_t> range(start, end);
            auto makeSlot = [&](size_t partIndex)
            {
                Helper::makePart(document, *index, partIndex, parts[partIndex], settings, control, &windowSlots[partIndex - start]);
            };
            PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, range.begin(), range.end(), makeSlot);
        }
        else
        {
            for (size_t partIndex = start; partIndex < end; ++partIndex)
            {
                Helper::makePart(document, *index, partIndex, parts[partIndex], settings, control, &windowSlots[partIndex - start]);
            }
        }

        // The parts of the window go to the receiver in their order, then they are released
        for (Helper::Slot& slot : windowSlots)
        {
            if (slot.isCanceled || PDFOperationControl::isOperationCancelled(control))
            {
                result.isCanceled = true;
                break;
            }
            if (!slot.result)
            {
                result.errorMessage = slot.errorMessage.isEmpty() ? tr("A file cannot be made.") : slot.errorMessage;
                break;
            }

            const bool isAccepted = receiver(std::move(*slot.result));
            slot.result.reset();
            if (!isAccepted)
            {
                result.isStopped = true;
                break;
            }
            ++result.receivedCount;
            if (progress)
            {
                progress->step();
            }
        }

        if (result.isCanceled || result.isStopped || !result.errorMessage.isEmpty())
        {
            break;
        }
    }

    if (progress)
    {
        progress->finish();
    }

    result.isSuccess = !result.isCanceled && !result.isStopped && result.errorMessage.isEmpty() && result.receivedCount == parts.size();
    return result;
}

}   // namespace pdf
