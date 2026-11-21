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

#include "pdfcertificatemanager.h"

#include <QDate>
#include <QFutureWatcher>
#include <QPointer>
#include <QString>

#include <atomic>
#include <memory>

class QPushButton;

namespace pdf
{

class PDFAppContext;
class PDFAppFileDialogRequest;
class PDFAppParagraph;
class PDFAppToolProgressCard;
class PDFComboBox;
class PDFDateTimeEdit;
class PDFLineEdit;

/// The result of the job of the creation of a certificate (plain values)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCertificateCreateResult
{
public:
    QByteArray pkcs12;              ///< The .pfx (PKCS#12 encrypted by the password), empty on a failure or a cancellation
    QByteArray certificateData;     ///< DER of the public certificate of the PKCS#12 (the certificate cache, Q5.11)
    QString errorMessage;           ///< Translated reason of a failure (never the password)
    bool isCanceled = false;        ///< The job was canceled (no error is shown)
};

/// The one job of the creation of a certificate of PDFAppCertificateCreateDialog (plan of step 6 of
/// phase 2, 6.4, B120, B137; QtConcurrent::run): it gets only values - the subject, the length of the
/// key, the validity and a copy of the password (NewCertificateInfo, a serial number of 127 random
/// bits of generateRandomBytes made by the core) - and the flag of the cancellation, which the callback
/// of the generation of the key reads (PDFSignatureFactory::createCertificate). It knows no QObject and
/// writes no file: it returns the bytes of the PKCS#12 and the public certificate; the GUI thread writes
/// the file. At its end (also after a cancellation) its copy of the password is cleared
/// (PDFSecureMemory::clear) and the thread resources of OpenSSL are released.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCertificateCreateJob
{
public:
    /// The generation of the key and the certificate (the tests give a generator, which waits)
    using Generator = QByteArray (*)(const PDFCertificateManager::NewCertificateInfo& info, const std::atomic<bool>* cancelFlag, QString* errorMessage);

    /// \param info The certificate; its password must be a copy of its own (detached), the job clears it
    /// \param cancelFlag The cancellation set by the dialog
    explicit PDFAppCertificateCreateJob(PDFCertificateManager::NewCertificateInfo info, std::shared_ptr<const std::atomic<bool>> cancelFlag);

    PDFAppCertificateCreateResult operator()();

    /// The generator of the jobs (tests); nullptr = PDFSignatureFactory::createCertificate
    static void setGenerator(Generator generator);

    /// The number of the jobs, whose copy of the password was non-empty and is cleared (only zero
    /// characters left) at their end - the check of the tests of S6.1 (the copy never stays)
    static int getClearedPasswordCount();

private:
    static std::atomic<Generator>& getGenerator();
    static std::atomic<int>& getClearedPasswordCounter();

    PDFCertificateManager::NewCertificateInfo m_info;
    std::shared_ptr<const std::atomic<bool>> m_cancelFlag;
};

/// Create a self-signed certificate (Figma 436:10081, 436:10427 lists, 436:10928 errors, 436:11280
/// created; plan of step 5 of phase 2, 2.3, owner answer Q5.3 a, R5-21; package P3; shared with
/// Manage Certificates of step 6): the name, the e-mail, the organization, the unit, the country, the
/// length of the key (2048 / 3072 / 4096, 2048 by default - owner answer Q6.14), the validity, the file
/// name and the password twice. The core creates the .pfx (PDFSignatureFactory::createCertificate:
/// X.509 v3, keyUsage digitalSignature, nonRepudiation, keyEncipherment, basicConstraints CA:FALSE,
/// B99, B130 - package P1b) and its public certificate goes to the certificate cache.
///
/// Opened by new + QPointer (never on the stack, never exec): it deletes itself when closed
/// (Qt::WA_DeleteOnClose); the opener keeps a QPointer. The password fields follow S5.1 / S5.2
/// (PDFLineEdit::setPasswordField, cleared when the dialog closes).
///
/// The form (436:10081): Name, E-mail, Organization (needed - the old dialog needed them too),
/// Organization unit (optional), Country ("CZ | Česko", sorted by the code, the region of the
/// system first chosen), Key length (436:10427: 2048 "Recommended", 3072 "Stronger · a little
/// slower", 4096 "Strongest · slower signing"), Valid until (the calendar, earliest today, five years
/// ahead by default), Saved as (the next free cert_N.pfx of the certificate folder, not editable), the
/// password twice (it may be empty). A field shows its error after the user has edited it (436:10928:
/// "Enter a name.", "The passwords do not match."); Create Certificate is Disabled until the form is
/// valid.
///
/// The generation (plan of step 6 of phase 2, 6.4; no frame - 436:10081 with the card of the
/// progress): Create Certificate starts one job PDFAppCertificateCreateJob by QtConcurrent::run; the
/// form is Disabled, the card "Creating the certificate…" is shown, Cancel stays. Cancel or the close
/// of the dialog (also with its tab) cancels the job: the dialog closes, no file is written, nothing is
/// emitted. The QFutureWatcher is a child of the dialog: the result of a dialog, which is gone, is
/// dropped. After the result the GUI thread writes the file by QSaveFile (a failure shows its error
/// and the form again), stores the public certificate into the cache, closes the dialog and emits
/// certificateCreated as its last call. A second start while the job runs does nothing.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCertificateCreateDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    /// create-dialog 436:10330: 640 wide
    static constexpr int CARD_WIDTH = 640;

    /// Key length chosen by default and the validity (436:10427, Q6.14)
    static constexpr int DEFAULT_KEY_LENGTH = PDFCertificateManager::DEFAULT_RSA_KEY_LENGTH;
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
    PDFAppToolProgressCard* getProgressCard() const;

    /// The job of the generation runs (the card of the progress is shown)
    bool isCreating() const;

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

    /// Starts the creation of the certificate (the button Create Certificate): the job of the
    /// generation. \returns false, when it does not start (an invalid form, a job running already).
    /// The result comes later: on a success the dialog closes and emits certificateCreated as its
    /// last call; a failure shows its error.
    bool create();

Q_SIGNALS:
    /// The certificate was created into the file (its absolute path); the last call of the
    /// creation (plan 10.3: emit last), the dialog closes before it (deleted later)
    void certificateCreated(const QString& fileName);

protected:
    virtual void hideEvent(QHideEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;

private:
    QWidget* createField(const QString& label, QWidget* control, PDFAppParagraph** errorText, QWidget* parent);
    QWidget* createRow(QWidget* first, QWidget* second);
    void fillCountries();
    void fillKeyLengths();
    void updateState();
    void updateKeyLengthChecks();
    void clearPasswords();
    void cancelJob();
    void showCreateError(const QString& error);
    void onFieldEdited();
    void onCreateClicked();
    void onJobFinished();

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
    QPointer<PDFAppToolProgressCard> m_progressCard;
    QPointer<QFutureWatcher<PDFAppCertificateCreateResult>> m_watcher;  ///< A child of the dialog: the result of a dialog, which is gone, is dropped
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;                    ///< Shared with the job (a plain value, no QObject)
    bool m_isNameEdited = false;
    bool m_isEmailEdited = false;
    bool m_isOrganizationEdited = false;
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
    void onSourceFileChosen(const QString& filePath);

    QPointer<PDFAppContext> m_context;
    QString m_sourceFilePath;
    QPointer<QPushButton> m_chooseButton;
    QPointer<QPushButton> m_cancelButton;
    QPointer<PDFAppFileDialogRequest> m_fileRequest;   ///< Choose Another File… waiting for its file dialog (a child of the dialog, A0)
    Outcome m_outcome = Outcome::None;
};

}   // namespace pdf

#endif // PDFAPPCERTIFICATEDIALOGS_H
