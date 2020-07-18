//    Copyright (C) 2019-2020 Jakub Melka
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

#include "pdfstructuretree.h"
#include "pdfdocument.h"
#include "pdfnametreeloader.h"
#include "pdfnumbertreeloader.h"

#include <array>

namespace pdf
{

/// Attribute definition structure
struct PDFStructureTreeAttributeDefinition
{
    constexpr inline PDFStructureTreeAttributeDefinition() = default;
    constexpr inline PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute type,
                                                         const char* name,
                                                         bool inheritable) :
        type(type),
        name(name),
        inheritable(inheritable)
    {

    }

    /// Returns attribute definition for given attribute name. This function
    /// always returns valid pointer. For uknown attribute, it returns
    /// user attribute definition.
    /// \param name Attribute name
    const PDFStructureTreeAttributeDefinition* getDefinition(const QByteArray& name);

    PDFStructureTreeAttribute::Owner getOwnerFromString(const QByteArray& string);

    PDFStructureTreeAttribute::Attribute type = PDFStructureTreeAttribute::Attribute::User;
    const char* name = nullptr;
    bool inheritable = false;
};


static constexpr std::array<std::pair<const char*, const PDFStructureTreeAttribute::Owner>, 16> s_ownerDefinitions =
{
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("Layout", PDFStructureTreeAttribute::Owner::Layout),
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("List", PDFStructureTreeAttribute::Owner::List),
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("PrintField", PDFStructureTreeAttribute::Owner::PrintField),
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("Table", PDFStructureTreeAttribute::Owner::Table),
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("Artifact", PDFStructureTreeAttribute::Owner::Artifact),
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("XML-1.00", PDFStructureTreeAttribute::Owner::XML_1_00),
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("HTML-3.20", PDFStructureTreeAttribute::Owner::HTML_3_20),
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("HTML-4.01", PDFStructureTreeAttribute::Owner::HTML_4_01),
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("HTML-5.00", PDFStructureTreeAttribute::Owner::HTML_5_00),
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("OEB-1.00", PDFStructureTreeAttribute::Owner::OEB_1_00),
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("RTF-1.05", PDFStructureTreeAttribute::Owner::RTF_1_05),
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("CSS-1.00", PDFStructureTreeAttribute::Owner::CSS_1_00),
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("CSS-2.00", PDFStructureTreeAttribute::Owner::CSS_2_00),
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("CSS-3.00", PDFStructureTreeAttribute::Owner::CSS_3_00),
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("RDFa-1.10", PDFStructureTreeAttribute::Owner::RDFa_1_10),
    std::pair<const char*, const PDFStructureTreeAttribute::Owner>("ARIA-1.1", PDFStructureTreeAttribute::Owner::ARIA_1_1)
};

static constexpr std::array<const PDFStructureTreeAttributeDefinition, PDFStructureTreeAttribute::Attribute::LastAttribute + 1> s_attributeDefinitions =
{
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::User, "", false), // User

    // Standard layout attributes
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::Placement, "Placement", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::WritingMode, "WritingMode", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::BackgroundColor, "BackgroundColor", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::BorderColor, "BorderColor", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::BorderStyle, "BorderStyle", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::BorderThickness, "BorderThickness", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::Color, "Color", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::Padding, "Padding", false),

    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::SpaceBefore, "SpaceBefore", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::SpaceAfter, "SpaceAfter", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::StartIndent, "StartIndent", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::EndIndent, "EndIndent", true),

    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::TextIndent, "TextIndent", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::TextAlign, "TextAlign", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::BBox, "BBox", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::Width, "Width", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::Height, "Height", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::BlockAlign, "BlockAlign", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::InlineAlign, "InlineAlign", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::TBorderStyle, "TBorderStyle", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::TPadding, "TPadding", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::LineHeight, "LineHeight", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::BaselineShift, "BaselineShift", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::TextDecorationType, "TextDecorationType", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::TextDecorationColor, "TextDecorationColor", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::TextDecorationThickness, "TextDecorationThickness", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::ColumnCount, "ColumnCount", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::ColumnWidths, "ColumnWidths", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::ColumnGap, "ColumnGap", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::GlyphOrientationVertical, "GlyphOrientationVertical", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::RubyAlign, "RubyAlign", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::RubyPosition, "RubyPosition", true),

    // List attributes
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::ListNumbering, "ListNumbering", true),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::ContinuedList, "ContinuedList", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::ContinuedFrom, "ContinuedFrom", false),

    // PrintField attributes
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::Role, "Role", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::Checked, "Checked", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::Checked, "checked", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::Desc, "Desc", false),

    // Table attributes
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::RowSpan, "RowSpan", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::ColSpan, "ColSpan", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::Headers, "Headers", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::Scope, "Scope", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::Short, "Short", false),

    // Artifact attributes
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::Type, "Type", false),
    PDFStructureTreeAttributeDefinition(PDFStructureTreeAttribute::Attribute::Subtype, "Subtype", false)
};

const PDFStructureTreeAttributeDefinition* PDFStructureTreeAttributeDefinition::getDefinition(const QByteArray& name)
{
    for (const PDFStructureTreeAttributeDefinition& definition : s_attributeDefinitions)
    {
        if (name == definition.name)
        {
            return &definition;
        }
    }

    Q_ASSERT(s_attributeDefinitions.front().type == PDFStructureTreeAttribute::Attribute::User);
    return &s_attributeDefinitions.front();
}

PDFStructureTreeAttribute::Owner PDFStructureTreeAttributeDefinition::getOwnerFromString(const QByteArray& string)
{
    for (const auto& item : s_ownerDefinitions)
    {
        if (string == item.first)
        {
            return item.second;
        }
    }

    return PDFStructureTreeAttribute::Owner::User;
}

PDFStructureTreeAttribute::PDFStructureTreeAttribute() :
    m_definition(&s_attributeDefinitions.front()),
    m_owner(Owner::Invalid),
    m_revision(0),
    m_namespace(),
    m_value()
{

}

PDFStructureTreeAttribute::Attribute PDFStructureTreeAttribute::getType() const
{
    Q_ASSERT(m_definition);
    return m_definition->type;
}

bool PDFStructureTreeAttribute::isInheritable() const
{
    Q_ASSERT(m_definition);
    return m_definition->inheritable;
}

QString PDFStructureTreeAttribute::getUserPropertyName(const PDFObjectStorage* storage) const
{
    if (const PDFDictionary* value = storage->getDictionaryFromObject(m_value))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        return loader.readTextStringFromDictionary(value, "N", QString());
    }

    return QString();
}

PDFObject PDFStructureTreeAttribute::getUserPropertyValue(const PDFObjectStorage* storage) const
{
    if (const PDFDictionary* value = storage->getDictionaryFromObject(m_value))
    {
        return value->get("V");
    }

    return PDFObject();
}

QString PDFStructureTreeAttribute::getUserPropertyFormattedValue(const PDFObjectStorage* storage) const
{
    if (const PDFDictionary* value = storage->getDictionaryFromObject(m_value))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        return loader.readTextStringFromDictionary(value, "F", QString());
    }

    return QString();
}

bool PDFStructureTreeAttribute::getUserPropertyIsHidden(const PDFObjectStorage* storage) const
{
    if (const PDFDictionary* value = storage->getDictionaryFromObject(m_value))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        return loader.readBooleanFromDictionary(value, "H", false);
    }

    return false;
}

std::vector<PDFObjectReference> PDFStructureTree::getParents(PDFInteger id) const
{
    std::vector<PDFObjectReference> result;
    ParentTreeEntry entry{ id, PDFObjectReference() };

    Q_ASSERT(std::is_sorted(m_parentTreeEntries.cbegin(), m_parentTreeEntries.cend()));
    auto iterators = std::equal_range(m_parentTreeEntries.cbegin(), m_parentTreeEntries.cend(), entry);
    result.reserve(std::distance(iterators.first, iterators.second));
    std::transform(iterators.first, iterators.second, std::back_inserter(result), [](const auto& item) { return item.reference; });
    return result;
}

PDFStructureTree PDFStructureTree::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFStructureTree tree;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFMarkedObjectsContext context;
        PDFObject kids = dictionary->get("K");

        if (kids.isArray())
        {
            const PDFArray* kidsArray = kids.getArray();
            for (const PDFObject& object : *kidsArray)
            {
                PDFStructureItemPointer item = PDFStructureItem::parse(storage, object, &context);
                if (item)
                {
                    tree.m_children.emplace_back(qMove(item));
                }
            }
        }
        else
        {
            PDFStructureItemPointer item = PDFStructureItem::parse(storage, object, &context);
            if (item)
            {
                tree.m_children.emplace_back(qMove(item));
            }
        }

        if (dictionary->hasKey("IDTree"))
        {
            tree.m_idTreeMap = PDFNameTreeLoader<PDFObjectReference>::parse(storage, dictionary->get("IDTree"), [](const PDFObjectStorage*, const PDFObject& object) { return object.isReference() ? object.getReference() : PDFObjectReference(); });
        }

        if (dictionary->hasKey("ParentTree"))
        {
            struct ParentTreeParseEntry
            {
                PDFInteger id = 0;
                std::vector<PDFObjectReference> references;

                bool operator<(const ParentTreeParseEntry& other) const
                {
                    return id < other.id;
                }

                static ParentTreeParseEntry parse(PDFInteger id, const PDFObjectStorage*, const PDFObject& object)
                {
                    if (object.isReference())
                    {
                        return ParentTreeParseEntry{ id, { object.getReference() } };
                    }
                    else if (object.isArray())
                    {
                        std::vector<PDFObjectReference> references;
                        for (const PDFObject& object : *object.getArray())
                        {
                            if (object.isReference())
                            {
                                references.emplace_back(object.getReference());
                            }
                        }

                        return ParentTreeParseEntry{ id, qMove(references) };
                    }

                    return ParentTreeParseEntry{ id, { } };
                }
            };
            auto entries = PDFNumberTreeLoader<ParentTreeParseEntry>::parse(storage, dictionary->get("ParentTree"));
            for (const auto& entry : entries)
            {
                for (const PDFObjectReference& reference : entry.references)
                {
                    tree.m_parentTreeEntries.emplace_back(ParentTreeEntry{entry.id, reference});
                }
            }
            std::stable_sort(tree.m_parentTreeEntries.begin(), tree.m_parentTreeEntries.end());
        }
    }

    return tree;
}

}   // namespace pdf
