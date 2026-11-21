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

#ifndef PDFAPPTOOLGUI_CERTIFICATES_H
#define PDFAPPTOOLGUI_CERTIFICATES_H

#include "pdfappcertificatedetails.h"
#include "pdfappcertificatetable.h"
#include "pdfappdeclarativetoolgui.h"
#include "pdfapptoolcertificatelist.h"
#include "pdfapptoolpanelparts.h"

#include "pdfcomponentrows.h"
#include "pdfdocumenttool.h"
#include "pdffluentwidgets.h"

#include <QFutureWatcher>
#include <QPointer>
#include <QSet>
#include <QToolButton>

#include <optional>
#include <vector>

class QPushButton;
class QToolButton;

namespace pdf
{

class PDFAppCertificateCreateDialog;
class PDFAppCertificateDetails;
class PDFAppCertificateImportDialog;
class PDFAppMainWindow;
class PDFAppParagraph;
class PDFAppToolCallout;
class PDFAppToolCanvasPage;
class PDFAppToolCard;
class PDFAppToolPanelHeader;
class PDFAppToolTab;
class PDFAppToolbarDivider;
class PDFDatabase;
class PDFMessageBox;
class PDFTileRow;
class PDFTreeView;

/// The core tool of protect.certificates (plan of step 6 of phase 2, 6.5, K6.11, Q6.7; the application
/// defines it, because it reads the certificates of the application): an Analysis, which lists the
/// certificates of the user and the trusted certificates - read only. It writes nothing, it changes no
/// trust and it never gives a secret, a path of a file or a folder (MCP of package P6 wraps it).
///
/// The result data (PDFDocumentToolResult::data): "certificates" - every .pfx file of the certificate
/// folder of the application and of the old folders, then the personal certificates of the system store
/// (PDFAppToolCertificateProvider::getManagedCertificates; the run has no database: a file with a
/// password has the details of the certificate cache, which the caller read in the GUI thread into
/// PDFDocumentToolContext::userCertificates - createCachedCertificates -, a file never opened by its
/// password is "locked" without details): source ("file" / "systemStore"), readOnly,
/// state ("valid", "expired", "notYetValid", "locked"), and for a known certificate name, organization,
/// email, issuer, validFrom, validTo (ISO 8601 UTC), fingerprint (SHA-256), keyAlgorithm, keyBits,
/// usableFor ("signing", "encryption"); "trusted" - the trusted certificates of
/// PDFDocumentToolContext::certificateStore (the caller gives them, createTrustedStore): type ("user" /
/// "system"), name, organization, validFrom, validTo, fingerprint. The facts count them.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentToolCertificates : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppDocumentToolCertificates)

public:
    static constexpr const char* TOOL_ID = "protect.certificates";

    /// The most rows of each list of the result (a hostile folder or store)
    static constexpr int MAXIMUM_ROWS = 1000;

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

    /// The trusted certificates of the user of the database (table trusted_certificate, the rows of
    /// PDFTrustedCertificateRepository::getCertificates as PDFCertificateEntry::EntryType::User) for
    /// PDFDocumentToolContext::certificateStore of a run; null without a database
    static std::shared_ptr<const PDFCertificateStore> createTrustedStore(PDFDatabase* database);

    /// The trusted certificates of a verification by the application for PDFDocumentToolContext::certificateStore
    /// (the authority of Sign with Certificate and Add Timestamp, DATA_TSA_TRUSTED): those of the user
    /// (createTrustedStore) and, with the setting signatures.useSystemStore, the root certificates of the system
    /// store (PDFAppToolGUICertificates::getSystemCertificatesProvider) - the trust of the Signatures panel
    /// (PDFAppDocumentSignatureVerifier); the core adds the Adobe Approved Trust List itself. GUI thread (the
    /// database); null without a database.
    static std::shared_ptr<const PDFCertificateStore> createVerificationStore(PDFAppContext* context);

    /// The public certificates of the .pfx files with a password, which the certificate cache of the
    /// application knows for the current file (owner answer Q5.11: stored at the creation, the import, the
    /// first unlock; PDFAppToolCertificateProvider::findCachedCertificate - a replaced file is not
    /// known), for PDFDocumentToolContext::userCertificates of a run of this tool: each entry has only
    /// pkcs12FilePath, isReadOnlyLocation and info - never its PKCS#12, a password or a private key.
    /// GUI thread (the database); at most MAXIMUM_ROWS entries.
    static std::vector<PDFCertificateEntry> createCachedCertificates(PDFAppContext* context);
};

/// The GUI of Manage Certificates in its own tab Certificates (plan of step 6 of phase 2, 2.4, 4.5,
/// K6.11, K6.13, K6.14, K6.17, Q6.12, Q6.13, R6-13; Figma 436:9398 ... 436:12324).
///
/// Left: "Certificate store / On this computer", the views My certificates / Trusted certificates
/// with their counts, the blue callout of the view ("Where the files are", "Who do you trust?").
/// Canvas: My certificates - the card "Certificate files" (the .pfx files of the folder of the
/// application and, Read-only, of the old Editor and Viewer with the offer of their copy, Q5.5; a
/// created or imported file first with the tag New) and the card "From the system store" (Read-only;
/// no card without such certificates - Linux, macOS), or the empty state 436:9646; Trusted
/// certificates - the table of PDFAppCertificateTableModel of Settings › Security (one model class,
/// the rows of PDFTrustedCertificateRepository and of the system store; a change of one place is shown
/// at the other by the notification of the database) and the note below it. Right: the detail of the
/// chosen certificate (PDFAppCertificateDetails). Toolbar: Create…, Import… | Delete, Open Folder (My
/// certificates); Add Trusted…, Remove (Trusted certificates); Tutorial; Close - no Primary button
/// (an analysis, K6.11).
///
/// Delete removes only a .pfx file of the certificate folder of the application (QFile::remove and its
/// row of the certificate cache) after the confirmation 436:11809 (Cancel Primary, Delete Certificate
/// Secondary - R6-13); a file of an old folder and a certificate of the system store are never
/// deleted. Add Trusted… adds a DER or PEM certificate of a file of the file dialog to the repository
/// (a duplicate or a file, which is no certificate, is refused with its message); Remove asks "Stop
/// trusting …?" (436:12324) and removes a row of the user (a system row cannot be removed). Portable
/// mode (Q6.13): Create, Import and Delete Disabled with PDFAppToolCertificatePresetPart::
/// getPortableText; Open Folder and the trusted certificates stay. Q6.12: TrustedCertStorage.bin of
/// the old applications is never read.
///
/// Ownership: the shell owns this GUI; the parts are children of the widgets of the shell (held by
/// QPointer). The dialogs are new + QPointer over the window (they delete themselves) and close with
/// this GUI (their signals are disconnected first). The watcher of the system certificates is a child
/// of this GUI (its result is dropped with it).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUICertificates : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    /// The view of the store (card-views 436:9525)
    enum class View
    {
        My,
        Trusted
    };

    /// Largest file of Add Trusted… (a PEM of the largest certificate of the repository)
    static constexpr qint64 MAXIMUM_TRUSTED_FILE_SIZE = 4 * PDFTrustedCertificateRepository::MAXIMUM_CERTIFICATE_SIZE;

    explicit PDFAppToolGUICertificates(QObject* parent = nullptr);
    virtual ~PDFAppToolGUICertificates() override;

    /// Registers what is not registered yet (PDFAppBuiltinTools::registerAll): the core tool of
    /// protect.certificates (PDFAppDocumentToolCertificates) and this GUI
    static void registerCertificatesTools();

    /// The own tab of Manage Certificates of the window, nullptr = none (one instance of the tab: a
    /// second start activates it - isSingleOwnTab, the rule of PDFAppMainWindow::createToolTab)
    static PDFAppToolTab* findCertificatesTab(const PDFAppMainWindow* window);

    /// Reads a certificate of Add Trusted… (DER or the first CERTIFICATE block of PEM, RFC 7468): its
    /// DER, empty for data, which are no certificate
    static QByteArray readCertificateFile(const QByteArray& data);

    /// The system certificates of the trusted view and of the trusted certificates of a run
    /// (PDFAppDocumentToolCertificates::createVerificationStore; tests; nullptr = the store of the system)
    static void setSystemCertificatesProvider(PDFAppSystemCertificatesTask::Provider provider);
    static PDFAppSystemCertificatesTask::Provider getSystemCertificatesProvider() { return s_systemProvider; }

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QString getConfirmText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual bool isConfirmShown() const override;

    /// The session is the use (owner 2026-11-20): a view, a selected certificate, a copy, or 10 s shown
    virtual bool isInteractive() const override { return true; }
    virtual bool isUndoShown() const override;
    virtual bool isSingleOwnTab() const override;
    virtual QString getStatusText() const override;
    virtual QString getOwnTabTitle() const override;
    virtual QString getOwnTabIconName() const override;
    virtual QString getCancelText() const override;
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const override;
    virtual void retranslate() override;

    View getView() const { return m_view; }
    void setView(View view);

    /// Chooses the row of My certificates (its filePath, PDFAppToolCertificateItem) / of the trusted
    /// table (its index); emits nothing but the status
    void chooseCertificate(const QString& filePath);
    void chooseTrustedRow(int row);

    /// The rows of My certificates in the order of the canvas (new files first)
    const std::vector<PDFAppToolCertificateItem>& getCertificates() const { return m_certificates; }
    const PDFAppToolCertificateItem* getChosenCertificate() const;
    const PDFAppCertificateEntry* getChosenTrustedEntry() const;

    /// Reads the certificates and the trusted rows again (the database has changed, a file)
    void reload();

    /// The system certificates of the trusted view are being read
    bool isLoadingSystemCertificates() const { return m_systemWatcher != nullptr; }

    /// Actions (as their buttons)
    void openCreateDialog();
    void openImportDialog();
    void openDeleteDialog();
    void openFolder();
    void addTrusted();
    void openRemoveTrustedDialog();

    /// Parts (tests; nullptr = none)
    QToolButton* getCreateButton() const { return m_createButton.data(); }
    QToolButton* getImportButton() const { return m_importButton.data(); }
    QToolButton* getDeleteButton() const { return m_deleteButton.data(); }
    QToolButton* getFolderButton() const { return m_folderButton.data(); }
    QToolButton* getAddTrustedButton() const { return m_addTrustedButton.data(); }
    QToolButton* getRemoveTrustedButton() const { return m_removeTrustedButton.data(); }
    QWidget* getActionsDivider() const;
    PDFAppToolPanelHeader* getStoreHeader() const { return m_storeHeader.data(); }
    PDFTileRow* getViewRow(View view) const;
    PDFAppToolCallout* getViewCallout() const { return m_viewCallout.data(); }
    QWidget* getMyPage() const { return m_myPage.data(); }
    QWidget* getTrustedPage() const { return m_trustedPage.data(); }
    PDFAppToolCard* getFilesCard() const { return m_filesCard.data(); }
    PDFAppToolCard* getSystemCard() const { return m_systemCard.data(); }
    QWidget* getEmptyState() const { return m_emptyState.data(); }
    PDFAppToolCallout* getCopyCallout() const { return m_copyCallout.data(); }
    PDFTileRow* getCertificateRow(const QString& filePath) const;
    PDFTreeView* getTrustedView() const { return m_trustedView.data(); }
    PDFAppCertificateTableModel* getTrustedModel() const { return m_trustedModel.data(); }
    PDFAppParagraph* getTrustedNote() const { return m_trustedNote.data(); }
    PDFAppCertificateDetails* getDetails() const { return m_details.data(); }
    PDFAppCertificateCreateDialog* getCreateDialog() const;
    PDFAppCertificateImportDialog* getImportDialog() const;
    PDFMessageBox* getDeleteDialog() const;
    PDFMessageBox* getRemoveTrustedDialog() const;
    PDFMessageBox* getAddTrustedDialog() const;

    /// The description of a row of My certificates (436:9398: "jane_novak.pfx · Acme Corp · RSA
    /// 2048-bit · valid until Sep 12, 2029", "cert_1.pfx · expired on Jan 15, 2025", "Acme Corp ·
    /// issued by Acme Issuing CA · valid until Mar 3, 2027")
    static QString getRowDescription(const PDFAppToolCertificateItem& item);

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

private:
    void buildCanvas();
    QWidget* createMyPage(QWidget* parent);
    QWidget* createTrustedPage(QWidget* parent);
    void rebuildRows();
    void updateRows();
    void updateParts();
    void updateDetails();
    void reloadCertificates(const QString& choice);
    void reloadTrusted();
    void reloadSystemTrusted();
    void updateTrustedModel();
    void setStatusMessage(const QString& message);
    bool isCreationAvailable() const;
    bool isDeletable(const PDFAppToolCertificateItem* item, QString* reason) const;
    QWidget* getDialogParent() const;
    void closeDialogs();
    void showAddTrustedProblem(const QString& title, const QString& text);

    void onViewRowClicked();
    void onCertificateRowClicked();
    void onTrustedCurrentChanged();
    void onCertificateCreated(const QString& fileName);
    void onCertificateImported(const QString& fileName);
    void onDeleteConfirmed();
    void onRemoveTrustedConfirmed();
    void onAddTrustedChooseAgain();
    void onCopyClicked();
    void onSystemCertificatesLoaded();
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onEverythingChanged();

    static PDFAppSystemCertificatesTask::Provider s_systemProvider;

    View m_view = View::My;
    bool m_isLoaded = false;                        ///< The certificates were read at the entry
    std::vector<PDFAppToolCertificateItem> m_certificates;
    QString m_chosenPath;
    QSet<QString> m_newFiles;                       ///< Created or imported in this tab (the tag New, 436:11280)
    QString m_statusMessage;                        ///< The message of the last action (436:11280), empty = the counts
    QString m_pendingDeletePath;                    ///< The file of the open confirmation of Delete
    std::vector<PDFAppCertificateEntry> m_userEntries;
    std::vector<PDFAppCertificateEntry> m_systemEntries;
    std::optional<PDFTrustedCertificate> m_pendingRemoval;  ///< The row of the open confirmation of Remove

    QPointer<QToolButton> m_createButton;
    QPointer<QToolButton> m_importButton;
    QPointer<PDFAppToolbarDivider> m_actionsDivider;
    QPointer<QToolButton> m_deleteButton;
    QPointer<QToolButton> m_folderButton;
    QPointer<QToolButton> m_addTrustedButton;
    QPointer<QToolButton> m_removeTrustedButton;
    QPointer<PDFAppToolPanelHeader> m_storeHeader;
    QPointer<PDFTileRow> m_myRow;
    QPointer<PDFTileRow> m_trustedRow;
    QPointer<PDFAppToolCallout> m_viewCallout;
    QPointer<PDFAppToolCanvasPage> m_canvasPage;
    QPointer<QWidget> m_myPage;
    QPointer<QWidget> m_trustedPage;
    QPointer<PDFAppToolCard> m_filesCard;
    QPointer<PDFAppToolCard> m_systemCard;
    QPointer<QWidget> m_emptyState;
    QPointer<QPushButton> m_emptyCreateButton;
    QPointer<QPushButton> m_emptyImportButton;
    QPointer<PDFAppToolCallout> m_copyCallout;
    std::vector<QPointer<PDFTileRow>> m_rows;       ///< The rows of m_certificates in their order
    QPointer<PDFTreeView> m_trustedView;
    QPointer<PDFAppCertificateTableModel> m_trustedModel;
    QPointer<PDFAppParagraph> m_trustedNote;
    QPointer<PDFAppCertificateDetails> m_details;
    QPointer<QFutureWatcher<PDFAppSystemCertificates>> m_systemWatcher;
    QPointer<PDFAppCertificateCreateDialog> m_createDialog;
    QPointer<PDFAppCertificateImportDialog> m_importDialog;
    QPointer<PDFMessageBox> m_deleteDialog;
    QPointer<PDFMessageBox> m_removeTrustedDialog;
    QPointer<PDFMessageBox> m_addTrustedDialog;
    QString m_copyError;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_CERTIFICATES_H
