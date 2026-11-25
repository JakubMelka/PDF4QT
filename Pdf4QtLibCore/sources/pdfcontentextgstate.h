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

#ifndef PDFCONTENTEXTGSTATE_H
#define PDFCONTENTEXTGSTATE_H

#include "pdfcontentstate.h"
#include "pdfobject.h"

#include <QByteArray>

#include <map>

namespace pdf
{

// Edit Content, stage E0, package P01. Plan: zadani_jadro.md 4.4 (R-1, R-2, R-6), 4.8 (ExtGState).

/// Creates ExtGState dictionaries (ISO 32000-2, 8.4.5, Table 57) with exactly the requested keys:
/// gs sets only the keys, which are present, so a dictionary of the editor never carries a key the
/// user did not change.
///
/// Rule R-6: a dictionary created for other properties never has /SMask; a soft mask is set
/// again only by a dictionary with the single key /SMask (the original mask dictionary by
/// reference, or /None).
///
/// Threads and ownership: static and reentrant; the result is a direct dictionary object.
class PDF4QTLIBCORESHARED_EXPORT PDFContentExtGStateWriter
{
public:
    PDFContentExtGStateWriter() = delete;

    /// The property is set by a key of an ExtGState and has no operator of its own (the alphas,
    /// the blend mode, the overprints, /SA, /TK, /AIS, the soft mask, /BG, /UCR, /TR, /HT, /HTO,
    /// /SM, /UseBlackPtComp)
    static bool isExtGStateProperty(PDFContentStateProperty property);

    /// The key of a property in an ExtGState ("CA", "ca", "BM", "OP", ...); empty for a property,
    /// which is not written by an ExtGState here. BlackGeneration, UndercolorRemoval and
    /// Transfer have two keys: createDictionary writes the PDF 2.0 key (BG2, UCR2, TR2) for a
    /// name value (/Default) and the plain key (BG, UCR, TR) for a function object; getKey gives
    /// the plain key.
    static QByteArray getKey(PDFContentStateProperty property);

    /// A dictionary /Type /ExtGState with the keys of the given properties. Properties, which
    /// are not isExtGStateProperty, and SoftMask are refused (a null object).
    static PDFObject createDictionary(const std::map<PDFContentStateProperty, PDFContentStateValue>& properties);

    /// A dictionary with the single key /SMask (R-6)
    /// \param softMask The soft mask dictionary of the document (a reference), or null for /None
    static PDFObject createSoftMaskDictionary(const PDFObject& softMask);

    /// Two ExtGState dictionaries of the editor set the same keys to the same values (an equal
    /// dictionary is shared inside of a container)
    static bool isEqual(const PDFObject& left, const PDFObject& right);
};

}   // namespace pdf

#endif // PDFCONTENTEXTGSTATE_H
