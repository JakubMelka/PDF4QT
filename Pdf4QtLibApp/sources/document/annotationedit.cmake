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

# Package B of step 4: editing of the annotations on the page - the annotation
# bar, the menus, the properties / geometry / copy to pages dialogs, the comment
# popup and their commands

list(APPEND PDF4QT_LIBAPP_DOCUMENT_SOURCES
    sources/document/pdfappannotationbar.cpp
    sources/document/pdfappannotationbar.h
    sources/document/pdfappannotationeditcommands.cpp
    sources/document/pdfappannotationeditcommands.h
    sources/document/pdfappannotationgeometrydialog.cpp
    sources/document/pdfappannotationgeometrydialog.h
    sources/document/pdfappannotationinfo.cpp
    sources/document/pdfappannotationinfo.h
    sources/document/pdfappannotationmenus.cpp
    sources/document/pdfappannotationmenus.h
    sources/document/pdfappannotationoperations_edit.cpp
    sources/document/pdfappannotationpropertiesdialog.cpp
    sources/document/pdfappannotationpropertiesdialog.h
    sources/document/pdfappcommentpopup.cpp
    sources/document/pdfappcommentpopup.h
    sources/document/pdfappcopytopagesdialog.cpp
    sources/document/pdfappcopytopagesdialog.h
)
