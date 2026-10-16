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

#ifndef PDFWIDGETSTYLEPROPERTIES_H
#define PDFWIDGETSTYLEPROPERTIES_H

#include "pdffluentglobal.h"

#include <QVariant>

class QObject;
class QWidget;

namespace pdf
{

/// Dynamic properties of widgets (and actions), by which the style selects
/// a variant of the look of the design. A property is set by \p set, which
/// also applies the new look to a widget, which is already shown.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFWidgetStyleProperties
{
public:
    PDFWidgetStyleProperties() = delete;

    /// QPushButton: the primary button (the default button of a dialog is primary by itself)
    static constexpr const char* PRIMARY = "primary";

    /// QAction of a menu, QPushButton: destructive action
    static constexpr const char* DESTRUCTIVE = "destructive";

    /// QLineEdit: pill shaped field (search field)
    static constexpr const char* PILL = "pill";

    /// QLineEdit: height of the field in pixels (34 by default; password field 40,
    /// toolbar field 32)
    static constexpr const char* FIELD_HEIGHT = "fieldHeight";

    /// QLineEdit: distances of the text from the left and from the right edge of the
    /// field (QMargins, the border is included), when the field holds icons or
    /// buttons of its own beside the text (password field, toolbar field)
    static constexpr const char* FIELD_TEXT_INSETS = "fieldTextInsets";

    /// QMenu (a popup window): global rectangle (QRect) of the widget, which has
    /// opened the popup below or above itself and which does not take the focus
    /// (a menu button). The style keeps the panel of the popup (without its shadow)
    /// at the edge, by which the popup is aligned with the widget. The property
    /// is valid for one showing of the popup, the style removes it at its hiding.
    static constexpr const char* POPUP_ANCHOR = "popupAnchor";

    /// QLineEdit, QTextEdit, QComboBox, QSpinBox: the value is invalid
    static constexpr const char* INVALID = "error";

    /// QComboBox: compact variant of property rows
    static constexpr const char* COMPACT = "compact";

    /// QComboBox: select with a prefix drawn before the value
    static constexpr const char* SELECT = "select";

    /// QComboBox: text of the prefix of the select
    static constexpr const char* SELECT_PREFIX = "selectPrefix";

    /// QFrame, QGroupBox, QTabWidget, QWidget: card
    static constexpr const char* CARD = "card";

    /// QFrame, QTextEdit, QWidget: note box
    static constexpr const char* NOTE = "note";

    /// QWidget: page filled with the color of the canvas (background of windows)
    static constexpr const char* CANVAS = "canvas";

    /// QFrame, QWidget: panel of a sidebar
    static constexpr const char* PANEL = "panel";

    /// QDockWidget: sidebar without a title bar
    static constexpr const char* SIDEBAR = "sidebar";

    /// QToolButton: icon tab of a sidebar
    static constexpr const char* SIDEBAR_TAB = "sidebarTab";

    /// QToolBar: row of the icon tabs of a sidebar
    static constexpr const char* SIDEBAR_TABS = "sidebarTabs";

    /// QTabBar: tabs of the documents (they scroll, their labels are not elided).
    /// Other widgets: the top bar of the application holding the tabs of the
    /// documents and the button of a new tab (background bg/titlebar).
    static constexpr const char* DOCUMENT_TABS = "documentTabs";

    /// QToolButton: button of a new tab of the document tabs
    static constexpr const char* NEW_TAB = "newTab";

    /// QToolButton: segment of a segmented control
    static constexpr const char* SEGMENT = "segment";

    /// QToolButton, QPushButton: filter chip
    static constexpr const char* CHIP = "chip";

    /// QToolButton, QPushButton: number shown after the label of a filter chip
    /// (Medium 11), the count is not shown, when the property is not set
    static constexpr const char* CHIP_COUNT = "chipCount";

    /// QToolButton, QPushButton: checkable filter chip, which is an on / off option
    /// (Whole words, Regular expression). When it is on, it shows the check mark
    /// instead of its icon; it has always the place of the icon, so it keeps its width.
    static constexpr const char* OPTION = "option";

    /// QToolButton, QLabel: item of the status bar
    static constexpr const char* STATUS_ITEM = "statusItem";

    /// QMenu, QFrame: popover panel
    static constexpr const char* POPOVER = "popover";

    /// QDialog, QWidget: dialog shown inside of a tab over a scrim
    static constexpr const char* OVERLAY = "overlay";

    /// QTreeView, QTableView, QListView: look of the rows of the view, a row of a
    /// component of the design: "outline" (Outline row: radius 8, paddings 6 / 8,
    /// chevron 14 in the slot of 16, indentation 18, focus ring 2 px inside),
    /// "certificate" (Certificate row: height 34, padding 10, focus ring 2 px inside,
    /// the selection keeps the colors of the texts), "library" (Library row: height
    /// 30, padding 10, no hover). Set it by PDFTreeView::setRowStyle.
    static constexpr const char* ROW_STYLE = "rowStyle";

    /// QProgressBar: thin progress line
    static constexpr const char* THIN = "thin";

    /// QAbstractSpinBox: width reserved for the suffix drawn by the widget
    static constexpr const char* SUFFIX_WIDTH = "suffixWidth";

    /// QAbstractSpinBox: width reserved for the prefix drawn by the widget
    static constexpr const char* PREFIX_WIDTH = "prefixWidth";

    /// Item of the status bar: tone of the item, "warning" for a warning
    static constexpr const char* TONE = "tone";

    /// QWidget: radius of the shape of the widget (used by the focus frame)
    static constexpr const char* RADIUS = "radius";

    /// QToolButton: color of the icon of a segment in all enabled states, the value
    /// of PDFThemeColors::Color (the sun of the Light segment of the theme switch)
    static constexpr const char* ICON_COLOR = "iconColor";

    /// QToolButton: the button shows the chevron of a menu, although it has
    /// no menu (it opens a popup of its own)
    static constexpr const char* MENU_INDICATOR = "menuIndicator";

    /// Sets the property and applies the look to the widget
    static void set(QWidget* widget, const char* name, const QVariant& value = QVariant(true));

    /// Returns true, if the object has the property and its value is not false
    static bool isSet(const QObject* object, const char* name);
};

}   // namespace pdf

#endif // PDFWIDGETSTYLEPROPERTIES_H
