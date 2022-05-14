#    Copyright (C) 2022 Jakub Melka
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
DEFINES += SIGNATUREPLUGIN_LIBRARY

QT += gui widgets

LIBS += -L$$OUT_PWD/../..

LIBS += -lPdf4QtLib

QMAKE_CXXFLAGS += /std:c++latest /utf-8

INCLUDEPATH += $$PWD/../../Pdf4QtLib/Sources

DESTDIR = $$OUT_PWD/../../pdfplugins

CONFIG += c++11

Pdf4Qt_OPENSSL_PATH = $$absolute_path(../../Tools, $$[QT_INSTALL_PREFIX])

# Link OpenSSL
LIBS += -L$$Pdf4Qt_OPENSSL_PATH/OpenSSL/Win_x64/bin -L$$Pdf4Qt_OPENSSL_PATH/OpenSSL/Win_x64/lib -llibcrypto -llibssl
INCLUDEPATH += $$Pdf4Qt_OPENSSL_PATH/OpenSSL/Win_x64/include
DEPENDPATH += $$Pdf4Qt_OPENSSL_PATH/OpenSSL/Win_x64/include

SOURCES += \
    certificatemanager.cpp \
    certificatemanagerdialog.cpp \
    createcertificatedialog.cpp \
    signatureplugin.cpp \
    signdialog.cpp
	
HEADERS += \
    certificatemanager.h \
    certificatemanagerdialog.h \
    createcertificatedialog.h \
    signatureplugin.h \
    signdialog.h

CONFIG += force_debug_info

DISTFILES += \
    SignaturePlugin.json

RESOURCES += \
    icons.qrc

FORMS += \
    certificatemanagerdialog.ui \
    createcertificatedialog.ui \
    signdialog.ui

