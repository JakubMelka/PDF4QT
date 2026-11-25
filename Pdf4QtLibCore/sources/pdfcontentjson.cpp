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

// Edit Content, stage E0. This file belongs to package P05 (document writer, tool, JSON). The
// bodies below are stubs of the skeleton PE0SKEL: each one ends the process with the name of its
// function (qFatal), until the package implements it.

#include "pdfcontentjson.h"

#include <QtGlobal>

#include <cstdlib>  // std::abort after qFatal: Qt does not declare qFatal noreturn for MSVC (error C4716)

namespace pdf
{

std::optional<PDFContentScript> PDFContentJson::readScript(const QString& text, std::vector<QString>* errors)
{
    Q_UNUSED(text);
    Q_UNUSED(errors);
    qFatal("PDFContentJson::readScript is not implemented (stage E0 skeleton, package P05)");
    std::abort();
}

QString PDFContentJson::writeScript(const PDFContentScript& script)
{
    Q_UNUSED(script);
    qFatal("PDFContentJson::writeScript is not implemented (stage E0 skeleton, package P05)");
    std::abort();
}

QString PDFContentJson::writeNodeId(PDFInteger pageIndex, const std::vector<PDFContentNodeId>& path)
{
    Q_UNUSED(pageIndex);
    Q_UNUSED(path);
    qFatal("PDFContentJson::writeNodeId is not implemented (stage E0 skeleton, package P05)");
    std::abort();
}

PDFContentNodeReference PDFContentJson::readNodeId(const QString& text, const PDFContentEditSession* session, QString* errorText)
{
    Q_UNUSED(text);
    Q_UNUSED(session);
    Q_UNUSED(errorText);
    qFatal("PDFContentJson::readNodeId is not implemented (stage E0 skeleton, package P05)");
    std::abort();
}

QJsonArray PDFContentJson::writeMatrix(const QTransform& matrix)
{
    Q_UNUSED(matrix);
    qFatal("PDFContentJson::writeMatrix is not implemented (stage E0 skeleton, package P05)");
    std::abort();
}

std::optional<QTransform> PDFContentJson::readMatrix(const QJsonValue& value)
{
    Q_UNUSED(value);
    qFatal("PDFContentJson::readMatrix is not implemented (stage E0 skeleton, package P05)");
    std::abort();
}

QJsonObject PDFContentJson::writeRect(const QRectF& rect)
{
    Q_UNUSED(rect);
    qFatal("PDFContentJson::writeRect is not implemented (stage E0 skeleton, package P05)");
    std::abort();
}

std::optional<QRectF> PDFContentJson::readRect(const QJsonValue& value)
{
    Q_UNUSED(value);
    qFatal("PDFContentJson::readRect is not implemented (stage E0 skeleton, package P05)");
    std::abort();
}

QJsonValue PDFContentJson::writeColor(const PDFContentColorValue& color)
{
    Q_UNUSED(color);
    qFatal("PDFContentJson::writeColor is not implemented (stage E0 skeleton, package P05)");
    std::abort();
}

std::optional<PDFContentColorValue> PDFContentJson::readColor(const QJsonValue& value, QString* errorText)
{
    Q_UNUSED(value);
    Q_UNUSED(errorText);
    qFatal("PDFContentJson::readColor is not implemented (stage E0 skeleton, package P05)");
    std::abort();
}

QString PDFContentJson::writeNodeKind(PDFContentNodeKind kind)
{
    Q_UNUSED(kind);
    qFatal("PDFContentJson::writeNodeKind is not implemented (stage E0 skeleton, package P05)");
    std::abort();
}

QJsonObject PDFContentJson::writePage(const PDFContentEditSession* session, PDFInteger pageIndex, const ListOptions& options)
{
    Q_UNUSED(session);
    Q_UNUSED(pageIndex);
    Q_UNUSED(options);
    qFatal("PDFContentJson::writePage is not implemented (stage E0 skeleton, package P05)");
    std::abort();
}

}   // namespace pdf
