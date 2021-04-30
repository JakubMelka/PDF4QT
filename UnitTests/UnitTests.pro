#    Copyright (C) 2018-2021 Jakub Melka
#
#    This file is part of Pdf4Qt.
#
#    Pdf4Qt is free software: you can redistribute it and/or modify
#    it under the terms of the GNU Lesser General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    with the written consent of the copyright owner, any later version.
#
#    Pdf4Qt is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU Lesser General Public License for more details.
#
#    You should have received a copy of the GNU Lesser General Public License
#    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.


QT += testlib

CONFIG += qt console warn_on depend_includepath testcase
CONFIG -= app_bundle

TEMPLATE = app

INCLUDEPATH += $$PWD/../Pdf4QtLib/Sources

DESTDIR = $$OUT_PWD/..

LIBS += -L$$OUT_PWD/..

LIBS += -lPdf4QtLib

QMAKE_CXXFLAGS += /std:c++latest

SOURCES += \ 
    tst_lexicalanalyzertest.cpp

target.path = $$DESTDIR/install
INSTALLS += target
