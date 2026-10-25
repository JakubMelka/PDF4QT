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

#ifndef PDFAPPSETTINGSSECTION_STORAGE_H
#define PDFAPPSETTINGSSECTION_STORAGE_H

#include "pdfappglobal.h"
#include "pdfappsettingssection.h"
#include "pdfapphomedialogs.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingrow.h"
#include "pdfappdocumentinfobar.h"
#include "pdfdatabase.h"

#include "pdfcomponentpanels.h"

#include "pdfthemecolors.h"

#include <QCheckBox>
#include <QPointer>
#include <QPushButton>

#include <optional>
#include <vector>

namespace pdf
{

class PDFAppCard;
class PDFAppInfoBar;
class PDFAppParagraph;
class PDFAppSettingGroup;
class PDFAppSettingRow;
class PDFKeyValueList;

/// Value of the row State (1269:14762): the icon 16 in the accent color of the
/// state (check-circle accent/green/icon, portable accent/blue/icon, ...) and the
/// text Regular 13 text/primary, gap 6; painted at paint time from the theme. Not
/// interactive, announced as its text.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppStorageStateValue : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int ICON_SIZE = 16;
    static constexpr int SPACING = 6;

    explicit PDFAppStorageStateValue(QWidget* parent = nullptr);
    virtual ~PDFAppStorageStateValue() override;

    void setState(const QString& iconName, PDFThemeColors::Color iconColor, const QString& text);

    const QString& getIconName() const { return m_iconName; }
    PDFThemeColors::Color getIconColor() const { return m_iconColor; }
    const QString& getText() const { return m_text; }

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    QString m_iconName;
    PDFThemeColors::Color m_iconColor = PDFThemeColors::AccentGreenIcon;
    QString m_text;
};

/// Dialog of the section Storage (1271:15367 delete, 1271:15532 import, 1271:15697
/// import error): PDFAppCardDialog 520 with the facts (Key value rows on bg/subtle)
/// and the buttons of the design - a destructive action (Secondary, leading) with
/// Cancel (Primary), or one OK (Primary)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppStorageDialog : public PDFAppCardDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCardDialog;

public:
    /// Width of the card (1271:15367)
    static constexpr int CARD_WIDTH = 520;

    /// Fact of the dialog: key, value and the elision of the value
    struct Fact
    {
        QString key;
        QString value;
        bool isFileName = false;
    };

    /// \p actionText empty = the dialog of an error with one OK
    explicit PDFAppStorageDialog(PDFThemeIcons::Badge badge, const QString& title, const QString& text, const std::vector<Fact>& facts,
                                 const QString& actionText, QWidget* parent);
    virtual ~PDFAppStorageDialog() override;

    PDFKeyValueList* getFacts() const { return m_facts; }

    /// Adds a block of facts below the content (Key value rows on bg/subtle as the
    /// facts of the constructor) with an optional heading above it (Semi Bold 13).
    /// \returns the list (object name \p objectName)
    PDFKeyValueList* addFacts(const QString& objectName, const QString& heading, const std::vector<Fact>& facts);

    /// Adds the check box of the library below the content (one per dialog) with
    /// an optional note below it (Regular 12 text/muted)
    QCheckBox* addCheckBox(const QString& objectName, const QString& text, const QString& note);

    /// The check box of addCheckBox (nullptr without it) and its note
    QCheckBox* getCheckBox() const { return m_checkBox; }
    PDFAppParagraph* getCheckBoxNote() const { return m_checkBoxNote; }

    /// Title and text given to the constructor
    const QString& getTitleText() const { return m_titleText; }
    const QString& getMessageText() const { return m_messageText; }

    /// Delete File / Import Settings (nullptr in an error dialog)
    QPushButton* getActionButton() const { return m_actionButton; }

    /// Cancel, or OK of an error dialog
    QPushButton* getCancelButton() const { return m_cancelButton; }

private:
    QString m_titleText;
    QString m_messageText;
    QPointer<PDFKeyValueList> m_facts;
    QPointer<QPushButton> m_actionButton;
    QPointer<QPushButton> m_cancelButton;
    QPointer<QCheckBox> m_checkBox;
    QPointer<PDFAppParagraph> m_checkBoxNote;
};

/// Section Storage (owner decision Q1, frames 1269:14388 / -empty 1270:14617 /
/// -portable 1270:14909 / -exported 1271:14929, note 1271:167948):
///  - Settings database: Location (the path of the database file, data text elided
///    in the middle, Show in Folder) and State (the mode of the storage as the
///    storage banners 958:28036 tell it; no second Try Again - the banner has it),
///  - Backup: Export settings… (PDFDatabase::exportBackup, the info bar of
///    1271:15212 with Show in Folder below the card) and Import settings…
///    (PDFDatabaseBackup::read, the confirmation 1271:15395, PDFDatabase::importBackup;
///    errors 1271:15560),
///  - Other database files: the files pdf4qt-<n>.db of the folder of other versions
///    (labeled by the version of PDF4QT, owner 2026-10-25), Delete… of an older one
///    (confirmation 1271:15230, PDFDatabase::deleteOlderDatabaseFile), a newer one with
///    the tag Newer version; "No other database files". Hidden in the portable mode.
/// No Restore defaults. The section follows the state of the database (Try Again of
/// a banner) and lists the files again, when it is shown.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppStorageSettingsSection : public PDFAppGenericSettingsSection
{
    Q_OBJECT

private:
    using BaseClass = PDFAppGenericSettingsSection;

public:
    /// Group keys
    static constexpr const char* DATABASE_GROUP_KEY = "Settings database";
    static constexpr const char* BACKUP_GROUP_KEY = "Backup";
    static constexpr const char* FILES_GROUP_KEY = "Other database files";

    /// Prefix of the anchors of the rows of the files ("file-1" = pdf4qt-1.db)
    static constexpr const char* FILE_ANCHOR_PREFIX = "file-";

    /// Id of the info bar of an export
    static constexpr const char* EXPORT_INFO_BAR_ID = "settings-exported";

    explicit PDFAppStorageSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent = nullptr);
    virtual ~PDFAppStorageSettingsSection() override;

    /// Storage has no Restore defaults (its rows are files, not settings)
    virtual bool isRestoreSupported() const override;
    virtual int applyFilter(const PDFSearchQuery& query) override;
    virtual void refresh(const QString& topic, const QString& key) override;

    /// Parts (tests)
    PDFAppSettingRow* getLocationRow() const { return m_locationRow; }
    PDFAppSettingRow* getStateRow() const { return m_stateRow; }
    PDFAppSettingRow* getExportRow() const { return m_exportRow; }
    PDFAppSettingRow* getImportRow() const { return m_importRow; }
    QPushButton* getShowInFolderButton() const { return m_showInFolderButton; }
    QPushButton* getExportButton() const { return m_exportButton; }
    QPushButton* getImportButton() const { return m_importButton; }
    PDFAppStorageStateValue* getStateValue() const { return m_stateValue; }
    PDFAppSettingGroup* getFilesGroup() const { return m_filesGroup; }
    PDFAppCard* getEmptyFilesCard() const { return m_emptyFilesCard; }
    PDFAppInfoBar* getExportInfoBar() const { return m_exportInfoBar; }
    PDFAppStorageDialog* getDialog() const { return m_dialog; }

    /// Rows of the other database files in their order
    QList<PDFAppSettingRow*> getFileRows() const;

    /// Full path of the database file shown by Location (empty in the portable mode)
    const QString& getLocationPath() const { return m_locationPath; }

    /// Export settings…: the file dialog, then the export (the info bar or an error)
    void exportSettings();

    /// Import settings…: the file dialog, the check of the file, the confirmation
    void importSettings();

    /// Delete… of the row of the file: the confirmation, then the deletion
    void requestDelete(const QString& filePath);

    /// Lists the files of the folder again
    void updateFiles();

    /// Shows the state of the storage again
    void updateState();

    /// Label of a database file of another version ("PDF4QT 2.0 · 2.4 MB · Oct 12, 2026")
    static QString getFileDescription(const PDFDatabaseFileInfo& file);

    /// Folder of the Export / Import file dialogs: the folder of the last used backup
    /// (state setting storage.backupFolder, owner 2026-10-25); an invalid, a device or
    /// a network path (PDFAppNetworkPaths - never touched) or a missing folder falls
    /// back to Documents
    static QString getBackupFolder(PDFAppContext* context);

    /// Most new certificates of a backup listed by the import confirmation
    static constexpr int MAXIMUM_LISTED_CERTIFICATES = 3;

    /// Certificates of the backup, which are certificates and which the database
    /// does not trust (owner 2026-10-25, Q7), in the order of the backup
    static std::vector<QByteArray> getNewCertificates(PDFDatabase* database, const PDFDatabaseBackup& backup);

    /// Security and signature settings, which the import of the backup weakens, as
    /// facts "title: old → new" (owner 2026-10-25, Q7). Weaker = web links or program
    /// launches allowed, where they were not; signature verification or strict mode
    /// turned off; expired certificates ignored; the system certificate store trusted
    /// (more trusted roots). A value the backup does not store is its default.
    static std::vector<PDFAppStorageDialog::Fact> getWeakenedSettings(PDFAppContext* context, const PDFDatabaseBackup& backup);

protected:
    virtual void finishContent() override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;

private:
    /// What the open dialog does, when its action is accepted
    enum class DialogKind
    {
        None,
        Delete,
        Import,
        Error
    };

    PDFDatabase* getDatabase() const;
    bool hasFolder() const;
    void updateLocationText();
    void updateExtraWidgets();
    void layoutExportInfoBar();
    void showExportInfoBar(const QString& filePath);
    void hideExportInfoBar();
    void openDialog(DialogKind kind, PDFAppStorageDialog* dialog);
    void closeDialog();
    void showError(const QString& title, const QString& text, const std::vector<PDFAppStorageDialog::Fact>& facts);
    void requestShowInFolder(const QString& filePath);
    void storeBackupFolder(const QString& filePath);

    void onShowInFolderClicked();
    void onExportClicked();
    void onImportClicked();
    void onDeleteClicked();
    void onDialogFinished(int result);
    void onDatabaseStateChanged();
    void onInfoBarActionTriggered(const QString& infoBarId, const QString& actionId);
    void onInfoBarCloseRequested(const QString& infoBarId);

    QPointer<PDFAppSettingGroup> m_databaseGroup;
    QPointer<PDFAppSettingGroup> m_backupGroup;
    QPointer<PDFAppSettingGroup> m_filesGroup;
    QPointer<PDFAppSettingRow> m_locationRow;
    QPointer<PDFAppSettingRow> m_stateRow;
    QPointer<PDFAppSettingRow> m_exportRow;
    QPointer<PDFAppSettingRow> m_importRow;
    QPointer<QPushButton> m_showInFolderButton;
    QPointer<QPushButton> m_exportButton;
    QPointer<QPushButton> m_importButton;
    QPointer<PDFAppStorageStateValue> m_stateValue;
    QPointer<PDFAppCard> m_emptyFilesCard;
    QPointer<QWidget> m_exportInfoBarSlot;
    QPointer<PDFAppInfoBar> m_exportInfoBar;
    QPointer<PDFAppStorageDialog> m_dialog;
    QList<QPointer<PDFAppSettingRow>> m_fileRows;

    QString m_locationPath;
    QString m_exportedFilePath;
    QString m_query;
    DialogKind m_dialogKind = DialogKind::None;
    QString m_pendingDeletePath;
    std::optional<PDFDatabaseBackup> m_pendingBackup;
    bool m_hasNewCertificates = false;
    bool m_isElidingLocation = false;
};

/// Factory of the section
class PDF4QTLIBAPPSHARED_EXPORT PDFAppStorageSettingsSectionFactory : public PDFAppSettingsSectionFactory
{
public:
    virtual PDFAppSettingsSection* createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPSETTINGSSECTION_STORAGE_H
