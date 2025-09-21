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

cmake_minimum_required(VERSION 3.16)

set(PDF4QT_VERSION 1.5.2.0)
message(STATUS "Build type - CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

project(PDF4QT VERSION ${PDF4QT_VERSION} LANGUAGES CXX)

message("VCPKG Enabled: " ${VCPKG_TOOLCHAIN})

option(PDF4QT_INSTALL_DEPENDENCIES "Install dependencies" ${VCPKG_TOOLCHAIN})
option(PDF4QT_INSTALL_QT_DEPENDENCIES "Install Qt dependencies" ${VCPKG_TOOLCHAIN})
option(PDF4QT_INSTALL_INCLUDE "Install include headers" ON)
option(PDF4QT_FLATPAK_BUILD "Special flag for Flatpak" OFF)

add_compile_definitions(PDF4QT_PROJECT_VERSION="${PDF4QT_VERSION}")
add_compile_definitions(QT_NO_EMIT)

if(PDF4QT_FLATPAK_BUILD)
    add_compile_definitions(PDF4QT_FLATPAK_BUILD)
endif()

if(WIN32 AND MSVC)
    option(PDF4QT_INSTALL_MSVC_REDISTRIBUTABLE "Install MSVC redistributable package" ON)
    option(PDF4QT_INSTALL_PREPARE_WIX_INSTALLER "Prepare Wix installer for Windows" ON)
endif()

option(PDF4QT_BUILD_ONLY_CORE_LIBRARY "Build only core library" OFF)

set(PDF4QT_QT_ROOT "" CACHE PATH "Qt root directory")

if(VCPKG_TOOLCHAIN AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install CACHE PATH "Installation directory" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Core LinguistTools)

if(PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Svg Xml)
else()
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Xml PrintSupport TextToSpeech Test)
endif()

qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES en de cs es ko zh_CN fr tr ru)

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Freetype REQUIRED)
find_package(OpenJPEG CONFIG REQUIRED)
find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
find_package(blend2d CONFIG REQUIRED)

if(VCPKG_TOOLCHAIN)
    find_package(lcms2 REQUIRED)
    set(LCMS2_LIBRARIES lcms2::lcms2 CACHE INTERNAL "LCMS2 libraries")
else() # lcms usually doesn't provide a config file
    find_library(LCMS2_LIBRARIES lcms2 REQUIRED)
endif()

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

if (UNIX AND NOT APPLE)
    set(PDF4QT_LINUX ON)
endif()

if (LINUX_GCC)
    find_package(TBB REQUIRED)
endif()

option(PDF4QT_INSTALL_TO_USR "Install to usr directory" ${VCPKG_TOOLCHAIN})

if(PDF4QT_LINUX)
    set(PDF4QT_INSTALL_LIB_DIR_IMPL ${CMAKE_INSTALL_LIBDIR})
    set(PDF4QT_INSTALL_BIN_DIR_IMPL ${CMAKE_INSTALL_BINDIR})

    if(VCPKG_TOOLCHAIN)
        # Check if VCPKG_OVERLAY_PORTS is set as an environment variable or a CMake argument
        if(NOT DEFINED ENV{VCPKG_OVERLAY_PORTS} AND NOT DEFINED VCPKG_OVERLAY_PORTS)
            message(WARNING "The VCPKG_OVERLAY_PORTS variable is not set! Application can crash in Open/Save dialog due to incompatible LIBPNG.")
        else()
            set(VCPKG_OVERLAY_PORTS_VALUE "$ENV{VCPKG_OVERLAY_PORTS}")
            if(DEFINED VCPKG_OVERLAY_PORTS)
                set(VCPKG_OVERLAY_PORTS_VALUE "${VCPKG_OVERLAY_PORTS}")
            endif()
            message(STATUS "The VCPKG_OVERLAY_PORTS variable is set to: ${VCPKG_OVERLAY_PORTS_VALUE}")
        endif()
    endif()

else()
    set(PDF4QT_INSTALL_LIB_DIR_IMPL ${CMAKE_INSTALL_BINDIR})
    set(PDF4QT_INSTALL_BIN_DIR_IMPL ${CMAKE_INSTALL_BINDIR})
endif()

if(PDF4QT_INSTALL_TO_USR)
    set(PDF4QT_INSTALL_LIB_DIR usr/${PDF4QT_INSTALL_LIB_DIR_IMPL})
    set(PDF4QT_INSTALL_BIN_DIR usr/${PDF4QT_INSTALL_BIN_DIR_IMPL})
    set(PDF4QT_INSTALL_SHARE_DIR usr/${CMAKE_INSTALL_DATAROOTDIR})
    set(PDF4QT_INSTALL_INCLUDE_DIR usr/${CMAKE_INSTALL_INCLUDEDIR})
else()
    set(PDF4QT_INSTALL_LIB_DIR ${PDF4QT_INSTALL_LIB_DIR_IMPL})
    set(PDF4QT_INSTALL_BIN_DIR ${PDF4QT_INSTALL_BIN_DIR_IMPL})
    set(PDF4QT_INSTALL_SHARE_DIR ${CMAKE_INSTALL_DATAROOTDIR})
    set(PDF4QT_INSTALL_INCLUDE_DIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

add_subdirectory(Pdf4QtLibCore)

if(PDF4QT_INSTALL_INCLUDE)
    # Install public headers from Pdf4QtLibCore/include
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Pdf4QtLibCore/sources/
            DESTINATION ${PDF4QT_INSTALL_INCLUDE_DIR}/Pdf4QtLibCore
            FILES_MATCHING PATTERN "*.h")

    # Install generated export header
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pdf4qtlibcore_export.h
            DESTINATION ${PDF4QT_INSTALL_INCLUDE_DIR}/Pdf4QtLibCore)

    # Install public headers from Pdf4QtLibWidgets/include
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Pdf4QtLibWidgets/sources/
            DESTINATION ${PDF4QT_INSTALL_INCLUDE_DIR}/Pdf4QtLibWidgets
            FILES_MATCHING PATTERN "*.h")

    # Install generated export header
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pdf4qtlibwidgets_export.h
            DESTINATION ${PDF4QT_INSTALL_INCLUDE_DIR}/Pdf4QtLibWidgets)
endif()

if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    add_subdirectory(Pdf4QtLibWidgets)
    add_subdirectory(CodeGenerator)
    add_subdirectory(JBIG2_Viewer)
    add_subdirectory(PdfExampleGenerator)
    add_subdirectory(PdfTool)
    add_subdirectory(UnitTests)
    add_subdirectory(Pdf4QtLibGui)
    add_subdirectory(Pdf4QtEditorPlugins)
    add_subdirectory(Pdf4QtEditor)
    add_subdirectory(Pdf4QtViewer)
    add_subdirectory(Pdf4QtPageMaster)
    add_subdirectory(Pdf4QtDiff)
    add_subdirectory(Pdf4QtLaunchPad)
    add_subdirectory(WixInstaller)
endif()

qt_collect_translation_source_targets(i18n_targets)
message("CMAKE_TRANSLATION_TARGETS = " ${i18n_targets})

set(PDF4QT_TRANSLATION_TS_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/translations)
set(PDF4QT_TRANSLATION_QM_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR}/translations)

message("PDF4QT_TRANSLATION_TS_OUTPUT_DIRECTORY" ${PDF4QT_TRANSLATION_TS_OUTPUT_DIRECTORY})
message("PDF4QT_TRANSLATION_QM_OUTPUT_DIRECTORY" ${PDF4QT_TRANSLATION_QM_OUTPUT_DIRECTORY})

qt_add_translations(TARGETS Pdf4QtEditor Pdf4QtViewer Pdf4QtPageMaster Pdf4QtDiff PdfTool
                    SOURCE_TARGETS ${i18n_targets}
                    MERGE_QT_TRANSLATIONS
                    TS_OUTPUT_DIRECTORY ${PDF4QT_TRANSLATION_TS_OUTPUT_DIRECTORY}
                    QM_OUTPUT_DIRECTORY ${PDF4QT_TRANSLATION_QM_OUTPUT_DIRECTORY}
                    QM_FILES_OUTPUT_VARIABLE pdf4qt_qm_files)

message("CMAKE_PREFIX_PATH = " ${CMAKE_PREFIX_PATH})
message("CMAKE_TOOLCHAIN_FILE = " ${CMAKE_TOOLCHAIN_FILE})

configure_file(version.txt.in version.txt)

if(NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    install(DIRECTORY ${PDF4QT_TRANSLATION_QM_OUTPUT_DIRECTORY}
            RUNTIME DESTINATION ${PDF4QT_INSTALL_BIN_DIR}
            FILES_MATCHING
            PATTERN *.qm)

    if(WIN32)
        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Desktop/44x44/
                RUNTIME DESTINATION ${PDF4QT_INSTALL_BIN_DIR}/assets/44x44
                FILES_MATCHING
                PATTERN *.png)

        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Desktop/150x150/
                RUNTIME DESTINATION ${PDF4QT_INSTALL_BIN_DIR}/assets/150x150
                FILES_MATCHING
                PATTERN *.png)

        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/AppxManifest.xml.in ${CMAKE_CURRENT_BINARY_DIR}/AppxManifest.xml)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/AppxManifest.xml DESTINATION ${PDF4QT_INSTALL_BIN_DIR})
        install(FILES assets/logo.png DESTINATION ${PDF4QT_INSTALL_BIN_DIR}/assets)
    else()
        configure_file(make-package.sh.in make-package.sh)

        install(FILES Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtDiff.desktop
                      Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtPageMaster.desktop
                      Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtViewer.desktop
                      Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtEditor.desktop
                      Desktop/io.github.JakubMelka.Pdf4qt.desktop
                      DESTINATION ${PDF4QT_INSTALL_SHARE_DIR}/applications)
        install(FILES Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtDiff.svg
                      Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtPageMaster.svg
                      Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtViewer.svg
                      Desktop/io.github.JakubMelka.Pdf4qt.Pdf4QtEditor.svg
                      Desktop/io.github.JakubMelka.Pdf4qt.svg
                      DESTINATION ${PDF4QT_INSTALL_SHARE_DIR}/icons/hicolor/scalable/apps)
        install(FILES Desktop/io.github.JakubMelka.Pdf4qt.appdata.xml DESTINATION ${PDF4QT_INSTALL_SHARE_DIR}/metainfo)
        install(FILES Desktop/128x128/io.github.JakubMelka.Pdf4qt.Pdf4QtDiff.png
                      Desktop/128x128/io.github.JakubMelka.Pdf4qt.Pdf4QtPageMaster.png
                      Desktop/128x128/io.github.JakubMelka.Pdf4qt.Pdf4QtViewer.png
                      Desktop/128x128/io.github.JakubMelka.Pdf4qt.Pdf4QtEditor.png
                      Desktop/128x128/io.github.JakubMelka.Pdf4qt.png
                      DESTINATION ${PDF4QT_INSTALL_SHARE_DIR}/icons/hicolor/128x128/apps)
    endif()

endif()

if(PDF4QT_INSTALL_DEPENDENCIES AND NOT PDF4QT_BUILD_ONLY_CORE_LIBRARY)
    install(DIRECTORY ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/
        RUNTIME DESTINATION ${PDF4QT_INSTALL_LIB_DIR}/
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
            RUNTIME DESTINATION ${PDF4QT_INSTALL_LIB_DIR}/
            FILES_MATCHING
            REGEX "(Qt6Core|Qt6Gui|Qt6PrintSupport|Qt6Svg|Qt6TextToSpeech|Qt6Widgets|Qt6Xml|Qt6Multimedia|Qt6Network)\\..*"
            PATTERN "Debug" EXCLUDE
            PATTERN "*.pdb" EXCLUDE
        )

        install(DIRECTORY ${PDF4QT_QT_ROOT}/plugins/platforms/
            RUNTIME DESTINATION ${PDF4QT_INSTALL_LIB_DIR}/platforms/
            FILES_MATCHING
            PATTERN "qwindows.dll"
            PATTERN "*.so"
            PATTERN "*.dylib"
            PATTERN "*.pdb" EXCLUDE
        )

        install(DIRECTORY ${PDF4QT_QT_ROOT}/plugins/iconengines/
            RUNTIME DESTINATION ${PDF4QT_INSTALL_LIB_DIR}/iconengines/
            FILES_MATCHING
            REGEX "qsvgicon\\..*"
            PATTERN "*.pdb" EXCLUDE
        )

        install(DIRECTORY ${PDF4QT_QT_ROOT}/plugins/imageformats/
            RUNTIME DESTINATION ${PDF4QT_INSTALL_LIB_DIR}/imageformats/
            FILES_MATCHING
            PATTERN "*.dll"
            PATTERN "*.so"
            PATTERN "*.dylib"
            REGEX "d\\..*" EXCLUDE
            PATTERN "*.pdb" EXCLUDE
        )

        install(DIRECTORY ${PDF4QT_QT_ROOT}/plugins/styles/
            RUNTIME DESTINATION ${PDF4QT_INSTALL_LIB_DIR}/styles/
            FILES_MATCHING
            PATTERN "*.dll"
            PATTERN "*.so"
            PATTERN "*.dylib"
            REGEX "d\\..*" EXCLUDE
            PATTERN "*.pdb" EXCLUDE
        )

        install(DIRECTORY ${PDF4QT_QT_ROOT}/plugins/texttospeech/
            RUNTIME DESTINATION ${PDF4QT_INSTALL_LIB_DIR}/texttospeech/
            FILES_MATCHING
            PATTERN "*.dll"
            PATTERN "*.so"
            PATTERN "*.dylib"
            REGEX "d\\..*" EXCLUDE
            PATTERN "*.pdb" EXCLUDE
        )
    endif()

    if(WIN32 AND MSVC AND PDF4QT_INSTALL_MSVC_REDISTRIBUTABLE)
        if(DEFINED ENV{MSVC_REDISTRIBUTABLES_PATH})
            set(MSVC_REDISTRIBUTABLES_PATH $ENV{MSVC_REDISTRIBUTABLES_PATH})
            message(STATUS "Using existing MSVC Redistributable Package Path = ${MSVC_REDISTRIBUTABLES_PATH}")
        else()
            get_filename_component(MSVC_REDISTRIBUTABLES_PATH $ENV{VCToolsRedistDir}/$ENV{VSCMD_ARG_TGT_ARCH}/Microsoft.VC${MSVC_TOOLSET_VERSION}.CRT/ ABSOLUTE)
            message(STATUS "MSVC Redistributable Package Path = ${MSVC_REDISTRIBUTABLES_PATH}")
        endif()

        install(DIRECTORY ${MSVC_REDISTRIBUTABLES_PATH}/
            RUNTIME DESTINATION ${PDF4QT_INSTALL_LIB_DIR}/
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

