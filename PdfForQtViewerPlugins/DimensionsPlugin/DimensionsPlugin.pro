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

TEMPLATE = lib
DEFINES += DIMENSIONPLUGIN_LIBRARY

QT += gui widgets

LIBS += -L$$OUT_PWD/../..

LIBS += -lPDFForQtLib

QMAKE_CXXFLAGS += /std:c++latest /utf-8

INCLUDEPATH += $$PWD/../../PDFForQtLib/Sources

DESTDIR = $$OUT_PWD/../../pdfplugins

CONFIG += c++11

SOURCES += \
    dimensionsplugin.cpp \
    dimensiontool.cpp \
    settingsdialog.cpp

HEADERS += \
    dimensionsplugin.h \
    dimensiontool.h \
    settingsdialog.h

CONFIG += force_debug_info

DISTFILES += \
    DimensionsPlugin.json

RESOURCES += \
    icons.qrc

FORMS += \
    settingsdialog.ui
