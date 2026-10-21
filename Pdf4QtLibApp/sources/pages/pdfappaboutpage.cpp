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

#include "pdfappaboutpage.h"
#include "pdfappcontext.h"
#include "pdfappworkspace.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the page About
class PDFAppAboutPageHelper
{
public:
    PDFAppAboutPageHelper() = delete;

    /// Line height of the paragraphs 13 px (line-height 1.5 of the design)
    static constexpr int BODY_LINE_HEIGHT = 20;

    /// Line height of the text of the license card (12 px, 1.4)
    static constexpr int SMALL_LINE_HEIGHT = 17;

    /// Height of the card of the libraries (libraries-list 111:2290)
    static constexpr int LIBRARIES_CARD_HEIGHT = 303;

    static PDFAppParagraph* createParagraph(const QString& text, int pixelSize, int lineHeight, PDFThemeColors::Color color, QWidget* parent)
    {
        PDFAppParagraph* paragraph = new PDFAppParagraph(PDFThemeFonts::getFont(QFont::Normal, pixelSize), lineHeight, color, parent);
        paragraph->setText(text);
        return paragraph;
    }

    /// Vertical layout without margins with the spacing
    static QVBoxLayout* createColumn(QWidget* widget, int spacing)
    {
        QVBoxLayout* layout = new QVBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(spacing);
        return layout;
    }
};

PDFAppLibraryModel::PDFAppLibraryModel(std::vector<PDFAppThirdPartyLibrary> libraries, QObject* parent) :
    BaseClass(parent),
    m_libraries(std::move(libraries))
{

}

PDFAppLibraryModel::~PDFAppLibraryModel() = default;

int PDFAppLibraryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_libraries.size());
}

int PDFAppLibraryModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant PDFAppLibraryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_libraries.size()) || index.column() < 0 || index.column() >= ColumnCount)
    {
        return QVariant();
    }

    const PDFAppThirdPartyLibrary& library = m_libraries[size_t(index.row())];
    switch (role)
    {
        case Qt::DisplayRole:
        case Qt::ToolTipRole:
        {
            switch (index.column())
            {
                case LibraryColumn:
                    return role == Qt::ToolTipRole ? library.homepage.toString() : library.name;
                case VersionColumn:
                    // No version in the headers of the library: a dash
                    return library.version.isEmpty() ? QStringLiteral("—") : library.version;
                case PurposeColumn:
                    return library.purpose;
                case LicenseColumn:
                    return role == Qt::ToolTipRole && !library.licenseDetails.isEmpty() ? library.licenseDetails : library.license;
                default:
                    break;
            }
            break;
        }

        case Qt::AccessibleTextRole:
        {
            if (index.column() == VersionColumn && library.version.isEmpty())
            {
                return tr("Unknown version");
            }
            return data(index, Qt::DisplayRole);
        }

        case Qt::FontRole:
            return index.column() == LibraryColumn ? QVariant(PDFThemeFonts::getFont(PDFThemeFonts::Role::SmallSemiBold)) : QVariant();

        case Qt::ForegroundRole:
        {
            // The tokens of the active theme at the time of the painting
            switch (index.column())
            {
                case LibraryColumn:
                    return PDFStylePainter::color(PDFThemeColors::TextPrimary);
                case PurposeColumn:
                    return PDFStylePainter::color(PDFThemeColors::TextTertiary);
                default:
                    return PDFStylePainter::color(PDFThemeColors::TextSecondary);
            }
        }

        case Qt::TextAlignmentRole:
            return index.column() == LicenseColumn ? QVariant(int(Qt::AlignTrailing | Qt::AlignVCenter)) : QVariant(int(Qt::AlignLeading | Qt::AlignVCenter));

        default:
            break;
    }

    return QVariant();
}

QVariant PDFAppLibraryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || section < 0 || section >= ColumnCount)
    {
        return QVariant();
    }

    if (role == Qt::DisplayRole)
    {
        switch (section)
        {
            case LibraryColumn:
                return tr("Library");
            case VersionColumn:
                return tr("Version");
            case PurposeColumn:
                return tr("Purpose");
            case LicenseColumn:
                return tr("License");
            default:
                break;
        }
    }

    if (role == Qt::TextAlignmentRole)
    {
        return section == LicenseColumn ? int(Qt::AlignTrailing | Qt::AlignVCenter) : int(Qt::AlignLeading | Qt::AlignVCenter);
    }

    return QVariant();
}

PDFAppLibraryView::PDFAppLibraryView(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("libraryView"));
    setRootIsDecorated(false);
    setItemsExpandable(false);
    setUniformRowHeights(true);
    setSelectionMode(QAbstractItemView::NoSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setRowStyle(RowStyle::Library);
    header()->setSectionsMovable(false);
    header()->setSectionsClickable(false);
    header()->setStretchLastSection(false);
}

PDFAppLibraryView::~PDFAppLibraryView() = default;

void PDFAppLibraryView::updateColumns()
{
    QHeaderView* headerView = header();
    if (!model() || headerView->count() < PDFAppLibraryModel::ColumnCount)
    {
        return;
    }

    headerView->setSectionResizeMode(PDFAppLibraryModel::LibraryColumn, QHeaderView::Fixed);
    headerView->setSectionResizeMode(PDFAppLibraryModel::VersionColumn, QHeaderView::Fixed);
    headerView->setSectionResizeMode(PDFAppLibraryModel::PurposeColumn, QHeaderView::Stretch);
    headerView->setSectionResizeMode(PDFAppLibraryModel::LicenseColumn, QHeaderView::Fixed);
    headerView->resizeSection(PDFAppLibraryModel::LibraryColumn, LIBRARY_WIDTH);
    headerView->resizeSection(PDFAppLibraryModel::VersionColumn, VERSION_WIDTH);
    headerView->resizeSection(PDFAppLibraryModel::LicenseColumn, LICENSE_WIDTH);

    // The minimal window has no place for the purpose (the other texts stay whole)
    const bool isPurposeVisible = viewport()->width() >= LIBRARY_WIDTH + VERSION_WIDTH + LICENSE_WIDTH + MINIMUM_PURPOSE_WIDTH;
    if (isColumnHidden(PDFAppLibraryModel::PurposeColumn) == isPurposeVisible)
    {
        setColumnHidden(PDFAppLibraryModel::PurposeColumn, !isPurposeVisible);
    }
}

void PDFAppLibraryView::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateColumns();
}

PDFAppAboutPage::PDFAppAboutPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent)
{
    setObjectName(QStringLiteral("aboutPage"));

    QVBoxLayout* layout = PDFAppAboutPageHelper::createColumn(this, 0);
    m_columns = PDFAppColumns::createMainAndSide(this);
    layout->addWidget(m_columns);
    layout->addStretch(1);

    QBoxLayout* mainLayout = m_columns->getMainLayout();
    QBoxLayout* sideLayout = m_columns->getSideLayout();
    QWidget* main = m_columns->getMainColumn();
    QWidget* side = m_columns->getSideColumn();

    mainLayout->addWidget(createAppCard(main));
    mainLayout->addWidget(createAuthorSection(main));
    mainLayout->addWidget(createLibrariesSection(main));
    mainLayout->addStretch(1);

    sideLayout->addWidget(createBuildSection(side));
    sideLayout->addWidget(createLicenseSection(side));
    sideLayout->addWidget(createLinksSection(side));
    sideLayout->addStretch(1);

    updateBuildInfo();
    if (context)
    {
        connect(context, &PDFAppContext::storageStateChanged, this, &PDFAppAboutPage::onStorageStateChanged);
    }
}

PDFAppAboutPage::~PDFAppAboutPage() = default;

bool PDFAppAboutPage::setRoute(const PDFAppRoute& route)
{
    // The only anchor of the page is the table of the third-party libraries
    const QString fragment = route.getFragment();
    if (!fragment.isEmpty() && fragment != QLatin1String(THIRD_PARTY_ANCHOR))
    {
        return false;
    }

    setAnchorWidget(fragment.isEmpty() ? nullptr : m_librariesSection.data());
    return BaseClass::setRoute(route);
}

QString PDFAppAboutPage::getTitle() const
{
    return tr("About");
}

QString PDFAppAboutPage::getHeaderTitle() const
{
    return tr("About");
}

QString PDFAppAboutPage::getHeaderSubtitle() const
{
    return tr("PDF4QT, its author, the build you are running and the open-source libraries it is made of.");
}

QUrl PDFAppAboutPage::getThirdPartyNoticesUrl()
{
    return QUrl(QStringLiteral("https://github.com/JakubMelka/PDF4QT/tree/master/3rdparty_licenses"), QUrl::StrictMode);
}

QList<PDFLinkRow*> PDFAppAboutPage::getLinkRows() const
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

QWidget* PDFAppAboutPage::createAppCard(QWidget* parent)
{
    using Helper = PDFAppAboutPageHelper;

    // app-card 111:2215: accent/blue, padding 20, gap 20, radius 12
    m_appCard = new PDFTintedCard(PDFAccentHue::Blue, parent);
    m_appCard->setObjectName(QStringLiteral("appCard"));
    QHBoxLayout* layout = new QHBoxLayout(m_appCard);
    layout->setContentsMargins(21, 21, 21, 21);
    layout->setSpacing(20);

    PDFIconBadge* logo = new PDFIconBadge(QString(), 88, 64, m_appCard);
    logo->setRadius(16.0);
    logo->setColors(PDFThemeColors::BgSurface, PDFThemeColors::IconDefault, PDFAccentHueColors::getColor(PDFAccentHue::Blue, PDFAccentHueColors::Role::Border));
    logo->setLogo(true);
    logo->setObjectName(QStringLiteral("logoBox"));
    layout->addWidget(logo, 0, Qt::AlignTop);

    QWidget* content = new QWidget(m_appCard);
    QVBoxLayout* contentLayout = Helper::createColumn(content, 8);
    layout->addWidget(content, 1);

    // Title Bold 22 and the version Regular 13 text/tertiary on the baseline, gap 10
    QWidget* titleRow = new QWidget(content);
    QHBoxLayout* titleLayout = new QHBoxLayout(titleRow);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(10);
    PDFAppParagraph* title = PDFAppPageWidgets::createText(QStringLiteral("PDF4QT"), PDFThemeFonts::getFont(QFont::Bold, 22), 27, PDFThemeColors::TextPrimary, titleRow);
    title->setObjectName(QStringLiteral("appTitle"));
    QString versionText = tr("Version %1").arg(PDFAppAboutInfo::getVersion());
    if (!PDFAppAboutInfo::getBuildDate().isEmpty())
    {
        versionText = tr("%1 · Built %2").arg(versionText, PDFAppAboutInfo::getBuildDate());
    }
    PDFAppParagraph* version = PDFAppPageWidgets::createText(versionText, PDFThemeFonts::getFont(QFont::Normal, 13), 16, PDFThemeColors::TextTertiary, titleRow);
    version->setObjectName(QStringLiteral("appVersion"));

    // The baselines of the title (27) and of the version (16, 9 px lower) are aligned
    version->setContentsMargins(0, 9, 0, 0);
    titleLayout->addWidget(title, 0, Qt::AlignTop);
    titleLayout->addWidget(version, 1, Qt::AlignTop);
    contentLayout->addWidget(titleRow);

    contentLayout->addWidget(Helper::createParagraph(tr("A fast, open-source PDF application for viewing, editing, annotating, organizing, converting and securing documents. "
                                                        "Written in C++ and Qt, it runs natively on Windows, Linux and macOS."),
                                                     13, Helper::BODY_LINE_HEIGHT, PDFThemeColors::TextSecondary, content));

    // facts (top padding 2, gaps 8)
    QWidget* facts = new QWidget(content);
    facts->setObjectName(QStringLiteral("facts"));
    PDFAppFlowLayout* factsLayout = new PDFAppFlowLayout(facts, 8, 8);
    factsLayout->setContentsMargins(0, 2, 0, 0);
    factsLayout->addWidget(new PDFFactPill(QStringLiteral("github"), tr("Open source"), facts));
    factsLayout->addWidget(new PDFFactPill(QStringLiteral("shield"), tr("MIT License"), facts));
    factsLayout->addWidget(new PDFFactPill(QStringLiteral("keyboard"), tr("C++20 · Qt 6"), facts));
    factsLayout->addWidget(new PDFFactPill(QStringLiteral("monitor"), tr("Windows · Linux · macOS"), facts));
    contentLayout->addWidget(facts);

    // buttons (top padding 6, gaps 10)
    QWidget* buttons = new QWidget(content);
    PDFAppFlowLayout* buttonsLayout = new PDFAppFlowLayout(buttons, 10, 10);
    buttonsLayout->setContentsMargins(0, 6, 0, 0);
    m_websiteButton = PDFAppPageWidgets::createButton(tr("Visit website"), QStringLiteral("external-link"), true, buttons);
    m_websiteButton->setObjectName(QStringLiteral("websiteButton"));
    withLink(m_websiteButton, PDFAppProjectLinks::getWebsite().toString());
    m_gitHubButton = PDFAppPageWidgets::createButton(tr("View on GitHub"), QStringLiteral("github"), false, buttons);
    m_gitHubButton->setObjectName(QStringLiteral("gitHubButton"));
    withLink(m_gitHubButton, PDFAppProjectLinks::getRepository().toString());
    buttonsLayout->addWidget(m_websiteButton);
    buttonsLayout->addWidget(m_gitHubButton);
    contentLayout->addWidget(buttons);

    return m_appCard;
}

QWidget* PDFAppAboutPage::createAuthorSection(QWidget* parent)
{
    using Helper = PDFAppAboutPageHelper;

    m_authorSection = new QWidget(parent);
    m_authorSection->setObjectName(QStringLiteral("authorSection"));
    QVBoxLayout* layout = Helper::createColumn(m_authorSection, 12);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("About the author"), nullptr, m_authorSection));

    // card 111:2260: padding 20, gap 14
    QWidget* card = PDFAppPageWidgets::createCard(20, 14, m_authorSection);
    card->setObjectName(QStringLiteral("authorCard"));
    QBoxLayout* cardLayout = qobject_cast<QBoxLayout*>(card->layout());
    layout->addWidget(card);

    QWidget* identity = new QWidget(card);
    QHBoxLayout* identityLayout = new QHBoxLayout(identity);
    identityLayout->setContentsMargins(0, 0, 0, 0);
    identityLayout->setSpacing(16);
    identityLayout->addWidget(new PDFAvatar(QStringLiteral("JM"), 56, 18, identity));

    QWidget* identityText = new QWidget(identity);
    QVBoxLayout* identityTextLayout = Helper::createColumn(identityText, 2);
    identityTextLayout->addWidget(PDFAppPageWidgets::createText(QStringLiteral("Jakub Melka"), PDFThemeFonts::getFont(QFont::Bold, 16), 19, PDFThemeColors::TextPrimary, identityText));
    identityTextLayout->addWidget(PDFAppPageWidgets::createText(tr("Author and maintainer of PDF4QT since 2018"), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextTertiary, identityText));
    identityLayout->addWidget(identityText, 1, Qt::AlignVCenter);
    cardLayout->addWidget(identity);

    cardLayout->addWidget(Helper::createParagraph(tr("PDF4QT is a one-person open-source project: the PDF engine, the desktop application and the command-line tool are all "
                                                     "written and maintained by Jakub. Bug reports, ideas and contributions on GitHub are always welcome."),
                                                  13, Helper::BODY_LINE_HEIGHT, PDFThemeColors::TextSecondary, card));

    QWidget* buttons = new QWidget(card);
    PDFAppFlowLayout* buttonsLayout = new PDFAppFlowLayout(buttons, 10, 10);
    m_profileButton = PDFAppPageWidgets::createButton(tr("GitHub profile"), QStringLiteral("github"), false, buttons);
    m_profileButton->setObjectName(QStringLiteral("profileButton"));
    withLink(m_profileButton, PDFAppProjectLinks::getAuthorProfile().toString());
    m_sponsorButton = PDFAppPageWidgets::createButton(tr("Sponsor the project"), QStringLiteral("heart"), false, buttons);
    m_sponsorButton->setObjectName(QStringLiteral("sponsorButton"));
    withLink(m_sponsorButton, PDFAppRoutes::sponsor().toString());
    buttonsLayout->addWidget(m_profileButton);
    buttonsLayout->addWidget(m_sponsorButton);
    cardLayout->addWidget(buttons);

    return m_authorSection;
}

QWidget* PDFAppAboutPage::createLibrariesSection(QWidget* parent)
{
    m_librariesSection = new QWidget(parent);
    m_librariesSection->setObjectName(QStringLiteral("librariesSection"));
    QVBoxLayout* layout = PDFAppAboutPageHelper::createColumn(m_librariesSection, 12);

    m_noticesLink = new PDFTextLink(tr("Third-party notices"));
    m_noticesLink->setObjectName(QStringLiteral("noticesLink"));
    withLink(m_noticesLink, getThirdPartyNoticesUrl().toString());
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Third-party libraries"), m_noticesLink, m_librariesSection));

    // libraries-list 111:2290: card 303 high, padding 8 (the table scrolls in it)
    QWidget* card = PDFAppPageWidgets::createCard(8, 0, m_librariesSection);
    card->setObjectName(QStringLiteral("librariesCard"));
    card->setFixedHeight(PDFAppAboutPageHelper::LIBRARIES_CARD_HEIGHT);
    layout->addWidget(card);

    m_libraryView = new PDFAppLibraryView(card);
    m_libraryView->setAccessibleName(tr("Third-party libraries"));
    m_libraryView->setModel(new PDFAppLibraryModel(PDFAppAboutInfo::getLibraries(), m_libraryView));
    m_libraryView->updateColumns();
    card->layout()->addWidget(m_libraryView);

    return m_librariesSection;
}

QWidget* PDFAppAboutPage::createBuildSection(QWidget* parent)
{
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("buildSection"));
    QVBoxLayout* layout = PDFAppAboutPageHelper::createColumn(section, 12);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Build information"), nullptr, section));

    // card 111:2368: padding 8; the button below the rows (paddings 8 / 2 / 2)
    QWidget* card = PDFAppPageWidgets::createCard(8, 0, section);
    card->setObjectName(QStringLiteral("buildCard"));
    layout->addWidget(card);

    m_buildInfo = new PDFKeyValueList(card);
    m_buildInfo->setObjectName(QStringLiteral("buildInfo"));
    m_buildInfo->setAccessibleName(tr("Build information"));
    card->layout()->addWidget(m_buildInfo);

    QWidget* buttonWrap = new QWidget(card);
    QHBoxLayout* buttonLayout = new QHBoxLayout(buttonWrap);
    buttonLayout->setContentsMargins(2, 8, 2, 2);
    m_copyButton = PDFAppPageWidgets::createButton(tr("Copy build info"), QStringLiteral("copy"), false, buttonWrap);
    m_copyButton->setObjectName(QStringLiteral("copyButton"));
    connect(m_copyButton, &QPushButton::clicked, this, &PDFAppAboutPage::onCopyClicked);
    buttonLayout->addWidget(m_copyButton, 1);
    card->layout()->addWidget(buttonWrap);

    return section;
}

QWidget* PDFAppAboutPage::createLicenseSection(QWidget* parent)
{
    using Helper = PDFAppAboutPageHelper;

    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("licenseSection"));
    QVBoxLayout* layout = Helper::createColumn(section, 12);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("License"), nullptr, section));

    // card 111:2405: padding 14, gap 8
    QWidget* card = PDFAppPageWidgets::createCard(14, 8, section);
    card->setObjectName(QStringLiteral("licenseCard"));
    QBoxLayout* cardLayout = qobject_cast<QBoxLayout*>(card->layout());
    layout->addWidget(card);

    QWidget* titleRow = new QWidget(card);
    QHBoxLayout* titleLayout = new QHBoxLayout(titleRow);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(10);
    PDFIconBadge* licenseBadge = new PDFIconBadge(QStringLiteral("shield"), 32, 16, titleRow);
    licenseBadge->setHue(PDFAccentHue::Green);
    titleLayout->addWidget(licenseBadge);
    // title-row 111:2406: the title is centered by the icon circle (y 7.5)
    QWidget* licenseTitle = PDFAppPageWidgets::createText(tr("MIT License"), PDFThemeFonts::getFont(QFont::DemiBold, 14), 17, PDFThemeColors::TextPrimary, titleRow);
    licenseTitle->setObjectName(QStringLiteral("licenseTitle"));
    titleLayout->addWidget(licenseTitle, 1, Qt::AlignVCenter);
    cardLayout->addWidget(titleRow);

    cardLayout->addWidget(Helper::createParagraph(tr("Free to use, modify and distribute, also commercially. The third-party libraries listed on this page keep their own licenses."),
                                                  12, Helper::SMALL_LINE_HEIGHT, PDFThemeColors::TextTertiary, card));

    m_licenseLink = new PDFTextLink(tr("Read the full license"), card);
    m_licenseLink->setObjectName(QStringLiteral("licenseLink"));
    withLink(m_licenseLink, PDFAppProjectLinks::getLicense().toString());
    cardLayout->addWidget(m_licenseLink, 0, Qt::AlignLeading);

    return section;
}

QWidget* PDFAppAboutPage::createLinksSection(QWidget* parent)
{
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("linksSection"));
    QVBoxLayout* layout = PDFAppAboutPageHelper::createColumn(section, 12);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Links"), nullptr, section));

    // links-list 111:2417: padding 8, gap 2
    QWidget* card = PDFAppPageWidgets::createCard(8, 2, section);
    card->setObjectName(QStringLiteral("linksCard"));
    layout->addWidget(card);

    withLink(addLinkRow(card, tr("Official website"), QStringLiteral("jakubmelka.github.io"), QStringLiteral("external-link")), PDFAppProjectLinks::getWebsite().toString());
    withLink(addLinkRow(card, tr("Documentation"), tr("User manual and guides"), QStringLiteral("book-open")), PDFAppRoutes::help(PDFAppRoutes::getDefaultHelpTopic()).toString());
    withLink(addLinkRow(card, tr("GitHub repository"), tr("Source code and releases"), QStringLiteral("github")), PDFAppProjectLinks::getRepository().toString());
    withLink(addLinkRow(card, tr("Discussions"), tr("Ask questions, share ideas"), QStringLiteral("message-square")), PDFAppProjectLinks::getDiscussions().toString());
    withLink(addLinkRow(card, tr("Report an issue"), tr("Tell us about a problem"), QStringLiteral("alert-circle")), PDFAppProjectLinks::getNewIssue().toString());
    return section;
}

PDFLinkRow* PDFAppAboutPage::addLinkRow(QWidget* card, const QString& title, const QString& subtitle, const QString& iconName)
{
    PDFLinkRow* row = new PDFLinkRow(title, subtitle, iconName, card);
    card->layout()->addWidget(row);
    m_linkRows.append(row);
    return row;
}

void PDFAppAboutPage::updateBuildInfo()
{
    m_buildRows = PDFAppAboutInfo::getBuildRows(getContext());
    if (!m_buildInfo)
    {
        return;
    }

    m_buildInfo->clear();
    for (const PDFAppBuildInfoRow& row : std::as_const(m_buildRows))
    {
        m_buildInfo->addRow(row.key, row.value);
    }
}

void PDFAppAboutPage::onStorageStateChanged()
{
    updateBuildInfo();
}

void PDFAppAboutPage::onCopyClicked()
{
    if (QClipboard* clipboard = QGuiApplication::clipboard())
    {
        clipboard->setText(PDFAppAboutInfo::toText(m_buildRows));
    }
}

PDFAppPage* PDFAppAboutPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppAboutPage(context, parent);
}

}   // namespace pdf
