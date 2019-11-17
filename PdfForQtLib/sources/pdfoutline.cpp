//    Copyright (C) 2019 Jakub Melka
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
        currentOutlineItem->setAction(PDFAction::parse(document, dictionary->get("A")));

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

const PDFAction* PDFOutlineItem::getAction() const
{
    return m_action.get();
}

void PDFOutlineItem::setAction(const PDFActionPtr& action)
{
    m_action = action;
}

}   // namespace pdf
