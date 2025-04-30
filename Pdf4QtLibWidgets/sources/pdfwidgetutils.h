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

#ifndef PDFWIDGETUTILS_H
#define PDFWIDGETUTILS_H

#include "pdfwidgetsglobal.h"
#include "pdfglobal.h"

#include <QIcon>
#include <QWidget>

namespace pdf
{

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFWidgetUtils
{
public:
    PDFWidgetUtils() = delete;

    /// Converts size in MM to pixel size
    static int getPixelSize(const QPaintDevice* device, pdf::PDFReal sizeMM);

    /// Scale horizontal DPI value
    /// \param device Paint device to obtain logical DPI for scaling
    static int scaleDPI_x(const QPaintDevice* device, int unscaledSize);

    /// Scale vertical DPI value
    /// \param device Paint device to obtain logical DPI for scaling
    static int scaleDPI_y(const QPaintDevice* device, int unscaledSize);

    /// Scale horizontal DPI value
    /// \param device Paint device to obtain logical DPI for scaling
    static PDFReal scaleDPI_x(const QPaintDevice* device, PDFReal unscaledSize);

    /// Scales widget based on DPI
    /// \param widget Widget to be scaled
    /// \param unscaledSize Unscaled size of the widget
    static void scaleWidget(QWidget* widget, QSize unscaledSize);

    /// Scales size based on DPI
    /// \param device Paint device to obtain logical DPI for scaling
    /// \param unscaledSize Unscaled size
    static QSize scaleDPI(const QPaintDevice* widget, QSize unscaledSize);

    /// Apply style to the widget
    static void style(QWidget* widget);

    /// Overrides automatically detected dark theme / light theme settings
    static void setDarkTheme(bool isLightTheme, bool isDarkTheme);

    /// Returns true if the dark theme is currently set for the application.
    static bool isDarkTheme();

    /// Converts an action's icon for use in a dark theme.
    /// \param action Pointer to the QAction to be converted.
    /// \param iconSize The size of the action's icon.
    /// \param devicePixelRatioF The device pixel ratio factor.
    static void convertActionForDarkTheme(QAction* action, QSize iconSize, qreal devicePixelRatioF);

    /// Converts an icon for use in a dark theme.
    /// \param icon The icon to be converted.
    /// \param iconSize The size of the icon.
    /// \param devicePixelRatioF The device pixel ratio factor.
    static QIcon convertIconForDarkTheme(QIcon icon, QSize iconSize, qreal devicePixelRatioF);

    /// Converts an action's icon for use in a dark theme.
    /// \param action Pointer to the QAction to be converted.
    /// \param iconSize The size of the action's icon.
    /// \param devicePixelRatioF The device pixel ratio factor.
    template<typename Container>
    static void convertActionsForDarkTheme(const Container& actions, QSize iconSize, qreal devicePixelRatioF)
    {
        for (QAction* action : actions)
        {
            convertActionForDarkTheme(action, iconSize, devicePixelRatioF);
        }
    }

    /// Checks menu accessibility for all descendants of the specified widget.
    /// \param widget The widget whose descendants are to be checked.
    static void checkMenuAccessibility(QWidget* widget);

    /// Checks menu accessibility for the specified menu.
    /// \param menu The menu to be checked for accessibility.
    static void checkMenuAccessibility(QMenu* menu);
};

} // namespace pdf

#endif // PDFWIDGETUTILS_H
