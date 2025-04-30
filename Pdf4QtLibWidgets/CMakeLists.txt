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
    sources/pdfwidgetannotation.h
    sources/pdfwidgetannotation.cpp
    sources/pdfpagecontentelements.cpp
    sources/pdfpagecontentelements.h
    sources/pdfitemmodels.cpp
    sources/pdfitemmodels.h
    sources/pdfwidgetformmanager.h
    sources/pdfwidgetformmanager.cpp
    sources/pdftexteditpseudowidget.cpp
    sources/pdftexteditpseudowidget.h
    sources/pdfdrawspacecontroller.cpp
    sources/pdfdrawspacecontroller.h
    sources/pdfcompiler.cpp
    sources/pdfcompiler.h
    sources/pdfdocumentdrawinterface.h
    sources/pdfwidgetsglobal.h
    sources/pdfcertificatelisthelper.h
    sources/pdfcertificatelisthelper.cpp
    sources/pdfpagecontenteditorediteditemsettings.h sources/pdfpagecontenteditorediteditemsettings.cpp sources/pdfpagecontenteditorediteditemsettings.ui
)

include(GenerateExportHeader)

GENERATE_EXPORT_HEADER(Pdf4QtLibWidgets
                       EXPORT_MACRO_NAME
                       PDF4QTLIBWIDGETSSHARED_EXPORT
                       EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR}/pdf4qtlibwidgets_export.h")

target_link_libraries(Pdf4QtLibWidgets PRIVATE Pdf4QtLibCore Qt6::Core Qt6::Gui Qt6::Xml Qt6::Svg Qt6::Widgets)

if(LINUX_GCC)
    target_link_libraries(Pdf4QtLibWidgets PUBLIC TBB::tbb)
endif()

if(MINGW)
    target_link_libraries(Pdf4QtLibWidgets PRIVATE Secur32 Mscms Gdi32 User32 crypt32)
endif()

target_link_libraries(Pdf4QtLibWidgets PRIVATE blend2d::blend2d)

target_include_directories(Pdf4QtLibWidgets INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/sources)
target_include_directories(Pdf4QtLibWidgets PUBLIC ${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR})

set_target_properties(Pdf4QtLibWidgets PROPERTIES
                      VERSION ${PDF4QT_VERSION}
                      SOVERSION ${PDF4QT_VERSION}
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR})

install(TARGETS Pdf4QtLibWidgets RUNTIME DESTINATION ${PDF4QT_INSTALL_BIN_DIR} LIBRARY DESTINATION ${PDF4QT_INSTALL_LIB_DIR})
