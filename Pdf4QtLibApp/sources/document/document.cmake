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

# Sources of the document tab (steps 3 and 4 of phase 1, GUI_overhaul/newapplication.md 4):
# the model of a document, its view, the panels of the sidebar, the search, the
# file operations and the properties (step 3), the annotations, the outline and
# the forms (step 4). Paths are relative to the directory of Pdf4QtLibApp. Each
# package owns one of the included files.

# Commands of the document tab: the table command id -> package and the base of
# the command handlers (wave 0, shared)
set(PDF4QT_LIBAPP_DOCUMENT_SOURCES
    sources/document/pdfappdocumentcommands.cpp
    sources/document/pdfappdocumentcommands.h
)

# Step 4, wave 0 (shared): the controller of the annotations of a view (commit,
# merge sessions, delegated engine user interface, lock), the widget-free
# operations (their bodies are in the package files) and the floating bar
list(APPEND PDF4QT_LIBAPP_DOCUMENT_SOURCES
    sources/document/pdfappannotationcontroller.cpp
    sources/document/pdfappannotationcontroller.h
    sources/document/pdfappannotationoperations.h
    sources/document/pdfappfloatingbar.cpp
    sources/document/pdfappfloatingbar.h
)

# Package A - model, lifecycle, undo / redo, window integration
include(${CMAKE_CURRENT_LIST_DIR}/model.cmake)

# Package B - view, toolbar, panels File / View / Go to, Select menu, status bar
include(${CMAKE_CURRENT_LIST_DIR}/view.cmake)

# Package C - sidebar and the navigation panels
include(${CMAKE_CURRENT_LIST_DIR}/panels.cmake)

# Package D - search, text index, signatures, speech
include(${CMAKE_CURRENT_LIST_DIR}/search.cmake)

# Package E - save, print, e-mail, automatic refresh
include(${CMAKE_CURRENT_LIST_DIR}/file.cmake)

# Package F - properties, links and actions, rendering issues, developer view
include(${CMAKE_CURRENT_LIST_DIR}/properties.cmake)

# Step 4, package A - Annotate and Insert, creation of annotations
include(${CMAKE_CURRENT_LIST_DIR}/annotate.cmake)

# Step 4, package B - editing of the annotations on the page
include(${CMAKE_CURRENT_LIST_DIR}/annotationedit.cmake)

# Step 4, package C - Notes and Outline editing
include(${CMAKE_CURRENT_LIST_DIR}/notesoutline.cmake)

# Step 4, package D - form filling
include(${CMAKE_CURRENT_LIST_DIR}/forms.cmake)
