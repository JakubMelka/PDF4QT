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

# Library of the new application PDF4QT (GUI_overhaul/newapplication.md): the
# storage (SQLite in WAL, sources/storage), the core of the application (setting
# definitions, command registry, command line, forwarding to a running instance,
# logs, application context - sources/core) and the window shell with the router
# and the pages (sources/shell). The library is not installed yet (decision 13 of
# the plan: installation after phase 2).

find_package(unofficial-sqlite3 CONFIG REQUIRED)

set(PDF4QT_LIBAPP_STORAGE_SOURCES
    sources/storage/pdfdatabase.cpp
    sources/storage/pdfdatabase.h
    sources/storage/pdfdatabasenotifier.cpp
    sources/storage/pdfdatabasenotifier.h
    sources/storage/pdfdatabaseschema.cpp
    sources/storage/pdfdatabaseschema.h
    sources/storage/pdfdatabasestatement.cpp
    sources/storage/pdfdatabasestatement.h
    sources/storage/pdfrecentrepository.cpp
    sources/storage/pdfrecentrepository.h
    sources/storage/pdfrepositoryhelper.cpp
    sources/storage/pdfrepositoryhelper.h
    sources/storage/pdfsettingsrepository.cpp
    sources/storage/pdfsettingsrepository.h
    sources/storage/pdfshortcutrepository.cpp
    sources/storage/pdfshortcutrepository.h
    sources/storage/pdftoolrepository.cpp
    sources/storage/pdftoolrepository.h
    sources/storage/pdfworkspacerepository.cpp
    sources/storage/pdfworkspacerepository.h
)

set(PDF4QT_LIBAPP_CORE_SOURCES
    sources/core/pdfappcommand.cpp
    sources/core/pdfappcommand.h
    sources/core/pdfappcommandline.cpp
    sources/core/pdfappcommandline.h
    sources/core/pdfappcontext.cpp
    sources/core/pdfappcontext.h
    sources/core/pdfappfolderinfo.cpp
    sources/core/pdfappfolderinfo.h
    sources/core/pdfappinstance.cpp
    sources/core/pdfappinstance.h
    sources/core/pdfapplog.cpp
    sources/core/pdfapplog.h
    sources/core/pdfapppaths.cpp
    sources/core/pdfapppaths.h
    sources/core/pdfappthumbnailer.cpp
    sources/core/pdfappthumbnailer.h
    sources/core/pdfappwindowfactory.cpp
    sources/core/pdfappwindowfactory.h
    sources/core/pdfsettingdefinition.cpp
    sources/core/pdfsettingdefinition.h
    sources/core/pdfsettingdefinitions.cpp
    sources/core/pdfsettingdefinitions.h
)

# What's New, the update check, the Markdown parser and the catalog of the manual (step 2)
list(APPEND PDF4QT_LIBAPP_CORE_SOURCES
    sources/core/pdfapphelpcatalog.cpp
    sources/core/pdfapphelpcatalog.h
    sources/core/pdfappmarkdown.cpp
    sources/core/pdfappmarkdown.h
    sources/core/pdfupdatechecker.cpp
    sources/core/pdfupdatechecker.h
    sources/core/pdfwhatsnew.cpp
    sources/core/pdfwhatsnew.h
)

# The window shell, the router and the pages add their files to
# PDF4QT_LIBAPP_SHELL_SOURCES in this file
include(${CMAKE_CURRENT_SOURCE_DIR}/sources/shell/shell.cmake)

# The pages of the routes (Home, Open, Recent, What's New, ...) add their files to
# PDF4QT_LIBAPP_PAGES_SOURCES in this file
include(${CMAKE_CURRENT_SOURCE_DIR}/sources/pages/pages.cmake)

add_library(Pdf4QtLibApp SHARED
    sources/pdfappglobal.h
    ${PDF4QT_LIBAPP_STORAGE_SOURCES}
    ${PDF4QT_LIBAPP_CORE_SOURCES}
    ${PDF4QT_LIBAPP_SHELL_SOURCES}
    ${PDF4QT_LIBAPP_PAGES_SOURCES}
)

include(GenerateExportHeader)

GENERATE_EXPORT_HEADER(Pdf4QtLibApp
                       EXPORT_MACRO_NAME
                       PDF4QTLIBAPPSHARED_EXPORT
                       EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR}/pdf4qtlibapp_export.h")

target_link_libraries(Pdf4QtLibApp PRIVATE Pdf4QtLibCore Pdf4QtLibWidgets Pdf4QtLibFluent)
target_link_libraries(Pdf4QtLibApp PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Svg)
target_link_libraries(Pdf4QtLibApp PRIVATE unofficial::sqlite3::sqlite3)

if(WIN32)
    # GetUserNameW, tokens of the processes (owner of the endpoint of the running
    # instance), AllowSetForegroundWindow, auto-hide taskbar of the maximized window
    target_link_libraries(Pdf4QtLibApp PRIVATE advapi32 user32 shell32)
endif()

if(LINUX_GCC)
    target_link_libraries(Pdf4QtLibApp PUBLIC TBB::tbb)
endif()

target_include_directories(Pdf4QtLibApp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/sources
    ${CMAKE_CURRENT_SOURCE_DIR}/sources/storage
    ${CMAKE_CURRENT_SOURCE_DIR}/sources/core
    ${CMAKE_CURRENT_SOURCE_DIR}/sources/shell
    ${CMAKE_CURRENT_SOURCE_DIR}/sources/pages)
target_include_directories(Pdf4QtLibApp PUBLIC ${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR})

set_target_properties(Pdf4QtLibApp PROPERTIES
                      VERSION ${PDF4QT_VERSION}
                      SOVERSION ${PDF4QT_VERSION}
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR})

# No install rules: the application is started from the build tree in phase 1
