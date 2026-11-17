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

#ifndef PDFAPPOUTPUTPREVIEWCANVAS_H
#define PDFAPPOUTPUTPREVIEWCANVAS_H

#include "pdfappglobal.h"
#include "pdfapptoolpanelparts.h"

#include "pdffluentwidgets.h"

#include <QImage>
#include <QPointer>
#include <QWidget>

namespace pdf
{

class PDFAppToolCallout;
class PDFLabel;

/// The picture of the page of Output Preview (viewport 474:8664): the image of the mode fitted into the widget
/// with 10 px around it and the line border/strong; while a new picture renders, the old one is dimmed and the
/// chip "Rendering page 3…" lies over it (476:11033). The position of the pointer over the image is reported in
/// the pixels of the image (the value under the cursor is read from the stored bitmap, never rendered again).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOutputPreviewPicture : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int MARGIN = 10;

    explicit PDFAppOutputPreviewPicture(QWidget* parent);
    virtual ~PDFAppOutputPreviewPicture() override;

    void setImage(const QImage& image);
    const QImage& getImage() const { return m_image; }

    /// The chip of the rendering (empty = none) over the dimmed picture
    void setRenderingText(const QString& text);
    const QString& getRenderingText() const { return m_renderingText; }

    /// Rectangle of the image in the widget (empty without an image)
    QRect getImageRect() const;

    /// The pixel of the image under a point of the widget (an invalid point outside of the image)
    QPoint mapToPixel(const QPoint& position) const;

Q_SIGNALS:
    /// The pointer is over a pixel of the image (an invalid point = outside of the image)
    void pixelHovered(const QPoint& pixel);

    /// The widget has a new size, so getImageRect may have changed: the zoom of the status bar
    /// follows it (K7.27; a new image is announced by the GUI, which sets it)
    void imageRectChanged();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void leaveEvent(QEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;

private:
    QImage m_image;
    QString m_renderingText;
};

/// The canvas of Output Preview (plan of step 7 of phase 2, 4.6; document-canvas 474:8386): bg/track, padding 20,
/// the bar of 36 ("Page 3 · Separations", Semi Bold 13), the red callout of the errors of the rendering
/// (476:11358), gap 12, the picture of the mode (PDFAppOutputPreviewPicture). A picture comes from the job of the
/// GUI; the canvas only shows it (GUI thread). PgUp / PgDn ask for the previous / next page.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOutputPreviewCanvas : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int PADDING = 20;
    static constexpr int GAP = 12;
    static constexpr int BAR_HEIGHT = 36;

    explicit PDFAppOutputPreviewCanvas(QWidget* parent = nullptr);
    virtual ~PDFAppOutputPreviewCanvas() override;

    /// The shown picture of the page
    void setImage(const QImage& image);
    const QImage& getImage() const;

    void setTitle(const QString& title);
    void setRenderingText(const QString& text);

    /// The callout of the errors of the rendering (an empty title hides it)
    void setError(const QString& title, const QString& text);

    PDFLabel* getTitleLabel() const { return m_title.data(); }
    PDFAppToolCallout* getErrorCallout() const { return m_error.data(); }
    PDFAppOutputPreviewPicture* getPicture() const { return m_picture.data(); }

Q_SIGNALS:
    /// The pointer is over a pixel of the picture (invalid = outside)
    void pixelHovered(const QPoint& pixel);

    /// PgUp (-1) / PgDn (+1)
    void pageStepRequested(int step);

    /// The rectangle of the picture may have changed (PDFAppOutputPreviewPicture::imageRectChanged, taken over
    /// queued: it comes from the event loop after the resize, never from inside of the layout of Qt)
    void imageRectChanged();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;

private:
    void onPixelHovered(const QPoint& pixel);
    void onImageRectChanged();

    QPointer<PDFLabel> m_title;
    QPointer<PDFAppToolCallout> m_error;
    QPointer<PDFAppOutputPreviewPicture> m_picture;
};

}   // namespace pdf

#endif // PDFAPPOUTPUTPREVIEWCANVAS_H
