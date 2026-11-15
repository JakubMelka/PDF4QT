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

#ifndef PDFAPPCERTIFICATEDIALOGS_H
#define PDFAPPCERTIFICATEDIALOGS_H

#include "pdfappglobal.h"
#include "pdfmessagebox.h"

#include <QDate>
#include <QPointer>
#include <QString>

class QPushButton;

namespace pdf
{

class PDFAppContext;
class PDFAppParagraph;
class PDFComboBox;
class PDFDateTimeEdit;
class PDFLineEdit;

/// Create a self-signed certificate (Figma 436:10081, 436:10427 lists, 436:10928 errors, 436:11280
/// created; plan of step 5 of phase 2, 2.3, owner answer Q5.3 a, R5-21; package P3; shared with
/// Manage Certificates of step 6): the name, the e-mail, the organization, the unit, the country, the
/// length of the key (1024 / 2048 / 4096, 2048 by default), the validity, the file name and the
/// password twice. The core creates the .pfx (PDFCertificateManager::createCertificate, with
/// keyEncipherment, B99 - package P1b) and its public certificate goes to the certificate cache.
///
/// Opened by new + QPointer (never on the stack, never exec): it deletes itself when closed
/// (Qt::WA_DeleteOnClose); the opener keeps a QPointer. The password fields follow S5.1 / S5.2
/// (PDFLineEdit::setPasswordField, cleared when the dialog closes).
///
/// The form (436:10081): Name, E-mail, Organization (needed - the old dialog needed them too),
/// Organization unit (optional), Country ("CZ | Česko", sorted by the code, the region of the
/// system first chosen), Key length (436:10427: 1024 "Weak · only for old systems", 2048
/// "Recommended", 4096 "Strongest · slower signing"), Valid until (the calendar, earliest today,
/// five years ahead by default), Saved as (the next free cert_N.pfx of the certificate folder, not
/// editable), the password twice (it may be empty). A field shows its error after the user has
/// edited it (436:10928: "Enter a name.", "The passwords do not match."); Create Certificate is
/// Disabled until the form is valid. The key is generated in the GUI thread (a 4096-bit key takes
/// a moment, as in the old dialog).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCertificateCreateDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    /// create-dialog 436:10330: 640 wide
    static constexpr int CARD_WIDTH = 640;

    /// Key length chosen by default and the validity (436:10427)
    static constexpr int DEFAULT_KEY_LENGTH = 2048;
    static constexpr int DEFAULT_VALIDITY_YEARS = 5;

    /// Latest date of Valid until: the validity in seconds of the core is an int
    static constexpr int MAXIMUM_VALIDITY_YEARS = 50;

    /// Longest texts of the fields (RFC 5280, Appendix A: ub-common-name, ub-organization-name,
    /// ub-organizational-unit-name 64; ub-emailaddress-length 255) and of the password
    static constexpr int MAXIMUM_NAME_LENGTH = 64;
    static constexpr int MAXIMUM_EMAIL_LENGTH = 255;
    static constexpr int MAXIMUM_PASSWORD_LENGTH = 1024;

    explicit PDFAppCertificateCreateDialog(PDFAppContext* context, QWidget* parent);
    virtual ~PDFAppCertificateCreateDialog() override;

    /// The parts (tests)
    PDFLineEdit* getNameField() const;
    PDFLineEdit* getEmailField() const;
    PDFLineEdit* getOrganizationField() const;
    PDFLineEdit* getUnitField() const;
    PDFComboBox* getCountryCombo() const;
    PDFComboBox* getKeyLengthCombo() const;
    PDFDateTimeEdit* getValidUntilEdit() const;
    PDFLineEdit* getFileField() const;
    PDFLineEdit* getPasswordField() const;
    PDFLineEdit* getRepeatField() const;
    QPushButton* getCreateButton() const;
    QPushButton* getCancelButton() const;

    /// The errors shown under the fields (empty = none shown)
    QString getNameError() const;
    QString getEmailError() const;
    QString getOrganizationError() const;
    QString getRepeatError() const;

    /// The error of a failed creation shown above the buttons (empty = none)
    QString getCreateError() const;

    /// The absolute path of the file, which will be created
    const QString& getFilePath() const { return m_filePath; }

    /// The form is valid (Create Certificate is enabled)
    bool isFormValid() const;

    /// The validity in seconds from now to the end of the chosen day (at least 1)
    static int getValiditySeconds(const QDateTime& now, const QDate& validUntil);

    /// The two-letter code of the country of an item of Country ("CZ")
    QString getCountryCode() const;

    /// Creates the certificate (the button Create Certificate): false and the error shown on a
    /// failure; on a success the dialog closes and emits certificateCreated as its last call
    bool create();

Q_SIGNALS:
    /// The certificate was created into the file (its absolute path); the last call of the
    /// creation (plan 10.3: emit last), the dialog closes before it (deleted later)
    void certificateCreated(const QString& fileName);

protected:
    virtual void hideEvent(QHideEvent* event) override;

private:
    QWidget* createField(const QString& label, QWidget* control, PDFAppParagraph** errorText, QWidget* parent);
    QWidget* createRow(QWidget* first, QWidget* second);
    void fillCountries();
    void fillKeyLengths();
    void updateState();
    void updateKeyLengthChecks();
    void clearPasswords();
    void onFieldEdited();
    void onCreateClicked();

    QPointer<PDFAppContext> m_context;
    QString m_filePath;
    QPointer<PDFLineEdit> m_nameField;
    QPointer<PDFLineEdit> m_emailField;
    QPointer<PDFLineEdit> m_organizationField;
    QPointer<PDFLineEdit> m_unitField;
    QPointer<PDFComboBox> m_countryCombo;
    QPointer<PDFComboBox> m_keyLengthCombo;
    QPointer<PDFDateTimeEdit> m_validUntilEdit;
    QPointer<PDFLineEdit> m_fileField;
    QPointer<PDFLineEdit> m_passwordField;
    QPointer<PDFLineEdit> m_repeatField;
    QPointer<PDFAppParagraph> m_nameError;
    QPointer<PDFAppParagraph> m_emailError;
    QPointer<PDFAppParagraph> m_organizationError;
    QPointer<PDFAppParagraph> m_repeatError;
    QPointer<PDFAppParagraph> m_createError;
    QPointer<QPushButton> m_createButton;
    QPointer<QPushButton> m_cancelButton;
    bool m_isNameEdited = false;
    bool m_isEmailEdited = false;
    bool m_isOrganizationEdited = false;
    bool m_isCreating = false;
};

/// Import of a .pfx file into the certificate folder of the application (Figma 436:11538 - the
/// collision of the name; plan of step 5 of phase 2, 2.3, 7.2; package P3; shared with step 6): the
/// file comes only from the file dialog of the user, it is copied (at most 1 MiB, a PKCS#12, else an
/// error). start() imports the file at once and reports it (the dialog is never shown then);
/// otherwise the dialog shows why: a file of the same name in the certificate folder (436:11538: "A
/// certificate file with this name already exists" - rename the file or choose another one; a file
/// with the same bytes counts as imported) or a file, which cannot be imported (no frame, the same
/// dialog), with Cancel and Choose Another File…. Opened by new + QPointer, it deletes itself when
/// closed.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCertificateImportDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    /// Longest imported file
    static constexpr qint64 MAXIMUM_FILE_SIZE = 1024 * 1024;

    /// What start() has done
    enum class Outcome
    {
        None,       ///< Not started
        Imported,   ///< The file was copied (or the same file was there already)
        Collision,  ///< Another file of the same name is in the folder (436:11538)
        Refused     ///< The file cannot be imported (too large, no PKCS#12, unreadable, not written)
    };

    /// \param context Context of the application (the certificate folder, the cache)
    /// \param sourceFilePath The .pfx file chosen by the user in the file dialog
    /// \param parent Parent widget
    explicit PDFAppCertificateImportDialog(PDFAppContext* context, const QString& sourceFilePath, QWidget* parent);
    virtual ~PDFAppCertificateImportDialog() override;

    /// The filter of the file dialog of the import
    static QString getFileFilter();

    /// Imports the source file: copied - certificateImported as the last call and the dialog goes
    /// (deleted later, never shown); else the dialog opens with the reason
    void start();

    Outcome getOutcome() const { return m_outcome; }
    const QString& getSourceFilePath() const { return m_sourceFilePath; }
    QPushButton* getChooseButton() const;
    QPushButton* getCancelButton() const;

    /// Copies the file into the folder: Imported (\p targetPath the copy), Collision (\p targetPath
    /// the existing file) or Refused (\p errorMessage the reason); the cache gets the certificate of
    /// a file without a password (Q5.11)
    static Outcome importFile(PDFAppContext* context, const QString& sourceFilePath, const QString& folder, QString* targetPath, QString* errorMessage);

Q_SIGNALS:
    /// The file was imported (the absolute path of the copy); the last call of the import
    void certificateImported(const QString& fileName);

private:
    void onChooseClicked();

    QPointer<PDFAppContext> m_context;
    QString m_sourceFilePath;
    QPointer<QPushButton> m_chooseButton;
    QPointer<QPushButton> m_cancelButton;
    Outcome m_outcome = Outcome::None;
};

}   // namespace pdf

#endif // PDFAPPCERTIFICATEDIALOGS_H
