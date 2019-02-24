//    Copyright (C) 2019 Jakub Melka
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
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.


#ifndef PDFUTILS_H
#define PDFUTILS_H

namespace pdf
{

/// Class for easy storing of cached item. This class is not thread safe,
/// and for this reason, access function are not constant (they can modify the
/// object).
template<typename T>
class PDFCachedItem
{
public:
    explicit inline PDFCachedItem() :
        m_dirty(true),
        m_object()
    {

    }

    /// Returns the cached object. If object is dirty, then cached object is refreshed.
    /// \param holder Holder object, which owns the cached item
    /// \param function Refresh function
    template<typename H>
    inline const T& get(const H* holder, T(H::* function)(void) const)
    {
        if (m_dirty)
        {
            m_object = (holder->*function)();
            m_dirty = false;
        }

        return m_object;
    }

    /// Invalidates the cached item, so it must be refreshed from the cache next time,
    /// if it is accessed.
    inline void dirty()
    {
        m_dirty = true;
        m_object = T();
    }

private:
    bool m_dirty;
    T m_object;
};

}   // namespace pdf

#endif // PDFUTILS_H
