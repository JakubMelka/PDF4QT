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

add_library(Pdf4QtViewer SHARED
    pdfaboutdialog.cpp
    pdfadvancedfindwidget.cpp
    pdfdocumentpropertiesdialog.cpp
    pdfencryptionsettingsdialog.cpp
    pdfencryptionstrengthhintwidget.cpp
    pdfoptimizedocumentdialog.cpp
    pdfprogramcontroller.cpp
    pdfrecentfilemanager.cpp
    pdfrendertoimagesdialog.cpp
    pdfsendmail.cpp
    pdfsidebarwidget.cpp
    pdftexttospeech.cpp
    pdfundoredomanager.cpp
    pdfviewermainwindow.cpp
    pdfviewermainwindowlite.cpp
    pdfviewersettings.cpp
    pdfviewersettingsdialog.cpp
    pdfwintaskbarprogress.cpp
    pdfaboutdialog.ui
    pdfadvancedfindwidget.ui
    pdfdocumentpropertiesdialog.ui
    pdfencryptionsettingsdialog.ui
    pdfoptimizedocumentdialog.ui
    pdfrendertoimagesdialog.ui
    pdfsidebarwidget.ui
    pdfviewermainwindow.ui
    pdfviewermainwindowlite.ui
    pdfviewersettingsdialog.ui
    pdf4qtviewer.qrc
)

add_compile_definitions(QT_INSTALL_DIRECTORY="${QT6_INSTALL_PREFIX}")

include(GenerateExportHeader)

GENERATE_EXPORT_HEADER(Pdf4QtViewer
                       EXPORT_MACRO_NAME
                       PDF4QTVIEWERLIBSHARED_EXPORT
                       EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR}/pdf4qtviewer_export.h")

target_link_libraries(Pdf4QtViewer PRIVATE Pdf4QtLib Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport Qt6::TextToSpeech Qt6::Xml Qt6::OpenGLWidgets)
target_include_directories(Pdf4QtViewer INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(Pdf4QtViewer PUBLIC ${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR})

set_target_properties(Pdf4QtLib PROPERTIES
                      VERSION ${PDF4QT_VERSION}
                      SOVERSION ${PDF4QT_VERSION})

install(TARGETS Pdf4QtViewer)
