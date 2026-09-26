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
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <new>

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

PDFObject PDFObject::createArray(PDFArrayBuilder array)
{
    return createWithContent(Type::Array, array.takeArray());
}

PDFObject PDFObject::createDictionary(PDFDictionaryBuilder dictionary)
{
    return createWithContent(Type::Dictionary, dictionary.takeDictionary());
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

    std::unique_ptr<PDFString> content(new PDFString(std::move(string)));
    content->optimize();
    return createWithContent(type, content.release());
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
            PDFArray::destroy(static_cast<PDFArray*>(content));
            break;

        case Type::Dictionary:
            PDFDictionary::destroy(static_cast<PDFDictionary*>(content));
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
    m_string = takeOwnedByteArray(string);
}

void PDFString::optimize()
{
    // Shared data would be copied, so they are not shrinked
    if (m_string.isDetached())
    {
        m_string.shrink_to_fit();
    }
}

/// Memory of the arrays and dictionaries - header followed by the items in a single
/// allocation. The memory is allocated by malloc and the items are constructed in
/// place, so the allocation doesn't depend on the macros of the debug heap.
template<typename Header, typename Item>
class PDFTrailingItems
{
public:
    PDFTrailingItems() = delete;

    /// Returns the items stored behind the header
    static Item* getItems(Header* header) { return std::launder(reinterpret_cast<Item*>(reinterpret_cast<char*>(header) + sizeof(Header))); }

    /// Returns the items stored behind the header
    static const Item* getItems(const Header* header) { return std::launder(reinterpret_cast<const Item*>(reinterpret_cast<const char*>(header) + sizeof(Header))); }

    /// Allocates the header with the memory for the given number of items,
    /// no items are constructed
    static Header* allocate(size_t capacity)
    {
        if (capacity > std::numeric_limits<uint32_t>::max() || capacity > (std::numeric_limits<size_t>::max() - sizeof(Header)) / sizeof(Item))
        {
            throw std::length_error("Too many items of the array or dictionary");
        }

        void* memory = std::malloc(sizeof(Header) + capacity * sizeof(Item));
        if (!memory)
        {
            throw std::bad_alloc();
        }

#pragma push_macro("new")
#undef new
        return ::new (memory) Header();
#pragma pop_macro("new")
    }

    /// Destroys the items and the header and releases the memory
    static void destroy(Header* header) noexcept
    {
        if (header)
        {
            std::destroy_n(getItems(header), header->m_count);
            header->~Header();
            std::free(header);
        }
    }

    /// Copies the items to a new memory for the given number of items
    static Header* copy(const Header* header, size_t capacity)
    {
        Q_ASSERT(header && header->m_count <= capacity);

        Header* newHeader = allocate(capacity);
        std::uninitialized_copy_n(getItems(header), header->m_count, getItems(newHeader));
        newHeader->m_count = header->m_count;
        return newHeader;
    }

    /// Moves the items to a new memory for the given number of items, the old
    /// memory is released
    static Header* reallocate(Header* header, size_t capacity)
    {
        Header* newHeader = allocate(capacity);

        if (header)
        {
            Q_ASSERT(header->m_count <= capacity);
            std::uninitialized_move_n(getItems(header), header->m_count, getItems(newHeader));
            newHeader->m_count = header->m_count;
            destroy(header);
        }

        return newHeader;
    }

    /// Creates the header with the items moved from the vector
    static Header* create(std::vector<Item>&& items)
    {
        Header* header = allocate(items.size());
        std::uninitialized_move_n(items.begin(), items.size(), getItems(header));
        header->m_count = static_cast<uint32_t>(items.size());
        items.clear();
        return header;
    }

    /// Enlarges the memory of the builder, so it can store the given number of items
    static void reserve(Header*& header, size_t& capacity, size_t newCapacity)
    {
        if (newCapacity > capacity)
        {
            header = reallocate(header, newCapacity);
            capacity = newCapacity;
        }
    }

    /// Allocates the memory of the builder of exactly the given size
    static void setFixedSize(Header*& header, size_t& capacity, bool& isFixedSize, size_t count)
    {
        // Fixed size can be set only to the empty builder
        Q_ASSERT(!header || header->m_count == 0);

        if (!header || capacity != count)
        {
            header = reallocate(header, count);
            capacity = count;
        }

        isFixedSize = true;
    }

    /// Makes room for one more item in the memory of the builder
    static void prepareAppend(Header*& header, size_t& capacity, bool isFixedSize)
    {
        const size_t count = header ? header->m_count : 0;
        if (count == capacity)
        {
            // Number of the items of the fixed size must not be exceeded. If it
            // is, the memory is enlarged, so the builder remains valid.
            Q_ASSERT(!isFixedSize);
            Q_UNUSED(isFixedSize);
            reserve(header, capacity, std::max<size_t>(count * 2, 4));
        }
    }

    /// Constructs the item at the end of the items, the memory must be available
    template<typename... Arguments>
    static void emplace(Header* header, Arguments&&... arguments)
    {
        std::construct_at(getItems(header) + header->m_count, std::forward<Arguments>(arguments)...);
        ++header->m_count;
    }

    /// Returns the header with the memory of the exact size, the builder becomes empty
    static Header* take(Header*& header, size_t& capacity, bool& isFixedSize)
    {
        // Number of the items of the fixed size must be exactly reached. If it
        // is not, the items are moved to the memory of the exact size.
        Q_ASSERT(!isFixedSize || (header && header->m_count == capacity));

        if (!header)
        {
            header = allocate(0);
        }
        else if (header->m_count != capacity)
        {
            header = reallocate(header, header->m_count);
        }

        capacity = 0;
        isFixedSize = false;
        return std::exchange(header, nullptr);
    }
};

using PDFArrayItems = PDFTrailingItems<PDFArray, PDFObject>;
using PDFDictionaryEntries = PDFTrailingItems<PDFDictionary, PDFDictionary::DictionaryEntry>;

bool PDFArray::operator==(const PDFArray& other) const
{
    return m_count == other.m_count && std::equal(begin(), end(), other.begin());
}

constinit const PDFArray PDFArray::s_emptyArray;

void PDFArray::destroy(PDFArray* array) noexcept
{
    PDFArrayItems::destroy(array);
}

void PDFArray::throwInvalidIndex(size_t index, size_t count)
{
    throw std::out_of_range(QString("Invalid index %1 of the array item, array has %2 items.").arg(index).arg(count).toStdString());
}

PDFArrayBuilder::PDFArrayBuilder(std::vector<PDFObject>&& items) :
    m_array(PDFArrayItems::create(std::move(items)))
{
    m_capacity = m_array->m_count;
}

PDFArrayBuilder::PDFArrayBuilder(const PDFArray& array) :
    m_array(PDFArrayItems::copy(&array, array.m_count)),
    m_capacity(array.m_count)
{

}

PDFArrayBuilder::~PDFArrayBuilder()
{
    PDFArrayItems::destroy(m_array);
}

PDFArrayBuilder::PDFArrayBuilder(const PDFArrayBuilder& other) :
    m_array(other.m_array ? PDFArrayItems::copy(other.m_array, other.m_capacity) : nullptr),
    m_capacity(other.m_array ? other.m_capacity : 0),
    m_isFixedSize(other.m_isFixedSize)
{

}

PDFArrayBuilder::PDFArrayBuilder(PDFArrayBuilder&& other) noexcept :
    m_array(std::exchange(other.m_array, nullptr)),
    m_capacity(std::exchange(other.m_capacity, 0)),
    m_isFixedSize(std::exchange(other.m_isFixedSize, false))
{

}

void PDFArrayBuilder::swap(PDFArrayBuilder& other) noexcept
{
    std::swap(m_array, other.m_array);
    std::swap(m_capacity, other.m_capacity);
    std::swap(m_isFixedSize, other.m_isFixedSize);
}

bool PDFArrayBuilder::operator==(const PDFArrayBuilder& other) const
{
    return getCount() == other.getCount() && std::equal(begin(), end(), other.begin());
}

void PDFArrayBuilder::reserve(size_t count)
{
    PDFArrayItems::reserve(m_array, m_capacity, count);
}

void PDFArrayBuilder::setFixedSize(size_t count)
{
    PDFArrayItems::setFixedSize(m_array, m_capacity, m_isFixedSize, count);
}

void PDFArrayBuilder::appendItem(PDFObject object)
{
    PDFArrayItems::prepareAppend(m_array, m_capacity, m_isFixedSize);
    PDFArrayItems::emplace(m_array, std::move(object));
}

void PDFArrayBuilder::setItem(PDFObject value, size_t index)
{
    if (index >= getCount())
    {
        PDFArray::throwInvalidIndex(index, getCount());
    }

    m_array->getItems()[index] = std::move(value);
}

const PDFObject& PDFArrayBuilder::getItem(size_t index) const
{
    if (index >= getCount())
    {
        PDFArray::throwInvalidIndex(index, getCount());
    }

    return m_array->getItems()[index];
}

PDFArray* PDFArrayBuilder::takeArray()
{
    return PDFArrayItems::take(m_array, m_capacity, m_isFixedSize);
}

bool PDFDictionary::operator==(const PDFDictionary& other) const
{
    return m_count == other.m_count && std::equal(begin(), end(), other.begin());
}

constinit const PDFObject PDFDictionary::s_nullObject;
constinit const PDFDictionary PDFDictionary::s_emptyDictionary;

const PDFObject& PDFDictionary::get(const QByteArray& key) const
{
    const DictionaryEntry* entry = find(begin(), end(), key.constData(), static_cast<size_t>(key.size()));
    return (entry != end()) ? entry->second : s_nullObject;
}

const PDFObject& PDFDictionary::get(const char* key) const
{
    const DictionaryEntry* entry = find(begin(), end(), key, std::strlen(key));
    return (entry != end()) ? entry->second : s_nullObject;
}

const PDFObject& PDFDictionary::get(const PDFInplaceOrMemoryString& key) const
{
    const DictionaryEntry* entry = find(begin(), end(), key);
    return (entry != end()) ? entry->second : s_nullObject;
}

bool PDFDictionary::hasKey(const QByteArray& key) const
{
    return find(begin(), end(), key.constData(), static_cast<size_t>(key.size())) != end();
}

bool PDFDictionary::hasKey(const char* key) const
{
    return find(begin(), end(), key, std::strlen(key)) != end();
}

void PDFDictionary::destroy(PDFDictionary* dictionary) noexcept
{
    PDFDictionaryEntries::destroy(dictionary);
}

const PDFDictionary::DictionaryEntry* PDFDictionary::find(const DictionaryEntry* begin, const DictionaryEntry* end, const char* key, size_t length)
{
    if (length <= static_cast<size_t>(PDFInplaceString::MAX_STRING_SIZE))
    {
        // Keys of this length are always stored inplace, so we convert the key
        // to the inplace string once (no allocation) and then compare whole
        // storage of the key (16 bytes) at once.
        const PDFInplaceOrMemoryString::RawStorage inplaceKey = PDFInplaceOrMemoryString::createInplaceRawStorage(key, length);
        return std::find_if(begin, end, [&inplaceKey](const DictionaryEntry& entry) { return entry.first.isInplace() && PDFInplaceOrMemoryString::isRawEqual(entry.first.getRawStorage(), inplaceKey); });
    }

    // Keys of other sizes are rejected without access to the string in the heap
    return std::find_if(begin, end, [key, length](const DictionaryEntry& entry) { return !entry.first.isInplace() && entry.first.canHaveSize(length) && entry.first.equals(key, length); });
}

const PDFDictionary::DictionaryEntry* PDFDictionary::find(const DictionaryEntry* begin, const DictionaryEntry* end, const PDFInplaceOrMemoryString& key)
{
    return std::find_if(begin, end, [&key](const DictionaryEntry& entry) { return entry.first == key; });
}

PDFDictionaryBuilder::PDFDictionaryBuilder(std::vector<DictionaryEntry>&& entries) :
    m_dictionary(PDFDictionaryEntries::create(std::move(entries)))
{
    m_capacity = m_dictionary->m_count;
}

PDFDictionaryBuilder::PDFDictionaryBuilder(const PDFDictionary& dictionary) :
    m_dictionary(PDFDictionaryEntries::copy(&dictionary, dictionary.m_count)),
    m_capacity(dictionary.m_count)
{

}

PDFDictionaryBuilder::~PDFDictionaryBuilder()
{
    PDFDictionaryEntries::destroy(m_dictionary);
}

PDFDictionaryBuilder::PDFDictionaryBuilder(const PDFDictionaryBuilder& other) :
    m_dictionary(other.m_dictionary ? PDFDictionaryEntries::copy(other.m_dictionary, other.m_capacity) : nullptr),
    m_capacity(other.m_dictionary ? other.m_capacity : 0),
    m_isFixedSize(other.m_isFixedSize)
{

}

PDFDictionaryBuilder::PDFDictionaryBuilder(PDFDictionaryBuilder&& other) noexcept :
    m_dictionary(std::exchange(other.m_dictionary, nullptr)),
    m_capacity(std::exchange(other.m_capacity, 0)),
    m_isFixedSize(std::exchange(other.m_isFixedSize, false))
{

}

void PDFDictionaryBuilder::swap(PDFDictionaryBuilder& other) noexcept
{
    std::swap(m_dictionary, other.m_dictionary);
    std::swap(m_capacity, other.m_capacity);
    std::swap(m_isFixedSize, other.m_isFixedSize);
}

bool PDFDictionaryBuilder::operator==(const PDFDictionaryBuilder& other) const
{
    return getCount() == other.getCount() && std::equal(begin(), end(), other.begin());
}

void PDFDictionaryBuilder::reserve(size_t count)
{
    PDFDictionaryEntries::reserve(m_dictionary, m_capacity, count);
}

void PDFDictionaryBuilder::setFixedSize(size_t count)
{
    PDFDictionaryEntries::setFixedSize(m_dictionary, m_capacity, m_isFixedSize, count);
}

const PDFObject& PDFDictionaryBuilder::get(const QByteArray& key) const
{
    const DictionaryEntry* entry = PDFDictionary::find(begin(), end(), key.constData(), static_cast<size_t>(key.size()));
    return (entry != end()) ? entry->second : PDFDictionary::s_nullObject;
}

const PDFObject& PDFDictionaryBuilder::get(const char* key) const
{
    const DictionaryEntry* entry = PDFDictionary::find(begin(), end(), key, std::strlen(key));
    return (entry != end()) ? entry->second : PDFDictionary::s_nullObject;
}

const PDFObject& PDFDictionaryBuilder::get(const PDFInplaceOrMemoryString& key) const
{
    const DictionaryEntry* entry = PDFDictionary::find(begin(), end(), key);
    return (entry != end()) ? entry->second : PDFDictionary::s_nullObject;
}

bool PDFDictionaryBuilder::hasKey(const QByteArray& key) const
{
    return PDFDictionary::find(begin(), end(), key.constData(), static_cast<size_t>(key.size())) != end();
}

bool PDFDictionaryBuilder::hasKey(const char* key) const
{
    return PDFDictionary::find(begin(), end(), key, std::strlen(key)) != end();
}

void PDFDictionaryBuilder::addEntry(PDFInplaceOrMemoryString&& key, PDFObject&& value)
{
    prepareAppend();
    PDFDictionaryEntries::emplace(m_dictionary, std::move(key), std::move(value));
}

void PDFDictionaryBuilder::addEntry(const PDFInplaceOrMemoryString& key, PDFObject&& value)
{
    // Key is copied first, it can be the key of this builder (for example,
    // getKey(i)), which is moved to a new memory, when the memory is enlarged.
    PDFInplaceOrMemoryString copiedKey(key);
    addEntry(std::move(copiedKey), std::move(value));
}

void PDFDictionaryBuilder::setEntry(const PDFInplaceOrMemoryString& key, PDFObject&& value)
{
    if (DictionaryEntry* entry = findEntry(key))
    {
        entry->second = std::move(value);
    }
    else
    {
        addEntry(key, std::move(value));
    }
}

void PDFDictionaryBuilder::removeEntry(const char* key)
{
    if (!m_dictionary)
    {
        return;
    }

    DictionaryEntry* entries = m_dictionary->getEntries();
    DictionaryEntry* entriesEnd = entries + m_dictionary->m_count;
    DictionaryEntry* entry = const_cast<DictionaryEntry*>(PDFDictionary::find(entries, entriesEnd, key, std::strlen(key)));

    if (entry != entriesEnd)
    {
        std::move(entry + 1, entriesEnd, entry);
        std::destroy_at(entriesEnd - 1);
        --m_dictionary->m_count;
    }
}

void PDFDictionaryBuilder::removeNullObjects()
{
    if (!m_dictionary)
    {
        return;
    }

    DictionaryEntry* entries = m_dictionary->getEntries();
    DictionaryEntry* entriesEnd = entries + m_dictionary->m_count;
    DictionaryEntry* newEnd = std::remove_if(entries, entriesEnd, [](const DictionaryEntry& entry) { return entry.second.isNull(); });
    std::destroy(newEnd, entriesEnd);
    m_dictionary->m_count = static_cast<uint32_t>(newEnd - entries);
}

PDFDictionary* PDFDictionaryBuilder::takeDictionary()
{
    return PDFDictionaryEntries::take(m_dictionary, m_capacity, m_isFixedSize);
}

PDFDictionaryBuilder::DictionaryEntry* PDFDictionaryBuilder::findEntry(const PDFInplaceOrMemoryString& key)
{
    if (!m_dictionary)
    {
        return nullptr;
    }

    DictionaryEntry* entries = m_dictionary->getEntries();
    DictionaryEntry* entriesEnd = entries + m_dictionary->m_count;
    DictionaryEntry* entry = const_cast<DictionaryEntry*>(PDFDictionary::find(entries, entriesEnd, key));
    return entry != entriesEnd ? entry : nullptr;
}

void PDFDictionaryBuilder::prepareAppend()
{
    PDFDictionaryEntries::prepareAppend(m_dictionary, m_capacity, m_isFixedSize);
}

PDFStream::PDFStream() :
    PDFStream(PDFDictionaryBuilder(), QByteArray())
{

}

PDFStream::PDFStream(PDFDictionaryBuilder dictionary, QByteArray&& content) :
    m_dictionary(PDFObject::createDictionary(std::move(dictionary))),
    m_content(takeOwnedByteArray(std::move(content)))
{

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

        PDFDictionaryBuilder targetDictionary(*leftStream->getDictionary());
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

        PDFDictionaryBuilder targetDictionary(*left.getDictionary());
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

        PDFArrayBuilder objects;
        objects.setFixedSize(leftArray->getCount() + rightArray->getCount());
        for (const PDFObject& item : *leftArray)
        {
            objects.appendItem(item);
        }
        for (const PDFObject& item : *rightArray)
        {
            objects.appendItem(item);
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
            PDFDictionaryBuilder dictionary(*stream->getDictionary());

            for (size_t i = 0, count = dictionary.getCount(); i < count; ++i)
            {
                dictionary.setEntry(dictionary.getKey(i), removeDuplicitReferencesInArrays(dictionary.getValue(i)));
            }

            return PDFObject::createStream(PDFStream(qMove(dictionary), QByteArray(*stream->getContent())));
        }

        case PDFObject::Type::Dictionary:
        {
            PDFDictionaryBuilder dictionary(*object.getDictionary());

            for (size_t i = 0, count = dictionary.getCount(); i < count; ++i)
            {
                dictionary.setEntry(dictionary.getKey(i), removeDuplicitReferencesInArrays(dictionary.getValue(i)));
            }

            return PDFObject::createDictionary(qMove(dictionary));
        }

        case PDFObject::Type::Array:
        {
            PDFArrayBuilder array;
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
    PDFInplaceOrMemoryString(string, string ? std::strlen(string) : 0)
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

    std::unique_ptr<PDFString> memoryString(new PDFString(std::move(string)));
    memoryString->optimize();
    memoryString->addReference();
    return memoryString.release();
}

void PDFInplaceOrMemoryString::destroyMemoryString(PDFString* string) noexcept
{
    delete string;
}

}   // namespace pdf
