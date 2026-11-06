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

#include "pdfdocumenthiddendatascan.h"
#include "pdfdocumenttoolsearch.h"

#include "pdfannotation.h"
#include "pdfcatalog.h"
#include "pdfexception.h"
#include "pdfform.h"
#include "pdfobjectutils.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfoperationcontrol.h"
#include "pdfoutline.h"
#include "pdfprogress.h"

#include <QJsonArray>
#include <QLocale>

#include <algorithm>
#include <map>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

/// The walk of the scan
class PDFDocumentHiddenDataScanHelper
{
public:
    PDFDocumentHiddenDataScanHelper() = delete;

    using Kind = PDFDocumentHiddenDataScan::Kind;
    using Result = PDFDocumentHiddenDataScan::Result;
    using KindResult = PDFDocumentHiddenDataScan::KindResult;

    /// Steps of the walks between the checks of the cancellation
    static constexpr int CANCEL_CHECK_STEP = 4096;

    /// Every object reachable from the trailer, each once: the metadata streams and the
    /// embedded files (the keys, which the sanitizer removes)
    static void scanObjects(const PDFObjectStorage* storage, const PDFOperationControl* control, Result* result)
    {
        PDFObjectWalk walk;
        std::set<PDFObjectReference> embeddedFiles;
        std::vector<PDFObject> stack;
        stack.push_back(storage->getTrailerDictionary());

        int steps = 0;
        while (!stack.empty())
        {
            if (++steps % CANCEL_CHECK_STEP == 0 && PDFOperationControl::isOperationCancelled(control))
            {
                result->isCanceled = true;
                return;
            }

            const PDFObject object = std::move(stack.back());
            stack.pop_back();

            switch (object.getType())
            {
                case PDFObject::Type::Reference:
                {
                    // The stack has no depth: the walk enters each object once
                    if (walk.enter(object, 0))
                    {
                        stack.push_back(storage->getObjectByReference(object.getReference()));
                    }
                    break;
                }

                case PDFObject::Type::Array:
                {
                    const PDFArray* array = object.getArray();
                    for (size_t i = 0, count = array->getCount(); i < count; ++i)
                    {
                        stack.push_back(array->getItem(i));
                    }
                    break;
                }

                case PDFObject::Type::Dictionary:
                case PDFObject::Type::Stream:
                {
                    const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : object.getDictionary();
                    if (dictionary->hasKey("Metadata"))
                    {
                        KindResult& metadata = result->get(Kind::Metadata);
                        metadata.isPresent = true;
                        ++metadata.count;
                    }
                    if (const PDFDictionary* files = storage->getDictionaryFromObject(dictionary->get("EF")))
                    {
                        KindResult& attachments = result->get(Kind::Attachments);
                        attachments.isPresent = true;
                        for (size_t i = 0, count = files->getCount(); i < count; ++i)
                        {
                            const PDFObject& file = files->getValue(i);
                            const PDFObject& fileObject = storage->getObject(file);
                            if (file.isReference() && fileObject.isStream() && embeddedFiles.insert(file.getReference()).second)
                            {
                                attachments.bytes += fileObject.getStream()->getContent()->size();
                            }
                        }
                    }
                    for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
                    {
                        stack.push_back(dictionary->getValue(i));
                    }
                    break;
                }

                default:
                    break;
            }
        }

        KindResult& attachments = result->get(Kind::Attachments);
        attachments.count = qMax(attachments.count, qint64(embeddedFiles.size()));
    }

    /// The catalog: document info, outline, page labels, search index, signature fields
    static void scanCatalog(const PDFDocument* document, Result* result)
    {
        const PDFObjectStorage* storage = &document->getStorage();
        const PDFCatalog* catalog = document->getCatalog();

        // Document info: the entries of the information dictionary
        if (const PDFDictionary* trailer = storage->getDictionaryFromObject(storage->getTrailerDictionary()))
        {
            if (const PDFDictionary* info = storage->getDictionaryFromObject(trailer->get("Info")))
            {
                KindResult& documentInfo = result->get(Kind::DocumentInfo);
                documentInfo.count = qint64(info->getCount());
                documentInfo.isPresent = documentInfo.count > 0;
            }
        }

        // ... and their properties in the XMP metadata of the document, which the sanitizer
        // removes with them (a packet, which cannot be read, may have them: the kind is offered,
        // the tool says, why it cannot remove them)
        const PDFDocumentSanitizer::DocumentInfoProperties xmp = PDFDocumentSanitizer::removeDocumentInfoProperties(PDFDocumentSanitizer::getCatalogMetadata(storage, nullptr));
        result->documentInfoXmp = xmp.names;
        result->isDocumentInfoXmpReadable = xmp.isReadable;
        KindResult& documentInfo = result->get(Kind::DocumentInfo);
        documentInfo.count += qint64(xmp.names.size());
        documentInfo.isPresent = documentInfo.count > 0 || !xmp.isReadable;

        const PDFDictionary* trailerDictionary = storage->getDictionaryFromObject(storage->getTrailerDictionary());
        const PDFDictionary* catalogDictionary = trailerDictionary ? storage->getDictionaryFromObject(trailerDictionary->get("Root")) : nullptr;
        if (!catalogDictionary)
        {
            return;
        }

        KindResult& outline = result->get(Kind::Outline);
        outline.isPresent = catalogDictionary->hasKey("Outlines") && !storage->getObject(catalogDictionary->get("Outlines")).isNull();
        if (const QSharedPointer<PDFOutlineItem> root = catalog->getOutlineRootPtr())
        {
            outline.count = qint64(root->getTotalCount());
        }

        KindResult& pageLabels = result->get(Kind::PageLabels);
        pageLabels.isPresent = catalogDictionary->hasKey("PageLabels") && !storage->getObject(catalogDictionary->get("PageLabels")).isNull();
        pageLabels.count = qint64(catalog->getPageLabels().size());
        if (pageLabels.isPresent)
        {
            result->pageLabelsText = createPageLabelsText(catalog);
        }

        if (const PDFDictionary* pieceInfo = storage->getDictionaryFromObject(catalogDictionary->get("PieceInfo")))
        {
            KindResult& searchIndex = result->get(Kind::SearchIndex);
            searchIndex.isPresent = pieceInfo->hasKey("SearchIndex") && !storage->getObject(pieceInfo->get("SearchIndex")).isNull();
            searchIndex.count = searchIndex.isPresent ? 1 : 0;
        }

        // Signature fields of the field tree; the permissions and the security store of the signatures
        KindResult& signatures = result->get(Kind::SignatureFields);
        if (const PDFDictionary* form = storage->getDictionaryFromObject(catalogDictionary->get("AcroForm")))
        {
            PDFObjectWalk walk(PDFFormField::MAX_FIELD_TREE_DEPTH);
            signatures.count = countSignatureFields(storage, form->get("Fields"), 0, &walk);
        }
        signatures.isPresent = signatures.count > 0 || catalogDictionary->hasKey("Perms") || catalogDictionary->hasKey("DSS");
    }

    /// Signature fields of an array of fields: one walk of the field tree (a field once, also
    /// when more parents list it)
    static qint64 countSignatureFields(const PDFObjectStorage* storage, const PDFObject& fieldsObject, int depth, PDFObjectWalk* walk)
    {
        const PDFObject& fields = storage->getObject(fieldsObject);
        if (!fields.isArray())
        {
            return 0;
        }

        qint64 count = 0;
        const PDFArray* array = fields.getArray();
        for (size_t i = 0, itemCount = array->getCount(); i < itemCount; ++i)
        {
            const PDFObject& item = array->getItem(i);
            if (!walk->enter(item, depth))
            {
                continue;
            }

            const PDFDictionary* field = storage->getDictionaryFromObject(item);
            if (!field)
            {
                continue;
            }
            if (PDFDocumentSanitizer::isSignatureField(storage, field))
            {
                ++count;
            }
            else if (field->hasKey("Kids"))
            {
                count += countSignatureFields(storage, field->get("Kids"), depth + 1, walk);
            }
        }
        return count;
    }

    /// "i–xii, 1–1,580": the ranges of the labels (at most three of them, then "…")
    static QString createPageLabelsText(const PDFCatalog* catalog)
    {
        const std::vector<PDFPageLabel>& labels = catalog->getPageLabels();
        const PDFInteger pageCount = PDFInteger(catalog->getPageCount());
        QStringList ranges;
        for (size_t i = 0; i < labels.size() && ranges.size() < 3; ++i)
        {
            const PDFInteger first = labels[i].getPageIndex();
            const PDFInteger last = (i + 1 < labels.size() ? labels[i + 1].getPageIndex() : pageCount) - 1;
            if (first < 0 || last < first)
            {
                continue;
            }

            const QString firstLabel = formatLabel(labels[i], 0);
            const QString lastLabel = formatLabel(labels[i], last - first);
            ranges << (first == last ? firstLabel : PDFDocumentHiddenDataScan::tr("%1–%2").arg(firstLabel, lastLabel));
        }
        QString text = ranges.join(QStringLiteral(", "));
        if (labels.size() > 3)
        {
            text += QStringLiteral(", …");
        }
        return PDFSearchQuery::sanitize(text, 256);
    }

    /// Label of a page of a range: the decimal numbers with the separators of the language
    static QString formatLabel(const PDFPageLabel& label, PDFInteger offset)
    {
        const PDFInteger number = qMax<PDFInteger>(1, label.getPageStartNumber()) + offset;
        const QString prefix = label.getPrefix().left(PDFCatalog::MAXIMUM_PAGE_LABEL_PREFIX_LENGTH);
        if (label.getNumberingStyle() == PDFPageLabel::NumberingStyle::DecimalArabic)
        {
            return prefix + QLocale().toString(qlonglong(number));
        }
        if (label.getNumberingStyle() == PDFPageLabel::NumberingStyle::None)
        {
            return prefix;
        }
        return prefix + PDFPageLabel::formatPageNumber(label.getNumberingStyle(), number);
    }

    /// The streams of a content object (a stream or an array of streams)
    static std::vector<const PDFStream*> getStreams(const PDFObjectStorage* storage, const PDFObject& contents)
    {
        std::vector<const PDFStream*> streams;
        const PDFObject& object = storage->getObject(contents);
        if (object.isStream())
        {
            streams.push_back(object.getStream());
        }
        else if (object.isArray())
        {
            const PDFArray* array = object.getArray();
            for (size_t i = 0, count = array->getCount(); i < count; ++i)
            {
                const PDFObject& item = storage->getObject(array->getItem(i));
                if (item.isStream())
                {
                    streams.push_back(item.getStream());
                }
            }
        }
        return streams;
    }

    /// State of the walk over the content of the pages for the invisible text
    struct ContentScan
    {
        const PDFDocument* document = nullptr;
        qint64 decodedBytes = 0;
        bool isLimitReached = false;
        std::map<PDFObjectReference, bool> forms;   ///< A form looked at: it has invisible text
        PDFObjectWalk formWalk = PDFObjectWalk(PDFDocumentHiddenDataScan::MAXIMUM_FORM_DEPTH); ///< The forms of all pages: each form once, to the depth of the forms
    };

    /// The decoded streams, within the limit (a stream, which cannot be decoded, has no text)
    static bool decodeStreams(ContentScan* scan, const std::vector<const PDFStream*>& streams, std::vector<QByteArray>* decoded)
    {
        for (const PDFStream* stream : streams)
        {
            if (scan->decodedBytes + qint64(stream->getContent()->size()) > PDFDocumentHiddenDataScan::MAXIMUM_DECODED_CONTENT)
            {
                scan->isLimitReached = true;
                return false;
            }

            try
            {
                decoded->push_back(scan->document->getDecodedStream(stream));
            }
            catch (const PDFException&)
            {
                continue;
            }

            scan->decodedBytes += decoded->back().size();
            if (scan->decodedBytes > PDFDocumentHiddenDataScan::MAXIMUM_DECODED_CONTENT)
            {
                scan->isLimitReached = true;
                return false;
            }
        }
        return true;
    }

    /// The content (the streams of one content stream) shows invisible text
    static bool hasInvisibleText(const std::vector<QByteArray>& decoded)
    {
        // Without the operator Tr nothing is invisible: the lexer is not needed
        bool hasOperator = false;
        for (const QByteArray& content : decoded)
        {
            hasOperator = hasOperator || content.contains("Tr");
        }
        if (!hasOperator)
        {
            return false;
        }

        PDFDocumentSanitizer::InvisibleTextState state;
        for (const QByteArray& content : decoded)
        {
            bool isChanged = false;
            try
            {
                PDFDocumentSanitizer::removeInvisibleText(content, &state, &isChanged);
            }
            catch (const PDFException&)
            {
                // A damaged stream is not read further; the sanitizer fails on it
                return false;
            }
            if (isChanged)
            {
                return true;
            }
        }
        return false;
    }

    /// The forms of the resources show invisible text
    static bool hasInvisibleTextInForms(ContentScan* scan, const PDFObject& resources, int depth)
    {
        const PDFObjectStorage* storage = &scan->document->getStorage();
        const PDFDictionary* resourcesDictionary = storage->getDictionaryFromObject(resources);
        const PDFDictionary* xobjects = resourcesDictionary ? storage->getDictionaryFromObject(resourcesDictionary->get("XObject")) : nullptr;
        if (!xobjects)
        {
            return false;
        }

        for (size_t i = 0, count = xobjects->getCount(); i < count && !scan->isLimitReached; ++i)
        {
            const PDFObject& item = xobjects->getValue(i);
            if (!item.isReference())
            {
                continue;
            }

            const auto it = scan->forms.find(item.getReference());
            if (it != scan->forms.end())
            {
                if (it->second)
                {
                    return true;
                }
                continue;
            }

            // Marked before it is looked at: a form, which draws itself, ends here; a form deeper
            // than the limit is not looked at (and not marked: a shorter path may reach it)
            if (!scan->formWalk.enter(item, depth))
            {
                continue;
            }
            scan->forms[item.getReference()] = false;
            const PDFObject& object = storage->getObject(item);
            if (!object.isStream())
            {
                continue;
            }
            const PDFDictionary* dictionary = object.getStream()->getDictionary();
            const PDFObject& subtype = storage->getObject(dictionary->get("Subtype"));
            if (!subtype.isName() || subtype.getString() != "Form")
            {
                continue;
            }

            std::vector<QByteArray> decoded;
            decodeStreams(scan, { object.getStream() }, &decoded);
            const bool isInvisible = hasInvisibleText(decoded) || hasInvisibleTextInForms(scan, dictionary->get("Resources"), depth + 1);
            scan->forms[item.getReference()] = isInvisible;
            if (isInvisible)
            {
                return true;
            }
        }
        return false;
    }

    /// The pages: comments, attachments of annotations, thumbnails, invisible text
    static void scanPages(const PDFDocument* document, bool includeInvisibleText, const PDFOperationControl* control, PDFProgressSink* progress, Result* result)
    {
        const PDFObjectStorage* storage = &document->getStorage();
        const PDFCatalog* catalog = document->getCatalog();
        const size_t pageCount = catalog->getPageCount();

        if (progress)
        {
            ProgressStartupInfo info;
            info.text = PDFDocumentHiddenDataScan::tr("Looking for hidden data");
            progress->start(pageCount, std::move(info));
        }

        ContentScan contentScan;
        contentScan.document = document;
        qint64 annotationCount = 0;
        qint64 popupCount = 0;
        qint64 fileAttachmentAnnotations = 0;
        bool isAnnotationLimitReached = false;
        std::set<const PDFArray*> scannedArrays;

        for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
        {
            if (PDFOperationControl::isOperationCancelled(control))
            {
                result->isCanceled = true;
                break;
            }

            const PDFPage* page = catalog->getPage(pageIndex);
            const PDFDictionary* pageDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(page->getPageReference()));
            if (pageDictionary)
            {
                if (pageDictionary->hasKey("Thumb"))
                {
                    KindResult& thumbnails = result->get(Kind::Thumbnails);
                    thumbnails.isPresent = true;
                    ++thumbnails.count;
                }

                // One array shared by more pages is looked at once
                const PDFObject& annotationsObject = storage->getObject(pageDictionary->get("Annots"));
                const PDFArray* annotations = annotationsObject.isArray() ? annotationsObject.getArray() : nullptr;
                if (annotations && scannedArrays.insert(annotations).second)
                {
                    for (size_t i = 0, count = annotations->getCount(); i < count; ++i)
                    {
                        if (++annotationCount > PDFDocumentHiddenDataScan::MAXIMUM_ANNOTATIONS)
                        {
                            isAnnotationLimitReached = true;
                            break;
                        }

                        PDFAnnotationPtr annotation = PDFAnnotation::parseItem(storage, annotations->getItem(i));
                        if (!annotation)
                        {
                            continue;
                        }

                        if (annotation->getType() == AnnotationType::Popup)
                        {
                            ++popupCount;
                        }
                        else if (PDFDocumentSanitizer::isCommentAnnotation(annotation.get()))
                        {
                            ++result->get(Kind::Comments).count;
                        }

                        if (annotation->getType() == AnnotationType::FileAttachment)
                        {
                            ++fileAttachmentAnnotations;
                        }
                    }
                }

                if (includeInvisibleText && !contentScan.isLimitReached)
                {
                    bool isInvisible = false;
                    const PDFDictionary* pieceInfo = storage->getDictionaryFromObject(pageDictionary->get("PieceInfo"));
                    if (pieceInfo && pieceInfo->hasKey(PDFOCRTextLayerWriter::PIECE_INFO_KEY))
                    {
                        isInvisible = true;
                    }
                    else
                    {
                        std::vector<QByteArray> decoded;
                        decodeStreams(&contentScan, getStreams(storage, pageDictionary->get("Contents")), &decoded);
                        isInvisible = hasInvisibleText(decoded) || hasInvisibleTextInForms(&contentScan, page->getResources(), 0);
                    }

                    if (isInvisible)
                    {
                        KindResult& invisibleText = result->get(Kind::InvisibleText);
                        invisibleText.isPresent = true;
                        ++invisibleText.count;
                    }
                }
            }

            if (progress)
            {
                progress->step();
            }
        }

        if (progress)
        {
            progress->finish();
        }

        KindResult& comments = result->get(Kind::Comments);
        if (comments.count == 0)
        {
            // Only popup windows: they have the texts of comments too
            comments.count = popupCount;
        }
        comments.isPresent = comments.count > 0;
        comments.isComplete = !isAnnotationLimitReached;

        KindResult& attachments = result->get(Kind::Attachments);
        result->attachmentAnnotations = fileAttachmentAnnotations;
        attachments.isPresent = attachments.isPresent || fileAttachmentAnnotations > 0;
        attachments.count = qMax(attachments.count, fileAttachmentAnnotations);
        attachments.isComplete = !isAnnotationLimitReached;

        KindResult& invisibleText = result->get(Kind::InvisibleText);
        invisibleText.isComplete = !contentScan.isLimitReached;
        if (contentScan.isLimitReached)
        {
            // What was not looked at may have it: the kind is offered
            invisibleText.isPresent = true;
        }
    }
};

int PDFDocumentHiddenDataScan::Result::getFoundCount() const
{
    int count = 0;
    for (const KindResult& kind : kinds)
    {
        count += kind.isPresent ? 1 : 0;
    }
    return count;
}

QJsonObject PDFDocumentHiddenDataScan::Result::toJson() const
{
    QJsonObject object;
    for (Kind kind : getKinds())
    {
        const KindResult& item = get(kind);
        QJsonObject kindObject;
        kindObject.insert(QStringLiteral("present"), item.isPresent);
        kindObject.insert(QStringLiteral("count"), qint64(item.count));
        if (item.bytes > 0)
        {
            kindObject.insert(QStringLiteral("bytes"), qint64(item.bytes));
        }
        if (!item.isComplete)
        {
            kindObject.insert(QStringLiteral("complete"), false);
        }
        if (kind == Kind::DocumentInfo)
        {
            // The properties of the XMP metadata among them ("dc:title")
            kindObject.insert(QStringLiteral("xmp"), QJsonArray::fromStringList(documentInfoXmp));
            if (!isDocumentInfoXmpReadable)
            {
                kindObject.insert(QStringLiteral("xmpReadable"), false);
            }
        }
        if (kind == Kind::Attachments && attachmentAnnotations > 0)
        {
            kindObject.insert(QStringLiteral("annotations"), qint64(attachmentAnnotations));
        }
        object.insert(getName(kind), kindObject);
    }
    if (!pageLabelsText.isEmpty())
    {
        object.insert(QStringLiteral("pageLabelsText"), pageLabelsText);
    }
    return object;
}

PDFDocumentHiddenDataScan::Result PDFDocumentHiddenDataScan::Result::fromJson(const QJsonObject& object)
{
    Result result;
    for (Kind kind : getKinds())
    {
        const QJsonObject kindObject = object.value(getName(kind)).toObject();
        KindResult& item = result.get(kind);
        item.isPresent = kindObject.value(QStringLiteral("present")).toBool();
        item.count = qMax<qint64>(0, kindObject.value(QStringLiteral("count")).toInteger());
        item.bytes = qMax<qint64>(0, kindObject.value(QStringLiteral("bytes")).toInteger());
        item.isComplete = kindObject.value(QStringLiteral("complete")).toBool(true);
    }
    const QJsonObject documentInfoObject = object.value(getName(Kind::DocumentInfo)).toObject();
    for (const QJsonValue& name : documentInfoObject.value(QStringLiteral("xmp")).toArray())
    {
        result.documentInfoXmp << name.toString();
    }
    result.isDocumentInfoXmpReadable = documentInfoObject.value(QStringLiteral("xmpReadable")).toBool(true);
    result.attachmentAnnotations = qMax<qint64>(0, object.value(getName(Kind::Attachments)).toObject().value(QStringLiteral("annotations")).toInteger());
    result.pageLabelsText = object.value(QStringLiteral("pageLabelsText")).toString();
    return result;
}

PDFDocumentHiddenDataScan::Result PDFDocumentHiddenDataScan::scan(const PDFDocument* document, bool includeInvisibleText, const PDFOperationControl* control, PDFProgressSink* progress)
{
    using Helper = PDFDocumentHiddenDataScanHelper;

    Result result;
    if (!document)
    {
        return result;
    }

    Helper::scanCatalog(document, &result);
    Helper::scanObjects(&document->getStorage(), control, &result);
    if (!result.isCanceled)
    {
        Helper::scanPages(document, includeInvisibleText, control, progress, &result);
    }
    return result;
}

std::vector<PDFDocumentHiddenDataScan::Kind> PDFDocumentHiddenDataScan::getKinds()
{
    return { Kind::DocumentInfo, Kind::Metadata, Kind::Comments, Kind::Attachments, Kind::Outline, Kind::PageLabels,
             Kind::SearchIndex, Kind::Thumbnails, Kind::InvisibleText, Kind::SignatureFields };
}

PDFDocumentSanitizer::SanitizationFlag PDFDocumentHiddenDataScan::getFlag(Kind kind)
{
    switch (kind)
    {
        case Kind::DocumentInfo:
            return PDFDocumentSanitizer::DocumentInfo;
        case Kind::Metadata:
            return PDFDocumentSanitizer::Metadata;
        case Kind::Comments:
            return PDFDocumentSanitizer::MarkupAnnotations;
        case Kind::Attachments:
            return PDFDocumentSanitizer::FileAttachments;
        case Kind::Outline:
            return PDFDocumentSanitizer::Outline;
        case Kind::PageLabels:
            return PDFDocumentSanitizer::PageLabels;
        case Kind::SearchIndex:
            return PDFDocumentSanitizer::EmbeddedSearchIndex;
        case Kind::Thumbnails:
            return PDFDocumentSanitizer::PageThumbnails;
        case Kind::InvisibleText:
            return PDFDocumentSanitizer::InvisibleText;
        case Kind::SignatureFields:
            return PDFDocumentSanitizer::SignatureFields;
    }
    return PDFDocumentSanitizer::None;
}

QString PDFDocumentHiddenDataScan::getName(Kind kind)
{
    switch (kind)
    {
        case Kind::DocumentInfo:
            return QStringLiteral("documentInfo");
        case Kind::Metadata:
            return QStringLiteral("metadata");
        case Kind::Comments:
            return QStringLiteral("comments");
        case Kind::Attachments:
            return QStringLiteral("attachments");
        case Kind::Outline:
            return QStringLiteral("outline");
        case Kind::PageLabels:
            return QStringLiteral("pageLabels");
        case Kind::SearchIndex:
            return QStringLiteral("searchIndex");
        case Kind::Thumbnails:
            return QStringLiteral("thumbnails");
        case Kind::InvisibleText:
            return QStringLiteral("invisibleText");
        case Kind::SignatureFields:
            return QStringLiteral("signatureFields");
    }
    return QString();
}

}   // namespace pdf
