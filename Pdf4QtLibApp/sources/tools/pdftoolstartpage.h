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

#ifndef PDFTOOLSTARTPAGE_H
#define PDFTOOLSTARTPAGE_H

#include "pdfappglobal.h"
#include "pdfapppage.h"
#include "pdfapppagelayout.h"
#include "pdfappmarkdownview.h"
#include "pdfapptooldocumentchooser.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"

#include <QPointer>
#include <QPushButton>

namespace pdf
{

class PDFAppToolInputList;
class PDFDocumentToolCatalogEntry;
class PDFTextLink;
enum class PDFAppToolStartPageText;

/// Start page of a tool (/tools/{toolId}, Figma 276:1737): it is built from the catalog,
/// from the help topic of the tool and from the suggestions, so every tool with a GUI has
/// it without code: choose a document (PDFAppToolDocumentChooser), the manual of the
/// tool, the tutorial (disabled without a real video with its place kept, owner answer
/// Q5), at a glance and the link to the manual. A chosen document opens the tool in the tab of
/// the document; a file opened or dropped on this page opens it too. Rebuilt at a change
/// of the language with the scroll position and the focus kept.
///
/// A tool with more inputs (Merge, 531:3208, plan of step 2 of phase 2, 4.3) has the chooser
/// of the mode Multiple instead (PDFAppToolInputChooser over the PDFAppToolInputList of the
/// page): the files dropped anywhere on the page join the list (PDF files and images, K8),
/// Open Merge with … hands the list over to the own tab of the tool
/// (PDFAppMainWindow::createToolTab, R13) and the page gets a new empty list. A tool without
/// an input (Blank PDF, Q10 / R21, 1392:4528) has the Primary Open … in the place of the
/// chooser, which opens its own tab, with the texts of its GUI (getStartPageText). The list
/// is a child of the page (it survives a rebuild).
class PDF4QTLIBAPPSHARED_EXPORT PDFToolStartPage : public PDFAppContentPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppContentPage;

public:
    /// \param context Context of the application
    /// \param toolId Id of a tool of the catalog
    /// \param parent Parent widget
    explicit PDFToolStartPage(PDFAppContext* context, const QString& toolId, QWidget* parent = nullptr);
    virtual ~PDFToolStartPage() override;

    const QString& getToolId() const { return m_toolId; }

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual QString getHeaderTitle() const override;
    virtual QString getHeaderSubtitle() const override;
    virtual QList<PDFAppBreadcrumbItem> getHeaderBreadcrumbs() const override;
    virtual PDFAppHeaderIcon getHeaderIcon() const override;
    virtual QString getHeaderTag() const override;
    virtual QString getHelpTopic() const override;
    virtual QString getOpenFilesToolId() const override;

    /// Parts (tests)
    PDFAppColumns* getColumns() const { return m_columns.data(); }
    PDFAppToolDocumentChooser* getChooser() const { return m_chooser.data(); }
    PDFAppToolInputChooser* getInputChooser() const { return m_inputChooser.data(); }
    PDFAppToolInputList* getInputList() const;
    QPushButton* getOpenToolButton() const { return m_openToolButton.data(); }
    PDFAppMarkdownView* getManualView() const { return m_manualView.data(); }
    PDFTintedCard* getTutorialCard() const { return m_tutorialCard.data(); }
    QPushButton* getWatchButton() const { return m_watchButton.data(); }
    PDFKeyValueList* getFacts() const { return m_facts.data(); }
    PDFAppParagraph* getOftenNextText() const { return m_oftenNext.data(); }
    PDFLinkRow* getManualLink() const { return m_manualLink.data(); }

    /// "Edit Invoice.pdf in its tab" (QB1 of step 3b, owner answer 8 of 12. 11.): a tool with the
    /// host of the tab of a document (Manage Pages, PDFDocumentToolCatalogEntry::hasDocumentHost),
    /// whose list has exactly one input and that one is an open document, offers the tool in the tab
    /// of that document - the own tab never changes its sources. Hidden otherwise.
    PDFTextLink* getEditInTabLink() const { return m_editInTabLink.data(); }

    /// The id of the open document of that link, empty = no link
    QString getEditInTabDocumentId() const;

    /// The suggestions of "Often used next" as they are shown ("Protect with Password, Merge")
    QString createOftenNextText() const;

protected:
    virtual void changeEvent(QEvent* event) override;
    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dragMoveEvent(QDragMoveEvent* event) override;
    virtual void dragLeaveEvent(QDragLeaveEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;

private:
    const PDFDocumentToolCatalogEntry* getTool() const;

    void build();
    void rebuild();
    QWidget* createChooseDocument(QWidget* parent);
    QWidget* createChooseDocuments(QWidget* parent);
    QWidget* createOpenTool(QWidget* parent);
    QString getLockedGlanceText() const;

    /// A text of the section of a tool without an input from its GUI, empty = the generic one
    QString getStartPageText(PDFAppToolStartPageText text) const;
    QWidget* createManual(QWidget* parent);
    QWidget* createTutorial(QWidget* parent);
    QWidget* createAtGlance(QWidget* parent);
    QWidget* createLinks(QWidget* parent);

    void onDocumentChosen(const QString& documentId);
    void onOpenFileRequested();
    void onOpenToolRequested();
    void setDropActive(bool isActive);
    void onMarkdownLinkActivated(const QString& link);
    void onManualLinkClicked();
    void connectInputList();
    void updateEditInTabLink();
    void onEditInTabClicked();

    QString m_toolId;
    QPointer<QWidget> m_content;
    QPointer<PDFAppColumns> m_columns;
    QPointer<PDFAppToolDocumentChooser> m_chooser;
    QPointer<PDFAppToolInputChooser> m_inputChooser;
    QPointer<PDFAppToolInputList> m_inputList;
    QPointer<QPushButton> m_openToolButton;
    QPointer<PDFAppMarkdownView> m_manualView;
    QPointer<PDFTintedCard> m_tutorialCard;
    QPointer<QPushButton> m_watchButton;
    QPointer<PDFKeyValueList> m_facts;
    QPointer<PDFAppParagraph> m_oftenNext;
    QPointer<PDFLinkRow> m_manualLink;
    QPointer<PDFTextLink> m_editInTabLink;
    bool m_isRebuildPending = false;
};

/// Factory of the start page of a tool (the tool of the route)
class PDF4QTLIBAPPSHARED_EXPORT PDFToolStartPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
    virtual PDFAppPage* createPageForRoute(PDFAppContext* context, QWidget* parent, const PDFAppRoute& route) const override;
};

}   // namespace pdf

#endif // PDFTOOLSTARTPAGE_H
