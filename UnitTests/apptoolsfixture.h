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

#ifndef APPTOOLSFIXTURE_H
#define APPTOOLSFIXTURE_H

#include "appdocumentfixture.h"

#include "pdfapppage.h"
#include "pdfapptoolpageregistry.h"
#include "pdftoolcollectionrepository.h"
#include "pdftoolrepository.h"

#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>
#include <utility>
#include <vector>

namespace pdf
{
class PDFAppDocumentTab;
class PDFAppTab;
class PDFCollectionRow;
class PDFPageNotFoundPage;
class PDFToolRow;
}

namespace pdftest
{

/// Page of a tool registered by the tests in PDFAppToolPageRegistry (the page path
/// catalog -> tool page of phase 2): it shows nothing, it remembers its route
class AppToolsTestPage : public pdf::PDFAppPage
{
    Q_OBJECT

public:
    explicit AppToolsTestPage(pdf::PDFAppContext* context, QWidget* parent = nullptr);

    virtual bool setRoute(const pdf::PDFAppRoute& route) override;
    virtual QString getTitle() const override;

    /// Tool of the route (the parameter toolId)
    const QString& getToolId() const { return m_toolId; }

private:
    QString m_toolId;
};

/// Factory of AppToolsTestPage
class AppToolsTestPageFactory : public pdf::PDFAppPageFactory
{
public:
    virtual pdf::PDFAppPage* createPage(pdf::PDFAppContext* context, QWidget* parent) const override;
};

/// Fixture of the tests of the Tools catalog (step 6): the fixture of the document
/// tab (a portable memory database, the real window shown offscreen, a temporary
/// folder) with the helpers of the tools. Every function, which changes something,
/// processes the events afterwards (the deferred deletions too). The destructor
/// removes the test pages registered by registerTestToolPage.
class AppToolsFixture : public AppDocumentFixture
{
public:
    explicit AppToolsFixture(QSize size = QSize(1440, 960));
    ~AppToolsFixture();

    /// Navigates to the route (Push) in the Home tab
    void navigate(const QString& route);

    /// Opens the route (/tools, /tools?category=…, /tools/collections/…) and
    /// returns the shown page (nullptr, when the window has no page)
    pdf::PDFAppPage* openTools(const QString& route = QStringLiteral("/tools"));

    /// Current tab and its page
    pdf::PDFAppTab* currentTab() const;
    pdf::PDFAppPage* currentPage() const;

    /// Current page as the type (nullptr for another page)
    template<typename T>
    T* page() const { return qobject_cast<T*>(currentPage()); }

    /// Current page as Page Not Found (nullptr for another page)
    pdf::PDFPageNotFoundPage* notFoundPage() const;

    /// Route of the current entry of the history of the current tab
    QString currentRoute() const;

    /// Widget of the tool in the current page (object name "tool-<id>",
    /// PDFAppToolPresentation), nullptr = none
    QWidget* findToolWidget(const QString& toolId) const;

    /// Tool row of the tool in the current page, nullptr = none
    pdf::PDFToolRow* findToolRow(const QString& toolId) const;

    /// Collection row of the collection (its route id) in the current page, nullptr = none
    pdf::PDFCollectionRow* findCollectionRow(const QString& routeId) const;

    /// Repositories of the context
    pdf::PDFToolRepository tools() const;
    pdf::PDFToolCollectionRepository collections() const;

    /// Runs the write by another connection (a thread of its own, as another
    /// instance of PDF4QT) and waits until the notifier of this connection has
    /// delivered a change of the topic. \returns false, when the write failed or no
    /// notification came.
    bool writeFromOtherConnection(const std::function<pdf::PDFDatabaseError(pdf::PDFDatabase*)>& write, const QString& topic);

    /// Pins / unpins the tool by another connection
    bool pinFromOtherConnection(const QString& toolId, bool isPinned = true);

    /// Records a use of the tool by another connection (\p time = its last use)
    bool recordUsageFromOtherConnection(const QString& toolId, qint64 time);

    /// Saves the collection by another connection (\p id = the row id of a new one)
    bool writeCollectionFromOtherConnection(const pdf::PDFToolCollectionDraft& draft, qint64* id = nullptr);

    /// Registers AppToolsTestPage as the page of the tool (removed by the destructor)
    bool registerTestToolPage(const QString& toolId, pdf::PDFAppToolPageKind kind = pdf::PDFAppToolPageKind::Start);

    /// Opens a document of \p pageCount pages
    pdf::PDFAppDocumentTab* openDocument(int pageCount = 3);

    /// Picture of the window (PDF4QT_APP_SCREENSHOTS=<folder>: written as <name>.png
    /// and at the device pixel ratio 2 with the open popups and dialogs as
    /// <name>-2x.png); a null picture without the variable
    QImage screenshot(const QString& name) const;

private:
    std::vector<std::pair<pdf::PDFAppToolPageKind, QString>> m_registeredPages;
};

}   // namespace pdftest

#endif // APPTOOLSFIXTURE_H
