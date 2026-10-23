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

#include "pdfappdocumentpropertiesdata.h"
#include "pdfapplocale.h"

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfencoding.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdfsecurityhandler.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QPageSize>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <set>
#include <tuple>

namespace pdf
{

/// Helpers of the data of the properties (file-local)
class PDFAppDocumentPropertiesDataHelper
{
public:
    PDFAppDocumentPropertiesDataHelper() = delete;

    static PDFAppPropertyItem item(const QString& key, const QString& value, const char* objectName, const QByteArray& infoKey = QByteArray())
    {
        PDFAppPropertyItem result;
        result.key = key;
        result.value = PDFAppDocumentPropertiesData::sanitize(value);

        // A value, which the row cannot show as it is (line breaks, a text longer
        // than MAXIMUM_VALUE_LENGTH), is read only: its edit would write the shown
        // text back and lose the rest of the value
        result.infoKey = result.value == value ? infoKey : QByteArray();
        result.objectName = QString::fromLatin1(objectName);
        return result;
    }

    static QString yesNo(bool value)
    {
        return value ? PDFAppDocumentPropertiesData::tr("Yes") : PDFAppDocumentPropertiesData::tr("No");
    }

    /// Text string of a value of the Info dictionary (empty, when it is not a string)
    static QString infoText(const PDFDocument* document, const PDFObject& object)
    {
        const PDFObject& value = document->getObject(object);
        if (value.isString())
        {
            return PDFEncoding::convertTextString(value.getString());
        }
        if (value.isName())
        {
            return QString::fromLatin1(value.getString());
        }
        return QString();
    }

    /// Name of a key, which is untrusted data (a PDF name)
    static QString keyText(const QByteArray& key)
    {
        return PDFAppDocumentPropertiesData::sanitize(QString::fromUtf8(key));
    }

    /// Text of a size of a page in millimeters ("210 × 297 mm")
    /// Length in whole millimeters (the paper sizes of the standards are whole millimeters)
    static QString formatMillimeters(qreal value)
    {
        return PDFAppLocale::formatNumber(qRound(qBound<qreal>(0.0, value, 1.0e9)));
    }

    static QString formatPaperSize(const QSizeF& size)
    {
        return PDFAppDocumentPropertiesData::tr("%1 × %2 mm").arg(formatMillimeters(size.width()), formatMillimeters(size.height()));
    }

    /// The permission is granted (always for a document, which is not encrypted)
    static bool isAllowed(const PDFSecurityHandler* handler, PDFSecurityHandler::Permission permission)
    {
        return !handler || handler->getMode() == EncryptionMode::None || handler->isAllowed(permission);
    }

    /// Sets the message of a failed validation. \returns false
    static bool fail(QString* errorMessage, const QString& message)
    {
        if (errorMessage)
        {
            *errorMessage = message;
        }
        return false;
    }

    /// Name of the standard paper format of the size (empty = no standard format)
    static QString paperFormat(const QSizeF& size)
    {
        if (!(size.width() > 0.0) || !(size.height() > 0.0) || !qIsFinite(size.width()) || !qIsFinite(size.height()))
        {
            return QString();
        }

        const QPageSize::PageSizeId id = QPageSize::id(size, QPageSize::Millimeter, QPageSize::FuzzyOrientationMatch);
        if (id == QPageSize::Custom)
        {
            return QString();
        }
        return QPageSize::name(id);
    }

    /// Size of the page in millimeters (rotated, nothing for an invalid page)
    static QSizeF pageSize(const PDFDocument* document, size_t pageIndex)
    {
        const PDFCatalog* catalog = document->getCatalog();
        if (pageIndex >= catalog->getPageCount())
        {
            return QSizeF();
        }
        return catalog->getPage(pageIndex)->getRotatedMediaBoxMM().size();
    }

    static QString pageLayoutText(PageLayout layout)
    {
        switch (layout)
        {
            case PageLayout::SinglePage:
                return PDFAppDocumentPropertiesData::tr("Single page");
            case PageLayout::OneColumn:
                return PDFAppDocumentPropertiesData::tr("Continuous");
            case PageLayout::TwoColumnLeft:
                return PDFAppDocumentPropertiesData::tr("Two columns, odd pages on the left");
            case PageLayout::TwoColumnRight:
                return PDFAppDocumentPropertiesData::tr("Two columns, odd pages on the right");
            case PageLayout::TwoPagesLeft:
                return PDFAppDocumentPropertiesData::tr("Two pages, odd pages on the left");
            case PageLayout::TwoPagesRight:
                return PDFAppDocumentPropertiesData::tr("Two pages, odd pages on the right");
            case PageLayout::Custom:
                return PDFAppDocumentPropertiesData::tr("Custom");
        }
        return PDFAppDocumentPropertiesData::getEmptyValueText();
    }

    static QString pageModeText(PageMode mode)
    {
        switch (mode)
        {
            case PageMode::UseNone:
                return PDFAppDocumentPropertiesData::tr("Page only");
            case PageMode::UseOutlines:
                return PDFAppDocumentPropertiesData::tr("Show outlines");
            case PageMode::UseThumbnails:
                return PDFAppDocumentPropertiesData::tr("Show thumbnails");
            case PageMode::Fullscreen:
                return PDFAppDocumentPropertiesData::tr("Full screen");
            case PageMode::UseOptionalContent:
                return PDFAppDocumentPropertiesData::tr("Show layers");
            case PageMode::UseAttachments:
                return PDFAppDocumentPropertiesData::tr("Show attachments");
        }
        return PDFAppDocumentPropertiesData::getEmptyValueText();
    }

    static QString duplexText(PDFViewerPreferences::Duplex duplex)
    {
        switch (duplex)
        {
            case PDFViewerPreferences::Duplex::None:
                return PDFAppDocumentPropertiesData::tr("Not set");
            case PDFViewerPreferences::Duplex::Simplex:
                return PDFAppDocumentPropertiesData::tr("Simplex");
            case PDFViewerPreferences::Duplex::DuplexFlipShortEdge:
                return PDFAppDocumentPropertiesData::tr("Duplex (flip short edge)");
            case PDFViewerPreferences::Duplex::DuplexFlipLongEdge:
                return PDFAppDocumentPropertiesData::tr("Duplex (flip long edge)");
        }
        return PDFAppDocumentPropertiesData::getEmptyValueText();
    }

    static QString pageRangesText(const std::vector<std::pair<PDFInteger, PDFInteger>>& ranges)
    {
        if (ranges.empty())
        {
            return PDFAppDocumentPropertiesData::tr("All pages");
        }

        const QLocale locale = PDFAppLocale::getUiLocale();
        QStringList parts;
        for (const auto& range : ranges)
        {
            if (parts.size() >= 100)
            {
                parts << QStringLiteral("…");
                break;
            }
            parts << (range.first == range.second ? locale.toString(range.first)
                                                  : QStringLiteral("%1–%2").arg(locale.toString(range.first), locale.toString(range.second)));
        }
        return parts.join(QStringLiteral(", "));
    }

    /// Text of a decoded metadata packet (BOM of UTF-16 / UTF-8, UTF-8 otherwise)
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

    static void writeAltText(QXmlStreamWriter& writer, const QString& element, const QString& text)
    {
        writer.writeStartElement(element);
        writer.writeStartElement(QStringLiteral("rdf:Alt"));
        writer.writeStartElement(QStringLiteral("rdf:li"));
        writer.writeAttribute(QStringLiteral("xml:lang"), QStringLiteral("x-default"));
        writer.writeCharacters(text);
        writer.writeEndElement();
        writer.writeEndElement();
        writer.writeEndElement();
    }

    static QString isoDate(const QDateTime& dateTime)
    {
        return dateTime.toOffsetFromUtc(dateTime.offsetFromUtc()).toString(Qt::ISODate);
    }

    /// Name of a font without the tag of a subset ("ABCDEF+Name"); \p isSubset is set
    static QString fontName(const QByteArray& name, bool* isSubset)
    {
        QString text = PDFAppDocumentPropertiesData::sanitize(QString::fromUtf8(name.left(512)));
        *isSubset = false;
        if (text.size() > 7 && text.at(6) == QLatin1Char('+'))
        {
            bool isTag = true;
            for (int i = 0; i < 6; ++i)
            {
                isTag = isTag && text.at(i) >= QLatin1Char('A') && text.at(i) <= QLatin1Char('Z');
            }
            if (isTag)
            {
                *isSubset = true;
                text = text.mid(7);
            }
        }
        return text;
    }

    /// Scans the fonts of the resources (and of the nested form XObjects)
    static void scanResources(const PDFDocument* document, const PDFObject& resourcesObject, int depth, std::set<PDFObjectReference>& visitedFonts,
                              std::set<PDFObjectReference>& visitedForms, std::vector<PDFAppFontInfo>& fonts)
    {
        if (depth > PDFAppDocumentFontScanTask::MAXIMUM_DEPTH || int(fonts.size()) >= PDFAppDocumentFontScanTask::MAXIMUM_FONTS)
        {
            return;
        }

        const PDFDictionary* resources = document->getDictionaryFromObject(resourcesObject);
        if (!resources)
        {
            return;
        }

        if (const PDFDictionary* fontsDictionary = document->getDictionaryFromObject(resources->get("Font")))
        {
            for (size_t i = 0; i < fontsDictionary->getCount() && int(fonts.size()) < PDFAppDocumentFontScanTask::MAXIMUM_FONTS; ++i)
            {
                const PDFObject& object = fontsDictionary->getValue(i);
                if (object.isReference())
                {
                    if (visitedFonts.count(object.getReference()))
                    {
                        continue;
                    }
                    visitedFonts.insert(object.getReference());
                }

                try
                {
                    const QByteArray key = fontsDictionary->getKey(i).getString();
                    PDFFontPointer font = PDFFont::createFont(object, key, document);
                    if (!font)
                    {
                        continue;
                    }

                    const FontDescriptor* descriptor = font->getFontDescriptor();
                    const FontType type = font->getFontType();

                    PDFAppFontInfo info;
                    info.name = fontName(descriptor->fontName, &info.isSubset);
                    if (info.name.isEmpty())
                    {
                        // A font without a descriptor (a standard font, a Type 3 font): its base font, or its key
                        const PDFDictionary* fontDictionary = document->getDictionaryFromObject(object);
                        const PDFObject& baseFont = fontDictionary ? document->getObject(fontDictionary->get("BaseFont")) : PDFObject();
                        info.name = fontName(baseFont.isName() ? baseFont.getString() : key, &info.isSubset);
                    }
                    info.family = PDFAppDocumentPropertiesData::sanitize(QString::fromUtf8(descriptor->fontFamily.left(512)));
                    info.type = PDFAppDocumentPropertiesData::getFontTypeText(int(type));
                    info.isEmbedded = type == FontType::Type3 || descriptor->isEmbedded();
                    fonts.push_back(std::move(info));
                }
                catch (const PDFException&)
                {
                    // A damaged font is left out
                }
            }
        }

        // Fonts of the form XObjects
        if (const PDFDictionary* xobjects = document->getDictionaryFromObject(resources->get("XObject")))
        {
            for (size_t i = 0; i < xobjects->getCount(); ++i)
            {
                const PDFObject& object = xobjects->getValue(i);
                if (object.isReference())
                {
                    if (visitedForms.count(object.getReference()))
                    {
                        continue;
                    }
                    visitedForms.insert(object.getReference());
                }

                const PDFObject& dereferenced = document->getObject(object);
                if (!dereferenced.isStream())
                {
                    continue;
                }

                const PDFDictionary* dictionary = dereferenced.getStream()->getDictionary();
                const PDFObject& subtype = document->getObject(dictionary->get("Subtype"));
                if (subtype.isName() && subtype.getString() == "Form")
                {
                    scanResources(document, dictionary->get("Resources"), depth + 1, visitedFonts, visitedForms, fonts);
                }
            }
        }
    }

    static bool isFontBefore(const PDFAppFontInfo& left, const PDFAppFontInfo& right)
    {
        const int comparison = QString::localeAwareCompare(left.name, right.name);
        if (comparison != 0)
        {
            return comparison < 0;
        }

        // Every field of isSameFont is a part of the order, so std::unique finds
        // the same fonts next to each other
        return std::tie(left.name, left.type, left.family, left.isEmbedded, left.isSubset) <
               std::tie(right.name, right.type, right.family, right.isEmbedded, right.isSubset);
    }

    static bool isSameFont(const PDFAppFontInfo& left, const PDFAppFontInfo& right)
    {
        return left.name == right.name && left.family == right.family && left.type == right.type &&
               left.isEmbedded == right.isEmbedded && left.isSubset == right.isSubset;
    }
};

QString PDFAppFontInfo::getStatusText() const
{
    if (!isEmbedded)
    {
        return PDFAppDocumentPropertiesData::tr("Not embedded");
    }
    return isSubset ? PDFAppDocumentPropertiesData::tr("Embedded subset") : PDFAppDocumentPropertiesData::tr("Embedded");
}

QList<QByteArray> PDFAppDocumentPropertiesData::getEditableInfoKeys()
{
    return { QByteArrayLiteral("Title"), QByteArrayLiteral("Subject"), QByteArrayLiteral("Author"), QByteArrayLiteral("Keywords") };
}

QString PDFAppDocumentPropertiesData::getEmptyValueText()
{
    return QStringLiteral("—");
}

QString PDFAppDocumentPropertiesData::sanitize(const QString& text)
{
    QString result = text.left(MAXIMUM_VALUE_LENGTH);
    for (QChar& character : result)
    {
        // Line breaks and other control characters of a value of one line
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

QString PDFAppDocumentPropertiesData::formatDateTime(const QDateTime& dateTime)
{
    if (!dateTime.isValid())
    {
        return QString();
    }

    // O17: the language of the user interface, not the locale of the system
    return PDFAppLocale::formatDateTime(dateTime.toLocalTime());
}

QString PDFAppDocumentPropertiesData::getFontTypeText(int fontType)
{
    switch (FontType(fontType))
    {
        case FontType::Type0:
            return tr("Type 0 (CID keyed)");
        case FontType::Type1:
            return tr("Type 1 (8 bit keyed)");
        case FontType::MMType1:
            return tr("Multiple master Type 1");
        case FontType::TrueType:
            return tr("TrueType");
        case FontType::Type3:
            return tr("Type 3 (glyph procedures)");
        case FontType::Invalid:
            break;
    }
    return tr("Unknown");
}

std::vector<PDFAppPropertyGroup> PDFAppDocumentPropertiesData::createGeneralGroups(const PDFDocument* document, const QString& filePath, const QString& title, qint64 lastRead)
{
    using Helper = PDFAppDocumentPropertiesDataHelper;

    std::vector<PDFAppPropertyGroup> groups;
    if (!document)
    {
        return groups;
    }

    const PDFDocumentInfo* info = document->getInfo();
    const PDFCatalog* catalog = document->getCatalog();
    const QLocale locale = PDFAppLocale::getUiLocale();

    // Description: the editable texts of the Info dictionary and the producer
    PDFAppPropertyGroup description;
    description.id = QStringLiteral("description");
    description.title = tr("Description");
    description.items.push_back(Helper::item(tr("Title"), info->title, "property-title", QByteArrayLiteral("Title")));
    description.items.push_back(Helper::item(tr("Subject"), info->subject, "property-subject", QByteArrayLiteral("Subject")));
    description.items.push_back(Helper::item(tr("Author"), info->author, "property-author", QByteArrayLiteral("Author")));
    description.items.push_back(Helper::item(tr("Keywords"), info->keywords, "property-keywords", QByteArrayLiteral("Keywords")));
    description.items.push_back(Helper::item(tr("Creator"), info->creator, "property-creator"));
    description.items.push_back(Helper::item(tr("Producer"), info->producer, "property-producer"));
    description.items.push_back(Helper::item(tr("PDF version"), QString::fromLatin1(document->getVersion().left(16)), "property-pdf-version"));
    description.items.push_back(Helper::item(tr("Created"), formatDateTime(info->creationDate), "property-created"));
    description.items.push_back(Helper::item(tr("Modified"), formatDateTime(info->modifiedDate), "property-modified"));
    groups.push_back(std::move(description));

    // Content: pages, the paper of the first page (various sizes, when the pages differ)
    PDFAppPropertyGroup content;
    content.id = QStringLiteral("content");
    content.title = tr("Content");
    const size_t pageCount = catalog->getPageCount();
    content.items.push_back(Helper::item(tr("Page count"), locale.toString(qulonglong(pageCount)), "property-page-count"));

    const QSizeF firstSize = Helper::pageSize(document, 0);
    bool isVarious = false;
    for (size_t i = 1; i < pageCount && !isVarious; ++i)
    {
        const QSizeF size = Helper::pageSize(document, i);
        isVarious = qAbs(size.width() - firstSize.width()) > 1.0 || qAbs(size.height() - firstSize.height()) > 1.0;
    }
    QString format = Helper::paperFormat(firstSize);
    if (pageCount == 0)
    {
        format.clear();
    }
    else if (isVarious)
    {
        format = tr("Various sizes");
    }
    else if (format.isEmpty())
    {
        format = tr("Custom");
    }
    content.items.push_back(Helper::item(tr("Paper format"), format, "property-paper-format"));
    QString paperSize = pageCount > 0 ? Helper::formatPaperSize(firstSize) : QString();
    if (isVarious)
    {
        paperSize = tr("%1 (page 1)").arg(paperSize);
    }
    content.items.push_back(Helper::item(tr("Paper size"), paperSize, "property-paper-size"));

    QString trapped;
    switch (info->trapped)
    {
        case PDFDocumentInfo::Trapped::True:
            trapped = tr("Yes");
            break;
        case PDFDocumentInfo::Trapped::False:
            trapped = tr("No");
            break;
        case PDFDocumentInfo::Trapped::Unknown:
            trapped = tr("Unknown");
            break;
    }
    content.items.push_back(Helper::item(tr("Trapped"), trapped, "property-trapped"));
    groups.push_back(std::move(content));

    // File: the file on the disk (a document without a file is not saved yet)
    PDFAppPropertyGroup file;
    file.id = QStringLiteral("file");
    file.title = tr("File");
    if (filePath.isEmpty())
    {
        file.items.push_back(Helper::item(tr("Name"), title, "property-name"));
        file.items.push_back(Helper::item(tr("Folder"), tr("Not saved yet"), "property-folder"));
    }
    else
    {
        const QFileInfo fileInfo(filePath);
        file.items.push_back(Helper::item(tr("Name"), fileInfo.fileName(), "property-name"));
        file.items.push_back(Helper::item(tr("Folder"), QDir::toNativeSeparators(fileInfo.absolutePath()), "property-folder"));
        file.items.push_back(Helper::item(tr("Size"), fileInfo.exists() ? locale.formattedDataSize(fileInfo.size(), 1, QLocale::DataSizeTraditionalFormat) : tr("The file does not exist"), "property-size"));
        file.items.push_back(Helper::item(tr("Writable"), Helper::yesNo(fileInfo.exists() && fileInfo.isWritable()), "property-writable"));
        file.items.push_back(Helper::item(tr("Created"), formatDateTime(fileInfo.birthTime()), "property-file-created"));
        file.items.push_back(Helper::item(tr("Modified"), formatDateTime(fileInfo.lastModified()), "property-file-modified"));
        file.items.push_back(Helper::item(tr("Last read"), lastRead > 0 ? formatDateTime(QDateTime::fromMSecsSinceEpoch(lastRead)) : QString(), "property-last-read"));
    }
    groups.push_back(std::move(file));

    // Custom properties: the other entries of the Info dictionary (texts are editable)
    PDFAppPropertyGroup custom;
    custom.id = QStringLiteral("custom");
    custom.title = tr("Custom properties");
    for (const auto& entry : info->extra)
    {
        if (int(custom.items.size()) >= MAXIMUM_CUSTOM_PROPERTIES)
        {
            break;
        }
        if (entry.first.isEmpty() || entry.first.size() > 127)
        {
            continue;
        }

        const QVariant& value = entry.second;
        if (value.typeId() == QMetaType::QDateTime)
        {
            custom.items.push_back(Helper::item(Helper::keyText(entry.first), formatDateTime(value.toDateTime()), "property-custom"));
        }
        else
        {
            custom.items.push_back(Helper::item(Helper::keyText(entry.first), value.toString(), "property-custom", entry.first));
        }
    }
    if (!custom.items.empty())
    {
        groups.push_back(std::move(custom));
    }

    return groups;
}

PDFAppPropertyGroup PDFAppDocumentPropertiesData::createEncryptionGroup(const PDFDocument* document)
{
    using Helper = PDFAppDocumentPropertiesDataHelper;

    PDFAppPropertyGroup group;
    group.id = QStringLiteral("encryption");
    group.title = tr("Encryption");

    const PDFSecurityHandler* handler = document ? document->getStorage().getSecurityHandler() : nullptr;
    const EncryptionMode mode = handler ? handler->getMode() : EncryptionMode::None;

    QString encryption;
    switch (mode)
    {
        case EncryptionMode::None:
            encryption = tr("Not encrypted");
            break;
        case EncryptionMode::Standard:
            encryption = tr("Standard (password)");
            break;
        case EncryptionMode::PublicKey:
            encryption = tr("Certificate (public key)");
            break;
        case EncryptionMode::Custom:
            encryption = tr("Custom security handler");
            break;
    }
    group.items.push_back(Helper::item(tr("Document encryption"), encryption, "property-document-encryption"));

    if (mode == EncryptionMode::None || !handler)
    {
        return group;
    }

    QString authorization;
    switch (handler->getAuthorizationResult())
    {
        case PDFSecurityHandler::AuthorizationResult::NoAuthorizationRequired:
            authorization = tr("Not required");
            break;
        case PDFSecurityHandler::AuthorizationResult::UserAuthorized:
            authorization = tr("User");
            break;
        case PDFSecurityHandler::AuthorizationResult::OwnerAuthorized:
            authorization = tr("Owner");
            break;
        case PDFSecurityHandler::AuthorizationResult::Failed:
            authorization = tr("Not authorized");
            break;
        case PDFSecurityHandler::AuthorizationResult::Cancelled:
            authorization = tr("Canceled");
            break;
    }
    group.items.push_back(Helper::item(tr("Authorized as"), authorization, "property-authorized-as"));
    group.items.push_back(Helper::item(tr("Metadata encrypted"), Helper::yesNo(handler->isMetadataEncrypted()), "property-metadata-encrypted"));

    QString version;
    switch (handler->getVersion())
    {
        case 1:
            version = tr("1 (RC4, 40 bit)");
            break;
        case 2:
            version = tr("2 (RC4, 40 - 128 bit)");
            break;
        case 3:
            version = tr("3 (unpublished algorithm)");
            break;
        case 4:
            version = tr("4 (RC4 or AES, 128 bit)");
            break;
        case 5:
            version = tr("5 (AES, 256 bit)");
            break;
        default:
            version = QString::number(handler->getVersion());
            break;
    }
    group.items.push_back(Helper::item(tr("Version"), version, "property-version"));
    return group;
}

QString PDFAppDocumentPropertiesData::getSecurityNote(const PDFDocument* document)
{
    const PDFSecurityHandler* handler = document ? document->getStorage().getSecurityHandler() : nullptr;
    if (!handler || handler->getMode() == EncryptionMode::None || handler->getAuthorizationResult() != PDFSecurityHandler::AuthorizationResult::UserAuthorized)
    {
        return QString();
    }

    for (const PDFAppPermissionItem& permission : createPermissions(document))
    {
        if (!permission.isAllowed)
        {
            return tr("You opened the document without the owner password. Open it with the owner password to lift the restrictions.");
        }
    }
    return QString();
}

std::vector<PDFAppPermissionItem> PDFAppDocumentPropertiesData::createPermissions(const PDFDocument* document)
{
    using Permission = PDFSecurityHandler::Permission;

    using Helper = PDFAppDocumentPropertiesDataHelper;

    const PDFSecurityHandler* handler = document ? document->getStorage().getSecurityHandler() : nullptr;
    std::vector<PDFAppPermissionItem> permissions;
    permissions.push_back({ tr("Print (low resolution)"), Helper::isAllowed(handler, Permission::PrintLowResolution) });
    permissions.push_back({ tr("Print (high resolution)"), Helper::isAllowed(handler, Permission::PrintHighResolution) });
    permissions.push_back({ tr("Content extraction"), Helper::isAllowed(handler, Permission::CopyContent) });
    permissions.push_back({ tr("Content extraction (accessibility)"), Helper::isAllowed(handler, Permission::Accessibility) });
    permissions.push_back({ tr("Page assembling"), Helper::isAllowed(handler, Permission::Assemble) });
    permissions.push_back({ tr("Modify content"), Helper::isAllowed(handler, Permission::Modify) });
    permissions.push_back({ tr("Modify interactive items"), Helper::isAllowed(handler, Permission::ModifyInteractiveItems) });
    permissions.push_back({ tr("Fill form fields"), Helper::isAllowed(handler, Permission::ModifyFormFields) });
    return permissions;
}

std::vector<PDFAppPropertyGroup> PDFAppDocumentPropertiesData::createDisplayGroups(const PDFDocument* document)
{
    using Helper = PDFAppDocumentPropertiesDataHelper;

    std::vector<PDFAppPropertyGroup> groups;
    if (!document)
    {
        return groups;
    }

    const PDFCatalog* catalog = document->getCatalog();
    const PDFViewerPreferences* preferences = catalog->getViewerPreferences();

    PDFAppPropertyGroup viewer;
    viewer.id = QStringLiteral("viewer");
    viewer.title = tr("Viewer settings");
    viewer.items.push_back(Helper::item(tr("Page layout"), Helper::pageLayoutText(catalog->getPageLayout()), "property-page-layout"));
    viewer.items.push_back(Helper::item(tr("View mode"), Helper::pageModeText(catalog->getPageMode()), "property-view-mode"));
    viewer.items.push_back(Helper::item(tr("Writing direction"), preferences->getDirection() == PDFViewerPreferences::Direction::RightToLeft ? tr("Right to left") : tr("Left to right"),
                                        "property-writing-direction"));
    viewer.items.push_back(Helper::item(tr("Window title"), preferences->getOptions().testFlag(PDFViewerPreferences::DisplayDocTitle) ? tr("Title of the document") : tr("Name of the file"),
                                        "property-window-title"));
    groups.push_back(std::move(viewer));

    PDFAppPropertyGroup print;
    print.id = QStringLiteral("print");
    print.title = tr("Default print settings");
    print.items.push_back(Helper::item(tr("Scale"), preferences->getPrintScaling() == PDFViewerPreferences::PrintScaling::None ? tr("None") : tr("Application default"), "property-scale"));
    print.items.push_back(Helper::item(tr("Duplex mode"), Helper::duplexText(preferences->getDuplex()), "property-duplex-mode"));
    print.items.push_back(Helper::item(tr("Pick tray by page size"), Helper::yesNo(preferences->getOptions().testFlag(PDFViewerPreferences::PickTrayByPDFSize)), "property-pick-tray"));
    print.items.push_back(Helper::item(tr("Print page ranges"), Helper::pageRangesText(preferences->getPrintPageRanges()), "property-print-page-ranges"));
    print.items.push_back(Helper::item(tr("Number of copies"), PDFAppLocale::formatNumber(qMax<PDFInteger>(1, preferences->getNumberOfCopies())), "property-number-of-copies"));
    groups.push_back(std::move(print));

    return groups;
}

QString PDFAppDocumentPropertiesData::getMetadata(const PDFDocument* document, bool* hasMetadata, QString* errorMessage)
{
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

    try
    {
        const QByteArray data = document->getDecodedStream(metadata.getStream());
        if (data.size() > MAXIMUM_METADATA_SIZE)
        {
            if (errorMessage)
            {
                *errorMessage = tr("The metadata packet is too large to be shown (%1).").arg(PDFAppLocale::formatDataSize(data.size(), 1, QLocale::DataSizeTraditionalFormat));
            }
            return QString();
        }
        return PDFAppDocumentPropertiesDataHelper::decodeMetadata(data);
    }
    catch (const PDFException& exception)
    {
        if (errorMessage)
        {
            *errorMessage = tr("The metadata packet cannot be read: %1").arg(sanitize(exception.getMessage()));
        }
    }
    return QString();
}

QByteArray PDFAppDocumentPropertiesData::createDefaultMetadata(const PDFDocument* document, const QDateTime& now)
{
    using Helper = PDFAppDocumentPropertiesDataHelper;

    const PDFDocumentInfo emptyInfo;
    const PDFDocumentInfo* info = document ? document->getInfo() : &emptyInfo;

    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(2);
    writer.writeProcessingInstruction(QStringLiteral("xpacket"), QStringLiteral("begin=\"%1\" id=\"W5M0MpCehiHzreSzNTczkc9d\"").arg(QChar(0xFEFF)));
    writer.writeStartElement(QStringLiteral("x:xmpmeta"));
    writer.writeNamespace(QStringLiteral("adobe:ns:meta/"), QStringLiteral("x"));
    writer.writeStartElement(QStringLiteral("rdf:RDF"));
    writer.writeNamespace(QStringLiteral("http://www.w3.org/1999/02/22-rdf-syntax-ns#"), QStringLiteral("rdf"));
    writer.writeStartElement(QStringLiteral("rdf:Description"));
    writer.writeAttribute(QStringLiteral("rdf:about"), QString());
    writer.writeNamespace(QStringLiteral("http://purl.org/dc/elements/1.1/"), QStringLiteral("dc"));
    writer.writeNamespace(QStringLiteral("http://ns.adobe.com/xap/1.0/"), QStringLiteral("xmp"));
    writer.writeNamespace(QStringLiteral("http://ns.adobe.com/pdf/1.3/"), QStringLiteral("pdf"));

    writer.writeTextElement(QStringLiteral("dc:format"), QStringLiteral("application/pdf"));
    if (!info->title.isEmpty())
    {
        Helper::writeAltText(writer, QStringLiteral("dc:title"), info->title);
    }
    if (!info->author.isEmpty())
    {
        writer.writeStartElement(QStringLiteral("dc:creator"));
        writer.writeStartElement(QStringLiteral("rdf:Seq"));
        writer.writeTextElement(QStringLiteral("rdf:li"), info->author);
        writer.writeEndElement();
        writer.writeEndElement();
    }
    if (!info->subject.isEmpty())
    {
        Helper::writeAltText(writer, QStringLiteral("dc:description"), info->subject);
    }
    if (info->creationDate.isValid())
    {
        writer.writeTextElement(QStringLiteral("xmp:CreateDate"), Helper::isoDate(info->creationDate));
    }
    if (info->modifiedDate.isValid())
    {
        writer.writeTextElement(QStringLiteral("xmp:ModifyDate"), Helper::isoDate(info->modifiedDate));
    }
    if (now.isValid())
    {
        writer.writeTextElement(QStringLiteral("xmp:MetadataDate"), Helper::isoDate(now));
    }
    if (!info->creator.isEmpty())
    {
        writer.writeTextElement(QStringLiteral("xmp:CreatorTool"), info->creator);
    }
    if (!info->producer.isEmpty())
    {
        writer.writeTextElement(QStringLiteral("pdf:Producer"), info->producer);
    }
    if (!info->keywords.isEmpty())
    {
        writer.writeTextElement(QStringLiteral("pdf:Keywords"), info->keywords);
    }

    writer.writeEndElement();   // rdf:Description
    writer.writeEndElement();   // rdf:RDF
    writer.writeEndElement();   // x:xmpmeta
    writer.writeProcessingInstruction(QStringLiteral("xpacket"), QStringLiteral("end=\"w\""));
    writer.writeEndDocument();
    return xml;
}

bool PDFAppDocumentPropertiesData::validateMetadata(const QByteArray& xml, QString* errorMessage)
{
    using Helper = PDFAppDocumentPropertiesDataHelper;

    if (xml.trimmed().isEmpty())
    {
        return Helper::fail(errorMessage, tr("The metadata packet is empty."));
    }
    if (xml.size() > MAXIMUM_METADATA_SIZE)
    {
        return Helper::fail(errorMessage, tr("The metadata packet is too large."));
    }

    QXmlStreamReader reader(xml);
    int elements = 0;
    while (!reader.atEnd())
    {
        switch (reader.readNext())
        {
            case QXmlStreamReader::DTD:
                // Entities of a DTD are refused (an expansion of entities, external files)
                return Helper::fail(errorMessage, tr("Document type definitions are not allowed in the metadata."));

            case QXmlStreamReader::StartElement:
                ++elements;
                break;

            default:
                break;
        }
    }

    if (reader.hasError())
    {
        return Helper::fail(errorMessage, tr("The metadata is not a well-formed XML (line %1, column %2): %3").arg(reader.lineNumber()).arg(reader.columnNumber()).arg(sanitize(reader.errorString())));
    }
    if (elements == 0)
    {
        return Helper::fail(errorMessage, tr("The metadata packet has no element."));
    }

    if (errorMessage)
    {
        errorMessage->clear();
    }
    return true;
}

PDFDocumentPointer PDFAppDocumentPropertiesData::setInfoValue(const PDFDocument* document, const QByteArray& key, const QString& value)
{
    if (!document || key.isEmpty() || key.size() > 127 || key.contains('\0'))
    {
        return PDFDocumentPointer();
    }

    try
    {
        PDFDocumentModifier modifier(document);
        PDFDocumentBuilder* builder = modifier.getBuilder();

        // The Info dictionary: an indirect object (usual), a direct dictionary of the trailer, or none
        PDFObjectReference infoReference = builder->getDocumentInfo();
        PDFDictionaryBuilder dictionary;
        if (infoReference.isValid())
        {
            if (const PDFDictionary* infoDictionary = builder->getDictionaryFromObject(builder->getObjectByReference(infoReference)))
            {
                dictionary = PDFDictionaryBuilder(*infoDictionary);
            }
        }
        else if (const PDFDictionary* trailer = document->getTrailerDictionary())
        {
            if (const PDFDictionary* infoDictionary = document->getDictionaryFromObject(trailer->get("Info")))
            {
                dictionary = PDFDictionaryBuilder(*infoDictionary);
            }
        }

        const QString oldValue = PDFAppDocumentPropertiesDataHelper::infoText(document, dictionary.get(key));
        const bool hasKey = dictionary.hasKey(key);
        if (oldValue == value && (hasKey || !value.isEmpty()))
        {
            return PDFDocumentPointer();
        }
        if (!hasKey && value.isEmpty())
        {
            return PDFDocumentPointer();
        }

        if (value.isEmpty())
        {
            dictionary.removeEntry(key.constData());
        }
        else
        {
            dictionary.setEntry(PDFInplaceOrMemoryString(key), PDFObjectFactory::createTextString(value));
        }

        PDFObject infoObject = PDFObject::createDictionary(std::move(dictionary));
        if (infoReference.isValid())
        {
            builder->setObject(infoReference, std::move(infoObject));
        }
        else
        {
            builder->setDocumentInfo(builder->addObject(std::move(infoObject)));
        }

        if (!modifier.finalize())
        {
            return PDFDocumentPointer();
        }
        return modifier.getDocument();
    }
    catch (const PDFException&)
    {
        return PDFDocumentPointer();
    }
}

PDFDocumentPointer PDFAppDocumentPropertiesData::setMetadata(const PDFDocument* document, const QByteArray& xml)
{
    if (!document || !validateMetadata(xml, nullptr))
    {
        return PDFDocumentPointer();
    }

    bool hasMetadata = false;
    const QString current = getMetadata(document, &hasMetadata, nullptr);
    if (hasMetadata && current.toUtf8() == xml)
    {
        return PDFDocumentPointer();
    }

    try
    {
        PDFDocumentModifier modifier(document);
        modifier.getBuilder()->setCatalogMetadata(xml);
        if (!modifier.finalize())
        {
            return PDFDocumentPointer();
        }
        return modifier.getDocument();
    }
    catch (const PDFException&)
    {
        return PDFDocumentPointer();
    }
}

QString PDFAppDocumentPropertiesData::createCopyText(const QString& heading, const std::vector<PDFAppPropertyGroup>& groups)
{
    QStringList lines;
    if (!heading.isEmpty())
    {
        lines << heading;
    }

    for (const PDFAppPropertyGroup& group : groups)
    {
        if (!lines.isEmpty())
        {
            lines << QString();
        }
        lines << group.title;
        for (const PDFAppPropertyItem& item : group.items)
        {
            lines << tr("%1: %2").arg(item.key, item.value.isEmpty() ? getEmptyValueText() : item.value);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

PDFAppDocumentFontScanTask::PDFAppDocumentFontScanTask(PDFDocumentPointer document, std::shared_ptr<std::atomic_bool> cancel) :
    m_document(std::move(document)),
    m_cancel(std::move(cancel))
{

}

std::vector<PDFAppFontInfo> PDFAppDocumentFontScanTask::operator()() const
{
    return scan(m_document.data(), m_cancel.get());
}

std::vector<PDFAppFontInfo> PDFAppDocumentFontScanTask::scan(const PDFDocument* document, const std::atomic_bool* cancel)
{
    using Helper = PDFAppDocumentPropertiesDataHelper;

    std::vector<PDFAppFontInfo> fonts;
    if (!document)
    {
        return fonts;
    }

    std::set<PDFObjectReference> visitedFonts;
    std::set<PDFObjectReference> visitedForms;
    std::set<PDFObjectReference> visitedResources;
    const PDFCatalog* catalog = document->getCatalog();
    for (size_t i = 0; i < catalog->getPageCount(); ++i)
    {
        if (cancel && cancel->load())
        {
            return std::vector<PDFAppFontInfo>();
        }

        // Pages often share their resources: a shared dictionary is scanned once
        const PDFObject& resources = catalog->getPage(i)->getResources();
        if (resources.isReference())
        {
            if (visitedResources.count(resources.getReference()))
            {
                continue;
            }
            visitedResources.insert(resources.getReference());
        }

        Helper::scanResources(document, resources, 0, visitedFonts, visitedForms, fonts);
    }

    // Each font once (direct font dictionaries of more pages are the same font)
    std::sort(fonts.begin(), fonts.end(), &Helper::isFontBefore);
    fonts.erase(std::unique(fonts.begin(), fonts.end(), &Helper::isSameFont), fonts.end());
    return fonts;
}

}   // namespace pdf
