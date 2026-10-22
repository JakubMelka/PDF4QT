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

# Package C of step 3: the sidebar, the base and the registry of its panels, the
# navigation panels and their commands

list(APPEND PDF4QT_LIBAPP_DOCUMENT_SOURCES
    sources/document/pdfappdocumentpanel.cpp
    sources/document/pdfappdocumentpanel.h
    sources/document/pdfappdocumentpanelcommands.cpp
    sources/document/pdfappdocumentpanelcommands.h
    sources/document/pdfappdocumentpanels_attachments.cpp
    sources/document/pdfappdocumentpanels_attachments.h
    sources/document/pdfappdocumentpanels_bookmarks.cpp
    sources/document/pdfappdocumentpanels_bookmarks.h
    sources/document/pdfappdocumentpanels_common.cpp
    sources/document/pdfappdocumentpanels_common.h
    sources/document/pdfappdocumentpanels_layers.cpp
    sources/document/pdfappdocumentpanels_layers.h
    sources/document/pdfappdocumentpanels_notes.cpp
    sources/document/pdfappdocumentpanels_notes.h
    sources/document/pdfappdocumentpanels_outline.cpp
    sources/document/pdfappdocumentpanels_outline.h
    sources/document/pdfappdocumentpanels_thumbnails.cpp
    sources/document/pdfappdocumentpanels_thumbnails.h
    sources/document/pdfappdocumentsidebar.cpp
    sources/document/pdfappdocumentsidebar.h
    sources/storage/pdfbookmarkrepository.cpp
    sources/storage/pdfbookmarkrepository.h
)
