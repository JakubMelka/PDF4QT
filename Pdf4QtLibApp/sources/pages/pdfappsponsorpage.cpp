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

#include "pdfappsponsorpage.h"
#include "pdfappworkspace.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentrows.h"
#include "pdfthemefonts.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the page Sponsor
class PDFAppSponsorPageHelper
{
public:
    PDFAppSponsorPageHelper() = delete;

    /// Line heights of the paragraphs: 13 px with 1.5, 12 px with 1.4, the headline Bold 22
    static constexpr int BODY_LINE_HEIGHT = 20;
    static constexpr int SMALL_LINE_HEIGHT = 17;
    static constexpr int HEADLINE_LINE_HEIGHT = 27;

    /// Highlight cards: two in a row (395 px at 802 px), one in a narrow column
    static constexpr int HIGHLIGHT_MINIMUM_WIDTH = 260;

    static PDFAppParagraph* createParagraph(const QString& text, const QFont& font, int lineHeight, PDFThemeColors::Color color, QWidget* parent)
    {
        PDFAppParagraph* paragraph = new PDFAppParagraph(font, lineHeight, color, parent);
        paragraph->setText(text);
        return paragraph;
    }

    static QVBoxLayout* createColumn(QWidget* widget, int spacing)
    {
        QVBoxLayout* layout = new QVBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(spacing);
        return layout;
    }

    static PDFThemeColors::Color hue(PDFAccentHue hue, PDFAccentHueColors::Role role)
    {
        return PDFAccentHueColors::getColor(hue, role);
    }
};

PDFAppSponsorPage::PDFAppSponsorPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent)
{
    setObjectName(QStringLiteral("sponsorPage"));

    QVBoxLayout* layout = PDFAppSponsorPageHelper::createColumn(this, 24);
    layout->addStretch(1);
    buildContent();
}

PDFAppSponsorPage::~PDFAppSponsorPage() = default;

void PDFAppSponsorPage::buildContent()
{
    QBoxLayout* layout = qobject_cast<QBoxLayout*>(this->layout());
    if (!layout)
    {
        return;
    }
    layout->insertWidget(0, createHero(this));

    // sponsor-grid 122:2693: the main column and the side column 340
    m_columns = PDFAppColumns::createMainAndSide(this);
    layout->insertWidget(1, m_columns);

    QBoxLayout* mainLayout = m_columns->getMainLayout();
    QBoxLayout* sideLayout = m_columns->getSideLayout();
    mainLayout->addWidget(createPossibilities(m_columns->getMainColumn()));
    mainLayout->addWidget(createWaysToHelp(m_columns->getMainColumn()));
    mainLayout->addStretch(1);
    sideLayout->addWidget(createAuthorNote(m_columns->getSideColumn()));
    sideLayout->addWidget(createPriorities(m_columns->getSideColumn()));
    sideLayout->addWidget(createPromise(m_columns->getSideColumn()));
    sideLayout->addStretch(1);
}

void PDFAppSponsorPage::rebuild()
{
    const int scrollPosition = isVisible() ? getScrollPosition() : -1;
    const QString focusName = PDFAppPageRebuild::getFocusedName(this);

    m_highlightCards.clear();
    m_linkRows.clear();
    m_priorityRows.clear();
    delete m_hero.data();
    delete m_columns.data();
    buildContent();
    if (m_hero)
    {
        m_hero->show();
    }
    if (m_columns)
    {
        m_columns->show();
    }

    if (scrollPosition >= 0)
    {
        // The content keeps its height: the position is kept now and after the layout
        PDFAppPageRebuild::finishLayout();
        setScrollPosition(scrollPosition);
        requestDeferredScroll(scrollPosition, QString());
    }
    PDFAppPageRebuild::restoreFocus(this, focusName);
}

void PDFAppSponsorPage::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && m_columns)
    {
        rebuild();
    }
}

bool PDFAppSponsorPage::setRoute(const PDFAppRoute& route)
{
    // A static page: no query and no anchor (the route table refuses them too)
    if (!route.getQueryItems().isEmpty() || !route.getFragment().isEmpty())
    {
        return false;
    }
    return BaseClass::setRoute(route);
}

QString PDFAppSponsorPage::getTitle() const
{
    return tr("Sponsor");
}

QString PDFAppSponsorPage::getHeaderTitle() const
{
    return tr("Sponsor");
}

QString PDFAppSponsorPage::getHeaderSubtitle() const
{
    return tr("PDF4QT is free and open source. Your support keeps it that way and gives it time to grow.");
}

QList<PDFHighlightCard*> PDFAppSponsorPage::getHighlightCards() const
{
    QList<PDFHighlightCard*> cards;
    for (const QPointer<PDFHighlightCard>& card : m_highlightCards)
    {
        if (card)
        {
            cards.append(card.data());
        }
    }
    return cards;
}

QList<PDFLinkRow*> PDFAppSponsorPage::getLinkRows() const
{
    QList<PDFLinkRow*> rows;
    for (const QPointer<PDFLinkRow>& row : m_linkRows)
    {
        if (row)
        {
            rows.append(row.data());
        }
    }
    return rows;
}

QList<PDFStatusRow*> PDFAppSponsorPage::getPriorityRows() const
{
    QList<PDFStatusRow*> rows;
    for (const QPointer<PDFStatusRow>& row : m_priorityRows)
    {
        if (row)
        {
            rows.append(row.data());
        }
    }
    return rows;
}

QWidget* PDFAppSponsorPage::createHero(QWidget* parent)
{
    using Helper = PDFAppSponsorPageHelper;

    // hero 122:2947: accent/blue, padding 22, gap 20, radius 12
    m_hero = new PDFTintedCard(PDFAccentHue::Blue, parent);
    m_hero->setObjectName(QStringLiteral("hero"));
    QHBoxLayout* layout = new QHBoxLayout(m_hero);
    layout->setContentsMargins(23, 23, 23, 23);
    layout->setSpacing(20);

    PDFIconBadge* heart = new PDFIconBadge(QStringLiteral("heart"), 72, 32, m_hero);
    heart->setColors(PDFThemeColors::BgSurface, Helper::hue(PDFAccentHue::Red, PDFAccentHueColors::Role::Icon), Helper::hue(PDFAccentHue::Blue, PDFAccentHueColors::Role::Border));
    heart->setObjectName(QStringLiteral("heartBadge"));
    layout->addWidget(heart, 0, Qt::AlignTop);

    QWidget* content = new QWidget(m_hero);
    QVBoxLayout* contentLayout = Helper::createColumn(content, 8);
    layout->addWidget(content, 1);

    PDFAppParagraph* headline = Helper::createParagraph(tr("If PDF4QT saves you time, consider giving some back."), PDFThemeFonts::getFont(QFont::Bold, 22),
                                                        Helper::HEADLINE_LINE_HEIGHT, PDFThemeColors::TextPrimary, content);
    headline->setObjectName(QStringLiteral("headline"));
    contentLayout->addWidget(headline);
    contentLayout->addWidget(Helper::createParagraph(tr("PDF4QT has been developed since 2018 by one person, in his spare time, without ads, telemetry or paid tiers. "
                                                        "Sponsoring is a thank-you to the author: it rewards the thousands of hours already spent and makes the next ones easier to find."),
                                                     PDFThemeFonts::getFont(QFont::Normal, 13), Helper::BODY_LINE_HEIGHT, PDFThemeColors::TextSecondary, content));

    // facts (top padding 2, gaps 8)
    QWidget* facts = new QWidget(content);
    facts->setObjectName(QStringLiteral("facts"));
    PDFAppFlowLayout* factsLayout = new PDFAppFlowLayout(facts, 8, 8);
    factsLayout->setContentsMargins(0, 2, 0, 0);
    factsLayout->addWidget(new PDFFactPill(QStringLiteral("clock"), tr("Free since 2018"), facts));
    factsLayout->addWidget(new PDFFactPill(QStringLiteral("home"), tr("One developer"), facts));
    factsLayout->addWidget(new PDFFactPill(QStringLiteral("shield"), tr("No ads, no telemetry"), facts));
    factsLayout->addWidget(new PDFFactPill(QStringLiteral("file-text"), tr("MIT licensed"), facts));
    contentLayout->addWidget(facts);

    // buttons (top padding 8, gaps 10, centered vertically)
    QWidget* buttons = new QWidget(content);
    PDFAppFlowLayout* buttonsLayout = new PDFAppFlowLayout(buttons, 10, 10);
    buttonsLayout->setContentsMargins(0, 8, 0, 0);
    m_sponsorButton = PDFAppPageWidgets::createButton(tr("Sponsor on GitHub"), QStringLiteral("heart"), true, buttons);
    m_sponsorButton->setObjectName(QStringLiteral("sponsorButton"));
    withLink(m_sponsorButton, PDFAppProjectLinks::getSponsors().toString());
    m_starButton = PDFAppPageWidgets::createButton(tr("Star on GitHub"), QStringLiteral("star"), false, buttons);
    m_starButton->setObjectName(QStringLiteral("starButton"));
    withLink(m_starButton, PDFAppProjectLinks::getRepository().toString());
    m_storeButton = PDFAppPageWidgets::createButton(tr("Rate on Microsoft Store"), QStringLiteral("monitor"), false, buttons);
    m_storeButton->setObjectName(QStringLiteral("storeButton"));
    withLink(m_storeButton, PDFAppProjectLinks::getMicrosoftStore().toString());
    m_oneTimeLink = new PDFTextLink(tr("Prefer a one-time contribution?"), buttons);
    m_oneTimeLink->setObjectName(QStringLiteral("oneTimeLink"));
    withLink(m_oneTimeLink, PDFAppProjectLinks::getOneTimeSponsorship().toString());
    buttonsLayout->addWidget(m_sponsorButton);
    buttonsLayout->addWidget(m_starButton);
    buttonsLayout->addWidget(m_storeButton);
    buttonsLayout->addWidget(m_oneTimeLink);
    contentLayout->addWidget(buttons);

    return m_hero;
}

QWidget* PDFAppSponsorPage::createPossibilities(QWidget* parent)
{
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("possibilities"));
    QVBoxLayout* layout = PDFAppSponsorPageHelper::createColumn(section, 12);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("What your support makes possible"), PDFAppPageWidgets::createHint(tr("Time, mostly"), nullptr), section));

    // two rows of two highlight cards, gaps 12
    PDFAppGrid* grid = new PDFAppGrid(PDFAppSponsorPageHelper::HIGHLIGHT_MINIMUM_WIDTH, 2, 12, 12, section);
    grid->setObjectName(QStringLiteral("highlights"));
    struct Highlight
    {
        QString title;
        QString description;
        const char* iconName;
        PDFAccentHue hue;
    };
    const Highlight highlights[] = {
        { tr("New tools and features"), tr("Time to build what people ask for most, such as the unified application and better OCR."), "zap", PDFAccentHue::Amber },
        { tr("Bug fixes and maintenance"), tr("Keeping the PDF engine reliable as Qt, compilers and the PDF standard move on."), "settings", PDFAccentHue::Red },
        { tr("Documentation and tutorials"), tr("Guides and videos, so that fewer people get stuck and more get the most out of the tools."), "book-open", PDFAccentHue::Blue },
        { tr("Releases for every platform"), tr("Building, packaging and testing on Windows, Linux and macOS, including Flatpak and AppImage."), "monitor", PDFAccentHue::Green }
    };
    for (const Highlight& highlight : highlights)
    {
        PDFHighlightCard* card = new PDFHighlightCard(highlight.title, highlight.description, QString::fromLatin1(highlight.iconName), highlight.hue, grid);
        grid->addWidget(card);
        m_highlightCards.append(card);
    }
    layout->addWidget(grid);
    return section;
}

QWidget* PDFAppSponsorPage::createWaysToHelp(QWidget* parent)
{
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("waysToHelp"));
    QVBoxLayout* layout = PDFAppSponsorPageHelper::createColumn(section, 12);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Ways to help"), PDFAppPageWidgets::createHint(tr("Money is only one of them"), nullptr), section));

    // list 123:2794: card, padding 8, gap 2
    QWidget* card = PDFAppPageWidgets::createCard(8, 2, section);
    card->setObjectName(QStringLiteral("waysCard"));
    layout->addWidget(card);

    struct Way
    {
        QString title;
        QString subtitle;
        const char* iconName;
        QUrl link;
    };
    const Way ways[] = {
        { tr("Become a sponsor"), tr("Monthly or one-time, through GitHub Sponsors"), "heart", PDFAppProjectLinks::getSponsors() },
        { tr("Star the repository"), tr("Helps other people find the project"), "star", PDFAppProjectLinks::getRepository() },
        { tr("Leave a review on Microsoft Store"), tr("A few honest words go a long way"), "monitor", PDFAppProjectLinks::getMicrosoftStore() },
        { tr("Tell a colleague"), tr("Word of mouth is how most people find PDF4QT"), "message-square", PDFAppProjectLinks::getWebsite() },
        { tr("Report bugs and suggest features"), tr("A clear report saves hours of guesswork"), "alert-circle", PDFAppProjectLinks::getNewIssue() },
        { tr("Contribute code or translations"), tr("Pull requests are welcome, small ones too"), "keyboard", PDFAppProjectLinks::getPullRequests() }
    };
    for (const Way& way : ways)
    {
        PDFLinkRow* row = new PDFLinkRow(way.title, way.subtitle, QString::fromLatin1(way.iconName), card);
        withLink(row, way.link.toString());
        card->layout()->addWidget(row);
        m_linkRows.append(row);
    }
    return section;
}

QWidget* PDFAppSponsorPage::createAuthorNote(QWidget* parent)
{
    using Helper = PDFAppSponsorPageHelper;

    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("authorNote"));
    QVBoxLayout* layout = Helper::createColumn(section, 12);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("A note from the author"), nullptr, section));

    // card 123:2858: padding 16, gap 12
    QWidget* card = PDFAppPageWidgets::createCard(16, 12, section);
    QBoxLayout* cardLayout = qobject_cast<QBoxLayout*>(card->layout());
    layout->addWidget(card);

    QWidget* identity = new QWidget(card);
    QHBoxLayout* identityLayout = new QHBoxLayout(identity);
    identityLayout->setContentsMargins(0, 0, 0, 0);
    identityLayout->setSpacing(12);
    identityLayout->addWidget(new PDFAvatar(QStringLiteral("JM"), 40, 14, identity));
    QWidget* identityText = new QWidget(identity);
    QVBoxLayout* identityTextLayout = Helper::createColumn(identityText, 1);
    identityTextLayout->addWidget(PDFAppPageWidgets::createText(QStringLiteral("Jakub Melka"), PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold), 16, PDFThemeColors::TextPrimary, identityText));
    identityTextLayout->addWidget(PDFAppPageWidgets::createText(tr("Author of PDF4QT"), PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular), 13, PDFThemeColors::TextMuted, identityText));
    identityLayout->addWidget(identityText, 1, Qt::AlignVCenter);
    cardLayout->addWidget(identity);

    cardLayout->addWidget(Helper::createParagraph(tr("I write PDF4QT because I enjoy it, and I intend to keep it free. To be honest about sponsorship: I do not commit to spending all of it "
                                                     "on the project. Some of it pays for the tools and services PDF4QT needs, and some of it is simply a personal reward, which is "
                                                     "what keeps a spare-time project alive for years. Thank you for considering it."),
                                                  PDFThemeFonts::getFont(QFont::Normal, 13), Helper::BODY_LINE_HEIGHT, PDFThemeColors::TextSecondary, card));
    return section;
}

QWidget* PDFAppSponsorPage::createPriorities(QWidget* parent)
{
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("priorities"));
    QVBoxLayout* layout = PDFAppSponsorPageHelper::createColumn(section, 12);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("What the time goes into now"), nullptr, section));

    // list 123:2869: card, padding 8, rows 46
    QWidget* card = PDFAppPageWidgets::createCard(8, 0, section);
    layout->addWidget(card);

    struct Priority
    {
        QString title;
        QString subtitle;
        QString status;
        bool isActive;
    };
    const Priority priorities[] = {
        { tr("Unified application"), tr("One app instead of four, this design"), tr("In progress"), true },
        { tr("OCR accuracy and speed"), tr("Better layout detection, faster runs"), tr("In progress"), true },
        { tr("Video tutorials"), tr("Short videos for every tool"), tr("Next"), false },
        { tr("Large documents"), tr("Lower memory use, faster opening"), tr("Next"), false },
        { tr("Linux packaging"), tr("Flatpak and AppImage kept current"), tr("Planned"), false }
    };
    for (const Priority& priority : priorities)
    {
        PDFStatusRow* row = new PDFStatusRow(priority.title, priority.subtitle, priority.status, priority.isActive, card);
        card->layout()->addWidget(row);
        m_priorityRows.append(row);
    }
    return section;
}

QWidget* PDFAppSponsorPage::createPromise(QWidget* parent)
{
    using Helper = PDFAppSponsorPageHelper;

    // promise 123:2905: accent/green, padding 14, gap 12
    m_promise = new PDFTintedCard(PDFAccentHue::Green, parent);
    m_promise->setObjectName(QStringLiteral("promise"));
    QHBoxLayout* layout = new QHBoxLayout(m_promise);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(12);
    PDFIconBadge* shield = new PDFIconBadge(QStringLiteral("shield"), 36, 18, m_promise);
    shield->setColors(PDFThemeColors::BgSurface, Helper::hue(PDFAccentHue::Green, PDFAccentHueColors::Role::Icon));
    layout->addWidget(shield, 0, Qt::AlignTop);

    QWidget* texts = new QWidget(m_promise);
    QVBoxLayout* textsLayout = Helper::createColumn(texts, 3);
    textsLayout->addWidget(PDFAppPageWidgets::createText(tr("Free, for everyone"), PDFThemeFonts::getFont(PDFThemeFonts::Role::TileTitle), 17, PDFThemeColors::TextPrimary, texts));
    textsLayout->addWidget(Helper::createParagraph(tr("PDF4QT stays free and open source under the MIT license, without ads or telemetry, however it is funded. "
                                                      "Sponsors get gratitude, not a different program."),
                                                   PDFThemeFonts::getFont(QFont::Normal, 12), Helper::SMALL_LINE_HEIGHT, PDFThemeColors::TextSecondary, texts));
    layout->addWidget(texts, 1);
    return m_promise;
}

PDFAppPage* PDFAppSponsorPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppSponsorPage(context, parent);
}

}   // namespace pdf
