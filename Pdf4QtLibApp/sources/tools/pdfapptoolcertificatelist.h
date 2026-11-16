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

#ifndef PDFAPPTOOLCERTIFICATELIST_H
#define PDFAPPTOOLCERTIFICATELIST_H

#include "pdfappglobal.h"
#include "pdfcertificatestore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QWidget>

#include <atomic>
#include <optional>
#include <vector>

class QPushButton;
class QToolButton;

namespace pdf
{

class PDFAppCertificateCreateDialog;
class PDFAppCertificateImportDialog;
class PDFAppContext;
class PDFAppParagraph;
class PDFAppToolCallout;
class PDFAppToolCard;
class PDFAppToolPanelBuilder;
class PDFAppToolPasswordCard;
class PDFLabel;
class PDFTileRow;

/// What the certificates of a list are for (plan of step 6 of phase 2, 4.5, 10.3; package P5 implements
/// the rules of Signing): the states of the rows and which rows of the system store can be chosen
enum class PDFAppToolCertificateUsage
{
    Encryption, ///< Encrypt with Certificate and its confirmation of MCP (step 5): keyEncipherment of an RSA key; the system store Disabled (Q5.4)
    Signing     ///< Sign with Certificate and its confirmation of MCP (step 6): digitalSignature or nonRepudiation, an extended key usage allowing the signing (5.3, B116); the rows of the system store of Windows can be chosen (Q6.2), not on Linux and macOS (no system rows)
};

/// One row of the list of the certificates of Encrypt with Certificate (Figma 433:3049, 433:4208;
/// plan of step 5 of phase 2, 4.2, K5.9, owner answers Q5.4, Q5.5, Q5.11, R5-18). Plain values; a
/// row of a .pfx file never unlocked has only its file (the details come from the certificate cache
/// after the first unlock).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCertificateItem
{
public:
    /// Where the certificate is (the tag of the row: ".pfx file" / "System store"; R5-18: by
    /// PDFCertificateEntry::pkcs12, not by its entry type)
    enum class Source
    {
        PfxFile,
        SystemStore
    };

    /// Whether the row can be chosen, and why not (a Disabled row with its reason)
    enum class State
    {
        Valid,              ///< Can be chosen
        Locked,             ///< A .pfx file never unlocked: the file name and "Details are shown after the password" (Q5.11); can be chosen
        Expired,            ///< Out of its validity (433:3049, red text), Disabled
        NoEncryptionUsage,  ///< Without keyEncipherment (B99, an old certificate of the old Editor), Disabled
        NoSigningUsage     ///< The list for signing (PDFAppToolCertificateUsage::Signing): no digitalSignature nor nonRepudiation, or an extended key usage, which does not allow the signing of documents (plan of step 6 of phase 2, 5.3, B116), Disabled
    };

    Source source = Source::PfxFile;
    State state = State::Locked;
    QString filePath;               ///< The .pfx file (shown only as its name; never given to MCP); a row of the system store has the identifier "system-store:<fingerprint>" (PDFAppToolCertificateProvider::getSystemStorePath; no file - findEntry resolves it)
    QString fingerprint;            ///< SHA-256 of the certificate (PDFDocumentToolEncryption::getCertificateFingerprint); empty while Locked
    QByteArray certificateData;     ///< DER of the public X.509 certificate (the details of Manage Certificates); empty while Locked
    bool isUsableForEncryption = false; ///< The certificate can be a recipient of the encryption (PDFCertificateManager::isUsableForEncryption)
    bool isUsableForSigning = false;    ///< The certificate may sign documents (PDFAppToolCertificateProvider::isUsableForSigning, 5.3, B116)
    QString name;                   ///< Common name of the subject
    QString organization;           ///< Organization of the subject
    QString email;                  ///< E-mail of the subject
    QString issuer;                 ///< Name of the issuer (a certificate of the system store, 433:3049 "issued by")
    QDateTime validFrom;            ///< Start of the validity
    QDateTime validUntil;           ///< End of the validity (PDFAppLocale formats it, "valid until Sep 12, 2029")
    int keyBits = 0;                ///< Length of the key (the Strength meter of the algorithm, R5-20)
    bool isInOldFolder = false;     ///< A .pfx of the folder of the old Editor or Viewer (Q5.5: read only, its copy is offered at the first use)
    bool isPasswordProtected = true;///< The .pfx file opens only with a password (its certificate cannot be read without it)

    /// The row can be chosen (Valid, Locked)
    bool isChoosable() const { return state == State::Valid || state == State::Locked; }

    /// The title of the row: the common name, else the file name
    QString getTitle() const;
};

/// The certificates of the user for Encrypt with Certificate and its confirmation of MCP (plan of
/// step 5 of phase 2, 4.5, 7.2, R5-1, R5-7, Q5.5, Q5.11; package P3 implements it over the core API of
/// P1b): the .pfx files of the certificate folder of the application and, read only, of the old
/// Editor and Viewer (PDFCertificateManager), with the details of the certificate cache
/// (PDFCertificateCacheRepository); unlock opens one file by its password, stores its public
/// certificate into the cache and gives the entry of the run (PDFAppToolRunEnvironment::
/// userCertificates). The password is used once and never kept. A value object, GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCertificateProvider
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppToolCertificateProvider)

public:
    using Usage = PDFAppToolCertificateUsage;

    explicit PDFAppToolCertificateProvider(PDFAppContext* context);

    /// The rows of the list (433:3049) made for the usage, the valid ones first. A file of an old
    /// folder, whose copy with the same bytes is in the folder of the application, is listed once
    /// (the copy). The certificates of the system store: for Encryption those, which can be a recipient
    /// (choosable by their public certificate, the addendum of the owner of 15. 11.), for Signing those,
    /// which may sign (choosable, Q6.2).
    std::vector<PDFAppToolCertificateItem> getCertificates(Usage usage = Usage::Encryption) const;

    /// Every certificate of Manage Certificates (436:9398): the .pfx files of the folder of the
    /// application and of the old folders (a copied file once) in the order of the folders, then
    /// every personal certificate of the system store (isSystemStoreIncluded); the states of the
    /// usage Signing (Locked, Expired), the flags isUsableFor… of each certificate
    std::vector<PDFAppToolCertificateItem> getManagedCertificates() const;

    /// The prefix of the identifier of a row of the system store (PDFAppToolCertificateItem::filePath)
    static constexpr const char* SYSTEM_STORE_PREFIX = "system-store:";

    /// The identifier of a row of the system store of the fingerprint ("system-store:<fingerprint>")
    static QString getSystemStorePath(const QString& fingerprint);

    /// The path is the identifier of a row of the system store, not a file
    static bool isSystemStorePath(const QString& filePath);

    /// The certificate may sign documents (plan of step 6 of phase 2, 5.3, B116): the one rule of
    /// PDFSignatureFactory::unlock, PDFCertificateManager::isUsableForSigning of the certificate of the
    /// info. An info without its certificate (no row of a real certificate: a .pfx, whose certificate
    /// is not known, is Locked) cannot sign. unlock checks the certificate again before a run.
    static bool isUsableForSigning(const PDFCertificateInfo& info);

    /// Sets the state of the row for the usage (Locked and Expired stay; NoEncryptionUsage /
    /// NoSigningUsage by the certificate; a row of the system store by the same rules - Q6.2, and for
    /// Encryption the addendum of the owner of 15. 11.: a recipient by its public certificate only)
    static void applyUsage(PDFAppToolCertificateItem* item, Usage usage, const QDateTime& now);

    /// Opens the .pfx file of the row by its password: the entry with its PKCS#12 and its
    /// certificate (the cache gets the public certificate); std::nullopt and "Invalid certificate or
    /// password." (433:3972) for a wrong password or a damaged file
    /// \param filePath The file of the row
    /// \param password The password of the file (UTF-8, B84; never logged or stored)
    /// \param errorMessage Translated reason of a failure (may be null)
    std::optional<PDFCertificateEntry> unlock(const QString& filePath, const QString& password, QString* errorMessage) const;

    /// The entry of the .pfx file of the path as the core lists it (its PKCS#12, not unlocked), or
    /// of the certificate of the system store of the identifier getSystemStorePath (its info, no
    /// PKCS#12 - PDFSignatureFactory::unlock signs by the store); std::nullopt = no such certificate
    std::optional<PDFCertificateEntry> findEntry(const QString& filePath) const;

    /// Copies the .pfx file of an old folder into the folder of the application (owner answer
    /// Q5.5, PDFCertificateManager::copyCertificateToUserDirectory); the path of the copy, empty
    /// and the reason on a failure
    QString copyToApplicationFolder(const QString& filePath, QString* errorMessage) const;

    /// Stores the public certificate of a .pfx file into the certificate cache (Q5.11: at the
    /// creation, the import, the first unlock); nothing without a database or for other data
    void storeCertificate(const QString& filePath, const QByteArray& certificateData) const;

    /// The row of an entry of the core for the usage: \p info is its certificate (the entry, the
    /// cache or the unlock), std::nullopt = not known (Locked); \p now decides Expired
    static PDFAppToolCertificateItem createItem(const PDFCertificateEntry& entry, const std::optional<PDFCertificateInfo>& info, const QDateTime& now, Usage usage = Usage::Encryption);

    /// The text of the second line of a row (433:3049: "Acme Corp · jane.novak@example.com · valid
    /// until Sep 12, 2029 · password protected", "Expired on Jan 15, 2025 – cannot be used")
    static QString getDescription(const PDFAppToolCertificateItem& item);

    /// The certificate of the .pfx file of a path, which the cache knows for the current size and
    /// time of change of the file (a replaced file is Locked again); std::nullopt = not known
    std::optional<PDFCertificateInfo> findCachedCertificate(const QString& filePath) const;

    /// The certificates of the system store are listed (default; Q5.4: only those, which could
    /// encrypt - an RSA key with keyEncipherment -, Disabled with their reason). Tests turn it off,
    /// so that the rows do not depend on the certificates of the computer. Thread-safe (the run of
    /// protect.certificates reads it); set only in the GUI thread.
    static bool isSystemStoreIncluded();
    static void setSystemStoreIncluded(bool isIncluded);

private:
    static std::atomic<bool>& getSystemStoreIncluded();

    /// The rows of the entries (getCertificates, getManagedCertificates): \p isEverySystemCertificate
    /// lists every certificate of the system store, else only those of the usage
    std::vector<PDFAppToolCertificateItem> createItems(Usage usage, bool isEverySystemCertificate) const;

    QPointer<PDFAppContext> m_context;
};

/// The list of the certificates of Encrypt with Certificate (433:3049 card-certificates: the card
/// with the caption Certificate and the rows; K5.9 - the rows of the frame, not the component
/// Certificate row; package P3). A row is a PDFTileRow of the kind ListItem (the painter of
/// PDFTileRowDelegate): the tile pfx-file / monitor, the title, the tag ".pfx file" / "System
/// store", the description and ✓ of the chosen row (bg/accent-subtle, the tile Accent; the other
/// rows Muted). The rows are widgets, so the card has the height of all of them inside of the
/// scrolled form (the checklist of Remove Hidden Data does the same). A row, which cannot be
/// chosen (Expired, NoEncryptionUsage, NoSigningUsage), is not clickable (no focus, no hover) and
/// shows its reason (the expired row in accent/red/icon, 433:3220).
///
/// Ownership: a child of the canvas of the GUI or of the confirmation of MCP, held there by
/// QPointer; it owns its card and rows. Rows replaced by setCertificates are hidden and deleted
/// later (a row may be the sender of the click, which leads to it).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCertificateList : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppToolCertificateList(QWidget* parent);
    virtual ~PDFAppToolCertificateList() override;

    /// Shows the rows; the chosen file stays chosen, when it is among them and can be chosen
    /// (else none is chosen)
    void setCertificates(std::vector<PDFAppToolCertificateItem> certificates);
    const std::vector<PDFAppToolCertificateItem>& getCertificates() const { return m_certificates; }

    /// Chooses the row of the file (nothing for a Disabled row or an unknown file); emits nothing
    void choose(const QString& filePath);

    /// The file and the fingerprint of the chosen row (empty = none; the fingerprint is empty for a
    /// Locked row until it is unlocked)
    QString getChosenFilePath() const;
    QString getChosenFingerprint() const;

    /// The chosen row, nullptr = none
    const PDFAppToolCertificateItem* getChosenItem() const;

    /// The row of the index of getCertificates (tests), nullptr = none
    PDFTileRow* getRow(size_t index) const;

    /// The caption of the card ("Certificate"; the confirmation of MCP may name it otherwise)
    void setCaption(const QString& caption);

    using Usage = PDFAppToolCertificateUsage;

    /// What the certificates are for (default Encryption; Sign with Certificate sets Signing - plan of
    /// step 6 of phase 2, 10.3). The list makes its rows for the usage: setCertificates and a change
    /// of the usage set the state of every row (PDFAppToolCertificateProvider::applyUsage), the rows,
    /// which can be chosen, first; a chosen row, which cannot be chosen for the usage, is dropped.
    /// Emits nothing.
    void setUsage(Usage usage);
    Usage getUsage() const { return m_usage; }

Q_SIGNALS:
    /// The user has chosen a row (the last call of the click; plan 10.3)
    void certificateChosen(const QString& fingerprint);

private:
    void applyUsage();
    void rebuildRows();
    void updateRows();
    void onRowClicked();

    std::vector<PDFAppToolCertificateItem> m_certificates;
    QString m_chosenFilePath;
    Usage m_usage = Usage::Encryption;
    QPointer<PDFAppToolCard> m_card;
    QPointer<PDFTileRow> m_caption;
    std::vector<QPointer<PDFTileRow>> m_rows;
};

/// The part of the preset Encrypt with Certificate of PDFAppToolGUIEncryption (Figma 433:3049 ...
/// 433:4721, plan 4.5 GUI, K5.9 - K5.12, R5-21; package P3 implements it, the GUI of P2 places it):
/// the list of the certificates or the empty state 433:3282, the card of the password of the .pfx
/// file (PDFAppToolPasswordCard::Kind::Certificate of P2), the callouts of the preset (a certificate,
/// which expired or cannot encrypt, R5-21 "You will not be able to change or remove this protection
/// yourself"), the buttons Create Certificate… and Import… of the toolbar with their dialogs. The GUI
/// of P2 builds the rest of the preset (the bars, the summaries, the card of the permissions) and asks
/// this part for its state and for the values of the run. The part belongs to the GUI (its child
/// object); the widgets it creates belong to their parents, the part holds them by QPointer.
///
/// The content of the canvas (createCanvasContent, the form of 433:3182 above the card of the
/// permissions): the heading "Encrypt for your digital ID" (433:3183), the card of the list
/// (433:3186) or the empty state (433:3422, with Create Certificate… and Import .pfx…), the offer to
/// copy a .pfx file of an old folder (Q5.5), the card "Certificate password" (433:3221: its frame,
/// the title, the label "Password of <file>" and the bare field of
/// PDFAppToolPasswordCard::Kind::Certificate) and the callout of R5-21. The GUI hides its card of
/// the permissions while hasCertificates() is false (433:3282).
///
/// The dialogs are opened by new + QPointer over the window of the content (they delete
/// themselves); a created or imported file is chosen when its dialog reports it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCertificatePresetPart : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppToolCertificatePresetPart(PDFAppContext* context, QObject* parent);
    virtual ~PDFAppToolCertificatePresetPart() override;

    /// The content of the canvas above the card of the permissions (the heading, the list, the
    /// password, the callouts); a second call replaces the parts of the first one
    QWidget* createCanvasContent(QWidget* parent);

    /// A divider, Create Certificate… and Import… of the toolbar (433:3069: after the menu Encrypt)
    void describeToolbar(PDFAppToolPanelBuilder& builder);

    /// A certificate is chosen, can encrypt and its password is typed (a file without a password
    /// needs none)
    bool isConfirmEnabled() const;

    /// The values of a run: unlocks the chosen file by its password (PDFAppToolCertificateProvider::
    /// unlock), adds the parameters certificate (the fingerprint) and certificatePassword to
    /// \p parameters and the entry to \p certificates (PDFAppToolRunEnvironment::userCertificates);
    /// false and the reason ("Invalid certificate or password.", 433:3972) on a failure, which the
    /// field shows too. Emits nothing directly: stateChanged follows from the event loop.
    bool prepareRun(QVariantMap* parameters, std::vector<PDFCertificateEntry>* certificates, QString* errorMessage);

    /// Empties the password of the .pfx file (S5.1: after the run, Cancel, the close of the tool)
    void clearSecrets();

    /// The list has a certificate (else the empty state 433:3282 is shown; the GUI hides its card
    /// of the permissions)
    bool hasCertificates() const;

    /// The chosen certificate (the summaries "Opens for", the Strength meter of the key - R5-20),
    /// std::nullopt = none
    std::optional<PDFAppToolCertificateItem> getChosenCertificate() const;

    /// The status message of the preset (433:3282 "Create or import a certificate with a private key
    /// first", 433:3742 "Enter the password of the certificate file", 433:3972 "Invalid certificate
    /// or password", 433:3049 "Only the holder of the private key of “Jane Novak” will be able to
    /// open the document")
    QString getStatusText() const;

    /// The permissions of the card of the GUI are restricted (not "Everything is allowed"): the
    /// callout of R5-21 "You will not be able to change or remove this protection yourself"
    /// (owner answer Q5.3 b: without bit 2 the holder cannot change the encryption)
    void setPermissionsRestricted(bool isRestricted);

    /// Reads the list again (a file created, imported or removed outside); the choice stays
    void refresh();

    /// Chooses the row of the file (as a click; emits stateChanged)
    void choose(const QString& filePath);

    /// Opens Create a self-signed certificate (436:10081) / the file dialog and the import of a
    /// .pfx file (436:11538); nothing in portable mode (isCreationAvailable)
    void openCreateDialog();
    void openImportDialog();

    /// A certificate may be created or imported: not in portable mode (--portable, owner 2026-11-15)
    /// - its private key would be written on a computer, which need not be safe; a .pfx file, which
    /// is in the folder already, can still be used. Every entry (the toolbar, the empty state) is
    /// disabled with getPortableText.
    bool isCreationAvailable() const;

    /// Why Create Certificate… and Import… are disabled in portable mode (their tooltip)
    static QString getPortableText();

    /// The card of an empty state without certificates (433:3422, 436:9646): the tile with the icon
    /// certificate, the title, the text centered, Create Certificate… (Primary) and Import .pfx…
    /// Disabled with getPortableText, when \p isCreationAvailable is false. The caller connects the
    /// buttons (object names certificateEmptyCreate, certificateEmptyImport).
    static PDFAppToolCard* createEmptyState(const QString& title, const QString& text, bool isCreationAvailable, QWidget* parent, QPushButton** createButton, QPushButton** importButton);

    /// Tests only: the fixtures of the application start with --portable (their database lives in
    /// the memory) and their certificate folders are temporary folders of the test
    /// (AppTestCertificateFolders) - creating and importing is allowed there as after a normal start.
    /// Off by default. GUI thread.
    static void setCreationInPortableModeAllowed(bool isAllowed);

    /// Parts (tests; nullptr = none)
    QWidget* getContent() const { return m_content.data(); }
    PDFAppToolCertificateList* getList() const { return m_list.data(); }
    QWidget* getEmptyState() const { return m_emptyState.data(); }
    QWidget* getPasswordFrame() const { return m_passwordFrame.data(); }
    PDFAppToolPasswordCard* getPasswordCard() const;
    PDFLabel* getPasswordLabel() const;
    PDFAppToolCallout* getCopyCallout() const;
    PDFAppToolCallout* getRestrictionCallout() const;
    QToolButton* getCreateButton() const;
    QToolButton* getImportButton() const;
    PDFAppCertificateCreateDialog* getCreateDialog() const;
    PDFAppCertificateImportDialog* getImportDialog() const;

    /// The text of the field shown after a failed unlock (empty = none)
    const QString& getUnlockError() const { return m_unlockError; }

Q_SIGNALS:
    /// The choice, the password or the list has changed (the last call of the change)
    void stateChanged();

private:
    static bool& getCreationInPortableModeAllowed();

    void updateParts();
    QWidget* getDialogParent() const;
    void notifyStateChanged();
    void onNotifyStateChanged();
    void onCertificateChosen();
    void onPasswordChanged();
    void onCertificateCreated(const QString& fileName);
    void onCertificateImported(const QString& fileName);
    void onCopyClicked();

    /// Reads the list and chooses \p filePath (empty = the current choice, else the most recently
    /// used certificate of the cache, else the first row, which can be chosen)
    void reload(const QString& filePath);

    PDFAppToolCertificateProvider m_provider;
    QPointer<PDFAppContext> m_context;
    QPointer<QWidget> m_content;
    QPointer<PDFAppToolCertificateList> m_list;
    QPointer<QWidget> m_emptyState;
    QPointer<QWidget> m_passwordFrame;
    QPointer<PDFLabel> m_passwordLabel;
    QPointer<QWidget> m_passwordField;
    QPointer<PDFAppParagraph> m_noPasswordText;
    QPointer<PDFAppToolPasswordCard> m_passwordCard;
    QPointer<PDFAppToolCallout> m_copyCallout;
    QPointer<QPushButton> m_copyButton;
    QPointer<PDFAppToolCallout> m_restrictionCallout;
    QPointer<QToolButton> m_createButton;
    QPointer<QToolButton> m_importButton;
    QPointer<PDFAppCertificateCreateDialog> m_createDialog;
    QPointer<PDFAppCertificateImportDialog> m_importDialog;
    std::vector<PDFAppToolCertificateItem> m_certificates;
    QString m_unlockError;
    QString m_copyError;
    bool m_isRestricted = false;
    bool m_isNotifyPending = false;
};

}   // namespace pdf

#endif // PDFAPPTOOLCERTIFICATELIST_H
