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

#ifndef PDFAPPWHATSNEWPAGE_H
#define PDFAPPWHATSNEWPAGE_H

#include "pdfappglobal.h"
#include "pdfapppagelayout.h"
#include "pdfappmarkdownview.h"
#include "pdfappworkspace.h"
#include "pdfupdatechecker.h"
#include "pdfwhatsnew.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentrows.h"

#include <QPointer>
#include <QPushButton>

namespace pdf
{



/// Card Update status of What's New (Figma 104:2141, the states 989:14257): the
/// circle with the icon of the state, the title, one line of detail, the time of
/// the last check and the buttons. The buttons keep their place in every state:
/// Checking disables Check for updates, Could not check renames it to Try Again,
/// Update available adds the Primary Download from GitHub below it; Automatic check
/// is off has the link Turn on in Settings instead of the time.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppUpdateStatusCard : public PDFAppCard
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCard;

public:
    explicit PDFAppUpdateStatusCard(QWidget* parent = nullptr);
    virtual ~PDFAppUpdateStatusCard() override;

    /// Shows the status; \p now is the current time (texts "today at 09:12")
    void setStatus(const PDFUpdateStatus& status, qint64 now);
    const PDFUpdateStatus& getStatus() const { return m_status; }

    /// Texts of the shown state (tests)
    QString getTitle() const;
    QString getDetail() const;
    QString getTimeText() const;

    PDFIconBadge* getIconCircle() const { return m_icon; }
    QPushButton* getCheckButton() const { return m_checkButton; }
    QPushButton* getDownloadButton() const { return m_downloadButton; }
    PDFTextLink* getSettingsLink() const { return m_settingsLink; }

Q_SIGNALS:
    void checkRequested();
    void downloadRequested();
    void settingsRequested();

private:
    void updateTexts();

    PDFUpdateStatus m_status;
    qint64 m_now = 0;
    QPointer<PDFIconBadge> m_icon;
    QPointer<PDFLabel> m_title;
    QPointer<PDFAppParagraph> m_detail;
    QPointer<PDFLabel> m_time;
    QPointer<PDFTextLink> m_settingsLink;
    QPointer<QPushButton> m_checkButton;
    QPointer<QPushButton> m_downloadButton;

protected:
    virtual void changeEvent(QEvent* event) override;
};

/// Section Version history (Figma 104:2215): the header with View all / Show
/// less and a card of the versions of the catalog (the newest first). Collapsed,
/// it shows COLLAPSED_VERSIONS rows (and the shown version, when it is older).
/// A row (component Version row 103:1950) is a link to its version (signal
/// versionActivated): the shown version is selected, the running one current.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppVersionHistory : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppVersionHistory(QWidget* parent = nullptr);
    virtual ~PDFAppVersionHistory() override;

    /// Rows of the collapsed history
    static constexpr int COLLAPSED_VERSIONS = 4;

    /// Shows the releases of the catalog; \p shownVersion is selected, \p runningVersion is current
    void setReleases(const std::vector<PDFWhatsNewRelease>& releases, const QVersionNumber& shownVersion, const QVersionNumber& runningVersion);

    bool isAllShown() const { return m_isAllShown; }
    void setAllShown(bool isAllShown);

    QList<PDFVersionRow*> getRows() const;
    PDFTextLink* getViewAllLink() const { return m_viewAllLink; }

    /// Version of a row (null for an unknown row)
    QVersionNumber getRowVersion(const PDFVersionRow* row) const;

Q_SIGNALS:
    void versionActivated(const QVersionNumber& version);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void fill();
    void onRowClicked();
    void onViewAllClicked();

    struct Item
    {
        QVersionNumber version;
        QString date;
        QString summary;
    };

    std::vector<Item> m_items;
    QVersionNumber m_shownVersion;
    QVersionNumber m_runningVersion;
    bool m_isAllShown = false;
    QPointer<PDFAppSectionHeader> m_header;
    QPointer<PDFTextLink> m_viewAllLink;
    QPointer<PDFAppCard> m_card;
    QList<QPointer<PDFVersionRow>> m_rows;
    std::vector<QVersionNumber> m_rowVersions;
};

/// Page What's New (/whats-new, /whats-new/{version}; Figma pdf4qt-whatsnew 103:1951,
/// previous release 988:13320, all versions 988:13824, update available 988:152102):
/// the release card, Highlights, New tools in this release, Fixed issues, the card
/// Update status, Links, Version history and Thank you. Empty sections are hidden.
/// The data come from the What's New files (PDFWhatsNewCatalog), the stars of the
/// new tools and the update check from the database.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppWhatsNewPage : public PDFAppContentPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppContentPage;

public:
    explicit PDFAppWhatsNewPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppWhatsNewPage() override;

    /// Highest list of the fixed issues (it scrolls inside of its card)
    static constexpr int MAXIMUM_ISSUE_LIST_HEIGHT = 310;

    /// Key of the expanded version history in the view state
    static QString getAllVersionsKey() { return QStringLiteral("allVersions"); }

    /// Replaces the catalog of the releases (tests); the route is shown again
    void setCatalog(const PDFWhatsNewCatalog& catalog);
    const PDFWhatsNewCatalog& getCatalog() const { return m_catalog; }

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual QString getHeaderTitle() const override;
    virtual QString getHeaderSubtitle() const override;
    virtual QVariantMap saveViewState() const override;
    virtual void restoreViewState(const QVariantMap& state) override;

    /// Shown release (invalid before setRoute)
    const PDFWhatsNewRelease& getRelease() const { return m_release; }

    /// The shown release is the running version
    bool isRunningRelease() const { return m_isRunningRelease; }

    /// Parts of the page (tests)
    QWidget* getReleaseCard() const { return m_releaseCard; }
    PDFStatusBadge* getReleaseBadge() const { return m_releaseBadge; }
    QPushButton* getReleaseNotesButton() const { return m_releaseNotesButton; }
    QPushButton* getExploreToolsButton() const { return m_exploreToolsButton; }
    QWidget* getHighlightsSection() const { return m_highlightsSection; }
    QWidget* getNewToolsSection() const { return m_newToolsSection; }
    QWidget* getFixedIssuesSection() const { return m_fixedIssuesSection; }
    PDFAppScrollArea* getIssueScrollArea() const { return m_issueScrollArea; }
    PDFAppUpdateStatusCard* getUpdateStatusCard() const { return m_updateCard; }
    QWidget* getLinksSection() const { return m_linksSection; }
    PDFAppVersionHistory* getVersionHistory() const { return m_versionHistory; }
    PDFTextLink* getViewAllIssuesLink() const { return m_viewAllIssuesLink; }
    QList<PDFHighlightCard*> getHighlightCards() const;
    QList<PDFToolRow*> getToolRows() const;
    QList<PDFIssueRow*> getIssueRows() const;
    QList<PDFLinkRow*> getLinkRows() const;

    /// Release of the running version or, when it has no file, the newest older one
    static const PDFWhatsNewRelease* findRunningRelease(const PDFWhatsNewCatalog& catalog, const QVersionNumber& runningVersion);

    /// Addresses of the links
    static QUrl getIssueUrl(int number);
    static QUrl getRepositoryUrl();
    static QUrl getChangelogUrl();

protected:
    virtual void showEvent(QShowEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual bool scrollToAnchor(const QString& anchor) override;

private:
    void build();
    void clear();
    QWidget* createReleaseCard(QWidget* parent);
    QWidget* createHighlights(QWidget* parent);
    QWidget* createNewTools(QWidget* parent);
    QWidget* createFixedIssues(QWidget* parent);
    QWidget* createLinks(QWidget* parent);
    QWidget* createThankYou(QWidget* parent);
    void updateStatus();
    void updatePins();

    void onReleaseNotesClicked();
    void onExploreToolsClicked();
    void onToolClicked();
    void onToolPinnedChanged(bool isPinned);
    void onIssueClicked();
    void onLinkClicked();
    void onVersionActivated(const QVersionNumber& version);
    void onViewAllIssuesClicked();
    void onCheckRequested();
    void onDownloadRequested();
    void onSettingsRequested();
    void onCheckerStatusChanged();
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onEverythingChanged();

    PDFWhatsNewCatalog m_catalog;
    PDFWhatsNewRelease m_release;
    QVersionNumber m_runningVersion;
    bool m_isRunningRelease = false;
    bool m_isUpdatingPins = false;
    QPointer<PDFUpdateChecker> m_checker;

    QPointer<QWidget> m_content;
    QPointer<QWidget> m_releaseCard;
    QPointer<PDFStatusBadge> m_releaseBadge;
    QPointer<QPushButton> m_releaseNotesButton;
    QPointer<QPushButton> m_exploreToolsButton;
    QPointer<QWidget> m_highlightsSection;
    QPointer<QWidget> m_newToolsSection;
    QPointer<QWidget> m_fixedIssuesSection;
    QPointer<PDFAppScrollArea> m_issueScrollArea;
    QPointer<PDFAppUpdateStatusCard> m_updateCard;
    QPointer<QWidget> m_linksSection;
    QPointer<PDFAppVersionHistory> m_versionHistory;
    QPointer<PDFTextLink> m_viewAllIssuesLink;
};

/// Page of the full release notes (/whats-new/{version}/notes, Figma
/// pdf4qt-whatsnew-notes 989:152125): the Markdown of "## Release notes" in the
/// Markdown view (the layout of the manual), Version history and the links Release
/// on GitHub, Full changelog and Back to What's New at the side
class PDF4QTLIBAPPSHARED_EXPORT PDFAppReleaseNotesPage : public PDFAppContentPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppContentPage;

public:
    explicit PDFAppReleaseNotesPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppReleaseNotesPage() override;

    void setCatalog(const PDFWhatsNewCatalog& catalog);

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual QString getHeaderTitle() const override;
    virtual QString getHeaderSubtitle() const override;
    virtual QList<PDFAppBreadcrumbItem> getHeaderBreadcrumbs() const override;
    virtual QVariantMap saveViewState() const override;
    virtual void restoreViewState(const QVariantMap& state) override;

    const PDFWhatsNewRelease& getRelease() const { return m_release; }
    PDFAppMarkdownView* getMarkdownView() const { return m_markdownView; }
    PDFAppVersionHistory* getVersionHistory() const { return m_versionHistory; }
    QList<PDFLinkRow*> getLinkRows() const;

protected:
    virtual bool scrollToAnchor(const QString& anchor) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void build();
    void onMarkdownLinkActivated(const QString& target, pdf::PDFAppMarkdownLinkKind kind);
    void onVersionActivated(const QVersionNumber& version);
    void onLinkClicked();

    PDFWhatsNewCatalog m_catalog;
    PDFWhatsNewRelease m_release;
    QVersionNumber m_runningVersion;
    QPointer<QWidget> m_content;
    QPointer<PDFAppMarkdownView> m_markdownView;
    QPointer<PDFAppVersionHistory> m_versionHistory;
    QPointer<QWidget> m_linksCard;
};

/// Factory of the What's New page
class PDF4QTLIBAPPSHARED_EXPORT PDFAppWhatsNewPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

/// Factory of the page of the release notes
class PDF4QTLIBAPPSHARED_EXPORT PDFAppReleaseNotesPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPWHATSNEWPAGE_H
