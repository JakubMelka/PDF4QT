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

/// Storage, which can mark objects (for example, when we want to mark already visited objects
/// during parsing some complex structure, such as tree)
class PDFMarkedObjectsContext
{
public:
    inline explicit PDFMarkedObjectsContext() = default;

    inline bool isMarked(PDFObjectReference reference) const { return m_markedReferences.count(reference); }
    inline void mark(PDFObjectReference reference) { m_markedReferences.insert(reference); }
    inline void unmark(PDFObjectReference reference) { m_markedReferences.erase(reference); }

private:
    std::set<PDFObjectReference> m_markedReferences;
};

/// Class for marking/unmarking objects functioning as guard. If not already marked, then
/// during existence of this guard, object is marked and then it is unmarked. If it is
/// already marked, then nothing happens and locked flag is set to false.
class PDFMarkedObjectsLock
{
public:
    explicit inline PDFMarkedObjectsLock(PDFMarkedObjectsContext* context, PDFObjectReference reference) :
        m_context(context),
        m_reference(reference),
        m_locked(!context->isMarked(reference))
    {
        if (m_locked)
        {
            context->mark(reference);
        }
    }

    inline ~PDFMarkedObjectsLock()
    {
        if (m_locked)
        {
            m_context->unmark(m_reference);
        }
    }

    explicit operator bool() const { return m_locked; }

private:
    PDFMarkedObjectsContext* m_context;
    PDFObjectReference m_reference;
    bool m_locked;
};

} // namespace pdf

#endif // PDFOBJECTUTILS_H
