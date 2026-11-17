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
#include "pdfappdocumentpermissions.h"
#include "pdfapplocale.h"

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreport.h"
#include "pdfencoding.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdfoperationcontrol.h"
#include "pdfsecurityhandler.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <numeric>
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

    /// A row of a key of the Info dictionary read by the core (R7-5: one implementation of the data):
    /// editable, when \p isEditable and the shown value is the whole value of the document
    static PDFAppPropertyItem infoItem(const PDFDocumentReportBasicInformation& information, const char* key, const QString& title, const char* objectName, bool isEditable)
    {
        const PDFDocumentReportBasicInformation::InfoEntry* entry = information.findInfo(QByteArray(key));
        PDFAppPropertyItem result;
        result.key = title;
        result.value = entry ? (entry->dateTime.isValid() ? PDFAppDocumentPropertiesData::formatDateTime(entry->dateTime) : entry->value) : QString();
        result.infoKey = isEditable && (!entry || !entry->isShortened) ? QByteArray(key) : QByteArray();
        result.objectName = QString::fromLatin1(objectName);
        return result;
    }

    /// The permission is granted by the data of the core (always without a document)
    static bool isAllowed(const PDFDocument* document, const PDFDocumentReportBasicInformation& information, PDFSecurityHandler::Permission permission)
    {
        if (!document)
        {
            return true;
        }
        for (const auto& item : information.permissions)
        {
            if (item.first == permission)
            {
                return item.second;
            }
        }
        return false;
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
        return PDFAppDocumentPropertiesData::getPaperFormatName(size);
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

/// The cancellation flag of the scan of the fonts as an operation control of the core (a thread of
/// QtConcurrent reads it; the flag is shared by the page and the task)
class PDFAppDocumentFontScanControl : public PDFOperationControl
{
public:
    explicit PDFAppDocumentFontScanControl(const std::atomic_bool* cancel) :
        m_cancel(cancel)
    {

    }

    virtual bool isOperationCanceled() const override
    {
        return m_cancel && m_cancel->load();
    }

private:
    const std::atomic_bool* m_cancel;
};

QString PDFAppFontInfo::getStatusText() const
{
    if (!isEmbedded)
    {
        return PDFAppDocumentPropertiesData::tr("Not embedded");
    }
    return isSubset ? PDFAppDocumentPropertiesData::tr("Embedded subset") : PDFAppDocumentPropertiesData::tr("Embedded");
}

QString PDFAppDocumentPropertiesData::getPaperFormatName(const QSizeF& sizeMM)
{
    // R7-5: the one implementation of the core (Document Report, PdfTool info)
    return PDFDocumentReport::getPaperFormatName(sizeMM);
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
    // R7-5: the one rule of the core (the same limit, MAXIMUM_VALUE_LENGTH)
    static_assert(MAXIMUM_VALUE_LENGTH == PDFDocumentReport::MAXIMUM_VALUE_LENGTH);
    return PDFDocumentReport::sanitize(text);
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

    // R7-5: the data of the core (Document Report, MCP, PdfTool info), the texts of the page here; the
    // parts, which parse the form and the XMP, are not read in the thread of the user interface
    const PDFDocumentReportBasicInformation information = PDFDocumentReport::getBasicInformation(document, { });
    const PDFCatalog* catalog = document->getCatalog();
    const QLocale locale = PDFAppLocale::getUiLocale();

    // Description: the editable texts of the Info dictionary and the producer
    PDFAppPropertyGroup description;
    description.id = QStringLiteral("description");
    description.title = tr("Description");
    description.items.push_back(Helper::infoItem(information, "Title", tr("Title"), "property-title", true));
    description.items.push_back(Helper::infoItem(information, "Subject", tr("Subject"), "property-subject", true));
    description.items.push_back(Helper::infoItem(information, "Author", tr("Author"), "property-author", true));
    description.items.push_back(Helper::infoItem(information, "Keywords", tr("Keywords"), "property-keywords", true));
    description.items.push_back(Helper::infoItem(information, "Creator", tr("Creator"), "property-creator", false));
    description.items.push_back(Helper::infoItem(information, "Producer", tr("Producer"), "property-producer", false));
    description.items.push_back(Helper::item(tr("PDF version"), information.version, "property-pdf-version"));
    description.items.push_back(Helper::infoItem(information, "CreationDate", tr("Created"), "property-created", false));
    description.items.push_back(Helper::infoItem(information, "ModDate", tr("Modified"), "property-modified", false));
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

    // /Trapped (14.11.6, Table 349): absent is Unknown
    QString trapped = tr("Unknown");
    if (information.trapped == "True")
    {
        trapped = tr("Yes");
    }
    else if (information.trapped == "False")
    {
        trapped = tr("No");
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
    for (const PDFDocumentReportBasicInformation::InfoEntry& entry : information.info)
    {
        if (!entry.isCustom || int(custom.items.size()) >= MAXIMUM_CUSTOM_PROPERTIES)
        {
            continue;
        }

        PDFAppPropertyItem item;
        item.key = Helper::keyText(entry.key);
        item.objectName = QStringLiteral("property-custom");
        if (entry.dateTime.isValid())
        {
            item.value = formatDateTime(entry.dateTime);
        }
        else
        {
            // A value, which the row cannot show whole, is read only (its edit would lose the rest)
            item.value = entry.value;
            item.infoKey = entry.isShortened ? QByteArray() : entry.key;
        }
        custom.items.push_back(std::move(item));
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

    // R7-5: the data of the core, the texts of the page
    const PDFDocumentReportBasicInformation information = PDFDocumentReport::getBasicInformation(document, { });
    const EncryptionMode mode = information.encryptionMode;

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

    if (mode == EncryptionMode::None)
    {
        return group;
    }

    QString authorization;
    switch (information.authorization)
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
        case PDFSecurityHandler::AuthorizationResult::Canceled:
            authorization = tr("Canceled");
            break;
    }
    group.items.push_back(Helper::item(tr("Authorized as"), authorization, "property-authorized-as"));
    group.items.push_back(Helper::item(tr("Metadata encrypted"), Helper::yesNo(information.isMetadataEncrypted), "property-metadata-encrypted"));

    QString version;
    switch (information.encryptionVersion)
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
            version = QString::number(information.encryptionVersion);
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
            return tr("You opened the document without the owner password. If you own it, unlock it with its owner password to lift the restrictions until it is closed.");
        }
    }
    return QString();
}

std::vector<PDFAppPermissionItem> PDFAppDocumentPropertiesData::createPermissions(const PDFDocument* document)
{
    using Permission = PDFSecurityHandler::Permission;

    // R7-5: the permissions of the core (Table 22 with the table of revision 2, the owner has all of
    // them - the rule of PDFAppDocumentPermissions); without a document everything is allowed
    using Helper = PDFAppDocumentPropertiesDataHelper;

    const PDFDocumentReportBasicInformation information = PDFDocumentReport::getBasicInformation(document, { });
    std::vector<PDFAppPermissionItem> permissions;
    permissions.push_back({ tr("Print (low resolution)"), Helper::isAllowed(document, information, Permission::PrintLowResolution) });
    permissions.push_back({ tr("Print (high resolution)"), Helper::isAllowed(document, information, Permission::PrintHighResolution) });
    permissions.push_back({ tr("Content extraction"), Helper::isAllowed(document, information, Permission::CopyContent) });
    permissions.push_back({ tr("Content extraction (accessibility)"), Helper::isAllowed(document, information, Permission::Accessibility) });
    permissions.push_back({ tr("Page assembling"), Helper::isAllowed(document, information, Permission::Assemble) });
    permissions.push_back({ tr("Modify content"), Helper::isAllowed(document, information, Permission::Modify) });
    permissions.push_back({ tr("Modify interactive items"), Helper::isAllowed(document, information, Permission::ModifyInteractiveItems) });
    permissions.push_back({ tr("Fill form fields"), Helper::isAllowed(document, information, Permission::ModifyFormFields) });
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
    // R7-5: the one implementation of the core - decoded with the limit (Q7.18), a bomb is never
    // decoded whole (it was decoded first and measured after)
    static_assert(MAXIMUM_METADATA_SIZE == PDFDocumentReport::MAXIMUM_METADATA_SIZE);
    return PDFDocumentReport::getMetadata(document, hasMetadata, errorMessage);
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
    if (!document || (cancel && cancel->load()))
    {
        return fonts;
    }

    // K7.24, R7-5: the one walk of the core over the resources of all pages (forms, appearance streams,
    // patterns, Type 3 fonts, soft masks; one visited set, a depth limit) - Document Report and PdfTool
    // info-fonts read the same; the page shows the name without the tag of a subset
    std::vector<PDFInteger> pageIndices(document->getCatalog()->getPageCount(), 0);
    std::iota(pageIndices.begin(), pageIndices.end(), 0);
    const PDFAppDocumentFontScanControl control(cancel);
    for (const PDFDocumentReportFont& font : PDFDocumentReport::getFonts(document, pageIndices, false, &control))
    {
        if (int(fonts.size()) >= MAXIMUM_FONTS)
        {
            break;
        }

        PDFAppFontInfo info;
        info.name = font.isSubset ? font.name.mid(7) : font.name;
        info.family = font.family;
        info.type = PDFAppDocumentPropertiesData::getFontTypeText(int(font.fontType));
        info.isEmbedded = font.isEmbedded;
        info.isSubset = font.isSubset;
        fonts.push_back(std::move(info));
    }
    if (control.isOperationCanceled())
    {
        return std::vector<PDFAppFontInfo>();
    }

    // Each font once (direct font dictionaries of more pages are the same font)
    std::sort(fonts.begin(), fonts.end(), &Helper::isFontBefore);
    fonts.erase(std::unique(fonts.begin(), fonts.end(), &Helper::isSameFont), fonts.end());
    return fonts;
}

}   // namespace pdf
