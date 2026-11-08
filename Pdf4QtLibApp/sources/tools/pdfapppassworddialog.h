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

#ifndef PDFAPPPASSWORDDIALOG_H
#define PDFAPPPASSWORDDIALOG_H

#include "pdfappglobal.h"
#include "pdfapphomedialogs.h"
#include "pdfappdocumentpermissions.h"

#include <QPointer>
#include <QString>

class QPushButton;

namespace pdf
{

class PDFAppDocument;
class PDFAppParagraph;
class PDFAppToolInputList;
class PDFLineEdit;

/// Password of an input of a tool with more inputs (pages-merge-password 398:27726, K19: the
/// badge alert-triangle; shared by the tools, which add files - Merge now, Compare later):
/// 460 wide, the title "Password for Annex_A.pdf", the text, the field Password (34), the error
/// "Wrong password – check Caps Lock and try again", Remove File at the leading edge, Cancel
/// and Unlock (Primary). The state Restricted (Q2 (c), K39, no frame) asks for the owner
/// password with its own title and text.
///
/// The dialog works with the list: Unlock gives the password to PDFAppToolInputList::setPassword
/// and waits for the reading (the field and Unlock are disabled); a wrong password shows the
/// error and stays in the field, all selected, in its Error state (owner 2026-11-08, 398:28125:
/// the next typing replaces it), an accepted one closes the dialog (accepted). Remove File closes the dialog and asks the tool to remove
/// the input (removeRequested - the tool removes it by its Undo history, the dialog never
/// removes it itself). The input removed meanwhile or the list deleted closes the dialog
/// (rejected).
///
/// The document mode (owner 2026-11-08, package O; no frame of its own - Q17: the parts of
/// 1390:34569 and 398:27726): an action of an open document, which its permissions forbid
/// (PDFAppDocumentPermissions::request), asks for the owner password of the document. The
/// title "Owner password for Contract.pdf", the text naming the action and the missing
/// permission in plain words, the list of what the document forbids, the field Owner
/// password, the error, the note that the password is not stored, Cancel and Unlock (no
/// Remove File). Unlock gives the password to PDFAppDocument::unlockWithOwnerPassword at
/// once (no thread: the handler verifies it, the file is not read): a wrong one shows the
/// error, the right one closes the dialog (accepted). The document unlocked meanwhile (by
/// another dialog) closes it accepted; the document deleted or not ready closes it rejected.
///
/// Ownership: a popup, opened by open() without a nested event loop (new + QPointer at the
/// caller, Qt::WA_DeleteOnClose); the list and the document are held by a QPointer.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPasswordDialog : public PDFAppCardDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCardDialog;

public:
    /// Width of password-dialog 398:28116
    static constexpr int WIDTH = 460;

    /// Gap of the parts of 398:28116 (the label, the field, the error)
    static constexpr int GAP = 14;

    /// Height of the field (Text field 398:28125)
    static constexpr int FIELD_HEIGHT = 34;

    /// Lines of the note (1390:34806, Regular 11)
    static constexpr int NOTE_LINE = 13;

    /// \param list The inputs (the dialog does not own it)
    /// \param inputId The input, which waits for its password
    /// \param parent Parent widget (the scrim covers its window)
    explicit PDFAppPasswordDialog(PDFAppToolInputList* list, const QString& inputId, QWidget* parent);

    /// The document mode
    /// \param document The restricted document (the dialog does not own it)
    /// \param request The action, which waits for the permission
    /// \param parent Parent widget (the scrim covers its window)
    explicit PDFAppPasswordDialog(PDFAppDocument* document, const PDFAppPermissionRequest& request, QWidget* parent);
    virtual ~PDFAppPasswordDialog() override;

    /// Opens the dialog without blocking (window modal, deleted when it is closed).
    /// \returns the dialog, nullptr when the input does not wait for a password
    static PDFAppPasswordDialog* openDialog(PDFAppToolInputList* list, const QString& inputId, QWidget* parent);

    /// Opens the document mode without blocking (window modal, deleted when it is closed);
    /// an open dialog of the same document in the window is raised instead of a second one.
    /// \returns the dialog, nullptr when the document cannot be unlocked (PDFAppDocumentPermissions::canUnlock)
    static PDFAppPasswordDialog* openOwnerDialog(PDFAppDocument* document, const PDFAppPermissionRequest& request, QWidget* parent);

    /// The document of the document mode (null in the mode of an input, or deleted)
    PDFAppDocument* getDocument() const { return m_document.data(); }

    /// The dialog asks for the owner password of an open document (the document mode)
    bool isDocumentMode() const { return m_isDocumentMode; }

    /// Text of the list of what the document forbids (the document mode)
    QString getForbiddenText() const;

    /// The text below the title (the action and the missing permission in the document mode)
    const QString& getText() const { return m_messageText; }

    const QString& getInputId() const { return m_inputId; }

    /// The dialog asks for the owner password (the input is Restricted)
    bool isOwnerPassword() const { return m_isOwnerPassword; }

    /// The password is being read
    bool isBusy() const { return m_isBusy; }

    PDFLineEdit* getPasswordField() const { return m_passwordField; }
    QPushButton* getUnlockButton() const { return m_unlockButton; }
    QPushButton* getCancelButton() const { return m_cancelButton; }
    QPushButton* getRemoveButton() const { return m_removeButton; }

    /// Text of the error below the field (empty = no error shown)
    QString getErrorText() const;

Q_SIGNALS:
    /// Remove File: the tool removes the input (the dialog is closed already)
    void removeRequested(const QString& inputId);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void updateTexts();
    void updateButtons();
    void setError(bool isShown);

    /// Focuses the field and selects its text (a wrong password, 398:28125)
    void selectWrongPassword();
    void onUnlockClicked();
    void onRemoveClicked();
    void onPasswordEdited();
    void onInputChanged(const QString& inputId);
    void onInputsChanged();
    void onListDestroyed();
    void onDocumentChanged();
    void onDocumentDestroyed();
    void createParts();
    void unlockDocument();

    QPointer<PDFAppToolInputList> m_list;
    QPointer<PDFAppDocument> m_document;
    PDFAppPermissionRequest m_request;
    bool m_isDocumentMode = false;
    QString m_messageText;
    QPointer<PDFAppParagraph> m_forbiddenText;
    QPointer<PDFAppParagraph> m_noteText;
    QString m_inputId;
    QString m_name;
    bool m_isOwnerPassword = false;
    bool m_isBusy = false;
    QPointer<PDFLabel> m_fieldLabel;
    QPointer<PDFLineEdit> m_passwordField;
    QPointer<QWidget> m_errorRow;
    QPointer<PDFAppParagraph> m_errorText;
    QPointer<QPushButton> m_removeButton;
    QPointer<QPushButton> m_cancelButton;
    QPointer<QPushButton> m_unlockButton;
};

}   // namespace pdf

#endif // PDFAPPPASSWORDDIALOG_H
