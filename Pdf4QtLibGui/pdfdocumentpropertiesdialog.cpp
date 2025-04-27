// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pdfdocumentpropertiesdialog.h"
#include "ui_pdfdocumentpropertiesdialog.h"

#include "pdfdocument.h"
#include "pdfwidgetutils.h"
#include "pdffont.h"
#include "pdfutils.h"
#include "pdfexception.h"
#include "pdfexecutionpolicy.h"

#include <QLocale>
#include <QPageSize>
#include <QtConcurrent/QtConcurrent>

#include "pdfdbgheap.h"

#include <stack>
#include <execution>

namespace pdfviewer
{

class PDFTreeFactory : public pdf::ITreeFactory
{
public:
    PDFTreeFactory(QTreeWidgetItem* root)
    {
        m_roots.push(root);
    }

    // ITreeFactory interface
    virtual void pushItem(QStringList texts) override;
    virtual void addItem(QStringList texts) override;
    virtual void popItem() override;

private:
    std::stack<QTreeWidgetItem*> m_roots;
};

void PDFTreeFactory::pushItem(QStringList texts)
{
    Q_ASSERT(!m_roots.empty());
    m_roots.push(new QTreeWidgetItem(m_roots.top(), texts));
}

void PDFTreeFactory::addItem(QStringList texts)
{
    Q_ASSERT(!m_roots.empty());
    new QTreeWidgetItem(m_roots.top(), texts);
}

void PDFTreeFactory::popItem()
{
    Q_ASSERT(!m_roots.empty());
    m_roots.pop();
}

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
    initializeFonts(document);
    initializeDisplayAndPrintSettings(document);

    const int minimumSectionSize = pdf::PDFWidgetUtils::scaleDPI_x(this, 300);
    for (QTreeWidget* widget : findChildren<QTreeWidget*>(QString(), Qt::FindChildrenRecursively))
    {
        widget->header()->setMinimumSectionSize(minimumSectionSize);
    }

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(750, 600));
    pdf::PDFWidgetUtils::style(this);
}

PDFDocumentPropertiesDialog::~PDFDocumentPropertiesDialog()
{
    Q_ASSERT(m_fontTreeWidgetItems.empty());
    delete ui;
}

void PDFDocumentPropertiesDialog::initializeProperties(const pdf::PDFDocument* document)
{
    QLocale locale;

    // Initialize document properties
    QTreeWidgetItem* propertiesRoot = new QTreeWidgetItem({ tr("Properties") });

    const pdf::PDFDocumentInfo* info = document->getInfo();
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
        case pdf::PDFDocumentInfo::Trapped::True:
            trapped = tr("Yes");
            break;

        case pdf::PDFDocumentInfo::Trapped::False:
            trapped = tr("No");
            break;

        case pdf::PDFDocumentInfo::Trapped::Unknown:
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
            QString value = (valueVariant.typeId() == QMetaType::QDateTime) ? locale.toString(valueVariant.toDateTime()) : valueVariant.toString();
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

        case pdf::EncryptionMode::PublicKey:
            modeString = tr("Public Key");
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

    if (securityHandler->getAuthorizationResult() != pdf::PDFSecurityHandler::AuthorizationResult::NoAuthorizationRequired)
    {
        new QTreeWidgetItem(securityRoot, { tr("Metadata encrypted"), securityHandler->isMetadataEncrypted() ? tr("Yes") : tr("No") });
        new QTreeWidgetItem(securityRoot, { tr("Version"), locale.toString(securityHandler->getVersion()) });
    }

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

void PDFDocumentPropertiesDialog::initializeFonts(const pdf::PDFDocument* document)
{
    auto createFontInfo = [this, document]()
    {
        pdf::PDFInteger pageCount = document->getCatalog()->getPageCount();

        QMutex fontTreeItemMutex;
        QMutex usedFontReferencesMutex;
        std::set<pdf::PDFObjectReference> usedFontReferences;

        auto processPage = [&](pdf::PDFInteger pageIndex)
        {
            try
            {
                const pdf::PDFPage* page = document->getCatalog()->getPage(pageIndex);
                if (const pdf::PDFDictionary* resourcesDictionary = document->getDictionaryFromObject(page->getResources()))
                {
                    if (const pdf::PDFDictionary* fontsDictionary = document->getDictionaryFromObject(resourcesDictionary->get("Font")))
                    {
                        // Iterate trough each font
                        const size_t fontsCount = fontsDictionary->getCount();
                        for (size_t i = 0; i < fontsCount; ++i)
                        {
                            pdf::PDFObject object = fontsDictionary->getValue(i);
                            if (object.isReference())
                            {
                                // Check, if we have not processed the object. If we have it processed,
                                // then do nothing, otherwise insert it into the processed objects.
                                // We must also use mutex, because we use multithreading.
                                QMutexLocker lock(&usedFontReferencesMutex);
                                if (usedFontReferences.count(object.getReference()))
                                {
                                    continue;
                                }
                                else
                                {
                                    usedFontReferences.insert(object.getReference());
                                }
                            }

                            try
                            {
                                if (pdf::PDFFontPointer font = pdf::PDFFont::createFont(object, fontsDictionary->getKey(i).getString(), document))
                                {
                                    pdf::PDFRenderErrorReporterDummy dummyReporter;
                                    pdf::PDFRealizedFontPointer realizedFont = pdf::PDFRealizedFont::createRealizedFont(font, 8.0, &dummyReporter);
                                    if (realizedFont)
                                    {
                                        const pdf::FontType fontType = font->getFontType();
                                        const pdf::FontDescriptor* fontDescriptor = font->getFontDescriptor();
                                        QString fontName = fontDescriptor->fontName;

                                        // Try to remove characters from +, if we have font name 'SDFDSF+ValidFontName'
                                        int plusPos = fontName.lastIndexOf('+');
                                        if (plusPos != -1 && plusPos < fontName.size() - 1)
                                        {
                                            fontName = fontName.mid(plusPos + 1);
                                        }

                                        if (fontName.isEmpty())
                                        {
                                            fontName = QString::fromLatin1(fontsDictionary->getKey(i).getString());
                                        }

                                        std::unique_ptr<QTreeWidgetItem> fontRootItemPtr = std::make_unique<QTreeWidgetItem>(QStringList({ fontName }));
                                        QTreeWidgetItem* fontRootItem = fontRootItemPtr.get();

                                        QString fontTypeString;
                                        switch (fontType)
                                        {
                                            case pdf::FontType::TrueType:
                                                fontTypeString = tr("TrueType");
                                                break;

                                            case pdf::FontType::Type0:
                                                fontTypeString = tr("Type0 (CID keyed)");
                                                break;

                                            case pdf::FontType::Type1:
                                                fontTypeString = tr("Type1 (8 bit keyed)");
                                                break;

                                            case pdf::FontType::MMType1:
                                                fontTypeString = tr("MMType1 (8 bit keyed)");
                                                break;

                                            case pdf::FontType::Type3:
                                                fontTypeString = tr("Type3 (content streams for font glyphs)");
                                                break;

                                            default:
                                                Q_ASSERT(false);
                                                break;
                                        }

                                        new QTreeWidgetItem(fontRootItem, { tr("Type"), fontTypeString });
                                        if (!fontDescriptor->fontFamily.isEmpty())
                                        {
                                            new QTreeWidgetItem(fontRootItem, { tr("Font family"), fontDescriptor->fontFamily });
                                        }

                                        new QTreeWidgetItem(fontRootItem, { tr("Embedded subset"), fontDescriptor->getEmbeddedFontData() ? tr("Yes") : tr("No") });

                                        PDFTreeFactory treeFactory1(fontRootItem);
                                        font->dumpFontToTreeItem(&treeFactory1);

                                        PDFTreeFactory treeFactory2(fontRootItem);
                                        realizedFont->dumpFontToTreeItem(&treeFactory2);

                                        // Separator item
                                        new QTreeWidgetItem(fontRootItem, QStringList());

                                        // Finally add the tree item
                                        QMutexLocker lock(&fontTreeItemMutex);
                                        m_fontTreeWidgetItems.push_back(fontRootItemPtr.release());
                                    }
                                }
                            }
                            catch (const pdf::PDFException &)
                            {
                                // Do nothing, some error occured, continue with next font
                                continue;
                            }
                        }
                    }
                }
            }
            catch (const pdf::PDFException &)
            {
                // Do nothing, some error occured
            }
        };

        pdf::PDFIntegerRange<pdf::PDFInteger> indices(pdf::PDFInteger(0), pageCount);
        pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Page, indices.begin(), indices.end(), processPage);
    };
    m_future = QtConcurrent::run(createFontInfo);
    connect(&m_futureWatcher, &QFutureWatcher<void>::finished, this, &PDFDocumentPropertiesDialog::onFontsFinished);
    m_futureWatcher.setFuture(m_future);
}

void PDFDocumentPropertiesDialog::initializeDisplayAndPrintSettings(const pdf::PDFDocument* document)
{
    const pdf::PDFCatalog* catalog = document->getCatalog();
    pdf::PageLayout pageLayout = catalog->getPageLayout();
    pdf::PageMode pageMode = catalog->getPageMode();
    const pdf::PDFViewerPreferences* viewerPreferences = catalog->getViewerPreferences();

    QTreeWidgetItem* viewerSettingsRoot = new QTreeWidgetItem({ tr("Viewer settings") });
    QTreeWidgetItem* printerSettingsRoot = new QTreeWidgetItem({ tr("Default printer settings") });

    QString pageLayoutString;
    switch (pageLayout)
    {
        case pdf::PageLayout::SinglePage:
            pageLayoutString = tr("Single page");
            break;

        case pdf::PageLayout::OneColumn:
            pageLayoutString = tr("Continuous column");
            break;

        case pdf::PageLayout::TwoColumnLeft:
        case pdf::PageLayout::TwoColumnRight:
            pageLayoutString = tr("Two continuous columns");
            break;

        case pdf::PageLayout::TwoPagesLeft:
        case pdf::PageLayout::TwoPagesRight:
            pageLayoutString = tr("Two pages");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    QString pageModeString;
    switch (pageMode)
    {
        case pdf::PageMode::UseNone:
            pageModeString = tr("Default");
            break;

        case pdf::PageMode::UseOutlines:
            pageModeString = tr("Show outlines");
            break;

        case pdf::PageMode::UseThumbnails:
            pageModeString = tr("Show thumbnails");
            break;

        case pdf::PageMode::Fullscreen:
            pageModeString = tr("Fullscreen");
            break;

        case pdf::PageMode::UseOptionalContent:
            pageModeString = tr("Show optional content");
            break;

        case pdf::PageMode::UseAttachments:
            pageModeString = tr("Show attachments");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    QString directionString;
    switch (viewerPreferences->getDirection())
    {
        case pdf::PDFViewerPreferences::Direction::LeftToRight:
            directionString = tr("Left to right");
            break;

        case pdf::PDFViewerPreferences::Direction::RightToLeft:
            directionString = tr("Right to left");
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    new QTreeWidgetItem(viewerSettingsRoot, { tr("Page layout"), pageLayoutString });
    new QTreeWidgetItem(viewerSettingsRoot, { tr("View mode"), pageModeString });
    new QTreeWidgetItem(viewerSettingsRoot, { tr("Writing direction"), directionString });

    QString printScalingString;
    switch (viewerPreferences->getPrintScaling())
    {
        case pdf::PDFViewerPreferences::PrintScaling::None:
            printScalingString = tr("None");
            break;

        case pdf::PDFViewerPreferences::PrintScaling::AppDefault:
            printScalingString = tr("Application default");
            break;

        default:
            Q_ASSERT(false);
            break;
    }
    new QTreeWidgetItem(printerSettingsRoot, { tr("Scale"), printScalingString });

    QString duplexString;
    switch (viewerPreferences->getDuplex())
    {
        case pdf::PDFViewerPreferences::Duplex::None:
            duplexString = tr("None");
            break;

        case pdf::PDFViewerPreferences::Duplex::Simplex:
            duplexString = tr("Simplex");
            break;

        case pdf::PDFViewerPreferences::Duplex::DuplexFlipLongEdge:
            duplexString = tr("Duplex (flip long edge)");
            break;

        case pdf::PDFViewerPreferences::Duplex::DuplexFlipShortEdge:
            duplexString = tr("Duplex (flip long edge)");
            break;

        default:
            Q_ASSERT(false);
            break;
    }
    new QTreeWidgetItem(printerSettingsRoot, { tr("Duplex mode"), duplexString });
    new QTreeWidgetItem(printerSettingsRoot, { tr("Pick tray by page size"), viewerPreferences->getOptions().testFlag(pdf::PDFViewerPreferences::PickTrayByPDFSize) ? tr("Yes") : tr("No") });

    QStringList pageRanges;
    for (const std::pair<pdf::PDFInteger, pdf::PDFInteger>& pageRange : viewerPreferences->getPrintPageRanges())
    {
        pageRanges << QString("%1-%2").arg(pageRange.first).arg(pageRange.second);
    }
    QString pageRangesString = pageRanges.join(",");
    new QTreeWidgetItem(printerSettingsRoot, { tr("Default print page ranges"), pageRangesString });
    new QTreeWidgetItem(printerSettingsRoot, { tr("Number of copies"), QString::number(viewerPreferences->getNumberOfCopies()) });

    ui->displayAndPrintTreeWidget->addTopLevelItem(viewerSettingsRoot);
    ui->displayAndPrintTreeWidget->addTopLevelItem(printerSettingsRoot);
    ui->displayAndPrintTreeWidget->expandAll();
    ui->displayAndPrintTreeWidget->resizeColumnToContents(0);
}

void PDFDocumentPropertiesDialog::onFontsFinished()
{
    if (!m_fontTreeWidgetItems.empty())
    {
        std::sort(m_fontTreeWidgetItems.begin(), m_fontTreeWidgetItems.end(), [](QTreeWidgetItem* left, QTreeWidgetItem* right) { return left->data(0, Qt::DisplayRole).toString() < right->data(0, Qt::DisplayRole).toString(); });
        for (QTreeWidgetItem* item : m_fontTreeWidgetItems)
        {
            ui->fontsTreeWidget->addTopLevelItem(item);
        }
        m_fontTreeWidgetItems.clear();

        ui->fontsTreeWidget->collapseAll();
        ui->fontsTreeWidget->expandToDepth(0);
        ui->fontsTreeWidget->resizeColumnToContents(0);
    }
}

void PDFDocumentPropertiesDialog::closeEvent(QCloseEvent* event)
{
    // We must wait for finishing of font loading;
    m_futureWatcher.waitForFinished();

    // We must delete all font tree items, because of asynchronous signal sent
    qDeleteAll(m_fontTreeWidgetItems);
    m_fontTreeWidgetItems.clear();

    BaseClass::closeEvent(event);
}

}   // namespace pdfviewer
