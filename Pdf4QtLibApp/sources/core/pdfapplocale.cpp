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
#include <QTimeZone>

#include <atomic>
#include <map>
#include <vector>

namespace pdf
{

/// Storage of the locale of the user interface (read also by worker threads)
class PDFAppLocaleHelper
{
public:
    PDFAppLocaleHelper() = delete;

    /// A minute and an hour in milliseconds (times of recent activity)
    static constexpr qint64 MINUTE = 60 * 1000;
    static constexpr qint64 HOUR = 60 * MINUTE;

    /// Fixed current time of the tests (0 = the clock)
    static std::atomic<qint64>& getTimeOverride()
    {
        static std::atomic<qint64> time = 0;
        return time;
    }

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

    /// The language ("" = English); access only under getMutex()
    static QString& getLanguage()
    {
        static QString language;
        return language;
    }

    /// Most default locales kept alive by retainDefaultLocale (a language is changed
    /// by the user, not in a loop; the locales of the languages are cached anyway)
    static constexpr size_t MAXIMUM_RETAINED_LOCALES = 256;

    /// Default locales of the process, which have been replaced (GUI thread).
    /// QLocale::setDefault is not thread safe: a worker thread constructing QLocale()
    /// (tr() of "%Ln", the profile descriptions of the color management, Qt itself)
    /// at the moment of a change of the language copies the private data of the
    /// previous default. The data are kept alive here, so such a copy never reads
    /// freed memory (QLocale shares its data, each copy here holds a reference).
    static void retainDefaultLocale(const QLocale& locale)
    {
        static std::vector<QLocale> retainedLocales;
        if (retainedLocales.size() < MAXIMUM_RETAINED_LOCALES)
        {
            retainedLocales.push_back(locale);
        }
    }

    /// Locale of a language of the user interface, created once per language (GUI
    /// thread): the data of a locale, which has been the default, stay alive
    static QLocale getCachedLocale(const QString& language)
    {
        static std::map<QString, QLocale> locales;
        auto it = locales.find(language);
        if (it == locales.end())
        {
            it = locales.emplace(language, PDFAppLocale::getLocaleForLanguage(language)).first;
        }
        return it->second;
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
        if (locale == getDefaultUiLocale())
        {
            PDFAppLocaleHelper::getLanguage().clear();
        }
    }
    applyDefaultLocale();
}

QString PDFAppLocale::getUiLanguage()
{
    QMutexLocker locker(&PDFAppLocaleHelper::getMutex());
    return PDFAppLocaleHelper::getLanguage();
}

void PDFAppLocale::setUiLanguage(const QString& language)
{
    const QString name = language.compare(QLatin1String("en"), Qt::CaseInsensitive) == 0 || language.size() > 32 ? QString() : language;
    const QLocale locale = PDFAppLocaleHelper::getCachedLocale(name);
    {
        QMutexLocker locker(&PDFAppLocaleHelper::getMutex());
        PDFAppLocaleHelper::getLocale() = locale;
        PDFAppLocaleHelper::getLanguage() = locale == getDefaultUiLocale() ? QString() : name;
    }
    applyDefaultLocale();
}

void PDFAppLocale::applyDefaultLocale()
{
    // QLocale::setDefault is not reentrant: it is called only when the default differs
    const QLocale locale = getUiLocale();
    const QLocale previous;
    if (previous != locale)
    {
        PDFAppLocaleHelper::retainDefaultLocale(previous);
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

QString PDFAppLocale::formatRecentTime(const QDateTime& time, const QDateTime& now, RecentTimeUse use)
{
    if (!time.isValid() || !now.isValid())
    {
        return QString();
    }

    // The elapsed time is the difference of the instants (a change of the daylight
    // saving time does not change it); a future time less than a minute ahead is a
    // difference of the clocks of two instances
    const qint64 elapsed = now.toMSecsSinceEpoch() - time.toMSecsSinceEpoch();
    if (elapsed > -PDFAppLocaleHelper::MINUTE && elapsed < PDFAppLocaleHelper::MINUTE)
    {
        if (use == RecentTimeUse::InSentence)
        {
            return tr("just now", "Time of recent activity less than a minute ago inside of a sentence: Opened just now");
        }
        return tr("Just now", "Time of recent activity less than a minute ago");
    }
    if (elapsed > 0 && elapsed < PDFAppLocaleHelper::HOUR)
    {
        const int minutes = int(elapsed / PDFAppLocaleHelper::MINUTE);
        return minutes == 1 ? tr("%Ln minute ago", "Time of recent activity", minutes) : tr("%Ln minutes ago", "Time of recent activity", minutes);
    }

    // The calendar days of the time zone of the current time
    const QDateTime local = time.toTimeZone(now.timeZone());
    const QDate date = local.date();
    const QDate today = now.date();
    const QString clock = formatClock(local.time());
    if (date == today)
    {
        return tr("Today %1", "Time of recent activity: Today 15:56").arg(clock);
    }
    if (date == today.addDays(-1))
    {
        return tr("Yesterday %1", "Time of recent activity: Yesterday 16:20").arg(clock);
    }
    return formatDate(date);
}

qint64 PDFAppLocale::getCurrentTime()
{
    const qint64 time = PDFAppLocaleHelper::getTimeOverride().load();
    return time > 0 ? time : QDateTime::currentMSecsSinceEpoch();
}

void PDFAppLocale::setCurrentTimeOverride(qint64 time)
{
    PDFAppLocaleHelper::getTimeOverride().store(qMax(qint64(0), time));
}

}   // namespace pdf
