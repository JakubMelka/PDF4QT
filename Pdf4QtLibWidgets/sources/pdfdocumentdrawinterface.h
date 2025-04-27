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

#ifndef PDFDOCUMENTDRAWINTERFACE_H
#define PDFDOCUMENTDRAWINTERFACE_H

#include "pdfwidgetsglobal.h"
#include "pdfglobal.h"
#include "pdfexception.h"

#include <optional>

class QPainter;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

namespace pdf
{
class PDFColorConvertor;
class PDFPrecompiledPage;
class PDFTextLayoutGetter;

class PDF4QTLIBWIDGETSSHARED_EXPORT IDocumentDrawInterface
{
public:
    explicit inline IDocumentDrawInterface() = default;
    virtual ~IDocumentDrawInterface() = default;

    /// Performs drawing of additional graphics onto the painter using precompiled page,
    /// optionally text layout and page point to device point matrix.
    /// \param painter Painter
    /// \param pageIndex Page index
    /// \param compiledPage Compiled page
    /// \param layoutGetter Layout getter
    /// \param pagePointToDevicePointMatrix Matrix mapping page space to device point space
    /// \param convertor Color convertor
    /// \param[out] errors Output parameter - rendering errors
    virtual void drawPage(QPainter* painter,
                          pdf::PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const;

    /// Performs drawing of additional graphics after all pages are drawn onto the painter.
    /// \param painter Painter
    /// \param rect Draw rectangle (usually viewport rectangle of the pdf widget)
    virtual void drawPostRendering(QPainter* painter, QRect rect) const;

    /// Returns true if drawing of the page content should be suppressed.
    /// This is used for special purposes, such as rendering edited page content.
    virtual bool isPageContentDrawSuppressed() const;
};

/// Input interface for handling events. Implementations should react on these events,
/// and set it to accepted, if they were consumed by the interface. Interface, which
/// consumes mouse press event, should also consume mouse release event.
class IDrawWidgetInputInterface
{
public:
    explicit inline IDrawWidgetInputInterface() = default;
    virtual ~IDrawWidgetInputInterface() = default;

    enum InputPriority
    {
        ToolPriority = 10,
        FormPriority = 20,
        AnnotationPriority = 30,
        UserPriority = 40
    };

    /// Handles shortcut override event. Accept this event, when you want given
    /// key sequence to be propagated to keyPressEvent.
    /// \param widget Widget
    /// \param event Event
    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) = 0;

    /// Handles key press event from widget
    /// \param widget Widget
    /// \param event Event
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) = 0;

    /// Handles key release event from widget
    /// \param widget Widget
    /// \param event Event
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event) = 0;

    /// Handles mouse press event from widget
    /// \param widget Widget
    /// \param event Event
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) = 0;

    /// Handles mouse double click event from widget
    /// \param widget Widget
    /// \param event Event
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event) = 0;

    /// Handles mouse release event from widget
    /// \param widget Widget
    /// \param event Event
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) = 0;

    /// Handles mouse move event from widget
    /// \param widget Widget
    /// \param event Event
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) = 0;

    /// Handles mouse wheel event from widget
    /// \param widget Widget
    /// \param event Event
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event) = 0;

    /// Returns tooltip
    virtual QString getTooltip() const = 0;

    /// Returns current cursor
    virtual const std::optional<QCursor>& getCursor() const = 0;

    /// Returns input priority (interfaces with higher priority
    /// will get input events before interfaces with lower priority)
    virtual int getInputPriority() const = 0;

    class Comparator
    {
    public:
        explicit constexpr inline Comparator() = default;

        bool operator()(IDrawWidgetInputInterface* left, IDrawWidgetInputInterface* right) const
        {
            return std::make_pair(left->getInputPriority(), left) < std::make_pair(right->getInputPriority(), right);
        }
    };

    static constexpr Comparator getComparator() { return Comparator(); }
};

}   // namespace pdf

#endif // PDFDOCUMENTDRAWINTERFACE_H
