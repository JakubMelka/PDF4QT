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

#ifndef PDFAPPTOOLRESULTDIALOG_H
#define PDFAPPTOOLRESULTDIALOG_H

#include "pdfappglobal.h"
#include "pdfapptoolresultwriter.h"
#include "pdfappworkspace.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfdocumenttool.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"

#include <QPointer>
#include <QString>
#include <QWidget>

#include <map>

#include <QCheckBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>

class QButtonGroup;
class QVBoxLayout;

namespace pdf
{

class PDFAppContext;
class PDFAppDocumentTab;
class PDFAppToolChooserDialog;
class PDFDocumentToolCatalogEntry;
class PDFDocumentToolRunner;

/// The folder of a written file (809:140625): one line, a long path elided in the
/// middle, the whole path in its tooltip
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPathLabel : public PDFLabel
{
    Q_OBJECT

private:
    using BaseClass = PDFLabel;

public:
    explicit PDFAppToolPathLabel(QWidget* parent);
    virtual ~PDFAppToolPathLabel() override;

    /// The whole path in the native notation
    const QString& getPath() const { return m_path; }
    void setPath(const QString& path);

protected:
    virtual void resizeEvent(QResizeEvent* event) override;

private:
    void updateText();

    QString m_path;
};

/// One choice of the result dialog (Figma option-tab 312:7393, option-copy 312:7400):
/// a card of the radius 10 with a radio button, an optional tag, a description and the
/// controls of the choice, which are shown only when it is selected. The selected card
/// has the border border/focus and the fill bg/accent 13 % over bg/surface. A click
/// anywhere on the card selects it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolResultOption : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppToolResultOption(QWidget* parent);
    virtual ~PDFAppToolResultOption() override;

    /// Geometry of the frames
    static constexpr int RADIUS = 10;
    static constexpr int PADDING_H = 13;        ///< Padding 12 + the border
    static constexpr int PADDING_V = 11;        ///< Padding 10 + the border
    static constexpr int DETAIL_INDENT = 26;    ///< The radio 18 and its gap 8
    static constexpr int DETAIL_SPACING = 8;
    static constexpr int DESCRIPTION_LINE = 16; ///< The line of the description on every page (312:7399, 339:10300, 378:4336, 438:14342, 438:16073)

    QRadioButton* getRadioButton() const { return m_radioButton; }
    PDFTag* getTag() const { return m_tag; }

    /// Texts of the option
    void setTexts(const QString& title, const QString& description);
    QString getDescription() const;

    /// Tag "Recommended" of the preselected choice (empty = no tag)
    void setTagText(const QString& text);

    /// Layout of the controls shown only when the option is selected
    QVBoxLayout* getControlsLayout() const { return m_controlsLayout; }

    /// The option is selected (its radio button is checked)
    bool isSelected() const;

Q_SIGNALS:
    /// The selection has changed, the size of the option too
    void selectionChanged();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;

private:
    void onRadioToggled(bool checked);

    QPointer<QRadioButton> m_radioButton;
    QPointer<PDFTag> m_tag;
    QPointer<PDFAppParagraph> m_description;
    QPointer<QWidget> m_controls;
    QPointer<QVBoxLayout> m_controlsLayout;
};

/// What the result dialog shows (data of one finished run of a tool)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolResultInput
{
public:
    /// The tool (never null): its title, preselected choice, signature impact, successors
    const PDFDocumentToolCatalogEntry* tool = nullptr;

    /// Result of the run (status Success)
    PDFDocumentToolResult result;

    QString title;              ///< Title of the head; empty = "<Tool> finished" (312:7380). A tool may say more ("Pages rotated", 389:21590)
    QString subtitle;           ///< Subtitle of the head; empty = by the kind of the result
    QString statusText;         ///< Message of the status bar; empty = by the kind of the result. A tool may say more ("Hidden data removed – choose what to do with the result", 438:14342)
    QString fileDescription;    ///< Description of Save as a file of an export ("The text is written to a new .txt file. …", 339:10300)
    QString fileConfirmText;    ///< Button of Save as a file of an export ("Save Text", 339:10323)
    QString previousToolId;     ///< Tool, after whose result this one ran (the pair of the suggestions)
    QPointer<QWidget> scrimTarget;  ///< The covered widget: the canvas of the tool (312:7373); null = the body of the window
};

/// The decision of the user in the result dialog
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolResultOutcome
{
public:
    enum class Action
    {
        Discarded,      ///< Discard result, Esc: nothing has happened (also the document or the tab went away)
        Kept,           ///< Keep in this tab: one undo step
        CopySaved,      ///< Save as a copy: the file is written
        Overwritten,    ///< Overwrite the original file: the file is written, one undo step
        OpenedInNewTab, ///< Open in a new tab: a new unsaved document
        FileSaved,      ///< Save as a file: the file is written
        CopiedToClipboard, ///< Copy to clipboard
        Closed,         ///< An analysis: nothing to decide
        Rerun           ///< The document has changed: the tool runs again, the runner reports its new result (the shell opens the dialog again)
    };

    Action action = Action::Discarded;
    QString filePath;               ///< Written file
    QString continueToolId;         ///< The tool chosen by Then continue with (empty = none)
    QString continueDocumentId;     ///< The document it runs on: the document of the tab, the opened copy or the new tab (F11); empty = none
    bool isOpenFolderRequested = false;  ///< Open the folder afterwards (an export): the page asks the navigator, which checks the address

    /// The result was accepted: the tool counts as used
    bool isAccepted() const { return action != Action::Discarded && action != Action::Rerun; }
};

/// The result dialog of every tool (plan of the first batch, chapters 1.1 and 4; Figma
/// 296:2761, 316:7293, 339:9601, 378:3674, 389:21590, 303:4897, 438:14342, 438:16073). It
/// is always modal (owner answer D) and covers the canvas of the tool; in a window too
/// small for the card it covers the body of the window and its body scrolls.
///
/// The kind of the dialog comes from the result:
///  - a changed document (ChangeDocument): Keep in this tab, Save as a copy, Overwrite
///    the original file, Then continue with;
///  - a new document (NewDocument): Open in a new tab, Save as a file, Then continue with;
///  - a file of the result (Files): Save as a file, Copy to clipboard of a text, no Then
///    continue with (K20);
///  - nothing to write (Analysis): only the facts and the button Close.
///
/// The rules of the choices are in one place, \p getChoices: a signed document with a
/// tool of the impact LostCopyOnly offers only Save as a copy with the amber warning
/// (O31, chapter 4); a document without a file has no Overwrite (F5), a locked one neither
/// Keep nor Overwrite. The choice preselected by the catalog has the tag Recommended. A
/// changed document of a display version of an XFA form cannot be written at all (F5, the
/// shell does not run such a tool): its dialog says so and offers only Close.
///
/// The dialog holds the blocking operation of the runner until its close (F2): the
/// operation ends just before the commit. A write error leaves everything in the dialog
/// (316:7293, K5), an existing file asks by the state Replace File inside of the
/// dialog (tools.md 9.3 point 11). The tool is recorded as used, when the result is
/// accepted. Everything ends by one signal \p decided; the dialog deletes itself. A
/// lost document or a deleted runner closes the dialog without a decision (F2).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolResultDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    /// Width of the card (312:7374)
    static constexpr int CARD_WIDTH = 520;

    /// Smallest width of the card in a narrow window
    static constexpr int MINIMUM_CARD_WIDTH = 360;

    /// Space kept around the card over the covered widget
    static constexpr int HOST_MARGIN = 8;

    /// Smallest height of the scrolled part of a low window
    static constexpr int MINIMUM_BODY_HEIGHT = 96;

    /// Most rows of the search of Another tool…
    static constexpr int MAXIMUM_CHOOSER_ROWS = 12;

    /// A choice of the dialog
    enum class Choice
    {
        Keep,       ///< Keep in this tab
        Copy,       ///< Save as a copy
        Overwrite,  ///< Overwrite the original file
        NewTab,     ///< Open in a new tab
        File,       ///< Save as a file
        Clipboard   ///< Copy to clipboard
    };

    /// Kind of the result the dialog decides about
    enum class ResultKind
    {
        ChangeDocument,
        NewDocument,
        Files,
        Analysis
    };

    /// What the document allows
    class DocumentState
    {
    public:
        bool hasFile = true;        ///< The document has a file (not Edit a Copy, not an attachment from the memory)
        bool isSigned = false;      ///< The document has a signed signature field
        bool isLocked = false;      ///< The document is locked
    };

    /// Kind of the dialog of a result
    static ResultKind getResultKind(const PDFDocumentToolResult& result);

    /// The offered choices in the order of the dialog (the rules of O31, F5)
    static std::vector<Choice> getChoices(const PDFDocumentToolCatalogEntry* tool, ResultKind kind, const DocumentState& state);

    /// The preselected choice among \p choices (the catalog, else the first one)
    static Choice getPreselectedChoice(const PDFDocumentToolCatalogEntry* tool, const std::vector<Choice>& choices);

    /// Creates the dialog of a result in the state Waiting of the runner, not shown yet
    /// \param context Context of the application (the use of the tool, the suggestions)
    /// \param tab Tab of the document, may be null for a tool without a document
    /// \param runner The runner of the result
    /// \param input Result and its texts
    /// \param parent The window
    explicit PDFAppToolResultDialog(PDFAppContext* context, PDFAppDocumentTab* tab, PDFDocumentToolRunner* runner, PDFAppToolResultInput input, QWidget* parent);
    virtual ~PDFAppToolResultDialog() override;

    /// Creates and opens the modal dialog of a result of a tool of a document tab: it
    /// deletes itself after \p decided
    static PDFAppToolResultDialog* open(PDFAppDocumentTab* tab, PDFDocumentToolRunner* runner, PDFAppToolResultInput input);

    ResultKind getResultKind() const { return m_kind; }
    Choice getSelectedChoice() const { return m_selectedChoice; }
    void selectChoice(Choice choice);

    /// Option of a choice, nullptr when it is not offered
    PDFAppToolResultOption* getOption(Choice choice) const;

    /// The parts of the dialog (tests, the shell)
    QWidget* getFactsWidget() const { return m_factsPanel; }
    PDFKeyValueList* getFacts() const { return m_facts; }
    PDFContinueWith* getContinueWith() const { return m_continueWith; }
    PDFTintedCard* getSignedWarning() const { return m_signedWarning; }
    PDFTintedCard* getWriteError() const { return m_writeError; }
    PDFLabel* getNameError() const { return m_nameError; }
    PDFLineEdit* getFileNameField() const { return m_fileNameField; }
    PDFLineEdit* getFilePathField() const { return m_filePathField; }
    PDFAppToolPathLabel* getFolderLabel() const { return m_folderLabel; }
    QPushButton* getBrowseButton() const { return m_browseButton; }
    QCheckBox* getOpenCheckBox() const { return m_openCheckBox; }
    QPushButton* getChooseLocationButton() const { return m_chooseLocationButton; }
    QPushButton* getDiscardButton() const { return m_discardButton; }
    QPushButton* getConfirmButton() const { return m_confirmButton; }
    QScrollArea* getScrollArea() const { return m_scrollArea; }

    /// The target of the write of the selected choice
    QString getTargetFilePath() const;

    /// The dialog waits for a write
    bool isWriting() const;

    /// The confirmation replaces an existing file (the state Replace File)
    bool isReplaceConfirmed() const { return m_isReplaceConfirmed; }

    /// Message of the status bar of the shell: "<Tool> finished · the result is waiting
    /// for your decision" (296:2761, 378:3674: "the new document"), the text of the tool,
    /// or the error of the write (316:7293)
    QString getStatusText() const;

    /// Confirms the selected choice (the Primary button)
    void confirm();

    virtual void reject() override;
    virtual void setVisible(bool visible) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

Q_SIGNALS:
    /// The user has decided, the dialog is hidden and goes away. A slot may delete
    /// the tab or the window.
    void decided(const pdf::PDFAppToolResultOutcome& outcome);

    /// Text of getStatusText has changed
    void statusTextChanged(const QString& text);

protected:
    virtual void changeEvent(QEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;

private:
    void createHead();
    void createBody();
    void createFacts(QVBoxLayout* layout, QWidget* parent);
    void createOptions(QVBoxLayout* layout, QWidget* parent);
    void createContinueWith(QVBoxLayout* layout, QWidget* parent);
    void createFileControls(PDFAppToolResultOption* option, bool isFullPath, bool isChecked);
    PDFAppToolResultOption* addOption(Choice choice, QVBoxLayout* layout, QWidget* parent);

    /// Texts, which depend on the language
    void updateTexts();
    QString getDefaultTitle() const;
    QString getDefaultSubtitle() const;
    QString getConfirmText() const;
    QString getConfirmIconName() const;
    QString getFailedWriteTitle() const;
    void updateContinueNames();
    void updateConfirmButton();
    void updateContinueVisibility();
    void adaptToHost();
    void placeWriteError();

    /// The choice writes a file to a location of the user
    bool isLocationChoice() const;

    /// The folder or the name of the selected choice is valid; shows the reason otherwise
    bool checkTarget();
    void showNameError(const QString& text);
    void clearStates();

    /// The target is the file of a document open in a tab: a copy never replaces it
    /// (the document of the tab, and of this tab - "its file stays as it is")
    bool isTargetOpenInTab(const QString& filePath) const;

    /// The error of a write at the choice, which failed. \p isFileWritten: Overwrite has
    /// written the file, only the document of the tab has refused the result
    void showWriteError(const QString& text, bool isFileWritten = false);
    void setBusy(bool isBusy);

    const PDFDocumentToolOutputDocument* findOutput(PDFDocumentToolOutputDocument::Kind kind) const;
    QString getOriginFilePath() const;
    QString getInitialFolder() const;
    QString getSuggestedFileName() const;

    void onChoiceToggled();
    void onConfirmClicked();
    void onDiscardClicked();
    void onBrowseClicked();
    void onChooseLocationClicked();
    void onTargetEdited();
    void onOpenCheckToggled();
    void onContinueChanged();
    void onAnotherToolRequested();
    void onChooserFinished(int result);
    void onWriterFinished(const pdf::PDFAppToolResultWriter::Result& result);
    void onRunnerGone();

    /// Commits the changed document (Keep in this tab)
    void commitKeep();

    /// Writes over the file of the document
    void startOverwrite();

    /// Ends the dialog by the decision
    void finishWith(PDFAppToolResultOutcome outcome);

    /// Id of the document of the next tool, and opens what is to be opened
    void openResult(PDFAppToolResultOutcome* outcome);

    QPointer<PDFAppContext> m_context;
    QPointer<PDFAppDocumentTab> m_tab;
    QPointer<PDFDocumentToolRunner> m_runner;
    PDFAppToolResultInput m_input;
    PDFAppDocumentSnapshot m_snapshot;
    ResultKind m_kind = ResultKind::Analysis;
    DocumentState m_state;
    Choice m_selectedChoice = Choice::Keep;
    std::map<Choice, QPointer<PDFAppToolResultOption>> m_options;

    QPointer<QButtonGroup> m_choiceGroup;
    QPointer<QScrollArea> m_scrollArea;
    QPointer<QWidget> m_body;
    QPointer<QWidget> m_factsPanel;
    QPointer<PDFKeyValueList> m_facts;
    QPointer<PDFContinueWith> m_continueWith;
    QStringList m_continueToolIds;
    QPointer<PDFTintedCard> m_signedWarning;
    QPointer<PDFTintedCard> m_writeError;
    QPointer<PDFAppParagraph> m_writeErrorText;
    QPointer<PDFLabel> m_writeErrorTitle;
    QPointer<QPushButton> m_chooseLocationButton;
    QPointer<PDFAppParagraph> m_titleLabel;
    QPointer<PDFAppParagraph> m_subtitleLabel;
    QPointer<PDFLabel> m_nameError;
    QPointer<PDFLineEdit> m_fileNameField;
    QPointer<PDFLineEdit> m_filePathField;
    QPointer<PDFAppToolPathLabel> m_folderLabel;
    QPointer<QPushButton> m_browseButton;
    QPointer<QCheckBox> m_openCheckBox;
    QPointer<QPushButton> m_discardButton;
    QPointer<QPushButton> m_confirmButton;
    QPointer<PDFAppToolResultWriter> m_writer;
    QPointer<QWidget> m_host;
    QPointer<PDFAppToolChooserDialog> m_chooser;

    QString m_folder;               ///< Folder of a copy (the file name is in its field)
    QString m_consentedPath;        ///< A path chosen by the file dialog: a conscious choice of a network location (T1)
    QString m_statusError;          ///< Error of the last write for the status bar
    bool m_isReplaceConfirmed = false;
    bool m_isDecided = false;
    bool m_isOverwriteRunning = false;
    bool m_isFileWrittenError = false;  ///< The shown error says, that the file has been written
    bool m_isDisplayVersion = false;    ///< A changed document of a display version of an XFA form: nothing can be done with it (F5)
};

}   // namespace pdf

#endif // PDFAPPTOOLRESULTDIALOG_H
