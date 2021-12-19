#    Copyright (C) 2020-2021 Jakub Melka
#
#    This file is part of PDF4QT.
#
#    PDF4QT is free software: you can redistribute it and/or modify
#    it under the terms of the GNU Lesser General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    with the written consent of the copyright owner, any later version.
#
#    PDF4QT is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU Lesser General Public License for more details.
#
#    You should have received a copy of the GNU Lesser General Public License
#    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

CONFIG += c++11 console
CONFIG -= app_bundle

TARGET = PdfTool
VERSION = 1.1.0

QMAKE_TARGET_DESCRIPTION = "PDF tool for Qt"
QMAKE_TARGET_COPYRIGHT = "(c) Jakub Melka 2018-2021"

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

QMAKE_CXXFLAGS += /std:c++latest /utf-8

INCLUDEPATH += $$PWD/../PDF4QtLib/Sources

DESTDIR = $$OUT_PWD/..

LIBS += -L$$OUT_PWD/..

LIBS += -lPDF4QtLib

SOURCES += \
        main.cpp \
        pdfoutputformatter.cpp \
        pdftoolabstractapplication.cpp \
        pdftoolattachments.cpp \
        pdftoolaudiobook.cpp \
        pdftoolcertstore.cpp \
        pdftoolcolorprofiles.cpp \
        pdftooldecrypt.cpp \
        pdftooldiff.cpp \
        pdftoolencrypt.cpp \
        pdftoolfetchimages.cpp \
        pdftoolfetchtext.cpp \
        pdftoolinfo.cpp \
        pdftoolinfofonts.cpp \
        pdftoolinfoinks.cpp \
        pdftoolinfojavascript.cpp \
        pdftoolinfometadata.cpp \
        pdftoolinfonameddestinations.cpp \
        pdftoolinfopageboxes.cpp \
        pdftoolinfostructuretree.cpp \
        pdftoolinkcoverage.cpp \
        pdftooloptimize.cpp \
        pdftoolrender.cpp \
        pdftoolseparate.cpp \
        pdftoolstatistics.cpp \
        pdftoolunite.cpp \
        pdftoolverifysignatures.cpp \
        pdftoolxml.cpp

application.files = $$DESTDIR/PdfTool.exe
application.path = $$DESTDIR/install
application.CONFIG += no_check_exist
INSTALLS += application

HEADERS += \
    pdfoutputformatter.h \
    pdftoolabstractapplication.h \
    pdftoolattachments.h \
    pdftoolaudiobook.h \
    pdftoolcertstore.h \
    pdftoolcolorprofiles.h \
    pdftooldecrypt.h \
    pdftooldiff.h \
    pdftoolencrypt.h \
    pdftoolfetchimages.h \
    pdftoolfetchtext.h \
    pdftoolinfo.h \
    pdftoolinfofonts.h \
    pdftoolinfoinks.h \
    pdftoolinfojavascript.h \
    pdftoolinfometadata.h \
    pdftoolinfonameddestinations.h \
    pdftoolinfopageboxes.h \
    pdftoolinfostructuretree.h \
    pdftoolinkcoverage.h \
    pdftooloptimize.h \
    pdftoolrender.h \
    pdftoolseparate.h \
    pdftoolstatistics.h \
    pdftoolunite.h \
    pdftoolverifysignatures.h \
    pdftoolxml.h
