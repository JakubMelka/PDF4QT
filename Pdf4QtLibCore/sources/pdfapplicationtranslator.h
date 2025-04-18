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

#ifndef PDFAPPLICATIONTRANSLATOR_H
#define PDFAPPLICATIONTRANSLATOR_H

#include "pdfglobal.h"

#include <QTranslator>

namespace pdf
{

class PDF4QTLIBCORESHARED_EXPORT PDFApplicationTranslator
{
    Q_GADGET

public:
    explicit PDFApplicationTranslator();
    ~PDFApplicationTranslator();

    enum ELanguage
    {
        E_LANGUAGE_AUTOMATIC_SELECTION,
        E_LANGUAGE_ENGLISH,
        E_LANGUAGE_CZECH,
        E_LANGUAGE_GERMAN,
        E_LANGUAGE_KOREAN,
        E_LANGUAGE_SPANISH
    };

    Q_ENUM(ELanguage)

    void installTranslator();
    void uninstallTranslator();
    void loadSettings();
    void saveSettings();

    ELanguage getLanguage() const;
    void setLanguage(ELanguage newLanguage);

private:
    QString loadLanguageKeyFromSettings();
    QString getLanguageFileName() const;

    QTranslator* m_translator = nullptr;
    ELanguage m_language = E_LANGUAGE_AUTOMATIC_SELECTION;
};

}   // namespace

#endif // PDFAPPLICATIONTRANSLATOR_H
