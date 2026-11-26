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

// Edit Content, stage E0, package P01 (operations, resources, stream writer, ExtGState).
// Plan: GUI_overhaul/edit_tool/zadani_jadro.md 4.4 (R-1, R-2, R-6), 4.8.
// Standard: ISO 32000-2, 8.4.5 (graphics state parameter dictionaries, Table 57).

#include "pdfcontentextgstate.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "pdfdbgheap.h"

namespace pdf
{

/// The keys and the values of the ExtGState dictionaries of the editor
class PDFContentExtGStateWriterHelper
{
public:
    PDFContentExtGStateWriterHelper() = delete;

    /// BlackGeneration, UndercolorRemoval and Transfer have a second key (BG2, UCR2, TR2), which
    /// accepts a name too (Table 57: the plain keys BG and UCR are functions only, TR2 is "the
    /// same as TR except that the value may also be the name Default")
    static bool hasSecondKey(PDFContentStateProperty property)
    {
        switch (property)
        {
            case PDFContentStateProperty::BlackGeneration:
            case PDFContentStateProperty::UndercolorRemoval:
            case PDFContentStateProperty::Transfer:
                return true;

            default:
                return false;
        }
    }

    /// The entry of a property: its key and its value. The value is null, when the kind of
    /// the value does not fit the property or the value cannot be written.
    static std::pair<QByteArray, PDFObject> createEntry(PDFContentStateProperty property, const PDFContentStateValue& value)
    {
        QByteArray key = PDFContentExtGStateWriter::getKey(property);
        PDFObject object;

        switch (property)
        {
            case PDFContentStateProperty::AlphaStroking:
            case PDFContentStateProperty::AlphaFilling:
            case PDFContentStateProperty::Smoothness:
            {
                if (value.kind == PDFContentStateValue::Kind::Real && std::isfinite(value.real))
                {
                    object = PDFObject::createReal(value.real);
                }
                break;
            }

            case PDFContentStateProperty::TextKnockout:
            case PDFContentStateProperty::AlphaIsShape:
            case PDFContentStateProperty::StrokeAdjustment:
            case PDFContentStateProperty::OverprintStroking:
            case PDFContentStateProperty::OverprintFilling:
            {
                if (value.kind == PDFContentStateValue::Kind::Boolean)
                {
                    object = PDFObject::createBool(value.boolean);
                }
                break;
            }

            case PDFContentStateProperty::OverprintMode:
            {
                if (value.kind == PDFContentStateValue::Kind::Integer)
                {
                    object = PDFObject::createInteger(value.integer);
                }
                break;
            }

            case PDFContentStateProperty::BlackPointCompensation:
            {
                if (value.kind == PDFContentStateValue::Kind::Name && !value.name.isEmpty())
                {
                    object = PDFObject::createName(value.name);
                }
                break;
            }

            case PDFContentStateProperty::BlendMode:
            case PDFContentStateProperty::Halftone:
            case PDFContentStateProperty::BlackGeneration:
            case PDFContentStateProperty::UndercolorRemoval:
            case PDFContentStateProperty::Transfer:
            {
                // A name (a blend mode, Default, Identity), or an object of the document taken
                // by reference from the symbolic state of the source (a function, a halftone,
                // an array of blend modes)
                if (value.kind == PDFContentStateValue::Kind::Name && !value.name.isEmpty())
                {
                    object = PDFObject::createName(value.name);
                }
                else if (value.kind == PDFContentStateValue::Kind::Object)
                {
                    object = value.object;
                }

                if (object.isName() && hasSecondKey(property))
                {
                    key.append('2');
                }
                break;
            }

            case PDFContentStateProperty::HalftoneOrigin:
            {
                if (value.kind == PDFContentStateValue::Kind::Object)
                {
                    object = value.object;
                }
                break;
            }

            default:
                break;
        }

        return std::make_pair(std::move(key), std::move(object));
    }

    /// The keys and the values a dictionary sets: /Type sets nothing, a key written twice
    /// counts once (the first one, as PDFDictionary::get reads it), a null value is no entry
    static std::vector<std::pair<QByteArray, PDFObject>> getSetEntries(const PDFDictionary* dictionary)
    {
        std::vector<std::pair<QByteArray, PDFObject>> entries;
        entries.reserve(dictionary->getCount());

        for (const PDFDictionary::DictionaryEntry& entry : *dictionary)
        {
            QByteArray key = entry.first.getString();
            if (key == "Type" || entry.second.isNull())
            {
                continue;
            }

            entries.emplace_back(std::move(key), entry.second);
        }

        // Stable: of two entries of the same key the first one stays the first one
        std::stable_sort(entries.begin(), entries.end(), &PDFContentExtGStateWriterHelper::isKeyLess);
        entries.erase(std::unique(entries.begin(), entries.end(), &PDFContentExtGStateWriterHelper::isKeyEqual), entries.end());
        return entries;
    }

    static bool isKeyLess(const std::pair<QByteArray, PDFObject>& left, const std::pair<QByteArray, PDFObject>& right)
    {
        return left.first < right.first;
    }

    static bool isKeyEqual(const std::pair<QByteArray, PDFObject>& left, const std::pair<QByteArray, PDFObject>& right)
    {
        return left.first == right.first;
    }

    /// Appends a number of 8 bytes to a key
    static void appendKeyNumber(QByteArray* key, quint64 value)
    {
        for (int i = 0; i < 8; ++i)
        {
            key->append(static_cast<char>((value >> (8 * i)) & 0xFF));
        }
    }

    /// Appends bytes with their length to a key (the key stays unambiguous)
    static void appendKeyBytes(QByteArray* key, QByteArrayView bytes)
    {
        appendKeyNumber(key, quint64(bytes.size()));
        key->append(bytes);
    }

    /// Appends the entries of a dictionary in their order to a key; false for a value, which is
    /// equal to nothing
    static bool appendDictionaryKey(QByteArray* key, const PDFDictionary* dictionary)
    {
        appendKeyNumber(key, quint64(dictionary->getCount()));
        for (const PDFDictionary::DictionaryEntry& entry : *dictionary)
        {
            appendKeyBytes(key, entry.first.getView());
            if (!appendObjectKey(key, entry.second))
            {
                return false;
            }
        }
        return true;
    }

    /// Appends the key of an object (PDFContentExtGStateWriter::getObjectKey): its type, then its
    /// value; every part of a variable length has its length first. False for an object, which
    /// is equal to nothing (a NaN in it).
    static bool appendObjectKey(QByteArray* key, const PDFObject& object)
    {
        const PDFObject::Type type = object.getType();
        key->append(static_cast<char>(type));

        switch (type)
        {
            case PDFObject::Type::Null:
                return true;

            case PDFObject::Type::Bool:
                key->append(object.getBool() ? '1' : '0');
                return true;

            case PDFObject::Type::Int:
                appendKeyNumber(key, static_cast<quint64>(object.getInteger()));
                return true;

            case PDFObject::Type::Real:
            {
                PDFReal value = object.getReal();
                if (std::isnan(value))
                {
                    return false;
                }

                // 0 and -0 are equal (==): one key
                if (value == 0.0)
                {
                    value = 0.0;
                }

                quint64 bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                appendKeyNumber(key, bits);
                return true;
            }

            case PDFObject::Type::String:
            case PDFObject::Type::Name:
                appendKeyBytes(key, object.getString());
                return true;

            case PDFObject::Type::Array:
            {
                const PDFArray* array = object.getArray();
                appendKeyNumber(key, quint64(array->getCount()));
                for (size_t i = 0, count = array->getCount(); i < count; ++i)
                {
                    if (!appendObjectKey(key, array->getItem(i)))
                    {
                        return false;
                    }
                }
                return true;
            }

            case PDFObject::Type::Dictionary:
                return appendDictionaryKey(key, object.getDictionary());

            case PDFObject::Type::Stream:
            {
                const PDFStream* stream = object.getStream();
                if (!appendDictionaryKey(key, stream->getDictionary()))
                {
                    return false;
                }
                appendKeyBytes(key, *stream->getContent());
                return true;
            }

            case PDFObject::Type::Reference:
            {
                const PDFObjectReference reference = object.getReference();
                appendKeyNumber(key, static_cast<quint64>(reference.objectNumber));
                appendKeyNumber(key, static_cast<quint64>(reference.generation));
                return true;
            }

            default:
                break;
        }

        // A type, which PDFObject::operator== does not compare, is equal to nothing
        return false;
    }
};

bool PDFContentExtGStateWriter::isExtGStateProperty(PDFContentStateProperty property)
{
    switch (property)
    {
        case PDFContentStateProperty::TextKnockout:
        case PDFContentStateProperty::AlphaStroking:
        case PDFContentStateProperty::AlphaFilling:
        case PDFContentStateProperty::BlendMode:
        case PDFContentStateProperty::SoftMask:
        case PDFContentStateProperty::AlphaIsShape:
        case PDFContentStateProperty::StrokeAdjustment:
        case PDFContentStateProperty::OverprintStroking:
        case PDFContentStateProperty::OverprintFilling:
        case PDFContentStateProperty::OverprintMode:
        case PDFContentStateProperty::BlackGeneration:
        case PDFContentStateProperty::UndercolorRemoval:
        case PDFContentStateProperty::Transfer:
        case PDFContentStateProperty::Halftone:
        case PDFContentStateProperty::HalftoneOrigin:
        case PDFContentStateProperty::Smoothness:
        case PDFContentStateProperty::BlackPointCompensation:
            return true;

        default:
            // The properties with an operator of their own (w J j M d ri i, the colors, Tf and
            // the text state operators) are written by their operators
            return false;
    }
}

QByteArray PDFContentExtGStateWriter::getKey(PDFContentStateProperty property)
{
    // ISO 32000-2, 8.4.5, Table 57
    switch (property)
    {
        case PDFContentStateProperty::TextKnockout:
            return QByteArrayLiteral("TK");

        case PDFContentStateProperty::AlphaStroking:
            return QByteArrayLiteral("CA");

        case PDFContentStateProperty::AlphaFilling:
            return QByteArrayLiteral("ca");

        case PDFContentStateProperty::BlendMode:
            return QByteArrayLiteral("BM");

        case PDFContentStateProperty::SoftMask:
            return QByteArrayLiteral("SMask");

        case PDFContentStateProperty::AlphaIsShape:
            return QByteArrayLiteral("AIS");

        case PDFContentStateProperty::StrokeAdjustment:
            return QByteArrayLiteral("SA");

        case PDFContentStateProperty::OverprintStroking:
            return QByteArrayLiteral("OP");

        case PDFContentStateProperty::OverprintFilling:
            return QByteArrayLiteral("op");

        case PDFContentStateProperty::OverprintMode:
            return QByteArrayLiteral("OPM");

        case PDFContentStateProperty::BlackGeneration:
            return QByteArrayLiteral("BG");

        case PDFContentStateProperty::UndercolorRemoval:
            return QByteArrayLiteral("UCR");

        case PDFContentStateProperty::Transfer:
            return QByteArrayLiteral("TR");

        case PDFContentStateProperty::Halftone:
            return QByteArrayLiteral("HT");

        case PDFContentStateProperty::HalftoneOrigin:
            return QByteArrayLiteral("HTO");

        case PDFContentStateProperty::Smoothness:
            return QByteArrayLiteral("SM");

        case PDFContentStateProperty::BlackPointCompensation:
            return QByteArrayLiteral("UseBlackPtComp");

        default:
            return QByteArray();
    }
}

PDFObject PDFContentExtGStateWriter::createDictionary(const std::map<PDFContentStateProperty, PDFContentStateValue>& properties)
{
    // Table 57, the entry OP: "Specifying an OP entry sets both parameters unless there is also
    // an op entry in the same graphics state parameter dictionary". A dictionary with /OP alone
    // would change the overprint of filling too, which nobody asked for: the caller gives both
    // (the stream writer adds the current value of the other one from the state of the node).
    if (properties.find(PDFContentStateProperty::OverprintStroking) != properties.cend() &&
        properties.find(PDFContentStateProperty::OverprintFilling) == properties.cend())
    {
        return PDFObject();
    }

    PDFDictionaryBuilder dictionary;
    dictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName(QByteArrayLiteral("ExtGState")));

    for (const auto& [property, value] : properties)
    {
        if (!isExtGStateProperty(property) || property == PDFContentStateProperty::SoftMask)
        {
            // R-6: a soft mask is set only by a dictionary with the single key /SMask
            return PDFObject();
        }

        std::pair<QByteArray, PDFObject> entry = PDFContentExtGStateWriterHelper::createEntry(property, value);
        if (entry.second.isNull())
        {
            return PDFObject();
        }

        dictionary.addEntry(PDFInplaceOrMemoryString(std::move(entry.first)), std::move(entry.second));
    }

    return PDFObject::createDictionary(std::move(dictionary));
}

PDFObject PDFContentExtGStateWriter::createSoftMaskDictionary(const PDFObject& softMask)
{
    // ISO 32000-2, 11.6.5.1: the value is a soft-mask dictionary or the name None. The mask is
    // kept by reference: the dictionary of the document is never copied or changed (R-6).
    PDFDictionaryBuilder dictionary;
    dictionary.addEntry(PDFInplaceOrMemoryString("SMask"), softMask.isNull() ? PDFObject::createName(QByteArrayLiteral("None")) : softMask);
    return PDFObject::createDictionary(std::move(dictionary));
}

bool PDFContentExtGStateWriter::isEqual(const PDFObject& left, const PDFObject& right)
{
    if (!left.isDictionary() || !right.isDictionary())
    {
        return false;
    }

    // The order of the keys plays no role for gs
    return PDFContentExtGStateWriterHelper::getSetEntries(left.getDictionary()) == PDFContentExtGStateWriterHelper::getSetEntries(right.getDictionary());
}

QByteArray PDFContentExtGStateWriter::getEqualityKey(const PDFObject& dictionary)
{
    if (!dictionary.isDictionary())
    {
        return QByteArray();
    }

    // isEqual compares the vectors of getSetEntries: the same count, and each pair the same key
    // and an equal value (PDFObject::operator==). Each part of the key has its length or a fixed
    // size, so equal keys are equal vectors and back, as long as no value is equal to nothing.
    const std::vector<std::pair<QByteArray, PDFObject>> entries = PDFContentExtGStateWriterHelper::getSetEntries(dictionary.getDictionary());

    QByteArray key;
    PDFContentExtGStateWriterHelper::appendKeyNumber(&key, quint64(entries.size()));
    for (const auto& [entryKey, value] : entries)
    {
        PDFContentExtGStateWriterHelper::appendKeyBytes(&key, entryKey);
        if (!PDFContentExtGStateWriterHelper::appendObjectKey(&key, value))
        {
            return QByteArray();
        }
    }

    return key;
}

QByteArray PDFContentExtGStateWriter::getObjectKey(const PDFObject& object)
{
    QByteArray key;
    if (!PDFContentExtGStateWriterHelper::appendObjectKey(&key, object))
    {
        return QByteArray();
    }

    return key;
}

}   // namespace pdf
