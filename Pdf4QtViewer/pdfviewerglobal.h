//    Copyright (C) 2020-2021 Jakub Melka
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

#ifndef PDFVIEWERGLOBAL_H
#define PDFVIEWERGLOBAL_H

#include <pdf4qtviewer_export.h>

#if !defined(PDF4QTVIEWERLIBSHARED_EXPORT)

#if defined(PDF4QTVIEWER_LIBRARY)
#  define PDF4QTVIEWERLIBSHARED_EXPORT Q_DECL_EXPORT
#else
#  define PDF4QTVIEWERLIBSHARED_EXPORT Q_DECL_IMPORT
#endif

#endif

#endif // PDFVIEWERGLOBAL_H
