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

#include "pdfcreatebitonaldocumentdialog.h"
#include "ui_pdfcreatebitonaldocumentdialog.h"

#include "pdfwidgetutils.h"
#include "pdfdocumentwriter.h"
#include "pdfdocumentbuilder.h"
#include "pdfdrawspacecontroller.h"
#include "pdfimage.h"
#include "pdfexception.h"
#include "pdfimageconversion.h"
#include "pdfoptimizer.h"
#include "pdfoptionalcontent.h"
#include "pdfpage.h"
#include "pdfrenderer.h"
#include "pdfstreamfilters.h"
#include "pdfutils.h"

#include <QCheckBox>
#include <QPushButton>
#include <QElapsedTimer>
#include <QtConcurrent/QtConcurrent>
#include <QListWidget>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QScopeGuard>
#include <QtSvg/QSvgRenderer>
#include <QMouseEvent>
#include <QToolTip>

#include "pdfdbgheap.h"

namespace pdfviewer
{

/// Entries of the original image dictionary, which are still valid for the converted
/// bitonal image. Entries describing the image samples (size, color space, filters)
/// and the transparency (soft masks) are always replaced by the converted ones.
static constexpr const char* PRESERVED_IMAGE_DICTIONARY_KEYS[] =
{
    "Intent",
    "Interpolate",
    "OC",
    "Metadata",
    "StructParent",
    "AF",
    "Measure",
    "PtData"
};

/// Returns the renderer features used to rasterize the pages. Annotations are
/// deliberately not rendered - they are kept as live annotation objects in the
/// converted document, so they would be painted twice.
static pdf::PDFRenderer::Features getPageRasterizationFeatures()
{
    return pdf::PDFRenderer::Features(pdf::PDFRenderer::Antialiasing |
                                      pdf::PDFRenderer::TextAntialiasing |
                                      pdf::PDFRenderer::SmoothImages |
                                      pdf::PDFRenderer::ClipToCropBox);
}

/// Composites the rasterized page onto the white background. Areas of the page,
/// which are not painted at all, are transparent in the rasterized image, but
/// they represent a blank paper, so they must become white.
static QImage compositePageImageOntoWhite(const QImage& image)
{
    if (image.isNull())
    {
        return image;
    }

    QImage result(image.size(), QImage::Format_RGB32);
    result.fill(Qt::white);

    QPainter painter(&result);
    painter.drawImage(0, 0, image);
    painter.end();

    return result;
}

PDFCreateBitonalDocumentPreviewWidget::PDFCreateBitonalDocumentPreviewWidget(QWidget* parent) :
    QWidget(parent)
{

}

PDFCreateBitonalDocumentPreviewWidget::~PDFCreateBitonalDocumentPreviewWidget()
{

}

void PDFCreateBitonalDocumentPreviewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);

    // Caption rect
    QRect captionRect = rect();
    captionRect.setHeight(painter.fontMetrics().lineSpacing() * 2);

    painter.fillRect(captionRect, QColor::fromRgb(0, 0, 128, 255));

    if (!m_caption.isEmpty())
    {
        painter.setPen(Qt::white);
        painter.drawText(captionRect, m_caption, QTextOption(Qt::AlignCenter));
    }

    QRect imageRect = rect();
    imageRect.setTop(captionRect.bottom());
    imageRect = imageRect.adjusted(16, 16, -32, -32);

    if (imageRect.isValid() && !m_image.isNull())
    {
        QRect imageDrawRect = imageRect;
        imageDrawRect.setSize(m_image.size().scaled(imageRect.size(), Qt::KeepAspectRatio));
        imageDrawRect.moveCenter(imageRect.center());
        painter.drawImage(imageDrawRect, m_image);
    }
}

void PDFCreateBitonalDocumentPreviewWidget::setCaption(QString caption)
{
    if (m_caption != caption)
    {
        m_caption = caption;
        update();
    }
}

void PDFCreateBitonalDocumentPreviewWidget::setImage(QImage image)
{
    m_image = std::move(image);
    update();
}

PDFCreateBitonalDocumentDialog::PDFCreateBitonalDocumentDialog(const pdf::PDFDocument* document,
                                                               pdf::PDFDrawWidgetProxy* proxy,
                                                               const pdf::PDFCMS* cms,
                                                               pdf::PDFProgress* progress,
                                                               QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFCreateBitonalDocumentDialog),
    m_document(document),
    m_proxy(proxy),
    m_cms(cms),
    m_createBitonalDocumentButton(nullptr),
    m_conversionInProgress(false),
    m_processed(false),
    m_leftPreviewWidget(new PDFCreateBitonalDocumentPreviewWidget(this)),
    m_rightPreviewWidget(new PDFCreateBitonalDocumentPreviewWidget(this)),
    m_progress(progress),
    m_optionalContentActivity(nullptr),
    m_rasterizerPool(nullptr)
{
    ui->setupUi(this);

    m_leftPreviewWidget->setCaption(tr("ORIGINAL"));
    m_rightPreviewWidget->setCaption(tr("BITONAL"));

    ui->mainGridLayout->addWidget(m_leftPreviewWidget, 1, 1);
    ui->mainGridLayout->addWidget(m_rightPreviewWidget, 1, 2);

    m_classifier.classify(document);
    m_imageReferences = m_classifier.getObjectsByType(pdf::PDFObjectClassifier::Image);

    m_optionalContentActivity = new pdf::PDFOptionalContentActivity(document, pdf::OCUsage::Export, this);
    m_rasterizerPool = new pdf::PDFRasterizerPool(m_document,
                                                  m_proxy->getFontCache(),
                                                  m_proxy->getCMSManager(),
                                                  m_optionalContentActivity,
                                                  getPageRasterizationFeatures(),
                                                  m_proxy->getMeshQualitySettings(),
                                                  pdf::PDFRasterizerPool::getDefaultRasterizerCount(),
                                                  m_proxy->getRendererEngine(),
                                                  this);

    ui->conversionSourceComboBox->addItem(tr("Images"), static_cast<int>(ConversionSource::Images));
    ui->conversionSourceComboBox->addItem(tr("Whole pages"), static_cast<int>(ConversionSource::Pages));

    ui->conversionMethodComboBox->addItem(tr("Automatic (Otsu's 1D method)"), static_cast<int>(pdf::PDFImageConversion::ConversionMethod::Automatic));
    ui->conversionMethodComboBox->addItem(tr("User-defined threshold"), static_cast<int>(pdf::PDFImageConversion::ConversionMethod::Manual));
    ui->conversionMethodComboBox->addItem(tr("Adaptive thresholding"), static_cast<int>(pdf::PDFImageConversion::ConversionMethod::Adaptive));
    ui->conversionMethodComboBox->addItem(tr("Dithering (Floyd-Steinberg)"), static_cast<int>(pdf::PDFImageConversion::ConversionMethod::Dither));

    // Rasterizing the page is expensive, so the preview is not updated
    // while the resolution is being typed
    ui->resolutionEditBox->setKeyboardTracking(false);
    ui->resolutionEditBox->setValue(getEstimatedDpiResolution());

    m_createBitonalDocumentButton = ui->buttonBox->addButton(tr("Perform"), QDialogButtonBox::ActionRole);
    connect(m_createBitonalDocumentButton, &QPushButton::clicked, this, &PDFCreateBitonalDocumentDialog::onCreateBitonalDocumentButtonClicked);
    connect(ui->conversionSourceComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &PDFCreateBitonalDocumentDialog::onConversionSourceChanged);
    connect(ui->conversionMethodComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &PDFCreateBitonalDocumentDialog::onConversionSettingsChanged);
    connect(ui->imageListWidget, &QListWidget::currentItemChanged, this, &PDFCreateBitonalDocumentDialog::updatePreview);
    connect(ui->thresholdEditBox, qOverload<int>(&QSpinBox::valueChanged), this, &PDFCreateBitonalDocumentDialog::onConversionSettingsChanged);
    connect(ui->resolutionEditBox, qOverload<int>(&QSpinBox::valueChanged), this, &PDFCreateBitonalDocumentDialog::onConversionSettingsChanged);

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(1024, 768));
    updateUi();
    pdf::PDFWidgetUtils::style(this);

    ImagePreviewDelegate* delegate = new ImagePreviewDelegate(&m_itemsToBeConverted, this);
    connect(delegate, &ImagePreviewDelegate::conversionEnabledChanged, this, &PDFCreateBitonalDocumentDialog::onConversionEnabledChanged);
    ui->imageListWidget->setItemDelegate(delegate);

    setGeometry(parent->geometry());

    loadItems();
    updatePreview();
}

PDFCreateBitonalDocumentDialog::~PDFCreateBitonalDocumentDialog()
{
    Q_ASSERT(!m_conversionInProgress);
    Q_ASSERT(!m_future.isRunning());

    delete ui;
}

void PDFCreateBitonalDocumentDialog::onPerformFinished()
{
    m_processed = m_future.result();
    m_conversionInProgress = false;
    updateUi();
}

bool PDFCreateBitonalDocumentDialog::createBitonalDocument(const ConversionSettings& settings)
{
    try
    {
        pdf::PDFDocumentBuilder builder(m_document);
        bool isConverted = false;

        switch (settings.conversionSource)
        {
            case ConversionSource::Images:
                isConverted = createBitonalDocumentFromImages(builder, settings);
                break;

            case ConversionSource::Pages:
                isConverted = createBitonalDocumentFromPages(builder, settings);
                break;

            default:
                Q_ASSERT(false);
                break;
        }

        if (!isConverted)
        {
            // Nothing has been converted
            return false;
        }

        pdf::PDFDocument builtDocument = builder.build();

        // Images and content streams, which have been replaced, are not referenced
        // by the document anymore. They must be removed, otherwise the converted
        // document would be even larger than the original one.
        pdf::PDFOptimizer optimizer(pdf::PDFOptimizer::RemoveUnusedObjects, nullptr);
        optimizer.setDocument(&builtDocument);
        optimizer.optimize();

        m_bitonalDocument = pdf::PDFDocument(optimizer.takeStorage(), builtDocument.getInfo()->version, QByteArray());
        return true;
    }
    catch (const pdf::PDFException&)
    {
        m_bitonalDocument = pdf::PDFDocument();
        return false;
    }
}

bool PDFCreateBitonalDocumentDialog::createBitonalDocumentFromImages(pdf::PDFDocumentBuilder& builder, const ConversionSettings& settings)
{
    std::vector<ConversionItemInfo> itemsToBeConverted;
    std::copy_if(settings.items.begin(), settings.items.end(), std::back_inserter(itemsToBeConverted), [](const auto& item) { return item.conversionEnabled; });

    // Do we have something to be converted?
    if (itemsToBeConverted.empty())
    {
        return false;
    }

    pdf::ProgressStartupInfo info;
    info.showDialog = true;
    info.text = tr("Converting images...");
    m_progress->start(itemsToBeConverted.size(), std::move(info));

    // The progress must be finished even when an exception escapes from this function
    auto progressGuard = qScopeGuard([this]() { m_progress->finish(); });

    bool isConverted = false;
    pdf::PDFCMSGeneric genericCms;
    pdf::PDFRenderErrorReporterDummy errorReporter;

    for (const ConversionItemInfo& item : itemsToBeConverted)
    {
        const pdf::PDFObjectReference reference = item.imageReference;
        std::optional<pdf::PDFImage> pdfImage = getImageFromReference(reference);

        QImage image;
        if (pdfImage)
        {
            try
            {
                image = pdfImage->getImage(&genericCms, &errorReporter, nullptr);
            }
            catch (const pdf::PDFException&)
            {
                // Do nothing
            }
        }

        if (image.isNull())
        {
            m_progress->step();
            continue;
        }

        QImage alphaMask;
        QImage bitonalImage = convertImageToBitonal(image, settings.conversionMethod, settings.manualThreshold, &alphaMask);
        pdf::PDFObject imageObject = createBitonalImageObject(bitonalImage);

        if (!imageObject.isNull())
        {
            pdf::PDFDictionary dictionary = *imageObject.getStream()->getDictionary();
            QByteArray content = *imageObject.getStream()->getContent();

            // Transfer the entries of the original image, which are not related
            // to the image samples, into the converted image.
            if (const pdf::PDFDictionary* originalDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(reference)))
            {
                for (const char* key : PRESERVED_IMAGE_DICTIONARY_KEYS)
                {
                    if (originalDictionary->hasKey(key))
                    {
                        dictionary.setEntry(pdf::PDFInplaceOrMemoryString(key), pdf::PDFObject(originalDictionary->get(key)));
                    }
                }
            }

            // The original image can be transparent - it can have a soft mask, a stencil
            // mask, a color key mask, or an alpha channel stored directly in the image
            // data. All these variants are decoded into the alpha channel of the decoded
            // image, so a single soft mask created from that alpha channel replaces them.
            if (!alphaMask.isNull())
            {
                pdf::PDFObject softMaskObject = createBitonalImageObject(alphaMask);

                if (!softMaskObject.isNull())
                {
                    pdf::PDFObjectReference softMaskReference = builder.addObject(std::move(softMaskObject));
                    dictionary.setEntry(pdf::PDFInplaceOrMemoryString("SMask"), pdf::PDFObject::createReference(softMaskReference));
                }
            }

            builder.setObject(reference, pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(std::move(dictionary), std::move(content))));
            isConverted = true;
        }

        m_progress->step();
    }

    return isConverted;
}

bool PDFCreateBitonalDocumentDialog::createBitonalDocumentFromPages(pdf::PDFDocumentBuilder& builder, const ConversionSettings& settings)
{
    std::vector<pdf::PDFInteger> pageIndices;

    for (const ConversionItemInfo& item : settings.items)
    {
        if (item.conversionEnabled && item.pageIndex >= 0)
        {
            pageIndices.push_back(item.pageIndex);
        }
    }

    // Do we have something to be converted?
    if (pageIndices.empty())
    {
        return false;
    }

    pdf::ProgressStartupInfo info;
    info.showDialog = true;
    info.text = tr("Converting pages...");
    m_progress->start(pageIndices.size(), std::move(info));

    // The progress must be finished even when an exception escapes from this function
    auto progressGuard = qScopeGuard([this]() { m_progress->finish(); });

    bool isConverted = false;
    const pdf::PDFCatalog* catalog = m_document->getCatalog();
    const int dpiResolution = settings.dpiResolution;

    // Pages are rasterized and converted in parallel, but the converted images are
    // stored as encoded image streams only - the rasterized images are large and we
    // do not want to keep all of them in the memory at once.
    std::vector<pdf::PDFObject> imageObjects(catalog->getPageCount());

    auto pageSizeGetter = [dpiResolution](const pdf::PDFPage* page) { return getPageImageSize(page, dpiResolution); };
    auto pageImageProcessor = [this, &imageObjects, &settings](pdf::PDFInteger pageIndex, QImage image)
    {
        QImage bitonalImage = convertImageToBitonal(image, settings.conversionMethod, settings.manualThreshold, nullptr);
        imageObjects[size_t(pageIndex)] = createBitonalImageObject(bitonalImage);
        m_progress->step();
    };

    renderPages(pageIndices, pageSizeGetter, pageImageProcessor);

    const bool isWholeDocumentConverted = pageIndices.size() == catalog->getPageCount();

    for (const pdf::PDFInteger pageIndex : pageIndices)
    {
        pdf::PDFObject& imageObject = imageObjects[size_t(pageIndex)];
        const pdf::PDFPage* page = catalog->getPage(size_t(pageIndex));

        if (imageObject.isNull() || !page)
        {
            continue;
        }

        const QRectF mediaBox = page->getMediaBox();
        if (mediaBox.isEmpty())
        {
            continue;
        }

        const pdf::PDFObjectReference imageReference = builder.addObject(std::move(imageObject));

        // Image is placed onto the whole media box of the page. The media box is
        // expressed in the coordinate system of the page, in which the y axis points
        // upwards, so the lower left corner of the box is (left(), top()).
        QByteArray contentStream = QString("q %1 0 0 %2 %3 %4 cm /BitonalImage Do Q")
                                       .arg(mediaBox.width(), 0, 'f', 6)
                                       .arg(mediaBox.height(), 0, 'f', 6)
                                       .arg(mediaBox.left(), 0, 'f', 6)
                                       .arg(mediaBox.top(), 0, 'f', 6).toLatin1();
        QByteArray compressedContentStream = pdf::PDFFlateDecodeFilter::compress(contentStream);

        pdf::PDFDictionary contentStreamDictionary;
        contentStreamDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createName("FlateDecode"));
        contentStreamDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(compressedContentStream.size()));

        const pdf::PDFObjectReference contentStreamReference = builder.addObject(pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(std::move(contentStreamDictionary), std::move(compressedContentStream))));

        pdf::PDFDictionary xobjectDictionary;
        xobjectDictionary.setEntry(pdf::PDFInplaceOrMemoryString("BitonalImage"), pdf::PDFObject::createReference(imageReference));

        pdf::PDFArray procSetArray;
        procSetArray.appendItem(pdf::PDFObject::createName("PDF"));
        procSetArray.appendItem(pdf::PDFObject::createName("ImageB"));

        pdf::PDFDictionary resourcesDictionary;
        resourcesDictionary.setEntry(pdf::PDFInplaceOrMemoryString("XObject"), pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(xobjectDictionary))));
        resourcesDictionary.setEntry(pdf::PDFInplaceOrMemoryString("ProcSet"), pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(std::move(procSetArray))));

        const pdf::PDFObjectReference pageReference = page->getPageReference();
        const pdf::PDFDictionary* originalPageDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(pageReference));

        if (!originalPageDictionary)
        {
            continue;
        }

        // Everything except the page content is preserved - the page keeps its size,
        // rotation, annotations and other properties.
        pdf::PDFDictionary pageDictionary = *originalPageDictionary;
        pageDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentStreamReference));
        pageDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(resourcesDictionary))));

        // The marked content of the page is gone, so the page must not be a part
        // of the structure tree anymore.
        pageDictionary.removeEntry("StructParents");

        // The embedded thumbnail shows the original, colored content of the page.
        // Keeping it would both display a wrong preview in the viewer and prevent
        // the removal of the unused objects it references.
        pageDictionary.removeEntry("Thumb");

        builder.setObject(pageReference, pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(pageDictionary))));
        isConverted = true;
    }

    if (isConverted && isWholeDocumentConverted)
    {
        // No page is tagged anymore, so the whole structure tree can be removed
        const pdf::PDFObjectReference catalogReference = builder.getCatalogReference();

        if (const pdf::PDFDictionary* originalCatalogDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(catalogReference)))
        {
            pdf::PDFDictionary catalogDictionary = *originalCatalogDictionary;
            catalogDictionary.removeEntry("StructTreeRoot");
            catalogDictionary.removeEntry("MarkInfo");
            builder.setObject(catalogReference, pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(catalogDictionary))));
        }
    }

    return isConverted;
}

QImage PDFCreateBitonalDocumentDialog::convertImageToBitonal(const QImage& image,
                                                             pdf::PDFImageConversion::ConversionMethod conversionMethod,
                                                             int threshold,
                                                             QImage* alphaMask)
{
    if (image.isNull())
    {
        return QImage();
    }

    pdf::PDFImageConversion imageConversion;
    imageConversion.setConversionMethod(conversionMethod);
    imageConversion.setThreshold(threshold);
    imageConversion.setAlphaMode(pdf::PDFImageConversion::AlphaMode::Composite);
    imageConversion.setImage(image);

    if (!imageConversion.convert())
    {
        return QImage();
    }

    if (alphaMask)
    {
        *alphaMask = imageConversion.getConvertedAlphaMask();
    }

    return imageConversion.getConvertedImage();
}

pdf::PDFObject PDFCreateBitonalDocumentDialog::createBitonalImageObject(const QImage& image)
{
    if (image.isNull())
    {
        return pdf::PDFObject();
    }

    try
    {
        pdf::PDFImage::ImageEncodeOptions options;
        options.compression = pdf::PDFImage::ImageCompression::Flate;
        options.colorMode = pdf::PDFImage::ImageColorMode::Monochrome;
        options.enablePngPredictor = true;

        pdf::PDFStream stream = pdf::PDFImage::createStreamFromImage(image, options, nullptr);

        pdf::PDFDictionary dictionary = *stream.getDictionary();
        QByteArray content = *stream.getContent();

        return pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(std::move(dictionary), std::move(content)));
    }
    catch (const pdf::PDFException&)
    {
        return pdf::PDFObject();
    }
}

void PDFCreateBitonalDocumentDialog::renderPages(const std::vector<pdf::PDFInteger>& pageIndices,
                                                 const std::function<QSize(const pdf::PDFPage*)>& pageSizeGetter,
                                                 const std::function<void(pdf::PDFInteger, QImage)>& pageImageProcessor) const
{
    const pdf::PDFCatalog* catalog = m_document->getCatalog();

    // Rasterizer always renders the page as it is displayed, i.e. with the page
    // rotation applied. We want the image in the coordinate system of the page,
    // so a rotated page is rendered into a transposed image, which is then rotated
    // back. Rotating by a multiple of 90 degrees is a lossless operation.
    auto imageSizeGetter = [&pageSizeGetter](const pdf::PDFPage* page) -> QSize
    {
        const QSize size = pageSizeGetter(page);
        const pdf::PageRotation rotation = page->getPageRotation();

        if (rotation == pdf::PageRotation::Rotate90 || rotation == pdf::PageRotation::Rotate270)
        {
            return size.transposed();
        }

        return size;
    };

    auto processImage = [catalog, &pageImageProcessor](pdf::PDFRenderedPageImage& renderedPageImage)
    {
        QImage image = compositePageImageOntoWhite(renderedPageImage.pageImage);
        const pdf::PDFPage* page = catalog->getPage(size_t(renderedPageImage.pageIndex));

        if (!image.isNull() && page)
        {
            QTransform transform;

            switch (page->getPageRotation())
            {
                case pdf::PageRotation::Rotate90:
                    transform.rotate(-90);
                    break;

                case pdf::PageRotation::Rotate180:
                    transform.rotate(180);
                    break;

                case pdf::PageRotation::Rotate270:
                    transform.rotate(90);
                    break;

                default:
                    break;
            }

            if (!transform.isIdentity())
            {
                image = image.transformed(transform);
            }
        }

        pageImageProcessor(renderedPageImage.pageIndex, std::move(image));
    };

    m_rasterizerPool->render(pageIndices, imageSizeGetter, processImage, nullptr);
}

QImage PDFCreateBitonalDocumentDialog::renderPage(pdf::PDFInteger pageIndex, QSize size) const
{
    QImage result;

    auto pageSizeGetter = [size](const pdf::PDFPage*) { return size; };
    auto pageImageProcessor = [&result](pdf::PDFInteger, QImage image) { result = std::move(image); };

    renderPages({ pageIndex }, pageSizeGetter, pageImageProcessor);

    return result;
}

QSize PDFCreateBitonalDocumentDialog::getPageImageSize(const pdf::PDFPage* page, int dpiResolution)
{
    Q_ASSERT(page);

    const QSizeF size = page->getMediaBox().size() * pdf::PDF_POINT_TO_INCH * dpiResolution;
    return size.toSize().expandedTo(QSize(1, 1));
}

int PDFCreateBitonalDocumentDialog::getEstimatedDpiResolution() const
{
    constexpr int DEFAULT_DPI_RESOLUTION = 300;
    constexpr int MAXIMUM_DPI_RESOLUTION = 600;

    pdf::PDFDocumentDataLoaderDecorator loader(m_document);
    int dpiResolution = 0;

    // Scanned documents store the page as an image covering the whole page. We estimate
    // the resolution from the size of these images, so the rasterized page keeps the
    // details of the original scan. We do not know, which part of the page the image
    // actually covers (that would require an analysis of the content stream), so the
    // estimate is only used to raise the resolution above the default one - a small
    // logo must never lower it.
    const pdf::PDFCatalog* catalog = m_document->getCatalog();
    for (size_t pageIndex = 0, pageCount = catalog->getPageCount(); pageIndex < pageCount; ++pageIndex)
    {
        const pdf::PDFPage* page = catalog->getPage(pageIndex);

        if (!page)
        {
            continue;
        }

        const QRectF mediaBox = page->getMediaBox();
        if (mediaBox.width() < 1.0 || mediaBox.height() < 1.0)
        {
            continue;
        }

        // Resources are an inheritable attribute of the page, so they can be stored
        // in a parent node of the page tree. PDFPage resolves the inheritance for us.
        const pdf::PDFDictionary* resourcesDictionary = m_document->getDictionaryFromObject(page->getResources());
        if (!resourcesDictionary)
        {
            continue;
        }

        const pdf::PDFDictionary* xobjectDictionary = m_document->getDictionaryFromObject(resourcesDictionary->get("XObject"));
        if (!xobjectDictionary)
        {
            continue;
        }

        for (size_t index = 0, count = xobjectDictionary->getCount(); index < count; ++index)
        {
            const pdf::PDFDictionary* imageDictionary = m_document->getDictionaryFromObject(xobjectDictionary->getValue(index));

            if (!imageDictionary || loader.readNameFromDictionary(imageDictionary, "Subtype") != "Image")
            {
                continue;
            }

            const pdf::PDFInteger width = loader.readIntegerFromDictionary(imageDictionary, "Width", 0);
            const pdf::PDFInteger height = loader.readIntegerFromDictionary(imageDictionary, "Height", 0);

            if (width > 0)
            {
                dpiResolution = qMax(dpiResolution, qRound(width / (mediaBox.width() * pdf::PDF_POINT_TO_INCH)));
            }

            if (height > 0)
            {
                dpiResolution = qMax(dpiResolution, qRound(height / (mediaBox.height() * pdf::PDF_POINT_TO_INCH)));
            }
        }
    }

    return qBound(DEFAULT_DPI_RESOLUTION, dpiResolution, MAXIMUM_DPI_RESOLUTION);
}

bool PDFCreateBitonalDocumentDialog::isStencilMask(pdf::PDFObjectReference reference) const
{
    if (const pdf::PDFDictionary* dictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(reference)))
    {
        pdf::PDFDocumentDataLoaderDecorator loader(m_document);
        return loader.readBooleanFromDictionary(dictionary, "ImageMask", false);
    }

    return false;
}

void PDFCreateBitonalDocumentDialog::onCreateBitonalDocumentButtonClicked()
{
    Q_ASSERT(!m_conversionInProgress);
    Q_ASSERT(!m_future.isRunning());

    // All inputs of the conversion are snapshotted here, in the GUI thread, and the
    // worker gets its own copy of them. The worker must not read the state of the
    // dialog, because the user could change it while the conversion is running.
    ConversionSettings settings = getConversionSettings();

    m_processed = false;
    m_bitonalDocument = pdf::PDFDocument();
    m_conversionInProgress = true;
    m_future = QtConcurrent::run([this, settings = std::move(settings)]() { return createBitonalDocument(settings); });
    m_futureWatcher.emplace();
    connect(&m_futureWatcher.value(), &QFutureWatcher<bool>::finished, this, &PDFCreateBitonalDocumentDialog::onPerformFinished);
    m_futureWatcher->setFuture(m_future);
    updateUi();
}

void PDFCreateBitonalDocumentDialog::onConversionSourceChanged()
{
    m_conversionSource = getSelectedConversionSource();

    invalidateResult();
    loadItems();
    updateUi();
    updatePreview();
}

void PDFCreateBitonalDocumentDialog::onConversionSettingsChanged()
{
    invalidateResult();
    updateUi();
    updatePreview();
}

void PDFCreateBitonalDocumentDialog::onConversionEnabledChanged()
{
    invalidateResult();
    ui->imageListWidget->viewport()->update();
}

void PDFCreateBitonalDocumentDialog::invalidateResult()
{
    if (!m_processed)
    {
        // Nothing has been created yet, or the conversion is just running.
        // In both cases there is no result, which could become obsolete.
        return;
    }

    m_processed = false;
    m_bitonalDocument = pdf::PDFDocument();
    updateUi();
}

PDFCreateBitonalDocumentDialog::ConversionSettings PDFCreateBitonalDocumentDialog::getConversionSettings() const
{
    ConversionSettings settings;
    settings.conversionSource = getSelectedConversionSource();
    settings.conversionMethod = getSelectedConversionMethod();
    settings.manualThreshold = ui->thresholdEditBox->value();
    settings.dpiResolution = ui->resolutionEditBox->value();
    settings.items = m_itemsToBeConverted;
    return settings;
}

void PDFCreateBitonalDocumentDialog::loadItems()
{
    QSignalBlocker blocker(ui->imageListWidget);

    ui->imageListWidget->clear();
    m_itemsToBeConverted.clear();

    m_cachedPageImage = QImage();
    m_cachedPageIndex = -1;
    m_cachedPageDpiResolution = 0;

    const QSize iconSize(QSize(256, 256));
    ui->imageListWidget->setIconSize(iconSize);
    const QSize thumbnailSize = iconSize * ui->imageListWidget->devicePixelRatioF();

    switch (m_conversionSource)
    {
        case ConversionSource::Images:
            loadImageItems(thumbnailSize);
            break;

        case ConversionSource::Pages:
            loadPageItems(thumbnailSize);
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    if (ui->imageListWidget->count() > 0)
    {
        ui->imageListWidget->setCurrentRow(0);
    }
}

void PDFCreateBitonalDocumentDialog::loadImageItems(QSize thumbnailSize)
{
    for (pdf::PDFObjectReference reference : m_imageReferences)
    {
        if (isStencilMask(reference))
        {
            // Stencil masks are bitonal images painted using the current fill
            // color - there is nothing to be converted.
            continue;
        }

        std::optional<pdf::PDFImage> pdfImage = getImageFromReference(reference);
        if (!pdfImage)
        {
            continue;
        }

        pdf::PDFCMSGeneric genericCms;
        pdf::PDFRenderErrorReporterDummy errorReporter;
        QImage image;

        try
        {
            image = pdfImage->getImage(&genericCms, &errorReporter, nullptr);
        }
        catch (const pdf::PDFException&)
        {
            // Do nothing
        }

        if (image.isNull())
        {
            continue;
        }

        QListWidgetItem* item = new QListWidgetItem(ui->imageListWidget);
        image = image.scaled(thumbnailSize.width(), thumbnailSize.height(), Qt::KeepAspectRatio, Qt::FastTransformation);
        item->setIcon(QIcon(QPixmap::fromImage(image)));
        Qt::ItemFlags flags = item->flags();
        flags.setFlag(Qt::ItemIsEditable, true);
        item->setFlags(flags);

        ConversionItemInfo conversionItemInfo;
        conversionItemInfo.imageReference = reference;
        conversionItemInfo.conversionEnabled = true;
        m_itemsToBeConverted.push_back(conversionItemInfo);
    }
}

void PDFCreateBitonalDocumentDialog::loadPageItems(QSize thumbnailSize)
{
    const pdf::PDFCatalog* catalog = m_document->getCatalog();
    const size_t pageCount = catalog->getPageCount();

    std::vector<pdf::PDFInteger> pageIndices;
    pageIndices.reserve(pageCount);

    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        pageIndices.push_back(pdf::PDFInteger(pageIndex));
    }

    std::vector<QImage> pageImages(pageCount);

    auto pageSizeGetter = [thumbnailSize](const pdf::PDFPage* page) -> QSize
    {
        QSizeF size = page->getMediaBox().size();
        size.scale(thumbnailSize.width(), thumbnailSize.height(), Qt::KeepAspectRatio);
        return size.toSize().expandedTo(QSize(1, 1));
    };

    auto pageImageProcessor = [&pageImages](pdf::PDFInteger pageIndex, QImage image)
    {
        pageImages[size_t(pageIndex)] = std::move(image);
    };

    renderPages(pageIndices, pageSizeGetter, pageImageProcessor);

    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        QListWidgetItem* item = new QListWidgetItem(ui->imageListWidget);
        item->setIcon(QIcon(QPixmap::fromImage(pageImages[pageIndex])));
        item->setToolTip(tr("Page %1").arg(pageIndex + 1));
        Qt::ItemFlags flags = item->flags();
        flags.setFlag(Qt::ItemIsEditable, true);
        item->setFlags(flags);

        ConversionItemInfo conversionItemInfo;
        conversionItemInfo.pageIndex = pdf::PDFInteger(pageIndex);
        conversionItemInfo.conversionEnabled = true;
        m_itemsToBeConverted.push_back(conversionItemInfo);
    }
}

void PDFCreateBitonalDocumentDialog::updateUi()
{
    m_conversionSource = getSelectedConversionSource();

    const pdf::PDFImageConversion::ConversionMethod conversionMethod = getSelectedConversionMethod();
    const bool usesThreshold = conversionMethod == pdf::PDFImageConversion::ConversionMethod::Manual ||
                               conversionMethod == pdf::PDFImageConversion::ConversionMethod::Dither;
    const bool usesResolution = m_conversionSource == ConversionSource::Pages;

    // While the conversion is running, no input can be changed - the worker is using
    // a snapshot of them and a modified input would not match the produced document.
    ui->thresholdLabel->setEnabled(usesThreshold && !m_conversionInProgress);
    ui->thresholdEditBox->setEnabled(usesThreshold && !m_conversionInProgress);
    ui->resolutionLabel->setEnabled(usesResolution && !m_conversionInProgress);
    ui->resolutionEditBox->setEnabled(usesResolution && !m_conversionInProgress);
    ui->conversionSourceLabel->setEnabled(!m_conversionInProgress);
    ui->conversionSourceComboBox->setEnabled(!m_conversionInProgress);
    ui->conversionMethodLabel->setEnabled(!m_conversionInProgress);
    ui->conversionMethodComboBox->setEnabled(!m_conversionInProgress);
    ui->imageListWidget->setEnabled(!m_conversionInProgress);

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_processed && !m_conversionInProgress);
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(!m_conversionInProgress);
    m_createBitonalDocumentButton->setEnabled(!m_conversionInProgress);
}

void PDFCreateBitonalDocumentDialog::updatePreview()
{
    QModelIndex index = ui->imageListWidget->currentIndex();

    m_previewImageLeft = QImage();
    m_previewImageRight = QImage();

    const pdf::PDFImageConversion::ConversionMethod conversionMethod = getSelectedConversionMethod();
    const int manualThreshold = ui->thresholdEditBox->value();
    const int dpiResolution = ui->resolutionEditBox->value();

    if (index.isValid() && index.row() < int(m_itemsToBeConverted.size()))
    {
        const ConversionItemInfo& info = m_itemsToBeConverted.at(index.row());
        QImage image;

        switch (m_conversionSource)
        {
            case ConversionSource::Images:
            {
                if (std::optional<pdf::PDFImage> pdfImage = getImageFromReference(info.imageReference))
                {
                    pdf::PDFCMSGeneric cmsGeneric;
                    pdf::PDFRenderErrorReporterDummy reporter;

                    try
                    {
                        image = pdfImage->getImage(&cmsGeneric, &reporter, nullptr);
                    }
                    catch (const pdf::PDFException&)
                    {
                        // Do nothing
                    }
                }
                break;
            }

            case ConversionSource::Pages:
            {
                if (m_cachedPageIndex != info.pageIndex || m_cachedPageDpiResolution != dpiResolution)
                {
                    const pdf::PDFPage* page = m_document->getCatalog()->getPage(size_t(info.pageIndex));
                    m_cachedPageImage = page ? renderPage(info.pageIndex, getPageImageSize(page, dpiResolution)) : QImage();
                    m_cachedPageIndex = info.pageIndex;
                    m_cachedPageDpiResolution = dpiResolution;
                }

                image = m_cachedPageImage;
                break;
            }

            default:
                Q_ASSERT(false);
                break;
        }

        QImage bitonalImage = convertImageToBitonal(image, conversionMethod, manualThreshold, nullptr);

        if (!bitonalImage.isNull())
        {
            m_previewImageLeft = std::move(image);
            m_previewImageRight = std::move(bitonalImage);
        }
    }

    m_leftPreviewWidget->setImage(m_previewImageLeft);
    m_rightPreviewWidget->setImage(m_previewImageRight);
}

pdf::PDFImageConversion::ConversionMethod PDFCreateBitonalDocumentDialog::getSelectedConversionMethod() const
{
    return static_cast<pdf::PDFImageConversion::ConversionMethod>(ui->conversionMethodComboBox->currentData().toInt());
}

PDFCreateBitonalDocumentDialog::ConversionSource PDFCreateBitonalDocumentDialog::getSelectedConversionSource() const
{
    return static_cast<ConversionSource>(ui->conversionSourceComboBox->currentData().toInt());
}

std::optional<pdf::PDFImage> PDFCreateBitonalDocumentDialog::getImageFromReference(pdf::PDFObjectReference reference) const
{
    std::optional<pdf::PDFImage> pdfImage;
    pdf::PDFObject imageObject = m_document->getObjectByReference(reference);
    pdf::PDFRenderErrorReporterDummy errorReporter;

    if (!imageObject.isStream())
    {
        // Image is not stream
        return pdfImage;
    }

    const pdf::PDFStream* stream = imageObject.getStream();
    try
    {
        pdf::PDFColorSpacePointer colorSpace;
        const pdf::PDFDictionary* streamDictionary = stream->getDictionary();
        if (streamDictionary->hasKey("ColorSpace"))
        {
            const pdf::PDFObject& colorSpaceObject = m_document->getObject(streamDictionary->get("ColorSpace"));
            if (colorSpaceObject.isName() || colorSpaceObject.isArray())
            {
                pdf::PDFDictionary dummyDictionary;
                colorSpace = pdf::PDFAbstractColorSpace::createColorSpace(&dummyDictionary, m_document, colorSpaceObject);
            }
        }
        pdfImage.emplace(pdf::PDFImage::createImage(m_document,
                                                    stream,
                                                    colorSpace,
                                                    false,
                                                    pdf::RenderingIntent::Perceptual,
                                                    &errorReporter));
    }
    catch (pdf::PDFException)
    {
        // Do nothing
    }

    return pdfImage;
}

ImagePreviewDelegate::ImagePreviewDelegate(std::vector<PDFCreateBitonalDocumentDialog::ConversionItemInfo>* conversionItemInfos, QObject *parent) :
    QStyledItemDelegate(parent),
    m_conversionItemInfos(conversionItemInfos)
{
    m_yesRenderer.load(QString(":/resources/result-ok.svg"));
    m_noRenderer.load(QString(":/resources/result-error.svg"));
}

void ImagePreviewDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    QRect markRect = getMarkRect(option);

    if (index.isValid() && index.row() < int(m_conversionItemInfos->size()))
    {
        const PDFCreateBitonalDocumentDialog::ConversionItemInfo& info = m_conversionItemInfos->at(index.row());
        if (info.conversionEnabled)
        {
            m_yesRenderer.render(painter, markRect);
        }
        else
        {
            m_noRenderer.render(painter, markRect);
        }
    }
}

bool ImagePreviewDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    Q_UNUSED(model);
    Q_UNUSED(index);

    if (event->type() == QEvent::MouseButtonPress && index.isValid() && index.row() < int(m_conversionItemInfos->size()))
    {
        QMouseEvent* mouseEvent = dynamic_cast<QMouseEvent*>(event);
        if (mouseEvent && mouseEvent->button() == Qt::LeftButton)
        {
            // Do we click on yes/no mark?
            QRectF markRect = getMarkRect(option);
            if (markRect.contains(mouseEvent->position()))
            {
                PDFCreateBitonalDocumentDialog::ConversionItemInfo& info = m_conversionItemInfos->at(index.row());
                info.conversionEnabled = !info.conversionEnabled;
                Q_EMIT conversionEnabledChanged();
                return true;
            }
        }
    }

    return false;
}

bool ImagePreviewDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    Q_UNUSED(index);

    if (!event || !view)
    {
        return false;
    }

    if (event->type() == QEvent::ToolTip)
    {
        // Are we hovering over yes/no mark?
        QRectF markRect = getMarkRect(option);
        if (markRect.contains(event->pos()))
        {
            event->accept();
            QToolTip::showText(event->globalPos(), tr("Toggle this icon to switch the conversion of this item to bitonal format on or off."), view);
            return true;
        }
    }

    return false;
}

QRect ImagePreviewDelegate::getMarkRect(const QStyleOptionViewItem& option) const
{
    QSize markSize = pdf::PDFWidgetUtils::scaleDPI(option.widget, s_iconSize);
    QRect markRect(option.rect.left(), option.rect.top(), markSize.width(), markSize.height());
    return markRect;
}

}   // namespace pdfviewer
