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

#ifndef PDFAPPAIPAGE_H
#define PDFAPPAIPAGE_H

#include "pdfappglobal.h"
#include "pdfapphomedialogs.h"
#include "pdfappmcpclientsetup.h"
#include "pdfapppagelayout.h"
#include "pdfmcprepository.h"
#include "pdfapphomewidgets.h"
#include "pdfappworkspace.h"

#include "pdfcomponentassistants.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdffluentwidgets.h"

#include <QButtonGroup>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QToolButton>

#include <vector>

class QAbstractButton;
class QButtonGroup;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QToolButton;

namespace pdf
{

class PDFActivityRow;
class PDFAppEmptyState;
class PDFAppMcpController;
class PDFAppParagraph;
class PDFAppSettingGroup;
class PDFAppSettingRow;
class PDFAssistantRow;
class PDFAssistantTile;
class PDFCodeBox;
class PDFIconBadge;
class PDFLineEdit;
class PDFStatusBadge;
class PDFTextLink;
class PDFTintedCard;

/// Message box Create a new access token? (940:135813, the variant without
/// clients of 955:150808): the amber badge, the names of the connected
/// assistants in the text, Create New Token (Secondary) at the leading edge and
/// Cancel (Primary, the first focus) at the trailing edge. The texts follow the
/// assistants while the box is shown.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAiNewTokenDialog : public PDFAppCardDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCardDialog;

public:
    /// Width of the card 941:7795
    static constexpr int CARD_WIDTH = 440;

    explicit PDFAppAiNewTokenDialog(QWidget* parent);
    virtual ~PDFAppAiNewTokenDialog() override;

    /// Display names of the connected assistants (an empty list = the variant
    /// without clients)
    void setClientNames(const QStringList& names);
    const QStringList& getClientNames() const { return m_clientNames; }

    /// The token is required (Require access token or the remote mode). Without
    /// it the assistants connect without any token (the default of the local mode,
    /// owner 2026-10-28): the text does not tell to set them up again
    void setTokenRequired(bool isTokenRequired);
    bool isTokenRequired() const { return m_isTokenRequired; }

    /// Text of the box for the names (tests)
    static QString getBodyText(const QStringList& names, bool isTokenRequired = true);

    QPushButton* getCreateButton() const { return m_createButton; }
    QPushButton* getCancelButton() const { return m_cancelButton; }

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void updateTexts();

    QStringList m_clientNames;
    bool m_isTokenRequired = true;
    QPointer<QPushButton> m_createButton;
    QPointer<QPushButton> m_cancelButton;
};

/// Page AI /ai (Figma pdf4qt-ai 653:5231 and its states 653:5814 stopped,
/// 653:6384 remote, 940:135321 other instance, 955:148587 port in use,
/// 955:149138 setup of another client, 940:135813 new token; plan 5.1, package E
/// of step 7). The main column (802) holds the server card and the groups
/// Connection and Permissions (rows of the settings mcp.* bound by
/// PDFAppSettingControl - the permissions by the segmented control), the side
/// column (340) the connected assistants (rows mcp_client), the recent activity
/// (the 10 newest rows of mcp_activity, owner decision Q6), Learn more and the
/// privacy / remote note; Connect an assistant takes the whole width below them
/// (six tiles and the setup card with the texts of PDFAppMcpClientSetup).
///
/// Everything follows the controller of the context (its state, the
/// assistants, the activity) and the notifier (settings of another instance);
/// another PDF4QT window, which runs the server, is shown by its row mcp_server
/// and its requests go there by mcp_request (Disconnect, Disconnect All, Show
/// Other Window). The content is built again at QEvent::LanguageChange.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAiPage : public PDFAppContentPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppContentPage;

public:
    /// Rows of Recent activity (owner decision Q6)
    static constexpr int ACTIVITY_ROW_COUNT = 10;

    /// What the server card shows (the state of the controller with the remote mode)
    enum class ShownState
    {
        Stopped,        ///< 653:5814
        Starting,       ///< no frame: the stopped card with the badge Starting…
        Running,        ///< 653:5231
        Remote,         ///< 653:6384
        OtherInstance,  ///< 940:135321
        PortInUse,      ///< 955:148587
        Failed          ///< no frame: the card of the port in use with the error of the server
    };

    explicit PDFAppAiPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppAiPage() override;

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual QString getHeaderTitle() const override;
    virtual QString getHeaderSubtitle() const override;

    /// One line of the state of the server (the accessible description of the
    /// server card, tests): "The MCP server is running at <address>." …
    QString getStateText() const;

    ShownState getShownState() const { return m_shownState; }

    /// The client, whose setup card is shown
    PDFAppMcpSetupClient getSetupClient() const { return m_setupClient; }
    void setSetupClient(PDFAppMcpSetupClient client);

    /// The setup of the shown card with the whole token (what the copy buttons copy)
    PDFAppMcpSetup getSetup() const;

    /// Opens the message box Create a new access token? (false = not possible)
    bool openNewTokenDialog();
    PDFAppAiNewTokenDialog* getNewTokenDialog() const { return m_newTokenDialog; }

    // Parts (tests)
    PDFTintedCard* getServerCard() const { return m_serverCard; }
    QList<PDFStatusBadge*> getStatusBadges() const;
    PDFLineEdit* getAddressField() const { return m_addressField; }
    QPushButton* getStartStopButton() const { return m_startStopButton; }
    QPushButton* getCopyAddressButton() const { return m_copyAddressButton; }
    PDFTintedCard* getCallout() const { return m_callout; }
    QPushButton* getCalloutButton() const { return m_calloutButton; }
    PDFAppSettingRow* findSettingRow(const QString& anchor) const;
    QRadioButton* getLocalRadio() const { return m_localRadio; }
    QRadioButton* getRemoteRadio() const { return m_remoteRadio; }
    PDFLineEdit* getTokenField() const { return m_tokenField; }
    QToolButton* getCopyTokenButton() const { return m_copyTokenButton; }
    QToolButton* getNewTokenButton() const { return m_newTokenButton; }
    QWidget* getRemoteWarning() const { return m_remoteWarning; }
    QList<PDFAssistantRow*> getAssistantRows() const;
    PDFAppEmptyState* getAssistantsEmptyState() const { return m_assistantsEmpty; }
    PDFTextLink* getDisconnectAllLink() const { return m_disconnectAllLink; }
    PDFAppParagraph* getDisconnectHint() const { return m_disconnectHint; }
    QList<PDFActivityRow*> getActivityRows() const;
    PDFAppEmptyState* getActivityEmptyState() const { return m_activityEmpty; }
    PDFTextLink* getClearLink() const { return m_clearLink; }
    PDFTintedCard* getNote() const { return m_note; }
    PDFAssistantTile* getTile(PDFAppMcpSetupClient client) const;
    QList<QPushButton*> getVariantChips() const;
    QList<PDFCodeBox*> getCodeBoxes() const;
    QPushButton* getCopySetupButton() const { return m_copySetupButton; }
    QPushButton* getDocumentationButton() const { return m_documentationButton; }

protected:
    virtual void changeEvent(QEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;

private:
    /// A row of Recent activity with the data of its row mcp_activity
    struct ActivityItem
    {
        PDFMcpActivityRow row;
        quint64 confirmationId = 0;     ///< Waiting confirmation of this instance (0 = not clickable)
    };

    void buildContent();
    void rebuild();

    QWidget* createServerCard(QWidget* parent);
    QWidget* createConnectionGroup(QWidget* parent);
    QWidget* createPermissionsGroup(QWidget* parent);
    QWidget* createAssistantsSection(QWidget* parent);
    QWidget* createActivitySection(QWidget* parent);
    QWidget* createLinksSection(QWidget* parent);
    QWidget* createNote(QWidget* parent);
    QWidget* createConnectSection(QWidget* parent);
    PDFAppSettingRow* createDefinitionRow(const QString& key, const QString& description, QWidget* parent);
    QToolButton* createToolbarButton(const QString& iconName, const QString& toolTip, QWidget* parent);

    ShownState computeShownState() const;
    QString getShownAddress() const;
    QString getShownToken() const;
    bool isTokenRequired() const;
    bool isRemoteAllowed() const;

    void updateAll();
    void updateServer();
    void updateConnection();
    void updateAssistants();
    void updateActivity();
    void updateSetup();
    void rebuildSetupCard();

    void onStateChanged();
    void onAssistantsChanged();
    void onActivityChanged();
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void onStartStopClicked();
    void onCopyAddressClicked();
    void onCalloutClicked();
    void onLocalClicked();
    void onRemoteClicked();
    void onCopyTokenClicked();
    void onNewTokenClicked();
    void onNewTokenFinished(int result);
    void onDisconnectClicked();
    void onDisconnectAllClicked();
    void onActivityClicked();
    void onClearClicked();
    void onTileToggled(int id, bool isChecked);
    void onVariantToggled(int id, bool isChecked);
    void onCopySetupClicked();

    QPointer<PDFAppMcpController> m_controller;
    ShownState m_shownState = ShownState::Stopped;
    PDFAppMcpSetupClient m_setupClient = PDFAppMcpSetupClient::ClaudeCode;

    QPointer<PDFAppColumns> m_columns;
    QPointer<QWidget> m_connectSection;

    // Server card
    QPointer<PDFTintedCard> m_serverCard;
    QPointer<PDFIconBadge> m_serverLogo;
    QPointer<PDFStatusBadge> m_stateBadge;
    QPointer<PDFStatusBadge> m_remoteBadge;
    QPointer<PDFAppParagraph> m_serverMeta;
    QPointer<PDFTintedCard> m_callout;
    QPointer<PDFIconBadge> m_calloutIcon;
    QPointer<PDFAppParagraph> m_calloutTitle;
    QPointer<PDFAppParagraph> m_calloutText;
    QPointer<QPushButton> m_calloutButton;
    QPointer<PDFLineEdit> m_addressField;
    QPointer<QWidget> m_addressBox;
    QPointer<QPushButton> m_copyAddressButton;
    QPointer<QPushButton> m_startStopButton;

    // Connection and Permissions
    QPointer<PDFAppSettingGroup> m_connectionGroup;
    QPointer<PDFAppSettingGroup> m_permissionsGroup;
    QList<QPointer<PDFAppSettingRow>> m_settingRows;
    QPointer<PDFAppSettingRow> m_portRow;
    QPointer<PDFAppSettingRow> m_requireTokenRow;
    QPointer<QRadioButton> m_localRadio;
    QPointer<QRadioButton> m_remoteRadio;
    QPointer<QWidget> m_remoteWarning;
    QPointer<PDFLineEdit> m_tokenField;
    QPointer<QToolButton> m_copyTokenButton;
    QPointer<QToolButton> m_newTokenButton;

    // Side column
    QPointer<PDFTextLink> m_disconnectAllLink;
    QPointer<QWidget> m_assistantsCard;
    QList<QPointer<PDFAssistantRow>> m_assistantRows;
    QPointer<PDFAppEmptyState> m_assistantsEmpty;
    QPointer<PDFAppParagraph> m_disconnectHint;
    std::vector<PDFMcpClientRow> m_shownClients;
    bool m_hasShownClients = false;
    QPointer<PDFTextLink> m_clearLink;
    QPointer<QWidget> m_activityCard;
    QList<QPointer<PDFActivityRow>> m_activityRows;
    std::vector<ActivityItem> m_activityItems;
    QPointer<PDFAppEmptyState> m_activityEmpty;
    QPointer<PDFTintedCard> m_note;
    QPointer<PDFIconBadge> m_noteIcon;
    QPointer<PDFAppParagraph> m_noteTitle;
    QPointer<PDFAppParagraph> m_noteText;

    // Connect an assistant
    QPointer<PDFAppParagraph> m_connectHint;
    QPointer<QButtonGroup> m_tileGroup;
    QList<QPointer<PDFAssistantTile>> m_tiles;
    QPointer<QWidget> m_setupCard;
    QPointer<QButtonGroup> m_variantGroup;
    QList<QPointer<QPushButton>> m_variantChips;
    QList<QPointer<PDFCodeBox>> m_codeBoxes;
    QPointer<QPushButton> m_copySetupButton;
    QPointer<QPushButton> m_documentationButton;
    QString m_setupSignature;

    QPointer<PDFAppAiNewTokenDialog> m_newTokenDialog;
};

/// Factory of the page AI
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAiPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPAIPAGE_H
