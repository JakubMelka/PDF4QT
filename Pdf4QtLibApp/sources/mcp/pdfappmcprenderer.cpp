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


#include "pdfappmcprenderer.h"
#include "pdfappmcpjob.h"
#include "pdfappmcparguments.h"
#include "pdfappmcpdocumentresources.h"
#include "pdfappmcphost.h"
#include "pdfappmcpreadsupport.h"

#include "pdfannotation.h"
#include "pdfcms.h"
#include "pdfcolorconvertor.h"
#include "pdfcolorspaces.h"
#include "pdfconstants.h"
#include "pdfdocument.h"
#include "pdffont.h"
#include "pdfmcpcatalog.h"
#include "pdfmcpjson.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoptionalcontent.h"
#include "pdfpainter.h"
#include "pdfpainterutils.h"
#include "pdfrenderer.h"
#include "pdftextlayout.h"

#include <QBuffer>
#include <QPainter>

#include <cmath>

namespace pdf
{

/// The caches and the managers of one rendering in a worker (as the printing)
class PDFAppMcpRenderContext
{
public:
    explicit PDFAppMcpRenderContext(const PDFDocument* document) :
        m_document(document),
        m_fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT),
        m_optionalContentActivity(document, OCUsage::View, nullptr),
        m_cmsManager(nullptr),
        m_annotationManager(&m_fontCache, &m_iccProfileCache, &m_cmsManager, &m_optionalContentActivity, PDFMeshQualitySettings(),
                            getFeatures(), PDFAnnotationManager::Target::View, nullptr)
    {
        // The document is not modified: the caches and the annotations only read it
        PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), &m_optionalContentActivity);
        m_fontCache.setDocument(modifiedDocument);
        m_iccProfileCache.setDocument(modifiedDocument);
        m_cmsManager.setDocument(document);
        m_cmsManager.setSettings(PDFCMSSettings());
        m_annotationManager.setDocument(modifiedDocument);
    }

    static PDFRenderer::Features getFeatures()
    {
        return PDFRenderer::Features(PDFRenderer::Antialiasing | PDFRenderer::TextAntialiasing | PDFRenderer::SmoothImages |
                                     PDFRenderer::ClipToCropBox | PDFRenderer::DisplayAnnotations);
    }

    void draw(QPainter* painter, PDFInteger pageIndex, const QTransform& pageToImage, bool showAnnotations, const PDFOperationControl* control)
    {
        const PDFPage* page = m_document->getCatalog()->getPage(size_t(pageIndex));
        const PDFCMSPointer cms = m_cmsManager.getCurrentCMS();
        PDFRenderer renderer(m_document, &m_fontCache, &m_iccProfileCache, cms.data(), &m_optionalContentActivity, getFeatures(), PDFMeshQualitySettings());
        renderer.setOperationControl(control);

        PDFFontCacheShrinkGuard shrinkGuard(&m_fontCache);
        PDFPrecompiledPage compiledPage;
        renderer.compile(&compiledPage, size_t(pageIndex));
        if (PDFOperationControl::isOperationCancelled(control))
        {
            return;
        }

        compiledPage.draw(painter, page->getCropBox(), pageToImage, getFeatures(), 1.0);
        if (showAnnotations)
        {
            PDFColorConvertor convertor = cms->getColorConvertor();
            PDFRenderer::applyFeaturesToAnnotationColorConvertor(getFeatures(), convertor);
            PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
            QList<PDFRenderError> errors;
            m_annotationManager.drawPage(painter, pageIndex, &compiledPage, textLayoutGetter, pageToImage, convertor, errors);
        }
    }

private:
    const PDFDocument* m_document;
    PDFFontCache m_fontCache;
    PDFICCProfileCache m_iccProfileCache;
    PDFOptionalContentActivity m_optionalContentActivity;
    PDFCMSManager m_cmsManager;
    PDFAnnotationManager m_annotationManager;
};

PDFAppMcpRenderGeometry PDFAppMcpRenderer::getGeometry(const PDFPage* page, int dpi, const std::optional<QRectF>& area, QString* errorMessage)
{
    PDFAppMcpRenderGeometry geometry;
    if (!page)
    {
        *errorMessage = QStringLiteral("The page does not exist.");
        return geometry;
    }

    const QRectF cropBox = page->getCropBox();
    const QRectF rotatedCropBox = PDFPage::getRotatedBox(cropBox, page->getPageRotation());
    const double userUnit = std::isfinite(page->getUserUnit()) && page->getUserUnit() > 0.0 ? page->getUserUnit() : 1.0;
    const double scale = qBound(1, dpi, 600) / 72.0 * userUnit;
    const double fullWidth = rotatedCropBox.width() * scale;
    const double fullHeight = rotatedCropBox.height() * scale;
    if (!std::isfinite(fullWidth) || !std::isfinite(fullHeight) || fullWidth <= 0.0 || fullHeight <= 0.0)
    {
        *errorMessage = QStringLiteral("The page has an empty or invalid crop box.");
        return geometry;
    }

    // The matrix of the whole page; an area is its part
    QTransform pageToImage = PDFRenderer::createMediaBoxToDevicePointMatrix(rotatedCropBox, QRectF(0.0, 0.0, fullWidth, fullHeight), page->getPageRotation());
    QRectF imageRect(0.0, 0.0, fullWidth, fullHeight);
    QRectF pageArea = cropBox.normalized();
    if (area)
    {
        pageArea = area->normalized().intersected(cropBox.normalized());
        if (pageArea.isEmpty())
        {
            *errorMessage = QStringLiteral("area: the area lies outside of the crop box of the page (see document.get_pages).");
            return geometry;
        }
        imageRect = pageToImage.mapRect(pageArea);
        pageToImage = pageToImage * QTransform::fromTranslate(-imageRect.left(), -imageRect.top());
    }

    const double pixels = std::ceil(imageRect.width()) * std::ceil(imageRect.height());
    if (!std::isfinite(pixels) || pixels > double(MAXIMUM_PIXELS))
    {
        *errorMessage = QStringLiteral("The image would have %1 megapixels; at most %2 are allowed. Lower dpi or render an area of the page.")
                            .arg(std::isfinite(pixels) ? QString::number(pixels / 1000000.0, 'f', 1) : QStringLiteral("too many"))
                            .arg(MAXIMUM_PIXELS / 1000000);
        return geometry;
    }

    geometry.imageSize = QSize(qMax(1, int(std::lround(imageRect.width()))), qMax(1, int(std::lround(imageRect.height()))));
    geometry.pageToImage = pageToImage;
    geometry.area = pageArea;
    return geometry;
}

QImage PDFAppMcpRenderer::render(const PDFDocument* document, PDFInteger pageIndex, const PDFAppMcpRenderGeometry& geometry, bool showAnnotations, const PDFOperationControl* control)
{
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    if (!catalog || pageIndex < 0 || pageIndex >= PDFInteger(catalog->getPageCount()) || !geometry.isValid())
    {
        return QImage();
    }

    QImage image(geometry.imageSize, QImage::Format_RGB32);
    if (image.isNull())
    {
        return QImage();
    }
    image.fill(Qt::white);

    PDFAppMcpRenderContext context(document);
    {
        QPainter painter(&image);
        context.draw(&painter, pageIndex, geometry.pageToImage, showAnnotations, control);
    }

    if (PDFOperationControl::isOperationCancelled(control))
    {
        return QImage();
    }
    return image;
}

std::vector<double> PDFAppMcpRenderer::getMatrixValues(const QTransform& transform)
{
    return { transform.m11(), transform.m12(), transform.m21(), transform.m22(), transform.dx(), transform.dy() };
}

QByteArray PDFAppMcpRenderer::encode(const QImage& image, bool isJpeg, int jpegQuality)
{
    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly))
    {
        return QByteArray();
    }

    const bool isWritten = isJpeg ? image.save(&buffer, "JPEG", qBound(30, jpegQuality, 95)) : image.save(&buffer, "PNG");
    return isWritten ? data : QByteArray();
}

PDFAppMcpRenderJob::PDFAppMcpRenderJob(std::shared_ptr<PDFMcpCall> call, PDFAppDocumentSnapshot snapshot, PDFAppMcpRenderRequest request) :
    PDFAppMcpJob(std::move(call), std::move(snapshot)),
    m_request(std::move(request))
{

}

void PDFAppMcpRenderJob::execute()
{
    if (!getCall() || !getSnapshot().isValid())
    {
        return;
    }

    switch (m_request.kind)
    {
        case PDFAppMcpRenderKind::Page:
            executePage();
            break;
        case PDFAppMcpRenderKind::Overview:
            executeOverview();
            break;
    }
}

void PDFAppMcpRenderJob::executePage()
{
    using Support = PDFAppMcpReadSupport;

    const std::shared_ptr<PDFMcpCall>& call = getCall();
    const PDFDocument* document = getSnapshot().document.data();
    const PDFCatalog* catalog = document->getCatalog();
    const PDFPage* page = m_request.pageIndex >= 0 && m_request.pageIndex < PDFInteger(catalog->getPageCount()) ? catalog->getPage(size_t(m_request.pageIndex)) : nullptr;

    QString error;
    const PDFAppMcpRenderGeometry geometry = PDFAppMcpRenderer::getGeometry(page, m_request.dpi, m_request.area, &error);
    if (!geometry.isValid())
    {
        m_isFailed = true;
        call->finish(PDFMcpCallResult::createToolError(error));
        return;
    }

    call->reportProgress(0, 2, QStringLiteral("Rendering page %1").arg(m_request.pageIndex + 1));
    const QImage image = PDFAppMcpRenderer::render(document, m_request.pageIndex, geometry, m_request.showAnnotations, call.get());
    if (call->isCanceled())
    {
        return;
    }
    if (image.isNull())
    {
        m_isFailed = true;
        call->finish(PDFMcpCallResult::createToolError(QStringLiteral("The page could not be rendered (not enough memory). Lower dpi or render an area.")));
        return;
    }

    call->reportProgress(1, 2, QStringLiteral("Encoding the image"));
    const QByteArray data = PDFAppMcpRenderer::encode(image, m_request.isJpeg, m_request.jpegQuality);
    if (data.isEmpty() || data.size() > PDFAppMcpRenderer::MAXIMUM_IMAGE_BYTES)
    {
        m_isFailed = true;
        call->finish(PDFMcpCallResult::createToolError(QStringLiteral("The image has %1 MiB, more than the limit of 12 MiB. Lower dpi, render an area, or use the format jpeg.")
                                                       .arg(QString::number(double(data.size()) / 1048576.0, 'f', 1))));
        return;
    }
    m_pages.insert(int(m_request.pageIndex + 1));

    const QString mimeType = m_request.isJpeg ? QStringLiteral("image/jpeg") : QStringLiteral("image/png");
    if (m_request.isResource)
    {
        call->finish(PDFAppMcpDocumentResources::createBlobResult(m_request.resourceUri, mimeType, data));
        return;
    }

    PDFMcpJsonWriter writer;
    writer.beginObject();
    m_request.header.write(writer);
    writer.writeMember(QLatin1StringView("page"), qint64(m_request.pageIndex + 1));
    writer.writeMember(QLatin1StringView("label"), Support::sanitizeName(PDFAppMcpArguments::getPageLabel(document, m_request.pageIndex), 256));
    writer.writeMember(QLatin1StringView("widthPx"), image.width());
    writer.writeMember(QLatin1StringView("heightPx"), image.height());
    writer.writeMember(QLatin1StringView("dpi"), m_request.dpi);
    Support::writeRect(writer, QLatin1StringView("area"), geometry.area);
    writer.key(QLatin1StringView("pageToImage"));
    writer.beginArray();
    for (double value : PDFAppMcpRenderer::getMatrixValues(geometry.pageToImage))
    {
        writer.writeNumber(value == 0.0 ? 0.0 : value);
    }
    writer.endArray();
    writer.key(QLatin1StringView("imageToPage"));
    writer.beginArray();
    for (double value : PDFAppMcpRenderer::getMatrixValues(geometry.pageToImage.inverted()))
    {
        writer.writeNumber(value == 0.0 ? 0.0 : value);
    }
    writer.endArray();
    writer.writeMember(QLatin1StringView("format"), m_request.isJpeg ? QLatin1StringView("jpeg") : QLatin1StringView("png"));
    writer.writeMember(QLatin1StringView("bytes"), qint64(data.size()));
    writer.endObject();

    const QByteArray structured = Support::createStructuredContent(QStringLiteral("page"), writer.take());
    PDFMcpContentBuilder builder;
    builder.reserve(data.size() * 4 / 3 + structured.size() * 2 + 1024);
    builder.addText(QString::fromUtf8(structured));
    builder.addImage(data, mimeType);
    const QString uri = QStringLiteral("pdf4qt://documents/%1/pages/%2/image/%3").arg(m_request.header.documentId).arg(m_request.pageIndex + 1).arg(m_request.dpi);
    builder.addResourceLink(uri, QStringLiteral("page-%1-image").arg(m_request.pageIndex + 1), QStringLiteral("Page %1 at %2 dpi").arg(m_request.pageIndex + 1).arg(m_request.dpi), QStringLiteral("image/png"));
    builder.setStructuredContent(structured);
    call->finish(builder.createCallResult(false));
}

void PDFAppMcpRenderJob::executeOverview()
{
    using Support = PDFAppMcpReadSupport;

    const std::shared_ptr<PDFMcpCall>& call = getCall();
    const PDFDocument* document = getSnapshot().document.data();
    const PDFCatalog* catalog = document->getCatalog();
    const int count = int(m_request.pageIndices.size());
    if (count <= 0)
    {
        m_isFailed = true;
        call->finish(PDFMcpCallResult::createToolError(QStringLiteral("pages: pass at least one page.")));
        return;
    }

    // A grid of square cells with the page numbers below the pages
    constexpr int padding = 8;
    constexpr int labelHeight = 18;
    const int cellSize = qBound(64, m_request.maximumCellSize, 256);
    const int columns = qMax(1, int(std::ceil(std::sqrt(double(count)))));
    const int rows = (count + columns - 1) / columns;
    const QSize sheetSize(padding + columns * (cellSize + padding), padding + rows * (cellSize + labelHeight + padding));

    QImage sheet(sheetSize, QImage::Format_RGB32);
    if (sheet.isNull())
    {
        m_isFailed = true;
        call->finish(PDFMcpCallResult::createToolError(QStringLiteral("The overview could not be created (not enough memory).")));
        return;
    }
    sheet.fill(QColor(0xE8, 0xE8, 0xE8));

    PDFMcpJsonWriter writer;
    writer.beginObject();
    m_request.header.write(writer);
    writer.writeMember(QLatin1StringView("widthPx"), sheet.width());
    writer.writeMember(QLatin1StringView("heightPx"), sheet.height());
    writer.key(QLatin1StringView("cells"));
    writer.beginArray();

    PDFAppMcpRenderContext context(document);
    QPainter painter(&sheet);
    for (int i = 0; i < count; ++i)
    {
        if (call->isCanceled())
        {
            return;
        }
        call->reportProgress(i, count, QStringLiteral("Rendering page %1 of %2").arg(i + 1).arg(count));

        const PDFInteger pageIndex = m_request.pageIndices[size_t(i)];
        const PDFPage* page = pageIndex >= 0 && pageIndex < PDFInteger(catalog->getPageCount()) ? catalog->getPage(size_t(pageIndex)) : nullptr;
        const QSizeF pageSize = page ? PDFPage::getRotatedBox(page->getCropBox(), page->getPageRotation()).size() : QSizeF();
        if (!page || !pageSize.isValid() || pageSize.isEmpty() || !std::isfinite(pageSize.width()) || !std::isfinite(pageSize.height()))
        {
            continue;
        }

        const QSizeF fitted = pageSize.scaled(QSizeF(cellSize, cellSize), Qt::KeepAspectRatio);
        const int column = i % columns;
        const int row = i / columns;
        const QRectF cellRect(padding + column * (cellSize + padding), padding + row * (cellSize + labelHeight + padding), cellSize, cellSize);
        QRectF pageRect(QPointF(0, 0), fitted);
        pageRect.moveCenter(cellRect.center());
        pageRect = QRectF(std::round(pageRect.left()), std::round(pageRect.top()), std::round(pageRect.width()), std::round(pageRect.height()));

        painter.fillRect(pageRect, Qt::white);
        const QRectF rotatedCropBox = PDFPage::getRotatedBox(page->getCropBox(), page->getPageRotation());
        const QTransform pageToImage = PDFRenderer::createMediaBoxToDevicePointMatrix(rotatedCropBox, pageRect, page->getPageRotation());
        {
            PDFPainterStateGuard guard(&painter);
            painter.setClipRect(pageRect);
            context.draw(&painter, pageIndex, pageToImage, true, call.get());
        }

        const QString label = Support::sanitizeName(PDFAppMcpArguments::getPageLabel(document, pageIndex), 32);
        const QString caption = label == QString::number(pageIndex + 1) ? QString::number(pageIndex + 1) : QStringLiteral("%1 (%2)").arg(pageIndex + 1).arg(label);
        {
            PDFPainterStateGuard guard(&painter);
            painter.setPen(Qt::black);
            painter.drawText(QRectF(cellRect.left(), cellRect.bottom() + 2, cellRect.width(), labelHeight - 2), Qt::AlignHCenter | Qt::AlignTop, caption);
        }

        writer.beginObject();
        writer.writeMember(QLatin1StringView("page"), qint64(pageIndex + 1));
        writer.writeMember(QLatin1StringView("label"), label);
        writer.writeMember(QLatin1StringView("x"), qint64(pageRect.left()));
        writer.writeMember(QLatin1StringView("y"), qint64(pageRect.top()));
        writer.writeMember(QLatin1StringView("width"), qint64(pageRect.width()));
        writer.writeMember(QLatin1StringView("height"), qint64(pageRect.height()));
        writer.endObject();
        m_pages.insert(int(pageIndex + 1));
    }
    painter.end();

    writer.endArray();
    writer.endObject();

    if (call->isCanceled())
    {
        return;
    }

    const QByteArray data = PDFAppMcpRenderer::encode(sheet, false, 0);
    if (data.isEmpty() || data.size() > PDFAppMcpRenderer::MAXIMUM_IMAGE_BYTES)
    {
        m_isFailed = true;
        call->finish(PDFMcpCallResult::createToolError(QStringLiteral("The overview could not be encoded. Pass fewer pages or a smaller maxSize.")));
        return;
    }

    const QByteArray structured = Support::createStructuredContent(QStringLiteral("overview"), writer.take());
    PDFMcpContentBuilder builder;
    builder.reserve(data.size() * 4 / 3 + structured.size() * 2 + 1024);
    builder.addText(QString::fromUtf8(structured));
    builder.addImage(data, QStringLiteral("image/png"));
    builder.setStructuredContent(structured);
    call->finish(builder.createCallResult(false));
}

void PDFAppMcpRenderJob::finish(PDFAppMcpHost* host)
{
    const std::shared_ptr<PDFMcpCall>& call = getCall();
    if (!host || !call)
    {
        return;
    }

    if (call->isCanceled() && !call->isFinished())
    {
        call->finish(PDFAppMcpJobRunnable::createFailure(*call, QStringLiteral("The request was canceled.")));
    }

    const std::optional<PDFMcpCallResult> result = call->getResult();
    const bool isError = m_isFailed || !result || result->kind == PDFMcpCallResult::Kind::Error || result->isError;
    PDFAppDocument* document = host->getResolver().resolve(m_request.header.documentId, false).document;
    host->recordActivity(call, m_request.actionId, document, QJsonObject(), PDFAppMcpReadSupport::getActivityStatus(*call, isError), m_pages);
}

}   // namespace pdf
