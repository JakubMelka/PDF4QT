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

#ifndef APPSETTINGSFIXTURE_H
#define APPSETTINGSFIXTURE_H

#include "appdocumentfixture.h"

#include <QImage>
#include <QString>
#include <QVariant>

namespace pdf
{
class PDFAppDocumentTab;
class PDFAppSettingRow;
class PDFAppSettingsPage;
class PDFSettingsRepository;
}

namespace pdftest
{

/// Fixture of the tests of the page Settings (step 5): the fixture of the document
/// tab (a portable memory database, the real window shown offscreen, a temporary
/// folder) with the helpers of the Settings page. Every function, which changes
/// something, processes the events afterwards (the deferred deletions too).
class AppSettingsFixture : public AppDocumentFixture
{
public:
    explicit AppSettingsFixture(QSize size = QSize(1440, 960));
    ~AppSettingsFixture();

    /// Navigates to the route (Push) in the Home tab
    void navigate(const QString& route);

    /// Opens /settings/{section}#anchor (General for an empty section). \returns
    /// the page (nullptr, when another page is shown - Page Not Found)
    pdf::PDFAppSettingsPage* openSettings(const QString& section = QString(), const QString& anchor = QString());

    /// Opens the filtered page /settings?q=
    pdf::PDFAppSettingsPage* openFilter(const QString& query);

    /// Current page of the current tab as the Settings page (nullptr = another page)
    pdf::PDFAppSettingsPage* settingsPage() const;

    /// Row of the anchor in the shown Settings page
    pdf::PDFAppSettingRow* findRow(const QString& anchor) const;

    /// Sets the value by the control of the row as the user does (a click of a
    /// check box, the arrows of a combo box, typing into a spin box or a field with
    /// Enter, the keys of a slider, a color picked in the palette). \returns false,
    /// when the control cannot show the value.
    static bool setControlValue(pdf::PDFAppSettingRow* row, const QVariant& value);

    /// Repository of the settings of the context
    pdf::PDFSettingsRepository settings() const;

    /// Writes the value by another connection (a thread of its own, as another
    /// instance of PDF4QT) and waits until the notifier of this connection has
    /// delivered the change. An invalid \p value removes the stored value.
    bool writeFromOtherConnection(const QString& key, const QVariant& value);

    /// Opens a document of \p pageCount pages (the consumers of the settings)
    pdf::PDFAppDocumentTab* openDocument(int pageCount = 3);

    /// Picture of the window (PDF4QT_APP_SCREENSHOTS=<folder>: written as <name>.png);
    /// a null picture without the variable
    QImage screenshot(const QString& name) const;
};

}   // namespace pdftest

#endif // APPSETTINGSFIXTURE_H
