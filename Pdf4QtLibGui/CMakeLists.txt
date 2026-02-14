# MIT License
#
# Copyright (c) 2018-2025 Jakub Melka and Contributors
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

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
    pdffullscreenwidget.h
    pdffullscreenwidget.cpp
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
