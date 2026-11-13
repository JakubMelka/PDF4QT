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

#include "pdfnewdocumentfinisher.h"

#include "pdfdocumentbuilder.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumentsanitizer.h"
#include "pdfdocumenttool.h"
#include "pdfencoding.h"
#include "pdfobjectutils.h"
#include "pdfoperationcontrol.h"
#include "pdfoptimizer.h"
#include "pdfsecurityhandler.h"

#include <QCryptographicHash>
#include <QUuid>

#include <set>

namespace pdf
{

/// Helpers of the finishing of a new document
class PDFNewDocumentFinisherHelper
{
public:
    PDFNewDocumentFinisherHelper() = delete;

    static PDFInplaceOrMemoryString key(const char* text)
    {
        return PDFInplaceOrMemoryString(text);
    }

    /// The trailer dictionary (of a cross-reference stream too, ISO 32000-2, 7.5.8.2)
    static const PDFDictionary* getTrailer(const PDFObjectStorage* storage)
    {
        return storage->getDictionaryFromObject(storage->getTrailerDictionary());
    }

    /// The name of an object, empty for another type
    static QByteArray getName(const PDFObject& object)
    {
        return object.isName() ? object.getString() : QByteArray();
    }

    /// A text string as a QString (an empty one for another type)
    static QString getText(const PDFObject& object)
    {
        return object.isString() ? PDFEncoding::convertTextString(object.getString()) : QString();
    }

    /// The items of an array (an empty list for another type)
    static std::vector<PDFObject> getItems(const PDFObjectStorage* storage, const PDFObject& object)
    {
        std::vector<PDFObject> items;
        const PDFObject& dereferenced = storage->getObject(object);
        if (dereferenced.isArray())
        {
            const PDFArray* array = dereferenced.getArray();
            items.reserve(array->getCount());
            for (size_t i = 0; i < array->getCount(); ++i)
            {
                items.push_back(array->getItem(i));
            }
        }
        return items;
    }

    static PDFObject createArray(const std::vector<PDFObject>& items)
    {
        PDFArrayBuilder array;
        array.reserve(items.size());
        for (const PDFObject& item : items)
        {
            array.appendItem(item);
        }
        return PDFObject::createArray(std::move(array));
    }

    /// 14.4: a new file gets a new identifier, both strings are the same
    static PDFObject createId(const QDateTime& now, size_t objectCount)
    {
        QCryptographicHash hash(QCryptographicHash::Md5);
        hash.addData(QUuid::createUuid().toRfc4122());
        hash.addData(now.toString(Qt::ISODateWithMs).toUtf8());
        hash.addData(QByteArray::number(qint64(objectCount)));
        const QByteArray identifier = hash.result();
        return createArray({ PDFObject::createString(identifier), PDFObject::createString(identifier) });
    }

    /// The metadata of PDF 2.0 with the dates of the creation (14.3.3, the XMP counterparts of
    /// /CreationDate, /ModDate and /Producer of /Info)
    static QByteArray createMetadata(const QDateTime& now, const QString& producer)
    {
        const QString date8601 = now.toOffsetFromUtc(now.offsetFromUtc()).toString(Qt::ISODate);
        QByteArray xmp;
        xmp += "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n";
        xmp += "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n";
        xmp += "<rdf:Description rdf:about=\"\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\" xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\">\n";
        xmp += "<xmp:CreateDate>" + date8601.toUtf8() + "</xmp:CreateDate>\n";
        xmp += "<xmp:ModifyDate>" + date8601.toUtf8() + "</xmp:ModifyDate>\n";
        xmp += "<pdf:Producer>" + producer.toHtmlEscaped().toUtf8() + "</pdf:Producer>\n";
        xmp += "</rdf:Description>\n</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>";
        return xmp;
    }

    /// Depth of the tree of the form fields, which is walked
    static constexpr int MAXIMUM_FIELD_DEPTH = 64;

    /// A dictionary of the catalog (AcroForm) written back: into its object, or into the catalog
    static void setCatalogDictionary(PDFObjectStorage* storage, PDFDictionaryBuilder* catalog, const char* entry, PDFDictionaryBuilder dictionary)
    {
        const PDFObject value = catalog->get(entry);
        if (value.isReference())
        {
            storage->setObject(value.getReference(), PDFObject::createDictionary(std::move(dictionary)));
        }
        else
        {
            catalog->setEntry(key(entry), PDFObject::createDictionary(std::move(dictionary)));
        }
    }

    /// Owner answer Q4 of step 2: a signature of another file is never valid (12.8.1) - the value
    /// of every signature field goes, the field stays empty for a new signature, the widgets
    /// lose their appearance. The fields are walked by one PDFObjectWalk for the whole tree
    /// (a field shared by two parents or a cycle of /Kids is read once). Returns the count.
    static qint64 removeSignatureValues(PDFObjectStorage* storage, const PDFDictionary* acroForm)
    {
        struct Node
        {
            PDFObject field;
            QByteArray inheritedType;   ///< /FT of the parents (Table 226: inheritable)
            bool isParentSigned = false;
            int depth = 0;
        };

        std::vector<Node> stack;
        for (const PDFObject& field : getItems(storage, acroForm->get("Fields")))
        {
            stack.push_back({ field, QByteArray(), false, 0 });
        }

        qint64 removed = 0;
        PDFObjectWalk walk(MAXIMUM_FIELD_DEPTH);
        while (!stack.empty())
        {
            Node node = std::move(stack.back());
            stack.pop_back();
            if (!walk.enter(node.field, node.depth))
            {
                continue;
            }
            const PDFDictionary* dictionary = storage->getDictionaryFromObject(node.field);
            if (!dictionary)
            {
                continue;
            }

            const QByteArray ownType = getName(dictionary->get("FT"));
            const QByteArray type = ownType.isEmpty() ? node.inheritedType : ownType;
            // A field without a value is not signed: /V null or a reference to an undefined or
            // unreadable object is an absent entry (7.3.7, 7.3.10), as PDFForm::getSignatureCount
            const bool isSigned = type == "Sig" && !storage->getObject(dictionary->get("V")).isNull();
            const bool isWidget = getName(dictionary->get("Subtype")) == "Widget";
            const std::vector<PDFObject> kids = getItems(storage, dictionary->get("Kids"));
            if ((isSigned || (node.isParentSigned && isWidget)) && node.field.isReference())
            {
                PDFDictionaryBuilder builder(*dictionary);
                builder.removeEntry("V");
                if (isWidget)
                {
                    builder.removeEntry("AP");
                    builder.removeEntry("AS");
                }
                dictionary = nullptr;
                storage->setObject(node.field.getReference(), PDFObject::createDictionary(std::move(builder)));
                removed += isSigned ? 1 : 0;
            }

            for (const PDFObject& kid : kids)
            {
                stack.push_back({ kid, type, isSigned, node.depth + 1 });
            }
        }
        return removed;
    }

    /// The signatures of a new document (removeSignatureValues), bit 2 AppendOnly of /SigFlags
    /// (Table 225), /Perms and /DSS of the catalog (12.8.4: they belong to the signatures)
    static qint64 removeSignatures(PDFObjectStorage* storage, const PDFObject& catalogObject)
    {
        const PDFDictionary* catalog = storage->getDictionaryFromObject(catalogObject);
        if (!catalog || !catalogObject.isReference())
        {
            return 0;
        }

        PDFDictionaryBuilder catalogBuilder(*catalog);
        qint64 removed = 0;
        if (const PDFDictionary* acroForm = storage->getDictionaryFromObject(catalog->get("AcroForm")))
        {
            removed = removeSignatureValues(storage, acroForm);
            acroForm = storage->getDictionaryFromObject(storage->getDictionaryFromObject(catalogObject)->get("AcroForm"));
            const PDFObject flags = acroForm ? storage->getObject(acroForm->get("SigFlags")) : PDFObject();
            if (removed > 0 && flags.isInt() && (flags.getInteger() & 2))
            {
                PDFDictionaryBuilder form(*acroForm);
                form.setEntry(key("SigFlags"), PDFObject::createInteger(flags.getInteger() & ~PDFInteger(2)));
                setCatalogDictionary(storage, &catalogBuilder, "AcroForm", std::move(form));
            }
        }

        const bool hasCatalogEntries = catalogBuilder.hasKey("Perms") || catalogBuilder.hasKey("DSS");
        catalogBuilder.removeEntry("Perms");
        catalogBuilder.removeEntry("DSS");
        if (hasCatalogEntries || removed > 0)
        {
            storage->setObject(catalogObject.getReference(), PDFObject::createDictionary(std::move(catalogBuilder)));
        }
        return removed;
    }

    /// 8.11.2.2 (R10 of step 2): every layer, which the content or an annotation uses, is in /OCGs
    static void listLayers(PDFDocumentBuilder* builder, const PDFDictionary* catalog)
    {
        const PDFObject ocPropertiesObject = catalog->get("OCProperties");
        const PDFDictionary* ocProperties = builder->getDictionaryFromObject(ocPropertiesObject);
        if (!ocProperties)
        {
            return;
        }

        std::vector<PDFObject> groups = getItems(builder->getStorage(), ocProperties->get("OCGs"));
        std::set<PDFObjectReference> listed;
        for (const PDFObject& group : groups)
        {
            if (group.isReference())
            {
                listed.insert(group.getReference());
            }
        }

        const PDFObjectStorage::PDFObjects& objects = builder->getStorage()->getObjects();
        bool isChanged = false;
        for (size_t i = 0; i < objects.size(); ++i)
        {
            const PDFDictionary* dictionary = objects[i].object.isDictionary() ? objects[i].object.getDictionary() : nullptr;
            const PDFObjectReference reference(PDFInteger(i), objects[i].generation);
            if (dictionary && getName(dictionary->get("Type")) == "OCG" && !listed.count(reference))
            {
                groups.push_back(PDFObject::createReference(reference));
                listed.insert(reference);
                isChanged = true;
            }
        }
        if (!isChanged)
        {
            return;
        }

        PDFDictionaryBuilder properties(*ocProperties);
        properties.setEntry(key("OCGs"), createArray(groups));
        if (ocPropertiesObject.isReference())
        {
            builder->setObject(ocPropertiesObject.getReference(), PDFObject::createDictionary(std::move(properties)));
        }
        else
        {
            PDFDictionaryBuilder catalogBuilder(*catalog);
            catalogBuilder.setEntry(key("OCProperties"), PDFObject::createDictionary(std::move(properties)));
            builder->setObject(builder->getCatalogReference(), PDFObject::createDictionary(std::move(catalogBuilder)));
        }
    }
};

PDFNewDocumentFinisher::Result PDFNewDocumentFinisher::finish(PDFDocument document, const Settings& settings, const PDFOperationControl* control)
{
    using Helper = PDFNewDocumentFinisherHelper;

    Result result;
    if (PDFOperationControl::isOperationCancelled(control))
    {
        result.isCanceled = true;
        return result;
    }

    const QDateTime now = settings.creationTime.isValid() ? settings.creationTime : QDateTime::currentDateTime();
    const bool isNewInfo = !settings.isWholeSingleDocument || settings.isNewFile;
    PDFObjectStorage storage = document.getStorage();
    document = PDFDocument();

    // A new document: a new identifier, not encrypted (owner answer Q2 of step 2)
    PDFDictionaryBuilder trailer;
    trailer.addEntry(Helper::key("ID"), Helper::createId(now, storage.getObjects().size()));
    trailer.addEntry(Helper::key("Encrypt"), PDFObject());
    storage.updateTrailerDictionary(PDFObject::createDictionary(std::move(trailer)));
    storage.setSecurityHandler(PDFSecurityHandlerPointer(new PDFNoneSecurityHandler()));

    const PDFDictionary* trailerDictionary = Helper::getTrailer(&storage);
    const PDFObject infoObject = trailerDictionary ? trailerDictionary->get("Info") : PDFObject();
    const PDFObject catalogObject = trailerDictionary ? trailerDictionary->get("Root") : PDFObject();

    // A signature of another file is never valid in a new one (Q4 of step 2)
    result.signaturesRemoved = Helper::removeSignatures(&storage, catalogObject);

    // Creation and modification of a new document are now (14.3.3); PDF 2.0 has them in XMP too
    if (isNewInfo && infoObject.isReference())
    {
        if (const PDFDictionary* info = storage.getDictionaryFromObject(infoObject))
        {
            PDFDictionaryBuilder infoBuilder(*info);
            const PDFObject date = PDFObject::createString(PDFEncoding::convertDateTimeToString(now));
            infoBuilder.setEntry(Helper::key("CreationDate"), date);
            infoBuilder.setEntry(Helper::key("ModDate"), date);
            const QString producer = Helper::getText(info->get("Producer"));
            storage.setObject(infoObject.getReference(), PDFObject::createDictionary(std::move(infoBuilder)));

            if (settings.version >= PDFVersion(2, 0) && catalogObject.isReference())
            {
                QByteArray xmp = Helper::createMetadata(now, producer);
                PDFDictionaryBuilder metadataDictionary;
                metadataDictionary.addEntry(Helper::key("Type"), PDFObject::createName("Metadata"));
                metadataDictionary.addEntry(Helper::key("Subtype"), PDFObject::createName("XML"));
                metadataDictionary.addEntry(Helper::key("Length"), PDFObject::createInteger(xmp.size()));
                const PDFObjectReference metadata = storage.addObject(PDFObject::createStream(PDFStream(std::move(metadataDictionary), std::move(xmp))));
                if (const PDFDictionary* catalog = storage.getDictionaryFromObject(catalogObject))
                {
                    PDFDictionaryBuilder catalogBuilder(*catalog);
                    catalogBuilder.setEntry(Helper::key("Metadata"), PDFObject::createReference(metadata));
                    storage.setObject(catalogObject.getReference(), PDFObject::createDictionary(std::move(catalogBuilder)));
                }
            }
            else if (settings.isWholeSingleDocument)
            {
                // The metadata of the input stay (a part of Split keeps its catalog), but their
                // counterparts of /Info (14.3.3, Table 349: the dates of the input among them)
                // would contradict the new /Info: they go, everything else stays byte by byte
                const PDFDocumentSanitizer::DocumentInfoProperties properties = PDFDocumentSanitizer::removeDocumentInfoProperties(PDFDocumentSanitizer::getCatalogMetadata(&storage, nullptr));
                if (!properties.names.isEmpty())
                {
                    PDFDocumentBuilder metadataBuilder(storage, settings.version);
                    metadataBuilder.setCatalogMetadata(properties.metadata);
                    storage = metadataBuilder.build().getStorage();
                }
            }
        }
    }

    // The objects nothing refers to go (the pages of the other parts, the catalogs and the page
    // trees of the inputs, the containers of the file structure), the identical ones are merged
    // and the rest is numbered from 1 without a gap (R2: the cross-reference table of a part
    // has only its own objects)
    PDFOptimizer optimizer(PDFOptimizer::OptimizationFlags(PDFOptimizer::RemoveUnusedObjects | PDFOptimizer::ShrinkObjectStorage |
                                                           PDFOptimizer::DereferenceSimpleObjects | PDFOptimizer::MergeIdenticalObjects), nullptr);
    optimizer.setOperationControl(control);
    optimizer.setStorage(storage);
    optimizer.optimize();
    storage = optimizer.takeStorage();
    if (PDFOperationControl::isOperationCancelled(control))
    {
        result.isCanceled = true;
        return result;
    }

    // The merged identical objects can repeat a reference in an array of the layers or fields
    PDFDocument finished(PDFObjectStorage(storage), settings.version, QByteArray());
    {
        PDFDocumentBuilder finalBuilder(&finished);
        const PDFDictionary* catalog = finalBuilder.getDictionaryFromObject(finalBuilder.getObjectByReference(finalBuilder.getCatalogReference()));
        if (catalog)
        {
            PDFDocumentDataLoaderDecorator loader(finalBuilder.getStorage());
            for (const char* entry : { "OCProperties", "AcroForm" })
            {
                const PDFObjectReference reference = loader.readReferenceFromDictionary(catalog, entry);
                if (reference.isValid())
                {
                    finalBuilder.setObject(reference, PDFObjectManipulator::removeDuplicitReferencesInArrays(finalBuilder.getObjectByReference(reference)));
                }
            }
        }

        if (!settings.isWholeSingleDocument && catalog)
        {
            Helper::listLayers(&finalBuilder, catalog);
        }

        PDFDocument finalDocument = finalBuilder.build();
        PDFObjectStorage finalStorage = finalDocument.getStorage();

        // The build writes the time of now into /ModDate: a new document has the one time of its
        // creation in /Info and in XMP (14.3.3)
        const PDFDictionary* finalTrailer = Helper::getTrailer(&finalStorage);
        const PDFObject finalInfo = finalTrailer ? finalTrailer->get("Info") : PDFObject();
        const PDFDictionary* infoDictionary = finalStorage.getDictionaryFromObject(finalInfo);
        if (isNewInfo && finalInfo.isReference() && infoDictionary)
        {
            PDFDictionaryBuilder infoBuilder(*infoDictionary);
            infoBuilder.setEntry(Helper::key("ModDate"), PDFObject::createString(PDFEncoding::convertDateTimeToString(now)));
            infoBuilder.setEntry(Helper::key("CreationDate"), PDFObject::createString(PDFEncoding::convertDateTimeToString(now)));
            finalStorage.setObject(finalInfo.getReference(), PDFObject::createDictionary(std::move(infoBuilder)));
        }
        finished = PDFDocument(std::move(finalStorage), settings.version, QByteArray());
    }

    // A new document must not declare a standard it was not checked against (R7 of step 2)
    if (settings.removeConformance)
    {
        PDFObjectStorage conformanceStorage = finished.getStorage();
        const PDFDocumentTool::ConformanceRemoval conformance = PDFDocumentTool::removeConformanceDeclaration(&conformanceStorage, &finished);
        if (!conformance.kept.isEmpty())
        {
            result.errorMessage = PDFDocumentTool::createConformanceFailure(conformance.kept).message;
            return result;
        }
        if (!conformance.removed.isEmpty())
        {
            result.conformanceRemoved = conformance.removed;
            finished = PDFDocument(std::move(conformanceStorage), settings.version, QByteArray());
        }
    }

    if (qint64(finished.getStorage().getObjects().size()) > PDFDocumentMerger::MAXIMUM_OBJECT_COUNT)
    {
        result.errorMessage = PDFDocumentMerger::tr("The result would have more than %Ln objects.", nullptr, int(PDFDocumentMerger::MAXIMUM_OBJECT_COUNT));
        return result;
    }

    result.isSuccess = true;
    result.document = std::move(finished);
    return result;
}

}   // namespace pdf
