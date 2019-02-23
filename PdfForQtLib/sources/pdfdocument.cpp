//    Copyright (C) 2018 Jakub Melka
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


#include "pdfdocument.h"
#include "pdfparser.h"
#include "pdfencoding.h"
#include "pdfstreamfilters.h"
#include "pdfconstants.h"

namespace pdf
{

// Entries for "Info" entry in trailer dictionary
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY = "Info";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_TITLE = "Title";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_AUTHOR = "Author";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_SUBJECT = "Subject";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_KEYWORDS = "Keywords";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_CREATOR = "Creator";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_PRODUCER = "Producer";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_CREATION_DATE = "CreationDate";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_MODIFIED_DATE = "ModDate";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_TRAPPED = "Trapped";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_TRAPPED_TRUE = "True";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_TRAPPED_FALSE = "False";
static constexpr const char* PDF_DOCUMENT_INFO_ENTRY_TRAPPED_UNKNOWN = "Unknown";

QByteArray PDFDocument::getDecodedStream(const PDFStream* stream) const
{
    const PDFDictionary* dictionary = stream->getDictionary();

    // Retrieve filters
    PDFObject filters;
    if (dictionary->hasKey(PDF_STREAM_DICT_FILTER))
    {
        filters = getObject(dictionary->get(PDF_STREAM_DICT_FILTER));
    }
    else if (dictionary->hasKey(PDF_STREAM_DICT_FILE_FILTER))
    {
        filters = getObject(dictionary->get(PDF_STREAM_DICT_FILE_FILTER));
    }

    // Retrieve filter parameters
    PDFObject filterParameters;
    if (dictionary->hasKey(PDF_STREAM_DICT_DECODE_PARMS))
    {
        filterParameters = getObject(dictionary->get(PDF_STREAM_DICT_DECODE_PARMS));
    }
    else if (dictionary->hasKey(PDF_STREAM_DICT_FDECODE_PARMS))
    {
        filterParameters = getObject(dictionary->get(PDF_STREAM_DICT_FDECODE_PARMS));
    }

    std::vector<const PDFStreamFilter*> filterObjects;
    std::vector<PDFObject> filterParameterObjects;

    if (filters.isName())
    {
        filterObjects.push_back(PDFStreamFilterStorage::getFilter(filters.getString()));
    }
    else if (filters.isArray())
    {
        const PDFArray* filterArray = filters.getArray();
        const size_t filterCount = filterArray->getCount();
        for (size_t i = 0; i < filterCount; ++i)
        {
            const PDFObject& object = getObject(filterArray->getItem(i));
            if (object.isName())
            {
                filterObjects.push_back(PDFStreamFilterStorage::getFilter(object.getString()));
            }
            else
            {
                return QByteArray();
            }
        }
    }
    else if (!filters.isNull())
    {
        return QByteArray();
    }

    if (filterParameters.isArray())
    {
        const PDFArray* filterParameterArray = filterParameters.getArray();
        const size_t filterParameterCount = filterParameterArray->getCount();
        for (size_t i = 0; i < filterParameterCount; ++i)
        {
            const PDFObject& object = getObject(filterParameterArray->getItem(i));
            filterParameterObjects.push_back(object);
        }
    }
    else
    {
        filterParameterObjects.push_back(filterParameters);
    }

    filterParameterObjects.resize(filterObjects.size());
    std::reverse(filterObjects.begin(), filterObjects.end());
    std::reverse(filterParameterObjects.begin(), filterParameterObjects.end());

    QByteArray result = *stream->getContent();

    for (size_t i = 0, count = filterObjects.size(); i < count; ++i)
    {
        const PDFStreamFilter* streamFilter = filterObjects[i];
        const PDFObject& streamFilterParameters = filterParameterObjects[i];

        if (streamFilter)
        {
            result = streamFilter->apply(result, this, streamFilterParameters);
        }
    }

    return result;
}

void PDFDocument::init()
{
    initInfo();

    const PDFObject& trailerDictionary = m_pdfObjectStorage.getTrailerDictionary();

    // Trailer object should be dictionary here. It is verified in the document reader.
    Q_ASSERT(trailerDictionary.isDictionary());

    const PDFDictionary* dictionary = trailerDictionary.getDictionary();
    Q_ASSERT(dictionary);

    m_catalog = PDFCatalog::parse(getObject(dictionary->get("Root")), this);
}

void PDFDocument::initInfo()
{
    const PDFObject& trailerDictionary = m_pdfObjectStorage.getTrailerDictionary();

    // Trailer object should be dictionary here. It is verified in the document reader.
    Q_ASSERT(trailerDictionary.isDictionary());

    const PDFDictionary* dictionary = trailerDictionary.getDictionary();
    Q_ASSERT(dictionary);

    if (dictionary->hasKey(PDF_DOCUMENT_INFO_ENTRY))
    {
        const PDFObject& info = getObject(dictionary->get(PDF_DOCUMENT_INFO_ENTRY));

        if (info.isDictionary())
        {
            const PDFDictionary* infoDictionary = info.getDictionary();
            Q_ASSERT(infoDictionary);

            auto readTextString = [this, infoDictionary](const char* entry, QString& fillEntry)
            {
                if (infoDictionary->hasKey(entry))
                {
                    const PDFObject& stringObject = getObject(infoDictionary->get(entry));
                    if (stringObject.isString())
                    {
                        // We have succesfully read the string, convert it according to encoding
                        fillEntry = PDFEncoding::convertTextString(stringObject.getString());
                    }
                    else if (!stringObject.isNull())
                    {
                        throw PDFParserException(tr("Bad format of document info entry in trailer dictionary. String expected."));
                    }
                }
            };
            readTextString(PDF_DOCUMENT_INFO_ENTRY_TITLE, m_info.title);
            readTextString(PDF_DOCUMENT_INFO_ENTRY_AUTHOR, m_info.author);
            readTextString(PDF_DOCUMENT_INFO_ENTRY_SUBJECT, m_info.subject);
            readTextString(PDF_DOCUMENT_INFO_ENTRY_KEYWORDS, m_info.keywords);
            readTextString(PDF_DOCUMENT_INFO_ENTRY_CREATOR, m_info.creator);
            readTextString(PDF_DOCUMENT_INFO_ENTRY_PRODUCER, m_info.producer);

            auto readDate= [this, infoDictionary](const char* entry, QDateTime& fillEntry)
            {
                if (infoDictionary->hasKey(entry))
                {
                    const PDFObject& stringObject = getObject(infoDictionary->get(entry));
                    if (stringObject.isString())
                    {
                        // We have succesfully read the string, convert it to date time
                        fillEntry = PDFEncoding::convertToDateTime(stringObject.getString());

                        if (!fillEntry.isValid())
                        {
                            throw PDFParserException(tr("Bad format of document info entry in trailer dictionary. String with date time format expected."));
                        }
                    }
                    else if (!stringObject.isNull())
                    {
                        throw PDFParserException(tr("Bad format of document info entry in trailer dictionary. String with date time format expected."));
                    }
                }
            };
            readDate(PDF_DOCUMENT_INFO_ENTRY_CREATION_DATE, m_info.creationDate);
            readDate(PDF_DOCUMENT_INFO_ENTRY_MODIFIED_DATE, m_info.modifiedDate);

            if (infoDictionary->hasKey(PDF_DOCUMENT_INFO_ENTRY_TRAPPED))
            {
                const PDFObject& nameObject = getObject(infoDictionary->get(PDF_DOCUMENT_INFO_ENTRY_TRAPPED));
                if (nameObject.isName())
                {
                    const QByteArray& name = nameObject.getString();
                    if (name == PDF_DOCUMENT_INFO_ENTRY_TRAPPED_TRUE)
                    {
                        m_info.trapped = Info::Trapped::True;
                    }
                    else if (name == PDF_DOCUMENT_INFO_ENTRY_TRAPPED_FALSE)
                    {
                        m_info.trapped = Info::Trapped::False;
                    }
                    else if (name == PDF_DOCUMENT_INFO_ENTRY_TRAPPED_UNKNOWN)
                    {
                        m_info.trapped = Info::Trapped::Unknown;
                    }
                    else
                    {
                        throw PDFParserException(tr("Bad format of document info entry in trailer dictionary. Trapping information expected"));
                    }
                }
                else
                {
                    throw PDFParserException(tr("Bad format of document info entry in trailer dictionary. Trapping information expected"));
                }
            }
        }
        else if (!info.isNull()) // Info may be invalid...
        {
            throw PDFParserException(tr("Bad format of document info entry in trailer dictionary."));
        }
    }
}

const PDFObject& PDFObjectStorage::getObject(PDFObjectReference reference) const
{
    if (reference.objectNumber >= 0 &&
        reference.objectNumber < static_cast<PDFInteger>(m_objects.size()) &&
        m_objects[reference.objectNumber].generation == reference.generation)
    {
        return m_objects[reference.objectNumber].object;
    }
    else
    {
        static const PDFObject dummy;
        return dummy;
    }
}

PDFInteger PDFDocumentDataLoaderDecorator::readInteger(const PDFObject& object, PDFInteger defaultValue) const
{
    const PDFObject& dereferencedObject = m_document->getObject(object);
    if (dereferencedObject.isInt())
    {
        return dereferencedObject.getInteger();
    }

    return defaultValue;
}

PDFReal PDFDocumentDataLoaderDecorator::readNumber(const PDFObject& object, PDFReal defaultValue) const
{
    const PDFObject& dereferencedObject = m_document->getObject(object);

    if (dereferencedObject.isReal())
    {
        return dereferencedObject.getReal();
    } else if (dereferencedObject.isInt())
    {
        return dereferencedObject.getInteger();
    }

    return defaultValue;
}

QString PDFDocumentDataLoaderDecorator::readTextString(const PDFObject& object, const QString& defaultValue) const
{
    const PDFObject& dereferencedObject = m_document->getObject(object);
    if (dereferencedObject.isString())
    {
        return PDFEncoding::convertTextString(dereferencedObject.getString());
    }

    return defaultValue;
}

QRectF PDFDocumentDataLoaderDecorator::readRectangle(const PDFObject& object, const QRectF& defaultValue) const
{
    const PDFObject& dereferencedObject = m_document->getObject(object);
    if (dereferencedObject.isArray())
    {
        const PDFArray* array = dereferencedObject.getArray();
        if (array->getCount() == 4)
        {
            std::array<PDFReal, 4> items;
            for (size_t i = 0; i < 4; ++i)
            {
                const PDFObject& object = m_document->getObject(array->getItem(i));
                if (object.isReal())
                {
                    items[i] = object.getReal();
                }
                else if (object.isInt())
                {
                    items[i] = object.getInteger();
                }
                else
                {
                    return defaultValue;
                }
            }

            const PDFReal xMin = qMin(items[0], items[2]);
            const PDFReal xMax = qMax(items[0], items[2]);
            const PDFReal yMin = qMin(items[1], items[3]);
            const PDFReal yMax = qMax(items[1], items[3]);

            return QRectF(xMin, yMin, xMax - xMin, yMax - yMin);
        }
    }

    return defaultValue;
}

PDFReal PDFDocumentDataLoaderDecorator::readNumberFromDictionary(const PDFDictionary* dictionary, const char* key, PDFReal defaultValue) const
{
    if (dictionary->hasKey(key))
    {
        return readNumber(dictionary->get(key), defaultValue);
    }

    return defaultValue;
}

PDFInteger PDFDocumentDataLoaderDecorator::readIntegerFromDictionary(const PDFDictionary* dictionary, const char* key, PDFInteger defaultValue) const
{
    if (dictionary->hasKey(key))
    {
        return readInteger(dictionary->get(key), defaultValue);
    }

    return defaultValue;
}

std::vector<PDFReal> PDFDocumentDataLoaderDecorator::readNumberArray(const PDFObject& object) const
{
    const PDFObject& dereferencedObject = m_document->getObject(object);
    if (dereferencedObject.isArray())
    {
        const PDFArray* array = dereferencedObject.getArray();
        std::vector<PDFReal> result;
        const size_t count = array->getCount();
        result.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            const PDFReal number = readNumber(array->getItem(i), std::numeric_limits<PDFReal>::quiet_NaN());
            if (std::isnan(number))
            {
                return std::vector<PDFReal>();
            }
            result.push_back(number);
        }

        // We assume, that RVO (return value optimization) will not work for this function
        // (multiple return points).
        return std::move(result);
    }

    return std::vector<PDFReal>();
}

}   // namespace pdf
