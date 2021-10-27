//    Copyright (C) 2021 Jakub Melka
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

#ifndef PDFXFAENGINE_H
#define PDFXFAENGINE_H

#include "pdfglobal.h"

#include <optional>
#include <memory>

namespace pdf
{

namespace xfa
{
struct XFA_InplaceTag;
struct XFA_SharedMemoryTag;

template<typename Value, struct Tag>
class PDFXFAValueHolder
{
public:
    constexpr inline bool hasValue() const { return false; }
    constexpr const Value* getValue() const { return nullptr; }
};

template<typename Value>
class PDFXFAValueHolder<Value, XFA_InplaceTag>
{
public:
    inline constexpr PDFXFAValueHolder(std::optional<Value> value) :
        m_value(std::move(value))
    {

    }

    constexpr inline bool hasValue() const { return m_value.has_value(); }
    constexpr const Value* getValue() const { return m_value.has_value() ? &m_value.value() : nullptr; }

private:
    std::optional<Value> m_value;
};

template<typename Value>
class PDFXFAValueHolder<Value, XFA_SharedMemoryTag>
{
public:
    inline constexpr PDFXFAValueHolder(std::optional<Value> value) :
        m_value()
    {
        if (value)
        {
            m_value = std::make_shared(std::move(*value));
        }
    }

    constexpr inline bool hasValue() const { return m_value; }
    constexpr const Value* getValue() const { return m_value.get(); }

private:
    std::shared_ptr<Value> m_value;
};

template<typename Value>
using XFA_Attribute = PDFXFAValueHolder<Value, XFA_InplaceTag>;

template<typename Value>
using XFA_Node = PDFXFAValueHolder<Value, typename Value::StorageTag>;

class XFA_Measurement
{
public:
    enum Type
    {
        in,
        cm,
        mm,
        pt,
        em,
        percent
    };

    constexpr inline XFA_Measurement() :
        m_value(0.0),
        m_type(in)
    {

    }

    constexpr inline XFA_Measurement(PDFReal value, Type type) :
        m_value(value),
        m_type(type)
    {

    }

    constexpr inline XFA_Measurement(PDFReal value) :
        m_value(value),
        m_type(in)
    {

    }

    constexpr PDFReal getValue() const { return m_value; }
    constexpr Type getType() const { return m_type; }

private:
    PDFReal m_value;
    Type m_type;
};

class XFA_AbstractNode
{
public:
    constexpr inline XFA_AbstractNode() = default;
    virtual ~XFA_AbstractNode();
};

}   // namespace xfa

class PDFXFAEngine
{
public:
    PDFXFAEngine();

private:
};

/* START GENERATED CODE */

/* END GENERATED CODE */

}   // namespace pdf

#endif // PDFXFAENGINE_H
