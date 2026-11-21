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

#include "pdfappcertificatedialogs.h"
#include "pdfappcontext.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolcertificatelist.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpathrules.h"
#include "pdfappworkspace.h"

#include "pdfcertificatemanager.h"
#include "pdfcomponentdelegates.h"
#include "pdfexception.h"
#include "pdffluentwidgets.h"
#include "pdfsecurityhandler.h"
#include "pdfsignaturehandler.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <optional>

namespace pdf
{

/// File-local constants and helpers of the dialogs of the certificates
class PDFAppCertificateDialogsHelper
{
public:
    PDFAppCertificateDialogsHelper() = delete;

    /// create-dialog 436:10330: two fields in a row 12 apart; a field: the label, the control and
    /// the error 4 apart
    static constexpr int ROW_GAP = 12;
    static constexpr int FIELD_GAP = 4;

    /// The collision of the import 436:11538: 480 wide
    static constexpr int IMPORT_CARD_WIDTH = 480;

    /// Label of a field (Medium 12 text/secondary, line 15)
    static PDFLabel* createFieldLabel(const QString& text, QWidget* parent)
    {
        PDFLabel* label = PDFAppPageWidgets::createLabel(text, QFont::Medium, 12, PDFThemeColors::TextSecondary, parent);
        label->setLineHeight(15);
        return label;
    }

    /// A text under a field: Regular 11 in lines of 13 (436:11193 the error in text/danger,
    /// 436:10412 the hint in text/muted)
    static PDFAppParagraph* createSmallText(const QString& text, PDFThemeColors::Color color, QWidget* parent)
    {
        return PDFAppPageWidgets::createText(text, PDFThemeFonts::getFont(QFont::Normal, 11), 13, color, parent);
    }

    /// The e-mail is one address: a local part, '@' and a domain, no spaces
    static bool isValidEmail(const QString& email)
    {
        const QString trimmed = email.trimmed();
        const qsizetype at = trimmed.indexOf(QLatin1Char('@'));
        return at > 0 && at == trimmed.lastIndexOf(QLatin1Char('@')) && at < trimmed.size() - 1 && !trimmed.contains(QLatin1Char(' '));
    }

    /// Reads at most \p maximumSize + 1 bytes of a file (a larger file is never read whole)
    static std::optional<QByteArray> readBounded(const QString& filePath, qint64 maximumSize)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly))
        {
            return std::nullopt;
        }
        return file.read(maximumSize + 1);
    }

    /// Today in the local time of the clock of the application (PDFAppLocale::getCurrentTime)
    static QDateTime getNow()
    {
        return QDateTime::fromMSecsSinceEpoch(PDFAppLocale::getCurrentTime());
    }

    /// A tile row of a list of a combo box (436:10427): the icon of the tile, the title, the
    /// description; the chosen item has ✓ (Qt::CheckStateRole of the delegate)
    static void setChoiceItem(PDFComboBox* combo, int index, const QString& iconName, const QString& description)
    {
        combo->setItemData(index, iconName, PDFComponentRoles::IconNameRole);
        if (!description.isEmpty())
        {
            combo->setItemData(index, description, PDFComponentRoles::SubtitleRole);
        }
    }

    /// ✓ of the current item of a combo box with a tile row delegate
    static void updateChecks(PDFComboBox* combo)
    {
        for (int i = 0; i < combo->count(); ++i)
        {
            combo->setItemData(i, i == combo->currentIndex() ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
        }
    }

    /// The tile rows of the lists of 436:10427 (Country, Key length)
    static void setTileDelegate(PDFComboBox* combo)
    {
        PDFTileRowDelegate* delegate = new PDFTileRowDelegate(combo);
        delegate->setKind(PDFTileRowDelegate::Kind::MenuItem);
        combo->setItemDelegate(delegate);
    }
};

// ------------------------------------------------------------ PDFAppCertificateCreateJob

PDFAppCertificateCreateJob::PDFAppCertificateCreateJob(PDFCertificateManager::NewCertificateInfo info, std::shared_ptr<const std::atomic<bool>> cancelFlag) :
    m_info(std::move(info)),
    m_cancelFlag(std::move(cancelFlag))
{

}

PDFAppCertificateCreateResult PDFAppCertificateCreateJob::operator()()
{
    // A worker thread: only the values of the job, no QObject (plan 6.4)
    PDFAppCertificateCreateResult result;
    const std::atomic<bool>* cancelFlag = m_cancelFlag.get();
    const Generator generator = getGenerator().load();

    QString error;
    try
    {
        result.pkcs12 = generator ? generator(m_info, cancelFlag, &error) : PDFSignatureFactory::createCertificate(m_info, cancelFlag, &error);

        // Q5.11: the public certificate of the new file goes to the cache (read here, while the
        // copy of the password lives)
        if (!result.pkcs12.isEmpty())
        {
            if (std::optional<PDFCertificateInfo> certificate = PDFCertificateManager::readCertificate(result.pkcs12, m_info.privateKeyPasword))
            {
                result.certificateData = certificate->getCertificateData();
            }
        }
    }
    catch (const PDFException& exception)
    {
        error = exception.getMessage();
        PDFSecureMemory::clear(result.pkcs12);
        result.pkcs12.clear();
    }

    result.isCanceled = cancelFlag && cancelFlag->load();
    if (result.isCanceled || result.certificateData.isEmpty())
    {
        // A canceled job gives nothing; a PKCS#12, which does not open by its password, is no result
        // (S6.1: the bytes with the private key are overwritten before they are freed)
        PDFSecureMemory::clear(result.pkcs12);
        result.pkcs12.clear();
        result.certificateData.clear();
    }
    result.errorMessage = result.isCanceled ? QString() : error;

    // S6.1: the copy of the password of the job is cleared at its end (also after a cancellation)
    const bool hasPassword = !m_info.privateKeyPasword.isEmpty();
    PDFSecureMemory::clear(m_info.privateKeyPasword);
    bool isCleared = hasPassword;
    for (const QChar character : std::as_const(m_info.privateKeyPasword))
    {
        isCleared = isCleared && character.isNull();
    }
    if (isCleared)
    {
        ++getClearedPasswordCounter();
    }
    m_info.privateKeyPasword.clear();

    // B137: the state of OpenSSL of this thread of the pool goes with the job
    PDFSignatureHandler::releaseThreadResources();
    return result;
}

void PDFAppCertificateCreateJob::setGenerator(Generator generator)
{
    getGenerator().store(generator);
}

int PDFAppCertificateCreateJob::getClearedPasswordCount()
{
    return getClearedPasswordCounter().load();
}

std::atomic<PDFAppCertificateCreateJob::Generator>& PDFAppCertificateCreateJob::getGenerator()
{
    static std::atomic<Generator> generator = nullptr;
    return generator;
}

std::atomic<int>& PDFAppCertificateCreateJob::getClearedPasswordCounter()
{
    static std::atomic<int> counter = 0;
    return counter;
}

// ------------------------------------------------------------ PDFAppCertificateCreateDialog

PDFAppCertificateCreateDialog::PDFAppCertificateCreateDialog(PDFAppContext* context, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Information, tr("Create a self-signed certificate"),
              tr("A digital ID for signing and encryption. Others have to add it to their trusted certificates to see your signatures as valid."),
              QDialogButtonBox::NoButton, parent),
    m_context(context)
{
    using Helper = PDFAppCertificateDialogsHelper;

    setObjectName(QStringLiteral("certificateCreateDialog"));
    setAttribute(Qt::WA_DeleteOnClose);
    setCardWidth(CARD_WIDTH);
    PDFAppMainWindow::prepareDialog(this, parent);

    m_filePath = PDFCertificateManager::generateCertificateFileName();

    // 436:10342: Name, E-mail
    m_nameField = new PDFLineEdit(this);
    m_nameField->setObjectName(QStringLiteral("certificateName"));
    m_nameField->setMaxLength(MAXIMUM_NAME_LENGTH);
    m_emailField = new PDFLineEdit(this);
    m_emailField->setObjectName(QStringLiteral("certificateEmail"));
    m_emailField->setMaxLength(MAXIMUM_EMAIL_LENGTH);
    PDFAppParagraph* nameError = nullptr;
    PDFAppParagraph* emailError = nullptr;
    QWidget* nameField = createField(tr("Name"), m_nameField, &nameError, this);
    QWidget* emailField = createField(tr("E-mail"), m_emailField, &emailError, this);
    m_nameError = nameError;
    m_emailError = emailError;
    addContent(createRow(nameField, emailField));

    // 436:10351: Organization, Organization unit (optional)
    m_organizationField = new PDFLineEdit(this);
    m_organizationField->setObjectName(QStringLiteral("certificateOrganization"));
    m_organizationField->setMaxLength(MAXIMUM_NAME_LENGTH);
    m_unitField = new PDFLineEdit(this);
    m_unitField->setObjectName(QStringLiteral("certificateUnit"));
    m_unitField->setMaxLength(MAXIMUM_NAME_LENGTH);
    PDFAppParagraph* organizationError = nullptr;
    QWidget* organizationField = createField(tr("Organization"), m_organizationField, &organizationError, this);
    m_organizationError = organizationError;
    addContent(createRow(organizationField, createField(tr("Organization unit (optional)"), m_unitField, nullptr, this)));

    // 436:10368: Country, Key length
    m_countryCombo = new PDFComboBox(this);
    m_countryCombo->setObjectName(QStringLiteral("certificateCountry"));
    fillCountries();
    m_keyLengthCombo = new PDFComboBox(this);
    m_keyLengthCombo->setObjectName(QStringLiteral("certificateKeyLength"));
    fillKeyLengths();
    addContent(createRow(createField(tr("Country"), m_countryCombo, nullptr, this), createField(tr("Key length"), m_keyLengthCombo, nullptr, this)));

    // 436:10376: Valid until (earliest today, five years ahead by default), Saved as
    const QDate today = Helper::getNow().toLocalTime().date();
    m_validUntilEdit = new PDFDateTimeEdit(today.addYears(DEFAULT_VALIDITY_YEARS), this);
    m_validUntilEdit->setObjectName(QStringLiteral("certificateValidUntil"));
    m_validUntilEdit->setLocale(PDFAppLocale::getUiLocale());
    m_validUntilEdit->setDisplayFormat(QCoreApplication::translate("pdf::PDFAppLocale", "MMM d, yyyy", "Format of a date of the user interface (QLocale::toString): Oct 23, 2026"));
    m_validUntilEdit->setDateRange(today, today.addYears(MAXIMUM_VALIDITY_YEARS));
    m_validUntilEdit->setCalendarPopupEnabled(true);
    m_fileField = new PDFLineEdit(QFileInfo(m_filePath).fileName(), this);
    m_fileField->setObjectName(QStringLiteral("certificateFile"));
    m_fileField->setReadOnly(true);
    m_fileField->setEnabled(false);
    addContent(createRow(createField(tr("Valid until"), m_validUntilEdit, nullptr, this), createField(tr("Saved as"), m_fileField, nullptr, this)));

    // 436:10407: Password, Repeat password (S5.1, S5.2: the field never gives its text away)
    m_passwordField = new PDFLineEdit(this);
    m_passwordField->setObjectName(QStringLiteral("certificatePassword"));
    m_passwordField->setPasswordField();
    m_passwordField->setMaxLength(MAXIMUM_PASSWORD_LENGTH);
    m_repeatField = new PDFLineEdit(this);
    m_repeatField->setObjectName(QStringLiteral("certificateRepeatPassword"));
    m_repeatField->setPasswordField();
    m_repeatField->setMaxLength(MAXIMUM_PASSWORD_LENGTH);
    PDFAppParagraph* repeatError = nullptr;
    QWidget* repeatField = createField(tr("Repeat password"), m_repeatField, &repeatError, this);
    m_repeatError = repeatError;
    addContent(createRow(createField(tr("Password"), m_passwordField, nullptr, this), repeatField));

    // 436:10412: the hint; the error of a failed creation (no frame) below it
    PDFAppParagraph* hint = Helper::createSmallText(tr("The password protects the private key in the .pfx file. It can be left empty, but then anyone with the file can sign as you."),
                                                    PDFThemeColors::TextMuted, this);
    hint->setObjectName(QStringLiteral("certificateHint"));
    addContent(hint);
    m_createError = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextDanger, this);
    m_createError->setObjectName(QStringLiteral("certificateCreateError"));
    m_createError->hide();
    addContent(m_createError);

    // 6.4 (no frame, 436:10081 with the card of the progress of a tool): the generation of the key
    // has no known progress - a busy bar
    m_progressCard = new PDFAppToolProgressCard(this);
    m_progressCard->setObjectName(QStringLiteral("certificateProgress"));
    m_progressCard->getBar()->setRange(0, 0);
    m_progressCard->hide();
    addContent(m_progressCard);

    // 436:10413: Cancel, Create Certificate (Primary with certificate-add)
    m_cancelButton = addActionButton(tr("Cancel"), ActionPosition::Trailing, false);
    m_cancelButton->setObjectName(QStringLiteral("certificateCancel"));
    m_createButton = addActionButton(tr("Create Certificate"), ActionPosition::Trailing, true);
    m_createButton->setObjectName(QStringLiteral("certificateCreate"));
    m_createButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("certificate-add")));

    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppCertificateCreateDialog::reject);
    connect(m_createButton.data(), &QPushButton::clicked, this, &PDFAppCertificateCreateDialog::onCreateClicked);
    connect(m_nameField.data(), &QLineEdit::textEdited, this, &PDFAppCertificateCreateDialog::onFieldEdited);
    connect(m_emailField.data(), &QLineEdit::textEdited, this, &PDFAppCertificateCreateDialog::onFieldEdited);
    connect(m_organizationField.data(), &QLineEdit::textEdited, this, &PDFAppCertificateCreateDialog::onFieldEdited);
    connect(m_passwordField.data(), &QLineEdit::textChanged, this, &PDFAppCertificateCreateDialog::updateState);
    connect(m_repeatField.data(), &QLineEdit::textChanged, this, &PDFAppCertificateCreateDialog::updateState);
    connect(m_keyLengthCombo.data(), &QComboBox::currentIndexChanged, this, &PDFAppCertificateCreateDialog::updateKeyLengthChecks);
    connect(m_countryCombo.data(), &QComboBox::currentIndexChanged, this, &PDFAppCertificateCreateDialog::updateKeyLengthChecks);

    updateState();
    m_nameField->setFocus();
}

PDFAppCertificateCreateDialog::~PDFAppCertificateCreateDialog()
{
    // The job of a dialog, which goes, is canceled; its result is dropped with the watcher
    cancelJob();
    clearPasswords();
}

PDFLineEdit* PDFAppCertificateCreateDialog::getNameField() const
{
    return m_nameField.data();
}

PDFLineEdit* PDFAppCertificateCreateDialog::getEmailField() const
{
    return m_emailField.data();
}

PDFLineEdit* PDFAppCertificateCreateDialog::getOrganizationField() const
{
    return m_organizationField.data();
}

PDFLineEdit* PDFAppCertificateCreateDialog::getUnitField() const
{
    return m_unitField.data();
}

PDFComboBox* PDFAppCertificateCreateDialog::getCountryCombo() const
{
    return m_countryCombo.data();
}

PDFComboBox* PDFAppCertificateCreateDialog::getKeyLengthCombo() const
{
    return m_keyLengthCombo.data();
}

PDFDateTimeEdit* PDFAppCertificateCreateDialog::getValidUntilEdit() const
{
    return m_validUntilEdit.data();
}

PDFLineEdit* PDFAppCertificateCreateDialog::getFileField() const
{
    return m_fileField.data();
}

PDFLineEdit* PDFAppCertificateCreateDialog::getPasswordField() const
{
    return m_passwordField.data();
}

PDFLineEdit* PDFAppCertificateCreateDialog::getRepeatField() const
{
    return m_repeatField.data();
}

QPushButton* PDFAppCertificateCreateDialog::getCreateButton() const
{
    return m_createButton.data();
}

QPushButton* PDFAppCertificateCreateDialog::getCancelButton() const
{
    return m_cancelButton.data();
}

PDFAppToolProgressCard* PDFAppCertificateCreateDialog::getProgressCard() const
{
    return m_progressCard.data();
}

bool PDFAppCertificateCreateDialog::isCreating() const
{
    return m_watcher != nullptr;
}

QString PDFAppCertificateCreateDialog::getNameError() const
{
    return m_nameError && !m_nameError->isHidden() ? m_nameError->text() : QString();
}

QString PDFAppCertificateCreateDialog::getEmailError() const
{
    return m_emailError && !m_emailError->isHidden() ? m_emailError->text() : QString();
}

QString PDFAppCertificateCreateDialog::getOrganizationError() const
{
    return m_organizationError && !m_organizationError->isHidden() ? m_organizationError->text() : QString();
}

QString PDFAppCertificateCreateDialog::getRepeatError() const
{
    return m_repeatError && !m_repeatError->isHidden() ? m_repeatError->text() : QString();
}

QString PDFAppCertificateCreateDialog::getCreateError() const
{
    return m_createError && !m_createError->isHidden() ? m_createError->text() : QString();
}

bool PDFAppCertificateCreateDialog::isFormValid() const
{
    if (!m_nameField || !m_emailField || !m_organizationField || !m_passwordField || !m_repeatField || !m_keyLengthCombo || !m_validUntilEdit)
    {
        return false;
    }

    return !m_nameField->text().trimmed().isEmpty() &&
           PDFAppCertificateDialogsHelper::isValidEmail(m_emailField->text()) &&
           !m_organizationField->text().trimmed().isEmpty() &&
           m_passwordField->text() == m_repeatField->text() &&
           m_keyLengthCombo->currentData().toInt() > 0;
}

int PDFAppCertificateCreateDialog::getValiditySeconds(const QDateTime& now, const QDate& validUntil)
{
    // The certificate is valid to the end of the chosen day of the local time
    const QDateTime end(validUntil, QTime(23, 59, 59));
    const qint64 seconds = now.secsTo(end);
    return int(qBound(qint64(1), seconds, qint64((std::numeric_limits<int>::max)())));
}

QString PDFAppCertificateCreateDialog::getCountryCode() const
{
    return m_countryCombo ? m_countryCombo->currentData().toString() : QString();
}

bool PDFAppCertificateCreateDialog::create()
{
    using Helper = PDFAppCertificateDialogsHelper;

    // One job at a time: a second start (a double click, Enter) does nothing
    if (isCreating() || !isFormValid())
    {
        return false;
    }

    // The values of the job (6.4): the subject, the key, the validity and a copy of the password,
    // which has a buffer of its own (the text of the field shares its buffer - the job clears its
    // copy at its end). The serial number is 127 random bits of generateRandomBytes (the core,
    // serialNumber 1; RFC 5280, 4.1.2.2): two certificates of the same name are told apart by the
    // issuer and serial number of a recipient of PKCS#7 (ISO 32000-2, 7.6.5).
    PDFCertificateManager::NewCertificateInfo info;
    const QString password = m_passwordField->text();
    info.privateKeyPasword = QString(password.constData(), password.size());
    info.certCountryCode = getCountryCode();
    info.certOrganization = m_organizationField->text().trimmed();
    info.certOrganizationUnit = m_unitField ? m_unitField->text().trimmed() : QString();
    info.certCommonName = m_nameField->text().trimmed();
    info.certEmail = m_emailField->text().trimmed();
    info.rsaKeyLength = m_keyLengthCombo->currentData().toInt();
    info.validityInSeconds = getValiditySeconds(Helper::getNow(), m_validUntilEdit->date());
    info.serialNumber = 1;

    if (m_createError)
    {
        m_createError->hide();
    }
    if (m_progressCard)
    {
        m_progressCard->setProgress(0, tr("Creating the certificate…"), tr("Generating an RSA key of %1 bits. A longer key takes longer.").arg(info.rsaKeyLength), QString());
    }

    // The watcher is a child of the dialog: the result of a dialog, which is gone, is dropped
    m_cancelFlag = std::make_shared<std::atomic<bool>>(false);
    QFutureWatcher<PDFAppCertificateCreateResult>* watcher = new QFutureWatcher<PDFAppCertificateCreateResult>(this);
    m_watcher = watcher;
    connect(watcher, &QFutureWatcherBase::finished, this, &PDFAppCertificateCreateDialog::onJobFinished);
    watcher->setFuture(QtConcurrent::run(PDFAppCertificateCreateJob(std::move(info), m_cancelFlag)));
    updateState();
    return true;
}

void PDFAppCertificateCreateDialog::onJobFinished()
{
    QFutureWatcher<PDFAppCertificateCreateResult>* watcher = m_watcher;
    if (!watcher || sender() != watcher)
    {
        return;
    }

    // The result is taken out of the future (not copied): its PKCS#12 is the only holder of the bytes,
    // so PDFSecureMemory::clear below overwrites them (a copy would share them with the store of the
    // future, which frees them unwiped - review RS6APP, S6.1)
    QFuture<PDFAppCertificateCreateResult> future = watcher->future();
    PDFAppCertificateCreateResult result = future.isValid() && future.isResultReadyAt(0) ? future.takeResult() : PDFAppCertificateCreateResult();
    m_watcher = nullptr;
    m_cancelFlag.reset();
    watcher->deleteLater();

    if (result.isCanceled)
    {
        // Nothing is written (a canceled job of a dialog, which is still open)
        updateState();
        return;
    }

    if (result.pkcs12.isEmpty())
    {
        showCreateError(result.errorMessage.isEmpty() ? tr("The certificate cannot be created.") : result.errorMessage);
        return;
    }

    // The GUI thread writes the file (6.4): the name proposed, when the dialog opened, may be taken
    // meanwhile; an existing file is never replaced
    if (QFileInfo::exists(m_filePath))
    {
        m_filePath = PDFCertificateManager::generateCertificateFileName();
        if (m_fileField)
        {
            m_fileField->setText(QFileInfo(m_filePath).fileName());
        }
    }
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSaveFile file(m_filePath);
    const bool isWritten = !QFileInfo::exists(m_filePath) && file.open(QIODevice::WriteOnly) && file.write(result.pkcs12) == result.pkcs12.size() && file.commit();
    PDFSecureMemory::clear(result.pkcs12);
    if (!isWritten)
    {
        showCreateError(tr("The certificate file %1 cannot be written into your certificate folder.").arg(QFileInfo(m_filePath).fileName()));
        return;
    }

    // Q5.11: the public certificate goes to the cache (only the certificate, never the key). The
    // commit notifies the database at once (PDFDatabaseNotifier): code of the application runs.
    QPointer<PDFAppCertificateCreateDialog> guard(this);
    PDFAppToolCertificateProvider(m_context).storeCertificate(m_filePath, result.certificateData);
    if (!guard)
    {
        return;
    }

    // Emit last: the dialog is closed (deleted later) before the signal
    const QString filePath = m_filePath;
    clearPasswords();
    close();
    Q_EMIT certificateCreated(filePath);
}

void PDFAppCertificateCreateDialog::cancelJob()
{
    // The callback of the generation reads the flag; the result of the job is dropped (no file)
    if (m_cancelFlag)
    {
        m_cancelFlag->store(true);
        m_cancelFlag.reset();
    }
    if (m_watcher)
    {
        m_watcher->disconnect(this);
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }
}

void PDFAppCertificateCreateDialog::showCreateError(const QString& error)
{
    if (m_createError)
    {
        m_createError->setText(error);
        m_createError->show();
    }
    updateState();
}

void PDFAppCertificateCreateDialog::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);

    // 436:10081 (review RS6FIGMA): the message box takes its height before the fields are polished, so
    // the fields came out 4 px shorter than their 34 / 40 and were cut. Once the dialog is shown and laid
    // out, updateState gives it the height of its content (queued: the layouts settle in the event loop;
    // the call is dropped with the dialog)
    QMetaObject::invokeMethod(this, &PDFAppCertificateCreateDialog::updateState, Qt::QueuedConnection);
}

void PDFAppCertificateCreateDialog::hideEvent(QHideEvent* event)
{
    // 6.4: the close of the dialog (Cancel, Esc, the close of its tool or tab) cancels the job;
    // S5.1: the passwords do not stay in a hidden dialog. A hide by the window system (the main window
    // minimized) is no close: the job goes on (review RS6APP - it was canceled with a stuck dialog)
    if (!event->spontaneous())
    {
        cancelJob();
        clearPasswords();
    }
    BaseClass::hideEvent(event);
}

QWidget* PDFAppCertificateCreateDialog::createField(const QString& label, QWidget* control, PDFAppParagraph** errorText, QWidget* parent)
{
    using Helper = PDFAppCertificateDialogsHelper;

    // field-Name 436:10343: the label, the control, the error, 4 apart
    QWidget* field = new QWidget(parent);
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(field, Helper::FIELD_GAP);
    layout->addWidget(Helper::createFieldLabel(label, field));
    control->setParent(field);
    control->setAccessibleName(label);
    // The control keeps its height of the design (34, a password field 40), when an error appears
    // under a field of the row; the width is shared 1:1 by the two fields of the row (436:10368)
    control->setMinimumHeight(control->sizeHint().height());
    control->setMinimumWidth(1);
    layout->addWidget(control);
    if (errorText)
    {
        *errorText = Helper::createSmallText(QString(), PDFThemeColors::TextDanger, field);
        (*errorText)->hide();
        layout->addWidget(*errorText);
    }
    layout->addStretch(1);
    return field;
}

QWidget* PDFAppCertificateCreateDialog::createRow(QWidget* first, QWidget* second)
{
    QWidget* row = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(PDFAppCertificateDialogsHelper::ROW_GAP);
    first->setParent(row);
    second->setParent(row);
    first->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    second->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(first, 1, Qt::AlignTop);
    layout->addWidget(second, 1, Qt::AlignTop);
    return row;
}

void PDFAppCertificateCreateDialog::fillCountries()
{
    // 436:10427: "CZ | Česko" by the native name of the main locale of the territory, sorted by
    // the code; the region of the system is chosen first (the old dialog chose it too)
    std::map<QString, QString> countries;
    const QList<QLocale> locales = QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyTerritory);
    for (const QLocale& locale : locales)
    {
        const QLocale::Territory territory = locale.territory();
        if (territory == QLocale::AnyTerritory)
        {
            continue;
        }

        const QString code = QLocale::territoryToCode(territory);
        if (code.size() != 2 || countries.count(code))
        {
            continue;
        }

        QString name = QLocale(QLocale::AnyLanguage, territory).nativeTerritoryName();
        if (name.isEmpty())
        {
            name = locale.nativeTerritoryName();
        }
        if (!name.isEmpty())
        {
            countries[code] = name;
        }
    }

    for (const auto& [code, name] : countries)
    {
        m_countryCombo->addItem(tr("%1 | %2", "Country of a certificate: CZ | Česko").arg(code, name), code);
        PDFAppCertificateDialogsHelper::setChoiceItem(m_countryCombo, m_countryCombo->count() - 1, QStringLiteral("globe"), QString());
    }
    PDFAppCertificateDialogsHelper::setTileDelegate(m_countryCombo);

    const int regional = m_countryCombo->findData(QLocale::territoryToCode(PDFAppLocale::getRegionalLocale().territory()));
    m_countryCombo->setCurrentIndex(regional >= 0 ? regional : 0);
    PDFAppCertificateDialogsHelper::updateChecks(m_countryCombo);
}

void PDFAppCertificateCreateDialog::fillKeyLengths()
{
    // 436:10427 (owner answer Q6.14: no RSA 1024): the three lengths of RSA with their
    // descriptions, 2048 by default - the lengths of PDFSignatureFactory::createCertificate
    static_assert(std::size(PDFCertificateManager::RSA_KEY_LENGTHS) == 3, "Each length of the core has its row of 436:10427");
    m_keyLengthCombo->addItem(tr("2048 bits"), PDFCertificateManager::RSA_KEY_LENGTHS[0]);
    PDFAppCertificateDialogsHelper::setChoiceItem(m_keyLengthCombo, 0, QStringLiteral("key"), tr("Recommended"));
    m_keyLengthCombo->addItem(tr("3072 bits"), PDFCertificateManager::RSA_KEY_LENGTHS[1]);
    PDFAppCertificateDialogsHelper::setChoiceItem(m_keyLengthCombo, 1, QStringLiteral("key"), tr("Stronger · a little slower"));
    m_keyLengthCombo->addItem(tr("4096 bits"), PDFCertificateManager::RSA_KEY_LENGTHS[2]);
    PDFAppCertificateDialogsHelper::setChoiceItem(m_keyLengthCombo, 2, QStringLiteral("key"), tr("Strongest · slower signing"));
    PDFAppCertificateDialogsHelper::setTileDelegate(m_keyLengthCombo);
    m_keyLengthCombo->setCurrentIndex(m_keyLengthCombo->findData(DEFAULT_KEY_LENGTH));
    PDFAppCertificateDialogsHelper::updateChecks(m_keyLengthCombo);
}

void PDFAppCertificateCreateDialog::updateState()
{
    using Helper = PDFAppCertificateDialogsHelper;

    // 436:10928: an error is shown under a field, which the user has edited
    if (m_nameField && m_nameError)
    {
        const bool isInvalid = m_isNameEdited && m_nameField->text().trimmed().isEmpty();
        m_nameError->setText(tr("Enter a name."));
        m_nameError->setVisible(isInvalid);
        m_nameField->setInvalid(isInvalid, isInvalid ? m_nameError->text() : QString());
    }
    if (m_emailField && m_emailError)
    {
        const QString email = m_emailField->text().trimmed();
        const bool isInvalid = m_isEmailEdited && !Helper::isValidEmail(email);
        m_emailError->setText(email.isEmpty() ? tr("Enter an e-mail address.") : tr("Enter a valid e-mail address."));
        m_emailError->setVisible(isInvalid);
        m_emailField->setInvalid(isInvalid, isInvalid ? m_emailError->text() : QString());
    }
    if (m_organizationField && m_organizationError)
    {
        const bool isInvalid = m_isOrganizationEdited && m_organizationField->text().trimmed().isEmpty();
        m_organizationError->setText(tr("Enter an organization."));
        m_organizationError->setVisible(isInvalid);
        m_organizationField->setInvalid(isInvalid, isInvalid ? m_organizationError->text() : QString());
    }
    if (m_passwordField && m_repeatField && m_repeatError)
    {
        // The repetition is checked, when it is typed
        const bool isMismatch = !m_repeatField->text().isEmpty() && m_passwordField->text() != m_repeatField->text();
        m_repeatError->setText(tr("The passwords do not match."));
        m_repeatError->setVisible(isMismatch);
        m_repeatField->setInvalid(isMismatch, isMismatch ? m_repeatError->text() : QString());
    }
    // 6.4: the form is Disabled while the job runs, the card of the progress is shown; Cancel stays
    const bool isRunning = isCreating();
    for (QWidget* field : std::initializer_list<QWidget*>{ m_nameField, m_emailField, m_organizationField, m_unitField, m_countryCombo, m_keyLengthCombo, m_validUntilEdit, m_passwordField, m_repeatField })
    {
        if (field)
        {
            field->setEnabled(!isRunning);
        }
    }
    if (m_progressCard)
    {
        m_progressCard->setVisible(isRunning);
    }
    if (m_createButton)
    {
        m_createButton->setEnabled(isFormValid() && !isRunning);
    }

    // The card follows the height of its content at its width: a shown or hidden error changes it.
    // The nested layouts of the rows cache their heights until their own layout requests come from
    // the event loop, so they are invalidated first.
    for (QLayout* nestedLayout : findChildren<QLayout*>())
    {
        nestedLayout->invalidate();
    }
    resize(width(), qMax(minimumHeight(), sizeHint().height()));
}

void PDFAppCertificateCreateDialog::updateKeyLengthChecks()
{
    if (m_keyLengthCombo)
    {
        PDFAppCertificateDialogsHelper::updateChecks(m_keyLengthCombo);
    }
    if (m_countryCombo)
    {
        PDFAppCertificateDialogsHelper::updateChecks(m_countryCombo);
    }
}

void PDFAppCertificateCreateDialog::clearPasswords()
{
    // QLineEdit::setText drops the undo history of the field (Ctrl+Z brings nothing back); clear
    // would keep the removed password in it - undoable, when the field shows the password
    if (m_passwordField)
    {
        m_passwordField->setText(QString());
    }
    if (m_repeatField)
    {
        m_repeatField->setText(QString());
    }
}

void PDFAppCertificateCreateDialog::onFieldEdited()
{
    const QObject* field = sender();
    m_isNameEdited = m_isNameEdited || field == m_nameField;
    m_isEmailEdited = m_isEmailEdited || field == m_emailField;
    m_isOrganizationEdited = m_isOrganizationEdited || field == m_organizationField;
    if (m_createError)
    {
        m_createError->hide();
    }
    updateState();
}

void PDFAppCertificateCreateDialog::onCreateClicked()
{
    // The last call: create may close the dialog and emit
    create();
}

// ------------------------------------------------------------ PDFAppCertificateImportDialog

PDFAppCertificateImportDialog::PDFAppCertificateImportDialog(PDFAppContext* context, const QString& sourceFilePath, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, tr("A certificate file with this name already exists"), QString(), QDialogButtonBox::NoButton, parent),
    m_context(context),
    m_sourceFilePath(sourceFilePath)
{
    setObjectName(QStringLiteral("certificateImportDialog"));
    setAttribute(Qt::WA_DeleteOnClose);
    setCardWidth(PDFAppCertificateDialogsHelper::IMPORT_CARD_WIDTH);
    PDFAppMainWindow::prepareDialog(this, parent);

    // 436:11538: Cancel, Choose Another File… (Primary with import)
    m_cancelButton = addActionButton(tr("Cancel"), ActionPosition::Trailing, false);
    m_cancelButton->setObjectName(QStringLiteral("certificateImportCancel"));
    m_chooseButton = addActionButton(tr("Choose Another File…"), ActionPosition::Trailing, true);
    m_chooseButton->setObjectName(QStringLiteral("certificateImportChoose"));
    m_chooseButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("import")));

    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppCertificateImportDialog::reject);
    connect(m_chooseButton.data(), &QPushButton::clicked, this, &PDFAppCertificateImportDialog::onChooseClicked);
}

PDFAppCertificateImportDialog::~PDFAppCertificateImportDialog() = default;

QPushButton* PDFAppCertificateImportDialog::getChooseButton() const
{
    return m_chooseButton.data();
}

QPushButton* PDFAppCertificateImportDialog::getCancelButton() const
{
    return m_cancelButton.data();
}

QString PDFAppCertificateImportDialog::getFileFilter()
{
    return tr("Certificate files (*.pfx *.p12)");
}

void PDFAppCertificateImportDialog::start()
{
    QString targetPath;
    QString error;

    // The import writes the certificate cache: its commit notifies the database at once - code of
    // the application runs, the dialog may be gone after it
    QPointer<PDFAppCertificateImportDialog> guard(this);
    const Outcome outcome = importFile(m_context, m_sourceFilePath, PDFCertificateManager::getCertificateDirectory(), &targetPath, &error);
    if (!guard)
    {
        return;
    }
    m_outcome = outcome;

    switch (m_outcome)
    {
        case Outcome::Imported:
        {
            // Emit last: the dialog goes before the signal (a shown one closes, deleted later)
            if (isVisible())
            {
                close();
            }
            else
            {
                deleteLater();
            }
            Q_EMIT certificateImported(targetPath);
            return;
        }

        case Outcome::Collision:
            // 436:11538
            setTitle(tr("A certificate file with this name already exists"));
            setText(tr("%1 is already in your certificate folder. Rename the file you want to import, or choose another one.").arg(QFileInfo(targetPath).fileName()));
            break;

        case Outcome::Refused:
        case Outcome::None:
            setTitle(tr("The certificate file cannot be imported"));
            setText(error);
            break;
    }

    adjustSize();
    if (!isVisible())
    {
        open();
    }
}

PDFAppCertificateImportDialog::Outcome PDFAppCertificateImportDialog::importFile(PDFAppContext* context, const QString& sourceFilePath, const QString& folder, QString* targetPath, QString* errorMessage)
{
    using Helper = PDFAppCertificateDialogsHelper;

    QString error;
    QString target;
    Outcome outcome = Outcome::Refused;
    const QFileInfo sourceInfo(sourceFilePath);

    // Plan 7.2: at most 1 MiB, a PKCS#12; a larger file is never read whole. Plan of step 6, 7: a
    // network place or a device is never contacted (PDFAppToolPathRules)
    const bool isRemote = !sourceFilePath.isEmpty() && PDFAppToolPathRules::isDeviceOrNetworkPath(sourceFilePath);
    const bool isFile = !sourceFilePath.isEmpty() && !isRemote && sourceInfo.isFile();
    const bool isTooLarge = isFile && sourceInfo.size() > MAXIMUM_FILE_SIZE;
    const std::optional<QByteArray> data = (isFile && !isTooLarge) ? Helper::readBounded(sourceFilePath, MAXIMUM_FILE_SIZE) : std::nullopt;
    if (isRemote)
    {
        error = tr("The file %1 is not on this computer. Copy it to this computer first.").arg(sourceInfo.fileName());
    }
    else if (!isFile)
    {
        error = tr("The file %1 does not exist.").arg(sourceInfo.fileName());
    }
    else if (isTooLarge || (data && data->size() > MAXIMUM_FILE_SIZE))
    {
        error = tr("The file %1 is too large for a certificate file.").arg(sourceInfo.fileName());
    }
    else if (!data)
    {
        error = tr("The file %1 cannot be read.").arg(sourceInfo.fileName());
    }
    else if (!PDFCertificateManager::isPkcs12(*data))
    {
        error = tr("The file %1 is not a certificate file with a private key (.pfx).").arg(sourceInfo.fileName());
    }
    else
    {
        // The folder lists only .pfx files: a .p12 file gets the extension .pfx
        const QString fileName = sourceInfo.suffix().compare(QLatin1String("pfx"), Qt::CaseInsensitive) == 0 ? sourceInfo.fileName() : sourceInfo.completeBaseName() + QStringLiteral(".pfx");
        target = QDir(folder).absoluteFilePath(fileName);

        if (QFileInfo::exists(target))
        {
            // The same file (the same bytes) counts as imported; another file is never replaced
            std::optional<QByteArray> existing = Helper::readBounded(target, MAXIMUM_FILE_SIZE);
            outcome = (existing && *existing == *data) ? Outcome::Imported : Outcome::Collision;
        }
        else
        {
            QDir().mkpath(folder);
            QSaveFile file(target);
            if (file.open(QIODevice::WriteOnly) && file.write(*data) == data->size() && file.commit())
            {
                outcome = Outcome::Imported;
            }
            else
            {
                error = tr("The file %1 cannot be written into your certificate folder.").arg(fileName);
            }
        }

        // Q5.11: a file without a password gives its certificate to the cache at once
        if (outcome == Outcome::Imported)
        {
            if (std::optional<PDFCertificateInfo> certificate = PDFCertificateManager::readCertificate(*data, QString()))
            {
                PDFAppToolCertificateProvider(context).storeCertificate(target, certificate->getCertificateData());
            }
        }
    }

    if (targetPath)
    {
        *targetPath = target;
    }
    if (errorMessage)
    {
        *errorMessage = error;
    }
    return outcome;
}

void PDFAppCertificateImportDialog::onChooseClicked()
{
    // A0: the file dialog answers onSourceFileChosen (the request is a child of the dialog: a dialog gone meanwhile gets
    // nothing); one file dialog at a time
    if (m_fileRequest && m_fileRequest->isPending())
    {
        return;
    }
    m_fileRequest = PDFAppPanelFileDialogs::requestOpenFileName(this, tr("Import Certificate"), QFileInfo(m_sourceFilePath).absolutePath(), getFileFilter(), this,
                                                                &PDFAppCertificateImportDialog::onSourceFileChosen);
}

void PDFAppCertificateImportDialog::onSourceFileChosen(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return;
    }

    m_sourceFilePath = filePath;

    // The last call: start may emit and close the dialog
    start();
}

}   // namespace pdf

#include "moc_pdfappcertificatedialogs.cpp"
