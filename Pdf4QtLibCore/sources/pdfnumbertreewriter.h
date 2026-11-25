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

#ifndef PDFNUMBERTREEWRITER_H
#define PDFNUMBERTREEWRITER_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <map>

namespace pdf
{
class PDFDocumentBuilder;

// Edit Content, stage E0, package P04. Plan: zadani_jadro.md 4.12 (writing); zadani.md 11.2 (new
// code: the merging of documents has only a flat single-node writer, nothing is moved from it).

/// Writes a number tree (ISO 32000-2, 7.9.7, Table 37): a balanced tree with leaves of at most
/// KEYS_PER_LEAF keys and intermediate nodes of at most KIDS_PER_NODE kids, /Limits in every node
/// except the root, the keys ascending. The root of a small tree holds /Nums itself.
///
/// Threads and ownership: static and reentrant; the nodes are added to the builder as indirect
/// objects, the root is returned.
class PDF4QTLIBCORESHARED_EXPORT PDFNumberTreeWriter
{
public:
    PDFNumberTreeWriter() = delete;

    static constexpr size_t KEYS_PER_LEAF = 64;
    static constexpr size_t KIDS_PER_NODE = 64;

    /// Writes a number tree.
    /// \param builder Builder of the document
    /// \param items Key -> value (a direct object or a reference; written as it is)
    /// \returns the root dictionary (a direct object: the caller stores it or adds it as an object)
    static PDFObject write(PDFDocumentBuilder* builder, const std::map<PDFInteger, PDFObject>& items);
};

}   // namespace pdf

#endif // PDFNUMBERTREEWRITER_H
