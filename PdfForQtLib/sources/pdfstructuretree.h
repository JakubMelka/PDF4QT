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
#include "pdffile.h"

namespace pdf
{

class PDFObjectStorage;
struct PDFStructureTreeAttributeDefinition;

class  PDFFORQTLIBSHARED_EXPORT PDFStructureTreeAttribute
{
public:

    enum class Owner
    {
        Invalid,

        /// Defined for user owner
        UserProperties,

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

    explicit PDFStructureTreeAttribute();
    explicit PDFStructureTreeAttribute(const PDFStructureTreeAttributeDefinition* definition,
                                       Owner owner,
                                       PDFInteger revision,
                                       PDFObjectReference namespaceReference,
                                       PDFObject value);

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

    /// Sets attribute revision number
    void setRevision(PDFInteger revision) { m_revision = revision; }

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

    /// Parses attributes and adds them into \p attributes array. Invalid
    /// attributes are not added. New attributes are appended to the end
    /// of the array.
    /// \param storage Storage
    /// \param object Container of attributes
    /// \param attributes[in,out] Attributes
    static void parseAttributes(const PDFObjectStorage* storage, PDFObject object, std::vector<PDFStructureTreeAttribute>& attributes);

private:
    /// Parses single attribute dictionary and appends new attributes to the end of the list.
    /// \param storage Storage
    /// \param object Container of attributes
    /// \param attributes[in,out] Attributes
    static void parseAttributeDictionary(const PDFObjectStorage* storage, PDFObject object, std::vector<PDFStructureTreeAttribute>& attributes);

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

    enum Type
    {
        Invalid,

        // Document level types - chapter 14.8.4.3 of PDF 2.0 specification
        Document, DocumentFragment,

        // Grouping types - chapter 14.8.4.4 of PDF 2.0 specification
        Part, Div, Aside,

        // Block level structure types - chapter 14.8.4.5 of PDF 2.0 specification
        P, H1, H2, H3, H4, H5, H6, H7, H, Title, FENote,

        // Subblock level structure types - chapter 14.8.4.6 of PDF 2.0 specification
        Sub,

        // Inline structure types - chapter 14.8.4.7 of PDF 2.0 specification
        Lbl, Span, Em, Strong, Link, Annot, Form, Ruby, RB, RT, RP, Warichu, WR, WP,

        // Other structure types - chapter 14.8.4.7 of PDF 2.0 specification
        L, LI, LBody, Table, TR, TH, TD, THead, TBody, TFoot, Caption, Figure, Formula, Artifact,

        // PDF 1.7 backward compatibility types
        Sect, Art, BlockQuote, TOC, TOCI, Index, NonStruct, Private, Quote, Note, Reference, BibEntry, Code,

        // Last type identifier
        LastType,
    };

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

    /// Get structure tree type from name
    /// \param name Name
    static Type getTypeFromName(const QByteArray& name);

protected:
    PDFStructureItem* m_parent;
    PDFStructureTree* m_root;
    std::vector<PDFStructureItemPointer> m_children;
};

/// Structure tree namespace
class PDFStructureTreeNamespace
{
public:
    explicit inline PDFStructureTreeNamespace() = default;

    const PDFObjectReference& getSelfReference() const { return m_selfReference; }
    const QString& getNamespace() const { return m_namespace; }
    const PDFFileSpecification& getSchema() const { return m_schema; }
    const PDFObject& getRoleMapNS() const { return m_roleMapNS; }

    static PDFStructureTreeNamespace parse(const PDFObjectStorage* storage, PDFObject object);

private:
    PDFObjectReference m_selfReference;
    QString m_namespace;
    PDFFileSpecification m_schema;
    PDFObject m_roleMapNS;
};

using PDFStructureTreeNamespaces = std::vector<PDFStructureTreeNamespace>;

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

    /// Returns type from role. Role can be an entry in RoleMap dictionary,
    /// or one of the standard roles.
    /// \param role Role
    Type getTypeFromRole(const QByteArray& role) const;

    /// Returns class attributes for given class. If class is not found,
    /// then empty attributes are returned.
    /// \param className Class name
    const std::vector<PDFStructureTreeAttribute>& getClassAttributes(const QByteArray& className) const;

    /// Returns a list of namespaces
    const PDFStructureTreeNamespaces& getNamespaces() const { return m_namespaces; }

    /// Returns a list of pronunciation lexicons
    const std::vector<PDFFileSpecification>& getPronunciationLexicons() const { return m_pronunciationLexicons; }

    /// Returns a list of associated files
    const std::vector<PDFFileSpecification>& getAssociatedFiles() const { return m_associatedFiles; }

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
    PDFInteger m_parentNextKey = 0;
    std::map<QByteArray, Type> m_roleMap;
    std::map<QByteArray, std::vector<PDFStructureTreeAttribute>> m_classMap;
    PDFStructureTreeNamespaces m_namespaces;
    std::vector<PDFFileSpecification> m_pronunciationLexicons;
    std::vector<PDFFileSpecification> m_associatedFiles;
};

}   // namespace pdf

#endif // PDFSTRUCTURETREE_H
