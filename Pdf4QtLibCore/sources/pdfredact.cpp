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

#include "pdfredact.h"
#include "pdfannotation.h"
#include "pdfexecutionpolicy.h"
#include "pdfpainter.h"
#include "pdfdocumentbuilder.h"
#include "pdfoperationcontrol.h"
#include "pdfoptimizer.h"
#include "pdfoutline.h"
#include "pdfpathboolean.h"
#include "pdfprogress.h"
#include "pdfstreamfilters.h"
#include "pdfutils.h"

#include <QThread>

#include <array>
#include <cmath>

#include "pdfdbgheap.h"

namespace pdf
{

/// The parts of a redaction, which work on plain values (PDFRedact)
class PDFRedactHelper
{
public:
    /// The size of a page without a valid media box (R6-17): US Letter, the default of the readers
    static constexpr PDFReal DEFAULT_PAGE_WIDTH = 612.0;
    static constexpr PDFReal DEFAULT_PAGE_HEIGHT = 792.0;

    /// The keys of /Info, which are carried over by the options (ISO 32000-2, Table 46): the title
    /// by CopyTitle, the rest by CopyMetadata (R6-6: only their direct values)
    static constexpr std::array<const char*, 7> METADATA_STRING_KEYS = { "Author", "Subject", "Keywords", "Creator", "Producer", "CreationDate", "ModDate" };

    /// Pages of a chunk of the parallel part of perform per thread (the compiled pages of a chunk are
    /// held in the memory, until they are written)
    static constexpr int PAGES_PER_THREAD = 2;

    /// The rebuilt page of the parallel part of perform
    struct Page
    {
        QRectF box;                                     ///< The new media box
        QPainterPath area;                              ///< The redacted area within the box
        PDFPrecompiledPage compiledPage;                ///< The redacted content (when isCompiled)
        bool isCompiled = false;                        ///< The page has a content (not a whole page, not a page without a size)
        QByteArray fill;                                ///< The content stream of the fill of the area (empty = none)
        std::vector<PDFRedact::PageProblem> problems;
    };

    /// The matrix of the page to the painter of PDFContentStreamBuilder: the content stream has the
    /// coordinates of the page - the painter in the Qt system (y down) is flipped back by QPdfWriter,
    /// so it is any flip of y (it cancels out)
    static QTransform getPageMatrix(const QRectF& box)
    {
        return QTransform(1.0, 0.0, 0.0, -1.0, 0.0, box.height());
    }

    /// QPdfWriter writes the alpha of only 0 and 255 of an image as an explicit mask (/Mask, ISO
    /// 32000-2, 8.9.6.3) without /BitsPerComponent - optional of an image mask (Table 87: 1, if it is
    /// present). It is written explicitly, so every reader takes the mask (the reader of PDF4QT
    /// refused it and so the check of the copy could not read the image)
    static void completeImageMasks(PDFDocumentBuilder* builder, const PDFObject& resources)
    {
        const PDFDictionary* resourcesDictionary = builder->getDictionaryFromObject(builder->getObject(resources));
        const PDFDictionary* xobjects = resourcesDictionary ? builder->getDictionaryFromObject(builder->getObject(resourcesDictionary->get("XObject"))) : nullptr;
        for (size_t i = 0; xobjects && i < xobjects->getCount(); ++i)
        {
            const PDFObject& image = builder->getObject(xobjects->getValue(i));
            const PDFObject& maskReference = image.isStream() ? image.getStream()->getDictionary()->get("Mask") : PDFObject();
            if (!maskReference.isReference())
            {
                continue;
            }

            const PDFObject mask = builder->getObjectByReference(maskReference.getReference());
            const PDFDictionary* maskDictionary = mask.isStream() ? mask.getStream()->getDictionary() : nullptr;
            const PDFObject& isImageMask = maskDictionary ? builder->getObject(maskDictionary->get("ImageMask")) : PDFObject();
            if (isImageMask.isBool() && isImageMask.getBool() && !maskDictionary->hasKey("BitsPerComponent"))
            {
                PDFDictionaryBuilder completedDictionary(*maskDictionary);
                completedDictionary.setEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(1));
                builder->setObject(maskReference.getReference(), PDFObject::createStream(PDFStream(std::move(completedDictionary), QByteArray(*mask.getStream()->getContent()))));
            }
        }
    }

    /// Appends the rebuilt page to the document (the calling thread of perform): its content drawn
    /// into a content stream, the fill as its last content stream (R6-5), the rotation, the unit and
    /// the boxes of the page within its new media box
    static PDFObjectReference appendPage(PDFDocumentBuilder* builder, const PDFPage* page, Page* rebuiltPage)
    {
        const PDFObjectReference newPageReference = builder->appendPage(rebuiltPage->box);

        PDFArrayBuilder contents;
        PDFObject resources = PDFObject::createDictionary(PDFDictionaryBuilder());
        if (rebuiltPage->isCompiled)
        {
            PDFContentStreamBuilder contentStreamBuilder(rebuiltPage->box.size(), PDFContentStreamBuilder::CoordinateSystem::Qt);
            QPainter* painter = contentStreamBuilder.begin();

            // PDFPrecompiledPage::draw sets the world matrix of the painter (it does not combine it): the
            // correction of the height of the page of QPdfWriter (B149), which begin has put into the matrix
            // of the painter, is a part of the matrix of the page. Without it the content moved against the
            // areas by the difference of the heights (2 pt of a page near a standard size, up to 0.5 pt of
            // the rounding) and was left under the areas (ASan D2 of step 6 of phase 2)
            rebuiltPage->compiledPage.draw(painter, QRectF(), getPageMatrix(rebuiltPage->box) * painter->worldTransform(), PDFRenderer::None, 1.0);
            const PDFContentStreamBuilder::ContentStream content = contentStreamBuilder.end(painter);
            rebuiltPage->compiledPage = PDFPrecompiledPage();

            // The content of QPdfWriter leaves its matrix of the page (the flip of y) in the graphics state
            // at its end, and the state is carried over to the next content stream of the page (ISO
            // 32000-2, 7.8.2, 8.4.2): the content is put between q and Q, so the fill of the areas is
            // drawn in the default user space of the page, where its coordinates are
            const PDFObject& writtenContent = content.document.getObject(content.contents);
            if (content.document.getCatalog()->getPageCount() > 0 && writtenContent.isStream())
            {
                // A page larger than 14 400 points: QPdfWriter (PDF 1.6) writes its content in units of
                // its /UserUnit (Table 31), which the rebuilt page does not take over - the content is
                // scaled back to points, else it would shrink towards the origin of the page
                const PDFReal writerUserUnit = content.document.getCatalog()->getPage(0)->getUserUnit();
                QByteArray prefix = "q\n";
                if (std::isfinite(writerUserUnit) && writerUserUnit > 0.0 && writerUserUnit != 1.0)
                {
                    // The full precision (a unit of 1.3889 instead of 20000 / 14400 would move a point at
                    // 20 000 points by more than a point); the unit is at most 75 000, never an exponent
                    const QByteArray unit = QByteArray::number(writerUserUnit, 'f', 12);
                    prefix += unit + " 0 0 " + unit + " 0 0 cm\n";
                }

                const std::vector<PDFObject> copiedObjects = builder->copyFrom({ content.resources }, content.document.getStorage(), true);
                if (copiedObjects.size() == 1)
                {
                    resources = copiedObjects[0];
                    contents.appendItem(addContentStream(builder, prefix + content.document.getDecodedStream(writtenContent.getStream()) + "\nQ\n"));
                    completeImageMasks(builder, resources);
                }
            }
        }
        if (contents.getCount() == 0)
        {
            // An empty content (a whole page, a page without a valid size); /Length is required (7.3.8.2)
            PDFDictionaryBuilder emptyDictionary;
            emptyDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(0));
            contents.appendItem(PDFObject::createReference(builder->addObject(PDFObject::createStream(PDFStream(std::move(emptyDictionary), QByteArray())))));
        }

        if (!rebuiltPage->fill.isEmpty())
        {
            // R6-5: the fill is the last content stream of the page
            contents.appendItem(addContentStream(builder, rebuiltPage->fill));
        }

        PDFObjectFactory pageUpdate;
        pageUpdate.beginDictionary();
        pageUpdate.beginDictionaryItem("Contents");
        pageUpdate << PDFObject::createArray(std::move(contents));
        pageUpdate.endDictionaryItem();
        pageUpdate.beginDictionaryItem("Resources");
        pageUpdate << resources;
        pageUpdate.endDictionaryItem();
        pageUpdate.endDictionary();
        builder->mergeTo(newPageReference, pageUpdate.takeObject());

        if (page)
        {
            builder->setPageRotation(newPageReference, page->getPageRotation());

            // B148: the size of a unit of the default user space (Table 31)
            if (page->getUserUnit() != 1.0)
            {
                builder->setPageUserUnit(newPageReference, page->getUserUnit());
            }

            const QRectF bleedBox = getBoxWithin(page->getBleedBox(), rebuiltPage->box);
            const QRectF trimBox = getBoxWithin(page->getTrimBox(), rebuiltPage->box);
            const QRectF artBox = getBoxWithin(page->getArtBox(), rebuiltPage->box);
            if (bleedBox.isValid())
            {
                builder->setPageBleedBox(newPageReference, bleedBox);
            }
            if (trimBox.isValid())
            {
                builder->setPageTrimBox(newPageReference, trimBox);
            }
            if (artBox.isValid())
            {
                builder->setPageArtBox(newPageReference, artBox);
            }
        }

        return newPageReference;
    }

    /// A new content stream of the data (compressed by Flate), the reference to it
    static PDFObject addContentStream(PDFDocumentBuilder* builder, const QByteArray& data)
    {
        QByteArray compressedData = PDFFlateDecodeFilter::compress(data);
        PDFArrayBuilder filters;
        filters.appendItem(PDFObject::createName("FlateDecode"));
        PDFDictionaryBuilder streamDictionary;
        streamDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressedData.size()));
        streamDictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createArray(std::move(filters)));
        return PDFObject::createReference(builder->addObject(PDFObject::createStream(PDFStream(std::move(streamDictionary), std::move(compressedData)))));
    }

    /// The area within the box (a region of the non-zero rule): each polygon of the area clipped by the
    /// box in its own coordinates (Sutherland-Hodgman, the box is convex). The areas are clipped before
    /// they are united: the union normalizes its working space to its bounding box, so an area far away
    /// from the page (a coordinate of 1e12 of MCP or of /QuadPoints) would make the grid of the union
    /// coarser than the page and every area of the page would vanish - nothing would be redacted.
    static QPainterPath clipToBox(const QPainterPath& area, const QRectF& box)
    {
        QPainterPath result;
        result.setFillRule(Qt::WindingFill);

        for (const QPolygonF& polygon : area.toSubpathPolygons())
        {
            QPolygonF clipped = polygon;
            for (int edge = 0; edge < 4 && !clipped.isEmpty(); ++edge)
            {
                // The inside of an edge: the signed distance from it is not negative
                auto distance = [edge, &box](const QPointF& point) -> PDFReal
                {
                    switch (edge)
                    {
                        case 0:
                            return point.x() - box.left();
                        case 1:
                            return box.right() - point.x();
                        case 2:
                            return point.y() - box.top();
                        default:
                            return box.bottom() - point.y();
                    }
                };

                const QPolygonF input = clipped;
                clipped.clear();
                for (qsizetype i = 0; i < input.size(); ++i)
                {
                    const QPointF& current = input[i];
                    const QPointF& previous = input[(i + input.size() - 1) % input.size()];
                    const PDFReal currentDistance = distance(current);
                    const PDFReal previousDistance = distance(previous);
                    if (!std::isfinite(currentDistance) || !std::isfinite(previousDistance))
                    {
                        // A polygon of a non-finite point covers nothing
                        clipped.clear();
                        break;
                    }

                    if ((currentDistance >= 0.0) != (previousDistance >= 0.0))
                    {
                        const PDFReal t = previousDistance / (previousDistance - currentDistance);
                        clipped << previous + (current - previous) * t;
                    }
                    if (currentDistance >= 0.0)
                    {
                        clipped << current;
                    }
                }
            }

            if (clipped.size() >= 3)
            {
                result.addPolygon(clipped);
                result.closeSubpath();
            }
        }

        return result;
    }

    /// A number of a content stream (at most 4 decimal places, no trailing zero)
    static QByteArray formatNumber(PDFReal value)
    {
        if (!std::isfinite(value) || std::abs(value) < 0.00005)
        {
            return "0";
        }

        QByteArray text = QByteArray::number(value, 'f', 4);
        while (text.endsWith('0'))
        {
            text.chop(1);
        }
        if (text.endsWith('.'))
        {
            text.chop(1);
        }
        return text;
    }

    /// The content stream of the fill of the area (R6-5: the own last content stream of the page,
    /// so the check renders the page without it): the color in DeviceRGB, the area filled by the
    /// non-zero rule (its outer boundaries and holes are oriented by the union, 8.5.3.3.2)
    static QByteArray createFillContent(const QPainterPath& area, QColor color)
    {
        const QList<QPolygonF> polygons = area.toSubpathPolygons();
        if (polygons.isEmpty())
        {
            return QByteArray();
        }

        QByteArray content = "q\n";
        content += formatNumber(color.redF()) + ' ' + formatNumber(color.greenF()) + ' ' + formatNumber(color.blueF()) + " rg\n";
        for (const QPolygonF& polygon : polygons)
        {
            if (polygon.size() < 3)
            {
                continue;
            }

            for (int i = 0; i < polygon.size(); ++i)
            {
                content += formatNumber(polygon[i].x()) + ' ' + formatNumber(polygon[i].y()) + (i == 0 ? " m\n" : " l\n");
            }
            content += "h\n";
        }
        content += "f\nQ\n";
        return content;
    }

    /// The rectangle as a path of the non-zero rule
    static QPainterPath createRectanglePath(const QRectF& rectangle)
    {
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        path.addRect(rectangle);
        return path;
    }

    /// A box of the page (/BleedBox, /TrimBox, /ArtBox) within the new media box (Table 31: they lie
    /// within the media box); an invalid rectangle, when it has nothing in it
    static QRectF getBoxWithin(const QRectF& box, const QRectF& mediaBox)
    {
        const QRectF normalized = box.normalized();
        if (!normalized.isValid() || normalized.isEmpty())
        {
            return QRectF();
        }
        const QRectF intersected = normalized.intersected(mediaBox);
        return intersected.isValid() && !intersected.isEmpty() ? intersected : QRectF();
    }

    /// /Info of the copy (R6-6): only the direct values of the standard keys - a string after its
    /// reference is resolved, /Trapped a name - so no other object of the document is carried over
    /// (a reference to a page, a content stream or an image under an area). The title only with
    /// CopyTitle, the rest only with CopyMetadata.
    static void copyDocumentInfo(PDFDocumentBuilder* builder, const PDFDocument* document, PDFRedact::Options options)
    {
        if (!options.testFlag(PDFRedact::CopyTitle) && !options.testFlag(PDFRedact::CopyMetadata))
        {
            return;
        }

        const PDFDictionary* info = document->getDictionaryFromObject(document->getTrailerDictionary()->get("Info"));
        if (!info)
        {
            return;
        }

        PDFObjectFactory factory;
        factory.beginDictionary();

        auto copyString = [&factory, document, info](const char* key)
        {
            const PDFObject& value = document->getObject(info->get(key));
            if (value.isString())
            {
                factory.beginDictionaryItem(key);
                factory << PDFObject::createString(value.getString());
                factory.endDictionaryItem();
            }
        };

        if (options.testFlag(PDFRedact::CopyTitle))
        {
            copyString("Title");
        }

        if (options.testFlag(PDFRedact::CopyMetadata))
        {
            for (const char* key : METADATA_STRING_KEYS)
            {
                copyString(key);
            }

            const PDFObject& trapped = document->getObject(info->get("Trapped"));
            if (trapped.isName())
            {
                factory.beginDictionaryItem("Trapped");
                factory << WrapName(trapped.getString());
                factory.endDictionaryItem();
            }
        }

        factory.endDictionary();
        builder->updateDocumentInfo(factory.takeObject());
    }

    /// The outline of the copy: a destination of a page of the document leads to its new page; any
    /// other action (a page unknown to the copy, GoToDp of a document part, a link elsewhere) is
    /// dropped (R6-30), the item keeps its title
    static void copyOutline(PDFDocumentBuilder* builder, const PDFDocument* document, const std::map<PDFObjectReference, PDFObjectReference>& pageMap)
    {
        const QSharedPointer<PDFOutlineItem> outlineRoot = document->getCatalog()->getOutlineRootPtr();
        if (!outlineRoot || outlineRoot->getChildCount() == 0)
        {
            return;
        }

        const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
        QSharedPointer<PDFOutlineItem> outline = outlineRoot->clone();
        auto mapAction = [document, &pageMap, pageCount](PDFOutlineItem* item)
        {
            PDFActionGoTo* action = dynamic_cast<PDFActionGoTo*>(item->getAction());
            if (!action)
            {
                item->setAction(PDFActionPtr());
                return;
            }

            PDFDestination destination = action->getDestination();
            if (destination.isNamedDestination())
            {
                const PDFDestination* namedDestination = document->getCatalog()->getNamedDestination(destination.getName());
                destination = namedDestination ? *namedDestination : PDFDestination();
            }

            if (destination.getDestinationType() == DestinationType::Invalid || destination.isNamedDestination())
            {
                item->setAction(PDFActionPtr());
                return;
            }

            if (destination.getPageReference().isValid())
            {
                auto it = pageMap.find(destination.getPageReference());
                if (it == pageMap.cend())
                {
                    item->setAction(PDFActionPtr());
                    return;
                }
                destination.setPageReference(it->second);
            }
            else
            {
                // ISO 32000-2, 12.3.2.2: the page of a destination in the document is an indirect reference
                // to the page object (an integer page number only of a remote document) - a destination by
                // a page number (a damaged file) gets the reference of the page in the copy (RS6SEC Low)
                const PDFInteger pageIndex = destination.getPageIndex();
                const PDFPage* page = pageIndex >= 0 && pageIndex < pageCount ? document->getCatalog()->getPage(size_t(pageIndex)) : nullptr;
                auto it = page ? pageMap.find(page->getPageReference()) : pageMap.cend();
                if (it == pageMap.cend())
                {
                    item->setAction(PDFActionPtr());
                    return;
                }
                destination.setPageReference(it->second);
            }

            item->setAction(PDFActionPtr(new PDFActionGoTo(destination, PDFDestination())));
        };
        outline->apply(mapAction);
        builder->setOutline(outline.data());
    }
};

PDFRedact::PDFRedact(const PDFDocument* document,
                     const PDFFontCache* fontCache,
                     const PDFICCProfileCache* iccProfileCache,
                     const PDFCMS* cms,
                     const PDFOptionalContentActivity* optionalContentActivity,
                     const PDFMeshQualitySettings* meshQualitySettings,
                     QColor redactFillColor) :
    m_document(document),
    m_fontCache(fontCache),
    m_iccProfileCache(iccProfileCache),
    m_cms(cms),
    m_optionalContentActivity(optionalContentActivity),
    m_meshQualitySettings(meshQualitySettings),
    m_redactFillColor(redactFillColor)
{

}

QRectF PDFRedact::getVisibleBox(const PDFPage* page)
{
    if (!page)
    {
        return QRectF();
    }

    const QRectF mediaBox = page->getMediaBox().normalized();
    const bool isFinite = std::isfinite(mediaBox.left()) && std::isfinite(mediaBox.top()) && std::isfinite(mediaBox.right()) && std::isfinite(mediaBox.bottom());
    if (!isFinite || !mediaBox.isValid() || mediaBox.isEmpty())
    {
        return QRectF();
    }

    // ISO 32000-2, Table 31: the crop box is clipped to the media box; one outside of it shows
    // nothing - the media box is kept then
    const QRectF cropBox = page->getCropBox().normalized();
    const QRectF visibleBox = cropBox.isValid() ? cropBox.intersected(mediaBox) : mediaBox;
    return visibleBox.isValid() && !visibleBox.isEmpty() ? visibleBox : mediaBox;
}

std::vector<PDFRedact::Area> PDFRedact::getMarkAreas(const PDFDocument* document, QString* errorMessage, int* markCount)
{
    std::vector<Area> areas;
    if (errorMessage)
    {
        errorMessage->clear();
    }
    if (markCount)
    {
        *markCount = 0;
    }
    if (!document)
    {
        return areas;
    }

    const PDFObjectStorage* storage = &document->getStorage();
    const PDFCatalog* catalog = document->getCatalog();
    int count = 0;
    bool isTooManyQuadrilaterals = false;
    for (size_t pageIndex = 0; pageIndex < catalog->getPageCount(); ++pageIndex)
    {
        const PDFPage* page = catalog->getPage(pageIndex);
        if (!page)
        {
            continue;
        }

        for (const PDFObjectReference& annotationReference : page->getAnnotations())
        {
            // Only a Redact annotation is parsed (ISO 32000-2, Table 195)
            const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(annotationReference));
            const PDFObject& subtype = dictionary ? storage->getObject(dictionary->get("Subtype")) : PDFObject();
            if (!subtype.isName() || subtype.getString() != "Redact")
            {
                continue;
            }

            PDFAnnotationPtr annotation = PDFAnnotation::parse(storage, annotationReference);
            const PDFRedactAnnotation* redactAnnotation = dynamic_cast<const PDFRedactAnnotation*>(annotation.get());
            if (!redactAnnotation)
            {
                continue;
            }

            ++count;
            if (redactAnnotation->getRedactionRegion().getQuadrilaterals().size() > size_t(MAXIMUM_QUADRILATERAL_COUNT))
            {
                isTooManyQuadrilaterals = true;
            }

            const QPainterPath& path = redactAnnotation->getRedactionRegion().getPath();
            if (!path.isEmpty())
            {
                areas.push_back({ PDFInteger(pageIndex), path });
            }
        }
    }

    if (markCount)
    {
        *markCount = count;
    }

    // The marks are all read (the old interface uses them all); the limits make the tool fail
    if (errorMessage)
    {
        if (count > MAXIMUM_MARK_COUNT)
        {
            *errorMessage = tr("The document has %1 redaction marks, at most %2 can be redacted at once.").arg(count).arg(MAXIMUM_MARK_COUNT);
        }
        else if (isTooManyQuadrilaterals)
        {
            *errorMessage = tr("A redaction mark has more than %1 parts.").arg(MAXIMUM_QUADRILATERAL_COUNT);
        }
    }

    return areas;
}

PDFRedact::Result PDFRedact::perform(const Settings& settings, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFRedactHelper;

    Result result;
    const PDFCatalog* catalog = m_document->getCatalog();
    const size_t pageCount = catalog->getPageCount();

    // The areas of each page and the whole pages
    std::vector<QPainterPath> pageAreas(pageCount);
    std::vector<bool> isWholePage(pageCount, false);
    for (const Area& area : settings.areas)
    {
        if (area.pageIndex >= 0 && size_t(area.pageIndex) < pageCount)
        {
            pageAreas[size_t(area.pageIndex)].addPath(area.path);
        }
    }
    for (const PDFInteger pageIndex : settings.wholePages)
    {
        if (pageIndex >= 0 && size_t(pageIndex) < pageCount)
        {
            isWholePage[size_t(pageIndex)] = true;
        }
    }

    PDFRenderer renderer(m_document, m_fontCache, m_iccProfileCache, m_cms, m_optionalContentActivity, PDFRenderer::None, *m_meshQualitySettings);
    renderer.setOperationControl(control);

    if (progress)
    {
        ProgressStartupInfo info;
        info.showDialog = false;
        info.text = tr("Redacting pages");
        progress->start(pageCount, std::move(info));
    }

    // The pages in parallel by chunks (the compiled pages of a chunk are held until they are written):
    // each page has its own compiled page and its own entry of the results; the document is only read.
    // The content streams are written and read back (PDFContentStreamBuilder, whose reader runs in the
    // pool of the pages) in this thread, never in a task of the pool: a nested parallel loop of the
    // same pool would wait for the threads, which wait for it.
    PDFDocumentBuilder builder;
    builder.createDocument();
    std::map<PDFObjectReference, PDFObjectReference> pageMap;

    std::vector<Helper::Page> pages;
    size_t chunkStart = 0;
    auto processPage = [&](PDFInteger pageIndex)
    {
        Helper::Page& rebuiltPage = pages[size_t(pageIndex) - chunkStart];
        if (PDFOperationControl::isOperationCanceled(control))
        {
            return;
        }

        const PDFPage* page = catalog->getPage(size_t(pageIndex));
        const QRectF box = getVisibleBox(page);
        if (!box.isValid())
        {
            // R6-17: a page without a valid media box has no content in the copy
            rebuiltPage.box = QRectF(0.0, 0.0, Helper::DEFAULT_PAGE_WIDTH, Helper::DEFAULT_PAGE_HEIGHT);
            rebuiltPage.problems.push_back({ pageIndex, tr("Page %1 has no valid size, it is empty in the copy.").arg(pageIndex + 1) });
            if (progress)
            {
                progress->step();
            }
            return;
        }

        rebuiltPage.box = box;
        const QPainterPath boxPath = Helper::createRectanglePath(box);
        if (isWholePage[size_t(pageIndex)])
        {
            // R6-30: a blank page of the same size, its whole box is the area
            rebuiltPage.area = boxPath;
        }
        else
        {
            rebuiltPage.area = PDFPathBoolean::intersect(PDFPathBoolean::uniteSubpaths(Helper::clipToBox(pageAreas[size_t(pageIndex)], box)), boxPath);

            renderer.compile(&rebuiltPage.compiledPage, size_t(pageIndex));
            rebuiltPage.isCompiled = true;

            // B143: an error of the page is a problem of the result, never dropped silently
            for (const PDFRenderError& error : rebuiltPage.compiledPage.getErrors())
            {
                if (error.type == RenderErrorType::Error || error.type == RenderErrorType::NotImplemented || error.type == RenderErrorType::NotSupported)
                {
                    rebuiltPage.problems.push_back({ pageIndex, tr("Page %1: some content could not be drawn.").arg(pageIndex + 1) });
                    break;
                }
            }

            rebuiltPage.compiledPage.redact(rebuiltPage.area, box, Helper::getPageMatrix(box), settings.fillColor.value_or(QColor()));
        }

        if (settings.fillColor && !rebuiltPage.area.isEmpty())
        {
            rebuiltPage.fill = Helper::createFillContent(rebuiltPage.area, *settings.fillColor);
        }

        if (progress)
        {
            progress->step();
        }
    };

    const size_t chunkSize = size_t(qMax(1, QThread::idealThreadCount()) * Helper::PAGES_PER_THREAD);
    for (; chunkStart < pageCount; chunkStart += chunkSize)
    {
        const size_t chunkEnd = qMin(pageCount, chunkStart + chunkSize);
        pages = std::vector<Helper::Page>(chunkEnd - chunkStart);

        const PDFIntegerRange<PDFInteger> pageRange{ PDFInteger(chunkStart), PDFInteger(chunkEnd) };
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pageRange.begin(), pageRange.end(), processPage);
        if (PDFOperationControl::isOperationCanceled(control))
        {
            result.isStopped = true;
            return result;
        }

        // The document is assembled in this thread
        for (size_t pageIndex = chunkStart; pageIndex < chunkEnd; ++pageIndex)
        {
            Helper::Page& rebuiltPage = pages[pageIndex - chunkStart];
            const PDFPage* page = catalog->getPage(pageIndex);
            const PDFObjectReference newPageReference = Helper::appendPage(&builder, page, &rebuiltPage);
            if (page)
            {
                pageMap[page->getPageReference()] = newPageReference;
            }

            if (!rebuiltPage.area.isEmpty())
            {
                result.areas.push_back({ PDFInteger(pageIndex), rebuiltPage.area });
            }
            result.pageProblems.insert(result.pageProblems.end(), rebuiltPage.problems.cbegin(), rebuiltPage.problems.cend());
        }
    }

    if (progress)
    {
        progress->finish();
    }

    Helper::copyDocumentInfo(&builder, m_document, settings.options);
    if (settings.options.testFlag(CopyOutline))
    {
        Helper::copyOutline(&builder, m_document, pageMap);
    }

    PDFDocument redactedDocument = builder.build();
    PDFOptimizer optimizer(PDFOptimizer::All, nullptr);
    optimizer.setDocument(&redactedDocument);
    optimizer.optimize();
    result.document = optimizer.takeOptimizedDocument();
    return result;
}

PDFDocument PDFRedact::perform(Options options)
{
    // The old interface: the marks of the document, the fill of the constructor; every mark is
    // redacted (the limits are of the tool)
    Settings settings;
    settings.areas = getMarkAreas(m_document, nullptr);
    if (m_redactFillColor.isValid())
    {
        settings.fillColor = m_redactFillColor;
    }
    settings.options = options;
    return perform(settings, nullptr, nullptr).document;
}

}   // namespace pdf
