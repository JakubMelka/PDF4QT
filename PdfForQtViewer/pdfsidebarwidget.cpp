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

#include "pdfsidebarwidget.h"
#include "ui_pdfsidebarwidget.h"

#include "pdfdocument.h"
#include "pdfitemmodels.h"

namespace pdfviewer
{

PDFSidebarWidget::PDFSidebarWidget(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::PDFSidebarWidget),
    m_outlineTreeModel(nullptr),
    m_optionalContentTreeModel(nullptr),
    m_document(nullptr),
    m_optionalContentActivity(nullptr)
{
    ui->setupUi(this);

    // Outline
    QIcon bookmarkIcon(":/resources/bookmark.svg");
    m_outlineTreeModel = new pdf::PDFOutlineTreeItemModel(qMove(bookmarkIcon), this);
    ui->bookmarksTreeView->setModel(m_outlineTreeModel);
    ui->bookmarksTreeView->header()->hide();
    connect(ui->bookmarksTreeView, &QTreeView::clicked, this, &PDFSidebarWidget::onOutlineItemClicked);

    // Optional content
    ui->optionalContentTreeView->header()->hide();
    m_optionalContentTreeModel = new pdf::PDFOptionalContentTreeItemModel(this);
    ui->optionalContentTreeView->setModel(m_optionalContentTreeModel);

    m_pageInfo[Invalid] = { nullptr, ui->emptyPage };
    m_pageInfo[OptionalContent] = { ui->optionalContentButton, ui->optionalContentPage };
    m_pageInfo[Bookmarks] = { ui->bookmarksButton, ui->bookmarksPage };
    m_pageInfo[Thumbnails] = { ui->thumbnailsButton, ui->thumbnailsPage };

    setAutoFillBackground(true);
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

    // Update optional content
    m_optionalContentTreeModel->setDocument(document);
    m_optionalContentTreeModel->setActivity(m_optionalContentActivity);
    ui->optionalContentTreeView->expandAll();

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

        case OptionalContent:
            return m_optionalContentTreeModel->isEmpty();

        case Bookmarks:
            return m_outlineTreeModel->isEmpty();

        case Thumbnails:
            return true;

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

void PDFSidebarWidget::onOutlineItemClicked(const QModelIndex& index)
{
    if (const pdf::PDFAction* action = m_outlineTreeModel->getAction(index))
    {
        emit actionTriggered(action);
    }
}

}   // namespace pdfviewer
