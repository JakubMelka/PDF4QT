//    Copyright (C) 2018-2022 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.


#include "pdfvisitor.h"
#include "pdfdbgheap.h"

namespace pdf
{

void PDFAbstractVisitor::acceptArray(const PDFArray* array)
{
    Q_ASSERT(array);

    for (size_t i = 0, count = array->getCount(); i < count; ++i)
    {
        array->getItem(i).accept(this);
    }
}

void PDFAbstractVisitor::acceptDictionary(const PDFDictionary* dictionary)
{
    Q_ASSERT(dictionary);

    for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
    {
        dictionary->getValue(i).accept(this);
    }
}

void PDFAbstractVisitor::acceptStream(const PDFStream* stream)
{
    Q_ASSERT(stream);

    acceptDictionary(stream->getDictionary());
}

PDFStatisticsCollector::PDFStatisticsCollector()
{

}

void PDFStatisticsCollector::visitNull()
{
    collectStatisticsOfSimpleObject(PDFObject::Type::Null);
}

void PDFStatisticsCollector::visitBool(bool value)
{
    Q_UNUSED(value);
    collectStatisticsOfSimpleObject(PDFObject::Type::Bool);
}

void PDFStatisticsCollector::visitInt(PDFInteger value)
{
    Q_UNUSED(value);
     collectStatisticsOfSimpleObject(PDFObject::Type::Int);
}

void PDFStatisticsCollector::visitReal(PDFReal value)
{
    Q_UNUSED(value);
    collectStatisticsOfSimpleObject(PDFObject::Type::Real);
}

void PDFStatisticsCollector::visitString(PDFStringRef string)
{
    Statistics& statistics = m_statistics[size_t(PDFObject::Type::String)];
    if (string.inplaceString)
    {
        collectStatisticsOfSimpleObject(PDFObject::Type::String);
    }
    else
    {
        collectStatisticsOfString(string.memoryString, statistics);
    }
}

void PDFStatisticsCollector::visitName(PDFStringRef name)
{
    Statistics& statistics = m_statistics[size_t(PDFObject::Type::Name)];
    if (name.inplaceString)
    {
        collectStatisticsOfSimpleObject(PDFObject::Type::Name);
    }
    else
    {
        collectStatisticsOfString(name.memoryString, statistics);
    }
}

void PDFStatisticsCollector::visitArray(const PDFArray* array)
{
    Statistics& statistics = m_statistics[size_t(PDFObject::Type::Array)];
    statistics.count += 1;
    statistics.memoryConsumptionEstimate += sizeof(PDFObject) + sizeof(PDFArray);

    // We process elements of the array, together with memory consumption,
    // in the call of acceptArray function. No need to calculate memory consumption here.
    // Just calculate the overhead.
    statistics.memoryOverheadEstimate += (array->getCapacity() - array->getCount()) * sizeof(PDFObject);

    acceptArray(array);
}

void PDFStatisticsCollector::visitDictionary(const PDFDictionary* dictionary)
{
    Statistics& statistics = m_statistics[size_t(PDFObject::Type::Dictionary)];
    collectStatisticsOfDictionary(statistics, dictionary);

    acceptDictionary(dictionary);
}

void PDFStatisticsCollector::visitStream(const PDFStream* stream)
{
    Statistics& statistics = m_statistics[size_t(PDFObject::Type::Stream)];
    collectStatisticsOfDictionary(statistics, stream->getDictionary());

    const QByteArray& byteArray = *stream->getContent();
    const uint64_t memoryConsumption = byteArray.size() * sizeof(char);
    const uint64_t memoryOverhead = (byteArray.capacity() - byteArray.size()) * sizeof(char);

    statistics.memoryConsumptionEstimate += memoryConsumption;
    statistics.memoryOverheadEstimate += memoryOverhead;

    acceptStream(stream);
}

void PDFStatisticsCollector::visitReference(const PDFObjectReference reference)
{
    Q_UNUSED(reference);
    collectStatisticsOfSimpleObject(PDFObject::Type::Reference);
}

void PDFStatisticsCollector::collectStatisticsOfDictionary(Statistics& statistics, const PDFDictionary* dictionary)
{
    statistics.count += 1;
    statistics.memoryConsumptionEstimate += sizeof(PDFObject) + sizeof(PDFDictionary);

    constexpr uint64_t sizeOfItem = sizeof(std::pair<QByteArray, PDFObject>);
    constexpr uint64_t sizeOfItemWithoutObject = sizeOfItem - sizeof(PDFObject);

    uint64_t consumptionEstimate = sizeOfItemWithoutObject * dictionary->getCount();
    uint64_t overheadEstimate = sizeOfItem * (dictionary->getCapacity() - dictionary->getCount());

    for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
    {
        if (!dictionary->getKey(i).isInplace())
        {
            QByteArray key = dictionary->getKey(i).getString();

            consumptionEstimate += key.size() * sizeof(char);
            overheadEstimate += (key.capacity() - key.size()) * sizeof(char);
        }
    }

    statistics.memoryConsumptionEstimate += consumptionEstimate;
    statistics.memoryOverheadEstimate += overheadEstimate;
}

void PDFStatisticsCollector::collectStatisticsOfString(const PDFString* string, Statistics& statistics)
{
    statistics.count += 1;
    statistics.memoryConsumptionEstimate += sizeof(PDFObject) + sizeof(PDFString);

    const QByteArray& byteArray = string->getString();
    const uint64_t memoryConsumption = byteArray.size() * sizeof(char);
    const uint64_t memoryOverhead = (byteArray.capacity() - byteArray.size()) * sizeof(char);

    statistics.memoryConsumptionEstimate += memoryConsumption;
    statistics.memoryOverheadEstimate += memoryOverhead;
}

void PDFStatisticsCollector::collectStatisticsOfSimpleObject(PDFObject::Type type)
{
    Statistics& statistics = m_statistics[size_t(type)];
    statistics.count += 1;
    statistics.memoryConsumptionEstimate += sizeof(PDFObject);
}

}   // namespace pdf
