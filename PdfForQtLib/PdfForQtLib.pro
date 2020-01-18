#    Copyright (C) 2018-2019 Jakub Melka
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

QT       += gui widgets

TARGET = PdfForQtLib
TEMPLATE = lib

DEFINES += PDFFORQTLIB_LIBRARY

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
    sources/pdfblendfunction.cpp \
    sources/pdfccittfaxdecoder.cpp \
    sources/pdfcms.cpp \
    sources/pdfcompiler.cpp \
    sources/pdfexecutionpolicy.cpp \
    sources/pdffile.cpp \
    sources/pdfitemmodels.cpp \
    sources/pdfjbig2decoder.cpp \
    sources/pdfmultimedia.cpp \
    sources/pdfobject.cpp \
    sources/pdfoptionalcontent.cpp \
    sources/pdfoutline.cpp \
    sources/pdfpagetransition.cpp \
    sources/pdfparser.cpp \
    sources/pdfdocument.cpp \
    sources/pdfdocumentreader.cpp \
    sources/pdfpattern.cpp \
    sources/pdfprogress.cpp \
    sources/pdfsecurityhandler.cpp \
    sources/pdftextlayout.cpp \
    sources/pdfutils.cpp \
    sources/pdfwidgettool.cpp \
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
    sources/pdfblendfunction.h \
    sources/pdfccittfaxdecoder.h \
    sources/pdfcms.h \
    sources/pdfcompiler.h \
    sources/pdfexecutionpolicy.h \
    sources/pdffile.h \
    sources/pdfitemmodels.h \
    sources/pdfjbig2decoder.h \
    sources/pdfmeshqualitysettings.h \
    sources/pdfmultimedia.h \
    sources/pdfnametreeloader.h \
    sources/pdfobject.h \
    sources/pdfoptionalcontent.h \
    sources/pdfoutline.h \
    sources/pdfpagetransition.h \
    sources/pdfparser.h \
    sources/pdfglobal.h \
    sources/pdfconstants.h \
    sources/pdfdocument.h \
    sources/pdfdocumentreader.h \
    sources/pdfpattern.h \
    sources/pdfprogress.h \
    sources/pdfsecurityhandler.h \
    sources/pdftextlayout.h \
    sources/pdfwidgettool.h \
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
    sources/pdfrenderingerrorswidget.ui

PDFFORQT_DEPENDENCIES_PATH = K:\Programming\PDF\PDF_For_Qt\PDfForQt-Dependencies

# Link to freetype library
LIBS += -L$$PDFFORQT_DEPENDENCIES_PATH/FreeType/ -lfreetype
INCLUDEPATH += $$PDFFORQT_DEPENDENCIES_PATH/FreeType/include
DEPENDPATH += $$PDFFORQT_DEPENDENCIES_PATH/FreeType/include

# Add freetype to installations
freetype_lib.files = $$PDFFORQT_DEPENDENCIES_PATH/FreeType/freetype.dll
freetype_lib.path = $$DESTDIR/install
INSTALLS += freetype_lib

# Link to OpenJPEG library
LIBS += -L$$PDFFORQT_DEPENDENCIES_PATH/OpenJPEG/lib/ -lopenjp2
INCLUDEPATH += $$PDFFORQT_DEPENDENCIES_PATH/OpenJPEG/include/openjpeg-2.3
DEPENDPATH += $$PDFFORQT_DEPENDENCIES_PATH/OpenJPEG/include/openjpeg-2.3

# Add OpenJPEG to installations
openjpeg_lib.files = $$PDFFORQT_DEPENDENCIES_PATH/OpenJPEG/bin/openjp2.dll
openjpeg_lib.path = $$DESTDIR/install
INSTALLS += openjpeg_lib

# Link to Independent JPEG Groups libjpeg
LIBS += -L$$PDFFORQT_DEPENDENCIES_PATH/libjpeg/bin/ -ljpeg
INCLUDEPATH += $$PDFFORQT_DEPENDENCIES_PATH/libjpeg/include
DEPENDPATH += $$PDFFORQT_DEPENDENCIES_PATH/libjpeg/include

# Add libjpeg to installations
libjpeg.files = $$PDFFORQT_DEPENDENCIES_PATH/libjpeg/bin/jpeg.dll
libjpeg.path = $$DESTDIR/install
INSTALLS += libjpeg

# Link OpenSSL
LIBS += -L$$PDFFORQT_DEPENDENCIES_PATH/OpenSSL/ -llibcrypto -llibssl
INCLUDEPATH += $$PDFFORQT_DEPENDENCIES_PATH/OpenSSL/include
DEPENDPATH += $$PDFFORQT_DEPENDENCIES_PATH/OpenSSL/include

# Add OpenSSL to installations
openssl_lib.files = $$PDFFORQT_DEPENDENCIES_PATH/OpenSSL/libcrypto-3.dll $$PDFFORQT_DEPENDENCIES_PATH/OpenSSL/libssl-3.dll
openssl_lib.path = $$DESTDIR/install
INSTALLS += openssl_lib

# Link zlib
LIBS += -L$$PDFFORQT_DEPENDENCIES_PATH/zlib/bin/ -lzlib
INCLUDEPATH += $$PDFFORQT_DEPENDENCIES_PATH/zlib/include
DEPENDPATH += $$PDFFORQT_DEPENDENCIES_PATH/zlib/include

# Add zlib to installations
zlib.files = $$PDFFORQT_DEPENDENCIES_PATH/zlib/bin/zlib.dll
zlib.path = $$DESTDIR/install
INSTALLS += zlib

# Link lcms2
LIBS += -L$$PDFFORQT_DEPENDENCIES_PATH/lcms2/bin/ -llcms2
INCLUDEPATH += $$PDFFORQT_DEPENDENCIES_PATH/lcms2/include
DEPENDPATH += $$PDFFORQT_DEPENDENCIES_PATH/lcms2/include

# Add lcms2 to installations
lcms2.files = $$PDFFORQT_DEPENDENCIES_PATH/lcms2/bin/lcms2.dll
lcms2.path = $$DESTDIR/install
INSTALLS += lcms2

# ensure debug info even for RELEASE build
CONFIG += force_debug_info

QMAKE_CXXFLAGS += /std:c++latest /utf-8

# resource manifest
CMAP_RESOURCE_INPUT = $$PWD/cmaps.qrc
cmap_resource_builder.commands = $$[QT_HOST_BINS]/rcc -binary ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT} -threshold 0 -compress 9
cmap_resource_builder.depend_command = $$[QT_HOST_BINS]/rcc -list $$QMAKE_RESOURCE_FLAGS ${QMAKE_FILE_IN}
cmap_resource_builder.input = CMAP_RESOURCE_INPUT
cmap_resource_builder.output = $$DESTDIR/${QMAKE_FILE_IN_BASE}.qrb
cmap_resource_builder.CONFIG += no_link target_predeps
QMAKE_EXTRA_COMPILERS += cmap_resource_builder

cmaps_files.files = $$DESTDIR/cmaps.qrb
cmaps_files.path = $$DESTDIR/install

INSTALLS += cmaps_files

pdfforqt_library.files = $$DESTDIR/PdfForQtLib.dll
pdfforqt_library.path = $$DESTDIR/install

INSTALLS += pdfforqt_library

CONFIG(debug, debug|release) {
SUFFIX = d
}

qt_libraries.files =    $$[QT_INSTALL_BINS]/Qt?Widgets$${SUFFIX}.dll \
                        $$[QT_INSTALL_BINS]/Qt?Gui$${SUFFIX}.dll \
                        $$[QT_INSTALL_BINS]/Qt?Core$${SUFFIX}.dll \
                        $$[QT_INSTALL_BINS]/Qt?WinExtras$${SUFFIX}.dll \
                        $$[QT_INSTALL_BINS]/Qt?Svg$${SUFFIX}.dll
qt_libraries.path = $$DESTDIR/install
INSTALLS += qt_libraries
