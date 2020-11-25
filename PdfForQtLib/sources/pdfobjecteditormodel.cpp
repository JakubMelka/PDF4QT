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

#include "pdfobjecteditormodel.h"

namespace pdf
{

PDFObjectEditorAbstractModel::PDFObjectEditorAbstractModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFObjectEditorAbstractModel::~PDFObjectEditorAbstractModel()
{

}

size_t PDFObjectEditorAbstractModel::getAttributeCount() const
{
    return m_attributes.size();
}

const QString& PDFObjectEditorAbstractModel::getAttributeCategory(size_t index) const
{
    return m_attributes.at(index).category;
}

const QString& PDFObjectEditorAbstractModel::getAttributeSubcategory(size_t index) const
{
    return m_attributes.at(index).subcategory;
}

const QString& PDFObjectEditorAbstractModel::getAttributeName(size_t index) const
{
    return m_attributes.at(index).name;
}

size_t PDFObjectEditorAbstractModel::createAttribute(ObjectEditorAttributeType type,
                                                     QString attributeName,
                                                     QString category,
                                                     QString subcategory,
                                                     QString name,
                                                     PDFObject defaultValue,
                                                     uint32_t typeFlags,
                                                     PDFObjectEditorModelAttribute::Flags flags)
{
    size_t index = m_attributes.size();

    PDFObjectEditorModelAttribute attribute;
    attribute.type = type;
    attribute.dictionaryAttribute = QStringList(qMove(attributeName));
    attribute.category = qMove(category);
    attribute.subcategory = qMove(subcategory);
    attribute.name = qMove(name);
    attribute.defaultValue = qMove(defaultValue);
    attribute.typeFlags = typeFlags;
    attribute.attributeFlags = flags;
    m_attributes.emplace_back(qMove(attribute));

    return index;
}

PDFObjectEditorAnnotationsModel::PDFObjectEditorAnnotationsModel(QObject* parent) :
    BaseClass(parent)
{
    createAttribute(ObjectEditorAttributeType::Constant, "Type", tr("General"), tr("General"), tr("Type"), PDFObject::createName("Annot"), 0, PDFObjectEditorModelAttribute::Hidden);
    createAttribute(ObjectEditorAttributeType::Type, "Subtype", tr("General"), tr("General"), tr("Type"));

    PDFObjectEditorModelAttributeEnumItems typeEnumItems;
    typeEnumItems.emplace_back(tr("Text"), Text, PDFObject::createName("Text"));
    typeEnumItems.emplace_back(tr("Link"), Link, PDFObject::createName("Link"));
    typeEnumItems.emplace_back(tr("Free text"), FreeText, PDFObject::createName("FreeText"));
    typeEnumItems.emplace_back(tr("Line"), Line, PDFObject::createName("Line"));
    typeEnumItems.emplace_back(tr("Square"), Square, PDFObject::createName("Square"));
    typeEnumItems.emplace_back(tr("Circle"), Circle, PDFObject::createName("Circle"));
    typeEnumItems.emplace_back(tr("Polygon"), Polygon, PDFObject::createName("Polygon"));
    typeEnumItems.emplace_back(tr("Polyline"), PolyLine, PDFObject::createName("PolyLine"));
    typeEnumItems.emplace_back(tr("Highlight"), Highlight, PDFObject::createName("Highlight"));
    typeEnumItems.emplace_back(tr("Underline"), Underline, PDFObject::createName("Underline"));
    typeEnumItems.emplace_back(tr("Squiggly"), Squiggly, PDFObject::createName("Squiggly"));
    typeEnumItems.emplace_back(tr("Strike Out"), StrikeOut, PDFObject::createName("StrikeOut"));
    typeEnumItems.emplace_back(tr("Caret"), Caret, PDFObject::createName("Caret"));
    typeEnumItems.emplace_back(tr("Stamp"), Stamp, PDFObject::createName("Stamp"));
    typeEnumItems.emplace_back(tr("Ink"), Ink, PDFObject::createName("Ink"));
    typeEnumItems.emplace_back(tr("File attachment"), FileAttachment, PDFObject::createName("FileAttachment"));
    typeEnumItems.emplace_back(tr("Redaction"), Redact, PDFObject::createName("Redact"));
    m_attributes.back().enumItems = qMove(typeEnumItems);

    createAttribute(ObjectEditorAttributeType::Rectangle, "Rect", tr("General"), tr("General"), tr("Rectangle"), PDFObject());
    createAttribute(ObjectEditorAttributeType::TextLine, "NM", tr("Contents"), tr("Contents"), tr("Name"));
    createAttribute(ObjectEditorAttributeType::TextBrowser, "Contents", tr("Contents"), tr("Contents"), tr("Contents"));
    createAttribute(ObjectEditorAttributeType::DateTime, "M", tr("General"), tr("Info"), tr("Modified"), PDFObject(), 0, PDFObjectEditorModelAttribute::Readonly);
}

} // namespace pdf
