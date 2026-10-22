// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#ifndef PDFAPPDOCUMENTPANELS_OUTLINE_H
#define PDFAPPDOCUMENTPANELS_OUTLINE_H

#include "pdfappglobal.h"
#include "pdfappdocumentpanel.h"

#include "pdfdocument.h"
#include "pdffluentwidgets.h"

#include <QPointer>
#include <QSortFilterProxyModel>

#include <vector>

class QStandardItem;
class QStandardItemModel;

namespace pdf
{

class PDFLineEdit;
class PDFTreeView;

/// Filter of the outline: an item matching the text (without case) is shown with
/// its parents (recursive filtering)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOutlineFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

private:
    using BaseClass = QSortFilterProxyModel;

public:
    explicit PDFAppOutlineFilterModel(QObject* parent);
    virtual ~PDFAppOutlineFilterModel() override;
};

/// Outline panel (pdf4qt-document-outline 195:1085), read only in step 3 (the
/// editing and the button outline-new 195:1485 are step 4): the header "Outline /
/// 86 items", the field "Search outline…" and the tree of PDFTreeView with the
/// preset Outline. A click (Enter) goes to the destination of the item; the item
/// of the current page is selected (the item with the greatest page not after
/// the current page, the deepest of equal ones), its parents are expanded.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOutlinePanel : public PDFAppDocumentPanel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanel;

public:
    /// Role of the page of an item (index from 0, -1 = no page in the document)
    static constexpr int PageRole = Qt::UserRole + 1;

    /// Most items read from an outline (the rest is left out)
    static constexpr int MAXIMUM_ITEMS = 100000;

    explicit PDFAppOutlinePanel(QWidget* parent);
    virtual ~PDFAppOutlinePanel() override;

    virtual QString getId() const override;
    virtual QString getTitle() const override;
    virtual QString getSubtitle() const override;
    virtual QString getIconName() const override;
    virtual void setDocument(PDFAppDocument* document, PDFAppDocumentView* view) override;

    /// Number of the items of the outline
    int getItemCount() const { return int(m_items.size()); }

    /// Filters the items by the text
    void setFilterText(const QString& text);

    /// Index (of the filter model of the view) of the item selected for the
    /// current page, an invalid index if none
    QModelIndex getCurrentItem() const;

    /// Activates the item (of the filter model of the view): goes to its page
    void activateItem(const QModelIndex& index);

    PDFTreeView* getTreeView() const { return m_tree; }
    PDFLineEdit* getSearchField() const { return m_searchField; }
    QAbstractItemModel* getViewModel() const;

    /// The outline of the document has items
    static bool hasOutline(const PDFDocument* document);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    struct Item
    {
        QStandardItem* item = nullptr;
        PDFInteger page = -1;
    };

    void onDocumentChanged(const PDFModifiedDocument& modified);
    void onDocumentDestroyed();
    void onCurrentPageChanged(PDFInteger pageIndex);
    void onSearchTextChanged(const QString& text);
    void onItemClicked(const QModelIndex& index);
    void rebuild();
    void selectCurrentItem();

    QPointer<PDFLineEdit> m_searchField;
    QPointer<PDFTreeView> m_tree;
    QPointer<QStandardItemModel> m_model;
    QPointer<PDFAppOutlineFilterModel> m_filterModel;
    QPointer<PDFAppDocument> m_connectedDocument;
    QPointer<PDFAppDocumentView> m_connectedView;
    QSharedPointer<PDFOutlineItem> m_outlineRoot;   ///< Keeps the items of the actions alive
    PDFDocumentPointer m_document;
    std::vector<Item> m_items;                      ///< Items in the order of the document (pre-order)
    bool m_isSelecting = false;
};

/// Factory of the outline panel (the tab only for a document with an outline)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOutlinePanelFactory : public PDFAppDocumentPanelFactory
{
public:
    virtual QString getId() const override;
    virtual int getOrder() const override;
    virtual QString getTitle() const override;
    virtual QString getIconName() const override;
    virtual bool isAvailable(const PDFAppDocument* document) const override;
    virtual PDFAppDocumentPanel* createPanel(QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTPANELS_OUTLINE_H
