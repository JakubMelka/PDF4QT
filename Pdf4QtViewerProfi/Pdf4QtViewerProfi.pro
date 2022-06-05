#    Copyright (C) 2018-2022 Jakub Melka
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

QT += core gui widgets winextras

TARGET = Pdf4QtViewerProfi
TEMPLATE = app

include(../Pdf4Qt.pri)

RC_ICONS = $$PWD/app-icon.ico

QMAKE_TARGET_DESCRIPTION = "PDF viewer for Qt, Profi version"
QMAKE_TARGET_COPYRIGHT = "(c) Jakub Melka 2018-2022"

DEFINES += QT_DEPRECATED_WARNINGS

win32-*g++|unix: {
    LIBS += -ltbb
}

INCLUDEPATH += $$PWD/../PDF4QtLib/Sources $$PWD/../PDF4QtViewer
DESTDIR = $$OUT_PWD/..
LIBS += -L$$OUT_PWD/..
LIBS += -lPDF4QtLib -lPDF4QtViewer
CONFIG += force_debug_info

application.files = $$DESTDIR/Pdf4QtViewerProfi.exe
application.path = $$DESTDIR/install
application.CONFIG += no_check_exist
INSTALLS += application

SOURCES += \
    main.cpp
