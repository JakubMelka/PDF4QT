// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pdfbitonaldocumentcreator.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"
#include "pdfpage.h"
#include "pdfrenderer.h"

#include <QtTest>
#include <QImage>

#include <memory>
#include <vector>

/// Everything, which the rasterizer pool needs. The pool keeps a reference to the mesh
/// quality settings, so they are declared before it and they outlive it.
class RenderingContext
{
public:
    explicit RenderingContext(pdf::PDFDocument* document) :
        m_optionalContentActivity(document, pdf::OCUsage::Export, nullptr),
        m_cmsManager(nullptr),
        m_fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT)
    {
        m_cmsManager.setDocument(document);
        m_cmsManager.setSettings(pdf::PDFCMSSettings());

        pdf::PDFModifiedDocument modifiedDocument(document, &m_optionalContentActivity);
        m_fontCache.setDocument(modifiedDocument);
        m_fontCache.setCacheShrinkEnabled(nullptr, false);

        m_rasterizerPool = std::make_unique<pdf::PDFRasterizerPool>(document,
                                                                    &m_fontCache,
                                                                    &m_cmsManager,
                                                                    &m_optionalContentActivity,
                                                                    pdf::PDFBitonalDocumentCreator::getPageRasterizationFeatures(),
                                                                    m_meshQualitySettings,
                                                                    pdf::PDFRasterizerPool::getDefaultRasterizerCount(),
                                                                    pdf::RendererEngine::QPainter,
                                                                    nullptr);
    }

    ~RenderingContext()
    {
        m_rasterizerPool.reset();
        m_fontCache.setCacheShrinkEnabled(nullptr, true);
    }

    pdf::PDFRasterizerPool* getRasterizerPool() { return m_rasterizerPool.get(); }

private:
    pdf::PDFOptionalContentActivity m_optionalContentActivity;
    pdf::PDFCMSManager m_cmsManager;
    pdf::PDFFontCache m_fontCache;
    pdf::PDFMeshQualitySettings m_meshQualitySettings;
    std::unique_ptr<pdf::PDFRasterizerPool> m_rasterizerPool;
};

/// Operation control, which reports the operation as cancelled from the beginning
class CancelledOperationControl : public pdf::PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return true; }
};

/// Tests of the page conversion of \p pdf::PDFBitonalDocumentCreator. Unlike the
/// conversion of a single image, the page conversion needs the whole rendering
/// machinery - the font cache, the color management and the rasterizer pool - so it
/// lives in its own test executable, which runs a QGuiApplication.
class BitonalDocumentTest : public QObject
{
    Q_OBJECT

private slots:
    void test_page_conversion_replaces_page_content();
    void test_page_conversion_maps_pages_independently();
    void test_page_conversion_is_cancellable();
    void test_page_conversion_keeps_structure_tree_when_partial();
    void test_page_conversion_keeps_structure_tree_when_page_fails();
    void test_page_conversion_removes_structure_tree_when_complete();
    void test_page_conversion_needs_rasterizer_pool();

private:
    /// Resolution used by the tests. It is deliberately low - the tests verify, which
    /// image lands on which page, not the quality of the thresholding.
    static constexpr int TEST_DPI_RESOLUTION = 48;

    /// Creates a document, whose pages have the given sizes (in points). The left half
    /// of each page is painted black and the right one is left white, so a converted
    /// page can be recognized both by its size and by its content.
    /// \param pageSizes Sizes of the pages
    /// \param addStructureTree Adds a structure tree to the catalog and to the pages
    static pdf::PDFDocument createDocument(const std::vector<QSizeF>& pageSizes, bool addStructureTree);

    /// Returns the dictionary of the image, which the conversion has placed onto a page
    static const pdf::PDFDictionary* getPageBitonalImage(const pdf::PDFDocument& document, size_t pageIndex);

    /// Returns the dictionary of the page
    static const pdf::PDFDictionary* getPageDictionary(const pdf::PDFDocument& document, size_t pageIndex);

    /// Returns the dictionary of the catalog of the document
    static const pdf::PDFDictionary* getCatalogDictionary(const pdf::PDFDocument& document);

    /// Creates the settings converting the given pages using the algorithm
    /// \param pageCount Number of the converted pages, starting from the first one
    static pdf::PDFBitonalDocumentCreator::Settings createPageSettings(size_t pageCount);
};

pdf::PDFDocument BitonalDocumentTest::createDocument(const std::vector<QSizeF>& pageSizes, bool addStructureTree)
{
    pdf::PDFDocumentBuilder builder;

    pdf::PDFInteger structParent = 0;

    for (const QSizeF& pageSize : pageSizes)
    {
        const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(QPointF(0, 0), pageSize));

        QByteArray content = QString("0 0 0 rg 0 0 %1 %2 re f").arg(pageSize.width() / 2.0, 0, 'f', 3)
                                                               .arg(pageSize.height(), 0, 'f', 3).toLatin1();

        pdf::PDFDictionary contentDictionary;
        contentDictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH),
                                   pdf::PDFObject::createInteger(content.size()));
        const pdf::PDFObjectReference contentReference = builder.addObject(
            pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(std::move(contentDictionary), std::move(content))));

        pdf::PDFDictionary pageUpdate;
        pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentReference));

        if (addStructureTree)
        {
            pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("StructParents"), pdf::PDFObject::createInteger(structParent++));
        }

        builder.mergeTo(pageReference, pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(pageUpdate))));
    }

    if (addStructureTree)
    {
        pdf::PDFDictionary structTreeRoot;
        structTreeRoot.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("StructTreeRoot"));
        const pdf::PDFObjectReference structTreeRootReference = builder.addObject(
            pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(structTreeRoot))));

        pdf::PDFDictionary markInfo;
        markInfo.addEntry(pdf::PDFInplaceOrMemoryString("Marked"), pdf::PDFObject::createBool(true));

        pdf::PDFDictionary catalogUpdate;
        catalogUpdate.addEntry(pdf::PDFInplaceOrMemoryString("StructTreeRoot"), pdf::PDFObject::createReference(structTreeRootReference));
        catalogUpdate.addEntry(pdf::PDFInplaceOrMemoryString("MarkInfo"),
                               pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(markInfo))));

        builder.mergeTo(builder.getCatalogReference(),
                        pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(catalogUpdate))));
    }

    return builder.build();
}

const pdf::PDFDictionary* BitonalDocumentTest::getPageBitonalImage(const pdf::PDFDocument& document, size_t pageIndex)
{
    const pdf::PDFPage* page = document.getCatalog()->getPage(pageIndex);

    if (!page)
    {
        return nullptr;
    }

    const pdf::PDFDictionary* resources = document.getDictionaryFromObject(page->getResources());

    if (!resources)
    {
        return nullptr;
    }

    const pdf::PDFDictionary* xObjects = document.getDictionaryFromObject(resources->get("XObject"));

    if (!xObjects)
    {
        return nullptr;
    }

    return document.getDictionaryFromObject(xObjects->get("BitonalImage"));
}

const pdf::PDFDictionary* BitonalDocumentTest::getPageDictionary(const pdf::PDFDocument& document, size_t pageIndex)
{
    const pdf::PDFPage* page = document.getCatalog()->getPage(pageIndex);
    return page ? document.getDictionaryFromObject(document.getObjectByReference(page->getPageReference())) : nullptr;
}

const pdf::PDFDictionary* BitonalDocumentTest::getCatalogDictionary(const pdf::PDFDocument& document)
{
    const pdf::PDFDictionary* trailerDictionary = document.getTrailerDictionary();
    return trailerDictionary ? document.getDictionaryFromObject(trailerDictionary->get("Root")) : nullptr;
}

pdf::PDFBitonalDocumentCreator::Settings BitonalDocumentTest::createPageSettings(size_t pageCount)
{
    pdf::PDFBitonalDocumentCreator::Settings settings;
    settings.conversionSource = pdf::PDFBitonalDocumentCreator::ConversionSource::Pages;
    settings.conversionMethod = pdf::PDFImageConversion::ConversionMethod::Automatic;
    settings.dpiResolution = TEST_DPI_RESOLUTION;

    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        pdf::PDFBitonalDocumentCreator::ItemInfo item;
        item.pageIndex = pdf::PDFInteger(pageIndex);
        settings.items.push_back(item);
    }

    return settings;
}

void BitonalDocumentTest::test_page_conversion_replaces_page_content()
{
    pdf::PDFDocument document = createDocument({ QSizeF(200, 100) }, false);
    RenderingContext context(&document);

    pdf::PDFBitonalDocumentCreator creator(&document, context.getRasterizerPool(), nullptr);

    QVERIFY(creator.createBitonalDocument(createPageSettings(1)));
    QCOMPARE(creator.getConvertedItemCount(), size_t(1));
    QCOMPARE(creator.getFailedItemCount(), size_t(0));

    const QSize expectedImageSize = pdf::PDFBitonalDocumentCreator::getPageImageSize(document.getCatalog()->getPage(0), TEST_DPI_RESOLUTION);

    pdf::PDFDocument bitonalDocument = creator.takeBitonalDocument();
    QCOMPARE(bitonalDocument.getCatalog()->getPageCount(), size_t(1));

    const pdf::PDFPage* bitonalPage = bitonalDocument.getCatalog()->getPage(0);
    QVERIFY(bitonalPage);

    // The page keeps its geometry, only its content is replaced
    QCOMPARE(bitonalPage->getMediaBox(), QRectF(0, 0, 200, 100));

    const pdf::PDFDictionary* imageDictionary = getPageBitonalImage(bitonalDocument, 0);
    QVERIFY(imageDictionary);

    pdf::PDFDocumentDataLoaderDecorator loader(&bitonalDocument);
    QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "BitsPerComponent", 0), 1);
    QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "Width", 0), expectedImageSize.width());
    QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "Height", 0), expectedImageSize.height());

    // The converted page really shows the content of the original one - the left half
    // of the page is black and the right one is white
    RenderingContext bitonalContext(&bitonalDocument);
    pdf::PDFBitonalDocumentCreator bitonalCreator(&bitonalDocument, bitonalContext.getRasterizerPool(), nullptr);

    const QImage renderedPage = bitonalCreator.renderPage(0, QSize(200, 100), nullptr);
    QVERIFY(!renderedPage.isNull());
    QCOMPARE(renderedPage.size(), QSize(200, 100));
    QVERIFY(qGray(renderedPage.pixel(50, 50)) < 64);
    QVERIFY(qGray(renderedPage.pixel(150, 50)) > 192);
}

void BitonalDocumentTest::test_page_conversion_maps_pages_independently()
{
    // Pages are rasterized and converted in parallel, so a mistake in the indexing
    // would place the image of one page onto another one. Every page has a different
    // size here, so the image, which belongs to it, can be recognized.
    std::vector<QSizeF> pageSizes;
    for (int index = 0; index < 8; ++index)
    {
        pageSizes.push_back(QSizeF(120 + 20 * index, 90 + 10 * index));
    }

    pdf::PDFDocument document = createDocument(pageSizes, false);
    RenderingContext context(&document);

    pdf::PDFBitonalDocumentCreator creator(&document, context.getRasterizerPool(), nullptr);

    std::vector<QSize> expectedImageSizes;
    for (size_t pageIndex = 0; pageIndex < pageSizes.size(); ++pageIndex)
    {
        expectedImageSizes.push_back(pdf::PDFBitonalDocumentCreator::getPageImageSize(document.getCatalog()->getPage(pageIndex), TEST_DPI_RESOLUTION));
    }

    QVERIFY(creator.createBitonalDocument(createPageSettings(pageSizes.size())));
    QCOMPARE(creator.getConvertedItemCount(), pageSizes.size());
    QCOMPARE(creator.getFailedItemCount(), size_t(0));

    const pdf::PDFDocument bitonalDocument = creator.takeBitonalDocument();
    QCOMPARE(bitonalDocument.getCatalog()->getPageCount(), pageSizes.size());

    pdf::PDFDocumentDataLoaderDecorator loader(&bitonalDocument);

    for (size_t pageIndex = 0; pageIndex < pageSizes.size(); ++pageIndex)
    {
        const pdf::PDFDictionary* imageDictionary = getPageBitonalImage(bitonalDocument, pageIndex);
        QVERIFY(imageDictionary);

        QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "BitsPerComponent", 0), 1);
        QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "Width", 0), expectedImageSizes[pageIndex].width());
        QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "Height", 0), expectedImageSizes[pageIndex].height());
    }
}

void BitonalDocumentTest::test_page_conversion_is_cancellable()
{
    pdf::PDFDocument document = createDocument({ QSizeF(200, 100), QSizeF(200, 100), QSizeF(200, 100) }, false);
    RenderingContext context(&document);

    pdf::PDFBitonalDocumentCreator creator(&document, context.getRasterizerPool(), nullptr);

    CancelledOperationControl operationControl;

    auto pageSizeGetter = [](const pdf::PDFPage*) { return QSize(64, 32); };

    int renderedPageCount = 0;
    auto pageImageProcessor = [&renderedPageCount](pdf::PDFInteger, QImage) { ++renderedPageCount; };

    // A cancelled rendering must not produce anything - the preview, which has asked
    // for it, has already thrown its result away and it is waiting for the worker
    creator.renderPages({ 0, 1, 2 }, pageSizeGetter, pageImageProcessor, &operationControl);
    QCOMPARE(renderedPageCount, 0);

    QVERIFY(creator.renderPage(0, QSize(64, 32), &operationControl).isNull());

    // Without the cancellation the very same request produces the images
    creator.renderPages({ 0, 1, 2 }, pageSizeGetter, pageImageProcessor, nullptr);
    QCOMPARE(renderedPageCount, 3);

    QVERIFY(!creator.renderPage(0, QSize(64, 32), nullptr).isNull());
}

void BitonalDocumentTest::test_page_conversion_keeps_structure_tree_when_partial()
{
    pdf::PDFDocument document = createDocument({ QSizeF(200, 100), QSizeF(200, 100) }, true);
    RenderingContext context(&document);

    pdf::PDFBitonalDocumentCreator creator(&document, context.getRasterizerPool(), nullptr);

    QVERIFY(creator.createBitonalDocument(createPageSettings(1)));
    QCOMPARE(creator.getConvertedItemCount(), size_t(1));

    const pdf::PDFDocument bitonalDocument = creator.takeBitonalDocument();

    // The second page still contains its original marked content, so the structure
    // tree must survive - that page is still a part of it
    const pdf::PDFDictionary* catalogDictionary = getCatalogDictionary(bitonalDocument);
    QVERIFY(catalogDictionary);
    QVERIFY(catalogDictionary->hasKey("StructTreeRoot"));
    QVERIFY(catalogDictionary->hasKey("MarkInfo"));

    // The converted page is not a part of the structure tree anymore, the other one is
    const pdf::PDFDictionary* convertedPageDictionary = getPageDictionary(bitonalDocument, 0);
    QVERIFY(convertedPageDictionary);
    QVERIFY(!convertedPageDictionary->hasKey("StructParents"));

    const pdf::PDFDictionary* originalPageDictionary = getPageDictionary(bitonalDocument, 1);
    QVERIFY(originalPageDictionary);
    QVERIFY(originalPageDictionary->hasKey("StructParents"));
}

void BitonalDocumentTest::test_page_conversion_keeps_structure_tree_when_page_fails()
{
    // The second page has an empty media box, so its content cannot be replaced by an
    // image - the conversion is requested for the whole document, but one of its pages
    // keeps its original marked content
    pdf::PDFDocument document = createDocument({ QSizeF(200, 100), QSizeF(0, 0) }, true);
    RenderingContext context(&document);

    pdf::PDFBitonalDocumentCreator creator(&document, context.getRasterizerPool(), nullptr);

    QVERIFY(creator.createBitonalDocument(createPageSettings(2)));
    QCOMPARE(creator.getConvertedItemCount(), size_t(1));
    QCOMPARE(creator.getFailedItemCount(), size_t(1));

    const pdf::PDFDocument bitonalDocument = creator.takeBitonalDocument();

    // The number of the requested pages matches the number of the pages of the
    // document, but one of them has not been converted, so the structure tree,
    // which that page still uses, must be preserved
    const pdf::PDFDictionary* catalogDictionary = getCatalogDictionary(bitonalDocument);
    QVERIFY(catalogDictionary);
    QVERIFY(catalogDictionary->hasKey("StructTreeRoot"));
    QVERIFY(catalogDictionary->hasKey("MarkInfo"));

    const pdf::PDFDictionary* failedPageDictionary = getPageDictionary(bitonalDocument, 1);
    QVERIFY(failedPageDictionary);
    QVERIFY(failedPageDictionary->hasKey("StructParents"));
}

void BitonalDocumentTest::test_page_conversion_removes_structure_tree_when_complete()
{
    pdf::PDFDocument document = createDocument({ QSizeF(200, 100), QSizeF(200, 100) }, true);
    RenderingContext context(&document);

    pdf::PDFBitonalDocumentCreator creator(&document, context.getRasterizerPool(), nullptr);

    QVERIFY(creator.createBitonalDocument(createPageSettings(2)));
    QCOMPARE(creator.getConvertedItemCount(), size_t(2));
    QCOMPARE(creator.getFailedItemCount(), size_t(0));

    const pdf::PDFDocument bitonalDocument = creator.takeBitonalDocument();

    // No page is tagged anymore, so the whole structure tree is gone
    const pdf::PDFDictionary* catalogDictionary = getCatalogDictionary(bitonalDocument);
    QVERIFY(catalogDictionary);
    QVERIFY(!catalogDictionary->hasKey("StructTreeRoot"));
    QVERIFY(!catalogDictionary->hasKey("MarkInfo"));
}

void BitonalDocumentTest::test_page_conversion_needs_rasterizer_pool()
{
    pdf::PDFDocument document = createDocument({ QSizeF(200, 100) }, false);

    // Pages cannot be rasterized without the pool, so the conversion by the algorithm
    // must fail instead of producing a document with empty pages
    pdf::PDFBitonalDocumentCreator creator(&document, nullptr, nullptr);

    pdf::PDFBitonalDocumentCreator::Settings settings = createPageSettings(1);
    QVERIFY(!creator.createBitonalDocument(settings));
    QCOMPARE(creator.getConvertedItemCount(), size_t(0));

    // Filling a page needs no rasterization at all
    settings.items.front().mode = pdf::PDFBitonalDocumentCreator::ItemMode::FillWhite;
    QVERIFY(creator.createBitonalDocument(settings));
    QCOMPARE(creator.getConvertedItemCount(), size_t(1));
}

QTEST_MAIN(BitonalDocumentTest)

#include "tst_bitonaldocumenttest.moc"
