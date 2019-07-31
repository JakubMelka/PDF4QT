#    Copyright (C) 2018-2019 Jakub Melka
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


QT += testlib
QT -= gui

CONFIG += qt console warn_on depend_includepath testcase
CONFIG -= app_bundle

TEMPLATE = app

INCLUDEPATH += $$PWD/../PDFForQtLib/Sources

DESTDIR = $$OUT_PWD/..

LIBS += -L$$OUT_PWD/..

LIBS += -lPDFForQtLib

QMAKE_CXXFLAGS += /std:c++latest

SOURCES += \ 
    tst_lexicalanalyzertest.cpp

target.path = $$DESTDIR/install
INSTALLS += target
