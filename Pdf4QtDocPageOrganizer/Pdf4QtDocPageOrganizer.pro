#    Copyright (C) 2021 Jakub Melka
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

QT += core gui widgets winextras

TARGET = Pdf4QtDocPageOrganizer
TEMPLATE = app

VERSION = 1.0.0

RC_ICONS = $$PWD/app-icon.ico

QMAKE_TARGET_DESCRIPTION = "PDF Document Page Organizer"
QMAKE_TARGET_COPYRIGHT = "(c) Jakub Melka 2018-2021"

DEFINES += QT_DEPRECATED_WARNINGS
QMAKE_CXXFLAGS += /std:c++latest /utf-8

INCLUDEPATH += $$PWD/../PDF4QtLib/Sources
DESTDIR = $$OUT_PWD/..
LIBS += -L$$OUT_PWD/..
LIBS += -lPDF4QtLib
CONFIG += force_debug_info no_check_exist

application.files = $$DESTDIR/Pdf4QtDocPageOrganizer.exe
application.path = $$DESTDIR/install
application.CONFIG += no_check_exist
INSTALLS += application

SOURCES += \
    aboutdialog.cpp \
    assembleoutputsettingsdialog.cpp \
    main.cpp \
    mainwindow.cpp \
    pageitemdelegate.cpp \
    pageitemmodel.cpp \
    selectbookmarkstoregroupdialog.cpp

FORMS += \
    aboutdialog.ui \
    assembleoutputsettingsdialog.ui \
    mainwindow.ui \
    selectbookmarkstoregroupdialog.ui

HEADERS += \
    aboutdialog.h \
    assembleoutputsettingsdialog.h \
    mainwindow.h \
    pageitemdelegate.h \
    pageitemmodel.h \
    selectbookmarkstoregroupdialog.h

RESOURCES += \
    resources.qrc
