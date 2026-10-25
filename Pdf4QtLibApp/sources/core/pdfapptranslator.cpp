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

#include "pdfapptranslator.h"
#include "pdfapplocale.h"
#include "pdfsettingdefinitions.h"

#include "config.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QFileInfo>
#include <QtEndian>
#include <QLocale>
#include <QTranslator>
#include <QWidget>

namespace pdf
{

/// Settings of the tests (GUI thread only, as the translator)
class PDFAppTranslatorHelper
{
public:
    PDFAppTranslatorHelper() = delete;

    /// Most languages of the system examined by "auto"
    static constexpr qsizetype MAXIMUM_SYSTEM_LANGUAGES = 32;

    /// Longest name of a language of the system
    static constexpr qsizetype MAXIMUM_NAME_LENGTH = 64;

    static QString& getDirectoryOverride()
    {
        static QString directory;
        return directory;
    }

    static QStringList& getSystemLanguagesOverride()
    {
        static QStringList languages;
        return languages;
    }

    static bool isEnglish(const QString& language)
    {
        return language == PDFSettingValues::languageEnglish();
    }

    /// A translation of the application (merged with the translations of Qt) has no
    /// block of dependencies: QTranslator would load every file it names, a relative
    /// one from the folder, an absolute one from anywhere (also another computer).
    /// The blocks of a .qm file follow its magic of 16 bytes: a tag of one byte and
    /// the length of the block (4 bytes, big endian).
    static bool hasDependencies(const QString& filePath)
    {
        constexpr qsizetype MAGIC_LENGTH = 16;
        constexpr qsizetype BLOCK_HEADER_LENGTH = 5;
        constexpr uchar DEPENDENCIES_TAG = 0x96;

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly))
        {
            return false;
        }

        const QByteArray data = file.read(PDFAppTranslator::MAXIMUM_FILE_SIZE);
        qsizetype position = MAGIC_LENGTH;
        while (position + BLOCK_HEADER_LENGTH <= data.size())
        {
            const uchar* block = reinterpret_cast<const uchar*>(data.constData() + position);
            if (block[0] == DEPENDENCIES_TAG)
            {
                return true;
            }

            const quint32 length = qFromBigEndian<quint32>(block + 1);
            if (qint64(length) > qint64(data.size() - position - BLOCK_HEADER_LENGTH))
            {
                break;
            }
            position += BLOCK_HEADER_LENGTH + qsizetype(length);
        }
        return false;
    }

    /// Windows of the application, guarded (a LocaleChange may close one)
    static QList<QPointer<QWidget>> getTopLevelWidgets()
    {
        QList<QPointer<QWidget>> windows;
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            windows.append(widget);
        }
        return windows;
    }
};

PDFAppTranslator::PDFAppTranslator(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppTranslator::~PDFAppTranslator()
{
    if (m_translator)
    {
        QCoreApplication::removeTranslator(m_translator.get());
        m_translator.reset();
    }

    // The locale of the user interface goes back to English, which the next
    // context (tests) or the end of the process expects
    if (m_isApplied && !m_activeLanguage.isEmpty())
    {
        PDFAppLocale::setUiLanguage(QString());
    }
}

void PDFAppTranslator::setLanguage(const QString& language)
{
    // Only the ids of the options name a file; any other value is English
    QString requested = language;
    if (requested != PDFSettingValues::languageAuto() && !getTranslatedLanguages().contains(requested))
    {
        requested = PDFSettingValues::languageEnglish();
    }

    const QString resolved = requested == PDFSettingValues::languageAuto() ? resolveAutomaticLanguage(getSystemLanguages()) : requested;
    m_language = requested;
    if (m_isApplied && resolved == m_resolvedLanguage && m_error == Error::None)
    {
        // The same language (also "auto" of the same system language): nothing to do
        return;
    }

    std::unique_ptr<QTranslator> translator;
    QString activeLanguage;
    Error error = Error::None;
    if (!PDFAppTranslatorHelper::isEnglish(resolved))
    {
        const QFileInfo fileInfo(getTranslationFile(resolved));
        translator = std::make_unique<QTranslator>();
        if (!fileInfo.isFile() || fileInfo.size() > MAXIMUM_FILE_SIZE)
        {
            translator.reset();
            error = Error::NotFound;
        }
        else if (PDFAppTranslatorHelper::hasDependencies(fileInfo.absoluteFilePath()) ||
                 !translator->load(fileInfo.absoluteFilePath(), QString(), QStringLiteral(""), QStringLiteral("")) || translator->isEmpty())
        {
            translator.reset();
            error = Error::Damaged;
        }
        else
        {
            activeLanguage = resolved;
        }

        if (error != Error::None)
        {
            qWarning("Translation %s could not be loaded from %s", qPrintable(resolved), qPrintable(fileInfo.absoluteFilePath()));
        }
    }

    m_isApplied = true;
    m_resolvedLanguage = resolved;
    m_activeLanguage = activeLanguage;
    m_error = error;

    // The locale first: the texts retranslated by LanguageChange format in it (O17).
    // QLocale::setDefault does not change the locale of the existing widgets (and
    // QApplication does not do it at LocaleChange on Windows): each window, whose
    // locale is not set explicitly, takes the new default and gives it to its
    // children (QWidget::unsetLocale sends them LocaleChange).
    const QLocale previousLocale = PDFAppLocale::getUiLocale();
    PDFAppLocale::setUiLanguage(activeLanguage);
    if (PDFAppLocale::getUiLocale() != previousLocale && qobject_cast<QApplication*>(QCoreApplication::instance()))
    {
        for (const QPointer<QWidget>& window : PDFAppTranslatorHelper::getTopLevelWidgets())
        {
            if (window && !window->testAttribute(Qt::WA_SetLocale))
            {
                window->unsetLocale();
            }
        }
    }

    // The new translator before the old one is removed. QApplication posts
    // LanguageChange to its windows for each of them, the posted events of a
    // window are joined. A change of the locale is always a change of the
    // translator (only a loaded translation has another locale than English).
    std::unique_ptr<QTranslator> oldTranslator = std::move(m_translator);
    m_translator = std::move(translator);
    if (m_translator)
    {
        QCoreApplication::installTranslator(m_translator.get());
    }
    if (oldTranslator)
    {
        QCoreApplication::removeTranslator(oldTranslator.get());
        oldTranslator.reset();
    }

    Q_EMIT languageChanged();
}

QString PDFAppTranslator::getErrorMessage() const
{
    // Translated now: a message built at the change would stay in the language,
    // which has just been left
    switch (m_error)
    {
        case Error::None:
            return QString();

        case Error::NotFound:
            return tr("The translation file was not found. PDF4QT is shown in English.");

        case Error::Damaged:
            return tr("The translation file is damaged. PDF4QT is shown in English.");
    }
    return QString();
}

QStringList PDFAppTranslator::getTranslatedLanguages()
{
    return { QStringLiteral("de"), QStringLiteral("ko"), QStringLiteral("es"), QStringLiteral("cs"), QStringLiteral("zh_CN"),
             QStringLiteral("zh_TW"), QStringLiteral("fr"), QStringLiteral("tr"), QStringLiteral("ru") };
}

QString PDFAppTranslator::getTranslationDirectory()
{
    const QString& directory = PDFAppTranslatorHelper::getDirectoryOverride();
    if (!directory.isEmpty())
    {
        return directory;
    }

    QDir applicationDirectory(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(applicationDirectory.absoluteFilePath(QStringLiteral(PDF4QT_TRANSLATIONS_RELATIVE_PATH)));
}

void PDFAppTranslator::setTranslationDirectory(const QString& directory)
{
    PDFAppTranslatorHelper::getDirectoryOverride() = directory;
}

QString PDFAppTranslator::getTranslationFile(const QString& language)
{
    if (!getTranslatedLanguages().contains(language))
    {
        return QString();
    }
    return QDir(getTranslationDirectory()).absoluteFilePath(QStringLiteral("PDF4QT_%1.qm").arg(language));
}

void PDFAppTranslator::setSystemLanguages(const QStringList& languages)
{
    PDFAppTranslatorHelper::getSystemLanguagesOverride() = languages;
}

QStringList PDFAppTranslator::getSystemLanguages()
{
    const QStringList& languages = PDFAppTranslatorHelper::getSystemLanguagesOverride();
    return languages.isEmpty() ? QLocale::system().uiLanguages() : languages;
}

QString PDFAppTranslator::getLanguageId(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > PDFAppTranslatorHelper::MAXIMUM_NAME_LENGTH)
    {
        return QString();
    }

    const QLocale locale(trimmed);
    switch (locale.language())
    {
        case QLocale::C:
        case QLocale::AnyLanguage:
            return QString();

        case QLocale::English:
            return PDFSettingValues::languageEnglish();

        case QLocale::Chinese:
        {
            // Traditional Chinese by the script, or by the regions writing it
            const QString lowerName = trimmed.toLower();
            const bool isTraditional = locale.script() == QLocale::TraditionalHanScript || lowerName.contains(QStringLiteral("hant")) ||
                                       (!lowerName.contains(QStringLiteral("hans")) &&
                                        (locale.territory() == QLocale::Taiwan || locale.territory() == QLocale::HongKong || locale.territory() == QLocale::Macao));
            return isTraditional ? QStringLiteral("zh_TW") : QStringLiteral("zh_CN");
        }

        default:
            break;
    }

    const QString code = QLocale::languageToCode(locale.language(), QLocale::ISO639Part1);
    return getTranslatedLanguages().contains(code) ? code : QString();
}

QString PDFAppTranslator::resolveAutomaticLanguage(const QStringList& systemLanguages)
{
    qsizetype examined = 0;
    for (const QString& name : systemLanguages)
    {
        if (++examined > PDFAppTranslatorHelper::MAXIMUM_SYSTEM_LANGUAGES)
        {
            break;
        }

        const QString language = getLanguageId(name);
        if (PDFAppTranslatorHelper::isEnglish(language))
        {
            return language;
        }

        if (!language.isEmpty() && QFileInfo(getTranslationFile(language)).isFile())
        {
            return language;
        }
    }

    return PDFSettingValues::languageEnglish();
}

}   // namespace pdf
