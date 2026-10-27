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


#ifndef PDFAPPMCPRENDERER_H
#define PDFAPPMCPRENDERER_H

#include "pdfappglobal.h"
#include "pdfappmcpdocumentreader.h"
#include "pdfappmcpjob.h"

#include <QImage>
#include <QRectF>
#include <QTransform>

#include <optional>
#include <set>
#include <vector>

namespace pdf
{

class PDFDocument;
class PDFOperationControl;
class PDFPage;

/// What a render job makes
enum class PDFAppMcpRenderKind
{
    Page,       ///< render.page and the image resource of a page
    Overview    ///< render.overview (a contact sheet)
};

/// Parameters of a render job (copied in the GUI thread)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpRenderRequest
{
public:
    PDFAppMcpRenderKind kind = PDFAppMcpRenderKind::Page;
    QString actionId;                       ///< "render.page" (activity)
    PDFAppMcpDocumentHeader header;
    PDFInteger pageIndex = 0;               ///< Page
    int dpi = 150;                          ///< Page
    std::optional<QRectF> area;             ///< Page: the area in the page space
    bool showAnnotations = true;            ///< Page
    bool isJpeg = false;                    ///< Page
    int jpegQuality = 85;                   ///< Page
    std::vector<PDFInteger> pageIndices;    ///< Overview
    int maximumCellSize = 192;              ///< Overview
    bool isResource = false;                ///< The result is a blob resource (resources/read)
    QString resourceUri;
};

/// Geometry of a rendered page: the size of the image and the matrix from the
/// page space (unrotated default user space) to the pixels of the image
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpRenderGeometry
{
public:
    QSize imageSize;
    QTransform pageToImage;
    QRectF area;            ///< Rendered area in the page space (the crop box or its part)

    bool isValid() const { return imageSize.isValid() && !imageSize.isEmpty(); }
};

/// Images of pages for the assistants (plan of step 7, 3.4 and 2.3
/// PDFAppMcpRenderer): PDFRenderer with its own font, ICC profile and
/// annotation manager instances in the worker (as the thumbnailer and the
/// printing), the generic color management, the default optional content of
/// the view. At most 2 renders run at once (heavy jobs).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpRenderer
{
public:
    PDFAppMcpRenderer() = delete;

    /// Most pixels of an image (50 MPx)
    static constexpr qint64 MAXIMUM_PIXELS = qint64(50) * 1000 * 1000;

    /// Largest encoded image (12 MiB)
    static constexpr qint64 MAXIMUM_IMAGE_BYTES = qint64(12) * 1024 * 1024;

    /// Geometry of the page (\p area: only this part, in the page space) at the
    /// resolution; \p errorMessage for an empty area or too many pixels
    static PDFAppMcpRenderGeometry getGeometry(const PDFPage* page, int dpi, const std::optional<QRectF>& area, QString* errorMessage);

    /// Renders the page (a worker)
    static QImage render(const PDFDocument* document, PDFInteger pageIndex, const PDFAppMcpRenderGeometry& geometry, bool showAnnotations, const PDFOperationControl* control);

    /// Matrix [a, b, c, d, e, f] of a transform (PDF order)
    static std::vector<double> getMatrixValues(const QTransform& transform);

    /// Encodes the image (PNG or JPEG with the quality)
    static QByteArray encode(const QImage& image, bool isJpeg, int jpegQuality);
};

/// A render job (heavy)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpRenderJob : public PDFAppMcpJob
{
public:
    PDFAppMcpRenderJob(std::shared_ptr<PDFMcpCall> call, PDFAppDocumentSnapshot snapshot, PDFAppMcpRenderRequest request);

    virtual bool isHeavy() const override { return true; }
    virtual void execute() override;
    virtual void finish(PDFAppMcpHost* host) override;

private:
    void executePage();
    void executeOverview();

    PDFAppMcpRenderRequest m_request;
    std::set<int> m_pages;
    bool m_isFailed = false;
};

}   // namespace pdf

#endif // PDFAPPMCPRENDERER_H
