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

#ifndef APPLOCALEGUARD_H
#define APPLOCALEGUARD_H

#include "pdfapplocale.h"

#include <QLocale>

namespace pdftest
{

/// Simulates a locale of the system for the time of a test (owner decision O17):
/// the default locale of the process is the locale of the system, until the
/// application sets it (Windows does not let a test change the locale of the user).
/// The destructor restores the default locale and the locale of the user
/// interface (also after a failed QCOMPARE).
class AppSystemLocaleGuard
{
public:
    explicit AppSystemLocaleGuard(const QLocale& systemLocale)
    {
        QLocale::setDefault(systemLocale);
    }

    ~AppSystemLocaleGuard()
    {
        pdf::PDFAppLocale::setUiLocale(pdf::PDFAppLocale::getDefaultUiLocale());
        QLocale::setDefault(m_previous);
    }

    AppSystemLocaleGuard(const AppSystemLocaleGuard&) = delete;
    AppSystemLocaleGuard& operator=(const AppSystemLocaleGuard&) = delete;

    /// A Czech system
    static QLocale czech() { return QLocale(QLocale::Czech, QLocale::Czechia); }

private:
    QLocale m_previous;
};

}   // namespace pdftest

#endif // APPLOCALEGUARD_H
