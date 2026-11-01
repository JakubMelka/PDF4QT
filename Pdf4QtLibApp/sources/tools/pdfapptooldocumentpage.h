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

#ifndef PDFAPPTOOLDOCUMENTPAGE_H
#define PDFAPPTOOLDOCUMENTPAGE_H

#include "pdfappdocumentpage.h"
#include "pdfdocumenttoolshell.h"

#include <QPointer>
#include <QString>

namespace pdf
{

class PDFAppToolHost;

/// Route adapter of a tool in the tab of a document (/document/{id}/tool/{toolId}, plan
/// of the first batch, chapter 2.1): a document page, so the tab shows it with its
/// persistent view. Its setRoute enters the shell of the tool, the shell ends with
/// the page: a navigation first asks the shell (prepareLeave, F14), Cancel and an accepted
/// result ask for the route of the document themselves (F15).
///
/// The route is refused (Page Not Found, T3): another document, a tool, which is not
/// available for a locked document, a tool working without a document. A document
/// shown later (the opening of a file) is checked again, then the tab goes to the
/// document with a status message; a display version of an XFA form the same (F5).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolDocumentPage : public PDFAppDocumentPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPage;

public:
    /// \param context Context of the application
    /// \param toolId Tool of the page
    /// \param parent The tab
    explicit PDFAppToolDocumentPage(PDFAppContext* context, const QString& toolId, QWidget* parent = nullptr);
    virtual ~PDFAppToolDocumentPage() override;

    const QString& getToolId() const { return m_toolId; }

    /// Shell of the tool, nullptr until the document is shown and after its end
    PDFDocumentToolShell* getShell() const { return m_shell.data(); }

    virtual QString getTitle() const override;
    virtual QString getHelpTopic() const override;
    virtual bool prepareLeave(const PDFAppRoute& target) override;

    /// The route of the tool: the view is not a part of it
    virtual PDFAppRoute getCurrentRoute() const override;

protected:
    virtual bool isRouteOfPage(const PDFAppRoute& route) const override;
    virtual void applyRoute() override;

private:
    /// The reason, why the tool cannot run on the document now, empty = none
    QString getRefusalText() const;

    /// Leaves the tool for the document (setRoute runs in a navigation: the navigator does
    /// the route, which this asks for, after it)
    void leaveToDocument(const QString& message);
    void onCloseRequested(const pdf::PDFAppToolCloseRequest& request);

    QString m_toolId;
    QPointer<PDFAppToolHost> m_host;
    QPointer<PDFDocumentToolShell> m_shell;
};

/// Factory of the page of a tool in a document tab
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolDocumentPageFactory : public PDFAppPageFactory
{
public:
    /// The tool depends on the route: nullptr
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;

    /// The page of the tool of the route; nullptr (Page Not Found) without the
    /// implementation or the GUI of the tool
    virtual PDFAppPage* createPageForRoute(PDFAppContext* context, QWidget* parent, const PDFAppRoute& route) const override;
};

}   // namespace pdf

#endif // PDFAPPTOOLDOCUMENTPAGE_H
