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

# Sources of the window shell of the new application: the window without the
# frame of the system, tabs, router (PDFAppNavigator, PDFAppRouteTable) and the
# pages. Paths are relative to the directory of Pdf4QtLibApp.

set(PDF4QT_LIBAPP_SHELL_SOURCES
    sources/shell/pdfappcolorpicker.cpp
    sources/shell/pdfappcolorpicker.h
    sources/shell/pdfapphistory.cpp
    sources/shell/pdfapphistory.h
    sources/shell/pdfappmainwindow.cpp
    sources/shell/pdfappmainwindow.h
    sources/shell/pdfappnavigator.cpp
    sources/shell/pdfappnavigator.h
    sources/shell/pdfapppage.cpp
    sources/shell/pdfapppage.h
    sources/shell/pdfapppagenotfound.cpp
    sources/shell/pdfapppagenotfound.h
    sources/shell/pdfapproute.cpp
    sources/shell/pdfapproute.h
    sources/shell/pdfapproutetable.cpp
    sources/shell/pdfapproutetable.h
    sources/shell/pdfappsessionend.cpp
    sources/shell/pdfappsessionend.h
    sources/shell/pdfapptab.cpp
    sources/shell/pdfapptab.h
    sources/shell/pdfapptopbar.cpp
    sources/shell/pdfapptopbar.h
    sources/shell/pdfappwindowframe.cpp
    sources/shell/pdfappwindowframe.h
    sources/shell/pdfappwindowgeometry.cpp
    sources/shell/pdfappwindowgeometry.h
    sources/shell/pdfappworkspace.cpp
    sources/shell/pdfappworkspace.h
)
