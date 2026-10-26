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

#include "pdfapphomepage.h"
#include "pdfapproute.h"
#include "pdftoolrepository.h"
#include "pdfapptoolpresentation.h"

#include "pdfdocumenttoolcatalog.h"

#include "pdfcomponentcards.h"
#include "pdfcomponentrows.h"
#include "pdfthemefonts.h"

#include <QBoxLayout>
#include <QFrame>
#include <QMenu>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the page Home
class PDFAppHomePageHelper
{
public:
    PDFAppHomePageHelper() = delete;

    /// Property of a widget holding its path / tool id / link
    static constexpr const char* PATH_PROPERTY = "pdf4qtPath";
    static constexpr const char* TOOL_PROPERTY = "pdf4qtToolId";

    /// Tiles: five in a row at 1166 px (224), fewer in a narrow page
    static constexpr int TILE_MINIMUM_WIDTH = 200;

    /// Divider of the support list (border/divider, 1 px)
    static QWidget* createDivider(QWidget* parent)
    {
        // A horizontal line of a QFrame: the style draws it border/divider
        QFrame* divider = new QFrame(parent);
        divider->setFrameShape(QFrame::HLine);
        divider->setFixedHeight(1);
        return divider;
    }
};

PDFAppHomePage::PDFAppHomePage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent)
{
    setObjectName(QStringLiteral("homePage"));

    // workspace-pane 28:4: Quick Actions, the dashboard of three columns, gap 24
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(this, 24);
    layout->addWidget(createQuickActions(this));

    m_columns = new PDFAppColumns(this);
    m_columns->setObjectName(QStringLiteral("dashboard"));
    QWidget* recentFiles = m_columns->addColumn(0, 397, 300);
    QWidget* learn = m_columns->addColumn(0, 360, 300);
    QWidget* tools = m_columns->addColumn(0, 361, 300);
    m_columns->getColumnLayout(0)->addWidget(createRecentFiles(recentFiles));
    m_columns->getColumnLayout(1)->addWidget(createLearnAndSupport(learn));
    m_columns->getColumnLayout(2)->addWidget(createRecentTools(tools));
    layout->addWidget(m_columns);
    layout->addStretch(1);
}

PDFAppHomePage::~PDFAppHomePage() = default;

bool PDFAppHomePage::setRoute(const PDFAppRoute& route)
{
    // /home has no query and no anchor (the route table refuses them as well)
    if (!route.getQueryItems().isEmpty() || !route.getFragment().isEmpty())
    {
        return false;
    }
    return BaseClass::setRoute(route);
}

QString PDFAppHomePage::getTitle() const
{
    return tr("Home");
}

QString PDFAppHomePage::getHeaderTitle() const
{
    return tr("Welcome to PDF4QT");
}

QString PDFAppHomePage::getHeaderSubtitle() const
{
    return tr("A fast, modern and open-source PDF editor for Windows, Linux and macOS.");
}

QString PDFAppHomePage::getHeaderSubtitleAccent() const
{
    //: The stressed word of the subtitle of Home (it must be a part of the translated subtitle)
    return tr("PDF");
}

QStringList PDFAppHomePage::getQuickActionToolIds()
{
    // Quick Actions of 28:4 between Open PDF and All Tools
    return { QStringLiteral("convert.images-to-pdf"), QStringLiteral("convert.pdf-to-images"), QStringLiteral("convert.ocr"), QStringLiteral("edit.content"),
             QStringLiteral("pages.merge"), QStringLiteral("pages.manage"), QStringLiteral("optimize.compress"), QStringLiteral("protect.password"),
             QStringLiteral("protect.sign"), QStringLiteral("protect.sanitize"), QStringLiteral("review.measure") };
}

QString PDFAppHomePage::getQuickActionDescription(const QString& toolId, const QString& catalogDescription)
{
    if (toolId == QLatin1String("protect.sanitize"))
    {
        //: Short description of the tool Remove Hidden Data in its tile of Home
        return tr("Strip metadata");
    }
    return catalogDescription;
}

QWidget* PDFAppHomePage::createQuickActions(QWidget* parent)
{
    // quick-actions-section: the title Bold 16, the tiles 88 px high, gaps 12
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("quickActions"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, 12);
    PDFAppParagraph* title = PDFAppPageWidgets::createText(tr("Quick Actions"), PDFThemeFonts::getFont(QFont::Bold, 16), 19, PDFThemeColors::TextPrimary, section);
    title->setObjectName(QStringLiteral("quickActionsTitle"));
    layout->addWidget(title);

    PDFAppGrid* grid = new PDFAppGrid(PDFAppHomePageHelper::TILE_MINIMUM_WIDTH, 5, 12, 12, section);
    grid->setObjectName(QStringLiteral("tiles"));
    m_tiles = grid;

    PDFQuickActionTile* openTile = new PDFQuickActionTile(tr("Open PDF"), tr("Browse and open a document"), QStringLiteral("folder-open"), PDFAccentHue::Blue, grid);
    openTile->setObjectName(QStringLiteral("tileOpen"));
    openTile->setProperty(PDFAppHomePageHelper::TOOL_PROPERTY, PDFAppRoutes::open().toString());
    grid->addWidget(openTile);
    m_quickActionTiles << openTile;

    for (const QString& toolId : getQuickActionToolIds())
    {
        const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(toolId);
        if (!entry)
        {
            continue;
        }

        PDFQuickActionTile* tile = new PDFQuickActionTile(entry->getTitle(), getQuickActionDescription(toolId, entry->getDescription()), entry->getIconName(),
                                                          PDFAppToolPresentation::getHue(entry->getHue()), grid);
        tile->setObjectName(QStringLiteral("tile-%1").arg(toolId));
        tile->setProperty(PDFAppHomePageHelper::TOOL_PROPERTY, PDFAppRoutes::tool(toolId).toString());
        grid->addWidget(tile);
        m_quickActionTiles << tile;
    }

    PDFQuickActionTile* allTools = new PDFQuickActionTile(tr("All Tools"), tr("View complete catalog"), QStringLiteral("tools"), PDFAccentHue::Neutral, grid);
    allTools->setObjectName(QStringLiteral("tileAllTools"));
    allTools->setProperty(PDFAppHomePageHelper::TOOL_PROPERTY, PDFAppRoutes::tools().toString());
    grid->addWidget(allTools);
    m_quickActionTiles << allTools;

    for (const QPointer<PDFQuickActionTile>& tile : std::as_const(m_quickActionTiles))
    {
        connect(tile, &PDFQuickActionTile::clicked, this, &PDFAppHomePage::onTileClicked);
    }

    layout->addWidget(grid);
    return section;
}

QWidget* PDFAppHomePage::createRecentFiles(QWidget* parent)
{
    // recent-files: the header with View all, the list card (padding 8, rows without gaps)
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("recentFiles"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, 12);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Recent Files"), createLink(tr("View all"), PDFAppRoutes::recent(), section), section));

    m_recentFilesCard = PDFAppPageWidgets::createCard(8, 0, section);
    m_recentFilesCard->setObjectName(QStringLiteral("recentFilesList"));
    m_recentFilesLayout = qobject_cast<QVBoxLayout*>(m_recentFilesCard->layout());

    // The small empty state (the scale of the Recent empty state 955:142865)
    m_recentFilesEmpty = new PDFAppEmptyState(PDFAppEmptyState::Size::Small, QStringLiteral("clock"), m_recentFilesCard);
    m_recentFilesEmpty->setObjectName(QStringLiteral("recentFilesEmpty"));
    m_recentFilesEmpty->setTexts(tr("No recent documents"), tr("Documents you open appear here."));
    m_recentFilesLayout->addWidget(m_recentFilesEmpty);

    m_dropZone = new PDFAppDropZone(PDFAppDropZone::Variant::Compact, m_recentFilesCard);
    m_recentFilesLayout->addWidget(m_dropZone);
    m_recentFilesLayout->addStretch(1);
    layout->addWidget(m_recentFilesCard, 1);
    return section;
}

QWidget* PDFAppHomePage::createLearnAndSupport(QWidget* parent)
{
    // learn-and-support: Learn & Resources and Support, gap 20
    QWidget* column = new QWidget(parent);
    column->setObjectName(QStringLiteral("learnAndSupport"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(column, 20);

    QWidget* learn = new QWidget(column);
    learn->setObjectName(QStringLiteral("learn"));
    QVBoxLayout* learnLayout = PDFAppPageWidgets::createColumn(learn, 12);
    learnLayout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Learn & Resources"), createLink(tr("View all resources"), PDFAppRoutes::support(), learn), learn));

    QWidget* cards = new QWidget(learn);
    cards->setObjectName(QStringLiteral("resourceCards"));
    QHBoxLayout* cardsLayout = new QHBoxLayout(cards);
    cardsLayout->setContentsMargins(0, 0, 0, 0);
    cardsLayout->setSpacing(12);

    struct Resource
    {
        QString title;
        QString description;
        const char* iconName;
        PDFAccentHue hue;
        PDFAppRoute route;
        const char* objectName;
    };

    const Resource resources[] = {
        { tr("Video Tutorials"), tr("Watch step-by-step guides"), "play-circle", PDFAccentHue::Violet, PDFAppRoutes::support(), "resourceVideos" },
        { tr("User Manual"), tr("Detailed documentation"), "book-open", PDFAccentHue::Green, PDFAppRoutes::help(PDFAppRoutes::getDefaultHelpTopic()), "resourceManual" },
        { tr("Getting Started"), tr("New to PDF4QT? Start here"), "help-circle", PDFAccentHue::Amber, PDFAppRoutes::help(PDFAppRoutes::getDefaultHelpTopic()), "resourceGettingStarted" }
    };

    for (const Resource& resource : resources)
    {
        PDFResourceCard* card = new PDFResourceCard(resource.title, resource.description, QString::fromLatin1(resource.iconName), resource.hue, cards);
        card->setObjectName(QString::fromLatin1(resource.objectName));
        withLink(card, resource.route.toString());
        cardsLayout->addWidget(card, 1);
        m_resourceCards << card;
    }
    learnLayout->addWidget(cards);
    layout->addWidget(learn);

    // support: the list card with dividers (padding 6)
    QWidget* support = new QWidget(column);
    support->setObjectName(QStringLiteral("support"));
    QVBoxLayout* supportLayout = PDFAppPageWidgets::createColumn(support, 12);
    supportLayout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Support"), nullptr, support));
    // support-list: padding 6 with the border inside of it (164 = 6 + 3 x 50 + 2 + 6)
    m_supportCard = PDFAppPageWidgets::createCard(5, 0, support);
    m_supportCard->setObjectName(QStringLiteral("supportList"));
    QBoxLayout* cardLayout = qobject_cast<QBoxLayout*>(m_supportCard->layout());

    struct SupportLink
    {
        QString title;
        QString subtitle;
        const char* iconName;
        PDFThemeColors::Color iconColor;
        QString link;
        const char* objectName;
    };

    const SupportLink links[] = {
        { tr("Report Issue"), tr("Tell us about a problem"), "alert-circle", PDFThemeColors::IconDanger, PDFAppProjectLinks::getNewIssue().toString(), "reportIssue" },
        { tr("Discuss on GitHub"), tr("Join the community"), "github", PDFThemeColors::IconStrong, PDFAppProjectLinks::getDiscussions().toString(), "discussOnGitHub" },
        { tr("Become a Sponsor"), tr("Support the development of PDF4QT"), "heart", PDFThemeColors::AccentRedIcon, PDFAppRoutes::sponsor().toString(), "becomeSponsor" }
    };

    bool isFirst = true;
    for (const SupportLink& link : links)
    {
        if (!isFirst)
        {
            cardLayout->addWidget(PDFAppHomePageHelper::createDivider(m_supportCard));
        }
        isFirst = false;

        PDFLinkRow* row = new PDFLinkRow(link.title, link.subtitle, QString::fromLatin1(link.iconName), m_supportCard);
        row->setObjectName(QString::fromLatin1(link.objectName));
        row->setIconColor(link.iconColor);
        withLink(row, link.link);
        cardLayout->addWidget(row);
        m_supportRows << row;
    }
    cardLayout->addStretch(1);
    supportLayout->addWidget(m_supportCard, 1);
    layout->addWidget(support, 1);
    return column;
}

QWidget* PDFAppHomePage::createRecentTools(QWidget* parent)
{
    // recent-tools: the header, the list card (padding 8, gap 2)
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("recentTools"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, 12);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Recent Tools"), nullptr, section));

    m_recentToolsCard = PDFAppPageWidgets::createCard(8, 2, section);
    m_recentToolsCard->setObjectName(QStringLiteral("recentToolsList"));
    m_recentToolsLayout = qobject_cast<QVBoxLayout*>(m_recentToolsCard->layout());
    m_recentToolsEmpty = new PDFAppEmptyState(PDFAppEmptyState::Size::Small, QStringLiteral("tools"), m_recentToolsCard);
    m_recentToolsEmpty->setObjectName(QStringLiteral("recentToolsEmpty"));
    m_recentToolsEmpty->setTexts(tr("No recent tools"), tr("Tools you use appear here."));
    m_recentToolsLayout->addWidget(m_recentToolsEmpty);
    m_recentToolsLayout->addStretch(1);
    layout->addWidget(m_recentToolsCard, 1);
    return section;
}

QList<PDFQuickActionTile*> PDFAppHomePage::getQuickActionTiles() const
{
    QList<PDFQuickActionTile*> result;
    for (const QPointer<PDFQuickActionTile>& tile : m_quickActionTiles)
    {
        if (tile)
        {
            result << tile.data();
        }
    }
    return result;
}

QList<PDFRecentFileRow*> PDFAppHomePage::getRecentFileRows() const
{
    QList<PDFRecentFileRow*> result;
    for (const QPointer<PDFRecentFileRow>& row : m_recentFileRows)
    {
        if (row)
        {
            result << row.data();
        }
    }
    return result;
}

QList<PDFResourceCard*> PDFAppHomePage::getResourceCards() const
{
    QList<PDFResourceCard*> result;
    for (const QPointer<PDFResourceCard>& card : m_resourceCards)
    {
        if (card)
        {
            result << card.data();
        }
    }
    return result;
}

QList<PDFLinkRow*> PDFAppHomePage::getSupportRows() const
{
    QList<PDFLinkRow*> result;
    for (const QPointer<PDFLinkRow>& row : m_supportRows)
    {
        if (row)
        {
            result << row.data();
        }
    }
    return result;
}

QList<PDFRecentToolRow*> PDFAppHomePage::getRecentToolRows() const
{
    QList<PDFRecentToolRow*> result;
    for (const QPointer<PDFRecentToolRow>& row : m_recentToolRows)
    {
        if (row)
        {
            result << row.data();
        }
    }
    return result;
}

void PDFAppHomePage::refresh()
{
    refreshRecentFiles();
    refreshRecentTools();
}

bool PDFAppHomePage::isTopicRelevant(const QString& topic) const
{
    return BaseClass::isTopicRelevant(topic) || topic == PDFToolRepository::getUsageTopic();
}

void PDFAppHomePage::refreshRecentFiles()
{
    PDFAppContext* context = getContext();
    std::vector<PDFRecentFile> files;
    if (context && context->getDatabase())
    {
        PDFRecentQuery query;
        query.limit = MAXIMUM_RECENT_FILES;
        files = context->getRecent().getFiles(query);
    }

    const qint64 now = getNow();
    QStringList signatureParts;
    for (const PDFRecentFile& file : files)
    {
        // The times, not their texts: the minute ticker updates the texts (O25)
        signatureParts << QStringLiteral("%1|%2|%3|%4").arg(file.path, file.name).arg(file.lastOpened).arg(file.pinned);
    }
    const QString signature = signatureParts.join(QChar(0x1F));

    if (signature != m_recentFilesSignature || m_recentFileRows.size() != qsizetype(files.size()))
    {
        m_recentFilesSignature = signature;
        for (const QPointer<PDFRecentFileRow>& row : std::as_const(m_recentFileRows))
        {
            if (row)
            {
                row->hide();
                row->deleteLater();
            }
        }
        m_recentFileRows.clear();

        int index = 0;
        for (const PDFRecentFile& file : files)
        {
            // recent-file-row: the name, the folder, the time and the menu (no star on Home)
            PDFRecentFileRow* row = new PDFRecentFileRow(PDFAppHomeFormat::toDisplayText(file.name), PDFAppHomeFormat::toDisplayPath(file.folder), QString(), m_recentFilesCard);
            PDFAppHomeFormat::setTimeText(row, PDFAppHomeFormat::TimeText::Time, file.lastOpened, now);
            row->setObjectName(QStringLiteral("recentFileRow"));
            row->setStarVisible(false);
            row->setPinned(file.pinned);
            row->setProperty(PDFAppHomePageHelper::PATH_PROPERTY, file.path);

            PDFAppHomeItem item;
            item.kind = PDFAppHomeItemKind::File;
            item.path = file.path;
            item.name = file.name;
            item.isPinned = file.pinned;
            row->setMenu(createItemMenu(item, row));
            connect(row, &PDFRecentFileRow::clicked, this, &PDFAppHomePage::onRecentFileClicked);

            m_recentFilesLayout->insertWidget(index++, row);
            m_recentFileRows << row;
            requestPathInfo(file.path);
            updateFileStatus(row);
        }
    }

    if (m_recentFilesEmpty)
    {
        m_recentFilesEmpty->setVisible(files.empty());
    }
}

void PDFAppHomePage::refreshRecentTools()
{
    PDFAppContext* context = getContext();
    std::vector<PDFToolUsage> usage;
    if (context && context->getDatabase())
    {
        usage = context->getTools().getUsage(MAXIMUM_RECENT_TOOLS);
    }

    const qint64 now = getNow();
    QStringList signatureParts;
    for (const PDFToolUsage& tool : usage)
    {
        signatureParts << QStringLiteral("%1|%2").arg(tool.toolId).arg(tool.lastUsed);
    }
    const QString signature = signatureParts.join(QChar(0x1F));
    if (signature == m_recentToolsSignature && m_recentToolRows.size() == qsizetype(usage.size()))
    {
        return;
    }
    m_recentToolsSignature = signature;

    for (const QPointer<PDFRecentToolRow>& row : std::as_const(m_recentToolRows))
    {
        if (row)
        {
            row->hide();
            row->deleteLater();
        }
    }
    m_recentToolRows.clear();

    int index = 0;
    for (const PDFToolUsage& tool : usage)
    {
        // Unknown ids are skipped by the repository; the catalog is checked again
        const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(tool.toolId);
        if (!entry)
        {
            continue;
        }

        PDFRecentToolRow* row = new PDFRecentToolRow(entry->getTitle(), QString(), entry->getIconName(), PDFAppToolPresentation::getHue(entry->getHue()), m_recentToolsCard);
        PDFAppHomeFormat::setTimeText(row, PDFAppHomeFormat::TimeText::Time, tool.lastUsed, now);
        row->setObjectName(QStringLiteral("recentToolRow"));
        row->setProperty(PDFAppHomePageHelper::TOOL_PROPERTY, PDFAppRoutes::tool(entry->getId()).toString());
        connect(row, &PDFRecentToolRow::clicked, this, &PDFAppHomePage::onRecentToolClicked);
        m_recentToolsLayout->insertWidget(index++, row);
        m_recentToolRows << row;
    }

    if (m_recentToolsEmpty)
    {
        m_recentToolsEmpty->setVisible(m_recentToolRows.isEmpty());
    }
}

void PDFAppHomePage::onPathInfoChanged(const QString& path)
{
    for (const QPointer<PDFRecentFileRow>& row : std::as_const(m_recentFileRows))
    {
        if (row && row->property(PDFAppHomePageHelper::PATH_PROPERTY).toString() == path)
        {
            updateFileStatus(row);
        }
    }
}

void PDFAppHomePage::updateFileStatus(PDFRecentFileRow* row)
{
    const std::optional<PDFAppFolderInfo> info = getPathInfo(row->property(PDFAppHomePageHelper::PATH_PROPERTY).toString());
    const bool isMissing = info && info->pdfCount != -2 && !info->isFile;
    row->setStatus(isMissing ? PDFRecentFileRow::Status::Missing : PDFRecentFileRow::Status::Available);
}

void PDFAppHomePage::onTileClicked()
{
    // The tiles do not identify themselves in the signal: the route is their property
    const QObject* tile = sender();
    if (!tile)
    {
        return;
    }

    const PDFAppRoute route = PDFAppRoute::parse(tile->property(PDFAppHomePageHelper::TOOL_PROPERTY).toString());
    if (route.isValid())
    {
        requestNavigation(route);
    }
}

void PDFAppHomePage::onRecentFileClicked()
{
    const QObject* row = sender();
    if (row)
    {
        openFile(row->property(PDFAppHomePageHelper::PATH_PROPERTY).toString());
    }
}

void PDFAppHomePage::onRecentToolClicked()
{
    onTileClicked();
}

PDFAppPage* PDFAppHomePageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppHomePage(context, parent);
}

}   // namespace pdf
