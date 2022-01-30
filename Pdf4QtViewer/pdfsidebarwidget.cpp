//    Copyright (C) 2019-2022 Jakub Melka
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

#include "pdfsidebarwidget.h"
#include "ui_pdfsidebarwidget.h"

#include "pdfviewersettings.h"

#include "pdfwidgetutils.h"
#include "pdftexttospeech.h"
#include "pdfdbgheap.h"

#include "pdfdocument.h"
#include "pdfitemmodels.h"
#include "pdfexception.h"
#include "pdfsignaturehandler.h"
#include "pdfdrawspacecontroller.h"

#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>
#include <QPainter>

namespace pdfviewer
{

constexpr const char* STYLESHEET =
        "QPushButton { background-color: #404040; color: #FFFFFF; }"
        "QPushButton:disabled { background-color: #404040; color: #000000; }"
        "QPushButton:checked { background-color: #808080; color: #FFFFFF; }"
        "QWidget#thumbnailsToolbarWidget { background-color: #F0F0F0 }"
        "QWidget#speechPage { background-color: #F0F0F0 }"
        "QWidget#PDFSidebarWidget { background-color: #404040; background: green;}";

PDFSidebarWidget::PDFSidebarWidget(pdf::PDFDrawWidgetProxy* proxy,
                                   PDFTextToSpeech* textToSpeech,
                                   pdf::PDFCertificateStore* certificateStore,
                                   PDFViewerSettings* settings,
                                   QWidget* parent) :
    QWidget(parent),
    ui(new Ui::PDFSidebarWidget),
    m_proxy(proxy),
    m_textToSpeech(textToSpeech),
    m_certificateStore(certificateStore),
    m_settings(settings),
    m_outlineTreeModel(nullptr),
    m_thumbnailsModel(nullptr),
    m_optionalContentTreeModel(nullptr),
    m_document(nullptr),
    m_optionalContentActivity(nullptr),
    m_attachmentsTreeModel(nullptr)
{
    ui->setupUi(this);

    setStyleSheet(STYLESHEET);

    // Outline
    QIcon bookmarkIcon(":/resources/bookmark.svg");
    m_outlineTreeModel = new pdf::PDFOutlineTreeItemModel(qMove(bookmarkIcon), this);
    ui->bookmarksTreeView->setModel(m_outlineTreeModel);
    ui->bookmarksTreeView->header()->hide();
    connect(ui->bookmarksTreeView, &QTreeView::clicked, this, &PDFSidebarWidget::onOutlineItemClicked);

    // Thumbnails
    m_thumbnailsModel = new pdf::PDFThumbnailsItemModel(proxy, this);
    int thumbnailsMargin = ui->thumbnailsListView->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, ui->thumbnailsListView) + 1;
    int thumbnailsFontSize = QFontMetrics(ui->thumbnailsListView->font()).lineSpacing();
    m_thumbnailsModel->setExtraItemSizeHint(2 * thumbnailsMargin, thumbnailsMargin + thumbnailsFontSize);
    ui->thumbnailsListView->setModel(m_thumbnailsModel);
    connect(ui->thumbnailsSizeSlider, &QSlider::valueChanged, this, &PDFSidebarWidget::onThumbnailsSizeChanged);
    connect(ui->thumbnailsListView, &QListView::clicked, this, &PDFSidebarWidget::onThumbnailClicked);
    onThumbnailsSizeChanged(ui->thumbnailsSizeSlider->value());

    // Optional content
    ui->optionalContentTreeView->header()->hide();
    m_optionalContentTreeModel = new pdf::PDFOptionalContentTreeItemModel(this);
    ui->optionalContentTreeView->setModel(m_optionalContentTreeModel);

    // Attachments
    ui->attachmentsTreeView->header()->hide();
    m_attachmentsTreeModel = new pdf::PDFAttachmentsTreeItemModel(this);
    ui->attachmentsTreeView->setModel(m_attachmentsTreeModel);
    ui->attachmentsTreeView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->attachmentsTreeView->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->attachmentsTreeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->attachmentsTreeView, &QTreeView::customContextMenuRequested, this, &PDFSidebarWidget::onAttachmentCustomContextMenuRequested);

    m_pageInfo[Invalid] = { nullptr, ui->emptyPage };
    m_pageInfo[OptionalContent] = { ui->optionalContentButton, ui->optionalContentPage };
    m_pageInfo[Bookmarks] = { ui->bookmarksButton, ui->bookmarksPage };
    m_pageInfo[Thumbnails] = { ui->thumbnailsButton, ui->thumbnailsPage };
    m_pageInfo[Attachments] = { ui->attachmentsButton, ui->attachmentsPage };
    m_pageInfo[Speech] = { ui->speechButton, ui->speechPage };
    m_pageInfo[Signatures] = { ui->signaturesButton, ui->signaturesPage };

    for (const auto& pageInfo : m_pageInfo)
    {
        if (pageInfo.second.button)
        {
            connect(pageInfo.second.button, &QPushButton::clicked, this, &PDFSidebarWidget::onPageButtonClicked);
        }
    }

    m_textToSpeech->initializeUI(ui->speechLocaleComboBox, ui->speechVoiceComboBox,
                                 ui->speechRateEdit, ui->speechPitchEdit, ui->speechVolumeEdit,
                                 ui->speechPlayButton, ui->speechPauseButton, ui->speechStopButton, ui->speechSynchronizeButton,
                                 ui->speechRateValueLabel, ui->speechPitchValueLabel, ui->speechVolumeValueLabel,
                                 ui->speechActualTextEdit);

    ui->signatureTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->signatureTreeWidget, &QTreeWidget::customContextMenuRequested, this, &PDFSidebarWidget::onSignatureCustomContextMenuRequested);

    selectPage(Invalid);
    updateButtons();
}

PDFSidebarWidget::~PDFSidebarWidget()
{
    delete ui;
}

void PDFSidebarWidget::setDocument(const pdf::PDFModifiedDocument& document, const std::vector<pdf::PDFSignatureVerificationResult>& signatures)
{
    m_document = document;
    m_optionalContentActivity = document.getOptionalContentActivity();
    m_signatures = signatures;

    // Update outline
    m_outlineTreeModel->setDocument(document);

    // Thumbnails
    m_thumbnailsModel->setDocument(document);

    // Update optional content
    m_optionalContentTreeModel->setDocument(document);
    m_optionalContentTreeModel->setActivity(m_optionalContentActivity);
    ui->optionalContentTreeView->expandAll();

    // Update attachments
    m_attachmentsTreeModel->setDocument(document);
    ui->attachmentsTreeView->expandAll();
    ui->attachmentsTreeView->resizeColumnToContents(0);

    Page preferred = Invalid;
    if (m_document)
    {
        const pdf::PageMode pageMode = m_document->getCatalog()->getPageMode();
        switch (pageMode)
        {
            case pdf::PageMode::UseOutlines:
                preferred = Bookmarks;
                break;

            case pdf::PageMode::UseThumbnails:
                preferred = Thumbnails;
                break;

            case pdf::PageMode::UseOptionalContent:
                preferred = OptionalContent;
                break;

            case pdf::PageMode::UseAttachments:
                preferred = Attachments;
                break;

            case pdf::PageMode::Fullscreen:
            {
                pdf::PDFViewerPreferences::NonFullScreenPageMode nonFullscreenPageMode = m_document->getCatalog()->getViewerPreferences()->getNonFullScreenPageMode();
                switch (nonFullscreenPageMode)
                {
                    case pdf::PDFViewerPreferences::NonFullScreenPageMode::UseOutline:
                        preferred = Bookmarks;
                        break;

                    case pdf::PDFViewerPreferences::NonFullScreenPageMode::UseThumbnails:
                        preferred = Thumbnails;
                        break;

                    case pdf::PDFViewerPreferences::NonFullScreenPageMode::UseOptionalContent:
                        preferred = OptionalContent;
                        break;

                    default:
                        break;
                }

                break;
            }

            default:
                break;
        }
    }

    // Update GUI
    updateGUI(preferred);
    updateButtons();
    updateSignatures(signatures);
}

bool PDFSidebarWidget::isEmpty() const
{
    for (int i = _BEGIN; i < _END; ++i)
    {
        if (!isEmpty(static_cast<Page>(i)))
        {
            return false;
        }
    }

    return true;
}

bool PDFSidebarWidget::isEmpty(Page page) const
{
    switch (page)
    {
        case Invalid:
            return true;

        case Bookmarks:
            return m_outlineTreeModel->isEmpty();

        case Thumbnails:
            return m_thumbnailsModel->isEmpty();

        case OptionalContent:
            return m_optionalContentTreeModel->isEmpty();

        case Attachments:
            return m_attachmentsTreeModel->isEmpty();

        case Speech:
            return !m_textToSpeech->isValid();

        case Signatures:
            return m_signatures.empty();

        default:
            Q_ASSERT(false);
            break;
    }

    return true;
}

void PDFSidebarWidget::selectPage(Page page)
{
    // Switch state of the buttons and select the page
    for (const auto& pageInfo : m_pageInfo)
    {
        if (pageInfo.second.button)
        {
            pageInfo.second.button->setChecked(pageInfo.first == page);
        }

        if (pageInfo.first == page)
        {
            ui->stackedWidget->setCurrentWidget(pageInfo.second.page);
        }
    }
}

std::vector<PDFSidebarWidget::Page> PDFSidebarWidget::getValidPages() const
{
    std::vector<PDFSidebarWidget::Page> result;
    result.reserve(_END - _BEGIN + 1);

    for (int i = _BEGIN; i < _END; ++i)
    {
        if (!isEmpty(static_cast<Page>(i)))
        {
            result.push_back(static_cast<Page>(i));
        }
    }

    return result;
}

void PDFSidebarWidget::setCurrentPages(const std::vector<pdf::PDFInteger>& currentPages)
{
    if (!currentPages.empty() && ui->synchronizeThumbnailsButton->isChecked())
    {
        QModelIndex index = m_thumbnailsModel->index(currentPages.front(), 0, QModelIndex());
        if (index.isValid())
        {
            ui->thumbnailsListView->scrollTo(index, QListView::EnsureVisible);

            // Try to examine, if we have to switch the current index
            QModelIndex currentIndex = ui->thumbnailsListView->currentIndex();
            if (currentIndex.isValid())
            {
                const pdf::PDFInteger currentPageIndex = m_thumbnailsModel->getPageIndex(currentIndex);
                Q_ASSERT(std::is_sorted(currentPages.cbegin(), currentPages.cend()));
                if (std::binary_search(currentPages.cbegin(), currentPages.cend(), currentPageIndex))
                {
                    return;
                }
            }
            ui->thumbnailsListView->setCurrentIndex(index);
        }
    }
}

void PDFSidebarWidget::updateGUI(Page preferredPage)
{
    if (preferredPage != Invalid && !isEmpty(preferredPage))
    {
        selectPage(preferredPage);
    }
    else
    {
        // Select first nonempty page
        std::vector<Page> validPages = getValidPages();
        if (!validPages.empty())
        {
            selectPage(validPages.front());
        }
        else
        {
            selectPage(Invalid);
        }
    }
}

void PDFSidebarWidget::updateButtons()
{
    for (const auto& pageInfo : m_pageInfo)
    {
        if (pageInfo.second.button)
        {
            pageInfo.second.button->setEnabled(!isEmpty(pageInfo.first));
        }
    }
}

void PDFSidebarWidget::updateSignatures(const std::vector<pdf::PDFSignatureVerificationResult>& signatures)
{
    ui->signatureTreeWidget->setUpdatesEnabled(false);
    ui->signatureTreeWidget->clear();

    m_certificateInfos.clear();

    QIcon okIcon(":/resources/result-ok.svg");
    QIcon errorIcon(":/resources/result-error.svg");
    QIcon warningIcon(":/resources/result-warning.svg");
    QIcon infoIcon(":/resources/result-information.svg");

    if (m_settings->getSettings().m_signatureTreatWarningsAsErrors)
    {
        warningIcon = errorIcon;
    }

    for (const pdf::PDFSignatureVerificationResult& signature : signatures)
    {
        const pdf::PDFCertificateInfos& certificateInfos = signature.getCertificateInfos();
        const pdf::PDFCertificateInfo* certificateInfo = !certificateInfos.empty() ? &certificateInfos.front() : nullptr;

        QString templateString;
        switch (signature.getType())
        {
            case pdf::PDFSignature::Type::Sig:
                templateString = tr("Signature - %1");
                break;

            case pdf::PDFSignature::Type::DocTimeStamp:
                templateString = tr("Timestamp - %1");
                break;

            case pdf::PDFSignature::Type::Invalid:
                continue;

            default:
                Q_ASSERT(false);
                break;
        }

        QString text = templateString.arg(certificateInfo ? certificateInfo->getName(pdf::PDFCertificateInfo::CommonName) : tr("Unknown"));
        QTreeWidgetItem* rootItem = new QTreeWidgetItem(QStringList(text));

        if (signature.hasError())
        {
            rootItem->setIcon(0, errorIcon);
        }
        else if (signature.hasWarning())
        {
            rootItem->setIcon(0, warningIcon);
        }
        else
        {
            rootItem->setIcon(0, okIcon);
        }

        if (signature.isCertificateValid())
        {
            QTreeWidgetItem* certificateItem = new QTreeWidgetItem(rootItem, QStringList(tr("Certificate is valid.")));
            certificateItem->setIcon(0, okIcon);
        }

        if (signature.isSignatureValid())
        {
            QTreeWidgetItem* signatureItem = new QTreeWidgetItem(rootItem, QStringList(tr("Signature is valid.")));
            signatureItem->setIcon(0, okIcon);
        }

        for (const QString& error : signature.getErrors())
        {
            QTreeWidgetItem* item = new QTreeWidgetItem(rootItem, QStringList(error));
            item->setIcon(0, errorIcon);
        }

        for (const QString& error : signature.getWarnings())
        {
            QTreeWidgetItem* item = new QTreeWidgetItem(rootItem, QStringList(error));
            item->setIcon(0, warningIcon);
        }

        QString hashAlgorithms = signature.getHashAlgorithms().join(", ");
        if (!hashAlgorithms.isEmpty())
        {
            QTreeWidgetItem* item = new QTreeWidgetItem(rootItem, QStringList(tr("Hash algorithm: %1").arg(hashAlgorithms.toUpper())));
            item->setIcon(0, infoIcon);
        }

        QDateTime signingDate = signature.getSignatureDate();
        if (signingDate.isValid())
        {
            QTreeWidgetItem* item = new QTreeWidgetItem(rootItem, QStringList(QString("Signing date/time: %2").arg(signingDate.toString(Qt::DefaultLocaleShortDate))));
            item->setIcon(0, infoIcon);
        }

        QDateTime timestampDate = signature.getTimestampDate();
        if (timestampDate.isValid())
        {
            QTreeWidgetItem* item = new QTreeWidgetItem(rootItem, QStringList(QString("Timestamp: %2").arg(timestampDate.toString(Qt::DefaultLocaleShortDate))));
            item->setIcon(0, infoIcon);
        }

        if (certificateInfo)
        {
            QTreeWidgetItem* certChainRoot = new QTreeWidgetItem(rootItem, QStringList(tr("Certificate validation chain")));
            certChainRoot->setIcon(0, infoIcon);
            for (const pdf::PDFCertificateInfo& currentCertificateInfo : certificateInfos)
            {
                QTreeWidgetItem* certRoot = new QTreeWidgetItem(certChainRoot, QStringList(currentCertificateInfo.getName(pdf::PDFCertificateInfo::CommonName)));
                certRoot->setIcon(0, infoIcon);

                pdf::PDFCertificateInfo::KeyUsageFlags keyUsageFlags = currentCertificateInfo.getKeyUsage();
                if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageCertSign))
                {
                    m_certificateInfos.push_back(currentCertificateInfo);
                    certRoot->setData(0, Qt::UserRole, QVariant(static_cast<qint64>(m_certificateInfos.size() - 1)));
                }

                auto addName = [certRoot, &currentCertificateInfo, &infoIcon](pdf::PDFCertificateInfo::NameEntry nameEntry, QString caption)
                {
                    QString text = currentCertificateInfo.getName(nameEntry);
                    if (!text.isEmpty())
                    {
                        QTreeWidgetItem* item = new QTreeWidgetItem(certRoot, QStringList(QString("%1: %2").arg(caption, text)));
                        item->setIcon(0, infoIcon);
                    }
                };

                QString publicKeyMethod;
                switch (currentCertificateInfo.getPublicKey())
                {
                    case pdf::PDFCertificateInfo::KeyRSA:
                        publicKeyMethod = tr("Protected by RSA method, %1-bit key").arg(currentCertificateInfo.getKeySize());
                        break;

                    case pdf::PDFCertificateInfo::KeyDSA:
                        publicKeyMethod = tr("Protected by DSA method, %1-bit key").arg(currentCertificateInfo.getKeySize());
                        break;

                    case pdf::PDFCertificateInfo::KeyEC:
                        publicKeyMethod = tr("Protected by EC method, %1-bit key").arg(currentCertificateInfo.getKeySize());
                        break;

                    case pdf::PDFCertificateInfo::KeyDH:
                        publicKeyMethod = tr("Protected by DH method, %1-bit key").arg(currentCertificateInfo.getKeySize());
                        break;

                    case pdf::PDFCertificateInfo::KeyUnknown:
                        publicKeyMethod = tr("Unknown protection method, %1-bit key").arg(currentCertificateInfo.getKeySize());
                        break;

                    default:
                        Q_ASSERT(false);
                        break;
                }

                addName(pdf::PDFCertificateInfo::CountryName, tr("Country"));
                addName(pdf::PDFCertificateInfo::OrganizationName, tr("Organization"));
                addName(pdf::PDFCertificateInfo::OrganizationalUnitName, tr("Org. unit"));
                addName(pdf::PDFCertificateInfo::DistinguishedName, tr("Name"));
                addName(pdf::PDFCertificateInfo::StateOrProvinceName, tr("State"));
                addName(pdf::PDFCertificateInfo::SerialNumber, tr("Serial number"));
                addName(pdf::PDFCertificateInfo::LocalityName, tr("Locality"));
                addName(pdf::PDFCertificateInfo::Title, tr("Title"));
                addName(pdf::PDFCertificateInfo::Surname, tr("Surname"));
                addName(pdf::PDFCertificateInfo::GivenName, tr("Forename"));
                addName(pdf::PDFCertificateInfo::Initials, tr("Initials"));
                addName(pdf::PDFCertificateInfo::Pseudonym, tr("Pseudonym"));
                addName(pdf::PDFCertificateInfo::GenerationalQualifier, tr("Qualifier"));
                addName(pdf::PDFCertificateInfo::Email, tr("Email"));

                QTreeWidgetItem* publicKeyItem = new QTreeWidgetItem(certRoot, QStringList(publicKeyMethod));
                publicKeyItem->setIcon(0, infoIcon);

                QDateTime notValidBefore = currentCertificateInfo.getNotValidBefore().toLocalTime();
                QDateTime notValidAfter = currentCertificateInfo.getNotValidAfter().toLocalTime();

                if (notValidBefore.isValid())
                {
                    QTreeWidgetItem* item = new QTreeWidgetItem(certRoot, QStringList(QString("Valid from: %2").arg(notValidBefore.toString(Qt::DefaultLocaleShortDate))));
                    item->setIcon(0, infoIcon);
                }

                if (notValidAfter.isValid())
                {
                    QTreeWidgetItem* item = new QTreeWidgetItem(certRoot, QStringList(QString("Valid to: %2").arg(notValidAfter.toString(Qt::DefaultLocaleShortDate))));
                    item->setIcon(0, infoIcon);
                }

                QStringList keyUsages;
                if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageDigitalSignature))
                {
                    keyUsages << tr("Digital signatures");
                }
                if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageNonRepudiation))
                {
                    keyUsages << tr("Non-repudiation");
                }
                if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageKeyEncipherment))
                {
                    keyUsages << tr("Key encipherement");
                }
                if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageDataEncipherment))
                {
                    keyUsages << tr("Application data encipherement");
                }
                if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageAgreement))
                {
                    keyUsages << tr("Key agreement");
                }
                if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageCertSign))
                {
                    keyUsages << tr("Verify signatures on certificates");
                }
                if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageCrlSign))
                {
                    keyUsages << tr("Verify signatures on revocation information");
                }
                if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageEncipherOnly))
                {
                    keyUsages << tr("Encipher data during key agreement");
                }
                if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageDecipherOnly))
                {
                    keyUsages << tr("Decipher data during key agreement");
                }
                if (keyUsageFlags.testFlag(pdf::PDFCertificateInfo::KeyUsageExtended_TIMESTAMP))
                {
                    keyUsages << tr("Trusted timestamping");
                }

                if (!keyUsages.isEmpty())
                {
                    QTreeWidgetItem* keyUsageRoot = new QTreeWidgetItem(certRoot, QStringList(tr("Key usages")));
                    keyUsageRoot->setIcon(0, infoIcon);

                    for (const QString& keyUsage : keyUsages)
                    {
                        QTreeWidgetItem* keyUsageItem = new QTreeWidgetItem(keyUsageRoot, QStringList(keyUsage));
                        keyUsageItem->setIcon(0, infoIcon);
                    }
                }
            }
        }

        ui->signatureTreeWidget->addTopLevelItem(rootItem);
    }

    ui->signatureTreeWidget->expandToDepth(1);
    ui->signatureTreeWidget->setUpdatesEnabled(true);
}

void PDFSidebarWidget::onPageButtonClicked()
{
    QObject* pushButton = sender();

    for (const auto& pageInfo : m_pageInfo)
    {
        if (pageInfo.second.button == pushButton)
        {
            Q_ASSERT(!isEmpty(pageInfo.first));
            selectPage(pageInfo.first);
            break;
        }
    }
}

void PDFSidebarWidget::onOutlineItemClicked(const QModelIndex& index)
{
    if (const pdf::PDFAction* action = m_outlineTreeModel->getAction(index))
    {
        emit actionTriggered(action);
    }
}

void PDFSidebarWidget::onThumbnailsSizeChanged(int size)
{
    const int thumbnailsSize = pdf::PDFWidgetUtils::getPixelSize(this, size * 10.0);
    Q_ASSERT(thumbnailsSize > 0);
    m_thumbnailsModel->setThumbnailsSize(thumbnailsSize);
}

void PDFSidebarWidget::onAttachmentCustomContextMenuRequested(const QPoint& pos)
{
    if (const pdf::PDFFileSpecification* fileSpecification = m_attachmentsTreeModel->getFileSpecification(ui->attachmentsTreeView->indexAt(pos)))
    {
        QMenu menu(this);
        QAction* action = new QAction(QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton, nullptr, ui->attachmentsTreeView), tr("Save to File..."), &menu);

        auto onSaveTriggered = [this, fileSpecification]()
        {
            const pdf::PDFEmbeddedFile* platformFile = fileSpecification->getPlatformFile();
            if (platformFile && platformFile->isValid())
            {
                QString defaultFileName = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QDir::separator() + fileSpecification->getPlatformFileName();
                QString saveFileName = QFileDialog::getSaveFileName(this, tr("Save attachment"), defaultFileName);
                if (!saveFileName.isEmpty())
                {
                    try
                    {
                        QByteArray data = m_document->getDecodedStream(platformFile->getStream());

                        QFile file(saveFileName);
                        if (file.open(QFile::WriteOnly | QFile::Truncate))
                        {
                            file.write(data);
                            file.close();
                        }
                        else
                        {
                            QMessageBox::critical(this, tr("Error"), tr("Failed to save attachment to file. %1").arg(file.errorString()));
                        }
                    }
                    catch (pdf::PDFException e)
                    {
                        QMessageBox::critical(this, tr("Error"), tr("Failed to save attachment to file. %1").arg(e.getMessage()));
                    }
                }
            }
            else
            {
                QMessageBox::critical(this, tr("Error"), tr("Failed to save attachment to file. Attachment is corrupted."));
            }
        };
        connect(action, &QAction::triggered, this, onSaveTriggered);

        menu.addAction(action);
        menu.exec(ui->attachmentsTreeView->viewport()->mapToGlobal(pos));
    }
}

void PDFSidebarWidget::onThumbnailClicked(const QModelIndex& index)
{
    if (index.isValid())
    {
        m_proxy->goToPage(m_thumbnailsModel->getPageIndex(index));
    }
}

void PDFSidebarWidget::onSignatureCustomContextMenuRequested(const QPoint& pos)
{
    if (QTreeWidgetItem* item = ui->signatureTreeWidget->itemAt(pos))
    {
        QVariant data = item->data(0, Qt::UserRole);
        if (data.isValid())
        {
            const pdf::PDFCertificateInfo& info = m_certificateInfos.at(data.toInt());
            if (!m_certificateStore->contains(info))
            {
                QMenu menu;
                QAction* action = menu.addAction(tr("Add to trusted certificates"));

                auto addCertificate = [this, info]()
                {
                    if (QMessageBox::question(this, tr("Add to Trusted Certificate Store"), tr("Are you sure want to add '%1' to the trusted certificate store?").arg(info.getName(pdf::PDFCertificateInfo::CommonName))) == QMessageBox::Yes)
                    {
                        if (!m_certificateStore->add(pdf::PDFCertificateStore::EntryType::User, info))
                        {
                            QMessageBox::critical(this, tr("Trusted Certificate Store Error"), tr("Failed to add certificate to the trusted certificate store."));
                        }
                    }
                };
                connect(action, &QAction::triggered, this, addCertificate);

                menu.exec(ui->signatureTreeWidget->viewport()->mapToGlobal(pos));
            }
        }
    }
}

void PDFSidebarWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(64, 64, 64));
}

}   // namespace pdfviewer
