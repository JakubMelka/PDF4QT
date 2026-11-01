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

# The pages of the gallery: compiled once, linked into the gallery and into the
# tests UnitTestsWidgetStyle, UnitTestsFluentComponents and UnitTestsFluentLifetime
# (an object library - each executable gets the same objects as from its own
# sources). The pages depend on no definition of a test.
add_library(Pdf4QtFluentGalleryPages OBJECT
    galleryaipages.cpp
    gallerycardpages.cpp
    gallerycomponentpages.cpp
    gallerydelegatepages.cpp
    gallerypanelpages.cpp
    gallerypages.cpp
    gallerypages.h
)
target_include_directories(Pdf4QtFluentGalleryPages PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Pdf4QtFluentGalleryPages PUBLIC Pdf4QtLibFluent Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(Pdf4QtFluentGalleryPages PROPERTIES QT_EXCLUDE_FROM_TRANSLATION ON)
pdf4qt_target_precompile_headers(Pdf4QtFluentGalleryPages MODULES CORE GUI WIDGETS)

# Gallery of the widgets drawn by PDFWidgetStyle. It is a development tool:
# each tab shows one section of the page "Widgets" of the design, and the
# samples can be exported to images, which are compared with the design.
add_executable(Pdf4QtFluentGallery
    gallerywindow.cpp
    gallerywindow.h
    main.cpp
)

target_link_libraries(Pdf4QtFluentGallery PRIVATE Pdf4QtFluentGalleryPages Pdf4QtLibFluent Qt6::Core Qt6::Gui Qt6::Widgets)
pdf4qt_target_precompile_headers(Pdf4QtFluentGallery REUSE_FROM Pdf4QtFluentGalleryPages)

set_target_properties(Pdf4QtFluentGallery PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    QT_EXCLUDE_FROM_TRANSLATION ON
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)
