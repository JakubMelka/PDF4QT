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

    /// Shows the tooltip
    /// \param globalPosition Position of the pointer
    /// \param title Name of the control
    /// \param shortcut Shortcut shown after the name
    /// \param description Description shown below the name
    /// \param widget Widget, to which the tooltip belongs. The tooltip is hidden,
    ///        when the pointer leaves the widget.
    static void showText(const QPoint& globalPosition, const QString& title, const QString& shortcut = QString(), const QString& description = QString(), QWidget* widget = nullptr);

    /// Shows the tooltip of the widget (its tooltip text, the shortcut of its
    /// action; lines after the first one are the description).
    /// \returns False, if the widget has no tooltip, which can be shown this way
    static bool showForWidget(QWidget* widget, const QPoint& globalPosition, const QPoint& position);

    /// Hides the tooltip
    static void hideText();

    /// Returns true, if the tooltip is shown
    static bool isVisible();

    /// Returns the window of the tooltip, or nullptr, if no tooltip has been shown yet
    static QWidget* getWindow();
};

}   // namespace pdf

#endif // PDFTOOLTIP_H
