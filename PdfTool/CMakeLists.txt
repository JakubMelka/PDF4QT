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

add_executable(PdfTool
    main.cpp 
    pdfoutputformatter.cpp 
    pdftoolabstractapplication.cpp 
    pdftoolattachments.cpp 
    pdftoolaudiobook.cpp 
    pdftoolcertstore.cpp 
    pdftoolcolorprofiles.cpp 
    pdftooldecrypt.cpp 
    pdftooldiff.cpp 
    pdftoolencrypt.cpp 
    pdftoolfetchimages.cpp 
    pdftoolfetchtext.cpp 
    pdftoolinfo.cpp 
    pdftoolinfofonts.cpp 
    pdftoolinfoinks.cpp 
    pdftoolinfojavascript.cpp 
    pdftoolinfometadata.cpp 
    pdftoolinfonameddestinations.cpp 
    pdftoolinfopageboxes.cpp 
    pdftoolinfostructuretree.cpp 
    pdftoolinkcoverage.cpp 
    pdftooloptimize.cpp 
    pdftoolrender.cpp 
    pdftoolseparate.cpp 
    pdftoolstatistics.cpp 
    pdftoolunite.cpp 
    pdftoolverifysignatures.cpp 
    pdftoolxml.cpp
)

target_link_libraries(PdfTool PRIVATE Pdf4QtLib Qt6::Core Qt6::Gui Qt6::Xml)

if(MINGW)
    target_link_libraries(PdfTool PRIVATE ole32 sapi)
endif()

set_target_properties(PdfTool PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}
)

install(TARGETS PdfTool RUNTIME DESTINATION ${PDF4QT_INSTALL_BIN_DIR} LIBRARY DESTINATION ${PDF4QT_INSTALL_LIB_DIR})
