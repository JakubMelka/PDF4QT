#    Copyright (C) 2022-2024 Jakub Melka
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

add_library(Pdf4QtLibGui SHARED
    pdfaboutdialog.cpp
    pdfaboutdialog.h
    pdfadvancedfindwidget.cpp
    pdfadvancedfindwidget.h
    pdfdocumentpropertiesdialog.cpp
    pdfdocumentpropertiesdialog.h
    pdfencryptionsettingsdialog.cpp
    pdfencryptionsettingsdialog.h
    pdfencryptionstrengthhintwidget.cpp
    pdfencryptionstrengthhintwidget.h
    pdfoptimizedocumentdialog.cpp
    pdfoptimizedocumentdialog.h
    pdfprogramcontroller.cpp
    pdfprogramcontroller.h
    pdfrecentfilemanager.cpp
    pdfrecentfilemanager.h
    pdfrendertoimagesdialog.cpp
    pdfrendertoimagesdialog.h
    pdfsendmail.cpp
    pdfsendmail.h
    pdfsidebarwidget.cpp
    pdfsidebarwidget.h
    pdftexttospeech.cpp
    pdftexttospeech.h
    pdfundoredomanager.cpp
    pdfundoredomanager.h
    pdfeditormainwindow.cpp
    pdfeditormainwindow.h
    pdfviewermainwindow.cpp
    pdfviewermainwindow.h
    pdfviewersettings.cpp
    pdfviewersettings.h
    pdfviewersettingsdialog.cpp
    pdfviewersettingsdialog.h
    pdfwintaskbarprogress.cpp
    pdfwintaskbarprogress.h
    pdfaboutdialog.ui
    pdfadvancedfindwidget.ui
    pdfdocumentpropertiesdialog.ui
    pdfencryptionsettingsdialog.ui
    pdfoptimizedocumentdialog.ui
    pdfrendertoimagesdialog.ui
    pdfsidebarwidget.ui
    pdfeditormainwindow.ui
    pdfviewermainwindow.ui
    pdfviewersettingsdialog.ui
    pdfsanitizedocumentdialog.ui
    pdfsanitizedocumentdialog.cpp
    pdfsanitizedocumentdialog.h
    pdfcreatebitonaldocumentdialog.ui
    pdfcreatebitonaldocumentdialog.cpp
    pdfcreatebitonaldocumentdialog.h
    pdf4qtlibgui.qrc
    pdfbookmarkmanager.h
    pdfbookmarkmanager.cpp
    pdfbookmarkui.h
    pdfbookmarkui.cpp
    pdfactioncombobox.h
    pdfactioncombobox.cpp
)

add_compile_definitions(QT_INSTALL_DIRECTORY="${QT6_INSTALL_PREFIX}")

include(GenerateExportHeader)

GENERATE_EXPORT_HEADER(Pdf4QtLibGui
                       EXPORT_MACRO_NAME
                       PDF4QTLIBGUILIBSHARED_EXPORT
                       EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR}/pdf4qtlibgui_export.h")

target_link_libraries(Pdf4QtLibGui PRIVATE Pdf4QtLibCore Pdf4QtLibWidgets Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport Qt6::TextToSpeech Qt6::Xml Qt6::Svg)
target_include_directories(Pdf4QtLibGui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(Pdf4QtLibGui PUBLIC ${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR})

set_target_properties(Pdf4QtLibGui PROPERTIES
                      VERSION ${PDF4QT_VERSION}
                      SOVERSION ${PDF4QT_VERSION}
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR})

install(TARGETS Pdf4QtLibGui RUNTIME DESTINATION ${PDF4QT_INSTALL_BIN_DIR} LIBRARY DESTINATION ${PDF4QT_INSTALL_LIB_DIR})
