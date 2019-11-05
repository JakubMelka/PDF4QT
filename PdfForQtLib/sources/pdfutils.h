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
#include <iterator>

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

    /// Returns the cached object. If object is dirty, then cached object is refreshed.
    /// \param holder Holder object, which owns the cached item
    /// \param function Refresh function
    template<typename H>
    inline const T& get(H* holder, T(H::* function)(void))
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
class PDFFORQTLIBSHARED_EXPORT PDFBitReader
{
public:
    using Value = uint64_t;

    explicit PDFBitReader(const QByteArray* stream, Value bitsPerComponent);

    PDFBitReader(const PDFBitReader&) = default;
    PDFBitReader(PDFBitReader&&) = default;

    PDFBitReader& operator=(const PDFBitReader&) = default;
    PDFBitReader& operator=(PDFBitReader&&) = default;

    /// Returns maximal value of n-bit unsigned integer.
    Value max() const { return m_maximalValue; }

    /// Reads single n-bit value from the stream. If stream hasn't enough data,
    /// then exception is thrown.
    Value read() { return read(m_bitsPerComponent); }

    /// Reads single n-bit value from the stream. If stream hasn't enough data,
    /// then exception is thrown.
    Value read(Value bits);

    /// Reads single n-bit value from the stream. If stream hasn't enough data,
    /// then exception is thrown. State of the stream is not changed, i.e., read
    /// bits are reverted back.
    Value look(Value bits) const;

    /// Seeks the desired position in the data stream. If position can't be seeked,
    /// then exception is thrown.
    void seek(qint64 position);

    /// Skips desired number of bytes
    void skipBytes(Value bytes);

    /// Seeks data to the byte boundary (number of processed bits is divisible by 8)
    void alignToBytes();

    /// Returns true, if we are at the end of the data stream (no more data can be read)
    bool isAtEnd() const;

    /// Returns position in the data stream (byte position, not bit position, so
    /// result of this function is sometimes inaccurate)
    int getPosition() const { return m_position; }

    /// Reads signed 32-bit integer from the stream
    int32_t readSignedInt();

    /// Reads signed 8-bit integer from the stream
    int8_t readSignedByte();

    /// Reads unsigned 32-bit integer from the stream
    uint32_t readUnsignedInt() { return read(32); }

    /// Reads unsigned 16-bit integer from the stream
    uint16_t readUnsignedWord() { return read(16); }

    /// Reads unsigned 8-bit integer from the stream
    uint8_t readUnsignedByte() { return read(8); }

    /// Return underlying byte stream
    const QByteArray* getStream() const { return m_stream; }

    /// Reads substream from current stream. This function works only on byte boundary,
    /// otherwise exception is thrown.
    /// \param length Length of the substream. Can be -1, in this case, all remaining data is read.
    QByteArray readSubstream(int length);

private:
    const QByteArray* m_stream;
    int m_position;

    Value m_bitsPerComponent;
    Value m_maximalValue;

    Value m_buffer;
    Value m_bitsInBuffer;
};

/// Bit writer
class PDFBitWriter
{
public:
    using Value = uint64_t;

    explicit PDFBitWriter(Value bitsPerComponent);

    /// Writes value to the output stream
    void write(Value value);

    /// Finish line - align to byte boundary
    void finishLine() { flush(true); }

    /// Returns the result byte array
    QByteArray takeByteArray() { return qMove(m_outputByteArray); }

    /// Reserve memory in buffer
    void reserve(int size) { m_outputByteArray.reserve(size); }

private:
    void flush(bool alignToByteBoundary);

    QByteArray m_outputByteArray;

    Value m_bitsPerComponent;
    Value m_mask;
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

/// Implements range for range based for cycles
template<typename T>
class PDFIntegerRange
{
public:
    explicit inline constexpr PDFIntegerRange(T begin, T end) : m_begin(begin), m_end(end) { }

    struct Iterator : public std::iterator<std::random_access_iterator_tag, T, ptrdiff_t, T*, T&>
    {
        inline Iterator() : value(T(0)) { }
        inline Iterator(T value) : value(value) { }

        inline bool operator==(const Iterator& other) const { return value == other.value; }
        inline bool operator!=(const Iterator& other) const { return value != other.value; }

        inline T operator*() const { return value; }
        inline Iterator& operator+=(ptrdiff_t movement) { value += T(movement); return *this; }
        inline Iterator& operator-=(ptrdiff_t movement) { value -= T(movement); return *this; }
        inline Iterator operator+(ptrdiff_t movement) const { return Iterator(value + T(movement)); }
        inline ptrdiff_t operator-(const Iterator& other) const { return ptrdiff_t(value - other.value); }

        inline Iterator& operator++()
        {
            ++value;
            return *this;
        }

        inline Iterator operator++(int)
        {
            Iterator copy(*this);
            ++value;
            return copy;
        }

        inline Iterator& operator--()
        {
            --value;
            return *this;
        }

        inline Iterator operator--(int)
        {
            Iterator copy(*this);
            --value;
            return copy;
        }

        T value = 0;
    };

    Iterator begin() const { return Iterator(m_begin); }
    Iterator end() const { return Iterator(m_end); }

private:
    T m_begin;
    T m_end;
};

template<typename T>
bool contains(T value, std::initializer_list<T> list)
{
    return (std::find(list.begin(), list.end(), value) != list.end());
}

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

/// This function computes ceil of log base 2 of value. The algorithm is taken
/// from: http://graphics.stanford.edu/~seander/bithacks.html#IntegerLogDeBruijn.
/// License for this function is public domain.
inline constexpr uint8_t log2ceil(uint32_t value)
{
    const uint32_t originalValue = value;
    constexpr uint8_t MULTIPLY_DE_BRUIJN_BIT_POSITION[32] =
    {
      0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
      8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31
    };

    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;

    uint8_t logarithm = MULTIPLY_DE_BRUIJN_BIT_POSITION[static_cast<uint32_t>((value * 0x07C4ACDDU) >> 27)];

    // Ceil
    if ((1U << logarithm) < originalValue)
    {
        ++logarithm;
    }

    return logarithm;
}

}   // namespace pdf

#endif // PDFUTILS_H
