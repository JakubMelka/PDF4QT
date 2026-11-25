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

#ifndef PDFCONTENTSTREAMWRITER_H
#define PDFCONTENTSTREAMWRITER_H

#include "pdfcontentnode.h"

#include <QByteArray>
#include <QString>

#include <vector>

namespace pdf
{

// Edit Content, stage E0, package P01. Plan: zadani_jadro.md 4.4 (base rule, R-1 wrap, R-7 joints,
// canonical writing through PDFContentStreamText).

// Threads and ownership of the plain types of this header (PDFContentWriteError,
// PDFContentStreamWriteResult): values owned by the caller of the writer; the objects of a result
// are PDFObject values and point to nothing of the container or of the document.

/// An error of a node, which cannot be written (a value, which is not finite, a glyph without a
/// font): the container is not written at all then
struct PDFContentWriteError
{
    PDFContentContainerKey container;   ///< The container of the node: filled by the document writer, not valid in a result of the stream writer
    PDFContentNodeId nodeId;
    QString text;   ///< Translated
};

/// The bytes of a container and what they need
struct PDFContentStreamWriteResult
{
    QByteArray content;                         ///< The content stream (not compressed); empty, when there are errors
    PDFContentResources resources;              ///< The resources of the container with the names added for the written overrides: the ExtGState dictionaries of the editor as direct dictionaries (a resource value may be direct), the objects of the overrides (pattern copies) by their references
    std::vector<PDFContentWriteError> errors;

    bool isSuccess() const { return errors.empty(); }
};

/// Writes a container as the bytes of one content stream, in the order of its tree.
///
/// - A node, which is not modified, has no override and whose operations are not suppressed, is the
///   concatenation of the raw bytes of its operations. A container, which was not changed at all,
///   gives its original content byte for byte (PDFContentOperationList::getOriginalContent).
/// - An override is the wrap q, cm, properties, the node, Q (R-1); never around a node with a
///   clipping effect. Properties without an operator are one ExtGState with exactly their keys
///   (PDFContentExtGStateWriter); a soft mask by R-6.
/// - The writer has no document and creates no indirect object. R-5 and R-6 arrive as ready
///   values of the override: the copy of a pattern with its new /Matrix is made by the command
///   (PDFContentColorValue::patternObject, a provisional reference of PDFContentEditSession::
///   addNewObject), the soft mask and its matrices are values of the override too. The writer
///   only names them in the resources and writes the operators.
/// - Suppressed operations are not written.
/// - Between two written operations, which were not neighbors in the original content, one line
///   feed is written, whenever two regular characters would meet (R-7,
///   PDFContentStreamText::isSeparatorNeeded); original neighbors are joined unchanged. A missing
///   Q, ET or EMC is not added.
/// - Modified and generated nodes are written canonically (PDFContentStreamText), one operator a line.
///
/// Threads and ownership: static and reentrant; reads the immutable container only.
class PDF4QTLIBCORESHARED_EXPORT PDFContentStreamWriter
{
public:
    PDFContentStreamWriter() = delete;

    /// Writes the container
    static PDFContentStreamWriteResult write(const PDFContentContainer& container);

    /// The canonical operators of a path (re for a rectangle of PDFContentPath::isRectangle, else
    /// m l c h), one operator a line, without the painting operator
    /// \param path The path
    /// \param errorText A coordinate, which is not finite
    /// \returns the bytes, empty with \p errorText set on an error
    static QByteArray writePath(const PDFContentPath& path, QString* errorText);

    /// The painting operator of a path with its clipping operator before it ("W n", "f", "B*", ...)
    static QByteArray writePathPaint(const PDFContentPathPaint& paint);

    /// "a b c d e f cm" of a matrix; empty with \p errorText set, when a value is not finite
    static QByteArray writeMatrix(const QTransform& matrix, QString* errorText);
};

}   // namespace pdf

#endif // PDFCONTENTSTREAMWRITER_H
