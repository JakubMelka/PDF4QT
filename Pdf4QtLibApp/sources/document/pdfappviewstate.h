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

#ifndef PDFAPPVIEWSTATE_H
#define PDFAPPVIEWSTATE_H

#include "pdfappglobal.h"

#include <QString>

#include <optional>

namespace pdf
{

/// Page layout of the view of a document (View panel 196:2749). The first page on
/// the right side is a separate option (PDFAppViewState::firstPageRight).
enum class PDFAppViewLayout
{
    SinglePage,     ///< One page at a time
    Continuous,     ///< Pages in one scrolled column
    TwoPages,       ///< Two pages at a time
    TwoColumns      ///< Pages in two scrolled columns
};

/// Zoom of the view: one of the fits, or a custom zoom factor
enum class PDFAppViewZoomMode
{
    Custom,         ///< PDFAppViewState::zoom
    FitPage,        ///< The whole page
    FitWidth,       ///< The width of the page
    FitHeight       ///< The height of the page
};

/// State of the view of a document (step 3 plan, package B): the layout, the zoom,
/// the rotation of the view and the sidebar. It is stored as a text in the Recent
/// file (PDFRecentRepository::setViewState) and restored, when the document is
/// opened again. The text comes from the database: it is untrusted input, so
/// fromText accepts a bounded JSON object only and keeps each valid value - an
/// invalid or missing value stays unset (the view keeps its default for it).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppViewState
{
public:
    /// Maximal length of the text (the limit of the Recent file)
    static constexpr int MAXIMUM_TEXT_LENGTH = 4096;

    /// Bounds of the zoom factor (the bounds of the draw widget proxy: 8 % - 6400 %)
    static constexpr double MINIMUM_ZOOM = 0.08;
    static constexpr double MAXIMUM_ZOOM = 64.0;

    /// Bounds of the width of the sidebar
    static constexpr int MINIMUM_SIDEBAR_WIDTH = 200;
    static constexpr int MAXIMUM_SIDEBAR_WIDTH = 1200;

    /// Maximal length of the id of a panel
    static constexpr int MAXIMUM_PANEL_ID_LENGTH = 32;

    bool operator==(const PDFAppViewState&) const = default;

    std::optional<PDFAppViewLayout> layout;
    std::optional<bool> firstPageRight;
    std::optional<PDFAppViewZoomMode> zoomMode;
    std::optional<double> zoom;                 ///< Zoom factor of the custom zoom (1.0 = 100 %)
    std::optional<int> rotation;                ///< Rotation of the view in degrees: 0, 90, 180, 270
    std::optional<bool> sidebarVisible;
    std::optional<int> sidebarWidth;            ///< Width of the sidebar in pixels
    std::optional<QString> panelId;             ///< Shown panel of the sidebar (lowercase letters and dashes)

    /// Returns true, if no value is set
    bool isEmpty() const;

    /// Compact JSON text of the set values
    QString toText() const;

    /// Parses the text. A text, which is too long or which is not a JSON object,
    /// gives an empty state; values of a wrong type or out of their range are left out.
    static PDFAppViewState fromText(const QString& text);

    /// Names of the layouts and of the zoom modes in the text (and in the settings
    /// viewing.pageLayout / viewing.zoom); an unknown name gives std::nullopt
    static QString getLayoutName(PDFAppViewLayout layout);
    static std::optional<PDFAppViewLayout> getLayoutFromName(const QString& name);
    static QString getZoomModeName(PDFAppViewZoomMode mode);
    static std::optional<PDFAppViewZoomMode> getZoomModeFromName(const QString& name);

    /// The id of a panel is valid (1 - 32 characters a-z, 0-9 and dashes)
    static bool isValidPanelId(const QString& panelId);
};

}   // namespace pdf

#endif // PDFAPPVIEWSTATE_H
