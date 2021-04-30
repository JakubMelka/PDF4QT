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

QT += core gui widgets winextras

TARGET = Pdf4QtViewerProfi
TEMPLATE = app

VERSION = 1.0.0

RC_ICONS = $$PWD/app-icon.ico

QMAKE_TARGET_DESCRIPTION = "PDF viewer for Qt, Profi version"
QMAKE_TARGET_COPYRIGHT = "(c) Jakub Melka 2018-2021"

DEFINES += QT_DEPRECATED_WARNINGS
QMAKE_CXXFLAGS += /std:c++latest /utf-8

INCLUDEPATH += $$PWD/../PDF4QtLib/Sources $$PWD/../PDF4QtViewer
DESTDIR = $$OUT_PWD/..
LIBS += -L$$OUT_PWD/..
LIBS += -lPDF4QtLib -lPDF4QtViewer
CONFIG += force_debug_info

application.files = $$DESTDIR/Pdf4QtViewerProfi.exe
application.path = $$DESTDIR/install
INSTALLS += application

SOURCES += \
    main.cpp
