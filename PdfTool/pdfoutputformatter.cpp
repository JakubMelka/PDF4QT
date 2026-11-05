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

#include "pdfoutputformatter.h"

#include <QMutex>
#include <QTextStream>
#include <QStringEncoder>

#ifdef Q_OS_WIN
#include "Windows.h"
#endif

namespace pdftool
{

void PDFConsole::writeText(QString text, QStringConverter::Encoding encoding)
{
#ifdef Q_OS_WIN
    HANDLE outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!WriteConsoleW(outputHandle, text.utf16(), text.size(), nullptr, nullptr))
    {
        // Write console failed. This can happen only, if outputHandle is not handle
        // to console screen buffer, but, for example a file or a pipe.

        QStringEncoder encoder(encoding);
        QByteArray encodedData = encoder.encode(text);
        WriteFile(outputHandle, encodedData.constData(), encodedData.size(), nullptr, nullptr);
    }
#else
    QTextStream(stdout) << text;
#endif
}

QMutex s_writeErrorMutex;

void PDFConsole::writeError(QString text, QStringConverter::Encoding encoding)
{
    if (text.isEmpty())
    {
        return;
    }

    QMutexLocker lock(&s_writeErrorMutex);

    text += "\n";

#ifdef Q_OS_WIN
    HANDLE outputHandle = GetStdHandle(STD_ERROR_HANDLE);
    if (!WriteConsoleW(outputHandle, text.utf16(), text.size(), nullptr, nullptr))
    {
        // Write console failed. This can happen only, if outputHandle is not handle
        // to console screen buffer, but, for example a file or a pipe.

        QStringEncoder encoder(encoding);
        QByteArray encodedData = encoder.encode(text);
        WriteFile(outputHandle, encodedData.constData(), encodedData.size(), nullptr, nullptr);
    }
#else
    QTextStream stream(stdout);
    stream << text;
    stream << Qt::endl;
#endif
}

void PDFConsole::writeData(const QByteArray& data)
{
    if (!data.isEmpty())
    {
        QTextStream stream(stdout);
        stream.device()->write(data);
    }
}

}   // pdftool
