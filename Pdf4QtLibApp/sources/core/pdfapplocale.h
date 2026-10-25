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

#ifndef PDFAPPLOCALE_H
#define PDFAPPLOCALE_H

#include "pdfappglobal.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>
#include <QString>

namespace pdf
{

/// Locale of the user interface (owner decision O17): dates, times, file sizes and
/// numbers shown by the application are formatted in the language of the user
/// interface (the locale of the active translation), never by the locale of the
/// system - an English interface on a Czech system shows "Oct 23, 2026" and
/// "1.3 kB", not "říj 23, 2026" and "1,3 kB".
///
/// The application formats its texts explicitly by the functions of this class.
/// The locale is also the default locale of the process (QLocale::setDefault, set
/// by PDFAppContext), so "%Ln" of tr(), Qt widgets and Pdf4QtLibFluent (QLocale(),
/// QWidget::locale()) follow it. Regional settings, which are not a language (the
/// default paper size, the measurement system of the printer), come from
/// getRegionalLocale() - the system.
///
/// The locale is the locale of the shown translation (PDFAppTranslator applies the
/// setting appearance.language, O12: English by default), English (United States)
/// for English. Thread safe: the formatting functions may be called from worker
/// threads.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppLocale
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppLocale)

public:
    PDFAppLocale() = delete;

    /// Locale of the language of the user interface
    static QLocale getUiLocale();

    /// Locale of the user interface before any change: English (United States)
    static QLocale getDefaultUiLocale();

    /// Locale of a language of the setting appearance.language: "en" (and an empty
    /// or invalid name) is English (United States), "auto" the language of the
    /// system, otherwise the locale of the name ("cs", "pt_BR")
    static QLocale getLocaleForLanguage(const QString& language);

    /// Changes the locale of the user interface (tests) and makes it the default
    /// locale of the process. GUI thread.
    static void setUiLocale(const QLocale& locale);

    /// Language of the user interface: "" for English, else the id of the shown
    /// translation ("cs", "zh_CN") - the help topics, What's New and the release
    /// notes load their translation of this language (step 5). Thread safe.
    static QString getUiLanguage();

    /// Changes the language of the user interface (PDFAppTranslator; "" or "en" =
    /// English) and its locale (getLocaleForLanguage), which becomes the default
    /// locale of the process. GUI thread.
    static void setUiLanguage(const QString& language);

    /// Makes the locale of the user interface the default locale of the process
    /// (QLocale::setDefault), when it is not already. Called by PDFAppContext at the
    /// start, before the windows and the worker threads exist. GUI thread.
    static void applyDefaultLocale();

    /// Locale of the region of the system: the default paper size and the units of
    /// the printer - a regional setting, not a language
    static QLocale getRegionalLocale();

    /// "1,234"
    static QString formatNumber(qint64 value);

    /// "Oct 23, 2026"
    static QString formatDate(const QDate& date);

    /// "Oct 23" (a date of the current year)
    static QString formatShortDate(const QDate& date);

    /// "14:05"
    static QString formatClock(const QTime& time);

    /// "Oct 23, 2026, 14:05" (the date time as it is, the caller converts it to the
    /// local time); an empty text for an invalid date time
    static QString formatDateTime(const QDateTime& dateTime);

    /// "1.3 kB" (QLocale::formattedDataSize in the locale of the user interface)
    static QString formatDataSize(qint64 bytes, int precision, QLocale::DataSizeFormats format);
};

}   // namespace pdf

#endif // PDFAPPLOCALE_H
