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

#include "pdfjsonhelper.h"
#include "pdfdbgheap.h"

namespace pdf
{

QJsonArray PDFJsonHelper::stringListToJson(const QStringList& list)
{
    return QJsonArray::fromStringList(list);
}

QStringList PDFJsonHelper::stringListFromJson(const QJsonValue& value)
{
    QStringList list;
    for (const QJsonValue& item : value.toArray())
    {
        list << item.toString();
    }
    return list;
}

QJsonArray PDFJsonHelper::intListToJson(const std::vector<int>& list)
{
    QJsonArray array;
    for (int item : list)
    {
        array.append(item);
    }
    return array;
}

std::vector<int> PDFJsonHelper::intListFromJson(const QJsonValue& value)
{
    std::vector<int> list;
    for (const QJsonValue& item : value.toArray())
    {
        list.push_back(item.toInt());
    }
    return list;
}

QJsonArray PDFJsonHelper::integerListToJson(const std::vector<PDFInteger>& list)
{
    QJsonArray array;
    for (PDFInteger item : list)
    {
        array.append(qint64(item));
    }
    return array;
}

std::vector<PDFInteger> PDFJsonHelper::integerListFromJson(const QJsonValue& value)
{
    std::vector<PDFInteger> list;
    for (const QJsonValue& item : value.toArray())
    {
        list.push_back(PDFInteger(item.toDouble()));
    }
    return list;
}

QJsonArray PDFJsonHelper::rectToJson(const QRectF& rect)
{
    QJsonArray array;
    array.append(rect.left());
    array.append(rect.top());
    array.append(rect.width());
    array.append(rect.height());
    return array;
}

QRectF PDFJsonHelper::rectFromJson(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (array.size() >= 4)
    {
        return QRectF(array[0].toDouble(), array[1].toDouble(), array[2].toDouble(), array[3].toDouble());
    }
    return QRectF();
}

QJsonArray PDFJsonHelper::lineToJson(const QLineF& line)
{
    QJsonArray array;
    array.append(line.x1());
    array.append(line.y1());
    array.append(line.x2());
    array.append(line.y2());
    return array;
}

QLineF PDFJsonHelper::lineFromJson(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (array.size() >= 4)
    {
        return QLineF(array[0].toDouble(), array[1].toDouble(), array[2].toDouble(), array[3].toDouble());
    }
    return QLineF();
}

QJsonArray PDFJsonHelper::transformToJson(const QTransform& transform)
{
    QJsonArray array;
    array.append(transform.m11());
    array.append(transform.m12());
    array.append(transform.m21());
    array.append(transform.m22());
    array.append(transform.dx());
    array.append(transform.dy());
    return array;
}

QTransform PDFJsonHelper::transformFromJson(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (array.size() >= 6)
    {
        return QTransform(array[0].toDouble(), array[1].toDouble(), array[2].toDouble(), array[3].toDouble(), array[4].toDouble(), array[5].toDouble());
    }
    return QTransform();
}

QJsonArray PDFJsonHelper::sizeToJson(const QSize& size)
{
    QJsonArray array;
    array.append(size.width());
    array.append(size.height());
    return array;
}

QSize PDFJsonHelper::sizeFromJson(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (array.size() >= 2)
    {
        return QSize(array[0].toInt(), array[1].toInt());
    }
    return QSize();
}

QString PDFJsonHelper::dateTimeToJson(const QDateTime& dateTime)
{
    return dateTime.isValid() ? dateTime.toUTC().toString(Qt::ISODateWithMs) : QString();
}

QDateTime PDFJsonHelper::dateTimeFromJson(const QJsonValue& value)
{
    const QString text = value.toString();
    return text.isEmpty() ? QDateTime() : QDateTime::fromString(text, Qt::ISODateWithMs);
}

}   // namespace pdf
