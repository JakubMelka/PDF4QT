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

#include "pdfappwhatsnewpage.h"
#include "pdfapptoolpresentation.h"
#include "pdfappcontext.h"
#include "pdfapplocale.h"
#include "pdfapproute.h"
#include "pdfappworkspace.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdftoolrepository.h"

#include "pdfdocumenttoolcatalog.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the What's New pages
class PDFAppWhatsNewHelper
{
public:
    PDFAppWhatsNewHelper() = delete;

    static QString getReleaseDate(const PDFWhatsNewRelease& release);
    static QString getReleaseMeta(const PDFWhatsNewRelease& release, const QVersionNumber& runningVersion);
    static bool isSameVersion(const QVersionNumber& left, const QVersionNumber& right);
    static QWidget* createSection(const QString& title, QWidget* content, QWidget* parent, PDFAppSectionHeader** header = nullptr);
    static PDFLinkRow* addLinkRow(QVBoxLayout* layout, const QString& title, const QString& subtitle, const QString& iconName, const QString& link, QWidget* parent);
    static QString getLinkTarget(const QObject* row) { return row ? row->property("pdf4qtLink").toString() : QString(); }
    static QWidget* createColumn(QWidget* parent);
    static QString getOldestVersion(const PDFWhatsNewCatalog& catalog);
};

QString PDFAppWhatsNewHelper::getReleaseDate(const PDFWhatsNewRelease& release)
{
    if (release.isUnreleased || !release.date.isValid())
    {
        return QCoreApplication::translate("pdf::PDFAppWhatsNewPage", "Not released yet");
    }
    return PDFUpdateStatus::formatDate(release.date);
}

QString PDFAppWhatsNewHelper::getReleaseMeta(const PDFWhatsNewRelease& release, const QVersionNumber& runningVersion)
{
    const QString date = release.isUnreleased || !release.date.isValid() ? QCoreApplication::translate("pdf::PDFAppWhatsNewPage", "Not released yet")
                                                                         : QCoreApplication::translate("pdf::PDFAppWhatsNewPage", "Released %1").arg(PDFUpdateStatus::formatDate(release.date));
    const QString running = isSameVersion(release.version, runningVersion)
                                ? QCoreApplication::translate("pdf::PDFAppWhatsNewPage", "This is the version you are running")
                                : QCoreApplication::translate("pdf::PDFAppWhatsNewPage", "You are running %1").arg(PDFWhatsNewRelease::getDisplayVersion(runningVersion));
    return QStringLiteral("%1 · %2").arg(date, running);
}

bool PDFAppWhatsNewHelper::isSameVersion(const QVersionNumber& left, const QVersionNumber& right)
{
    return !left.isNull() && left.normalized() == right.normalized();
}

QWidget* PDFAppWhatsNewHelper::createSection(const QString& title, QWidget* content, QWidget* parent, PDFAppSectionHeader** header)
{
    // Section: header 18 px, gap 12, the content
    QWidget* section = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    PDFAppSectionHeader* sectionHeader = new PDFAppSectionHeader(title, section);
    layout->addWidget(sectionHeader);
    if (content)
    {
        content->setParent(section);
        layout->addWidget(content);
    }

    if (header)
    {
        *header = sectionHeader;
    }
    return section;
}

PDFLinkRow* PDFAppWhatsNewHelper::addLinkRow(QVBoxLayout* layout, const QString& title, const QString& subtitle, const QString& iconName, const QString& link, QWidget* parent)
{
    PDFLinkRow* row = new PDFLinkRow(title, subtitle, iconName, parent);
    row->setProperty("pdf4qtLink", link);
    layout->addWidget(row);
    return row;
}

QWidget* PDFAppWhatsNewHelper::createColumn(QWidget* parent)
{
    QWidget* column = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(24);
    return column;
}

QString PDFAppWhatsNewHelper::getOldestVersion(const PDFWhatsNewCatalog& catalog)
{
    const std::vector<PDFWhatsNewRelease>& releases = catalog.getReleases();
    return releases.empty() ? QString() : releases.back().getDisplayVersion();
}

PDFAppUpdateStatusCard::PDFAppUpdateStatusCard(QWidget* parent) :
    BaseClass(QMargins(17, 17, 17, 17), 12, parent)
{
    setObjectName(QStringLiteral("updateStatusCard"));

    QWidget* status = new QWidget(this);
    QHBoxLayout* statusLayout = new QHBoxLayout(status);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(12);

    m_icon = new PDFIconBadge(QString(), 40, 20, status);
    statusLayout->addWidget(m_icon, 0, Qt::AlignVCenter);

    QWidget* texts = new QWidget(status);
    QVBoxLayout* textsLayout = new QVBoxLayout(texts);
    textsLayout->setContentsMargins(0, 0, 0, 0);
    textsLayout->setSpacing(2);
    m_title = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 14, PDFThemeColors::TextPrimary, texts);
    m_detail = new PDFAppParagraph(PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextTertiary, texts);
    textsLayout->addWidget(m_title);
    textsLayout->addWidget(m_detail);
    // status 104:2145: the title and the detail (34 px) centered by the icon circle
    statusLayout->addWidget(texts, 1, Qt::AlignVCenter);
    getLayout()->addWidget(status);

    // last-checked 104:2152: the line of 13 px, the button 12 px below it
    m_time = PDFAppPageWidgets::createLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, this);
    m_time->setFixedHeight(13);
    getLayout()->addWidget(m_time);

    m_settingsLink = new PDFTextLink(this);
    getLayout()->addWidget(m_settingsLink, 0, Qt::AlignLeading);

    m_checkButton = new QPushButton(PDFThemeIcons::getIcon(QStringLiteral("rotate-ccw")), QString(), this);
    m_checkButton->setObjectName(QStringLiteral("checkForUpdates"));
    getLayout()->addWidget(m_checkButton);

    m_downloadButton = new QPushButton(PDFThemeIcons::getIcon(QStringLiteral("external-link")), QString(), this);
    m_downloadButton->setObjectName(QStringLiteral("downloadFromGitHub"));
    PDFWidgetStyleProperties::set(m_downloadButton, PDFWidgetStyleProperties::PRIMARY);
    getLayout()->addWidget(m_downloadButton);

    connect(m_checkButton, &QPushButton::clicked, this, &PDFAppUpdateStatusCard::checkRequested);
    connect(m_downloadButton, &QPushButton::clicked, this, &PDFAppUpdateStatusCard::downloadRequested);
    connect(m_settingsLink, &PDFTextLink::clicked, this, &PDFAppUpdateStatusCard::settingsRequested);

    updateTexts();
}

PDFAppUpdateStatusCard::~PDFAppUpdateStatusCard() = default;

void PDFAppUpdateStatusCard::setStatus(const PDFUpdateStatus& status, qint64 now)
{
    m_status = status;
    m_now = now;
    updateTexts();
}

QString PDFAppUpdateStatusCard::getTitle() const
{
    return m_title ? m_title->text() : QString();
}

QString PDFAppUpdateStatusCard::getDetail() const
{
    return m_detail ? m_detail->text() : QString();
}

QString PDFAppUpdateStatusCard::getTimeText() const
{
    return (m_time && !m_time->isHidden()) ? m_time->text() : QString();
}

void PDFAppUpdateStatusCard::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

void PDFAppUpdateStatusCard::updateTexts()
{
    if (!m_icon || !m_title || !m_detail || !m_time || !m_settingsLink || !m_checkButton || !m_downloadButton)
    {
        return;
    }

    const QString running = PDFWhatsNewRelease::getDisplayVersion(m_status.runningVersion);
    const QString latest = PDFWhatsNewRelease::getDisplayVersion(m_status.latestVersion);

    QString iconName;
    PDFAccentHue hue = PDFAccentHue::Neutral;
    QString title;
    QString detail;
    QString time;
    bool isSettingsLinkVisible = false;
    bool isDownloadVisible = false;
    bool isCheckEnabled = true;
    QString checkText = tr("Check for updates");

    switch (m_status.state)
    {
        case PDFUpdateStatus::State::NotChecked:
            iconName = QStringLiteral("rotate-ccw");
            title = tr("Not checked yet");
            detail = tr("PDF4QT %1 has not looked for updates yet").arg(running);
            break;

        case PDFUpdateStatus::State::Checking:
            iconName = QStringLiteral("hourglass");
            hue = PDFAccentHue::Blue;
            title = tr("Checking for updates…");
            detail = tr("Asking GitHub for the latest release");
            if (m_status.lastSuccess > 0)
            {
                time = tr("Last checked %1").arg(PDFUpdateStatus::formatTime(m_status.lastSuccess, m_now));
            }
            isCheckEnabled = false;
            break;

        case PDFUpdateStatus::State::UpToDate:
            iconName = QStringLiteral("check");
            hue = PDFAccentHue::Green;
            title = tr("You're up to date");
            detail = tr("PDF4QT %1 is the latest version").arg(running);
            time = tr("Last checked %1").arg(PDFUpdateStatus::formatTime(m_status.lastSuccess, m_now));
            break;

        case PDFUpdateStatus::State::UpdateAvailable:
            iconName = QStringLiteral("download");
            hue = PDFAccentHue::Blue;
            title = tr("PDF4QT %1 is available").arg(latest);
            detail = tr("You are running %1").arg(running);
            if (m_status.lastSuccess > 0)
            {
                time = m_status.isAutoCheck ? tr("Checked automatically %1").arg(PDFUpdateStatus::formatTime(m_status.lastSuccess, m_now))
                                            : tr("Last checked %1").arg(PDFUpdateStatus::formatTime(m_status.lastSuccess, m_now));
            }
            isDownloadVisible = true;
            break;

        case PDFUpdateStatus::State::Failed:
            iconName = QStringLiteral("alert-triangle");
            hue = PDFAccentHue::Amber;
            title = tr("Could not check for updates");
            detail = m_status.error;
            time = m_status.lastSuccess > 0 ? tr("Last successful check %1").arg(PDFUpdateStatus::formatTime(m_status.lastSuccess, m_now))
                                            : tr("No successful check yet");
            checkText = tr("Try Again");
            break;

        case PDFUpdateStatus::State::AutoCheckOff:
            iconName = QStringLiteral("timer-off");
            title = tr("Automatic check is off");
            detail = m_status.lastSuccess > 0 ? tr("PDF4QT %1 · last checked %2").arg(running, PDFUpdateStatus::formatDate(QDateTime::fromMSecsSinceEpoch(m_status.lastSuccess).date()))
                                              : tr("PDF4QT %1 · never checked").arg(running);
            isSettingsLinkVisible = true;
            break;
    }

    m_icon->setIconName(iconName);
    m_icon->setHue(hue);
    m_title->setText(title);
    m_detail->setText(detail);
    m_time->setText(time);
    m_time->setVisible(!time.isEmpty());
    m_settingsLink->setText(tr("Turn on in Settings"));
    m_settingsLink->setVisible(isSettingsLinkVisible);
    m_checkButton->setText(checkText);
    m_checkButton->setEnabled(isCheckEnabled);
    m_downloadButton->setText(tr("Download from GitHub"));
    m_downloadButton->setVisible(isDownloadVisible);

    setAccessibleName(title);
    setAccessibleDescription(detail);
}

PDFAppVersionHistory::PDFAppVersionHistory(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("versionHistory"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    m_header = new PDFAppSectionHeader(tr("Version history"), this);
    m_viewAllLink = new PDFTextLink(m_header);
    m_header->setTrailingWidget(m_viewAllLink);
    layout->addWidget(m_header);

    m_card = new PDFAppCard(QMargins(9, 9, 9, 9), 0, this);
    layout->addWidget(m_card);

    connect(m_viewAllLink, &PDFTextLink::clicked, this, &PDFAppVersionHistory::onViewAllClicked);
}

PDFAppVersionHistory::~PDFAppVersionHistory() = default;

void PDFAppVersionHistory::setReleases(const std::vector<PDFWhatsNewRelease>& releases, const QVersionNumber& shownVersion, const QVersionNumber& runningVersion)
{
    m_items.clear();
    for (const PDFWhatsNewRelease& release : releases)
    {
        Item item;
        item.version = release.version;
        item.date = PDFAppWhatsNewHelper::getReleaseDate(release);
        item.summary = release.summary;
        m_items.push_back(std::move(item));
    }
    m_shownVersion = shownVersion;
    m_runningVersion = runningVersion;
    fill();
}

void PDFAppVersionHistory::setAllShown(bool isAllShown)
{
    if (m_isAllShown != isAllShown)
    {
        m_isAllShown = isAllShown;
        fill();
    }
}

QList<PDFVersionRow*> PDFAppVersionHistory::getRows() const
{
    QList<PDFVersionRow*> rows;
    for (const QPointer<PDFVersionRow>& row : m_rows)
    {
        if (row)
        {
            rows.append(row);
        }
    }
    return rows;
}

QVersionNumber PDFAppVersionHistory::getRowVersion(const PDFVersionRow* row) const
{
    for (qsizetype i = 0; i < m_rows.size() && size_t(i) < m_rowVersions.size(); ++i)
    {
        if (m_rows.at(i) == row)
        {
            return m_rowVersions[size_t(i)];
        }
    }
    return QVersionNumber();
}

void PDFAppVersionHistory::fill()
{
    if (!m_card || !m_viewAllLink)
    {
        return;
    }

    for (const QPointer<PDFVersionRow>& row : m_rows)
    {
        delete row.data();
    }
    m_rows.clear();
    m_rowVersions.clear();

    // Collapsed: the first rows and the shown version, when it is older
    std::vector<size_t> indices;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        const bool isShown = PDFAppWhatsNewHelper::isSameVersion(m_items[i].version, m_shownVersion);
        if (m_isAllShown || i < size_t(COLLAPSED_VERSIONS) || isShown)
        {
            indices.push_back(i);
        }
    }

    for (size_t k = 0; k < indices.size(); ++k)
    {
        const Item& item = m_items[indices[k]];
        PDFVersionRow* row = new PDFVersionRow(m_card);
        row->setVersion(QStringLiteral("v%1").arg(PDFWhatsNewRelease::getDisplayVersion(item.version)));
        row->setDate(item.date);
        row->setSummary(item.summary);
        row->setCurrent(PDFAppWhatsNewHelper::isSameVersion(item.version, m_runningVersion));
        row->setSelected(PDFAppWhatsNewHelper::isSameVersion(item.version, m_shownVersion));
        row->setLineVisible(k + 1 < indices.size());
        m_card->getLayout()->addWidget(row);
        connect(row, &PDFVersionRow::clicked, this, &PDFAppVersionHistory::onRowClicked);
        m_rows.append(row);
        m_rowVersions.push_back(item.version);
    }

    m_viewAllLink->setText(m_isAllShown ? tr("Show less") : tr("View all"));
    m_viewAllLink->setVisible(m_items.size() > size_t(COLLAPSED_VERSIONS));
    setVisible(!m_items.empty());
}

void PDFAppVersionHistory::onRowClicked()
{
    const PDFVersionRow* row = qobject_cast<const PDFVersionRow*>(sender());
    const QVersionNumber version = getRowVersion(row);
    if (!version.isNull())
    {
        // The slot may delete the history (navigation)
        Q_EMIT versionActivated(version);
    }
}

void PDFAppVersionHistory::onViewAllClicked()
{
    // fill keeps the link (it is a part of the header)
    setAllShown(!m_isAllShown);
}

void PDFAppVersionHistory::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        if (m_header && m_header->getTitleLabel())
        {
            m_header->getTitleLabel()->setText(tr("Version history"));
        }
        if (m_viewAllLink)
        {
            m_viewAllLink->setText(m_isAllShown ? tr("Show less") : tr("View all"));
        }
    }
}

PDFAppWhatsNewPage::PDFAppWhatsNewPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent),
    m_catalog(PDFWhatsNewCatalog::getBuiltin()),
    m_runningVersion(PDFWhatsNewCatalog::getRunningVersion())
{
    setObjectName(QStringLiteral("whatsNewPage"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_checker = PDFUpdateChecker::getInstance(context);
    if (m_checker)
    {
        m_runningVersion = m_checker->getRunningVersion();
        connect(m_checker, &PDFUpdateChecker::statusChanged, this, &PDFAppWhatsNewPage::onCheckerStatusChanged);
    }

    if (context && context->getNotifier())
    {
        connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppWhatsNewPage::onDatabaseChanged);
        connect(context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppWhatsNewPage::onEverythingChanged);
    }
}

PDFAppWhatsNewPage::~PDFAppWhatsNewPage() = default;

const PDFWhatsNewRelease* PDFAppWhatsNewPage::findRunningRelease(const PDFWhatsNewCatalog& catalog, const QVersionNumber& runningVersion)
{
    if (const PDFWhatsNewRelease* release = catalog.findRelease(runningVersion))
    {
        return release;
    }

    // The newest release older than the running version (a build between releases)
    for (const PDFWhatsNewRelease& release : catalog.getReleases())
    {
        if (QVersionNumber::compare(release.version.normalized(), runningVersion.normalized()) < 0)
        {
            return &release;
        }
    }
    return catalog.getReleases().empty() ? nullptr : &catalog.getReleases().front();
}

QUrl PDFAppWhatsNewPage::getIssueUrl(int number)
{
    return QUrl(QStringLiteral("https://github.com/JakubMelka/PDF4QT/issues/%1").arg(number));
}

QUrl PDFAppWhatsNewPage::getRepositoryUrl()
{
    return QUrl(QStringLiteral("https://github.com/JakubMelka/PDF4QT"));
}

QUrl PDFAppWhatsNewPage::getChangelogUrl()
{
    return QUrl(QStringLiteral("https://github.com/JakubMelka/PDF4QT/blob/master/RELEASES.txt"));
}

void PDFAppWhatsNewPage::setCatalog(const PDFWhatsNewCatalog& catalog)
{
    m_catalog = std::make_shared<const PDFWhatsNewCatalog>(catalog);
    if (getRoute().isValid())
    {
        setRoute(getRoute());
    }
}

bool PDFAppWhatsNewPage::setRoute(const PDFAppRoute& route)
{
    // /whats-new = the running version, /whats-new/{version} = the version of the catalog
    const PDFWhatsNewRelease* release = nullptr;
    if (route.getSegments().size() >= 2)
    {
        release = m_catalog->findRelease(PDFWhatsNewParser::parseVersion(route.getSegment(1)));
    }
    else
    {
        release = findRunningRelease(*m_catalog, m_runningVersion);
    }

    if (!release)
    {
        return false;
    }

    BaseClass::setRoute(route);

    // The release in the language of the user interface (step 5), English without a translation
    m_release = m_catalog->getLocalizedRelease(release->version, PDFAppLocale::getUiLanguage());
    m_isRunningRelease = PDFAppWhatsNewHelper::isSameVersion(m_release.version, m_runningVersion);
    build();

    QPointer<PDFAppWhatsNewPage> guard(this);
    Q_EMIT titleChanged();
    return !guard.isNull();
}

QString PDFAppWhatsNewPage::getTitle() const
{
    return tr("What's New");
}

QString PDFAppWhatsNewPage::getHeaderTitle() const
{
    return tr("What's New");
}

QString PDFAppWhatsNewPage::getHeaderSubtitle() const
{
    if (!m_release.isValid() || m_isRunningRelease)
    {
        return tr("What changed in the latest PDF4QT release: new tools, improvements and fixed issues.");
    }
    return tr("What changed in PDF4QT %1, an earlier release.").arg(m_release.getDisplayVersion());
}

QVariantMap PDFAppWhatsNewPage::saveViewState() const
{
    QVariantMap state = BaseClass::saveViewState();
    state.insert(getAllVersionsKey(), m_versionHistory && m_versionHistory->isAllShown());
    return state;
}

void PDFAppWhatsNewPage::restoreViewState(const QVariantMap& state)
{
    if (m_versionHistory)
    {
        m_versionHistory->setAllShown(state.value(getAllVersionsKey()).toBool());
    }
    BaseClass::restoreViewState(state);
}

void PDFAppWhatsNewPage::clear()
{
    if (m_content)
    {
        delete m_content.data();
    }
}

void PDFAppWhatsNewPage::build()
{
    const bool isAllVersionsShown = m_versionHistory && m_versionHistory->isAllShown();
    clear();

    m_content = new QWidget(this);
    m_content->setObjectName(QStringLiteral("whatsNewContent"));
    layout()->addWidget(m_content);

    QWidget* mainColumn = PDFAppWhatsNewHelper::createColumn(m_content);
    mainColumn->setObjectName(QStringLiteral("mainColumn"));
    QVBoxLayout* mainLayout = static_cast<QVBoxLayout*>(mainColumn->layout());
    mainLayout->addWidget(createReleaseCard(mainColumn));
    mainLayout->addWidget(createHighlights(mainColumn));
    mainLayout->addWidget(createNewTools(mainColumn));
    mainLayout->addWidget(createFixedIssues(mainColumn));

    QWidget* sideColumn = PDFAppWhatsNewHelper::createColumn(m_content);
    sideColumn->setObjectName(QStringLiteral("sideColumn"));
    QVBoxLayout* sideLayout = static_cast<QVBoxLayout*>(sideColumn->layout());

    m_updateCard = new PDFAppUpdateStatusCard(sideColumn);
    sideLayout->addWidget(PDFAppWhatsNewHelper::createSection(tr("Update status"), m_updateCard, sideColumn));
    connect(m_updateCard, &PDFAppUpdateStatusCard::checkRequested, this, &PDFAppWhatsNewPage::onCheckRequested);
    connect(m_updateCard, &PDFAppUpdateStatusCard::downloadRequested, this, &PDFAppWhatsNewPage::onDownloadRequested);
    connect(m_updateCard, &PDFAppUpdateStatusCard::settingsRequested, this, &PDFAppWhatsNewPage::onSettingsRequested);

    sideLayout->addWidget(createLinks(sideColumn));

    m_versionHistory = new PDFAppVersionHistory(sideColumn);
    m_versionHistory->setReleases(m_catalog->getReleases(), m_release.version, m_runningVersion);
    m_versionHistory->setAllShown(isAllVersionsShown);
    connect(m_versionHistory, &PDFAppVersionHistory::versionActivated, this, &PDFAppWhatsNewPage::onVersionActivated);
    sideLayout->addWidget(m_versionHistory);

    sideLayout->addWidget(createThankYou(sideColumn));

    PDFAppColumns::createInContent(m_content, mainColumn, sideColumn);
    m_content->show();

    updateStatus();
    updatePins();
}

QWidget* PDFAppWhatsNewPage::createReleaseCard(QWidget* parent)
{
    // release-card (104:1921): accent/blue for the running version, a plain card
    // for an older one (988:13320); padding 20, gap 20, the logo 56 (radius 14)
    PDFTintedCard* card = new PDFTintedCard(parent);
    card->setObjectName(QStringLiteral("releaseCard"));
    card->setHue(m_isRunningRelease ? std::optional<PDFAccentHue>(PDFAccentHue::Blue) : std::nullopt);
    m_releaseCard = card;

    QHBoxLayout* layout = new QHBoxLayout(card);
    layout->setContentsMargins(21, 21, 21, 21);
    layout->setSpacing(20);

    PDFIconBadge* logo = new PDFIconBadge(QStringLiteral("file-text"), 56, 28, card);
    logo->setRadius(14.0);
    logo->setColors(PDFThemeColors::BgAccent, PDFThemeColors::IconOnAccent);
    layout->addWidget(logo, 0, Qt::AlignTop);

    QWidget* content = new QWidget(card);
    QVBoxLayout* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(6);
    layout->addWidget(content, 1);

    QWidget* titleRow = new QWidget(content);
    QHBoxLayout* titleLayout = new QHBoxLayout(titleRow);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(10);
    PDFLabel* title = PDFAppPageWidgets::createLabel(tr("PDF4QT %1").arg(m_release.getDisplayVersion()), QFont::Bold, 22, PDFThemeColors::TextPrimary, titleRow);
    title->setObjectName(QStringLiteral("releaseTitle"));
    titleLayout->addWidget(title);
    m_releaseBadge = new PDFStatusBadge(titleRow);
    titleLayout->addWidget(m_releaseBadge, 0, Qt::AlignVCenter);
    titleLayout->addStretch(1);
    contentLayout->addWidget(titleRow);

    PDFAppParagraph* meta = new PDFAppParagraph(PDFThemeFonts::getFont(QFont::Normal, 13), 16, PDFThemeColors::TextTertiary, content);
    meta->setObjectName(QStringLiteral("releaseMeta"));
    meta->setText(PDFAppWhatsNewHelper::getReleaseMeta(m_release, m_runningVersion));
    contentLayout->addWidget(meta);

    PDFAppParagraph* description = new PDFAppParagraph(PDFThemeFonts::getFont(QFont::Normal, 13), 20, PDFThemeColors::TextSecondary, content);
    description->setObjectName(QStringLiteral("releaseOverview"));
    description->setText(m_release.overview);
    contentLayout->addWidget(description);

    QWidget* buttons = new QWidget(content);
    QHBoxLayout* buttonsLayout = new QHBoxLayout(buttons);
    buttonsLayout->setContentsMargins(0, 8, 0, 0);
    buttonsLayout->setSpacing(10);

    m_releaseNotesButton = new QPushButton(PDFThemeIcons::getIcon(QStringLiteral("book-open")), tr("Read full release notes"), buttons);
    m_releaseNotesButton->setObjectName(QStringLiteral("readReleaseNotes"));
    PDFWidgetStyleProperties::set(m_releaseNotesButton, PDFWidgetStyleProperties::PRIMARY);
    buttonsLayout->addWidget(m_releaseNotesButton);

    m_exploreToolsButton = new QPushButton(PDFThemeIcons::getIcon(QStringLiteral("grid")), tr("Explore new tools"), buttons);
    m_exploreToolsButton->setObjectName(QStringLiteral("exploreNewTools"));
    m_exploreToolsButton->setVisible(!m_release.newTools.isEmpty());
    buttonsLayout->addWidget(m_exploreToolsButton);
    buttonsLayout->addStretch(1);
    contentLayout->addWidget(buttons);

    connect(m_releaseNotesButton, &QPushButton::clicked, this, &PDFAppWhatsNewPage::onReleaseNotesClicked);
    connect(m_exploreToolsButton, &QPushButton::clicked, this, &PDFAppWhatsNewPage::onExploreToolsClicked);

    return card;
}

QWidget* PDFAppWhatsNewPage::createHighlights(QWidget* parent)
{
    // Highlight cards in 3 columns, gaps 12 (fewer columns in a narrow window)
    PDFAppGrid* grid = new PDFAppGrid(220, 3, 12, 12, parent);
    for (const PDFWhatsNewHighlight& highlight : m_release.highlights)
    {
        PDFHighlightCard* card = new PDFHighlightCard(highlight.title, highlight.description, highlight.iconName, highlight.hue, grid);
        grid->addWidget(card);
    }

    m_highlightsSection = PDFAppWhatsNewHelper::createSection(tr("Highlights"), grid, parent);
    m_highlightsSection->setObjectName(QStringLiteral("highlights"));
    m_highlightsSection->setVisible(!m_release.highlights.empty());
    return m_highlightsSection;
}

QWidget* PDFAppWhatsNewPage::createNewTools(QWidget* parent)
{
    // new-tools-list (104:2035): a card with the padding 6, Tool rows in 2 columns, gaps 8 / 2
    PDFAppCard* card = new PDFAppCard(QMargins(7, 7, 7, 7), 0, parent);
    PDFAppGrid* grid = new PDFAppGrid(300, 2, 8, 2, card);
    card->getLayout()->addWidget(grid);

    for (const QString& toolId : m_release.newTools)
    {
        const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
        if (!tool)
        {
            continue;
        }

        PDFToolRow* row = new PDFToolRow(tool->getTitle(), tool->getDescription(), tool->getIconName(), PDFAppToolPresentation::getHue(tool->getHue()), grid);
        row->setProperty("pdf4qtToolId", toolId);
        grid->addWidget(row);
        connect(row, &PDFToolRow::clicked, this, &PDFAppWhatsNewPage::onToolClicked);
        connect(row, &PDFToolRow::pinnedChanged, this, &PDFAppWhatsNewPage::onToolPinnedChanged);
    }

    PDFAppSectionHeader* header = nullptr;
    m_newToolsSection = PDFAppWhatsNewHelper::createSection(tr("New tools in this release"), card, parent, &header);
    m_newToolsSection->setObjectName(QStringLiteral("newTools"));
    header->setTrailingWidget(PDFAppPageWidgets::createLabel(tr("Pin them with ★ to keep them close"), PDFThemeFonts::Role::Small, PDFThemeColors::TextMuted, header));
    m_newToolsSection->setVisible(grid->getCellCount() > 0);
    return m_newToolsSection;
}

QWidget* PDFAppWhatsNewPage::createFixedIssues(QWidget* parent)
{
    // fixed-issues-list (104:2095): Issue rows (52, gaps 2) with the padding 8 in a
    // card, which scrolls inside, when it is higher than 310
    PDFAppCard* card = new PDFAppCard(QMargins(1, 1, 1, 1), 0, parent);
    QWidget* list = new QWidget();
    QVBoxLayout* listLayout = new QVBoxLayout(list);
    listLayout->setContentsMargins(8, 8, 8, 8);
    listLayout->setSpacing(2);

    for (const PDFWhatsNewIssue& issue : m_release.fixedIssues)
    {
        const QString date = issue.date.isValid() ? PDFUpdateStatus::formatDate(issue.date) : QString();
        PDFIssueRow* row = new PDFIssueRow(QStringLiteral("#%1").arg(issue.number), issue.title, issue.description, date, list);
        row->setProperty("pdf4qtIssue", issue.number);
        listLayout->addWidget(row);
        connect(row, &PDFIssueRow::clicked, this, &PDFAppWhatsNewPage::onIssueClicked);
    }

    m_issueScrollArea = new PDFAppScrollArea(list, card);
    m_issueScrollArea->setObjectName(QStringLiteral("issueList"));
    card->getLayout()->addWidget(m_issueScrollArea);

    const int count = int(m_release.fixedIssues.size());
    const int rowHeight = count > 0 ? list->layout()->itemAt(0)->sizeHint().height() : 52;
    const int contentHeight = count * rowHeight + qMax(count - 1, 0) * 2 + 16;
    m_issueScrollArea->setFixedHeight(qMin(contentHeight, MAXIMUM_ISSUE_LIST_HEIGHT - 2));

    PDFAppSectionHeader* header = nullptr;
    m_fixedIssuesSection = PDFAppWhatsNewHelper::createSection(tr("Fixed issues"), card, parent, &header);
    m_fixedIssuesSection->setObjectName(QStringLiteral("fixedIssues"));
    m_viewAllIssuesLink = new PDFTextLink(tr("View all on GitHub"), header);
    header->setTrailingWidget(m_viewAllIssuesLink);
    connect(m_viewAllIssuesLink, &PDFTextLink::clicked, this, &PDFAppWhatsNewPage::onViewAllIssuesClicked);
    m_fixedIssuesSection->setVisible(count > 0);
    return m_fixedIssuesSection;
}

QWidget* PDFAppWhatsNewPage::createLinks(QWidget* parent)
{
    // links-list (104:2164): Link rows (50, gaps 2) with the padding 8
    PDFAppCard* card = new PDFAppCard(QMargins(9, 9, 9, 9), 2, parent);
    const QString oldest = PDFAppWhatsNewHelper::getOldestVersion(*m_catalog);
    QList<PDFLinkRow*> rows;
    rows << PDFAppWhatsNewHelper::addLinkRow(card->getLayout(), tr("Full changelog"), oldest.isEmpty() ? tr("All changes of PDF4QT") : tr("All changes since %1").arg(oldest),
                                             QStringLiteral("file-text"), getChangelogUrl().toString(), card);
    rows << PDFAppWhatsNewHelper::addLinkRow(card->getLayout(), tr("GitHub repository"), tr("Source code and releases"), QStringLiteral("github"), getRepositoryUrl().toString(), card);
    rows << PDFAppWhatsNewHelper::addLinkRow(card->getLayout(), tr("Discussions"), tr("Ask questions, share ideas"), QStringLiteral("message-square"),
                                             QStringLiteral("https://github.com/JakubMelka/PDF4QT/discussions"), card);
    rows << PDFAppWhatsNewHelper::addLinkRow(card->getLayout(), tr("Report an issue"), tr("Tell us about a problem"), QStringLiteral("alert-circle"),
                                             QStringLiteral("https://github.com/JakubMelka/PDF4QT/issues/new"), card);
    rows << PDFAppWhatsNewHelper::addLinkRow(card->getLayout(), tr("Request a feature"), tr("Suggest an improvement"), QStringLiteral("plus-circle"),
                                             QStringLiteral("https://github.com/JakubMelka/PDF4QT/issues/new?labels=enhancement"), card);
    for (PDFLinkRow* row : rows)
    {
        connect(row, &PDFLinkRow::clicked, this, &PDFAppWhatsNewPage::onLinkClicked);
    }

    m_linksSection = PDFAppWhatsNewHelper::createSection(tr("Links"), card, parent);
    m_linksSection->setObjectName(QStringLiteral("links"));
    return m_linksSection;
}

QWidget* PDFAppWhatsNewPage::createThankYou(QWidget* parent)
{
    // thank-you (104:2281): accent/green, padding 14, gap 12, the circle 36 bg/surface
    PDFTintedCard* card = new PDFTintedCard(parent);
    card->setObjectName(QStringLiteral("thankYou"));
    card->setHue(PDFAccentHue::Green);

    QHBoxLayout* layout = new QHBoxLayout(card);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(12);

    PDFIconBadge* circle = new PDFIconBadge(QStringLiteral("heart"), 36, 18, card);
    circle->setColors(PDFThemeColors::BgSurface, PDFThemeColors::AccentRedIcon);
    layout->addWidget(circle, 0, Qt::AlignVCenter);

    QWidget* texts = new QWidget(card);
    QVBoxLayout* textsLayout = new QVBoxLayout(texts);
    textsLayout->setContentsMargins(0, 0, 0, 0);
    textsLayout->setSpacing(2);
    textsLayout->addWidget(PDFAppPageWidgets::createLabel(tr("Thank you!"), QFont::Bold, 14, PDFThemeColors::TextPrimary, texts));
    PDFAppParagraph* description = new PDFAppParagraph(PDFThemeFonts::getFont(QFont::Normal, 12), 16, PDFThemeColors::TextSecondary, texts);
    description->setText(tr("Your feedback, bug reports and feature requests make PDF4QT better."));
    textsLayout->addWidget(description);
    layout->addWidget(texts, 1);

    return card;
}

QList<PDFHighlightCard*> PDFAppWhatsNewPage::getHighlightCards() const
{
    return m_highlightsSection ? m_highlightsSection->findChildren<PDFHighlightCard*>() : QList<PDFHighlightCard*>();
}

QList<PDFToolRow*> PDFAppWhatsNewPage::getToolRows() const
{
    return m_newToolsSection ? m_newToolsSection->findChildren<PDFToolRow*>() : QList<PDFToolRow*>();
}

QList<PDFIssueRow*> PDFAppWhatsNewPage::getIssueRows() const
{
    return m_fixedIssuesSection ? m_fixedIssuesSection->findChildren<PDFIssueRow*>() : QList<PDFIssueRow*>();
}

QList<PDFLinkRow*> PDFAppWhatsNewPage::getLinkRows() const
{
    return m_linksSection ? m_linksSection->findChildren<PDFLinkRow*>() : QList<PDFLinkRow*>();
}

void PDFAppWhatsNewPage::updateStatus()
{
    if (!m_checker || !m_updateCard)
    {
        return;
    }

    const PDFUpdateStatus status = m_checker->getStatus();
    m_updateCard->setStatus(status, m_checker->getCurrentTime());

    if (!m_releaseBadge)
    {
        return;
    }

    if (!m_isRunningRelease)
    {
        m_releaseBadge->setBadge(tr("Previous release"), QStringLiteral("clock"), PDFAccentHue::Neutral);
        m_releaseBadge->show();
    }
    else if (status.state == PDFUpdateStatus::State::UpToDate)
    {
        m_releaseBadge->setBadge(tr("Up to date"), QStringLiteral("check"), PDFAccentHue::Green);
        m_releaseBadge->show();
    }
    else if (status.state == PDFUpdateStatus::State::UpdateAvailable)
    {
        m_releaseBadge->setBadge(tr("Update available"), QStringLiteral("download"), PDFAccentHue::Amber);
        m_releaseBadge->show();
    }
    else
    {
        m_releaseBadge->hide();
    }
}

void PDFAppWhatsNewPage::updatePins()
{
    // The stars of the new tools show the pinned tools of the database (tool_pin)
    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase())
    {
        return;
    }

    const QStringList pinnedTools = PDFToolRepository(context->getDatabase()).getPinnedTools();
    m_isUpdatingPins = true;
    for (PDFToolRow* row : getToolRows())
    {
        row->setPinned(pinnedTools.contains(row->property("pdf4qtToolId").toString()));
    }
    m_isUpdatingPins = false;
}

void PDFAppWhatsNewPage::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);

    // The page reads the database when it is shown; a check is started, when it is due
    QPointer<PDFAppWhatsNewPage> guard(this);
    updateStatus();
    updatePins();
    if (m_checker)
    {
        m_checker->checkIfDue();
    }
}

void PDFAppWhatsNewPage::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && m_release.isValid())
    {
        // The translation of the release of the new language (the shared catalog is not parsed again)
        m_release = m_catalog->getLocalizedRelease(m_release.version, PDFAppLocale::getUiLanguage());
        build();
    }
}

bool PDFAppWhatsNewPage::scrollToAnchor(const QString& anchor)
{
    QWidget* target = nullptr;
    if (anchor == QStringLiteral("new-tools"))
    {
        target = m_newToolsSection;
    }
    else if (anchor == QStringLiteral("fixed-issues"))
    {
        target = m_fixedIssuesSection;
    }
    else if (anchor == QStringLiteral("highlights"))
    {
        target = m_highlightsSection;
    }

    return target && target->isVisibleTo(this) && scrollToWidget(target);
}

void PDFAppWhatsNewPage::onReleaseNotesClicked()
{
    requestNavigation(PDFAppRoutes::releaseNotes(m_release.getDisplayVersion()));
}

void PDFAppWhatsNewPage::onExploreToolsClicked()
{
    scrollToAnchor(QStringLiteral("new-tools"));
}

void PDFAppWhatsNewPage::onToolClicked()
{
    const QString toolId = sender() ? sender()->property("pdf4qtToolId").toString() : QString();
    if (!toolId.isEmpty())
    {
        requestNavigation(PDFAppRoutes::tool(toolId));
    }
}

void PDFAppWhatsNewPage::onToolPinnedChanged(bool isPinned)
{
    PDFAppContext* context = getContext();
    QObject* row = sender();
    if (m_isUpdatingPins || !context || !context->getDatabase() || !row)
    {
        return;
    }

    // The star writes the database; the notification updates the stars of all pages
    const QString toolId = row->property("pdf4qtToolId").toString();
    const PDFDatabaseError error = PDFToolRepository(context->getDatabase()).setPinned(toolId, isPinned);
    if (error.isError())
    {
        qWarning("The tool %s cannot be pinned: %s", qPrintable(toolId), qPrintable(error.toString()));
        updatePins();
    }
}

void PDFAppWhatsNewPage::onIssueClicked()
{
    bool ok = false;
    const int number = sender() ? sender()->property("pdf4qtIssue").toInt(&ok) : 0;
    if (ok && number > 0)
    {
        Q_EMIT externalUrlRequested(getIssueUrl(number));
    }
}

void PDFAppWhatsNewPage::onLinkClicked()
{
    const QString link = PDFAppWhatsNewHelper::getLinkTarget(sender());
    if (!link.isEmpty())
    {
        openLink(link);
    }
}

void PDFAppWhatsNewPage::onVersionActivated(const QVersionNumber& version)
{
    requestNavigation(PDFAppRoutes::whatsNew(PDFWhatsNewRelease::getDisplayVersion(version)));
}

void PDFAppWhatsNewPage::onViewAllIssuesClicked()
{
    Q_EMIT externalUrlRequested(PDFUpdateChecker::getReleaseUrl(m_release.version));
}

void PDFAppWhatsNewPage::onCheckRequested()
{
    if (m_checker)
    {
        m_checker->checkNow();
    }
}

void PDFAppWhatsNewPage::onDownloadRequested()
{
    if (m_checker)
    {
        Q_EMIT externalUrlRequested(m_checker->getStatus().getDownloadUrl());
    }
}

void PDFAppWhatsNewPage::onSettingsRequested()
{
    requestNavigation(PDFAppRoutes::settings(PDFSettingDefinitions::sectionGeneral(), QStringLiteral("check-for-updates")));
}

void PDFAppWhatsNewPage::onCheckerStatusChanged()
{
    updateStatus();
}

void PDFAppWhatsNewPage::onDatabaseChanged(const QString& topic, const QString& key)
{
    if (topic == PDFSettingsRepository::getTopic() && key.startsWith(QStringLiteral("update.")))
    {
        updateStatus();
    }
    else if (topic == PDFToolRepository::getPinTopic())
    {
        updatePins();
    }
}

void PDFAppWhatsNewPage::onEverythingChanged()
{
    updateStatus();
    updatePins();
}

PDFAppReleaseNotesPage::PDFAppReleaseNotesPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent),
    m_catalog(PDFWhatsNewCatalog::getBuiltin()),
    m_runningVersion(PDFWhatsNewCatalog::getRunningVersion())
{
    setObjectName(QStringLiteral("releaseNotesPage"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    if (PDFUpdateChecker* checker = PDFUpdateChecker::getInstance(context))
    {
        m_runningVersion = checker->getRunningVersion();
    }
}

PDFAppReleaseNotesPage::~PDFAppReleaseNotesPage() = default;

void PDFAppReleaseNotesPage::setCatalog(const PDFWhatsNewCatalog& catalog)
{
    m_catalog = std::make_shared<const PDFWhatsNewCatalog>(catalog);
    if (getRoute().isValid())
    {
        setRoute(getRoute());
    }
}

bool PDFAppReleaseNotesPage::setRoute(const PDFAppRoute& route)
{
    const PDFWhatsNewRelease* release = m_catalog->findRelease(PDFWhatsNewParser::parseVersion(route.getSegment(1)));
    if (!release)
    {
        return false;
    }

    BaseClass::setRoute(route);
    m_release = m_catalog->getLocalizedRelease(release->version, PDFAppLocale::getUiLanguage());
    build();

    QPointer<PDFAppReleaseNotesPage> guard(this);
    Q_EMIT titleChanged();
    if (guard && !route.getFragment().isEmpty())
    {
        requestDeferredScroll(-1, route.getFragment());
    }
    return !guard.isNull();
}

QString PDFAppReleaseNotesPage::getTitle() const
{
    return getHeaderTitle();
}

QString PDFAppReleaseNotesPage::getHeaderTitle() const
{
    return tr("PDF4QT %1 release notes").arg(m_release.getDisplayVersion());
}

QList<PDFAppBreadcrumbItem> PDFAppReleaseNotesPage::getHeaderBreadcrumbs() const
{
    // What's New › PDF4QT 1.5.3 › Release notes
    QList<PDFAppBreadcrumbItem> items;
    if (m_release.isValid())
    {
        items.append({ tr("What's New"), PDFAppRoutes::whatsNew() });
        items.append({ tr("PDF4QT %1").arg(m_release.getDisplayVersion()), PDFAppRoutes::whatsNew(m_release.getDisplayVersion()) });
        items.append({ tr("Release notes"), PDFAppRoute() });
    }
    return items;
}

QString PDFAppReleaseNotesPage::getHeaderSubtitle() const
{
    return PDFAppWhatsNewHelper::getReleaseMeta(m_release, m_runningVersion);
}

QVariantMap PDFAppReleaseNotesPage::saveViewState() const
{
    QVariantMap state = BaseClass::saveViewState();
    state.insert(PDFAppWhatsNewPage::getAllVersionsKey(), m_versionHistory && m_versionHistory->isAllShown());
    return state;
}

void PDFAppReleaseNotesPage::restoreViewState(const QVariantMap& state)
{
    if (m_versionHistory)
    {
        m_versionHistory->setAllShown(state.value(PDFAppWhatsNewPage::getAllVersionsKey()).toBool());
    }
    BaseClass::restoreViewState(state);
}

void PDFAppReleaseNotesPage::build()
{
    const bool isAllShown = m_versionHistory && m_versionHistory->isAllShown();
    if (m_content)
    {
        delete m_content.data();
    }

    m_content = new QWidget(this);
    m_content->setObjectName(QStringLiteral("releaseNotesContent"));
    layout()->addWidget(m_content);

    QWidget* grid = m_content;

    m_markdownView = new PDFAppMarkdownView(grid);
    m_markdownView->setObjectName(QStringLiteral("releaseNotes"));
    m_markdownView->setResourceBase(PDFWhatsNewCatalog::getResourceDirectory() + QStringLiteral("/"));
    m_markdownView->setMarkdown(m_release.releaseNotes.isEmpty() ? m_release.overview : m_release.releaseNotes);
    m_markdownView->setAccessibleName(getHeaderTitle());
    connect(m_markdownView, &PDFAppMarkdownView::linkActivated, this, &PDFAppReleaseNotesPage::onMarkdownLinkActivated);

    QWidget* sideColumn = PDFAppWhatsNewHelper::createColumn(grid);
    QVBoxLayout* sideLayout = static_cast<QVBoxLayout*>(sideColumn->layout());

    m_versionHistory = new PDFAppVersionHistory(sideColumn);
    m_versionHistory->setReleases(m_catalog->getReleases(), m_release.version, m_runningVersion);
    m_versionHistory->setAllShown(isAllShown);
    connect(m_versionHistory, &PDFAppVersionHistory::versionActivated, this, &PDFAppReleaseNotesPage::onVersionActivated);
    sideLayout->addWidget(m_versionHistory);

    PDFAppCard* card = new PDFAppCard(QMargins(9, 9, 9, 9), 2, sideColumn);
    const QString version = m_release.getDisplayVersion();
    const QString oldest = PDFAppWhatsNewHelper::getOldestVersion(*m_catalog);
    QList<PDFLinkRow*> rows;
    rows << PDFAppWhatsNewHelper::addLinkRow(card->getLayout(), tr("Release on GitHub"), tr("Downloads and assets of %1").arg(version), QStringLiteral("github"),
                                             PDFUpdateChecker::getReleaseUrl(m_release.version).toString(), card);
    rows << PDFAppWhatsNewHelper::addLinkRow(card->getLayout(), tr("Full changelog"), oldest.isEmpty() ? tr("All changes of PDF4QT") : tr("All changes since %1").arg(oldest),
                                             QStringLiteral("file-text"), PDFAppWhatsNewPage::getChangelogUrl().toString(), card);
    rows << PDFAppWhatsNewHelper::addLinkRow(card->getLayout(), tr("Back to What's New"), tr("Highlights, new tools and fixed issues"), QStringLiteral("gift"),
                                             PDFAppRoutes::whatsNew(version).toString(), card);
    for (PDFLinkRow* row : rows)
    {
        connect(row, &PDFLinkRow::clicked, this, &PDFAppReleaseNotesPage::onLinkClicked);
    }
    m_linksCard = card;
    sideLayout->addWidget(PDFAppWhatsNewHelper::createSection(tr("Links"), card, sideColumn));

    PDFAppColumns::createInContent(m_content, m_markdownView, sideColumn);
    m_content->show();
}

QList<PDFLinkRow*> PDFAppReleaseNotesPage::getLinkRows() const
{
    return m_linksCard ? m_linksCard->findChildren<PDFLinkRow*>() : QList<PDFLinkRow*>();
}

bool PDFAppReleaseNotesPage::scrollToAnchor(const QString& anchor)
{
    if (!m_markdownView)
    {
        return false;
    }

    const int position = m_markdownView->getAnchorPosition(anchor);
    return position >= 0 && scrollToWidget(m_markdownView, position);
}

void PDFAppReleaseNotesPage::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && m_release.isValid())
    {
        // The translation of the release of the new language (the shared catalog is not parsed again)
        m_release = m_catalog->getLocalizedRelease(m_release.version, PDFAppLocale::getUiLanguage());
        build();
    }
}

void PDFAppReleaseNotesPage::onMarkdownLinkActivated(const QString& target, PDFAppMarkdownLinkKind kind)
{
    Q_UNUSED(kind);
    openLink(target);
}

void PDFAppReleaseNotesPage::onVersionActivated(const QVersionNumber& version)
{
    requestNavigation(PDFAppRoutes::releaseNotes(PDFWhatsNewRelease::getDisplayVersion(version)));
}

void PDFAppReleaseNotesPage::onLinkClicked()
{
    const QString link = PDFAppWhatsNewHelper::getLinkTarget(sender());
    if (!link.isEmpty())
    {
        openLink(link);
    }
}

PDFAppPage* PDFAppWhatsNewPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppWhatsNewPage(context, parent);
}

PDFAppPage* PDFAppReleaseNotesPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppReleaseNotesPage(context, parent);
}

}   // namespace pdf
