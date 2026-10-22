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

# Package A of step 3: the model of a document (PDFAppDocument, undo stack,
# loader, registry), the document tab and the route adapter /document/{id}

list(APPEND PDF4QT_LIBAPP_DOCUMENT_SOURCES
    sources/document/pdfappdocument.cpp
    sources/document/pdfappdocument.h
    sources/document/pdfappdocumentdialogs.cpp
    sources/document/pdfappdocumentdialogs.h
    sources/document/pdfappdocumentloader.cpp
    sources/document/pdfappdocumentloader.h
    sources/document/pdfappdocumentmodelcommands.cpp
    sources/document/pdfappdocumentmodelcommands.h
    sources/document/pdfappdocumentpage.cpp
    sources/document/pdfappdocumentpage.h
    sources/document/pdfappdocumentregistry.cpp
    sources/document/pdfappdocumentregistry.h
    sources/document/pdfappdocumenttab.cpp
    sources/document/pdfappdocumenttab.h
    sources/document/pdfappundostack.cpp
    sources/document/pdfappundostack.h
)
