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

QT += core gui widgets winextras printsupport texttospeech network xml

TARGET = Pdf4QtViewer
TEMPLATE = lib

include(../Pdf4Qt.pri)

DEFINES += PDF4QTVIEWER_LIBRARY

QMAKE_TARGET_DESCRIPTION = "PDF viewer support library"
QMAKE_TARGET_COPYRIGHT = "(c) Jakub Melka 2018-2022"

DEFINES += QT_DEPRECATED_WARNINGS

win32-*g++|unix: {
    LIBS += -ltbb
}

INCLUDEPATH += $$PWD/../PDF4QtLib/Sources
DESTDIR = $$OUT_PWD/..
LIBS += -L$$OUT_PWD/..
LIBS += -lPDF4QtLib

SOURCES += \
        pdfaboutdialog.cpp \
        pdfadvancedfindwidget.cpp \
        pdfdocumentpropertiesdialog.cpp \
        pdfencryptionsettingsdialog.cpp \
        pdfencryptionstrengthhintwidget.cpp \
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
        pdfencryptionsettingsdialog.h \
        pdfencryptionstrengthhintwidget.h \
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
        pdfencryptionsettingsdialog.ui \
        pdfoptimizedocumentdialog.ui \
        pdfrendertoimagesdialog.ui \
        pdfsidebarwidget.ui \
        pdfviewermainwindow.ui \
        pdfviewermainwindowlite.ui \
        pdfviewersettingsdialog.ui

CONFIG += force_debug_info

viewer_library.files = $$DESTDIR/Pdf4QtViewer.dll
viewer_library.path = $$DESTDIR/install
viewer_library.CONFIG += no_check_exist
INSTALLS += viewer_library

plugins.files = $$DESTDIR/pdfplugins/ObjectInspectorPlugin.dll \
                $$DESTDIR/pdfplugins/OutputPreviewPlugin.dll \
                $$DESTDIR/pdfplugins/DimensionsPlugin.dll \
                $$DESTDIR/pdfplugins/SoftProofingPlugin.dll \
                $$DESTDIR/pdfplugins/RedactPlugin.dll \
                $$DESTDIR/pdfplugins/AudioBookPlugin.dll \
                $$DESTDIR/pdfplugins/SignaturePlugin.dll

plugins.path = $$DESTDIR/install/pdfplugins
plugins.CONFIG += no_check_exist
INSTALLS += plugins

RESOURCES += \
    pdf4qtviewer.qrc

DEFINES += QT_INSTALL_DIRECTORY=\"\\\"$$[QT_INSTALL_BINS]\\\"\"
