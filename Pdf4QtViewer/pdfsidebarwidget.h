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


#ifndef PDFSIDEBARWIDGET_H
#define PDFSIDEBARWIDGET_H

#include "pdfglobal.h"
#include "pdfbookmarkmanager.h"

#include <QWidget>

class QPushButton;
class QToolButton;
class QWidget;

namespace Ui
{
class PDFSidebarWidget;
}

namespace pdf
{
class PDFAction;
class PDFDocument;
class PDFCertificateInfo;
class PDFDrawWidgetProxy;
class PDFCertificateStore;
class PDFModifiedDocument;
class PDFThumbnailsItemModel;
class PDFOutlineTreeItemModel;
class PDFOptionalContentActivity;
class PDFAttachmentsTreeItemModel;
class PDFSignatureVerificationResult;
class PDFOptionalContentTreeItemModel;
}

namespace pdfviewer
{
class PDFTextToSpeech;
class PDFViewerSettings;
class PDFBookmarkItemModel;

class PDFSidebarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PDFSidebarWidget(pdf::PDFDrawWidgetProxy* proxy,
                              PDFTextToSpeech* textToSpeech,
                              pdf::PDFCertificateStore* certificateStore,
                              PDFBookmarkManager* bookmarkManager,
                              PDFViewerSettings* settings,
                              bool editableOutline,
                              QWidget* parent);
    virtual ~PDFSidebarWidget() override;

    virtual void paintEvent(QPaintEvent* event) override;

    enum Page
    {
        Invalid,
        _BEGIN,
        Outline = _BEGIN,
        Thumbnails,
        OptionalContent,
        Attachments,
        Speech,
        Signatures,
        Bookmarks,
        _END
    };

    void setDocument(const pdf::PDFModifiedDocument& document, const std::vector<pdf::PDFSignatureVerificationResult>& signatures);

    /// Returns true, if all items in sidebar are empty
    bool isEmpty() const;

    /// Returns true, if page is empty
    bool isEmpty(Page page) const;

    /// Selects current page
    void selectPage(Page page);

    /// Returns list of valid pages (nonempty pages)
    std::vector<Page> getValidPages() const;

    /// Sets current pages (for example, selects the correct thumbnail)
    void setCurrentPages(const std::vector<pdf::PDFInteger>& currentPages);

signals:
    void actionTriggered(const pdf::PDFAction* action);
    void documentModified(pdf::PDFModifiedDocument document);

private:
    void updateGUI(Page preferredPage);
    void updateButtons();
    void updateSignatures(const std::vector<pdf::PDFSignatureVerificationResult>& signatures);

    void onPageButtonClicked();
    void onOutlineItemClicked(const QModelIndex& index);
    void onThumbnailsSizeChanged(int size);
    void onAttachmentCustomContextMenuRequested(const QPoint& pos);
    void onThumbnailClicked(const QModelIndex& index);
    void onSignatureCustomContextMenuRequested(const QPoint &pos);
    void onOutlineTreeViewContextMenuRequested(const QPoint &pos);
    void onOutlineItemsChanged();

    struct PageInfo
    {
        QToolButton* button = nullptr;
        QWidget* page = nullptr;
    };

    Ui::PDFSidebarWidget* ui;
    pdf::PDFDrawWidgetProxy* m_proxy;
    PDFTextToSpeech* m_textToSpeech;
    pdf::PDFCertificateStore* m_certificateStore;
    PDFBookmarkManager* m_bookmarkManager;
    PDFViewerSettings* m_settings;
    pdf::PDFOutlineTreeItemModel* m_outlineTreeModel;
    pdf::PDFThumbnailsItemModel* m_thumbnailsModel;
    pdf::PDFOptionalContentTreeItemModel* m_optionalContentTreeModel;
    PDFBookmarkItemModel* m_bookmarkItemModel;
    const pdf::PDFDocument* m_document;
    pdf::PDFOptionalContentActivity* m_optionalContentActivity;
    pdf::PDFAttachmentsTreeItemModel* m_attachmentsTreeModel;
    std::map<Page, PageInfo> m_pageInfo;
    std::vector<pdf::PDFSignatureVerificationResult> m_signatures;
    std::vector<pdf::PDFCertificateInfo> m_certificateInfos;
};

}   // namespace pdfviewer

#endif // PDFSIDEBARWIDGET_H
