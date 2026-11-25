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

// Edit Content, stage E0. This file belongs to package P03 (session, undo, basic commands). The
// bodies below are stubs of the skeleton PE0SKEL: each one ends the process with the name of its
// function (qFatal), until the package implements it - the registry of the JSON types
// (registerType, getTypes) included. Real code of the skeleton: the destructor of
// PDFContentCommand and its default mergeWith (commands are not merged).

#include "pdfcontentcommands.h"

#include <QtGlobal>

#include <cstdlib>  // std::abort after qFatal: Qt does not declare qFatal noreturn for MSVC (error C4716)

namespace pdf
{

PDFContentCommand::~PDFContentCommand() = default;

bool PDFContentCommand::mergeWith(const PDFContentCommand& next)
{
    Q_UNUSED(next);
    return false;
}

PDFContentCommandPointer PDFContentCommandFactory::createDelete(std::vector<PDFContentNodeReference> nodes)
{
    Q_UNUSED(nodes);
    qFatal("PDFContentCommandFactory::createDelete is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFContentCommandPointer PDFContentCommandFactory::createTransform(std::vector<PDFContentNodeReference> nodes, const QTransform& matrix)
{
    Q_UNUSED(nodes);
    Q_UNUSED(matrix);
    qFatal("PDFContentCommandFactory::createTransform is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFContentCommandPointer PDFContentCommandFactory::createReorder(std::vector<PDFContentNodeReference> nodes, PDFContentNodeReference parent, PDFInteger index, ReorderMode mode)
{
    Q_UNUSED(nodes);
    Q_UNUSED(parent);
    Q_UNUSED(index);
    Q_UNUSED(mode);
    qFatal("PDFContentCommandFactory::createReorder is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFContentCommandPointer PDFContentCommandFactory::createCopy(std::vector<PDFContentNodeReference> nodes, PDFContentNodeReference parent, PDFInteger index, const QTransform& matrix)
{
    Q_UNUSED(nodes);
    Q_UNUSED(parent);
    Q_UNUSED(index);
    Q_UNUSED(matrix);
    qFatal("PDFContentCommandFactory::createCopy is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFContentCommandPointer PDFContentCommandFactory::createSetAppearance(std::vector<PDFContentNodeReference> nodes, std::map<PDFContentStateProperty, PDFContentStateValue> properties)
{
    Q_UNUSED(nodes);
    Q_UNUSED(properties);
    qFatal("PDFContentCommandFactory::createSetAppearance is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFContentCommandPointer PDFContentCommandFactory::createSetTransparency(std::vector<PDFContentNodeReference> nodes, std::optional<PDFReal> alphaStroking, std::optional<PDFReal> alphaFilling, std::optional<QByteArray> blendMode)
{
    Q_UNUSED(nodes);
    Q_UNUSED(alphaStroking);
    Q_UNUSED(alphaFilling);
    Q_UNUSED(blendMode);
    qFatal("PDFContentCommandFactory::createSetTransparency is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFContentCommandPointer PDFContentCommandFactory::createGroup(std::vector<PDFContentNodeReference> nodes)
{
    Q_UNUSED(nodes);
    qFatal("PDFContentCommandFactory::createGroup is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFContentCommandPointer PDFContentCommandFactory::createUngroup(PDFContentNodeReference group)
{
    Q_UNUSED(group);
    qFatal("PDFContentCommandFactory::createUngroup is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFContentCommandPointer PDFContentCommandFactory::createClearPage(PDFContentContainerKey page)
{
    Q_UNUSED(page);
    qFatal("PDFContentCommandFactory::createClearPage is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFContentCommandPointer PDFContentCommandFactory::fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
{
    Q_UNUSED(object);
    Q_UNUSED(session);
    Q_UNUSED(path);
    Q_UNUSED(errors);
    qFatal("PDFContentCommandFactory::fromJson is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

bool PDFContentCommandFactory::registerType(const char* type, Creator creator)
{
    Q_UNUSED(type);
    Q_UNUSED(creator);
    qFatal("PDFContentCommandFactory::registerType is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

std::vector<QByteArray> PDFContentCommandFactory::getTypes()
{
    qFatal("PDFContentCommandFactory::getTypes is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

}   // namespace pdf
