//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftoolinfo.h"
#include "pdfform.h"
#include "pdfjavascriptscanner.h"

#include <QPageSize>
#include <QCryptographicHash>

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

    QLocale locale;

    const pdf::PDFDocumentInfo* info = document.getInfo();
    const pdf::PDFCatalog* catalog = document.getCatalog();

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

    writeProperty("title", PDFToolTranslationContext::tr("Title"), info->title);
    writeProperty("subject", PDFToolTranslationContext::tr("Subject"), info->subject);
    writeProperty("keywords", PDFToolTranslationContext::tr("Keywords"), info->keywords);
    writeProperty("author", PDFToolTranslationContext::tr("Author"), info->author);
    writeProperty("creator", PDFToolTranslationContext::tr("Creator"), info->creator);
    writeProperty("producer", PDFToolTranslationContext::tr("Producer"), info->producer);
    writeProperty("creation-date", PDFToolTranslationContext::tr("Creation date"), convertDateTimeToString(info->creationDate.toLocalTime(), options.outputDateFormat));
    writeProperty("modified-date", PDFToolTranslationContext::tr("Modified date"), convertDateTimeToString(info->modifiedDate.toLocalTime(), options.outputDateFormat));
    writeProperty("version", PDFToolTranslationContext::tr("Version"), QString::fromLatin1(document.getVersion()));

    QString trapped;
    switch (info->trapped)
    {
        case pdf::PDFDocumentInfo::Trapped::True:
            trapped = PDFToolTranslationContext::tr("Yes");
            break;

        case pdf::PDFDocumentInfo::Trapped::False:
            trapped = PDFToolTranslationContext::tr("No");
            break;

        case pdf::PDFDocumentInfo::Trapped::Unknown:
            trapped = PDFToolTranslationContext::tr("Unknown");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    writeProperty("tagged", PDFToolTranslationContext::tr("Tagged"), trapped);

    QString formType;
    pdf::PDFForm form = pdf::PDFForm::parse(&document, catalog->getFormObject());
    switch (form.getFormType())
    {
        case pdf::PDFForm::FormType::None:
            formType = PDFToolTranslationContext::tr("None");
            break;

        case pdf::PDFForm::FormType::AcroForm:
            formType = PDFToolTranslationContext::tr("AcroForm");
            break;

        case pdf::PDFForm::FormType::XFAForm:
            formType = PDFToolTranslationContext::tr("XFA");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    writeProperty("form-type", PDFToolTranslationContext::tr("Form type"), formType);

    const pdf::PDFInteger pageCount = catalog->getPageCount();
    writeProperty("page-count", PDFToolTranslationContext::tr("Page count"), locale.toString(pageCount));
    if (pageCount > 0)
    {
        const pdf::PDFPage* firstPage = catalog->getPage(0);
        QSizeF pageSizeMM = firstPage->getRectMM(firstPage->getRotatedMediaBox()).size();
        QPageSize pageSize(pageSizeMM, QPageSize::Millimeter, QString(), QPageSize::FuzzyOrientationMatch);
        QString paperSizeString = QString("%1 x %2 mm").arg(locale.toString(pageSizeMM.width()), locale.toString(pageSizeMM.height()));

        writeProperty("paper-format", PDFToolTranslationContext::tr("Paper format"), pageSize.name());
        writeProperty("paper-size", PDFToolTranslationContext::tr("Paper size"), paperSizeString);
    }

    if (!info->extra.empty())
    {
        for (const auto& item : info->extra)
        {
            QString key = QString::fromLatin1(item.first);
            QVariant valueVariant = item.second;
            QString value = (valueVariant.type() == QVariant::DateTime) ? convertDateTimeToString(valueVariant.toDateTime().toLocalTime(), options.outputDateFormat) : valueVariant.toString();
            writeProperty("custom-property", key, value);
        }
    }

    writeProperty("file-name", PDFToolTranslationContext::tr("File name"), options.document);
    writeProperty("file-size", PDFToolTranslationContext::tr("File size"), locale.toString(sourceData.size()));

    pdf::PDFJavaScriptScanner scanner(&document);
    writeProperty("javascript", PDFToolTranslationContext::tr("JavaScript"), scanner.hasJavaScript() ? PDFToolTranslationContext::tr("Yes") : PDFToolTranslationContext::tr("No"));

    const pdf::PDFSecurityHandler* securityHandler = document.getStorage().getSecurityHandler();
    const pdf::EncryptionMode mode = securityHandler->getMode();
    QString modeString;
    switch (mode)
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
    switch (securityHandler->getAuthorizationResult())
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
    if (securityHandler->getAuthorizationResult() != pdf::PDFSecurityHandler::AuthorizationResult::NoAuthorizationRequired)
    {
        writeProperty("metadata-encrypted", PDFToolTranslationContext::tr("Metadata encrypted"), securityHandler->isMetadataEncrypted() ? PDFToolTranslationContext::tr("Yes") : PDFToolTranslationContext::tr("No"));
        writeProperty("version", PDFToolTranslationContext::tr("Version"), locale.toString(securityHandler->getVersion()));

        if (securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::PrintLowResolution))
        {
            permissions << PDFToolTranslationContext::tr("Print (low resolution)");
        }
        if (securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::PrintHighResolution))
        {
            permissions << PDFToolTranslationContext::tr("Print");
        }
        if (securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::CopyContent))
        {
            permissions << PDFToolTranslationContext::tr("Copy content");
        }
        if (securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::Accessibility))
        {
            permissions << PDFToolTranslationContext::tr("Accessibility");
        }
        if (securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::Assemble))
        {
            permissions << PDFToolTranslationContext::tr("Page assembling");
        }
        if (securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::Modify))
        {
            permissions << PDFToolTranslationContext::tr("Modify content");
        }
        if (securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::ModifyInteractiveItems))
        {
            permissions << PDFToolTranslationContext::tr("Modify interactive items");
        }
        if (securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::ModifyFormFields))
        {
            permissions << PDFToolTranslationContext::tr("Form filling");
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

        auto writeHash = [&formatter, &sourceData](QCryptographicHash::Algorithm algorithm, const QString& algorithmName, const QString& algorithmDescription)
        {
            formatter.beginTableRow(algorithmName);
            formatter.writeTableColumn("algorithm", algorithmDescription);
            formatter.writeTableColumn("hash", QString::fromLatin1(QCryptographicHash::hash(sourceData, algorithm).toHex()).toUpper());
            formatter.endTableRow();
        };

        writeHash(QCryptographicHash::Md5, "MD5", PDFToolTranslationContext::tr("MD5"));
        writeHash(QCryptographicHash::Sha1, "SHA1", PDFToolTranslationContext::tr("SHA1"));
        writeHash(QCryptographicHash::Sha256, "SHA256", PDFToolTranslationContext::tr("SHA256"));
        writeHash(QCryptographicHash::Sha384, "SHA384", PDFToolTranslationContext::tr("SHA384"));
        writeHash(QCryptographicHash::Sha512, "SHA512", PDFToolTranslationContext::tr("SHA512"));

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
