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

#ifndef APPTOOLOWNTABFIXTURE_H
#define APPTOOLOWNTABFIXTURE_H

#include "apptoolshellfixture.h"

#include "pdfapptooltab.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttoolgui.h"

#include <QCoreApplication>
#include <QPointer>
#include <QString>

#include <atomic>
#include <memory>

class QLabel;

namespace pdftest
{

/// Test tool of the own tab of a tool (step 2 of phase 2, package B1): it takes the id of a
/// tool of the catalog with its own tab and the result NewDocument - pages.merge (inputs
/// Multiple) or convert.blank (no input). It is registered only by the tests.
///   mode = new     - a new document with a page of the size of the first page of each input
///                    page (A4 portrait without an input: "pages" pages); the facts Pages
///   mode = partial - as new, the last input is left out (leftOutInputs: its name, its pages
///                    and the reason "The file cannot be read: test")
///   mode = fail    - Failed
/// setWrittenPages(n > 0): the output carries writtenData of another document with n pages
/// (the caller must open these bytes and never write the document again, plan 4.5). The
/// inputs are read from the context (the documents in their order). setHold keeps execute at
/// its start until it is released or the run is canceled.
class OwnTabTestTool : public pdf::PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdftest::OwnTabTestTool)

public:
    explicit OwnTabTestTool(QString id) : m_id(std::move(id)) { }

    QString getId() const override { return m_id; }
    pdf::PDFDocumentToolParameterDefinitions getParameters() const override;
    pdf::PDFDocumentToolResult execute(const pdf::PDFDocumentToolContext& context, const QVariantMap& parameters, const pdf::PDFOperationControl* control, pdf::PDFProgressSink* progress) const override;
    bool hasPreview() const override { return m_hasPreview.load(); }
    pdf::PDFDocumentToolResult computePreview(const pdf::PDFDocumentToolContext& context, const QVariantMap& parameters, const pdf::PDFOperationControl* control, pdf::PDFProgressSink* progress) const override;

    void setHold(bool hold) { m_hold.store(hold); }
    int getHeldCount() const { return m_heldCount.load(); }
    int getExecuteCount() const { return m_executeCount.load(); }
    int getPreviewCount() const { return m_previewCount.load(); }
    void setWrittenPages(int pageCount) { m_writtenPages.store(pageCount); }
    void setPreview(bool hasPreview) { m_hasPreview.store(hasPreview); }

    /// Number of the input documents of the last run or preview
    int getLastInputCount() const { return m_lastInputCount.load(); }

    /// A document of \p pageCount A4 pages
    static pdf::PDFDocumentPointer createDocument(int pageCount);

private:
    bool waitWhileHeld(const pdf::PDFOperationControl* control) const;

    QString m_id;
    std::atomic_bool m_hold = false;
    std::atomic_bool m_hasPreview = false;
    std::atomic_int m_writtenPages = 0;
    mutable std::atomic_int m_heldCount = 0;
    mutable std::atomic_int m_executeCount = 0;
    mutable std::atomic_int m_previewCount = 0;
    mutable std::atomic_int m_lastInputCount = 0;
};

/// GUI of the test tool in its own tab: every part (an action, the left, right and canvas
/// parts as plain widgets), the inputs of the list of the tab in the environment (R16), the
/// texts of the own tab (title, loss text of the list K16, status items)
class OwnTabTestGUI : public pdf::PDFDocumentToolGUI
{
    Q_OBJECT

public:
    /// \param isDialogSkipped The result opens without the dialog (Blank PDF, 4.6)
    /// \param title Title of the own tab, empty = the default (the catalog)
    explicit OwnTabTestGUI(bool isDialogSkipped, QString title);

    virtual QWidget* createToolActions(QWidget* parent) override;
    virtual QWidget* createRightPanel(QWidget* parent) override;
    virtual QWidget* createLeftPanel(QWidget* parent) override;
    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getConfirmText() const override;
    virtual void prepareEnvironment(pdf::PDFAppToolRunEnvironment* environment) const override;
    virtual bool isResultDialogSkipped() const override { return m_isDialogSkipped; }
    virtual QString getOwnTabTitle() const override;
    virtual QString getOwnTabIconName() const override;
    virtual QString getOwnTabLossText() const override;
    virtual QString getOwnTabPageText() const override;

    QPointer<QLabel> rightLabel;
    QPointer<QWidget> canvasPage;
    int notAcceptedCount = 0;

    virtual void onRunNotAccepted(const pdf::PDFDocumentToolResult& result) override;

private:
    bool m_isDialogSkipped = false;
    QString m_title;
};

/// Factory of OwnTabTestGUI
class OwnTabTestGUIFactory : public pdf::PDFDocumentToolGUIFactory
{
public:
    OwnTabTestGUIFactory(bool isDialogSkipped, QString title) : m_isDialogSkipped(isDialogSkipped), m_title(std::move(title)) { }

    virtual pdf::PDFDocumentToolGUI* createGUI() const override { return new OwnTabTestGUI(m_isDialogSkipped, m_title); }

private:
    bool m_isDialogSkipped = false;
    QString m_title;
};

/// Fixture of the own tab of a tool: the shell fixture with the test tool of the own tab
class AppToolOwnTabFixture : public AppToolShellFixture
{
public:
    explicit AppToolOwnTabFixture(QSize size = QSize(1440, 960));
    ~AppToolOwnTabFixture();

    /// Registers the test tool of the own tab under \p toolId (pages.merge, convert.blank);
    /// what was registered under the id comes back with the destructor
    std::shared_ptr<OwnTabTestTool> registerOwnTabTool(const QString& toolId, bool isDialogSkipped = false, const QString& title = QString());

    /// PDFAppMainWindow::createToolTab and the settled event loop
    pdf::PDFAppToolTab* createToolTab(const QString& toolId, pdf::PDFAppToolInputList* inputList = nullptr);

    /// The shell and the GUI of the own tab, nullptr = none (the shell of a document tab: the base)
    using AppToolShellFixture::getShell;
    static pdf::PDFDocumentToolShell* getShell(const pdf::PDFAppToolTab* tab);
    static OwnTabTestGUI* getOwnTabGUI(const pdf::PDFAppToolTab* tab);

private:
    struct RegisteredTool
    {
        QString id;
        std::shared_ptr<const pdf::PDFDocumentTool> previousTool;
        std::shared_ptr<const pdf::PDFDocumentToolGUIFactory> previousFactory;
    };

    std::vector<RegisteredTool> m_registeredTools;
};

}   // namespace pdftest

#endif // APPTOOLOWNTABFIXTURE_H
