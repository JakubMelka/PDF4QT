#    Copyright (C) 2020 Jakub Melka
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

CONFIG += c++11 console
CONFIG -= app_bundle

TARGET = PdfTool
VERSION = 1.0.0

QMAKE_TARGET_DESCRIPTION = "PDF tool for Qt"
QMAKE_TARGET_COPYRIGHT = "(c) Jakub Melka 2018-2020"

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

QMAKE_CXXFLAGS += /std:c++latest /utf-8

INCLUDEPATH += $$PWD/../PDFForQtLib/Sources

DESTDIR = $$OUT_PWD/..

LIBS += -L$$OUT_PWD/..

LIBS += -lPDFForQtLib

SOURCES += \
        main.cpp \
        pdfoutputformatter.cpp \
        pdftoolabstractapplication.cpp \
        pdftoolattachments.cpp \
        pdftoolaudiobook.cpp \
        pdftoolfetchtext.cpp \
        pdftoolinfo.cpp \
        pdftoolinfofonts.cpp \
        pdftoolinfojavascript.cpp \
        pdftoolinfometadata.cpp \
        pdftoolinfonameddestinations.cpp \
        pdftoolinfopageboxes.cpp \
        pdftoolinfostructuretree.cpp \
        pdftoolverifysignatures.cpp \
        pdftoolxml.cpp

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

application.files = $$DESTDIR/PdfTool.exe
application.path = $$DESTDIR/install
INSTALLS += application

HEADERS += \
    pdfoutputformatter.h \
    pdftoolabstractapplication.h \
    pdftoolattachments.h \
    pdftoolaudiobook.h \
    pdftoolfetchtext.h \
    pdftoolinfo.h \
    pdftoolinfofonts.h \
    pdftoolinfojavascript.h \
    pdftoolinfometadata.h \
    pdftoolinfonameddestinations.h \
    pdftoolinfopageboxes.h \
    pdftoolinfostructuretree.h \
    pdftoolverifysignatures.h \
    pdftoolxml.h
