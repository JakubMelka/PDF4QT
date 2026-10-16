# MIT License
#
# Copyright (c) 2018-2026 Jakub Melka and Contributors
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

# Look of the user interface: the widget style, the colors of the themes and
# the widgets, which draw themselves. The library depends only on Qt, so each
# application of the project can use it.
add_library(Pdf4QtLibFluent SHARED
    sources/pdfaccenthue.cpp
    sources/pdfaccenthue.h
    sources/pdfcomponentbuttons.cpp
    sources/pdfcomponentbuttons.h
    sources/pdfcomponentpanels.cpp
    sources/pdfcomponentpanels.h
    sources/pdfcomponentrows.cpp
    sources/pdfcomponentrows.h
    sources/pdffluentglobal.h
    sources/pdffluentwidgets.cpp
    sources/pdffluentwidgets.h
    sources/pdfmessagebox.cpp
    sources/pdfmessagebox.h
    sources/pdfstylepainter.cpp
    sources/pdfstylepainter.h
    sources/pdfthemecolors.cpp
    sources/pdfthemecolors.h
    sources/pdfthemefonts.cpp
    sources/pdfthemefonts.h
    sources/pdfthemeiconengine.cpp
    sources/pdfthemeiconengine.h
    sources/pdfthememanager.cpp
    sources/pdfthememanager.h
    sources/pdftooltip.cpp
    sources/pdftooltip.h
    sources/pdfwidgetstyle.cpp
    sources/pdfwidgetstyle.h
    sources/pdfwidgetstylebuttons.cpp
    sources/pdfwidgetstylecontainers.cpp
    sources/pdfwidgetstylehelpers.cpp
    sources/pdfwidgetstyleimpl.cpp
    sources/pdfwidgetstyleimpl.h
    sources/pdfwidgetstyleinputs.cpp
    sources/pdfwidgetstyleitemviews.cpp
    sources/pdfwidgetstylemenus.cpp
    sources/pdfwidgetstylemetrics.cpp
    sources/pdfwidgetstyleproperties.h
    sources/pdfwidgetstyleranges.cpp
    sources/pdfwidgetstyletabs.cpp
    resources/fluent.qrc
)

include(GenerateExportHeader)

GENERATE_EXPORT_HEADER(Pdf4QtLibFluent
                       EXPORT_MACRO_NAME
                       PDF4QTLIBFLUENTSHARED_EXPORT
                       EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR}/pdf4qtlibfluent_export.h")

target_link_libraries(Pdf4QtLibFluent PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)

target_include_directories(Pdf4QtLibFluent INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/sources)
target_include_directories(Pdf4QtLibFluent PUBLIC ${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR})

set_target_properties(Pdf4QtLibFluent PROPERTIES
                      VERSION ${PDF4QT_VERSION}
                      SOVERSION ${PDF4QT_VERSION}
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR})

# The library is not installed yet - no distributed application links it.
