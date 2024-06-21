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
