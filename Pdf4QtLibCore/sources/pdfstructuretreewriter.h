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

#ifndef PDFSTRUCTURETREEWRITER_H
#define PDFSTRUCTURETREEWRITER_H

#include "pdfcontentstructure.h"

#include <QString>

#include <vector>

namespace pdf
{
class PDFDocumentBuilder;

// Edit Content, stage E0 (base) and E5 (extension by P51), package P04. Plan: zadani_jadro.md 4.12
// (writing).

/// The result of writing the structure. Threads and ownership: a value owned by the caller.
struct PDFStructureTreeWriteResult
{
    bool isWritten = false;         ///< Something was written (false for an unmodified model: nothing is touched)
    std::vector<QString> errors;    ///< Translated; with an error nothing was written
};

/// Writes the changes of a structure model into the document being built.
///
/// - Only modified elements are written (/K, /P and the text keys; the other keys as they were
///   read); new elements become new objects.
/// - When an item of the parent tree changed, the whole /ParentTree is built again from the model
///   by PDFNumberTreeWriter, the foreign items included unchanged and the nulls of the arrays
///   kept at their indices; /ParentTreeNextKey is written.
/// - /RoleMap, /ClassMap and the namespaces are not changed; /IDTree only loses entries.
///
/// Threads and ownership: static and reentrant; writes into the builder only.
class PDF4QTLIBCORESHARED_EXPORT PDFStructureTreeWriter
{
public:
    PDFStructureTreeWriter() = delete;

    /// Writes the model.
    /// \param builder Builder of the document, from which the model was read (with the changes
    ///        of the content already in it)
    /// \param model The model
    static PDFStructureTreeWriteResult write(PDFDocumentBuilder* builder, const PDFContentStructureModel& model);
};

}   // namespace pdf

#endif // PDFSTRUCTURETREEWRITER_H
