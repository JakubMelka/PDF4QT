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
#include <QElapsedTimer>
#include <QImage>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

/// Everything, which the rasterizer pool needs. The pool keeps a reference to the mesh
/// quality settings, so they are declared before it and they outlive it.
class RenderingContext
{
public:
    explicit RenderingContext(pdf::PDFDocument* document, int rasterizerCount = pdf::PDFRasterizerPool::getDefaultRasterizerCount()) :
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
                                                                    rasterizerCount,
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

/// Operation control, which reports the operation as cancelled from the given query
/// on - the first queries pass, so the rendering starts and it is cancelled while
/// the page is being compiled
class CountingOperationControl : public pdf::PDFOperationControl
{
public:
    explicit CountingOperationControl(int passedQueryCount) : m_passedQueryCount(passedQueryCount) { }

    virtual bool isOperationCancelled() const override { return m_queryCount.fetch_add(1, std::memory_order_relaxed) >= m_passedQueryCount; }

private:
    int m_passedQueryCount;
    mutable std::atomic<int> m_queryCount = { 0 };
};

/// Operation control, which reports the operation as cancelled after the given time
class TimedOperationControl : public pdf::PDFOperationControl
{
public:
    explicit TimedOperationControl(qint64 milliseconds) : m_milliseconds(milliseconds) { m_timer.start(); }

    virtual bool isOperationCancelled() const override { return m_timer.elapsed() >= m_milliseconds; }

private:
    qint64 m_milliseconds;
    QElapsedTimer m_timer;
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
    void test_page_conversion_last_item_wins();
    void test_page_conversion_is_cancellable();
    void test_page_conversion_is_cancellable_while_compiling();
    void test_page_conversion_is_cancellable_while_waiting_for_rasterizer();
    void test_page_conversion_fails_on_rendering_error();
    void test_page_conversion_keeps_structure_tree_when_partial();
    void test_page_conversion_prunes_structure_tree_when_partial();
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
    /// \param brokenImagePages Pages, which also paint an image, whose data are damaged
    static pdf::PDFDocument createDocument(const std::vector<QSizeF>& pageSizes,
                                           bool addStructureTree,
                                           const std::vector<size_t>& brokenImagePages = { });

    /// A two page tagged document with a real structure tree, see the test of the
    /// pruning of the structure tree for its layout
    struct TaggedDocument
    {
        pdf::PDFDocument document;
        std::vector<pdf::PDFObjectReference> pages;
        pdf::PDFObjectReference annotation;     ///< Annotation of the first page
        pdf::PDFObjectReference image;          ///< Image of the first page
        pdf::PDFObjectReference root;           ///< Structure tree root
        pdf::PDFObjectReference documentElement;
        pdf::PDFObjectReference paragraph1;     ///< Paragraph of the first page
        pdf::PDFObjectReference paragraph2;     ///< Paragraph of the second page
        pdf::PDFObjectReference span;           ///< Span spanning both pages
        pdf::PDFObjectReference link;           ///< Link referring to the annotation of the first page
        pdf::PDFObjectReference figure;         ///< Figure referring to the image of the first page
        pdf::PDFObjectReference emptyElement;   ///< Element of the first page without any content
    };

    static TaggedDocument createTaggedDocument();

    /// Returns the references stored in the array of the dictionary
    static std::vector<pdf::PDFObjectReference> getReferenceArray(const pdf::PDFDocument& document, const pdf::PDFDictionary* dictionary, const char* key);

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

pdf::PDFDocument BitonalDocumentTest::createDocument(const std::vector<QSizeF>& pageSizes,
                                                     bool addStructureTree,
                                                     const std::vector<size_t>& brokenImagePages)
{
    pdf::PDFDocumentBuilder builder;

    pdf::PDFInteger structParent = 0;

    for (size_t pageIndex = 0; pageIndex < pageSizes.size(); ++pageIndex)
    {
        const QSizeF& pageSize = pageSizes[pageIndex];
        const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(QPointF(0, 0), pageSize));
        const bool hasBrokenImage = std::find(brokenImagePages.cbegin(), brokenImagePages.cend(), pageIndex) != brokenImagePages.cend();

        QByteArray content = QString("0 0 0 rg 0 0 %1 %2 re f").arg(pageSize.width() / 2.0, 0, 'f', 3)
                                                               .arg(pageSize.height(), 0, 'f', 3).toLatin1();

        pdf::PDFDictionary pageUpdate;

        if (hasBrokenImage)
        {
            // The image has a valid dictionary, but its data are not a flate stream, so
            // the renderer reports an error and skips the image. The valid part of the
            // page is rendered.
            QByteArray garbage("this is not a compressed image stream at all");

            pdf::PDFDictionary imageDictionary;
            imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
            imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Image"));
            imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Width"), pdf::PDFObject::createInteger(16));
            imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Height"), pdf::PDFObject::createInteger(16));
            imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("BitsPerComponent"), pdf::PDFObject::createInteger(8));
            imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), pdf::PDFObject::createName("DeviceRGB"));
            imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_FILTER), pdf::PDFObject::createName("FlateDecode"));
            imageDictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(garbage.size()));
            const pdf::PDFObjectReference imageReference = builder.addObject(
                pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(std::move(imageDictionary), std::move(garbage))));

            pdf::PDFDictionary xobjects;
            xobjects.addEntry(pdf::PDFInplaceOrMemoryString("Im1"), pdf::PDFObject::createReference(imageReference));

            pdf::PDFDictionary resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(xobjects))));
            pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(resources))));

            content.append(QString(" q %1 0 0 %2 %3 0 cm /Im1 Do Q").arg(pageSize.width() / 4.0, 0, 'f', 3)
                                                                   .arg(pageSize.height() / 2.0, 0, 'f', 3)
                                                                   .arg(pageSize.width() * 0.6, 0, 'f', 3).toLatin1());
        }

        pdf::PDFDictionary contentDictionary;
        contentDictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH),
                                   pdf::PDFObject::createInteger(content.size()));
        const pdf::PDFObjectReference contentReference = builder.addObject(
            pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(std::move(contentDictionary), std::move(content))));

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

BitonalDocumentTest::TaggedDocument BitonalDocumentTest::createTaggedDocument()
{
    // Two pages, both with a paragraph of their own, a span, whose content lies on both
    // pages, a link, whose annotation lies on the first page, a figure, whose image lies
    // on the first page, and an element of the first page without any content.
    TaggedDocument result;
    pdf::PDFDocumentBuilder builder;

    auto createDictionary = [](std::initializer_list<std::pair<const char*, pdf::PDFObject>> entries)
    {
        pdf::PDFDictionary dictionary;
        for (const auto& [key, value] : entries)
        {
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString(key), pdf::PDFObject(value));
        }
        return pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(dictionary)));
    };

    auto createArray = [](std::initializer_list<pdf::PDFObject> items)
    {
        pdf::PDFArray array;
        for (const pdf::PDFObject& item : items)
        {
            array.appendItem(item);
        }
        return pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(std::move(array)));
    };

    auto ref = [](pdf::PDFObjectReference reference) { return pdf::PDFObject::createReference(reference); };
    auto name = [](const char* value) { return pdf::PDFObject::createName(value); };
    auto integer = [](pdf::PDFInteger value) { return pdf::PDFObject::createInteger(value); };
    auto string = [](const char* value) { return pdf::PDFObject::createString(value); };

    // The elements are allocated first, so they can refer to each other
    auto allocate = [&builder]() { return builder.addObject(pdf::PDFObject()); };
    result.root = allocate();
    result.documentElement = allocate();
    result.paragraph1 = allocate();
    result.paragraph2 = allocate();
    result.span = allocate();
    result.link = allocate();
    result.figure = allocate();
    result.emptyElement = allocate();
    result.annotation = allocate();
    result.image = allocate();

    for (size_t pageIndex = 0; pageIndex < 2; ++pageIndex)
    {
        const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, 200, 100));
        result.pages.push_back(pageReference);

        QByteArray content("/P <</MCID 0>> BDC 0 0 0 rg 0 0 100 100 re f EMC /Span <</MCID 1>> BDC 0 0 10 10 re f EMC");
        pdf::PDFDictionary contentDictionary;
        contentDictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), integer(content.size()));
        const pdf::PDFObjectReference contentReference = builder.addObject(
            pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(std::move(contentDictionary), std::move(content))));

        pdf::PDFDictionary pageUpdate;
        pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), ref(contentReference));
        pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("StructParents"), integer(pdf::PDFInteger(pageIndex)));

        if (pageIndex == 0)
        {
            pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Annots"), createArray({ ref(result.annotation) }));
            pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), createDictionary({ { "XObject", createDictionary({ { "Im1", ref(result.image) } }) } }));
        }

        builder.mergeTo(pageReference, pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(pageUpdate))));
    }

    const pdf::PDFObjectReference page0 = result.pages[0];
    const pdf::PDFObjectReference page1 = result.pages[1];

    builder.setObject(result.annotation, createDictionary({ { "Type", name("Annot") }, { "Subtype", name("Link") }, { "Rect", createArray({ integer(0), integer(0), integer(10), integer(10) }) }, { "P", ref(page0) }, { "StructParent", integer(2) } }));
    builder.setObject(result.image, createDictionary({ { "Type", name("XObject") }, { "Subtype", name("Image") }, { "Width", integer(1) }, { "Height", integer(1) }, { "StructParent", integer(3) } }));

    builder.setObject(result.paragraph1, createDictionary({ { "Type", name("StructElem") }, { "S", name("P") }, { "P", ref(result.documentElement) }, { "Pg", ref(page0) }, { "K", integer(0) }, { "ID", string("p1") }, { "Ref", createArray({ ref(result.paragraph2) }) } }));
    builder.setObject(result.paragraph2, createDictionary({ { "Type", name("StructElem") }, { "S", name("P") }, { "P", ref(result.documentElement) }, { "Pg", ref(page1) }, { "K", createArray({ integer(0) }) }, { "ID", string("p2") }, { "Ref", createArray({ ref(result.paragraph1), ref(result.span) }) } }));
    builder.setObject(result.span, createDictionary({ { "Type", name("StructElem") }, { "S", name("Span") }, { "P", ref(result.documentElement) }, { "Pg", ref(page0) }, { "K", createArray({ integer(1), createDictionary({ { "Type", name("MCR") }, { "Pg", ref(page1) }, { "MCID", integer(1) } }) }) }, { "ID", string("span") } }));
    builder.setObject(result.link, createDictionary({ { "Type", name("StructElem") }, { "S", name("Link") }, { "P", ref(result.documentElement) }, { "Pg", ref(page0) }, { "K", createArray({ createDictionary({ { "Type", name("OBJR") }, { "Obj", ref(result.annotation) } }) }) }, { "ID", string("link") } }));
    builder.setObject(result.figure, createDictionary({ { "Type", name("StructElem") }, { "S", name("Figure") }, { "P", ref(result.documentElement) }, { "Pg", ref(page0) }, { "K", createDictionary({ { "Type", name("OBJR") }, { "Obj", ref(result.image) } }) }, { "ID", string("figure") } }));
    builder.setObject(result.emptyElement, createDictionary({ { "Type", name("StructElem") }, { "S", name("Figure") }, { "P", ref(result.documentElement) }, { "Pg", ref(page0) }, { "ID", string("empty") } }));
    builder.setObject(result.documentElement, createDictionary({ { "Type", name("StructElem") }, { "S", name("Document") }, { "P", ref(result.root) }, { "K", createArray({ ref(result.paragraph1), ref(result.paragraph2), ref(result.span), ref(result.link), ref(result.figure), ref(result.emptyElement) }) } }));

    // The parent tree is nested into a kid node, so the rebuilding of the tree is
    // exercised, the ID tree is a single node
    const pdf::PDFObjectReference parentTreeKid = builder.addObject(createDictionary({
        { "Limits", createArray({ integer(0), integer(3) }) },
        { "Nums", createArray({ integer(0), createArray({ ref(result.paragraph1), ref(result.span) }),
                                integer(1), createArray({ ref(result.paragraph2), ref(result.span) }),
                                integer(2), ref(result.link),
                                integer(3), ref(result.figure) }) } }));
    const pdf::PDFObjectReference parentTree = builder.addObject(createDictionary({ { "Kids", createArray({ ref(parentTreeKid) }) } }));
    const pdf::PDFObjectReference idTree = builder.addObject(createDictionary({
        { "Names", createArray({ string("empty"), ref(result.emptyElement), string("figure"), ref(result.figure), string("link"), ref(result.link),
                                 string("p1"), ref(result.paragraph1), string("p2"), ref(result.paragraph2), string("span"), ref(result.span) }) } }));

    builder.setObject(result.root, createDictionary({ { "Type", name("StructTreeRoot") }, { "K", ref(result.documentElement) }, { "ParentTree", ref(parentTree) }, { "ParentTreeNextKey", integer(4) }, { "IDTree", ref(idTree) } }));

    pdf::PDFDictionary catalogUpdate;
    catalogUpdate.addEntry(pdf::PDFInplaceOrMemoryString("StructTreeRoot"), ref(result.root));
    catalogUpdate.addEntry(pdf::PDFInplaceOrMemoryString("MarkInfo"), createDictionary({ { "Marked", pdf::PDFObject::createBool(true) } }));
    builder.mergeTo(builder.getCatalogReference(), pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(catalogUpdate))));

    result.document = builder.build();
    return result;
}

std::vector<pdf::PDFObjectReference> BitonalDocumentTest::getReferenceArray(const pdf::PDFDocument& document, const pdf::PDFDictionary* dictionary, const char* key)
{
    std::vector<pdf::PDFObjectReference> references;

    if (!dictionary)
    {
        return references;
    }

    const pdf::PDFObject object = document.getObject(dictionary->get(key));

    if (object.isArray())
    {
        for (const pdf::PDFObject& item : *object.getArray())
        {
            references.push_back(item.isReference() ? item.getReference() : pdf::PDFObjectReference());
        }
    }
    else if (object.isReference())
    {
        references.push_back(object.getReference());
    }

    return references;
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

void BitonalDocumentTest::test_page_conversion_last_item_wins()
{
    pdf::PDFDocument document = createDocument({ QSizeF(200, 100) }, false);
    RenderingContext context(&document);

    pdf::PDFBitonalDocumentCreator creator(&document, context.getRasterizerPool(), nullptr);

    // The same page requested twice - the mode of the last occurrence wins, even when
    // it leaves the page untouched
    pdf::PDFBitonalDocumentCreator::Settings settings = createPageSettings(1);
    pdf::PDFBitonalDocumentCreator::ItemInfo originalItem = settings.items.front();
    originalItem.mode = pdf::PDFBitonalDocumentCreator::ItemMode::Original;
    settings.items.push_back(originalItem);

    QVERIFY(!creator.createBitonalDocument(settings));
    QCOMPARE(creator.getConvertedItemCount(), size_t(0));
    QCOMPARE(creator.getFailedItemCount(), size_t(0));

    // ... and the other way round, the algorithm following the untouched page converts it
    settings.items.push_back(createPageSettings(1).items.front());
    QVERIFY(creator.createBitonalDocument(settings));
    QCOMPARE(creator.getConvertedItemCount(), size_t(1));
    QCOMPARE(creator.getFailedItemCount(), size_t(0));
    QVERIFY(getPageBitonalImage(creator.takeBitonalDocument(), 0));
}

void BitonalDocumentTest::test_page_conversion_is_cancellable()
{
    pdf::PDFDocument document = createDocument({ QSizeF(200, 100), QSizeF(200, 100), QSizeF(200, 100) }, false);
    RenderingContext context(&document);

    pdf::PDFBitonalDocumentCreator creator(&document, context.getRasterizerPool(), nullptr);

    CancelledOperationControl operationControl;

    auto pageSizeGetter = [](const pdf::PDFPage*) { return QSize(64, 32); };

    // The pages are processed in parallel, so the counter is shared by several threads
    std::atomic<int> renderedPageCount = { 0 };
    auto pageImageProcessor = [&renderedPageCount](pdf::PDFInteger, QImage) { renderedPageCount.fetch_add(1, std::memory_order_relaxed); };

    // A cancelled rendering must not produce anything - the preview, which has asked
    // for it, has already thrown its result away and it is waiting for the worker
    creator.renderPages({ 0, 1, 2 }, pageSizeGetter, pageImageProcessor, &operationControl);
    QCOMPARE(renderedPageCount.load(), 0);

    QVERIFY(creator.renderPage(0, QSize(64, 32), &operationControl).isNull());

    // Without the cancellation the very same request produces the images
    creator.renderPages({ 0, 1, 2 }, pageSizeGetter, pageImageProcessor, nullptr);
    QCOMPARE(renderedPageCount.load(), 3);

    QVERIFY(!creator.renderPage(0, QSize(64, 32), nullptr).isNull());
}

void BitonalDocumentTest::test_page_conversion_is_cancellable_while_compiling()
{
    pdf::PDFDocument document = createDocument({ QSizeF(200, 100) }, false);

    // The pool has a single rasterizer and the test holds it, so a rendering, which
    // does not notice the cancellation after the compilation of the page, would wait
    // for the rasterizer forever
    RenderingContext context(&document, 1);
    pdf::PDFRasterizerPool* pool = context.getRasterizerPool();
    pdf::PDFRasterizer* heldRasterizer = pool->acquire();
    QVERIFY(heldRasterizer);

    pdf::PDFBitonalDocumentCreator creator(&document, pool, nullptr);

    // The first query passes, so the rendering starts, every following query reports
    // the cancellation - the compilation of the page is interrupted
    CountingOperationControl operationControl(1);

    std::atomic<int> renderedPageCount = { 0 };
    std::atomic<bool> isFinished = { false };

    std::thread thread([&]()
    {
        creator.renderPages({ 0 }, [](const pdf::PDFPage*) { return QSize(64, 32); }, [&renderedPageCount](pdf::PDFInteger, QImage) { renderedPageCount.fetch_add(1, std::memory_order_relaxed); }, &operationControl);
        isFinished.store(true, std::memory_order_release);
    });

    QElapsedTimer timer;
    timer.start();

    while (!isFinished.load(std::memory_order_acquire) && timer.elapsed() < 10000)
    {
        QTest::qWait(10);
    }

    const bool hasFinished = isFinished.load(std::memory_order_acquire);

    // The rasterizer is released in any case, so a blocked worker can finish and be joined
    pool->release(heldRasterizer);
    thread.join();

    QVERIFY2(hasFinished, "The cancelled rendering has waited for the rasterizer.");
    QCOMPARE(renderedPageCount.load(), 0);
}

void BitonalDocumentTest::test_page_conversion_is_cancellable_while_waiting_for_rasterizer()
{
    pdf::PDFDocument document = createDocument({ QSizeF(200, 100) }, false);

    // The pool has a single rasterizer and the test holds it, so the rendering has to
    // wait for it. The operation is cancelled while it is waiting, so it must give up
    // the waiting - a cancelled task must not occupy the rasterizer, which a newer
    // task is waiting for.
    RenderingContext context(&document, 1);
    pdf::PDFRasterizerPool* pool = context.getRasterizerPool();
    pdf::PDFRasterizer* heldRasterizer = pool->acquire();
    QVERIFY(heldRasterizer);

    pdf::PDFBitonalDocumentCreator creator(&document, pool, nullptr);

    constexpr qint64 CANCEL_AFTER_MS = 200;
    TimedOperationControl operationControl(CANCEL_AFTER_MS);

    std::atomic<int> renderedPageCount = { 0 };
    std::atomic<bool> isFinished = { false };

    QElapsedTimer timer;
    timer.start();

    std::thread thread([&]()
    {
        creator.renderPages({ 0 }, [](const pdf::PDFPage*) { return QSize(64, 32); }, [&renderedPageCount](pdf::PDFInteger, QImage) { renderedPageCount.fetch_add(1, std::memory_order_relaxed); }, &operationControl);
        isFinished.store(true, std::memory_order_release);
    });

    while (!isFinished.load(std::memory_order_acquire) && timer.elapsed() < 10000)
    {
        QTest::qWait(10);
    }

    const bool hasFinished = isFinished.load(std::memory_order_acquire);
    const qint64 elapsed = timer.elapsed();

    pool->release(heldRasterizer);
    thread.join();

    QVERIFY2(hasFinished, "The cancelled rendering has waited for the rasterizer.");
    QVERIFY(elapsed >= CANCEL_AFTER_MS);
    QCOMPARE(renderedPageCount.load(), 0);

    // The pool is usable after that - the rasterizer has been returned
    QVERIFY(!creator.renderPage(0, QSize(64, 32), nullptr).isNull());
}

void BitonalDocumentTest::test_page_conversion_fails_on_rendering_error()
{
    // The second page paints a valid black rectangle and an image, whose data are
    // damaged. The renderer skips the image and renders the rest of the page, so
    // it returns a valid image, which does not show the whole content of the page.
    pdf::PDFDocument document = createDocument({ QSizeF(200, 100), QSizeF(200, 100) }, false, { 1 });
    RenderingContext context(&document);

    pdf::PDFBitonalDocumentCreator creator(&document, context.getRasterizerPool(), nullptr);

    // The rendering of the damaged page reports the failure by a null image, the
    // valid page is rendered
    QVERIFY(!creator.renderPage(0, QSize(64, 32), nullptr).isNull());
    QVERIFY(creator.renderPage(1, QSize(64, 32), nullptr).isNull());

    const pdf::PDFPage* originalPage = document.getCatalog()->getPage(1);
    QVERIFY(originalPage);
    const pdf::PDFDictionary* originalPageDictionary = getPageDictionary(document, 1);
    QVERIFY(originalPageDictionary);
    const pdf::PDFObject originalContents = originalPageDictionary->get("Contents");
    QVERIFY(originalContents.isReference());

    // Such an image must not replace the content of the page - the page is left as it
    // is and it is counted as failed, so the caller can warn the user
    QVERIFY(creator.createBitonalDocument(createPageSettings(2)));
    QCOMPARE(creator.getConvertedItemCount(), size_t(1));
    QCOMPARE(creator.getFailedItemCount(), size_t(1));

    const pdf::PDFDocument bitonalDocument = creator.takeBitonalDocument();
    QVERIFY(getPageBitonalImage(bitonalDocument, 0));
    QVERIFY(!getPageBitonalImage(bitonalDocument, 1));

    const pdf::PDFDictionary* failedPageDictionary = getPageDictionary(bitonalDocument, 1);
    QVERIFY(failedPageDictionary);
    QVERIFY(failedPageDictionary->get("Contents") == originalContents);
    QVERIFY(bitonalDocument.getObjectByReference(originalContents.getReference()).isStream());

    // A document, whose only page is damaged, is not converted at all
    pdf::PDFDocument damagedDocument = createDocument({ QSizeF(200, 100) }, false, { 0 });
    RenderingContext damagedContext(&damagedDocument);
    pdf::PDFBitonalDocumentCreator damagedCreator(&damagedDocument, damagedContext.getRasterizerPool(), nullptr);
    QVERIFY(!damagedCreator.createBitonalDocument(createPageSettings(1)));
    QCOMPARE(damagedCreator.getConvertedItemCount(), size_t(0));
    QCOMPARE(damagedCreator.getFailedItemCount(), size_t(1));

    // Filling the page needs no rendering, so it works even for the damaged page
    pdf::PDFBitonalDocumentCreator::Settings fillSettings = createPageSettings(1);
    fillSettings.items.front().mode = pdf::PDFBitonalDocumentCreator::ItemMode::FillWhite;
    QVERIFY(damagedCreator.createBitonalDocument(fillSettings));
    QCOMPARE(damagedCreator.getConvertedItemCount(), size_t(1));
    QCOMPARE(damagedCreator.getFailedItemCount(), size_t(0));
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

void BitonalDocumentTest::test_page_conversion_prunes_structure_tree_when_partial()
{
    TaggedDocument tagged = createTaggedDocument();
    RenderingContext context(&tagged.document);

    pdf::PDFBitonalDocumentCreator creator(&tagged.document, context.getRasterizerPool(), nullptr);

    // Only the first page is converted
    QVERIFY(creator.createBitonalDocument(createPageSettings(1)));
    QCOMPARE(creator.getConvertedItemCount(), size_t(1));
    QCOMPARE(creator.getFailedItemCount(), size_t(0));

    const pdf::PDFDocument bitonalDocument = creator.takeBitonalDocument();
    pdf::PDFDocumentDataLoaderDecorator loader(&bitonalDocument);

    auto getDictionary = [&bitonalDocument](pdf::PDFObjectReference reference) { return bitonalDocument.getDictionaryFromObject(bitonalDocument.getObjectByReference(reference)); };

    // The structure tree survives, because the second page keeps its marked content
    const pdf::PDFDictionary* catalogDictionary = getCatalogDictionary(bitonalDocument);
    QVERIFY(catalogDictionary);
    QVERIFY(catalogDictionary->hasKey("StructTreeRoot"));
    QVERIFY(catalogDictionary->hasKey("MarkInfo"));
    QVERIFY(catalogDictionary->get("StructTreeRoot") == pdf::PDFObject::createReference(tagged.root));

    QVERIFY(!getPageDictionary(bitonalDocument, 0)->hasKey("StructParents"));
    QCOMPARE(loader.readIntegerFromDictionary(getPageDictionary(bitonalDocument, 1), "StructParents", -1), pdf::PDFInteger(1));

    // The annotation of the converted page is kept alive
    QCOMPARE(getReferenceArray(bitonalDocument, getPageDictionary(bitonalDocument, 0), "Annots"), std::vector<pdf::PDFObjectReference>({ tagged.annotation }));
    QVERIFY(getDictionary(tagged.annotation));

    // The paragraph of the converted page and the figure referring to its image have
    // lost all their content, so they are gone from the document element and from
    // the document
    const pdf::PDFDictionary* documentElement = getDictionary(tagged.documentElement);
    QVERIFY(documentElement);
    QCOMPARE(getReferenceArray(bitonalDocument, documentElement, "K"), std::vector<pdf::PDFObjectReference>({ tagged.paragraph2, tagged.span, tagged.link, tagged.emptyElement }));
    QVERIFY(bitonalDocument.getObjectByReference(tagged.paragraph1).isNull());
    QVERIFY(bitonalDocument.getObjectByReference(tagged.figure).isNull());
    QVERIFY(bitonalDocument.getObjectByReference(tagged.image).isNull());

    // The paragraph of the other page is untouched, except that it does not refer to
    // the removed paragraph anymore
    const pdf::PDFDictionary* paragraph2 = getDictionary(tagged.paragraph2);
    QVERIFY(paragraph2);
    QVERIFY(paragraph2->get("K") == tagged.document.getObjectByReference(tagged.paragraph2).getDictionary()->get("K"));
    QCOMPARE(getReferenceArray(bitonalDocument, paragraph2, "Ref"), std::vector<pdf::PDFObjectReference>({ tagged.span }));

    // The span keeps only the marked content of the other page
    const pdf::PDFDictionary* span = getDictionary(tagged.span);
    QVERIFY(span);
    const pdf::PDFObject spanKids = bitonalDocument.getObject(span->get("K"));
    QVERIFY(spanKids.isArray());
    QCOMPARE(spanKids.getArray()->getCount(), size_t(1));
    const pdf::PDFDictionary* markedContentReference = bitonalDocument.getDictionaryFromObject(spanKids.getArray()->getItem(0));
    QVERIFY(markedContentReference);
    QCOMPARE(loader.readNameFromDictionary(markedContentReference, "Type"), QByteArray("MCR"));
    QCOMPARE(loader.readIntegerFromDictionary(markedContentReference, "MCID", -1), pdf::PDFInteger(1));
    QCOMPARE(loader.readReferenceFromDictionary(markedContentReference, "Pg"), tagged.pages[1]);

    // The link refers to the annotation, which is kept, so it is untouched
    const pdf::PDFDictionary* link = getDictionary(tagged.link);
    QVERIFY(link);
    QVERIFY(link->get("K") == tagged.document.getObjectByReference(tagged.link).getDictionary()->get("K"));

    // The element without any content is untouched
    QVERIFY(bitonalDocument.getObjectByReference(tagged.emptyElement) == tagged.document.getObjectByReference(tagged.emptyElement));

    // The parent tree has lost the key of the converted page and the key of the removed
    // figure, the ID tree has lost the removed elements
    const pdf::PDFDictionary* root = getDictionary(tagged.root);
    QVERIFY(root);
    QCOMPARE(loader.readIntegerFromDictionary(root, "ParentTreeNextKey", -1), pdf::PDFInteger(4));

    const pdf::PDFDictionary* parentTree = bitonalDocument.getDictionaryFromObject(root->get("ParentTree"));
    QVERIFY(parentTree);
    QVERIFY(!parentTree->hasKey("Kids"));
    const pdf::PDFObject numbers = bitonalDocument.getObject(parentTree->get("Nums"));
    QVERIFY(numbers.isArray());
    QCOMPARE(numbers.getArray()->getCount(), size_t(4));
    QCOMPARE(numbers.getArray()->getItem(0).getInteger(), pdf::PDFInteger(1));
    QCOMPARE(getReferenceArray(bitonalDocument, parentTree, "Nums").size(), size_t(4));
    const pdf::PDFObject page1Parents = bitonalDocument.getObject(numbers.getArray()->getItem(1));
    QVERIFY(page1Parents.isArray());
    QCOMPARE(page1Parents.getArray()->getCount(), size_t(2));
    QVERIFY(page1Parents.getArray()->getItem(0) == pdf::PDFObject::createReference(tagged.paragraph2));
    QVERIFY(page1Parents.getArray()->getItem(1) == pdf::PDFObject::createReference(tagged.span));
    QCOMPARE(numbers.getArray()->getItem(2).getInteger(), pdf::PDFInteger(2));
    QVERIFY(numbers.getArray()->getItem(3) == pdf::PDFObject::createReference(tagged.link));

    const pdf::PDFDictionary* idTree = bitonalDocument.getDictionaryFromObject(root->get("IDTree"));
    QVERIFY(idTree);
    const pdf::PDFObject names = bitonalDocument.getObject(idTree->get("Names"));
    QVERIFY(names.isArray());
    QCOMPARE(names.getArray()->getCount(), size_t(8));
    QCOMPARE(names.getArray()->getItem(0).getString(), QByteArray("empty"));
    QVERIFY(names.getArray()->getItem(1) == pdf::PDFObject::createReference(tagged.emptyElement));
    QCOMPARE(names.getArray()->getItem(2).getString(), QByteArray("link"));
    QVERIFY(names.getArray()->getItem(3) == pdf::PDFObject::createReference(tagged.link));
    QCOMPARE(names.getArray()->getItem(4).getString(), QByteArray("p2"));
    QVERIFY(names.getArray()->getItem(5) == pdf::PDFObject::createReference(tagged.paragraph2));
    QCOMPARE(names.getArray()->getItem(6).getString(), QByteArray("span"));
    QVERIFY(names.getArray()->getItem(7) == pdf::PDFObject::createReference(tagged.span));

    // Converting the other page as well removes the whole tree
    pdf::PDFBitonalDocumentCreator completeCreator(&tagged.document, context.getRasterizerPool(), nullptr);
    QVERIFY(completeCreator.createBitonalDocument(createPageSettings(2)));
    QCOMPARE(completeCreator.getConvertedItemCount(), size_t(2));

    const pdf::PDFDocument completeDocument = completeCreator.takeBitonalDocument();
    QVERIFY(!getCatalogDictionary(completeDocument)->hasKey("StructTreeRoot"));
    QVERIFY(completeDocument.getObjectByReference(tagged.root).isNull());
    QVERIFY(completeDocument.getObjectByReference(tagged.span).isNull());
    QVERIFY(completeDocument.getDictionaryFromObject(completeDocument.getObjectByReference(tagged.annotation)));

    // Converting only the second page keeps the elements of the first page and prunes
    // the span the other way round - the span keeps its own marked content
    pdf::PDFBitonalDocumentCreator secondCreator(&tagged.document, context.getRasterizerPool(), nullptr);
    pdf::PDFBitonalDocumentCreator::Settings secondPageSettings = createPageSettings(2);
    secondPageSettings.items.erase(secondPageSettings.items.begin());
    QVERIFY(secondCreator.createBitonalDocument(secondPageSettings));
    QCOMPARE(secondCreator.getConvertedItemCount(), size_t(1));

    const pdf::PDFDocument secondDocument = secondCreator.takeBitonalDocument();
    const pdf::PDFDictionary* secondDocumentElement = secondDocument.getDictionaryFromObject(secondDocument.getObjectByReference(tagged.documentElement));
    QVERIFY(secondDocumentElement);
    QCOMPARE(getReferenceArray(secondDocument, secondDocumentElement, "K"), std::vector<pdf::PDFObjectReference>({ tagged.paragraph1, tagged.span, tagged.link, tagged.figure, tagged.emptyElement }));
    QVERIFY(secondDocument.getObjectByReference(tagged.paragraph2).isNull());

    const pdf::PDFDictionary* secondSpan = secondDocument.getDictionaryFromObject(secondDocument.getObjectByReference(tagged.span));
    QVERIFY(secondSpan);
    const pdf::PDFObject secondSpanKids = secondDocument.getObject(secondSpan->get("K"));
    QVERIFY(secondSpanKids.isArray());
    QCOMPARE(secondSpanKids.getArray()->getCount(), size_t(1));
    QCOMPARE(secondSpanKids.getArray()->getItem(0).getInteger(), pdf::PDFInteger(1));

    const pdf::PDFDictionary* secondParagraph1 = secondDocument.getDictionaryFromObject(secondDocument.getObjectByReference(tagged.paragraph1));
    QVERIFY(secondParagraph1);
    QVERIFY(!secondParagraph1->hasKey("Ref"));
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
