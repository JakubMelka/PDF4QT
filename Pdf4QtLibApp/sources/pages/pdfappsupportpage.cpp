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

#include "pdfappsupportpage.h"
#include "pdfappcontext.h"
#include "pdfapptab.h"
#include "pdfappworkspace.h"
#include "pdfdatabasenotifier.h"
#include "pdftoolrepository.h"

#include "pdfaccenthue.h"
#include "pdfcomponentcards.h"
#include "pdfthemefonts.h"
#include "pdfwidgetstyleproperties.h"

#include <QHBoxLayout>
#include <QPixmap>
#include <QShowEvent>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the page Support
class PDFAppSupportPageHelper
{
public:
    PDFAppSupportPageHelper() = delete;

    /// Quick action tiles: four in a row at 1166 px (282.5), fewer in a narrow page
    static constexpr int TILE_MINIMUM_WIDTH = 220;

    /// Video cards: four in a row at 1132 px (266.5), fewer in a narrow page
    static constexpr int VIDEO_MINIMUM_WIDTH = 220;

    /// The header of the tutorials has the search below the texts in a narrower card
    static constexpr int HEADER_THRESHOLD = 760;

    /// Width of the search field of the tutorials (tutorial-search)
    static constexpr int SEARCH_WIDTH = 280;

    /// Text of a button: '&' of a text is not a mnemonic
    static QString toButtonText(const QString& text)
    {
        return QString(text).replace(QLatin1Char('&'), QLatin1String("&&"));
    }

    static QVBoxLayout* createColumn(QWidget* widget, int spacing)
    {
        QVBoxLayout* layout = new QVBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(spacing);
        return layout;
    }
};

PDFAppSupportPage::PDFAppSupportPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent)
{
    setObjectName(QStringLiteral("supportPage"));

    // support-grid 116:2460: quick actions and the tutorials, gap 24
    QVBoxLayout* layout = PDFAppSupportPageHelper::createColumn(this, 24);
    layout->addWidget(createQuickActions(this));
    layout->addWidget(createTutorials(this));
    layout->addStretch(1);

    if (context && context->getNotifier())
    {
        connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppSupportPage::onDatabaseChanged);
        connect(context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppSupportPage::onEverythingChanged);
    }

    refreshUsedTools();
    applyFilter();
}

PDFAppSupportPage::~PDFAppSupportPage() = default;

bool PDFAppSupportPage::setRoute(const PDFAppRoute& route)
{
    const QString topic = route.getQueryValue(QStringLiteral("topic"));
    const QString searchText = route.getQueryValue(QStringLiteral("q"));

    // The route table validates the values too; the page never shows an unknown topic
    if ((!topic.isEmpty() && !PDFSupportVideoCatalog::getRouteTopicIds().contains(topic)) || searchText.size() > PDFSupportVideoCatalog::MAXIMUM_SEARCH_LENGTH ||
        !route.getFragment().isEmpty())
    {
        return false;
    }

    m_topic = topic;
    m_searchText = searchText;
    if (m_searchField && m_searchField->text() != searchText)
    {
        m_searchField->setText(searchText);
    }

    applyFilter();
    return BaseClass::setRoute(route);
}

QString PDFAppSupportPage::getTitle() const
{
    return tr("Support");
}

QString PDFAppSupportPage::getHeaderTitle() const
{
    return tr("Support");
}

QString PDFAppSupportPage::getHeaderSubtitle() const
{
    return tr("Get help, watch video tutorials and join the community.");
}

PDFAppRoute PDFAppSupportPage::createRoute(const QString& topic, const QString& searchText)
{
    return PDFAppRoutes::support(topic, searchText.left(PDFSupportVideoCatalog::MAXIMUM_SEARCH_LENGTH));
}

QString PDFAppSupportPage::sanitizeSearchText(const QString& topic, const QString& text)
{
    // Control characters and separators of lines never belong to a route (a pasted
    // text), lone surrogates have no UTF-8 encoding
    QString result;
    result.reserve(qMin(text.size(), qsizetype(PDFSupportVideoCatalog::MAXIMUM_SEARCH_LENGTH)));
    for (qsizetype i = 0; i < text.size() && result.size() < PDFSupportVideoCatalog::MAXIMUM_SEARCH_LENGTH; ++i)
    {
        const QChar character = text[i];
        const char16_t code = character.unicode();
        if (character.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate())
        {
            if (result.size() + 2 > PDFSupportVideoCatalog::MAXIMUM_SEARCH_LENGTH)
            {
                break;
            }
            result += character;
            result += text[++i];
        }
        else if (character.isSurrogate())
        {
            continue;
        }
        else if (code < 0x20 || (code >= 0x7F && code <= 0x9F) || code == 0x2028 || code == 0x2029)
        {
            result += QLatin1Char(' ');
        }
        else
        {
            result += character;
        }
    }

    // The encoded route has a limit of its length (a search of many non-Latin characters)
    while (!result.isEmpty() && !createRoute(topic, result).isValid())
    {
        result.chop(result.size() >= 2 && result.back().isLowSurrogate() ? 2 : 1);
    }
    return result;
}

void PDFAppSupportPage::setFilter(const QString& topic, const QString& searchText)
{
    const QString validTopic = PDFSupportVideoCatalog::getRouteTopicIds().contains(topic) ? topic : QString();
    const QString validText = sanitizeSearchText(validTopic, searchText);
    if (validTopic == m_topic && validText == m_searchText)
    {
        return;
    }

    m_topic = validTopic;
    m_searchText = validText;
    if (m_searchField && sanitizeSearchText(validTopic, m_searchField->text()) != validText)
    {
        m_searchField->setText(validText);
    }

    applyFilter();
    updateRouteInHistory();
}

void PDFAppSupportPage::refreshUsedTools()
{
    QSet<QString> usedToolIds;
    PDFAppContext* context = getContext();
    if (context && context->getDatabase())
    {
        // Unknown tool ids (rows of another version) are skipped by the repository
        for (const QString& toolId : context->getTools().getUsedTools())
        {
            usedToolIds.insert(toolId);
        }
    }

    if (usedToolIds == m_usedToolIds)
    {
        return;
    }

    m_usedToolIds = usedToolIds;
    updateChips();
    applyFilter();
}

QList<PDFQuickActionTile*> PDFAppSupportPage::getQuickActions() const
{
    QList<PDFQuickActionTile*> tiles;
    for (const QPointer<PDFQuickActionTile>& tile : m_quickActions)
    {
        if (tile)
        {
            tiles.append(tile.data());
        }
    }
    return tiles;
}

QPushButton* PDFAppSupportPage::getChip(const QString& topic) const
{
    auto it = m_chips.find(topic);
    return it != m_chips.end() ? it->second.data() : nullptr;
}

QList<QPushButton*> PDFAppSupportPage::getChips() const
{
    // In the order of the page: All, For you, the topics
    QList<QPushButton*> chips;
    QStringList order;
    order << QString::fromLatin1(ALL_TOPIC) << PDFSupportVideoCatalog::getRouteTopicIds();
    for (const QString& topic : std::as_const(order))
    {
        if (QPushButton* chip = getChip(topic))
        {
            chips.append(chip);
        }
    }
    return chips;
}

QList<PDFVideoCard*> PDFAppSupportPage::getVideoCards() const
{
    QList<PDFVideoCard*> cards;
    for (const PDFSupportVideo& video : PDFSupportVideoCatalog::getVideos())
    {
        if (PDFVideoCard* card = getVideoCard(video.getId()))
        {
            cards.append(card);
        }
    }
    return cards;
}

QList<PDFVideoCard*> PDFAppSupportPage::getVisibleVideoCards() const
{
    QList<PDFVideoCard*> cards;
    for (PDFVideoCard* card : getVideoCards())
    {
        if (!card->isHidden())
        {
            cards.append(card);
        }
    }
    return cards;
}

PDFVideoCard* PDFAppSupportPage::getVideoCard(const QString& videoId) const
{
    auto it = m_videoCards.find(videoId);
    return it != m_videoCards.end() ? it->second.data() : nullptr;
}

void PDFAppSupportPage::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);

    // The used tools are read, when the page is shown (nothing is cached)
    if (!event->spontaneous())
    {
        refreshUsedTools();
    }
}

QWidget* PDFAppSupportPage::createQuickActions(QWidget* parent)
{
    struct QuickAction
    {
        QString title;
        QString description;
        const char* iconName;
        PDFAccentHue hue;
        QString link;
        const char* objectName;
    };

    const QuickAction actions[] = {
        { tr("Report an issue"), tr("Found a bug? Tell us on GitHub"), "alert-circle", PDFAccentHue::Red, PDFAppProjectLinks::getNewIssue().toString(), "reportIssueTile" },
        { tr("Request a feature"), tr("Suggest an improvement"), "plus-circle", PDFAccentHue::Amber, PDFAppProjectLinks::getNewIssue().toString(), "requestFeatureTile" },
        { tr("Read the manual"), tr("Documentation and guides"), "book-open", PDFAccentHue::Blue, PDFAppRoutes::help(PDFAppRoutes::getDefaultHelpTopic()).toString(), "manualTile" },
        { tr("Join the discussion"), tr("Ask questions, share ideas"), "message-square", PDFAccentHue::Green, PDFAppProjectLinks::getDiscussions().toString(), "discussionTile" }
    };

    // quick-actions 117:2421: four tiles, gap 12
    m_quickActionGrid = new PDFAppGrid(PDFAppSupportPageHelper::TILE_MINIMUM_WIDTH, 4, 12, 12, parent);
    m_quickActionGrid->setObjectName(QStringLiteral("quickActions"));
    for (const QuickAction& action : actions)
    {
        PDFQuickActionTile* tile = new PDFQuickActionTile(action.title, action.description, QString::fromLatin1(action.iconName), action.hue, m_quickActionGrid);
        tile->setObjectName(QString::fromLatin1(action.objectName));
        PDFAppPageWidgets::setLink(tile, action.link);
        connect(tile, &PDFQuickActionTile::clicked, this, &PDFAppSupportPage::onQuickActionClicked);
        m_quickActionGrid->addWidget(tile);
        m_quickActions.append(tile);
    }
    return m_quickActionGrid;
}

QWidget* PDFAppSupportPage::createTutorials(QWidget* parent)
{
    using Helper = PDFAppSupportPageHelper;

    // tutorials 117:2495: card, padding 16, gap 14
    QWidget* card = PDFAppPageWidgets::createCard(16, 14, parent);
    card->setObjectName(QStringLiteral("tutorials"));
    QBoxLayout* cardLayout = qobject_cast<QBoxLayout*>(card->layout());

    // header: the texts (Bold 15, Regular 12 text/tertiary, gap 3) and the
    // search with the channel button (gap 10)
    QWidget* texts = new QWidget();
    QVBoxLayout* textsLayout = Helper::createColumn(texts, 3);
    textsLayout->addWidget(PDFAppPageWidgets::createText(tr("Video tutorials"), PDFThemeFonts::getFont(PDFThemeFonts::Role::Title), 18, PDFThemeColors::TextPrimary, texts));
    textsLayout->addWidget(PDFAppPageWidgets::createText(tr("Short videos organized by topic. The tags on each card open related tutorials."), PDFThemeFonts::getFont(PDFThemeFonts::Role::Small), 15,
                                      PDFThemeColors::TextTertiary, texts));

    QWidget* actions = new QWidget();
    PDFAppFlowLayout* actionsLayout = new PDFAppFlowLayout(actions, 10, 10);
    m_searchField = new PDFLineEdit(actions);
    m_searchField->setObjectName(QStringLiteral("tutorialSearch"));
    m_searchField->setSearchField();
    m_searchField->setPlaceholderText(tr("Search tutorials…"));
    m_searchField->setAccessibleName(tr("Search tutorials"));
    m_searchField->setMaxLength(PDFSupportVideoCatalog::MAXIMUM_SEARCH_LENGTH);
    m_searchField->setClearButtonEnabled(true);
    m_searchField->setFixedWidth(Helper::SEARCH_WIDTH);
    connect(m_searchField, &QLineEdit::textEdited, this, &PDFAppSupportPage::onSearchTextEdited);
    actionsLayout->addWidget(m_searchField);

    m_channelButton = PDFAppPageWidgets::createButton(tr("YouTube channel"), QStringLiteral("external-link"), false, actions);
    m_channelButton->setObjectName(QStringLiteral("channelButton"));
    connect(m_channelButton, &QPushButton::clicked, this, &PDFAppSupportPage::onChannelClicked);
    actionsLayout->addWidget(m_channelButton);

    cardLayout->addWidget(new PDFAppResponsiveRow(texts, actions, Helper::HEADER_THRESHOLD, 12, card));

    // topics 117:2513: chips All, For you and the topics with their counts, gap 8
    QWidget* topics = new QWidget(card);
    topics->setObjectName(QStringLiteral("topics"));
    topics->setAccessibleName(tr("Topics"));
    PDFAppFlowLayout* topicsLayout = new PDFAppFlowLayout(topics, 8, 8);
    m_chipGroup = new QButtonGroup(this);
    m_chipGroup->setExclusive(true);
    topicsLayout->addWidget(createChip(QString::fromLatin1(ALL_TOPIC), tr("All"), int(PDFSupportVideoCatalog::getVideos().size()), topics));
    topicsLayout->addWidget(createChip(QString::fromLatin1(PDFSupportVideoCatalog::FOR_YOU_TOPIC), tr("For you"), 0, topics));
    for (const PDFSupportVideoTopic topic : PDFSupportVideoCatalog::getTopics())
    {
        topicsLayout->addWidget(createChip(PDFSupportVideoCatalog::getTopicId(topic), PDFSupportVideoCatalog::getTopicTitle(topic), PDFSupportVideoCatalog::getVideoCount(topic), topics));
    }
    connect(m_chipGroup, &QButtonGroup::buttonClicked, this, &PDFAppSupportPage::onChipClicked);
    cardLayout->addWidget(topics);

    // video-grid 117:2543: cards in rows of four, gaps 16
    m_videoGrid = new PDFAppGrid(Helper::VIDEO_MINIMUM_WIDTH, 4, 16, 16, card);
    m_videoGrid->setObjectName(QStringLiteral("videoGrid"));
    for (const PDFSupportVideo& video : PDFSupportVideoCatalog::getVideos())
    {
        PDFVideoCard* videoCard = new PDFVideoCard(video.getTitle(), video.getDescription(), video.getDuration(), video.getTags(), m_videoGrid);
        videoCard->setObjectName(video.getId());

        // A bundled picture of the video (none in the mockup: the placeholder)
        const QString thumbnailPath = video.getThumbnailPath();
        if (!thumbnailPath.isEmpty())
        {
            const QPixmap thumbnail(thumbnailPath);
            if (!thumbnail.isNull())
            {
                videoCard->setPixmap(thumbnail);
            }
        }

        PDFAppPageWidgets::setLink(videoCard, video.getLink().toString());
        connect(videoCard, &PDFVideoCard::clicked, this, &PDFAppSupportPage::onVideoClicked);
        connect(videoCard, &PDFVideoCard::tagClicked, this, &PDFAppSupportPage::onTagClicked);
        m_videoGrid->addWidget(videoCard);
        m_videoCards[video.getId()] = videoCard;
    }
    cardLayout->addWidget(m_videoGrid);

    // Empty list (For you without used tools, a search without a result)
    m_emptyText = new PDFAppParagraph(PDFThemeFonts::getFont(PDFThemeFonts::Role::Body), 20, PDFThemeColors::TextTertiary, card);
    m_emptyText->setObjectName(QStringLiteral("emptyText"));
    m_emptyText->setAlignment(Qt::AlignHCenter);
    m_emptyText->setContentsMargins(0, 24, 0, 24);
    m_emptyText->hide();
    cardLayout->addWidget(m_emptyText);

    return card;
}

QPushButton* PDFAppSupportPage::createChip(const QString& topic, const QString& text, int count, QWidget* parent)
{
    QPushButton* chip = new QPushButton(PDFAppSupportPageHelper::toButtonText(text), parent);
    chip->setObjectName(topic.isEmpty() ? QStringLiteral("chip-all") : QStringLiteral("chip-%1").arg(topic));
    chip->setProperty(PDFWidgetStyleProperties::CHIP, true);
    chip->setProperty(PDFWidgetStyleProperties::CHIP_COUNT, qMax(0, count));
    chip->setCheckable(true);
    chip->setAutoDefault(false);
    chip->setAccessibleName(text);
    m_chipGroup->addButton(chip);
    m_chips[topic] = chip;
    return chip;
}

void PDFAppSupportPage::applyFilter()
{
    const bool isForYou = m_topic == QLatin1String(PDFSupportVideoCatalog::FOR_YOU_TOPIC);
    const std::optional<PDFSupportVideoTopic> topic = PDFSupportVideoCatalog::findTopic(m_topic);

    int visibleCount = 0;
    for (const PDFSupportVideo& video : PDFSupportVideoCatalog::getVideos())
    {
        bool isVisible = PDFSupportVideoCatalog::matchesSearch(video, m_searchText);
        if (isForYou)
        {
            isVisible = isVisible && PDFSupportVideoCatalog::isForYou(video, m_usedToolIds);
        }
        else if (topic)
        {
            isVisible = isVisible && video.getTopic() == *topic;
        }

        if (PDFVideoCard* card = getVideoCard(video.getId()))
        {
            card->setHidden(!isVisible);
        }
        visibleCount += isVisible ? 1 : 0;
    }

    if (m_videoGrid)
    {
        m_videoGrid->setHidden(visibleCount == 0);
        m_videoGrid->updateCells();
    }

    if (m_emptyText)
    {
        QString text;
        if (visibleCount == 0)
        {
            if (isForYou && m_usedToolIds.isEmpty())
            {
                text = tr("Tutorials for the tools you use appear here. You have not used any tool yet.");
            }
            else if (isForYou && m_searchText.isEmpty())
            {
                text = tr("None of the tutorials shows a tool you have used.");
            }
            else
            {
                text = tr("No tutorials match “%1”.").arg(m_searchText.simplified());
            }
        }
        m_emptyText->setText(text);
        m_emptyText->setHidden(visibleCount > 0);
    }

    // The chip of the filter is checked (an exclusive group: one stop of the focus)
    if (QPushButton* chip = getChip(m_topic))
    {
        if (!chip->isChecked())
        {
            chip->setChecked(true);
        }
    }
}

void PDFAppSupportPage::updateChips()
{
    if (QPushButton* chip = getChip(QString::fromLatin1(PDFSupportVideoCatalog::FOR_YOU_TOPIC)))
    {
        int count = 0;
        for (const PDFSupportVideo& video : PDFSupportVideoCatalog::getVideos())
        {
            count += PDFSupportVideoCatalog::isForYou(video, m_usedToolIds) ? 1 : 0;
        }

        if (chip->property(PDFWidgetStyleProperties::CHIP_COUNT).toInt() != count)
        {
            PDFWidgetStyleProperties::set(chip, PDFWidgetStyleProperties::CHIP_COUNT, count);
            chip->updateGeometry();
        }
    }
}

void PDFAppSupportPage::updateRouteInHistory()
{
    // The filter is the state of the route: the current entry of the history of
    // the tab gets the new route, the page stays (the focus stays in the search
    // field or on the chip)
    replaceRoute(createRoute(m_topic, m_searchText));
}

void PDFAppSupportPage::onSearchTextEdited(const QString& text)
{
    setFilter(m_topic, text);
}

void PDFAppSupportPage::onChipClicked(QAbstractButton* button)
{
    for (const auto& [topic, chip] : m_chips)
    {
        if (chip && chip.data() == button)
        {
            setFilter(topic, m_searchText);
            return;
        }
    }
}

void PDFAppSupportPage::onVideoClicked()
{
    // The clicked signal does not identify the card: its link is its property
    const PDFVideoCard* card = qobject_cast<const PDFVideoCard*>(sender());
    if (!card)
    {
        return;
    }

    const QUrl url(PDFAppPageWidgets::getLink(card), QUrl::StrictMode);
    if (PDFSupportVideoCatalog::isAllowedLink(url))
    {
        openExternalUrl(url);
    }
}

void PDFAppSupportPage::onTagClicked(const QString& tag)
{
    // A tag shows the related tutorials of all topics
    setFilter(QString(), tag);
}

void PDFAppSupportPage::onChannelClicked()
{
    const QUrl url = PDFSupportVideoCatalog::getChannelUrl();
    if (PDFSupportVideoCatalog::isAllowedLink(url))
    {
        openExternalUrl(url);
    }
}

void PDFAppSupportPage::onQuickActionClicked()
{
    // The clicked signal does not identify the tile: its link is its property
    const PDFQuickActionTile* tile = qobject_cast<const PDFQuickActionTile*>(sender());
    if (!tile)
    {
        return;
    }

    openLink(PDFAppPageWidgets::getLink(tile));
}

void PDFAppSupportPage::onDatabaseChanged(const QString& topic, const QString& key)
{
    Q_UNUSED(key);

    if (topic == PDFToolRepository::getUsageTopic())
    {
        refreshUsedTools();
    }
}

void PDFAppSupportPage::onEverythingChanged()
{
    refreshUsedTools();
}

PDFAppPage* PDFAppSupportPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppSupportPage(context, parent);
}

}   // namespace pdf
