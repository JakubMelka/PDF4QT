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

#ifndef PDFAPPDOCUMENTPAGE_H
#define PDFAPPDOCUMENTPAGE_H

#include "pdfappglobal.h"
#include "pdfapppage.h"

#include <QPointer>
#include <QTimer>

namespace pdf
{

class PDFAppDocumentTab;

/// Route adapter of the document view (/document/{id}?page=&panel=&q=, step 3 plan
/// 1.1). The page itself shows nothing - its tab shows the persistent view, while
/// this page is the page of the tab. setRoute applies the route to the view: the
/// page (physical, from 1; out of range = ignored, the document opens), the panel
/// of the sidebar and the search text (package D). A route of another document or
/// a page outside of a document tab is refused (Page Not Found).
///
/// A route given while the document is being read (the opening of a file, the
/// password state) is kept and applied, when the document is shown. The page
/// writes the current page, panel and search text back by replaceRoute (debounced
/// ROUTE_DELAY), so Back / Forward and a copied link return to the same place.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentPage : public PDFAppPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPage;

public:
    explicit PDFAppDocumentPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppDocumentPage() override;

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual bool hasSearchHeader() const override;

    /// Delay of the writing of the route
    static constexpr int ROUTE_DELAY = 300;

    /// Tab of the page (nullptr, when the page is not in a document tab)
    PDFAppDocumentTab* getDocumentTab() const;

    /// Route of the current state of the view (page, panel, search text)
    PDFAppRoute getCurrentRoute() const;

    /// Writes the current state of the view into the route now (the debounced write)
    void updateRoute();

private:
    void applyRoute();
    void onDocumentStateChanged();
    void onViewChanged();

    QPointer<PDFAppDocumentTab> m_tab;
    QPointer<QTimer> m_routeTimer;
    bool m_isRoutePending = false;  ///< The route waits for the document to be shown
    bool m_isApplying = false;      ///< The route is being applied (no write back)
};

/// Factory of the document page (route /document/{documentId})
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTPAGE_H
