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

#ifndef PDFTHEMEFONTS_H
#define PDFTHEMEFONTS_H

#include "pdffluentglobal.h"

#include <QFont>
#include <QString>

namespace pdf
{

/// Fonts of the user interface. The design uses the font Inter, which is a part
/// of the library (static files Regular, Medium, Semi Bold and Bold). Sizes are
/// in logical pixels, as the design has them.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFThemeFonts
{
public:
    PDFThemeFonts() = delete;

    enum class Role
    {
        Body,               ///< Regular 13 - texts, values of fields, menu items, labels of check boxes
        BodyMedium,         ///< Medium 13 - toolbar menu buttons, unselected document tabs, selected rows
        BodySemiBold,       ///< Semi Bold 13 - labels of buttons, selected tabs, titles of group boxes
        Small,              ///< Regular 12 - secondary texts, shortcuts, suffixes
        SmallMedium,        ///< Medium 12 - tooltips
        SmallSemiBold,      ///< Semi Bold 12 - filter chips, values of compact selects
        Caption,            ///< Medium 11 - captions, tags, table headers, badges
        CaptionRegular,     ///< Regular 11 - status bar
        CaptionSemiBold,    ///< Semi Bold 11 - checked segments
        Tiny,               ///< Regular 10
        Title,              ///< Bold 15 - titles of sections and cards
        DialogTitle,        ///< Semi Bold 16 - titles of dialogs
        TinySemiBold,       ///< Semi Bold 10 - name of the reading theme under its sample
        BodyBold,           ///< Bold 13 - title of a resource card
        TileTitle,          ///< Bold 14 - title of a quick action tile and of a highlight card
        Display             ///< Bold 22 - sample letters of a reading theme card
    };

    /// Loads the fonts of the library into the font database of the application.
    /// The call is repeated safely, a QGuiApplication must exist.
    /// Call from the thread of the user interface only.
    /// \returns False, if a font has not been loaded
    static bool loadFonts();

    /// Returns the family of the fonts of the user interface
    static QString getFamily();

    /// Returns the font of the role
    static QFont getFont(Role role);

    /// Returns the font of the given weight and size in pixels
    static QFont getFont(QFont::Weight weight, int pixelSize);

    /// Returns the family of the monospaced font (JetBrains Mono of the library,
    /// owner decision Q5 of step 7: addresses, commands and code boxes of the
    /// page AI); the fixed font of the system, when the bundled font is missing
    static QString getMonospaceFamily();

    /// Returns the monospaced font of the given weight and size in pixels
    static QFont getMonospaceFont(QFont::Weight weight, int pixelSize);
};

}   // namespace pdf

#endif // PDFTHEMEFONTS_H
