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

#include "pdfappdocumentsignatures.h"
#include "pdfapplocale.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsearchindex.h"
#include "pdfappdocumentview.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdfdatabasenotifier.h"
#include "pdftrustedcertificaterepository.h"

#include "pdfcatalog.h"
#include "pdfcertificatestore.h"
#include "pdfexception.h"
#include "pdfform.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <new>

namespace pdf
{

/// Helpers of the signatures
class PDFAppDocumentSignaturesHelper
{
public:
    PDFAppDocumentSignaturesHelper() = delete;

    /// Releases the state of OpenSSL of the worker thread at the end of the
    /// verification, whichever way it ends (PDFSignatureHandler::releaseThreadResources)
    class ThreadResourcesGuard
    {
    public:
        ThreadResourcesGuard() = default;
        ThreadResourcesGuard(const ThreadResourcesGuard&) = delete;
        ThreadResourcesGuard& operator=(const ThreadResourcesGuard&) = delete;
        ~ThreadResourcesGuard() { PDFSignatureHandler::releaseThreadResources(); }
    };

    /// Counts the signature fields of a form (functor of PDFForm::apply)
    class SignatureFieldCounter
    {
    public:
        explicit SignatureFieldCounter(int* count) : m_count(count) { }

        void operator()(const PDFFormField* field) const
        {
            if (field && field->getFieldType() == PDFFormField::FieldType::Signature)
            {
                ++(*m_count);
            }
        }

    private:
        int* m_count;
    };

    /// Date of a card ("Sep 28, 2026")
    static QString formatDate(const QDateTime& dateTime)
    {
        if (!dateTime.isValid())
        {
            return QString();
        }
        return PDFAppLocale::formatDate(dateTime.toLocalTime().date());
    }

    /// Date and time of a card ("Sep 28, 2026, 14:02")
    static QString formatDateTime(const QDateTime& dateTime)
    {
        if (!dateTime.isValid())
        {
            return QString();
        }
        return PDFAppLocale::formatDateTime(dateTime.toLocalTime());
    }

    /// Name of a certificate (common name, organization or "Unknown")
    static QString getCertificateName(const PDFCertificateInfo& info)
    {
        for (PDFCertificateInfo::NameEntry entry : { PDFCertificateInfo::CommonName, PDFCertificateInfo::OrganizationName, PDFCertificateInfo::Email })
        {
            const QString name = info.getName(entry).trimmed();
            if (!name.isEmpty())
            {
                // A name of a certificate is untrusted text: one line of a sane length
                return name.simplified().left(256);
            }
        }
        return PDFAppDocumentSignatureVerifier::tr("Unknown");
    }

    /// SHA-256 fingerprint of a certificate in groups of four hexadecimal digits
    /// ("3A5F 09C1 ...", 16 groups, the line can wrap between them)
    static QString getFingerprint(const QByteArray& certificate)
    {
        const QByteArray hex = QCryptographicHash::hash(certificate, QCryptographicHash::Sha256).toHex().toUpper();
        QStringList groups;
        for (qsizetype i = 0; i < hex.size(); i += 4)
        {
            groups << QString::fromLatin1(hex.mid(i, 4));
        }
        return groups.join(QLatin1Char(' '));
    }

    /// Name of the algorithm of a key ("RSA 2048")
    static QString getKeyText(const PDFCertificateInfo& info)
    {
        QString algorithm;
        switch (info.getPublicKey())
        {
            case PDFCertificateInfo::KeyRSA:
                algorithm = QStringLiteral("RSA");
                break;
            case PDFCertificateInfo::KeyDSA:
                algorithm = QStringLiteral("DSA");
                break;
            case PDFCertificateInfo::KeyEC:
                algorithm = QStringLiteral("EC");
                break;
            case PDFCertificateInfo::KeyDH:
                algorithm = QStringLiteral("DH");
                break;
            default:
                return QString();
        }

        return info.getKeySize() > 0 ? QStringLiteral("%1 %2").arg(algorithm).arg(info.getKeySize()) : algorithm;
    }

    /// Name of a hash algorithm of the design ("sha256" -> "SHA-256")
    static QString getHashAlgorithmText(const QString& algorithm)
    {
        const QString text = algorithm.simplified().left(64).toUpper();
        if (text.startsWith(QStringLiteral("SHA")) && text.size() > 3 && text.at(3).isDigit())
        {
            return QStringLiteral("SHA-%1").arg(text.mid(3));
        }
        return text;
    }

    static bool contains(const std::vector<QByteArray>& certificates, const QByteArray& data)
    {
        return std::find(certificates.cbegin(), certificates.cend(), data) != certificates.cend();
    }
};

PDFAppDocumentSignatureVerifier::PDFAppDocumentSignatureVerifier(PDFAppDocument* document, QObject* parent) :
    BaseClass(parent),
    m_document(document)
{
    if (document)
    {
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppDocumentSignatureVerifier::onDocumentStateChanged);
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppDocumentSignatureVerifier::onDocumentChanged);

        PDFAppContext* context = document->getContext();
        if (context && context->getNotifier())
        {
            connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppDocumentSignatureVerifier::onDatabaseChanged);
            connect(context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppDocumentSignatureVerifier::onDatabaseEverythingChanged);
        }

        if (document->isReady())
        {
            startVerification();
        }
    }
}

PDFAppDocumentSignatureVerifier::~PDFAppDocumentSignatureVerifier()
{
    stopVerification();
}

bool PDFAppDocumentSignatureVerifier::hasSignatures() const
{
    return m_verification && m_verification->signatureFieldCount > 0;
}

void PDFAppDocumentSignatureVerifier::verify()
{
    startVerification();
}

QString PDFAppDocumentSignatureVerifier::trustCertificate(const QByteArray& certificateData)
{
    PDFAppContext* context = m_document ? m_document->getContext() : nullptr;
    if (!context || !context->getDatabase())
    {
        return tr("The certificate cannot be stored.");
    }

    PDFTrustedCertificateRepository repository(context->getDatabase());
    const PDFDatabaseError error = repository.addCertificate(certificateData);
    if (error.isError())
    {
        return error.getMessage();
    }

    // The notification of the table starts the verification too; it is started
    // at once, so the result does not depend on the delivery of the notification
    startVerification();
    return QString();
}

PDFAppSignatureSettings PDFAppDocumentSignatureVerifier::readSettings(PDFAppContext* context)
{
    PDFAppSignatureSettings settings;
    if (!context || !context->getDatabase())
    {
        return settings;
    }

    const PDFSettingsRepository repository = context->getSettings();
    settings.verify = repository.getBool(PDFSettingKeys::signaturesVerify());
    settings.strict = repository.getBool(PDFSettingKeys::signaturesStrict());
    settings.ignoreExpiration = repository.getBool(PDFSettingKeys::signaturesIgnoreExpiration());
    settings.useSystemStore = repository.getBool(PDFSettingKeys::signaturesUseSystemStore());

    for (const PDFTrustedCertificate& certificate : PDFTrustedCertificateRepository(context->getDatabase()).getCertificates())
    {
        settings.trustedCertificates.push_back(certificate.data);
    }
    return settings;
}

PDFAppSignatureVerificationPointer PDFAppDocumentSignatureVerifier::runVerification(PDFDocumentPointer document,
                                                                                    QByteArray sourceData,
                                                                                    PDFAppSignatureSettings settings,
                                                                                    std::shared_ptr<PDFAppCancelFlag> cancelFlag)
{
    // The pool thread may outlive the application (valgrind: the error queue of
    // d2i_PKCS7 left in a live thread at the exit)
    PDFAppDocumentSignaturesHelper::ThreadResourcesGuard threadResourcesGuard;

    std::shared_ptr<PDFAppSignatureVerification> verification = std::make_shared<PDFAppSignatureVerification>();
    verification->isVerified = settings.verify;
    verification->isStrict = settings.strict;
    if (!document || PDFOperationControl::isOperationCancelled(cancelFlag.get()))
    {
        return verification;
    }

    // An exception must not leave the worker (the watcher would throw it again from
    // the slot of the GUI thread): a damaged form has no verified signatures
    try
    {
        const PDFForm form = PDFForm::parse(document.data(), document->getCatalog()->getFormObject());
        int signatureFieldCount = 0;
        form.apply(PDFAppDocumentSignaturesHelper::SignatureFieldCounter(&signatureFieldCount));
        verification->signatureFieldCount = signatureFieldCount;
        if (!settings.verify || signatureFieldCount == 0 || PDFOperationControl::isOperationCancelled(cancelFlag.get()))
        {
            return verification;
        }

        // Trusted: the certificates of the user, the AATL list always, the system
        // store by the setting (O15); never the file TrustedCertStorage.bin
        PDFCertificateStore store;
        for (const QByteArray& certificate : settings.trustedCertificates)
        {
            store.add(PDFCertificateEntry::EntryType::User, certificate);
        }

        PDFSignatureHandler::Parameters parameters;
        parameters.store = &store;
        parameters.dss = &document->getCatalog()->getDocumentSecurityStore();
        parameters.enableVerification = true;
        parameters.ignoreExpirationDate = settings.ignoreExpiration;
        parameters.useSystemCertificateStore = settings.useSystemStore;
        parameters.aatlCertificates = PDFSignatureHandler::AATLCertificates::Always;
        verification->results = PDFSignatureHandler::verifySignatures(form, sourceData, parameters);
    }
    catch (const PDFException&)
    {
        verification->results.clear();
    }
    catch (const std::bad_alloc&)
    {
        verification->results.clear();
    }
    return verification;
}

void PDFAppDocumentSignatureVerifier::startVerification()
{
    stopVerification();
    if (!m_document || !m_document->isReady())
    {
        return;
    }

    const PDFAppDocumentSnapshot snapshot = m_document->getSnapshot();
    if (!snapshot.isValid())
    {
        return;
    }

    m_verifiedData = snapshot.originalData;
    m_cancelFlag = std::make_shared<PDFAppCancelFlag>();
    m_watcher = new Watcher(this);
    connect(m_watcher, &QFutureWatcherBase::finished, this, &PDFAppDocumentSignatureVerifier::onVerificationFinished);
    m_watcher->setFuture(QtConcurrent::run(&PDFAppDocumentSignatureVerifier::runVerification, snapshot.document, snapshot.originalData,
                                           readSettings(m_document->getContext()), m_cancelFlag));
    Q_EMIT verificationChanged();
}

void PDFAppDocumentSignatureVerifier::stopVerification()
{
    if (m_cancelFlag)
    {
        m_cancelFlag->cancel();
        m_cancelFlag.reset();
    }

    // The worker ends by itself, its result is dropped
    if (m_watcher)
    {
        m_watcher->disconnect(this);
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }
}

void PDFAppDocumentSignatureVerifier::onVerificationFinished()
{
    PDFAppSignatureVerificationPointer verification;
    if (m_watcher)
    {
        verification = m_watcher->result();
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }
    m_cancelFlag.reset();

    m_verification = verification ? verification : std::make_shared<PDFAppSignatureVerification>();
    applyResults();

    Q_EMIT verificationChanged();
}

void PDFAppDocumentSignatureVerifier::applyResults()
{
    if (!m_document || !m_document->isReady() || !m_verification)
    {
        return;
    }

    // The results belong to the bytes of the file, they are valid for every
    // version made from them; the document emits signatureResultsChanged, also for
    // an empty list (the sidebar shows or hides the tab of the panel)
    m_isApplyingResults = true;
    m_document->setSignatureResults(m_verification->results, m_document->getVersion());
    m_isApplyingResults = false;
}

void PDFAppDocumentSignatureVerifier::onDocumentStateChanged()
{
    if (!m_document)
    {
        return;
    }

    if (m_document->isReady())
    {
        if (!m_watcher && (!m_verification || !m_verifiedData.isSharedWith(m_document->getOriginalData())))
        {
            startVerification();
        }
    }
    else
    {
        stopVerification();
        m_verification.reset();
        m_verifiedData.clear();
        Q_EMIT verificationChanged();
    }
}

void PDFAppDocumentSignatureVerifier::onDocumentChanged(const PDFModifiedDocument& modified)
{
    Q_UNUSED(modified);
    if (!m_document || !m_document->isReady() || m_isApplyingResults)
    {
        return;
    }

    // Other bytes of the file (a save, a reload): verified again; the same bytes:
    // the results are given to the new version
    const QByteArray& originalData = m_document->getOriginalData();
    if (!m_verifiedData.isSharedWith(originalData) && m_verifiedData != originalData)
    {
        startVerification();
    }
    else if (!m_watcher)
    {
        applyResults();
    }
}

void PDFAppDocumentSignatureVerifier::onDatabaseChanged(const QString& topic, const QString& key)
{
    const bool isSignatureSetting = topic == PDFSettingsRepository::getTopic() && key.startsWith(QStringLiteral("signatures."));
    if (isSignatureSetting || topic == PDFTrustedCertificateRepository::getTopic())
    {
        startVerification();
    }
}

void PDFAppDocumentSignatureVerifier::onDatabaseEverythingChanged()
{
    startVerification();
}

PDFComponentPainter::SignatureStatus PDFAppDocumentSignatureVerifier::getStatus(const PDFSignatureVerificationResult& result, bool isStrict)
{
    using Status = PDFComponentPainter::SignatureStatus;
    using Flag = PDFSignatureVerificationResult::VerificationFlag;

    // A broken signature, a changed document or a revoked certificate is invalid
    if (result.hasSignatureError() || result.hasFlag(Flag::Error_Certificate_Revoked) || result.hasFlag(Flag::Error_NoHandler) || result.hasFlag(Flag::Error_Generic))
    {
        return Status::Invalid;
    }

    // An intact signature with a certificate, which is not trusted or has expired,
    // and warnings need attention (strict mode: they are errors)
    if (result.hasCertificateError() || result.hasWarning() || !result.isValid())
    {
        return isStrict ? Status::Invalid : Status::Warning;
    }

    return Status::Valid;
}

PDFAppSignatureSummary PDFAppDocumentSignatureVerifier::getSummary(const std::vector<PDFSignatureVerificationResult>& results, bool isStrict)
{
    PDFAppSignatureSummary summary;
    for (const PDFSignatureVerificationResult& result : results)
    {
        if (result.getType() == PDFSignature::Type::DocTimeStamp)
        {
            ++summary.timestampCount;
        }
        else
        {
            ++summary.signatureCount;
        }

        switch (getStatus(result, isStrict))
        {
            case PDFComponentPainter::SignatureStatus::Valid:
                ++summary.validCount;
                break;
            case PDFComponentPainter::SignatureStatus::Warning:
                ++summary.warningCount;
                break;
            default:
                ++summary.invalidCount;
                break;
        }
    }
    return summary;
}

QString PDFAppDocumentSignatureVerifier::getStatusText(const PDFAppSignatureSummary& summary, bool isVerified)
{
    QString items;
    if (summary.signatureCount > 0)
    {
        items = (summary.signatureCount == 1 ? tr("1 signature") : tr("%Ln signatures", nullptr, summary.signatureCount));
    }
    else
    {
        items = (summary.timestampCount == 1 ? tr("1 timestamp") : tr("%Ln timestamps", nullptr, summary.timestampCount));
    }

    QString state;
    if (!isVerified)
    {
        state = tr("not verified");
    }
    else if (summary.invalidCount > 0)
    {
        state = tr("%Ln invalid", nullptr, summary.invalidCount);
    }
    else if (summary.warningCount > 0)
    {
        state = (summary.warningCount == 1 ? tr("1 needs attention") : tr("%Ln need attention", nullptr, summary.warningCount));
    }
    else
    {
        state = tr("valid");
    }

    return tr("%1 · %2").arg(items, state);
}

int PDFAppDocumentSignatureVerifier::getTrustableCertificateIndex(const PDFSignatureVerificationResult& result, const std::vector<QByteArray>& trustedCertificates)
{
    using Flag = PDFSignatureVerificationResult::VerificationFlag;

    const PDFCertificateInfos& certificates = result.getCertificateInfos();
    const bool isUntrusted = result.hasFlag(Flag::Error_Certificate_TrustedNotFound) ||
                             result.hasFlag(Flag::Error_Certificate_SelfSigned) ||
                             result.hasFlag(Flag::Error_Certificate_SelfSignedChain);
    if (!isUntrusted || certificates.empty())
    {
        return -1;
    }

    // The root of the chain (the last certificate) is offered, unless it is trusted already
    const int index = int(certificates.size()) - 1;
    const QByteArray data = certificates.back().getCertificateData();
    if (!PDFTrustedCertificateRepository::isValidCertificate(data) || PDFAppDocumentSignaturesHelper::contains(trustedCertificates, data))
    {
        return -1;
    }
    return index;
}

PDFComponentPainter::SignatureCard PDFAppDocumentSignatureVerifier::getCard(const PDFSignatureVerificationResult& result, bool isStrict, const std::vector<QByteArray>& trustedCertificates)
{
    using Status = PDFComponentPainter::SignatureStatus;
    using Flag = PDFSignatureVerificationResult::VerificationFlag;
    using Helper = PDFAppDocumentSignaturesHelper;

    PDFComponentPainter::SignatureCard card;
    card.status = getStatus(result, isStrict);

    const PDFCertificateInfos& certificates = result.getCertificateInfos();
    const bool isTimestamp = result.getType() == PDFSignature::Type::DocTimeStamp;
    const QString issuerName = certificates.empty() ? tr("Unknown") : Helper::getCertificateName(certificates.front());
    const QDateTime signatureDate = result.getSignatureDate().isValid() ? result.getSignatureDate() : result.getTimestampDate();

    if (isTimestamp)
    {
        card.signer = tr("Document timestamp");
        const QString date = Helper::formatDateTime(result.getTimestampDate().isValid() ? result.getTimestampDate() : signatureDate);
        card.meta = date.isEmpty() ? issuerName : tr("%1 · %2").arg(issuerName, date);
    }
    else
    {
        card.signer = issuerName;
        const QString date = Helper::formatDate(signatureDate);
        card.meta = date.isEmpty() ? tr("Digital signature") : tr("Digital signature · %1").arg(date);
    }

    // The summary says, what the user may rely on
    switch (card.status)
    {
        case Status::Valid:
            card.summary = isTimestamp ? tr("The document existed in this form at the stated time.") : tr("The document has not been changed since it was signed.");
            break;

        case Status::Warning:
            if (result.hasFlag(Flag::Error_Certificate_Expired))
            {
                card.summary = tr("The signature is intact, but the certificate of the signer has expired.");
            }
            else if (result.hasCertificateError())
            {
                card.summary = tr("The signature is intact, but the certificate of the signer is not trusted.");
            }
            else if (result.hasFlag(Flag::Warning_Signature_NotCoveredBytes))
            {
                card.summary = tr("The signature is intact, but a part of the document is not covered by it.");
            }
            else
            {
                card.summary = tr("The signature is intact, but it could not be verified completely.");
            }
            break;

        default:
            if (result.hasFlag(Flag::Error_Certificate_Revoked))
            {
                card.summary = tr("The certificate of the signer has been revoked.");
            }
            else if (result.hasSignatureError())
            {
                card.summary = tr("The document has been changed since it was signed, or the signature is damaged.");
            }
            else if (!result.getErrors().isEmpty())
            {
                card.summary = result.getErrors().front().simplified().left(512);
            }
            else if (!result.getWarnings().isEmpty())
            {
                card.summary = result.getWarnings().front().simplified().left(512);
            }
            else
            {
                card.summary = tr("The signature is not valid.");
            }
            break;
    }

    // Details
    const QString signedOn = Helper::formatDateTime(signatureDate);
    if (!signedOn.isEmpty())
    {
        card.details.append({ isTimestamp ? tr("Time") : tr("Signed on"), signedOn });
    }
    if (!result.getHashAlgorithms().isEmpty())
    {
        QStringList algorithms;
        for (const QString& algorithm : result.getHashAlgorithms())
        {
            algorithms << Helper::getHashAlgorithmText(algorithm);
        }
        card.details.append({ tr("Hash algorithm"), algorithms.join(QStringLiteral(", ")).left(128) });
    }
    if (!isTimestamp)
    {
        const bool isCovered = !result.hasFlag(Flag::Warning_Signature_NotCoveredBytes) && !result.hasSignatureError();
        card.details.append({ tr("Covers"), isCovered ? tr("Entire document") : tr("A part of the document") });

        QString timestamp;
        if (result.hasFlag(Flag::Warning_Signature_TimestampNotVerified))
        {
            timestamp = tr("Embedded, not verified");
        }
        else if (result.getTimestampDate().isValid())
        {
            timestamp = tr("Embedded, valid");
        }
        else
        {
            timestamp = tr("None");
        }
        card.details.append({ tr("Timestamp"), timestamp });
    }

    // The chain from the signer to the root
    const int trustableIndex = getTrustableCertificateIndex(result, trustedCertificates);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const int count = int(certificates.size());
    for (int i = 0; i < count; ++i)
    {
        const PDFCertificateInfo& info = certificates[size_t(i)];

        PDFComponentPainter::SignatureCertificate certificate;
        certificate.name = Helper::getCertificateName(info);
        certificate.status = Status::Valid;

        QStringList parts;
        parts << ((i == 0) ? tr("Signer") : (i == count - 1 ? tr("Root") : tr("Intermediate")));
        const QString key = Helper::getKeyText(info);
        if (!key.isEmpty())
        {
            parts << key;
        }

        const QDateTime notValidAfter = info.getNotValidAfter();
        const bool isExpired = notValidAfter.isValid() && notValidAfter < now;
        if (notValidAfter.isValid())
        {
            parts << (isExpired ? tr("expired on %1").arg(Helper::formatDate(notValidAfter)) : tr("valid until %1").arg(Helper::formatDate(notValidAfter)));
        }
        certificate.info = parts.join(QStringLiteral(" · "));

        if (i == 0 && result.hasFlag(Flag::Error_Certificate_Revoked))
        {
            certificate.status = Status::Invalid;
            certificate.info = tr("Revoked");
        }
        else if (isExpired && !result.hasFlag(Flag::Error_Certificate_Expired))
        {
            // The expiration is ignored by the setting: the information says it
            certificate.status = Status::Valid;
        }
        else if (isExpired)
        {
            certificate.status = isStrict ? Status::Invalid : Status::Warning;
        }

        if (i == trustableIndex)
        {
            certificate.status = isStrict ? Status::Invalid : Status::Warning;
            certificate.info = tr("Not among trusted certificates");
            certificate.linkText = tr("Add to trusted certificates");
        }
        card.certificates.append(certificate);
    }

    return card;
}

PDFAppSignatureStatusItem::PDFAppSignatureStatusItem(PDFAppDocumentSignatureVerifier* verifier, PDFAppDocumentView* view, QWidget* parent) :
    BaseClass(parent),
    m_verifier(verifier),
    m_view(view)
{
    setObjectName(QStringLiteral("signatureStatusItem"));
    setAutoRaise(true);
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setIconSize(QSize(14, 14));
    setFocusPolicy(Qt::NoFocus);
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::STATUS_ITEM);
    connect(this, &QToolButton::clicked, this, &PDFAppSignatureStatusItem::onClicked);
    if (verifier)
    {
        connect(verifier, &PDFAppDocumentSignatureVerifier::verificationChanged, this, &PDFAppSignatureStatusItem::updateItem);
    }
    updateItem();
}

PDFAppSignatureStatusItem::~PDFAppSignatureStatusItem() = default;

void PDFAppSignatureStatusItem::updateItem()
{
    const PDFAppSignatureVerificationPointer verification = m_verifier ? m_verifier->getVerification() : nullptr;
    if (!verification || verification->signatureFieldCount == 0)
    {
        setVisible(false);
        return;
    }

    QString text;
    QString iconName = QStringLiteral("shield-check");
    bool isWarning = false;
    if (m_verifier->isVerifying())
    {
        text = tr("Verifying signatures…");
        iconName = QStringLiteral("shield");
    }
    else
    {
        const PDFAppSignatureSummary summary = PDFAppDocumentSignatureVerifier::getSummary(verification->results, verification->isStrict);
        PDFAppSignatureSummary shown = summary;
        if (summary.signatureCount + summary.timestampCount == 0)
        {
            // Not verified: the fields of the form are counted
            shown.signatureCount = verification->signatureFieldCount;
        }
        text = PDFAppDocumentSignatureVerifier::getStatusText(shown, verification->isVerified);
        isWarning = !verification->isVerified || summary.invalidCount > 0 || summary.warningCount > 0;
        if (isWarning)
        {
            iconName = QStringLiteral("alert-triangle");
        }
    }

    setText(text);
    setToolTip(tr("Show the signatures of the document"));
    setIcon(PDFThemeIcons::getIcon(iconName));
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::TONE, isWarning ? QStringLiteral("warning") : QString());
    setVisible(true);
}

void PDFAppSignatureStatusItem::onClicked()
{
    if (m_view)
    {
        m_view->showPanel(PDFAppDocumentPanelIds::signatures());
    }
}

PDFAppDocumentSignaturesPanel::PDFAppDocumentSignaturesPanel(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("signaturesPanel"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("signaturesScrollArea"));
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    PDFWidgetStyleProperties::set(m_scrollArea, PDFWidgetStyleProperties::OVERLAY_SCROLL_BARS);
    layout->addWidget(m_scrollArea, 1);

    // sidebar-content 195:3359: paddings 12 / 2 / 12 / 16, gap 10
    m_content = new QWidget(m_scrollArea);
    m_content->setObjectName(QStringLiteral("signaturesContent"));
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(12, 2, 12, 16);
    m_contentLayout->setSpacing(10);
    m_scrollArea->setWidget(m_content);

    // summary-banner 195:3360: paddings 12 / 10, gap 10, icon 18
    m_banner = new PDFTintedCard(PDFAccentHue::Amber, m_content);
    m_banner->setObjectName(QStringLiteral("signaturesBanner"));
    QHBoxLayout* bannerLayout = new QHBoxLayout(m_banner);
    bannerLayout->setContentsMargins(12, 10, 12, 10);
    bannerLayout->setSpacing(10);
    m_bannerIcon = new PDFIconBadge(QStringLiteral("alert-triangle"), 18, 18, m_banner);
    m_bannerIcon->setObjectName(QStringLiteral("signaturesBannerIcon"));
    m_bannerIcon->setColors(std::nullopt, PDFThemeColors::AccentAmberIcon);
    bannerLayout->addWidget(m_bannerIcon, 0, Qt::AlignVCenter);
    QVBoxLayout* bannerText = new QVBoxLayout();
    bannerText->setContentsMargins(0, 0, 0, 0);
    bannerText->setSpacing(1);
    m_bannerTitle = new PDFLabel(QString(), PDFThemeFonts::Role::SmallSemiBold, PDFThemeColors::TextPrimary, m_banner);
    m_bannerTitle->setWordWrap(true);
    m_bannerText = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextTertiary, m_banner);
    m_bannerText->setWordWrap(true);
    bannerText->addWidget(m_bannerTitle);
    bannerText->addWidget(m_bannerText);
    bannerLayout->addLayout(bannerText, 1);
    m_contentLayout->addWidget(m_banner);
    m_contentLayout->addStretch(1);
    m_banner->setVisible(false);
}

PDFAppDocumentSignaturesPanel::~PDFAppDocumentSignaturesPanel()
{
    // The confirmation is a window of its own: it does not outlive the panel
    if (m_trustDialog)
    {
        m_trustDialog->disconnect(this);
        m_trustDialog->deleteLater();
    }
}

QString PDFAppDocumentSignaturesPanel::getId() const
{
    return PDFAppDocumentPanelIds::signatures();
}

QString PDFAppDocumentSignaturesPanel::getTitle() const
{
    return tr("Signatures");
}

QString PDFAppDocumentSignaturesPanel::getSubtitle() const
{
    const PDFAppSignatureVerificationPointer verification = m_verifier ? m_verifier->getVerification() : nullptr;
    if (m_verifier && m_verifier->isVerifying())
    {
        return tr("Verifying…");
    }
    if (!verification || verification->signatureFieldCount == 0)
    {
        return tr("No signatures");
    }

    const PDFAppSignatureSummary summary = PDFAppDocumentSignatureVerifier::getSummary(verification->results, verification->isStrict);
    if (summary.signatureCount + summary.timestampCount == 0)
    {
        return (verification->signatureFieldCount == 1 ? tr("1 signature") : tr("%Ln signatures", nullptr, verification->signatureFieldCount));
    }
    if (summary.timestampCount == 0)
    {
        return (summary.signatureCount == 1 ? tr("1 signature") : tr("%Ln signatures", nullptr, summary.signatureCount));
    }
    if (summary.signatureCount == 0)
    {
        return (summary.timestampCount == 1 ? tr("1 timestamp") : tr("%Ln timestamps", nullptr, summary.timestampCount));
    }
    return tr("%1, %2").arg((summary.signatureCount == 1 ? tr("1 signature") : tr("%Ln signatures", nullptr, summary.signatureCount)), (summary.timestampCount == 1 ? tr("1 timestamp") : tr("%Ln timestamps", nullptr, summary.timestampCount)));
}

QString PDFAppDocumentSignaturesPanel::getIconName() const
{
    return QStringLiteral("pen-tool");
}

void PDFAppDocumentSignaturesPanel::setDocument(PDFAppDocument* document, PDFAppDocumentView* view)
{
    BaseClass::setDocument(document, view);

    if (m_verifier)
    {
        m_verifier->disconnect(this);
    }

    PDFAppDocumentSearchCommandHandler* handler = PDFAppDocumentSearchCommandHandler::findHandler(view ? static_cast<QObject*>(view) : static_cast<QObject*>(document));
    m_verifier = handler ? handler->getSignatureVerifier() : nullptr;
    if (m_verifier)
    {
        connect(m_verifier, &PDFAppDocumentSignatureVerifier::verificationChanged, this, &PDFAppDocumentSignaturesPanel::onVerificationChanged);
    }

    // The cards of another document start collapsed
    for (const QPointer<PDFSignatureCard>& card : m_cards)
    {
        delete card.data();
    }
    m_cards.clear();
    m_expanded.clear();
    rebuild();
}

std::vector<PDFSignatureCard*> PDFAppDocumentSignaturesPanel::getCards() const
{
    std::vector<PDFSignatureCard*> cards;
    for (const QPointer<PDFSignatureCard>& card : m_cards)
    {
        if (card)
        {
            cards.push_back(card.data());
        }
    }
    return cards;
}

PDFMessageBox* PDFAppDocumentSignaturesPanel::getTrustDialog() const
{
    return m_trustDialog.data();
}

PDFTintedCard* PDFAppDocumentSignaturesPanel::getBanner() const
{
    return (m_banner && !m_banner->isHidden()) ? m_banner.data() : nullptr;
}

void PDFAppDocumentSignaturesPanel::onVerificationChanged()
{
    rebuild();

    QPointer<PDFAppDocumentSignaturesPanel> guard(this);
    Q_EMIT titleChanged();
    if (guard)
    {
        Q_EMIT availabilityChanged();
    }
}

void PDFAppDocumentSignaturesPanel::rebuild()
{
    // The expanded cards stay expanded, when the same signatures are shown again
    // (also after a verification, during which no cards are shown - a changed
    // setting, a trusted certificate)
    if (!m_cards.empty())
    {
        m_expanded.clear();
        for (const QPointer<PDFSignatureCard>& card : m_cards)
        {
            m_expanded.push_back(card && card->isExpanded());
            delete card.data();
        }
    }
    m_cards.clear();
    const std::vector<bool> expanded = m_expanded;
    m_trustableCertificates.clear();

    const PDFAppSignatureVerificationPointer verification = m_verifier ? m_verifier->getVerification() : nullptr;
    const bool isVerifying = m_verifier && m_verifier->isVerifying();
    std::vector<QByteArray> trustedCertificates;
    if (PDFAppDocument* document = getDocument())
    {
        trustedCertificates = PDFAppDocumentSignatureVerifier::readSettings(document->getContext()).trustedCertificates;
    }

    // The banner of the summary
    QString bannerTitle;
    QString bannerText;
    PDFAccentHue hue = PDFAccentHue::Amber;
    QString bannerIcon = QStringLiteral("alert-triangle");
    if (isVerifying)
    {
        hue = PDFAccentHue::Neutral;
        bannerIcon = QStringLiteral("hourglass");
        bannerTitle = tr("Verifying the signatures");
        bannerText = tr("The signatures are verified offline on this computer.");
    }
    else if (verification && verification->signatureFieldCount > 0 && !verification->isVerified)
    {
        hue = PDFAccentHue::Neutral;
        bannerIcon = QStringLiteral("info");
        bannerTitle = tr("Signatures are not verified");
        bannerText = tr("Verification of the signatures is turned off in Settings › Security.");
    }
    else if (verification && !verification->results.empty())
    {
        const PDFAppSignatureSummary summary = PDFAppDocumentSignatureVerifier::getSummary(verification->results, verification->isStrict);
        const int count = summary.signatureCount + summary.timestampCount;
        if (summary.invalidCount > 0)
        {
            hue = PDFAccentHue::Red;
            bannerIcon = QStringLiteral("x-circle");
            bannerTitle = (summary.invalidCount == 1 ? tr("One signature is not valid") : tr("%Ln signatures are not valid", nullptr, summary.invalidCount));
        }
        else if (summary.warningCount > 0)
        {
            bannerTitle = (summary.warningCount == 1 ? tr("One signature needs attention") : tr("%Ln signatures need attention", nullptr, summary.warningCount));
        }
        else
        {
            hue = PDFAccentHue::Green;
            bannerIcon = QStringLiteral("shield-check");
            bannerTitle = count == 1 ? tr("The signature is valid") : tr("All signatures are valid");
        }

        if (summary.validCount > 0 && summary.validCount < count)
        {
            bannerText = (summary.validCount == 1 ? tr("The other signature is valid.") : tr("The other %Ln signatures are valid.", nullptr, summary.validCount));
        }
    }
    else if (verification && verification->signatureFieldCount > 0)
    {
        hue = PDFAccentHue::Neutral;
        bannerIcon = QStringLiteral("info");
        bannerTitle = tr("The signature fields are not signed");
    }

    if (m_banner)
    {
        m_banner->setHue(hue);
        if (m_bannerIcon)
        {
            m_bannerIcon->setIconName(bannerIcon);
            m_bannerIcon->setColors(std::nullopt, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon));
        }
        m_bannerTitle->setText(bannerTitle);
        m_bannerText->setText(bannerText);
        m_bannerText->setVisible(!bannerText.isEmpty());
        m_banner->setVisible(!bannerTitle.isEmpty());
    }

    // The cards (Signature card 192:1032), the first one needing attention expanded
    if (verification && !isVerifying)
    {
        int insertIndex = 1;
        bool isFirstAttention = true;
        for (size_t i = 0; i < verification->results.size(); ++i)
        {
            const PDFSignatureVerificationResult& result = verification->results[i];
            const PDFComponentPainter::SignatureCard content = PDFAppDocumentSignatureVerifier::getCard(result, verification->isStrict, trustedCertificates);

            PDFSignatureCard* card = new PDFSignatureCard(m_content);
            card->setObjectName(QStringLiteral("signatureCard%1").arg(i));
            card->setSigner(content.signer);
            card->setMeta(content.meta);
            card->setSummary(content.summary);
            card->setStatus(content.status);
            card->setDetails(content.details);
            card->setCertificates(content.certificates);

            bool isExpanded = (i < expanded.size()) ? expanded[i] : false;
            if (i >= expanded.size() && content.status != PDFComponentPainter::SignatureStatus::Valid && isFirstAttention)
            {
                isExpanded = true;
            }
            if (content.status != PDFComponentPainter::SignatureStatus::Valid)
            {
                isFirstAttention = false;
            }
            card->setExpanded(isExpanded);
            connect(card, &PDFSignatureCard::certificateLinkActivated, this, &PDFAppDocumentSignaturesPanel::onCertificateLinkActivated);

            m_contentLayout->insertWidget(insertIndex++, card);
            m_cards.emplace_back(card);

            const int trustableIndex = PDFAppDocumentSignatureVerifier::getTrustableCertificateIndex(result, trustedCertificates);
            m_trustableCertificates.push_back(trustableIndex >= 0 ? result.getCertificateInfos()[size_t(trustableIndex)].getCertificateData() : QByteArray());
        }
    }
}

void PDFAppDocumentSignaturesPanel::onCertificateLinkActivated(int index)
{
    Q_UNUSED(index);

    PDFSignatureCard* card = qobject_cast<PDFSignatureCard*>(sender());
    if (!card || m_trustDialog)
    {
        return;
    }

    QByteArray certificate;
    QString name;
    QString issuer;
    for (size_t i = 0; i < m_cards.size(); ++i)
    {
        if (m_cards[i] == card && i < m_trustableCertificates.size())
        {
            certificate = m_trustableCertificates[i];
            const std::optional<PDFCertificateInfo> info = PDFCertificateInfo::getCertificateInfo(certificate);
            name = info ? PDFAppDocumentSignaturesHelper::getCertificateName(*info) : QString();

            // The issuer is untrusted text too: one line of a sane length
            issuer = PDFCertificateInfo::getIssuerName(certificate).simplified().left(256);
        }
    }
    if (certificate.isEmpty())
    {
        return;
    }

    // The trust is a decision of the user: a confirmation names the certificate
    m_pendingCertificate = certificate;
    m_trustDialog = new PDFMessageBox(PDFThemeIcons::Badge::Warning,
                                      tr("Trust the certificate “%1”?").arg(name),
                                      tr("Signatures made by certificates issued by it will be shown as valid in all documents. Trust only a certificate, whose origin you know."),
                                      QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                      window());
    m_trustDialog->setObjectName(QStringLiteral("trustCertificateDialog"));
    m_trustDialog->setAttribute(Qt::WA_DeleteOnClose);

    // O18: the subject, the issuer and the SHA-256 fingerprint, which the user can
    // compare with the one published by the owner of the certificate
    QFrame* facts = new QFrame(m_trustDialog);
    facts->setObjectName(QStringLiteral("certificateFacts"));
    PDFWidgetStyleProperties::set(facts, PDFWidgetStyleProperties::NOTE);
    QVBoxLayout* factsLayout = new QVBoxLayout(facts);
    factsLayout->setContentsMargins(12, 4, 12, 4);
    factsLayout->setSpacing(0);
    PDFKeyValueList* list = new PDFKeyValueList(facts);
    list->setObjectName(QStringLiteral("certificateFactsList"));
    list->addRow(tr("Issued to"), name);
    list->addRow(tr("Issued by"), issuer.isEmpty() ? tr("Unknown") : issuer);
    factsLayout->addWidget(list);
    PDFLabel* fingerprintLabel = new PDFLabel(tr("SHA-256 fingerprint"), PDFThemeFonts::Role::Small, PDFThemeColors::TextTertiary, facts);
    fingerprintLabel->setObjectName(QStringLiteral("fingerprintTitle"));
    PDFLabel* fingerprint = new PDFLabel(PDFAppDocumentSignaturesHelper::getFingerprint(certificate), PDFThemeFonts::Role::SmallMedium, PDFThemeColors::TextPrimary, facts);
    fingerprint->setObjectName(QStringLiteral("fingerprint"));
    fingerprint->setTextFormat(Qt::PlainText);
    fingerprint->setWordWrap(true);
    fingerprint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    factsLayout->addWidget(fingerprintLabel);
    factsLayout->addWidget(fingerprint);
    factsLayout->addSpacing(8);
    m_trustDialog->addContent(facts);
    if (QAbstractButton* button = m_trustDialog->getButtonBox()->button(QDialogButtonBox::Ok))
    {
        button->setText(tr("Trust"));
    }
    m_trustDialog->setDefaultButton(QDialogButtonBox::Cancel);
    connect(m_trustDialog, &QDialog::finished, this, &PDFAppDocumentSignaturesPanel::onTrustDialogFinished);
    m_trustDialog->open();
}

void PDFAppDocumentSignaturesPanel::onTrustDialogFinished(int result)
{
    Q_UNUSED(result);

    const bool isAccepted = m_trustDialog && m_trustDialog->getClickedStandardButton() == QDialogButtonBox::Ok;
    m_trustDialog = nullptr;
    const QByteArray certificate = m_pendingCertificate;
    m_pendingCertificate.clear();

    if (!isAccepted || !m_verifier || certificate.isEmpty())
    {
        return;
    }

    QPointer<PDFAppDocumentSignaturesPanel> guard(this);
    const QString error = m_verifier->trustCertificate(certificate);
    if (guard && !error.isEmpty())
    {
        // Not exec(): the panel or its tab may be closed meanwhile
        PDFMessageBox* message = new PDFMessageBox(PDFThemeIcons::Badge::Critical, tr("The certificate was not trusted"), error, QDialogButtonBox::Ok, window());
        message->setAttribute(Qt::WA_DeleteOnClose);
        message->open();
    }
}

}   // namespace pdf
