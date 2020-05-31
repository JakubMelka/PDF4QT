//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFOBJECTUTILS_H
#define PDFOBJECTUTILS_H

#include "pdfobject.h"

#include <set>

namespace pdf
{
class PDFObjectStorage;

/// Utilities for manipulation with objects
class PDFObjectUtils
{
public:
    /// Returns list of references referenced by \p objects. So, all references, which are present
    /// in objects, appear in the result set, including objects, which are referenced by referenced
    /// objects (so, transitive closure above reference graph is returned).
    /// \param objects Objects
    /// \param storage Storage
    static std::set<PDFObjectReference> getReferences(const std::vector<PDFObject>& objects, const PDFObjectStorage& storage);

    static PDFObject replaceReferences(const PDFObject& object, const std::map<PDFObjectReference, PDFObjectReference>& referenceMapping);

private:
    PDFObjectUtils() = delete;
};

} // namespace pdf

#endif // PDFOBJECTUTILS_H
