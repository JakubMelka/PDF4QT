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

add_library(Pdf4QtLib SHARED
    sources/pdfaction.cpp
    sources/pdfadvancedtools.cpp
    sources/pdfalgorithmlcs.cpp
    sources/pdfannotation.cpp
    sources/pdfblendfunction.cpp
    sources/pdfccittfaxdecoder.cpp
    sources/pdfcertificatemanager.cpp
    sources/pdfcertificatemanagerdialog.cpp
    sources/pdfcms.cpp
    sources/pdfcompiler.cpp
    sources/pdfcreatecertificatedialog.cpp
    sources/pdfdiff.cpp
    sources/pdfdocumentbuilder.cpp
    sources/pdfdocumentmanipulator.cpp
    sources/pdfdocumenttextflow.cpp
    sources/pdfdocumenttextfloweditormodel.cpp
    sources/pdfdocumentwriter.cpp
    sources/pdfexecutionpolicy.cpp
    sources/pdffile.cpp
    sources/pdfform.cpp
    sources/pdficontheme.cpp
    sources/pdfitemmodels.cpp
    sources/pdfjavascriptscanner.cpp
    sources/pdfjbig2decoder.cpp
    sources/pdfmultimedia.cpp
    sources/pdfobject.cpp
    sources/pdfobjecteditormodel.cpp
    sources/pdfobjecteditorwidget.cpp
    sources/pdfobjectutils.cpp
    sources/pdfoptimizer.cpp
    sources/pdfoptionalcontent.cpp
    sources/pdfoutline.cpp
    sources/pdfpagecontenteditorstylesettings.cpp
    sources/pdfpagecontenteditortools.cpp
    sources/pdfpagecontenteditorwidget.cpp
    sources/pdfpagecontentelements.cpp
    sources/pdfpagenavigation.cpp
    sources/pdfpagetransition.cpp
    sources/pdfpainterutils.cpp
    sources/pdfparser.cpp
    sources/pdfdocument.cpp
    sources/pdfdocumentreader.cpp
    sources/pdfpattern.cpp
    sources/pdfplugin.cpp
    sources/pdfprogress.cpp
    sources/pdfredact.cpp
    sources/pdfsecurityhandler.cpp
    sources/pdfselectpagesdialog.cpp
    sources/pdfsignaturehandler.cpp
    sources/pdfsnapper.cpp
    sources/pdfstructuretree.cpp
    sources/pdftexteditpseudowidget.cpp
    sources/pdftextlayout.cpp
    sources/pdftransparencyrenderer.cpp
    sources/pdfutils.cpp
    sources/pdfwidgettool.cpp
    sources/pdfwidgetutils.cpp
    sources/pdfxfaengine.cpp
    sources/pdfxreftable.cpp
    sources/pdfvisitor.cpp
    sources/pdfencoding.cpp
    sources/pdfcatalog.cpp
    sources/pdfpage.cpp
    sources/pdfstreamfilters.cpp
    sources/pdfdrawspacecontroller.cpp
    sources/pdfdrawwidget.cpp
    sources/pdfcolorspaces.cpp
    sources/pdfrenderer.cpp
    sources/pdfpagecontentprocessor.cpp
    sources/pdfpainter.cpp
    sources/pdfrenderingerrorswidget.cpp
    sources/pdffunction.cpp
    sources/pdfnametounicode.cpp
    sources/pdffont.cpp
    sources/pdfimage.cpp
    sources/pdfcertificatemanagerdialog.ui
    sources/pdfcreatecertificatedialog.ui
    sources/pdfpagecontenteditorstylesettings.ui
    sources/pdfpagecontenteditorwidget.ui
    sources/pdfrenderingerrorswidget.ui
    sources/pdfselectpagesdialog.ui
    sources/pdfobjecteditorwidget_impl.h
    sources/pdfdocumentsanitizer.h
    sources/pdfdocumentsanitizer.cpp
    cmaps.qrc
)

include(GenerateExportHeader)

GENERATE_EXPORT_HEADER(Pdf4QtLib
                       EXPORT_MACRO_NAME
                       PDF4QTLIBSHARED_EXPORT
                       EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR}/pdf4qtlib_export.h")

target_link_libraries(Pdf4QtLib PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml Qt6::Svg Qt6::OpenGLWidgets)
target_link_libraries(Pdf4QtLib PRIVATE lcms2::lcms2)
target_link_libraries(Pdf4QtLib PRIVATE OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(Pdf4QtLib PRIVATE ZLIB::ZLIB)
target_link_libraries(Pdf4QtLib PRIVATE freetype)
target_link_libraries(Pdf4QtLib PRIVATE openjp2)
target_link_libraries(Pdf4QtLib PRIVATE JPEG::JPEG)

if(LINUX_GCC)
    target_link_libraries(Pdf4QtLib PUBLIC TBB::tbb)
endif()

if(MINGW)
    target_link_libraries(Pdf4QtLib PRIVATE Secur32 Mscms Gdi32 User32 crypt32)
endif()

target_include_directories(Pdf4QtLib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/sources)
target_include_directories(Pdf4QtLib PUBLIC ${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR})

set_target_properties(Pdf4QtLib PROPERTIES
                      VERSION ${PDF4QT_VERSION}
                      SOVERSION ${PDF4QT_VERSION})

install(TARGETS Pdf4QtLib)
