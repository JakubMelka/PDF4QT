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

#ifndef PDFAPPDOCUMENTDIALOGS_H
#define PDFAPPDOCUMENTDIALOGS_H

#include "pdfappglobal.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfapphomedialogs.h"
#include "pdfapphomewidgets.h"

#include "pdffluentwidgets.h"
#include "pdflockeddocumentpicture.h"

#include <QFrame>
#include <QMenu>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <vector>

class QPushButton;
class QVBoxLayout;

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentOperation;

/// Screen of a document tab, which does not show its document (package A):
/// the document is being read (a progress, shown after LOADING_DELAY, so a fast
/// opening does not flash), or the file is protected by a password (locked-body of
/// pdf4qt-document-password 228:8709: the card with the picture Locked document,
/// the title, the name of the file, the password field, the error of a wrong
/// password 228:8965 and the buttons Close tab / Open document; the status bar
/// says the state). The screen has no toolbar: the document is not open yet.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentStateScreen : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Width of the password card (password-card 228:8800)
    static constexpr int CARD_WIDTH = 440;

    /// Delay of the progress of the reading
    static constexpr int LOADING_DELAY = 300;

    explicit PDFAppDocumentStateScreen(QWidget* parent = nullptr);
    virtual ~PDFAppDocumentStateScreen() override;

    /// Shows the state of the document (nullptr = nothing)
    void setDocument(PDFAppDocument* document);

    /// Parts of the screen (tests)
    QWidget* getPasswordCard() const { return m_passwordCard; }
    QWidget* getLoadingPanel() const { return m_loadingPanel; }
    PDFLineEdit* getPasswordField() const { return m_passwordField; }
    QPushButton* getOpenButton() const { return m_openButton; }
    QPushButton* getCloseButton() const { return m_closeButton; }
    QWidget* getErrorRow() const { return m_errorRow; }
    PDFLockedDocumentPicture* getPicture() const { return m_picture; }
    PDFAppDocumentStatusBar* getStatusBar() const { return m_statusBar; }
    PDFLabel* getTitleLabel() const { return m_titleLabel; }
    PDFLabel* getSubtitleLabel() const { return m_subtitle; }

Q_SIGNALS:
    /// The user has entered the password (Open document, Enter)
    void passwordEntered(const QString& password);

    /// The user wants to close the tab (Close tab)
    void closeRequested();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void createPasswordCard(QWidget* parent);
    void createLoadingPanel(QWidget* parent);
    void updateState();
    void updateTexts();
    void updateOpenButton();
    void onOpenClicked();
    void onCloseClicked();
    void onPasswordEdited();
    void onLoadingDelayElapsed();
    void onLoadingProgressChanged();

    QPointer<PDFAppDocument> m_document;
    QPointer<QWidget> m_body;
    QPointer<QFrame> m_passwordCard;
    QPointer<PDFLockedDocumentPicture> m_picture;
    QPointer<PDFLabel> m_titleLabel;
    QPointer<PDFLabel> m_subtitle;
    QPointer<PDFLineEdit> m_passwordField;
    QPointer<QWidget> m_errorRow;
    QPointer<PDFLabel> m_errorText;
    QPointer<QPushButton> m_closeButton;
    QPointer<QPushButton> m_openButton;
    QPointer<QWidget> m_loadingPanel;
    QPointer<PDFLabel> m_loadingTitle;
    QPointer<QWidget> m_loadingBar;
    QPointer<PDFAppDocumentStatusBar> m_statusBar;
    QPointer<QTimer> m_loadingTimer;
    bool m_isLoadingShown = false;
};

/// Choice of a dialog of a document (the buttons of the dialogs of the design)
enum class PDFAppDocumentChoice
{
    Cancel,             ///< Cancel, Esc, the dialog closed by the window
    Save,               ///< Save (close-unsaved), Save All (quit-unsaved)
    Discard,            ///< Don't Save (close-unsaved), Quit Without Saving (quit-unsaved)
    Stop,               ///< Stop and Close (close-running)
    Keep                ///< Keep Running (close-running)
};

/// Message box of a document tab with buttons, each of them a choice (the note
/// 958:28033: the scrim over the tab, the 36 px badge, width 440 or 520 with a
/// list; the destructive choice at the leading edge as a Secondary button, the
/// safe one as the Primary button at the trailing edge, Cancel next to it). The
/// dialog closes by a click on a button (accepted) or by Esc (Cancel).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentChoiceDialog : public PDFAppCardDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCardDialog;

public:
    explicit PDFAppDocumentChoiceDialog(PDFThemeIcons::Badge badge, const QString& title, const QString& text, int width, QWidget* parent);
    virtual ~PDFAppDocumentChoiceDialog() override;

    /// Adds the button of the choice (\p objectName for the tests and the accessibility)
    QPushButton* addChoice(const QString& text, PDFAppDocumentChoice choice, bool isPrimary, bool isLeading, const QString& objectName);

    /// Choice of the user (Cancel until a button is clicked)
    PDFAppDocumentChoice getChoice() const { return m_choice; }

    /// Button of the choice (nullptr, if there is none)
    QPushButton* getChoiceButton(PDFAppDocumentChoice choice) const;

    /// Runs the dialog (exec): \returns the choice; the dialog, the parent or the
    /// caller may be deleted meanwhile - the dialog is held by a QPointer and the
    /// choice is Cancel, when it is gone (decision D10)
    static PDFAppDocumentChoice run(PDFAppDocumentChoiceDialog* dialog);

private:
    void onChoiceClicked();

    struct ChoiceButton
    {
        QPointer<QPushButton> button;
        PDFAppDocumentChoice choice = PDFAppDocumentChoice::Cancel;
    };

    std::vector<ChoiceButton> m_buttons;
    PDFAppDocumentChoice m_choice = PDFAppDocumentChoice::Cancel;
};

/// List of items in a note box of a dialog (quit-unsaved 955:145378 documents,
/// open-failed 1028:24297 items): bg/subtle, radius 10, paddings 4 / 12, rows
/// of the icon 16, the name (Medium 12) with its folder (Regular 11 text/muted)
/// and the status at the trailing side, dividers between the rows
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentItemList : public QFrame
{
    Q_OBJECT

private:
    using BaseClass = QFrame;

public:
    /// Most rows shown (the rest is summarized by the last row)
    static constexpr int MAXIMUM_ROWS = 8;

    /// Widest status at the trailing side of a row
    static constexpr int STATUS_WIDTH = 260;

    explicit PDFAppDocumentItemList(QWidget* parent = nullptr);
    virtual ~PDFAppDocumentItemList() override;

    /// Adds a row. \p isError colors the status as an error (text/danger).
    void addItem(const QString& iconName, const QString& name, const QString& detail, const QString& status, bool isError);

    /// Adds the last row "… and N more" (called by the dialog over MAXIMUM_ROWS)
    void addMoreRow(int count);

    int getItemCount() const { return m_count; }

private:
    void addDivider();

    QPointer<QVBoxLayout> m_layout;
    int m_count = 0;
};

/// Save the changes to <file>? (pdf4qt-document-close-unsaved 955:144943):
/// Don't Save / Cancel / Save
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCloseUnsavedDialog : public PDFAppDocumentChoiceDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentChoiceDialog;

public:
    explicit PDFAppCloseUnsavedDialog(const PDFAppDocument* document, QWidget* parent);
    virtual ~PDFAppCloseUnsavedDialog() override;

    /// Text of the unsaved changes of the document ("3 unsaved changes")
    static QString getChangesText(const PDFAppDocument* document);

    /// Text of the dialog for the document
    static QString getText(const PDFAppDocument* document);
};

/// Stop <operation> and close the tab? (pdf4qt-document-close-running 955:145813):
/// Stop and Close / Keep Running
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCloseRunningDialog : public PDFAppDocumentChoiceDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentChoiceDialog;

public:
    explicit PDFAppCloseRunningDialog(const PDFAppDocument* document, QWidget* parent);
    virtual ~PDFAppCloseRunningDialog() override;

    /// Title and text of the dialog for the running operations of the document
    static QString getTitle(const PDFAppDocument* document);
    static QString getText(const PDFAppDocument* document);

private:
    /// The first operation, which the user is asked about (and their count)
    static const PDFAppDocumentOperation* getOperation(const PDFAppDocument* document, int* count);
};

/// Save the changes before quitting? (pdf4qt-quit-unsaved 955:145378): the
/// documents with unsaved changes and Quit Without Saving / Cancel / Save All
class PDF4QTLIBAPPSHARED_EXPORT PDFAppQuitUnsavedDialog : public PDFAppDocumentChoiceDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentChoiceDialog;

public:
    explicit PDFAppQuitUnsavedDialog(const std::vector<const PDFAppDocument*>& documents, QWidget* parent);
    virtual ~PDFAppQuitUnsavedDialog() override;

    PDFAppDocumentItemList* getList() const { return m_list; }

private:
    QPointer<PDFAppDocumentItemList> m_list;
};

/// Item of the dialog open-failed: a file, which was not opened, or an option of
/// the command line, which was not used
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOpenFailure
{
public:
    bool isOption = false;  ///< An option of the command line (else a file)
    QString name;           ///< The file (absolute path) or the option
    QString reason;         ///< Why it was not used (translated), may be empty

    bool operator==(const PDFAppOpenFailure&) const = default;
};

/// N files and M options could not be used (pdf4qt-document-open-failed 1028:24297):
/// the list of the files and options with their reasons, Copy Details / Close
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOpenFailedDialog : public PDFAppCardDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCardDialog;

public:
    explicit PDFAppOpenFailedDialog(const std::vector<PDFAppOpenFailure>& failures, const QString& openedTitle, QWidget* parent);
    virtual ~PDFAppOpenFailedDialog() override;

    const std::vector<PDFAppOpenFailure>& getFailures() const { return m_failures; }
    PDFAppDocumentItemList* getList() const { return m_list; }
    QPushButton* getCopyButton() const { return m_copyButton; }
    QPushButton* getCloseButton() const { return m_closeButton; }

    /// Title of the dialog for the failures ("2 files and 1 option could not be used")
    static QString getTitle(const std::vector<PDFAppOpenFailure>& failures);

    /// Text of all failures for the clipboard (Copy Details)
    static QString getDetails(const std::vector<PDFAppOpenFailure>& failures);

private:
    void onCopyClicked();
    void onCloseClicked();

    std::vector<PDFAppOpenFailure> m_failures;
    QPointer<PDFAppDocumentItemList> m_list;
    QPointer<QPushButton> m_copyButton;
    QPointer<QPushButton> m_closeButton;
};

/// Document is locked (popover-locked 266:12435 of pdf4qt-document-locked
/// 266:12010): the text and the buttons Unlock / Edit a Copy below the lock of
/// the document tab. A popover menu of the style (radius 12, shadow 0 / 8 / 24);
/// the caret of the frame is not drawn (the popovers of the library have none).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppLockPopover : public QMenu
{
    Q_OBJECT

private:
    using BaseClass = QMenu;

public:
    /// Width of the panel of the popover
    static constexpr int WIDTH = 300;

    explicit PDFAppLockPopover(QWidget* parent = nullptr);
    virtual ~PDFAppLockPopover() override;

    QPushButton* getUnlockButton() const { return m_unlockButton; }
    QPushButton* getEditCopyButton() const { return m_editCopyButton; }

    /// Shows the popover below the rectangle of the lock (global coordinates)
    void showBelow(const QRect& anchor);

Q_SIGNALS:
    void unlockRequested();
    void editCopyRequested();

private:
    void onUnlockClicked();
    void onEditCopyClicked();

    QPointer<QPushButton> m_unlockButton;
    QPointer<QPushButton> m_editCopyButton;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTDIALOGS_H
