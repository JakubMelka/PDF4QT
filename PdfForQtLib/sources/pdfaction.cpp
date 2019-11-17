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

#include "pdfaction.h"
#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdfencoding.h"

namespace pdf
{

PDFActionPtr PDFAction::parse(const PDFDocument* document, PDFObject object)
{
    std::set<PDFObjectReference> usedReferences;
    return parseImpl(document, qMove(object), usedReferences);
}

void PDFAction::apply(const std::function<void (const PDFAction*)>& callback)
{
    callback(this);

    for (const PDFActionPtr& nextAction : m_nextActions)
    {
        nextAction->apply(callback);
    }
}

PDFActionPtr PDFAction::parseImpl(const PDFDocument* document, PDFObject object, std::set<PDFObjectReference>& usedReferences)
{
    if (object.isReference())
    {
        PDFObjectReference reference = object.getReference();
        if (usedReferences.count(reference))
        {
            throw PDFException(PDFTranslationContext::tr("Circular dependence in actions found."));
        }
        usedReferences.insert(reference);
        object = document->getObjectByReference(reference);
    }

    if (object.isNull())
    {
        return PDFActionPtr();
    }

    if (!object.isDictionary())
    {
        throw PDFException(PDFTranslationContext::tr("Invalid action."));
    }

    PDFDocumentDataLoaderDecorator loader(document);
    const PDFDictionary* dictionary = object.getDictionary();
    QByteArray name = loader.readNameFromDictionary(dictionary, "S");

    if (name == "GoTo") // Goto action
    {
        PDFDestination destination = PDFDestination::parse(document, dictionary->get("D"));
        return PDFActionPtr(new PDFActionGoTo(qMove(destination)));
    }
    else if (name == "GoToR")
    {
        PDFDestination destination = PDFDestination::parse(document, dictionary->get("D"));
        PDFFileSpecification fileSpecification = PDFFileSpecification::parse(document, dictionary->get("F"));
        return PDFActionPtr(new PDFActionGoToR(qMove(destination), qMove(fileSpecification), loader.readBooleanFromDictionary(dictionary, "NewWindow", false)));
    }
    else if (name == "GoToE")
    {
        PDFDestination destination = PDFDestination::parse(document, dictionary->get("D"));
        PDFFileSpecification fileSpecification = PDFFileSpecification::parse(document, dictionary->get("F"));
        return PDFActionPtr(new PDFActionGoToE(qMove(destination), qMove(fileSpecification), loader.readBooleanFromDictionary(dictionary, "NewWindow", false), document->getObject(dictionary->get("T"))));
    }
    else if (name == "Launch")
    {
        PDFFileSpecification fileSpecification = PDFFileSpecification::parse(document, dictionary->get("F"));
        const bool newWindow = loader.readBooleanFromDictionary(dictionary, "NewWindow", false);
        PDFActionLaunch::Win win;

        const PDFObject& winDictionaryObject = document->getObject(dictionary->get("Win"));
        if (winDictionaryObject.isDictionary())
        {
            const PDFDictionary* winDictionary = winDictionaryObject.getDictionary();
            win.file = loader.readStringFromDictionary(winDictionary, "F");
            win.directory = loader.readStringFromDictionary(winDictionary, "D");
            win.operation = loader.readStringFromDictionary(winDictionary, "O");
            win.parameters = loader.readStringFromDictionary(winDictionary, "P");
        }

        return PDFActionPtr(new PDFActionLaunch(qMove(fileSpecification), newWindow, qMove(win)));
    }
    else if (name == "Thread")
    {
        PDFFileSpecification fileSpecification = PDFFileSpecification::parse(document, dictionary->get("F"));
        PDFActionThread::Thread thread;
        PDFActionThread::Bead bead;

        const PDFObject& threadObject = dictionary->get("D");
        if (threadObject.isReference())
        {
            thread = threadObject.getReference();
        }
        else if (threadObject.isInt())
        {
            thread = threadObject.getInteger();
        }
        else if (threadObject.isString())
        {
            thread = PDFEncoding::convertTextString(threadObject.getString());
        }
        const PDFObject& beadObject = dictionary->get("B");
        if (beadObject.isReference())
        {
            bead = beadObject.getReference();
        }
        else if (beadObject.isInt())
        {
            bead = beadObject.getInteger();
        }

        return PDFActionPtr(new PDFActionThread(qMove(fileSpecification), qMove(thread), qMove(bead)));
    }
    else if (name == "URI")
    {
        return PDFActionPtr(new PDFActionURI(loader.readStringFromDictionary(dictionary, "URI"), loader.readBooleanFromDictionary(dictionary, "IsMap", false)));
    }

    return PDFActionPtr();
}

PDFDestination PDFDestination::parse(const PDFDocument* document, PDFObject object)
{
    PDFDestination result;
    object = document->getObject(object);

    if (object.isName() || object.isString())
    {
        QByteArray name = object.getString();
        result.m_destinationType = DestinationType::Named;
        result.m_name = name;
    }
    else if (object.isArray())
    {
        const PDFArray* array = object.getArray();
        if (array->getCount() < 2)
        {
            throw PDFException(PDFTranslationContext::tr("Invalid destination - array has invalid size."));
        }

        PDFDocumentDataLoaderDecorator loader(document);

        // First parse page number/page index
        PDFObject pageNumberObject = array->getItem(0);
        if (pageNumberObject.isReference())
        {
            result.m_pageReference = pageNumberObject.getReference();
        }
        else if (pageNumberObject.isInt())
        {
            result.m_pageIndex = pageNumberObject.getInteger();
        }
        else
        {
            throw PDFException(PDFTranslationContext::tr("Invalid destination - invalid page reference."));
        }

        QByteArray name = loader.readName(array->getItem(1));

        size_t currentIndex = 2;
        auto readNumber = [&]()
        {
            if (currentIndex >= array->getCount())
            {
                throw PDFException(PDFTranslationContext::tr("Invalid destination - array has invalid size."));
            }
            return loader.readNumber(array->getItem(currentIndex++), 0.0);
        };

        if (name == "XYZ")
        {
            result.m_destinationType = DestinationType::XYZ;
            result.m_left = readNumber();
            result.m_top = readNumber();
            result.m_zoom = readNumber();
        }
        else if (name == "Fit")
        {
            result.m_destinationType = DestinationType::Fit;
        }
        else if (name == "FitH")
        {
            result.m_destinationType = DestinationType::FitH;
            result.m_top = readNumber();
        }
        else if (name == "FitV")
        {
            result.m_destinationType = DestinationType::FitV;
            result.m_left = readNumber();
        }
        else if (name == "FitR")
        {
            result.m_destinationType = DestinationType::FitR;
            result.m_left = readNumber();
            result.m_bottom = readNumber();
            result.m_right = readNumber();
            result.m_top = readNumber();
        }
        else if (name == "FitB")
        {
            result.m_destinationType = DestinationType::FitB;
        }
        else if (name == "FitBH")
        {
            result.m_destinationType = DestinationType::FitBH;
            result.m_top = readNumber();
        }
        else if (name == "FitBV")
        {
            result.m_destinationType = DestinationType::FitBV;
            result.m_left = readNumber();
        }
        else
        {
            throw PDFException(PDFTranslationContext::tr("Invalid destination - unknown type."));
        }
    }

    return result;
}

}   // namespace pdf
