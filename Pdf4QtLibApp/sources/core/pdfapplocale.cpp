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

#include "pdfapplocale.h"

#include <QMutex>
#include <QMutexLocker>

namespace pdf
{

/// Storage of the locale of the user interface (read also by worker threads)
class PDFAppLocaleHelper
{
public:
    PDFAppLocaleHelper() = delete;

    static QMutex& getMutex()
    {
        static QMutex mutex;
        return mutex;
    }

    /// The locale; access only under getMutex()
    static QLocale& getLocale()
    {
        static QLocale locale = PDFAppLocale::getDefaultUiLocale();
        return locale;
    }
};

QLocale PDFAppLocale::getUiLocale()
{
    QMutexLocker locker(&PDFAppLocaleHelper::getMutex());
    return PDFAppLocaleHelper::getLocale();
}

QLocale PDFAppLocale::getDefaultUiLocale()
{
    return QLocale(QLocale::English, QLocale::UnitedStates);
}

QLocale PDFAppLocale::getLocaleForLanguage(const QString& language)
{
    const QString name = language.trimmed();
    if (name.compare(QLatin1String("auto"), Qt::CaseInsensitive) == 0)
    {
        return QLocale::system();
    }

    if (name.isEmpty() || name.size() > 32 || name.compare(QLatin1String("en"), Qt::CaseInsensitive) == 0)
    {
        return getDefaultUiLocale();
    }

    // An unknown name gives the C locale: English is the language of the interface then
    const QLocale locale(name);
    return locale.language() == QLocale::C ? getDefaultUiLocale() : locale;
}

void PDFAppLocale::setUiLocale(const QLocale& locale)
{
    {
        QMutexLocker locker(&PDFAppLocaleHelper::getMutex());
        PDFAppLocaleHelper::getLocale() = locale;
    }
    applyDefaultLocale();
}

void PDFAppLocale::applyDefaultLocale()
{
    // QLocale::setDefault is not reentrant: it is called only when the default differs
    const QLocale locale = getUiLocale();
    if (QLocale() != locale)
    {
        QLocale::setDefault(locale);
    }
}

QLocale PDFAppLocale::getRegionalLocale()
{
    return QLocale::system();
}

QString PDFAppLocale::formatNumber(qint64 value)
{
    return getUiLocale().toString(value);
}

QString PDFAppLocale::formatDate(const QDate& date)
{
    return getUiLocale().toString(date, tr("MMM d, yyyy", "Format of a date of the user interface (QLocale::toString): Oct 23, 2026"));
}

QString PDFAppLocale::formatShortDate(const QDate& date)
{
    return getUiLocale().toString(date, tr("MMM d", "Format of a date of the current year (QLocale::toString): Oct 23"));
}

QString PDFAppLocale::formatClock(const QTime& time)
{
    return getUiLocale().toString(time, tr("HH:mm", "Format of a time of the user interface (QLocale::toString): 14:05"));
}

QString PDFAppLocale::formatDateTime(const QDateTime& dateTime)
{
    if (!dateTime.isValid())
    {
        return QString();
    }
    return tr("%1, %2", "Date and time: Oct 23, 2026, 14:05").arg(formatDate(dateTime.date()), formatClock(dateTime.time()));
}

QString PDFAppLocale::formatDataSize(qint64 bytes, int precision, QLocale::DataSizeFormats format)
{
    return getUiLocale().formattedDataSize(qMax(qint64(0), bytes), qBound(0, precision, 3), format);
}

}   // namespace pdf
