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

#ifndef PDFAPPWINDOWGEOMETRY_H
#define PDFAPPWINDOWGEOMETRY_H

#include "pdfappglobal.h"

#include <QRect>
#include <QString>

#include <vector>

class QWidget;

namespace pdf
{

/// The geometry of the main window remembered across the starts (the state
/// setting window.geometry, owner 2026-10-29): the normal geometry (the position
/// and the size of the restored window in the coordinates of Qt - the client area,
/// not the frame), the maximized state and the screen with its geometry, so a
/// changed setup of the monitors is recognized.
///
/// The value is an own JSON text, not QWidget::saveGeometry: the frame of the
/// window is not the frame of the system (PDFAppWindowFrame removes the title bar
/// by WM_NCCALCSIZE on Windows, the window is frameless on Linux), so the frame
/// geometry of a blob of saveGeometry and the title bar, which restoreGeometry
/// keeps on a screen, do not describe the window. The client geometry is what the
/// window sets by setGeometry.
///
/// The stored text is untrusted (another version, a hostile writer of the
/// database): a text, which is not a valid object of the format, gives no
/// geometry (the window takes its default), the placement keeps the window
/// inside of the available geometry of a screen, which exists now.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppWindowGeometry
{
public:
    /// A screen of the placement (QScreen, or a screen of a test)
    struct Screen
    {
        QString name;
        QRect geometry;
        QRect availableGeometry;
    };

    /// Limits of the stored value
    static constexpr int MAXIMUM_TEXT_LENGTH = 1024;
    static constexpr int MAXIMUM_SCREEN_NAME_LENGTH = 256;
    static constexpr int MAXIMUM_COORDINATE = 1000000;
    static constexpr int MAXIMUM_SIZE = 100000;

    /// Offset of a window cascaded from another window of the process
    static constexpr int CASCADE_OFFSET = 32;

    /// Version of the format of the text
    static constexpr int VERSION = 1;

    PDFAppWindowGeometry() = default;
    PDFAppWindowGeometry(const QRect& normalGeometry, bool isMaximized, const QString& screenName, const QRect& screenGeometry);

    /// The normal geometry and the geometry of the screen are within the limits
    bool isValid() const;

    const QRect& getNormalGeometry() const { return m_normalGeometry; }
    bool isMaximized() const { return m_isMaximized; }
    const QString& getScreenName() const { return m_screenName; }
    const QRect& getScreenGeometry() const { return m_screenGeometry; }

    /// The stored text (compact JSON, at most MAXIMUM_TEXT_LENGTH characters);
    /// an invalid geometry gives an empty text
    QString toText() const;

    /// The geometry of the stored text; garbage, a text of another version or of
    /// wrong types, a non-integer or out of range number give an invalid geometry
    static PDFAppWindowGeometry fromText(const QString& text);

    /// The geometry of a window: its normal geometry (also of a maximized, a full
    /// screen or a minimized window), \p isMaximized (the state below the full
    /// screen), the screen of the window
    static PDFAppWindowGeometry fromWindow(const QWidget* window, bool isMaximized);

    /// The screens of the application
    static std::vector<Screen> getScreens();

    /// The geometry of the restored window on \p screens: the same screen (by its
    /// name and geometry) keeps the rectangle, a screen of the name with another
    /// geometry keeps the position relative to the screen, otherwise the screen
    /// with the largest part of the rectangle or the nearest screen takes it. The
    /// window is moved and bounded into the available geometry of the screen (at
    /// least \p minimumSize). \returns false for an invalid geometry or no screen.
    bool place(const std::vector<Screen>& screens, const QSize& minimumSize, QRect* geometry) const;

    /// The geometry of a new window of the process next to the window of
    /// \p normalGeometry: moved by CASCADE_OFFSET (to the top left corner of the
    /// available geometry, when it leaves it), never at the same position
    static QRect cascade(const QRect& normalGeometry, const std::vector<Screen>& screens);

private:
    QRect m_normalGeometry;
    bool m_isMaximized = false;
    QString m_screenName;
    QRect m_screenGeometry;
};

}   // namespace pdf

#endif // PDFAPPWINDOWGEOMETRY_H
