// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "pdfdocumentlinkoperations.h"

#include "pdfaction.h"
#include "pdfannotation.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttoolsearch.h"
#include "pdfoperationcontrol.h"

#include <QJsonObject>
#include <QRegularExpression>

#include <map>

#include "pdfdbgheap.h"

namespace pdf
{

/// Helpers of the link operations
class PDFDocumentLinkOperationsHelper
{
public:
    PDFDocumentLinkOperationsHelper() = delete;

    /// The pages of the catalog: their references (an annotation, which is a page, is never removed)
    static std::set<PDFObjectReference> getPageReferences(const PDFDocument* document)
    {
        std::set<PDFObjectReference> references;
        const PDFCatalog* catalog = document->getCatalog();
        for (size_t i = 0, count = catalog->getPageCount(); i < count; ++i)
        {
            references.insert(catalog->getPage(i)->getPageReference());
        }
        return references;
    }

    /// The array /Annots of a page dictionary, nullptr = none
    static const PDFArray* getAnnotations(const PDFObjectStorage* storage, const PDFDictionary* pageDictionary)
    {
        if (!pageDictionary)
        {
            return nullptr;
        }
        const PDFObject& annotations = storage->getObject(pageDictionary->get("Annots"));
        return annotations.isArray() ? annotations.getArray() : nullptr;
    }
};

QString PDFDocumentLinkOperations::getKindName(LinkKind kind)
{
    switch (kind)
    {
        case LinkKind::Web:
            return QStringLiteral("web");
        case LinkKind::EMail:
            return QStringLiteral("email");
        default:
            return QStringLiteral("other");
    }
}

PDFDocumentLinkOperations::LinkKind PDFDocumentLinkOperations::getLinkKind(const QByteArray& address)
{
    const QByteArray trimmed = address.trimmed().toLower();
    if (trimmed.startsWith("http:") || trimmed.startsWith("https:"))
    {
        return LinkKind::Web;
    }
    if (trimmed.startsWith("mailto:"))
    {
        return LinkKind::EMail;
    }
    return LinkKind::Other;
}

QString PDFDocumentLinkOperations::createId(PDFObjectReference reference, PDFInteger pageIndex, int annotationIndex)
{
    if (reference.isValid())
    {
        return QStringLiteral("%1:%2").arg(reference.objectNumber).arg(reference.generation);
    }
    return QStringLiteral("p%1:%2").arg(pageIndex).arg(annotationIndex);
}

bool PDFDocumentLinkOperations::parseIds(const QString& text, std::set<QString>* ids, QString* invalidItem)
{
    static const QRegularExpression idExpression(QStringLiteral("^p?\\d{1,18}:\\d{1,9}$"));

    for (QStringView item : QStringView(text).split(QLatin1Char(',')))
    {
        const QString trimmed = item.trimmed().toString();
        if (trimmed.isEmpty())
        {
            continue;
        }
        if (!idExpression.match(trimmed).hasMatch())
        {
            if (invalidItem)
            {
                *invalidItem = PDFSearchQuery::sanitize(trimmed, 32);
            }
            return false;
        }
        if (ids)
        {
            ids->insert(trimmed);
        }
    }
    return true;
}

QByteArray PDFDocumentLinkOperations::getAddress(const PDFAnnotation* annotation)
{
    const PDFLinkAnnotation* link = dynamic_cast<const PDFLinkAnnotation*>(annotation);
    if (!link)
    {
        return QByteArray();
    }

    for (const PDFAction* action : { link->getAction(), link->getURIAction() })
    {
        if (action && action->getType() == ActionType::URI)
        {
            return static_cast<const PDFActionURI*>(action)->getURI();
        }
    }
    return QByteArray();
}

QString PDFDocumentLinkOperations::createTitle(const QString& address, LinkKind kind)
{
    QString title = address;
    switch (kind)
    {
        case LinkKind::Web:
        {
            // "https://www.acme-corp.example/pricing" -> "acme-corp.example/pricing" (438:14920)
            const qsizetype schemeEnd = title.indexOf(QStringLiteral("//"));
            title = schemeEnd >= 0 ? title.mid(schemeEnd + 2) : title.mid(title.indexOf(QLatin1Char(':')) + 1);
            if (title.startsWith(QStringLiteral("www."), Qt::CaseInsensitive))
            {
                title = title.mid(4);
            }
            while (title.endsWith(QLatin1Char('/')))
            {
                title.chop(1);
            }
            break;
        }

        case LinkKind::EMail:
        {
            // "mailto:jane.novak@example.com?subject=…" -> "jane.novak@example.com" (438:14878)
            title = title.mid(title.indexOf(QLatin1Char(':')) + 1);
            const qsizetype queryStart = title.indexOf(QLatin1Char('?'));
            if (queryStart >= 0)
            {
                title.truncate(queryStart);
            }
            break;
        }

        default:
            break;
    }
    return title.isEmpty() ? address : title;
}

PDFDocumentLinkOperations::LinkList PDFDocumentLinkOperations::listExternalLinks(const PDFDocument* document, const PDFOperationControl* control)
{
    using Helper = PDFDocumentLinkOperationsHelper;

    LinkList list;
    if (!document)
    {
        return list;
    }

    const PDFObjectStorage* storage = &document->getStorage();
    const PDFCatalog* catalog = document->getCatalog();
    for (size_t pageIndex = 0, pageCount = catalog->getPageCount(); pageIndex < pageCount; ++pageIndex)
    {
        if (PDFOperationControl::isOperationCancelled(control))
        {
            break;
        }

        const PDFDictionary* pageDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(catalog->getPage(pageIndex)->getPageReference()));
        const PDFArray* annotations = Helper::getAnnotations(storage, pageDictionary);
        if (!annotations)
        {
            continue;
        }

        for (size_t i = 0, count = annotations->getCount(); i < count; ++i)
        {
            const PDFObject& item = annotations->getItem(i);
            PDFAnnotationPtr annotation = PDFAnnotation::parseItem(storage, item);
            if (!annotation || annotation->getType() != AnnotationType::Link)
            {
                continue;
            }

            if (!PDFAnnotation::isExternalLinkAnnotation(annotation.data()))
            {
                ++list.internalLinkCount;
                continue;
            }

            if (list.links.size() >= size_t(MAXIMUM_LINKS))
            {
                list.isTruncated = true;
                continue;
            }

            const QByteArray address = getAddress(annotation.data());
            ExternalLink link;
            link.pageIndex = PDFInteger(pageIndex);
            link.annotationIndex = int(i);
            link.annotationReference = item.isReference() ? item.getReference() : PDFObjectReference();
            link.id = createId(link.annotationReference, link.pageIndex, link.annotationIndex);
            link.kind = getLinkKind(address);
            link.address = PDFSearchQuery::sanitize(QString::fromUtf8(address.left(MAXIMUM_TEXT_LENGTH * 4)), MAXIMUM_TEXT_LENGTH);
            link.title = PDFSearchQuery::sanitize(annotation->getContents(), MAXIMUM_TEXT_LENGTH);
            if (link.title.isEmpty())
            {
                link.title = createTitle(link.address, link.kind);
            }
            link.rectangle = annotation->getRectangle();
            list.links.push_back(std::move(link));
        }
    }
    return list;
}

PDFDocumentLinkOperations::RemovalResult PDFDocumentLinkOperations::removeLinks(const PDFDocument* document, const std::vector<ExternalLink>& links)
{
    using Helper = PDFDocumentLinkOperationsHelper;

    RemovalResult result;
    if (!document || links.empty())
    {
        return result;
    }

    // The items of /Annots of each page, which go
    std::map<PDFInteger, std::set<int>> removedItems;
    for (const ExternalLink& link : links)
    {
        removedItems[link.pageIndex].insert(link.annotationIndex);
    }

    const std::set<PDFObjectReference> pageReferences = Helper::getPageReferences(document);
    const PDFCatalog* catalog = document->getCatalog();
    PDFDocumentBuilder builder(document);
    std::set<PDFObjectReference> nulledAnnotations;

    for (const auto& [pageIndex, indices] : removedItems)
    {
        if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
        {
            continue;
        }

        const PDFObjectReference pageReference = catalog->getPage(size_t(pageIndex))->getPageReference();
        const PDFDictionary* pageDictionary = builder.getStorage()->getDictionaryFromObject(builder.getObjectByReference(pageReference));
        const PDFArray* annotations = Helper::getAnnotations(builder.getStorage(), pageDictionary);
        if (!annotations)
        {
            continue;
        }

        PDFArrayBuilder keptItems;
        int removedCount = 0;
        for (size_t i = 0, count = annotations->getCount(); i < count; ++i)
        {
            const PDFObject& item = annotations->getItem(i);
            if (!indices.count(int(i)))
            {
                keptItems.appendItem(item);
                continue;
            }

            ++removedCount;
            if (item.isReference() && !pageReferences.count(item.getReference()))
            {
                nulledAnnotations.insert(item.getReference());
            }
        }

        if (removedCount == 0)
        {
            continue;
        }

        // The page gets its own array: an array shared by more pages stays for the others
        PDFDictionaryBuilder updatedPage(*pageDictionary);
        if (keptItems.getCount() > 0)
        {
            updatedPage.setEntry(PDFInplaceOrMemoryString("Annots"), PDFObject::createArray(std::move(keptItems)));
        }
        else
        {
            updatedPage.removeEntry("Annots");
        }
        builder.setObject(pageReference, PDFObject::createDictionary(std::move(updatedPage)));
        result.removed += removedCount;
    }

    if (result.removed == 0)
    {
        return result;
    }

    for (const PDFObjectReference& reference : nulledAnnotations)
    {
        builder.setObject(reference, PDFObject());
    }

    result.document = PDFDocumentPointer(new PDFDocument(builder.build()));
    result.flags = PDFModifiedDocument::Annotation;
    return result;
}

PDFDocumentLinkOperations::RemovalResult PDFDocumentLinkOperations::removeExternalLinkAnnotations(const PDFDocument* document)
{
    RemovalResult result;
    if (!document)
    {
        return result;
    }

    PDFDocumentModifier modifier(document);
    PDFDocumentBuilder* builder = modifier.getBuilder();
    builder->flattenPageTree();

    const PDFObjectStorage* storage = builder->getStorage();
    PDFDocumentDataLoaderDecorator loader(storage);

    std::vector<std::pair<PDFObjectReference, PDFObjectReference>> annotationsToRemove;
    const std::vector<PDFObjectReference> pageReferences = builder->getPages();

    for (const PDFObjectReference pageReference : pageReferences)
    {
        const PDFObject& pageObject = storage->getObjectByReference(pageReference);
        const PDFDictionary* pageDictionary = storage->getDictionaryFromObject(pageObject);
        if (!pageDictionary)
        {
            continue;
        }

        const std::vector<PDFObjectReference> annotationReferences = loader.readReferenceArrayFromDictionary(pageDictionary, "Annots");
        for (const PDFObjectReference& annotationReference : annotationReferences)
        {
            PDFAnnotationPtr annotation = PDFAnnotation::parse(storage, annotationReference);
            if (PDFAnnotation::isExternalLinkAnnotation(annotation.data()))
            {
                annotationsToRemove.emplace_back(pageReference, annotationReference);
            }
        }
    }

    result.removed = static_cast<int>(annotationsToRemove.size());
    if (annotationsToRemove.empty())
    {
        return result;
    }

    for (const auto& item : annotationsToRemove)
    {
        builder->removeAnnotation(item.first, item.second);
    }

    modifier.markAnnotationsChanged();
    if (modifier.finalize())
    {
        result.document = modifier.getDocument();
        result.flags = modifier.getFlags();
    }

    return result;
}

QJsonArray PDFDocumentLinkOperations::toJson(const std::vector<ExternalLink>& links)
{
    QJsonArray array;
    for (const ExternalLink& link : links)
    {
        QJsonObject object;
        object.insert(QStringLiteral("id"), link.id);
        object.insert(QStringLiteral("page"), qint64(link.pageIndex + 1));
        object.insert(QStringLiteral("index"), link.annotationIndex);
        object.insert(QStringLiteral("kind"), getKindName(link.kind));
        object.insert(QStringLiteral("title"), link.title);
        object.insert(QStringLiteral("address"), link.address);
        object.insert(QStringLiteral("rect"), QJsonArray({ link.rectangle.left(), link.rectangle.top(), link.rectangle.width(), link.rectangle.height() }));
        if (link.annotationReference.isValid())
        {
            object.insert(QStringLiteral("object"), QJsonArray({ qint64(link.annotationReference.objectNumber), qint64(link.annotationReference.generation) }));
        }
        array.append(object);
    }
    return array;
}

std::vector<PDFDocumentLinkOperations::ExternalLink> PDFDocumentLinkOperations::fromJson(const QJsonArray& array)
{
    std::vector<ExternalLink> links;
    links.reserve(size_t(qMin<qsizetype>(array.size(), MAXIMUM_LINKS)));
    for (const QJsonValue& value : array)
    {
        if (links.size() >= size_t(MAXIMUM_LINKS))
        {
            break;
        }

        const QJsonObject object = value.toObject();
        ExternalLink link;
        link.id = object.value(QStringLiteral("id")).toString();
        link.pageIndex = object.value(QStringLiteral("page")).toInteger() - 1;
        link.annotationIndex = object.value(QStringLiteral("index")).toInt(-1);
        const QString kind = object.value(QStringLiteral("kind")).toString();
        link.kind = kind == QLatin1String("web") ? LinkKind::Web : (kind == QLatin1String("email") ? LinkKind::EMail : LinkKind::Other);
        link.title = object.value(QStringLiteral("title")).toString();
        link.address = object.value(QStringLiteral("address")).toString();
        const QJsonArray rect = object.value(QStringLiteral("rect")).toArray();
        if (rect.size() == 4)
        {
            link.rectangle = QRectF(rect.at(0).toDouble(), rect.at(1).toDouble(), rect.at(2).toDouble(), rect.at(3).toDouble());
        }
        const QJsonArray reference = object.value(QStringLiteral("object")).toArray();
        if (reference.size() == 2)
        {
            link.annotationReference = PDFObjectReference(reference.at(0).toInteger(), reference.at(1).toInteger());
        }
        if (link.pageIndex >= 0 && link.annotationIndex >= 0 && !link.id.isEmpty())
        {
            links.push_back(std::move(link));
        }
    }
    return links;
}

}   // namespace pdf
