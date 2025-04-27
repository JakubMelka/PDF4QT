// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef PDFWINTASKBARPROGRESS_H
#define PDFWINTASKBARPROGRESS_H

#include "pdfviewerglobal.h"
#include "pdfglobal.h"

#include <QObject>

class QWindow;

namespace pdfviewer
{

class PDFWinTaskBarProgressImpl;

/// Windows task bar progress. Displays progress
/// in the task bar icon in window's bottom side.
class PDFWinTaskBarProgress : public QObject
{
    Q_OBJECT

public:
    PDFWinTaskBarProgress(QObject* parent);
    virtual ~PDFWinTaskBarProgress() override;

    void setWindow(QWindow* window);

    void reset();
    void show();
    void hide();

    void setValue(pdf::PDFInteger value);
    void setRange(pdf::PDFInteger min, pdf::PDFInteger max);

private:
    PDFWinTaskBarProgressImpl* m_impl;
};

}   // namespace pdfviewer

#endif // PDFWINTASKBARPROGRESS_H
