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

#include <QObject>
#include <QPointer>
#include <QString>
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

    /// Texts of the result dialog ("Pages rotated", the file of an export)
    virtual void fillResultInput(PDFAppToolResultInput* input) const;

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

Q_SIGNALS:
    /// The state or the text of the Primary button has changed
    void confirmStateChanged();

    /// The message of the status bar has changed
    void statusTextChanged();

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
