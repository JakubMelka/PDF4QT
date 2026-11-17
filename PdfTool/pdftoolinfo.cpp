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

#include "pdftoolinfo.h"
#include "pdfdocumentreport.h"
#include "pdfjavascriptscanner.h"

namespace pdftool
{

static PDFToolInfoApplication s_infoApplication;

QString PDFToolInfoApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "info";

        case Name:
            return PDFToolTranslationContext::tr("Info");

        case Description:
            return PDFToolTranslationContext::tr("Retrieve basic informations about a document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolInfoApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    // The data of the one implementation of Document Report, Properties and MCP (R7-5); the numbers
    // in the C locale (Q7.14, R7-14)
    const QLocale locale = QLocale::c();
    const pdf::PDFDocumentReportBasicInformation information = pdf::PDFDocumentReport::getBasicInformation(&document);

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("info", PDFToolTranslationContext::tr("Information about document %1").arg(options.document));
    formatter.endl();

    formatter.beginTable("properties", PDFToolTranslationContext::tr("Properties:"));

    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("property", PDFToolTranslationContext::tr("Property"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("value", PDFToolTranslationContext::tr("Value"), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    auto writeProperty = [&formatter](const QString& propertyName, const QString& property, const QString& value)
    {
        formatter.beginTableRow(propertyName);
        formatter.writeTableColumn("property", property);
        formatter.writeTableColumn("value", value);
        formatter.endTableRow();
    };

    auto getText = [&information](const char* key)
    {
        const pdf::PDFDocumentReportBasicInformation::InfoEntry* entry = information.findInfo(QByteArray(key));
        return entry ? entry->value : QString();
    };

    auto getDate = [&information, &options](const char* key)
    {
        const pdf::PDFDocumentReportBasicInformation::InfoEntry* entry = information.findInfo(QByteArray(key));
        return entry && entry->dateTime.isValid() ? convertDateTimeToString(entry->dateTime.toLocalTime(), options.outputDateFormat) : QString();
    };

    writeProperty("title", PDFToolTranslationContext::tr("Title"), getText("Title"));
    writeProperty("subject", PDFToolTranslationContext::tr("Subject"), getText("Subject"));
    writeProperty("keywords", PDFToolTranslationContext::tr("Keywords"), getText("Keywords"));
    writeProperty("author", PDFToolTranslationContext::tr("Author"), getText("Author"));
    writeProperty("creator", PDFToolTranslationContext::tr("Creator"), getText("Creator"));
    writeProperty("producer", PDFToolTranslationContext::tr("Producer"), getText("Producer"));
    writeProperty("creation-date", PDFToolTranslationContext::tr("Creation date"), getDate("CreationDate"));
    writeProperty("modified-date", PDFToolTranslationContext::tr("Modified date"), getDate("ModDate"));
    writeProperty("version", PDFToolTranslationContext::tr("Version"), information.version);

    // B176 / B204: /Trapped is Trapped (14.11.6, Table 349: absent = Unknown), Tagged is /MarkInfo /Marked (14.7.1)
    QString trapped = PDFToolTranslationContext::tr("Unknown");
    if (information.trapped == "True")
    {
        trapped = PDFToolTranslationContext::tr("Yes");
    }
    else if (information.trapped == "False")
    {
        trapped = PDFToolTranslationContext::tr("No");
    }
    writeProperty("trapped", PDFToolTranslationContext::tr("Trapped"), trapped);
    writeProperty("tagged", PDFToolTranslationContext::tr("Tagged"), information.isTagged ? PDFToolTranslationContext::tr("Yes") : PDFToolTranslationContext::tr("No"));

    QString formType = PDFToolTranslationContext::tr("None");
    if (information.formType == QLatin1String("AcroForm"))
    {
        formType = PDFToolTranslationContext::tr("AcroForm");
    }
    else if (information.formType == QLatin1String("XFA"))
    {
        formType = PDFToolTranslationContext::tr("XFA");
    }
    writeProperty("form-type", PDFToolTranslationContext::tr("Form type"), formType);

    writeProperty("page-count", PDFToolTranslationContext::tr("Page count"), locale.toString(information.pageCount));
    if (information.pageCount > 0)
    {
        const QSizeF pageSizeMM = information.firstPageSizeMM;
        const QString paperSizeString = QString("%1 x %2 mm").arg(locale.toString(pageSizeMM.width()), locale.toString(pageSizeMM.height()));
        writeProperty("paper-format", PDFToolTranslationContext::tr("Paper format"), !information.paperFormat.isEmpty() ? information.paperFormat : PDFToolTranslationContext::tr("Custom"));
        writeProperty("paper-size", PDFToolTranslationContext::tr("Paper size"), paperSizeString);
    }

    for (const pdf::PDFDocumentReportBasicInformation::InfoEntry& entry : information.info)
    {
        if (entry.isCustom)
        {
            const QString value = entry.dateTime.isValid() ? convertDateTimeToString(entry.dateTime.toLocalTime(), options.outputDateFormat) : entry.value;
            writeProperty("custom-property", pdf::PDFDocumentReport::sanitize(QString::fromLatin1(entry.key)), value);
        }
    }

    writeProperty("file-name", PDFToolTranslationContext::tr("File name"), options.document);
    writeProperty("file-size", PDFToolTranslationContext::tr("File size"), locale.toString(sourceData.size()));

    pdf::PDFJavaScriptScanner scanner(&document);
    writeProperty("javascript", PDFToolTranslationContext::tr("JavaScript"), scanner.hasJavaScript() ? PDFToolTranslationContext::tr("Yes") : PDFToolTranslationContext::tr("No"));

    QString modeString;
    switch (information.encryptionMode)
    {
        case pdf::EncryptionMode::None:
            modeString = PDFToolTranslationContext::tr("None");
            break;

        case pdf::EncryptionMode::Standard:
            modeString = PDFToolTranslationContext::tr("Standard");
            break;

        case pdf::EncryptionMode::PublicKey:
            modeString = PDFToolTranslationContext::tr("Public Key");
            break;

        case pdf::EncryptionMode::Custom:
            modeString = PDFToolTranslationContext::tr("Custom");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    QString authorizationMode;
    switch (information.authorization)
    {
        case pdf::PDFSecurityHandler::AuthorizationResult::NoAuthorizationRequired:
            authorizationMode = PDFToolTranslationContext::tr("No authorization required");
            break;

        case pdf::PDFSecurityHandler::AuthorizationResult::OwnerAuthorized:
            authorizationMode = PDFToolTranslationContext::tr("Authorized as owner");
            break;

        case pdf::PDFSecurityHandler::AuthorizationResult::UserAuthorized:
            authorizationMode = PDFToolTranslationContext::tr("Authorized as user");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    writeProperty("encryption", PDFToolTranslationContext::tr("Encryption"), modeString);
    writeProperty("authorized-as", PDFToolTranslationContext::tr("Authorization"), authorizationMode);

    QStringList permissions;
    if (information.authorization != pdf::PDFSecurityHandler::AuthorizationResult::NoAuthorizationRequired)
    {
        writeProperty("metadata-encrypted", PDFToolTranslationContext::tr("Metadata encrypted"), information.isMetadataEncrypted ? PDFToolTranslationContext::tr("Yes") : PDFToolTranslationContext::tr("No"));

        // B176: the revision of the security handler (/R, Table 21) under its own key, not a second "version"
        writeProperty("encryption-revision", PDFToolTranslationContext::tr("Encryption revision"), locale.toString(information.encryptionRevision));

        for (const auto& permission : information.permissions)
        {
            if (!permission.second)
            {
                continue;
            }

            switch (permission.first)
            {
                case pdf::PDFSecurityHandler::Permission::PrintLowResolution:
                    permissions << PDFToolTranslationContext::tr("Print (low resolution)");
                    break;
                case pdf::PDFSecurityHandler::Permission::PrintHighResolution:
                    permissions << PDFToolTranslationContext::tr("Print");
                    break;
                case pdf::PDFSecurityHandler::Permission::CopyContent:
                    permissions << PDFToolTranslationContext::tr("Copy content");
                    break;
                case pdf::PDFSecurityHandler::Permission::Accessibility:
                    permissions << PDFToolTranslationContext::tr("Accessibility");
                    break;
                case pdf::PDFSecurityHandler::Permission::Assemble:
                    permissions << PDFToolTranslationContext::tr("Page assembling");
                    break;
                case pdf::PDFSecurityHandler::Permission::Modify:
                    permissions << PDFToolTranslationContext::tr("Modify content");
                    break;
                case pdf::PDFSecurityHandler::Permission::ModifyInteractiveItems:
                    permissions << PDFToolTranslationContext::tr("Modify interactive items");
                    break;
                case pdf::PDFSecurityHandler::Permission::ModifyFormFields:
                    permissions << PDFToolTranslationContext::tr("Form filling");
                    break;
            }
        }
    }
    else
    {
        permissions << PDFToolTranslationContext::tr("All");
    }

    writeProperty("permissions", PDFToolTranslationContext::tr("Permissions"), permissions.join(", "));

    formatter.endTable();

    if (options.computeHashes)
    {
        formatter.endl();

        formatter.beginTable("hashes", PDFToolTranslationContext::tr("File hashes:"));

        formatter.beginTableHeaderRow("header");
        formatter.writeTableHeaderColumn("algorithm", PDFToolTranslationContext::tr("Algorithm"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("hash", PDFToolTranslationContext::tr("Hash"), Qt::AlignLeft);
        formatter.endTableHeaderRow();

        for (const auto& hash : pdf::PDFDocumentReport::getFileHashes(sourceData))
        {
            formatter.beginTableRow(hash.first);
            formatter.writeTableColumn("algorithm", hash.first);
            formatter.writeTableColumn("hash", hash.second);
            formatter.endTableRow();
        }

        formatter.endTable();
    }

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolInfoApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | DateFormat | ComputeHashes;
}

}   // namespace pdftool
