#    Copyright (C) 2018 Jakub Melka
#
#    This file is part of PdfForQt.
#
#    PdfForQt is free software: you can redistribute it and/or modify
#    it under the terms of the GNU Lesser General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    PdfForQt is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU Lesser General Public License for more details.
#
#    You should have received a copy of the GNU Lesser General Public License
#    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

QT       -= gui

TARGET = PdfForQtLib
TEMPLATE = lib

DEFINES += PDFFORQTLIB_LIBRARY

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

DESTDIR = $$OUT_PWD/..

SOURCES += \
    sources/pdfobject.cpp \
    sources/pdfparser.cpp \
    sources/pdfdocument.cpp \
    sources/pdfdocumentreader.cpp \
    sources/pdfxreftable.cpp \
    sources/pdfvisitor.cpp

HEADERS += \
    sources/pdfobject.h \
    sources/pdfparser.h \
    sources/pdfglobal.h \
    sources/pdfconstants.h \
    sources/pdfdocument.h \
    sources/pdfdocumentreader.h \
    sources/pdfxreftable.h \
    sources/pdfflatmap.h \
    sources/pdfvisitor.h

unix {
    target.path = /usr/lib
    INSTALLS += target
}


CONFIG += force_debug_info


QMAKE_CXXFLAGS += /std:c++latest
