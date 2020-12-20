#    Copyright (C) 2018-2020 Jakub Melka
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

QT += core gui widgets winextras printsupport texttospeech network xml

TARGET = PdfForQtViewer
TEMPLATE = app

VERSION = 1.0.0

RC_ICONS = $$PWD/resources/app-icon.ico

QMAKE_TARGET_DESCRIPTION = "PDF viewer for Qt"
QMAKE_TARGET_COPYRIGHT = "(c) Jakub Melka 2018-2020"


DEFINES += QT_DEPRECATED_WARNINGS

QMAKE_CXXFLAGS += /std:c++latest /utf-8

INCLUDEPATH += $$PWD/../PDFForQtLib/Sources

DESTDIR = $$OUT_PWD/..

LIBS += -L$$OUT_PWD/..

LIBS += -lPDFForQtLib

SOURCES += \
        main.cpp \
        pdfaboutdialog.cpp \
        pdfadvancedfindwidget.cpp \
        pdfdocumentpropertiesdialog.cpp \
        pdfoptimizedocumentdialog.cpp \
        pdfprogramcontroller.cpp \
        pdfrecentfilemanager.cpp \
        pdfrendertoimagesdialog.cpp \
        pdfsendmail.cpp \
        pdfsidebarwidget.cpp \
        pdftexttospeech.cpp \
        pdfundoredomanager.cpp \
        pdfviewermainwindow.cpp \
        pdfviewersettings.cpp \
        pdfviewersettingsdialog.cpp

HEADERS += \
        pdfaboutdialog.h \
        pdfadvancedfindwidget.h \
        pdfdocumentpropertiesdialog.h \
        pdfoptimizedocumentdialog.h \
        pdfprogramcontroller.h \
        pdfrecentfilemanager.h \
        pdfrendertoimagesdialog.h \
        pdfsendmail.h \
        pdfsidebarwidget.h \
        pdftexttospeech.h \
        pdfundoredomanager.h \
        pdfviewermainwindow.h \
        pdfviewersettings.h \
        pdfviewersettingsdialog.h

FORMS += \
        pdfaboutdialog.ui \
        pdfadvancedfindwidget.ui \
        pdfdocumentpropertiesdialog.ui \
        pdfoptimizedocumentdialog.ui \
        pdfrendertoimagesdialog.ui \
        pdfsidebarwidget.ui \
        pdfviewermainwindow.ui \
        pdfviewersettingsdialog.ui

CONFIG += force_debug_info

application.files = $$DESTDIR/PdfForQtViewer.exe
application.path = $$DESTDIR/install
INSTALLS += application

plugins.files = $$files($$DESTDIR/pdfplugins/*.dll)
plugins.path = $$DESTDIR/install/pdfplugins
INSTALLS += plugins

RESOURCES += \
    pdfforqtviewer.qrc

