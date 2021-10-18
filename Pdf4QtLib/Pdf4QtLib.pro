#    Copyright (C) 2018-2021 Jakub Melka
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

QT       += gui widgets

TARGET = Pdf4QtLib
TEMPLATE = lib

win32:TARGET_EXT = .dll
VERSION = 1.0.0

QMAKE_TARGET_DESCRIPTION = "PDF rendering / editing library for Qt"
QMAKE_TARGET_COPYRIGHT = "(c) Jakub Melka 2018-2021"

DEFINES += PDF4QTLIB_LIBRARY

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

DESTDIR = $$OUT_PWD/..

SOURCES += \
    sources/pdfaction.cpp \
    sources/pdfadvancedtools.cpp \
    sources/pdfalgorithmlcs.cpp \
    sources/pdfannotation.cpp \
    sources/pdfblendfunction.cpp \
    sources/pdfccittfaxdecoder.cpp \
    sources/pdfcms.cpp \
    sources/pdfcompiler.cpp \
    sources/pdfdiff.cpp \
    sources/pdfdocumentbuilder.cpp \
    sources/pdfdocumentmanipulator.cpp \
    sources/pdfdocumenttextflow.cpp \
    sources/pdfdocumenttextfloweditormodel.cpp \
    sources/pdfdocumentwriter.cpp \
    sources/pdfexecutionpolicy.cpp \
    sources/pdffile.cpp \
    sources/pdfform.cpp \
    sources/pdficontheme.cpp \
    sources/pdfitemmodels.cpp \
    sources/pdfjavascriptscanner.cpp \
    sources/pdfjbig2decoder.cpp \
    sources/pdfmultimedia.cpp \
    sources/pdfobject.cpp \
    sources/pdfobjecteditormodel.cpp \
    sources/pdfobjecteditorwidget.cpp \
    sources/pdfobjectutils.cpp \
    sources/pdfoptimizer.cpp \
    sources/pdfoptionalcontent.cpp \
    sources/pdfoutline.cpp \
    sources/pdfpagenavigation.cpp \
    sources/pdfpagetransition.cpp \
    sources/pdfpainterutils.cpp \
    sources/pdfparser.cpp \
    sources/pdfdocument.cpp \
    sources/pdfdocumentreader.cpp \
    sources/pdfpattern.cpp \
    sources/pdfplugin.cpp \
    sources/pdfprogress.cpp \
    sources/pdfredact.cpp \
    sources/pdfsecurityhandler.cpp \
    sources/pdfselectpagesdialog.cpp \
    sources/pdfsignaturehandler.cpp \
    sources/pdfsnapper.cpp \
    sources/pdfstructuretree.cpp \
    sources/pdftextlayout.cpp \
    sources/pdftransparencyrenderer.cpp \
    sources/pdfutils.cpp \
    sources/pdfwidgettool.cpp \
    sources/pdfwidgetutils.cpp \
    sources/pdfxreftable.cpp \
    sources/pdfvisitor.cpp \
    sources/pdfencoding.cpp \
    sources/pdfcatalog.cpp \
    sources/pdfpage.cpp \
    sources/pdfstreamfilters.cpp \
    sources/pdfdrawspacecontroller.cpp \
    sources/pdfdrawwidget.cpp \
    sources/pdfcolorspaces.cpp \
    sources/pdfrenderer.cpp \
    sources/pdfpagecontentprocessor.cpp \
    sources/pdfpainter.cpp \
    sources/pdfrenderingerrorswidget.cpp \
    sources/pdffunction.cpp \
    sources/pdfnametounicode.cpp \
    sources/pdffont.cpp \
    sources/pdfimage.cpp

HEADERS += \
    sources/pdfaction.h \
    sources/pdfadvancedtools.h \
    sources/pdfalgorithmlcs.h \
    sources/pdfannotation.h \
    sources/pdfblendfunction.h \
    sources/pdfccittfaxdecoder.h \
    sources/pdfcms.h \
    sources/pdfcompiler.h \
    sources/pdfdiff.h \
    sources/pdfdocumentbuilder.h \
    sources/pdfdocumentdrawinterface.h \
    sources/pdfdocumentmanipulator.h \
    sources/pdfdocumenttextflow.h \
    sources/pdfdocumenttextfloweditormodel.h \
    sources/pdfdocumentwriter.h \
    sources/pdfexecutionpolicy.h \
    sources/pdffile.h \
    sources/pdfform.h \
    sources/pdficontheme.h \
    sources/pdfitemmodels.h \
    sources/pdfjavascriptscanner.h \
    sources/pdfjbig2decoder.h \
    sources/pdfmeshqualitysettings.h \
    sources/pdfmultimedia.h \
    sources/pdfnametreeloader.h \
    sources/pdfobject.h \
    sources/pdfobjecteditormodel.h \
    sources/pdfobjecteditorwidget.h \
    sources/pdfobjecteditorwidget_impl.h \
    sources/pdfobjectutils.h \
    sources/pdfoptimizer.h \
    sources/pdfoptionalcontent.h \
    sources/pdfoutline.h \
    sources/pdfpagenavigation.h \
    sources/pdfpagetransition.h \
    sources/pdfpainterutils.h \
    sources/pdfparser.h \
    sources/pdfglobal.h \
    sources/pdfconstants.h \
    sources/pdfdocument.h \
    sources/pdfdocumentreader.h \
    sources/pdfpattern.h \
    sources/pdfplugin.h \
    sources/pdfprogress.h \
    sources/pdfredact.h \
    sources/pdfsecurityhandler.h \
    sources/pdfselectpagesdialog.h \
    sources/pdfsignaturehandler.h \
    sources/pdfsignaturehandler_impl.h \
    sources/pdfsnapper.h \
    sources/pdfstructuretree.h \
    sources/pdftextlayout.h \
    sources/pdftransparencyrenderer.h \
    sources/pdfwidgettool.h \
    sources/pdfwidgetutils.h \
    sources/pdfxreftable.h \
    sources/pdfflatmap.h \
    sources/pdfvisitor.h \
    sources/pdfencoding.h \
    sources/pdfcatalog.h \
    sources/pdfnumbertreeloader.h \
    sources/pdfpage.h \
    sources/pdfstreamfilters.h \
    sources/pdfdrawspacecontroller.h \
    sources/pdfdrawwidget.h \
    sources/pdfflatarray.h \
    sources/pdfcolorspaces.h \
    sources/pdfrenderer.h \
    sources/pdfpagecontentprocessor.h \
    sources/pdfpainter.h \
    sources/pdfutils.h \
    sources/pdfrenderingerrorswidget.h \
    sources/pdffunction.h \
    sources/pdfnametounicode.h \
    sources/pdffont.h \
    sources/pdfexception.h \
    sources/pdfimage.h

FORMS += \
    sources/pdfrenderingerrorswidget.ui \
    sources/pdfselectpagesdialog.ui

RESOURCES += cmaps.qrc

Pdf4Qt_DEPENDENCIES_PATH = $$absolute_path(../../PdfforQt-Dependencies, $$PWD)
Pdf4Qt_OPENSSL_PATH = $$absolute_path(../../Tools, $$[QT_INSTALL_PREFIX])

CONFIG(debug, debug|release) {
SUFFIX = d
}

# Link to freetype library
LIBS += -L$$Pdf4Qt_DEPENDENCIES_PATH/FreeType/ -lfreetype$${SUFFIX}
INCLUDEPATH += $$Pdf4Qt_DEPENDENCIES_PATH/FreeType/include
DEPENDPATH += $$Pdf4Qt_DEPENDENCIES_PATH/FreeType/include

# Link to OpenJPEG library
LIBS += -L$$Pdf4Qt_DEPENDENCIES_PATH/OpenJPEG/lib/ -lopenjp2$${SUFFIX}
INCLUDEPATH += $$Pdf4Qt_DEPENDENCIES_PATH/OpenJPEG/include/openjpeg-2.3
DEPENDPATH += $$Pdf4Qt_DEPENDENCIES_PATH/OpenJPEG/include/openjpeg-2.3
DEFINES += OPJ_STATIC

# Link to Independent JPEG Groups libjpeg
LIBS += -L$$Pdf4Qt_DEPENDENCIES_PATH/libjpeg/bin/ -ljpeg$${SUFFIX}
INCLUDEPATH += $$Pdf4Qt_DEPENDENCIES_PATH/libjpeg/include
DEPENDPATH += $$Pdf4Qt_DEPENDENCIES_PATH/libjpeg/include

# Link OpenSSL
LIBS += -L$$Pdf4Qt_OPENSSL_PATH/OpenSSL/Win_x64/bin -L$$Pdf4Qt_OPENSSL_PATH/OpenSSL/Win_x64/lib -llibcrypto -llibssl
INCLUDEPATH += $$Pdf4Qt_OPENSSL_PATH/OpenSSL/Win_x64/include
DEPENDPATH += $$Pdf4Qt_OPENSSL_PATH/OpenSSL/Win_x64/include

# Add OpenSSL to installations
openssl_lib.files = $$Pdf4Qt_OPENSSL_PATH/OpenSSL/Win_x64/bin/libcrypto-1_1-x64.dll $$Pdf4Qt_OPENSSL_PATH/OpenSSL/Win_x64/bin/libssl-1_1-x64.dll
openssl_lib.path = $$DESTDIR/install
INSTALLS += openssl_lib

# Link zlib
LIBS += -L$$Pdf4Qt_DEPENDENCIES_PATH/zlib/bin/ -lzlibstatic$${SUFFIX}
INCLUDEPATH += $$Pdf4Qt_DEPENDENCIES_PATH/zlib/include
DEPENDPATH += $$Pdf4Qt_DEPENDENCIES_PATH/zlib/include

# Link lcms2
LIBS += -L$$Pdf4Qt_DEPENDENCIES_PATH/lcms2/bin$${SUFFIX}/ -llcms2_static
INCLUDEPATH += $$Pdf4Qt_DEPENDENCIES_PATH/lcms2/include
DEPENDPATH += $$Pdf4Qt_DEPENDENCIES_PATH/lcms2/include

# ensure debug info even for RELEASE build
CONFIG += force_debug_info

QMAKE_CXXFLAGS += /std:c++latest /utf-8
QMAKE_RESOURCE_FLAGS += -threshold 0 -compress 9

PdfforQt_library.files = $$DESTDIR/Pdf4QtLib.dll
PdfforQt_library.path = $$DESTDIR/install
PdfforQt_library.CONFIG += no_check_exist

INSTALLS += PdfforQt_library

qt_libraries.files =    $$[QT_INSTALL_BINS]/Qt?Widgets$${SUFFIX}.dll \
                        $$[QT_INSTALL_BINS]/Qt?Gui$${SUFFIX}.dll \
                        $$[QT_INSTALL_BINS]/Qt?Core$${SUFFIX}.dll \
                        $$[QT_INSTALL_BINS]/Qt?WinExtras$${SUFFIX}.dll \
                        $$[QT_INSTALL_BINS]/Qt?Svg$${SUFFIX}.dll \
                        $$[QT_INSTALL_BINS]/Qt?PrintSupport$${SUFFIX}.dll \
                        $$[QT_INSTALL_BINS]/Qt?TextToSpeech$${SUFFIX}.dll \
                        $$[QT_INSTALL_BINS]/Qt?Network$${SUFFIX}.dll \
                        $$[QT_INSTALL_BINS]/Qt?Xml$${SUFFIX}.dll
qt_libraries.path = $$DESTDIR/install
INSTALLS += qt_libraries

qt_plugin_platform.files = $$[QT_INSTALL_PLUGINS]/platforms/qwindows$${SUFFIX}.dll
qt_plugin_platform.path = $$DESTDIR/install/platforms
INSTALLS += qt_plugin_platform

qt_plugin_style.files = $$[QT_INSTALL_PLUGINS]/styles/qwindowsvistastyle$${SUFFIX}.dll
qt_plugin_style.path = $$DESTDIR/install/styles
INSTALLS += qt_plugin_style

qt_plugin_imageformat.files = $$[QT_INSTALL_PLUGINS]/imageformats/qgif$${SUFFIX}.dll \
                              $$[QT_INSTALL_PLUGINS]/imageformats/qicns$${SUFFIX}.dll \
                              $$[QT_INSTALL_PLUGINS]/imageformats/qico$${SUFFIX}.dll \
                              $$[QT_INSTALL_PLUGINS]/imageformats/qjpeg$${SUFFIX}.dll \
                              $$[QT_INSTALL_PLUGINS]/imageformats/qsvg$${SUFFIX}.dll \
                              $$[QT_INSTALL_PLUGINS]/imageformats/qtga$${SUFFIX}.dll \
                              $$[QT_INSTALL_PLUGINS]/imageformats/qtiff$${SUFFIX}.dll \
                              $$[QT_INSTALL_PLUGINS]/imageformats/qwbpm$${SUFFIX}.dll \
                              $$[QT_INSTALL_PLUGINS]/imageformats/qwebp$${SUFFIX}.dll
qt_plugin_imageformat.path = $$DESTDIR/install/imageformats
INSTALLS += qt_plugin_imageformat

qt_plugin_iconengine.files = $$[QT_INSTALL_PLUGINS]/iconengines/qsvgicon$${SUFFIX}.dll
qt_plugin_iconengine.path = $$DESTDIR/install/iconengines
INSTALLS += qt_plugin_iconengine

qt_plugin_printsupport.files = $$[QT_INSTALL_PLUGINS]/printsupport/windowsprintersupport$${SUFFIX}.dll
qt_plugin_printsupport.path = $$DESTDIR/install/printsupport
INSTALLS += qt_plugin_printsupport

qt_plugin_texttospeech.files = $$[QT_INSTALL_PLUGINS]/texttospeech/qtexttospeech_sapi$${SUFFIX}.dll
qt_plugin_texttospeech.path = $$DESTDIR/install/texttospeech
INSTALLS += qt_plugin_texttospeech

