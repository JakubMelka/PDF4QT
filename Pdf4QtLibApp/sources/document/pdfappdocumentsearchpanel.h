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

#ifndef PDFAPPDOCUMENTSEARCHPANEL_H
#define PDFAPPDOCUMENTSEARCHPANEL_H

#include "pdfappglobal.h"
#include "pdfappdocumentpanel.h"

#include <QPointer>

class QPushButton;
class QToolButton;
class QModelIndex;

namespace pdf
{

class PDFAppDocumentSearchController;
class PDFAppHeaderText;
class PDFLabel;
class PDFListView;
class PDFSearchResultDelegate;

/// Panel of the search results (Figma 198:4520): its own header (title "Search
/// results", the subtitle "24 results for “revenue”", previous / next / close),
/// the options Match case, Whole words, Regex and Wildcards (option chips), the
/// virtualized list of the occurrences (PDFSearchResultDelegate) and a note,
/// when the results reach the safety limit. The panel has no tab of the sidebar,
/// the search field of the toolbar shows it. The footer "Mark as annotations"
/// (PDFAppMarkResultsFooter, step 4 package A) is shown with the results.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSearchPanel : public PDFAppDocumentPanel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanel;

public:
    /// Width of an option chip (198:4934)
    static constexpr int OPTION_WIDTH = 134;

    explicit PDFAppDocumentSearchPanel(QWidget* parent = nullptr);
    virtual ~PDFAppDocumentSearchPanel() override;

    virtual QString getId() const override;
    virtual QString getTitle() const override;
    virtual QString getSubtitle() const override;
    virtual QString getIconName() const override;
    virtual bool hasOwnHeader() const override;
    virtual void setDocument(PDFAppDocument* document, PDFAppDocumentView* view) override;

    /// Parts of the panel (tests)
    PDFListView* getListView() const;
    QPushButton* getOptionButton(int index) const;
    QToolButton* getPreviousButton() const;
    QToolButton* getNextButton() const;
    QToolButton* getCloseButton() const;
    PDFLabel* getMessageLabel() const;
    PDFLabel* getNoteLabel() const;

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void updateState();
    void updateOptions();
    void onOptionToggled(bool checked);
    void onPreviousClicked();
    void onNextClicked();
    void onCloseClicked();
    void onCurrentRowChanged(const QModelIndex& current, const QModelIndex& previous);
    void onCurrentIndexChanged(int index);

    QPointer<PDFAppDocumentSearchController> m_controller;
    QPointer<PDFAppHeaderText> m_title;
    QPointer<PDFAppHeaderText> m_subtitle;
    QPointer<QToolButton> m_previousButton;
    QPointer<QToolButton> m_nextButton;
    QPointer<QToolButton> m_closeButton;
    QPointer<QPushButton> m_matchCaseButton;
    QPointer<QPushButton> m_wholeWordsButton;
    QPointer<QPushButton> m_regexButton;
    QPointer<QPushButton> m_wildcardsButton;
    QPointer<PDFLabel> m_messageLabel;
    QPointer<PDFListView> m_listView;
    QPointer<PDFSearchResultDelegate> m_delegate;
    QPointer<PDFLabel> m_noteLabel;
    bool m_isUpdating = false;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTSEARCHPANEL_H
