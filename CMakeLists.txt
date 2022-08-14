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

cmake_minimum_required(VERSION 3.16)

set(PDF4QT_VERSION 1.2.2)

project(PDF4QT VERSION ${PDF4QT_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Xml PrintSupport TextToSpeech)
qt_standard_project_setup()

add_subdirectory(Pdf4QtLib)
add_subdirectory(CodeGenerator)
add_subdirectory(JBIG2_Viewer)
add_subdirectory(PdfExampleGenerator)
add_subdirectory(PdfTool)
add_subdirectory(UnitTests)
add_subdirectory(Pdf4QtViewer)
add_subdirectory(Pdf4QtViewerPlugins)
add_subdirectory(Pdf4QtViewerProfi)
add_subdirectory(Pdf4QtViewerLite)
add_subdirectory(Pdf4QtDocPageOrganizer)
add_subdirectory(Pdf4QtDocDiff)

message("CMAKE_PREFIX_PATH = " ${CMAKE_PREFIX_PATH})
message("CMAKE_TOOLCHAIN_FILE = " ${CMAKE_TOOLCHAIN_FILE})


#macro(dump_variables)
#    message(STATUS "dump_variables------------------------------------------{")
#    get_cmake_property(_variableNames VARIABLES)
#    foreach (_variableName ${_variableNames})
#        message(STATUS "${_variableName}=${${_variableName}}")
#    endforeach()
#    message(STATUS "dump_variables------------------------------------------}")
#endmacro()
#
#dump_variables()
