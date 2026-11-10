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

#ifndef PDFAPPIMAGEPAGEPREVIEW_H
#define PDFAPPIMAGEPAGEPREVIEW_H

#include "pdfappglobal.h"

#include "pdfcms.h"
#include "pdfimagepagefactory.h"

#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QWidget>

namespace pdf
{

/// A page with its image placed by its layout (plan of step 3 of phase 2, 4.1, R1): Images to
/// PDF 336:1976 ("Page 1 of 6 · IMG_2041.jpg", Fit / 100 %, the page 600 x 424 with the shadow
/// 0 / 8 / 24), the new pages of Add Image as Page 388:18837 (package C draws them by this class
/// - step C2 of 9.4).
///
///  - The page and the image are placed by PDFImagePageLayout::getPageSize, getArea and
///    getImageRect - one rule for the preview and the page of the core; the image is clipped by
///    the area (Fill, Original size). The margins and the anchor are never mirrored for
///    right-to-left (the page belongs to the document).
///  - Fit (default): the page fills the contents rectangle of the widget (its contents margins
///    are the room of the shadow and of the captions of the caller), centered, its ratio kept.
///    Actual: 100 % = the page at 96 dpi (K12 of step 2), centered in the contents rectangle;
///    sizeHint is the size of the page then, so a scroll area scrolls it.
///  - The paper is reading/paper/page, the shadow 0 / 8 / 24 of effect/shadow (setShadowVisible,
///    on by default); tokens only.
///  - The image is a value (a decoded preview, QImage implicitly shared) with its size in points
///    by its resolution; nothing of a document or of a thread is held.
///  - Ownership: the GUI of the tool (a child of its canvas page), held by a QPointer. It emits
///    no signal. Thread: GUI only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppImagePagePreview : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Resolution of the actual size (K12 of step 2: a point is 1/72 inch at 96 dpi)
    static constexpr double ACTUAL_DPI = 96.0;

    /// Shadow of the paper (336:2235)
    static constexpr int SHADOW_OFFSET = 8;
    static constexpr int SHADOW_BLUR = 24;

    explicit PDFAppImagePagePreview(QWidget* parent);
    virtual ~PDFAppImagePagePreview() override;

    /// The image shown (as it is shown: the orientation of EXIF applied) and its size in points
    /// by its resolution (PDFImagePageFactory::getPageSize)
    void setImage(const QImage& image, QSizeF imageSize);
    const QImage& getImage() const { return m_image; }
    QSizeF getImageSize() const { return m_imageSize; }

    /// The layout of the image on its page
    void setPageLayout(const PDFImagePageLayout& layout);
    const PDFImagePageLayout& getPageLayout() const { return m_layout; }

    /// Fit (true, default) or the actual size (false)
    void setFit(bool isFit);
    bool isFit() const { return m_isFit; }

    /// The shadow of the paper is drawn (default true)
    void setShadowVisible(bool isVisible);
    bool isShadowVisible() const { return m_isShadowVisible; }

    /// Size of the page in points (PDFImagePageLayout::getPageSize)
    QSizeF getPageSize() const;

    /// Rectangle of the page in the coordinates of the widget (empty without an image)
    QRectF getPageRect() const;

    /// Rectangle of the laid out image in the coordinates of the widget (empty without an image;
    /// it may be larger than the area, getAreaRect clips it)
    QRectF getImageRect() const;

    /// Area of the image (the page without the margins) in the coordinates of the widget
    QRectF getAreaRect() const;

    /// Scale of the drawn page to points (0 without an image)
    double getScale() const;

    /// Rows of the picture converted by one call of the color management
    static constexpr int COLOR_MANAGED_ROWS = 64;

    /// The picture as a viewer shows the image of the new page (K43): its colors through the
    /// color management of the application - an image with an ICC profile from its profile (the
    /// ICCBased color space of PDFImagePageFactory), else DeviceGray or DeviceRGB - with the
    /// relative colorimetric intent (the default of 8.6.5.8 of ISO 32000-2); the alpha is kept.
    /// A null \p cms or a failed conversion gives the picture back. Any thread (plain values).
    static QImage getColorManagedPicture(const QImage& picture, const PDFCMS* cms);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    /// The page has a size (an image of a size)
    bool hasPage() const;

    /// Maps a rectangle of the page (points from its top left corner) to the widget
    QRectF mapFromPage(const QRectF& rect) const;

    QImage m_image;
    QSizeF m_imageSize;
    PDFImagePageLayout m_layout;
    bool m_isFit = true;
    bool m_isShadowVisible = true;
};

}   // namespace pdf

#endif // PDFAPPIMAGEPAGEPREVIEW_H
