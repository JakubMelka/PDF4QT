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


#ifndef PDFSIDEBARWIDGET_H
#define PDFSIDEBARWIDGET_H

#include <QWidget>

class QPushButton;
class QWidget;

namespace Ui
{
class PDFSidebarWidget;
}

namespace pdf
{
class PDFDocument;
class PDFOutlineTreeItemModel;
class PDFOptionalContentActivity;
class PDFOptionalContentTreeItemModel;
}

namespace pdfviewer
{

class PDFSidebarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PDFSidebarWidget(QWidget* parent = nullptr);
    virtual ~PDFSidebarWidget() override;

    enum Page
    {
        Invalid,
        _BEGIN,
        OptionalContent = _BEGIN,
        Bookmarks,
        Thumbnails,
        _END
    };

    void setDocument(const pdf::PDFDocument* document, pdf::PDFOptionalContentActivity* optionalContentActivity);

    /// Returns true, if all items in sidebar are empty
    bool isEmpty() const;

    /// Returns true, if page is empty
    bool isEmpty(Page page) const;

    /// Selects current page
    void selectPage(Page page);

    /// Returns list of valid pages (nonempty pages)
    std::vector<Page> getValidPages() const;

private:
    void updateGUI(Page preferredPage);
    void updateButtons();

    struct PageInfo
    {
        QPushButton* button = nullptr;
        QWidget* page = nullptr;
    };

    Ui::PDFSidebarWidget* ui;
    pdf::PDFOutlineTreeItemModel* m_outlineTreeModel;
    pdf::PDFOptionalContentTreeItemModel* m_optionalContentTreeModel;
    const pdf::PDFDocument* m_document;
    pdf::PDFOptionalContentActivity* m_optionalContentActivity;
    std::map<Page, PageInfo> m_pageInfo;
};

}   // namespace pdfviewer

#endif // PDFSIDEBARWIDGET_H
