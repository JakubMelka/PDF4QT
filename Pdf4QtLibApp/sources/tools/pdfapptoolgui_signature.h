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

#ifndef PDFAPPTOOLGUI_SIGNATURE_H
#define PDFAPPTOOLGUI_SIGNATURE_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfappcertificatedialogs.h"   // before pdfapptoolcertificatelist.h: its inline getters need the dialogs
#include "pdfapptoolcertificatelist.h"
#include "pdfapptooldrawingpart.h"
#include "pdfapptooltimestampauthoritypart.h"
#include "pdfdocumenttool_signature.h"

#include <QFutureWatcher>
#include <QPointer>
#include <QTimer>

#include <optional>
#include <vector>

namespace pdf
{

class PDFAppCertificateCreateDialog;
class PDFAppCertificateImportDialog;
class PDFAppDocumentSignatureVerifier;
class PDFAppToolCallout;
class PDFAppToolPanelHeader;
class PDFAppToolPasswordCard;
class PDFKeyValueList;
class PDFLabel;
class PDFMessageBox;
class PDFSignatureCard;
class PDFTileRow;

/// An empty signature field of the document, which Sign with Certificate can sign (Q6.3, R6-25)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolSignatureField
{
public:
    QString name;           ///< The fully qualified name (the parameter field)
    QString disabledReason; ///< Why the field cannot be signed (a lock, /SV, the core); empty = it can
    bool isVisible = false; ///< A widget with a non-empty /Rect

    /// The empty signature fields of a document (no /V), walked over the parsed form (PDFForm::parse
    /// keeps one visited set and a depth limit); the reasons of /Lock (FieldMDP, 12.8.2.4 - its
    /// signature is not written, Q6.3) are filled here, the others by the GUI from the core
    static std::vector<PDFAppToolSignatureField> getEmptyFields(const PDFDocument* document);
};

/// The GUI of Sign with Certificate and Add Timestamp - one class, two presets (plan of step 6 of
/// phase 2, 2.1, 2.2, 4.5, 6.1 ... 6.3, K6.3, K6.4, K6.8, K6.10, R6-4, R6-11; package P4 implements it):
/// Sign (434:4338 ... 434:9276) places PDFAppToolCertificateList in its signing mode
/// (PDFAppToolCertificateList::setUsage(Signing), package P5) with the password card
/// (PDFAppToolPasswordCard::Kind::Certificate) and its live check (one job, 6.3), the menus Signature,
/// Visible (the empty signature fields of Q6.3) and Format (Q6.1), the drawing of the appearance
/// (PDFAppToolDrawingPart) and the authority (PDFAppToolTimestampAuthorityPart); Timestamp
/// (435:7037 ... 435:8356) the cards of the earlier signatures and the authority. The result dialog is
/// its FileOnly branch (PDFAppToolResultDialog::ResultKind::FileOnly, the suffixes _SIGNED and
/// _TIMESTAMPED, no Then continue with).
///
/// Secrets (6.1, R6-11): the password lives in its field only; getRunParameters gives it to the run, the
/// field is emptied at the start of the run, at Cancel, at the close of the tool and at a change of the
/// certificate; the run keeps it until its end or until the error dialog of the authority closes (Try
/// Again / Sign Without Timestamp use it, Change Authority and the close forget it).
///
/// The questions before a run (434:7495 "Sign without a visible appearance?", 434:7784 "Graphics are on
/// more than one page") are asked when the Primary button is pressed: prepareEnvironment refuses the
/// run while the question is open (the shell drops the values of the run), the answer confirms again.
///
/// The live check of the password (6.3): 500 ms after the last change one QtConcurrent::run job gets a
/// copy of the PKCS#12 and of the password (PDFSignatureFactory::isPasswordValid), returns a bool and
/// releases the OpenSSL state of its thread (B137); a newer change drops the result of an older job (a
/// generation); the job holds no QObject. The watcher is a child of this GUI.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUISignature : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    using Mode = PDFDocumentToolSignature::Mode;

    /// The state of the live check of the password (6.3)
    enum class PasswordState
    {
        NotChecked, ///< Nothing typed, no file chosen, or the check waits / runs
        Valid,      ///< The password opens the .pfx file
        Invalid     ///< 434:7143 "Password to open certificate is invalid."
    };

    /// The pause of the live check after the last key (6.3)
    static constexpr int PASSWORD_CHECK_DELAY = 500;

    /// Widths of the menus: Signature 330 (434:5308), Visible 320 (434:5327)
    static constexpr int TYPE_PANEL_WIDTH = 330;
    static constexpr int APPEARANCE_PANEL_WIDTH = 320;

    /// Width of the card of the error of the timestamp authority (434:8518, 435:8117): the groups of the
    /// buttons Close, Change Authority | Sign Without Timestamp, Try Again keep their gap
    static constexpr int TIMESTAMP_ERROR_WIDTH = 680;

    explicit PDFAppToolGUISignature(QObject* parent = nullptr);
    virtual ~PDFAppToolGUISignature() override;

    /// Registers what is not registered yet (PDFAppBuiltinTools::registerAll): the two tools of
    /// PDFDocumentToolSignature in PDFDocumentToolRegistry and this GUI for each of them in
    /// PDFDocumentToolGUIRegistry
    static void registerSignatureTools();

    /// The preset of the GUI: the mode of the id of its tool (Sign before the attachment)
    Mode getMode() const;

    /// The state of the live check
    PasswordState getPasswordState() const { return m_passwordState; }

    /// The document of the tab: its certification level forbids the run (R6-4: Sign with P = 1; Sign
    /// and Timestamp with P = 1 and unsaved changes), empty = allowed
    QString getCertificationRefusal() const;

    /// The empty signature fields of the document of the tab (Q6.3) with their reasons
    const std::vector<PDFAppToolSignatureField>& getEmptyFields() const;

    /// Empties the password of the GUI (6.1)
    void clearSecrets();

    /// Parts (tests; nullptr = none)
    PDFAppToolDrawingPart* getDrawingPart() const { return m_drawingPart.data(); }
    PDFAppToolTimestampAuthorityPart* getAuthorityPart() const { return m_authorityPart.data(); }
    PDFAppToolCertificateList* getCertificateList() const { return m_certificateList.data(); }
    QWidget* getEmptyState() const { return m_emptyState.data(); }
    QWidget* getPasswordFrame() const { return m_passwordFrame.data(); }
    PDFLabel* getPasswordLabel() const { return m_passwordLabel.data(); }
    PDFAppToolPasswordCard* getPasswordCard() const { return m_passwordCard.data(); }
    QWidget* getAuthoritySection() const { return m_authoritySection.data(); }
    PDFAppToolPanelHeader* getRightHeader() const { return m_rightHeader.data(); }
    PDFAppToolPanelHeader* getLeftHeader() const { return m_leftHeader.data(); }
    PDFAppToolCallout* getNoAppearanceCallout() const { return m_noAppearanceCallout.data(); }
    PDFAppToolCallout* getUnsavedCallout() const { return m_unsavedCallout.data(); }
    PDFAppToolCallout* getCertifiedCallout() const { return m_certifiedCallout.data(); }
    PDFAppToolCallout* getNoCertificateNeededCallout() const { return m_noCertificateCallout.data(); }
    PDFKeyValueList* getSummaryRows() const { return m_summaryRows.data(); }
    QWidget* getElementsSection() const { return m_elementsSection.data(); }
    QWidget* getStyleSection() const { return m_styleSection.data(); }
    PDFAppToolPanelPopup* getAppearancePanel() const { return m_appearancePanel.data(); }
    PDFTileRow* getAppearanceRow(const QString& id) const;
    const std::vector<QPointer<PDFSignatureCard>>& getSignatureCards() const { return m_signatureCards; }
    PDFMessageBox* getQuestionDialog() const { return m_questionDialog.data(); }
    PDFMessageBox* getTimestampErrorDialog() const { return m_timestampErrorDialog.data(); }

    /// The password kept for Try Again / Sign Without Timestamp (R6-11; tests of the canary): empty =
    /// none is kept
    bool isRetryPasswordKept() const { return !m_retryPassword.isEmpty(); }

    /// The values of prepareEnvironment for the next run (tests of 6.1)
    const QVariantMap& getCertificateRunValues() const { return m_certificateRunValues; }

    virtual QWidget* createOverlay(QWidget* canvasArea) override;
    virtual void setContentEnabled(bool isEnabled) override;
    virtual QString getConfirmText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual std::vector<PDFSecurityHandler::Permission> getRunPermissions() const override;
    virtual bool handlesPermissionRefusal() const override;
    virtual QVariantMap getRunParameters() const override;
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual bool confirmResult(const PDFDocumentToolResult& result) override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onRunStateChanged(bool isRunning) override;
    virtual void onRunRefused() override;
    virtual void onRunNotAccepted(const PDFDocumentToolResult& result) override;
    virtual void onDocumentChanged() override;

    /// Esc: the active creation tool of the drawing of Sign ends (owner 2026-11-17), then the selected
    /// elements are deselected (owner 2026-11-18); the tool stays
    virtual bool handleEscape() override;
    virtual QString getEscapeModeHint() const override;
    virtual void retranslate() override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

private:
    /// The question before a run (434:7495, 434:7784)
    enum class Question
    {
        None,
        NoGraphics,
        MultiplePages
    };

    void ensureParts();
    void describeSignLeftPanel(PDFAppToolPanelBuilder& builder);
    void describeTimestampLeftPanel(PDFAppToolPanelBuilder& builder);
    void describeSignRightPanel(PDFAppToolPanelBuilder& builder);
    void describeTimestampRightPanel(PDFAppToolPanelBuilder& builder);

    /// The parts show the values and the document (no signal of the GUI)
    void updateParts();
    void updateSummary();
    void updateAppearanceMenu();
    void updateSignatureCards();
    void updateCanvasHint();

    /// The primary button and the status follow the parts (the last calls)
    void notifyStateChanged();

    /// Reads the certificates again and chooses \p filePath (empty: the current one, else the first
    /// one, which can be chosen)
    void reloadCertificates(const QString& filePath);

    bool isTimestampRequested() const;
    bool isVisibleAppearance() const;
    QString getChosenField() const;
    QString getSignerName() const;

    /// The earlier signatures and timestamps of the document (the verification of the tab)
    int getEarlierSignatureCount() const;
    PDFAppDocumentSignatureVerifier* getVerifier() const;

    /// The password of the field or the one kept for a retry (R6-11)
    QString getRunPassword() const;

    /// The values of the certificate of a run: the fingerprint and the password, the entry with its
    /// PKCS#12 (false and the reason, which the field shows)
    bool prepareCertificate(QVariantMap* values, std::vector<PDFCertificateEntry>* certificates, QString* errorMessage) const;
    void dropCertificateRunValues() const;
    void forgetRetryPassword();

    /// Try Again, Sign Without Timestamp (R6-11): confirms with the kept password and the answered
    /// question of the failed run; a run, which does not start, forgets the password (the last call)
    void confirmRetry();

    /// The question, which the values of now need before a run (None = none)
    Question getNeededQuestion() const;
    void openQuestion(Question question);
    void openTimestampErrorDialog(const PDFDocumentToolResult& result);
    QWidget* getDialogParent() const;

    void startPasswordCheck();
    static bool checkPassword(QByteArray pkcs12, QByteArray password);

    void onCertificateChosen();
    void onPasswordChanged();
    void onPasswordTimer();
    void onPasswordCheckFinished();
    void onUrlChanged();
    void onDrawingChanged();
    void onDrawingSelectionChanged();
    void onClearRequested();
    void onAppearanceRowClicked();
    void onCreateClicked();
    void onImportClicked();
    void onCertificateCreated(const QString& fileName);
    void onCertificateImported(const QString& fileName);
    void onVerificationChanged();
    void onQuestionAccepted();
    void onQuestionFinished();
    void onTryAgainClicked();
    void onSignWithoutTimestampClicked();
    void onChangeAuthorityClicked();
    void onTimestampErrorFinished();
    void onPendingRunEnded();

    PDFAppToolCertificateProvider m_provider;
    std::vector<PDFAppToolCertificateItem> m_certificates;
    mutable std::optional<std::vector<PDFAppToolSignatureField>> m_fields;   ///< Of the shown version (onDocumentChanged drops it)

    QPointer<PDFAppToolDrawingPart> m_drawingPart;                  ///< Child object (Sign)
    QPointer<PDFAppToolTimestampAuthorityPart> m_authorityPart;     ///< Child object
    QPointer<PDFAppToolCertificateList> m_certificateList;
    QPointer<QWidget> m_signerSection;
    QPointer<QWidget> m_emptyState;
    QPointer<QWidget> m_passwordFrame;
    QPointer<PDFLabel> m_passwordLabel;
    QPointer<PDFAppToolPasswordCard> m_passwordCard;
    QPointer<PDFAppParagraph> m_noPasswordText;
    QPointer<QWidget> m_authoritySection;
    QPointer<PDFAppToolPanelHeader> m_leftHeader;
    QPointer<PDFAppToolPanelHeader> m_rightHeader;
    QPointer<PDFAppToolCallout> m_noAppearanceCallout;
    QPointer<PDFAppToolCallout> m_unsavedCallout;
    QPointer<PDFAppToolCallout> m_certifiedCallout;
    QPointer<PDFAppToolCallout> m_noCertificateCallout;
    QPointer<PDFAppParagraph> m_timestampNote;
    QPointer<QWidget> m_elementsSection;
    QPointer<QWidget> m_styleSection;
    QPointer<PDFKeyValueList> m_summaryRows;
    QPointer<PDFAppToolPanelPopup> m_appearancePanel;
    std::vector<std::pair<QString, QPointer<PDFTileRow>>> m_appearanceRows;
    QPointer<QWidget> m_cardsContainer;
    std::vector<QPointer<PDFSignatureCard>> m_signatureCards;
    QPointer<PDFAppDocumentSignatureVerifier> m_verifier;

    QPointer<PDFAppCertificateCreateDialog> m_createDialog;
    QPointer<PDFAppCertificateImportDialog> m_importDialog;
    QPointer<PDFMessageBox> m_questionDialog;
    QPointer<PDFMessageBox> m_timestampErrorDialog;
    Question m_openQuestion = Question::None;
    Question m_answeredQuestion = Question::None;   ///< The user has answered it: the next confirmation runs
    mutable Question m_pendingQuestion = Question::None;    ///< prepareEnvironment asks it (the run is refused)

    QPointer<QTimer> m_passwordTimer;
    QPointer<QFutureWatcher<bool>> m_passwordWatcher;
    int m_passwordGeneration = 0;
    PasswordState m_passwordState = PasswordState::NotChecked;
    QString m_unlockError;
    QString m_chosenCertificatePath;               ///< The chosen file of the list (a new list after a change of the language keeps it)

    mutable QVariantMap m_certificateRunValues;    ///< The certificate of prepareEnvironment for the next getRunParameters (dropped at the start of the run)
    QString m_retryPassword;                       ///< R6-11: the password of the run until the error dialog of the authority closes
    QString m_retryFilePath;                       ///< The .pfx file of the kept password
    bool m_isRetrying = false;                     ///< Try Again / Sign Without Timestamp confirm with the kept password
    Question m_retryAnsweredQuestion = Question::None;  ///< The answer of the failed run: its retry does not ask again
    bool m_isRunPending = false;                   ///< A run has ended, its result has not decided the kept password yet
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_SIGNATURE_H
