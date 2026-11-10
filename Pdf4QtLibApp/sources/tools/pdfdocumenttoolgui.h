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

#ifndef PDFDOCUMENTTOOLGUI_H
#define PDFDOCUMENTTOOLGUI_H

#include "pdfappglobal.h"
#include "pdfapptoolresultdialog.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfsecurityhandler.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <map>
#include <memory>
#include <vector>

class QBoxLayout;
class QWidget;

namespace pdf
{

class PDFAppContext;
class PDFAppDocument;
class PDFAppToolHost;
class PDFAppToolInputList;
class PDFAppToolParameterModel;
class PDFAppToolToolbar;
class PDFDocumentToolCatalogEntry;
class PDFDocumentToolShell;

/// Where the card of the progress and the callout The document is busy lie (K7):
/// the top of the right panel by default; Compress has its card inside of the section
/// Result (295:1711)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolProgressPlace
{
public:
    QBoxLayout* cardLayout = nullptr;       ///< Layout of the card, nullptr = the top of the right panel
    int cardIndex = 0;                      ///< Index of the card in the layout
    QBoxLayout* calloutLayout = nullptr;    ///< Layout of the callout, nullptr = below the card
    int calloutIndex = 0;                   ///< Index of the callout in the layout
};

/// What the list of the inputs of a tool accepts (plan of step 3 of phase 2, 9.3, K41): Merge 2
/// to 1000 PDF files and images, Interleave exactly 2 PDF files in the slots Front sides and Back
/// sides, Images to PDF 1 to 1000 images, Insert Pages its sources in the tab of the document.
/// A value; PDFAppToolInputList, the chooser and the Primary button read it (package D).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolInputRules
{
public:
    int minimum = 2;                ///< Fewest usable inputs, which the tool runs with
    int maximum = 1000;             ///< Most inputs
    bool acceptsPdf = true;         ///< A PDF file or an open document is an input
    bool acceptsImages = true;      ///< An image file or the clipboard is an input
    QStringList slotTitles;         ///< Titles of the fixed places of the inputs ("Front sides", "Back sides"); empty = a free list
};

/// A text of the section of the start page of a tool without an input, which opens the tool
/// in its own tab (Blank PDF 1392:4528, Q10)
enum class PDFAppToolStartPageText
{
    Title,      ///< Title of the section ("Start a new document")
    Hint,       ///< Hint next to the title ("Blank PDF needs no document · …")
    Card,       ///< Text of the card next to the Primary button
    Note        ///< The note below the card
};

/// What differs between the tools in the shell (plan of the first batch, chapter 2.1):
/// the shell has the flow, the toolbar, the frame of the panels, the progress, the keys.
/// A thin tool derives from PDFAppDeclarativeToolGUI and describes its panels; a tool
/// with its own canvas (Compress: before and after) overrides the functions below.
/// The parts are created once and again after a change of the language; their values
/// live in the parameter model, so a new part shows the same state. The functions run
/// in the thread of the user interface. A hook may not delete the shell.
class PDF4QTLIBAPPSHARED_EXPORT PDFDocumentToolGUI : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFDocumentToolGUI(QObject* parent = nullptr);
    virtual ~PDFDocumentToolGUI() override;

    /// Shell of the GUI: the functions below may use the getters only after it
    void attach(PDFDocumentToolShell* shell);

    PDFDocumentToolShell* getShell() const;
    PDFAppToolHost* getHost() const;
    PDFAppContext* getContext() const;
    PDFAppDocument* getDocument() const;
    PDFAppToolParameterModel* getModel() const;
    PDFDocumentToolRunner* getRunner() const;
    PDFAppToolToolbar* getToolbar() const;

    /// Entry of the catalog
    const PDFDocumentToolCatalogEntry* getTool() const;

    // ------------------------------------------------------------- parts

    /// Actions of the toolbar (tool-actions 291:1135), nullptr = none (no third divider)
    virtual QWidget* createToolActions(QWidget* parent);

    /// Content of the right panel (sidebar-right 291:1177), nullptr = no right panel
    virtual QWidget* createRightPanel(QWidget* parent);

    /// Content of the left panel, nullptr = the sidebar of the document stays
    virtual QWidget* createLeftPanel(QWidget* parent);

    /// Widget in the place of the canvas (a grid, a form), nullptr = the document stays
    virtual QWidget* createCanvasPage(QWidget* parent);

    /// Widget over the canvas (the handle of Compress), nullptr = none; it is a child
    /// of the canvas area for the time of the tool
    virtual QWidget* createOverlay(QWidget* canvasArea);

    /// Gap of the sections of the right panel: 16 of Optimize, 14 of the others (1.1)
    virtual int getRightPanelSpacing() const;

    /// Panel of the sidebar shown by the entry (Thumbnails of Compress and Structure),
    /// empty = the panel of the user
    virtual QString getInitialSidebarPanel() const;

    /// Where the progress lies; the layouts belong to the content of the right panel
    virtual PDFAppToolProgressPlace getProgressPlace() const;

    /// A tool, which has its own card, keeps it enabled: the default disables the
    /// actions and the content of both panels while the tool runs (295:1711, 438:14093)
    virtual void setContentEnabled(bool isEnabled);

    // ------------------------------------------------------------- texts

    /// Text of the Primary button (default: the title of the tool) and its icon
    /// (default: the icon of the tool)
    virtual QString getConfirmText() const;
    virtual QString getConfirmIconName() const;

    /// The Primary button works: no error of the parameters, no reported error of the input
    virtual bool isConfirmEnabled() const;

    /// Message of the status bar while the user edits: "Compress · Report.pdf · 1,592
    /// pages · 24.8 MB" (291:994)
    virtual QString getStatusText() const;

    /// The part of the status message after the name of the document: "1,592 pages" (291:994),
    /// Optimize Structure "48,210 objects" (302:4072); empty = none
    virtual QString getStatusDetailText() const;

    /// Message of the status bar during a run: "Compress: Recompressing images · 152 of
    /// 412 · press Stop or Esc to stop" (295:1711)
    virtual QString getRunningStatusText(const QString& stepText) const;

    /// Title of the card of the progress: "Compress… 37 %" (295:2151)
    virtual QString getRunningTitle(int percentage) const;

    /// Title and step line of the card of the progress (default: getRunningTitle and the step).
    /// A tool, whose step is its title, says it there (356:13079 "Reading page 640 of 1,592",
    /// no step line).
    virtual QString getRunningCardTitle(int percentage, const QString& stepText) const;
    virtual QString getRunningCardStepText(const QString& stepText) const;

    /// Line of the time left of the card (295:2155 "About 40 seconds left", 356:13079 with
    /// the note of its tool), \p seconds -1 = not known yet, empty = no line (438:14093)
    virtual QString getRunningTimeText(int seconds) const;

    /// Text of the callout The document is busy, empty = no callout (Extract Text keeps
    /// the document free; by default only a tool of the kind ChangeDocument has it, K7)
    virtual QString getBusyText() const;

    /// Refusal of a document without the permission of the tool (339:10328, owner answer
    /// C): the title, the line below it and the text of the message box
    virtual QString getPermissionTitle() const;
    virtual QString getPermissionSubtitle() const;
    virtual QString getPermissionText() const;

    /// The permissions of Table 22, which the run with the values of now needs besides those of
    /// the catalog entry (Remove Hidden Data: bit 6 for the chosen comments, attachments and
    /// signature fields). The shell asks them by PDFAppDocumentPermissions::request when the
    /// Primary button is pressed: the owner password dialog of a document, which can be unlocked,
    /// the user confirms again after the unlock (owner 2026-11-08). Default: none.
    virtual std::vector<PDFSecurityHandler::Permission> getRunPermissions() const;

    /// Texts of the result dialog ("Pages rotated", the file of an export)
    virtual void fillResultInput(PDFAppToolResultInput* input) const;

    // ------------------------------------------------------------- own tab (plan of step 2 of phase 2, 4.1, 4.6)

    /// The result needs no decision (Blank PDF, 337:1976 "there is no result dialog"): a
    /// successful run of a NewDocument result replaces the own tab of the tool by a tab of
    /// the new document, unsaved, at the same place (PDFAppMainWindow::replaceToolTab). No
    /// Then continue with (K2). Default: false, the result dialog decides.
    virtual bool isResultDialogSkipped() const;

    /// Title of the own tab of the tool (Blank PDF "New document", K1); default: the title
    /// of the tool in the catalog ("Merge")
    virtual QString getOwnTabTitle() const;

    /// Icon of the own tab (Blank PDF the icon of a file "file", 337:1976); default: the
    /// icon of the tool in the catalog
    virtual QString getOwnTabIconName() const;

    /// What is lost by the close of the own tab (K16: "The list of 3 files is lost."): a
    /// non-empty text puts the dot on the tab and the close of the tab or of the window asks
    /// with it. Default: empty, nothing is lost.
    virtual QString getOwnTabLossText() const;

    /// Value of the row Locked document of At a glance of the start page (K9; Merge "Can be
    /// merged"); empty = the text of the locked access of the catalog
    virtual QString getLockedGlanceText() const;

    /// A text of the start page of a tool without an input (Blank PDF 1392:4528); empty = the
    /// generic text of the start page. Read from a GUI made only for the text (no shell).
    virtual QString getStartPageText(PDFAppToolStartPageText text) const;

    /// Texts of the page item and of the zoom item of the status bar of the own tab (Merge
    /// "16 pages" and no zoom, K12; Blank "Page 1 / 3" and the zoom of its preview "151 %");
    /// empty = the item is hidden. Read with getStatusText (updateStatusText).
    virtual QString getOwnTabPageText() const;
    virtual QString getOwnTabZoomText() const;

    /// The list of the inputs of a tool of more inputs in its own tab (owned by the tab,
    /// PDFAppToolInputList of package B2), nullptr = none (a tool in a document tab)
    PDFAppToolInputList* getInputList() const;

    /// What the list of the inputs accepts (plan of step 3 of phase 2, K41): the minimum and the
    /// maximum by PDFDocumentToolCatalogEntry::getInputCountRange of the tool, when it takes more
    /// inputs (else, and without a shell, 2 to 1000 as today), PDF files and images, no slots.
    /// Interleave and Images to PDF override it.
    virtual PDFAppToolInputRules getInputRules() const;

    /// The new document of the result replaces the own tab of the tool by the choice "Open in
    /// this tab" of the result dialog (Images to PDF 336:3572, Q13 / R16; Blank PDF replaces it
    /// without the dialog, isResultDialogSkipped). The dialog offers Open in this tab instead
    /// of Open in a new tab (the branch is package A's, the replacement package D's). Default:
    /// false.
    virtual bool isResultInThisTab() const;

    // ------------------------------------------------------------- flow

    /// Parameters of a run or of a preview (default: the values of the model)
    virtual QVariantMap getRunParameters() const;

    /// Parameters of a preview (default: getRunParameters). A tool, whose preview does not
    /// depend on its parameters (the scan of Remove Hidden Data, the links of Remove External
    /// Links), gives the same ones: a change of a parameter takes the cached preview.
    virtual QVariantMap getPreviewParameters() const;

    /// Inputs of a run besides the snapshot (default: as the host fills them)
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const;

    /// A parameter has changed (also by Undo and Redo)
    virtual void onParametersChanged(const QString& name);

    /// The preview has changed: the new one, nullptr = none is valid any more
    virtual void onPreviewChanged(const PDFDocumentToolResult* preview);

    /// The preview computes
    virtual void onPreviewProgress(int percentage, const QString& stepText);

    /// A run has started or it has ended without a result to decide about
    virtual void onRunStateChanged(bool isRunning);

    /// The run has ended with NothingToDo or Failed; the default shows an info bar over
    /// the canvas (295:2182 is a state of the panel: Compress overrides it), a lack of memory
    /// says the shell by its dialog after this function, for every tool. \returns nothing, the
    /// tool stays open.
    virtual void onRunNotAccepted(const PDFDocumentToolResult& result);

    /// A new version of the document is shown (the facts of the panels)
    virtual void onDocumentChanged();

    /// The language has changed: the shell creates the parts again after it
    virtual void retranslate();

    // ------------------------------------------------------------- input errors

    /// Error of an input, which the model cannot hold (a page range being written),
    /// empty = valid; the Primary button is disabled while any input has an error.
    /// \p key names the input.
    void setInputError(const QString& key, const QString& error);

    /// Says, that the texts and the states of the Primary button may have changed
    void updateConfirmState();

    /// Says, that the message of the status bar may have changed
    void updateStatusText();

    /// Says, that the title, the icon or the loss text of the own tab may have changed
    void updateOwnTabState();

Q_SIGNALS:
    /// The state or the text of the Primary button has changed
    void confirmStateChanged();

    /// The message of the status bar has changed
    void statusTextChanged();

    /// The title, the icon or the loss text of the own tab may have changed (the tab
    /// updates its title and its dot); a slot may not delete the shell
    void ownTabStateChanged();

private:
    QPointer<PDFDocumentToolShell> m_shell;
    std::map<QString, QString> m_inputErrors;
};

/// Factory of the GUI of a tool: one class per tool, no lambdas
class PDF4QTLIBAPPSHARED_EXPORT PDFDocumentToolGUIFactory
{
public:
    PDFDocumentToolGUIFactory() = default;
    virtual ~PDFDocumentToolGUIFactory();

    PDFDocumentToolGUIFactory(const PDFDocumentToolGUIFactory&) = delete;
    PDFDocumentToolGUIFactory& operator=(const PDFDocumentToolGUIFactory&) = delete;

    /// Creates the GUI of the tool (the shell owns it)
    virtual PDFDocumentToolGUI* createGUI() const = 0;
};

/// Factory of a GUI class with the default constructor
template<typename T>
class PDFDocumentToolGUIFactoryOf : public PDFDocumentToolGUIFactory
{
public:
    virtual PDFDocumentToolGUI* createGUI() const override { return new T(); }
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOLGUI_H
