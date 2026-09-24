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

#include "pdfobject.h"
#include "pdfvisitor.h"
#include "pdfexception.h"
#include "pdfdbgheap.h"

#include <set>

namespace pdf
{

bool PDFObject::operator==(const PDFObject& other) const
{
    const Type type = getType();
    if (type != other.getType())
    {
        return false;
    }

    switch (type)
    {
        case Type::Null:
            return true;

        case Type::Bool:
            return getBool() == other.getBool();

        case Type::Int:
            return getInteger() == other.getInteger();

        case Type::Real:
            // NaN is not equal to anything, including itself
            return getReal() == other.getReal();

        case Type::String:
        case Type::Name:
        {
            if (isInplaceString() && other.isInplaceString())
            {
                // Tag (with type) and the whole inplace string are compared
                // at once, unused characters are always zero.
                return std::memcmp(&m_storage.string, &other.m_storage.string, sizeof(InplaceStringStorage)) == 0;
            }

            return getStringView() == other.getStringView();
        }

        case Type::Array:
            return *getArray() == *other.getArray();

        case Type::Dictionary:
            return *getDictionary() == *other.getDictionary();

        case Type::Stream:
            return *getStream() == *other.getStream();

        case Type::Reference:
            return getReference() == other.getReference();

        default:
            Q_ASSERT(false);
            break;
    }

    return false;
}

void PDFObject::accept(PDFAbstractVisitor* visitor) const
{
    switch (getType())
    {
        case Type::Null:
            visitor->visitNull();
            break;

        case Type::Bool:
            visitor->visitBool(getBool());
            break;

        case Type::Int:
            visitor->visitInt(getInteger());
            break;

        case Type::Real:
            visitor->visitReal(getReal());
            break;

        case Type::String:
            visitor->visitString(getStringObject());
            break;

        case Type::Name:
            visitor->visitName(getStringObject());
            break;

        case Type::Array:
            visitor->visitArray(getArray());
            break;

        case Type::Dictionary:
            visitor->visitDictionary(getDictionary());
            break;

        case Type::Stream:
            visitor->visitStream(getStream());
            break;

        case Type::Reference:
            visitor->visitReference(getReference());
            break;

        default:
            Q_ASSERT(false);
    }
}

PDFObject PDFObject::createArray(PDFArray array)
{
    array.optimize();
    return createWithContent(Type::Array, new PDFArray(std::move(array)));
}

PDFObject PDFObject::createDictionary(PDFDictionary dictionary)
{
    dictionary.optimize();
    return createWithContent(Type::Dictionary, new PDFDictionary(std::move(dictionary)));
}

PDFObject PDFObject::createStream(PDFStream stream)
{
    stream.optimize();
    return createWithContent(Type::Stream, new PDFStream(std::move(stream)));
}

PDFObject PDFObject::createName(QByteArray name)
{
    return createStringObject(Type::Name, std::move(name));
}

PDFObject PDFObject::createString(QByteArray name)
{
    return createStringObject(Type::String, std::move(name));
}

PDFObject PDFObject::createName(PDFStringRef name)
{
    return createStringObject(Type::Name, name);
}

PDFObject PDFObject::createString(PDFStringRef name)
{
    return createStringObject(Type::String, name);
}

PDFObject PDFObject::createWithContent(Type type, PDFObjectContent* content) noexcept
{
    Q_ASSERT(content && content->getReferenceCount() == 0);
    content->addReference();

    ValueStorage storage;
    storage.tag = makeTag(type, true);
    storage.content = content;
    return PDFObject(Storage(storage));
}

PDFObject PDFObject::createStringObject(Type type, QByteArray string)
{
    Q_ASSERT(type == Type::String || type == Type::Name);

    if (string.size() <= PDFInplaceString::MAX_STRING_SIZE)
    {
        InplaceStringStorage storage;
        storage.tag = makeTag(type);
        storage.string = PDFInplaceString(string.constData(), static_cast<int>(string.size()));
        return PDFObject(Storage(storage));
    }

    PDFString* content = new PDFString(std::move(string));
    content->optimize();
    return createWithContent(type, content);
}

PDFObject PDFObject::createStringObject(Type type, PDFStringRef string)
{
    Q_ASSERT(type == Type::String || type == Type::Name);

    if (string.inplaceString)
    {
        // Size is checked, members of the inplace string are public. Inplace string is
        // constructed again, so characters behind the end of the string are zero.
        const int size = qMin(static_cast<int>(string.inplaceString->size), PDFInplaceString::MAX_STRING_SIZE);

        InplaceStringStorage storage;
        storage.tag = makeTag(type);
        storage.string = PDFInplaceString(string.inplaceString->string.data(), size);
        return PDFObject(Storage(storage));
    }

    // Byte array is shared with the memory string, no data are copied (shared
    // byte array is not shrinked)
    return createStringObject(type, string.getString());
}

PDFObject PDFObject::createReferenceWithContent(const PDFObjectReference& reference)
{
    return createWithContent(Type::Reference, new PDFReferenceContent(reference));
}

void PDFObject::destroyContent(uint8_t tag, PDFObjectContent* content) noexcept
{
    Q_ASSERT(tag & CONTENT_FLAG);

    switch (static_cast<Type>(tag & TYPE_MASK))
    {
        case Type::String:
        case Type::Name:
            delete static_cast<PDFString*>(content);
            break;

        case Type::Array:
            delete static_cast<PDFArray*>(content);
            break;

        case Type::Dictionary:
            delete static_cast<PDFDictionary*>(content);
            break;

        case Type::Stream:
            delete static_cast<PDFStream*>(content);
            break;

        case Type::Reference:
            delete static_cast<PDFReferenceContent*>(content);
            break;

        default:
            // Other types never have content
            Q_ASSERT(false);
            break;
    }
}

void PDFObject::throwInvalidType(Type expectedType)
{
    QString typeName;
    switch (expectedType)
    {
        case Type::Bool:
            typeName = PDFTranslationContext::tr("boolean");
            break;

        case Type::Int:
            typeName = PDFTranslationContext::tr("integer");
            break;

        case Type::Real:
            typeName = PDFTranslationContext::tr("real number");
            break;

        case Type::String:
        case Type::Name:
            typeName = PDFTranslationContext::tr("string");
            break;

        case Type::Array:
            typeName = PDFTranslationContext::tr("array");
            break;

        case Type::Dictionary:
            typeName = PDFTranslationContext::tr("dictionary");
            break;

        case Type::Stream:
            typeName = PDFTranslationContext::tr("stream");
            break;

        case Type::Reference:
            typeName = PDFTranslationContext::tr("reference");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    throw PDFException(PDFTranslationContext::tr("Invalid type of the object, %1 was expected.").arg(typeName));
}

void PDFString::setString(const QByteArray& string)
{
    m_string = string;
}

void PDFString::optimize()
{
    // Shared data would be copied, so they are not shrinked
    if (m_string.isDetached())
    {
        m_string.shrink_to_fit();
    }
}

void PDFArray::appendItem(PDFObject object)
{
    m_objects.push_back(std::move(object));
}

void PDFArray::optimize()
{
    m_objects.shrink_to_fit();
}

bool PDFDictionary::operator==(const PDFDictionary& other) const
{
    return m_dictionary == other.m_dictionary;
}

constinit const PDFObject PDFDictionary::s_nullObject;

const PDFObject& PDFDictionary::get(const QByteArray& key) const
{
    auto it = find(key);
    return (it != m_dictionary.cend()) ? it->second : s_nullObject;
}

const PDFObject& PDFDictionary::get(const char* key) const
{
    auto it = find(key);
    return (it != m_dictionary.cend()) ? it->second : s_nullObject;
}

const PDFObject& PDFDictionary::get(const PDFInplaceOrMemoryString& key) const
{
    auto it = find(key);
    return (it != m_dictionary.cend()) ? it->second : s_nullObject;
}

void PDFDictionary::removeEntry(const char* key)
{
    auto it = find(key);
    if (it != m_dictionary.end())
    {
        m_dictionary.erase(it);
    }
}

void PDFDictionary::setEntry(const PDFInplaceOrMemoryString& key, PDFObject&& value)
{
    auto it = find(key);
    if (it != m_dictionary.end())
    {
        it->second = qMove(value);
    }
    else
    {
        addEntry(key, qMove(value));
    }
}

void PDFDictionary::removeNullObjects()
{
    m_dictionary.erase(std::remove_if(m_dictionary.begin(), m_dictionary.end(), [](const DictionaryEntry& entry) { return entry.second.isNull(); }), m_dictionary.end());
    m_dictionary.shrink_to_fit();
}

void PDFDictionary::optimize()
{
    m_dictionary.shrink_to_fit();
}

std::vector<PDFDictionary::DictionaryEntry>::const_iterator PDFDictionary::find(const QByteArray& key) const
{
    return find(key.constData(), static_cast<size_t>(key.size()));
}

std::vector<PDFDictionary::DictionaryEntry>::iterator PDFDictionary::find(const QByteArray& key)
{
    return find(key.constData(), static_cast<size_t>(key.size()));
}

std::vector<PDFDictionary::DictionaryEntry>::const_iterator PDFDictionary::find(const char* key) const
{
    return find(key, std::strlen(key));
}

std::vector<PDFDictionary::DictionaryEntry>::iterator PDFDictionary::find(const char* key)
{
    return find(key, std::strlen(key));
}

std::vector<PDFDictionary::DictionaryEntry>::const_iterator PDFDictionary::find(const PDFInplaceOrMemoryString& key) const
{
    return std::find_if(m_dictionary.cbegin(), m_dictionary.cend(), [&key](const DictionaryEntry& entry) { return entry.first == key; });
}

std::vector<PDFDictionary::DictionaryEntry>::iterator PDFDictionary::find(const PDFInplaceOrMemoryString& key)
{
    return std::find_if(m_dictionary.begin(), m_dictionary.end(), [&key](const DictionaryEntry& entry) { return entry.first == key; });
}

std::vector<PDFDictionary::DictionaryEntry>::const_iterator PDFDictionary::find(const char* key, size_t length) const
{
    if (length <= static_cast<size_t>(PDFInplaceString::MAX_STRING_SIZE))
    {
        // Keys of this length are always stored inplace, so we convert the key
        // to the inplace string once (no allocation) and then compare whole
        // storage of the key (16 bytes) at once.
        const PDFInplaceOrMemoryString::RawStorage inplaceKey = PDFInplaceOrMemoryString::createInplaceRawStorage(key, length);
        return std::find_if(m_dictionary.cbegin(), m_dictionary.cend(), [&inplaceKey](const DictionaryEntry& entry) { return PDFInplaceOrMemoryString::isRawEqual(entry.first.getRawStorage(), inplaceKey); });
    }

    // Keys of other sizes are rejected without access to the string in the heap
    return std::find_if(m_dictionary.cbegin(), m_dictionary.cend(), [key, length](const DictionaryEntry& entry) { return !entry.first.isInplace() && entry.first.canHaveSize(length) && entry.first.equals(key, length); });
}

std::vector<PDFDictionary::DictionaryEntry>::iterator PDFDictionary::find(const char* key, size_t length)
{
    auto it = std::as_const(*this).find(key, length);
    return std::next(m_dictionary.begin(), std::distance(m_dictionary.cbegin(), it));
}

PDFObject PDFObjectManipulator::merge(PDFObject left, PDFObject right, MergeFlags flags)
{
    const bool leftHasDictionary = left.isDictionary() || left.isStream();

    if (left.getType() != right.getType() && (leftHasDictionary != right.isDictionary()))
    {
        return right;
    }

    if (left.isStream())
    {
        Q_ASSERT(right.isDictionary() || right.isStream());
        const PDFStream* leftStream = left.getStream();
        const PDFStream* rightStream = right.isStream() ? right.getStream() : nullptr;

        PDFDictionary targetDictionary = *leftStream->getDictionary();
        const PDFDictionary& sourceDictionary = rightStream ? *rightStream->getDictionary() : *right.getDictionary();

        for (size_t i = 0, count = sourceDictionary.getCount(); i < count; ++i)
        {
            const auto& key = sourceDictionary.getKey(i);
            PDFObject value = merge(targetDictionary.get(key), sourceDictionary.getValue(i), flags);
            targetDictionary.setEntry(key, qMove(value));
        }

        if (flags.testFlag(RemoveNullObjects))
        {
            targetDictionary.removeNullObjects();
        }

        return PDFObject::createStream(PDFStream(qMove(targetDictionary), QByteArray(rightStream ? *rightStream->getContent() : *leftStream->getContent())));
    }
    if (left.isDictionary())
    {
        Q_ASSERT(right.isDictionary());

        PDFDictionary targetDictionary = *left.getDictionary();
        const PDFDictionary& sourceDictionary = *right.getDictionary();

        for (size_t i = 0, count = sourceDictionary.getCount(); i < count; ++i)
        {
            const auto& key = sourceDictionary.getKey(i);
            PDFObject value = merge(targetDictionary.get(key), sourceDictionary.getValue(i), flags);
            targetDictionary.setEntry(key, qMove(value));
        }

        if (flags.testFlag(RemoveNullObjects))
        {
            targetDictionary.removeNullObjects();
        }

        return PDFObject::createDictionary(qMove(targetDictionary));
    }
    else if (left.isArray() && right.isArray() && flags.testFlag(ConcatenateArrays))
    {
        // Concatenate arrays
        const PDFArray* leftArray = left.getArray();
        const PDFArray* rightArray = right.getArray();

        std::vector<PDFObject> objects;
        objects.reserve(leftArray->getCount() + rightArray->getCount());
        for (size_t i = 0, count = leftArray->getCount(); i < count; ++i)
        {
            objects.emplace_back(leftArray->getItem(i));
        }
        for (size_t i = 0, count = rightArray->getCount(); i < count; ++i)
        {
            objects.emplace_back(rightArray->getItem(i));
        }
        return PDFObject::createArray(qMove(objects));
    }

    return right;
}

PDFObject PDFObjectManipulator::removeNullObjects(PDFObject object)
{
    return merge(object, object, RemoveNullObjects);
}

PDFObject PDFObjectManipulator::removeDuplicitReferencesInArrays(PDFObject object)
{
    switch (object.getType())
    {
        case PDFObject::Type::Stream:
        {
            const PDFStream* stream = object.getStream();
            PDFDictionary dictionary = *stream->getDictionary();

            for (size_t i = 0, count = dictionary.getCount(); i < count; ++i)
            {
                dictionary.setEntry(dictionary.getKey(i), removeDuplicitReferencesInArrays(dictionary.getValue(i)));
            }

            return PDFObject::createStream(PDFStream(qMove(dictionary), QByteArray(*stream->getContent())));
        }

        case PDFObject::Type::Dictionary:
        {
            PDFDictionary dictionary = *object.getDictionary();

            for (size_t i = 0, count = dictionary.getCount(); i < count; ++i)
            {
                dictionary.setEntry(dictionary.getKey(i), removeDuplicitReferencesInArrays(dictionary.getValue(i)));
            }

            return PDFObject::createDictionary(qMove(dictionary));
        }

        case PDFObject::Type::Array:
        {
            PDFArray array;
            std::set<PDFObjectReference> usedReferences;

            for (const PDFObject& arrayObject : *object.getArray())
            {
                if (arrayObject.isReference())
                {
                    PDFObjectReference reference = arrayObject.getReference();
                    if (!usedReferences.count(reference))
                    {
                        usedReferences.insert(reference);
                        array.appendItem(PDFObject::createReference(reference));
                    }
                }
                else
                {
                    array.appendItem(removeDuplicitReferencesInArrays(arrayObject));
                }
            }

            return PDFObject::createArray(qMove(array));
        }

        default:
            return object;
    }
}

QByteArray PDFStringRef::getString() const
{
    if (inplaceString)
    {
        // Size is checked, members of the inplace string are public
        const int size = qMin(static_cast<int>(inplaceString->size), PDFInplaceString::MAX_STRING_SIZE);
        return QByteArray(inplaceString->string.data(), size);
    }
    if (memoryString)
    {
        return memoryString->getString();
    }
    return QByteArray();
}

PDFInplaceOrMemoryString::PDFInplaceOrMemoryString(const char* string) :
    PDFInplaceOrMemoryString(string, std::strlen(string))
{

}

PDFInplaceOrMemoryString::PDFInplaceOrMemoryString(QByteArray string)
{
    if (string.size() <= PDFInplaceString::MAX_STRING_SIZE)
    {
        InplaceStorage storage;
        storage.string = PDFInplaceString(string.constData(), static_cast<int>(string.size()));
        m_storage = Storage(storage);
    }
    else
    {
        MemoryStorage storage;
        storage.size = getStoredSize(static_cast<size_t>(string.size()));
        storage.string = createMemoryString(std::move(string));
        m_storage = Storage(storage);
    }
}

PDFString* PDFInplaceOrMemoryString::createMemoryString(QByteArray string)
{
    Q_ASSERT(string.size() > PDFInplaceString::MAX_STRING_SIZE);

    PDFString* memoryString = new PDFString(std::move(string));
    memoryString->optimize();
    memoryString->addReference();
    return memoryString;
}

void PDFInplaceOrMemoryString::destroyMemoryString(PDFString* string) noexcept
{
    delete string;
}

}   // namespace pdf
