//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftoolinfostructuretree.h"
#include "pdfstructuretree.h"
#include "pdfencoding.h"

namespace pdftool
{

class PDFStructureTreePrintVisitor : public pdf::PDFStructureTreeAbstractVisitor
{
public:
    explicit PDFStructureTreePrintVisitor(const pdf::PDFDocument* document,
                                          const pdf::PDFStructureTree* tree,
                                          PDFOutputFormatter* formatter) :
        m_document(document),
        m_tree(tree),
        m_formatter(formatter)
    {

    }

    virtual void visitStructureTree(const pdf::PDFStructureTree* structureTree) override;
    virtual void visitStructureElement(const pdf::PDFStructureElement* structureElement) override;
    virtual void visitStructureMarkedContentReference(const pdf::PDFStructureMarkedContentReference* structureMarkedContentReference) override;
    virtual void visitStructureObjectReference(const pdf::PDFStructureObjectReference* structureObjectReference) override;

private:
    const pdf::PDFDocument* m_document;
    const pdf::PDFStructureTree* m_tree;
    PDFOutputFormatter* m_formatter;
    QLocale m_locale;
};

void PDFStructureTreePrintVisitor::visitStructureTree(const pdf::PDFStructureTree* structureTree)
{
    m_formatter->beginHeader("tree", PDFToolTranslationContext::tr("Structure Tree"));
    acceptChildren(structureTree);
    m_formatter->endHeader();
}

void PDFStructureTreePrintVisitor::visitStructureElement(const pdf::PDFStructureElement* structureElement)
{
    pdf::PDFInteger pageIndex = m_document->getCatalog()->getPageIndexFromPageReference(structureElement->getPageReference());
    m_formatter->beginHeader("element", QString::fromLatin1(structureElement->getTypeName()), pageIndex);

    const std::vector<pdf::PDFStructureTreeAttribute>& attributes = structureElement->getAttributes();
    if (!attributes.empty())
    {
        m_formatter->beginTable("attributes", PDFToolTranslationContext::tr("Attributes"));

        m_formatter->beginTableHeaderRow("header");
        m_formatter->writeTableHeaderColumn("no", PDFToolTranslationContext::tr("No"));
        m_formatter->writeTableHeaderColumn("type", PDFToolTranslationContext::tr("Type"));
        m_formatter->writeTableHeaderColumn("owner", PDFToolTranslationContext::tr("Owner"));
        m_formatter->writeTableHeaderColumn("revision", PDFToolTranslationContext::tr("Revision"));
        m_formatter->writeTableHeaderColumn("hidden", PDFToolTranslationContext::tr("Hidden"));
        m_formatter->writeTableHeaderColumn("value", PDFToolTranslationContext::tr("Value"));
        m_formatter->endTableHeaderRow();

        int ref = 0;
        for (const pdf::PDFStructureTreeAttribute& attribute : attributes)
        {
            m_formatter->beginTableRow("attribute", ref);

            m_formatter->writeTableColumn("no", m_locale.toString(ref + 1), Qt::AlignRight);
            m_formatter->writeTableColumn("type", attribute.getTypeName(&m_document->getStorage()));
            m_formatter->writeTableColumn("owner", attribute.getOwnerName());

            if (attribute.getRevision() > 0)
            {
                m_formatter->writeTableColumn("revision", m_locale.toString(attribute.getRevision()));
            }
            else
            {
                m_formatter->writeTableColumn("revision", QString());
            }

            if (attribute.isUser())
            {
                m_formatter->writeTableColumn("hidden", attribute.getUserPropertyIsHidden(&m_document->getStorage()) ? PDFToolTranslationContext::tr("Yes") : PDFToolTranslationContext::tr("No"));
            }
            else
            {
                m_formatter->writeTableColumn("hidden", QString());
            }

            QString value;
            pdf::PDFObject valueObject = attribute.getValue();
            if (attribute.isUser())
            {
                value = attribute.getUserPropertyFormattedValue(&m_document->getStorage());
                valueObject = attribute.getUserPropertyValue(&m_document->getStorage());
            }
            valueObject = m_document->getObject(valueObject);

            if (value.isEmpty())
            {
                switch (valueObject.getType())
                {
                    case pdf::PDFObject::Type::Null:
                        value = PDFToolTranslationContext::tr("[null]");
                        break;

                    case pdf::PDFObject::Type::Bool:
                        value = valueObject.getBool() ? PDFToolTranslationContext::tr("Yes") : PDFToolTranslationContext::tr("No");
                        break;

                    case pdf::PDFObject::Type::Int:
                        value = m_locale.toString(valueObject.getInteger());
                        break;

                    case pdf::PDFObject::Type::Real:
                        value = m_locale.toString(valueObject.getReal());
                        break;

                    case pdf::PDFObject::Type::String:
                    case pdf::PDFObject::Type::Name:
                        value = pdf::PDFEncoding::convertSmartFromByteStringToUnicode(valueObject.getString(), nullptr);
                        break;

                    case pdf::PDFObject::Type::Array:
                    case pdf::PDFObject::Type::Dictionary:
                    case pdf::PDFObject::Type::Stream:
                    case pdf::PDFObject::Type::Reference:
                        value = PDFToolTranslationContext::tr("[complex type]");
                        break;

                    default:
                        break;
                }
            }
            m_formatter->writeTableColumn("value", value);

            m_formatter->endTableRow();

            ++ref;
        }

        m_formatter->endTable();
    }

    bool hasText = false;
    std::array<QString, pdf::PDFStructureElement::LastStringValue> stringValues;
    for (int i = 0; i < pdf::PDFStructureElement::LastStringValue; ++i)
    {
        stringValues[i] = structureElement->getText(static_cast<pdf::PDFStructureElement::StringValue>(i));
        hasText = hasText || !stringValues[i].isEmpty();
    }

    if (hasText)
    {
        m_formatter->beginTable("properties", PDFToolTranslationContext::tr("Properties"));

        m_formatter->beginTableHeaderRow("header");
        m_formatter->writeTableHeaderColumn("no", PDFToolTranslationContext::tr("No"));
        m_formatter->writeTableHeaderColumn("property", PDFToolTranslationContext::tr("Property"));
        m_formatter->writeTableHeaderColumn("value", PDFToolTranslationContext::tr("Value"));
        m_formatter->endTableHeaderRow();

        int ref = 1;
        for (int i = 0; i < pdf::PDFStructureElement::LastStringValue; ++i)
        {
            if (stringValues[i].isEmpty())
            {
                continue;
            }

            QString propertyName;
            switch (i)
            {
                case pdf::PDFStructureElement::Title:
                    propertyName = PDFToolTranslationContext::tr("Title");
                    break;

                case pdf::PDFStructureElement::Language:
                    propertyName = PDFToolTranslationContext::tr("Language");
                    break;

                case pdf::PDFStructureElement::AlternativeDescription:
                    propertyName = PDFToolTranslationContext::tr("Alternative description");
                    break;

                case pdf::PDFStructureElement::ExpandedForm:
                    propertyName = PDFToolTranslationContext::tr("Expanded form");
                    break;

                case pdf::PDFStructureElement::ActualText:
                    propertyName = PDFToolTranslationContext::tr("Actual text");
                    break;

                case pdf::PDFStructureElement::Phoneme:
                    propertyName = PDFToolTranslationContext::tr("Phoneme");
                    break;

                default:
                    Q_ASSERT(false);
                    break;
            }

            m_formatter->beginTableRow("property", i);
            m_formatter->writeTableColumn("no", m_locale.toString(ref++), Qt::AlignRight);
            m_formatter->writeTableColumn("property", propertyName);
            m_formatter->writeTableColumn("value", stringValues[i]);
            m_formatter->endTableRow();
        }

        m_formatter->endTable();
    }

    acceptChildren(structureElement);
    m_formatter->endHeader();
}

void PDFStructureTreePrintVisitor::visitStructureMarkedContentReference(const pdf::PDFStructureMarkedContentReference* structureMarkedContentReference)
{
    const pdf::PDFInteger reference = structureMarkedContentReference->getMarkedContentIdentifier();
    m_formatter->writeText("marked-content-reference", PDFToolTranslationContext::tr("Marked Content Reference %1").arg(reference), reference);
}

void PDFStructureTreePrintVisitor::visitStructureObjectReference(const pdf::PDFStructureObjectReference* structureObjectReference)
{
    const pdf::PDFObjectReference reference = structureObjectReference->getObjectReference();
    m_formatter->writeText("structure-object-reference", PDFToolTranslationContext::tr("Structure Object Reference [%1 %2 R]").arg(reference.objectNumber).arg(reference.generation), reference.objectNumber);
}

static PDFToolInfoStructureTreeApplication s_infoStructureTreeApplication;

QString PDFToolInfoStructureTreeApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "info-struct-tree";

        case Name:
            return PDFToolTranslationContext::tr("Info (Structure tree)");

        case Description:
            return PDFToolTranslationContext::tr("Examine structure tree in tagged document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolInfoStructureTreeApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    pdf::PDFStructureTree structureTree = pdf::PDFStructureTree::parse(&document.getStorage(), document.getCatalog()->getStructureTreeRoot());
    if (structureTree.isValid())
    {
        PDFOutputFormatter formatter(options.outputStyle, options.outputCodec);
        formatter.beginDocument("info-structure-tree", PDFToolTranslationContext::tr("Structure tree in document %1").arg(options.document));

        PDFStructureTreePrintVisitor visitor(&document, &structureTree, &formatter);
        structureTree.accept(&visitor);

        formatter.endDocument();
        PDFConsole::writeText(formatter.getString(), options.outputCodec);
    }
    else
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("No structure tree found in document."), options.outputCodec);
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolInfoStructureTreeApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument;
}

}   // namespace pdftool
