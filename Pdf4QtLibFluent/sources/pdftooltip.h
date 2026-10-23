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

#ifndef PDFTOOLTIP_H
#define PDFTOOLTIP_H

#include "pdffluentglobal.h"

#include <QPoint>
#include <QString>

class QWidget;

namespace pdf
{

/// Tooltip of the design: the name of the control, its shortcut and an optional
/// description. It replaces QToolTip, whose window takes its colors from
/// a palette. PDFWidgetStyle shows it for the tooltips of widgets and of items
/// of views by itself.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFToolTip
{
public:
    PDFToolTip() = delete;

    /// Look of the tooltip
    enum class Style
    {
        /// Tooltip of a control (Widgets 16): the name Medium 12 text/primary,
        /// border/strong, radius 8, the description wraps at 260 px
        Default,

        /// Tooltip of a link of a document (1028:22472): the name and the
        /// description Regular 11 in lines of 13 px (text/primary, text/muted),
        /// border/default, radius 6; the lines are not wrapped, the name is
        /// elided in the middle and the description at the end at 480 px
        Compact
    };

    /// Shows the tooltip
    /// \param globalPosition Position of the pointer
    /// \param title Name of the control
    /// \param shortcut Shortcut shown after the name
    /// \param description Description shown below the name
    /// \param widget Widget, to which the tooltip belongs. The tooltip is hidden,
    ///        when the pointer leaves the widget.
    static void showText(const QPoint& globalPosition, const QString& title, const QString& shortcut = QString(), const QString& description = QString(), QWidget* widget = nullptr);

    /// Shows the tooltip of the given look (see the other overload)
    static void showText(const QPoint& globalPosition, const QString& title, const QString& shortcut, const QString& description, QWidget* widget, Style style);

    /// Returns the look of the shown tooltip (Default, when no tooltip is shown)
    static Style getStyle();

    /// Shows the tooltip of the widget (its tooltip text, the shortcut of its
    /// action; lines after the first one are the description).
    /// \returns False, if the widget has no tooltip, which can be shown this way
    static bool showForWidget(QWidget* widget, const QPoint& globalPosition, const QPoint& position);

    /// Hides the tooltip
    static void hideText();

    /// Returns true, if the tooltip is shown
    static bool isVisible();

    /// Returns the title of the shown tooltip (an empty string, when no tooltip is shown)
    static QString getTitle();

    /// Returns the window of the tooltip, or nullptr, if no tooltip has been shown yet
    static QWidget* getWindow();
};

}   // namespace pdf

#endif // PDFTOOLTIP_H
