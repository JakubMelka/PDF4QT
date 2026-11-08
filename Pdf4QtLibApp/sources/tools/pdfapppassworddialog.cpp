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

#include "pdfapppassworddialog.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolmergemodel.h"
#include "pdfappdocumentloader.h"
#include "pdfappdocument.h"

#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"

#include <QEvent>
#include <QPushButton>
#include <QVBoxLayout>

namespace pdf
{

PDFAppPasswordDialog::PDFAppPasswordDialog(PDFAppToolInputList* list, const QString& inputId, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, true, QString(), QString(), WIDTH, parent),
    m_list(list),
    m_inputId(inputId)
{
    setObjectName(QStringLiteral("passwordDialog"));

    const PDFAppToolInput* input = list ? list->findInput(inputId) : nullptr;
    m_name = input ? input->name : QString();
    m_isOwnerPassword = input && input->state == PDFAppToolInputState::Restricted;

    createParts();
    if (list)
    {
        connect(list, &PDFAppToolInputList::inputChanged, this, &PDFAppPasswordDialog::onInputChanged);
        connect(list, &PDFAppToolInputList::inputsChanged, this, &PDFAppPasswordDialog::onInputsChanged);
        connect(list, &QObject::destroyed, this, &PDFAppPasswordDialog::onListDestroyed);
    }

    updateTexts();
    setError(input && input->isPasswordWrong);
    updateButtons();
    m_passwordField->setFocus();
}

PDFAppPasswordDialog::PDFAppPasswordDialog(PDFAppDocument* document, const PDFAppPermissionRequest& request, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, true, QString(), QString(), WIDTH, parent),
    m_document(document),
    m_request(request),
    m_isDocumentMode(true)
{
    setObjectName(QStringLiteral("ownerPasswordDialog"));
    m_name = document ? document->getTitle() : QString();
    m_isOwnerPassword = true;

    createParts();
    if (document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppPasswordDialog::onDocumentChanged);
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppPasswordDialog::onDocumentChanged);
        connect(document, &QObject::destroyed, this, &PDFAppPasswordDialog::onDocumentDestroyed);
    }

    updateTexts();
    setError(false);
    updateButtons();
    m_passwordField->setFocus();
}

PDFAppPasswordDialog::~PDFAppPasswordDialog() = default;

void PDFAppPasswordDialog::createParts()
{
    // 398:28116: the label Password, the field 34, the error - gaps 14; the document mode
    // adds the list of what the document forbids above and the note of 1390:34569 below
    QWidget* form = new QWidget(this);
    form->setObjectName(QStringLiteral("passwordForm"));
    QVBoxLayout* formLayout = new QVBoxLayout(form);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(GAP);
    if (m_isDocumentMode)
    {
        m_forbiddenText = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(PDFThemeFonts::Role::Small), 15, PDFThemeColors::TextSecondary, form);
        m_forbiddenText->setObjectName(QStringLiteral("forbiddenText"));
        formLayout->addWidget(m_forbiddenText);
    }
    // The label Regular 12 text/secondary (398:28124), the field 34 (398:28125), the error
    // Medium 11 text/danger (398:28131), the note of the owner password Regular 11 text/muted
    // (1390:34806)
    m_fieldLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextSecondary, form);
    m_fieldLabel->setObjectName(QStringLiteral("passwordLabel"));
    formLayout->addWidget(m_fieldLabel);
    m_passwordField = new PDFLineEdit(form);
    m_passwordField->setObjectName(QStringLiteral("passwordField"));
    m_passwordField->setPasswordField();
    m_passwordField->setMaxLength(PDFAppDocumentReading::MAXIMUM_PASSWORD_LENGTH);
    m_passwordField->setFixedHeight(FIELD_HEIGHT);
    formLayout->addWidget(m_passwordField);
    PDFAppParagraph* errorText = nullptr;
    m_errorRow = createErrorRow(form, &errorText);
    m_errorText = errorText;
    m_errorText->setFont(PDFThemeFonts::getFont(QFont::Medium, 11));
    formLayout->addWidget(m_errorRow);
    if (m_isDocumentMode || m_isOwnerPassword)
    {
        m_noteText = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), NOTE_LINE, PDFThemeColors::TextMuted, form);
        m_noteText->setObjectName(QStringLiteral("noteText"));
        formLayout->addWidget(m_noteText);
    }
    addContent(form);

    // actions 398:28133: Remove File (leading, only for an input), Cancel, Unlock (Primary)
    if (!m_isDocumentMode)
    {
        m_removeButton = addActionButton(QString(), false, true);
        m_removeButton->setObjectName(QStringLiteral("removeFileButton"));
        m_removeButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("trash")));
        connect(m_removeButton.data(), &QPushButton::clicked, this, &PDFAppPasswordDialog::onRemoveClicked);
    }
    m_cancelButton = addActionButton(QString(), false, false);
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    m_unlockButton = addActionButton(QString(), true, false);
    m_unlockButton->setObjectName(QStringLiteral("unlockButton"));
    m_unlockButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("key")));

    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppPasswordDialog::reject);
    connect(m_unlockButton.data(), &QPushButton::clicked, this, &PDFAppPasswordDialog::onUnlockClicked);
    connect(m_passwordField.data(), &QLineEdit::textEdited, this, &PDFAppPasswordDialog::onPasswordEdited);
}

PDFAppPasswordDialog* PDFAppPasswordDialog::openOwnerDialog(PDFAppDocument* document, const PDFAppPermissionRequest& request, QWidget* parent)
{
    if (!document || !document->isReady() || !PDFAppDocumentPermissions::canUnlock(document->getDocument().data()))
    {
        return nullptr;
    }

    // One question per document: a second action raises the open dialog
    QWidget* window = parent ? parent->window() : nullptr;
    if (window)
    {
        for (PDFAppPasswordDialog* dialog : window->findChildren<PDFAppPasswordDialog*>())
        {
            if (dialog->isDocumentMode() && dialog->getDocument() == document && dialog->isVisible())
            {
                dialog->raise();
                dialog->activateWindow();
                return dialog;
            }
        }
    }

    PDFAppPasswordDialog* dialog = new PDFAppPasswordDialog(document, request, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->open();
    return dialog;
}

QString PDFAppPasswordDialog::getForbiddenText() const
{
    return m_forbiddenText ? m_forbiddenText->text() : QString();
}

void PDFAppPasswordDialog::onDocumentChanged()
{
    // The unlock itself is handled by onUnlockClicked
    if (m_isBusy)
    {
        return;
    }

    PDFAppDocument* document = m_document.data();
    if (!document || !document->isReady())
    {
        reject();
        return;
    }

    // Unlocked meanwhile (another dialog, the properties): nothing waits for the password
    if (!PDFAppDocumentPermissions::canUnlock(document->getDocument().data()))
    {
        if (document->isOwnerUnlocked())
        {
            accept();
        }
        else
        {
            reject();
        }
        return;
    }
    updateTexts();
}

void PDFAppPasswordDialog::onDocumentDestroyed()
{
    // Only the pointer is forgotten here; the dialog closes from the event loop
    m_document = nullptr;
    QMetaObject::invokeMethod(this, &PDFAppPasswordDialog::reject, Qt::QueuedConnection);
}

PDFAppPasswordDialog* PDFAppPasswordDialog::openDialog(PDFAppToolInputList* list, const QString& inputId, QWidget* parent)
{
    const PDFAppToolInput* input = list ? list->findInput(inputId) : nullptr;
    if (!input || (input->state != PDFAppToolInputState::PasswordRequired && input->state != PDFAppToolInputState::Restricted))
    {
        return nullptr;
    }

    PDFAppPasswordDialog* dialog = new PDFAppPasswordDialog(list, inputId, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->open();
    return dialog;
}

QString PDFAppPasswordDialog::getErrorText() const
{
    return m_errorRow && !m_errorRow->isHidden() && m_errorText ? m_errorText->text() : QString();
}

void PDFAppPasswordDialog::updateTexts()
{
    if (m_isDocumentMode)
    {
        // The parts of 1390:34569: the title, the action and the missing permission in
        // plain words, what the document forbids, the note of the password
        const PDFDocument* pdfDocument = m_document && m_document->isReady() ? m_document->getDocument().data() : nullptr;
        setTitle(PDFAppDocumentPermissions::getDialogTitle(m_name));
        m_messageText = PDFAppDocumentPermissions::getDialogText(pdfDocument, m_request);
        setText(m_messageText);
        if (m_forbiddenText)
        {
            m_forbiddenText->setText(PDFAppDocumentPermissions::getForbiddenListText(pdfDocument));
        }
        if (m_noteText)
        {
            m_noteText->setText(tr("The password is used only to unlock this document until it is closed and is not stored."));
        }
    }
    else if (m_isOwnerPassword)
    {
        // pages-merge-password-owner 1390:34569: what the file does not allow, the note of the
        // password below the field
        setTitle(tr("Owner password for %1").arg(m_name));
        const PDFAppToolInput* input = m_list ? m_list->findInput(m_inputId) : nullptr;
        const QString restriction = input ? PDFAppToolMergePageModel::getRestrictionText(input->document.data()) : QString();
        m_messageText = restriction.isEmpty() ? tr("The file restricts printing, copying or changes. The merged document would not keep the restrictions, so the owner password is needed.")
                                              : tr("%1, and the merged PDF would lose these restrictions.").arg(restriction);
        setText(m_messageText);
        if (m_noteText)
        {
            m_noteText->setText(tr("The password is used only to read the file and is not stored."));
        }
    }
    else
    {
        setTitle(tr("Password for %1").arg(m_name));
        m_messageText = tr("The file is encrypted. The password is used only to read its pages.");
        setText(m_messageText);
    }

    if (m_fieldLabel)
    {
        m_fieldLabel->setText(m_isOwnerPassword ? tr("Owner password") : tr("Password"));
    }
    if (m_passwordField)
    {
        m_passwordField->setAccessibleName(m_isOwnerPassword ? tr("Owner password of %1").arg(m_name) : tr("Password of %1").arg(m_name));
    }
    if (m_errorText)
    {
        m_errorText->setText(tr("Wrong password – check Caps Lock and try again"));
    }
    if (m_removeButton)
    {
        m_removeButton->setText(tr("Remove File"));
    }
    if (m_cancelButton)
    {
        m_cancelButton->setText(tr("Cancel"));
    }
    if (m_unlockButton)
    {
        m_unlockButton->setText(tr("Unlock"));
    }
}

void PDFAppPasswordDialog::updateButtons()
{
    if (m_passwordField)
    {
        m_passwordField->setEnabled(!m_isBusy);
    }
    if (m_unlockButton)
    {
        m_unlockButton->setEnabled(!m_isBusy && m_passwordField && !m_passwordField->text().isEmpty());
    }
    if (m_removeButton)
    {
        m_removeButton->setEnabled(!m_isBusy);
    }
}

void PDFAppPasswordDialog::setError(bool isShown)
{
    if (m_errorRow)
    {
        m_errorRow->setVisible(isShown);
    }
    if (m_passwordField)
    {
        m_passwordField->setInvalid(isShown, isShown && m_errorText ? m_errorText->text() : QString());
    }
    adjustSize();
}

void PDFAppPasswordDialog::onPasswordEdited()
{
    // A new password hides the error of the last one
    if (!getErrorText().isEmpty())
    {
        setError(false);
    }
    updateButtons();
}

void PDFAppPasswordDialog::onUnlockClicked()
{
    if (m_isDocumentMode)
    {
        unlockDocument();
        return;
    }

    if (!m_list || !m_passwordField || m_isBusy || m_passwordField->text().isEmpty())
    {
        return;
    }

    // The field keeps the password while it is verified: a wrong one stays selected in the
    // Error state (398:28125), so the next typing replaces it
    const QString password = m_passwordField->text();
    m_isBusy = true;
    setError(false);
    updateButtons();

    // The list emits inputChanged (Loading) in this call: its slot may close the dialog
    QPointer<PDFAppPasswordDialog> guard(this);
    const bool isStarted = m_list->setPassword(m_inputId, password);
    if (guard && !isStarted)
    {
        m_isBusy = false;
        updateButtons();
    }
}

void PDFAppPasswordDialog::unlockDocument()
{
    PDFAppDocument* document = m_document.data();
    if (!document || !m_passwordField || m_isBusy || m_passwordField->text().isEmpty())
    {
        return;
    }

    // The document verifies the password by its handler and emits documentChanged - its
    // slots may delete the dialog or the document (the tab closes)
    const QString password = m_passwordField->text();
    m_isBusy = true;
    QPointer<PDFAppPasswordDialog> guard(this);
    const bool isUnlocked = document->unlockWithOwnerPassword(password);
    if (!guard)
    {
        return;
    }

    m_isBusy = false;
    if (isUnlocked)
    {
        accept();
        return;
    }

    // 398:27726: the error, the wrong password stays selected in the field
    setError(true);
    updateButtons();
    selectWrongPassword();
}

void PDFAppPasswordDialog::selectWrongPassword()
{
    // 398:28125: the masked text of the wrong password, selected, in the Error state: the
    // next typing replaces it
    if (m_passwordField)
    {
        m_passwordField->setFocus();
        m_passwordField->selectAll();
    }
}

void PDFAppPasswordDialog::onRemoveClicked()
{
    // The dialog closes first; the tool removes the input (its slot may delete the dialog)
    const QString inputId = m_inputId;
    done(QDialog::Rejected);
    Q_EMIT removeRequested(inputId);
}

void PDFAppPasswordDialog::onInputChanged(const QString& inputId)
{
    if (inputId != m_inputId || !m_list)
    {
        return;
    }

    const PDFAppToolInput* input = m_list->findInput(m_inputId);
    if (!input)
    {
        reject();
        return;
    }

    switch (input->state)
    {
        case PDFAppToolInputState::Loading:
            m_isBusy = true;
            updateButtons();
            break;

        case PDFAppToolInputState::PasswordRequired:
        case PDFAppToolInputState::Restricted:
        {
            // A wrong password: the error, the password selected in the field (398:27726); a
            // password of the user, which opened a restricted file, asks for the owner
            // password now - an empty field for it
            const bool isOwnerPassword = input->state == PDFAppToolInputState::Restricted;
            const bool isModeChanged = isOwnerPassword != m_isOwnerPassword;
            m_isOwnerPassword = isOwnerPassword;
            m_isBusy = false;
            if (isModeChanged && m_passwordField)
            {
                m_passwordField->clear();
            }
            updateTexts();
            setError(input->isPasswordWrong && !isModeChanged);
            updateButtons();
            selectWrongPassword();
            break;
        }

        case PDFAppToolInputState::Ready:
        case PDFAppToolInputState::Damaged:
        case PDFAppToolInputState::Unsupported:
        case PDFAppToolInputState::NoPages:
            // The password was accepted (the row shows what the file is now)
            m_isBusy = false;
            accept();
            break;
    }
}

void PDFAppPasswordDialog::onInputsChanged()
{
    // The input has been removed (Undo, the panel): nothing waits for its password
    if (!m_list || m_list->indexOf(m_inputId) < 0)
    {
        reject();
    }
}

void PDFAppPasswordDialog::onListDestroyed()
{
    // Only the pointer is forgotten here; the dialog closes from the event loop
    m_list = nullptr;
    QMetaObject::invokeMethod(this, &PDFAppPasswordDialog::reject, Qt::QueuedConnection);
}

void PDFAppPasswordDialog::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

}   // namespace pdf

#include "moc_pdfapppassworddialog.cpp"
