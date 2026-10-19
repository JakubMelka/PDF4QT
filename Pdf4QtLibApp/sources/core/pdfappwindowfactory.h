// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef PDFAPPWINDOWFACTORY_H
#define PDFAPPWINDOWFACTORY_H

#include "pdfappglobal.h"

class QWidget;

namespace pdf
{

class PDFAppContext;

/// Creation of the main window - the one call of main into the shell. The window
/// opens the files of the command line of the context and connects
/// PDFAppContext::openRequested (files of other processes as new tabs, raise and
/// activate the window). It creates PDFAppMainWindow (sources/shell).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppWindowFactory
{
public:
    PDFAppWindowFactory() = delete;

    /// Creates the main window (not shown). The caller owns it; \p context must
    /// outlive the window.
    static QWidget* createMainWindow(PDFAppContext* context);
};

}   // namespace pdf

#endif // PDFAPPWINDOWFACTORY_H
