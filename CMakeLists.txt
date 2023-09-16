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

set(PDF4QT_VERSION 1.3.5)

project(PDF4QT VERSION ${PDF4QT_VERSION} LANGUAGES CXX)

option(PDF4QT_INSTALL_DEPENDENCIES "Install dependencies" ON)
option(PDF4QT_INSTALL_QT_DEPENDENCIES "Install Qt dependencies" ON)

add_compile_definitions(PDF4QT_PROJECT_VERSION="${PDF4QT_VERSION}")
add_compile_definitions(QT_NO_EMIT)

if(WIN32 AND MSVC)
    option(PDF4QT_INSTALL_MSVC_REDISTRIBUTABLE "Install MSVC redistributable package" ON)
    option(PDF4QT_INSTALL_PREPARE_WIX_INSTALLER "Prepare Wix installer for Windows" ON)
endif()

set(PDF4QT_QT_ROOT "" CACHE PATH "Qt root directory")

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install CACHE PATH "Installation directory" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Xml PrintSupport TextToSpeech OpenGL OpenGLWidgets Multimedia Network Test)
qt_standard_project_setup()

find_package(OpenSSL REQUIRED)
find_package(lcms REQUIRED)
find_package(ZLIB REQUIRED)
find_package(freetype CONFIG REQUIRED)
find_package(OpenJPEG CONFIG REQUIRED)
find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_AUTORCC_OPTIONS "--threshold;0;--compress;9")

if (MSVC)
    add_compile_options(/bigobj /W4 /wd5054 /wd4127 /wd4702)
endif()

if (MINGW)
    add_compile_options("-Wa,-mbig-obj")
endif()

if (UNIX AND NOT APPLE AND CMAKE_COMPILER_IS_GNUCXX)
    set(LINUX_GCC ON)
endif()

if (LINUX_GCC)
    find_package(TBB REQUIRED)
endif()

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
add_subdirectory(WixInstaller)

message("CMAKE_PREFIX_PATH = " ${CMAKE_PREFIX_PATH})
message("CMAKE_TOOLCHAIN_FILE = " ${CMAKE_TOOLCHAIN_FILE})

install(FILES Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtDocDiff.desktop Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtDocPageOrganizer.desktop Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtViewerLite.desktop Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtViewerProfi.desktop DESTINATION share/applications)
install(FILES Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtDocDiff.svg Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtDocPageOrganizer.svg Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtViewerLite.svg Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtViewerProfi.svg DESTINATION share/icons)
install(FILES Desktop/io.github.JakubMelka.Pdf4qt.appdata.xml DESTINATION share/metainfo)

configure_file(make-package.sh.in make-package.sh)

if(PDF4QT_INSTALL_DEPENDENCIES)
    install(DIRECTORY ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/
        TYPE BIN
        FILES_MATCHING
        PATTERN "*.dll"
        PATTERN "*.so"
        PATTERN "*.dylib"
    )

    if("${PDF4QT_QT_ROOT}" STREQUAL "")
        message(WARNING "Set variable PDF4QT_QT_ROOT to Qt6 root directory")
    endif()

    if(PDF4QT_INSTALL_QT_DEPENDENCIES)
        install(DIRECTORY ${PDF4QT_QT_ROOT}/bin/
            RUNTIME DESTINATION bin/
            FILES_MATCHING
            REGEX "(Qt6Core|Qt6Gui|Qt6PrintSupport|Qt6Svg|Qt6TextToSpeech|Qt6Widgets|Qt6Xml|Qt6OpenGL|Qt6OpenGLWidgets|Qt6Multimedia|Qt6Network)\\..*"
            PATTERN "Debug" EXCLUDE
        )

        install(DIRECTORY ${PDF4QT_QT_ROOT}/plugins/platforms/
            RUNTIME DESTINATION bin/platforms/
            FILES_MATCHING
            PATTERN "qwindows.dll"
            PATTERN "*.so"
            PATTERN "*.dylib"
        )

        install(DIRECTORY ${PDF4QT_QT_ROOT}/plugins/iconengines/
            RUNTIME DESTINATION bin/iconengines/
            FILES_MATCHING
            REGEX "qsvgicon\\..*"
        )

        install(DIRECTORY ${PDF4QT_QT_ROOT}/plugins/imageformats/
            RUNTIME DESTINATION bin/imageformats/
            FILES_MATCHING
            PATTERN "*.dll"
            PATTERN "*.so"
            PATTERN "*.dylib"
            REGEX "d\\..*" EXCLUDE
        )

        install(DIRECTORY ${PDF4QT_QT_ROOT}/plugins/styles/
            RUNTIME DESTINATION bin/styles/
            FILES_MATCHING
            PATTERN "*.dll"
            PATTERN "*.so"
            PATTERN "*.dylib"
            REGEX "d\\..*" EXCLUDE
        )

        install(DIRECTORY ${PDF4QT_QT_ROOT}/plugins/texttospeech/
            RUNTIME DESTINATION bin/texttospeech/
            FILES_MATCHING
            PATTERN "*.dll"
            PATTERN "*.so"
            PATTERN "*.dylib"
            REGEX "d\\..*" EXCLUDE
        )
    endif()

    if(WIN32 AND MSVC AND PDF4QT_INSTALL_MSVC_REDISTRIBUTABLE)
        get_filename_component(MSVC_REDISTRIBUTABLES_PATH $ENV{VCToolsRedistDir}/$ENV{VSCMD_ARG_TGT_ARCH}/Microsoft.VC${MSVC_TOOLSET_VERSION}.CRT/ ABSOLUTE)
        message(STATUS "MSVC Redistributable Package Path = ${MSVC_REDISTRIBUTABLES_PATH}")

        install(DIRECTORY ${MSVC_REDISTRIBUTABLES_PATH}/
            RUNTIME DESTINATION bin/
            FILES_MATCHING
            PATTERN "*.dll"
        )
    endif()
endif()

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

