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

#ifndef PDFJSONHELPER_H
#define PDFJSONHELPER_H

#include "pdfglobal.h"

#include <QSize>
#include <QLineF>
#include <QRectF>
#include <QPointF>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonValue>
#include <QTransform>
#include <QStringList>

#include <array>
#include <vector>
#include <optional>

namespace pdf
{

/// Conversion of common values to JSON values and back. The conversions from JSON
/// are tolerant: a value of a wrong type, or an array with missing items, gives
/// a default value.
class PDF4QTLIBCORESHARED_EXPORT PDFJsonHelper
{
public:
    PDFJsonHelper() = delete;

    /// Converts the list of strings to an array of strings
    static QJsonArray stringListToJson(const QStringList& list);

    /// Converts the array of strings to the list of strings (items, which
    /// are not strings, give empty strings)
    static QStringList stringListFromJson(const QJsonValue& value);

    /// Converts the list of integers to an array of numbers
    static QJsonArray intListToJson(const std::vector<int>& list);

    /// Converts the array of numbers to the list of integers
    static std::vector<int> intListFromJson(const QJsonValue& value);

    /// Converts the list of 64-bit integers to an array of numbers
    static QJsonArray integerListToJson(const std::vector<PDFInteger>& list);

    /// Converts the array of numbers to the list of 64-bit integers
    static std::vector<PDFInteger> integerListFromJson(const QJsonValue& value);

    /// Converts the rectangle to the array [left, top, width, height]
    static QJsonArray rectToJson(const QRectF& rect);

    /// Converts the array [left, top, width, height] to the rectangle
    /// (a null rectangle, if the array is shorter)
    static QRectF rectFromJson(const QJsonValue& value);

    /// Converts the line to the array [x1, y1, x2, y2]
    static QJsonArray lineToJson(const QLineF& line);

    /// Converts the array [x1, y1, x2, y2] to the line (a null line, if the array is shorter)
    static QLineF lineFromJson(const QJsonValue& value);

    /// Converts the transformation to the array [m11, m12, m21, m22, dx, dy]
    static QJsonArray transformToJson(const QTransform& transform);

    /// Converts the array [m11, m12, m21, m22, dx, dy] to the transformation
    /// (the identity, if the array is shorter)
    static QTransform transformFromJson(const QJsonValue& value);

    /// Converts the size to the array [width, height]
    static QJsonArray sizeToJson(const QSize& size);

    /// Converts the array [width, height] to the size (an invalid size, if the array is shorter)
    static QSize sizeFromJson(const QJsonValue& value);

    /// Converts the date and time to ISO 8601 in UTC with milliseconds
    /// (an empty string for an invalid date and time)
    static QString dateTimeToJson(const QDateTime& dateTime);

    /// Converts the ISO 8601 string to the date and time (an invalid date and time
    /// for an empty string)
    static QDateTime dateTimeFromJson(const QJsonValue& value);

    /// Converts the points to the array [x1, y1, x2, y2, ...]
    template<size_t N>
    static QJsonArray pointsToJson(const std::array<QPointF, N>& points)
    {
        QJsonArray array;
        for (const QPointF& point : points)
        {
            array.append(point.x());
            array.append(point.y());
        }
        return array;
    }

    /// Converts the array [x1, y1, x2, y2, ...] to the points. Returns no value,
    /// if the array does not contain exactly 2 * N numbers.
    template<size_t N>
    static std::optional<std::array<QPointF, N>> pointsFromJson(const QJsonValue& value)
    {
        const QJsonArray array = value.toArray();
        if (array.size() != qsizetype(2 * N))
        {
            return std::nullopt;
        }

        std::array<QPointF, N> points = { };
        for (size_t i = 0; i < N; ++i)
        {
            points[i] = QPointF(array.at(qsizetype(2 * i)).toDouble(), array.at(qsizetype(2 * i + 1)).toDouble());
        }
        return points;
    }
};

}   // namespace pdf

#endif // PDFJSONHELPER_H
