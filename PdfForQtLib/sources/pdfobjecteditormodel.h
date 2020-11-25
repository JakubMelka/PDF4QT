//    Copyright (C) 2020 Jakub Melka
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
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFOBJECTEDITORABSTRACTMODEL_H
#define PDFOBJECTEDITORABSTRACTMODEL_H

#include "pdfobject.h"

#include <QStringList>

namespace pdf
{

enum ObjectEditorAttributeType
{
    Constant,       ///< Constant attribute, which is always written to the object
    Type,           ///< Type attribute, switches between main types of object
    TextLine,       ///< Single line text
    TextBrowser,    ///< Multiple line text
    Rectangle,      ///< Rectangle defined by x,y,width,height
    DateTime,       ///< Date/time
    Invalid
};

struct PDFObjectEditorModelAttributeEnumItem
{
    QString name;
    uint32_t flags = 0;
    PDFObject value;
};

using PDFObjectEditorModelAttributeEnumItems = std::vector<PDFObjectEditorModelAttributeEnumItem>;

struct PDFObjectEditorModelAttribute
{
    enum Flag
    {
        None                    = 0x0000,
        Readonly                = 0x0001,   ///< Attribute is always read only
        HideInsteadOfDisable    = 0x0002,   ///< Hide all widgets of this attribute, if it is disabled
        Hidden                  = 0x0004,   ///< Attribute is always hidden (not viewable in gui)
    };
    Q_DECLARE_FLAGS(Flags, Flag)

    /// Attribute type
    ObjectEditorAttributeType type = ObjectEditorAttributeType::Invalid;

    /// Attribute name in object dictionary. In case of subdictionaries,
    /// there can be a path of constisting of dictionary names and last
    /// string in the string list is key in the dictionary. If this attribute
    /// is empty, then this attribute is not represented in final object.
    QStringList dictionaryAttribute;

    /// Category
    QString category;

    /// Subcategory
    QString subcategory;

    /// Name of the attribute, which is displayed in the gui.
    QString name;

    /// Default value
    PDFObject defaultValue;

    /// Type flags, this filters attributes by object type. If set to zero,
    /// then this attribute doesn't depend on object type.
    uint32_t typeFlags = 0;

    /// Attribute flags
    Flags attributeFlags = None;

    /// Enum items
    PDFObjectEditorModelAttributeEnumItems enumItems;
};

class PDFFORQTLIBSHARED_EXPORT PDFObjectEditorAbstractModel : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFObjectEditorAbstractModel(QObject* parent);
    virtual ~PDFObjectEditorAbstractModel();

    size_t getAttributeCount() const;
    const QString& getAttributeCategory(size_t index) const;
    const QString& getAttributeSubcategory(size_t index) const;
    const QString& getAttributeName(size_t index) const;

protected:
    size_t createAttribute(ObjectEditorAttributeType type,
                           QString attributeName,
                           QString category,
                           QString subcategory,
                           QString name,
                           PDFObject defaultValue = PDFObject(),
                           uint32_t typeFlags = 0,
                           PDFObjectEditorModelAttribute::Flags flags = PDFObjectEditorModelAttribute::None);

    std::vector<PDFObjectEditorModelAttribute> m_attributes;
};

class PDFFORQTLIBSHARED_EXPORT PDFObjectEditorAnnotationsModel : public PDFObjectEditorAbstractModel
{
    Q_OBJECT

private:
    using BaseClass = PDFObjectEditorAbstractModel;

    enum AnnotationTypes : uint32_t
    {
        Text            = 1 << 0,
        Link            = 1 << 1,
        FreeText        = 1 << 2,
        Line            = 1 << 3,
        Square          = 1 << 4,
        Circle          = 1 << 5,
        Polygon         = 1 << 6,
        PolyLine        = 1 << 7,
        Highlight       = 1 << 8,
        Underline       = 1 << 9,
        Squiggly        = 1 << 10,
        StrikeOut       = 1 << 11,
        Caret           = 1 << 12,
        Stamp           = 1 << 13,
        Ink             = 1 << 14,
        FileAttachment  = 1 << 15,
        Redact          = 1 << 16,

        Markup = Text | FreeText | Line | Square | Circle | Polygon | PolyLine | Highlight | Underline | Squiggly | StrikeOut | Caret | Stamp | Ink | FileAttachment | Redact
    };

public:
    explicit PDFObjectEditorAnnotationsModel(QObject* parent);
};

} // namespace pdf

#endif // PDFOBJECTEDITORABSTRACTMODEL_H
