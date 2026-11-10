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

#ifndef PDFAPPTOOLOUTPUTFILES_H
#define PDFAPPTOOLOUTPUTFILES_H

#include "pdfappglobal.h"
#include "pdfapptoolfilesink.h"

#include "pdfmessagebox.h"

#include <QPointer>

class QLineEdit;
class QPushButton;

namespace pdf
{

class PDFAppToolPanelBuilder;
class PDFAppToolPanelPopup;
class PDFAppParagraph;
class PDFDocumentToolGUI;
class PDFLineEdit;
class PDFTextLink;

class PDFAppSettingControl;
class PDFAppToolCallout;

/// The section Output files of the right panel of every tool, which writes more files (383:3315,
/// 385:11029; plan 4.2, R14, tools.md 9.3 point 19): Folder with Browse…, File name with the
/// tokens, the preview of the names ("Handbook_1.pdf … Handbook_4.pdf", "4 files · # is replaced
/// by the file number"), the red callout of an invalid template (383:9088) and Existing files.
/// The values are the parameters folder, fileName, existingFiles of PDFDocumentToolFileOutput in
/// the model of the tool (one undo step each).
///
/// Ownership: a child of the GUI (its QObject parent), the widgets belong to the right panel
/// (QPointer); built again by describe after a change of the language.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolOutputFilesSection : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppToolOutputFilesSection(PDFDocumentToolGUI* gui);
    virtual ~PDFAppToolOutputFilesSection() override;

    /// Adds the section to the panel of the builder
    void describe(PDFAppToolPanelBuilder& builder);

    /// The preview of the names: the names of the files, the hint of the token and an error
    /// (empty = none; the callout is shown, the preview hidden) with the text of its callout
    /// (383:9088 "Add # to the file name…")
    void setPreview(const QStringList& fileNames, const QString& hint, const QString& error, const QString& errorText = QString());

    /// Every file would have one name (383:9088): the box says "Handbook.pdf × 4" in red and "The
    /// names must differ", the callout at the top of the panel the error and its remedy
    void setDuplicateNames(const QString& fileName, int count, const QString& error, const QString& errorText);

    /// The section is shown (Extract: only One file per page)
    void setVisible(bool isVisible);

    /// The folder of the files: the parameter, empty = the folder of the document
    QString getFolder() const;

    /// The parts (tests)
    QWidget* getContainer() const { return m_container; }
    QPushButton* getBrowseButton() const { return m_browseButton; }
    PDFAppParagraph* getPreview() const { return m_preview; }
    PDFAppParagraph* getPreviewHint() const { return m_previewHint; }
    PDFAppToolCallout* getErrorCallout() const { return m_errorCallout; }
    QLineEdit* getFolderField() const { return m_folderField; }
    QLineEdit* getNameField() const { return m_nameField; }
    PDFTextLink* getTokensLink() const { return m_tokensLink; }

    /// The panel of the link Name tokens (383:4447), null until the link is clicked first
    PDFAppToolPanelPopup* getTokensPanel() const { return m_tokensPanel; }

    /// Opens (or closes) the panel of the tokens, the same as a click of the link
    void onTokensClicked();

private:
    void onBrowseClicked();
    void onTokenClicked();
    void updateFolderPlaceholder();
    void updatePreview();

    QStringList m_fileNames;
    QString m_hint;
    QString m_error;
    QString m_errorText;
    QString m_duplicateName;
    int m_duplicateCount = 0;
    QPointer<PDFDocumentToolGUI> m_gui;
    QPointer<QWidget> m_container;
    QPointer<QPushButton> m_browseButton;
    QPointer<QLineEdit> m_folderField;
    QPointer<QLineEdit> m_nameField;
    QPointer<PDFTextLink> m_tokensLink;
    QPointer<PDFAppToolPanelPopup> m_tokensPanel;
    QPointer<QWidget> m_namesBox;
    QPointer<PDFAppParagraph> m_preview;
    QPointer<PDFAppParagraph> m_previewHint;
    QPointer<PDFAppToolCallout> m_errorCallout;
    bool m_isVisible = true;
};

/// The question of a file of a run, which exists (Figma 356:11997, R3; every tool of more
/// files): "Report_17.png already exists", "The folder Exports already contains 37 files with
/// the names of this export.", the check box "Do the same for the other 36 files", Stop Export,
/// Skip, Rename, Overwrite. A file, which is a source or open in a tab, cannot be overwritten
/// (R14): Overwrite is disabled and the text says it (no frame: the neighbor 356:11997).
///
/// Ownership: created by new by the shell, held in a QPointer, deleted on close; answered emits
/// answered last (a slot may delete the dialog or the shell).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolFileConflictDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    explicit PDFAppToolFileConflictDialog(const PDFAppToolFileSink::Question& question, QWidget* parent);
    virtual ~PDFAppToolFileConflictDialog() override;

    const PDFAppToolFileSink::Question& getQuestion() const { return m_question; }

    /// The buttons (tests)
    QPushButton* getStopButton() const { return m_stopButton; }
    QPushButton* getSkipButton() const { return m_skipButton; }
    QPushButton* getRenameButton() const { return m_renameButton; }
    QPushButton* getOverwriteButton() const { return m_overwriteButton; }

    /// Answers by a button (the same as its click); Esc is Stop Export
    void answer(PDFAppToolFileSink::Answer answer);

    virtual void reject() override;

Q_SIGNALS:
    /// The user has answered; the dialog closes. A slot may delete the dialog.
    void answered(pdf::PDFAppToolFileSink::Answer answer, bool isForAll);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void updateTexts();
    void onStopClicked();
    void onSkipClicked();
    void onRenameClicked();
    void onOverwriteClicked();

    PDFAppToolFileSink::Question m_question;
    QPointer<QPushButton> m_stopButton;
    QPointer<QPushButton> m_skipButton;
    QPointer<QPushButton> m_renameButton;
    QPointer<QPushButton> m_overwriteButton;
    bool m_isAnswered = false;
};

}   // namespace pdf

#endif // PDFAPPTOOLOUTPUTFILES_H
