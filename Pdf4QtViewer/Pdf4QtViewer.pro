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

QT += core gui widgets winextras printsupport texttospeech network xml

TARGET = Pdf4QtViewer
TEMPLATE = lib

win32:TARGET_EXT = .dll
VERSION = 1.0.0

DEFINES += Pdf4QtVIEWER_LIBRARY

QMAKE_TARGET_DESCRIPTION = "PDF viewer support library"
QMAKE_TARGET_COPYRIGHT = "(c) Jakub Melka 2018-2021"

DEFINES += QT_DEPRECATED_WARNINGS

QMAKE_CXXFLAGS += /std:c++latest /utf-8

INCLUDEPATH += $$PWD/../PDF4QtLib/Sources
DESTDIR = $$OUT_PWD/..
LIBS += -L$$OUT_PWD/..
LIBS += -lPDF4QtLib

SOURCES += \
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
        pdfviewermainwindowlite.cpp \
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
        pdfviewerglobal.h \
        pdfviewermainwindow.h \
        pdfviewermainwindowlite.h \
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
        pdfviewermainwindowlite.ui \
        pdfviewersettingsdialog.ui

CONFIG += force_debug_info

viewer_library.files = $$DESTDIR/Pdf4QtViewer.dll
viewer_library.path = $$DESTDIR/install
INSTALLS += viewer_library

plugins.files = $$files($$DESTDIR/pdfplugins/*.dll)
plugins.path = $$DESTDIR/install/pdfplugins
INSTALLS += plugins

RESOURCES += \
    pdf4qtviewer.qrc

DEFINES += QT_INSTALL_DIRECTORY=\"\\\"$$[QT_INSTALL_BINS]\\\"\"
