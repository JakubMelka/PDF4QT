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

#ifndef PDFAPPDOCUMENTSIGNATURES_H
#define PDFAPPDOCUMENTSIGNATURES_H

#include "pdfappglobal.h"
#include "pdfappdocumentpanel.h"

#include "pdfdocument.h"
#include "pdfsignaturehandler.h"

#include "pdfcomponentpainter.h"

#include <QByteArray>
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QToolButton>

#include <memory>
#include <vector>

class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace pdf
{

class PDFAppCancelFlag;
class PDFAppContext;
class PDFAppDocument;
class PDFAppDocumentView;
class PDFIconBadge;
class PDFLabel;
class PDFMessageBox;
class PDFModifiedDocument;
class PDFSignatureCard;
class PDFTintedCard;

/// Settings of one verification (read in the GUI thread, used by the worker)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSignatureSettings
{
public:
    bool verify = true;                 ///< signatures.verify
    bool strict = false;                ///< signatures.strict (warnings are errors)
    bool ignoreExpiration = false;      ///< signatures.ignoreExpiration
    bool useSystemStore = true;         ///< signatures.useSystemStore
    std::vector<QByteArray> trustedCertificates;    ///< trusted_certificate (DER)
};

/// Result of one verification of the signatures of a file
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSignatureVerification
{
public:
    std::vector<PDFSignatureVerificationResult> results;
    int signatureFieldCount = 0;        ///< Signature fields of the form (also without verification)
    bool isVerified = false;            ///< The verification was turned on
    bool isStrict = false;              ///< Warnings are errors
};

using PDFAppSignatureVerificationPointer = std::shared_ptr<const PDFAppSignatureVerification>;

/// Summary of the signatures of a document (status bar item, panel)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSignatureSummary
{
public:
    int signatureCount = 0;     ///< Signatures (not timestamps)
    int timestampCount = 0;     ///< Document timestamps
    int validCount = 0;
    int warningCount = 0;
    int invalidCount = 0;
};

/// Verification of the signatures of the document of a tab (owner decision O15):
/// offline, in a worker thread, after the document is shown and again after it
/// was saved (the bytes of the file have changed), after a change of the settings
/// signatures.* and of the trusted certificates. Trusted are always the bundled
/// AATL list (PDFSignatureHandler::AATLCertificates::Always), the certificates of
/// the user (table trusted_certificate) and, by signatures.useSystemStore, the
/// store of the system; the old TrustedCertStorage.bin is never read. The results
/// belong to the bytes of the file: a change of the document in memory keeps them
/// (they are given to the new version), a running verification of other bytes is
/// canceled and nobody waits for it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSignatureVerifier : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    using Watcher = QFutureWatcher<PDFAppSignatureVerificationPointer>;

    explicit PDFAppDocumentSignatureVerifier(PDFAppDocument* document, QObject* parent);
    virtual ~PDFAppDocumentSignatureVerifier() override;

    /// The verification runs
    bool isVerifying() const { return m_watcher != nullptr; }

    /// Result of the last verification (nullptr = none yet)
    const PDFAppSignatureVerificationPointer& getVerification() const { return m_verification; }

    /// The document has signature fields (known after the first verification)
    bool hasSignatures() const;

    /// Starts the verification of the current bytes of the file again
    void verify();

    /// Adds the certificate (DER) to the trusted certificates of the user and
    /// verifies the signatures again. \returns an error text (empty = success).
    QString trustCertificate(const QByteArray& certificateData);

    /// Settings of the verification from the settings and the database
    static PDFAppSignatureSettings readSettings(PDFAppContext* context);

    /// Verifies the signatures (the function of the worker thread)
    static PDFAppSignatureVerificationPointer runVerification(PDFDocumentPointer document,
                                                              QByteArray sourceData,
                                                              PDFAppSignatureSettings settings,
                                                              std::shared_ptr<PDFAppCancelFlag> cancelFlag);

    /// Status of the card of a result (strict: a warning is invalid)
    static PDFComponentPainter::SignatureStatus getStatus(const PDFSignatureVerificationResult& result, bool isStrict);

    /// Summary of the results
    static PDFAppSignatureSummary getSummary(const std::vector<PDFSignatureVerificationResult>& results, bool isStrict);

    /// Text of the status bar item ("2 signatures · 1 needs attention")
    static QString getStatusText(const PDFAppSignatureSummary& summary, bool isVerified);

    /// Content of the card of a result (Figma Signature card 192:1032)
    /// \param result Result of the verification
    /// \param isStrict Warnings are errors
    /// \param trustedCertificates Certificates trusted by the user (no link for them)
    static PDFComponentPainter::SignatureCard getCard(const PDFSignatureVerificationResult& result, bool isStrict, const std::vector<QByteArray>& trustedCertificates);

    /// Index of the certificate of the chain, which can be trusted by the user
    /// (the last certificate of a chain without a trusted root), -1 = none
    static int getTrustableCertificateIndex(const PDFSignatureVerificationResult& result, const std::vector<QByteArray>& trustedCertificates);

Q_SIGNALS:
    /// The verification has started or ended, the results have changed
    void verificationChanged();

private:
    void startVerification();
    void stopVerification();
    void onVerificationFinished();
    void onDocumentStateChanged();
    void onDocumentChanged(const pdf::PDFModifiedDocument& modified);
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onDatabaseEverythingChanged();
    void applyResults();

    QPointer<PDFAppDocument> m_document;
    QPointer<Watcher> m_watcher;
    std::shared_ptr<PDFAppCancelFlag> m_cancelFlag;
    QByteArray m_verifiedData;          ///< Bytes of the file of the running or the last verification
    PDFAppSignatureVerificationPointer m_verification;
    bool m_isApplyingResults = false;
};

/// Item of the status bar with the summary of the signatures (Figma status-signatures
/// 266:7859: Status bar item, tone warning, "2 signatures · 1 needs attention"); a
/// click shows the Signatures panel. Hidden without signatures.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSignatureStatusItem : public QToolButton
{
    Q_OBJECT

private:
    using BaseClass = QToolButton;

public:
    explicit PDFAppSignatureStatusItem(PDFAppDocumentSignatureVerifier* verifier, PDFAppDocumentView* view, QWidget* parent = nullptr);
    virtual ~PDFAppSignatureStatusItem() override;

    void updateItem();

private:
    void onClicked();

    QPointer<PDFAppDocumentSignatureVerifier> m_verifier;
    QPointer<PDFAppDocumentView> m_view;
};

/// Panel Signatures of the sidebar (Figma 195:2959): the summary banner and the
/// cards of the signatures and timestamps (PDFSignatureCard) with the details and
/// the certificate chain; "Add to trusted certificates" trusts the last
/// certificate of a chain without a trusted root (after a confirmation).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSignaturesPanel : public PDFAppDocumentPanel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanel;

public:
    explicit PDFAppDocumentSignaturesPanel(QWidget* parent = nullptr);
    virtual ~PDFAppDocumentSignaturesPanel() override;

    virtual QString getId() const override;
    virtual QString getTitle() const override;
    virtual QString getSubtitle() const override;
    virtual QString getIconName() const override;
    virtual void setDocument(PDFAppDocument* document, PDFAppDocumentView* view) override;

    /// Cards of the results (in the order of the results)
    std::vector<PDFSignatureCard*> getCards() const;

    /// Banner of the summary (nullptr, when it is hidden)
    PDFTintedCard* getBanner() const;

    /// Confirmation of Add to trusted certificates, while it is shown
    PDFMessageBox* getTrustDialog() const;

private:
    void rebuild();
    void onVerificationChanged();
    void onCertificateLinkActivated(int index);
    void onTrustDialogFinished(int result);

    QPointer<PDFAppDocumentSignatureVerifier> m_verifier;
    QPointer<QScrollArea> m_scrollArea;
    QPointer<QWidget> m_content;
    QPointer<QVBoxLayout> m_contentLayout;
    QPointer<PDFTintedCard> m_banner;
    QPointer<PDFLabel> m_bannerTitle;
    QPointer<PDFLabel> m_bannerText;
    QPointer<PDFIconBadge> m_bannerIcon;
    std::vector<QPointer<PDFSignatureCard>> m_cards;
    std::vector<QByteArray> m_trustableCertificates;    ///< Certificate of the link of each card (empty = no link)
    QPointer<PDFMessageBox> m_trustDialog;
    QByteArray m_pendingCertificate;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTSIGNATURES_H
