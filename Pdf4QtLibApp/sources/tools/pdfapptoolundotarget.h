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

#ifndef PDFAPPTOOLUNDOTARGET_H
#define PDFAPPTOOLUNDOTARGET_H

#include "pdfappglobal.h"

#include <QObject>

namespace pdf
{

/// What Undo and Redo act on while a tool is open in a document tab: the commands
/// and their keys go to this target, never to the document (owner decision of
/// 2026-11-01). The view holds it for the time of the tool mode and does not own it;
/// PDFAppToolParameterModel is the implementation of the application.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolUndoTarget : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;
    virtual void undo() = 0;
    virtual void redo() = 0;

Q_SIGNALS:
    /// The result of canUndo or canRedo may have changed
    void undoStateChanged();
};

}   // namespace pdf

#endif // PDFAPPTOOLUNDOTARGET_H
