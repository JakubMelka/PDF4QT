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

#include <QtTest>

#include "pdfobject.h"
#include "pdfparser.h"
#include "pdfvisitor.h"
#include "pdfexception.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <latch>
#include <limits>
#include <mutex>
#include <random>
#include <thread>
#include <type_traits>
#include <vector>

using namespace pdf;

/// Visitor, which records visited values as text
class PDFObjectTestVisitor : public PDFAbstractVisitor
{
public:
    virtual void visitNull() override { m_log << "null"; }
    virtual void visitBool(bool value) override { m_log << QString("bool %1").arg(value ? 1 : 0); }
    virtual void visitInt(PDFInteger value) override { m_log << QString("int %1").arg(value); }
    virtual void visitReal(PDFReal value) override { m_log << QString("real %1").arg(value); }
    virtual void visitString(PDFStringRef string) override { m_log << QString("string %1").arg(QString::fromLatin1(string.getString())); }
    virtual void visitName(PDFStringRef name) override { m_log << QString("name %1").arg(QString::fromLatin1(name.getString())); }
    virtual void visitArray(const PDFArray* array) override { m_log << QString("array %1").arg(array->getCount()); }
    virtual void visitDictionary(const PDFDictionary* dictionary) override { m_log << QString("dictionary %1").arg(dictionary->getCount()); }
    virtual void visitStream(const PDFStream* stream) override { m_log << QString("stream %1").arg(stream->getContent()->size()); }
    virtual void visitReference(const PDFObjectReference reference) override { m_log << QString("reference %1 %2").arg(reference.objectNumber).arg(reference.generation); }

    const QStringList& getLog() const { return m_log; }

private:
    QStringList m_log;
};

class PDFObjectTest : public QObject
{
    Q_OBJECT

public:
    explicit PDFObjectTest() = default;

private slots:
    void test_sizes();
    void test_null();
    void test_bool();
    void test_integer();
    void test_real();
    void test_reference();
    void test_string();
    void test_string_ref();
    void test_type_mismatch();
    void test_copy_and_move();
    void test_self_assignment_and_aliasing();
    void test_content_lifetime();
    void test_vector_of_objects();
    void test_array();
    void test_dictionary();
    void test_dictionary_keys();
    void test_stream();
    void test_equality();
    void test_visitor();
    void test_manipulator();
    void test_concurrent_copies();
    void test_concurrent_last_reference();
    void test_concurrent_subobjects();
    void test_concurrent_dictionary_lookup();
    void test_concurrent_keys();
    void test_concurrent_parsing();
    void test_concurrent_random_objects();

private:
    /// Returns number of threads for concurrent tests (more threads,
    /// than cores, to have also preempted threads)
    static int getThreadCount();

    /// Runs the function in the given number of threads. All threads
    /// are started at the same moment to maximize the contention.
    /// \param threadCount Number of threads
    /// \param function Function, which receives index of the thread
    static void runConcurrently(int threadCount, const std::function<void(int)>& function);

    /// Creates data of the given size, containing all byte values (including zero)
    static QByteArray createData(qsizetype size, int seed);

    /// Creates the probe - detached byte array, whose sharing is used to detect,
    /// if the content of the object, which uses the probe, was destroyed.
    static QByteArray createProbe(int seed);

    static PDFObject createDictionary(std::initializer_list<std::pair<QByteArray, PDFObject>> entries);
    static PDFObject createArray(std::initializer_list<PDFObject> items);
    static PDFObject createStream(std::initializer_list<std::pair<QByteArray, PDFObject>> entries, QByteArray content);

    /// Creates distinct objects of all types and all storage kinds
    static std::vector<PDFObject> createSampleObjects();

    /// Returns true, if function throws PDFException with non-empty message
    static bool throwsPDFException(const std::function<void()>& function);
};

int PDFObjectTest::getThreadCount()
{
    const int hardwareThreads = static_cast<int>(std::thread::hardware_concurrency());
    return qBound(4, 2 * hardwareThreads, 32);
}

void PDFObjectTest::runConcurrently(int threadCount, const std::function<void(int)>& function)
{
    std::latch start(threadCount);
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        threads.emplace_back([&start, &function, i]()
        {
            start.arrive_and_wait();
            function(i);
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }
}

QByteArray PDFObjectTest::createData(qsizetype size, int seed)
{
    QByteArray data(size, Qt::Uninitialized);
    for (qsizetype i = 0; i < size; ++i)
    {
        data[i] = static_cast<char>((i * 37 + seed * 11) & 0xFF);
    }
    return data;
}

QByteArray PDFObjectTest::createProbe(int seed)
{
    QByteArray probe = createData(4096, seed);
    Q_ASSERT(probe.isDetached());
    return probe;
}

PDFObject PDFObjectTest::createDictionary(std::initializer_list<std::pair<QByteArray, PDFObject>> entries)
{
    PDFDictionary dictionary;
    for (const auto& entry : entries)
    {
        dictionary.addEntry(PDFInplaceOrMemoryString(entry.first), PDFObject(entry.second));
    }
    return PDFObject::createDictionary(std::move(dictionary));
}

PDFObject PDFObjectTest::createArray(std::initializer_list<PDFObject> items)
{
    return PDFObject::createArray(std::vector<PDFObject>(items));
}

PDFObject PDFObjectTest::createStream(std::initializer_list<std::pair<QByteArray, PDFObject>> entries, QByteArray content)
{
    PDFDictionary dictionary;
    for (const auto& entry : entries)
    {
        dictionary.addEntry(PDFInplaceOrMemoryString(entry.first), PDFObject(entry.second));
    }
    dictionary.addEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
    return PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content)));
}

std::vector<PDFObject> PDFObjectTest::createSampleObjects()
{
    std::vector<PDFObject> objects;
    objects.push_back(PDFObject::createNull());
    objects.push_back(PDFObject::createBool(false));
    objects.push_back(PDFObject::createBool(true));
    objects.push_back(PDFObject::createInteger(0));
    objects.push_back(PDFObject::createInteger(-1));
    objects.push_back(PDFObject::createInteger(std::numeric_limits<PDFInteger>::max()));
    objects.push_back(PDFObject::createReal(0.5));
    objects.push_back(PDFObject::createReal(-std::numeric_limits<PDFReal>::infinity()));
    objects.push_back(PDFObject::createString(""));
    objects.push_back(PDFObject::createString("Inplace"));
    objects.push_back(PDFObject::createString("A string, which is stored in the heap"));
    objects.push_back(PDFObject::createName(""));
    objects.push_back(PDFObject::createName("Inplace"));
    objects.push_back(PDFObject::createName("NameStoredInTheHeap"));
    objects.push_back(createArray({ }));
    objects.push_back(createArray({ PDFObject::createInteger(1), PDFObject::createName("Two") }));
    objects.push_back(createDictionary({ }));
    objects.push_back(createDictionary({ { "Type", PDFObject::createName("Page") }, { "VeryLongKeyOfTheDictionary", PDFObject::createInteger(5) } }));
    objects.push_back(createStream({ { "Filter", PDFObject::createName("FlateDecode") } }, createData(100, 1)));
    objects.push_back(PDFObject::createReference(PDFObjectReference(1, 0)));
    objects.push_back(PDFObject::createReference(PDFObjectReference(1, 1)));
    objects.push_back(PDFObject::createReference(PDFObjectReference(1, PDFInteger(std::numeric_limits<int32_t>::max()) + 1)));
    return objects;
}

bool PDFObjectTest::throwsPDFException(const std::function<void()>& function)
{
    try
    {
        function();
    }
    catch (const PDFException& exception)
    {
        return !exception.getMessage().isEmpty();
    }
    catch (...)
    {
        return false;
    }

    return false;
}

void PDFObjectTest::test_sizes()
{
    QCOMPARE(sizeof(PDFObject), size_t(16));
    QCOMPARE(sizeof(PDFInplaceOrMemoryString), size_t(16));
    QCOMPARE(sizeof(PDFDictionary::DictionaryEntry), size_t(32));
    QCOMPARE(sizeof(PDFInplaceString), size_t(15));
    QCOMPARE(PDFInplaceString::MAX_STRING_SIZE, 14);

    QVERIFY(std::is_nothrow_move_constructible_v<PDFObject>);
    QVERIFY(std::is_nothrow_move_assignable_v<PDFObject>);
    QVERIFY(std::is_nothrow_copy_constructible_v<PDFObject>);
    QVERIFY(std::is_nothrow_swappable_v<PDFObject>);
    QVERIFY(std::is_nothrow_move_constructible_v<PDFInplaceOrMemoryString>);
    QVERIFY(std::is_nothrow_move_assignable_v<PDFInplaceOrMemoryString>);
    QVERIFY(std::is_nothrow_copy_constructible_v<PDFInplaceOrMemoryString>);

    // Content can't be deleted through the pointer to the base class
    QVERIFY(!std::is_destructible_v<PDFObjectContent>);
    QVERIFY(!std::has_virtual_destructor_v<PDFObjectContent>);
}

void PDFObjectTest::test_null()
{
    PDFObject object;
    QVERIFY(object.isNull());
    QCOMPARE(object.getType(), PDFObject::Type::Null);
    QVERIFY(!object.isBool() && !object.isInt() && !object.isReal() && !object.isString() && !object.isName());
    QVERIFY(!object.isArray() && !object.isDictionary() && !object.isStream() && !object.isReference());
    QCOMPARE(object.getContentReferenceCount(), uint32_t(0));

    QVERIFY(object == PDFObject::createNull());
    QVERIFY(!(object != PDFObject::createNull()));
    QVERIFY(object != PDFObject::createInteger(0));
    QVERIFY(object != PDFObject::createBool(false));
    QVERIFY(object != PDFObject::createReal(0.0));
    QVERIFY(object != PDFObject::createName(""));
    QVERIFY(object != PDFObject::createString(""));
    QVERIFY(object != createArray({ }));
    QVERIFY(object != createDictionary({ }));
    QVERIFY(object != PDFObject::createReference(PDFObjectReference()));

    // Null object returned by the dictionary for missing keys
    PDFDictionary dictionary;
    QVERIFY(dictionary.get("Missing").isNull());
    QVERIFY(&dictionary.get("Missing") == &dictionary.get(QByteArray("Other")));
}

void PDFObjectTest::test_bool()
{
    for (bool value : { false, true })
    {
        PDFObject object = PDFObject::createBool(value);
        QVERIFY(object.isBool());
        QCOMPARE(object.getType(), PDFObject::Type::Bool);
        QCOMPARE(object.getBool(), value);
        QCOMPARE(object.getContentReferenceCount(), uint32_t(0));
        QVERIFY(object == PDFObject::createBool(value));
        QVERIFY(object != PDFObject::createBool(!value));
        QVERIFY(object != PDFObject::createInteger(value ? 1 : 0));

        PDFObject copy = object;
        QCOMPARE(copy.getBool(), value);
    }
}

void PDFObjectTest::test_integer()
{
    const std::vector<PDFInteger> values = { 0, 1, -1, 42, -42, PDFInteger(1) << 40, -(PDFInteger(1) << 40),
                                             std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min(),
                                             std::numeric_limits<PDFInteger>::max(), std::numeric_limits<PDFInteger>::min() };

    for (PDFInteger value : values)
    {
        PDFObject object = PDFObject::createInteger(value);
        QVERIFY(object.isInt());
        QCOMPARE(object.getType(), PDFObject::Type::Int);
        QCOMPARE(object.getInteger(), value);
        QCOMPARE(object.getContentReferenceCount(), uint32_t(0));
        QVERIFY(object == PDFObject::createInteger(value));
        QVERIFY(object != PDFObject::createInteger(value ^ 1));
        QVERIFY(object != PDFObject::createReal(PDFReal(value)));

        PDFObject copy = object;
        QCOMPARE(copy.getInteger(), value);
    }
}

void PDFObjectTest::test_real()
{
    const std::vector<PDFReal> values = { 0.0, -0.0, 1.0, -1.5, 3.141592653589793, 1e300, -1e-300,
                                          std::numeric_limits<PDFReal>::denorm_min(),
                                          std::numeric_limits<PDFReal>::max(),
                                          std::numeric_limits<PDFReal>::lowest(),
                                          std::numeric_limits<PDFReal>::infinity(),
                                          -std::numeric_limits<PDFReal>::infinity() };

    for (PDFReal value : values)
    {
        PDFObject object = PDFObject::createReal(value);
        QVERIFY(object.isReal());
        QCOMPARE(object.getType(), PDFObject::Type::Real);
        QCOMPARE(object.getReal(), value);
        QCOMPARE(std::signbit(object.getReal()), std::signbit(value));
        QCOMPARE(object.getContentReferenceCount(), uint32_t(0));
        QVERIFY(object == PDFObject::createReal(value));

        PDFObject copy = object;
        QCOMPARE(copy.getReal(), value);
    }

    // Positive and negative zero are equal numbers
    QVERIFY(PDFObject::createReal(0.0) == PDFObject::createReal(-0.0));

    // NaN is not equal to anything, including itself
    PDFObject nan = PDFObject::createReal(std::numeric_limits<PDFReal>::quiet_NaN());
    QVERIFY(std::isnan(nan.getReal()));
    const PDFObject& nanAlias = nan;
    QVERIFY(!(nan == nanAlias));
    QVERIFY(nan != nanAlias);
    PDFObject nanCopy = nan;
    QVERIFY(std::isnan(nanCopy.getReal()));
    QVERIFY(nan != nanCopy);
}

void PDFObjectTest::test_reference()
{
    const PDFInteger int32Min = std::numeric_limits<int32_t>::min();
    const PDFInteger int32Max = std::numeric_limits<int32_t>::max();
    const PDFInteger int64Min = std::numeric_limits<PDFInteger>::min();
    const PDFInteger int64Max = std::numeric_limits<PDFInteger>::max();

    // Generations in the range of 32-bit integer are stored inplace, others in the heap
    const std::vector<std::pair<PDFObjectReference, bool>> references =
    {
        { PDFObjectReference(0, 0), false },
        { PDFObjectReference(1, 0), false },
        { PDFObjectReference(12345, 65535), false },
        { PDFObjectReference(-1, -1), false },
        { PDFObjectReference(int64Max, int32Max), false },
        { PDFObjectReference(int64Min, int32Min), false },
        { PDFObjectReference(7, int32Max + 1), true },
        { PDFObjectReference(7, int32Min - 1), true },
        { PDFObjectReference(int64Max, int64Max), true },
        { PDFObjectReference(int64Min, int64Min), true },
    };

    for (const auto& [reference, isInHeap] : references)
    {
        PDFObject object = PDFObject::createReference(reference);
        QVERIFY(object.isReference());
        QCOMPARE(object.getType(), PDFObject::Type::Reference);
        QCOMPARE(object.getReference().objectNumber, reference.objectNumber);
        QCOMPARE(object.getReference().generation, reference.generation);
        QCOMPARE(object.getContentReferenceCount(), uint32_t(isInHeap ? 1 : 0));

        PDFObject copy = object;
        QCOMPARE(copy.getContentReferenceCount(), uint32_t(isInHeap ? 2 : 0));
        QVERIFY(copy == object);
        QVERIFY(object == PDFObject::createReference(reference));
        QVERIFY(object != PDFObject::createReference(PDFObjectReference(reference.objectNumber ^ 1, reference.generation)));
        QVERIFY(object != PDFObject::createReference(PDFObjectReference(reference.objectNumber, reference.generation ^ 1)));
        QVERIFY(object != PDFObject::createInteger(reference.objectNumber));
    }
}

void PDFObjectTest::test_string()
{
    std::vector<qsizetype> lengths;
    for (qsizetype length = 0; length <= 40; ++length)
    {
        lengths.push_back(length);
    }
    lengths.insert(lengths.end(), { 255, 256, 1000, 65536, 1000000 });

    for (PDFObject::Type type : { PDFObject::Type::String, PDFObject::Type::Name })
    {
        auto create = [type](QByteArray data) { return type == PDFObject::Type::String ? PDFObject::createString(std::move(data)) : PDFObject::createName(std::move(data)); };
        const PDFObject::Type otherType = type == PDFObject::Type::String ? PDFObject::Type::Name : PDFObject::Type::String;

        for (qsizetype length : lengths)
        {
            const QByteArray data = createData(length, int(length));
            const bool isInplace = length <= PDFInplaceString::MAX_STRING_SIZE;
            const QString message = QString("type %1, length %2").arg(int(type)).arg(length);

            PDFObject object = create(data);
            QVERIFY2(object.getType() == type, qPrintable(message));
            QVERIFY2(object.isString() == (type == PDFObject::Type::String), qPrintable(message));
            QVERIFY2(object.isName() == (type == PDFObject::Type::Name), qPrintable(message));
            QVERIFY2(object.getString() == data, qPrintable(message));
            QVERIFY2(object.getContentReferenceCount() == (isInplace ? 0u : 1u), qPrintable(message));

            PDFStringRef stringRef = object.getStringObject();
            QVERIFY2((stringRef.inplaceString != nullptr) == isInplace, qPrintable(message));
            QVERIFY2((stringRef.memoryString != nullptr) == !isInplace, qPrintable(message));
            QVERIFY2(stringRef.getString() == data, qPrintable(message));

            if (isInplace)
            {
                QVERIFY2(stringRef.inplaceString->size == length, qPrintable(message));
            }
            else
            {
                // String in the heap shares the data, no copy is created
                QVERIFY2(stringRef.memoryString->getString().constData() == data.constData(), qPrintable(message));
            }

            // Equal strings, different strings and different types
            QVERIFY2(object == create(data), qPrintable(message));
            QVERIFY2(object == create(QByteArray(data.constData(), data.size())), qPrintable(message));
            QVERIFY2(!(object != create(data)), qPrintable(message));
            QVERIFY2(object != create(data + 'x'), qPrintable(message));
            QVERIFY2(object.getType() != otherType && object != (otherType == PDFObject::Type::String ? PDFObject::createString(data) : PDFObject::createName(data)), qPrintable(message));

            if (length > 0)
            {
                QByteArray changedFirst = data;
                changedFirst[0] = char(changedFirst[0] ^ 1);
                QByteArray changedLast = data;
                changedLast[length - 1] = char(changedLast[length - 1] ^ 0x80);

                QVERIFY2(object != create(changedFirst), qPrintable(message));
                QVERIFY2(object != create(changedLast), qPrintable(message));
                QVERIFY2(object != create(data.left(length - 1)), qPrintable(message));
            }
        }
    }

    // Strings with zero bytes only
    QVERIFY(PDFObject::createString(QByteArray(3, '\0')) != PDFObject::createString(QByteArray(4, '\0')));
    QVERIFY(PDFObject::createString(QByteArray(3, '\0')) != PDFObject::createString(QByteArray()));
    QVERIFY(PDFObject::createString(QByteArray(20, '\0')) != PDFObject::createString(QByteArray(21, '\0')));
    QVERIFY(PDFObject::createString(QByteArray(20, '\0')) == PDFObject::createString(QByteArray(20, '\0')));
    QCOMPARE(PDFObject::createString(QByteArray(14, '\0')).getString(), QByteArray(14, '\0'));
}

void PDFObjectTest::test_string_ref()
{
    const QByteArray shortData("Short");
    const QByteArray longData("This string is stored in the heap");

    for (const QByteArray& data : { shortData, longData })
    {
        PDFObject name = PDFObject::createName(data);
        PDFObject string = PDFObject::createString(data);

        PDFObject nameFromRef = PDFObject::createName(string.getStringObject());
        PDFObject stringFromRef = PDFObject::createString(name.getStringObject());

        QVERIFY(nameFromRef == name);
        QVERIFY(stringFromRef == string);
        QVERIFY(nameFromRef.isName());
        QVERIFY(stringFromRef.isString());
        QCOMPARE(nameFromRef.getString(), data);
        QCOMPARE(stringFromRef.getString(), data);
    }

    // Empty reference creates empty string
    PDFObject empty = PDFObject::createString(PDFStringRef());
    QVERIFY(empty.isString());
    QCOMPARE(empty.getString(), QByteArray());
    QVERIFY(empty == PDFObject::createString(QByteArray()));

    // Memory string with short content (created by hand) is stored inplace
    PDFString shortMemoryString(QByteArray("abc"));
    PDFStringRef shortMemoryRef;
    shortMemoryRef.memoryString = &shortMemoryString;
    PDFObject fromShortMemory = PDFObject::createName(shortMemoryRef);
    QVERIFY(fromShortMemory.getStringObject().inplaceString);
    QVERIFY(fromShortMemory == PDFObject::createName("abc"));

    // Inplace string with garbage behind the end of the string (members are public)
    PDFInplaceString garbage("abc", 3);
    garbage.string[10] = 'X';
    PDFStringRef garbageRef;
    garbageRef.inplaceString = &garbage;
    QVERIFY(PDFObject::createName(garbageRef) == PDFObject::createName("abc"));

    // Inplace string with invalid size is clamped
    PDFInplaceString invalidSize("abcdefghijklmn", 14);
    invalidSize.size = 200;
    PDFStringRef invalidSizeRef;
    invalidSizeRef.inplaceString = &invalidSize;
    QCOMPARE(invalidSizeRef.getString(), QByteArray("abcdefghijklmn"));
    QCOMPARE(PDFObject::createString(invalidSizeRef).getString(), QByteArray("abcdefghijklmn"));
    QCOMPARE(invalidSize.getString(), QByteArray("abcdefghijklmn"));
}

void PDFObjectTest::test_type_mismatch()
{
    using Type = PDFObject::Type;

    struct Accessor
    {
        const char* name;
        std::vector<Type> acceptedTypes;
        std::function<void(const PDFObject&)> function;
    };

    const std::vector<Accessor> accessors =
    {
        { "getBool", { Type::Bool }, [](const PDFObject& object) { object.getBool(); } },
        { "getInteger", { Type::Int }, [](const PDFObject& object) { object.getInteger(); } },
        { "getReal", { Type::Real }, [](const PDFObject& object) { object.getReal(); } },
        { "getString", { Type::String, Type::Name }, [](const PDFObject& object) { object.getString(); } },
        { "getStringObject", { Type::String, Type::Name }, [](const PDFObject& object) { object.getStringObject(); } },
        { "getArray", { Type::Array }, [](const PDFObject& object) { object.getArray(); } },
        { "getDictionary", { Type::Dictionary }, [](const PDFObject& object) { object.getDictionary(); } },
        { "getStream", { Type::Stream }, [](const PDFObject& object) { object.getStream(); } },
        { "getReference", { Type::Reference }, [](const PDFObject& object) { object.getReference(); } },
    };

    for (const PDFObject& object : createSampleObjects())
    {
        for (const Accessor& accessor : accessors)
        {
            const bool accepted = std::find(accessor.acceptedTypes.cbegin(), accessor.acceptedTypes.cend(), object.getType()) != accessor.acceptedTypes.cend();
            const bool throws = throwsPDFException([&]() { accessor.function(object); });
            QVERIFY2(throws != accepted, qPrintable(QString("%1 on object of type %2").arg(accessor.name).arg(int(object.getType()))));
        }
    }
}

void PDFObjectTest::test_copy_and_move()
{
    const std::vector<PDFObject> samples = createSampleObjects();

    for (size_t i = 0; i < samples.size(); ++i)
    {
        const PDFObject& sample = samples[i];
        const QString message = QString("sample %1").arg(i);
        const bool hasContent = sample.getContentReferenceCount() > 0;
        const uint32_t base = sample.getContentReferenceCount();
        auto expectedCount = [hasContent, base](uint32_t added) { return hasContent ? base + added : uint32_t(0); };

        {
            // Copy constructor
            PDFObject copy(sample);
            QVERIFY2(copy == sample, qPrintable(message));
            QVERIFY2(copy.getType() == sample.getType(), qPrintable(message));
            QVERIFY2(sample.getContentReferenceCount() == expectedCount(1), qPrintable(message));

            // Move constructor - moved object becomes null, count is unchanged
            PDFObject moved(std::move(copy));
            QVERIFY2(copy.isNull(), qPrintable(message));
            QVERIFY2(copy.getContentReferenceCount() == 0, qPrintable(message));
            QVERIFY2(moved.getType() == sample.getType(), qPrintable(message));
            QVERIFY2(sample.getContentReferenceCount() == expectedCount(1), qPrintable(message));

            // Copy assignment to objects of all types
            for (const PDFObject& other : samples)
            {
                PDFObject target = other;
                target = sample;
                QVERIFY2(target.getType() == sample.getType(), qPrintable(message));
                QVERIFY2(sample.getContentReferenceCount() == expectedCount(2), qPrintable(message));
            }
            QVERIFY2(sample.getContentReferenceCount() == expectedCount(1), qPrintable(message));

            // Move assignment to objects of all types
            for (const PDFObject& other : samples)
            {
                PDFObject source = sample;
                PDFObject target = other;
                target = std::move(source);
                QVERIFY2(source.isNull(), qPrintable(message));
                QVERIFY2(target.getType() == sample.getType(), qPrintable(message));
                QVERIFY2(sample.getContentReferenceCount() == expectedCount(2), qPrintable(message));
            }
            QVERIFY2(sample.getContentReferenceCount() == expectedCount(1), qPrintable(message));

            // Swap
            PDFObject other = PDFObject::createInteger(5);
            moved.swap(other);
            QVERIFY2(moved.getInteger() == 5, qPrintable(message));
            QVERIFY2(other.getType() == sample.getType(), qPrintable(message));
            std::swap(moved, other);
            QVERIFY2(other.getInteger() == 5, qPrintable(message));
            QVERIFY2(moved.getType() == sample.getType(), qPrintable(message));
            QVERIFY2(sample.getContentReferenceCount() == expectedCount(1), qPrintable(message));

            // Assignment of null releases the content
            moved = PDFObject();
            QVERIFY2(moved.isNull(), qPrintable(message));
            QVERIFY2(sample.getContentReferenceCount() == expectedCount(0), qPrintable(message));
        }

        QVERIFY2(sample.getContentReferenceCount() == base, qPrintable(message));
    }
}

void PDFObjectTest::test_self_assignment_and_aliasing()
{
    QByteArray probe = createProbe(1);

    {
        PDFObject object = createArray({ createStream({ }, probe), PDFObject::createName("NameStoredInTheHeap") });
        PDFObject& alias = object;

        // Self copy assignment
        object = alias;
        QCOMPARE(object.getContentReferenceCount(), uint32_t(1));
        QCOMPARE(object.getArray()->getCount(), size_t(2));

        // Self move assignment keeps the object unchanged
        object = std::move(alias);
        QVERIFY(object.isArray());
        QCOMPARE(object.getContentReferenceCount(), uint32_t(1));
        QCOMPARE(object.getArray()->getCount(), size_t(2));

        // Self swap
        object.swap(alias);
        QVERIFY(object.isArray());
        QCOMPARE(object.getContentReferenceCount(), uint32_t(1));

        // Assignment of the item, which is owned by the object itself (the array
        // is destroyed during the assignment, the item must survive)
        object = object.getArray()->getItem(0);
        QVERIFY(object.isStream());
        QCOMPARE(object.getContentReferenceCount(), uint32_t(1));
        QCOMPARE(*object.getStream()->getContent(), probe);

        // The same for the dictionary
        object = createDictionary({ { "Key", PDFObject(object) } });
        object = object.getDictionary()->get("Key");
        QVERIFY(object.isStream());
        QCOMPARE(object.getContentReferenceCount(), uint32_t(1));
        QCOMPARE(*object.getStream()->getContent(), probe);
        QVERIFY(!probe.isDetached());
    }

    QVERIFY(probe.isDetached());

    // Dictionary entry with the key, which references the dictionary itself
    PDFDictionary dictionary;
    dictionary.addEntry(PDFInplaceOrMemoryString("VeryLongKeyOfTheDictionary"), PDFObject::createInteger(1));
    dictionary.addEntry(PDFInplaceOrMemoryString("Short"), PDFObject::createInteger(2));
    dictionary.optimize();
    QCOMPARE(dictionary.getCapacity(), dictionary.getCount());
    dictionary.addEntry(dictionary.getKey(0), PDFObject::createInteger(3));
    QCOMPARE(dictionary.getCount(), size_t(3));
    QCOMPARE(dictionary.getKey(2).getString(), QByteArray("VeryLongKeyOfTheDictionary"));
    dictionary.setEntry(dictionary.getKey(1), PDFObject::createInteger(4));
    QCOMPARE(dictionary.get("Short").getInteger(), PDFInteger(4));

    // Array item set from the array itself
    PDFArray array(std::vector<PDFObject>{ PDFObject::createName("NameStoredInTheHeap"), PDFObject::createInteger(1) });
    array.setItem(array.getItem(0), 1);
    QCOMPARE(array.getItem(1).getString(), QByteArray("NameStoredInTheHeap"));
    QCOMPARE(array.getItem(0).getContentReferenceCount(), uint32_t(2));
}

void PDFObjectTest::test_content_lifetime()
{
    QByteArray stringProbe = createProbe(1);
    QByteArray streamProbe = createProbe(2);
    QByteArray keyProbe = createProbe(3);
    QByteArray nameProbe = createProbe(4);

    PDFObject copyOfNested;

    {
        PDFObject string = PDFObject::createString(stringProbe);
        PDFObject name = PDFObject::createName(nameProbe);
        PDFObject stream = createStream({ }, streamProbe);

        PDFDictionary dictionary;
        dictionary.addEntry(PDFInplaceOrMemoryString(keyProbe), PDFObject(stream));
        dictionary.addEntry(PDFInplaceOrMemoryString("Name"), PDFObject(name));
        PDFObject root = createArray({ PDFObject::createDictionary(std::move(dictionary)), string });

        QVERIFY(!stringProbe.isDetached());
        QVERIFY(!streamProbe.isDetached());
        QVERIFY(!keyProbe.isDetached());
        QVERIFY(!nameProbe.isDetached());

        QCOMPARE(stream.getContentReferenceCount(), uint32_t(2));
        QCOMPARE(string.getContentReferenceCount(), uint32_t(2));

        // Destroy the local objects, content is still referenced by the root
        stream = PDFObject();
        string = PDFObject();
        name = PDFObject();
        QVERIFY(!stringProbe.isDetached());
        QVERIFY(!streamProbe.isDetached());
        QVERIFY(!nameProbe.isDetached());

        // Keep nested object, destroy the root
        copyOfNested = root.getArray()->getItem(0);
        root = PDFObject();
        QVERIFY(stringProbe.isDetached());
        QVERIFY(!streamProbe.isDetached());
        QVERIFY(!keyProbe.isDetached());
        QVERIFY(!nameProbe.isDetached());
        QCOMPARE(copyOfNested.getContentReferenceCount(), uint32_t(1));
        QCOMPARE(*copyOfNested.getDictionary()->get(keyProbe).getStream()->getContent(), streamProbe);
    }

    copyOfNested = PDFObject();
    QVERIFY(streamProbe.isDetached());
    QVERIFY(keyProbe.isDetached());
    QVERIFY(nameProbe.isDetached());

    // Values of the dictionary copied to the stack
    QByteArray valueProbe = createProbe(5);
    {
        PDFObject object = createDictionary({ { "Value", PDFObject::createString(valueProbe) } });
        PDFDictionary copy = *object.getDictionary();
        object = PDFObject();
        QVERIFY(!valueProbe.isDetached());
        QCOMPARE(copy.get("Value").getString(), valueProbe);
    }
    QVERIFY(valueProbe.isDetached());
}

void PDFObjectTest::test_vector_of_objects()
{
    QByteArray probe = createProbe(1);
    PDFObject shared = PDFObject::createString(probe);

    {
        std::vector<PDFObject> objects;
        for (int i = 0; i < 10000; ++i)
        {
            // Reallocations must move objects, not copy them
            objects.push_back(i % 2 ? shared : PDFObject::createInteger(i));
        }
        QCOMPARE(shared.getContentReferenceCount(), uint32_t(5001));

        objects.erase(objects.begin(), objects.begin() + 1000);
        QCOMPARE(shared.getContentReferenceCount(), uint32_t(4501));

        objects.insert(objects.begin() + 10, 100, shared);
        QCOMPARE(shared.getContentReferenceCount(), uint32_t(4601));

        std::reverse(objects.begin(), objects.end());
        std::stable_partition(objects.begin(), objects.end(), [](const PDFObject& object) { return object.isInt(); });
        QCOMPARE(shared.getContentReferenceCount(), uint32_t(4601));
        QVERIFY(std::is_partitioned(objects.cbegin(), objects.cend(), [](const PDFObject& object) { return object.isInt(); }));

        std::vector<PDFObject> copy = objects;
        QCOMPARE(shared.getContentReferenceCount(), uint32_t(9201));
        QVERIFY(copy == objects);

        objects.clear();
        objects.shrink_to_fit();
        QCOMPARE(shared.getContentReferenceCount(), uint32_t(4601));
    }

    QCOMPARE(shared.getContentReferenceCount(), uint32_t(1));
    shared = PDFObject();
    QVERIFY(probe.isDetached());
}

void PDFObjectTest::test_array()
{
    PDFObject empty = createArray({ });
    QVERIFY(empty.isArray());
    QCOMPARE(empty.getArray()->getCount(), size_t(0));
    QVERIFY(empty.getArray()->begin() == empty.getArray()->end());
    QVERIFY_THROWS_EXCEPTION(std::out_of_range, empty.getArray()->getItem(0));

    std::vector<PDFObject> items;
    items.push_back(PDFObject::createInteger(1));
    items.push_back(PDFObject::createReal(2.5));
    items.push_back(PDFObject::createName("Three"));
    items.push_back(createArray({ PDFObject::createInteger(4) }));
    items.reserve(100);

    PDFObject object = PDFObject::createArray(std::vector<PDFObject>(items));
    const PDFArray* array = object.getArray();
    QCOMPARE(array->getCount(), size_t(4));
    QCOMPARE(array->getCapacity(), size_t(4));
    QCOMPARE(array->getItem(0).getInteger(), PDFInteger(1));
    QCOMPARE(array->getItem(1).getReal(), 2.5);
    QCOMPARE(array->getItem(2).getString(), QByteArray("Three"));
    QCOMPARE(array->getItem(3).getArray()->getItem(0).getInteger(), PDFInteger(4));
    QVERIFY_THROWS_EXCEPTION(std::out_of_range, array->getItem(4));

    size_t count = 0;
    for (const PDFObject& item : *array)
    {
        QVERIFY(item == items[count]);
        ++count;
    }
    QCOMPARE(count, size_t(4));

    // Structural equality
    QVERIFY(object == PDFObject::createArray(std::vector<PDFObject>(items)));
    QVERIFY(object != empty);
    items[3] = createArray({ PDFObject::createInteger(5) });
    QVERIFY(object != PDFObject::createArray(std::vector<PDFObject>(items)));
    items.pop_back();
    QVERIFY(object != PDFObject::createArray(std::vector<PDFObject>(items)));

    // Array with NaN is not equal to itself (structural comparison)
    PDFObject nanArray = createArray({ PDFObject::createReal(std::numeric_limits<PDFReal>::quiet_NaN()) });
    const PDFObject& nanArrayAlias = nanArray;
    QVERIFY(nanArray != nanArrayAlias);

    // Modification of the copy doesn't change the original
    PDFArray copy = *array;
    copy.appendItem(PDFObject::createInteger(5));
    copy.setItem(PDFObject::createNull(), 0);
    QCOMPARE(copy.getCount(), size_t(5));
    QCOMPARE(array->getCount(), size_t(4));
    QCOMPARE(array->getItem(0).getInteger(), PDFInteger(1));
    QCOMPARE(copy.getItem(3).getContentReferenceCount(), uint32_t(2));
}

void PDFObjectTest::test_dictionary()
{
    // Keys of all lengths, including the boundary of inplace strings
    std::vector<QByteArray> keys;
    for (int length = 0; length <= 32; ++length)
    {
        QByteArray key(length, Qt::Uninitialized);
        for (int i = 0; i < length; ++i)
        {
            key[i] = char('A' + (i + length) % 26);
        }
        keys.push_back(key);
    }
    keys.push_back(QByteArray("Zero\0Byte", 9));
    keys.push_back(QByteArray("VeryLongKey\0WithZeroByte", 24));

    PDFDictionary dictionary;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        dictionary.addEntry(PDFInplaceOrMemoryString(keys[i]), PDFObject::createInteger(PDFInteger(i)));
    }

    PDFObject object = PDFObject::createDictionary(PDFDictionary(dictionary));
    const PDFDictionary* stored = object.getDictionary();
    QCOMPARE(stored->getCount(), keys.size());
    QCOMPARE(stored->getCapacity(), keys.size());

    for (size_t i = 0; i < keys.size(); ++i)
    {
        const QByteArray& key = keys[i];
        const QString message = QString("key %1").arg(QString::fromLatin1(key.toHex()));
        const PDFInteger value = PDFInteger(i);

        QVERIFY2(stored->getKey(i).getString() == key, qPrintable(message));
        QVERIFY2(stored->getValue(i).getInteger() == value, qPrintable(message));
        QVERIFY2(stored->get(key).getInteger() == value, qPrintable(message));
        QVERIFY2(stored->get(PDFInplaceOrMemoryString(key)).getInteger() == value, qPrintable(message));
        QVERIFY2(stored->hasKey(key), qPrintable(message));

        if (!key.contains('\0'))
        {
            QVERIFY2(stored->get(key.constData()).getInteger() == value, qPrintable(message));
            QVERIFY2(stored->hasKey(key.constData()), qPrintable(message));
        }

        // Keys, which differ in one character, or which are prefixes or extensions
        if (!key.isEmpty())
        {
            QByteArray changedFirst = key;
            changedFirst[0] = char(changedFirst[0] ^ 0x20);
            QByteArray changedLast = key;
            changedLast[key.size() - 1] = char(changedLast[key.size() - 1] ^ 0x20);
            QVERIFY2(stored->get(changedFirst).isNull(), qPrintable(message));
            QVERIFY2(stored->get(changedLast).isNull(), qPrintable(message));
            QVERIFY2(!stored->hasKey(changedLast), qPrintable(message));
        }

        QVERIFY2(stored->get(key + "#").isNull(), qPrintable(message));
        QVERIFY2(stored->get(PDFInplaceOrMemoryString(key + "#")).isNull(), qPrintable(message));
    }

    // Modifications
    dictionary.setEntry(PDFInplaceOrMemoryString(keys[5]), PDFObject::createName("Five"));
    dictionary.setEntry(PDFInplaceOrMemoryString(keys[20]), PDFObject::createName("Twenty"));
    dictionary.setEntry(PDFInplaceOrMemoryString("NewKey"), PDFObject::createNull());
    QCOMPARE(dictionary.getCount(), keys.size() + 1);
    QCOMPARE(dictionary.get(keys[5]).getString(), QByteArray("Five"));
    QCOMPARE(dictionary.get(keys[20]).getString(), QByteArray("Twenty"));
    QVERIFY(dictionary.hasKey("NewKey"));

    dictionary.removeNullObjects();
    QCOMPARE(dictionary.getCount(), keys.size());
    QVERIFY(!dictionary.hasKey("NewKey"));

    dictionary.removeEntry(keys[14].constData());
    dictionary.removeEntry(keys[15].constData());
    dictionary.removeEntry("Missing");
    QCOMPARE(dictionary.getCount(), keys.size() - 2);
    QVERIFY(!dictionary.hasKey(keys[14]));
    QVERIFY(!dictionary.hasKey(keys[15]));
    QVERIFY(dictionary.hasKey(keys[13]));
    QVERIFY(dictionary.hasKey(keys[16]));

    // Stored dictionary is not changed by modification of the original
    QCOMPARE(stored->getCount(), keys.size());
    QCOMPARE(stored->get(keys[5]).getInteger(), PDFInteger(5));

    // Duplicate key - first entry is found
    PDFDictionary duplicate;
    duplicate.addEntry(PDFInplaceOrMemoryString("Key"), PDFObject::createInteger(1));
    duplicate.addEntry(PDFInplaceOrMemoryString("Key"), PDFObject::createInteger(2));
    QCOMPARE(duplicate.get("Key").getInteger(), PDFInteger(1));

    // Equality is sensitive to the order of the entries
    QVERIFY(createDictionary({ { "A", PDFObject::createInteger(1) }, { "B", PDFObject::createInteger(2) } }) ==
            createDictionary({ { "A", PDFObject::createInteger(1) }, { "B", PDFObject::createInteger(2) } }));
    QVERIFY(createDictionary({ { "A", PDFObject::createInteger(1) }, { "B", PDFObject::createInteger(2) } }) !=
            createDictionary({ { "B", PDFObject::createInteger(2) }, { "A", PDFObject::createInteger(1) } }));
    QVERIFY(createDictionary({ { "A", PDFObject::createInteger(1) } }) != createDictionary({ { "A", PDFObject::createInteger(2) } }));
    QVERIFY(createDictionary({ { "A", PDFObject::createInteger(1) } }) != createDictionary({ { "B", PDFObject::createInteger(1) } }));

    // Empty dictionary
    PDFDictionary emptyDictionary;
    QVERIFY(emptyDictionary.isEmpty());
    QVERIFY(emptyDictionary.get("").isNull());
    QVERIFY(emptyDictionary.get(keys.back()).isNull());
    QVERIFY(!emptyDictionary.hasKey("Key"));
}

void PDFObjectTest::test_dictionary_keys()
{
    // Default key is an empty inplace string
    PDFInplaceOrMemoryString defaultKey;
    QVERIFY(defaultKey.isInplace());
    QCOMPARE(defaultKey.getString(), QByteArray());
    QCOMPARE(defaultKey.getView().size(), qsizetype(0));
    QVERIFY(defaultKey == PDFInplaceOrMemoryString(""));
    QVERIFY(defaultKey == PDFInplaceOrMemoryString(QByteArray()));
    QVERIFY(defaultKey == "");
    QCOMPARE(defaultKey.getContentReferenceCount(), uint32_t(0));

    for (int length = 0; length <= 40; ++length)
    {
        const QByteArray data = createData(length, length);
        const bool isInplace = length <= PDFInplaceString::MAX_STRING_SIZE;
        const QString message = QString("length %1").arg(length);

        PDFInplaceOrMemoryString key(data);
        PDFInplaceOrMemoryString keyFromPointer(data.constData(), size_t(data.size()));

        QVERIFY2(key.isInplace() == isInplace, qPrintable(message));
        QVERIFY2(keyFromPointer.isInplace() == isInplace, qPrintable(message));
        QVERIFY2(key.getString() == data, qPrintable(message));
        QVERIFY2(key.getView() == QByteArrayView(data), qPrintable(message));
        QVERIFY2(key == keyFromPointer, qPrintable(message));
        QVERIFY2(key == data, qPrintable(message));
        QVERIFY2(key.equals(data.constData(), size_t(data.size())), qPrintable(message));
        QVERIFY2(key != PDFInplaceOrMemoryString(data + 'x'), qPrintable(message));
        QVERIFY2(!(key == data + 'x'), qPrintable(message));
        QVERIFY2(key.getContentReferenceCount() == (isInplace ? 0u : 1u), qPrintable(message));

        if (!isInplace)
        {
            // Byte array is shared, not copied
            QVERIFY2(key.getView().constData() == data.constData(), qPrintable(message));
        }

        if (length > 0)
        {
            QByteArray changed = data;
            changed[length - 1] = char(changed[length - 1] ^ 1);
            QVERIFY2(key != PDFInplaceOrMemoryString(changed), qPrintable(message));
            QVERIFY2(!key.equals(changed.constData(), size_t(changed.size())), qPrintable(message));
        }

        // Copy, move, swap and self assignment
        {
            PDFInplaceOrMemoryString copy = key;
            QVERIFY2(copy == key, qPrintable(message));
            QVERIFY2(key.getContentReferenceCount() == (isInplace ? 0u : 2u), qPrintable(message));

            PDFInplaceOrMemoryString moved = std::move(copy);
            QVERIFY2(copy.isInplace() && copy.getString().isEmpty(), qPrintable(message));
            QVERIFY2(moved == key, qPrintable(message));
            QVERIFY2(key.getContentReferenceCount() == (isInplace ? 0u : 2u), qPrintable(message));

            PDFInplaceOrMemoryString& alias = moved;
            moved = alias;
            moved = std::move(alias);
            QVERIFY2(moved == key, qPrintable(message));
            QVERIFY2(key.getContentReferenceCount() == (isInplace ? 0u : 2u), qPrintable(message));

            PDFInplaceOrMemoryString other("Other");
            moved.swap(other);
            QVERIFY2(moved == "Other", qPrintable(message));
            QVERIFY2(other == key, qPrintable(message));

            other = PDFInplaceOrMemoryString();
            QVERIFY2(key.getContentReferenceCount() == (isInplace ? 0u : 1u), qPrintable(message));
        }
    }

    // Keys with zero bytes
    QVERIFY(PDFInplaceOrMemoryString(QByteArray("a\0b", 3)) != PDFInplaceOrMemoryString(QByteArray("a\0c", 3)));
    QVERIFY(PDFInplaceOrMemoryString(QByteArray("a\0b", 3)) != PDFInplaceOrMemoryString("a"));
    QVERIFY(PDFInplaceOrMemoryString(QByteArray(20, '\0')) != PDFInplaceOrMemoryString(QByteArray(21, '\0')));
    QVERIFY(PDFInplaceOrMemoryString(QByteArray(20, '\0')) == PDFInplaceOrMemoryString(QByteArray(20, '\0')));
    QVERIFY(PDFInplaceOrMemoryString("a", 1) == PDFInplaceOrMemoryString("abc", 1));
}

void PDFObjectTest::test_stream()
{
    QByteArray content = createData(10000, 3);
    PDFObject stream = createStream({ { "Filter", PDFObject::createName("FlateDecode") } }, content);

    QVERIFY(stream.isStream());
    QCOMPARE(stream.getType(), PDFObject::Type::Stream);
    const PDFStream* streamContent = stream.getStream();
    QCOMPARE(*streamContent->getContent(), content);
    QCOMPARE(streamContent->getContent()->constData(), content.constData());
    QCOMPARE(streamContent->getDictionary()->get("Filter").getString(), QByteArray("FlateDecode"));
    QCOMPARE(streamContent->getDictionary()->get("Length").getInteger(), PDFInteger(10000));

    QVERIFY(stream == createStream({ { "Filter", PDFObject::createName("FlateDecode") } }, content));
    QVERIFY(stream != createStream({ { "Filter", PDFObject::createName("LZWDecode") } }, content));
    QVERIFY(stream != createStream({ { "Filter", PDFObject::createName("FlateDecode") } }, content.left(9999) + 'x'));
    QVERIFY(stream != createDictionary({ { "Filter", PDFObject::createName("FlateDecode") }, { "Length", PDFObject::createInteger(10000) } }));

    // Copy of the stream content
    PDFStream copy = *streamContent;
    QCOMPARE(*copy.getContent(), content);
    QVERIFY(copy == *streamContent);

    // Detached stream data with capacity are shrinked
    QByteArray reserved = createData(100, 4);
    reserved.reserve(10000);
    PDFObject shrinked = PDFObject::createStream(PDFStream(PDFDictionary(), std::move(reserved)));
    QCOMPARE(shrinked.getStream()->getContent()->capacity(), qsizetype(100));
}

void PDFObjectTest::test_equality()
{
    const std::vector<PDFObject> samples = createSampleObjects();
    const std::vector<PDFObject> samplesAgain = createSampleObjects();

    for (size_t i = 0; i < samples.size(); ++i)
    {
        for (size_t j = 0; j < samples.size(); ++j)
        {
            const bool expected = i == j;
            const QString message = QString("samples %1 and %2").arg(i).arg(j);
            QVERIFY2((samples[i] == samples[j]) == expected, qPrintable(message));
            QVERIFY2((samples[i] != samples[j]) == !expected, qPrintable(message));
            QVERIFY2((samples[i] == samplesAgain[j]) == expected, qPrintable(message));
            QVERIFY2((samplesAgain[j] == samples[i]) == expected, qPrintable(message));
        }
    }

    // Nested structures
    auto createTree = []()
    {
        return createArray({ createDictionary({ { "Kids", createArray({ PDFObject::createReference(PDFObjectReference(3, 0)) }) },
                                                { "VeryLongKeyOfTheDictionary", PDFObject::createString("A string, which is stored in the heap") } }),
                             createStream({ { "Filter", PDFObject::createName("FlateDecode") } }, QByteArray("data")) });
    };

    QVERIFY(createTree() == createTree());
    PDFObject tree = createTree();
    PDFObject copy = tree;
    QVERIFY(tree == copy);
}

void PDFObjectTest::test_visitor()
{
    PDFObjectTestVisitor visitor;
    PDFObject::createNull().accept(&visitor);
    PDFObject::createBool(true).accept(&visitor);
    PDFObject::createInteger(-5).accept(&visitor);
    PDFObject::createReal(1.5).accept(&visitor);
    PDFObject::createString("text").accept(&visitor);
    PDFObject::createString("A string, which is stored in the heap").accept(&visitor);
    PDFObject::createName("Name").accept(&visitor);
    createArray({ PDFObject::createInteger(1), PDFObject::createInteger(2) }).accept(&visitor);
    createDictionary({ { "A", PDFObject::createInteger(1) } }).accept(&visitor);
    createStream({ }, QByteArray("abc")).accept(&visitor);
    PDFObject::createReference(PDFObjectReference(12, 3)).accept(&visitor);
    PDFObject::createReference(PDFObjectReference(12, PDFInteger(1) << 40)).accept(&visitor);

    const QStringList expected =
    {
        "null",
        "bool 1",
        "int -5",
        "real 1.5",
        "string text",
        "string A string, which is stored in the heap",
        "name Name",
        "array 2",
        "dictionary 1",
        "stream 3",
        "reference 12 3",
        QString("reference 12 %1").arg(PDFInteger(1) << 40),
    };

    QCOMPARE(visitor.getLog(), expected);
}

void PDFObjectTest::test_manipulator()
{
    PDFObject left = createDictionary({ { "A", PDFObject::createInteger(1) },
                                        { "Nested", createDictionary({ { "X", PDFObject::createInteger(1) }, { "Y", PDFObject::createInteger(2) } }) },
                                        { "Array", createArray({ PDFObject::createInteger(1) }) },
                                        { "Removed", PDFObject::createInteger(5) } });
    PDFObject right = createDictionary({ { "B", PDFObject::createInteger(2) },
                                         { "Nested", createDictionary({ { "Y", PDFObject::createInteger(3) } }) },
                                         { "Array", createArray({ PDFObject::createInteger(2) }) },
                                         { "Removed", PDFObject::createNull() } });

    PDFObject merged = PDFObjectManipulator::merge(left, right, PDFObjectManipulator::RemoveNullObjects);
    const PDFDictionary* dictionary = merged.getDictionary();
    QCOMPARE(dictionary->get("A").getInteger(), PDFInteger(1));
    QCOMPARE(dictionary->get("B").getInteger(), PDFInteger(2));
    QCOMPARE(dictionary->get("Nested").getDictionary()->get("X").getInteger(), PDFInteger(1));
    QCOMPARE(dictionary->get("Nested").getDictionary()->get("Y").getInteger(), PDFInteger(3));
    QCOMPARE(dictionary->get("Array").getArray()->getCount(), size_t(1));
    QCOMPARE(dictionary->get("Array").getArray()->getItem(0).getInteger(), PDFInteger(2));
    QVERIFY(!dictionary->hasKey("Removed"));

    PDFObject concatenated = PDFObjectManipulator::merge(left, right, PDFObjectManipulator::ConcatenateArrays);
    QCOMPARE(concatenated.getDictionary()->get("Array").getArray()->getCount(), size_t(2));
    QVERIFY(concatenated.getDictionary()->hasKey("Removed"));

    // Array and a different type - right object is used
    PDFObject replaced = PDFObjectManipulator::merge(createArray({ PDFObject::createInteger(1) }), PDFObject::createInteger(5), PDFObjectManipulator::ConcatenateArrays);
    QCOMPARE(replaced.getInteger(), PDFInteger(5));
    replaced = PDFObjectManipulator::merge(PDFObject::createInteger(5), createArray({ PDFObject::createInteger(1) }), PDFObjectManipulator::ConcatenateArrays);
    QVERIFY(replaced.isArray());

    // Stream and dictionary - dictionary is merged, content is kept (and shared)
    QByteArray content = createData(1000, 5);
    PDFObject stream = createStream({ { "Filter", PDFObject::createName("FlateDecode") } }, content);
    PDFObject mergedStream = PDFObjectManipulator::merge(stream, createDictionary({ { "Filter", PDFObject::createName("LZWDecode") } }), PDFObjectManipulator::NoFlag);
    QVERIFY(mergedStream.isStream());
    QCOMPARE(mergedStream.getStream()->getDictionary()->get("Filter").getString(), QByteArray("LZWDecode"));
    QCOMPARE(*mergedStream.getStream()->getContent(), content);
    QCOMPARE(mergedStream.getStream()->getContent()->constData(), content.constData());

    // Removing null objects
    PDFObject withNulls = createDictionary({ { "A", PDFObject::createNull() }, { "B", createDictionary({ { "C", PDFObject::createNull() }, { "D", PDFObject::createInteger(1) } }) } });
    PDFObject withoutNulls = PDFObjectManipulator::removeNullObjects(withNulls);
    QVERIFY(!withoutNulls.getDictionary()->hasKey("A"));
    QVERIFY(!withoutNulls.getDictionary()->get("B").getDictionary()->hasKey("C"));
    QVERIFY(withoutNulls.getDictionary()->get("B").getDictionary()->hasKey("D"));

    // Duplicate references
    PDFObject references = createDictionary({ { "Kids", createArray({ PDFObject::createReference(PDFObjectReference(1, 0)),
                                                                      PDFObject::createReference(PDFObjectReference(2, 0)),
                                                                      PDFObject::createReference(PDFObjectReference(1, 0)),
                                                                      PDFObject::createReference(PDFObjectReference(1, PDFInteger(1) << 40)),
                                                                      PDFObject::createReference(PDFObjectReference(1, PDFInteger(1) << 40)) }) } });
    PDFObject unique = PDFObjectManipulator::removeDuplicitReferencesInArrays(references);
    QVERIFY(unique.getDictionary()->get("Kids") == createArray({ PDFObject::createReference(PDFObjectReference(1, 0)),
                                                                 PDFObject::createReference(PDFObjectReference(2, 0)),
                                                                 PDFObject::createReference(PDFObjectReference(1, PDFInteger(1) << 40)) }));
}

void PDFObjectTest::test_concurrent_copies()
{
    QByteArray probe = createProbe(1);
    QByteArray keyProbe = createProbe(2);

    PDFDictionary dictionary;
    dictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Page"));
    dictionary.addEntry(PDFInplaceOrMemoryString(keyProbe), PDFObject::createString(probe));
    dictionary.addEntry(PDFInplaceOrMemoryString("Kids"), createArray({ PDFObject::createReference(PDFObjectReference(1, 0)), createStream({ }, probe) }));
    PDFObject shared = PDFObject::createDictionary(std::move(dictionary));
    PDFObject expected = shared;

    const int threadCount = getThreadCount();
    const int iterations = 10000;
    std::atomic<int> errors = 0;

    runConcurrently(threadCount, [&](int threadIndex)
    {
        std::vector<PDFObject> localObjects;
        for (int i = 0; i < iterations; ++i)
        {
            PDFObject copy = shared;
            PDFObject second(copy);
            PDFObject moved(std::move(copy));
            second = moved;
            second = std::move(moved);

            const PDFDictionary* copyDictionary = second.getDictionary();
            if (copyDictionary->get("Type").getString() != "Page" ||
                copyDictionary->get(keyProbe).getString().size() != probe.size() ||
                copyDictionary->get("Kids").getArray()->getCount() != 2)
            {
                ++errors;
            }

            // Keep some copies for a while, so copies are destroyed in different order
            if ((i + threadIndex) % 7 == 0)
            {
                localObjects.push_back(copyDictionary->get("Kids"));
                localObjects.push_back(second);
            }
            if (localObjects.size() > 50)
            {
                localObjects.erase(localObjects.begin(), localObjects.begin() + 25);
            }
        }
    });

    QCOMPARE(errors.load(), 0);
    QCOMPARE(shared.getContentReferenceCount(), uint32_t(2));
    QCOMPARE(shared.getDictionary()->get("Kids").getContentReferenceCount(), uint32_t(1));
    QVERIFY(shared == expected);

    shared = PDFObject();
    QVERIFY(!probe.isDetached());
    expected = PDFObject();
    QVERIFY(probe.isDetached());
    QVERIFY(keyProbe.isDetached());
}

void PDFObjectTest::test_concurrent_last_reference()
{
    const int threadCount = getThreadCount();
    const int rounds = 500;

    for (int round = 0; round < rounds; ++round)
    {
        QByteArray probe = createProbe(round);
        std::vector<PDFObject> copies;

        {
            PDFObject object = createArray({ createStream({ }, probe), PDFObject::createName("NameStoredInTheHeap") });
            copies.assign(size_t(threadCount), object);
            QCOMPARE(object.getContentReferenceCount(), uint32_t(threadCount + 1));
        }

        QCOMPARE(copies.front().getContentReferenceCount(), uint32_t(threadCount));

        // All threads release their references at the same moment,
        // exactly one of them must destroy the content.
        runConcurrently(threadCount, [&copies](int threadIndex)
        {
            PDFObject object = std::move(copies[threadIndex]);
            PDFObject item = object.getArray()->getItem(0);
            object = PDFObject();
            item = PDFObject();
        });

        QVERIFY2(probe.isDetached(), qPrintable(QString("round %1").arg(round)));
    }
}

void PDFObjectTest::test_concurrent_subobjects()
{
    const int threadCount = getThreadCount();
    const int itemCount = 64;

    std::vector<QByteArray> probes;
    std::vector<PDFObject> items;
    for (int i = 0; i < itemCount; ++i)
    {
        probes.push_back(createProbe(i));
        items.push_back(createDictionary({ { "Index", PDFObject::createInteger(i) }, { "Data", createStream({ }, probes.back()) } }));
    }

    PDFObject root = PDFObject::createArray(std::move(items));
    std::vector<PDFObject> rootCopies(size_t(threadCount), root);
    root = PDFObject();

    std::atomic<int> errors = 0;
    std::latch rootsReleased(threadCount);

    // Each thread takes items from its own copy of the root, releases the root, waits
    // until all roots are released (so the items are owned only by the threads)
    // and then reads and releases the items.
    runConcurrently(threadCount, [&](int threadIndex)
    {
        std::vector<PDFObject> threadItems;
        const PDFArray* array = rootCopies[threadIndex].getArray();
        for (size_t i = size_t(threadIndex) % 2; i < array->getCount(); i += 2)
        {
            threadItems.push_back(array->getItem(i));
        }

        rootCopies[threadIndex] = PDFObject();
        rootsReleased.arrive_and_wait();

        for (const PDFObject& item : threadItems)
        {
            const PDFDictionary* dictionary = item.getDictionary();
            const PDFInteger index = dictionary->get("Index").getInteger();
            if (*dictionary->get("Data").getStream()->getContent() != probes[size_t(index)])
            {
                ++errors;
            }
        }
    });

    QCOMPARE(errors.load(), 0);
    for (const QByteArray& probe : probes)
    {
        QVERIFY(probe.isDetached());
    }
}

void PDFObjectTest::test_concurrent_dictionary_lookup()
{
    std::vector<QByteArray> keys;
    PDFDictionary dictionary;
    for (int i = 0; i < 200; ++i)
    {
        QByteArray key = "Key" + QByteArray::number(i);
        if (i % 3 == 0)
        {
            key += "WithLongSuffixStoredInTheHeap";
        }
        keys.push_back(key);
        dictionary.addEntry(PDFInplaceOrMemoryString(key), PDFObject::createInteger(i));
    }
    const PDFObject shared = PDFObject::createDictionary(std::move(dictionary));

    const int threadCount = getThreadCount();
    std::atomic<int> errors = 0;

    runConcurrently(threadCount, [&](int threadIndex)
    {
        std::mt19937 generator(threadIndex);
        std::uniform_int_distribution<int> distribution(0, int(keys.size()) - 1);

        for (int i = 0; i < 5000; ++i)
        {
            const int index = distribution(generator);
            const QByteArray& key = keys[size_t(index)];
            const PDFDictionary* sharedDictionary = shared.getDictionary();

            if (sharedDictionary->get(key).getInteger() != index ||
                sharedDictionary->get(key.constData()).getInteger() != index ||
                sharedDictionary->get(PDFInplaceOrMemoryString(key)).getInteger() != index ||
                !sharedDictionary->get(key + "X").isNull())
            {
                ++errors;
            }
        }
    });

    QCOMPARE(errors.load(), 0);
    QCOMPARE(shared.getContentReferenceCount(), uint32_t(1));
}

void PDFObjectTest::test_concurrent_keys()
{
    QByteArray probe = createProbe(7);
    std::vector<PDFInplaceOrMemoryString> keys;
    keys.push_back(PDFInplaceOrMemoryString(probe));
    keys.push_back(PDFInplaceOrMemoryString("Short"));
    keys.push_back(PDFInplaceOrMemoryString("KeyStoredInTheHeap"));

    const int threadCount = getThreadCount();
    std::atomic<int> errors = 0;

    runConcurrently(threadCount, [&](int threadIndex)
    {
        std::vector<PDFInplaceOrMemoryString> localKeys;
        for (int i = 0; i < 20000; ++i)
        {
            const PDFInplaceOrMemoryString& key = keys[size_t(i + threadIndex) % keys.size()];
            PDFInplaceOrMemoryString copy = key;
            PDFInplaceOrMemoryString moved = std::move(copy);
            copy = moved;

            if (!(copy == key) || !(moved == key) || copy.getView().size() != key.getView().size())
            {
                ++errors;
            }

            if (i % 5 == 0)
            {
                localKeys.push_back(copy);
            }
            if (localKeys.size() > 40)
            {
                localKeys.erase(localKeys.begin(), localKeys.begin() + 20);
            }
        }
    });

    QCOMPARE(errors.load(), 0);
    QCOMPARE(keys[0].getContentReferenceCount(), uint32_t(1));
    QCOMPARE(keys[2].getContentReferenceCount(), uint32_t(1));

    keys.clear();
    QVERIFY(probe.isDetached());
}

void PDFObjectTest::test_concurrent_parsing()
{
    // Document containing all kinds of objects, long names and keys (stored in
    // the heap), deeply nested arrays and dictionaries and streams, whose length
    // is an indirect object (so nested parser is used).
    QByteArray data = "[";
    for (int i = 0; i < 200; ++i)
    {
        data += "<< /Type /Page /Index " + QByteArray::number(i) +
                " /VeryLongKeyOfTheDictionary /VeryLongValueOfTheName"
                " /Real " + QByteArray::number(i) + ".5 /Bool true /Null null"
                " /String (A string, which is stored in the heap " + QByteArray::number(i) + ")"
                " /Short (abc) /Hex <48656C6C6F> /Esc#20aped /N#41me"
                " /Kids [1 0 R 2 0 R [3 [4 [5 [6 [7]]]]] << /A << /B << /C 1 >> >> >>]"
                " /Stream << /Length 5 0 R >>\nstream\n0123456789\nendstream"
                " >> ";
    }
    data += "]";

    auto parse = [&data]()
    {
        PDFParsingContext context([](PDFParsingContext*, PDFObjectReference reference)
        {
            // Nested parser, which uses scratch stacks of the thread too (parser
            // doesn't copy the data, so they must exist during the parsing)
            const QByteArray nestedData = QByteArray("[[1 2 [3]] << /A [4] >>] ") + QByteArray::number(reference.objectNumber * 2);
            PDFParser nestedParser(nestedData, nullptr, PDFParser::None);
            PDFObject nestedArray = nestedParser.getObject();
            PDFObject length = nestedParser.getObject();
            return nestedArray.getArray()->getCount() == 2 ? length : PDFObject::createInteger(0);
        });

        PDFParser parser(data, &context, PDFParser::AllowStreams);
        return parser.getObject();
    };

    const PDFObject expected = parse();
    QVERIFY(expected.isArray());
    QCOMPARE(expected.getArray()->getCount(), size_t(200));
    const PDFDictionary* first = expected.getArray()->getItem(0).getDictionary();
    QCOMPARE(first->get("VeryLongKeyOfTheDictionary").getString(), QByteArray("VeryLongValueOfTheName"));
    QCOMPARE(first->get("Esc aped").getString(), QByteArray("NAme"));
    QCOMPARE(first->get("Hex").getString(), QByteArray("Hello"));
    QCOMPARE(*first->get("Stream").getStream()->getContent(), QByteArray("0123456789"));

    const int threadCount = getThreadCount();
    std::atomic<int> errors = 0;
    std::mutex exchangeMutex;
    std::vector<PDFObject> exchange(static_cast<size_t>(threadCount));

    runConcurrently(threadCount, [&](int threadIndex)
    {
        for (int i = 0; i < 4; ++i)
        {
            PDFObject object;
            try
            {
                object = parse();
            }
            catch (const PDFException&)
            {
                ++errors;
                continue;
            }

            if (object != expected)
            {
                ++errors;
            }

            // Object parsed in this thread is destroyed in another thread
            std::scoped_lock lock(exchangeMutex);
            exchange[size_t(threadIndex + 1) % exchange.size()] = std::move(object);
        }

        // Parsing errors in threads (scratch stacks must be cleaned up)
        try
        {
            const QByteArray invalidData("[1 [2 << /A [3");
            PDFParser parser(invalidData, nullptr, PDFParser::None);
            parser.getObject();
            ++errors;
        }
        catch (const PDFException&)
        {
            // Expected
        }

        const QByteArray validData("[5 6]");
        PDFParser parser(validData, nullptr, PDFParser::None);
        if (parser.getObject().getArray()->getCount() != 2)
        {
            ++errors;
        }
    });

    QCOMPARE(errors.load(), 0);
    exchange.clear();
    QCOMPARE(expected.getContentReferenceCount(), uint32_t(1));
}

void PDFObjectTest::test_concurrent_random_objects()
{
    // Shared pool of objects, threads create new objects, which share
    // content with the pool, exchange them and destroy them in other threads
    std::vector<QByteArray> probes;
    std::vector<PDFObject> pool;
    for (int i = 0; i < 32; ++i)
    {
        probes.push_back(createProbe(i));
        switch (i % 4)
        {
            case 0:
                pool.push_back(PDFObject::createString(probes.back()));
                break;
            case 1:
                pool.push_back(createStream({ { "Index", PDFObject::createInteger(i) } }, probes.back()));
                break;
            case 2:
                pool.push_back(createDictionary({ { probes.back(), PDFObject::createInteger(i) } }));
                break;
            default:
                pool.push_back(createArray({ PDFObject::createInteger(i), PDFObject::createName(probes.back()) }));
                break;
        }
    }

    const int threadCount = getThreadCount();
    std::atomic<int> errors = 0;
    std::vector<std::mutex> mutexes(static_cast<size_t>(threadCount));
    std::vector<std::vector<PDFObject>> mailboxes(static_cast<size_t>(threadCount));

    runConcurrently(threadCount, [&](int threadIndex)
    {
        std::mt19937 generator(1000 + threadIndex);
        std::uniform_int_distribution<size_t> poolDistribution(0, pool.size() - 1);
        std::uniform_int_distribution<int> kindDistribution(0, 3);

        for (int i = 0; i < 2000; ++i)
        {
            // Create a new object from randomly chosen objects of the pool
            std::vector<PDFObject> items;
            PDFDictionary dictionary;
            const size_t itemCount = 1 + size_t(i % 5);
            for (size_t j = 0; j < itemCount; ++j)
            {
                const PDFObject& item = pool[poolDistribution(generator)];
                items.push_back(item);
                dictionary.addEntry(PDFInplaceOrMemoryString("Key" + QByteArray::number(qulonglong(j)) + "StoredInTheHeap"), PDFObject(item));
            }

            PDFObject object;
            switch (kindDistribution(generator))
            {
                case 0:
                    object = PDFObject::createArray(std::vector<PDFObject>(items));
                    break;
                case 1:
                    object = PDFObject::createDictionary(std::move(dictionary));
                    break;
                case 2:
                    object = PDFObject::createStream(PDFStream(std::move(dictionary), QByteArray("data")));
                    break;
                default:
                    object = createArray({ PDFObject::createArray(std::vector<PDFObject>(items)), PDFObject::createInteger(i) });
                    break;
            }

            // Structural comparison with the copy
            PDFObject copy = object;
            if (copy != object)
            {
                ++errors;
            }

            // Send the object to another thread, destroy objects received from others
            const size_t target = size_t(threadIndex + 1 + i % 3) % mailboxes.size();
            {
                std::scoped_lock lock(mutexes[target]);
                mailboxes[target].push_back(std::move(object));
            }

            std::vector<PDFObject> received;
            {
                std::scoped_lock lock(mutexes[size_t(threadIndex)]);
                received.swap(mailboxes[size_t(threadIndex)]);
            }
            for (const PDFObject& receivedObject : received)
            {
                if (receivedObject.isNull() || receivedObject.getContentReferenceCount() == 0)
                {
                    ++errors;
                }
            }
        }
    });

    QCOMPARE(errors.load(), 0);
    mailboxes.clear();

    for (const PDFObject& object : pool)
    {
        QCOMPARE(object.getContentReferenceCount(), uint32_t(1));
    }

    pool.clear();
    for (const QByteArray& probe : probes)
    {
        QVERIFY(probe.isDetached());
    }
}

QTEST_APPLESS_MAIN(PDFObjectTest)

#include "tst_pdfobjecttest.moc"
