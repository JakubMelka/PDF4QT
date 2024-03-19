//    Copyright (C) 2020-2021 Jakub Melka
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

#ifndef PDFADVANCEDFINDWIDGET_H
#define PDFADVANCEDFINDWIDGET_H

#include "pdfglobal.h"
#include "pdfdrawspacecontroller.h"
#include "pdftextlayout.h"

#include <QWidget>

namespace Ui
{
class PDFAdvancedFindWidget;
}

namespace pdf
{
class PDFDocument;
class PDFDrawWidgetProxy;
}

namespace pdfviewer
{

class PDFAdvancedFindWidget : public QWidget, public pdf::IDocumentDrawInterface
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAdvancedFindWidget(pdf::PDFDrawWidgetProxy* proxy, QWidget* parent = nullptr);
    virtual ~PDFAdvancedFindWidget() override;

    virtual void drawPage(QPainter* painter,
                          pdf::PDFInteger pageIndex,
                          const pdf::PDFPrecompiledPage* compiledPage,
                          pdf::PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          QList<pdf::PDFRenderError>& errors) const override;

    void setDocument(const pdf::PDFModifiedDocument& document);

    /// Returns selected text items in the table (selected means not all results,
    /// but those, which are selected rows in the result table)
    pdf::PDFTextSelection getSelectedText() const;

protected:
    virtual void showEvent(QShowEvent* event) override;
    virtual void hideEvent(QHideEvent* event) override;

private slots:
    void on_searchButton_clicked();
    void onSelectionChanged();
    void onResultItemDoubleClicked(int row, int column);

    void on_clearButton_clicked();

private:
    void updateUI();
    void updateResultsUI();
    void performSearch();

    pdf::PDFTextSelection getTextSelection() const { return m_textSelection.get(this, &PDFAdvancedFindWidget::getTextSelectionImpl); }
    pdf::PDFTextSelection getTextSelectionImpl() const;

    struct SearchParameters
    {
        QString phrase;
        bool isCaseSensitive = false;
        bool isWholeWordsOnly = false;
        bool isRegularExpression = false;
        bool isDotMatchingEverything = false;
        bool isMultiline = false;
        bool isSearchFinished = false;
        bool isSoftHyphenRemoved = false;
    };

    Ui::PDFAdvancedFindWidget* ui;

    pdf::PDFDrawWidgetProxy* m_proxy;
    const pdf::PDFDocument* m_document;
    SearchParameters m_parameters;
    pdf::PDFFindResults m_findResults;
    mutable pdf::PDFCachedItem<pdf::PDFTextSelection> m_textSelection;
};

}   // namespace pdfviewer

#endif // PDFADVANCEDFINDWIDGET_H
