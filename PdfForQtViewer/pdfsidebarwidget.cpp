//    Copyright (C) 2019-2020 Jakub Melka
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

#include "pdfsidebarwidget.h"
#include "ui_pdfsidebarwidget.h"

#include "pdfwidgetutils.h"

#include "pdfdocument.h"
#include "pdfitemmodels.h"
#include "pdfexception.h"
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
        "QWidget#PDFSidebarWidget { background-color: #404040; background: green;}";

PDFSidebarWidget::PDFSidebarWidget(pdf::PDFDrawWidgetProxy* proxy, QWidget* parent) :
    QWidget(parent),
    ui(new Ui::PDFSidebarWidget),
    m_proxy(proxy),
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

    for (const auto& pageInfo : m_pageInfo)
    {
        if (pageInfo.second.button)
        {
            connect(pageInfo.second.button, &QPushButton::clicked, this, &PDFSidebarWidget::onPageButtonClicked);
        }
    }

    selectPage(Invalid);
    updateButtons();
}

PDFSidebarWidget::~PDFSidebarWidget()
{
    delete ui;
}

void PDFSidebarWidget::setDocument(const pdf::PDFDocument* document, pdf::PDFOptionalContentActivity* optionalContentActivity)
{
    m_document = document;
    m_optionalContentActivity = optionalContentActivity;

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
    const int thumbnailsSize = PDFWidgetUtils::getPixelSize(this, size * 10.0);
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

void PDFSidebarWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(64, 64, 64));
}

}   // namespace pdfviewer
