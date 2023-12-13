//    Copyright (C) 2019-2023 Jakub Melka
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
#include "pdfdrawwidget.h"
#include "pdfwidgettool.h"

#include "pdfdocument.h"
#include "pdfitemmodels.h"
#include "pdfexception.h"
#include "pdfsignaturehandler.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdocumentbuilder.h"
#include "pdfwidgetutils.h"

#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>
#include <QPainter>
#include <QTextToSpeech>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>
#include <QPushButton>

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
                                   bool editableOutline,
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
    QIcon outlineIcon(":/resources/outline.svg");
    m_outlineTreeModel = new pdf::PDFOutlineTreeItemModel(qMove(outlineIcon), editableOutline, this);
    ui->outlineTreeView->setModel(m_outlineTreeModel);
    ui->outlineTreeView->header()->hide();

    if (editableOutline)
    {
        ui->outlineTreeView->setDragEnabled(true);
        ui->outlineTreeView->setAcceptDrops(true);
        ui->outlineTreeView->setDropIndicatorShown(true);
        ui->outlineTreeView->setDragDropMode(QAbstractItemView::InternalMove);
        ui->outlineTreeView->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(ui->outlineTreeView, &QTreeView::customContextMenuRequested, this, &PDFSidebarWidget::onOutlineTreeViewContextMenuRequested);
        connect(m_outlineTreeModel, &pdf::PDFOutlineTreeItemModel::dataChanged, this, &PDFSidebarWidget::onOutlineItemsChanged);
        connect(m_outlineTreeModel, &pdf::PDFOutlineTreeItemModel::rowsInserted, this, &PDFSidebarWidget::onOutlineItemsChanged);
        connect(m_outlineTreeModel, &pdf::PDFOutlineTreeItemModel::rowsRemoved, this, &PDFSidebarWidget::onOutlineItemsChanged);
        connect(m_outlineTreeModel, &pdf::PDFOutlineTreeItemModel::rowsMoved, this, &PDFSidebarWidget::onOutlineItemsChanged);
    }

    connect(ui->outlineTreeView, &QTreeView::clicked, this, &PDFSidebarWidget::onOutlineItemClicked);

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
    m_pageInfo[Outline] = { ui->outlineButton, ui->outlinePage };
    m_pageInfo[Thumbnails] = { ui->thumbnailsButton, ui->thumbnailsPage };
    m_pageInfo[Attachments] = { ui->attachmentsButton, ui->attachmentsPage };
    m_pageInfo[Speech] = { ui->speechButton, ui->speechPage };
    m_pageInfo[Signatures] = { ui->signaturesButton, ui->signaturesPage };

    for (const auto& pageInfo : m_pageInfo)
    {
        if (pageInfo.second.button)
        {
            connect(pageInfo.second.button, &QToolButton::clicked, this, &PDFSidebarWidget::onPageButtonClicked);
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
                preferred = Outline;
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
                        preferred = Outline;
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

        case Outline:
            return m_outlineTreeModel->isEmpty() && (!m_document || !m_outlineTreeModel->isEditable());

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
        QToolButton* pushButton = pageInfo.second.button;
        if (pushButton)
        {
            pushButton->setChecked(pageInfo.first == page);

            QFont font = pushButton->font();
            font.setBold(pageInfo.first == page);
            pushButton->setFont(font);
        }

        if (pageInfo.first == page)
        {
            ui->stackedWidget->setCurrentWidget(pageInfo.second.page);
        }
    }

    if (page == Speech && ui->speechVoiceComboBox->count() == 0)
    {
        // Check, if speech engine is properly set
        QStringList speechEngines = QTextToSpeech::availableEngines();
        if (speechEngines.isEmpty())
        {
            QMessageBox::critical(this, tr("Error"), tr("Speech feature is unavailable. No speech engines detected. If you're using Linux, please install speech libraries like 'flite' or 'speechd'."));
        }
        else
        {
            QMessageBox::critical(this, tr("Error"), tr("The speech feature is available, but its options are not properly set. Please check the speech settings in the options dialog."));
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
            QTreeWidgetItem* item = new QTreeWidgetItem(rootItem, QStringList(QString("Signing date/time: %2").arg(QLocale::system().toString(signingDate, QLocale::ShortFormat))));
            item->setIcon(0, infoIcon);
        }

        QDateTime timestampDate = signature.getTimestampDate();
        if (timestampDate.isValid())
        {
            QTreeWidgetItem* item = new QTreeWidgetItem(rootItem, QStringList(QString("Timestamp: %2").arg(QLocale::system().toString(timestampDate, QLocale::ShortFormat))));
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
                    QTreeWidgetItem* item = new QTreeWidgetItem(certRoot, QStringList(QString("Valid from: %2").arg(QLocale::system().toString(notValidBefore, QLocale::ShortFormat))));
                    item->setIcon(0, infoIcon);
                }

                if (notValidAfter.isValid())
                {
                    QTreeWidgetItem* item = new QTreeWidgetItem(certRoot, QStringList(QString("Valid to: %2").arg(QLocale::system().toString(notValidAfter, QLocale::ShortFormat))));
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
        Q_EMIT actionTriggered(action);
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
                    catch (const pdf::PDFException &e)
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
        QVariant itemData = item->data(0, Qt::UserRole);
        if (itemData.isValid())
        {
            const pdf::PDFCertificateInfo& info = m_certificateInfos.at(itemData.toInt());
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

void PDFSidebarWidget::onOutlineTreeViewContextMenuRequested(const QPoint& pos)
{
    QMenu contextMenu;

    QModelIndex index = ui->outlineTreeView->indexAt(pos);

    auto onFollow = [this, index]()
    {
        onOutlineItemClicked(index);
    };

    auto onInsert = [this, index]()
    {
        if (index.isValid())
        {
            ui->outlineTreeView->model()->insertRow(index.row() + 1, index.parent());
        }
        else
        {
            ui->outlineTreeView->model()->insertRow(ui->outlineTreeView->model()->rowCount());
        }
    };

    auto onDelete = [this, index]()
    {
        ui->outlineTreeView->model()->removeRow(index.row(), index.parent());
    };

    auto onRename = [this, index]()
    {
        ui->outlineTreeView->edit(index);
    };

    QAction* followAction = contextMenu.addAction(tr("Follow"), onFollow);
    followAction->setEnabled(index.isValid());
    contextMenu.addSeparator();

    QAction* deleteAction = contextMenu.addAction(tr("Delete"), onDelete);
    QAction* insertAction = contextMenu.addAction(tr("Insert"), this, onInsert);
    QAction* renameAction = contextMenu.addAction(tr("Rename"), this, onRename);

    deleteAction->setEnabled(index.isValid());
    insertAction->setEnabled(true);
    renameAction->setEnabled(index.isValid());

    contextMenu.addSeparator();

    const pdf::PDFOutlineItem* outlineItem = m_outlineTreeModel->getOutlineItem(index);
    const bool isFontBold = outlineItem && outlineItem->isFontBold();
    const bool isFontItalics = outlineItem && outlineItem->isFontItalics();

    auto onFontBold = [this, index, isFontBold]()
    {
        m_outlineTreeModel->setFontBold(index, !isFontBold);
    };

    auto onFontItalic = [this, index, isFontItalics]()
    {
        m_outlineTreeModel->setFontItalics(index, !isFontItalics);
    };

    QAction* fontBoldAction = contextMenu.addAction(tr("Font Bold"), onFontBold);
    QAction* fontItalicAction = contextMenu.addAction(tr("Font Italic"), onFontItalic);
    fontBoldAction->setCheckable(true);
    fontItalicAction->setCheckable(true);
    fontBoldAction->setChecked(isFontBold);
    fontItalicAction->setChecked(isFontItalics);
    fontBoldAction->setEnabled(index.isValid());
    fontItalicAction->setEnabled(index.isValid());

    QMenu* submenu = new QMenu(tr("Set Target"), &contextMenu);
    QAction* targetAction = contextMenu.addMenu(submenu);
    targetAction->setEnabled(index.isValid());

    auto createOnSetTarget = [this, index](pdf::DestinationType destinationType)
    {
        auto onSetTarget = [this, index, destinationType]()
        {
            pdf::PDFToolManager* toolManager = m_proxy->getWidget()->getToolManager();

            auto pickRectangle = [this, index, destinationType](pdf::PDFInteger pageIndex, QRectF rect)
            {
                pdf::PDFDestination destination;
                destination.setDestinationType(destinationType);
                destination.setPageIndex(pageIndex);
                destination.setPageReference(m_document->getCatalog()->getPage(pageIndex)->getPageReference());
                destination.setLeft(rect.left());
                destination.setRight(rect.right());
                destination.setTop(rect.bottom());
                destination.setBottom(rect.top());
                destination.setZoom(m_proxy->getZoom());
                m_outlineTreeModel->setDestination(index, destination);
            };

            toolManager->pickRectangle(pickRectangle);
        };

        return onSetTarget;
    };

    auto onNamedDestinationTriggered = [this, index]()
    {
        class SelectNamedDestinationDialog : public QDialog
        {
        public:
            explicit SelectNamedDestinationDialog(const QStringList& items, QWidget* parent)
                : QDialog(parent)
            {
                setWindowTitle(tr("Select Named Destination"));
                setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 150));

                QVBoxLayout* layout = new QVBoxLayout(this);

                m_comboBox = new QComboBox(this);
                m_comboBox->addItems(items);
                m_comboBox->setEditable(false);
                layout->addWidget(m_comboBox);

                QHBoxLayout* buttonsLayout = new QHBoxLayout();
                QPushButton* okButton = new QPushButton(tr("OK"), this);
                QPushButton* cancelButton = new QPushButton(tr("Cancel"), this);

                buttonsLayout->addWidget(okButton);
                buttonsLayout->addWidget(cancelButton);

                layout->addLayout(buttonsLayout);

                connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
                connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
            }

            QString selectedText() const
            {
                return m_comboBox->currentText();
            }

        private:
            QComboBox* m_comboBox;
        };

        QStringList items;
        for (const auto& namedDestination : m_document->getCatalog()->getNamedDestinations())
        {
            items << QString::fromLatin1(namedDestination.first);
        }

        SelectNamedDestinationDialog dialog(items, m_proxy->getWidget());
        if (dialog.exec() == QDialog::Accepted)
        {
            m_outlineTreeModel->setDestination(index, pdf::PDFDestination::createNamed(dialog.selectedText().toLatin1()));
        }
    };

    submenu->addAction(tr("Named Destination"), onNamedDestinationTriggered);
    submenu->addAction(tr("Fit Page"), createOnSetTarget(pdf::DestinationType::Fit));
    submenu->addAction(tr("Fit Page Horizontally"), createOnSetTarget(pdf::DestinationType::FitH));
    submenu->addAction(tr("Fit Page Vertically"), createOnSetTarget(pdf::DestinationType::FitV));
    submenu->addAction(tr("Fit Rectangle"), createOnSetTarget(pdf::DestinationType::FitR));
    submenu->addAction(tr("Fit Bounding Box"), createOnSetTarget(pdf::DestinationType::FitB));
    submenu->addAction(tr("Fit Bounding Box Horizontally"), createOnSetTarget(pdf::DestinationType::FitBH));
    submenu->addAction(tr("Fit Bounding Box Vertically"), createOnSetTarget(pdf::DestinationType::FitBV));
    submenu->addAction(tr("XYZ"), createOnSetTarget(pdf::DestinationType::XYZ));

    contextMenu.exec(ui->outlineTreeView->mapToGlobal(pos));
}

void PDFSidebarWidget::onOutlineItemsChanged()
{
    if (m_document)
    {
        pdf::PDFDocumentBuilder builder(m_document);
        builder.setOutline(m_outlineTreeModel->getRootOutlineItem());

        pdf::PDFDocumentPointer pointer(new pdf::PDFDocument(builder.build()));
        pdf::PDFModifiedDocument document(qMove(pointer), m_optionalContentActivity, pdf::PDFModifiedDocument::None);
        Q_EMIT documentModified(qMove(document));
    }
}

void PDFSidebarWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(64, 64, 64));
}

}   // namespace pdfviewer
