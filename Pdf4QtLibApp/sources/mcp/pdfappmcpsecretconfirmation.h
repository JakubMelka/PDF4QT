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

#ifndef PDFAPPMCPSECRETCONFIRMATION_H
#define PDFAPPMCPSECRETCONFIRMATION_H

#include "pdfappglobal.h"
#include "pdfappmcpconfirmdialog.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpasswordcard.h"
#include "pdfcertificatestore.h"
#include "pdfdocumenttool_encryption.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <memory>
#include <optional>
#include <vector>

namespace pdf
{

class PDFAppContext;
class PDFAppToolCallout;
class PDFAppToolCertificateList;
class PDFAppToolCertificateProvider;
class PDFLabel;

/// The confirmation of an action of MCP, which needs a password or a certificate (Protect with
/// Password, Restrict Permissions, Encrypt with Certificate; plan of step 5 of phase 2, 6.3, owner
/// answer Q5.6, R5-12; package P4): an assistant never sends nor receives a password (the Secrets and
/// the certificate are MCP-excluded, PDFDocumentToolParameterDefinition::isMcpExcluded), the user
/// types them here - also when the permission of the action is Allow, the tool cannot run without
/// them. No frame in Figma (plan 2.5): the confirmation of an assistant (958:28030) of the variant of
/// the result - Save a copy (958:27539) for save_copy, the change (940:133693) for keep, the save over
/// the file (940:134248) for overwrite - with the facts of the asked protection, the password cards
/// (PDFAppToolPasswordCard with its Strength meter) or the list of the certificates with the card of
/// the password of the .pfx file (PDFAppToolCertificateList, PDFAppToolCertificateProvider), the
/// warning of a weaker algorithm than the current one (the downgrade of 6.9) and of a certificate
/// encryption the user cannot remove (R5-21). Its verb is the verb of the result (Save Copy, Allow,
/// Save) and Deny cancels; no default button, the input delay of the confirmations.
///
/// Before it ends Confirmed, the values are checked by the validation of the tool on the document
/// (PDFDocumentTool::validate): an error is shown at its card (432:3721 the same password, 447:15766
/// the passwords do not match, 433:3972 "Invalid certificate or password.") and the dialog stays open.
/// A document, which keeps its restrictions, needs its own owner password (owner answer Q5.1): the
/// card of the owner password appears, when the validation asks for it, and the run gets
/// ownerSameAsOpen false.
///
/// Lifetime: new + QPointer (the call may end while the dialog is open); shown by showInTab as a child
/// of a PDFAppMcpConfirmScrim over the tab of the document (modal for that tab only, as every
/// confirmation of an assistant), else Qt::WA_DeleteOnClose. The opener keeps a QPointer and calls
/// withdraw when the call ends without the user. The dialog waits at most getTimeout (TIMEOUT, 2
/// minutes) from the moment it is shown, at most QUEUE_TIMEOUT before it is shown (a hidden tab), then
/// ends TimedOut - the call returns isError with "state": "password_required". At the end the dialog
/// (with its scrim) is deleted later and confirmationEnded is the last call. The values go only to the
/// run (takeRunValues, takeUserCertificates, once): never into the response, the activity of MCP, a
/// log or the database; the fields are cleared at the end (S5.1) and the values, which were not taken,
/// by the destructor (PDFSecureMemory::clear).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpSecretConfirmation : public PDFAppMcpConfirmDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppMcpConfirmDialog;

public:
    /// The longest wait for the user from the moment the dialog is shown (R5-12: 2 minutes, as the
    /// password screen of files.open - a product limit)
    static constexpr int TIMEOUT = 2 * 60 * 1000;

    /// The longest wait before the dialog is shown (its tab hidden; the bound of the queue of the
    /// confirmations, PDFAppMcpConfirmationUi::DEFAULT_QUEUE_TIMEOUT)
    static constexpr int QUEUE_TIMEOUT = 10 * 60 * 1000;

    /// How the confirmation ended
    enum class Outcome
    {
        Pending,    ///< Not ended yet
        Confirmed,  ///< The user typed the values and confirmed (the run starts)
        Canceled,   ///< The user canceled (the activity "denied_user")
        TimedOut,   ///< No answer in time (isError, "state": "password_required")
        Withdrawn   ///< The call ended without the user (canceled, disconnected, the tab closed)
    };
    Q_ENUM(Outcome)

    /// A confirmation without the details of a call (no document, the variant of a change)
    /// \param context Context of the application (the certificates of the user)
    /// \param toolId The tool of the action (protect.password, protect.permissions, protect.certificate-encrypt)
    /// \param parameters The normalized parameters of the assistant (no Secret, no certificate)
    /// \param parent Parent widget
    explicit PDFAppMcpSecretConfirmation(PDFAppContext* context, const QString& toolId, const QVariantMap& parameters, QWidget* parent);

    /// The confirmation of a call: \p confirmation gives the assistant, the document, the variant
    /// (kind) and the details of the result (the path, the content, the facts of the signatures); the
    /// facts of the asked protection are added to them
    explicit PDFAppMcpSecretConfirmation(PDFAppContext* context, std::shared_ptr<PDFAppMcpConfirmation> confirmation, const QString& toolId,
                                         const QVariantMap& parameters, QWidget* parent);
    virtual ~PDFAppMcpSecretConfirmation() override;

    /// Shows the confirmation over \p tab (a new PDFAppMcpConfirmScrim, the owner of the dialog) and
    /// gives the focus to its first field
    static PDFAppMcpSecretConfirmation* showInTab(PDFAppContext* context, std::shared_ptr<PDFAppMcpConfirmation> confirmation, const QString& toolId,
                                                  const QVariantMap& parameters, QWidget* tab);

    Outcome getOutcome() const { return m_outcome; }

    /// The values of the Secrets and of the certificate for the run (Confirmed only; once - the
    /// dialog forgets them): the passwords, the fingerprint of the certificate and ownerSameAsOpen
    /// false, when the document needs its own owner password (Q5.1)
    QVariantMap takeRunValues();

    /// The chosen certificate with its PKCS#12 for PDFAppToolRunEnvironment::userCertificates
    /// (Encrypt with Certificate, Confirmed only; once)
    std::vector<PDFCertificateEntry> takeUserCertificates();

    /// The call has ended without the user: the dialog ends Withdrawn and closes (deleteLater)
    void withdraw();

    /// Every needed value is typed (the cards are complete, a certificate is chosen)
    bool isConfirmEnabled() const;

    /// The parts (tests; nullptr for a part, which the tool has not)
    PDFAppToolPasswordCard* getOpenPasswordCard() const { return m_openCard.data(); }
    PDFAppToolPasswordCard* getOwnerPasswordCard() const { return m_ownerCard.data(); }
    PDFAppToolPasswordCard* getCertificatePasswordCard() const { return m_certificateCard.data(); }
    PDFAppToolCertificateList* getCertificateList() const;

    /// The titles and the texts of the shown callouts (the downgrade, the certificate, the errors)
    QStringList getCalloutTexts() const;

    /// The time of the user (TIMEOUT; tests shorten it); at least 1 ms
    static void setTimeout(int timeout);
    static int getTimeout();

    /// Rank of the strength of an encryption method of document.get_info ("RC4 40-bit" < "RC4" <
    /// "AES 128-bit" / "certificate" < "AES 256-bit"; 0 = not encrypted or unknown)
    static int getMethodStrength(const QString& method);

    /// Rank of the strength of an algorithm of the tool (an id of PDFDocumentToolEncryption::
    /// ALGORITHM_*, or the certificate encryption: AES 256-bit)
    static int getAlgorithmStrength(const QString& algorithm, bool isCertificate);

    /// Clears the strings of \p values (PDFSecureMemory::clear of each string, which only the map
    /// holds) and empties the map (S5.1)
    static void clearSecretValues(QVariantMap& values);

Q_SIGNALS:
    /// The confirmation has ended (the last call of the end; the dialog and its scrim are deleted later)
    void confirmationEnded(pdf::PDFAppMcpSecretConfirmation::Outcome outcome);

protected:
    virtual void showEvent(QShowEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    /// The facts of the asked protection added to the details of \p confirmation (the rows of a change,
    /// the facts of a save); returns it
    static std::shared_ptr<PDFAppMcpConfirmation> prepareConfirmation(std::shared_ptr<PDFAppMcpConfirmation> confirmation, const QString& toolId, const QVariantMap& parameters);

    void build();
    void updateTexts();
    void updateCardLabels(PDFAppToolPasswordCard* card);
    void updateConfirmState();
    void onAllowed(quint64 confirmationId);
    void onDenied(quint64 confirmationId);
    void onCardChanged();
    void onCertificateChosen(const QString& fingerprint);
    void onTimeout();
    void onTick();

    /// The values of the run of the typed passwords; false and the errors shown at the cards
    bool prepareRunValues();

    /// Shows the errors of the validation at their cards (an error of another parameter in the
    /// callout of the errors)
    void showErrors(const std::vector<PDFDocumentToolParameterError>& errors);

    PDFAppToolPasswordCard* createCard(PDFAppToolPasswordCard::Kind kind);
    void showOwnerCard();
    void clearSecrets();
    void finish(Outcome outcome);

    QPointer<PDFAppContext> m_context;
    QString m_toolId;
    QVariantMap m_parameters;
    std::optional<PDFDocumentToolEncryption::Mode> m_mode;
    Outcome m_outcome = Outcome::Pending;
    QString m_currentMethod;                ///< The encryption of the document now (document.get_info), empty = none
    bool m_isOwnerForced = false;           ///< The document needs its own owner password (Q5.1)

    QPointer<PDFAppToolPasswordCard> m_openCard;
    QPointer<PDFAppToolPasswordCard> m_ownerCard;
    QPointer<PDFAppToolPasswordCard> m_certificateCard;
    QPointer<PDFAppToolCertificateList> m_certificateList;
    QPointer<QWidget> m_cardsContainer;
    QPointer<PDFAppToolCallout> m_downgradeCallout;
    QPointer<PDFAppToolCallout> m_lockoutCallout;
    QPointer<PDFAppToolCallout> m_errorCallout;
    QPointer<PDFLabel> m_secretNote;
    std::unique_ptr<PDFAppToolCertificateProvider> m_provider;

    QTimer m_timer;                         ///< The end of the time of the user
    QTimer m_tickTimer;                     ///< The countdown
    QElapsedTimer m_shownTimer;             ///< Since the first show
    bool m_isShown = false;

    QVariantMap m_runValues;                ///< The values of a confirmed run, until they are taken
    std::vector<PDFCertificateEntry> m_certificates;

    static int s_timeout;
};

}   // namespace pdf

#endif // PDFAPPMCPSECRETCONFIRMATION_H
