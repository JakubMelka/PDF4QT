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

#ifndef PDFOBJECT_H
#define PDFOBJECT_H

#include "pdfglobal.h"

#include <QByteArray>
#include <QByteArrayView>

#include <atomic>
#include <memory>
#include <vector>
#include <variant>
#include <array>
#include <limits>
#include <initializer_list>
#include <cstring>
#include <cstdint>
#include <stdexcept>

namespace pdf
{
class PDFArray;
class PDFString;
class PDFStream;
class PDFDictionary;
class PDFAbstractVisitor;

/// This class represents a content of the PDF object, which is stored in the heap
/// and shared by the objects - array of objects, dictionary, content stream data,
/// or string data. The content is shared by an intrusive, thread safe reference
/// count. Content referenced by an object is never modified, so it can be read
/// from multiple threads simultaneously.
///
/// Content has no virtual table, it is always deleted through the type of the
/// object, which references it. For this reason, the destructor is protected,
/// so the content can't be deleted through the pointer to this class.
class PDFObjectContent
{
public:
    constexpr PDFObjectContent() noexcept = default;

    // Copy of the content is a new content, which is not referenced by any object,
    // so the reference count is never copied.
    inline PDFObjectContent(const PDFObjectContent&) noexcept : m_referenceCount(0) { }
    inline PDFObjectContent& operator=(const PDFObjectContent&) noexcept { return *this; }

protected:
    ~PDFObjectContent() = default;

    /// Keep owned arrays shared, but copy borrowed data before publishing content.
    static inline QByteArray takeOwnedByteArray(QByteArray value)
    {
        return !value.isEmpty() && value.capacity() == 0
            ? QByteArray(value.constData(), value.size()) : std::move(value);
    }

private:
    friend class PDFObject;
    friend class PDFInplaceOrMemoryString;

    /// Adds a reference to the content. Relaxed ordering is sufficient, a new
    /// reference can be created only from an existing one.
    inline void addReference() const noexcept { m_referenceCount.fetch_add(1, std::memory_order_relaxed); }

    /// Removes a reference to the content. Returns true, if it was the last
    /// reference, and the content must be deleted by the caller. Acquire-release
    /// ordering guarantees, that all accesses of other threads to the content
    /// happen before the deletion.
    inline bool removeReference() const noexcept { return m_referenceCount.fetch_sub(1, std::memory_order_acq_rel) == 1; }

    /// Returns current number of references (for diagnostic purposes only)
    inline uint64_t getReferenceCount() const noexcept { return m_referenceCount.load(std::memory_order_relaxed); }

    /// A 32-bit counter can overflow on large-memory systems. A 64-bit count
    /// cannot wrap for any number of 16-byte objects that fits in address space.
    mutable std::atomic<uint64_t> m_referenceCount = 0;
};

/// This class represents inplace string in the PDF object. To avoid too much
/// memory allocation, we store small strings inplace as small objects, so
/// we do not use memory allocator, so this doesn't cause performance downgrade.
/// Very often, PDF document consists of large number of names and strings
/// objects, which will fit into this category.
///
/// Constructors zero characters behind the end of the string. PDFObject and
/// PDFInplaceOrMemoryString preserve this invariant in their private storage.
struct PDFInplaceString
{
    static constexpr const int MAX_STRING_SIZE = 14;

    constexpr PDFInplaceString() = default;

    inline PDFInplaceString(const char* data, qsizetype size)
    {
        if (size < 0 || size > MAX_STRING_SIZE)
        {
            throw std::length_error("Invalid inplace string length");
        }
        if (size != 0)
        {
            if (!data)
            {
                throw std::invalid_argument("Null inplace string data");
            }
            std::memcpy(string.data(), data, static_cast<size_t>(size));
        }
        this->size = static_cast<uint8_t>(size);
    }

    inline PDFInplaceString(const QByteArray& data) : PDFInplaceString(data.constData(), data.size()) { }

    inline bool operator==(const PDFInplaceString& other) const
    {
        // Members are public: unused bytes need not be zero in standalone strings.
        return size == other.size && std::memcmp(string.data(), other.string.data(),
                                                qMin(static_cast<int>(size), MAX_STRING_SIZE)) == 0;
    }

    inline bool operator!=(const PDFInplaceString& other) const
    {
        return !(*this == other);
    }

    QByteArray getString() const
    {
        // Size is checked, because members are public
        return (size > 0) ? QByteArray(string.data(), qMin(static_cast<int>(size), MAX_STRING_SIZE)) : QByteArray();
    }

    uint8_t size = 0;
    std::array<char, MAX_STRING_SIZE> string = { };
};

static_assert(sizeof(PDFInplaceString) == 15, "Inplace string must fit into the object together with the type");

/// Reference to the string implementations
struct PDF4QTLIBCORESHARED_EXPORT PDFStringRef
{
    const PDFInplaceString* inplaceString = nullptr;
    const PDFString* memoryString = nullptr;

    QByteArray getString() const;
};

/// This class represents string, which can be inplace string (no memory allocation),
/// or string stored in the heap, if not enough space for embedded string. Strings
/// of at most PDFInplaceString::MAX_STRING_SIZE characters are always inplace,
/// longer strings are always stored in the heap and shared by the copies (the
/// same way as the content of the PDF object, so copies can be used from
/// multiple threads). Default constructed string is an empty inplace string.
/// The rule is guaranteed by the constructors, so an inplace string is never equal
/// to a string in the heap.
class PDF4QTLIBCORESHARED_EXPORT PDFInplaceOrMemoryString
{
public:
    constexpr PDFInplaceOrMemoryString() noexcept = default;
    explicit PDFInplaceOrMemoryString(const char* string);
    explicit PDFInplaceOrMemoryString(QByteArray string);
    explicit inline PDFInplaceOrMemoryString(const char* string, size_t length);

    inline ~PDFInplaceOrMemoryString() { release(); }

    inline PDFInplaceOrMemoryString(const PDFInplaceOrMemoryString& other) noexcept : m_storage(other.m_storage) { addReference(); }
    inline PDFInplaceOrMemoryString(PDFInplaceOrMemoryString&& other) noexcept : m_storage(other.m_storage) { other.m_storage = Storage(); }

    inline PDFInplaceOrMemoryString& operator=(const PDFInplaceOrMemoryString& other) noexcept { PDFInplaceOrMemoryString(other).swap(*this); return *this; }
    inline PDFInplaceOrMemoryString& operator=(PDFInplaceOrMemoryString&& other) noexcept { PDFInplaceOrMemoryString(std::move(other)).swap(*this); return *this; }

    /// Swaps two strings
    inline void swap(PDFInplaceOrMemoryString& other) noexcept { std::swap(m_storage, other.m_storage); }

    /// Returns true, if string is equal to the given string
    inline bool equals(const char* value, size_t length) const;

    inline bool operator==(const PDFInplaceOrMemoryString& other) const;
    inline bool operator!=(const PDFInplaceOrMemoryString& other) const { return !(*this == other); }

    inline bool operator==(const QByteArray& value) const { return equals(value.constData(), static_cast<size_t>(value.size())); }
    inline bool operator==(const char* value) const { return equals(value, value ? std::strlen(value) : 0); }

    /// Returns true, if string is inplace (i.e. doesn't allocate memory)
    inline bool isInplace() const { return getTag() == INPLACE_TAG; }

    /// Returns string. If string is inplace, byte array is constructed.
    inline QByteArray getString() const;

    /// Returns view of the string. View is valid as long as this string exists.
    inline QByteArrayView getView() const;

    /// Returns number of strings sharing the string in the heap,
    /// or zero, if string is inplace (for diagnostic purposes only).
    inline uint64_t getContentReferenceCount() const;

private:
    friend class PDFDictionary;

    static constexpr uint8_t INPLACE_TAG = 0;
    static constexpr uint8_t MEMORY_TAG = 1;

    /// Size of the string in the heap, which doesn't fit into 32 bits
    static constexpr uint32_t UNKNOWN_SIZE = std::numeric_limits<uint32_t>::max();

    /// Raw content of the storage (16 bytes)
    using RawStorage = std::array<uint64_t, 2>;

    struct InplaceStorage
    {
        uint8_t tag = INPLACE_TAG;
        PDFInplaceString string;
    };

    /// String in the heap. Size of the string is stored too, so strings of
    /// different sizes are compared without access to the string in the heap.
    struct MemoryStorage
    {
        uint8_t tag = MEMORY_TAG;
        std::array<uint8_t, 3> reserved = { };
        uint32_t size = UNKNOWN_SIZE;
        PDFString* string = nullptr;
    };

    union Storage
    {
        constexpr Storage() noexcept : inplace() { }
        constexpr explicit Storage(const InplaceStorage& inplaceStorage) noexcept : inplace(inplaceStorage) { }
        constexpr explicit Storage(const MemoryStorage& memoryStorage) noexcept : memory(memoryStorage) { }

        InplaceStorage inplace;
        MemoryStorage memory;
    };

    static_assert(sizeof(Storage) == 16, "String must have 16 bytes");
    static_assert(sizeof(InplaceStorage) == sizeof(Storage), "All bytes of inplace string must be defined");

    /// Returns tag of the string. Tag is in the common initial
    /// sequence of both storages, so it can be always read.
    inline uint8_t getTag() const { return m_storage.inplace.tag; }

    /// Returns raw content of the storage
    inline RawStorage getRawStorage() const
    {
        RawStorage rawStorage;
        std::memcpy(rawStorage.data(), &m_storage, sizeof(Storage));
        return rawStorage;
    }

    /// Returns raw content of the storage of the inplace string
    /// \param string String
    /// \param length Length of the string (at most PDFInplaceString::MAX_STRING_SIZE)
    static inline RawStorage createInplaceRawStorage(const char* string, size_t length)
    {
        Q_ASSERT(length <= static_cast<size_t>(PDFInplaceString::MAX_STRING_SIZE));

        InplaceStorage storage;
        storage.string = PDFInplaceString(string, static_cast<int>(length));

        RawStorage rawStorage;
        std::memcpy(rawStorage.data(), &storage, sizeof(InplaceStorage));
        return rawStorage;
    }

    /// Returns true, if raw storages are equal. For inplace strings, it means,
    /// that strings are equal (all 16 bytes of the inplace storage are defined).
    static inline bool isRawEqual(const RawStorage& left, const RawStorage& right) { return left[0] == right[0] && left[1] == right[1]; }

    /// Returns true, if storage of both strings is the same
    inline bool isRawEqual(const PDFInplaceOrMemoryString& other) const { return isRawEqual(getRawStorage(), other.getRawStorage()); }

    /// Returns true, if string in the heap can have the given size
    inline bool canHaveSize(size_t size) const { return m_storage.memory.size == UNKNOWN_SIZE || m_storage.memory.size == size; }

    /// Returns size stored in the memory storage
    static inline uint32_t getStoredSize(size_t size) { return size < UNKNOWN_SIZE ? static_cast<uint32_t>(size) : UNKNOWN_SIZE; }

    inline void addReference() const noexcept;
    inline void release() noexcept;

    /// Creates string in the heap with reference count equal to one
    static PDFString* createMemoryString(QByteArray string);

    /// Deletes string in the heap
    static void destroyMemoryString(PDFString* string) noexcept;

    Storage m_storage;
};

/// Immutable content may be read and shared concurrently, including copying the
/// same const object. Assignment, moving from, swapping or destroying a particular
/// PDFObject requires synchronization with all accesses to that same instance.
class PDF4QTLIBCORESHARED_EXPORT PDFObject
{
public:
    enum class Type : uint8_t
    {
        // Simple PDF objects
        Null,
        Bool,
        Int,
        Real,
        String,
        Name,

        // Complex PDF objects
        Array,
        Dictionary,
        Stream,
        Reference,

        // Last type mark
        LastType
    };

    static constexpr auto getTypes() { return std::array{ Type::Null, Type::Bool, Type::Int, Type::Real, Type::String, Type::Name, Type::Array, Type::Dictionary, Type::Stream, Type::Reference }; }

    // Default constructor should be constexpr (null object)
    constexpr PDFObject() noexcept = default;

    inline ~PDFObject() { release(); }

    // Copy shares the content (if any) with the copied object
    inline PDFObject(const PDFObject& other) noexcept : m_storage(other.m_storage) { addReference(); }

    // Moved object becomes a null object
    inline PDFObject(PDFObject&& other) noexcept : m_storage(other.m_storage) { other.m_storage = Storage(); }

    inline PDFObject& operator=(const PDFObject& other) noexcept { PDFObject(other).swap(*this); return *this; }
    inline PDFObject& operator=(PDFObject&& other) noexcept { PDFObject(std::move(other)).swap(*this); return *this; }

    /// Swaps two objects
    inline void swap(PDFObject& other) noexcept { std::swap(m_storage, other.m_storage); }

    inline Type getType() const { return static_cast<Type>(getTag() & TYPE_MASK); }

    // Test operators
    inline bool isNull() const { return getType() == Type::Null; }
    inline bool isBool() const { return getType() == Type::Bool; }
    inline bool isInt() const { return getType() == Type::Int; }
    inline bool isReal() const { return getType() == Type::Real; }
    inline bool isString() const { return getType() == Type::String; }
    inline bool isName() const { return getType() == Type::Name; }
    inline bool isArray() const { return getType() == Type::Array; }
    inline bool isDictionary() const { return getType() == Type::Dictionary; }
    inline bool isStream() const { return getType() == Type::Stream; }
    inline bool isReference() const { return getType() == Type::Reference; }

    // Value accessors. If object is of different type, then
    // PDFException is thrown.
    inline bool getBool() const;
    inline PDFInteger getInteger() const;
    inline PDFReal getReal() const;
    inline QByteArray getString() const;
    inline const PDFDictionary* getDictionary() const;
    inline PDFObjectReference getReference() const;
    inline PDFStringRef getStringObject() const;
    inline const PDFStream* getStream() const;
    inline const PDFArray* getArray() const;

    /// Returns number of objects sharing the content of this object in the heap,
    /// or zero, if object has no content in the heap (for diagnostic purposes only,
    /// value can be outdated, when copies are created or destroyed in other threads).
    inline uint64_t getContentReferenceCount() const;

    bool operator==(const PDFObject& other) const;
    bool operator!=(const PDFObject& other) const { return !(*this == other); }

    /// Accepts the visitor
    void accept(PDFAbstractVisitor* visitor) const;

    /// Creates a null object
    static inline PDFObject createNull() { return PDFObject(); }

    /// Creates a boolean object
    static inline PDFObject createBool(bool value);

    /// Creates an integer object
    static inline PDFObject createInteger(PDFInteger value);

    /// Creates an object with real number
    static inline PDFObject createReal(PDFReal value);

    /// Creates a reference object
    static inline PDFObject createReference(const PDFObjectReference& reference);

    /// Creates an array object
    static PDFObject createArray(PDFArray array);

    /// Creates a dictionary object
    static PDFObject createDictionary(PDFDictionary dictionary);

    /// Creates a stream object
    static PDFObject createStream(PDFStream stream);

    /// Creates a name object
    static PDFObject createName(QByteArray name);

    /// Creates a string object
    static PDFObject createString(QByteArray name);

    /// Creates a name object
    static PDFObject createName(PDFStringRef name);

    /// Creates a string object
    static PDFObject createString(PDFStringRef name);

private:
    /// Type is stored in the lower bits of the tag
    static constexpr uint8_t TYPE_MASK = 0x0F;

    /// Flag of the tag - object references content in the heap
    static constexpr uint8_t CONTENT_FLAG = 0x80;

    static constexpr uint8_t makeTag(Type type, bool hasContent = false) { return static_cast<uint8_t>(static_cast<uint8_t>(type) | (hasContent ? CONTENT_FLAG : 0)); }

    /// Storage of the objects, which are not inplace strings: type tag,
    /// generation of the reference and 8 bytes of the value (integer,
    /// real number, boolean, object number of the reference, or pointer
    /// to the content in the heap).
    struct ValueStorage
    {
        uint8_t tag = 0;
        uint8_t reserved8 = 0;
        uint16_t reserved16 = 0;
        int32_t generation = 0;

        union
        {
            PDFInteger integer = 0;
            PDFReal real;
            bool boolean;
            PDFObjectContent* content;
        };
    };

    /// Storage of inplace strings and names: type tag and the string
    struct InplaceStringStorage
    {
        uint8_t tag = 0;
        PDFInplaceString string;
    };

    /// Storage of the object. The tag is in the common initial
    /// sequence of both storages, so it can be always read.
    union Storage
    {
        constexpr Storage() noexcept : value() { }
        constexpr explicit Storage(const ValueStorage& valueStorage) noexcept : value(valueStorage) { }
        constexpr explicit Storage(const InplaceStringStorage& stringStorage) noexcept : string(stringStorage) { }

        ValueStorage value;
        InplaceStringStorage string;
    };

    static_assert(sizeof(Storage) == 16, "Object must have 16 bytes");

    /// Creates object from the storage. If storage references the
    /// content, the object takes ownership of the reference.
    explicit constexpr PDFObject(const Storage& storage) noexcept : m_storage(storage) { }

    inline uint8_t getTag() const { return m_storage.value.tag; }
    inline bool hasContent() const { return (getTag() & CONTENT_FLAG) != 0; }

    inline void addReference() const noexcept;
    inline void release() noexcept;

    /// Returns true, if object is inplace string or inplace name
    inline bool isInplaceString() const { return !hasContent() && (getType() == Type::String || getType() == Type::Name); }

    /// Returns view of the string or name
    inline QByteArrayView getStringView() const;

    /// Creates object referencing the content, which has reference count
    /// equal to zero. Object takes the ownership of the content.
    static PDFObject createWithContent(Type type, PDFObjectContent* content) noexcept;

    /// Creates string or name object
    static PDFObject createStringObject(Type type, QByteArray string);

    /// Creates string or name object
    static PDFObject createStringObject(Type type, PDFStringRef string);

    /// Creates reference, which can't be stored inplace
    static PDFObject createReferenceWithContent(const PDFObjectReference& reference);

    /// Deletes the content, which is not referenced anymore
    static void destroyContent(uint8_t tag, PDFObjectContent* content) noexcept;

    /// Throws exception about invalid type of the object
    [[noreturn]] static void throwInvalidType(Type expectedType);

    Storage m_storage;
};

/// Content of the reference object, whose generation number can't be stored inplace
/// in the object, because it is out of the range of 32-bit integer (it can happen
/// only in invalid documents).
class PDFReferenceContent : public PDFObjectContent
{
public:
    inline explicit PDFReferenceContent(PDFObjectReference reference) : m_reference(reference) { }

    PDFObjectReference getReference() const { return m_reference; }

private:
    PDFObjectReference m_reference;
};

/// Represents raw string in the PDF file. No conversions are performed, this is
/// reason, that we do not use QString, but QByteArray instead.
class PDF4QTLIBCORESHARED_EXPORT PDFString : public PDFObjectContent
{
public:
    inline explicit PDFString() = default;
    inline explicit PDFString(QByteArray&& value) :
        m_string(takeOwnedByteArray(std::move(value)))
    {

    }

    bool operator==(const PDFString& other) const { return m_string == other.m_string; }

    const QByteArray& getString() const { return m_string; }
    void setString(const QByteArray& string);

    /// Optimizes the string for memory consumption
    void optimize();

private:
    QByteArray m_string;
};

/// Represents an array of objects in the PDF file.
class PDF4QTLIBCORESHARED_EXPORT PDFArray : public PDFObjectContent
{
public:
    inline PDFArray() = default;
    inline PDFArray(std::vector<PDFObject>&& objects) : m_objects(qMove(objects)) { }

    PDFArray(const PDFArray&) = default;
    PDFArray(PDFArray&&) noexcept = default;

    // Copy before replacing content: the source may be owned by a nested object.
    PDFArray& operator=(const PDFArray& other) { PDFArray(other).swap(*this); return *this; }
    PDFArray& operator=(PDFArray&& other) noexcept { PDFArray(std::move(other)).swap(*this); return *this; }
    void swap(PDFArray& other) noexcept { m_objects.swap(other.m_objects); }

    bool operator==(const PDFArray& other) const { return m_objects == other.m_objects; }

    /// Returns item at the specified index. If index is invalid,
    /// then it throws an exception.
    const PDFObject& getItem(size_t index) const { return m_objects.at(index); }

    /// Sets item at the specified index. Index must be valid.
    void setItem(PDFObject value, size_t index) { m_objects[index] = qMove(value); }

    /// Returns size of the array (number of elements)
    size_t getCount() const { return m_objects.size(); }

    /// Returns capacity of the array (theoretical number of elements before reallocation)
    size_t getCapacity() const { return m_objects.capacity(); }

    /// Appends object to the end of object list
    void appendItem(PDFObject object);

    /// Optimizes the array for memory consumption
    void optimize();

    auto begin() { return m_objects.begin(); }
    auto end() { return m_objects.end(); }

    auto begin() const { return m_objects.begin(); }
    auto end() const { return m_objects.end(); }

private:
    std::vector<PDFObject> m_objects;
};

/// Represents a dictionary of objects in the PDF file. Dictionary is
/// an array of pairs key-value, where key is name object and value is any
/// PDF object. We do not use map, because dictionaries are usually small.
class PDF4QTLIBCORESHARED_EXPORT PDFDictionary : public PDFObjectContent
{
public:
    using DictionaryEntry = std::pair<PDFInplaceOrMemoryString, PDFObject>;

    inline PDFDictionary() = default;
    inline PDFDictionary(std::vector<DictionaryEntry>&& dictionary) : m_dictionary(qMove(dictionary)) { }

    PDFDictionary(const PDFDictionary&) = default;
    PDFDictionary(PDFDictionary&&) noexcept = default;

    // Copy before replacing content: the source may be owned by a nested object.
    PDFDictionary& operator=(const PDFDictionary& other) { PDFDictionary(other).swap(*this); return *this; }
    PDFDictionary& operator=(PDFDictionary&& other) noexcept { PDFDictionary(std::move(other)).swap(*this); return *this; }
    void swap(PDFDictionary& other) noexcept { m_dictionary.swap(other.m_dictionary); }

    bool operator==(const PDFDictionary& other) const;

    /// Returns object for the key. If key is not found in the dictionary,
    /// then valid reference to the null object is returned.
    /// \param key Key
    const PDFObject& get(const QByteArray& key) const;

    /// Returns object for the key. If key is not found in the dictionary,
    /// then valid reference to the null object is returned.
    /// \param key Key
    const PDFObject& get(const char* key) const;

    /// Returns object for the key. If key is not found in the dictionary,
    /// then valid reference to the null object is returned.
    /// \param key Key
    const PDFObject& get(const PDFInplaceOrMemoryString& key) const;

    /// Returns true, if dictionary contains a particular key
    /// \param key Key to be found in the dictionary
    bool hasKey(const QByteArray& key) const { return find(key) != m_dictionary.cend(); }

    /// Returns true, if dictionary contains a particular key
    /// \param key Key to be found in the dictionary
    bool hasKey(const char* key) const { return find(key) != m_dictionary.cend(); }

    /// Removes entry with given key. If entry with this key is not found,
    /// nothing happens.
    /// \param key Key to be removed
    void removeEntry(const char* key);

    /// Adds a new entry to the dictionary.
    /// \param key Key
    /// \param value Value
    void addEntry(PDFInplaceOrMemoryString&& key, PDFObject&& value) { m_dictionary.emplace_back(std::move(key), std::move(value)); }

    /// Adds a new entry to the dictionary.
    /// \param key Key
    /// \param value Value
    void addEntry(const PDFInplaceOrMemoryString& key, PDFObject&& value) { m_dictionary.emplace_back(key, std::move(value)); }

    /// Sets entry value. If entry with given key doesn't exist,
    /// then it is created.
    /// \param key Key
    /// \param value Value
    void setEntry(const PDFInplaceOrMemoryString& key, PDFObject&& value);

    /// Returns count of items in the dictionary
    size_t getCount() const { return m_dictionary.size(); }

    /// Returns capacity of items in the dictionary
    size_t getCapacity() const { return m_dictionary.capacity(); }

    /// Returns n-th key of the dictionary
    /// \param index Zero-based index of key in the dictionary
    const PDFInplaceOrMemoryString& getKey(size_t index) const { return m_dictionary[index].first; }

    /// Returns n-th value of the dictionary
    /// \param index Zero-based index of value in the dictionary
    const PDFObject& getValue(size_t index) const { return m_dictionary[index].second; }

    /// Removes null objects from dictionary
    void removeNullObjects();

    bool isEmpty() const { return getCount() == 0; }

    /// Optimizes the dictionary for memory consumption
    void optimize();

private:
    /// Finds an item in the dictionary array, if the item is not in the dictionary,
    /// then end iterator is returned.
    /// \param key Key to be found
    std::vector<DictionaryEntry>::const_iterator find(const QByteArray& key) const;

    /// Finds an item in the dictionary array, if the item is not in the dictionary,
    /// then end iterator is returned.
    /// \param key Key to be found
    std::vector<DictionaryEntry>::iterator find(const QByteArray& key);

    /// Finds an item in the dictionary array, if the item is not in the dictionary,
    /// then end iterator is returned.
    /// \param key Key to be found
    std::vector<DictionaryEntry>::const_iterator find(const char* key) const;

    /// Finds an item in the dictionary array, if the item is not in the dictionary,
    /// then end iterator is returned.
    /// \param key Key to be found
    std::vector<DictionaryEntry>::iterator find(const char* key);

    /// Finds an item in the dictionary array, if the item is not in the dictionary,
    /// then end iterator is returned.
    /// \param key Key to be found
    std::vector<DictionaryEntry>::const_iterator find(const PDFInplaceOrMemoryString& key) const;

    /// Finds an item in the dictionary array, if the item is not in the dictionary,
    /// then end iterator is returned.
    /// \param key Key to be found
    std::vector<DictionaryEntry>::iterator find(const PDFInplaceOrMemoryString& key);

    /// Finds an item in the dictionary array, if the item is not in the dictionary,
    /// then end iterator is returned.
    /// \param key Key to be found
    /// \param length Length of the key
    std::vector<DictionaryEntry>::const_iterator find(const char* key, size_t length) const;

    /// Finds an item in the dictionary array, if the item is not in the dictionary,
    /// then end iterator is returned.
    /// \param key Key to be found
    /// \param length Length of the key
    std::vector<DictionaryEntry>::iterator find(const char* key, size_t length);

    /// Null object returned for keys, which are not in the dictionary
    static const PDFObject s_nullObject;

    std::vector<DictionaryEntry> m_dictionary;
};

/// Represents a stream object in the PDF file. Stream consists of dictionary
/// and stream content - byte array.
class PDF4QTLIBCORESHARED_EXPORT PDFStream : public PDFObjectContent
{
public:
    inline explicit PDFStream() = default;
    inline explicit PDFStream(PDFDictionary&& dictionary, QByteArray&& content) :
        m_dictionary(std::move(dictionary)),
        m_content(takeOwnedByteArray(std::move(content)))
    {

    }

    PDFStream(const PDFStream&) = default;
    PDFStream(PDFStream&&) noexcept = default;

    PDFStream& operator=(const PDFStream& other) { PDFStream(other).swap(*this); return *this; }
    PDFStream& operator=(PDFStream&& other) noexcept { PDFStream(std::move(other)).swap(*this); return *this; }
    void swap(PDFStream& other) noexcept { m_dictionary.swap(other.m_dictionary); m_content.swap(other.m_content); }

    bool operator==(const PDFStream& other) const { return m_dictionary == other.m_dictionary && m_content == other.m_content; }

    /// Returns dictionary for this content stream
    const PDFDictionary* getDictionary() const { return &m_dictionary; }

    /// Optimizes the stream for memory consumption. Shared content
    /// would be copied, so it is not shrinked.
    void optimize() { m_dictionary.optimize(); if (m_content.isDetached()) { m_content.shrink_to_fit(); } }

    /// Returns content of the stream
    const QByteArray* getContent() const { return &m_content; }

private:
    PDFDictionary m_dictionary;
    QByteArray m_content;
};

class PDF4QTLIBCORESHARED_EXPORT PDFObjectManipulator
{
public:
    explicit PDFObjectManipulator() = delete;

    enum MergeFlag
    {
        NoFlag            = 0x0000,
        RemoveNullObjects = 0x0001, ///< Remove null object from dictionaries
        ConcatenateArrays = 0x0002, ///< Concatenate arrays instead of replace
    };
    Q_DECLARE_FLAGS(MergeFlags, MergeFlag)

    /// Merges two objects. If object type is different, then object from right is used.
    /// If both objects are dictionaries, then their content is merged, object \p right
    /// has precedence over object \p left. If both objects are arrays, and concatenating
    /// flag is turned on, then they are concatenated instead of replacing left array
    /// by right array. If remove null objects flag is turend on, then null objects
    /// are removed from dictionaries.
    /// \param left Left, 'slave' object
    /// \param right Right 'master' object, has priority over left
    /// \param flags Merge flags
    static PDFObject merge(PDFObject left, PDFObject right, MergeFlags flags);

    /// Remove null objects from all dictionaries
    /// \param object Object
    static PDFObject removeNullObjects(PDFObject object);

    /// Remove duplicit references from arrays
    /// \param object Object
    static PDFObject removeDuplicitReferencesInArrays(PDFObject object);
};

static_assert(sizeof(PDFObject) == 16, "Object must have 16 bytes");
static_assert(sizeof(PDFInplaceOrMemoryString) == 16, "Key of the dictionary must have 16 bytes");
static_assert(sizeof(PDFDictionary::DictionaryEntry) == 32, "Entry of the dictionary must have 32 bytes");

// Implementation

inline
PDFInplaceOrMemoryString::PDFInplaceOrMemoryString(const char* string, size_t length)
{
    if (length > static_cast<size_t>(std::numeric_limits<qsizetype>::max()))
    {
        throw std::length_error("Invalid string length");
    }
    if (!string && length != 0)
    {
        throw std::invalid_argument("Null string data");
    }

    if (length <= static_cast<size_t>(PDFInplaceString::MAX_STRING_SIZE))
    {
        InplaceStorage storage;
        storage.string = PDFInplaceString(string, static_cast<int>(length));
        m_storage = Storage(storage);
    }
    else
    {
        MemoryStorage storage;
        storage.size = getStoredSize(length);
        storage.string = createMemoryString(QByteArray(string, static_cast<qsizetype>(length)));
        m_storage = Storage(storage);
    }
}

inline
bool PDFInplaceOrMemoryString::equals(const char* value, size_t length) const
{
    if ((!value && length != 0) || length > static_cast<size_t>(std::numeric_limits<qsizetype>::max()))
    {
        return false;
    }

    if (isInplace())
    {
        const PDFInplaceString& string = m_storage.inplace.string;
        return string.size == length && (length == 0 || std::memcmp(string.string.data(), value, length) == 0);
    }

    // Sizes are compared first, without access to the string in the heap
    return canHaveSize(length) && getView() == QByteArrayView(value, static_cast<qsizetype>(length));
}

inline
bool PDFInplaceOrMemoryString::operator==(const PDFInplaceOrMemoryString& other) const
{
    if (isInplace())
    {
        return other.isInplace() && isRawEqual(other);
    }
    if (other.isInplace())
    {
        return false;
    }
    // Only inplace storage has a fully defined byte representation on every
    // architecture. Heap storage may contain padding after a 32-bit pointer.
    if (m_storage.memory.string == other.m_storage.memory.string)
    {
        return true;
    }

    if (m_storage.memory.size != UNKNOWN_SIZE && !other.canHaveSize(m_storage.memory.size))
    {
        return false;
    }

    return getView() == other.getView();
}

inline
QByteArray PDFInplaceOrMemoryString::getString() const
{
    return isInplace() ? m_storage.inplace.string.getString() : m_storage.memory.string->getString();
}

inline
QByteArrayView PDFInplaceOrMemoryString::getView() const
{
    if (isInplace())
    {
        return QByteArrayView(m_storage.inplace.string.string.data(), m_storage.inplace.string.size);
    }

    return QByteArrayView(m_storage.memory.string->getString());
}

inline
uint64_t PDFInplaceOrMemoryString::getContentReferenceCount() const
{
    return isInplace() ? 0 : m_storage.memory.string->getReferenceCount();
}

inline
void PDFInplaceOrMemoryString::addReference() const noexcept
{
    if (!isInplace())
    {
        m_storage.memory.string->addReference();
    }
}

inline
void PDFInplaceOrMemoryString::release() noexcept
{
    if (!isInplace() && m_storage.memory.string->removeReference())
    {
        destroyMemoryString(m_storage.memory.string);
    }
}

inline
bool PDFObject::getBool() const
{
    if (getTag() != makeTag(Type::Bool))
    {
        throwInvalidType(Type::Bool);
    }

    return m_storage.value.boolean;
}

inline
PDFInteger PDFObject::getInteger() const
{
    if (getTag() != makeTag(Type::Int))
    {
        throwInvalidType(Type::Int);
    }

    return m_storage.value.integer;
}

inline
PDFReal PDFObject::getReal() const
{
    if (getTag() != makeTag(Type::Real))
    {
        throwInvalidType(Type::Real);
    }

    return m_storage.value.real;
}

inline
QByteArray PDFObject::getString() const
{
    return getStringObject().getString();
}

inline
const PDFDictionary* PDFObject::getDictionary() const
{
    if (getTag() != makeTag(Type::Dictionary, true))
    {
        throwInvalidType(Type::Dictionary);
    }

    return static_cast<const PDFDictionary*>(m_storage.value.content);
}

inline
PDFObjectReference PDFObject::getReference() const
{
    const uint8_t tag = getTag();

    if (tag == makeTag(Type::Reference))
    {
        return PDFObjectReference(m_storage.value.integer, m_storage.value.generation);
    }

    if (tag != makeTag(Type::Reference, true))
    {
        throwInvalidType(Type::Reference);
    }

    return static_cast<const PDFReferenceContent*>(m_storage.value.content)->getReference();
}

inline
PDFStringRef PDFObject::getStringObject() const
{
    const uint8_t tag = getTag();

    if (tag == makeTag(Type::String) || tag == makeTag(Type::Name))
    {
        return { &m_storage.string.string, nullptr };
    }

    if (tag != makeTag(Type::String, true) && tag != makeTag(Type::Name, true))
    {
        throwInvalidType(Type::String);
    }

    return { nullptr, static_cast<const PDFString*>(m_storage.value.content) };
}

inline
const PDFStream* PDFObject::getStream() const
{
    if (getTag() != makeTag(Type::Stream, true))
    {
        throwInvalidType(Type::Stream);
    }

    return static_cast<const PDFStream*>(m_storage.value.content);
}

inline
const PDFArray* PDFObject::getArray() const
{
    if (getTag() != makeTag(Type::Array, true))
    {
        throwInvalidType(Type::Array);
    }

    return static_cast<const PDFArray*>(m_storage.value.content);
}

inline
uint64_t PDFObject::getContentReferenceCount() const
{
    return hasContent() ? m_storage.value.content->getReferenceCount() : 0;
}

inline
PDFObject PDFObject::createBool(bool value)
{
    ValueStorage storage;
    storage.tag = makeTag(Type::Bool);
    storage.boolean = value;
    return PDFObject(Storage(storage));
}

inline
PDFObject PDFObject::createInteger(PDFInteger value)
{
    ValueStorage storage;
    storage.tag = makeTag(Type::Int);
    storage.integer = value;
    return PDFObject(Storage(storage));
}

inline
PDFObject PDFObject::createReal(PDFReal value)
{
    ValueStorage storage;
    storage.tag = makeTag(Type::Real);
    storage.real = value;
    return PDFObject(Storage(storage));
}

inline
PDFObject PDFObject::createReference(const PDFObjectReference& reference)
{
    if (reference.generation < std::numeric_limits<int32_t>::min() || reference.generation > std::numeric_limits<int32_t>::max())
    {
        return createReferenceWithContent(reference);
    }

    ValueStorage storage;
    storage.tag = makeTag(Type::Reference);
    storage.generation = static_cast<int32_t>(reference.generation);
    storage.integer = reference.objectNumber;
    return PDFObject(Storage(storage));
}

inline
void PDFObject::addReference() const noexcept
{
    if (hasContent())
    {
        m_storage.value.content->addReference();
    }
}

inline
void PDFObject::release() noexcept
{
    if (hasContent() && m_storage.value.content->removeReference())
    {
        destroyContent(getTag(), m_storage.value.content);
    }
}

inline
QByteArrayView PDFObject::getStringView() const
{
    PDFStringRef stringRef = getStringObject();

    if (stringRef.inplaceString)
    {
        return QByteArrayView(stringRef.inplaceString->string.data(), stringRef.inplaceString->size);
    }

    return QByteArrayView(stringRef.memoryString->getString());
}

}   // namespace pdf

#endif // PDFOBJECT_H
