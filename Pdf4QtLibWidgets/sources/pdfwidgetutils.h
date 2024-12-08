//    Copyright (C) 2019-2021 Jakub Melka
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

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    /// Overrides automatically detected dark theme / light theme settings
    static void setDarkTheme(bool isDarkTheme);
#endif

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
