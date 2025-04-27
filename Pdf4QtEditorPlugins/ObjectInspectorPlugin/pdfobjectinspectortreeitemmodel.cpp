// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pdfobjectinspectortreeitemmodel.h"
#include "pdfdocument.h"
#include "pdfvisitor.h"
#include "pdfencoding.h"

#include <stack>

#include <QLocale>

namespace pdfplugin
{

class PDFObjectInspectorTreeItem : public pdf::PDFTreeItem
{
public:

    inline explicit PDFObjectInspectorTreeItem() = default;
    inline explicit PDFObjectInspectorTreeItem(PDFObjectInspectorTreeItem* parent) : pdf::PDFTreeItem(parent) { }
    inline explicit PDFObjectInspectorTreeItem(pdf::PDFObject object, PDFObjectInspectorTreeItem* parent) : pdf::PDFTreeItem(parent), m_object(std::move(object)) { }
    inline explicit PDFObjectInspectorTreeItem(QByteArray dictionaryKey, pdf::PDFObject object, PDFObjectInspectorTreeItem* parent) : pdf::PDFTreeItem(parent), m_dictionaryKey(std::move(dictionaryKey)), m_object(std::move(object)) { }
    inline explicit PDFObjectInspectorTreeItem(pdf::PDFObjectReference reference, pdf::PDFObject object, PDFObjectInspectorTreeItem* parent) : pdf::PDFTreeItem(parent), m_reference(std::move(reference)), m_object(std::move(object)) { }

    virtual ~PDFObjectInspectorTreeItem() override { }


    QByteArray getDictionaryKey() const;
    void setDictionaryKey(const QByteArray& dictionaryKey);

    pdf::PDFObjectReference getReference() const;
    void setReference(const pdf::PDFObjectReference& reference);

    const pdf::PDFObject& getObject() const;
    void setObject(const pdf::PDFObject& object);

private:
    QByteArray m_dictionaryKey;
    pdf::PDFObjectReference m_reference;
    pdf::PDFObject m_object;
};

QByteArray PDFObjectInspectorTreeItem::getDictionaryKey() const
{
    return m_dictionaryKey;
}

void PDFObjectInspectorTreeItem::setDictionaryKey(const QByteArray& dictionaryKey)
{
    m_dictionaryKey = dictionaryKey;
}

pdf::PDFObjectReference PDFObjectInspectorTreeItem::getReference() const
{
    return m_reference;
}

void PDFObjectInspectorTreeItem::setReference(const pdf::PDFObjectReference& reference)
{
    m_reference = reference;
}

const pdf::PDFObject& PDFObjectInspectorTreeItem::getObject() const
{
    return m_object;
}

void PDFObjectInspectorTreeItem::setObject(const pdf::PDFObject& object)
{
    m_object = object;
}

PDFObjectInspectorTreeItemModel::PDFObjectInspectorTreeItemModel(const pdf::PDFObjectClassifier* classifier, QObject* parent) :
    pdf::PDFTreeItemModel(parent),
    m_classifier(classifier)
{
}

QVariant PDFObjectInspectorTreeItemModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    Q_UNUSED(section);
    Q_UNUSED(orientation);
    Q_UNUSED(role);

    return QVariant();
}

int PDFObjectInspectorTreeItemModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);

    return 1;
}

QVariant PDFObjectInspectorTreeItemModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    if (role != Qt::DisplayRole)
    {
        return QVariant();
    }

    const PDFObjectInspectorTreeItem* item = static_cast<const PDFObjectInspectorTreeItem*>(index.internalPointer());
    const PDFObjectInspectorTreeItem* parent = static_cast<const PDFObjectInspectorTreeItem*>(index.parent().internalPointer());

    QStringList data;
    if (item->getReference().isValid() && (!parent || (parent && !parent->getReference().isValid())))
    {
        data << QString("%1 %2 R").arg(item->getReference().objectNumber).arg(item->getReference().generation);
    }

    QByteArray dictionaryKey = item->getDictionaryKey();
    if (!dictionaryKey.isEmpty())
    {
        data << QString("/%1").arg(QString::fromLatin1(dictionaryKey.toPercentEncoding()));
    }

    QLocale locale;

    const pdf::PDFObject& object = item->getObject();
    switch (object.getType())
    {
        case pdf::PDFObject::Type::Null:
            data << tr("null");
            break;

        case pdf::PDFObject::Type::Bool:
            data << (object.getBool() ? tr("true") : tr("false"));
            break;

        case pdf::PDFObject::Type::Int:
            data << locale.toString(object.getInteger());
            break;

        case pdf::PDFObject::Type::Real:
            data << locale.toString(object.getReal());
            break;

        case pdf::PDFObject::Type::String:
            data << QString("\"%1\"").arg(pdf::PDFEncoding::convertSmartFromByteStringToRepresentableQString(object.getString()));
            break;

        case pdf::PDFObject::Type::Name:
            data << QString("/%1").arg(QString::fromLatin1(object.getString().toPercentEncoding()));
            break;

        case pdf::PDFObject::Type::Array:
            data << tr("Array [%1 items]").arg(locale.toString(object.getArray()->getCount()));
            break;

        case pdf::PDFObject::Type::Dictionary:
            data << tr("Dictionary [%1 items]").arg(locale.toString(object.getDictionary()->getCount()));
            break;

        case pdf::PDFObject::Type::Stream:
            data << tr("Stream [%1 items, %2 data bytes]").arg(locale.toString(object.getStream()->getDictionary()->getCount()), locale.toString(object.getStream()->getContent()->size()));
            break;

        case pdf::PDFObject::Type::Reference:
            data << QString("%1 %2 R").arg(object.getReference().objectNumber).arg(object.getReference().generation);
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    return data.join(" ");
}

void PDFObjectInspectorTreeItemModel::update()
{
    beginResetModel();

    m_rootItem.reset();

    if (m_document)
    {
        std::set<pdf::PDFObjectReference> usedReferences;

        auto createObjectsFromClassifier = [this, &usedReferences](pdf::PDFObjectClassifier::Type type)
        {
            m_rootItem.reset(new PDFObjectInspectorTreeItem());

            for (pdf::PDFObjectReference reference : m_classifier->getObjectsByType(type))
            {
                pdf::PDFObject object = m_document->getStorage().getObjectByReference(reference);
                createObjectItem(getRootItem(), reference, object, true, usedReferences);
            }
        };

        switch (m_mode)
        {
            case pdfplugin::PDFObjectInspectorTreeItemModel::Document:
            {
                m_rootItem.reset(new PDFObjectInspectorTreeItem());
                const pdf::PDFObjectStorage& storage = m_document->getStorage();
                createObjectItem(getRootItem(), pdf::PDFObjectReference(), storage.getTrailerDictionary(), true, usedReferences);
                break;
            }

            case pdfplugin::PDFObjectInspectorTreeItemModel::Page:
            {
                m_rootItem.reset(new PDFObjectInspectorTreeItem());

                const size_t pageCount = m_document->getCatalog()->getPageCount();
                for (size_t i = 0; i < pageCount; ++i)
                {
                    if (const pdf::PDFPage* page = m_document->getCatalog()->getPage(i))
                    {
                        pdf::PDFObjectReference reference = page->getPageReference();
                        pdf::PDFObject object = m_document->getStorage().getObjectByReference(reference);
                        createObjectItem(getRootItem(), reference, object, true, usedReferences);
                    }
                }

                break;
            }

            case ContentStream:
                createObjectsFromClassifier(pdf::PDFObjectClassifier::ContentStream);
                break;

            case GraphicState:
                createObjectsFromClassifier(pdf::PDFObjectClassifier::GraphicState);
                break;

            case ColorSpace:
                createObjectsFromClassifier(pdf::PDFObjectClassifier::ColorSpace);
                break;

            case Pattern:
                createObjectsFromClassifier(pdf::PDFObjectClassifier::Pattern);
                break;

            case Shading:
                createObjectsFromClassifier(pdf::PDFObjectClassifier::Shading);
                break;

            case Image:
                createObjectsFromClassifier(pdf::PDFObjectClassifier::Image);
                break;

            case Form:
                createObjectsFromClassifier(pdf::PDFObjectClassifier::Form);
                break;

            case Font:
                createObjectsFromClassifier(pdf::PDFObjectClassifier::Font);
                break;

            case Action:
                createObjectsFromClassifier(pdf::PDFObjectClassifier::Action);
                break;

            case Annotation:
                createObjectsFromClassifier(pdf::PDFObjectClassifier::Annotation);
                break;

            case pdfplugin::PDFObjectInspectorTreeItemModel::List:
            {
                m_rootItem.reset(new PDFObjectInspectorTreeItem());

                const pdf::PDFObjectStorage& storage = m_document->getStorage();
                createObjectItem(getRootItem(), pdf::PDFObjectReference(), storage.getTrailerDictionary(), false, usedReferences);
                const pdf::PDFObjectStorage::PDFObjects& objects = storage.getObjects();
                for (size_t i = 0; i < objects.size(); ++i)
                {
                    pdf::PDFObjectReference reference(i, objects[i].generation);
                    pdf::PDFObject object = objects[i].object;

                    if (object.isNull())
                    {
                        // We skip null objects
                        continue;
                    }

                    createObjectItem(getRootItem(), reference, object, false, usedReferences);
                }

                break;
            }

            default:
                Q_ASSERT(false);
                break;
        }
    }

    endResetModel();
}

void PDFObjectInspectorTreeItemModel::setMode(Mode mode)
{
    if (m_mode != mode)
    {
        m_mode = mode;
        update();
    }
}

pdf::PDFObject PDFObjectInspectorTreeItemModel::getObjectFromIndex(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return pdf::PDFObject();
    }

    const PDFObjectInspectorTreeItem* item = static_cast<const PDFObjectInspectorTreeItem*>(index.internalPointer());
    return item->getObject();
}

pdf::PDFObjectReference PDFObjectInspectorTreeItemModel::getObjectReferenceFromIndex(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return pdf::PDFObjectReference();
    }

    const PDFObjectInspectorTreeItem* item = static_cast<const PDFObjectInspectorTreeItem*>(index.internalPointer());
    return item->getReference();
}

bool PDFObjectInspectorTreeItemModel::isRootObject(const QModelIndex& index) const
{
    return index.isValid() && !index.parent().isValid();
}

class PDFCreateObjectInspectorTreeItemFromObjectVisitor : public pdf::PDFAbstractVisitor
{
public:
    explicit PDFCreateObjectInspectorTreeItemFromObjectVisitor(std::set<pdf::PDFObjectReference>* usedReferences,
                                                               const pdf::PDFDocument* document,
                                                               bool followReferences,
                                                               pdf::PDFObjectReference reference,
                                                               PDFObjectInspectorTreeItem* parent) :
        m_usedReferences(usedReferences),
        m_document(document),
        m_followReferences(followReferences),
        m_reference(reference)
    {
        m_parents.push(parent);
    }

    virtual ~PDFCreateObjectInspectorTreeItemFromObjectVisitor() override
    {
        m_parents.pop();
        Q_ASSERT(m_parents.empty());
    }

    virtual void visitNull() override;
    virtual void visitBool(bool value) override;
    virtual void visitInt(pdf::PDFInteger value) override;
    virtual void visitReal(pdf::PDFReal value) override;
    virtual void visitString(pdf::PDFStringRef string) override;
    virtual void visitName(pdf::PDFStringRef name) override;
    virtual void visitArray(const pdf::PDFArray* array) override;
    virtual void visitDictionary(const pdf::PDFDictionary* dictionary) override;
    virtual void visitStream(const pdf::PDFStream* stream) override;
    virtual void visitReference(const pdf::PDFObjectReference reference) override;

private:
    std::set<pdf::PDFObjectReference>* m_usedReferences;
    const pdf::PDFDocument* m_document;
    bool m_followReferences;
    pdf::PDFObjectReference m_reference;
    std::stack<PDFObjectInspectorTreeItem*> m_parents;
};

void PDFCreateObjectInspectorTreeItemFromObjectVisitor::visitNull()
{
    m_parents.top()->addCreatedChild(new PDFObjectInspectorTreeItem(m_reference, pdf::PDFObject::createNull(), m_parents.top()));
}

void PDFCreateObjectInspectorTreeItemFromObjectVisitor::visitBool(bool value)
{
    m_parents.top()->addCreatedChild(new PDFObjectInspectorTreeItem(m_reference, pdf::PDFObject::createBool(value), m_parents.top()));
}

void PDFCreateObjectInspectorTreeItemFromObjectVisitor::visitInt(pdf::PDFInteger value)
{
    m_parents.top()->addCreatedChild(new PDFObjectInspectorTreeItem(m_reference, pdf::PDFObject::createInteger(value), m_parents.top()));
}

void PDFCreateObjectInspectorTreeItemFromObjectVisitor::visitReal(pdf::PDFReal value)
{
    m_parents.top()->addCreatedChild(new PDFObjectInspectorTreeItem(m_reference, pdf::PDFObject::createReal(value), m_parents.top()));
}

void PDFCreateObjectInspectorTreeItemFromObjectVisitor::visitString(pdf::PDFStringRef string)
{
    m_parents.top()->addCreatedChild(new PDFObjectInspectorTreeItem(m_reference, pdf::PDFObject::createString(string.getString()), m_parents.top()));
}

void PDFCreateObjectInspectorTreeItemFromObjectVisitor::visitName(pdf::PDFStringRef name)
{
    m_parents.top()->addCreatedChild(new PDFObjectInspectorTreeItem(m_reference, pdf::PDFObject::createName(name), m_parents.top()));
}

void PDFCreateObjectInspectorTreeItemFromObjectVisitor::visitArray(const pdf::PDFArray* array)
{
    PDFObjectInspectorTreeItem* arrayRoot = new PDFObjectInspectorTreeItem(m_reference, pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(*array)), m_parents.top());
    m_parents.top()->addCreatedChild(arrayRoot);
    m_parents.push(arrayRoot);
    acceptArray(array);
    m_parents.pop();
}

void PDFCreateObjectInspectorTreeItemFromObjectVisitor::visitDictionary(const pdf::PDFDictionary* dictionary)
{
    PDFObjectInspectorTreeItem* dictionaryRoot = new PDFObjectInspectorTreeItem(m_reference, pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(*dictionary)), m_parents.top());
    m_parents.top()->addCreatedChild(dictionaryRoot);
    m_parents.push(dictionaryRoot);

    acceptDictionary(dictionary);

    Q_ASSERT(dictionaryRoot->getChildCount() == dictionary->getCount());
    for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
    {
        PDFObjectInspectorTreeItem* child = static_cast<PDFObjectInspectorTreeItem*>(dictionaryRoot->getChild(int(i)));
        child->setDictionaryKey(dictionary->getKey(i).getString());
    }

    m_parents.pop();
}

void PDFCreateObjectInspectorTreeItemFromObjectVisitor::visitStream(const pdf::PDFStream* stream)
{
    PDFObjectInspectorTreeItem* streamRoot = new PDFObjectInspectorTreeItem(m_reference, pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(*stream)), m_parents.top());
    m_parents.top()->addCreatedChild(streamRoot);
    m_parents.push(streamRoot);

    const pdf::PDFDictionary* dictionary = stream->getDictionary();
    acceptDictionary(dictionary);

    Q_ASSERT(streamRoot->getChildCount() == dictionary->getCount());
    for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
    {
        PDFObjectInspectorTreeItem* child = static_cast<PDFObjectInspectorTreeItem*>(streamRoot->getChild(int(i)));
        child->setDictionaryKey(dictionary->getKey(i).getString());
    }

    m_parents.pop();
}

void PDFCreateObjectInspectorTreeItemFromObjectVisitor::visitReference(const pdf::PDFObjectReference reference)
{
    PDFObjectInspectorTreeItem* referenceRoot = new PDFObjectInspectorTreeItem(m_reference, pdf::PDFObject::createReference(reference), m_parents.top());
    m_parents.top()->addCreatedChild(referenceRoot);

    if (m_followReferences && reference.isValid())
    {
        Q_ASSERT(m_usedReferences);

        if (m_usedReferences->count(reference))
        {
            // Reference already followed
            return;
        }

        m_usedReferences->insert(reference);

        m_parents.push(referenceRoot);
        const pdf::PDFObject& object = m_document->getObjectByReference(reference);
        object.accept(this);
        m_parents.pop();
    }
}

void PDFObjectInspectorTreeItemModel::createObjectItem(PDFObjectInspectorTreeItem* parent,
                                                       pdf::PDFObjectReference reference,
                                                       pdf::PDFObject object,
                                                       bool followRef,
                                                       std::set<pdf::PDFObjectReference>& usedReferences) const
{
    PDFCreateObjectInspectorTreeItemFromObjectVisitor visitor(&usedReferences, m_document, followRef, reference, parent);
    object.accept(&visitor);
}

PDFObjectInspectorTreeItem* PDFObjectInspectorTreeItemModel::getRootItem()
{
    return static_cast<PDFObjectInspectorTreeItem*>(m_rootItem.get());
}

}   // namespace pdfplugin
