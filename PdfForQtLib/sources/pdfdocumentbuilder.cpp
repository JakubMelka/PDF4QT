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
#include "pdfdocumentreader.h"
#include "pdfvisitor.h"

#include <QBuffer>
#include <QPainter>
#include <QPdfWriter>

namespace pdf
{

class PDFCollectReferencesVisitor : public PDFAbstractVisitor
{
public:
    explicit PDFCollectReferencesVisitor(std::set<PDFObjectReference>& references) :
        m_references(references)
    {

    }

    virtual void visitArray(const PDFArray* array) override;
    virtual void visitDictionary(const PDFDictionary* dictionary) override;
    virtual void visitStream(const PDFStream* stream) override;
    virtual void visitReference(const PDFObjectReference reference) override;

private:
    std::set<PDFObjectReference>& m_references;
};

void PDFCollectReferencesVisitor::visitArray(const PDFArray* array)
{
    acceptArray(array);
}

void PDFCollectReferencesVisitor::visitDictionary(const PDFDictionary* dictionary)
{
    for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
    {
        dictionary->getValue(i).accept(this);
    }
}

void PDFCollectReferencesVisitor::visitStream(const PDFStream* stream)
{
    visitDictionary(stream->getDictionary());
}

void PDFCollectReferencesVisitor::visitReference(const PDFObjectReference reference)
{
    m_references.insert(reference);
}

class PDFReplaceReferencesVisitor : public PDFAbstractVisitor
{
public:
    explicit PDFReplaceReferencesVisitor(const std::map<PDFObjectReference, PDFObjectReference>& replacements) :
        m_replacements(replacements)
    {
        m_objectStack.reserve(32);
    }

    virtual void visitNull() override;
    virtual void visitBool(bool value) override;
    virtual void visitInt(PDFInteger value) override;
    virtual void visitReal(PDFReal value) override;
    virtual void visitString(const PDFString* string) override;
    virtual void visitName(const PDFString* name) override;
    virtual void visitArray(const PDFArray* array) override;
    virtual void visitDictionary(const PDFDictionary* dictionary) override;
    virtual void visitStream(const PDFStream* stream) override;
    virtual void visitReference(const PDFObjectReference reference) override;

    PDFObject getObject();

private:
    const std::map<PDFObjectReference, PDFObjectReference>& m_replacements;
    std::vector<PDFObject> m_objectStack;
};

void PDFReplaceReferencesVisitor::visitNull()
{
    m_objectStack.push_back(PDFObject::createNull());
}

void PDFReplaceReferencesVisitor::visitBool(bool value)
{
    m_objectStack.push_back(PDFObject::createBool(value));
}

void PDFReplaceReferencesVisitor::visitInt(PDFInteger value)
{
    m_objectStack.push_back(PDFObject::createInteger(value));
}

void PDFReplaceReferencesVisitor::visitReal(PDFReal value)
{
    m_objectStack.push_back(PDFObject::createReal(value));
}

void PDFReplaceReferencesVisitor::visitString(const PDFString* string)
{
    m_objectStack.push_back(PDFObject::createString(std::make_shared<PDFString>(*string)));
}

void PDFReplaceReferencesVisitor::visitName(const PDFString* name)
{
    m_objectStack.push_back(PDFObject::createName(std::make_shared<PDFString>(*name)));
}

void PDFReplaceReferencesVisitor::visitArray(const PDFArray* array)
{
    acceptArray(array);

    // We have all objects on the stack
    Q_ASSERT(array->getCount() <= m_objectStack.size());

    auto it = std::next(m_objectStack.cbegin(), m_objectStack.size() - array->getCount());
    std::vector<PDFObject> objects(it, m_objectStack.cend());
    PDFObject object = PDFObject::createArray(std::make_shared<PDFArray>(qMove(objects)));
    m_objectStack.erase(it, m_objectStack.cend());
    m_objectStack.push_back(object);
}

void PDFReplaceReferencesVisitor::visitDictionary(const PDFDictionary* dictionary)
{
    Q_ASSERT(dictionary);

    std::vector<PDFDictionary::DictionaryEntry> entries;
    entries.reserve(dictionary->getCount());

    for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
    {
        dictionary->getValue(i).accept(this);
        entries.emplace_back(dictionary->getKey(i), m_objectStack.back());
        m_objectStack.pop_back();
    }

    m_objectStack.push_back(PDFObject::createDictionary(std::make_shared<PDFDictionary>(qMove(entries))));
}

void PDFReplaceReferencesVisitor::visitStream(const PDFStream* stream)
{
    // Replace references in the dictionary
    visitDictionary(stream->getDictionary());
    PDFObject dictionaryObject = m_objectStack.back();
    m_objectStack.pop_back();
    m_objectStack.push_back(PDFObject::createStream(std::make_shared<PDFStream>(PDFDictionary(*dictionaryObject.getDictionary()), QByteArray(*stream->getContent()))));
}

void PDFReplaceReferencesVisitor::visitReference(const PDFObjectReference reference)
{
    m_objectStack.push_back(PDFObject::createReference(m_replacements.at(reference)));
}

PDFObject PDFReplaceReferencesVisitor::getObject()
{
    Q_ASSERT(m_objectStack.size() == 1);
    return qMove(m_objectStack.back());
}

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

PDFObjectFactory& PDFObjectFactory::operator<<(FileAttachmentIcon icon)
{
    switch (icon)
    {
        case FileAttachmentIcon::Graph:
            *this << WrapName("Graph");
            break;

        case FileAttachmentIcon::Paperclip:
            *this << WrapName("Paperclip");
            break;

        case FileAttachmentIcon::PushPin:
            *this << WrapName("PushPin");
            break;

        case FileAttachmentIcon::Tag:
            *this << WrapName("Tag");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(Stamp stamp)
{
    switch (stamp)
    {
        case Stamp::Approved:
            *this << WrapName("Approved");
            break;

        case Stamp::AsIs:
            *this << WrapName("AsIs");
            break;

        case Stamp::Confidential:
            *this << WrapName("Confidential");
            break;

        case Stamp::Departmental:
            *this << WrapName("Departmental");
            break;

        case Stamp::Draft:
            *this << WrapName("Draft");
            break;

        case Stamp::Experimental:
            *this << WrapName("Experimental");
            break;

        case Stamp::Expired:
            *this << WrapName("Expired");
            break;

        case Stamp::Final:
            *this << WrapName("Final");
            break;

        case Stamp::ForComment:
            *this << WrapName("ForComment");
            break;

        case Stamp::ForPublicRelease:
            *this << WrapName("ForPublicRelease");
            break;

        case Stamp::NotApproved:
            *this << WrapName("NotApproved");
            break;

        case Stamp::NotForPublicRelease:
            *this << WrapName("NotForPublicRelease");
            break;

        case Stamp::Sold:
            *this << WrapName("Sold");
            break;

        case Stamp::TopSecret:
            *this << WrapName("TopSecret");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(const PDFObject& object)
{
    addObject(object);
    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(AnnotationBorderStyle style)
{
    switch (style)
    {
        case AnnotationBorderStyle::Solid:
            *this << WrapName("S");
            break;

        case AnnotationBorderStyle::Dashed:
            *this << WrapName("D");
            break;

        case AnnotationBorderStyle::Beveled:
            *this << WrapName("B");
            break;

        case AnnotationBorderStyle::Inset:
            *this << WrapName("I");
            break;

        case AnnotationBorderStyle::Underline:
            *this << WrapName("U");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(const QDateTime& dateTime)
{
    addObject(PDFObject::createString(std::make_shared<PDFString>(PDFEncoding::converDateTimeToString(dateTime))));
    return *this;
}

PDFObjectFactory& PDFObjectFactory::operator<<(const QPointF& point)
{
    *this << point.x();
    *this << point.y();
    return *this;
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

void PDFDocumentBuilder::updateAnnotationAppearanceStreams(PDFObjectReference annotationReference)
{
    PDFAnnotationPtr annotation = PDFAnnotation::parse(&m_storage, annotationReference);
    if (!annotation)
    {
        return;
    }

    const PDFDictionary* pageDictionary = m_storage.getDictionaryFromObject(m_storage.getObject(annotation->getPageReference()));
    if (!pageDictionary)
    {
        return;
    }

    PDFDocumentDataLoaderDecorator loader(&m_storage);
    QRectF mediaBox = loader.readRectangle(pageDictionary->get("MediaBox"), QRectF());
    if (!mediaBox.isValid())
    {
        return;
    }

    std::vector<PDFAppeareanceStreams::Key> keys = annotation->getDrawKeys();
    std::map<PDFAppeareanceStreams::Key, PDFObjectReference> appearanceStreams;

    QRectF boundingRectangle;
    for (const PDFAppeareanceStreams::Key& key : keys)
    {
        PDFContentStreamBuilder builder(mediaBox.size(), PDFContentStreamBuilder::CoordinateSystem::PDF);

        AnnotationDrawParameters parameters;
        parameters.key = key;
        parameters.painter = builder.begin();
        annotation->draw(parameters);
        PDFContentStreamBuilder::ContentStream contentStream = builder.end(parameters.painter);

        if (!parameters.boundingRectangle.isValid() || !contentStream.pageObject.isValid())
        {
            // Invalid stream, do nothing
            continue;
        }

        boundingRectangle = boundingRectangle.united(parameters.boundingRectangle);

        std::vector<PDFObject> copiedObjects = copyFrom({ contentStream.resources, contentStream.contents }, contentStream.document.getStorage(), true);
        Q_ASSERT(copiedObjects.size() == 2);

        PDFObjectReference resourcesReference = copiedObjects[0].getReference();
        PDFObjectReference formReference = copiedObjects[1].getReference();

        // Create form object
        PDFObjectFactory formFactory;

        formFactory.beginDictionary();

        formFactory.beginDictionaryItem("Type");
        formFactory << WrapName("XObject");
        formFactory.endDictionaryItem();

        formFactory.beginDictionaryItem("Subtype");
        formFactory << WrapName("Form");
        formFactory.endDictionaryItem();

        formFactory.beginDictionaryItem("BBox");
        formFactory << parameters.boundingRectangle;
        formFactory.endDictionaryItem();

        formFactory.beginDictionaryItem("Resources");
        formFactory << resourcesReference;
        formFactory.endDictionaryItem();

        formFactory.endDictionary();

        mergeTo(formReference, formFactory.takeObject());
        appearanceStreams[key] = formReference;
    }

    if (!appearanceStreams.empty())
    {
        PDFObjectFactory asDictionaryFactory;
        asDictionaryFactory.beginDictionary();

        auto it = appearanceStreams.cbegin();
        while (it != appearanceStreams.cend())
        {
            const PDFAppeareanceStreams::Key& key = it->first;

            auto itEnd = std::next(it);
            while (itEnd != appearanceStreams.cend() && itEnd->first.first == key.first)
            {
                ++itEnd;
            }

            QByteArray name;
            switch (key.first)
            {
                case PDFAppeareanceStreams::Appearance::Normal:
                    name = "N";
                    break;
                case PDFAppeareanceStreams::Appearance::Rollover:
                    name = "R";
                    break;
                case PDFAppeareanceStreams::Appearance::Down:
                    name = "D";
                    break;

                default:
                    Q_ASSERT(false);
                    break;
            }

            asDictionaryFactory.beginDictionaryItem(name);
            const size_t streamCount = std::distance(it, itEnd);
            if (streamCount == 1)
            {
                asDictionaryFactory << it->second;
            }
            else
            {
                asDictionaryFactory.beginDictionary();
                for (; it != itEnd; ++it)
                {
                    asDictionaryFactory.beginDictionaryItem(it->first.second);
                    asDictionaryFactory << it->second;
                    asDictionaryFactory.endDictionaryItem();
                }
                asDictionaryFactory.endDictionary();
            }
            asDictionaryFactory.endDictionaryItem();

            it = itEnd;
        }

        asDictionaryFactory.endDictionary();

        PDFObjectFactory annotationFactory;
        annotationFactory.beginDictionary();

        annotationFactory.beginDictionaryItem("Rect");
        annotationFactory << boundingRectangle;
        annotationFactory.endDictionaryItem();

        annotationFactory.beginDictionaryItem("AP");
        annotationFactory << asDictionaryFactory.takeObject();
        annotationFactory.endDictionaryItem();

        annotationFactory.endDictionary();

        mergeTo(annotationReference, annotationFactory.takeObject());
    }
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

PDFObjectReference PDFDocumentBuilder::getDocumentInfo() const
{
    if (const PDFDictionary* trailerDictionary = getDictionaryFromObject(m_storage.getTrailerDictionary()))
    {
        PDFObject object = trailerDictionary->get("Info");
        if (object.isReference())
        {
            return object.getReference();
        }
    }

    return PDFObjectReference();
}

PDFObjectReference PDFDocumentBuilder::getCatalogReference() const
{
    if (const PDFDictionary* trailerDictionary = getDictionaryFromObject(m_storage.getTrailerDictionary()))
    {
        PDFObject object = trailerDictionary->get("Root");
        if (object.isReference())
        {
            return object.getReference();
        }
    }

    return PDFObjectReference();
}

void PDFDocumentBuilder::updateDocumentInfo(PDFObject info)
{
    PDFObjectReference infoReference = getDocumentInfo();
    if (!infoReference.isValid())
    {
        PDFObjectFactory objectFactory;
        objectFactory.beginDictionary();
        objectFactory.endDictionary();
        infoReference = addObject(objectFactory.takeObject());

        // Update the trailer dictionary
        objectFactory.beginDictionary();
        objectFactory.beginDictionaryItem("Info");
        objectFactory << infoReference;
        objectFactory.endDictionaryItem();
        objectFactory.endDictionary();
        m_storage.updateTrailerDictionary(objectFactory.takeObject());
    }

    mergeTo(infoReference, info);
}

QRectF PDFDocumentBuilder::getPolygonsBoundingRect(const Polygons& polygons) const
{
    QRectF rect;

    for (const QPolygonF& polygon : polygons)
    {
        rect = rect.united(polygon.boundingRect());
    }

    return rect;
}

std::vector<PDFObject> PDFDocumentBuilder::copyFrom(const std::vector<PDFObject>& objects, const PDFObjectStorage& storage, bool createReferences)
{
    // 1) Collect all references, which we must copy. If object is referenced, then
    //    we must also collect references of referenced object.
    std::set<PDFObjectReference> references;
    {
        PDFCollectReferencesVisitor collectReferencesVisitor(references);
        for (const PDFObject& object : objects)
        {
            object.accept(&collectReferencesVisitor);
        }
    }

    // Iterative algorihm, which adds additional references from referenced objects.
    // If new reference is added, then we must also check, that all referenced objects
    // from this object are added.
    std::set<PDFObjectReference> workSet = references;
    while (!workSet.empty())
    {
        std::set<PDFObjectReference> addedReferences;
        PDFCollectReferencesVisitor collectReferencesVisitor(addedReferences);
        for (const PDFObjectReference& objectReference : workSet)
        {
            storage.getObject(objectReference).accept(&collectReferencesVisitor);
        }

        workSet.clear();
        std::set_difference(addedReferences.cbegin(), addedReferences.cend(), references.cbegin(), references.cend(), std::inserter(workSet, workSet.cend()));
        references.merge(addedReferences);
    }

    // 2) Make room for new objects, together with mapping
    std::map<PDFObjectReference, PDFObjectReference> referenceMapping;
    for (const PDFObjectReference& reference : references)
    {
        referenceMapping[reference] = addObject(PDFObject::createNull());
    }

    // 3) Copy objects from other object to this one
    for (const PDFObjectReference& sourceReference : references)
    {
        const PDFObjectReference targetReference = referenceMapping.at(sourceReference);
        PDFReplaceReferencesVisitor replaceReferencesVisitor(referenceMapping);
        storage.getObject(sourceReference).accept(&replaceReferencesVisitor);
        m_storage.setObject(targetReference, replaceReferencesVisitor.getObject());
    }

    std::vector<PDFObject> result;
    result.reserve(objects.size());

    for (const PDFObject& object : objects)
    {
        if (object.isReference())
        {
            result.push_back(PDFObject::createReference(referenceMapping.at(object.getReference())));
        }
        else
        {
            PDFReplaceReferencesVisitor replaceReferencesVisitor(referenceMapping);
            object.accept(&replaceReferencesVisitor);

            if (createReferences)
            {
                result.push_back(PDFObject::createReference(addObject(replaceReferencesVisitor.getObject())));
            }
            else
            {
                result.push_back(replaceReferencesVisitor.getObject());
            }
        }
    }

    return result;
}

PDFContentStreamBuilder::PDFContentStreamBuilder(QSizeF size, CoordinateSystem coordinateSystem) :
    m_size(size),
    m_coordinateSystem(coordinateSystem),
    m_buffer(nullptr),
    m_pdfWriter(nullptr),
    m_painter(nullptr)
{

}

PDFContentStreamBuilder::~PDFContentStreamBuilder()
{
    Q_ASSERT(!m_buffer);
    Q_ASSERT(!m_pdfWriter);
    Q_ASSERT(!m_painter);
}

QPainter* PDFContentStreamBuilder::begin()
{
    Q_ASSERT(!m_buffer);
    Q_ASSERT(!m_pdfWriter);
    Q_ASSERT(!m_painter);

    m_buffer = new QBuffer();
    m_buffer->open(QBuffer::ReadWrite);

    m_pdfWriter = new QPdfWriter(m_buffer);
    m_pdfWriter->setPdfVersion(QPdfWriter::PdfVersion_1_6);
    m_pdfWriter->setPageSize(QPageSize(m_size, QPageSize::Point));
    m_pdfWriter->setResolution(72);
    m_pdfWriter->setPageMargins(QMarginsF());

    m_painter = new QPainter(m_pdfWriter);

    if (m_coordinateSystem == CoordinateSystem::PDF)
    {
        m_painter->translate(0, m_size.height());
        m_painter->scale(1.0, -1.0);
    }

    return m_painter;
}

PDFContentStreamBuilder::ContentStream PDFContentStreamBuilder::end(QPainter* painter)
{
    ContentStream result;
    Q_ASSERT(m_painter == painter);

    m_painter->end();
    delete m_painter;
    m_painter = nullptr;

    delete m_pdfWriter;
    m_pdfWriter = nullptr;

    QByteArray bufferData = m_buffer->buffer();
    m_buffer->close();
    delete m_buffer;
    m_buffer = nullptr;

    PDFDocumentReader reader(nullptr, nullptr);
    result.document = reader.readFromBuffer(bufferData);

    if (result.document.getCatalog()->getPageCount() > 0)
    {
        const PDFPage* page = result.document.getCatalog()->getPage(0);
        result.pageObject = page->getPageReference();
        result.resources = page->getResources();
        result.contents = page->getContents();
    }

    return result;
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


PDFObjectReference PDFDocumentBuilder::createAnnotationCaret(PDFObjectReference page,
                                                             QRectF rectangle,
                                                             PDFReal borderWidth,
                                                             QColor color,
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
    objectBuilder << WrapName("Caret");
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
    objectBuilder << WrapAnnotationColor(color);
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
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Annots");
    objectBuilder.beginArray();
    objectBuilder << annotationObject;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject pageAnnots = objectBuilder.takeObject();
    appendTo(page, pageAnnots);
    updateAnnotationAppearanceStreams(annotationObject);
    return annotationObject;
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
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Annots");
    objectBuilder.beginArray();
    objectBuilder << annotationObject;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject pageAnnots = objectBuilder.takeObject();
    appendTo(page, pageAnnots);
    updateAnnotationAppearanceStreams(annotationObject);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationFileAttachment(PDFObjectReference page,
                                                                      QPointF position,
                                                                      PDFObjectReference fileSpecification,
                                                                      FileAttachmentIcon icon,
                                                                      QString title,
                                                                      QString description)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Annot");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subtype");
    objectBuilder << WrapName("FileAttachment");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << QRectF(position, QSizeF(32, 32));
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("FS");
    objectBuilder << fileSpecification;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Name");
    objectBuilder << icon;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("T");
    objectBuilder << title;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Contents");
    objectBuilder << description;
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
    updateAnnotationAppearanceStreams(annotationReference);
    return annotationReference;
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
    updateAnnotationAppearanceStreams(annotationObject);
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
    objectBuilder << startPoint;
    objectBuilder << endPoint;
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
    updateAnnotationAppearanceStreams(annotationObject);
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
                                                                QPointF kneePoint,
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
    objectBuilder << startPoint;
    objectBuilder << kneePoint;
    objectBuilder << endPoint;
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
    updateAnnotationAppearanceStreams(annotationObject);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationHighlight(PDFObjectReference page,
                                                                 QRectF rectangle,
                                                                 QColor color,
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
    objectBuilder << WrapName("Highlight");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << rectangle;
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
    objectBuilder.beginDictionaryItem("C");
    objectBuilder << color;
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
    objectBuilder.beginDictionaryItem("QuadPoints");
    objectBuilder.beginArray();
    objectBuilder << rectangle.bottomLeft();
    objectBuilder << rectangle.bottomRight();
    objectBuilder << rectangle.topLeft();
    objectBuilder << rectangle.topRight();
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
    updateAnnotationAppearanceStreams(annotationObject);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationHighlight(PDFObjectReference page,
                                                                 QRectF rectangle,
                                                                 QColor color)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Annot");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subtype");
    objectBuilder << WrapName("Highlight");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << rectangle;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("CreationDate");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("C");
    objectBuilder << color;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("QuadPoints");
    objectBuilder.beginArray();
    objectBuilder << rectangle.bottomLeft();
    objectBuilder << rectangle.bottomRight();
    objectBuilder << rectangle.topLeft();
    objectBuilder << rectangle.topRight();
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
    updateAnnotationAppearanceStreams(annotationObject);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationInk(PDFObjectReference page,
                                                           QPolygonF inkPoints,
                                                           PDFReal borderWidth,
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
    objectBuilder << WrapName("Ink");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << inkPoints.boundingRect();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("F");
    objectBuilder << 4;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("InkList");
    objectBuilder.beginArray();
    objectBuilder << inkPoints;
    objectBuilder.endArray();
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
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Annots");
    objectBuilder.beginArray();
    objectBuilder << annotationObject;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject pageAnnots = objectBuilder.takeObject();
    appendTo(page, pageAnnots);
    updateAnnotationAppearanceStreams(annotationObject);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationInk(PDFObjectReference page,
                                                           Polygons inkPoints,
                                                           PDFReal borderWidth,
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
    objectBuilder << WrapName("Ink");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << getPolygonsBoundingRect(inkPoints);
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("F");
    objectBuilder << 4;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("InkList");
    objectBuilder << inkPoints;
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
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Annots");
    objectBuilder.beginArray();
    objectBuilder << annotationObject;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject pageAnnots = objectBuilder.takeObject();
    appendTo(page, pageAnnots);
    updateAnnotationAppearanceStreams(annotationObject);
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
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Annots");
    objectBuilder.beginArray();
    objectBuilder << annotationObject;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject pageAnnots = objectBuilder.takeObject();
    appendTo(page, pageAnnots);
    updateAnnotationAppearanceStreams(annotationObject);
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
                                                            AnnotationLineEnding endLineType,
                                                            PDFReal leaderLineLength,
                                                            PDFReal leaderLineOffset,
                                                            PDFReal leaderLineExtension,
                                                            bool displayContents,
                                                            bool displayedContentsTopAlign)
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
    objectBuilder.beginDictionaryItem("LL");
    objectBuilder << leaderLineLength;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("LLO");
    objectBuilder << leaderLineOffset;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("LLE");
    objectBuilder << leaderLineExtension;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Cap");
    objectBuilder << displayContents;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("CP");
    objectBuilder << (displayedContentsTopAlign ? WrapName("Top") : WrapName("Inline"));
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
    updateAnnotationAppearanceStreams(annotationObject);
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
    updateAnnotationAppearanceStreams(annotationReference);
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


PDFObjectReference PDFDocumentBuilder::createAnnotationPolygon(PDFObjectReference page,
                                                               QPolygonF polygon,
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
    objectBuilder << WrapName("Polygon");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << polygon.boundingRect();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("F");
    objectBuilder << 4;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Vertices");
    objectBuilder << polygon;
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
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Annots");
    objectBuilder.beginArray();
    objectBuilder << annotationObject;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject pageAnnots = objectBuilder.takeObject();
    appendTo(page, pageAnnots);
    updateAnnotationAppearanceStreams(annotationObject);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationPolyline(PDFObjectReference page,
                                                                QPolygonF polyline,
                                                                PDFReal borderWidth,
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
    objectBuilder << WrapName("PolyLine");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << polyline.boundingRect();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("F");
    objectBuilder << 4;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Vertices");
    objectBuilder << polyline;
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
    objectBuilder.beginDictionaryItem("");
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
    updateAnnotationAppearanceStreams(annotationObject);
    return annotationObject;
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
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Popup");
    objectBuilder << popupAnnotation;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject upgradedParentAnnotation = objectBuilder.takeObject();
    mergeTo(parentAnnotation, upgradedParentAnnotation);
    updateAnnotationAppearanceStreams(popupAnnotation);
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
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Annots");
    objectBuilder.beginArray();
    objectBuilder << annotationObject;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject pageAnnots = objectBuilder.takeObject();
    appendTo(page, pageAnnots);
    updateAnnotationAppearanceStreams(annotationObject);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationSquiggly(PDFObjectReference page,
                                                                QRectF rectangle,
                                                                QColor color,
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
    objectBuilder << WrapName("Squiggly");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << rectangle;
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
    objectBuilder.beginDictionaryItem("C");
    objectBuilder << color;
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
    objectBuilder.beginDictionaryItem("QuadPoints");
    objectBuilder.beginArray();
    objectBuilder << rectangle.bottomLeft();
    objectBuilder << rectangle.bottomRight();
    objectBuilder << rectangle.topLeft();
    objectBuilder << rectangle.topRight();
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
    updateAnnotationAppearanceStreams(annotationObject);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationSquiggly(PDFObjectReference page,
                                                                QRectF rectangle,
                                                                QColor color)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Annot");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subtype");
    objectBuilder << WrapName("Squiggly");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << rectangle;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("CreationDate");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("C");
    objectBuilder << color;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("QuadPoints");
    objectBuilder.beginArray();
    objectBuilder << rectangle.bottomLeft();
    objectBuilder << rectangle.bottomRight();
    objectBuilder << rectangle.topLeft();
    objectBuilder << rectangle.topRight();
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
    updateAnnotationAppearanceStreams(annotationObject);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationStamp(PDFObjectReference page,
                                                             QRectF rectangle,
                                                             Stamp stampType,
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
    objectBuilder << WrapName("Stamp");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << rectangle;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Name");
    objectBuilder << stampType;
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
    updateAnnotationAppearanceStreams(annotationObject);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationStrikeout(PDFObjectReference page,
                                                                 QRectF rectangle,
                                                                 QColor color,
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
    objectBuilder << WrapName("StrikeOut");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << rectangle;
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
    objectBuilder.beginDictionaryItem("C");
    objectBuilder << color;
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
    objectBuilder.beginDictionaryItem("QuadPoints");
    objectBuilder.beginArray();
    objectBuilder << rectangle.bottomLeft();
    objectBuilder << rectangle.bottomRight();
    objectBuilder << rectangle.topLeft();
    objectBuilder << rectangle.topRight();
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
    updateAnnotationAppearanceStreams(annotationObject);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationStrikeout(PDFObjectReference page,
                                                                 QRectF rectangle,
                                                                 QColor color)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Annot");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subtype");
    objectBuilder << WrapName("StrikeOut");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << rectangle;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("CreationDate");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("C");
    objectBuilder << color;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("QuadPoints");
    objectBuilder.beginArray();
    objectBuilder << rectangle.bottomLeft();
    objectBuilder << rectangle.bottomRight();
    objectBuilder << rectangle.topLeft();
    objectBuilder << rectangle.topRight();
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
    updateAnnotationAppearanceStreams(annotationObject);
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
    updateAnnotationAppearanceStreams(annotationObject);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationUnderline(PDFObjectReference page,
                                                                 QRectF rectangle,
                                                                 QColor color)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Type");
    objectBuilder << WrapName("Annot");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Subtype");
    objectBuilder << WrapName("Underline");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << rectangle;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("P");
    objectBuilder << page;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("CreationDate");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("C");
    objectBuilder << color;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("QuadPoints");
    objectBuilder.beginArray();
    objectBuilder << rectangle.bottomLeft();
    objectBuilder << rectangle.bottomRight();
    objectBuilder << rectangle.topLeft();
    objectBuilder << rectangle.topRight();
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
    updateAnnotationAppearanceStreams(annotationObject);
    return annotationObject;
}


PDFObjectReference PDFDocumentBuilder::createAnnotationUnderline(PDFObjectReference page,
                                                                 QRectF rectangle,
                                                                 QColor color,
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
    objectBuilder << WrapName("Underline");
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Rect");
    objectBuilder << rectangle;
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
    objectBuilder.beginDictionaryItem("C");
    objectBuilder << color;
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
    objectBuilder.beginDictionaryItem("QuadPoints");
    objectBuilder.beginArray();
    objectBuilder << rectangle.bottomLeft();
    objectBuilder << rectangle.bottomRight();
    objectBuilder << rectangle.topLeft();
    objectBuilder << rectangle.topRight();
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
    updateAnnotationAppearanceStreams(annotationObject);
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
    PDFObjectReference infoDictionary = addObject(objectBuilder.takeObject());
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Size");
    objectBuilder << 1;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Root");
    objectBuilder << catalog;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("Info");
    objectBuilder << infoDictionary;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject trailerDictionary = objectBuilder.takeObject();
    return trailerDictionary;
}


void PDFDocumentBuilder::setAnnotationBorder(PDFObjectReference annotation,
                                             PDFReal hRadius,
                                             PDFReal vRadius,
                                             PDFReal width)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Border");
    objectBuilder.beginArray();
    objectBuilder << hRadius;
    objectBuilder << vRadius;
    objectBuilder << width;
    objectBuilder.endArray();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject annotationObject = objectBuilder.takeObject();
    mergeTo(annotation, annotationObject);
}


void PDFDocumentBuilder::setAnnotationBorderStyle(PDFObjectReference annotation,
                                                  AnnotationBorderStyle style,
                                                  PDFReal width)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("BS");
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("W");
    objectBuilder << width;
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("S");
    objectBuilder << style;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject annotationObject = objectBuilder.takeObject();
    mergeTo(annotation, annotationObject);
}


void PDFDocumentBuilder::setAnnotationColor(PDFObjectReference annotation,
                                            QColor color)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("C");
    objectBuilder << WrapAnnotationColor(color);
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject annotationObject = objectBuilder.takeObject();
    mergeTo(annotation, annotationObject);
}


void PDFDocumentBuilder::setAnnotationContents(PDFObjectReference annotation,
                                               QString contents)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Contents");
    objectBuilder << contents;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject annotationObject = objectBuilder.takeObject();
    mergeTo(annotation, annotationObject);
}


void PDFDocumentBuilder::setAnnotationOpacity(PDFObjectReference annotation,
                                              PDFReal opacity)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("CA");
    objectBuilder << opacity;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject annotationObject = objectBuilder.takeObject();
    mergeTo(annotation, annotationObject);
}


void PDFDocumentBuilder::setAnnotationOpenState(PDFObjectReference annotation,
                                                bool isOpen)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Open");
    objectBuilder << isOpen;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject annotationObject = objectBuilder.takeObject();
    mergeTo(annotation, annotationObject);
}


void PDFDocumentBuilder::setAnnotationQuadPoints(PDFObjectReference annotation,
                                                 QPolygonF quadrilaterals)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("QuadPoints");
    objectBuilder << quadrilaterals;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject annotationObject = objectBuilder.takeObject();
    mergeTo(annotation, annotationObject);
}


void PDFDocumentBuilder::setAnnotationRichText(PDFObjectReference annotation,
                                               QString richText)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("RC");
    objectBuilder << richText;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject annotationObject = objectBuilder.takeObject();
    mergeTo(annotation, annotationObject);
}


void PDFDocumentBuilder::setAnnotationSubject(PDFObjectReference annotation,
                                              QString subject)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Subj");
    objectBuilder << subject;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject annotationObject = objectBuilder.takeObject();
    mergeTo(annotation, annotationObject);
}


void PDFDocumentBuilder::setAnnotationTitle(PDFObjectReference annotation,
                                            QString title)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("T");
    objectBuilder << title;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject annotationObject = objectBuilder.takeObject();
    mergeTo(annotation, annotationObject);
}


void PDFDocumentBuilder::setDocumentAuthor(QString author)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Author");
    objectBuilder << author;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject info = objectBuilder.takeObject();
    updateDocumentInfo(qMove(info));
}


void PDFDocumentBuilder::setDocumentCreationDate(QDateTime creationDate)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("CreationDate");
    objectBuilder << creationDate;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject info = objectBuilder.takeObject();
    updateDocumentInfo(qMove(info));
}


void PDFDocumentBuilder::setDocumentCreator(QString creator)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Creator");
    objectBuilder << creator;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject info = objectBuilder.takeObject();
    updateDocumentInfo(qMove(info));
}


void PDFDocumentBuilder::setDocumentKeywords(QString keywords)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Keywords");
    objectBuilder << keywords;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject info = objectBuilder.takeObject();
    updateDocumentInfo(qMove(info));
}


void PDFDocumentBuilder::setDocumentProducer(QString producer)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Producer");
    objectBuilder << producer;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject info = objectBuilder.takeObject();
    updateDocumentInfo(qMove(info));
}


void PDFDocumentBuilder::setDocumentSubject(QString subject)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Subject");
    objectBuilder << subject;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject info = objectBuilder.takeObject();
    updateDocumentInfo(qMove(info));
}


void PDFDocumentBuilder::setDocumentTitle(QString title)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Title");
    objectBuilder << title;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject info = objectBuilder.takeObject();
    updateDocumentInfo(qMove(info));
}


void PDFDocumentBuilder::setLanguage(QLocale locale)
{
    PDFObjectFactory objectBuilder;

    setLanguage(locale.name());
}


void PDFDocumentBuilder::setLanguage(QString language)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Lang");
    objectBuilder << language;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject updatedCatalog = objectBuilder.takeObject();
    mergeTo(getCatalogReference(), updatedCatalog);
}


void PDFDocumentBuilder::updateTrailerDictionary(PDFInteger objectCount)
{
    PDFObjectFactory objectBuilder;

    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Size");
    objectBuilder << objectCount;
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject trailerDictionary = objectBuilder.takeObject();
    objectBuilder.beginDictionary();
    objectBuilder.beginDictionaryItem("Producer");
    objectBuilder << getProducerString();
    objectBuilder.endDictionaryItem();
    objectBuilder.beginDictionaryItem("ModDate");
    objectBuilder << WrapCurrentDateTime();
    objectBuilder.endDictionaryItem();
    objectBuilder.endDictionary();
    PDFObject updatedInfoDictionary = objectBuilder.takeObject();
    m_storage.updateTrailerDictionary(qMove(trailerDictionary));
    updateDocumentInfo(qMove(updatedInfoDictionary));
}


/* END GENERATED CODE */

}   // namespace pdf
