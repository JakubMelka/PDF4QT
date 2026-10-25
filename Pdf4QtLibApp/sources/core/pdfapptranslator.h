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

#ifndef PDFAPPTRANSLATOR_H
#define PDFAPPTRANSLATOR_H

#include "pdfappglobal.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class QTranslator;

namespace pdf
{

/// Language of the user interface (step 5, owner decisions O12 / Q2 / Q3): the
/// setting appearance.language applied to the application at once. The translator
/// is owned by PDFAppContext, which applies the setting at the opening of the
/// storage and after every change of any instance of PDF4QT.
///
/// A language other than English loads the merged translation file
/// PDF4QT_<language>.qm (the texts of PDF4QT and of Qt) of the folder of the
/// translations (PDF4QT_TRANSLATIONS_RELATIVE_PATH next to the executable) and
/// installs it as the one QTranslator of the application. English is the language
/// of the source texts: no file is loaded. "auto" is the first language of the
/// system (QLocale::system().uiLanguages()), which has a translation file, English
/// otherwise. A missing or damaged file never blocks the application: English is
/// used and getErrorMessage says what happened (the row Language shows it).
///
/// The locale of the user interface (PDFAppLocale, owner decision O17) follows the
/// language, which is really shown: English (United States) for English and for a
/// missing translation. The widgets get QEvent::LanguageChange from the event loop
/// (QApplication posts it to the windows) also when only the locale has changed, so
/// every page, panel and dialog retranslates and reformats its texts.
///
/// GUI thread only. The language id is never taken from the database into a path
/// unchecked: only the ids of getTranslatedLanguages name a file.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppTranslator : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Largest translation file read (the merged files of today have about 1 MB)
    static constexpr qint64 MAXIMUM_FILE_SIZE = 64 * 1024 * 1024;

    explicit PDFAppTranslator(QObject* parent = nullptr);

    /// Removes the translator of the application and gives the locale of the user
    /// interface its default back, when this translator has changed them
    virtual ~PDFAppTranslator() override;

    /// Applies the value of the setting appearance.language ("auto", "en", "cs",
    /// "zh_CN", …; an unknown value is English). Nothing happens, when the shown
    /// language and the locale stay the same (a translation, which could not be
    /// loaded, is tried again - its file may have been restored).
    void setLanguage(const QString& language);

    /// Value of the setting last applied
    const QString& getLanguage() const { return m_language; }

    /// Language really shown: "" for English (also a failed translation), else the
    /// id of the translation ("cs", "zh_CN")
    const QString& getActiveLanguage() const { return m_activeLanguage; }

    /// Message of a translation, which could not be loaded (empty = no error), in
    /// the language shown now (it is translated, when it is asked for)
    QString getErrorMessage() const;

    /// The translation of the chosen language could not be loaded
    bool hasError() const { return m_error != Error::None; }

    /// Ids of the languages with a translation file (the options of
    /// appearance.language without "auto" and "en")
    static QStringList getTranslatedLanguages();

    /// Folder of the translation files: the folder of the tests, when set, else
    /// PDF4QT_TRANSLATIONS_RELATIVE_PATH below the folder of the executable
    static QString getTranslationDirectory();

    /// Tests: another folder of the translation files (empty = the default)
    static void setTranslationDirectory(const QString& directory);

    /// Path of the translation file of a language (empty for English and an unknown id)
    static QString getTranslationFile(const QString& language);

    /// Tests: the languages of the system (empty = QLocale::system().uiLanguages())
    static void setSystemLanguages(const QStringList& languages);
    static QStringList getSystemLanguages();

    /// Id of a translation of a BCP 47 name of a language of the system: "cs-CZ" →
    /// "cs", "zh-Hant-TW" / "zh-HK" → "zh_TW", "zh-CN" / "zh" → "zh_CN", "en-GB" →
    /// "en"; empty for a language without a translation
    static QString getLanguageId(const QString& name);

    /// The language of "auto": the first language of \p systemLanguages, which is
    /// English or has a translation file; English otherwise
    static QString resolveAutomaticLanguage(const QStringList& systemLanguages);

Q_SIGNALS:
    /// The language or the locale of the user interface has changed
    void languageChanged();

private:
    enum class Error
    {
        None,
        NotFound,
        Damaged
    };

    QString m_language;
    QString m_activeLanguage;
    Error m_error = Error::None;
    QString m_resolvedLanguage;
    std::unique_ptr<QTranslator> m_translator;
    bool m_isApplied = false;
};

}   // namespace pdf

#endif // PDFAPPTRANSLATOR_H
