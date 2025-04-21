//    Copyright (C) 2025-2025 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfapplicationtranslator.h"

#include <QDir>
#include <QSettings>
#include <QMetaEnum>
#include <QCoreApplication>

namespace pdf
{

PDFApplicationTranslator::PDFApplicationTranslator()
{

}

PDFApplicationTranslator::~PDFApplicationTranslator()
{
    uninstallTranslator();
}

PDFApplicationTranslator::ELanguage PDFApplicationTranslator::getLanguage() const
{
    return m_language;
}

void PDFApplicationTranslator::installTranslator()
{
    QDir applicationDirectory(QCoreApplication::applicationDirPath());
    applicationDirectory.cd("translations");
    QString translationPath = applicationDirectory.absolutePath();

    Q_ASSERT(!m_translator);
    m_translator = new QTranslator();

    switch (m_language)
    {
        case E_LANGUAGE_AUTOMATIC_SELECTION:
        {
            if (m_translator->load(QLocale::system(), "PDF4QT", "_", translationPath))
            {
                QCoreApplication::installTranslator(m_translator);
            }
            else
            {
                delete m_translator;
                m_translator = nullptr;
            }
            break;
        }

        case E_LANGUAGE_ENGLISH:
        case E_LANGUAGE_CZECH:
        case E_LANGUAGE_GERMAN:
        case E_LANGUAGE_KOREAN:
        case E_LANGUAGE_SPANISH:
        {
            QString languageFileName = getLanguageFileName();

            if (m_translator->load(languageFileName, translationPath))
            {
                QCoreApplication::installTranslator(m_translator);
            }
            else
            {
                delete m_translator;
                m_translator = nullptr;
            }
            break;
        }

        default:
        {
            delete m_translator;
            m_translator = nullptr;

            Q_ASSERT(false);
            break;
        }
    }
}

void PDFApplicationTranslator::uninstallTranslator()
{
    if (m_translator)
    {
        QCoreApplication::removeTranslator(m_translator);

        delete m_translator;
        m_translator = nullptr;
    }
}

void PDFApplicationTranslator::loadSettings()
{
    QMetaEnum metaEnum = QMetaEnum::fromType<ELanguage>();
    std::string languageKeyString = loadLanguageKeyFromSettings().toStdString();
    std::optional<quint64> value = metaEnum.keyToValue(languageKeyString.c_str());
    m_language = static_cast<ELanguage>(value.value_or(E_LANGUAGE_AUTOMATIC_SELECTION));
}

void PDFApplicationTranslator::saveSettings()
{
    QMetaEnum metaEnum = QMetaEnum::fromType<ELanguage>();
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("Language");
    settings.setValue("language", metaEnum.valueToKey(m_language));
    settings.endGroup();
}

void PDFApplicationTranslator::setLanguage(ELanguage newLanguage)
{
    m_language = newLanguage;
}

void PDFApplicationTranslator::saveSettings(QSettings& settings, ELanguage language)
{
    QMetaEnum metaEnum = QMetaEnum::fromType<ELanguage>();

    settings.beginGroup("Language");
    settings.setValue("language", metaEnum.valueToKey(language));
    settings.endGroup();
}

PDFApplicationTranslator::ELanguage PDFApplicationTranslator::loadSettings(QSettings& settings)
{
    QMetaEnum metaEnum = QMetaEnum::fromType<ELanguage>();
    std::string languageKeyString = loadLanguageKeyFromSettings(settings).toStdString();
    std::optional<quint64> value = metaEnum.keyToValue(languageKeyString.c_str());
    return static_cast<ELanguage>(value.value_or(E_LANGUAGE_AUTOMATIC_SELECTION));
}

QString PDFApplicationTranslator::loadLanguageKeyFromSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    return loadLanguageKeyFromSettings(settings);
}

QString PDFApplicationTranslator::loadLanguageKeyFromSettings(QSettings& settings)
{
    QMetaEnum metaEnum = QMetaEnum::fromType<ELanguage>();

    settings.beginGroup("Language");
    QString languageKey = settings.value("language", metaEnum.valueToKey(E_LANGUAGE_AUTOMATIC_SELECTION)).toString();
    settings.endGroup();

    return languageKey;
}

QString PDFApplicationTranslator::getLanguageFileName() const
{
    switch (m_language)
    {
        case E_LANGUAGE_ENGLISH:
            return QLatin1String("PDF4QT_en.qm");
        case E_LANGUAGE_CZECH:
            return QLatin1String("PDF4QT_cs.qm");
        case E_LANGUAGE_GERMAN:
            return QLatin1String("PDF4QT_de.qm");
        case E_LANGUAGE_KOREAN:
            return QLatin1String("PDF4QT_es.qm");
        case E_LANGUAGE_SPANISH:
            return QLatin1String("PDF4QT_ko.qm");
            break;

        default:
            Q_ASSERT(false);
    }

    return QString();
}

}   // namespace
