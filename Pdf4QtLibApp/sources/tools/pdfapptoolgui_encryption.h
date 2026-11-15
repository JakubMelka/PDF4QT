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

#ifndef PDFAPPTOOLGUI_ENCRYPTION_H
#define PDFAPPTOOLGUI_ENCRYPTION_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfappcertificatedialogs.h"   // before pdfapptoolcertificatelist.h: its inline getters need the dialogs
#include "pdfapptoolcertificatelist.h"
#include "pdfapptoolchangelist.h"
#include "pdfapptoolgui_protect.h"
#include "pdfapptoolpasswordcard.h"
#include "pdfapptoolpermissionlist.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttool_encryption.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"

#include <QCheckBox>
#include <QPointer>

#include <map>
#include <optional>

namespace pdf
{

class PDFDocument;

/// The protection of the document of a tool of the encryption, as the panels Now show it
/// (430:1068, 430:2481, 433:5022, 433:5811): read of its security handler, no state of the GUI
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolProtectionFacts
{
public:
    EncryptionMode mode = EncryptionMode::None;                 ///< The handler of the document
    QString algorithmTitle;                                     ///< "AES 256-bit", "RC4 128-bit", "Certificate"; empty = none
    bool isOpenPasswordRequired = false;                        ///< The empty user password does not open it (7.6.4.4); false for isAttachmentsOnly
    bool isAttachmentsOnly = false;                             ///< Only the embedded files are encrypted (7.6.6, /StmF and /StrF Identity): the document opens freely, its attachments need the password (review RS5SEC L5)
    bool isOwner = false;                                       ///< Opened or unlocked by the owner password (a certificate: bit 2 of its recipient)
    std::vector<PDFSecurityHandler::Permission> forbidden;      ///< What the file forbids by /P (PDFAppDocumentPermissions::getFileForbiddenPermissions)
    QString recipientName;                                      ///< A certificate: the holder (common name) of the certificate, whose private key opened it (PDFPublicKeySecurityHandler::getAuthorizedCertificate); empty = not known

    /// The facts of a document (null = not encrypted)
    static PDFAppToolProtectionFacts create(const PDFDocument* document);

    /// "Certificate" for the public-key handler (433:5811), empty for another handler
    static QString getAlgorithmTitle(const PDFSecurityHandler* securityHandler);

    /// The encryption of the document in the rows Now (PDFDocumentToolEncryption::getEncryptionName:
    /// "AES 256-bit", "AES 128-bit", "RC4 128-bit"; "Certificate" of 433:5811)
    static QString getEncryptionTitle(const PDFDocument* document);

    /// "All allowed", "Printing only", "Viewing only" or "3 of 8 allowed" of the forbidden permissions
    static QString getPermissionsText(const std::vector<PDFSecurityHandler::Permission>& forbidden);

    /// The value of the row Open password of these facts: Required / Not required / the text of
    /// the attachments (RS5SEC L5) - the text of the fact of the core,
    /// PDFDocumentToolEncryption::getOpenPasswordText
    QString getOpenPasswordText() const;
};

/// The GUI of the four tools of the encryption - one class, four presets (plan of step 5 of phase
/// 2, 4.2, K5.1; tools.md 10.3 point 30): Protect with Password (430:1068 ...), Restrict Permissions
/// (432:2105 ...), Encrypt with Certificate (433:3049 ...) and Remove Password (433:5022 ...). The
/// preset is the id of the tool (getMode, after the attachment to the shell); each preset is laid
/// out exactly by its frames from the shared parts: PDFAppToolPasswordCard, PDFStrengthMeter,
/// PDFAppToolPermissionList, PDFAppToolChangeList (P2), PDFAppToolCertificateList with
/// PDFAppToolCertificateProvider and the dialogs of the certificates (P3), the summaries Now / After
/// (PDFKeyValueList), the menus of PDFAppToolPanelBuilder, the callouts (PDFAppToolCallout).
/// Package P2 implements it with the presets Password, Permissions and Unlock; the preset
/// Certificate places PDFAppToolCertificatePresetPart of P3 (the list, the password of the .pfx
/// file, Create Certificate… and Import…) into its canvas and toolbar and asks it for its state
/// and for the values of the run (PDFAppToolCertificatePresetPart::prepareRun).
///
/// The passwords never enter the parameter model (S5.2, PDFAppToolParameterModel::setValue refuses
/// a Secret): the cards keep them, getRunParameters adds them to the values of the run only, and
/// prepareEnvironment gives the chosen certificate with its PKCS#12
/// (PDFAppToolRunEnvironment::userCertificates). The run asks for the owner authorization by
/// PDFAppPermissionAction::ChangeSecurity (K5.13, R5-4).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIEncryption : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    explicit PDFAppToolGUIEncryption(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIEncryption() override;

    /// Registers what is not registered yet (PDFAppBuiltinTools::registerAll): the four tools of
    /// PDFDocumentToolEncryption in PDFDocumentToolRegistry and this GUI for each of them in
    /// PDFDocumentToolGUIRegistry. Empty in the skeleton: package P2 registers them, when the core
    /// tool of P1b and the parts are ready (the tools stay unimplemented until then).
    static void registerEncryptionTools();

    /// The preset of the GUI: the mode of the id of its tool (Password before the attachment)
    PDFDocumentToolEncryption::Mode getMode() const;

    using Mode = PDFDocumentToolEncryption::Mode;
    using Preset = PDFDocumentToolEncryption::Preset;

    /// Width of the forms of the canvas: 620 (430:1194, 433:5141), 640 of the list of the
    /// permissions (432:2283) and of the certificate (433:3182); the top 20, the parts 12 apart
    static constexpr int FORM_WIDTH = 620;
    static constexpr int WIDE_FORM_WIDTH = 640;
    static constexpr int FORM_TOP = 20;
    static constexpr int FORM_GAP = 12;

    /// Widths of the menus: Preset 330 (432:2698), Algorithm 328, Encrypt 340 (430:1497)
    static constexpr int PRESET_PANEL_WIDTH = 330;
    static constexpr int ALGORITHM_PANEL_WIDTH = 328;
    static constexpr int SCOPE_PANEL_WIDTH = 340;

    /// The algorithm of the values of the model (AES 256-bit of a preset without it)
    PDFSecurityHandlerFactory::Algorithm getAlgorithm() const;

    /// The facts of the document of the tab
    PDFAppToolProtectionFacts getFacts() const;

    /// The owner password has to be its own one (owner answer Q5.1): the document keeps its
    /// restrictions (Protect with Password), or the preset sets them (Restrict Permissions)
    bool isOwnerPasswordRequired() const;

    /// The preset of the permissions of Restrict Permissions (Custom = none matches)
    Preset getCurrentPreset() const;

    /// Parts (nullptr = none)
    PDFAppToolPasswordCard* getOpenPasswordCard() const { return m_openCard.data(); }
    PDFAppToolPasswordCard* getOwnerPasswordCard() const { return m_ownerCard.data(); }
    QCheckBox* getOwnerSameCheck() const { return m_ownerSameCheck.data(); }
    PDFAppParagraph* getOwnerSameHint() const { return m_ownerSameHint.data(); }
    QCheckBox* getRequireOpenCheck() const { return m_requireOpenCheck.data(); }
    PDFAppToolPermissionList* getPermissionList() const { return m_permissionList.data(); }
    QWidget* getPermissionCard() const { return m_permissionCard.data(); }
    PDFIconButton* getPermissionToggle() const { return m_permissionToggle.data(); }
    PDFAppToolChangeList* getChangeList() const { return m_changeList.data(); }
    QWidget* getEmptyCard() const { return m_emptyCard.data(); }
    PDFKeyValueList* getNowRows() const { return m_nowRows.data(); }
    PDFKeyValueList* getSummaryRows() const { return m_summaryRows.data(); }
    PDFKeyValueList* getReadersRows() const { return m_readersRows.data(); }
    PDFStrengthMeter* getAlgorithmMeter() const { return m_algorithmMeter.data(); }
    PDFAppParagraph* getAlgorithmText() const { return m_algorithmText.data(); }
    PDFLabel* getHeadingTitle() const { return m_headingTitle.data(); }
    PDFAppParagraph* getHeadingText() const { return m_headingText.data(); }
    PDFAppToolCallout* getLostCallout() const { return m_lostCallout.data(); }
    PDFAppToolCallout* getReplacedCallout() const { return m_replacedCallout.data(); }
    PDFAppToolCallout* getWeakCallout() const { return m_weakCallout.data(); }
    PDFAppToolCallout* getLegacyCallout() const { return m_legacyCallout.data(); }
    PDFAppToolCallout* getNothingRestrictedCallout() const { return m_nothingRestrictedCallout.data(); }
    PDFAppToolCallout* getRemoveCallout() const { return m_removeCallout.data(); }
    PDFAppToolCallout* getInfoCallout() const { return m_infoCallout.data(); }
    PDFAppToolPanelPopup* getPresetPanel() const { return m_presetPanel.data(); }
    PDFTileRow* getPresetRow(Preset preset) const;
    PDFAppToolCertificatePresetPart* getCertificatePart() const { return m_certificatePart.data(); }
    QWidget* getForm() const { return m_form.data(); }

    /// Empties every password of the GUI (S5.1: after the start of a run, a change of the language)
    void clearSecrets();

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QString getConfirmText() const override;
    virtual QString getConfirmIconName() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual QString getPermissionTitle() const override;
    virtual QString getPermissionSubtitle() const override;
    virtual QVariantMap getRunParameters() const override;
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onRunStateChanged(bool isRunning) override;
    virtual void onRunRefused() override;
    virtual void onDocumentChanged() override;

    /// The values of prepareEnvironment for the next run (the fingerprint and the password of the
    /// .pfx file of Encrypt with Certificate; tests of S5.1)
    const QVariantMap& getCertificateRunValues() const { return m_certificateRunValues; }
    virtual void retranslate() override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

private:
    void buildForm();
    void buildPasswordForm(QWidget* form);
    void buildPermissionsForm(QWidget* form);
    void buildCertificateForm(QWidget* form);
    void buildUnlockForm(QWidget* form);
    QWidget* createOwnerPasswordCard(QWidget* parent);
    QWidget* createOpenPasswordCard(QWidget* parent);

    /// Every part shows the values and the document (no signal of the GUI)
    void updateParts();
    void dropCertificateRunValues() const;
    void updatePasswordErrors();
    void updateNowRows();
    void updateSummary();
    void updateCallouts();
    void updatePermissionCard();
    void updateUnlockForm();

    /// The parts have changed: the Primary button and the status (the last calls)
    void notifyStateChanged();

    /// The open password and the owner password are the same, where they may not be (432:3721,
    /// Q5.1): the validation of the core refuses the typed owner password
    bool isSamePasswordError() const;

    /// The errors of the validation of the core with the values of a run (Password, Permissions;
    /// the passwords of the copy are cleared after it)
    std::vector<PDFDocumentToolParameterError> getCoreErrors() const;

    /// The error of the core at the typed owner (\p isOwner) or open password, empty = none
    QString getFieldError(const std::vector<PDFDocumentToolParameterError>& errors, bool isOwner) const;

    /// The owner password is its own one in the run (not the open password)
    bool isOwnerPasswordSeparate() const;

    QString getScopeTitle() const;
    static QString getPresetTitle(Preset preset);

    void onPasswordChanged();
    void onOwnerSameToggled(bool isChecked);
    void onRequireOpenToggled(bool isChecked);
    void onPermissionsEdited(const QVariantMap& values);
    void onPresetRowClicked();
    void onPermissionToggleClicked();
    void onCertificateStateChanged();

    mutable std::optional<PDFAppToolProtectionFacts> m_facts;   ///< Of the shown version (getFacts reads it, onDocumentChanged drops it)
    bool m_isPermissionsExpanded = false;               ///< The card of the permissions of a certificate is open (433:4426)
    mutable QVariantMap m_certificateRunValues;         ///< The certificate of prepareEnvironment for the next getRunParameters (dropped at the start of the run)

    QPointer<PDFAppToolCanvasPage> m_canvasPage;
    QPointer<QWidget> m_form;
    QPointer<PDFLabel> m_headingTitle;
    QPointer<PDFAppParagraph> m_headingText;
    QPointer<PDFAppToolPasswordCard> m_openCard;
    QPointer<QWidget> m_openCardFrame;
    QPointer<PDFAppToolPasswordCard> m_ownerCard;
    QPointer<QWidget> m_ownerCardFrame;
    QPointer<QCheckBox> m_ownerSameCheck;
    QPointer<PDFAppParagraph> m_ownerSameHint;
    QPointer<QCheckBox> m_requireOpenCheck;
    QPointer<PDFAppParagraph> m_requireOpenHint;
    QPointer<PDFAppToolPermissionList> m_permissionList;
    QPointer<QWidget> m_permissionCard;
    QPointer<QWidget> m_permissionHeader;
    QPointer<PDFLabel> m_permissionTitle;
    QPointer<PDFAppParagraph> m_permissionSummary;
    QPointer<PDFIconButton> m_permissionToggle;
    QPointer<PDFAppToolChangeList> m_changeList;
    QPointer<QWidget> m_emptyCard;
    QPointer<PDFKeyValueList> m_nowRows;
    QPointer<PDFKeyValueList> m_summaryRows;
    QPointer<PDFKeyValueList> m_readersRows;
    QPointer<PDFStrengthMeter> m_algorithmMeter;
    QPointer<PDFAppParagraph> m_algorithmText;
    QPointer<PDFAppToolCallout> m_lostCallout;
    QPointer<PDFAppToolCallout> m_replacedCallout;
    QPointer<PDFAppToolCallout> m_weakCallout;
    QPointer<PDFAppToolCallout> m_legacyCallout;
    QPointer<PDFAppToolCallout> m_nothingRestrictedCallout;
    QPointer<PDFAppToolCallout> m_removeCallout;
    QPointer<PDFAppToolCallout> m_infoCallout;
    QPointer<PDFAppToolPanelPopup> m_presetPanel;
    std::map<Preset, QPointer<PDFTileRow>> m_presetRows;
    QPointer<PDFAppToolCertificatePresetPart> m_certificatePart;   ///< Child object of this GUI (created at the first canvas)
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_ENCRYPTION_H
