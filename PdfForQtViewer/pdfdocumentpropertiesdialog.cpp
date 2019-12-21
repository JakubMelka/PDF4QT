//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfdocumentpropertiesdialog.h"
#include "ui_pdfdocumentpropertiesdialog.h"

#include "pdfdocument.h"
#include "pdfwidgetutils.h"

#include <QLocale>
#include <QPageSize>

namespace pdfviewer
{

PDFDocumentPropertiesDialog::PDFDocumentPropertiesDialog(const pdf::PDFDocument* document,
                                                         const PDFFileInfo* fileInfo,
                                                         QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFDocumentPropertiesDialog)
{
    ui->setupUi(this);

    initializeProperties(document);
    initializeFileInfoProperties(fileInfo);
    initializeSecurity(document);

    const int defaultWidth = PDFWidgetUtils::getPixelSize(this, 240.0);
    const int defaultHeight = PDFWidgetUtils::getPixelSize(this, 200.0);
    resize(defaultWidth, defaultHeight);
}

PDFDocumentPropertiesDialog::~PDFDocumentPropertiesDialog()
{
    delete ui;
}

void PDFDocumentPropertiesDialog::initializeProperties(const pdf::PDFDocument* document)
{
    QLocale locale;

    // Initialize document properties
    QTreeWidgetItem* propertiesRoot = new QTreeWidgetItem({ tr("Properties") });

    const pdf::PDFDocument::Info* info = document->getInfo();
    const pdf::PDFCatalog* catalog = document->getCatalog();
    new QTreeWidgetItem(propertiesRoot, { tr("PDF version"), QString::fromLatin1(document->getVersion()) });
    new QTreeWidgetItem(propertiesRoot, { tr("Title"), info->title });
    new QTreeWidgetItem(propertiesRoot, { tr("Subject"), info->subject });
    new QTreeWidgetItem(propertiesRoot, { tr("Author"), info->author });
    new QTreeWidgetItem(propertiesRoot, { tr("Keywords"), info->keywords });
    new QTreeWidgetItem(propertiesRoot, { tr("Creator"), info->creator });
    new QTreeWidgetItem(propertiesRoot, { tr("Producer"), info->producer });
    new QTreeWidgetItem(propertiesRoot, { tr("Creation date"), locale.toString(info->creationDate) });
    new QTreeWidgetItem(propertiesRoot, { tr("Modified date"), locale.toString(info->modifiedDate) });

    QString trapped;
    switch (info->trapped)
    {
        case pdf::PDFDocument::Info::Trapped::True:
            trapped = tr("True");
            break;

        case pdf::PDFDocument::Info::Trapped::False:
            trapped = tr("False");
            break;

        case pdf::PDFDocument::Info::Trapped::Unknown:
            trapped = tr("Unknown");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    QTreeWidgetItem* contentRoot = new QTreeWidgetItem({ tr("Content") });
    const pdf::PDFInteger pageCount = catalog->getPageCount();
    new QTreeWidgetItem(contentRoot, { tr("Page count"), locale.toString(pageCount) });

    if (pageCount > 0)
    {
        const pdf::PDFPage* firstPage = catalog->getPage(0);
        QSizeF pageSizeMM = firstPage->getRectMM(firstPage->getRotatedMediaBox()).size();
        QPageSize pageSize(pageSizeMM, QPageSize::Millimeter, QString(), QPageSize::FuzzyOrientationMatch);
        QString paperSizeString = QString("%1 x %2 mm").arg(locale.toString(pageSizeMM.width()), locale.toString(pageSizeMM.height()));

        new QTreeWidgetItem(contentRoot, { tr("Paper format"), pageSize.name() });
        new QTreeWidgetItem(contentRoot, { tr("Paper size"), paperSizeString });
    }
    new QTreeWidgetItem(contentRoot, { tr("Trapped"), trapped });

    ui->propertiesTreeWidget->addTopLevelItem(propertiesRoot);
    ui->propertiesTreeWidget->addTopLevelItem(contentRoot);

    if (!info->extra.empty())
    {
        QTreeWidgetItem* customRoot = new QTreeWidgetItem({ tr("Custom properties") });
        for (const auto& item : info->extra)
        {
            QString key = QString::fromLatin1(item.first);
            QVariant valueVariant = item.second;
            QString value = (valueVariant.type() == QVariant::DateTime) ? locale.toString(valueVariant.toDateTime()) : valueVariant.toString();
            new QTreeWidgetItem(customRoot, { key, value });
        }
        ui->propertiesTreeWidget->addTopLevelItem(customRoot);
    }

    ui->propertiesTreeWidget->expandAll();
    ui->propertiesTreeWidget->resizeColumnToContents(0);
}

void PDFDocumentPropertiesDialog::initializeFileInfoProperties(const PDFFileInfo* fileInfo)
{
    QLocale locale;

    // Initialize document file info
    QTreeWidgetItem* fileInfoRoot = new QTreeWidgetItem({ tr("File information") });

    new QTreeWidgetItem(fileInfoRoot, { tr("Name"), fileInfo->fileName });
    new QTreeWidgetItem(fileInfoRoot, { tr("Directory"), fileInfo->path });
    new QTreeWidgetItem(fileInfoRoot, { tr("Writable"), fileInfo->writable ? tr("Yes") : tr("No") });

    QString fileSize;
    if (fileInfo->fileSize > 1024 * 1024)
    {
        fileSize = QString("%1 MB (%2 bytes)").arg(locale.toString(fileInfo->fileSize / (1024.0 * 1024.0)), locale.toString(fileInfo->fileSize));
    }
    else
    {
        fileSize = QString("%1 kB (%2 bytes)").arg(locale.toString(fileInfo->fileSize / 1024.0), locale.toString(fileInfo->fileSize));
    }

    new QTreeWidgetItem(fileInfoRoot, { tr("Size"), fileSize });
    new QTreeWidgetItem(fileInfoRoot, { tr("Created date"), locale.toString(fileInfo->creationTime) });
    new QTreeWidgetItem(fileInfoRoot, { tr("Modified date"), locale.toString(fileInfo->lastModifiedTime) });
    new QTreeWidgetItem(fileInfoRoot, { tr("Last read date"), locale.toString(fileInfo->lastReadTime) });

    ui->fileInfoTreeWidget->addTopLevelItem(fileInfoRoot);
    ui->fileInfoTreeWidget->expandAll();
    ui->fileInfoTreeWidget->resizeColumnToContents(0);
}

void PDFDocumentPropertiesDialog::initializeSecurity(const pdf::PDFDocument* document)
{
    QLocale locale;

    QTreeWidgetItem* securityRoot = new QTreeWidgetItem({ tr("Security") });
    const pdf::PDFSecurityHandler* securityHandler = document->getStorage().getSecurityHandler();
    const pdf::EncryptionMode mode = securityHandler->getMode();
    QString modeString;
    switch (mode)
    {
        case pdf::EncryptionMode::None:
            modeString = tr("None");
            break;

        case pdf::EncryptionMode::Standard:
            modeString = tr("Standard");
            break;

        case pdf::EncryptionMode::Custom:
            modeString = tr("Custom");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    QString authorizationMode;
    switch (securityHandler->getAuthorizationResult())
    {
        case pdf::PDFSecurityHandler::AuthorizationResult::NoAuthorizationRequired:
            authorizationMode = tr("No authorization required");
            break;

        case pdf::PDFSecurityHandler::AuthorizationResult::OwnerAuthorized:
            authorizationMode = tr("Authorized as owner");
            break;

        case pdf::PDFSecurityHandler::AuthorizationResult::UserAuthorized:
            authorizationMode = tr("Authorized as user");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    new QTreeWidgetItem(securityRoot, { tr("Document encryption"), modeString });
    new QTreeWidgetItem(securityRoot, { tr("Authorized as"), authorizationMode });
    new QTreeWidgetItem(securityRoot, { tr("Metadata encrypted"), securityHandler->isMetadataEncrypted() ? tr("Yes") : tr("No") });
    new QTreeWidgetItem(securityRoot, { tr("Version"), locale.toString(securityHandler->getVersion()) });

    QTreeWidgetItem* permissionsRoot = new QTreeWidgetItem({ tr("Permissions") });

    auto addPermissionInfo = [securityHandler, permissionsRoot](QString caption, pdf::PDFSecurityHandler::Permission permission)
    {
        new QTreeWidgetItem(permissionsRoot, { caption, securityHandler->isAllowed(permission) ? tr("Yes") : tr("No")});
    };
    addPermissionInfo(tr("Print (low resolution)"), pdf::PDFSecurityHandler::Permission::PrintLowResolution);
    addPermissionInfo(tr("Print (high resolution)"), pdf::PDFSecurityHandler::Permission::PrintHighResolution);
    addPermissionInfo(tr("Content extraction"), pdf::PDFSecurityHandler::Permission::CopyContent);
    addPermissionInfo(tr("Content extraction (accessibility)"), pdf::PDFSecurityHandler::Permission::Accessibility);
    addPermissionInfo(tr("Page assembling"), pdf::PDFSecurityHandler::Permission::Assemble);
    addPermissionInfo(tr("Modify content"), pdf::PDFSecurityHandler::Permission::Modify);
    addPermissionInfo(tr("Modify interactive items"), pdf::PDFSecurityHandler::Permission::ModifyInteractiveItems);
    addPermissionInfo(tr("Fill form fields"), pdf::PDFSecurityHandler::Permission::ModifyFormFields);

    ui->securityTreeWidget->addTopLevelItem(securityRoot);
    ui->securityTreeWidget->addTopLevelItem(permissionsRoot);
    ui->securityTreeWidget->expandAll();
    ui->securityTreeWidget->resizeColumnToContents(0);
}

}   // namespace pdfviewer
