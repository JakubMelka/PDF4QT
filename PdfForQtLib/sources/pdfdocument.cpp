//    Copyright (C) 2018-2020 Jakub Melka
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
#include "pdfencoding.h"
#include "pdfexception.h"
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

QByteArray PDFObjectStorage::getDecodedStream(const PDFStream* stream) const
{
    return PDFStreamFilterStorage::getDecodedStream(stream, std::bind(QOverload<const PDFObject&>::of(&PDFObjectStorage::getObject), this, std::placeholders::_1), getSecurityHandler());
}

PDFDocument::~PDFDocument()
{

}

bool PDFDocument::operator==(const PDFDocument& other) const
{
    // Document is considered equal, if storage is equal
    return m_pdfObjectStorage == other.m_pdfObjectStorage;
}

QByteArray PDFDocument::getDecodedStream(const PDFStream* stream) const
{
    return m_pdfObjectStorage.getDecodedStream(stream);
}

const PDFDictionary* PDFDocument::getTrailerDictionary() const
{
    const PDFObject& trailerDictionary = m_pdfObjectStorage.getTrailerDictionary();

    // Trailer object should be dictionary/stream here. It is verified in the document reader.
    Q_ASSERT(trailerDictionary.isDictionary() || trailerDictionary.isStream());

    if (trailerDictionary.isDictionary())
    {
        return trailerDictionary.getDictionary();
    }
    else if (trailerDictionary.isStream())
    {
        return trailerDictionary.getStream()->getDictionary();
    }

    return nullptr;
}

QByteArray PDFDocument::getVersion() const
{
    QByteArray result = m_catalog.getVersion();

    if (result.isEmpty() && m_info.version.isValid())
    {
        result = QString("%1.%2").arg(m_info.version.major).arg(m_info.version.minor).toLatin1();
    }

    return result;
}

void PDFDocument::init()
{
    initInfo();

    const PDFDictionary* dictionary = getTrailerDictionary();
    Q_ASSERT(dictionary);

    m_catalog = PDFCatalog::parse(getObject(dictionary->get("Root")), this);
}

void PDFDocument::initInfo()
{
    // Trailer object should be dictionary here. It is verified in the document reader.
    const PDFDictionary* dictionary = getTrailerDictionary();
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
                        throw PDFException(tr("Bad format of document info entry in trailer dictionary. String expected."));
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
                            throw PDFException(tr("Bad format of document info entry in trailer dictionary. String with date time format expected."));
                        }
                    }
                    else if (!stringObject.isNull())
                    {
                        throw PDFException(tr("Bad format of document info entry in trailer dictionary. String with date time format expected."));
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
                        throw PDFException(tr("Bad format of document info entry in trailer dictionary. Trapping information expected"));
                    }
                }
                else
                {
                    throw PDFException(tr("Bad format of document info entry in trailer dictionary. Trapping information expected"));
                }
            }

            // Scan for extra items
            constexpr const char* PREDEFINED_ITEMS[] = { PDF_DOCUMENT_INFO_ENTRY_TITLE, PDF_DOCUMENT_INFO_ENTRY_AUTHOR, PDF_DOCUMENT_INFO_ENTRY_SUBJECT,
                                                         PDF_DOCUMENT_INFO_ENTRY_KEYWORDS, PDF_DOCUMENT_INFO_ENTRY_CREATOR, PDF_DOCUMENT_INFO_ENTRY_PRODUCER,
                                                         PDF_DOCUMENT_INFO_ENTRY_CREATION_DATE, PDF_DOCUMENT_INFO_ENTRY_MODIFIED_DATE, PDF_DOCUMENT_INFO_ENTRY_TRAPPED };
            for (size_t i = 0; i < infoDictionary->getCount(); ++i)
            {
                const QByteArray& key = infoDictionary->getKey(i);
                if (std::none_of(std::begin(PREDEFINED_ITEMS), std::end(PREDEFINED_ITEMS), [&key](const char* item) { return item == key; }))
                {
                    const PDFObject& value = getObject(infoDictionary->getValue(i));
                    if (value.isString())
                    {
                        const QByteArray& stringValue = value.getString();
                        QDateTime dateTime = PDFEncoding::convertToDateTime(stringValue);
                        if (dateTime.isValid())
                        {
                            m_info.extra[key] = dateTime;
                        }
                        else
                        {
                            m_info.extra[key] = PDFEncoding::convertTextString(stringValue);
                        }
                    }
                }
            }
        }
        else if (!info.isNull()) // Info may be invalid...
        {
            throw PDFException(tr("Bad format of document info entry in trailer dictionary."));
        }
    }
}

bool PDFObjectStorage::operator==(const PDFObjectStorage& other) const
{
    // We compare just content. Security handler just defines encryption behavior.
    return m_objects == other.m_objects &&
           m_trailerDictionary == other.m_trailerDictionary;
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

PDFObjectReference PDFObjectStorage::addObject(PDFObject object)
{
    PDFObjectReference reference(m_objects.size(), 0);
    m_objects.emplace_back(0, qMove(object));
    return reference;
}

void PDFObjectStorage::setObject(PDFObjectReference reference, PDFObject object)
{
    m_objects[reference.objectNumber] = Entry(reference.generation, qMove(object));
}

void PDFObjectStorage::updateTrailerDictionary(PDFObject trailerDictionary)
{
    m_trailerDictionary = PDFObjectManipulator::merge(m_trailerDictionary, trailerDictionary, PDFObjectManipulator::RemoveNullObjects);
}

PDFDocumentDataLoaderDecorator::PDFDocumentDataLoaderDecorator(const PDFDocument* document)
    : m_storage(&document->getStorage())
{

}

QByteArray PDFDocumentDataLoaderDecorator::readName(const PDFObject& object) const
{
    const PDFObject& dereferencedObject = m_storage->getObject(object);
    if (dereferencedObject.isName())
    {
        return dereferencedObject.getString();
    }

    return QByteArray();
}

QByteArray PDFDocumentDataLoaderDecorator::readString(const PDFObject& object) const
{
    const PDFObject& dereferencedObject = m_storage->getObject(object);
    if (dereferencedObject.isString())
    {
        return dereferencedObject.getString();
    }

    return QByteArray();
}

PDFInteger PDFDocumentDataLoaderDecorator::readInteger(const PDFObject& object, PDFInteger defaultValue) const
{
    const PDFObject& dereferencedObject = m_storage->getObject(object);
    if (dereferencedObject.isInt())
    {
        return dereferencedObject.getInteger();
    }

    return defaultValue;
}

PDFReal PDFDocumentDataLoaderDecorator::readNumber(const PDFObject& object, PDFReal defaultValue) const
{
    const PDFObject& dereferencedObject = m_storage->getObject(object);

    if (dereferencedObject.isReal())
    {
        return dereferencedObject.getReal();
    } else if (dereferencedObject.isInt())
    {
        return dereferencedObject.getInteger();
    }

    return defaultValue;
}

bool PDFDocumentDataLoaderDecorator::readBoolean(const PDFObject& object, bool defaultValue) const
{
    const PDFObject& dereferencedObject = m_storage->getObject(object);

    if (dereferencedObject.isBool())
    {
        return dereferencedObject.getBool();
    }

    return defaultValue;
}

QString PDFDocumentDataLoaderDecorator::readTextString(const PDFObject& object, const QString& defaultValue) const
{
    const PDFObject& dereferencedObject = m_storage->getObject(object);
    if (dereferencedObject.isString())
    {
        return PDFEncoding::convertTextString(dereferencedObject.getString());
    }

    return defaultValue;
}

QRectF PDFDocumentDataLoaderDecorator::readRectangle(const PDFObject& object, const QRectF& defaultValue) const
{
    const PDFObject& dereferencedObject = m_storage->getObject(object);
    if (dereferencedObject.isArray())
    {
        const PDFArray* array = dereferencedObject.getArray();
        if (array->getCount() == 4)
        {
            std::array<PDFReal, 4> items;
            for (size_t i = 0; i < 4; ++i)
            {
                const PDFObject& object = m_storage->getObject(array->getItem(i));
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

QMatrix PDFDocumentDataLoaderDecorator::readMatrixFromDictionary(const PDFDictionary* dictionary, const char* key, QMatrix defaultValue) const
{
    if (dictionary->hasKey(key))
    {
        std::vector<PDFReal> matrixNumbers = readNumberArrayFromDictionary(dictionary, key);
        if (matrixNumbers.size() != 6)
        {
            throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid number of matrix elements. Expected 6, actual %1.").arg(matrixNumbers.size()));
        }

        return QMatrix(matrixNumbers[0], matrixNumbers[1], matrixNumbers[2], matrixNumbers[3], matrixNumbers[4], matrixNumbers[5]);
    }

    return defaultValue;
}

std::vector<PDFReal> PDFDocumentDataLoaderDecorator::readNumberArrayFromDictionary(const PDFDictionary* dictionary,
                                                                                   const char* key,
                                                                                   std::vector<PDFReal> defaultValue) const
{
    if (dictionary->hasKey(key))
    {
        return readNumberArray(dictionary->get(key), defaultValue);
    }

    return defaultValue;
}

std::vector<PDFInteger> PDFDocumentDataLoaderDecorator::readIntegerArrayFromDictionary(const PDFDictionary* dictionary, const char* key) const
{
    if (dictionary->hasKey(key))
    {
        return readIntegerArray(dictionary->get(key));
    }

    return std::vector<PDFInteger>();
}

PDFReal PDFDocumentDataLoaderDecorator::readNumberFromDictionary(const PDFDictionary* dictionary, const char* key, PDFReal defaultValue) const
{
    if (dictionary->hasKey(key))
    {
        return readNumber(dictionary->get(key), defaultValue);
    }

    return defaultValue;
}

PDFReal PDFDocumentDataLoaderDecorator::readNumberFromDictionary(const PDFDictionary* dictionary, const QByteArray& key, PDFReal defaultValue) const
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

QString PDFDocumentDataLoaderDecorator::readTextStringFromDictionary(const PDFDictionary* dictionary, const char* key, const QString& defaultValue) const
{
    if (dictionary->hasKey(key))
    {
        return readTextString(dictionary->get(key), defaultValue);
    }

    return defaultValue;
}

std::vector<PDFObjectReference> PDFDocumentDataLoaderDecorator::readReferenceArrayFromDictionary(const PDFDictionary* dictionary, const char* key) const
{
    if (dictionary->hasKey(key))
    {
        return readReferenceArray(dictionary->get(key));
    }

    return std::vector<PDFObjectReference>();
}

std::vector<PDFReal> PDFDocumentDataLoaderDecorator::readNumberArray(const PDFObject& object, std::vector<PDFReal> defaultValue) const
{
    const PDFObject& dereferencedObject = m_storage->getObject(object);
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
                return defaultValue;
            }
            result.push_back(number);
        }

        // We assume, that RVO (return value optimization) will not work for this function
        // (multiple return points).
        return std::move(result);
    }

    return defaultValue;
}

std::vector<PDFInteger> PDFDocumentDataLoaderDecorator::readIntegerArray(const PDFObject& object) const
{
    const PDFObject& dereferencedObject = m_storage->getObject(object);
    if (dereferencedObject.isArray())
    {
        const PDFArray* array = dereferencedObject.getArray();
        std::vector<PDFInteger> result;
        const size_t count = array->getCount();
        result.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            // This value is not representable in the current PDF parser. So we assume we
            // can't get this value.
            constexpr const PDFInteger INVALID_VALUE = std::numeric_limits<PDFInteger>::max();
            const PDFReal number = readInteger(array->getItem(i), INVALID_VALUE);
            if (number == INVALID_VALUE)
            {
                return std::vector<PDFInteger>();
            }
            result.push_back(number);
        }

        // We assume, that RVO (return value optimization) will not work for this function
        // (multiple return points).
        return std::move(result);
    }

    return std::vector<PDFInteger>();
}

PDFObjectReference PDFDocumentDataLoaderDecorator::readReferenceFromDictionary(const PDFDictionary* dictionary, const char* key) const
{
    const PDFObject& object = dictionary->get(key);

    if (object.isReference())
    {
        return object.getReference();
    }

    return PDFObjectReference();
}

std::vector<PDFObjectReference> PDFDocumentDataLoaderDecorator::readReferenceArray(const PDFObject& object) const
{
    const PDFObject& dereferencedObject = m_storage->getObject(object);
    if (dereferencedObject.isArray())
    {
        const PDFArray* array = dereferencedObject.getArray();
        std::vector<PDFObjectReference> result;
        const size_t count = array->getCount();
        result.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            const PDFObject& referenceObject = array->getItem(i);
            if (referenceObject.isReference())
            {
                result.push_back(referenceObject.getReference());
            }
            else
            {
                result.clear();
                break;
            }
        }

        // We assume, that RVO (return value optimization) will not work for this function
        // (multiple return points).
        return std::move(result);
    }

    return std::vector<PDFObjectReference>();
}

std::vector<QByteArray> PDFDocumentDataLoaderDecorator::readNameArray(const PDFObject& object) const
{
    const PDFObject& dereferencedObject = m_storage->getObject(object);
    if (dereferencedObject.isArray())
    {
        const PDFArray* array = dereferencedObject.getArray();
        std::vector<QByteArray> result;
        const size_t count = array->getCount();
        result.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            const PDFObject& nameObject = array->getItem(i);
            if (nameObject.isName())
            {
                result.push_back(nameObject.getString());
            }
            else
            {
                result.clear();
                break;
            }
        }

        // We assume, that RVO (return value optimization) will not work for this function
        // (multiple return points).
        return std::move(result);
    }

    return std::vector<QByteArray>();
}

std::vector<QByteArray> PDFDocumentDataLoaderDecorator::readNameArrayFromDictionary(const PDFDictionary* dictionary, const char* key) const
{
    if (dictionary->hasKey(key))
    {
        return readNameArray(dictionary->get(key));
    }

    return std::vector<QByteArray>();
}

bool PDFDocumentDataLoaderDecorator::readBooleanFromDictionary(const PDFDictionary* dictionary, const char* key, bool defaultValue) const
{
    if (dictionary->hasKey(key))
    {
        return readBoolean(dictionary->get(key), defaultValue);
    }

    return defaultValue;
}

QByteArray PDFDocumentDataLoaderDecorator::readNameFromDictionary(const PDFDictionary* dictionary, const char* key) const
{
    if (dictionary->hasKey(key))
    {
        return readName(dictionary->get(key));
    }

    return QByteArray();
}

QByteArray PDFDocumentDataLoaderDecorator::readStringFromDictionary(const PDFDictionary* dictionary, const char* key) const
{
    if (dictionary->hasKey(key))
    {
        return readString(dictionary->get(key));
    }

    return QByteArray();
}

std::vector<QByteArray> PDFDocumentDataLoaderDecorator::readStringArrayFromDictionary(const PDFDictionary* dictionary, const char* key) const
{
    if (dictionary->hasKey(key))
    {
        return readStringArray(dictionary->get(key));
    }

    return std::vector<QByteArray>();
}

QStringList PDFDocumentDataLoaderDecorator::readTextStringList(const PDFObject& object)
{
    QStringList result;

    const PDFObject& dereferencedObject = m_storage->getObject(object);
    if (dereferencedObject.isArray())
    {
        const PDFArray* array = dereferencedObject.getArray();
        const size_t count = array->getCount();
        result.reserve(int(count));

        for (size_t i = 0; i < count; ++i)
        {
            result << readTextString(array->getItem(i), QString());
        }
    }

    return result;
}

std::optional<QByteArray> PDFDocumentDataLoaderDecorator::readOptionalStringFromDictionary(const PDFDictionary* dictionary, const char* key) const
{
    if (dictionary->hasKey(key))
    {
        return readStringFromDictionary(dictionary, key);
    }
    return std::nullopt;
}

std::optional<PDFInteger> PDFDocumentDataLoaderDecorator::readOptionalIntegerFromDictionary(const PDFDictionary* dictionary, const char* key) const
{
    if (dictionary->hasKey(key))
    {
        PDFInteger integer = readIntegerFromDictionary(dictionary, key, std::numeric_limits<PDFInteger>::max());
        if (integer != std::numeric_limits<PDFInteger>::max())
        {
            return integer;
        }
    }
    return std::nullopt;
}

std::vector<QByteArray> PDFDocumentDataLoaderDecorator::readStringArray(const PDFObject& object) const
{
    const PDFObject& dereferencedObject = m_storage->getObject(object);
    if (dereferencedObject.isArray())
    {
        const PDFArray* array = dereferencedObject.getArray();
        std::vector<QByteArray> result;
        const size_t count = array->getCount();
        result.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            const PDFObject& stringObject = array->getItem(i);
            if (stringObject.isString())
            {
                result.push_back(stringObject.getString());
            }
            else
            {
                result.clear();
                break;
            }
        }

        // We assume, that RVO (return value optimization) will not work for this function
        // (multiple return points).
        return std::move(result);
    }

    return std::vector<QByteArray>();
}

}   // namespace pdf
