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

#include "pdfoutline.h"
#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdfencoding.h"

namespace pdf
{

size_t PDFOutlineItem::getTotalCount() const
{
    size_t count = m_children.size();

    for (size_t i = 0; i < m_children.size(); ++i)
    {
        count += getChild(i)->getTotalCount();
    }

    return count;
}

QSharedPointer<PDFOutlineItem> PDFOutlineItem::parse(const PDFDocument* document, const PDFObject& root)
{
    const PDFObject& rootDereferenced = document->getObject(root);
    if (rootDereferenced.isDictionary())
    {
        const PDFDictionary* dictionary = rootDereferenced.getDictionary();
        const PDFObject& first = dictionary->get("First");

        if (first.isReference())
        {
            QSharedPointer<PDFOutlineItem> result(new PDFOutlineItem());
            std::set<PDFObjectReference> visitedOutlineItems;
            parseImpl(document, result.get(), first.getReference(), visitedOutlineItems);
            return result;
        }
    }

    return QSharedPointer<PDFOutlineItem>();
}

void PDFOutlineItem::parseImpl(const PDFDocument* document,
                               PDFOutlineItem* parent,
                               PDFObjectReference currentItem,
                               std::set<PDFObjectReference>& visitedOutlineItems)
{
    auto checkCyclicDependence = [&visitedOutlineItems](PDFObjectReference reference)
    {
        if (visitedOutlineItems.count(reference))
        {
            throw PDFException(PDFTranslationContext::tr("Outline items have cyclic dependence."));
        }

        visitedOutlineItems.insert(reference);
    };
    checkCyclicDependence(currentItem);

    PDFObject dereferencedItem = document->getObjectByReference(currentItem);
    while (dereferencedItem.isDictionary())
    {
        const PDFDictionary* dictionary = dereferencedItem.getDictionary();

        QSharedPointer<PDFOutlineItem> currentOutlineItem(new PDFOutlineItem());
        const PDFObject& titleObject = document->getObject(dictionary->get("Title"));
        if (titleObject.isString())
        {
            currentOutlineItem->setTitle(PDFEncoding::convertTextString(titleObject.getString()));
        }
        currentOutlineItem->setAction(PDFAction::parse(&document->getStorage(), dictionary->get("A")));
        if (!currentOutlineItem->getAction() && dictionary->hasKey("Dest"))
        {
            currentOutlineItem->setAction(PDFActionPtr(new PDFActionGoTo(PDFDestination::parse(&document->getStorage(), dictionary->get("Dest")), PDFDestination())));
        }

        PDFDocumentDataLoaderDecorator loader(document);
        std::vector<PDFReal> colors = loader.readNumberArrayFromDictionary(dictionary, "C", { 0.0, 0.0, 0.0 });
        colors.resize(3, 0.0);
        currentOutlineItem->setTextColor(QColor::fromRgbF(colors[0], colors[1], colors[2]));
        PDFInteger flag = loader.readIntegerFromDictionary(dictionary, "F", 0);
        currentOutlineItem->setFontItalics(flag & 0x1);
        currentOutlineItem->setFontBold(flag & 0x2);
        PDFObject structureElementObject = dictionary->get("SE");
        if (structureElementObject.isReference())
        {
            currentOutlineItem->setStructureElement(structureElementObject.getReference());
        }

        // Parse children of this item
        const PDFObject& firstItem = dictionary->get("First");
        if (firstItem.isReference())
        {
            parseImpl(document, currentOutlineItem.get(), firstItem.getReference(), visitedOutlineItems);
        }

        // Add new child to the parent
        parent->addChild(qMove(currentOutlineItem));

        // Parse next item
        const PDFObject& nextItem = dictionary->get("Next");
        if (nextItem.isReference())
        {
            checkCyclicDependence(nextItem.getReference());
            dereferencedItem = document->getObject(nextItem);
        }
        else
        {
            // We are finished
            break;
        }
    }
}

PDFObjectReference PDFOutlineItem::getStructureElement() const
{
    return m_structureElement;
}

void PDFOutlineItem::setStructureElement(PDFObjectReference structureElement)
{
    m_structureElement = structureElement;
}

bool PDFOutlineItem::isFontBold() const
{
    return m_fontBold;
}

void PDFOutlineItem::setFontBold(bool fontBold)
{
    m_fontBold = fontBold;
}

bool PDFOutlineItem::isFontItalics() const
{
    return m_fontItalics;
}

void PDFOutlineItem::setFontItalics(bool fontItalics)
{
    m_fontItalics = fontItalics;
}

QColor PDFOutlineItem::getTextColor() const
{
    return m_textColor;
}

void PDFOutlineItem::setTextColor(const QColor& textColor)
{
    m_textColor = textColor;
}

const PDFAction* PDFOutlineItem::getAction() const
{
    return m_action.get();
}

void PDFOutlineItem::setAction(const PDFActionPtr& action)
{
    m_action = action;
}

}   // namespace pdf
