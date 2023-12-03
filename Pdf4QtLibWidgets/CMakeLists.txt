#    Copyright (C) 2023 Jakub Melka
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

add_library(Pdf4QtLibWidgets SHARED
    sources/pdfadvancedtools.cpp
    sources/pdfadvancedtools.h
    sources/pdfwidgettool.cpp
    sources/pdfwidgetutils.cpp
    sources/pdfwidgettool.h
    sources/pdfwidgetutils.h
    sources/pdfcertificatemanagerdialog.cpp
    sources/pdfcertificatemanagerdialog.h
    sources/pdfcertificatemanagerdialog.ui
    sources/pdfcreatecertificatedialog.cpp
    sources/pdfcreatecertificatedialog.h
    sources/pdfcreatecertificatedialog.ui
    sources/pdfdrawwidget.cpp
    sources/pdfdrawwidget.h
    sources/pdfobjecteditorwidget.h
    sources/pdfobjecteditorwidget_impl.h
    sources/pdfobjecteditorwidget.cpp
    sources/pdfpagecontenteditorstylesettings.h
    sources/pdfpagecontenteditorstylesettings.cpp
    sources/pdfpagecontenteditorstylesettings.ui
    sources/pdfpagecontenteditortools.cpp
    sources/pdfpagecontenteditorwidget.cpp
    sources/pdfpagecontenteditortools.h
    sources/pdfpagecontenteditorwidget.h
    sources/pdfpagecontenteditorwidget.ui
    sources/pdfrenderingerrorswidget.h
    sources/pdfrenderingerrorswidget.cpp
    sources/pdfrenderingerrorswidget.ui
    sources/pdfselectpagesdialog.h
    sources/pdfselectpagesdialog.cpp
    sources/pdfselectpagesdialog.ui
)

include(GenerateExportHeader)

GENERATE_EXPORT_HEADER(Pdf4QtLibWidgets
                       EXPORT_MACRO_NAME
                       PDF4QTLIBSHARED_EXPORT
                       EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR}/pdf4qtlibwidgets_export.h")

if(PDF4QT_ENABLE_OPENGL)
    target_link_libraries(Pdf4QtLibWidgets PRIVATE Qt6::OpenGLWidgets)
endif()

target_link_libraries(Pdf4QtLibWidgets PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Xml Qt6::Svg Qt6::Widgets)

if(LINUX_GCC)
    target_link_libraries(Pdf4QtLibWidgets PUBLIC TBB::tbb)
endif()

if(MINGW)
    target_link_libraries(Pdf4QtLibWidgets PRIVATE Secur32 Mscms Gdi32 User32 crypt32)
endif()

target_include_directories(Pdf4QtLibWidgets INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/sources)
target_include_directories(Pdf4QtLibWidgets PUBLIC ${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR})

set_target_properties(Pdf4QtLibWidgets PROPERTIES
                      VERSION ${PDF4QT_VERSION}
                      SOVERSION ${PDF4QT_VERSION}
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR})

install(TARGETS Pdf4QtLibWidgets RUNTIME DESTINATION ${PDF4QT_INSTALL_BIN_DIR} LIBRARY DESTINATION ${PDF4QT_INSTALL_LIB_DIR})
