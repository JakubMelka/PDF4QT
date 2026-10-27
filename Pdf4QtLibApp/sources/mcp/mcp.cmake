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

# The MCP server of the application (step 7): the controller, the host, the
# contract of the tools and the AI page. Each package adds its files to its own
# list below (small edits only).

# Wave 0: the controller, the host, the contract of the tools, the bridge of the
# tools of the catalog, the permissions, the activity, the clients, the
# arguments, the documents and the jobs
set(PDF4QT_LIBAPP_MCP_SOURCES
    sources/mcp/pdfappmcpactivity.cpp
    sources/mcp/pdfappmcpactivity.h
    sources/mcp/pdfappmcparguments.cpp
    sources/mcp/pdfappmcparguments.h
    sources/mcp/pdfappmcpcatalogtools.cpp
    sources/mcp/pdfappmcpcatalogtools.h
    sources/mcp/pdfappmcpclientnames.cpp
    sources/mcp/pdfappmcpclientnames.h
    sources/mcp/pdfappmcpconfirmation.cpp
    sources/mcp/pdfappmcpconfirmation.h
    sources/mcp/pdfappmcpcontroller.cpp
    sources/mcp/pdfappmcpcontroller.h
    sources/mcp/pdfappmcpdocumentresolver.cpp
    sources/mcp/pdfappmcpdocumentresolver.h
    sources/mcp/pdfappmcphost.cpp
    sources/mcp/pdfappmcphost.h
    sources/mcp/pdfappmcpjob.cpp
    sources/mcp/pdfappmcpjob.h
    sources/mcp/pdfappmcppermissions.cpp
    sources/mcp/pdfappmcppermissions.h
    sources/mcp/pdfappmcptoolhandler.cpp
    sources/mcp/pdfappmcptoolhandler.h
    sources/mcp/pdfappmcptools.cpp
    sources/mcp/pdfappmcptools.h
    sources/mcp/pdfappmcptoolset.cpp
    sources/mcp/pdfappmcptoolset.h
)

# Package B: the read tools, the document resources, prompts at run time, completion
list(APPEND PDF4QT_LIBAPP_MCP_SOURCES
    sources/mcp/pdfappmcpareahighlight.cpp
    sources/mcp/pdfappmcpareahighlight.h
    sources/mcp/pdfappmcpcompletion.cpp
    sources/mcp/pdfappmcpcompletion.h
    sources/mcp/pdfappmcpdocumentreader.cpp
    sources/mcp/pdfappmcpdocumentreader.h
    sources/mcp/pdfappmcpdocumentresources.cpp
    sources/mcp/pdfappmcpdocumentresources.h
    sources/mcp/pdfappmcpelicitation.cpp
    sources/mcp/pdfappmcpelicitation.h
    sources/mcp/pdfappmcppromptruntime.cpp
    sources/mcp/pdfappmcppromptruntime.h
    sources/mcp/pdfappmcpreadsupport.cpp
    sources/mcp/pdfappmcpreadsupport.h
    sources/mcp/pdfappmcprenderer.cpp
    sources/mcp/pdfappmcprenderer.h
    sources/mcp/pdfappmcptextservice.cpp
    sources/mcp/pdfappmcptextservice.h
    sources/mcp/pdfappmcptool_application.cpp
    sources/mcp/pdfappmcptool_application.h
    sources/mcp/pdfappmcptool_document.cpp
    sources/mcp/pdfappmcptool_document.h
    sources/mcp/pdfappmcptool_render.cpp
    sources/mcp/pdfappmcptool_render.h
    sources/mcp/pdfappmcptool_text.cpp
    sources/mcp/pdfappmcptool_text.h
    sources/mcp/pdfappmcptool_view.cpp
    sources/mcp/pdfappmcptool_view.h
)

# Package C: the edit tools, the confirmations, the preview, the bars
list(APPEND PDF4QT_LIBAPP_MCP_SOURCES
    sources/mcp/pdfappmcpchange.cpp
    sources/mcp/pdfappmcpchange.h
    sources/mcp/pdfappmcpconfirmationui.cpp
    sources/mcp/pdfappmcpconfirmationui.h
    sources/mcp/pdfappmcpconfirmdialog.cpp
    sources/mcp/pdfappmcpconfirmdialog.h
    sources/mcp/pdfappmcppreview.cpp
    sources/mcp/pdfappmcppreview.h
    sources/mcp/pdfappmcptool_annotations.cpp
    sources/mcp/pdfappmcptool_annotations.h
    sources/mcp/pdfappmcptool_files.cpp
    sources/mcp/pdfappmcptool_files.h
    sources/mcp/pdfappmcptool_forms.cpp
    sources/mcp/pdfappmcptool_forms.h
    sources/mcp/pdfappmcptool_outline.cpp
    sources/mcp/pdfappmcptool_outline.h
)

# Package D: the static resources, the prompt definitions, the setup texts
list(APPEND PDF4QT_LIBAPP_MCP_SOURCES
    sources/mcp/pdfappmcpclientsetup.cpp
    sources/mcp/pdfappmcpclientsetup.h
    sources/mcp/pdfappmcpcontent.cpp
    sources/mcp/pdfappmcpcontent.h
    sources/mcp/pdfappmcpprompts.cpp
    sources/mcp/pdfappmcpprompts.h
    sources/mcp/pdfappmcpresources.cpp
    sources/mcp/pdfappmcpresources.h
    resources/mcp.qrc
)

# Package E: the AI page and its cards and dialogs
list(APPEND PDF4QT_LIBAPP_MCP_SOURCES
    sources/mcp/pdfappaipage.cpp
    sources/mcp/pdfappaipage.h
)
