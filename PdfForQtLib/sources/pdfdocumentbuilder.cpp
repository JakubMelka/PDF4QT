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

#include "pdfdocumentbuilder.h"
#include "pdfencoding.h"
#include "pdfconstants.h"

namespace pdf
{

void PDFObjectFactory::beginArray()
{
    m_items.emplace_back(ItemType::Array, PDFArray());
}

void PDFObjectFactory::endArray()
{
    Item topItem = qMove(m_items.back());
    Q_ASSERT(topItem.type == ItemType::Array);
    m_items.pop_back();
    addObject(PDFObject::createArray(std::make_shared<PDFArray>(qMove(std::get<PDFArray>(topItem.object)))));
}

void PDFObjectFactory::beginDictionary()
{
    m_items.emplace_back(ItemType::Dictionary, PDFDictionary());
}

void PDFObjectFactory::endDictionary()
{
    Item topItem = qMove(m_items.back());
    Q_ASSERT(topItem.type == ItemType::Dictionary);
    m_items.pop_back();
    addObject(PDFObject::createDictionary(std::make_shared<PDFDictionary>(qMove(std::get<PDFDictionary>(topItem.object)))));
}

void PDFObjectFactory::beginDictionaryItem(const QByteArray& name)
{
    m_items.emplace_back(ItemType::DictionaryItem, name, PDFObject());
}

void PDFObjectFactory::endDictionaryItem()
{
    Item topItem = qMove(m_items.back());
    Q_ASSERT(topItem.type == ItemType::DictionaryItem);
    m_items.pop_back();

    Item& dictionaryItem = m_items.back();
    Q_ASSERT(dictionaryItem.type == ItemType::Dictionary);
    std::get<PDFDictionary>(dictionaryItem.object).addEntry(qMove(topItem.itemName), qMove(std::get<PDFObject>(topItem.object)));
}

PDFObjectFactory& PDFObjectFactory::operator<<(AnnotationLineEnding lineEnding)
{
    switch (lineEnding)
    {
        case AnnotationLineEnding::Square:
            *this << WrapName("Square");
            break;

        case AnnotationLineEnding::Circle:
            *this << WrapName("Circle");
            break;

        case AnnotationLineEnding::Diamond:
            *this << WrapName("Diamond");
            break;

        case AnnotationLineEnding::OpenArrow:
            *this << WrapName("OpenArrow");
            break;

        case AnnotationLineEnding::ClosedArrow:
            *this << WrapName("ClosedArrow");
            break;

        case AnnotationLineEnding::Butt:
            *this << WrapName("Butt");
            break;

        case AnnotationLineEnding::ROpenArrow:
            *this << WrapName("ROpenArrow");
            break;

        case AnnotationLineEnding::RClosedArrow:
            *this << WrapName("RClosedArrow");
            break;

        case AnnotationLineEnding::Slash:
            *this << WrapName("Slash");
            break;

        case AnnotationLineEnding::None:
        default:
            *this << WrapName("None");
            break;
    }

    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(WrapString string)
{
    addObject(PDFObject::createString(std::make_shared<PDFString>(qMove(string.string))));
    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(WrapFreeTextAlignment alignment)
{
    if (alignment.alignment.testFlag(Qt::AlignLeft))
    {
        *this << 0;
    }
    else if (alignment.alignment.testFlag(Qt::AlignHCenter))
    {
        *this << 1;
    }
    else if (alignment.alignment.testFlag(Qt::AlignRight))
    {
        *this << 2;
    }
    else
    {
        // Default is left alignment
        *this << 0;
    }
    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(LinkHighlightMode mode)
{
    switch (mode)
    {
        case LinkHighlightMode::None:
        {
            *this << WrapName("N");
            break;
        }

        case LinkHighlightMode::Invert:
        {
            *this << WrapName("I");
            break;
        }

        case LinkHighlightMode::Outline:
        {
            *this << WrapName("O");
            break;
        }

        case LinkHighlightMode::Push:
        {
            *this << WrapName("P");
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }

    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(TextAnnotationIcon icon)
{
    switch (icon)
    {
        case TextAnnotationIcon::Comment:
        {
            *this << WrapName("Comment");
            break;
        }

        case TextAnnotationIcon::Help:
        {
            *this << WrapName("Help");
            break;
        }

        case TextAnnotationIcon::Insert:
        {
            *this << WrapName("Insert");
            break;
        }

        case TextAnnotationIcon::Key:
        {
            *this << WrapName("Key");
            break;
        }

        case TextAnnotationIcon::NewParagraph:
        {
            *this << WrapName("NewParagraph");
            break;
        }

        case TextAnnotationIcon::Note:
        {
            *this << WrapName("Note");
            break;
        }

        case TextAnnotationIcon::Paragraph:
        {
            *this << WrapName("Paragraph");
            break;
        }

        default:
        {
            Q_ASSERT(false);
        }
    }

    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(WrapEmptyArray)
{
    beginArray();
    endArray();
    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(QString textString)
{
    if (!PDFEncoding::canConvertToEncoding(textString, PDFEncoding::Encoding::PDFDoc))
    {
        // Use unicode encoding
        QByteArray ba;

        {
            QTextStream textStream(&ba, QIODevice::WriteOnly);
            textStream.setCodec("UTF-16BE");
            textStream.setGenerateByteOrderMark(true);
            textStream << textString;
        }

        addObject(PDFObject::createString(std::make_shared<PDFString>(qMove(ba))));
    }
    else
    {
        // Use PDF document encoding
        addObject(PDFObject::createString(std::make_shared<PDFString>(PDFEncoding::convertToEncoding(textString, PDFEncoding::Encoding::PDFDoc))));
    }

    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(WrapAnnotationColor color)
{
    if (color.color.isValid())
    {
        // Jakub Melka: we will decide, if we have gray/rgb/cmyk color
        QColor value = color.color;
        if (value.spec() == QColor::Cmyk)
        {
            *this << std::initializer_list<PDFReal>{ value.cyanF(), value.magentaF(), value.yellowF(), value.blackF() };
        }
        else if (qIsGray(value.rgb()))
        {
            *this << std::initializer_list<PDFReal>{ value.redF() };
        }
        else
        {
            *this << std::initializer_list<PDFReal>{ value.redF(), value.greenF(), value.blueF() };
        }
    }
    else
    {
        addObject(PDFObject::createNull());
    }

    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(WrapCurrentDateTime)
{
    addObject(PDFObject::createString(std::make_shared<PDFString>(PDFEncoding::converDateTimeToString(QDateTime::currentDateTime()))));
    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(const QRectF& value)
{
    *this << std::initializer_list<PDFReal>{ value.left(), value.top(), value.right(), value.bottom() };
    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(int value)
{
    *this << PDFInteger(value);
    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(WrapName wrapName)
{
    addObject(PDFObject::createName(std::make_shared<PDFString>(qMove(wrapName.name))));
    return *this;
}

PDFObject PDFObjectFactory::takeObject()
{
    Q_ASSERT(m_items.size() == 1);
    Q_ASSERT(m_items.back().type == ItemType::Object);
    PDFObject result = qMove(std::get<PDFObject>(m_items.back().object));
    m_items.clear();
    return result;
}

void PDFObjectFactory::addObject(PDFObject object)
{
    if (m_items.empty())
    {
        m_items.emplace_back(ItemType::Object, qMove(object));
        return;
    }

    Item& topItem = m_items.back();
    switch (topItem.type)
    {
        case ItemType::Object:
            // Just override the object
            topItem.object = qMove(object);
            break;

        case ItemType::Dictionary:
            // Do not do anything - we are inside dictionary
            break;

        case ItemType::DictionaryItem:
            // Add item to dictionary item
            topItem.object = qMove(object);
            break;

        case ItemType::Array:
            std::get<PDFArray>(topItem.object).appendItem(qMove(object));
            break;

        default:
            Q_ASSERT(false);
            break;
    }
}

PDFObjectFactory& PDFObjectFactory::operator<<(std::nullptr_t)
{
    addObject(PDFObject::createNull());
    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(bool value)
{
    addObject(PDFObject::createBool(value));
    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(PDFReal value)
{
    addObject(PDFObject::createReal(value));
    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(PDFInteger value)
{
    addObject(PDFObject::createInteger(value));
    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(PDFObjectReference value)
{
    addObject(PDFObject::createReference(value));
    return *this;
}

PDFDocumentBuilder::PDFDocumentBuilder() :
    m_version(1, 7)
{
    createDocument();
}

PDFDocumentBuilder::PDFDocumentBuilder(const PDFDocument* document) :
    m_storage(document->getStorage()),
    m_version(document->getInfo()->version)
{

}

void PDFDocumentBuilder::reset()
{
    *this = PDFDocumentBuilder();
}

void PDFDocumentBuilder::createDocument()
{
    if (!m_storage.getObjects().empty())
    {
        reset();
    }

    addObject(PDFObject::createNull());
    PDFObjectReference catalog = createCatalog();
    PDFObject trailerDictionary = createTrailerDictionary(catalog);
    m_storage.updateTrailerDictionary(trailerDictionary);
    m_storage.setSecurityHandler(PDFSecurityHandlerPointer(new PDFNoneSecurityHandler()));
}

PDFDocument PDFDocumentBuilder::build()
{
    updateTrailerDictionary(m_storage.getObjects().size());
    return PDFDocument(PDFObjectStorage(m_storage), m_version);
}

std::array<PDFReal, 4> PDFDocumentBuilder::getAnnotationReductionRectangle(const QRectF& boundingRect, const QRectF& innerRect) const
{
    return { qAbs(innerRect.left() - boundingRect.left()), qAbs(boundingRect.bottom() - innerRect.bottom()), qAbs(boundingRect.right() - innerRect.right()), qAbs(boundingRect.top() - innerRect.top()) };
}

PDFObjectReference PDFDocumentBuilder::addObject(PDFObject object)
{
    return m_storage.addObject(PDFObjectManipulator::removeNullObjects(object));
}

void PDFDocumentBuilder::mergeTo(PDFObjectReference reference, PDFObject object)
{
    m_storage.setObject(reference, PDFObjectManipulator::merge(m_storage.getObject(reference), qMove(object), PDFObjectManipulator::RemoveNullObjects));
}

void PDFDocumentBuilder::appendTo(PDFObjectReference reference, PDFObject object)
{
    m_storage.setObject(reference, PDFObjectManipulator::merge(m_storage.getObject(reference), qMove(object), PDFObjectManipulator::ConcatenateArrays));
}

QRectF PDFDocumentBuilder::getPopupWindowRect(const QRectF& rectangle) const
{
    QRectF rect = rectangle.translated(rectangle.width() * 1.25, 0);
    rect.setSize(QSizeF(100, 100));
    return rect;
}

QString PDFDocumentBuilder::getProducerString() const
{
    return PDF_LIBRARY_NAME;
}

PDFObjectReference PDFDocumentBuilder::getPageTreeRoot() const
{
    if (const PDFDictionary* trailerDictionary = getDictionaryFromObject(m_storage.getTrailerDictionary()))
    {
        if (const PDFDictionary* catalogDictionary = getDictionaryFromObject(trailerDictionary->get("Root")))
        {
            PDFObject pagesRoot = catalogDictionary->get("Pages");
            if (pagesRoot.isReference())
            {
                return pagesRoot.getReference();
            }
        }
    }

    return PDFObjectReference();
}

PDFInteger PDFDocumentBuilder::getPageTreeRootChildCount() const
{
    if (const PDFDictionary* pageTreeRootDictionary = getDictionaryFromObject(getObjectByReference(getPageTreeRoot())))
    {
        PDFObject childCountObject = getObject(pageTreeRootDictionary->get("Count"));
        if (childCountObject.isInt())
        {
            return childCountObject.getInteger();
        }
    }

    return 0;
}

/* START GENERATED CODE */

PDFObjectReference PDFDocumentBuilder::appendPage(QRectF mediaBox)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Page");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Parent");
    objectBuilder << getPageTreeRoot();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionary();
    objectBuilder.endDictionary();
    objectBuilder.beginDictionaryItem("MediaBox");
    objectBuilder << mediaBox;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObjectReference pageReference = addObject(objectBuilder.takeObject());
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Kids");
    objectBuilder << std::initializer_list<PDFObjectReference>{ pageReference };
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Count");
    objectBuilder << getPageTreeRootChildCount() + 1;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject updatedTreeRoot = objectBuilder.takeObject();
    appendTo(getPageTreeRoot(), updatedTreeRoot);
    return pageReference;
}


PDFObjectReference PDFDocumentBuilder::createActionURI(QString URL)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Action");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("S");
    objectBuilder << WrapName("URI");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("URI");
    objectBuilder << URL;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObjectReference actionReference = addObject(objectBuilder.takeObject());
    return actionReference;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationCircle(PDFObjectReference page,
                                                              QRectF rectangle,
                                                              PDFReal borderWidth,
                                                              QColor fillColor,
                                                              QColor strokeColor,
                                                              QString title,
                                                              QString subject,
                                                              QString contents)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Annot");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subtype");
    objectBuilder << WrapName("Circle");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << rectangle;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("F");
    objectBuilder << 4;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("M");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("CreationDate");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Border");
    objectBuilder << std::initializer_list<PDFReal>{ 0.0, 0.0, borderWidth };
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("C");
    objectBuilder << WrapAnnotationColor(strokeColor);
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("IC");
    objectBuilder << WrapAnnotationColor(fillColor);
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("T");
    objectBuilder << title;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Contents");
    objectBuilder << contents;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subj");
    objectBuilder << subject;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObjectReference annotationObject = addObject(objectBuilder.takeObject());
    PDFObjectReference popupAnnotation = createAnnotationPopup(page, annotationObject, getPopupWindowRect(rectangle), false);

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Popup");
    objectBuilder << popupAnnotation;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject updateAnnotationPopup = objectBuilder.takeObject();
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Annots");
    objectBuilder.beginArray();
    objectBuilder << annotationObject;
    objectBuilder << popupAnnotation;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject pageAnnots = objectBuilder.takeObject();
    mergeTo(annotationObject, updateAnnotationPopup);
    appendTo(page, pageAnnots);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationFreeText(PDFObjectReference page,
                                                                QRectF rectangle,
                                                                QString title,
                                                                QString subject,
                                                                QString contents,
                                                                TextAlignment textAlignment)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Annot");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subtype");
    objectBuilder << WrapName("FreeText");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << rectangle;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("F");
    objectBuilder << 4;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("M");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("CreationDate");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("T");
    objectBuilder << title;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Contents");
    objectBuilder << contents;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subj");
    objectBuilder << subject;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Q");
    objectBuilder << WrapFreeTextAlignment(textAlignment);
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("DA");
    objectBuilder << WrapString("/Arial 10 Tf");
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObjectReference annotationObject = addObject(objectBuilder.takeObject());
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Annots");
    objectBuilder.beginArray();
    objectBuilder << annotationObject;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject pageAnnots = objectBuilder.takeObject();
    appendTo(page, pageAnnots);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationFreeText(PDFObjectReference page,
                                                                QRectF boundingRectangle,
                                                                QRectF textRectangle,
                                                                QString title,
                                                                QString subject,
                                                                QString contents,
                                                                TextAlignment textAlignment,
                                                                QPointF startPoint,
                                                                QPointF endPoint,
                                                                AnnotationLineEnding startLineType,
                                                                AnnotationLineEnding endLineType)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Annot");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subtype");
    objectBuilder << WrapName("FreeText");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << boundingRectangle;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("F");
    objectBuilder << 4;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("M");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("CreationDate");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("T");
    objectBuilder << title;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Contents");
    objectBuilder << contents;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subj");
    objectBuilder << subject;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Q");
    objectBuilder << WrapFreeTextAlignment(textAlignment);
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("DA");
    objectBuilder << WrapString("/Arial 10 Tf");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("RD");
    objectBuilder << getAnnotationReductionRectangle(boundingRectangle, textRectangle);
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("CL");
    objectBuilder.beginArray();
    objectBuilder << startPoint.x();
    objectBuilder << startPoint.y();
    objectBuilder << endPoint.x();
    objectBuilder << endPoint.y();
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("LE");
    objectBuilder.beginArray();
    objectBuilder << startLineType;
    objectBuilder << endLineType;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObjectReference annotationObject = addObject(objectBuilder.takeObject());
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Annots");
    objectBuilder.beginArray();
    objectBuilder << annotationObject;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject pageAnnots = objectBuilder.takeObject();
    appendTo(page, pageAnnots);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationLine(PDFObjectReference page,
                                                            QRectF boundingRect,
                                                            QPointF startPoint,
                                                            QPointF endPoint,
                                                            PDFReal lineWidth,
                                                            QColor fillColor,
                                                            QColor strokeColor,
                                                            QString title,
                                                            QString subject,
                                                            QString contents,
                                                            AnnotationLineEnding startLineType,
                                                            AnnotationLineEnding endLineType)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Annot");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subtype");
    objectBuilder << WrapName("Line");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << boundingRect;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("F");
    objectBuilder << 4;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("L");
    objectBuilder.beginArray();
    objectBuilder << startPoint.x();
    objectBuilder << startPoint.y();
    objectBuilder << endPoint.x();
    objectBuilder << endPoint.y();
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("LE");
    objectBuilder.beginArray();
    objectBuilder << startLineType;
    objectBuilder << endLineType;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("M");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("CreationDate");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Border");
    objectBuilder << std::initializer_list<PDFReal>{ 0.0, 0.0, lineWidth };
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("C");
    objectBuilder << WrapAnnotationColor(strokeColor);
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("IC");
    objectBuilder << WrapAnnotationColor(fillColor);
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("T");
    objectBuilder << title;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Contents");
    objectBuilder << contents;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subj");
    objectBuilder << subject;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObjectReference annotationObject = addObject(objectBuilder.takeObject());
    PDFObjectReference popupAnnotation = createAnnotationPopup(page, annotationObject, getPopupWindowRect(boundingRect), false);

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Popup");
    objectBuilder << popupAnnotation;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject updateAnnotationPopup = objectBuilder.takeObject();
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Annots");
    objectBuilder.beginArray();
    objectBuilder << annotationObject;
    objectBuilder << popupAnnotation;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject pageAnnots = objectBuilder.takeObject();
    mergeTo(annotationObject, updateAnnotationPopup);
    appendTo(page, pageAnnots);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationLink(PDFObjectReference page,
                                                            QRectF linkRectangle,
                                                            PDFObjectReference action,
                                                            LinkHighlightMode highlightMode)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Annot");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subtype");
    objectBuilder << WrapName("Link");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << linkRectangle;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("A");
    objectBuilder << action;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("H");
    objectBuilder << highlightMode;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObjectReference annotationReference = addObject(objectBuilder.takeObject());
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Annots");
    objectBuilder.beginArray();
    objectBuilder << annotationReference;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject pageAnnots = objectBuilder.takeObject();
    appendTo(page, pageAnnots);
    return annotationReference;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationLink(PDFObjectReference page,
                                                            QRectF linkRectangle,
                                                            QString URL,
                                                            LinkHighlightMode highlightMode)
{
    PDFObjectFactory objectBuilder;

    return createAnnotationLink(page, linkRectangle, createActionURI(URL), highlightMode);
}


PDFObjectReference PDFDocumentBuilder::createAnnotationPopup(PDFObjectReference page,
                                                             PDFObjectReference parentAnnotation,
                                                             QRectF rectangle,
                                                             bool opened)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Annot");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subtype");
    objectBuilder << WrapName("Popup");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << rectangle;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Parent");
    objectBuilder << parentAnnotation;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Open");
    objectBuilder << opened;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObjectReference popupAnnotation = addObject(objectBuilder.takeObject());
    return popupAnnotation;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationSquare(PDFObjectReference page,
                                                              QRectF rectangle,
                                                              PDFReal borderWidth,
                                                              QColor fillColor,
                                                              QColor strokeColor,
                                                              QString title,
                                                              QString subject,
                                                              QString contents)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Annot");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subtype");
    objectBuilder << WrapName("Square");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << rectangle;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("F");
    objectBuilder << 4;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("M");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("CreationDate");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Border");
    objectBuilder << std::initializer_list<PDFReal>{ 0.0, 0.0, borderWidth };
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("C");
    objectBuilder << WrapAnnotationColor(strokeColor);
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("IC");
    objectBuilder << WrapAnnotationColor(fillColor);
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("T");
    objectBuilder << title;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Contents");
    objectBuilder << contents;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subj");
    objectBuilder << subject;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObjectReference annotationObject = addObject(objectBuilder.takeObject());
    PDFObjectReference popupAnnotation = createAnnotationPopup(page, annotationObject, getPopupWindowRect(rectangle), false);

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Popup");
    objectBuilder << popupAnnotation;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject updateAnnotationPopup = objectBuilder.takeObject();
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Annots");
    objectBuilder.beginArray();
    objectBuilder << annotationObject;
    objectBuilder << popupAnnotation;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject pageAnnots = objectBuilder.takeObject();
    mergeTo(annotationObject, updateAnnotationPopup);
    appendTo(page, pageAnnots);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationText(PDFObjectReference page,
                                                            QRectF rectangle,
                                                            TextAnnotationIcon iconType,
                                                            QString title,
                                                            QString subject,
                                                            QString contents,
                                                            bool open)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Annot");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subtype");
    objectBuilder << WrapName("Text");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << rectangle;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Name");
    objectBuilder << iconType;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("F");
    objectBuilder << 4;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("M");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("CreationDate");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("T");
    objectBuilder << title;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Contents");
    objectBuilder << contents;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subj");
    objectBuilder << subject;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Open");
    objectBuilder << open;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObjectReference annotationObject = addObject(objectBuilder.takeObject());
    PDFObjectReference popupAnnotation = createAnnotationPopup(page, annotationObject, getPopupWindowRect(rectangle), false);

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Popup");
    objectBuilder << popupAnnotation;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject updateAnnotationPopup = objectBuilder.takeObject();
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Annots");
    objectBuilder.beginArray();
    objectBuilder << annotationObject;
    objectBuilder << popupAnnotation;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject pageAnnots = objectBuilder.takeObject();
    mergeTo(annotationObject, updateAnnotationPopup);
    appendTo(page, pageAnnots);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createCatalog()
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Catalog");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Pages");
    objectBuilder << createCatalogPageTreeRoot();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObjectReference catalogReference = addObject(objectBuilder.takeObject());
    return catalogReference;
}


PDFObjectReference PDFDocumentBuilder::createCatalogPageTreeRoot()
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Pages");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Kids");
    objectBuilder << WrapEmptyArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Count");
    objectBuilder << 0;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObjectReference pageTreeRoot = addObject(objectBuilder.takeObject());
    return pageTreeRoot;
}


PDFObject PDFDocumentBuilder::createTrailerDictionary(PDFObjectReference catalog)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Size");
    objectBuilder << 1;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Root");
    objectBuilder << catalog;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Info");
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Producer");
    objectBuilder << getProducerString();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("CreationDate");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("ModDate");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject trailerDictionary = objectBuilder.takeObject();
    return trailerDictionary;
}


void PDFDocumentBuilder::updateTrailerDictionary(PDFInteger objectCount)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Size");
    objectBuilder << objectCount;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Info");
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Producer");
    objectBuilder << getProducerString();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("ModDate");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject trailerDictionary = objectBuilder.takeObject();
    m_storage.updateTrailerDictionary(qMove(trailerDictionary));
}


/* END GENERATED CODE */

}   // namespace pdf
