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

#include "pdftoolstartpage.h"

#include "pdfappcontext.h"
#include "pdfappfiledrop.h"
#include "pdfapphelpcatalog.h"
#include "pdfappinfowidgets.h"
#include "pdfapplocale.h"
#include "pdfappmarkdownview.h"
#include "pdfapproute.h"
#include "pdfapptooldocumentchooser.h"
#include "pdfapptoolpageregistry.h"
#include "pdfapptoolpresentation.h"
#include "pdfapptoolstate.h"
#include "pdfsettingsrepository.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolsuggestions.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace pdf
{

/// tutorial-card 279:2028: accent/violet/bg with its stronger border
class PDFToolStartTutorialCard : public PDFTintedCard
{
public:
    explicit PDFToolStartTutorialCard(QWidget* parent) :
        PDFTintedCard(PDFAccentHue::Violet, parent)
    {

    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        PDFTintedCard::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        PDFStylePainter::strokeInside(&painter, QRectF(rect()), PDFStylePainter::Radii(getRadius()), PDFAccentHueColors::getColor(PDFAccentHue::Violet, PDFAccentHueColors::Role::BorderStrong), 1.0);
    }
};

/// thumbnail 279:2035: bg/muted, border/divider, radius 8
class PDFToolStartThumbnail : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const PDFStylePainter::Radii radii(8.0);
        PDFStylePainter::fill(&painter, QRectF(rect()), radii, PDFThemeColors::BgMuted);
        PDFStylePainter::strokeInside(&painter, QRectF(rect()), radii, PDFThemeColors::BorderDivider, 1.0);
    }
};

/// Helpers of the start page
class PDFToolStartPageHelper
{
public:
    PDFToolStartPageHelper() = delete;

    static constexpr int MAIN_WIDTH_MINIMUM = 400;
    static constexpr int SIDE_WIDTH = 340;
    static constexpr int SECTION_GAP = 12;      ///< Header and its card (choose-document 277:2036)
    static constexpr int TUTORIAL_HEIGHT = 369; ///< tutorial-card 279:2028 keeps its place without a video
    static constexpr int THUMBNAIL_HEIGHT = 170;
    static constexpr int OFTEN_NEXT_COUNT = 3;

    /// Header of a section: the title Semi Bold 15 and the hint Regular 12 text/muted, gap 12
    static QWidget* createHeader(const QString& title, const QString& hint, QWidget* parent)
    {
        QWidget* header = new QWidget(parent);
        QHBoxLayout* layout = new QHBoxLayout(header);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(SECTION_GAP);
        layout->addWidget(PDFAppPageWidgets::createLabel(title, QFont::DemiBold, 15, PDFThemeColors::TextPrimary, header), 0, Qt::AlignBaseline);
        if (!hint.isEmpty())
        {
            PDFLabel* hintLabel = PDFAppPageWidgets::createLabel(hint, QFont::Normal, 12, PDFThemeColors::TextMuted, header);
            hintLabel->setMinimumWidth(1);
            layout->addWidget(hintLabel, 1, Qt::AlignBaseline);
        }
        else
        {
            layout->addStretch(1);
        }
        return header;
    }

    /// Text of the kind of the input of At a glance
    static QString getInputText(PDFDocumentToolInputKind kind)
    {
        switch (kind)
        {
            case PDFDocumentToolInputKind::One:
                return PDFToolStartPage::tr("One open PDF");
            case PDFDocumentToolInputKind::Multiple:
                return PDFToolStartPage::tr("More PDF files");
            case PDFDocumentToolInputKind::None:
                return PDFToolStartPage::tr("No document");
        }
        return QString();
    }

    /// What the tool does with an open document
    static QString getOpenDocumentText(PDFDocumentToolInputKind kind)
    {
        return kind == PDFDocumentToolInputKind::None ? PDFToolStartPage::tr("Not used") : PDFToolStartPage::tr("Your choice");
    }

    static QString getLockedText(PDFDocumentToolLockedAccess access)
    {
        switch (access)
        {
            case PDFDocumentToolLockedAccess::Unavailable:
                return PDFToolStartPage::tr("Not available");
            case PDFDocumentToolLockedAccess::Available:
                return PDFToolStartPage::tr("Available");
            case PDFDocumentToolLockedAccess::NoDocument:
                return PDFToolStartPage::tr("Not used");
        }
        return QString();
    }

    static QString getSignatureText(PDFDocumentToolSignatureImpact impact)
    {
        switch (impact)
        {
            case PDFDocumentToolSignatureImpact::LostCopyOnly:
                return PDFToolStartPage::tr("Lost (copy only)");
            case PDFDocumentToolSignatureImpact::IncrementalSave:
                return PDFToolStartPage::tr("Kept in the older revision");
            case PDFDocumentToolSignatureImpact::None:
                return PDFToolStartPage::tr("Not affected");
        }
        return QString();
    }
};

PDFToolStartPage::PDFToolStartPage(PDFAppContext* context, const QString& toolId, QWidget* parent) :
    BaseClass(context, parent),
    m_toolId(toolId)
{
    setObjectName(QStringLiteral("toolStartPage"));
    PDFAppPageWidgets::createColumn(this, 0);
    build();
}

PDFToolStartPage::~PDFToolStartPage() = default;

const PDFDocumentToolCatalogEntry* PDFToolStartPage::getTool() const
{
    return PDFDocumentToolCatalog::findTool(m_toolId);
}

bool PDFToolStartPage::setRoute(const PDFAppRoute& route)
{
    if (!route.getQueryItems().isEmpty() || !route.getFragment().isEmpty() || !getTool())
    {
        return false;
    }
    return BaseClass::setRoute(route);
}

QString PDFToolStartPage::getTitle() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    return tool ? tool->getTitle() : QString();
}

QString PDFToolStartPage::getHeaderTitle() const
{
    return getTitle();
}

QString PDFToolStartPage::getHeaderSubtitle() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    return tool ? tool->getDescription() : QString();
}

QList<PDFAppBreadcrumbItem> PDFToolStartPage::getHeaderBreadcrumbs() const
{
    // breadcrumb 276:1974: Tools › Optimize › Compress
    QList<PDFAppBreadcrumbItem> items;
    const PDFDocumentToolCatalogEntry* tool = getTool();
    if (tool)
    {
        PDFAppBreadcrumbItem tools;
        tools.text = tr("Tools");
        tools.route = PDFAppRoutes::tools();
        items << tools;

        const PDFDocumentToolCategoryInfo* category = PDFDocumentToolCatalog::getCategoryInfo(tool->getCategory());
        PDFAppBreadcrumbItem categoryItem;
        categoryItem.text = category ? category->getChipText() : QString();
        categoryItem.route = category ? PDFAppRoutes::tools(category->getId()) : PDFAppRoutes::tools();
        items << categoryItem;

        PDFAppBreadcrumbItem current;
        current.text = tool->getTitle();
        items << current;
    }
    return items;
}

PDFAppHeaderIcon PDFToolStartPage::getHeaderIcon() const
{
    PDFAppHeaderIcon icon;
    if (const PDFDocumentToolCatalogEntry* tool = getTool())
    {
        icon.iconName = tool->getIconName();
        icon.hue = PDFAppToolPresentation::getHue(tool->getHue());
    }
    return icon;
}

QString PDFToolStartPage::getHeaderTag() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const PDFDocumentToolCategoryInfo* category = tool ? PDFDocumentToolCatalog::getCategoryInfo(tool->getCategory()) : nullptr;
    return category ? category->getChipText() : QString();
}

QString PDFToolStartPage::getHelpTopic() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const std::shared_ptr<const PDFAppHelpCatalog> catalog = PDFAppHelpCatalog::getBuiltin();
    return tool && catalog ? catalog->getToolTopic(*tool) : QStringLiteral("tools");
}

QString PDFToolStartPage::getOpenFilesToolId() const
{
    return m_toolId;
}

void PDFToolStartPage::build()
{
    using Helper = PDFToolStartPageHelper;

    m_content = new QWidget(this);
    m_content->setObjectName(QStringLiteral("toolStartContent"));
    QVBoxLayout* contentLayout = PDFAppPageWidgets::createColumn(m_content, 0);

    // tool-start-grid 276:1778: 802 + 24 + 340
    m_columns = PDFAppColumns::createMainAndSide(m_content, Helper::MAIN_WIDTH_MINIMUM, Helper::SIDE_WIDTH);
    m_columns->setObjectName(QStringLiteral("toolStartGrid"));
    contentLayout->addWidget(m_columns);
    contentLayout->addStretch(1);

    QWidget* main = m_columns->getMainColumn();
    QWidget* side = m_columns->getSideColumn();
    m_columns->getMainLayout()->addWidget(createChooseDocument(main));
    m_columns->getMainLayout()->addWidget(createManual(main));
    m_columns->getMainLayout()->addStretch(1);
    m_columns->getSideLayout()->addWidget(createTutorial(side));
    m_columns->getSideLayout()->addWidget(createAtGlance(side));
    m_columns->getSideLayout()->addWidget(createLinks(side));
    m_columns->getSideLayout()->addStretch(1);

    layout()->addWidget(m_content);
}

void PDFToolStartPage::rebuild()
{
    m_isRebuildPending = false;

    const int scrollPosition = getScrollPosition();
    const QString focusedName = PDFAppPageRebuild::getFocusedName(this);

    QWidget* oldContent = m_content.data();
    m_content = nullptr;
    if (oldContent)
    {
        // A slot of a part may run below this call: the old parts go from the event loop
        oldContent->hide();
        oldContent->deleteLater();
    }
    build();

    PDFAppPageRebuild::finishLayout();
    PDFAppPageRebuild::restoreFocus(this, focusedName);
    if (scrollPosition >= 0)
    {
        setScrollPosition(scrollPosition);
    }
    Q_EMIT titleChanged();
}

void PDFToolStartPage::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && !m_isRebuildPending)
    {
        m_isRebuildPending = true;
        QMetaObject::invokeMethod(this, &PDFToolStartPage::rebuild, Qt::QueuedConnection);
    }
}

QWidget* PDFToolStartPage::createChooseDocument(QWidget* parent)
{
    using Helper = PDFToolStartPageHelper;

    // choose-document 277:2036, gap 12
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("toolChooseDocument"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, Helper::SECTION_GAP);
    layout->addWidget(Helper::createHeader(tr("Choose a document"), tr("%1 works on one PDF · pick an open document or open a file").arg(title), section));

    m_chooser = new PDFAppToolDocumentChooser(getContext(), m_toolId, section);
    connect(m_chooser.data(), &PDFAppToolDocumentChooser::documentChosen, this, &PDFToolStartPage::onDocumentChosen);
    connect(m_chooser.data(), &PDFAppToolDocumentChooser::openFileRequested, this, &PDFToolStartPage::onOpenFileRequested);
    layout->addWidget(m_chooser);

    // note 277:2096: the icon info 14, gap 8, Regular 12 text/secondary
    QWidget* note = new QWidget(section);
    note->setObjectName(QStringLiteral("toolStartNote"));
    QHBoxLayout* noteLayout = new QHBoxLayout(note);
    noteLayout->setContentsMargins(0, 0, 0, 0);
    noteLayout->setSpacing(8);
    PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("info"), 14, 14, note);
    icon->setColors(std::nullopt, PDFThemeColors::IconDefault);
    noteLayout->addWidget(icon, 0, Qt::AlignTop);
    const QString noteText = tr("The tool opens in the document’s tab. Nothing changes until you press %1; %2.").arg(title, tool ? tool->getLockedNote() : QString());
    PDFAppParagraph* noteParagraph = PDFAppPageWidgets::createText(noteText, PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, note);
    noteParagraph->setObjectName(QStringLiteral("toolStartNoteText"));
    noteParagraph->setMinimumWidth(1);
    noteLayout->addWidget(noteParagraph, 1);
    layout->addWidget(note);
    return section;
}

QWidget* PDFToolStartPage::createManual(QWidget* parent)
{
    using Helper = PDFToolStartPageHelper;

    // documentation 280:2042: "How Compress works · from the user manual", the card radius 12
    const PDFDocumentToolCatalogEntry* tool = getTool();
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("toolManual"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, Helper::SECTION_GAP);
    layout->addWidget(Helper::createHeader(tr("How %1 works").arg(tool ? tool->getTitle() : QString()), tr("from the user manual"), section));

    m_manualView = new PDFAppMarkdownView(section);
    m_manualView->setObjectName(QStringLiteral("toolManualView"));
    m_manualView->setResourceBase(PDFAppHelpCatalog::getResourceDirectory() + QStringLiteral("/"));
    const std::shared_ptr<const PDFAppHelpCatalog> catalog = PDFAppHelpCatalog::getBuiltin();
    if (catalog)
    {
        const PDFAppHelpTopic topic = catalog->getLocalizedTopic(getHelpTopic(), PDFAppLocale::getUiLanguage());
        m_manualView->setMarkdown(topic.markdown);
        m_manualView->setAccessibleName(topic.title);
    }
    connect(m_manualView.data(), &PDFAppMarkdownView::linkActivated, this, &PDFToolStartPage::onMarkdownLinkActivated);
    layout->addWidget(m_manualView);
    return section;
}

QWidget* PDFToolStartPage::createTutorial(QWidget* parent)
{
    using Helper = PDFToolStartPageHelper;

    // tutorial-card 279:2028: without a real video (the links of the catalog of the videos
    // are mockups) the card is disabled with its place kept, owner answer Q5. A video title
    // or a duration, which do not exist, are not claimed.
    const PDFDocumentToolCatalogEntry* tool = getTool();
    PDFTintedCard* card = new PDFToolStartTutorialCard(parent);
    card->setObjectName(QStringLiteral("toolTutorialCard"));
    card->setRadius(12);
    card->setMinimumHeight(Helper::TUTORIAL_HEIGHT);
    m_tutorialCard = card;

    QVBoxLayout* layout = new QVBoxLayout(card);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(12);

    QHBoxLayout* head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(8);
    PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("play-circle"), 18, 18, card);
    icon->setColors(std::nullopt, PDFThemeColors::IconDisabled);
    head->addWidget(icon, 0, Qt::AlignVCenter);
    head->addWidget(PDFAppPageWidgets::createLabel(tr("Watch the tutorial"), QFont::DemiBold, 15, PDFThemeColors::TextDisabled, card), 1);
    layout->addLayout(head);

    QWidget* thumbnail = new PDFToolStartThumbnail(card);
    thumbnail->setObjectName(QStringLiteral("toolTutorialThumbnail"));
    thumbnail->setFixedHeight(Helper::THUMBNAIL_HEIGHT);
    QHBoxLayout* thumbnailLayout = new QHBoxLayout(thumbnail);
    PDFIconBadge* play = new PDFIconBadge(QStringLiteral("play"), 44, 16, thumbnail);
    play->setColors(PDFThemeColors::IconDisabled, PDFThemeColors::IconOnAccent);
    thumbnailLayout->addWidget(play, 0, Qt::AlignCenter);
    layout->addWidget(thumbnail);

    layout->addWidget(PDFAppPageWidgets::createLabel(tr("%1 in one step").arg(tool ? tool->getTitle() : QString()), QFont::DemiBold, 13, PDFThemeColors::TextDisabled, card));
    PDFAppParagraph* description = PDFAppPageWidgets::createText(tr("The video tutorial is not available yet. The user manual on this page explains the tool."),
                                                                 PDFThemeFonts::getFont(QFont::Normal, 12), 17, PDFThemeColors::TextDisabled, card);
    description->setMinimumWidth(1);
    layout->addWidget(description, 1, Qt::AlignTop);

    m_watchButton = PDFAppPageWidgets::createButton(tr("Watch on YouTube"), QStringLiteral("external-link"), true, card);
    m_watchButton->setObjectName(QStringLiteral("toolWatchTutorial"));
    m_watchButton->setEnabled(false);
    layout->addWidget(m_watchButton);
    return card;
}

QString PDFToolStartPage::createOftenNextText() const
{
    using Helper = PDFToolStartPageHelper;

    PDFDocumentToolSuggestionInput input;
    input.fromTools = QStringList{ m_toolId };
    input.excludedTools = QStringList{ m_toolId };
    input.maximumCount = Helper::OFTEN_NEXT_COUNT;

    PDFAppContext* context = getContext();
    if (context && context->getDatabase())
    {
        input.pairs = PDFAppToolState::readPairs(context->getDatabase(), input.fromTools);
        input.isSuggestNextEnabled = context->getSettings().getValue(QStringLiteral("tools.suggestNext")).toBool();
    }

    QStringList names;
    for (const QString& toolId : PDFDocumentToolSuggestions::suggest(input))
    {
        if (const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId))
        {
            names << tool->getTitle();
        }
    }
    return names.join(QStringLiteral(", "));
}

QWidget* PDFToolStartPage::createAtGlance(QWidget* parent)
{
    using Helper = PDFToolStartPageHelper;

    // at-a-glance 279:2048: the title Semi Bold 15, gap 12, the card paddings 8 / 16
    const PDFDocumentToolCatalogEntry* tool = getTool();
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("toolAtGlance"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, Helper::SECTION_GAP);
    layout->addWidget(Helper::createHeader(tr("At a glance"), QString(), section));

    PDFAppCard* card = new PDFAppCard(QMargins(17, 9, 17, 9), 0, section);
    m_facts = new PDFKeyValueList(card);
    m_facts->setObjectName(QStringLiteral("toolFacts"));
    if (tool)
    {
        m_facts->addRow(tr("Input"), Helper::getInputText(tool->getInputKind()));
        m_facts->addRow(tr("Result"), tool->getResultSummary());
        m_facts->addRow(tr("Open document"), Helper::getOpenDocumentText(tool->getInputKind()));
        m_facts->addRow(tr("Locked document"), Helper::getLockedText(tool->getLockedAccess()));
        m_facts->addRow(tr("Signatures"), Helper::getSignatureText(tool->getSignatureImpact()));
        const QString command = tool->getPdfToolCommand();
        if (!command.isEmpty())
        {
            m_facts->addRow(tr("Command line"), QStringLiteral("pdftool %1").arg(command));
        }
    }
    card->getLayout()->addWidget(m_facts);

    // often-next 823:1842: paddings 6 / 10, gap 2, the key Regular 12, the value Medium 12
    const QString oftenNext = createOftenNextText();
    if (!oftenNext.isEmpty())
    {
        QWidget* block = new QWidget(card);
        block->setObjectName(QStringLiteral("toolOftenNext"));
        QVBoxLayout* blockLayout = PDFAppPageWidgets::createColumn(block, 2);
        blockLayout->setContentsMargins(10, 6, 10, 6);
        blockLayout->addWidget(PDFAppPageWidgets::createText(tr("Often used next"), PDFThemeFonts::getFont(QFont::Normal, 12), 16, PDFThemeColors::TextTertiary, block));
        m_oftenNext = PDFAppPageWidgets::createText(oftenNext, PDFThemeFonts::getFont(QFont::Medium, 12), 16, PDFThemeColors::TextPrimary, block);
        m_oftenNext->setMinimumWidth(1);
        blockLayout->addWidget(m_oftenNext);
        card->getLayout()->addWidget(block);
    }
    layout->addWidget(card);
    return section;
}

QWidget* PDFToolStartPage::createLinks(QWidget* parent)
{
    // links 278:2029: the card paddings 6, Read the manual "Compress – options and presets explained"
    const PDFDocumentToolCatalogEntry* tool = getTool();
    PDFAppCard* card = PDFAppPageWidgets::createCard(6, 2, parent);
    card->setObjectName(QStringLiteral("toolLinks"));

    QString subtitle;
    const std::shared_ptr<const PDFAppHelpCatalog> catalog = PDFAppHelpCatalog::getBuiltin();
    if (catalog && tool)
    {
        const PDFAppHelpTopic topic = catalog->getLocalizedTopic(getHelpTopic(), PDFAppLocale::getUiLanguage());
        subtitle = tr("%1 – %2").arg(tool->getTitle(), topic.summary);
    }
    m_manualLink = new PDFLinkRow(tr("Read the manual"), subtitle, QStringLiteral("book-open"), card);
    m_manualLink->setObjectName(QStringLiteral("toolManualLink"));
    connect(m_manualLink.data(), &PDFLinkRow::clicked, this, &PDFToolStartPage::onManualLinkClicked);
    card->getLayout()->addWidget(m_manualLink);
    return card;
}

void PDFToolStartPage::onDocumentChosen(const QString& documentId)
{
    requestNavigation(PDFAppRoutes::documentTool(documentId, m_toolId), PDFAppNavigationMode::Push);
}

void PDFToolStartPage::onOpenFileRequested()
{
    // The native dialog runs its own event loop: the page may go meanwhile
    QPointer<PDFToolStartPage> guard(this);
    const QStringList files = PDFAppFileDialogs::getOpenFileNames(window(), QString());
    if (!guard || files.isEmpty())
    {
        return;
    }

    // One file of the mode One goes to the tool, more files only open
    std::vector<PDFAppOpenRequest> requests;
    for (const QString& file : files)
    {
        PDFAppOpenRequest request;
        request.filePath = file;
        request.toolId = requests.empty() ? m_toolId : QString();
        requests.push_back(std::move(request));
    }
    PDFAppDocumentActions::openRequests(this, requests);
}

void PDFToolStartPage::onMarkdownLinkActivated(const QString& link)
{
    openLink(link);
}

void PDFToolStartPage::onManualLinkClicked()
{
    openLink(PDFAppRoutes::help(getHelpTopic()).toString());
}

PDFAppPage* PDFToolStartPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    Q_UNUSED(context);
    Q_UNUSED(parent);
    return nullptr;
}

PDFAppPage* PDFToolStartPageFactory::createPageForRoute(PDFAppContext* context, QWidget* parent, const PDFAppRoute& route) const
{
    const QString toolId = PDFAppToolRoutePageFactory::getToolId(PDFAppToolPageKind::Start, route);
    return PDFDocumentToolCatalog::findTool(toolId) ? new PDFToolStartPage(context, toolId, parent) : nullptr;
}

}   // namespace pdf

#include "moc_pdftoolstartpage.cpp"
