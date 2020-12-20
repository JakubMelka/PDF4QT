//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt. If not, see <https://www.gnu.org/licenses/>.

#include "pdfobjecteditormodel.h"
#include "pdfdocumentbuilder.h"
#include "pdfblendfunction.h"

namespace pdf
{

PDFObjectEditorAbstractModel::PDFObjectEditorAbstractModel(QObject* parent) :
    BaseClass(parent),
    m_storage(nullptr),
    m_typeAttribute(0)
{

}

PDFObjectEditorAbstractModel::~PDFObjectEditorAbstractModel()
{

}

size_t PDFObjectEditorAbstractModel::getAttributeCount() const
{
    return m_attributes.size();
}

ObjectEditorAttributeType PDFObjectEditorAbstractModel::getAttributeType(size_t index) const
{
    return m_attributes.at(index).type;
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

const PDFObjectEditorModelAttributeEnumItems& PDFObjectEditorAbstractModel::getAttributeEnumItems(size_t index) const
{
    return m_attributes.at(index).enumItems;
}

bool PDFObjectEditorAbstractModel::queryAttribute(size_t index, Question question) const
{
    const PDFObjectEditorModelAttribute& attribute = m_attributes.at(index);

    switch (question)
    {
        case Question::IsMapped:
            return attribute.attributeFlags.testFlag(PDFObjectEditorModelAttribute::Hidden) || attribute.type == ObjectEditorAttributeType::Constant;

        case Question::HasAttribute:
        {
            // Check type flags
            if (attribute.typeFlags)
            {
                uint32_t currentTypeFlags = getCurrentTypeFlags();
                if (!(attribute.typeFlags & currentTypeFlags))
                {
                    return false;
                }
            }

            // Check selector
            if (attribute.selectorAttribute)
            {
                if (!getSelectorValue(attribute.selectorAttribute))
                {
                    return false;
                }
            }

            return true;
        }

        case Question::IsAttributeEditable:
            return queryAttribute(index, Question::HasAttribute) && !attribute.attributeFlags.testFlag(PDFObjectEditorModelAttribute::Readonly);

        case Question::IsSelector:
            return attribute.type == ObjectEditorAttributeType::Selector;

        case Question::IsPersisted:
            return !queryAttribute(index, Question::IsSelector) && !attribute.dictionaryAttribute.isEmpty();

        default:
            break;
    }

    return false;
}

bool PDFObjectEditorAbstractModel::getSelectorValue(size_t index) const
{
    return m_attributes.at(index).selectorAttributeValue;
}

void PDFObjectEditorAbstractModel::setSelectorValue(size_t index, bool value)
{
    m_attributes.at(index).selectorAttributeValue = value;
}

std::vector<size_t> PDFObjectEditorAbstractModel::getSelectorAttributes() const
{
    std::vector<size_t> result;
    result.reserve(8); // Estimate maximal number of selectors

    const size_t count = getAttributeCount();
    for (size_t i = 0; i < count; ++i)
    {
        if (queryAttribute(i, Question::IsSelector))
        {
            result.push_back(i);
        }
    }

    return result;
}

std::vector<size_t> PDFObjectEditorAbstractModel::getSelectorDependentAttributes(size_t selector) const
{
    std::vector<size_t> result;
    result.reserve(16); // Estimate maximal number of selector's attributes

    const size_t count = getAttributeCount();
    for (size_t i = 0; i < count; ++i)
    {
        if (m_attributes.at(i).selectorAttribute == selector)
        {
            result.push_back(i);
        }
    }

    return result;
}

PDFObject PDFObjectEditorAbstractModel::getValue(size_t index) const
{
    const QByteArrayList& dictionaryAttribute = m_attributes.at(index).dictionaryAttribute;
    if (dictionaryAttribute.isEmpty())
    {
        return PDFObject();
    }

    PDFDocumentDataLoaderDecorator loader(m_storage);

    if (const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(m_editedObject))
    {
        const int pathDepth = dictionaryAttribute.size() - 1;

        for (int i = 0; i < pathDepth; ++i)
        {
            dictionary = m_storage->getDictionaryFromObject(dictionary->get(dictionaryAttribute[i]));
            if (!dictionary)
            {
                return PDFObject();
            }
        }

        return dictionary->get(dictionaryAttribute.back());
    }

    return PDFObject();
}

PDFObject PDFObjectEditorAbstractModel::getDefaultValue(size_t index) const
{
    return m_attributes.at(index).defaultValue;
}

void PDFObjectEditorAbstractModel::setEditedObject(PDFObject object)
{
    if (m_editedObject != object)
    {
        m_editedObject = qMove(object);
        emit editedObjectChanged();
    }
}

PDFObject PDFObjectEditorAbstractModel::writeAttributeValueToObject(size_t attribute, PDFObject object, PDFObject value) const
{
    Q_ASSERT(queryAttribute(attribute, Question::IsPersisted));

    PDFObjectFactory factory;
    factory.beginDictionary();

    const QByteArrayList& dictionaryAttribute = m_attributes.at(attribute).dictionaryAttribute;
    const int pathDepth = dictionaryAttribute.size() - 1;
    for (int i = 0; i < pathDepth; ++i)
    {
        factory.beginDictionaryItem(dictionaryAttribute[i]);
        factory.beginDictionary();
    }

    factory.beginDictionaryItem(dictionaryAttribute.back());
    factory << qMove(value);
    factory.endDictionaryItem();

    for (int i = 0; i < pathDepth; ++i)
    {
        factory.endDictionary();
        factory.endDictionaryItem();
    }

    factory.endDictionaryItem();
    return PDFObjectManipulator::merge(qMove(object), factory.takeObject(), PDFObjectManipulator::RemoveNullObjects);
}

size_t PDFObjectEditorAbstractModel::createAttribute(ObjectEditorAttributeType type,
                                                     QByteArray attributeName,
                                                     QString category,
                                                     QString subcategory,
                                                     QString name,
                                                     PDFObject defaultValue,
                                                     uint32_t typeFlags,
                                                     PDFObjectEditorModelAttribute::Flags flags)
{
    size_t index = m_attributes.size();

    QByteArrayList attributes;
    attributes.push_back(attributeName);
    PDFObjectEditorModelAttribute attribute;
    attribute.type = type;
    attribute.dictionaryAttribute = attributes;
    attribute.category = qMove(category);
    attribute.subcategory = qMove(subcategory);
    attribute.name = qMove(name);
    attribute.defaultValue = qMove(defaultValue);
    attribute.typeFlags = typeFlags;
    attribute.attributeFlags = flags;
    m_attributes.emplace_back(qMove(attribute));

    if (type == ObjectEditorAttributeType::Type)
    {
        m_typeAttribute = index;
    }

    return index;
}

size_t PDFObjectEditorAbstractModel::createSelectorAttribute(QString category, QString subcategory, QString name)
{
    return createAttribute(ObjectEditorAttributeType::Selector, QByteArray(), qMove(category), qMove(subcategory), qMove(name));
}

uint32_t PDFObjectEditorAbstractModel::getCurrentTypeFlags() const
{
    PDFObject value = getValue(m_typeAttribute);

    for (const PDFObjectEditorModelAttributeEnumItem& item : m_attributes.at(m_typeAttribute).enumItems)
    {
        if (item.value == value)
        {
            return item.flags;
        }
    }

    return 0;
}

PDFObject PDFObjectEditorAbstractModel::getDefaultColor() const
{
    PDFObjectFactory factory;
    factory.beginArray();
    factory << std::initializer_list<PDFReal>{ 0.0, 0.0, 0.0 };
    factory.endArray();
    return factory.takeObject();
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
    createAttribute(ObjectEditorAttributeType::TextLine, "T", tr("Contents"), tr("Contents"), tr("Author"), PDFObject(), Markup);
    createAttribute(ObjectEditorAttributeType::TextLine, "Subj", tr("Contents"), tr("Contents"), tr("Subj"), PDFObject(), Markup);
    createAttribute(ObjectEditorAttributeType::TextBrowser, "Contents", tr("Contents"), tr("Contents"), tr("Contents"));
    createAttribute(ObjectEditorAttributeType::TextLine, "NM", tr("Contents"), tr("Contents"), tr("Annotation name"));
    createAttribute(ObjectEditorAttributeType::DateTime, "M", tr("General"), tr("Info"), tr("Modified"), PDFObject(), 0, PDFObjectEditorModelAttribute::Readonly);
    createAttribute(ObjectEditorAttributeType::DateTime, "M", tr("General"), tr("Info"), tr("Created"), PDFObject(), Markup, PDFObjectEditorModelAttribute::Readonly);
    createAttribute(ObjectEditorAttributeType::Flags, "F", tr("Options"), tr("Options"), QString());

    PDFObjectEditorModelAttributeEnumItems annotationFlagItems;
    annotationFlagItems.emplace_back(tr("Invisible"), 1 << 0, PDFObject());
    annotationFlagItems.emplace_back(tr("Hidden"), 1 << 1, PDFObject());
    annotationFlagItems.emplace_back(tr("Print"), 1 << 2, PDFObject());
    annotationFlagItems.emplace_back(tr("NoZoom"), 1 << 3, PDFObject());
    annotationFlagItems.emplace_back(tr("NoRotate"), 1 << 4, PDFObject());
    annotationFlagItems.emplace_back(tr("NoView"), 1 << 5, PDFObject());
    annotationFlagItems.emplace_back(tr("ReadOnly"), 1 << 6, PDFObject());
    annotationFlagItems.emplace_back(tr("Locked"), 1 << 7, PDFObject());
    annotationFlagItems.emplace_back(tr("ToggleNoView"), 1 << 8, PDFObject());
    annotationFlagItems.emplace_back(tr("LockedContents"), 1 << 9, PDFObject());
    m_attributes.back().enumItems = qMove(annotationFlagItems);

    size_t appearanceSelector = createSelectorAttribute(tr("General"), tr("Options"), tr("Modify appearance"));
    createAttribute(ObjectEditorAttributeType::Color, "C", tr("Appearance"), tr("Colors"), tr("Color"), getDefaultColor());
    m_attributes.back().selectorAttribute = appearanceSelector;

    createAttribute(ObjectEditorAttributeType::ComboBox, "BM", tr("Appearance"), tr("Transparency"), tr("Blend mode"), PDFObject::createName("Normal"));
    m_attributes.back().selectorAttribute = appearanceSelector;

    PDFObjectEditorModelAttributeEnumItems blendModeEnumItems;
    for (BlendMode mode : PDFBlendModeInfo::getBlendModes())
    {
        annotationFlagItems.emplace_back(PDFBlendModeInfo::getBlendModeTranslatedName(mode), uint(mode), PDFObject::createName(PDFBlendModeInfo::getBlendModeName(mode).toLatin1()));
    }
    m_attributes.back().enumItems = qMove(blendModeEnumItems);

    createAttribute(ObjectEditorAttributeType::Double, "ca", tr("Appearance"), tr("Transparency"), tr("Fill opacity"), PDFObject::createReal(1.0));
    m_attributes.back().selectorAttribute = appearanceSelector;
    m_attributes.back().minValue = 0.0;
    m_attributes.back().maxValue = 1.0;
    createAttribute(ObjectEditorAttributeType::Double, "CA", tr("Appearance"), tr("Transparency"), tr("Stroke opacity"), PDFObject::createReal(1.0));
    m_attributes.back().selectorAttribute = appearanceSelector;
    m_attributes.back().minValue = 0.0;
    m_attributes.back().maxValue = 1.0;

    createAttribute(ObjectEditorAttributeType::TextLine, "Lang", tr("General"), tr("General"), tr("Language"));

    // Sticky note annotation
    createAttribute(ObjectEditorAttributeType::ComboBox, "Name", tr("Sticky note"), tr("Sticky note"), tr("Type"), PDFObject::createName("Note"), Text);

    PDFObjectEditorModelAttributeEnumItems stickyNoteEnum;
    stickyNoteEnum.emplace_back(tr("Comment"), 0, PDFObject::createName("Comment"));
    stickyNoteEnum.emplace_back(tr("Key"), 2, PDFObject::createName("Key"));
    stickyNoteEnum.emplace_back(tr("Note"), 4, PDFObject::createName("Note"));
    stickyNoteEnum.emplace_back(tr("Help"), 8, PDFObject::createName("Help"));
    stickyNoteEnum.emplace_back(tr("New Paragraph"), 16, PDFObject::createName("NewParagraph"));
    stickyNoteEnum.emplace_back(tr("Paragraph,"), 32, PDFObject::createName("Paragraph"));
    stickyNoteEnum.emplace_back(tr("Insert"), 64, PDFObject::createName("Insert"));
    m_attributes.back().enumItems = qMove(stickyNoteEnum);

    createAttribute(ObjectEditorAttributeType::Boolean, "Name", tr("Sticky note"), tr("Sticky note"), tr("Open"), PDFObject::createBool(false), Text);
}

} // namespace pdf
