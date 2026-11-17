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

#ifndef TESTLOCALEGUARD_H
#define TESTLOCALEGUARD_H

// The default locale of a test, which is not the C locale (the fix wave of step 7 of phase 2: the facts and the
// data of the review tools of the core are machine-readable, in the C locale; only the GUI formats by
// PDFAppLocale). Arabic of Egypt writes its numbers by the Arabic-Indic digits (٠١٢…, the decimal separator ٫),
// so every number formatted by QLocale() differs from the C locale - also a count below 1000 and an integer.

#include <QLocale>

namespace pdftest
{

/// Sets the default locale to Arabic of Egypt for its lifetime and restores the previous one
class TestLocaleGuard
{
public:
    TestLocaleGuard() :
        m_previous(QLocale())
    {
        QLocale::setDefault(QLocale(QLocale::Arabic, QLocale::Egypt));
    }

    ~TestLocaleGuard()
    {
        QLocale::setDefault(m_previous);
    }

    TestLocaleGuard(const TestLocaleGuard&) = delete;
    TestLocaleGuard& operator=(const TestLocaleGuard&) = delete;

    /// True, when the default locale writes 5 otherwise than the C locale (the precondition of a test)
    static bool isDefaultLocaleOtherThanC() { return QLocale().toString(5) != QLocale::c().toString(5); }

private:
    QLocale m_previous;
};

}   // namespace pdftest

#endif // TESTLOCALEGUARD_H
