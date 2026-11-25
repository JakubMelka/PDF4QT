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

#include "pdfdocumentreport.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfencoding.h"
#include "pdfexception.h"
#include "pdfform.h"
#include "pdfoperationcontrol.h"
#include "pdfprogress.h"
#include "pdfresourcewalk.h"
#include "pdfstreamfilters.h"
#include "pdfstructuretree.h"
#include "pdftransparencyrenderer.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QPageSize>
#include <QStringDecoder>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

/// The parts of the report: the reading of the sections and the model of their data
class PDFDocumentReportHelper
{
public:
    PDFDocumentReportHelper() = delete;

    using Report = PDFDocumentReport;
    using Table = PDFDocumentToolTable;
    using ValueType = PDFDocumentToolTableValueType;

    // ------------------------------------------------------------ reading

    /// The dictionary of the key of the trailer (/Root, /Info, /Encrypt), nullptr = none
    static const PDFDictionary* getTrailerEntry(const PDFDocument* document, const char* key)
    {
        const PDFDictionary* trailer = document->getTrailerDictionary();
        return trailer ? document->getDictionaryFromObject(trailer->get(key)) : nullptr;
    }

    /// A name of a dictionary as a text (empty for another type)
    static QByteArray getName(const PDFDocument* document, const PDFDictionary* dictionary, const char* key)
    {
        const PDFObject& object = dictionary ? document->getObject(dictionary->get(key)) : PDFObject();
        return object.isName() ? object.getString() : QByteArray();
    }

    /// An integer of a dictionary (\p defaultValue for another type), bounded
    static PDFInteger getInteger(const PDFDocument* document, const PDFDictionary* dictionary, const char* key, PDFInteger defaultValue)
    {
        const PDFObject& object = dictionary ? document->getObject(dictionary->get(key)) : PDFObject();
        return object.isInt() ? qBound(PDFInteger(-1000000), object.getInteger(), PDFInteger(1000000)) : defaultValue;
    }

    /// The text of the XMP packet (a byte order mark of UTF-16 / UTF-8, UTF-8 otherwise)
    static QString decodeMetadata(const QByteArray& data)
    {
        const std::optional<QStringConverter::Encoding> encoding = QStringConverter::encodingForData(data);
        QStringDecoder decoder(encoding.value_or(QStringConverter::Utf8), QStringConverter::Flag::Stateless);
        QString text = decoder.decode(data);
        if (text.startsWith(QChar(0xFEFF)))
        {
            text.remove(0, 1);
        }
        return text;
    }

    /// The method of the encryption (Table 20, Table 25): "RC4, 40 bit", "AES, 256 bit"
    static QString getEncryptionAlgorithm(const PDFDocument* document, const PDFDictionary* encrypt, int version)
    {
        QByteArray method;
        if (version >= 4)
        {
            // The crypt filter of the streams (/StmF, /CF, /CFM)
            const QByteArray streamFilter = getName(document, encrypt, "StmF");
            const PDFDictionary* filters = encrypt ? document->getDictionaryFromObject(encrypt->get("CF")) : nullptr;
            const PDFDictionary* filter = filters && !streamFilter.isEmpty() ? document->getDictionaryFromObject(filters->get(streamFilter)) : nullptr;
            method = getName(document, filter, "CFM");
            if (streamFilter == "Identity")
            {
                method = "None";
            }
        }

        if (method == "AESV3" || (method.isEmpty() && version == 5))
        {
            return Report::tr("AES, 256 bit");
        }
        if (method == "AESV2")
        {
            return Report::tr("AES, 128 bit");
        }
        if (method == "V2" || version == 4)
        {
            return Report::tr("RC4, 128 bit");
        }
        if (method == "None")
        {
            return Report::tr("Not encrypted (identity)");
        }
        if (version == 2 || version == 3)
        {
            const PDFInteger length = getInteger(document, encrypt, "Length", 40);
            return Report::tr("RC4, %1 bit").arg(qBound(PDFInteger(40), length, PDFInteger(128)));
        }
        return Report::tr("RC4, 40 bit");
    }

    /// The name of a font is a subset (9.9.2: six upper-case letters and '+')
    static bool isSubsetName(const QString& name)
    {
        if (name.size() < 7 || name.at(6) != QLatin1Char('+'))
        {
            return false;
        }
        for (int i = 0; i < 6; ++i)
        {
            if (name.at(i) < QLatin1Char('A') || name.at(i) > QLatin1Char('Z'))
            {
                return false;
            }
        }
        return true;
    }

    static QString getFontTypeId(FontType type)
    {
        switch (type)
        {
            case FontType::Type0:
                return QStringLiteral("type0");
            case FontType::Type1:
                return QStringLiteral("type1");
            case FontType::MMType1:
                return QStringLiteral("mmType1");
            case FontType::TrueType:
                return QStringLiteral("trueType");
            case FontType::Type3:
                return QStringLiteral("type3");
            case FontType::Invalid:
                break;
        }
        return QStringLiteral("unknown");
    }

    static QString getEncodingText(const PDFDocument* document, const PDFObject& fontObject, const PDFFont* font)
    {
        if (const PDFSimpleFont* simpleFont = dynamic_cast<const PDFSimpleFont*>(font))
        {
            switch (simpleFont->getEncodingType())
            {
                case PDFEncoding::Encoding::Standard:
                    return QStringLiteral("Standard");
                case PDFEncoding::Encoding::MacRoman:
                    return QStringLiteral("MacRoman");
                case PDFEncoding::Encoding::WinAnsi:
                    return QStringLiteral("WinAnsi");
                case PDFEncoding::Encoding::PDFDoc:
                    return QStringLiteral("PDFDoc");
                case PDFEncoding::Encoding::MacExpert:
                    return QStringLiteral("MacExpert");
                case PDFEncoding::Encoding::Symbol:
                    return QStringLiteral("Symbol");
                case PDFEncoding::Encoding::ZapfDingbats:
                    return QStringLiteral("ZapfDingbats");
                default:
                    return QStringLiteral("Custom");
            }
        }

        // A composite font: the name of its CMap (Identity-H)
        const QByteArray name = getName(document, document->getDictionaryFromObject(fontObject), "Encoding");
        return Report::sanitize(QString::fromLatin1(name.left(128)));
    }

    /// The information of one font of the resources; false = not a font
    static bool readFont(const PDFDocument* document, const PDFObject& object, const QByteArray& key, bool isCharacterMapIncluded, PDFDocumentReportFont* info)
    {
        try
        {
            PDFFontPointer font = PDFFont::createFont(object, key, document);
            if (!font)
            {
                return false;
            }

            const FontDescriptor* descriptor = font->getFontDescriptor();
            const PDFDictionary* fontDictionary = document->getDictionaryFromObject(object);
            const QByteArray baseFont = getName(document, fontDictionary, "BaseFont");

            // K7.21: the name exactly as /BaseFont (the prefix of a subset kept); a font without it (Type 3)
            // by its descriptor or by its key of the resources
            QByteArray name = !baseFont.isEmpty() ? baseFont : descriptor->fontName;
            if (name.isEmpty())
            {
                name = key;
            }
            info->name = Report::sanitize(QString::fromUtf8(name.left(512)));
            info->isSubset = isSubsetName(info->name);
            info->family = Report::sanitize(QString::fromUtf8(descriptor->fontFamily.left(512)));
            info->fontType = font->getFontType();
            info->type = getFontTypeId(info->fontType);
            info->encoding = getEncodingText(document, object, font.data());
            info->isEmbedded = info->fontType == FontType::Type3 || descriptor->isEmbedded();
            const PDFFontCMap* toUnicode = font->getToUnicode();
            info->hasToUnicode = toUnicode && toUnicode->isValid();
            info->reference = object.isReference() ? object.getReference() : PDFObjectReference();
            readRealizedFont(font, isCharacterMapIncluded, info);
            return true;
        }
        catch (const PDFException&)
        {
            // A damaged font is left out
            return false;
        }
    }

    /// The substitute of a font, which is not embedded, and the character map of an embedded font: a font,
    /// which cannot be realized (no system font of its name), stays in the report without them
    static void readRealizedFont(const PDFFontPointer& font, bool isCharacterMapIncluded, PDFDocumentReportFont* info)
    {
        const bool needsCharacterMap = isCharacterMapIncluded && info->isEmbedded && info->fontType != FontType::Type3;
        if (info->isEmbedded && !needsCharacterMap)
        {
            return;
        }

        try
        {
            PDFRenderErrorReporterDummy reporter;
            PDFRealizedFontPointer realizedFont = PDFRealizedFont::createRealizedFont(font, 8.0, &reporter);
            if (realizedFont)
            {
                if (!info->isEmbedded)
                {
                    info->substitute = Report::sanitize(realizedFont->getPostScriptName());
                }
                if (needsCharacterMap)
                {
                    info->characterInfos = realizedFont->getCharacterInfos();
                }
            }
        }
        catch (const PDFException&)
        {
            // No substitute and no character map
        }
    }

    /// The pages of the document by their objects (one map for the whole section)
    static std::map<PDFObjectReference, PDFInteger> getPageMap(const PDFDocument* document)
    {
        std::map<PDFObjectReference, PDFInteger> pages;
        const PDFCatalog* catalog = document->getCatalog();
        for (size_t i = 0; i < catalog->getPageCount(); ++i)
        {
            pages.emplace(catalog->getPage(i)->getPageReference(), PDFInteger(i));
        }
        return pages;
    }

    static PDFInteger findPage(const std::map<PDFObjectReference, PDFInteger>& pages, PDFObjectReference reference)
    {
        auto it = pages.find(reference);
        return it != pages.cend() ? it->second : -1;
    }

    static QString getDestinationTypeName(DestinationType type)
    {
        switch (type)
        {
            case DestinationType::Invalid:
                return QStringLiteral("Invalid");
            case DestinationType::Named:
                return QStringLiteral("Named");
            case DestinationType::XYZ:
                return QStringLiteral("XYZ");
            case DestinationType::Fit:
                return QStringLiteral("Fit");
            case DestinationType::FitH:
                return QStringLiteral("FitH");
            case DestinationType::FitV:
                return QStringLiteral("FitV");
            case DestinationType::FitR:
                return QStringLiteral("FitR");
            case DestinationType::FitB:
                return QStringLiteral("FitB");
            case DestinationType::FitBH:
                return QStringLiteral("FitBH");
            case DestinationType::FitBV:
                return QStringLiteral("FitBV");
        }
        return QStringLiteral("Invalid");
    }

    static std::optional<PDFReal> finite(PDFReal value)
    {
        return std::isfinite(value) ? std::optional<PDFReal>(value) : std::nullopt;
    }

    static QString getContextId(PDFJavaScriptEntry::Type type)
    {
        switch (type)
        {
            case PDFJavaScriptEntry::Type::Document:
                return QStringLiteral("document");
            case PDFJavaScriptEntry::Type::Named:
                return QStringLiteral("named");
            case PDFJavaScriptEntry::Type::Form:
                return QStringLiteral("form");
            case PDFJavaScriptEntry::Type::Page:
                return QStringLiteral("page");
            case PDFJavaScriptEntry::Type::Annotation:
                return QStringLiteral("annotation");
            case PDFJavaScriptEntry::Type::Outline:
                return QStringLiteral("outline");
            case PDFJavaScriptEntry::Type::Xfa:
                return QStringLiteral("xfa");
            case PDFJavaScriptEntry::Type::Invalid:
                break;
        }
        return QStringLiteral("unknown");
    }

    static std::vector<PDFSecurityHandler::Permission> getPermissions()
    {
        using Permission = PDFSecurityHandler::Permission;
        return { Permission::PrintLowResolution, Permission::PrintHighResolution, Permission::CopyContent, Permission::Accessibility,
                 Permission::Assemble, Permission::Modify, Permission::ModifyInteractiveItems, Permission::ModifyFormFields };
    }

    // ------------------------------------------------------------ data

    static QString toIso(const QDateTime& dateTime)
    {
        return dateTime.isValid() ? dateTime.toOffsetFromUtc(dateTime.offsetFromUtc()).toString(Qt::ISODate) : QString();
    }

    static double round(double value, int decimals)
    {
        const double factor = std::pow(10.0, decimals);
        return std::isfinite(value) ? std::round(value * factor) / factor : 0.0;
    }

    static QJsonArray toJson(const QRectF& rect)
    {
        return QJsonArray({ round(rect.left(), 2), round(rect.top(), 2), round(rect.right(), 2), round(rect.bottom(), 2) });
    }

    static QJsonObject createBasicInformationData(const PDFDocument* document, const Report::Options& options)
    {
        PDFDocumentReportBasicInformation information = Report::getBasicInformation(document);
        information.isRepaired = options.isRepaired;

        QJsonObject data;
        data.insert(QStringLiteral("version"), information.version);

        QJsonArray info;
        for (const PDFDocumentReportBasicInformation::InfoEntry& entry : information.info)
        {
            QJsonObject item;
            item.insert(QStringLiteral("key"), Report::sanitize(QString::fromUtf8(entry.key)));
            item.insert(QStringLiteral("custom"), entry.isCustom);
            if (entry.dateTime.isValid())
            {
                item.insert(QStringLiteral("date"), toIso(entry.dateTime));
            }
            else
            {
                item.insert(QStringLiteral("value"), entry.value);
            }
            info.append(item);
        }
        data.insert(QStringLiteral("info"), info);
        data.insert(QStringLiteral("tagged"), information.isTagged);
        data.insert(QStringLiteral("structureTree"), information.hasStructureTree);
        if (!information.trapped.isEmpty())
        {
            data.insert(QStringLiteral("trapped"), Report::sanitize(QString::fromLatin1(information.trapped)));
        }
        data.insert(QStringLiteral("form"), information.formType);
        data.insert(QStringLiteral("pageCount"), qint64(information.pageCount));
        if (information.pageCount > 0)
        {
            data.insert(QStringLiteral("pageWidth"), round(information.firstPageSizeMM.width(), 2));
            data.insert(QStringLiteral("pageHeight"), round(information.firstPageSizeMM.height(), 2));
            if (!information.paperFormat.isEmpty())
            {
                data.insert(QStringLiteral("paperFormat"), information.paperFormat);
            }
        }
        if (!options.fileName.isEmpty())
        {
            data.insert(QStringLiteral("fileName"), Report::sanitize(options.fileName));
        }
        if (!options.originalData.isEmpty())
        {
            data.insert(QStringLiteral("fileSize"), qint64(options.originalData.size()));
        }
        data.insert(QStringLiteral("encrypted"), information.isEncrypted);
        if (information.isEncrypted)
        {
            data.insert(QStringLiteral("encryption"), information.encryptionMethod);
            data.insert(QStringLiteral("encryptionRevision"), information.encryptionRevision);

            QJsonArray permissions;
            for (const auto& permission : information.permissions)
            {
                QJsonObject item;
                item.insert(QStringLiteral("id"), Report::getPermissionId(permission.first));
                item.insert(QStringLiteral("allowed"), permission.second);
                permissions.append(item);
            }
            data.insert(QStringLiteral("permissions"), permissions);
        }
        data.insert(QStringLiteral("conformance"), QJsonArray::fromStringList(information.conformance));
        data.insert(QStringLiteral("repaired"), information.isRepaired);
        return data;
    }

    static QJsonObject createFileHashesData(const Report::Options& options)
    {
        QJsonObject data;
        data.insert(QStringLiteral("available"), !options.originalData.isEmpty());
        if (!options.originalData.isEmpty())
        {
            QJsonArray hashes;
            for (const auto& hash : Report::getFileHashes(options.originalData))
            {
                QJsonObject item;
                item.insert(QStringLiteral("algorithm"), hash.first);
                item.insert(QStringLiteral("hash"), hash.second);
                hashes.append(item);
            }
            data.insert(QStringLiteral("hashes"), hashes);
        }
        return data;
    }

    static QJsonObject createMetadataData(const PDFDocument* document)
    {
        bool hasMetadata = false;
        QString errorMessage;
        const QString text = Report::getMetadata(document, &hasMetadata, &errorMessage);

        QJsonObject data;
        data.insert(QStringLiteral("present"), hasMetadata);
        if (!errorMessage.isEmpty())
        {
            data.insert(QStringLiteral("error"), errorMessage);
        }
        else if (hasMetadata)
        {
            data.insert(QStringLiteral("text"), text);
        }
        return data;
    }

    static QJsonObject createJavaScriptData(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, const PDFOperationControl* control)
    {
        const PDFJavaScriptScanner scanner(document);
        const PDFJavaScriptScanner::Result result = scanner.scanDocument(pageIndices, PDFJavaScriptScanner::ScanMask, control);

        QJsonArray scripts;
        for (const PDFJavaScriptEntry& entry : result.entries)
        {
            QJsonObject item;
            item.insert(QStringLiteral("context"), getContextId(entry.type));
            if (!entry.name.isEmpty())
            {
                item.insert(QStringLiteral("name"), entry.name);
            }
            if (!entry.trigger.isEmpty())
            {
                item.insert(QStringLiteral("trigger"), entry.trigger);
            }
            if (entry.pageIndex >= 0)
            {
                item.insert(QStringLiteral("page"), qint64(entry.pageIndex + 1));
            }
            if (entry.actionReference.isValid())
            {
                item.insert(QStringLiteral("object"), QStringLiteral("%1 %2 R").arg(entry.actionReference.objectNumber).arg(entry.actionReference.generation));
            }
            item.insert(QStringLiteral("code"), entry.javaScript);
            item.insert(QStringLiteral("truncated"), entry.isTruncated);
            scripts.append(item);
        }

        QJsonObject data;
        data.insert(QStringLiteral("scripts"), scripts);
        data.insert(QStringLiteral("limitReached"), result.isLimitReached);
        data.insert(QStringLiteral("xfa"), result.hasXfa);
        return data;
    }

    static QJsonObject createNamedDestinationsData(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices)
    {
        QJsonArray destinations;
        const std::vector<PDFDocumentReportDestination> items = Report::getNamedDestinations(document, pageIndices);
        for (const PDFDocumentReportDestination& destination : items)
        {
            QJsonObject item;
            item.insert(QStringLiteral("name"), destination.name);
            item.insert(QStringLiteral("type"), destination.type);
            if (destination.pageIndex >= 0)
            {
                item.insert(QStringLiteral("page"), qint64(destination.pageIndex + 1));
            }
            const std::pair<const char*, const std::optional<PDFReal>*> values[] = { { "left", &destination.left }, { "top", &destination.top }, { "right", &destination.right },
                                                                                       { "bottom", &destination.bottom }, { "zoom", &destination.zoom } };
            for (const auto& value : values)
            {
                if (value.second->has_value())
                {
                    item.insert(QLatin1String(value.first), round(value.second->value(), 4));
                }
            }
            destinations.append(item);
        }

        QJsonObject data;
        data.insert(QStringLiteral("destinations"), destinations);
        data.insert(QStringLiteral("limitReached"), int(items.size()) >= Report::MAXIMUM_ROWS);
        return data;
    }

    static QJsonObject createPageBoxesData(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices)
    {
        QJsonArray groups;
        for (const PDFDocumentReportPageBoxes& boxes : Report::getPageBoxes(document, pageIndices))
        {
            QJsonObject group;
            group.insert(QStringLiteral("pages"), PDFDocumentToolPageSelection::createRangeText(boxes.pageIndices));
            group.insert(QStringLiteral("userUnit"), round(boxes.userUnit, 4));
            group.insert(QStringLiteral("media"), toJson(boxes.mediaBox));
            group.insert(QStringLiteral("crop"), toJson(boxes.cropBox));
            group.insert(QStringLiteral("bleed"), toJson(boxes.bleedBox));
            group.insert(QStringLiteral("trim"), toJson(boxes.trimBox));
            group.insert(QStringLiteral("art"), toJson(boxes.artBox));
            groups.append(group);
        }

        QJsonObject data;
        data.insert(QStringLiteral("groups"), groups);
        return data;
    }

    static QJsonObject createStructureTreeData(const PDFDocument* document, const PDFOperationControl* control)
    {
        const PDFCatalog* catalog = document->getCatalog();
        const bool isPresent = document->getDictionaryFromObject(catalog->getStructureTreeRoot()) != nullptr;
        QJsonArray elements;
        bool isLimitReached = false;
        if (isPresent)
        {
            const PDFStructureTree tree = PDFStructureTree::parse(&document->getStorage(), catalog->getStructureTreeRoot());
            const std::map<PDFObjectReference, PDFInteger> pages = getPageMap(document);

            // Depth first in the order of the tree, iterative (the parser bounds the depth)
            class Item
            {
            public:
                const PDFStructureItem* item = nullptr;
                int level = 0;
                PDFInteger pageIndex = -1;
            };

            std::vector<Item> stack;
            for (size_t i = tree.getChildCount(); i > 0; --i)
            {
                stack.push_back({ tree.getChild(i - 1), 1, -1 });
            }

            qint64 steps = 0;
            while (!stack.empty())
            {
                const Item current = stack.back();
                stack.pop_back();
                if ((++steps % 1024) == 0 && PDFOperationControl::isOperationCanceled(control))
                {
                    break;
                }

                const PDFStructureElement* element = current.item ? current.item->asStructureElement() : nullptr;
                if (!element)
                {
                    continue;
                }
                if (elements.size() >= Report::MAXIMUM_ROWS)
                {
                    isLimitReached = true;
                    break;
                }

                // /Pg of the element, else of its nearest ancestor (14.7.2, Table 355)
                const PDFInteger ownPage = element->getPageReference().isValid() ? findPage(pages, element->getPageReference()) : -1;
                const PDFInteger pageIndex = ownPage >= 0 ? ownPage : current.pageIndex;

                const qint64 contentItems = getContentItemCount(document, element);

                QJsonObject item;
                item.insert(QStringLiteral("level"), current.level);
                item.insert(QStringLiteral("type"), Report::sanitize(QString::fromLatin1(element->getTypeName().left(128))));
                if (pageIndex >= 0)
                {
                    item.insert(QStringLiteral("page"), qint64(pageIndex + 1));
                }
                const std::pair<const char*, PDFStructureElement::StringValue> texts[] = { { "title", PDFStructureElement::Title }, { "language", PDFStructureElement::Language },
                                                                                           { "alternativeDescription", PDFStructureElement::AlternativeDescription },
                                                                                           { "actualText", PDFStructureElement::ActualText }, { "expandedForm", PDFStructureElement::ExpandedForm } };
                for (const auto& text : texts)
                {
                    const QString& value = element->getText(text.second);
                    if (!value.isEmpty())
                    {
                        item.insert(QLatin1String(text.first), Report::sanitize(value));
                    }
                }
                item.insert(QStringLiteral("contentItems"), contentItems);
                elements.append(item);

                for (size_t i = element->getChildCount(); i > 0; --i)
                {
                    stack.push_back({ element->getChild(i - 1), current.level + 1, pageIndex });
                }
            }
        }

        QJsonObject data;
        data.insert(QStringLiteral("present"), isPresent);
        data.insert(QStringLiteral("elements"), elements);
        data.insert(QStringLiteral("limitReached"), isLimitReached);
        return data;
    }

    /// The content items of an element (14.7.2, Table 355 /K): marked-content sequences (an integer, a
    /// marked-content reference) and objects (an object reference) - read from the element dictionary
    /// (written when the parser of the tree kept only the dictionaries of its kids; it keeps the integer
    /// kids too since 2026-11-25, the count is the same)
    static qint64 getContentItemCount(const PDFDocument* document, const PDFStructureElement* element)
    {
        const PDFDictionary* dictionary = element->getSelfReference().isValid() ? document->getDictionaryFromObject(PDFObject::createReference(element->getSelfReference())) : nullptr;
        if (!dictionary)
        {
            qint64 count = 0;
            for (size_t i = 0; i < element->getChildCount(); ++i)
            {
                const PDFStructureItem* child = element->getChild(i);
                count += (child->asStructureMarkedContentReference() || child->asStructureObjectReference()) ? 1 : 0;
            }
            return count;
        }

        auto isContentItem = [document](const PDFObject& kid)
        {
            const PDFObject& object = document->getObject(kid);
            if (object.isInt())
            {
                return true;
            }
            const QByteArray type = getName(document, object.isDictionary() ? object.getDictionary() : nullptr, "Type");
            return type == "MCR" || type == "OBJR";
        };

        const PDFObject& kids = document->getObject(dictionary->get("K"));
        if (!kids.isArray())
        {
            return isContentItem(kids) ? 1 : 0;
        }

        qint64 count = 0;
        for (const PDFObject& kid : *kids.getArray())
        {
            count += isContentItem(kid) ? 1 : 0;
        }
        return count;
    }

    static QJsonObject createInksData(const PDFDocument* document, const PDFOperationControl* control)
    {
        QJsonArray inks;
        for (const PDFDocumentReportInk& ink : Report::getInks(document, control))
        {
            QJsonObject item;
            item.insert(QStringLiteral("name"), Report::sanitize(ink.textName.isEmpty() ? QString::fromUtf8(ink.name) : ink.textName));
            item.insert(QStringLiteral("spot"), ink.isSpot);
            item.insert(QStringLiteral("color"), ink.color.name(QColor::HexRgb).toUpper());
            inks.append(item);
        }

        QJsonObject data;
        data.insert(QStringLiteral("inks"), inks);
        return data;
    }

    static QJsonObject createFontsData(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, bool isCharacterMapIncluded, const PDFOperationControl* control)
    {
        bool isLimitReached = false;
        QJsonArray fonts;
        for (const PDFDocumentReportFont& font : Report::getFonts(document, pageIndices, isCharacterMapIncluded, control, &isLimitReached))
        {
            QJsonObject item;
            item.insert(QStringLiteral("name"), font.name);
            if (!font.family.isEmpty())
            {
                item.insert(QStringLiteral("family"), font.family);
            }
            item.insert(QStringLiteral("type"), font.type);
            if (!font.encoding.isEmpty())
            {
                item.insert(QStringLiteral("encoding"), font.encoding);
            }
            item.insert(QStringLiteral("embedded"), font.isEmbedded);
            item.insert(QStringLiteral("subset"), font.isSubset);
            item.insert(QStringLiteral("toUnicode"), font.hasToUnicode);
            if (!font.substitute.isEmpty())
            {
                item.insert(QStringLiteral("substitute"), font.substitute);
            }
            if (font.reference.isValid())
            {
                item.insert(QStringLiteral("object"), QStringLiteral("%1 %2 R").arg(font.reference.objectNumber).arg(font.reference.generation));
            }
            item.insert(QStringLiteral("pages"), PDFDocumentToolPageSelection::createRangeText(font.pageIndices));
            if (!font.characterInfos.empty())
            {
                QJsonArray characters;
                for (const CharacterInfo& character : font.characterInfos)
                {
                    QJsonObject characterItem;
                    characterItem.insert(QStringLiteral("glyph"), qint64(character.gid));
                    if (!character.character.isNull())
                    {
                        characterItem.insert(QStringLiteral("unicode"), qint64(character.character.unicode()));
                    }
                    characters.append(characterItem);
                }
                item.insert(QStringLiteral("characters"), characters);
            }
            fonts.append(item);
        }

        QJsonObject data;
        data.insert(QStringLiteral("fonts"), fonts);
        data.insert(QStringLiteral("limitReached"), isLimitReached);
        return data;
    }

    // ------------------------------------------------------------ model

    /// The values of the texts of the model
    class Context
    {
    public:
        const PDFDocumentToolValueFormatter* formatter = nullptr;
        PDFDocumentToolDateFormat dateFormat = PDFDocumentToolDateFormat::Iso;

        QString integer(qint64 value) const { return formatter->formatInteger(value); }
        QString real(double value, int decimals) const { return formatter->formatReal(value, decimals); }
    };

    static QString yesNo(bool value)
    {
        return value ? Report::tr("Yes") : Report::tr("No");
    }

    /// A JSON value as a cell: an integer, a real, a text; an absent value is an empty cell
    static QVariant cell(const QJsonValue& value, ValueType type)
    {
        switch (type)
        {
            case ValueType::Integer:
                return value.isDouble() ? QVariant(qint64(value.toDouble())) : QVariant();
            case ValueType::Real:
                return value.isDouble() ? QVariant(value.toDouble()) : QVariant();
            case ValueType::DateTime:
                return value.isString() ? QVariant(QDateTime::fromString(value.toString(), Qt::ISODate)) : QVariant();
            case ValueType::Text:
            case ValueType::DocumentText:
                return value.isString() ? QVariant(value.toString()) : QVariant();
        }
        return QVariant();
    }

    static QString getStandardInfoTitle(const QString& key)
    {
        if (key == QLatin1String("Title"))
        {
            return Report::tr("Title");
        }
        if (key == QLatin1String("Author"))
        {
            return Report::tr("Author");
        }
        if (key == QLatin1String("Subject"))
        {
            return Report::tr("Subject");
        }
        if (key == QLatin1String("Keywords"))
        {
            return Report::tr("Keywords");
        }
        if (key == QLatin1String("Creator"))
        {
            return Report::tr("Creator");
        }
        if (key == QLatin1String("Producer"))
        {
            return Report::tr("Producer");
        }
        if (key == QLatin1String("CreationDate"))
        {
            return Report::tr("Created");
        }
        if (key == QLatin1String("ModDate"))
        {
            return Report::tr("Modified");
        }
        return key;
    }

    static QString getPermissionTitle(const QString& id)
    {
        using Permission = PDFSecurityHandler::Permission;
        const std::pair<Permission, const char*> titles[] = { { Permission::PrintLowResolution, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Print (low resolution)") },
                                                              { Permission::PrintHighResolution, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Print (high resolution)") },
                                                              { Permission::CopyContent, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Content extraction") },
                                                              { Permission::Accessibility, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Content extraction (accessibility)") },
                                                              { Permission::Assemble, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Page assembling") },
                                                              { Permission::Modify, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Modify content") },
                                                              { Permission::ModifyInteractiveItems, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Modify interactive items") },
                                                              { Permission::ModifyFormFields, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Fill form fields") } };
        for (const auto& title : titles)
        {
            if (Report::getPermissionId(title.first) == id)
            {
                return Report::tr(title.second);
            }
        }
        return id;
    }

    static QString getTrappedText(const QJsonValue& value)
    {
        if (!value.isString())
        {
            return Report::tr("Not set");
        }
        const QString trapped = value.toString();
        if (trapped == QLatin1String("True"))
        {
            return Report::tr("Yes");
        }
        if (trapped == QLatin1String("False"))
        {
            return Report::tr("No");
        }
        if (trapped == QLatin1String("Unknown"))
        {
            return Report::tr("Unknown");
        }
        return trapped;
    }

    static QString getFormText(const QString& form)
    {
        if (form == QLatin1String("AcroForm"))
        {
            return Report::tr("AcroForm");
        }
        if (form == QLatin1String("XFA"))
        {
            return Report::tr("XFA");
        }
        return Report::tr("None");
    }

    static void addRow(Table& table, const QString& property, const QString& value)
    {
        table.addRow({ property, value });
    }

    static void createBasicInformationModel(const QJsonObject& data, const Context& context, PDFDocumentToolTableSection* section)
    {
        Table table;
        table.name = QStringLiteral("properties");
        table.title = Report::tr("Properties");
        table.addColumn(QStringLiteral("property"), Report::tr("Property"), ValueType::Text);
        table.addColumn(QStringLiteral("value"), Report::tr("Value"), ValueType::DocumentText);

        // Info (Table 349): the standard keys, then the custom ones
        const QJsonArray info = data.value(QStringLiteral("info")).toArray();
        for (const bool isCustom : { false, true })
        {
            for (const QJsonValue& value : info)
            {
                const QJsonObject entry = value.toObject();
                if (entry.value(QStringLiteral("custom")).toBool() != isCustom)
                {
                    continue;
                }

                const QString key = entry.value(QStringLiteral("key")).toString();
                const QDateTime dateTime = QDateTime::fromString(entry.value(QStringLiteral("date")).toString(), Qt::ISODate);
                const QString text = dateTime.isValid() ? context.formatter->formatDateTime(dateTime, context.dateFormat) : entry.value(QStringLiteral("value")).toString();
                addRow(table, isCustom ? key : getStandardInfoTitle(key), text);
            }
        }

        addRow(table, Report::tr("PDF version"), data.value(QStringLiteral("version")).toString());
        addRow(table, Report::tr("Tagged"), yesNo(data.value(QStringLiteral("tagged")).toBool()));
        addRow(table, Report::tr("Structure tree"), yesNo(data.value(QStringLiteral("structureTree")).toBool()));
        addRow(table, Report::tr("Trapped"), getTrappedText(data.value(QStringLiteral("trapped"))));
        addRow(table, Report::tr("Form"), getFormText(data.value(QStringLiteral("form")).toString()));
        const qint64 pageCount = qint64(data.value(QStringLiteral("pageCount")).toDouble());
        addRow(table, Report::tr("Pages"), context.integer(pageCount));
        if (pageCount > 0)
        {
            QString size = Report::tr("%1 × %2 mm").arg(context.real(data.value(QStringLiteral("pageWidth")).toDouble(), 1), context.real(data.value(QStringLiteral("pageHeight")).toDouble(), 1));
            const QString paperFormat = data.value(QStringLiteral("paperFormat")).toString();
            if (!paperFormat.isEmpty())
            {
                size = Report::tr("%1 (%2)").arg(size, paperFormat);
            }
            addRow(table, Report::tr("Page size (page 1)"), size);
        }
        if (data.contains(QStringLiteral("fileName")))
        {
            addRow(table, Report::tr("File name"), data.value(QStringLiteral("fileName")).toString());
        }
        if (data.contains(QStringLiteral("fileSize")))
        {
            addRow(table, Report::tr("File size"), Report::tr("%1 bytes").arg(context.integer(qint64(data.value(QStringLiteral("fileSize")).toDouble()))));
        }

        const bool isEncrypted = data.value(QStringLiteral("encrypted")).toBool();
        addRow(table, Report::tr("Encryption"), isEncrypted ? data.value(QStringLiteral("encryption")).toString() : Report::tr("Not encrypted"));

        const QJsonArray conformance = data.value(QStringLiteral("conformance")).toArray();
        QStringList standards;
        for (const QJsonValue& value : conformance)
        {
            standards << value.toString();
        }
        addRow(table, Report::tr("Conformance"), standards.isEmpty() ? Report::tr("None declared") : standards.join(QStringLiteral(", ")));
        if (data.value(QStringLiteral("repaired")).toBool())
        {
            addRow(table, Report::tr("Repaired when opened"), Report::tr("Yes"));
        }
        section->addTable(std::move(table));

        // The permissions of an encrypted document (Table 22)
        if (isEncrypted)
        {
            Table permissions;
            permissions.name = QStringLiteral("permissions");
            permissions.title = Report::tr("Permissions");
            permissions.addColumn(QStringLiteral("permission"), Report::tr("Permission"), ValueType::Text);
            permissions.addColumn(QStringLiteral("allowed"), Report::tr("Allowed"), ValueType::Text);
            for (const QJsonValue& value : data.value(QStringLiteral("permissions")).toArray())
            {
                const QJsonObject permission = value.toObject();
                permissions.addRow({ getPermissionTitle(permission.value(QStringLiteral("id")).toString()), yesNo(permission.value(QStringLiteral("allowed")).toBool()) });
            }
            section->addTable(std::move(permissions));
        }
    }

    static void createFileHashesModel(const QJsonObject& data, PDFDocumentToolTableSection* section)
    {
        if (!data.value(QStringLiteral("available")).toBool())
        {
            section->addParagraph(Report::tr("The hashes of the file are not available: the document has unsaved changes. Save the document first."));
            return;
        }

        Table table;
        table.name = QStringLiteral("hashes");
        table.title = Report::tr("Hashes of the file");
        table.addColumn(QStringLiteral("algorithm"), Report::tr("Algorithm"), ValueType::Text);
        table.addColumn(QStringLiteral("hash"), Report::tr("Hash"), ValueType::Text);
        for (const QJsonValue& value : data.value(QStringLiteral("hashes")).toArray())
        {
            const QJsonObject hash = value.toObject();
            table.addRow({ hash.value(QStringLiteral("algorithm")).toString(), hash.value(QStringLiteral("hash")).toString() });
        }
        section->addTable(std::move(table));
    }

    static void createMetadataModel(const QJsonObject& data, PDFDocumentToolTableSection* section)
    {
        if (!data.value(QStringLiteral("present")).toBool())
        {
            section->addParagraph(Report::tr("The document has no XMP metadata."));
        }
        else if (data.contains(QStringLiteral("error")))
        {
            section->addParagraph(data.value(QStringLiteral("error")).toString());
        }
        else
        {
            section->addCode(data.value(QStringLiteral("text")).toString());
        }
    }

    static QString getContextText(const QString& id)
    {
        if (id == QLatin1String("document"))
        {
            return Report::tr("Document");
        }
        if (id == QLatin1String("named"))
        {
            return Report::tr("Named");
        }
        if (id == QLatin1String("form"))
        {
            return Report::tr("Form field");
        }
        if (id == QLatin1String("page"))
        {
            return Report::tr("Page");
        }
        if (id == QLatin1String("annotation"))
        {
            return Report::tr("Annotation");
        }
        if (id == QLatin1String("outline"))
        {
            return Report::tr("Outline");
        }
        if (id == QLatin1String("xfa"))
        {
            return Report::tr("XFA form");
        }
        return id;
    }

    static void createJavaScriptModel(const QJsonObject& data, const Context& context, PDFDocumentToolTableSection* section)
    {
        const QJsonArray scripts = data.value(QStringLiteral("scripts")).toArray();
        section->addParagraph(Report::tr("The scripts are shown as a text; they are never run."));
        if (data.value(QStringLiteral("limitReached")).toBool())
        {
            section->addParagraph(Report::tr("Not all scripts could be read: the document has too many of them, or a chain of actions is too long."));
        }
        if (scripts.isEmpty())
        {
            section->addParagraph(Report::tr("No JavaScript was found."));
            return;
        }

        Table table;
        table.name = QStringLiteral("overview");
        table.title = Report::tr("Scripts");
        table.addColumn(QStringLiteral("number"), Report::tr("No."), ValueType::Integer);
        table.addColumn(QStringLiteral("context"), Report::tr("Context"), ValueType::Text);
        table.addColumn(QStringLiteral("name"), Report::tr("Name"), ValueType::DocumentText);
        table.addColumn(QStringLiteral("trigger"), Report::tr("Trigger"), ValueType::DocumentText);
        table.addColumn(QStringLiteral("page"), Report::tr("Page"), ValueType::Integer);
        table.addColumn(QStringLiteral("length"), Report::tr("Characters"), ValueType::Integer);

        qint64 number = 0;
        for (const QJsonValue& value : scripts)
        {
            const QJsonObject script = value.toObject();
            table.addRow({ ++number, getContextText(script.value(QStringLiteral("context")).toString()), cell(script.value(QStringLiteral("name")), ValueType::DocumentText),
                           cell(script.value(QStringLiteral("trigger")), ValueType::DocumentText), cell(script.value(QStringLiteral("page")), ValueType::Integer),
                           qint64(script.value(QStringLiteral("code")).toString().size()) });
        }
        section->addTable(std::move(table));

        number = 0;
        for (const QJsonValue& value : scripts)
        {
            const QJsonObject script = value.toObject();
            PDFDocumentToolTableSection* scriptSection = section->addSection(QStringLiteral("script"), Report::tr("JavaScript #%1").arg(context.integer(++number)));
            if (script.value(QStringLiteral("truncated")).toBool())
            {
                scriptSection->addParagraph(Report::tr("The script could not be read whole; only its beginning is shown."));
            }
            scriptSection->addCode(script.value(QStringLiteral("code")).toString());
        }
    }

    static void createNamedDestinationsModel(const QJsonObject& data, const Context& context, PDFDocumentToolTableSection* section)
    {
        const QJsonArray destinations = data.value(QStringLiteral("destinations")).toArray();
        if (destinations.isEmpty())
        {
            section->addParagraph(Report::tr("The selected pages have no named destinations."));
            return;
        }

        Table table;
        table.name = QStringLiteral("destinations");
        table.title = Report::tr("Named destinations");
        table.addColumn(QStringLiteral("name"), Report::tr("Name"), ValueType::DocumentText);
        table.addColumn(QStringLiteral("page"), Report::tr("Page"), ValueType::Text);
        table.addColumn(QStringLiteral("type"), Report::tr("Type"), ValueType::Text);
        table.addColumn(QStringLiteral("left"), Report::tr("Left (page units)"), ValueType::Real);
        table.addColumn(QStringLiteral("top"), Report::tr("Top (page units)"), ValueType::Real);
        table.addColumn(QStringLiteral("right"), Report::tr("Right (page units)"), ValueType::Real);
        table.addColumn(QStringLiteral("bottom"), Report::tr("Bottom (page units)"), ValueType::Real);
        table.addColumn(QStringLiteral("zoom"), Report::tr("Zoom"), ValueType::Real);
        for (const QJsonValue& value : destinations)
        {
            const QJsonObject destination = value.toObject();
            const QJsonValue page = destination.value(QStringLiteral("page"));
            table.addRow({ destination.value(QStringLiteral("name")).toString(), page.isDouble() ? context.integer(qint64(page.toDouble())) : Report::tr("Unresolved"),
                           destination.value(QStringLiteral("type")).toString(), cell(destination.value(QStringLiteral("left")), ValueType::Real),
                           cell(destination.value(QStringLiteral("top")), ValueType::Real), cell(destination.value(QStringLiteral("right")), ValueType::Real),
                           cell(destination.value(QStringLiteral("bottom")), ValueType::Real), cell(destination.value(QStringLiteral("zoom")), ValueType::Real) });
        }
        section->addTable(std::move(table));
        if (data.value(QStringLiteral("limitReached")).toBool())
        {
            section->addParagraph(Report::tr("The document has more named destinations; only the first %1 are listed.").arg(context.integer(Report::MAXIMUM_ROWS)));
        }
    }

    static void createPageBoxesModel(const QJsonObject& data, const Context& context, PDFDocumentToolTableSection* section)
    {
        const QJsonArray groups = data.value(QStringLiteral("groups")).toArray();
        if (groups.isEmpty())
        {
            section->addParagraph(Report::tr("No page is selected."));
            return;
        }

        const std::pair<const char*, const char*> boxes[] = { { "media", QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Media box") },
                                                              { "crop", QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Crop box") },
                                                              { "bleed", QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Bleed box") },
                                                              { "trim", QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Trim box") },
                                                              { "art", QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Art box") } };
        for (const QJsonValue& value : groups)
        {
            const QJsonObject group = value.toObject();
            Table table;
            table.name = QStringLiteral("pages");
            table.title = Report::tr("Pages %1").arg(group.value(QStringLiteral("pages")).toString());
            const double userUnit = group.value(QStringLiteral("userUnit")).toDouble(1.0);
            if (!qFuzzyCompare(userUnit, 1.0))
            {
                table.title = Report::tr("%1 (user unit %2 pt)").arg(table.title, context.real(userUnit, 4));
            }
            table.addColumn(QStringLiteral("box"), Report::tr("Box"), ValueType::Text);
            table.addColumn(QStringLiteral("left"), Report::tr("Left (mm)"), ValueType::Real);
            table.addColumn(QStringLiteral("bottom"), Report::tr("Bottom (mm)"), ValueType::Real);
            table.addColumn(QStringLiteral("right"), Report::tr("Right (mm)"), ValueType::Real);
            table.addColumn(QStringLiteral("top"), Report::tr("Top (mm)"), ValueType::Real);
            table.addColumn(QStringLiteral("width"), Report::tr("Width (mm)"), ValueType::Real);
            table.addColumn(QStringLiteral("height"), Report::tr("Height (mm)"), ValueType::Real);
            for (const auto& box : boxes)
            {
                // [left, top, right, bottom] of QRectF: the y of the page grows up, top < bottom by Qt
                const QJsonArray rect = group.value(QLatin1String(box.first)).toArray();
                const double left = rect.at(0).toDouble();
                const double low = rect.at(1).toDouble();
                const double right = rect.at(2).toDouble();
                const double high = rect.at(3).toDouble();
                table.addRow({ Report::tr(box.second), left, low, right, high, right - left, high - low });
            }
            section->addTable(std::move(table));
        }
    }

    static void createStructureTreeModel(const QJsonObject& data, const Context& context, PDFDocumentToolTableSection* section)
    {
        if (!data.value(QStringLiteral("present")).toBool())
        {
            section->addParagraph(Report::tr("The document has no structure tree."));
            return;
        }

        Table table;
        table.name = QStringLiteral("elements");
        table.title = Report::tr("Structure elements");
        table.addColumn(QStringLiteral("level"), Report::tr("Level"), ValueType::Integer);
        table.addColumn(QStringLiteral("type"), Report::tr("Type"), ValueType::DocumentText);
        table.addColumn(QStringLiteral("page"), Report::tr("Page"), ValueType::Integer);
        table.addColumn(QStringLiteral("title"), Report::tr("Title"), ValueType::DocumentText);
        table.addColumn(QStringLiteral("language"), Report::tr("Language"), ValueType::DocumentText);
        table.addColumn(QStringLiteral("alternativeDescription"), Report::tr("Alternative description"), ValueType::DocumentText);
        table.addColumn(QStringLiteral("actualText"), Report::tr("Actual text"), ValueType::DocumentText);
        table.addColumn(QStringLiteral("expandedForm"), Report::tr("Expanded form"), ValueType::DocumentText);
        table.addColumn(QStringLiteral("contentItems"), Report::tr("Content items"), ValueType::Integer);
        for (const QJsonValue& value : data.value(QStringLiteral("elements")).toArray())
        {
            const QJsonObject element = value.toObject();
            std::vector<QVariant> row;
            row.push_back(cell(element.value(QStringLiteral("level")), ValueType::Integer));
            for (const char* key : { "type", "page", "title", "language", "alternativeDescription", "actualText", "expandedForm", "contentItems" })
            {
                const bool isNumber = qstrcmp(key, "page") == 0 || qstrcmp(key, "contentItems") == 0;
                row.push_back(cell(element.value(QLatin1String(key)), isNumber ? ValueType::Integer : ValueType::DocumentText));
            }
            table.addRow(std::move(row));
        }
        section->addTable(std::move(table));
        if (data.value(QStringLiteral("limitReached")).toBool())
        {
            section->addParagraph(Report::tr("The structure tree has more elements; only the first %1 are listed.").arg(context.integer(Report::MAXIMUM_ROWS)));
        }
    }

    static void createInksModel(const QJsonObject& data, PDFDocumentToolTableSection* section)
    {
        Table table;
        table.name = QStringLiteral("inks");
        table.title = Report::tr("Inks");
        table.addColumn(QStringLiteral("number"), Report::tr("No."), ValueType::Integer);
        table.addColumn(QStringLiteral("name"), Report::tr("Name"), ValueType::DocumentText);
        table.addColumn(QStringLiteral("kind"), Report::tr("Kind"), ValueType::Text);
        table.addColumn(QStringLiteral("color"), Report::tr("Color"), ValueType::Text);
        qint64 number = 0;
        for (const QJsonValue& value : data.value(QStringLiteral("inks")).toArray())
        {
            const QJsonObject ink = value.toObject();
            table.addRow({ ++number, ink.value(QStringLiteral("name")).toString(), ink.value(QStringLiteral("spot")).toBool() ? Report::tr("Spot") : Report::tr("Process"),
                           ink.value(QStringLiteral("color")).toString() });
        }
        section->addTable(std::move(table));
    }

    static QString getFontTypeText(const QString& id)
    {
        if (id == QLatin1String("type0"))
        {
            return Report::tr("Type 0 (CID)");
        }
        if (id == QLatin1String("type1"))
        {
            return Report::tr("Type 1");
        }
        if (id == QLatin1String("mmType1"))
        {
            return Report::tr("Multiple master Type 1");
        }
        if (id == QLatin1String("trueType"))
        {
            return Report::tr("TrueType");
        }
        if (id == QLatin1String("type3"))
        {
            return Report::tr("Type 3");
        }
        return Report::tr("Unknown");
    }

    static void createFontsModel(const QJsonObject& data, const Context& context, PDFDocumentToolTableSection* section)
    {
        const QJsonArray fonts = data.value(QStringLiteral("fonts")).toArray();
        if (fonts.isEmpty())
        {
            section->addParagraph(Report::tr("The selected pages use no fonts."));
            return;
        }

        Table table;
        table.name = QStringLiteral("fonts");
        table.title = Report::tr("Fonts");
        table.addColumn(QStringLiteral("name"), Report::tr("Name"), ValueType::DocumentText);
        table.addColumn(QStringLiteral("type"), Report::tr("Type"), ValueType::Text);
        table.addColumn(QStringLiteral("encoding"), Report::tr("Encoding"), ValueType::DocumentText);
        table.addColumn(QStringLiteral("embedded"), Report::tr("Embedded"), ValueType::Text);
        table.addColumn(QStringLiteral("subset"), Report::tr("Subset"), ValueType::Text);
        table.addColumn(QStringLiteral("toUnicode"), Report::tr("Unicode map"), ValueType::Text);
        table.addColumn(QStringLiteral("substitute"), Report::tr("Substituted by"), ValueType::DocumentText);
        table.addColumn(QStringLiteral("object"), Report::tr("Object"), ValueType::Text);
        table.addColumn(QStringLiteral("pages"), Report::tr("Pages"), ValueType::Text);
        for (const QJsonValue& value : fonts)
        {
            const QJsonObject font = value.toObject();
            table.addRow({ font.value(QStringLiteral("name")).toString(), getFontTypeText(font.value(QStringLiteral("type")).toString()),
                           cell(font.value(QStringLiteral("encoding")), ValueType::DocumentText), yesNo(font.value(QStringLiteral("embedded")).toBool()),
                           yesNo(font.value(QStringLiteral("subset")).toBool()), yesNo(font.value(QStringLiteral("toUnicode")).toBool()),
                           cell(font.value(QStringLiteral("substitute")), ValueType::DocumentText), cell(font.value(QStringLiteral("object")), ValueType::Text),
                           font.value(QStringLiteral("pages")).toString() });
        }
        section->addTable(std::move(table));
        if (data.value(QStringLiteral("limitReached")).toBool())
        {
            section->addParagraph(Report::tr("Not all fonts could be read: the document has too many of them, or its resources are nested too deeply."));
        }

        // The character maps of the embedded fonts
        for (const QJsonValue& value : fonts)
        {
            const QJsonObject font = value.toObject();
            if (!font.contains(QStringLiteral("characters")))
            {
                continue;
            }

            Table characters;
            characters.name = QStringLiteral("characters");
            characters.title = Report::tr("Character map of the font %1").arg(font.value(QStringLiteral("name")).toString());
            characters.addColumn(QStringLiteral("glyph"), Report::tr("Glyph"), ValueType::Integer);
            characters.addColumn(QStringLiteral("character"), Report::tr("Character"), ValueType::DocumentText);
            characters.addColumn(QStringLiteral("unicode"), Report::tr("Unicode"), ValueType::Text);
            for (const QJsonValue& characterValue : font.value(QStringLiteral("characters")).toArray())
            {
                const QJsonObject character = characterValue.toObject();
                const QJsonValue unicode = character.value(QStringLiteral("unicode"));
                QVariant text;
                QVariant code;
                if (unicode.isDouble())
                {
                    const char16_t codePoint = char16_t(qBound(0, unicode.toInt(), 0xFFFF));
                    text = PDFDocumentToolTableWriter::toXmlText(QString(QChar(codePoint)));
                    code = QStringLiteral("U+%1").arg(QString::number(codePoint, 16).toUpper().rightJustified(4, QLatin1Char('0')));
                }
                characters.addRow({ cell(character.value(QStringLiteral("glyph")), ValueType::Integer), text, code });
            }
            section->addTable(std::move(characters));
        }
        Q_UNUSED(context);
    }
};

const PDFDocumentReportBasicInformation::InfoEntry* PDFDocumentReportBasicInformation::findInfo(const QByteArray& key) const
{
    for (const InfoEntry& entry : info)
    {
        if (entry.key == key)
        {
            return &entry;
        }
    }
    return nullptr;
}

PDFDocumentReportBasicInformation PDFDocumentReport::getBasicInformation(const PDFDocument* document)
{
    return getBasicInformation(document, { BasicInformationPart::Form, BasicInformationPart::Conformance });
}

PDFDocumentReportBasicInformation PDFDocumentReport::getBasicInformation(const PDFDocument* document, std::initializer_list<BasicInformationPart> parts)
{
    using Helper = PDFDocumentReportHelper;

    const bool isFormRead = std::find(parts.begin(), parts.end(), BasicInformationPart::Form) != parts.end();
    const bool isConformanceRead = std::find(parts.begin(), parts.end(), BasicInformationPart::Conformance) != parts.end();

    PDFDocumentReportBasicInformation result;
    if (!document)
    {
        return result;
    }

    const PDFDocumentInfo* info = document->getInfo();
    const PDFCatalog* catalog = document->getCatalog();
    result.version = QString::fromLatin1(document->getVersion().left(16));

    // Info (Table 349): the standard keys in the order of the table, then the custom keys
    auto addText = [&result](const char* key, const QString& value)
    {
        if (!value.isEmpty())
        {
            PDFDocumentReportBasicInformation::InfoEntry entry;
            entry.key = QByteArray(key);
            entry.value = sanitize(value);
            entry.isShortened = entry.value != value;
            result.info.push_back(std::move(entry));
        }
    };
    auto addDate = [&result](const char* key, const QDateTime& dateTime)
    {
        if (dateTime.isValid())
        {
            PDFDocumentReportBasicInformation::InfoEntry entry;
            entry.key = QByteArray(key);
            entry.dateTime = dateTime;
            entry.value = Helper::toIso(dateTime);
            result.info.push_back(std::move(entry));
        }
    };
    addText("Title", info->title);
    addText("Author", info->author);
    addText("Subject", info->subject);
    addText("Keywords", info->keywords);
    addText("Creator", info->creator);
    addText("Producer", info->producer);
    addDate("CreationDate", info->creationDate);
    addDate("ModDate", info->modifiedDate);

    int customCount = 0;
    for (const auto& extra : info->extra)
    {
        if (customCount >= MAXIMUM_CUSTOM_ENTRIES)
        {
            break;
        }
        if (extra.first.isEmpty() || extra.first.size() > 127)
        {
            continue;
        }

        PDFDocumentReportBasicInformation::InfoEntry entry;
        entry.key = extra.first;
        entry.isCustom = true;
        if (extra.second.typeId() == QMetaType::QDateTime)
        {
            entry.dateTime = extra.second.toDateTime();
            entry.value = Helper::toIso(entry.dateTime);
        }
        else
        {
            const QString value = extra.second.toString();
            entry.value = sanitize(value);
            entry.isShortened = entry.value != value;
        }
        result.info.push_back(std::move(entry));
        ++customCount;
    }

    // /Trapped as written (14.11.6; a boolean of PDF 1.3 is True / False) - never as Tagged (B204)
    if (const PDFDictionary* infoDictionary = Helper::getTrailerEntry(document, "Info"))
    {
        const PDFObject& trapped = document->getObject(infoDictionary->get("Trapped"));
        if (trapped.isName())
        {
            result.trapped = trapped.getString().left(64);
        }
        else if (trapped.isBool())
        {
            result.trapped = trapped.getBool() ? QByteArrayLiteral("True") : QByteArrayLiteral("False");
        }
    }

    // Tagged: /MarkInfo /Marked (14.7.1, Table 353) - the one definition of R7-5
    result.isTagged = catalog->isLogicalStructureMarked();
    result.hasStructureTree = document->getDictionaryFromObject(catalog->getStructureTreeRoot()) != nullptr;

    if (isFormRead)
    {
        try
        {
            const PDFForm form = PDFForm::parse(document, catalog->getFormObject());
            switch (form.getFormType())
            {
                case PDFForm::FormType::AcroForm:
                    result.formType = QStringLiteral("AcroForm");
                    break;
                case PDFForm::FormType::XFAForm:
                    result.formType = QStringLiteral("XFA");
                    break;
                default:
                    result.formType = QStringLiteral("None");
                    break;
            }
        }
        catch (const PDFException&)
        {
            result.formType = QStringLiteral("None");
        }
    }

    result.pageCount = PDFInteger(catalog->getPageCount());
    if (result.pageCount > 0)
    {
        const PDFPage* page = catalog->getPage(0);
        result.firstPageSizeMM = page->getPhysicalRectMM(page->getRotatedMediaBox()).size();
        result.paperFormat = getPaperFormatName(result.firstPageSizeMM);
    }

    // The encryption (7.6, Table 20) and the permissions (Table 22)
    const PDFSecurityHandler* handler = document->getStorage().getSecurityHandler();
    result.encryptionMode = handler ? handler->getMode() : EncryptionMode::None;
    result.isEncrypted = result.encryptionMode != EncryptionMode::None;
    if (handler && result.isEncrypted)
    {
        const PDFDictionary* encrypt = Helper::getTrailerEntry(document, "Encrypt");
        result.encryptionVersion = handler->getVersion();
        result.encryptionRevision = int(Helper::getInteger(document, encrypt, "R", 0));
        result.authorization = handler->getAuthorizationResult();
        result.isMetadataEncrypted = handler->isMetadataEncrypted();

        const QString filter = sanitize(QString::fromLatin1(Helper::getName(document, encrypt, "Filter").left(64)));
        const QString algorithm = Helper::getEncryptionAlgorithm(document, encrypt, result.encryptionVersion);
        result.encryptionMethod = result.encryptionRevision > 0 ? tr("%1, revision %2, %3").arg(filter).arg(result.encryptionRevision).arg(algorithm) : tr("%1, %2").arg(filter, algorithm);
    }

    const bool isOwner = !handler || !result.isEncrypted || handler->getAuthorizationResult() == PDFSecurityHandler::AuthorizationResult::OwnerAuthorized;
    for (const PDFSecurityHandler::Permission permission : Helper::getPermissions())
    {
        result.permissions.emplace_back(permission, isOwner || PDFSecurityHandler::isAllowedByRevision(handler, permission));
    }

    if (isConformanceRead)
    {
        bool hasMetadata = false;
        const QString metadata = getMetadata(document, &hasMetadata, nullptr);
        result.conformance = getConformance(metadata);
    }
    return result;
}

QString PDFDocumentReport::getMetadata(const PDFDocument* document, bool* hasMetadata, QString* errorMessage)
{
    using Helper = PDFDocumentReportHelper;

    if (hasMetadata)
    {
        *hasMetadata = false;
    }
    if (errorMessage)
    {
        errorMessage->clear();
    }
    if (!document)
    {
        return QString();
    }

    const PDFObject& metadata = document->getObject(document->getCatalog()->getMetadata());
    if (!metadata.isStream())
    {
        return QString();
    }
    if (hasMetadata)
    {
        *hasMetadata = true;
    }

    // Q7.18: the limited decoding - a packet over the limit is never decoded whole
    const PDFObjectStorage& storage = document->getStorage();
    const PDFLimitedStreamDecoder::Result result = PDFLimitedStreamDecoder::decode(metadata.getStream(), [&storage](const PDFObject& object) -> const PDFObject& { return storage.getObject(object); },
                                                                                    storage.getSecurityHandler(), MAXIMUM_METADATA_SIZE, nullptr);
    QString error;
    switch (result.status)
    {
        case PDFLimitedStreamDecoder::Status::Decoded:
            return Helper::decodeMetadata(result.data);

        case PDFLimitedStreamDecoder::Status::LimitExceeded:
            error = tr("The metadata packet is too large to be shown (more than %1).").arg(PDFDocumentToolFormat::formatSize(MAXIMUM_METADATA_SIZE));
            break;

        case PDFLimitedStreamDecoder::Status::Encoded:
            error = tr("The metadata packet cannot be read: its filter %1 is not supported.").arg(sanitize(QString::fromLatin1(result.filterName.left(64))));
            break;

        case PDFLimitedStreamDecoder::Status::Error:
        case PDFLimitedStreamDecoder::Status::Canceled:
            error = tr("The metadata packet cannot be read: %1").arg(sanitize(result.errorMessage));
            break;
    }

    if (errorMessage)
    {
        *errorMessage = error;
    }
    return QString();
}

std::vector<PDFDocumentReportFont> PDFDocumentReport::getFonts(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, bool isCharacterMapIncluded, const PDFOperationControl* control,
                                                               bool* isLimitReached)
{
    using Helper = PDFDocumentReportHelper;

    std::vector<PDFDocumentReportFont> fonts;
    bool limitReached = false;
    if (isLimitReached)
    {
        *isLimitReached = false;
    }
    if (!document)
    {
        return fonts;
    }

    // K7.24: the one walk over the resources (pages, forms, appearance streams, patterns, Type 3 fonts,
    // soft masks) - each owner once, every edge kept for the pages of a node
    const PDFResourceWalkResult walk = PDFResourceWalk::walk(document, pageIndices, PDFResourceWalk::Options(), control);
    if (walk.isCanceled || PDFOperationControl::isOperationCanceled(control))
    {
        return std::vector<PDFDocumentReportFont>();
    }
    limitReached = walk.isLimitReached;

    // Review RS7CORE: the fonts of each /Font dictionary are read once (a dictionary shared by many owners
    // was read by each of them, a direct font realized each time), and the pages of the fonts come from one
    // walk from each page over the edges (the pages of each node by a walk backwards over the whole graph
    // were quadratic in the nodes, and N owners of K fonts used by P pages inserted N * K * P page numbers)
    std::map<PDFObjectReference, size_t> referencedFonts;
    std::map<QString, size_t> directFonts;
    std::map<const PDFDictionary*, size_t> groupOfDictionary;      // A /Font dictionary and its fonts (the index of fontGroups)
    std::vector<std::vector<size_t>> fontGroups;                   // The fonts of each /Font dictionary (indices of fonts)
    std::vector<size_t> groupOfNode(walk.nodes.size(), SIZE_MAX);
    for (size_t nodeIndex = 0; nodeIndex < walk.nodes.size(); ++nodeIndex)
    {
        const PDFResourceWalkNode& node = walk.nodes[nodeIndex];
        const PDFDictionary* resources = document->getDictionaryFromObject(node.resources);
        const PDFDictionary* fontDictionary = resources ? document->getDictionaryFromObject(resources->get("Font")) : nullptr;
        if (!fontDictionary)
        {
            continue;
        }

        auto itGroup = groupOfDictionary.find(fontDictionary);
        if (itGroup != groupOfDictionary.cend())
        {
            groupOfNode[nodeIndex] = itGroup->second;
            continue;
        }

        groupOfNode[nodeIndex] = fontGroups.size();
        groupOfDictionary.emplace(fontDictionary, fontGroups.size());
        fontGroups.emplace_back();
        std::vector<size_t>& group = fontGroups.back();
        for (size_t i = 0; i < fontDictionary->getCount(); ++i)
        {
            if (PDFOperationControl::isOperationCanceled(control))
            {
                return std::vector<PDFDocumentReportFont>();
            }

            const PDFObject& object = fontDictionary->getValue(i);
            size_t fontIndex = fonts.size();
            if (object.isReference())
            {
                auto it = referencedFonts.find(object.getReference());
                if (it != referencedFonts.cend())
                {
                    fontIndex = it->second;
                }
            }

            if (fontIndex == fonts.size())
            {
                if (int(fonts.size()) >= MAXIMUM_FONTS)
                {
                    limitReached = true;
                    continue;
                }

                PDFDocumentReportFont font;
                if (!Helper::readFont(document, object, fontDictionary->getKey(i).getString(), isCharacterMapIncluded, &font))
                {
                    continue;
                }

                if (object.isReference())
                {
                    referencedFonts.emplace(object.getReference(), fonts.size());
                }
                else
                {
                    // A direct font of more resources is the same font, when everything of it is the same
                    const QString key = QStringLiteral("%1\n%2\n%3\n%4\n%5\n%6").arg(font.name, font.family, font.type, font.encoding).arg(int(font.isEmbedded)).arg(int(font.hasToUnicode));
                    auto it = directFonts.find(key);
                    if (it != directFonts.cend())
                    {
                        fontIndex = it->second;
                    }
                    else
                    {
                        directFonts.emplace(key, fonts.size());
                    }
                }

                if (fontIndex == fonts.size())
                {
                    fonts.push_back(std::move(font));
                }
            }

            group.push_back(fontIndex);
        }
    }

    // The pages: from each walked page forward over the edges, each node and each /Font dictionary once per
    // page, a font at most once per page; the work has the budget of the nodes of a walk
    std::vector<size_t> nodeStamp(walk.nodes.size(), SIZE_MAX);
    std::vector<size_t> groupStamp(fontGroups.size(), SIZE_MAX);
    std::vector<size_t> fontStamp(fonts.size(), SIZE_MAX);
    std::vector<size_t> queue;
    qint64 steps = 0;
    bool isBudgetSpent = false;
    for (size_t pageItem = 0; pageItem < walk.pages.size() && !isBudgetSpent; ++pageItem)
    {
        const PDFInteger pageIndex = walk.pages[pageItem].first;
        const size_t pageNode = walk.pages[pageItem].second;
        if (pageNode >= walk.nodes.size() || nodeStamp[pageNode] == pageItem)
        {
            continue;
        }

        queue.assign(1, pageNode);
        nodeStamp[pageNode] = pageItem;
        for (size_t queueIndex = 0; queueIndex < queue.size(); ++queueIndex)
        {
            const size_t nodeIndex = queue[queueIndex];
            if (++steps > PDFObjectWalk::DEFAULT_MAXIMUM_NODES)
            {
                isBudgetSpent = true;
                limitReached = true;
                break;
            }
            if ((steps % 4096) == 0 && PDFOperationControl::isOperationCanceled(control))
            {
                return std::vector<PDFDocumentReportFont>();
            }

            const size_t groupIndex = groupOfNode[nodeIndex];
            if (groupIndex != SIZE_MAX && groupStamp[groupIndex] != pageItem)
            {
                groupStamp[groupIndex] = pageItem;
                steps += qint64(fontGroups[groupIndex].size());
                for (const size_t fontIndex : fontGroups[groupIndex])
                {
                    if (fontStamp[fontIndex] != pageItem)
                    {
                        fontStamp[fontIndex] = pageItem;
                        fonts[fontIndex].pageIndices.push_back(pageIndex);
                    }
                }
            }

            for (const size_t child : walk.nodes[nodeIndex].children)
            {
                if (child < walk.nodes.size() && nodeStamp[child] != pageItem)
                {
                    nodeStamp[child] = pageItem;
                    queue.push_back(child);
                }
            }
        }
    }

    for (PDFDocumentReportFont& font : fonts)
    {
        std::sort(font.pageIndices.begin(), font.pageIndices.end());
        font.pageIndices.erase(std::unique(font.pageIndices.begin(), font.pageIndices.end()), font.pageIndices.end());
    }

    std::stable_sort(fonts.begin(), fonts.end(), [](const PDFDocumentReportFont& left, const PDFDocumentReportFont& right)
    {
        const int comparison = left.name.compare(right.name, Qt::CaseInsensitive);
        return comparison != 0 ? comparison < 0 : left.reference < right.reference;
    });

    if (isLimitReached)
    {
        *isLimitReached = limitReached;
    }
    return fonts;
}

std::vector<PDFDocumentReportDestination> PDFDocumentReport::getNamedDestinations(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices)
{
    using Helper = PDFDocumentReportHelper;

    std::vector<PDFDocumentReportDestination> result;
    if (!document)
    {
        return result;
    }

    const PDFCatalog* catalog = document->getCatalog();
    const PDFInteger pageCount = PDFInteger(catalog->getPageCount());
    std::set<PDFInteger> selectedPages;
    for (const PDFInteger pageIndex : pageIndices)
    {
        if (pageIndex >= 0 && pageIndex < pageCount)
        {
            selectedPages.insert(pageIndex);
        }
    }

    // An unresolved destination belongs to no page: it is listed with all pages (B189)
    const bool isAllPages = PDFInteger(selectedPages.size()) == pageCount;
    const std::map<PDFObjectReference, PDFInteger> pages = Helper::getPageMap(document);
    for (const auto& item : catalog->getNamedDestinations())
    {
        if (int(result.size()) >= MAXIMUM_ROWS)
        {
            break;
        }

        const PDFDestination& destination = item.second;
        PDFDocumentReportDestination entry;
        entry.name = sanitize(PDFEncoding::convertSmartFromByteStringToUnicode(item.first.left(MAXIMUM_VALUE_LENGTH), nullptr));
        entry.type = Helper::getDestinationTypeName(destination.getDestinationType());

        const DestinationType type = destination.getDestinationType();
        if (type != DestinationType::Invalid && type != DestinationType::Named)
        {
            if (destination.getPageReference().isValid())
            {
                entry.pageIndex = Helper::findPage(pages, destination.getPageReference());
            }
            else if (destination.getPageIndex() >= 0 && destination.getPageIndex() < pageCount)
            {
                entry.pageIndex = destination.getPageIndex();
            }
        }

        if (entry.pageIndex < 0 ? !isAllPages : selectedPages.count(entry.pageIndex) == 0)
        {
            continue;
        }

        // The values of the type (Table 149)
        switch (type)
        {
            case DestinationType::XYZ:
                entry.left = Helper::finite(destination.getLeft());
                entry.top = Helper::finite(destination.getTop());
                if (!qFuzzyIsNull(destination.getZoom()))
                {
                    entry.zoom = Helper::finite(destination.getZoom());
                }
                break;
            case DestinationType::FitH:
            case DestinationType::FitBH:
                entry.top = Helper::finite(destination.getTop());
                break;
            case DestinationType::FitV:
            case DestinationType::FitBV:
                entry.left = Helper::finite(destination.getLeft());
                break;
            case DestinationType::FitR:
                entry.left = Helper::finite(destination.getLeft());
                entry.top = Helper::finite(destination.getTop());
                entry.right = Helper::finite(destination.getRight());
                entry.bottom = Helper::finite(destination.getBottom());
                break;
            default:
                break;
        }
        result.push_back(std::move(entry));
    }

    // By the page (the unresolved last), then by the name
    std::stable_sort(result.begin(), result.end(), [](const PDFDocumentReportDestination& left, const PDFDocumentReportDestination& right)
    {
        const PDFInteger leftPage = left.pageIndex < 0 ? (std::numeric_limits<PDFInteger>::max)() : left.pageIndex;
        const PDFInteger rightPage = right.pageIndex < 0 ? (std::numeric_limits<PDFInteger>::max)() : right.pageIndex;
        return std::tie(leftPage, left.name) < std::tie(rightPage, right.name);
    });
    return result;
}

std::vector<PDFDocumentReportPageBoxes> PDFDocumentReport::getPageBoxes(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices)
{
    std::vector<PDFDocumentReportPageBoxes> result;
    if (!document)
    {
        return result;
    }

    const PDFCatalog* catalog = document->getCatalog();
    std::vector<PDFInteger> pages = pageIndices;
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());

    std::map<std::vector<double>, size_t> groups;
    for (const PDFInteger pageIndex : pages)
    {
        if (pageIndex < 0 || pageIndex >= PDFInteger(catalog->getPageCount()))
        {
            continue;
        }

        // The boxes as written (the defaults of Table 31) in mm with the effective /UserUnit
        const PDFPage* page = catalog->getPage(pageIndex);
        PDFDocumentReportPageBoxes boxes;
        boxes.userUnit = page->getEffectiveUserUnit();
        boxes.mediaBox = page->getPhysicalRectMM(page->getMediaBox());
        boxes.cropBox = page->getPhysicalRectMM(page->getCropBox());
        boxes.bleedBox = page->getPhysicalRectMM(page->getBleedBox());
        boxes.trimBox = page->getPhysicalRectMM(page->getTrimBox());
        boxes.artBox = page->getPhysicalRectMM(page->getArtBox());

        std::vector<double> key = { boxes.userUnit };
        for (const QRectF& rect : { boxes.mediaBox, boxes.cropBox, boxes.bleedBox, boxes.trimBox, boxes.artBox })
        {
            key.insert(key.end(), { rect.left(), rect.top(), rect.right(), rect.bottom() });
        }

        auto it = groups.find(key);
        if (it != groups.cend())
        {
            result[it->second].pageIndices.push_back(pageIndex);
        }
        else if (int(result.size()) < MAXIMUM_ROWS)
        {
            groups.emplace(std::move(key), result.size());
            boxes.pageIndices.push_back(pageIndex);
            result.push_back(std::move(boxes));
        }
    }
    return result;
}

std::vector<PDFDocumentReportInk> PDFDocumentReport::getInks(const PDFDocument* document, const PDFOperationControl* control)
{
    std::vector<PDFDocumentReportInk> result;
    if (!document)
    {
        return result;
    }

    // All pages, with the cancellation of the run (RS7CORE: a dictionary of a hostile document can have
    // millions of color spaces)
    std::vector<PDFInteger> pageIndices(document->getCatalog()->getPageCount(), 0);
    std::iota(pageIndices.begin(), pageIndices.end(), PDFInteger(0));

    PDFCMSManager manager(nullptr);
    manager.setDocument(document);
    PDFInkMapper mapper(&manager, document);
    mapper.createSpotColors(true, pageIndices, control);
    for (const PDFInkMapper::ColorInfo& info : mapper.getSeparations(4, true))
    {
        PDFDocumentReportInk ink;
        ink.name = info.name;
        ink.textName = info.textName;
        ink.isSpot = info.isSpot;
        ink.color = info.color;
        result.push_back(std::move(ink));
    }
    return result;
}

std::vector<std::pair<QString, QString>> PDFDocumentReport::getFileHashes(const QByteArray& data)
{
    const std::pair<QCryptographicHash::Algorithm, const char*> algorithms[] = { { QCryptographicHash::Md5, "MD5" }, { QCryptographicHash::Sha1, "SHA1" },
                                                                                 { QCryptographicHash::Sha256, "SHA256" }, { QCryptographicHash::Sha384, "SHA384" },
                                                                                 { QCryptographicHash::Sha512, "SHA512" } };
    std::vector<std::pair<QString, QString>> result;
    for (const auto& algorithm : algorithms)
    {
        result.emplace_back(QString::fromLatin1(algorithm.second), QString::fromLatin1(QCryptographicHash::hash(data, algorithm.first).toHex()).toUpper());
    }
    return result;
}

QStringList PDFDocumentReport::getConformance(const QString& metadata)
{
    // ISO 19005 (pdfaid:part, pdfaid:conformance) and ISO 14289 (pdfuaid:part): attributes of
    // rdf:Description or its elements
    const QString pdfaNamespace = QStringLiteral("http://www.aiim.org/pdfa/ns/id/");
    const QString pdfuaNamespace = QStringLiteral("http://www.aiim.org/pdfua/ns/id/");
    QString pdfaPart;
    QString pdfaConformance;
    QString pdfuaPart;

    QXmlStreamReader reader(metadata);
    while (!reader.atEnd())
    {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::DTD)
        {
            // No entities of a document type definition are expanded
            break;
        }
        if (token != QXmlStreamReader::StartElement)
        {
            continue;
        }

        auto assign = [&](QStringView namespaceUri, QStringView name, const QString& value)
        {
            if (namespaceUri == pdfaNamespace && name == QLatin1String("part"))
            {
                pdfaPart = value.trimmed().left(8);
            }
            else if (namespaceUri == pdfaNamespace && name == QLatin1String("conformance"))
            {
                pdfaConformance = value.trimmed().left(8);
            }
            else if (namespaceUri == pdfuaNamespace && name == QLatin1String("part"))
            {
                pdfuaPart = value.trimmed().left(8);
            }
        };

        for (const QXmlStreamAttribute& attribute : reader.attributes())
        {
            assign(attribute.namespaceUri(), attribute.name(), attribute.value().toString());
        }
        if (reader.namespaceUri() == pdfaNamespace || reader.namespaceUri() == pdfuaNamespace)
        {
            const QString namespaceUri = reader.namespaceUri().toString();
            const QString name = reader.name().toString();
            assign(namespaceUri, name, reader.readElementText(QXmlStreamReader::SkipChildElements));
        }
    }

    QStringList result;
    if (!pdfaPart.isEmpty())
    {
        result << QStringLiteral("PDF/A-%1%2").arg(pdfaPart, pdfaConformance.toLower());
    }
    if (!pdfuaPart.isEmpty())
    {
        result << QStringLiteral("PDF/UA-%1").arg(pdfuaPart);
    }
    return result;
}

PDFDocumentToolTableSection PDFDocumentReport::createReport(const PDFDocument* document, const Options& options, const PDFDocumentToolValueFormatter* formatter,
                                                            const PDFOperationControl* control, PDFProgressSink* progress, QJsonObject* data)
{
    const QJsonObject sections = createData(document, options, control, progress);
    if (data)
    {
        *data = sections;
    }
    if (PDFOperationControl::isOperationCanceled(control))
    {
        PDFDocumentToolTableSection report;
        report.name = QStringLiteral("report");
        return report;
    }
    return createModel(sections, formatter, formatter ? PDFDocumentToolDateFormat::Short : PDFDocumentToolDateFormat::Iso);
}

QJsonObject PDFDocumentReport::createData(const PDFDocument* document, const Options& options, const PDFOperationControl* control, PDFProgressSink* progress)
{
    using Helper = PDFDocumentReportHelper;

    QJsonObject sections;
    if (!document)
    {
        return sections;
    }

    if (progress)
    {
        progress->start(options.sections.size(), ProgressStartupInfo());
    }

    for (const PDFDocumentReportSection section : options.sections)
    {
        if (PDFOperationControl::isOperationCanceled(control))
        {
            return QJsonObject();
        }

        QJsonObject data;
        switch (section)
        {
            case PDFDocumentReportSection::BasicInformation:
                data = Helper::createBasicInformationData(document, options);
                break;
            case PDFDocumentReportSection::FileHashes:
                data = Helper::createFileHashesData(options);
                break;
            case PDFDocumentReportSection::Metadata:
                data = Helper::createMetadataData(document);
                break;
            case PDFDocumentReportSection::JavaScript:
                data = Helper::createJavaScriptData(document, options.pageIndices, control);
                break;
            case PDFDocumentReportSection::NamedDestinations:
                data = Helper::createNamedDestinationsData(document, options.pageIndices);
                break;
            case PDFDocumentReportSection::PageBoxes:
                data = Helper::createPageBoxesData(document, options.pageIndices);
                break;
            case PDFDocumentReportSection::StructureTree:
                data = Helper::createStructureTreeData(document, control);
                break;
            case PDFDocumentReportSection::Inks:
                data = Helper::createInksData(document, control);
                break;
            case PDFDocumentReportSection::Fonts:
                data = Helper::createFontsData(document, options.pageIndices, options.isCharacterMapIncluded, control);
                break;
        }
        sections.insert(getSectionId(section), data);

        if (progress)
        {
            progress->step();
        }
    }

    if (progress)
    {
        progress->finish();
    }
    return PDFOperationControl::isOperationCanceled(control) ? QJsonObject() : sections;
}

PDFDocumentToolTableSection PDFDocumentReport::createModel(const QJsonObject& data, const PDFDocumentToolValueFormatter* formatter, PDFDocumentToolDateFormat dateFormat)
{
    using Helper = PDFDocumentReportHelper;

    Helper::Context context;
    context.formatter = formatter ? formatter : PDFDocumentToolValueFormatter::getCLocale();
    context.dateFormat = dateFormat;

    PDFDocumentToolTableSection report;
    report.name = QStringLiteral("report");
    report.title = tr("Document Report");

    const std::pair<PDFDocumentReportSection, const char*> titles[] = { { PDFDocumentReportSection::BasicInformation, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Basic information") },
                                                                         { PDFDocumentReportSection::FileHashes, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "File hashes") },
                                                                         { PDFDocumentReportSection::Metadata, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Metadata (XMP)") },
                                                                         { PDFDocumentReportSection::JavaScript, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "JavaScript") },
                                                                         { PDFDocumentReportSection::NamedDestinations, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Named destinations") },
                                                                         { PDFDocumentReportSection::PageBoxes, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Page boxes") },
                                                                         { PDFDocumentReportSection::StructureTree, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Structure tree") },
                                                                         { PDFDocumentReportSection::Inks, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Inks") },
                                                                         { PDFDocumentReportSection::Fonts, QT_TRANSLATE_NOOP("pdf::PDFDocumentReport", "Fonts") } };
    for (const auto& title : titles)
    {
        const QString id = getSectionId(title.first);
        const QJsonValue value = data.value(id);
        if (!value.isObject())
        {
            continue;
        }

        const QJsonObject sectionData = value.toObject();
        PDFDocumentToolTableSection* section = report.addSection(id, tr(title.second));
        switch (title.first)
        {
            case PDFDocumentReportSection::BasicInformation:
                Helper::createBasicInformationModel(sectionData, context, section);
                break;
            case PDFDocumentReportSection::FileHashes:
                Helper::createFileHashesModel(sectionData, section);
                break;
            case PDFDocumentReportSection::Metadata:
                Helper::createMetadataModel(sectionData, section);
                break;
            case PDFDocumentReportSection::JavaScript:
                Helper::createJavaScriptModel(sectionData, context, section);
                break;
            case PDFDocumentReportSection::NamedDestinations:
                Helper::createNamedDestinationsModel(sectionData, context, section);
                break;
            case PDFDocumentReportSection::PageBoxes:
                Helper::createPageBoxesModel(sectionData, context, section);
                break;
            case PDFDocumentReportSection::StructureTree:
                Helper::createStructureTreeModel(sectionData, context, section);
                break;
            case PDFDocumentReportSection::Inks:
                Helper::createInksModel(sectionData, section);
                break;
            case PDFDocumentReportSection::Fonts:
                Helper::createFontsModel(sectionData, context, section);
                break;
        }
    }
    return report;
}

QString PDFDocumentReport::getEmptySectionReason(const PDFDocument* document, PDFDocumentReportSection section, bool hasOriginalData)
{
    if (!document)
    {
        return tr("No document is open.");
    }

    const PDFCatalog* catalog = document->getCatalog();
    switch (section)
    {
        case PDFDocumentReportSection::FileHashes:
            return hasOriginalData ? QString() : tr("Save the document first.");

        case PDFDocumentReportSection::Metadata:
            return document->getObject(catalog->getMetadata()).isStream() ? QString() : tr("The document has no XMP metadata.");

        case PDFDocumentReportSection::NamedDestinations:
            return !catalog->getNamedDestinations().empty() ? QString() : tr("The document has no named destinations.");

        case PDFDocumentReportSection::PageBoxes:
            return catalog->getPageCount() > 0 ? QString() : tr("The document has no pages.");

        case PDFDocumentReportSection::StructureTree:
            return document->getDictionaryFromObject(catalog->getStructureTreeRoot()) ? QString() : tr("The document has no structure tree.");

        case PDFDocumentReportSection::BasicInformation:
        case PDFDocumentReportSection::JavaScript:
        case PDFDocumentReportSection::Inks:
        case PDFDocumentReportSection::Fonts:
            break;
    }
    return QString();
}

QString PDFDocumentReport::sanitize(const QString& text)
{
    QString result = text.left(MAXIMUM_VALUE_LENGTH);
    for (QChar& character : result)
    {
        // Line breaks and the other control characters of a value of one line
        if (character.category() == QChar::Other_Control || character == QChar::LineSeparator || character == QChar::ParagraphSeparator)
        {
            character = QLatin1Char(' ');
        }
    }
    if (text.size() > MAXIMUM_VALUE_LENGTH)
    {
        result += QStringLiteral("…");
    }
    return result;
}

QString PDFDocumentReport::getPaperFormatName(const QSizeF& sizeMM)
{
    if (!(sizeMM.width() > 0.0) || !(sizeMM.height() > 0.0) || !qIsFinite(sizeMM.width()) || !qIsFinite(sizeMM.height()))
    {
        return QString();
    }

    const QPageSize::PageSizeId id = QPageSize::id(sizeMM, QPageSize::Millimeter, QPageSize::FuzzyOrientationMatch);
    if (id == QPageSize::Custom)
    {
        return QString();
    }
    return QPageSize::name(id);
}

QString PDFDocumentReport::getPermissionId(PDFSecurityHandler::Permission permission)
{
    using Permission = PDFSecurityHandler::Permission;
    switch (permission)
    {
        case Permission::PrintLowResolution:
            return QStringLiteral("printLowResolution");
        case Permission::Modify:
            return QStringLiteral("modify");
        case Permission::CopyContent:
            return QStringLiteral("copyContent");
        case Permission::ModifyInteractiveItems:
            return QStringLiteral("modifyInteractiveItems");
        case Permission::ModifyFormFields:
            return QStringLiteral("modifyFormFields");
        case Permission::Accessibility:
            return QStringLiteral("accessibility");
        case Permission::Assemble:
            return QStringLiteral("assemble");
        case Permission::PrintHighResolution:
            return QStringLiteral("printHighResolution");
    }
    return QString();
}

QString PDFDocumentReport::getSectionId(PDFDocumentReportSection section)
{
    switch (section)
    {
        case PDFDocumentReportSection::BasicInformation:
            return QStringLiteral("basicInformation");
        case PDFDocumentReportSection::FileHashes:
            return QStringLiteral("fileHashes");
        case PDFDocumentReportSection::Metadata:
            return QStringLiteral("metadata");
        case PDFDocumentReportSection::JavaScript:
            return QStringLiteral("javascript");
        case PDFDocumentReportSection::NamedDestinations:
            return QStringLiteral("namedDestinations");
        case PDFDocumentReportSection::PageBoxes:
            return QStringLiteral("pageBoxes");
        case PDFDocumentReportSection::StructureTree:
            return QStringLiteral("structureTree");
        case PDFDocumentReportSection::Inks:
            return QStringLiteral("inks");
        case PDFDocumentReportSection::Fonts:
            return QStringLiteral("fonts");
    }
    return QString();
}

std::optional<PDFDocumentReportSection> PDFDocumentReport::findSection(const QString& id)
{
    for (const PDFDocumentReportSection section : getSections())
    {
        if (getSectionId(section) == id)
        {
            return section;
        }
    }
    return std::nullopt;
}

std::vector<PDFDocumentReportSection> PDFDocumentReport::getSections()
{
    return { PDFDocumentReportSection::BasicInformation, PDFDocumentReportSection::FileHashes, PDFDocumentReportSection::Metadata,
             PDFDocumentReportSection::JavaScript, PDFDocumentReportSection::NamedDestinations, PDFDocumentReportSection::PageBoxes,
             PDFDocumentReportSection::StructureTree, PDFDocumentReportSection::Inks, PDFDocumentReportSection::Fonts };
}

}   // namespace pdf
