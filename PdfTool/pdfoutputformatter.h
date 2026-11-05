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

#ifndef PDFTOOLOUTPUTFORMATTER_H
#define PDFTOOLOUTPUTFORMATTER_H

// This header has the same name: the path says the formatter of the core
#include "../Pdf4QtLibCore/sources/pdfoutputformatter.h"

#include <QByteArray>
#include <QString>
#include <QStringConverter>

namespace pdftool
{

using pdf::PDFOutputFormatter;

class PDFConsole
{
public:

    /// Writes text to the console
    static void writeText(QString text, QStringConverter::Encoding encoding);

    /// Writes error to the console
    static void writeError(QString text, QStringConverter::Encoding encoding);

    /// Writes binary data to the console
    static void writeData(const QByteArray& data);

private:
    explicit PDFConsole() = delete;
};

}   // namespace pdftool

#endif // PDFTOOLOUTPUTFORMATTER_H
