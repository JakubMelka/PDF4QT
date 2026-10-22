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

#ifndef PDFAPPDOCUMENTSEARCH_H
#define PDFAPPDOCUMENTSEARCH_H

#include "pdfappdocumentcommands.h"

#include <QPointer>
#include <QString>

class QLineEdit;

namespace pdf
{

class PDFAppDocumentSearchController;
class PDFAppDocumentSignatureVerifier;
class PDFAppDocumentSpeechController;
class PDFAppDocumentTextIndexer;

/// Commands and services of package D of the document tab (search, signatures,
/// speech): Find in document (Ctrl+F - the search field of the toolbar), Find next
/// (F3), Find previous (Shift+F3) and Read aloud. The handler owns the services of
/// the tab, which live as long as the tab: the text index (PDFAppDocumentTextIndexer),
/// the search (PDFAppDocumentSearchController), the verification of the signatures
/// (PDFAppDocumentSignatureVerifier, started when the document is shown) and the
/// speech (PDFAppDocumentSpeechController, created at its first use). The panels
/// of the package find them by findHandler.
///
/// The search field of the toolbar (PDFAppDocumentToolbar::getSearchField)
/// starts the search by Enter, Esc closes the search; the route /document/{id}?q=
/// is applied by applyRouteQuery and written back from getRouteQuery.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSearchCommandHandler : public PDFAppDocumentCommandHandler
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentCommandHandler;

public:
    explicit PDFAppDocumentSearchCommandHandler(PDFAppDocumentTab* tab);
    virtual ~PDFAppDocumentSearchCommandHandler() override;

    virtual void execute(const QString& commandId) override;
    virtual bool isEnabled(const QString& commandId) const override;
    virtual bool isChecked(const QString& commandId) const override;

    /// Services of the tab
    PDFAppDocumentTextIndexer* getIndexer() const;
    PDFAppDocumentSearchController* getSearchController() const;
    PDFAppDocumentSignatureVerifier* getSignatureVerifier() const;

    /// Speech of the tab; it is created at the first call with \p create
    PDFAppDocumentSpeechController* getSpeechController(bool create);

    /// The search field of the toolbar (nullptr, when the toolbar has none)
    QLineEdit* getSearchField() const;

    /// Searches the text: the field shows it, the panel of the results is shown
    void startSearch(const QString& text);

    /// Closes the search: no results, no highlights, the panel shown before the
    /// search is shown again
    void closeSearch();

    /// Text of the search for the route (?q=), empty = no search
    QString getSearchText() const;

    /// Applies ?q= of the route of the tab (an empty text does nothing)
    static void applyRouteQuery(PDFAppDocumentTab* tab, const QString& query);

    /// Returns the value of ?q= of the tab (empty = no search)
    static QString getRouteQuery(const PDFAppDocumentTab* tab);

    /// Handler of the tab, which contains the object (a panel, the view, the
    /// document of the tab); nullptr outside of a document tab
    static PDFAppDocumentSearchCommandHandler* findHandler(const QObject* object);

Q_SIGNALS:
    /// The text of the search has changed (the route writes ?q=)
    void searchTextChanged();

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onFieldReturnPressed();
    void onPanelChanged(const QString& panelId);
    void onSearchStateChanged();
    void onSpeechStateChanged();
    void connectSearchField();

    QPointer<PDFAppDocumentTextIndexer> m_indexer;
    QPointer<PDFAppDocumentSearchController> m_searchController;
    QPointer<PDFAppDocumentSignatureVerifier> m_signatureVerifier;
    QPointer<PDFAppDocumentSpeechController> m_speechController;
    QPointer<QLineEdit> m_searchField;
    QString m_previousPanelId;
    QString m_searchText;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTSEARCH_H
