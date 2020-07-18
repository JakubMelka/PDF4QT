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

#ifndef PDFSTRUCTURETREE_H
#define PDFSTRUCTURETREE_H

#include "pdfobject.h"
#include "pdfobjectutils.h"

namespace pdf
{

class PDFObjectStorage;
struct PDFStructureTreeAttributeDefinition;

class  PDFFORQTLIBSHARED_EXPORT PDFStructureTreeAttribute
{
public:
    explicit PDFStructureTreeAttribute();

    enum class Owner
    {
        Invalid,

        /// Defined for user owner
        User,

        /// Defined for NSO (namespace owner)
        NSO,

        Layout,
        List,
        PrintField,
        Table,

        Artifact,

        XML_1_00,
        HTML_3_20,
        HTML_4_01,
        HTML_5_00,
        OEB_1_00,
        RTF_1_05,
        CSS_1_00,
        CSS_2_00,
        CSS_3_00,
        RDFa_1_10,
        ARIA_1_1,
    };

    enum Attribute
    {
        User,

        // Standard layout attributes
        Placement,
        WritingMode,
        BackgroundColor,
        BorderColor,
        BorderStyle,
        BorderThickness,
        Color,
        Padding,

        // Block element attributes
        SpaceBefore,
        SpaceAfter,
        StartIndent,
        EndIndent,
        TextIndent,
        TextAlign,
        BBox,
        Width,
        Height,
        BlockAlign,
        InlineAlign,
        TBorderStyle,
        TPadding,
        LineHeight,
        BaselineShift,
        TextDecorationType,
        TextDecorationColor,
        TextDecorationThickness,
        ColumnCount,
        ColumnWidths,
        ColumnGap,
        GlyphOrientationVertical,
        RubyAlign,
        RubyPosition,

        // List attributes
        ListNumbering,
        ContinuedList,
        ContinuedFrom,

        // PrintField attributes
        Role,
        Checked,
        Desc,

        // Table attributes
        RowSpan,
        ColSpan,
        Headers,
        Scope,
        Short,

        // Artifact attributes
        Type,
        Subtype,

        LastAttribute
    };

    /// Returns attribute type
    Attribute getType() const;

    /// Returns attribute owner
    Owner getOwner() const { return m_owner; }

    /// Returns true, if attribute is inheritable
    bool isInheritable() const;

    /// Returns attribute revision number
    PDFInteger getRevision() const { return m_revision; }

    /// Returns namespace for this attribute (or empty reference, if it doesn't exists)
    PDFObjectReference getNamespace() const { return m_namespace; }

    /// Returns attribute value
    const PDFObject& getValue() const { return m_value; }

    /// Returns default attribute value
    PDFObject getDefaultValue() const;

    /// Returns user property name. This function should be called only for
    /// user properties. If error occurs, then empty string is returned.
    /// \param storage Storage (for resolving of indirect objects)
    QString getUserPropertyName(const PDFObjectStorage* storage) const;

    /// Returns user property value. This function should be called only for
    /// user properties. If error occurs, then empty string is returned.
    /// \param storage Storage (for resolving of indirect objects)
    PDFObject getUserPropertyValue(const PDFObjectStorage* storage) const;

    /// Returns user property formatted value. This function should be called only for
    /// user properties. If error occurs, then empty string is returned.
    /// \param storage Storage (for resolving of indirect objects)
    QString getUserPropertyFormattedValue(const PDFObjectStorage* storage) const;

    /// Returns true, if user property is hidden. This function should be called only for
    /// user properties. If error occurs, then empty string is returned.
    /// \param storage Storage (for resolving of indirect objects)
    bool getUserPropertyIsHidden(const PDFObjectStorage* storage) const;

private:
    const PDFStructureTreeAttributeDefinition* m_definition = nullptr;

    /// Attribute owner
    Owner m_owner = Owner::Invalid;

    /// Revision number
    PDFInteger m_revision = 0;

    /// Namespace
    PDFObjectReference m_namespace;

    /// Value of attribute. In case of standard attribute, attribute
    /// value is directly stored here. In case of user attribute,
    /// then user attribute dictionary is stored here.
    PDFObject m_value;
};

class PDFStructureTree;
class PDFStructureItem;

using PDFStructureItemPointer = QSharedPointer<PDFStructureItem>;

/// Root class for all structure tree items
class PDFStructureItem
{
public:
    explicit inline PDFStructureItem(PDFStructureItem* parent, PDFStructureTree* root) :
        m_parent(parent),
        m_root(root)
    {

    }
    virtual ~PDFStructureItem() = default;

    virtual PDFStructureTree* asStructureTree() { return nullptr; }
    virtual const PDFStructureTree* asStructureTree() const { return nullptr; }

    const PDFStructureItem* getParent() const { return m_parent; }
    const PDFStructureTree* getTree() const { return m_root; }
    std::size_t getChildCount() const { return m_children.size(); }
    const PDFStructureItem* getChild(size_t i) const { return m_children.at(i).get(); }

    /// Parses structure tree item from the object. If error occurs,
    /// null pointer is returned.
    /// \param storage Storage
    /// \param object Structure tree item object
    /// \param context Parsing context
    static PDFStructureItemPointer parse(const PDFObjectStorage* storage, PDFObject object, PDFMarkedObjectsContext* context);

protected:
    PDFStructureItem* m_parent;
    PDFStructureTree* m_root;
    std::vector<PDFStructureItemPointer> m_children;
};

/// Structure tree, contains structure element hierarchy
class PDFStructureTree : public PDFStructureItem
{
public:
    explicit inline PDFStructureTree() : PDFStructureItem(nullptr, this) { }

    virtual PDFStructureTree* asStructureTree() override { return this; }
    virtual const PDFStructureTree* asStructureTree() const override { return this; }

    /// Returns parents from parent tree for given entry. If entry
    /// is not found, then empty vector is returned.
    /// \param id Id
    std::vector<PDFObjectReference> getParents(PDFInteger id) const;

    /// Parses structure tree from the object. If error occurs, empty structure
    /// tree is returned.
    /// \param storage Storage
    /// \param object Structure tree root object
    static PDFStructureTree parse(const PDFObjectStorage* storage, PDFObject object);

private:

    struct ParentTreeEntry
    {
        PDFInteger id = 0;
        PDFObjectReference reference;

        bool operator<(const ParentTreeEntry& other) const
        {
            return id < other.id;
        }
    };
    using ParentTreeEntries = std::vector<ParentTreeEntry>;

    std::map<QByteArray, PDFObjectReference> m_idTreeMap;
    ParentTreeEntries m_parentTreeEntries;
};

}   // namespace pdf

#endif // PDFSTRUCTURETREE_H
