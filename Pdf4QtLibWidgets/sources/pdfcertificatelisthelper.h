//    Copyright (C) 2023 Jakub Melka
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

#ifndef PDFCERTIFICATELISTHELPER_H
#define PDFCERTIFICATELISTHELPER_H

#include "pdfwidgetsglobal.h"
#include "pdfcertificatestore.h"

class QComboBox;

namespace pdf
{

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCertificateListHelper
{
public:
    PDFCertificateListHelper() = delete;

    static void initComboBox(QComboBox* comboBox);
    static void fillComboBox(QComboBox* comboBox, const PDFCertificateEntries& entries);

private:
    static constexpr int COLUMN_COUNT = 4;
};

}   // namespace pdf

#endif // PDFCERTIFICATELISTHELPER_H
