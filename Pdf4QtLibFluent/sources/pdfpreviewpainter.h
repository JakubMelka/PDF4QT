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

#ifndef PDFPREVIEWPAINTER_H
#define PDFPREVIEWPAINTER_H

#include "pdffluentglobal.h"

#include <QMargins>
#include <QRectF>
#include <QSize>
#include <QString>

class QPainter;

namespace pdf
{

/// Drawing of the pictures of the design (Figma page Components, chapter 4.7
/// of components.md): the schematic previews of the color modes and of the page
/// layouts (Mode preview, Layout preview), the illustration of a locked
/// document (Locked document) and the logo of the application (Logo / PDF4QT).
/// The pictures are schemes drawn from the tokens sample/*, reading/*, bg/track
/// and border/strong, not a preview of a real document; they have no states
/// (they are content: a disabled card keeps its picture).
///
/// All functions tolerate a null painter and an empty, negative or invalid
/// rectangle (nothing is drawn), restore the state of the painter, take the
/// colors of the active theme at the time of drawing and draw only into the
/// given rectangle (the shadow of the locked document reaches beyond it only on
/// request). The widget of the locked document is PDFLockedDocumentPicture
/// (pdflockeddocumentpicture.h). A rectangle smaller than the picture scales it down evenly;
/// a larger one keeps the size of the design (a mode preview fills the width
/// of the rectangle as the component, the other pictures are centered).
///
/// Only in the thread of the user interface (as PDFComponentPainter): the
/// colors of the active theme and the cache of the logo are read without a lock.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFPreviewPainter
{
public:
    PDFPreviewPainter() = delete;

    /// Picture of a preview (the variant of the design)
    enum class Preview
    {
        ModeNormal,         ///< Mode preview/Normal 171:4367
        ModeInverted,       ///< Mode preview/Inverted 171:4376
        ModeGrayscale,      ///< Mode preview/Grayscale 171:4385
        ModeHighContrast,   ///< Mode preview/High contrast 171:4394
        ModeBitonal,        ///< Mode preview/Bitonal 171:4403
        ModeCustomColors,   ///< Mode preview/Custom colors 171:4412
        LayoutSinglePage,   ///< Layout preview/Single page 192:735
        LayoutContinuous,   ///< Layout preview/Continuous 192:743
        LayoutTwoPages,     ///< Layout preview/Two pages 192:758
        LayoutTwoColumns    ///< Layout preview/Two columns 192:773
    };

    /// State of the illustration of a locked document (component Locked document 225:906)
    enum class LockedState
    {
        Locked,     ///< The document asks for its password (blue)
        Wrong       ///< A wrong password has been entered (red)
    };

    /// Returns the valid preview (an invalid value is replaced by ModeNormal)
    static Preview normalized(Preview preview);

    /// Returns the valid state (an invalid value is replaced by Locked)
    static LockedState normalized(LockedState state);

    /// Returns true, if the preview is a color mode (false for a page layout)
    static bool isModePreview(Preview preview);

    /// Returns the name of the preview for the user ("Normal", "High contrast",
    /// "Two columns", translated), the default name of a card showing it
    static QString getPreviewName(Preview preview);

    /// Returns the size of a preview (115 x 89)
    static QSize getPreviewSize();

    /// Draws the preview into the rectangle
    static void paintPreview(QPainter* painter, const QRectF& rect, Preview preview);

    /// Returns the size of the illustration of a locked document (160 x 160)
    static QSize getLockedDocumentSize();

    /// Returns how far the shadow of the badge of the locked document reaches
    /// beyond its 160 x 160 (right 6, bottom 10): the component does not clip it
    static QMargins getLockedDocumentOverflowMargins();

    /// Draws the illustration of a locked document centered into the rectangle.
    /// With \p paintOverflow the part of the shadow of the badge, which lies
    /// outside of the illustration, is drawn as well (scaled with the
    /// illustration, at most \p getLockedDocumentOverflowMargins beyond the rectangle).
    static void paintLockedDocument(QPainter* painter, const QRectF& rect, LockedState state, bool paintOverflow = false);

    /// Returns the size of the logo in the design (130 x 130; the logo is scaled freely)
    static QSize getLogoSize();

    /// Returns the path of the SVG file of the logo in the resources of the
    /// library (the export of the component Logo / PDF4QT 110:2154, its colors
    /// are the values of brand/logo-red and brand/logo-green, which are equal
    /// in both themes)
    static QString getLogoResourcePath();

    /// Draws the logo into the rectangle (scaled up or down, keeping its aspect
    /// ratio, centered). The SVG is parsed once: the picture of each size of the
    /// device pixels is kept (a few sizes); a painter rotated or sheared draws
    /// the SVG itself.
    static void paintLogo(QPainter* painter, const QRectF& rect);
};

}   // namespace pdf

#endif // PDFPREVIEWPAINTER_H
