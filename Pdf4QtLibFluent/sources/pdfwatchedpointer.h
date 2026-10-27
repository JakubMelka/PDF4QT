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

#ifndef PDFWATCHEDPOINTER_H
#define PDFWATCHEDPOINTER_H

#include <QObject>
#include <QPointer>

#include <cstddef>

namespace pdf
{

/// Guarded pointer of a watched object, which can be recognized also while it is
/// being destroyed. QPointer<T>::data() casts the QObject to T by static_cast, but
/// a widget in its destructor (a slot of destroyed, an event filter during the
/// destruction, while ~QWidget runs) is no longer a T - the cast is undefined
/// (UBSan vptr). \p isObject compares the address as a QObject only, \p data
/// returns the object only while it is still a T (qobject_cast, the meta object
/// of the destructor running), nullptr while a destructor of a base of T runs.
template<typename T>
class PDFWatchedPointer
{
public:
    PDFWatchedPointer() = default;
    explicit PDFWatchedPointer(T* object) : m_pointer(object), m_address(object) { }

    PDFWatchedPointer& operator=(T* object)
    {
        m_pointer = object;
        m_address = object;
        return *this;
    }

    PDFWatchedPointer& operator=(std::nullptr_t)
    {
        m_pointer = nullptr;
        m_address = nullptr;
        return *this;
    }

    T* data() const { return m_pointer.isNull() ? nullptr : qobject_cast<T*>(const_cast<QObject*>(m_address)); }
    T* operator->() const { return data(); }
    operator T*() const { return data(); }
    bool isNull() const { return m_pointer.isNull(); }

    /// Returns true, if the pointer holds the object (also an object in its
    /// destructor, before ~QObject clears the pointer); never casts the object
    bool isObject(const QObject* object) const { return object && !m_pointer.isNull() && m_address == object; }

private:
    QPointer<T> m_pointer;
    const QObject* m_address = nullptr;
};

}   // namespace pdf

#endif // PDFWATCHEDPOINTER_H
