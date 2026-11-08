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
#include "pdfapptoolinputlist.h"
#include "pdfapptoolpageregistry.h"
#include "pdfapptoolpresentation.h"
#include "pdfapptoolstate.h"
#include "pdfsettingsrepository.h"
#include "pdfappmainwindow.h"
#include "pdfdocumenttoolgui.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolsuggestions.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QMimeData>
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
        PDFLabel* titleLabel = PDFAppPageWidgets::createLabel(title, QFont::DemiBold, 15, PDFThemeColors::TextPrimary, header);
        if (!hint.isEmpty())
        {
            PDFLabel* hintLabel = PDFAppPageWidgets::createLabel(hint, QFont::Normal, 12, PDFThemeColors::TextMuted, header);
            hintLabel->setMinimumWidth(1);
            PDFAppPageWidgets::addBaselineLabels(layout, { { titleLabel }, { hintLabel, 1 } });
        }
        else
        {
            layout->addWidget(titleLabel);
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
                // K9, 531:3390
                return PDFToolStartPage::tr("Two or more PDFs");
            case PDFDocumentToolInputKind::None:
                return PDFToolStartPage::tr("No document");
        }
        return QString();
    }

    /// What the tool does with an open document (K9: a new document leaves the open
    /// documents of a tool with more inputs unchanged, 531:3392)
    static QString getOpenDocumentText(PDFDocumentToolInputKind kind, PDFDocumentToolResultKind resultKind)
    {
        if (kind == PDFDocumentToolInputKind::Multiple && resultKind == PDFDocumentToolResultKind::NewDocument)
        {
            return PDFToolStartPage::tr("Stay unchanged");
        }
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

    static QString getSignatureText(PDFDocumentToolSignatureImpact impact, PDFDocumentToolInputKind kind, PDFDocumentToolResultKind resultKind)
    {
        // K9, 531:3394: the signatures of the inputs are not in a new document of more inputs
        if (kind == PDFDocumentToolInputKind::Multiple && resultKind == PDFDocumentToolResultKind::NewDocument)
        {
            return PDFToolStartPage::tr("Not kept (new file)");
        }

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

    // The inputs of a tool with more inputs belong to the page (they survive a rebuild); the
    // files dropped anywhere on the page join them (K8)
    const PDFDocumentToolCatalogEntry* tool = getTool();
    if (tool && tool->getInputKind() == PDFDocumentToolInputKind::Multiple)
    {
        m_inputList = new PDFAppToolInputList(this);
        setAcceptDrops(true);
    }
    build();
}

PDFToolStartPage::~PDFToolStartPage() = default;

PDFAppToolInputList* PDFToolStartPage::getInputList() const
{
    return m_inputList.data();
}

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

        // The title of the category, not its short chip (531:3208 "Organize Pages", 1392:4528
        // "Create & Convert"; the chips of the catalog page are short, tools.md 3)
        const PDFDocumentToolCategoryInfo* category = PDFDocumentToolCatalog::getCategoryInfo(tool->getCategory());
        PDFAppBreadcrumbItem categoryItem;
        categoryItem.text = category ? category->getTitle() : QString();
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
    // The Tag of the header has the title of the category (531:3208, 1392:4528)
    const PDFDocumentToolCategoryInfo* category = tool ? PDFDocumentToolCatalog::getCategoryInfo(tool->getCategory()) : nullptr;
    return category ? category->getTitle() : QString();
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
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const PDFDocumentToolInputKind inputKind = tool ? tool->getInputKind() : PDFDocumentToolInputKind::One;
    switch (inputKind)
    {
        case PDFDocumentToolInputKind::One:
            m_columns->getMainLayout()->addWidget(createChooseDocument(main));
            break;
        case PDFDocumentToolInputKind::Multiple:
            m_columns->getMainLayout()->addWidget(createChooseDocuments(main));
            break;
        case PDFDocumentToolInputKind::None:
            m_columns->getMainLayout()->addWidget(createOpenTool(main));
            break;
    }
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
        // K9 (531:3389): a tool with more inputs speaks of the open documents; the row Locked
        // document is the text of the GUI of the tool, when it has one. A new document is "One
        // new document" (K9, also of a tool without an input: Blank PDF 1392:4710)
        const bool isMultiple = tool->getInputKind() == PDFDocumentToolInputKind::Multiple;
        const bool isNewDocument = tool->getResultKind() == PDFDocumentToolResultKind::NewDocument;
        const QString lockedText = getLockedGlanceText();
        m_facts->addRow(tr("Input"), Helper::getInputText(tool->getInputKind()));
        m_facts->addRow(tr("Result"), isNewDocument ? tr("One new document") : tool->getResultSummary());
        // A tool without an input speaks of all the open documents too (1392:4712)
        const bool isPlural = isMultiple || tool->getInputKind() == PDFDocumentToolInputKind::None;
        m_facts->addRow(isPlural ? tr("Open documents") : tr("Open document"), Helper::getOpenDocumentText(tool->getInputKind(), tool->getResultKind()));
        m_facts->addRow(tr("Locked document"), lockedText.isEmpty() ? Helper::getLockedText(tool->getLockedAccess()) : lockedText);
        m_facts->addRow(tr("Signatures"), Helper::getSignatureText(tool->getSignatureImpact(), tool->getInputKind(), tool->getResultKind()));
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

QString PDFToolStartPage::getLockedGlanceText() const
{
    // The GUI of the tool may have its own text (Merge "Can be merged"); it is made only
    // for the text and deleted at once (no parent, no document)
    const std::shared_ptr<const PDFDocumentToolGUIFactory> factory = PDFDocumentToolGUIRegistry::getFactory(m_toolId);
    if (!factory)
    {
        return QString();
    }

    const std::unique_ptr<PDFDocumentToolGUI> gui(factory->createGUI());
    return gui ? gui->getLockedGlanceText() : QString();
}

QWidget* PDFToolStartPage::createChooseDocuments(QWidget* parent)
{
    using Helper = PDFToolStartPageHelper;

    // choose-document 531:3261: the header, the card of the inputs, the note; gap 12
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("toolChooseDocument"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, Helper::SECTION_GAP);
    layout->addWidget(Helper::createHeader(tr("Choose documents"), tr("%1 combines two or more PDFs and images · check open documents, add files, then open the tool").arg(title), section));

    m_inputChooser = new PDFAppToolInputChooser(getContext(), m_toolId, m_inputList, section);
    connect(m_inputChooser.data(), &PDFAppToolInputChooser::openToolRequested, this, &PDFToolStartPage::onOpenToolRequested);
    layout->addWidget(m_inputChooser);

    // note 531:3288: the icon info 14, gap 8, Regular 12 text/secondary
    QWidget* note = new QWidget(section);
    note->setObjectName(QStringLiteral("toolStartNote"));
    QHBoxLayout* noteLayout = new QHBoxLayout(note);
    noteLayout->setContentsMargins(0, 0, 0, 0);
    noteLayout->setSpacing(8);
    PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("info"), 14, 14, note);
    icon->setColors(std::nullopt, PDFThemeColors::IconDefault);
    noteLayout->addWidget(icon, 0, Qt::AlignTop);
    const QString noteText = tr("%1 opens in its own tab with the documents in this order. The open documents are not changed; an unsaved document is used in its state at the moment it is added.").arg(title);
    PDFAppParagraph* noteParagraph = PDFAppPageWidgets::createText(noteText, PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, note);
    noteParagraph->setObjectName(QStringLiteral("toolStartNoteText"));
    noteParagraph->setMinimumWidth(1);
    noteLayout->addWidget(noteParagraph, 1);
    layout->addWidget(note);
    return section;
}

QWidget* PDFToolStartPage::createOpenTool(QWidget* parent)
{
    using Helper = PDFToolStartPageHelper;

    // Q10 / R21, choose-document of pdf4qt-tool-start-blank 1392:4584: a tool without an input
    // has no document to choose - the Primary Open … in the place of the chooser opens its own
    // tab. The texts are of the tool (its GUI), else generic ones.
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();
    const QString sectionTitle = getStartPageText(PDFAppToolStartPageText::Title);
    const QString hint = getStartPageText(PDFAppToolStartPageText::Hint);
    const QString cardText = getStartPageText(PDFAppToolStartPageText::Card);
    const QString noteText = getStartPageText(PDFAppToolStartPageText::Note);
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("toolChooseDocument"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, Helper::SECTION_GAP);
    layout->addWidget(Helper::createHeader(sectionTitle.isEmpty() ? tr("Start") : sectionTitle, hint.isEmpty() ? tr("%1 needs no document · it creates a new one").arg(title) : hint, section));

    // document-list 1392:4588: the card with the padding 8, the text Regular 12 text/secondary
    // and the Primary button 12 apart (cta 1392:4636)
    PDFAppCard* card = PDFAppPageWidgets::createCard(8, 0, section);
    card->setObjectName(QStringLiteral("toolOpenToolCard"));
    QHBoxLayout* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);
    PDFAppParagraph* text = PDFAppPageWidgets::createText(cardText.isEmpty() ? tr("The tool opens in its own tab. Nothing is created until you press Create PDF.") : cardText,
                                                          PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, card);
    text->setObjectName(QStringLiteral("toolOpenToolText"));
    text->setMinimumWidth(1);
    row->addWidget(text, 1, Qt::AlignVCenter);
    m_openToolButton = PDFAppPageWidgets::createButton(tr("Open %1").arg(title), tool ? tool->getIconName() : QString(), true, card);
    m_openToolButton->setObjectName(QStringLiteral("toolOpenTool"));
    connect(m_openToolButton.data(), &QPushButton::clicked, this, &PDFToolStartPage::onOpenToolRequested);
    row->addWidget(m_openToolButton, 0, Qt::AlignVCenter);
    card->getLayout()->addLayout(row);
    layout->addWidget(card);

    // note 1392:4639: the icon info 14, gap 8, Regular 12 text/secondary
    if (!noteText.isEmpty())
    {
        QWidget* note = new QWidget(section);
        note->setObjectName(QStringLiteral("toolStartNote"));
        QHBoxLayout* noteLayout = new QHBoxLayout(note);
        noteLayout->setContentsMargins(0, 0, 0, 0);
        noteLayout->setSpacing(8);
        PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("info"), 14, 14, note);
        icon->setColors(std::nullopt, PDFThemeColors::IconDefault);
        noteLayout->addWidget(icon, 0, Qt::AlignTop);
        PDFAppParagraph* noteParagraph = PDFAppPageWidgets::createText(noteText, PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, note);
        noteParagraph->setObjectName(QStringLiteral("toolStartNoteText"));
        noteParagraph->setMinimumWidth(1);
        noteLayout->addWidget(noteParagraph, 1);
        layout->addWidget(note);
    }
    return section;
}

QString PDFToolStartPage::getStartPageText(PDFAppToolStartPageText text) const
{
    // As getLockedGlanceText: the GUI of the tool is made only for its text
    const std::shared_ptr<const PDFDocumentToolGUIFactory> factory = PDFDocumentToolGUIRegistry::getFactory(m_toolId);
    if (!factory)
    {
        return QString();
    }

    const std::unique_ptr<PDFDocumentToolGUI> gui(factory->createGUI());
    return gui ? gui->getStartPageText(text) : QString();
}

void PDFToolStartPage::onOpenToolRequested()
{
    PDFAppMainWindow* window = qobject_cast<PDFAppMainWindow*>(this->window());
    if (!window)
    {
        return;
    }

    // R13: the list goes to the tab in this one synchronous call (setParent); the navigation
    // may run code of the application, the page may go meanwhile
    QPointer<PDFToolStartPage> guard(this);
    QPointer<PDFAppToolInputList> list(m_inputList);
    window->createToolTab(m_toolId, list.data());
    if (!guard || !m_inputList)
    {
        return;
    }

    // The tool took the list: the page starts with a new empty one
    if (m_inputList->parent() != this)
    {
        m_inputList = new PDFAppToolInputList(this);
        if (m_inputChooser)
        {
            m_inputChooser->setInputList(m_inputList);
        }
    }
}

void PDFToolStartPage::setDropActive(bool isActive)
{
    if (m_inputChooser && m_inputChooser->getDropZone())
    {
        m_inputChooser->getDropZone()->setDropActive(isActive);
    }
}

void PDFToolStartPage::dragEnterEvent(QDragEnterEvent* event)
{
    // Only a page with more inputs takes the drops itself (K8); otherwise the window does
    if (m_inputChooser && PDFAppFileDrop::hasLocalFiles(event->mimeData()))
    {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        setDropActive(true);
        return;
    }
    BaseClass::dragEnterEvent(event);
}

void PDFToolStartPage::dragMoveEvent(QDragMoveEvent* event)
{
    if (m_inputChooser && PDFAppFileDrop::hasLocalFiles(event->mimeData()))
    {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    BaseClass::dragMoveEvent(event);
}

void PDFToolStartPage::dragLeaveEvent(QDragLeaveEvent* event)
{
    setDropActive(false);
    BaseClass::dragLeaveEvent(event);
}

void PDFToolStartPage::dropEvent(QDropEvent* event)
{
    setDropActive(false);
    if (m_inputChooser && PDFAppFileDrop::hasLocalFiles(event->mimeData()))
    {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        m_inputChooser->dropFiles(event->mimeData());
        return;
    }
    BaseClass::dropEvent(event);
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
