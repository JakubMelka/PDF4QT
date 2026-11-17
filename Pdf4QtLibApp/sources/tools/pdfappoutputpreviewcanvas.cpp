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

#include "pdfappoutputpreviewcanvas.h"

#include "pdfapppagelayout.h"
#include "pdfapptoolpanelparts.h"

#include "pdffluentwidgets.h"
#include "pdfpainterutils.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace pdf
{

// ------------------------------------------------------------------ PDFAppOutputPreviewPicture

PDFAppOutputPreviewPicture::PDFAppOutputPreviewPicture(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("outputPreviewPicture"));
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

PDFAppOutputPreviewPicture::~PDFAppOutputPreviewPicture() = default;

void PDFAppOutputPreviewPicture::setImage(const QImage& image)
{
    // No imageRectChanged here: the GUI sets the image and announces its status text itself
    // (updateParts), a signal in the middle of its update would run foreign code too early
    m_image = image;
    update();
}

void PDFAppOutputPreviewPicture::setRenderingText(const QString& text)
{
    if (m_renderingText != text)
    {
        m_renderingText = text;
        update();
    }
}

QRect PDFAppOutputPreviewPicture::getImageRect() const
{
    if (m_image.isNull())
    {
        return QRect();
    }

    // The image fitted into the widget, centered (page-view 474:8665)
    const QRect available = rect().adjusted(MARGIN, MARGIN, -MARGIN, -MARGIN);
    const QSizeF imageSize = QSizeF(m_image.size()) / m_image.devicePixelRatio();
    const QSize size = imageSize.scaled(QSizeF(available.size()), Qt::KeepAspectRatio).toSize();
    return QRect(QPoint(available.left() + (available.width() - size.width()) / 2, available.top() + (available.height() - size.height()) / 2), size);
}

QPoint PDFAppOutputPreviewPicture::mapToPixel(const QPoint& position) const
{
    const QRect imageRect = getImageRect();
    if (imageRect.isEmpty() || !imageRect.contains(position))
    {
        return QPoint(-1, -1);
    }

    const double x = double(position.x() - imageRect.left()) / imageRect.width() * m_image.width();
    const double y = double(position.y() - imageRect.top()) / imageRect.height() * m_image.height();
    return QPoint(qBound(0, int(x), m_image.width() - 1), qBound(0, int(y), m_image.height() - 1));
}

void PDFAppOutputPreviewPicture::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const QRect imageRect = getImageRect();
    if (imageRect.isEmpty())
    {
        return;
    }

    // The page (a page does not follow the theme of the application) and its line border/strong
    {
        PDFPainterStateGuard guard(&painter);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (!m_renderingText.isEmpty())
        {
            // 476:11033: the old picture dimmed while the new one renders
            painter.setOpacity(0.4);
        }
        painter.drawImage(imageRect, m_image);
    }
    PDFStylePainter::strokeInside(&painter, QRectF(imageRect).adjusted(-1, -1, 1, 1), PDFStylePainter::Radii(0), PDFThemeColors::BorderStrong);

    if (!m_renderingText.isEmpty())
    {
        // The chip "Rendering page 3…" in the middle: bg/surface, border/default, radius 18, the icon refresh
        const QFont font = PDFThemeFonts::getFont(QFont::Medium, 13);
        const qreal textWidth = PDFStylePainter::textWidth(m_renderingText, font);
        const QSizeF chipSize(16.0 + 16.0 + 8.0 + textWidth + 16.0, 36.0);
        const QRectF chip(QPointF(imageRect.center().x() - chipSize.width() / 2.0, imageRect.center().y() - chipSize.height() / 2.0), chipSize);
        const PDFStylePainter::Radii radii(18);
        PDFStylePainter::fill(&painter, chip, radii, PDFThemeColors::BgSurface);
        PDFStylePainter::strokeInside(&painter, chip, radii, PDFThemeColors::BorderDefault);
        const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
        const QRectF icon(isRightToLeft ? chip.right() - 32.0 : chip.left() + 16.0, chip.center().y() - 8.0, 16.0, 16.0);
        PDFStylePainter::icon(&painter, icon, QStringLiteral("refresh-cw"), PDFThemeColors::TextAccent);
        const QRectF text(isRightToLeft ? chip.left() + 16.0 : icon.right() + 8.0, chip.top(), textWidth, chip.height());
        PDFStylePainter::text(&painter, text, Qt::AlignVCenter | Qt::AlignLeft | Qt::AlignAbsolute, m_renderingText, font, PDFStylePainter::color(PDFThemeColors::TextPrimary));
    }
}

void PDFAppOutputPreviewPicture::mouseMoveEvent(QMouseEvent* event)
{
    BaseClass::mouseMoveEvent(event);
    Q_EMIT pixelHovered(mapToPixel(event->position().toPoint()));
}

void PDFAppOutputPreviewPicture::leaveEvent(QEvent* event)
{
    BaseClass::leaveEvent(event);
    Q_EMIT pixelHovered(QPoint(-1, -1));
}

void PDFAppOutputPreviewPicture::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    Q_EMIT imageRectChanged();
}

// ------------------------------------------------------------------ PDFAppOutputPreviewCanvas

PDFAppOutputPreviewCanvas::PDFAppOutputPreviewCanvas(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("outputPreviewCanvas"));
    setFocusPolicy(Qt::StrongFocus);

    // document-canvas 474:8419: padding 20, gap 12
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(GAP);

    // preview-bar 474:8662
    QWidget* bar = new QWidget(this);
    bar->setFixedHeight(BAR_HEIGHT);
    QHBoxLayout* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    m_title = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, bar);
    m_title->setObjectName(QStringLiteral("outputPreviewTitle"));
    barLayout->addWidget(m_title, 1);
    layout->addWidget(bar);

    // 476:11358: the errors of the rendering over the page (red)
    m_error = new PDFAppToolCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), QString(), QString(), this);
    m_error->setObjectName(QStringLiteral("outputPreviewError"));
    m_error->hide();
    layout->addWidget(m_error);

    m_picture = new PDFAppOutputPreviewPicture(this);
    connect(m_picture.data(), &PDFAppOutputPreviewPicture::pixelHovered, this, &PDFAppOutputPreviewCanvas::onPixelHovered);
    // Queued (RS7APP L9): the picture emits from its resizeEvent, inside of the layout of Qt, which goes on with
    // the picture after it; a slot of the GUI says the status, whose slots may delete the canvas with the picture.
    // A queued call to a deleted canvas is dropped by Qt.
    connect(m_picture.data(), &PDFAppOutputPreviewPicture::imageRectChanged, this, &PDFAppOutputPreviewCanvas::onImageRectChanged, Qt::QueuedConnection);
    layout->addWidget(m_picture, 1);
}

PDFAppOutputPreviewCanvas::~PDFAppOutputPreviewCanvas() = default;

void PDFAppOutputPreviewCanvas::setImage(const QImage& image)
{
    m_picture->setImage(image);
}

const QImage& PDFAppOutputPreviewCanvas::getImage() const
{
    return m_picture->getImage();
}

void PDFAppOutputPreviewCanvas::setTitle(const QString& title)
{
    m_title->setText(title);
}

void PDFAppOutputPreviewCanvas::setRenderingText(const QString& text)
{
    m_picture->setRenderingText(text);
}

void PDFAppOutputPreviewCanvas::setError(const QString& title, const QString& text)
{
    m_error->setTexts(title, text);
    m_error->setVisible(!title.isEmpty());
}

void PDFAppOutputPreviewCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas: bg/track
    QPainter painter(this);
    painter.fillRect(rect(), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgTrack));
}

void PDFAppOutputPreviewCanvas::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
        case Qt::Key_PageUp:
            event->accept();
            Q_EMIT pageStepRequested(-1);
            return;
        case Qt::Key_PageDown:
            event->accept();
            Q_EMIT pageStepRequested(1);
            return;
        default:
            break;
    }
    BaseClass::keyPressEvent(event);
}

void PDFAppOutputPreviewCanvas::onPixelHovered(const QPoint& pixel)
{
    Q_EMIT pixelHovered(pixel);
}

void PDFAppOutputPreviewCanvas::onImageRectChanged()
{
    Q_EMIT imageRectChanged();
}

}   // namespace pdf

#include "moc_pdfappoutputpreviewcanvas.cpp"
