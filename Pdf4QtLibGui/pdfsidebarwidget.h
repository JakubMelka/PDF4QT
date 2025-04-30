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

#ifndef PDFSIDEBARWIDGET_H
#define PDFSIDEBARWIDGET_H

#include "pdfglobal.h"
#include "pdfbookmarkmanager.h"

#include <QWidget>

class QPushButton;
class QToolButton;
class QWidget;
class QStandardItemModel;
class QSortFilterProxyModel;

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
        Notes,
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
    void updateNotes();

    void onOutlineSearchText();
    void onNotesSearchText();
    void onPageButtonClicked();
    void onOutlineItemClicked(const QModelIndex& index);
    void onThumbnailsSizeChanged(int size);
    void onAttachmentCustomContextMenuRequested(const QPoint& pos);
    void onThumbnailClicked(const QModelIndex& index);
    void onSignatureCustomContextMenuRequested(const QPoint& pos);
    void onOutlineTreeViewContextMenuRequested(const QPoint& pos);
    void onNotesTreeViewContextMenuRequested(const QPoint& pos);
    void onOutlineItemsChanged();
    void onBookmarkActivated(int index, PDFBookmarkManager::Bookmark bookmark);
    void onBookmarsCurrentIndexChanged(const QModelIndex& current, const QModelIndex& previous);
    void onBookmarkClicked(const QModelIndex& index);
    void onNotesItemClicked(const QModelIndex& index);

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
    QSortFilterProxyModel* m_outlineSortProxyTreeModel;
    pdf::PDFThumbnailsItemModel* m_thumbnailsModel;
    pdf::PDFOptionalContentTreeItemModel* m_optionalContentTreeModel;
    PDFBookmarkItemModel* m_bookmarkItemModel;
    QStandardItemModel* m_notesTreeModel;
    QSortFilterProxyModel* m_notesSortProxyTreeModel;
    const pdf::PDFDocument* m_document;
    pdf::PDFOptionalContentActivity* m_optionalContentActivity;
    pdf::PDFAttachmentsTreeItemModel* m_attachmentsTreeModel;
    std::map<Page, PageInfo> m_pageInfo;
    std::vector<pdf::PDFSignatureVerificationResult> m_signatures;
    std::vector<pdf::PDFCertificateInfo> m_certificateInfos;
    std::vector<std::pair<pdf::PDFObjectReference, pdf::PDFInteger>> m_markupAnnotations;
    bool m_bookmarkChangeInProgress = false;
};

}   // namespace pdfviewer

#endif // PDFSIDEBARWIDGET_H
