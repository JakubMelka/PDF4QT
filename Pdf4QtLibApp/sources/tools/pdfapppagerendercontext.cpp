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

#include "pdfapppagerendercontext.h"

#include "pdfcatalog.h"
#include "pdfcolorconvertor.h"
#include "pdfconstants.h"
#include "pdfdocument.h"
#include "pdfmeshqualitysettings.h"
#include "pdfpainter.h"
#include "pdfpainterutils.h"
#include "pdftextlayout.h"

#include <QPainter>

namespace pdf
{

PDFRenderer::Features PDFAppPageRenderContext::getDefaultFeatures()
{
    return PDFRenderer::Features(PDFRenderer::Antialiasing | PDFRenderer::TextAntialiasing | PDFRenderer::SmoothImages |
                                 PDFRenderer::ClipToCropBox | PDFRenderer::DisplayAnnotations);
}

PDFAppPageRenderContext::PDFAppPageRenderContext(const PDFDocument* document, const PDFCMSSettings& cmsSettings, PDFRenderer::Features features) :
    m_document(document),
    m_features(features),
    m_fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT),
    m_optionalContentActivity(document, OCUsage::View, nullptr),
    m_cmsManager(nullptr),
    m_annotationManager(&m_fontCache, &m_iccProfileCache, &m_cmsManager, &m_optionalContentActivity, PDFMeshQualitySettings(),
                        features, PDFAnnotationManager::Target::View, nullptr)
{
    // The document is not modified: the caches and the annotations only read it
    PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), &m_optionalContentActivity);
    m_fontCache.setDocument(modifiedDocument);
    m_iccProfileCache.setDocument(modifiedDocument);
    m_cmsManager.setDocument(document);
    m_cmsManager.setSettings(cmsSettings);
    m_annotationManager.setDocument(modifiedDocument);
}

void PDFAppPageRenderContext::draw(QPainter* painter, PDFInteger pageIndex, const QTransform& pageToImage, bool showAnnotations, const PDFOperationControl* control)
{
    const PDFPage* page = m_document->getCatalog()->getPage(size_t(pageIndex));
    const PDFCMSPointer cms = m_cmsManager.getCurrentCMS();
    PDFRenderer renderer(m_document, &m_fontCache, &m_iccProfileCache, cms.data(), &m_optionalContentActivity, m_features, PDFMeshQualitySettings());
    renderer.setOperationControl(control);

    PDFFontCacheShrinkGuard shrinkGuard(&m_fontCache);
    PDFPrecompiledPage compiledPage;
    renderer.compile(&compiledPage, size_t(pageIndex));
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return;
    }

    compiledPage.draw(painter, page->getCropBox(), pageToImage, m_features, 1.0);
    if (showAnnotations)
    {
        PDFColorConvertor convertor = cms->getColorConvertor();
        PDFRenderer::applyFeaturesToAnnotationColorConvertor(m_features, convertor);
        PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
        QList<PDFRenderError> errors;
        m_annotationManager.drawPage(painter, pageIndex, &compiledPage, textLayoutGetter, pageToImage, convertor, errors);
    }
}

}   // namespace pdf
