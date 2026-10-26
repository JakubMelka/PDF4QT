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

# The Tools catalog of the new application (step 6, plan step6_app_plan.md 4):
# the shared part of wave 0 (presentation, state, built-in collections and their
# merge, the registry of the tool pages) and the files of the packages of wave 1
# (wave 0 stubs, which the packages replace). Paths are relative to the directory
# of Pdf4QtLibApp.

set(PDF4QT_LIBAPP_TOOLS_SOURCES
    sources/tools/pdfappbuiltintoolcollections.cpp
    sources/tools/pdfappbuiltintoolcollections.h
    sources/tools/pdfapptoolcollections.cpp
    sources/tools/pdfapptoolcollections.h
    sources/tools/pdfapptoolpageregistry.cpp
    sources/tools/pdfapptoolpageregistry.h
    sources/tools/pdfapptoolpresentation.cpp
    sources/tools/pdfapptoolpresentation.h
    sources/tools/pdfapptoolstate.cpp
    sources/tools/pdfapptoolstate.h
)

# Files of the packages of wave 1: each package adds its own files below its line
list(APPEND PDF4QT_LIBAPP_TOOLS_SOURCES
    # Package A - the Tools page
    sources/tools/pdfapptoolspage.cpp
    sources/tools/pdfapptoolspage.h
    sources/tools/pdfapptoolcategorycard.cpp
    sources/tools/pdfapptoolcategorycard.h
    sources/tools/pdfapptoolmenu.cpp
    sources/tools/pdfapptoolmenu.h
    # Package B - the collections page
    sources/tools/pdfapptoolcollectionspage.cpp
    sources/tools/pdfapptoolcollectionspage.h
    sources/tools/pdfapptoolcollectioneditor.cpp
    sources/tools/pdfapptoolcollectioneditor.h
    sources/tools/pdfappcollectiontoollist.cpp
    sources/tools/pdfappcollectiontoollist.h
    sources/tools/pdfappcollectioniconchooser.cpp
    sources/tools/pdfappcollectioniconchooser.h
    # Package C - the Tools panel of the document
    sources/tools/pdfappdocumenttoolspanel.cpp
    sources/tools/pdfappdocumenttoolspanel.h
)
