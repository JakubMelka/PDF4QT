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

#ifndef PDFAPPDOCUMENTPROPERTIES_H
#define PDFAPPDOCUMENTPROPERTIES_H

#include "pdfappdocumentcommands.h"
#include "pdfapppage.h"

#include <QPointer>

namespace pdf
{

class PDFAppDeveloperOverlay;
class PDFAppDocumentActionHandler;
class PDFAppRenderingIssues;

/// Command handler of package F of a document tab: file.properties (the page
/// Document properties, /document/{id}/properties/general) and
/// view.renderingIssues (the popover of the rendering issues). It owns the parts
/// of the package, which live with the tab: the links and actions of the
/// documents (PDFAppDocumentActionHandler), the rendering issues with their item
/// of the status bar (PDFAppRenderingIssues) and the developer view
/// (PDFAppDeveloperOverlay).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentPropertiesCommandHandler : public PDFAppDocumentCommandHandler
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentCommandHandler;

public:
    explicit PDFAppDocumentPropertiesCommandHandler(PDFAppDocumentTab* tab);
    virtual ~PDFAppDocumentPropertiesCommandHandler() override;

    virtual void execute(const QString& commandId) override;
    virtual bool isEnabled(const QString& commandId) const override;

    PDFAppDocumentActionHandler* getActionHandler() const;
    PDFAppRenderingIssues* getRenderingIssues() const;
    PDFAppDeveloperOverlay* getDeveloperOverlay() const;

    /// Shows the page Document properties of the section (general, security, ...)
    void showProperties(const QString& section);

    /// Shows the rendering issues: the view of the tab first (from the properties),
    /// then the popover above the item of the status bar
    void showRenderingIssues();

private:
    void onIssuesChanged();
    void onDeferredRenderingIssues();

    QPointer<PDFAppDocumentActionHandler> m_actionHandler;
    QPointer<PDFAppRenderingIssues> m_renderingIssues;
    QPointer<PDFAppDeveloperOverlay> m_developerOverlay;
};

/// Factory of the page Document properties (/document/{documentId}/properties/{section})
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentPropertiesPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTPROPERTIES_H
