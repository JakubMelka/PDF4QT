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

# Package A of step 4: the Annotate and Insert panels, the tool bars, the markup
# bar, the creation flows, Mark as annotations and their commands

list(APPEND PDF4QT_LIBAPP_DOCUMENT_SOURCES
    sources/document/pdfappannotatecommands.cpp
    sources/document/pdfappannotatecommands.h
    sources/document/pdfappannotatepanels.cpp
    sources/document/pdfappannotatepanels.h
    sources/document/pdfappannotationoperations_create.cpp
    sources/document/pdfappannotationcreatedialogs.cpp
    sources/document/pdfappannotationcreatedialogs.h
    sources/document/pdfappannotationcreatetools.cpp
    sources/document/pdfappannotationcreatetools.h
    sources/document/pdfappannotationcreator.cpp
    sources/document/pdfappannotationcreator.h
    sources/document/pdfappannotationtoolbar.cpp
    sources/document/pdfappannotationtoolbar.h
    sources/document/pdfappmarkresults.cpp
    sources/document/pdfappmarkresults.h
    sources/document/pdfappmarkupbar.cpp
    sources/document/pdfappmarkupbar.h
)
