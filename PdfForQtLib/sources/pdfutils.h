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

#include "pdfglobal.h"

#include <QByteArray>
#include <QDataStream>

#include <vector>

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

/// Bit-reader, which can read n-bit unsigned integers from the stream.
/// Number of bits can be set in the constructor and is constant.
class PDFBitReader
{
public:
    using Value = uint64_t;

    explicit PDFBitReader(QDataStream* stream, Value bitsPerComponent);

    /// Returns maximal value of n-bit unsigned integer.
    Value max() const { return m_maximalValue; }

    /// Reads single n-bit value from the stream. If stream hasn't enough data,
    /// then exception is thrown.
    Value read();

    /// Seeks the desired position in the data stream. If position can't be seeked,
    /// then exception is thrown.
    void seek(qint64 position);

private:
    QDataStream* m_stream;

    const Value m_bitsPerComponent;
    const Value m_maximalValue;

    Value m_buffer;
    Value m_bitsInBuffer;
};

/// Simple class guard, for properly saving/restoring new/old value. In the constructor,
/// new value is stored in the pointer (old one is being saved), and in the destructor,
/// old value is restored. This object assumes, that value is not a null pointer.
template<typename Value>
class PDFTemporaryValueChange
{
public:
    /// Constructor
    /// \param value Value pointer (must not be a null pointer)
    /// \param newValue New value to be set to the pointer
    explicit inline PDFTemporaryValueChange(Value* valuePointer, Value newValue) :
        m_oldValue(qMove(*valuePointer)),
        m_value(valuePointer)
    {
        *valuePointer = qMove(newValue);
    }

    inline ~PDFTemporaryValueChange()
    {
        *m_value = qMove(m_oldValue);
    }

private:
    Value m_oldValue;
    Value* m_value;
};

/// Performs linear mapping of value x in interval [x_min, x_max] to the interval [y_min, y_max].
/// \param x Value to be linearly remapped from interval [x_min, x_max] to the interval [y_min, y_max].
/// \param x_min Start of the input interval
/// \param x_max End of the input interval
/// \param y_min Start of the output interval
/// \param y_max End of the output interval
static inline constexpr PDFReal interpolate(PDFReal x, PDFReal x_min, PDFReal x_max, PDFReal y_min, PDFReal y_max)
{
    return y_min + (x - x_min) * (y_max - y_min) / (x_max - x_min);
}

inline
std::vector<uint8_t> convertByteArrayToVector(const QByteArray& data)
{
    return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(data.constData()), reinterpret_cast<const uint8_t*>(data.constData()) + data.size());
}

inline
const unsigned char* convertByteArrayToUcharPtr(const QByteArray& data)
{
    return reinterpret_cast<const unsigned char*>(data.constData());
}

inline
unsigned char* convertByteArrayToUcharPtr(QByteArray& data)
{
    return reinterpret_cast<unsigned char*>(data.data());
}

}   // namespace pdf

#endif // PDFUTILS_H
