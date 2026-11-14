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

#ifndef PDFDOCUMENTTOOL_PDFTOIMAGES_H
#define PDFDOCUMENTTOOL_PDFTOIMAGES_H

#include "pdfdocumenttool.h"
#include "pdfoptionalcontent.h"
#include "pdfrenderer.h"

#include <QImage>

#include <functional>
#include <map>
#include <optional>

namespace pdf
{

class PDFPage;

/// The image file of an export (PDF to Images, Extract Images; plan of step 4 of phase 2, 4.4, 4.5,
/// B67: the one place of the settings of the image writer of the new tools and of PdfTool render /
/// fetch-images). A value; encode is reentrant (each call has its own QImageWriter).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolImageFormat
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolImageFormat)

public:
    /// Names of the parameters of the image of both exports
    static constexpr const char* PARAMETER_FORMAT = "format";
    static constexpr const char* PARAMETER_SUBTYPE = "subtype";
    static constexpr const char* PARAMETER_COMPRESSION = "compression";
    static constexpr const char* PARAMETER_QUALITY = "quality";
    static constexpr const char* PARAMETER_OPTIMIZED_WRITE = "optimizedWrite";
    static constexpr const char* PARAMETER_PROGRESSIVE_SCAN_WRITE = "progressiveScanWrite";

    /// Most pixels of a side of an image of a format (R4-19): WebP 16 383, JPEG 65 535, the
    /// others 32 767 (the raster engine of Qt)
    static constexpr int MAXIMUM_SIDE = 32767;
    static constexpr int MAXIMUM_WEBP_SIDE = 16383;
    static constexpr int MAXIMUM_JPEG_SIDE = 65535;

    /// Most bytes of the pixels of one image (32 bits per pixel, plan 4.4: 2 GiB)
    static constexpr qint64 MAXIMUM_IMAGE_BYTES = qint64(2) * 1024 * 1024 * 1024 - 1;

    QByteArray format = "png";      ///< Format of QImageWriter (lower case: "png", "jpeg", "tiff", "webp", "bmp")
    QByteArray subtype;             ///< Subtype of the format, empty = the default
    int compression = -1;           ///< Compression of the format, -1 = the default of the format
    int quality = -1;               ///< Quality of a lossy format 0 to 100, -1 = the default of the format
    bool optimizedWrite = false;    ///< QImageWriter::setOptimizedWrite (JPEG)
    bool progressiveScanWrite = false;  ///< QImageWriter::setProgressiveScanWrite (JPEG)

    /// The image of the normalized parameters (the names above)
    static PDFDocumentToolImageFormat fromParameters(const QVariantMap& parameters);

    /// The image of the settings of the old image writer (PdfTool, the dialog Render to Images, B67)
    static PDFDocumentToolImageFormat fromWriterSettings(const PDFImageWriterSettings& settings);

    /// The parameters of the image (the names above), which fromParameters reads back
    QVariantMap toParameters() const;

    /// The formats, which QImageWriter writes (lower case, sorted)
    static QList<QByteArray> getSupportedFormats();

    /// The format can be written; false and the translated reason
    bool isValid(QString* errorMessage) const;

    /// Extension of the files with its dot (".png", ".jpg" for jpeg, ".tif" for tiff)
    QString getExtension() const;

    /// Name of the format for the user ("PNG", "JPEG")
    QString getDisplayName() const;

    /// Most pixels of a side of an image of this format
    int getMaximumSide() const;

    /// The format keeps transparency (a format without it gets a white background)
    bool hasAlpha() const;

    /// The bytes of the image file of \p image; empty and the translated reason, when the image
    /// cannot be written. Reentrant.
    QByteArray encode(const QImage& image, QString* errorMessage) const;
};

/// What renders the pages of PDF to Images besides its parameters: the color management, the
/// features of the renderer and the layers (owner answer Q4.6, R4-10 b). A value. The default
/// is the rendering without a tab (MCP, a tool without a view): the default color management
/// with the output intents of the document, PDFRenderer::getDefaultFeatures, the layers by
/// OCUsage::Export (ISO 32000-2, 8.11.4.4). A run of the application (create, the settings of a
/// view) never uses the color modes of a view (ColorAdjust_*) and IgnoreOptionalContent
/// (R4-10 b, getExportFeatures); the options of PdfTool render (--render-invert-colors,
/// --render-ignore-opt-content, ...) are explicit requests of its command line and are used as
/// given (plan 4.8: the old command keeps its function). The debugging flags and DisplayTimes are
/// never used (getOutputFeatures).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolPageRenderSettings
{
public:
    std::optional<PDFCMSSettings> cmsSettings;      ///< Color management (empty = the default settings)
    PDFRenderer::Features features = PDFRenderer::getDefaultFeatures();     ///< Features of the renderer, used as given (getOutputFeatures)
    RendererEngine engine = RendererEngine::Blend2D_SingleThread;
    int rasterizerCount = 0;                        ///< Rasterizers of the pool (0 = PDFRasterizerPool::getDefaultRasterizerCount)
    std::optional<std::map<PDFObjectReference, OCState>> layerStates;  ///< The states of the layers of a tab (empty = OCUsage::Export)

    /// Called for every rendered page in the threads of the pool, more pages at once (PdfTool
    /// render counts the times of the pages): thread-safe, it must not keep the image. Optional.
    std::function<void(const PDFRenderedPageImage& image)> pageObserver;

    /// The features of any output file: \p features without the debugging flags, DisplayTimes
    /// and LogicalSizeZooming, with ClipToCropBox (the written image is the crop box, B57)
    static PDFRenderer::Features getOutputFeatures(PDFRenderer::Features features);

    /// The features of the settings of a view used for an export (R4-10 b): getOutputFeatures
    /// without the color modes of the view and IgnoreOptionalContent
    static PDFRenderer::Features getExportFeatures(PDFRenderer::Features features);

    /// The settings of the rendering of a run (PDFDocumentToolContext::rendering, R4-10 b): its
    /// given values (the features by getExportFeatures), the defaults above for the empty ones
    static PDFDocumentToolPageRenderSettings create(const PDFDocumentToolRenderContext& rendering);
};

/// The tool PDF to Images (convert.pdf-to-images, Figma 339:3546 ... 339:7293; plan of step 4 of
/// phase 2, 4.4, K4.9 - K4.11, owner answers Q4.5 - Q4.7, R4-10 b, R4-16, R4-19; package P1). The
/// pages of the selection are rendered by PDFRasterizerPool::renderOrdered (the budget of 4 GiB,
/// the reorder window, the cancellation) and written in the order of the pages by the file sink of
/// the context (PDFDocumentToolFileSink); the problems of a page come from its errors
/// (PDFRenderedPageImage::getReportedErrors), never from the signal renderError. The size of the
/// image comes from the crop box with /Rotate and /UserUnit (B57); a page larger than the limit of
/// the format (WebP 16 383, JPEG 65 535, others 32 767 pixels per side, 2 GiB of data) is an error
/// of the validation of that page (R4-19). The layers: the GUI gives the states of the tab, MCP and
/// PdfTool OCUsage::Export (R4-10 b); the color modes of the view and the debugging flags are never
/// used; the annotations by the flag /F Print (12.5.3, Table 167).
///
/// Parameters: pages, format (an id of QImageWriter::supportedImageFormats: png, jpeg, tiff, webp,
/// bmp, ...), subtype, compression (-1 = the default of the format), quality (-1 = the default),
/// optimizedWrite, progressiveScanWrite, sizeBy (dpi | pixels), dpi (72 to 6000), pixels (the longer
/// side, 100 to 16 384), folder, fileName (default "{source_base}_@", it must contain @),
/// existingFiles (PDFDocumentToolFileOutput); onlyOutputs (panel-only: Retry N Files and Choose
/// Folder... of the result dialog write only these names, K4.11, R4-20). No gamma (B54, K4.10).
///
/// Result: Files written by the tool (WrittenFiles of the result dialog): writtenFiles,
/// notWrittenFiles, failedFiles, renderingIssues. Facts of 339:6505 "files" ("1,592 PNG ·
/// Report_1.png … Report_1592.png"), "size" (Total size), "time"; of 339:7293 (failed files)
/// "written" ("1,589 PNG · 2.1 GB"), "failed" ("3 files"), "time"; of 356:12504 (Stopped)
/// "written", "notExported" ("Pages 412 to 1,592"), "time"; "renderingIssues" ("2 pages") when
/// a page has problems. Data "fileCount", "writtenCount", "notWrittenCount", "failedCount",
/// "sizeTotal", "fileNames", "format", "durationMilliseconds".
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolPdfToImages : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolPdfToImages)

public:
    static constexpr const char* TOOL_ID = "convert.pdf-to-images";

    /// Names of the parameters (folder, fileName and existingFiles are of PDFDocumentToolFileOutput)
    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_FORMAT = "format";
    static constexpr const char* PARAMETER_SUBTYPE = "subtype";
    static constexpr const char* PARAMETER_COMPRESSION = "compression";
    static constexpr const char* PARAMETER_QUALITY = "quality";
    static constexpr const char* PARAMETER_OPTIMIZED_WRITE = "optimizedWrite";
    static constexpr const char* PARAMETER_PROGRESSIVE_SCAN_WRITE = "progressiveScanWrite";
    static constexpr const char* PARAMETER_SIZE_BY = "sizeBy";
    static constexpr const char* PARAMETER_DPI = "dpi";
    static constexpr const char* PARAMETER_PIXELS = "pixels";

    /// Identifiers of Size by (339:3546, 356:11387)
    static constexpr const char* SIZE_BY_DPI = "dpi";
    static constexpr const char* SIZE_BY_PIXELS = "pixels";

    /// Default format and name (339:3546: PNG, "Report_@")
    static constexpr const char* DEFAULT_FORMAT = "png";
    static constexpr const char* DEFAULT_FILE_NAME = "{source_base}_@";

    /// Bounds of the resolution (the old dialog: 72 to 6000 dpi, 100 to 16 384 pixels)
    static constexpr int MINIMUM_DPI = 72;
    static constexpr int MAXIMUM_DPI = 6000;
    static constexpr int DEFAULT_DPI = 300;
    static constexpr int MINIMUM_PIXELS = 100;
    static constexpr int MAXIMUM_PIXELS = 16384;
    static constexpr int DEFAULT_PIXELS = 2000;

    /// The geometry of the image of a page (B57): the crop box with /Rotate and /UserUnit
    class PDF4QTLIBCORESHARED_EXPORT PageGeometry
    {
    public:
        QSize imageSize;        ///< The written image (the rotated crop box)
        QSize renderSize;       ///< The rendered image (the rotated media box; the renderer maps the media box)
        QRect cropRect;         ///< The written part of the rendered image

        bool isValid() const { return !imageSize.isEmpty() && !renderSize.isEmpty(); }
    };

    /// The geometry of the image of a page by the normalized parameters (sizeBy, dpi, pixels):
    /// DPI - the crop box in points x /UserUnit x dpi / 72; Pixels - the longer side of the crop
    /// box is \p pixels. An invalid geometry for a page without an area.
    static PageGeometry getPageGeometry(const PDFPage* page, const QVariantMap& parameters);

    /// The image of the page is too large for the format (R4-19): the translated reason ("Page 12
    /// would be 70,157 px high – lower the resolution"), empty = it fits
    static QString checkPageGeometry(PDFInteger pageIndex, const PageGeometry& geometry, const PDFDocumentToolImageFormat& format);

    /// The pages of the selection (indices from 0); empty and the reason for an invalid one
    static std::vector<PDFInteger> getPages(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage);

    /// The names of the files of the pages (the template of the parameters, the extension of the
    /// format, "@" the page, "#" the number of the file); false and the reason (no "@", equal names)
    static bool createFileNames(const std::vector<PDFInteger>& pages, const QString& sourceFileName, const QVariantMap& parameters, const QDate& date, QStringList* fileNames, QString* errorMessage);

    /// The date of "{date}" of a run (PDF to Images, Extract Images): today, but Retry N Files
    /// (onlyOutputs, K4.11) writes the files of the earlier run - its names carry the date of that
    /// run (RS4CORE: a Retry after midnight found none of them). The dates written in the names
    /// (ISO 8601, as {date} writes them) are tried after today; the first date, whose names
    /// \p createNames contain every name of onlyOutputs, is the date of the run (today, when none).
    static QDate getRunDate(const QVariantMap& parameters, const std::function<QStringList(const QDate&)>& createNames);

    /// Renders the pages into the file sink of the context (see the class) with the render
    /// settings (PdfTool gives its options, execute the default)
    PDFDocumentToolResult exportPages(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFDocumentToolPageRenderSettings& renderSettings,
                                      const PDFOperationControl* control, PDFProgressSink* progress) const;

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
    virtual QVariantMap mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_PDFTOIMAGES_H
