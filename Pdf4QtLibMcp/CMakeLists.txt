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

# The MCP server of the new application (step 7, plan GUI_overhaul step7):
# protocol and transport - the JSON layer, the schemas, the calls, the catalog,
# the HTTP / SSE transport, the dispatcher of both protocol eras. QtCore and
# QtNetwork only (no Gui, no Widgets); Pdf4QtLibCore only for
# PDFOperationControl (a call is the cancellation of the Core functions).
# The application side (host, tools, pages) is in Pdf4QtLibApp/sources/mcp.
# Not installed in phase 1.

set(PDF4QT_LIBMCP_SOURCES
    sources/pdfmcperrorcodes.h
    sources/pdfmcplimits.h
    sources/pdfmcpjson.cpp
    sources/pdfmcpjson.h
    sources/pdfmcpschema.cpp
    sources/pdfmcpschema.h
    sources/pdfmcpcall.cpp
    sources/pdfmcpcall.h
    sources/pdfmcpcatalog.cpp
    sources/pdfmcpcatalog.h
    sources/pdfmcpuritemplate.cpp
    sources/pdfmcpuritemplate.h
    sources/pdfmcphost.cpp
    sources/pdfmcphost.h
    sources/pdfmcprequeststate.cpp
    sources/pdfmcprequeststate.h
    sources/pdfmcpserver.cpp
    sources/pdfmcpserver.h
    sources/pdfmcplistener.cpp
    sources/pdfmcplistener.h
    sources/pdfmcpiothreads.cpp
    sources/pdfmcpiothreads.h
)

# Package A of step 7 (transport and protocol core)
list(APPEND PDF4QT_LIBMCP_SOURCES
    sources/pdfmcphttp.cpp
    sources/pdfmcphttp.h
    sources/pdfmcpconnection.cpp
    sources/pdfmcpconnection.h
    sources/pdfmcpsse.cpp
    sources/pdfmcpsse.h
    sources/pdfmcpdispatcher.cpp
    sources/pdfmcpdispatcher.h
    sources/pdfmcpdispatcher_p.h
    sources/pdfmcpdispatcher_calls.cpp
    sources/pdfmcpdispatcher_protocol.cpp
    sources/pdfmcpdispatcher_streams.cpp
    sources/pdfmcpsessions.cpp
    sources/pdfmcpsessions.h
    sources/pdfmcpassistants.cpp
    sources/pdfmcpassistants.h
    sources/pdfmcpsubscriptions.cpp
    sources/pdfmcpsubscriptions.h
    sources/pdfmcptasks.cpp
    sources/pdfmcptasks.h
    sources/pdfmcpratelimiter.cpp
    sources/pdfmcpratelimiter.h
)

add_library(Pdf4QtLibMcp SHARED
    ${PDF4QT_LIBMCP_SOURCES}
)

include(GenerateExportHeader)

GENERATE_EXPORT_HEADER(Pdf4QtLibMcp
                       EXPORT_MACRO_NAME
                       PDF4QTLIBMCPSHARED_EXPORT
                       EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR}/pdf4qtlibmcp_export.h")

target_link_libraries(Pdf4QtLibMcp PUBLIC Pdf4QtLibCore)
target_link_libraries(Pdf4QtLibMcp PUBLIC Qt6::Core Qt6::Network)

if(WIN32)
    # closesocket of a descriptor, which no connection took
    target_link_libraries(Pdf4QtLibMcp PRIVATE ws2_32)
endif()

target_include_directories(Pdf4QtLibMcp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/sources)
target_include_directories(Pdf4QtLibMcp PUBLIC ${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR})

set_target_properties(Pdf4QtLibMcp PROPERTIES
                      VERSION ${PDF4QT_VERSION}
                      SOVERSION ${PDF4QT_VERSION}
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR})

# No install rules: the library is used by the new application, which is started
# from the build tree in phase 1
