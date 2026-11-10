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

#include "pdfdocumentmanipulator.h"
#include "pdfdocumentmerger.h"
#include "pdfimagepagefactory.h"
#include "pdfdbgheap.h"

#include <algorithm>
#include <optional>

namespace pdf
{

/// Helpers of the adapter of PageMaster
class PDFDocumentManipulatorHelper
{
public:
    PDFDocumentManipulatorHelper() = delete;

    /// The size of a page in points within the limits of the merger (PageMaster allows any
    /// size); the aspect ratio is kept (B30 of the plan of step 3: each side was limited alone)
    static QSizeF getPageSize(QSizeF millimeters)
    {
        return PDFImagePageFactory::getBoundedSize(QSizeF(millimeters.width() * PDF_MM_TO_POINT, millimeters.height() * PDF_MM_TO_POINT));
    }

    /// The documents of PageMaster outlive the call of assemble, the merger only reads them
    static void keepDocument(PDFDocument*)
    {

    }
};

PDFOperationResult PDFDocumentManipulator::assemble(const AssembledPages& pages)
{
    if (pages.empty())
    {
        return tr("Empty page list.");
    }

    m_assembledDocument = PDFDocument();

    std::vector<PDFDocumentMerger::Source> sources;
    std::vector<PDFDocumentMerger::Item> items;
    std::map<PDFInteger, size_t> documentSources;
    std::map<PDFInteger, size_t> imageSources;
    std::optional<size_t> blankSource;
    items.reserve(pages.size());

    for (const AssembledPage& page : pages)
    {
        PDFDocumentMerger::Item item;
        if (page.isDocumentPage())
        {
            auto document = m_documents.find(page.documentIndex);
            if (document == m_documents.cend() || !document->second)
            {
                return tr("Invalid document.");
            }
            if (page.pageIndex < 0 || page.pageIndex >= PDFInteger(document->second->getCatalog()->getPageCount()))
            {
                return tr("Missing page (%1) in a document.").arg(page.pageIndex);
            }

            auto source = documentSources.find(page.documentIndex);
            if (source == documentSources.cend())
            {
                PDFDocumentPointer pointer(const_cast<PDFDocument*>(document->second), &PDFDocumentManipulatorHelper::keepDocument);
                source = documentSources.emplace(page.documentIndex, sources.size()).first;
                sources.push_back(PDFDocumentMerger::Source::createDocument(pointer, QString()));
            }

            // PageMaster gives the rotation of the page, the merger its turn
            const PageRotation current = document->second->getCatalog()->getPage(size_t(page.pageIndex))->getPageRotation();
            item.source = source->second;
            item.pageIndex = page.pageIndex;
            item.turn = getPageRotationCombined(page.pageRotation, getPageRotationInversed(current));
            item.cropMargins = QMarginsF(page.cropMarginsMM.left() * PDF_MM_TO_POINT, page.cropMarginsMM.top() * PDF_MM_TO_POINT,
                                         page.cropMarginsMM.right() * PDF_MM_TO_POINT, page.cropMarginsMM.bottom() * PDF_MM_TO_POINT);
        }
        else if (page.isImagePage())
        {
            auto image = m_images.find(page.imageIndex);
            if (image == m_images.cend() || image->second.isNull())
            {
                return tr("Missing image.");
            }

            auto source = imageSources.find(page.imageIndex);
            if (source == imageSources.cend())
            {
                source = imageSources.emplace(page.imageIndex, sources.size()).first;
                sources.push_back(PDFDocumentMerger::Source::createImage(image->second, 0.0, QString()));
            }

            item.source = source->second;
            item.pageIndex = 0;
            item.turn = page.pageRotation;
            item.pageSize = PDFDocumentManipulatorHelper::getPageSize(page.pageSize);

            // The crop of the image in the coordinates of its page before the crop
            const QSizeF sourceSize = page.sourcePageSize.isEmpty() ? page.pageSize : page.sourcePageSize;
            if (!sourceSize.isEmpty())
            {
                const QMarginsF& margins = page.cropMarginsMM;
                QRectF clip(margins.left() / sourceSize.width(), margins.top() / sourceSize.height(),
                            1.0 - (margins.left() + margins.right()) / sourceSize.width(), 1.0 - (margins.top() + margins.bottom()) / sourceSize.height());
                clip = clip.intersected(QRectF(0.0, 0.0, 1.0, 1.0));
                item.imageClip = (clip.isValid() && !clip.isEmpty()) ? clip : QRectF();
            }
        }
        else
        {
            if (!blankSource)
            {
                blankSource = sources.size();
                sources.push_back(PDFDocumentMerger::Source::createBlank());
            }
            item.source = *blankSource;
            item.turn = page.pageRotation;
            item.pageSize = PDFDocumentManipulatorHelper::getPageSize(page.pageSize);
        }
        items.push_back(item);
    }

    PDFDocumentMerger::Settings settings;
    settings.checkPermissions = true;
    switch (m_outlineMode)
    {
        case OutlineMode::NoOutline:
            settings.outline = PDFDocumentMerger::OutlineMode::NoOutline;
            break;
        case OutlineMode::Join:
            settings.outline = PDFDocumentMerger::OutlineMode::Join;
            break;
        case OutlineMode::DocumentParts:
            settings.outline = PDFDocumentMerger::OutlineMode::DocumentParts;
            break;
    }

    PDFDocumentMerger::Result result = PDFDocumentMerger::merge(sources, items, settings, nullptr, nullptr);
    if (!result.isSuccess)
    {
        return result.errorMessage.isEmpty() ? tr("Error occured during page creation.") : result.errorMessage;
    }
    if (!result.leftOut.empty())
    {
        // PageMaster has no partial result: an input, which cannot be copied, is an error
        return result.leftOut.front().reason;
    }

    m_assembledDocument = std::move(result.document);
    return true;
}

PDFDocumentManipulator::OutlineMode PDFDocumentManipulator::getOutlineMode() const
{
    return m_outlineMode;
}

void PDFDocumentManipulator::setOutlineMode(OutlineMode outlineMode)
{
    m_outlineMode = outlineMode;
}

}   // namespace pdf
