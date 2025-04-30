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
                          const pdf::PDFColorConvertor& convertor,
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
