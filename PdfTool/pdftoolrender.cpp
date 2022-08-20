//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftoolrender.h"
#include "pdffont.h"
#include "pdfconstants.h"

#include <QElapsedTimer>

namespace pdftool
{

static PDFToolRender s_toolRenderApplication;
static PDFToolBenchmark s_toolBenchmarkApplication;

QString PDFToolRender::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "render";

        case Name:
            return PDFToolTranslationContext::tr("Render document");

        case Description:
            return PDFToolTranslationContext::tr("Render selected pages of document into image files.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

PDFToolAbstractApplication::Options PDFToolRender::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | ImageWriterSettings | ImageExportSettingsFiles | ImageExportSettingsResolution | ColorManagementSystem | RenderFlags;
}

void PDFToolRender::finish(const PDFToolOptions& options)
{
    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("render", PDFToolTranslationContext::tr("Render document %1").arg(options.document));
    formatter.endl();

    writeStatistics(formatter);
    if (options.renderShowPageStatistics)
    {
        writePageStatistics(formatter);
    }
    writeErrors(formatter);

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);
}

void PDFToolRender::onPageRendered(const PDFToolOptions& options, pdf::PDFRenderedPageImage& renderedPageImage)
{
    writePageInfoStatistics(renderedPageImage);
    QString fileName = options.imageExportSettings.getOutputFileName(renderedPageImage.pageIndex, options.imageWriterSettings.getCurrentFormat());

    QElapsedTimer imageWriterTimer;
    imageWriterTimer.start();

    QImageWriter imageWriter(fileName, options.imageWriterSettings.getCurrentFormat());
    imageWriter.setSubType(options.imageWriterSettings.getCurrentSubtype());
    imageWriter.setCompression(options.imageWriterSettings.getCompression());
    imageWriter.setQuality(options.imageWriterSettings.getQuality());
    imageWriter.setOptimizedWrite(options.imageWriterSettings.hasOptimizedWrite());
    imageWriter.setProgressiveScanWrite(options.imageWriterSettings.hasProgressiveScanWrite());

    if (!imageWriter.write(renderedPageImage.pageImage))
    {
        m_pageInfo[renderedPageImage.pageIndex].errors.emplace_back(pdf::PDFRenderError(pdf::RenderErrorType::Error, PDFToolTranslationContext::tr("Cannot write page image to file '%1', because: %2.").arg(fileName).arg(imageWriter.errorString())));
    }

    m_pageInfo[renderedPageImage.pageIndex].pageWriteTime = imageWriterTimer.elapsed();
}

QString PDFToolBenchmark::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "benchmark";

        case Name:
            return PDFToolTranslationContext::tr("Benchmark rendering");

        case Description:
            return PDFToolTranslationContext::tr("Benchmark page rendering (measure time, detect errors).");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

PDFToolAbstractApplication::Options PDFToolBenchmark::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | ImageExportSettingsResolution | ColorManagementSystem | RenderFlags;
}

void PDFToolBenchmark::finish(const PDFToolOptions& options)
{
    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("benchmark", PDFToolTranslationContext::tr("Benchmark rendering of document %1").arg(options.document));
    formatter.endl();

    writeStatistics(formatter);
    if (options.renderShowPageStatistics)
    {
        writePageStatistics(formatter);
    }
    writeErrors(formatter);

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);
}

void PDFToolBenchmark::onPageRendered(const PDFToolOptions& options, pdf::PDFRenderedPageImage& renderedPageImage)
{
    Q_UNUSED(options);
    writePageInfoStatistics(renderedPageImage);
}

int PDFToolRenderBase::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    QString parseError;
    std::vector<pdf::PDFInteger> pageIndices = options.getPageRange(document.getCatalog()->getPageCount(), parseError, true);

    if (!parseError.isEmpty())
    {
        PDFConsole::writeError(parseError, options.outputCodec);
        return ErrorInvalidArguments;
    }

    QString errorMessage;
    Options optionFlags = getOptionsFlags();
    if (!options.imageExportSettings.validate(&errorMessage, false, optionFlags.testFlag(ImageExportSettingsFiles), optionFlags.testFlag(ImageExportSettingsResolution)))
    {
        PDFConsole::writeError(errorMessage, options.outputCodec);
        return ErrorInvalidArguments;
    }

    // We are ready to render the document
    pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::Export, nullptr);
    pdf::PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    cmsManager.setSettings(options.cmsSettings);
    pdf::PDFMeshQualitySettings meshQualitySettings;
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFModifiedDocument md(&document, &optionalContentActivity);
    fontCache.setDocument(md);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    QSurfaceFormat surfaceFormat;
    if (options.renderUseHardwareRendering)
    {
        surfaceFormat = QSurfaceFormat::defaultFormat();
        surfaceFormat.setProfile(QSurfaceFormat::CoreProfile);
        surfaceFormat.setSamples(options.renderMSAAsamples);
        surfaceFormat.setColorSpace(QSurfaceFormat::sRGBColorSpace);
        surfaceFormat.setSwapBehavior(QSurfaceFormat::DefaultSwapBehavior);
    }

    m_pageInfo.resize(document.getCatalog()->getPageCount());
    pdf::PDFRasterizerPool rasterizerPool(&document, &fontCache, &cmsManager,
                                          &optionalContentActivity, options.renderFeatures, meshQualitySettings,
                                          pdf::PDFRasterizerPool::getCorrectedRasterizerCount(options.renderRasterizerCount),
                                          options.renderUseHardwareRendering, surfaceFormat, nullptr);

    auto onRenderError = [this](pdf::PDFInteger pageIndex, pdf::PDFRenderError error)
    {
        if (pageIndex != pdf::PDFCatalog::INVALID_PAGE_INDEX)
        {
            m_pageInfo[pageIndex].errors.emplace_back(qMove(error));
        }
    };
    QObject holder;
    QObject::connect(&rasterizerPool, &pdf::PDFRasterizerPool::renderError, &holder, onRenderError, Qt::DirectConnection);

    auto imageSizeGetter = [&options](const pdf::PDFPage* page) -> QSize
    {
        Q_ASSERT(page);

        switch (options.imageExportSettings.getResolutionMode())
        {
            case pdf::PDFPageImageExportSettings::ResolutionMode::DPI:
            {
                QSizeF size = page->getRotatedMediaBox().size() * pdf::PDF_POINT_TO_INCH * options.imageExportSettings.getDpiResolution();
                return size.toSize();
            }

            case pdf::PDFPageImageExportSettings::ResolutionMode::Pixels:
            {
                int pixelResolution = options.imageExportSettings.getPixelResolution();
                QSizeF size = page->getRotatedMediaBox().size().scaled(pixelResolution, pixelResolution, Qt::KeepAspectRatio);
                return size.toSize();
            }

            default:
            {
                Q_ASSERT(false);
                break;
            }
        }

        return QSize();
    };

    QElapsedTimer timer;
    timer.start();

    rasterizerPool.render(pageIndices, imageSizeGetter, std::bind(&PDFToolRenderBase::onPageRendered, this, options, std::placeholders::_1), nullptr);

    m_wallTime = timer.elapsed();

    fontCache.setCacheShrinkEnabled(nullptr, true);

    finish(options);
    return ExitSuccess;
}

void PDFToolRenderBase::writePageInfoStatistics(const pdf::PDFRenderedPageImage& renderedPageImage)
{
    PageInfo& info = m_pageInfo[renderedPageImage.pageIndex];
    info.isRendered = true;
    info.pageCompileTime = renderedPageImage.pageCompileTime;
    info.pageWaitTime = renderedPageImage.pageWaitTime;
    info.pageRenderTime = renderedPageImage.pageRenderTime;
    info.pageTotalTime = renderedPageImage.pageTotalTime;
    info.pageIndex = renderedPageImage.pageIndex;
}

void PDFToolRenderBase::writeStatistics(PDFOutputFormatter& formatter)
{
    // Jakub Melka: Write overall statistics
    qint64 pagesRendered = 0;
    qint64 pageCompileTime = 0;
    qint64 pageWaitTime = 0;
    qint64 pageRenderTime = 0;
    qint64 pageTotalTime = 0;
    qint64 pageWriteTime = 0;

    for (const PageInfo& info : m_pageInfo)
    {
        if (!info.isRendered)
        {
            continue;
        }

        ++pagesRendered;
        pageCompileTime += info.pageCompileTime;
        pageWaitTime += info.pageWaitTime;
        pageRenderTime += info.pageRenderTime;
        pageTotalTime += info.pageTotalTime + info.pageWriteTime;
        pageWriteTime += info.pageWriteTime;
    }

    if (pagesRendered > 0 && pageTotalTime > 0 && m_wallTime > 0)
    {
        QLocale locale;

        double renderingSpeedPerCore = double(pagesRendered) / (double(pageTotalTime) / 1000.0);
        double renderingSpeedWallTime = double(pagesRendered) / (double(m_wallTime) / 1000.0);

        double compileRatio = 100.0 * double(pageCompileTime) / double(pageTotalTime);
        double waitRatio = 100.0 * double(pageWaitTime) / double(pageTotalTime);
        double renderRatio = 100.0 * double(pageRenderTime) / double(pageTotalTime);
        double writeRatio = 100.0 * double(pageWriteTime) / double(pageTotalTime);

        formatter.beginTable("statistics", PDFToolTranslationContext::tr("Statistics"));

        formatter.beginTableHeaderRow("header");
        formatter.writeTableHeaderColumn("description", PDFToolTranslationContext::tr("Description"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("value", PDFToolTranslationContext::tr("Value"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("unit", PDFToolTranslationContext::tr("Unit"), Qt::AlignLeft);
        formatter.endTableHeaderRow();

        auto writeValue = [&formatter](QString name, QString description, QString value, QString unit)
        {
            formatter.beginTableRow(name);
            formatter.writeTableColumn("description", description);
            formatter.writeTableColumn("value", value, Qt::AlignRight);
            formatter.writeTableColumn("unit", unit);
            formatter.endTableRow();
        };

        writeValue("pages-rendered", PDFToolTranslationContext::tr("Pages rendered"), locale.toString(pagesRendered), PDFToolTranslationContext::tr("-"));
        writeValue("compile-time", PDFToolTranslationContext::tr("Total compile time"), locale.toString(pageCompileTime), PDFToolTranslationContext::tr("msec"));
        writeValue("render-time", PDFToolTranslationContext::tr("Total render time"), locale.toString(pageRenderTime), PDFToolTranslationContext::tr("msec"));
        writeValue("wait-time", PDFToolTranslationContext::tr("Total wait time"), locale.toString(pageWaitTime), PDFToolTranslationContext::tr("msec"));
        writeValue("write-time", PDFToolTranslationContext::tr("Total write time"), locale.toString(pageWriteTime), PDFToolTranslationContext::tr("msec"));
        writeValue("total-time", PDFToolTranslationContext::tr("Total time"), locale.toString(pageTotalTime), PDFToolTranslationContext::tr("msec"));
        writeValue("wall-time", PDFToolTranslationContext::tr("Wall time"), locale.toString(m_wallTime), PDFToolTranslationContext::tr("msec"));
        writeValue("pages-per-second-core", PDFToolTranslationContext::tr("Rendering speed (per core)"), locale.toString(renderingSpeedPerCore, 'f', 3), PDFToolTranslationContext::tr("pages / sec (one core)"));
        writeValue("pages-per-second-wall", PDFToolTranslationContext::tr("Rendering speed (wall time)"), locale.toString(renderingSpeedWallTime, 'f', 3), PDFToolTranslationContext::tr("pages / sec"));
        writeValue("compile-time-ratio", PDFToolTranslationContext::tr("Compile time ratio"), locale.toString(compileRatio, 'f', 2), PDFToolTranslationContext::tr("%"));
        writeValue("render-time-ratio", PDFToolTranslationContext::tr("Render time ratio"), locale.toString(renderRatio, 'f', 2), PDFToolTranslationContext::tr("%"));
        writeValue("wait-time-ratio", PDFToolTranslationContext::tr("Wait time ratio"), locale.toString(waitRatio, 'f', 2), PDFToolTranslationContext::tr("%"));
        writeValue("write-time-ratio", PDFToolTranslationContext::tr("Write time ratio"), locale.toString(writeRatio, 'f', 2), PDFToolTranslationContext::tr("%"));

        formatter.endTable();
        formatter.endl();
    }
}

void PDFToolRenderBase::writePageStatistics(PDFOutputFormatter& formatter)
{
    formatter.beginTable("page-statistics", PDFToolTranslationContext::tr("Page Statistics"));

    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("page-no", PDFToolTranslationContext::tr("Page No."), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("compile-time", PDFToolTranslationContext::tr("Compile Time [msec]"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("render-time", PDFToolTranslationContext::tr("Render Time [msec]"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("wait-time", PDFToolTranslationContext::tr("Wait Time [msec]"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("write-time", PDFToolTranslationContext::tr("Write Time [msec]"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("total-time", PDFToolTranslationContext::tr("Total Time [msec]"), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    QLocale locale;

    for (const PageInfo& info : m_pageInfo)
    {
        if (!info.isRendered)
        {
            continue;
        }

        formatter.beginTableRow("page", info.pageIndex + 1);
        formatter.writeTableColumn("page-no", locale.toString(info.pageIndex + 1), Qt::AlignRight);
        formatter.writeTableColumn("compile-time", locale.toString(info.pageCompileTime), Qt::AlignRight);
        formatter.writeTableColumn("render-time", locale.toString(info.pageRenderTime), Qt::AlignRight);
        formatter.writeTableColumn("wait-time", locale.toString(info.pageWaitTime), Qt::AlignRight);
        formatter.writeTableColumn("write-time", locale.toString(info.pageWriteTime), Qt::AlignRight);
        formatter.writeTableColumn("total-time", locale.toString(info.pageTotalTime), Qt::AlignRight);
        formatter.endTableRow();
    }

    formatter.endTable();
    formatter.endl();
}

void PDFToolRenderBase::writeErrors(PDFOutputFormatter& formatter)
{
    formatter.beginTable("rendering-errors", PDFToolTranslationContext::tr("Rendering Errors"));

    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("page-no", PDFToolTranslationContext::tr("Page No."), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("type", PDFToolTranslationContext::tr("Type"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("message", PDFToolTranslationContext::tr("Message"), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    QLocale locale;

    for (const PageInfo& info : m_pageInfo)
    {
        if (!info.isRendered)
        {
            continue;
        }

        for (const pdf::PDFRenderError& error : info.errors)
        {
            QString type;
            switch (error.type)
            {
                case pdf::RenderErrorType::Error:
                    type = PDFToolTranslationContext::tr("Error");
                    break;

                case pdf::RenderErrorType::Warning:
                    type = PDFToolTranslationContext::tr("Warning");
                    break;

                case pdf::RenderErrorType::NotImplemented:
                    type = PDFToolTranslationContext::tr("Not implemented");
                    break;

                case pdf::RenderErrorType::NotSupported:
                    type = PDFToolTranslationContext::tr("Not supported");
                    break;

                case pdf::RenderErrorType::Information:
                    type = PDFToolTranslationContext::tr("Information");
                    break;

                default:
                    Q_ASSERT(false);
                    break;
            }

            formatter.beginTableRow("page", info.pageIndex + 1);
            formatter.writeTableColumn("page-no", locale.toString(info.pageIndex + 1), Qt::AlignRight);
            formatter.writeTableColumn("type", type, Qt::AlignLeft);
            formatter.writeTableColumn("message", error.message, Qt::AlignLeft);
            formatter.endTableRow();
        }
    }

    formatter.endTable();
    formatter.endl();
}

}   // namespace pdftool
