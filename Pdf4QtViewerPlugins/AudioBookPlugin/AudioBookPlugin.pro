#    Copyright (C) 2021 Jakub Melka
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

TEMPLATE = lib
DEFINES += AUDIOBOOKPLUGIN_LIBRARY

QT += gui widgets

include(../../Pdf4Qt.pri)

LIBS += -L$$OUT_PWD/../..

LIBS += -lPdf4QtLib

win32-*g++: {
    LIBS += -lole32 -lsapi
}

INCLUDEPATH += $$PWD/../../Pdf4QtLib/Sources

DESTDIR = $$OUT_PWD/../../pdfplugins

SOURCES += \
    audiobookcreator.cpp \
    audiobookplugin.cpp \
    audiotextstreameditordockwidget.cpp

HEADERS += \
    audiobookcreator.h \
    audiobookplugin.h \
    audiotextstreameditordockwidget.h

CONFIG += force_debug_info

DISTFILES += \
    AudioBookPlugin.json

RESOURCES += \
    icons.qrc

FORMS += \
    audiotextstreameditordockwidget.ui

