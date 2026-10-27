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


#include "pdfappmcpdocumentreader.h"
#include "pdfappmcparguments.h"
#include "pdfappmcpreadsupport.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_attachments.h"
#include "pdfappdocumentsignatures.h"
#include "pdfappdocumentview.h"
#include "pdfbookmarkrepository.h"

#include "pdfaction.h"
#include "pdfcatalog.h"
#include "pdfcompiler.h"
#include "pdfdocument.h"
#include "pdfdrawspacecontroller.h"
#include "pdfform.h"
#include "pdfmcpjson.h"
#include "pdfoptionalcontent.h"
#include "pdfoutline.h"
#include "pdfsecurityhandler.h"
#include "pdftextlayout.h"

#include "pdfwidgettool.h"

#include <QColor>
#include <QFileInfo>
#include <QHash>

#include <map>
#include <set>

namespace pdf
{

/// Helpers of PDFAppMcpDocumentReader
class PDFAppMcpDocumentReaderHelper
{
public:
    PDFAppMcpDocumentReaderHelper() = delete;

    /// Deepest field tree read
    static constexpr int MAXIMUM_FIELD_DEPTH = 64;

    static QString getPageLayoutName(PageLayout layout)
    {
        switch (layout)
        {
            case PageLayout::SinglePage:
                return QStringLiteral("SinglePage");
            case PageLayout::OneColumn:
                return QStringLiteral("OneColumn");
            case PageLayout::TwoColumnLeft:
                return QStringLiteral("TwoColumnLeft");
            case PageLayout::TwoColumnRight:
                return QStringLiteral("TwoColumnRight");
            case PageLayout::TwoPagesLeft:
                return QStringLiteral("TwoPageLeft");
            case PageLayout::TwoPagesRight:
                return QStringLiteral("TwoPageRight");
            case PageLayout::Custom:
                return QStringLiteral("Custom");
        }
        return QStringLiteral("SinglePage");
    }

    static QString getPageModeName(PageMode mode)
    {
        switch (mode)
        {
            case PageMode::UseNone:
                return QStringLiteral("UseNone");
            case PageMode::UseOutlines:
                return QStringLiteral("UseOutlines");
            case PageMode::UseThumbnails:
                return QStringLiteral("UseThumbs");
            case PageMode::Fullscreen:
                return QStringLiteral("FullScreen");
            case PageMode::UseOptionalContent:
                return QStringLiteral("UseOC");
            case PageMode::UseAttachments:
                return QStringLiteral("UseAttachments");
        }
        return QStringLiteral("UseNone");
    }

    static int getRotation(PageRotation rotation)
    {
        switch (rotation)
        {
            case PageRotation::None:
                return 0;
            case PageRotation::Rotate90:
                return 90;
            case PageRotation::Rotate180:
                return 180;
            case PageRotation::Rotate270:
                return 270;
        }
        return 0;
    }

    static bool isAllowed(const PDFSecurityHandler* handler, PDFSecurityHandler::Permission permission)
    {
        return !handler || handler->getMode() == EncryptionMode::None || handler->isAllowed(permission);
    }

    /// The crypt filter of the streams of an encryption of version 4 is RC4 (/CFM
    /// /V2): V 4 is RC4 or AES-128 by its crypt filters (ISO 32000-2 7.6.5)
    static bool isRC4CryptFilter(const PDFDocument* document)
    {
        const PDFDictionary* trailer = document ? document->getTrailerDictionary() : nullptr;
        const PDFDictionary* encrypt = trailer ? document->getDictionaryFromObject(trailer->get("Encrypt")) : nullptr;
        const PDFDictionary* filters = encrypt ? document->getDictionaryFromObject(encrypt->get("CF")) : nullptr;
        if (!filters)
        {
            return false;
        }

        PDFDocumentDataLoaderDecorator loader(document);
        const QByteArray filterName = loader.readNameFromDictionary(encrypt, "StmF");
        const PDFDictionary* filter = filterName.isEmpty() ? nullptr : document->getDictionaryFromObject(filters->get(filterName));
        return filter && loader.readNameFromDictionary(filter, "CFM") == "V2";
    }

    static QString getEncryptionMethod(const PDFDocument* document, const PDFSecurityHandler* handler)
    {
        switch (handler->getMode())
        {
            case EncryptionMode::None:
                return QString();
            case EncryptionMode::PublicKey:
                return QStringLiteral("certificate");
            case EncryptionMode::Custom:
                return QStringLiteral("custom");
            case EncryptionMode::Standard:
                break;
        }

        switch (handler->getVersion())
        {
            case 1:
                return QStringLiteral("RC4 40-bit");
            case 2:
            case 3:
                return QStringLiteral("RC4");
            case 4:
                return isRC4CryptFilter(document) ? QStringLiteral("RC4") : QStringLiteral("AES 128-bit");
            case 5:
                return QStringLiteral("AES 256-bit");
            default:
                break;
        }
        return QStringLiteral("standard");
    }

    /// Page index of a page reference (-1 = none)
    static PDFInteger getPageIndex(const PDFCatalog* catalog, PDFObjectReference reference)
    {
        if (!reference.isValid())
        {
            return -1;
        }
        const size_t index = catalog->getPageIndexFromPageReference(reference);
        return index == PDFCatalog::INVALID_PAGE_INDEX ? -1 : PDFInteger(index);
    }

    /// Page index of a destination (named destinations resolved)
    static PDFInteger getDestinationPage(const PDFCatalog* catalog, const PDFDestination* destination)
    {
        if (destination && destination->getDestinationType() == DestinationType::Named)
        {
            destination = catalog->getNamedDestination(destination->getName());
        }
        if (!destination || destination->getDestinationType() == DestinationType::Invalid || destination->getDestinationType() == DestinationType::Named)
        {
            return -1;
        }

        const PDFInteger index = getPageIndex(catalog, destination->getPageReference());
        if (index >= 0)
        {
            return index;
        }

        // A destination of a remote document gives the page by its number
        const PDFInteger pageIndex = destination->getPageIndex();
        return pageIndex >= 0 && pageIndex < PDFInteger(catalog->getPageCount()) ? pageIndex : -1;
    }

    /// Writes the outline items of the parent (pre-order)
    static void writeOutlineItems(PDFMcpJsonWriter& writer, const PDFDocument* document, const PDFOutlineItem* parent, std::vector<size_t>& path,
                                  int maximumDepth, int maximumItems, int* itemCount, bool* isTruncated)
    {
        const PDFCatalog* catalog = document->getCatalog();
        const int level = int(path.size()) + 1;
        for (size_t i = 0; i < parent->getChildCount(); ++i)
        {
            if (*itemCount >= maximumItems)
            {
                *isTruncated = true;
                return;
            }

            const PDFOutlineItem* item = parent->getChild(i);
            if (!item)
            {
                continue;
            }

            path.push_back(i);
            ++*itemCount;

            writer.beginObject();
            writer.writeMember(QLatin1StringView("item"), PDFAppMcpDocumentReader::createOutlineItemId(path));
            writer.writeMember(QLatin1StringView("title"), PDFAppMcpReadSupport::sanitizeName(item->getTitle()));
            writer.writeMember(QLatin1StringView("level"), level);
            const PDFInteger pageIndex = PDFAppMcpDocumentReader::getOutlineItemPage(document, item);
            if (pageIndex >= 0)
            {
                writer.writeMember(QLatin1StringView("page"), qint64(pageIndex + 1));
                writer.writeMember(QLatin1StringView("label"), PDFAppMcpReadSupport::sanitizeName(PDFAppMcpArguments::getPageLabel(document, pageIndex), 256));
            }
            writer.writeMember(QLatin1StringView("bold"), item->isFontBold());
            writer.writeMember(QLatin1StringView("italic"), item->isFontItalics());
            writer.writeMember(QLatin1StringView("childCount"), qint64(item->getChildCount()));
            writer.endObject();

            if (level < maximumDepth)
            {
                writeOutlineItems(writer, document, item, path, maximumDepth, maximumItems, itemCount, isTruncated);
            }
            path.pop_back();
            Q_UNUSED(catalog);
        }
    }

    /// Terminal fields of the form (fields without children), depth first
    static void collectFields(const PDFFormField* field, int depth, std::vector<const PDFFormField*>* fields, std::set<const PDFFormField*>* visited)
    {
        if (!field || depth > MAXIMUM_FIELD_DEPTH || int(fields->size()) >= PDFAppMcpDocumentReader::MAXIMUM_FORM_FIELDS || !visited->insert(field).second)
        {
            return;
        }

        if (field->getChildFields().empty() || isWidgetGroup(field))
        {
            fields->push_back(field);
            return;
        }

        for (const PDFFormFieldPointer& child : field->getChildFields())
        {
            collectFields(child.get(), depth + 1, fields, visited);
        }
    }

    /// The children of the field are its widgets: kids without a partial name
    /// (/T), which Core parses as fields merged with their widgets (a radio group)
    static bool isWidgetGroup(const PDFFormField* field)
    {
        for (const PDFFormFieldPointer& child : field->getChildFields())
        {
            if (!child || !child->getName(PDFFormField::Partial).isEmpty() || !child->getChildFields().empty())
            {
                return false;
            }
        }
        return !field->getChildFields().empty();
    }

    /// The widgets of a field and of its nameless children
    static std::vector<PDFFormWidget> getWidgets(const PDFFormField* field)
    {
        std::vector<PDFFormWidget> widgets(field->getWidgets().cbegin(), field->getWidgets().cend());
        if (isWidgetGroup(field))
        {
            for (const PDFFormFieldPointer& child : field->getChildFields())
            {
                widgets.insert(widgets.end(), child->getWidgets().cbegin(), child->getWidgets().cend());
            }
        }
        return widgets;
    }

    /// Name of a button state or a value of a field (a name or a text string)
    static QString readValueText(const PDFDocument* document, const PDFObject& value)
    {
        const PDFObject& object = document->getObject(value);
        if (object.isName())
        {
            return QString::fromLatin1(object.getString());
        }
        if (object.isString())
        {
            PDFDocumentDataLoaderDecorator loader(document);
            return loader.readTextString(object, QString());
        }
        return QString();
    }

    /// The appearance states of the widgets of a button (except Off)
    static QStringList getButtonStates(const PDFDocument* document, const PDFFormField* field)
    {
        QStringList states;
        for (const PDFFormWidget& widget : getWidgets(field))
        {
            const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(widget.getWidget()));
            const PDFDictionary* appearance = dictionary ? document->getDictionaryFromObject(dictionary->get("AP")) : nullptr;
            const PDFDictionary* normal = appearance ? document->getDictionaryFromObject(appearance->get("N")) : nullptr;
            if (!normal)
            {
                continue;
            }

            for (size_t i = 0; i < normal->getCount() && states.size() < MAXIMUM_STATES; ++i)
            {
                const QString state = QString::fromLatin1(normal->getKey(i).getString());
                if (state != QLatin1String("Off") && !states.contains(state))
                {
                    states << state;
                }
            }
        }
        return states;
    }

    static constexpr qsizetype MAXIMUM_STATES = 1000;
};

PDFAppMcpDocumentHeader PDFAppMcpDocumentHeader::create(const PDFAppDocument* document)
{
    PDFAppMcpDocumentHeader header;
    if (document)
    {
        header.documentId = document->getId();
        header.title = PDFAppMcpReadSupport::sanitizeName(document->getTitle(), 256);
    }
    return header;
}

void PDFAppMcpDocumentHeader::write(PDFMcpJsonWriter& writer) const
{
    writer.writeMember(QLatin1StringView("documentId"), QStringView(documentId));
    writer.writeMember(QLatin1StringView("title"), QStringView(title));
}

QString PDFAppMcpDocumentReader::createOutlineItemId(const std::vector<size_t>& path)
{
    QStringList parts;
    for (size_t index : path)
    {
        parts << QString::number(qulonglong(index) + 1);
    }
    return parts.join(QLatin1Char('.'));
}

QByteArray PDFAppMcpDocumentReader::getInfo(const PDFAppDocument* document)
{
    using Helper = PDFAppMcpDocumentReaderHelper;
    using Support = PDFAppMcpReadSupport;

    PDFMcpJsonWriter writer;
    writer.beginObject();
    PDFAppMcpDocumentHeader::create(document).write(writer);

    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    if (!pdfDocument)
    {
        writer.endObject();
        return writer.take();
    }

    const PDFDocumentInfo* info = pdfDocument->getInfo();
    const PDFCatalog* catalog = pdfDocument->getCatalog();

    writer.key(QLatin1StringView("metadata"));
    writer.beginObject();
    const std::pair<const char*, const QString*> texts[] = { { "title", &info->title }, { "author", &info->author }, { "subject", &info->subject },
                                                             { "keywords", &info->keywords }, { "creator", &info->creator }, { "producer", &info->producer } };
    for (const auto& text : texts)
    {
        if (!text.second->isEmpty())
        {
            writer.writeMember(QLatin1StringView(text.first), Support::sanitizeName(*text.second));
        }
    }
    if (info->creationDate.isValid())
    {
        writer.writeMember(QLatin1StringView("created"), Support::formatDateTime(info->creationDate));
    }
    if (info->modifiedDate.isValid())
    {
        writer.writeMember(QLatin1StringView("modified"), Support::formatDateTime(info->modifiedDate));
    }
    writer.endObject();

    writer.writeMember(QLatin1StringView("pdfVersion"), QString::fromLatin1(pdfDocument->getVersion().left(16)));
    writer.writeMember(QLatin1StringView("pageCount"), qint64(catalog->getPageCount()));

    qint64 fileSize = document->getOriginalData().size();
    if (fileSize <= 0 && !document->getFilePath().isEmpty())
    {
        const QFileInfo fileInfo(document->getFilePath());
        fileSize = fileInfo.exists() ? fileInfo.size() : -1;
    }
    if (fileSize > 0 && !document->getFilePath().isEmpty())
    {
        writer.writeMember(QLatin1StringView("fileSize"), fileSize);
    }

    if (!catalog->getLanguage().isEmpty())
    {
        writer.writeMember(QLatin1StringView("language"), Support::sanitizeName(catalog->getLanguage(), 64));
    }
    writer.writeMember(QLatin1StringView("tagged"), catalog->isLogicalStructureMarked() || !catalog->getStructureTreeRoot().isNull());

    // The form, its signatures
    const PDFForm form = PDFForm::parse(pdfDocument.data(), catalog->getFormObject());
    QString formType = QStringLiteral("none");
    if (form.isXFAForm())
    {
        formType = QStringLiteral("xfa");
    }
    else if (form.isAcroForm() && !form.getFormFields().empty())
    {
        formType = QStringLiteral("acro");
    }
    writer.writeMember(QLatin1StringView("form"), formType);

    std::vector<const PDFFormField*> fields;
    std::set<const PDFFormField*> visited;
    for (const PDFFormFieldPointer& field : form.getFormFields())
    {
        Helper::collectFields(field.get(), 0, &fields, &visited);
    }
    qint64 signatureCount = 0;
    for (const PDFFormField* field : fields)
    {
        if (field->getFieldType() == PDFFormField::FieldType::Signature && !field->getValue().isNull())
        {
            ++signatureCount;
        }
    }

    const QSharedPointer<PDFOutlineItem> outline = catalog->getOutlineRootPtr();
    writer.writeMember(QLatin1StringView("outlineCount"), qint64(outline ? outline->getTotalCount() : 0));
    std::shared_ptr<const std::vector<PDFAppAttachment>> attachments = PDFAppAttachments::getAttachments(document);
    writer.writeMember(QLatin1StringView("attachmentCount"), qint64(attachments ? attachments->size() : 0));
    writer.writeMember(QLatin1StringView("layerCount"), qint64(catalog->getOptionalContentProperties()->getAllOptionalContentGroups().size()));
    writer.writeMember(QLatin1StringView("signatureCount"), signatureCount);

    const PDFSecurityHandler* handler = pdfDocument->getStorage().getSecurityHandler();
    if (handler && handler->getMode() != EncryptionMode::None)
    {
        writer.key(QLatin1StringView("encryption"));
        writer.beginObject();
        writer.writeMember(QLatin1StringView("method"), Helper::getEncryptionMethod(pdfDocument.data(), handler));
        writer.key(QLatin1StringView("permissions"));
        writer.beginObject();
        using Permission = PDFSecurityHandler::Permission;
        writer.writeMember(QLatin1StringView("print"), Helper::isAllowed(handler, Permission::PrintLowResolution) || Helper::isAllowed(handler, Permission::PrintHighResolution));
        writer.writeMember(QLatin1StringView("modify"), Helper::isAllowed(handler, Permission::Modify));
        writer.writeMember(QLatin1StringView("copy"), Helper::isAllowed(handler, Permission::CopyContent));
        writer.writeMember(QLatin1StringView("annotate"), Helper::isAllowed(handler, Permission::ModifyInteractiveItems));
        writer.writeMember(QLatin1StringView("fillForms"), Helper::isAllowed(handler, Permission::ModifyFormFields) || Helper::isAllowed(handler, Permission::ModifyInteractiveItems));
        writer.writeMember(QLatin1StringView("assemble"), Helper::isAllowed(handler, Permission::Assemble));
        writer.endObject();
        writer.endObject();
    }

    writer.writeMember(QLatin1StringView("pageLayout"), Helper::getPageLayoutName(catalog->getPageLayout()));
    writer.writeMember(QLatin1StringView("pageMode"), Helper::getPageModeName(catalog->getPageMode()));
    writer.writeMember(QLatin1StringView("isLocked"), document->isLocked());
    writer.writeMember(QLatin1StringView("isModified"), document->isModified());
    writer.writeMember(QLatin1StringView("version"), qint64(document->getVersion()));
    writer.writeMember(QLatin1StringView("origin"), QLatin1StringView("document"));
    writer.endObject();
    return writer.take();
}

QByteArray PDFAppMcpDocumentReader::getPages(const PDFAppMcpDocumentHeader& header, const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, std::optional<PDFInteger> nextPage)
{
    using Helper = PDFAppMcpDocumentReaderHelper;
    using Support = PDFAppMcpReadSupport;

    PDFMcpJsonWriter writer;
    writer.beginObject();
    header.write(writer);
    writer.key(QLatin1StringView("pages"));
    writer.beginArray();

    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    for (PDFInteger pageIndex : pageIndices)
    {
        const PDFPage* page = catalog && pageIndex >= 0 && pageIndex < PDFInteger(catalog->getPageCount()) ? catalog->getPage(size_t(pageIndex)) : nullptr;
        if (!page)
        {
            continue;
        }

        const QSizeF rotatedSize = page->getRotatedCropBox().size();
        writer.beginObject();
        writer.writeMember(QLatin1StringView("page"), qint64(pageIndex + 1));
        writer.writeMember(QLatin1StringView("label"), Support::sanitizeName(PDFAppMcpArguments::getPageLabel(document, pageIndex), 256));
        Support::writeRect(writer, QLatin1StringView("mediaBox"), page->getMediaBox());
        Support::writeRect(writer, QLatin1StringView("cropBox"), page->getCropBox());
        writer.writeMember(QLatin1StringView("rotation"), Helper::getRotation(page->getPageRotation()));
        writer.writeMember(QLatin1StringView("userUnit"), Support::round(page->getUserUnit()));
        writer.writeMember(QLatin1StringView("widthPt"), Support::round(rotatedSize.width()));
        writer.writeMember(QLatin1StringView("heightPt"), Support::round(rotatedSize.height()));
        writer.writeMember(QLatin1StringView("annotationCount"), qint64(page->getAnnotations().size()));
        writer.endObject();
    }

    writer.endArray();
    if (nextPage)
    {
        writer.writeMember(QLatin1StringView("nextPage"), qint64(*nextPage));
    }
    writer.endObject();
    return writer.take();
}

QByteArray PDFAppMcpDocumentReader::getOutline(const PDFAppMcpDocumentHeader& header, const PDFDocument* document, int maximumDepth, int maximumItems)
{
    PDFMcpJsonWriter writer;
    writer.beginObject();
    header.write(writer);
    writer.key(QLatin1StringView("items"));
    writer.beginArray();

    bool isTruncated = false;
    const QSharedPointer<PDFOutlineItem> root = document ? document->getCatalog()->getOutlineRootPtr() : QSharedPointer<PDFOutlineItem>();
    if (root)
    {
        std::vector<size_t> path;
        int itemCount = 0;
        PDFAppMcpDocumentReaderHelper::writeOutlineItems(writer, document, root.data(), path, qBound(1, maximumDepth, 256), qBound(1, maximumItems, 5000), &itemCount, &isTruncated);
    }

    writer.endArray();
    writer.writeMember(QLatin1StringView("truncated"), isTruncated);
    writer.writeMember(QLatin1StringView("origin"), QLatin1StringView("document"));
    writer.endObject();
    return writer.take();
}

const PDFOutlineItem* PDFAppMcpDocumentReader::findOutlineItem(const PDFDocument* document, const QString& itemId)
{
    const QSharedPointer<PDFOutlineItem> root = document ? document->getCatalog()->getOutlineRootPtr() : QSharedPointer<PDFOutlineItem>();
    if (!root || itemId.isEmpty() || itemId.size() > 256)
    {
        return nullptr;
    }

    const PDFOutlineItem* item = root.data();
    const QStringList parts = itemId.split(QLatin1Char('.'));
    if (parts.size() > 256)
    {
        return nullptr;
    }

    for (const QString& part : parts)
    {
        bool ok = false;
        const qulonglong index = part.toULongLong(&ok);
        if (!ok || index < 1 || index > item->getChildCount() || part.startsWith(QLatin1Char('0')) || part.startsWith(QLatin1Char('+')))
        {
            return nullptr;
        }
        item = item->getChild(size_t(index - 1));
        if (!item)
        {
            return nullptr;
        }
    }
    return item;
}

PDFInteger PDFAppMcpDocumentReader::getOutlineItemPage(const PDFDocument* document, const PDFOutlineItem* item)
{
    const PDFAction* action = item ? item->getAction() : nullptr;
    if (!document || !action)
    {
        return -1;
    }

    for (const PDFAction* current : action->getActionList())
    {
        if (current && current->getType() == ActionType::GoTo)
        {
            return PDFAppMcpDocumentReaderHelper::getDestinationPage(document->getCatalog(), &static_cast<const PDFActionGoTo*>(current)->getDestination());
        }
    }
    return -1;
}

QString PDFAppMcpDocumentReader::getAnnotationTypeName(const PDFAnnotation* annotation)
{
    if (!annotation)
    {
        return QStringLiteral("other");
    }

    switch (annotation->getType())
    {
        case AnnotationType::Highlight:
            return QStringLiteral("highlight");
        case AnnotationType::Underline:
            return QStringLiteral("underline");
        case AnnotationType::StrikeOut:
            return QStringLiteral("strikeout");
        case AnnotationType::Squiggly:
            return QStringLiteral("squiggly");
        case AnnotationType::Text:
            return QStringLiteral("note");
        case AnnotationType::FreeText:
            return QStringLiteral("text_box");
        case AnnotationType::Square:
            return QStringLiteral("rectangle");
        case AnnotationType::Circle:
            return QStringLiteral("ellipse");
        case AnnotationType::Line:
        {
            const PDFLineAnnotation* line = static_cast<const PDFLineAnnotation*>(annotation);
            const bool isArrow = line->getEndLineEnding() == AnnotationLineEnding::OpenArrow || line->getEndLineEnding() == AnnotationLineEnding::ClosedArrow ||
                                 line->getStartLineEnding() == AnnotationLineEnding::OpenArrow || line->getStartLineEnding() == AnnotationLineEnding::ClosedArrow;
            return isArrow ? QStringLiteral("arrow") : QStringLiteral("line");
        }
        case AnnotationType::Polygon:
            return QStringLiteral("polygon");
        case AnnotationType::Polyline:
            return QStringLiteral("polyline");
        case AnnotationType::Ink:
            return QStringLiteral("ink");
        case AnnotationType::Stamp:
            return QStringLiteral("stamp");
        case AnnotationType::Link:
            return QStringLiteral("link");
        case AnnotationType::Widget:
            return QStringLiteral("widget");
        case AnnotationType::Popup:
            return QStringLiteral("popup");
        default:
            break;
    }
    return QStringLiteral("other");
}

QString PDFAppMcpDocumentReader::createCursor(qint64 offset)
{
    return QString::fromLatin1(PDFMcpJson::toBase64Url(QByteArray("a:") + QByteArray::number(offset)));
}

std::optional<qint64> PDFAppMcpDocumentReader::parseCursor(const QString& cursor)
{
    if (cursor.isEmpty() || cursor.size() > 64)
    {
        return std::nullopt;
    }

    const std::optional<QByteArray> decoded = PDFMcpJson::fromBase64Url(cursor.toLatin1());
    if (!decoded || !decoded->startsWith("a:") || decoded->size() > 14)
    {
        return std::nullopt;
    }

    const QByteArray digits = decoded->mid(2);
    for (char c : digits)
    {
        if (c < '0' || c > '9')
        {
            return std::nullopt;
        }
    }

    bool ok = false;
    const qint64 offset = digits.toLongLong(&ok);
    if (!ok || offset < 0 || offset > 100000000)
    {
        return std::nullopt;
    }
    return offset;
}

QString PDFAppMcpDocumentReader::formatColor(const std::vector<PDFReal>& components)
{
    QColor color;
    switch (components.size())
    {
        case 1:
            color = QColor::fromRgbF(float(components[0]), float(components[0]), float(components[0]));
            break;
        case 3:
            color = QColor::fromRgbF(float(components[0]), float(components[1]), float(components[2]));
            break;
        case 4:
            color = QColor::fromCmykF(float(components[0]), float(components[1]), float(components[2]), float(components[3]));
            break;
        default:
            return QString();
    }

    for (PDFReal component : components)
    {
        if (!std::isfinite(component) || component < 0.0 || component > 1.0)
        {
            return QString();
        }
    }
    return color.isValid() ? color.name(QColor::HexRgb) : QString();
}

QByteArray PDFAppMcpDocumentReader::getAnnotations(const PDFAppMcpDocumentHeader& header, const PDFDocument* document, const PDFAppMcpAnnotationQuery& query)
{
    using Support = PDFAppMcpReadSupport;

    PDFMcpJsonWriter writer;
    writer.beginObject();
    header.write(writer);
    writer.key(QLatin1StringView("annotations"));
    writer.beginArray();

    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    PDFDocumentDataLoaderDecorator loader(document ? &document->getStorage() : nullptr);   // (no document: never used)
    qint64 index = 0;
    int written = 0;
    bool hasMore = false;

    for (PDFInteger pageIndex : query.pageIndices)
    {
        const PDFPage* page = catalog && pageIndex >= 0 && pageIndex < PDFInteger(catalog->getPageCount()) ? catalog->getPage(size_t(pageIndex)) : nullptr;
        if (!page || hasMore)
        {
            continue;
        }

        // The annotations of the page and the replies to each of them
        std::vector<PDFAnnotationPtr> annotations;
        const std::vector<PDFObjectReference>& references = page->getAnnotations();
        const size_t count = qMin(references.size(), MAXIMUM_PAGE_ANNOTATIONS);
        annotations.reserve(count);
        std::map<PDFObjectReference, std::vector<PDFObjectReference>> replies;
        for (size_t i = 0; i < count; ++i)
        {
            PDFAnnotationPtr annotation = PDFAnnotation::parse(&document->getStorage(), references[i]);
            if (annotation)
            {
                const PDFMarkupAnnotation* markup = annotation->asMarkupAnnotation();
                if (markup && markup->getInReplyTo().isValid())
                {
                    std::vector<PDFObjectReference>& list = replies[markup->getInReplyTo()];
                    if (list.size() < 1000)
                    {
                        list.push_back(references[i]);
                    }
                }
            }
            annotations.push_back(std::move(annotation));
        }

        for (size_t i = 0; i < annotations.size(); ++i)
        {
            const PDFAnnotation* annotation = annotations[i].get();
            if (!annotation)
            {
                continue;
            }

            const QString type = getAnnotationTypeName(annotation);
            const PDFMarkupAnnotation* markup = annotation->asMarkupAnnotation();
            const bool isReply = markup && markup->getInReplyTo().isValid();
            if ((!query.types.isEmpty() && !query.types.contains(type)) || (isReply && !query.includeReplies))
            {
                continue;
            }

            // The cursor skips the annotations of the previous calls
            if (index++ < query.offset)
            {
                continue;
            }

            if (written >= query.maximumResults)
            {
                hasMore = true;
                break;
            }
            ++written;

            const PDFObjectReference reference = references[i];
            writer.beginObject();
            writer.writeMember(QLatin1StringView("id"), PDFAppMcpArguments::formatAnnotationId(reference.objectNumber, reference.generation));
            writer.writeMember(QLatin1StringView("type"), type);
            writer.writeMember(QLatin1StringView("page"), qint64(pageIndex + 1));
            writer.writeMember(QLatin1StringView("label"), Support::sanitizeName(PDFAppMcpArguments::getPageLabel(document, pageIndex), 256));
            Support::writeRect(writer, QLatin1StringView("rect"), annotation->getRectangle());

            // The quadrilaterals of the text markups and links in the order of the PDF
            const AnnotationType annotationType = annotation->getType();
            const bool hasQuads = annotationType == AnnotationType::Highlight || annotationType == AnnotationType::Underline || annotationType == AnnotationType::StrikeOut ||
                                  annotationType == AnnotationType::Squiggly || annotationType == AnnotationType::Link;
            if (hasQuads)
            {
                const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(reference));
                std::vector<PDFReal> numbers = dictionary ? loader.readNumberArrayFromDictionary(dictionary, "QuadPoints") : std::vector<PDFReal>();
                const size_t quadCount = qMin(numbers.size() / 8, size_t(MAXIMUM_ANNOTATION_QUADS));
                if (quadCount > 0)
                {
                    QPolygonF quads;
                    quads.reserve(qsizetype(quadCount * 4));
                    for (size_t q = 0; q < quadCount * 8; q += 2)
                    {
                        quads << QPointF(numbers[q], numbers[q + 1]);
                    }
                    Support::writeQuads(writer, QLatin1StringView("quads"), quads);
                }
            }

            if (!annotation->getContents().isEmpty())
            {
                writer.writeMember(QLatin1StringView("contents"), Support::sanitizeContent(annotation->getContents()));
            }
            if (markup && !markup->getWindowTitle().isEmpty())
            {
                writer.writeMember(QLatin1StringView("author"), Support::sanitizeName(markup->getWindowTitle(), 256));
            }
            if (markup && !markup->getSubject().isEmpty())
            {
                writer.writeMember(QLatin1StringView("subject"), Support::sanitizeName(markup->getSubject(), 256));
            }
            const QString color = formatColor(annotation->getColor());
            if (!color.isEmpty())
            {
                writer.writeMember(QLatin1StringView("color"), color);
            }
            if (annotation->getLastModifiedDateTime().isValid())
            {
                writer.writeMember(QLatin1StringView("modified"), Support::formatDateTime(annotation->getLastModifiedDateTime()));
            }

            const PDFAnnotation::Flags flags = annotation->getFlags();
            writer.key(QLatin1StringView("flags"));
            writer.beginObject();
            writer.writeMember(QLatin1StringView("hidden"), flags.testFlag(PDFAnnotation::Hidden));
            writer.writeMember(QLatin1StringView("locked"), flags.testFlag(PDFAnnotation::Locked));
            writer.writeMember(QLatin1StringView("readOnly"), flags.testFlag(PDFAnnotation::ReadOnly));
            writer.writeMember(QLatin1StringView("print"), flags.testFlag(PDFAnnotation::Print));
            writer.endObject();

            if (isReply)
            {
                writer.writeMember(QLatin1StringView("inReplyTo"), PDFAppMcpArguments::formatAnnotationId(markup->getInReplyTo().objectNumber, markup->getInReplyTo().generation));
            }

            auto replyIt = replies.find(reference);
            if (replyIt != replies.end())
            {
                writer.key(QLatin1StringView("replyIds"));
                writer.beginArray();
                for (const PDFObjectReference& reply : replyIt->second)
                {
                    writer.writeString(PDFAppMcpArguments::formatAnnotationId(reply.objectNumber, reply.generation));
                }
                writer.endArray();
            }
            writer.endObject();
        }
    }

    writer.endArray();
    if (hasMore)
    {
        writer.writeMember(QLatin1StringView("nextCursor"), createCursor(query.offset + written));
    }
    writer.writeMember(QLatin1StringView("origin"), QLatin1StringView("document"));
    writer.endObject();
    return writer.take();
}

QByteArray PDFAppMcpDocumentReader::getFormFields(const PDFAppMcpDocumentHeader& header, const PDFDocument* document, const QStringList& names, bool includeValues)
{
    using Helper = PDFAppMcpDocumentReaderHelper;
    using Support = PDFAppMcpReadSupport;

    PDFMcpJsonWriter writer;
    writer.beginObject();
    header.write(writer);

    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    const PDFForm form = catalog ? PDFForm::parse(document, catalog->getFormObject()) : PDFForm();
    std::vector<const PDFFormField*> fields;
    std::set<const PDFFormField*> visited;
    for (const PDFFormFieldPointer& field : form.getFormFields())
    {
        Helper::collectFields(field.get(), 0, &fields, &visited);
    }

    const QSet<QString> nameFilter(names.cbegin(), names.cend());
    PDFDocumentDataLoaderDecorator loader(document ? &document->getStorage() : nullptr);   // (no document: never used)

    writer.key(QLatin1StringView("fields"));
    writer.beginArray();
    for (const PDFFormField* field : fields)
    {
        const QString name = field->getName(PDFFormField::FullyQualified);
        if (!nameFilter.isEmpty() && !nameFilter.contains(name))
        {
            continue;
        }

        const PDFFormField::FieldFlags flags = field->getFlags();
        QString type;
        const PDFFormFieldButton* button = nullptr;
        const PDFFormFieldChoice* choice = nullptr;
        switch (field->getFieldType())
        {
            case PDFFormField::FieldType::Button:
            {
                button = dynamic_cast<const PDFFormFieldButton*>(field);
                const PDFFormFieldButton::ButtonType buttonType = button ? button->getButtonType() : PDFFormFieldButton::ButtonType::PushButton;
                type = buttonType == PDFFormFieldButton::ButtonType::CheckBox ? QStringLiteral("checkbox") :
                       buttonType == PDFFormFieldButton::ButtonType::RadioButton ? QStringLiteral("radio") : QStringLiteral("button");
                break;
            }
            case PDFFormField::FieldType::Text:
                type = QStringLiteral("text");
                break;
            case PDFFormField::FieldType::Choice:
                choice = dynamic_cast<const PDFFormFieldChoice*>(field);
                type = choice && choice->isComboBox() ? QStringLiteral("combo") : QStringLiteral("list");
                break;
            case PDFFormField::FieldType::Signature:
                type = QStringLiteral("signature");
                break;
            case PDFFormField::FieldType::Invalid:
                continue;
        }

        writer.beginObject();
        writer.writeMember(QLatin1StringView("name"), Support::sanitizeName(name));
        writer.writeMember(QLatin1StringView("type"), type);

        const bool isPassword = flags.testFlag(PDFFormField::Password);
        const bool isMultiSelect = choice && flags.testFlag(PDFFormField::MultiSelect);
        if (includeValues && !isPassword)
        {
            const PDFObject& value = document->getObject(field->getValue());
            if (type == QLatin1String("checkbox"))
            {
                const QString state = Helper::readValueText(document, value);
                writer.writeMember(QLatin1StringView("checked"), !state.isEmpty() && state != QLatin1String("Off"));
            }
            else if (isMultiSelect)
            {
                writer.key(QLatin1StringView("selected"));
                writer.beginArray();
                if (value.isArray())
                {
                    const PDFArray* array = value.getArray();
                    for (size_t i = 0; i < array->getCount() && i < size_t(MAXIMUM_FIELD_OPTIONS); ++i)
                    {
                        writer.writeString(Support::sanitizeName(Helper::readValueText(document, array->getItem(i))));
                    }
                }
                else if (!value.isNull())
                {
                    writer.writeString(Support::sanitizeName(Helper::readValueText(document, value)));
                }
                writer.endArray();
            }
            else if (type != QLatin1String("button") && type != QLatin1String("signature"))
            {
                QString text = Helper::readValueText(document, value);
                if (type == QLatin1String("radio") && text == QLatin1String("Off"))
                {
                    text.clear();
                }
                if (!value.isNull())
                {
                    writer.writeMember(QLatin1StringView("value"), type == QLatin1String("text") ? Support::sanitizeContent(text) : Support::sanitizeName(text));
                }
            }

            const QString defaultValue = Helper::readValueText(document, field->getDefaultValue());
            if (!defaultValue.isEmpty())
            {
                writer.writeMember(QLatin1StringView("defaultValue"), Support::sanitizeContent(defaultValue));
            }
        }

        // The options of the choices and of the radio groups
        if (choice || type == QLatin1String("radio"))
        {
            writer.key(QLatin1StringView("options"));
            writer.beginArray();
            if (choice)
            {
                int optionCount = 0;
                for (const PDFFormFieldChoice::Option& option : choice->getOptions())
                {
                    if (optionCount++ >= MAXIMUM_FIELD_OPTIONS)
                    {
                        break;
                    }
                    writer.beginObject();
                    writer.writeMember(QLatin1StringView("value"), Support::sanitizeName(option.exportString));
                    writer.writeMember(QLatin1StringView("label"), Support::sanitizeName(option.userString.isEmpty() ? option.exportString : option.userString));
                    writer.endObject();
                }
            }
            else
            {
                for (const QString& state : Helper::getButtonStates(document, field))
                {
                    writer.beginObject();
                    writer.writeMember(QLatin1StringView("value"), Support::sanitizeName(state));
                    writer.writeMember(QLatin1StringView("label"), Support::sanitizeName(state));
                    writer.endObject();
                }
            }
            writer.endArray();
        }

        writer.writeMember(QLatin1StringView("required"), flags.testFlag(PDFFormField::Required));
        writer.writeMember(QLatin1StringView("readOnly"), flags.testFlag(PDFFormField::ReadOnly) || form.isXFAForm());
        if (type == QLatin1String("text"))
        {
            writer.writeMember(QLatin1StringView("multiline"), flags.testFlag(PDFFormField::Multiline));
            const PDFFormFieldText* textField = dynamic_cast<const PDFFormFieldText*>(field);
            if (textField && textField->getTextMaximalLength() > 0)
            {
                writer.writeMember(QLatin1StringView("maxLength"), qint64(textField->getTextMaximalLength()));
            }
        }

        // The first widget gives the page and the rectangle
        const std::vector<PDFFormWidget> widgets = Helper::getWidgets(field);
        if (!widgets.empty())
        {
            const PDFFormWidget& widget = widgets.front();
            const PDFInteger pageIndex = Helper::getPageIndex(catalog, widget.getPage());
            if (pageIndex >= 0)
            {
                writer.writeMember(QLatin1StringView("page"), qint64(pageIndex + 1));
                writer.writeMember(QLatin1StringView("label"), Support::sanitizeName(PDFAppMcpArguments::getPageLabel(document, pageIndex), 256));
            }

            const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(widget.getWidget()));
            if (dictionary)
            {
                const QRectF rect = loader.readRectangle(dictionary->get("Rect"), QRectF());
                if (rect.isValid())
                {
                    Support::writeRect(writer, QLatin1StringView("rect"), rect);
                }
            }
        }

        const QString tooltip = field->getName(PDFFormField::UserCaption);
        if (!tooltip.isEmpty() && tooltip != name)
        {
            writer.writeMember(QLatin1StringView("tooltip"), Support::sanitizeName(tooltip));
        }
        writer.endObject();
    }
    writer.endArray();

    if (form.isXFAForm())
    {
        writer.writeMember(QLatin1StringView("xfa"), true);
        writer.writeMember(QLatin1StringView("note"), QLatin1StringView("The form is an XFA form: PDF4QT shows its AcroForm part only and does not change its values."));
    }
    else if (fields.empty())
    {
        writer.writeMember(QLatin1StringView("note"), QLatin1StringView("The document has no form fields."));
    }
    writer.writeMember(QLatin1StringView("origin"), QLatin1StringView("document"));
    writer.endObject();
    return writer.take();
}

QByteArray PDFAppMcpDocumentReader::getAttachments(const PDFAppDocument* document)
{
    using Support = PDFAppMcpReadSupport;

    PDFMcpJsonWriter writer;
    writer.beginObject();
    PDFAppMcpDocumentHeader::create(document).write(writer);
    writer.key(QLatin1StringView("attachments"));
    writer.beginArray();

    std::shared_ptr<const std::vector<PDFAppAttachment>> attachments = PDFAppAttachments::getAttachments(document);
    if (attachments)
    {
        for (size_t i = 0; i < attachments->size(); ++i)
        {
            const PDFAppAttachment& attachment = attachments->at(i);
            const PDFEmbeddedFile* file = attachment.specification.getPlatformFile();
            writer.beginObject();
            writer.writeMember(QLatin1StringView("index"), qint64(i));
            writer.writeMember(QLatin1StringView("name"), Support::sanitizeName(attachment.name, 256));
            if (!attachment.description.isEmpty())
            {
                writer.writeMember(QLatin1StringView("description"), Support::sanitizeName(attachment.description));
            }
            if (file && !file->getSubtype().isEmpty())
            {
                writer.writeMember(QLatin1StringView("mimeType"), Support::sanitizeName(QString::fromLatin1(file->getSubtype()), 128));
            }
            if (attachment.size >= 0)
            {
                writer.writeMember(QLatin1StringView("size"), qint64(attachment.size));
            }
            if (file && file->getCreationDate().isValid())
            {
                writer.writeMember(QLatin1StringView("created"), Support::formatDateTime(file->getCreationDate()));
            }
            if (file && file->getModifiedDate().isValid())
            {
                writer.writeMember(QLatin1StringView("modified"), Support::formatDateTime(file->getModifiedDate()));
            }
            writer.endObject();
        }
    }

    writer.endArray();
    writer.writeMember(QLatin1StringView("origin"), QLatin1StringView("document"));
    writer.endObject();
    return writer.take();
}

QByteArray PDFAppMcpDocumentReader::getSignatures(const PDFAppDocument* document, bool isVerifying)
{
    using Support = PDFAppMcpReadSupport;

    PDFMcpJsonWriter writer;
    writer.beginObject();
    PDFAppMcpDocumentHeader::create(document).write(writer);
    writer.key(QLatin1StringView("signatures"));
    writer.beginArray();

    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    if (pdfDocument)
    {
        // Reason and location of the signature dictionaries of the fields
        std::map<PDFObjectReference, const PDFFormFieldSignature*> signatureFields;
        const PDFForm form = PDFForm::parse(pdfDocument.data(), pdfDocument->getCatalog()->getFormObject());
        std::vector<const PDFFormField*> fields;
        std::set<const PDFFormField*> visited;
        for (const PDFFormFieldPointer& field : form.getFormFields())
        {
            PDFAppMcpDocumentReaderHelper::collectFields(field.get(), 0, &fields, &visited);
        }
        for (const PDFFormField* field : fields)
        {
            if (const PDFFormFieldSignature* signatureField = dynamic_cast<const PDFFormFieldSignature*>(field))
            {
                signatureFields[field->getSelfReference()] = signatureField;
            }
        }

        const bool isStrict = PDFAppDocumentSignatureVerifier::readSettings(document->getContext()).strict;
        if (isVerifying && document->getSignatureResults().empty())
        {
            // The verification still runs: the signed fields are pending
            for (const auto& item : signatureFields)
            {
                if (item.second->getValue().isNull())
                {
                    continue;
                }
                writer.beginObject();
                writer.writeMember(QLatin1StringView("field"), Support::sanitizeName(item.second->getName(PDFFormField::FullyQualified)));
                writer.writeMember(QLatin1StringView("status"), QLatin1StringView("pending"));
                writer.endObject();
            }
        }

        for (const PDFSignatureVerificationResult& result : document->getSignatureResults())
        {
            writer.beginObject();
            writer.writeMember(QLatin1StringView("field"), Support::sanitizeName(result.getSignatureFieldQualifiedName()));
            const PDFCertificateInfos& certificates = result.getCertificateInfos();
            if (!certificates.empty())
            {
                writer.writeMember(QLatin1StringView("signer"), Support::sanitizeName(certificates.front().getName(PDFCertificateInfo::CommonName), 256));
            }
            const QDateTime signedAt = result.getSignatureDate();
            if (signedAt.isValid())
            {
                writer.writeMember(QLatin1StringView("signedAt"), Support::formatDateTime(signedAt));
            }

            QString status = QStringLiteral("unknown");
            switch (PDFAppDocumentSignatureVerifier::getStatus(result, isStrict))
            {
                case PDFComponentPainter::SignatureStatus::Valid:
                    status = QStringLiteral("valid");
                    break;
                case PDFComponentPainter::SignatureStatus::Invalid:
                    status = QStringLiteral("invalid");
                    break;
                case PDFComponentPainter::SignatureStatus::Warning:
                    status = QStringLiteral("unknown");
                    break;
            }
            writer.writeMember(QLatin1StringView("status"), status);
            writer.writeMember(QLatin1StringView("trusted"), result.isCertificateValid());
            const bool hasUncoveredBytes = result.hasFlag(PDFSignatureVerificationResult::Warning_Signature_NotCoveredBytes);
            writer.writeMember(QLatin1StringView("coversWholeDocument"), !hasUncoveredBytes);
            writer.writeMember(QLatin1StringView("modifiedAfter"), hasUncoveredBytes);

            auto fieldIt = signatureFields.find(result.getSignatureFieldReference());
            if (fieldIt != signatureFields.end())
            {
                const PDFSignature& signature = fieldIt->second->getSignature();
                if (!signature.getReason().isEmpty())
                {
                    writer.writeMember(QLatin1StringView("reason"), Support::sanitizeName(signature.getReason()));
                }
                if (!signature.getLocation().isEmpty())
                {
                    writer.writeMember(QLatin1StringView("location"), Support::sanitizeName(signature.getLocation()));
                }
            }
            writer.endObject();
        }
    }

    writer.endArray();
    writer.writeMember(QLatin1StringView("origin"), QLatin1StringView("document"));
    writer.endObject();
    return writer.take();
}

QByteArray PDFAppMcpDocumentReader::getSelection(const PDFAppDocument* document, const PDFAppDocumentView* view)
{
    using Support = PDFAppMcpReadSupport;

    PDFMcpJsonWriter writer;
    writer.beginObject();
    PDFAppMcpDocumentHeader::create(document).write(writer);

    PDFToolManager* toolManager = view ? view->getToolManager() : nullptr;
    PDFSelectTextTool* selectTextTool = toolManager ? toolManager->getSelectTextTool() : nullptr;
    PDFDrawWidgetProxy* proxy = view ? view->getProxy() : nullptr;
    PDFAsynchronousTextLayoutCompiler* compiler = proxy ? proxy->getTextLayoutCompiler() : nullptr;
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    if (selectTextTool && compiler && pdfDocument && view->hasTextSelection())
    {
        // The text of the selection (empty, when the document does not allow copying)
        const QString text = selectTextTool->getSelectedText();
        const PDFTextSelection& selection = selectTextTool->getTextSelection();
        auto it = selection.begin();
        if (!text.isEmpty() && it != selection.end())
        {
            const PDFInteger pageIndex = it->start.pageIndex;
            PDFTextLayoutGetter layoutGetter = compiler->getTextLayoutLazy(pageIndex);
            PDFTextSelectionPainter painter(&selection);
            QPolygonF quadrilaterals;
            painter.prepareGeometry(pageIndex, layoutGetter, QTransform(), &quadrilaterals);

            writer.key(QLatin1StringView("selection"));
            writer.beginObject();
            writer.writeMember(QLatin1StringView("page"), qint64(pageIndex + 1));
            writer.writeMember(QLatin1StringView("label"), Support::sanitizeName(PDFAppMcpArguments::getPageLabel(pdfDocument.data(), pageIndex), 256));
            writer.writeMember(QLatin1StringView("text"), Support::sanitizeContent(text));
            Support::writeQuads(writer, QLatin1StringView("quads"), quadrilaterals);
            Support::writeRect(writer, QLatin1StringView("rect"), quadrilaterals.boundingRect());
            writer.endObject();
        }
    }

    writer.writeMember(QLatin1StringView("origin"), QLatin1StringView("document"));
    writer.endObject();
    return writer.take();
}

QByteArray PDFAppMcpDocumentReader::getLayers(const PDFAppDocument* document)
{
    using Support = PDFAppMcpReadSupport;

    PDFMcpJsonWriter writer;
    writer.beginObject();
    PDFAppMcpDocumentHeader::create(document).write(writer);
    writer.key(QLatin1StringView("layers"));
    writer.beginArray();

    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    const PDFOptionalContentProperties* properties = pdfDocument ? pdfDocument->getCatalog()->getOptionalContentProperties() : nullptr;
    if (properties && properties->isValid())
    {
        const PDFOptionalContentActivity* activity = document->getOptionalContentActivity();
        const std::vector<PDFObjectReference>& locked = properties->getDefaultConfiguration().getLocked();
        int count = 0;
        for (const PDFObjectReference& reference : properties->getAllOptionalContentGroups())
        {
            if (count++ >= MAXIMUM_LAYERS)
            {
                break;
            }

            const PDFOptionalContentGroup& group = properties->getOptionalContentGroup(reference);
            const bool isVisible = activity ? activity->getState(reference) != OCState::OFF : true;
            writer.beginObject();
            writer.writeMember(QLatin1StringView("id"), PDFAppMcpArguments::formatAnnotationId(reference.objectNumber, reference.generation));
            writer.writeMember(QLatin1StringView("name"), Support::sanitizeName(group.getName(), 256));
            writer.writeMember(QLatin1StringView("visible"), isVisible);
            writer.writeMember(QLatin1StringView("locked"), std::find(locked.cbegin(), locked.cend(), reference) != locked.cend());
            writer.endObject();
        }
    }

    writer.endArray();
    writer.writeMember(QLatin1StringView("origin"), QLatin1StringView("document"));
    writer.endObject();
    return writer.take();
}

QByteArray PDFAppMcpDocumentReader::getBookmarks(PDFAppContext* context, const PDFAppDocument* document)
{
    using Support = PDFAppMcpReadSupport;

    PDFMcpJsonWriter writer;
    writer.beginObject();
    PDFAppMcpDocumentHeader::create(document).write(writer);
    writer.key(QLatin1StringView("bookmarks"));
    writer.beginArray();

    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    if (context && pdfDocument && !document->getFilePath().isEmpty())
    {
        const PDFInteger pageCount = PDFInteger(pdfDocument->getCatalog()->getPageCount());
        for (const PDFBookmark& bookmark : PDFBookmarkRepository(context->getDatabase()).getBookmarks(document->getFilePath()))
        {
            if (bookmark.page < 0 || bookmark.page >= pageCount)
            {
                continue;
            }
            writer.beginObject();
            writer.writeMember(QLatin1StringView("title"), Support::sanitizeName(bookmark.title, 256));
            writer.writeMember(QLatin1StringView("page"), qint64(bookmark.page + 1));
            writer.writeMember(QLatin1StringView("label"), Support::sanitizeName(PDFAppMcpArguments::getPageLabel(pdfDocument.data(), bookmark.page), 256));
            writer.writeMember(QLatin1StringView("auto"), false);
            writer.endObject();
        }
    }

    writer.endArray();
    writer.endObject();
    return writer.take();
}

}   // namespace pdf
